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

#ifndef KFD_DEBUG_EVENTS_H_INCLUDED
#define KFD_DEBUG_EVENTS_H_INCLUDED

#include "kfd_priv.h"

void kfd_dbg_trap_deactivate(struct kfd_process *target, bool unwind, int unwind_count);
int kfd_dbg_trap_activate(struct kfd_process *target);
int kfd_dbg_ev_query_debug_event(struct kfd_process *process,
			unsigned int *queue_id,
			unsigned int *gpu_id,
			uint64_t exception_clear_mask,
			uint64_t *event_status);
bool kfd_set_dbg_ev_from_interrupt(struct kfd_node *dev,
				   unsigned int pasid,
				   uint32_t doorbell_id,
				   uint64_t trap_mask,
				   void *exception_data,
				   size_t exception_data_size);
bool kfd_dbg_ev_raise(uint64_t event_mask,
			struct kfd_process *process, struct kfd_node *dev,
			unsigned int source_id, bool use_worker,
			void *exception_data,
			size_t exception_data_size);
int kfd_dbg_trap_disable(struct kfd_process *target);
int kfd_dbg_trap_enable(struct kfd_process *target, uint32_t fd,
			void __user *runtime_info,
			uint32_t *runtime_info_size);
int kfd_dbg_trap_set_wave_launch_override(struct kfd_process *target,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t *trap_mask_supported);
int kfd_dbg_trap_set_wave_launch_mode(struct kfd_process *target,
					uint8_t wave_launch_mode);
int kfd_dbg_trap_clear_dev_address_watch(struct kfd_process_device *pdd,
					uint32_t watch_id);
int kfd_dbg_trap_set_dev_address_watch(struct kfd_process_device *pdd,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_id,
					uint32_t watch_mode);
int kfd_dbg_trap_set_flags(struct kfd_process *target, uint32_t *flags);
int kfd_dbg_trap_query_exception_info(struct kfd_process *target,
		uint32_t source_id,
		uint32_t exception_code,
		bool clear_exception,
		void __user *info,
		uint32_t *info_size);
int kfd_dbg_send_exception_to_runtime(struct kfd_process *p,
					unsigned int dev_id,
					unsigned int queue_id,
					uint64_t error_reason);

static inline bool kfd_dbg_is_per_vmid_supported(struct kfd_node *dev)
{
	return KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 2) ||
	       KFD_GC_VERSION(dev) >= IP_VERSION(11, 0, 0);
}

void debug_event_write_work_handler(struct work_struct *work);
int kfd_dbg_trap_device_snapshot(struct kfd_process *target,
		uint64_t exception_clear_mask,
		void __user *user_info,
		uint32_t *number_of_device_infos,
		uint32_t *entry_size);

void kfd_dbg_set_enabled_debug_exception_mask(struct kfd_process *target,
					uint64_t exception_set_mask);
/*
 * If GFX off is enabled, chips that do not support RLC restore for the debug
 * registers will disable GFX off temporarily for the entire debug session.
 * See disable_on_trap_action_entry and enable_on_trap_action_exit for details.
 */
static inline bool kfd_dbg_is_rlc_restore_supported(struct kfd_node *dev)
{
	return !(KFD_GC_VERSION(dev) == IP_VERSION(10, 1, 10) ||
		 KFD_GC_VERSION(dev) == IP_VERSION(10, 1, 1));
}

static inline bool kfd_dbg_has_gws_support(struct kfd_node *dev)
{
	if ((KFD_GC_VERSION(dev) == IP_VERSION(9, 0, 1)
			&& dev->kfd->mec2_fw_version < 0x81b6) ||
		(KFD_GC_VERSION(dev) >= IP_VERSION(9, 1, 0)
			&& KFD_GC_VERSION(dev) <= IP_VERSION(9, 2, 2)
			&& dev->kfd->mec2_fw_version < 0x1b6) ||
		(KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 0)
			&& dev->kfd->mec2_fw_version < 0x1b6) ||
		(KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 1)
			&& dev->kfd->mec2_fw_version < 0x30) ||
		(KFD_GC_VERSION(dev) >= IP_VERSION(11, 0, 0) &&
			KFD_GC_VERSION(dev) < IP_VERSION(12, 0, 0)))
		return false;

	/* Assume debugging and cooperative launch supported otherwise. */
	return true;
}

int kfd_dbg_set_mes_debug_mode(struct kfd_process_device *pdd);
#endif
