/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "kfd_debug.h"
#include <linux/file.h>

int kfd_dbg_trap_disable(struct kfd_process *target)
{
	if (!target->debug_trap_enabled)
		return 0;

	fput(target->dbg_ev_file);
	target->dbg_ev_file = NULL;

	if (target->debugger_process) {
		atomic_dec(&target->debugger_process->debugged_process_count);
		target->debugger_process = NULL;
	}

	target->debug_trap_enabled = false;
	kfd_unref_process(target);

	return 0;
}

int kfd_dbg_trap_enable(struct kfd_process *target, uint32_t fd,
			void __user *runtime_info, uint32_t *runtime_size)
{
	struct file *f;
	uint32_t copy_size;
	int r = 0;

	if (target->debug_trap_enabled)
		return -EALREADY;

	copy_size = min((size_t)(*runtime_size), sizeof(target->runtime_info));

	f = fget(fd);
	if (!f) {
		pr_err("Failed to get file for (%i)\n", fd);
		return -EBADF;
	}

	target->dbg_ev_file = f;

	/* We already hold the process reference but hold another one for the
	 * debug session.
	 */
	kref_get(&target->ref);
	target->debug_trap_enabled = true;

	if (target->debugger_process)
		atomic_inc(&target->debugger_process->debugged_process_count);

	if (copy_to_user(runtime_info, (void *)&target->runtime_info, copy_size))
		r = -EFAULT;

	*runtime_size = sizeof(target->runtime_info);

	return r;
}
