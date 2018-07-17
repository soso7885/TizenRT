/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * kernel/task/task_exit.c
 *
 *   Copyright (C) 2008-2009, 2012-2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <sched.h>
#include <tinyara/mm/mm.h>

#include  "sched/sched.h"

#ifndef CONFIG_DISABLE_SIGNALS
#include "signal/signal.h"
#endif
#include "task/task.h"
#include <tinyara/ttrace.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Global Variables
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: task_exit
 *
 * Description:
 *   This is a part of the logic used to implement _exit().  The full
 *   implementation of _exit() is architecture-dependent. The _exit()
 *   function also implements the bottom half of exit() and pthread_exit().
 *
 *   This function causes the currently running task (i.e., the task at the
 *   head of the ready-to-run list) to cease to exist.  This function should
 *   never be called from normal user code, but only from the architecture-
 *   specific implementation of exit.
 *
 *   Threads/tasks could also be terminated via pthread_cancel, task_delete(),
 *   and task_restart().  In the last two cases, the task will be terminated
 *   as though exit() were called.
 *
 * Inputs:
 *   None
 *
 * Return Value:
 *   OK on success; or ERROR on failure
 *
 * Assumeptions:
 *   Interrupts are disabled.
 *
 ****************************************************************************/

int task_exit(void)
{
	FAR struct tcb_s *dtcb = this_task();
	FAR struct tcb_s *rtcb;
	int ret;
	struct mm_heap_s *heap = mm_get_heap_info();

	trace_begin(TTRACE_TAG_TASK, "task_exit");

	/* Since context switching should not occur during task_exit,
	 * in order to release the heap used by dtcb, it is necessary
	 * to check whether heap->mm_semaphore is acquired before executing exit.
	 * Otherwise, the critical section of mm operations might not be protected
	 * because the exiting task's pid is inevitably changed to one of the head
	 * of readytorun list. Unfortunatetly, if the holder of heap semaphore is the
	 * head of readytorun, then the thread of task_exit function re-enters and
	 * breaks the critical section.
	 */
	dtcb->lockcount++;
	mm_takesemaphore(heap);
	mm_givesemaphore(heap);

	/* Remove the TCB of the current task from the ready-to-run list.  A context
	 * switch will definitely be necessary -- that must be done by the
	 * architecture-specific logic.
	 *
	 * sched_removereadytorun will mark the task at the head of the ready-to-run
	 * with state == TSTATE_TASK_RUNNING
	 */

	(void)sched_removereadytorun(dtcb);
	rtcb = this_task();

	/* We are now in a bad state -- the head of the ready to run task list
	 * does not correspond to the thread that is running.  Disabling pre-
	 * emption on this TCB and marking the new ready-to-run task as not
	 * running (see, for example, get_errno_ptr()).
	 *
	 * We disable pre-emption here by directly incrementing the lockcount
	 * (vs. calling sched_lock()).
	 */

	rtcb->lockcount++;
	rtcb->task_state = TSTATE_TASK_READYTORUN;

	/* Move the TCB to the specified blocked task list and delete it.  Calling
	 * task_terminate with non-blocking true will suppress atexit() and on-exit()
	 * calls and will cause buffered I/O to fail to be flushed.  The former
	 * is required _exit() behavior; the latter is optional _exit() behavior.
	 */

	sched_addblocked(dtcb, TSTATE_TASK_INACTIVE);
	ret = task_terminate(dtcb->pid, true);
	rtcb->task_state = TSTATE_TASK_RUNNING;

	/* If there are any pending tasks, then add them to the ready-to-run
	 * task list now
	 */

	if (g_pendingtasks.head) {
		(void)sched_mergepending();
	}

	/* We can't use sched_unlock() to decrement the lock count because the
	 * sched_mergepending() call above might have changed the task at the
	 * head of the ready-to-run list.  Furthermore, we should not need to
	 * perform the unlock action anyway because we know that the pending
	 * task list is empty.  So all we really need to do is to decrement
	 * the lockcount on rctb.
	 */

	rtcb->lockcount--;
	trace_end(TTRACE_TAG_TASK);
	return ret;
}
