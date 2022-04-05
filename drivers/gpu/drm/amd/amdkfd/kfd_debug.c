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
#include "kfd_device_queue_manager.h"
#include <linux/file.h>

static int kfd_dbg_set_mes_debug_mode(struct kfd_process_device *pdd)
{
	uint32_t spi_dbg_cntl = pdd->spi_dbg_override | pdd->spi_dbg_launch_mode;
	uint32_t flags = pdd->process->dbg_flags;

	if (!kfd_dbg_is_per_vmid_supported(pdd->dev))
		return 0;

	return amdgpu_mes_set_shader_debugger(pdd->dev->adev, pdd->proc_ctx_gpu_addr, spi_dbg_cntl,
						pdd->watch_points, flags);
}

/* kfd_dbg_trap_deactivate:
 *	target: target process
 *	unwind: If this is unwinding a failed kfd_dbg_trap_enable()
 *	unwind_count:
 *		If unwind == true, how far down the pdd list we need
 *				to unwind
 *		else: ignored
 */
static void kfd_dbg_trap_deactivate(struct kfd_process *target, bool unwind, int unwind_count)
{
	int i;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		/* If this is an unwind, and we have unwound the required
		 * enable calls on the pdd list, we need to stop now
		 * otherwise we may mess up another debugger session.
		 */
		if (unwind && i == unwind_count)
			break;

		/* GFX off is already disabled by debug activate if not RLC restore supported. */
		if (kfd_dbg_is_rlc_restore_supported(pdd->dev))
			amdgpu_gfx_off_ctrl(pdd->dev->adev, false);
		pdd->spi_dbg_override =
				pdd->dev->kfd2kgd->disable_debug_trap(
				pdd->dev->adev,
				target->runtime_info.ttmp_setup,
				pdd->dev->vm_info.last_vmid_kfd);
		amdgpu_gfx_off_ctrl(pdd->dev->adev, true);

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev) &&
				release_debug_trap_vmid(pdd->dev->dqm, &pdd->qpd))
			pr_err("Failed to release debug vmid on [%i]\n", pdd->dev->id);

		if (!pdd->dev->kfd->shared_resources.enable_mes)
			debug_refresh_runlist(pdd->dev->dqm);
		else
			kfd_dbg_set_mes_debug_mode(pdd);
	}
}

int kfd_dbg_trap_disable(struct kfd_process *target)
{
	if (!target->debug_trap_enabled)
		return 0;

	/*
	 * Defer deactivation to runtime if runtime not enabled otherwise reset
	 * attached running target runtime state to enable for re-attach.
	 */
	if (target->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED)
		kfd_dbg_trap_deactivate(target, false, 0);
	else if (target->runtime_info.runtime_state != DEBUG_RUNTIME_STATE_DISABLED)
		target->runtime_info.runtime_state = DEBUG_RUNTIME_STATE_ENABLED;

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

static int kfd_dbg_trap_activate(struct kfd_process *target)
{
	int i, r = 0;

	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		if (!kfd_dbg_is_per_vmid_supported(pdd->dev)) {
			r = reserve_debug_trap_vmid(pdd->dev->dqm, &pdd->qpd);

			if (r) {
				target->runtime_info.runtime_state = (r == -EBUSY) ?
							DEBUG_RUNTIME_STATE_ENABLED_BUSY :
							DEBUG_RUNTIME_STATE_ENABLED_ERROR;

				goto unwind_err;
			}
		}

		/* Disable GFX OFF to prevent garbage read/writes to debug registers.
		 * If RLC restore of debug registers is not supported and runtime enable
		 * hasn't done so already on ttmp setup request, restore the trap config registers.
		 *
		 * If RLC restore of debug registers is not supported, keep gfx off disabled for
		 * the debug session.
		 */
		amdgpu_gfx_off_ctrl(pdd->dev->adev, false);
		if (!(kfd_dbg_is_rlc_restore_supported(pdd->dev) ||
						target->runtime_info.ttmp_setup))
			pdd->dev->kfd2kgd->enable_debug_trap(pdd->dev->adev, true,
								pdd->dev->vm_info.last_vmid_kfd);

		pdd->spi_dbg_override = pdd->dev->kfd2kgd->enable_debug_trap(
					pdd->dev->adev,
					false,
					pdd->dev->vm_info.last_vmid_kfd);

		if (kfd_dbg_is_rlc_restore_supported(pdd->dev))
			amdgpu_gfx_off_ctrl(pdd->dev->adev, true);

		if (!pdd->dev->kfd->shared_resources.enable_mes)
			r = debug_refresh_runlist(pdd->dev->dqm);
		else
			r = kfd_dbg_set_mes_debug_mode(pdd);

		if (r) {
			target->runtime_info.runtime_state =
					DEBUG_RUNTIME_STATE_ENABLED_ERROR;
			goto unwind_err;
		}
	}

	return 0;

unwind_err:
	/* Enabling debug failed, we need to disable on
	 * all GPUs so the enable is all or nothing.
	 */
	kfd_dbg_trap_deactivate(target, true, i);
	return r;
}

int kfd_dbg_trap_enable(struct kfd_process *target, uint32_t fd,
			void __user *runtime_info, uint32_t *runtime_size)
{
	struct file *f;
	uint32_t copy_size;
	int i, r = 0;

	if (target->debug_trap_enabled)
		return -EALREADY;

	/* Enable pre-checks */
	for (i = 0; i < target->n_pdds; i++) {
		struct kfd_process_device *pdd = target->pdds[i];

		if (!KFD_IS_SOC15(pdd->dev))
			return -ENODEV;

		if (!kfd_dbg_has_gws_support(pdd->dev) && pdd->qpd.num_gws)
			return -EBUSY;
	}

	copy_size = min((size_t)(*runtime_size), sizeof(target->runtime_info));

	f = fget(fd);
	if (!f) {
		pr_err("Failed to get file for (%i)\n", fd);
		return -EBADF;
	}

	target->dbg_ev_file = f;

	/* defer activation to runtime if not runtime enabled */
	if (target->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED)
		kfd_dbg_trap_activate(target);

	/* We already hold the process reference but hold another one for the
	 * debug session.
	 */
	kref_get(&target->ref);
	target->debug_trap_enabled = true;

	if (target->debugger_process)
		atomic_inc(&target->debugger_process->debugged_process_count);

	if (copy_to_user(runtime_info, (void *)&target->runtime_info, copy_size)) {
		kfd_dbg_trap_deactivate(target, false, 0);
		r = -EFAULT;
	}

	*runtime_size = sizeof(target->runtime_info);

	return r;
}
