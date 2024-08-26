/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2020-2022 Advanced Micro Devices, Inc.
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

#ifndef KFD_SMI_EVENTS_H_INCLUDED
#define KFD_SMI_EVENTS_H_INCLUDED

struct amdgpu_reset_context;

int kfd_smi_event_open(struct kfd_node *dev, uint32_t *fd);
void kfd_smi_event_update_vmfault(struct kfd_node *dev, uint16_t pasid);
void kfd_smi_event_update_thermal_throttling(struct kfd_node *dev,
					     uint64_t throttle_bitmask);
void kfd_smi_event_update_gpu_reset(struct kfd_node *dev, bool post_reset,
				    struct amdgpu_reset_context *reset_context);
void kfd_smi_event_page_fault_start(struct kfd_node *node, pid_t pid,
				    unsigned long address, bool write_fault,
				    ktime_t ts);
void kfd_smi_event_page_fault_end(struct kfd_node *node, pid_t pid,
				  unsigned long address, bool migration);
void kfd_smi_event_migration_start(struct kfd_node *node, pid_t pid,
			     unsigned long start, unsigned long end,
			     uint32_t from, uint32_t to,
			     uint32_t prefetch_loc, uint32_t preferred_loc,
			     uint32_t trigger);
void kfd_smi_event_migration_end(struct kfd_node *node, pid_t pid,
			     unsigned long start, unsigned long end,
			     uint32_t from, uint32_t to, uint32_t trigger);
void kfd_smi_event_queue_eviction(struct kfd_node *node, pid_t pid,
				  uint32_t trigger);
void kfd_smi_event_queue_restore(struct kfd_node *node, pid_t pid);
void kfd_smi_event_queue_restore_rescheduled(struct mm_struct *mm);
void kfd_smi_event_unmap_from_gpu(struct kfd_node *node, pid_t pid,
				  unsigned long address, unsigned long last,
				  uint32_t trigger);
#endif
