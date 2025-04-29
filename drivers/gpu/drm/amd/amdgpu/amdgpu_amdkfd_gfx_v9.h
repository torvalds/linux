/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

void kgd_gfx_v9_program_sh_mem_settings(struct amdgpu_device *adev, uint32_t vmid,
		uint32_t sh_mem_config,
		uint32_t sh_mem_ape1_base, uint32_t sh_mem_ape1_limit,
		uint32_t sh_mem_bases, uint32_t inst);
int kgd_gfx_v9_set_pasid_vmid_mapping(struct amdgpu_device *adev, u32 pasid,
		unsigned int vmid, uint32_t inst);
int kgd_gfx_v9_init_interrupts(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t inst);
int kgd_gfx_v9_hqd_load(struct amdgpu_device *adev, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm, uint32_t inst);
int kgd_gfx_v9_hiq_mqd_load(struct amdgpu_device *adev, void *mqd,
			    uint32_t pipe_id, uint32_t queue_id,
			    uint32_t doorbell_off, uint32_t inst);
int kgd_gfx_v9_hqd_dump(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs, uint32_t inst);
bool kgd_gfx_v9_hqd_is_occupied(struct amdgpu_device *adev,
			uint64_t queue_address, uint32_t pipe_id,
			uint32_t queue_id, uint32_t inst);
int kgd_gfx_v9_hqd_destroy(struct amdgpu_device *adev, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst);
int kgd_gfx_v9_wave_control_execute(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst);
bool kgd_gfx_v9_get_atc_vmid_pasid_mapping_info(struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid);
void kgd_gfx_v9_set_vm_context_page_table_base(struct amdgpu_device *adev,
			uint32_t vmid, uint64_t page_table_base);
void kgd_gfx_v9_get_cu_occupancy(struct amdgpu_device *adev,
				 struct kfd_cu_occupancy *cu_occupancy,
				 int *max_waves_per_cu, uint32_t inst);
void kgd_gfx_v9_program_trap_handler_settings(struct amdgpu_device *adev,
		uint32_t vmid, uint64_t tba_addr, uint64_t tma_addr,
		uint32_t inst);
void kgd_gfx_v9_acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst);
uint64_t kgd_gfx_v9_get_queue_mask(struct amdgpu_device *adev,
				uint32_t pipe_id, uint32_t queue_id);
void kgd_gfx_v9_release_queue(struct amdgpu_device *adev, uint32_t inst);
void kgd_gfx_v9_set_wave_launch_stall(struct amdgpu_device *adev,
					uint32_t vmid,
					bool stall);
uint32_t kgd_gfx_v9_enable_debug_trap(struct amdgpu_device *adev,
				      bool restore_dbg_registers,
				      uint32_t vmid);
uint32_t kgd_gfx_v9_disable_debug_trap(struct amdgpu_device *adev,
					bool keep_trap_enabled,
					uint32_t vmid);
int kgd_gfx_v9_validate_trap_override_request(struct amdgpu_device *adev,
					     uint32_t trap_override,
					     uint32_t *trap_mask_supported);
uint32_t kgd_gfx_v9_set_wave_launch_mode(struct amdgpu_device *adev,
					uint8_t wave_launch_mode,
					uint32_t vmid);
uint32_t kgd_gfx_v9_set_wave_launch_trap_override(struct amdgpu_device *adev,
					     uint32_t vmid,
					     uint32_t trap_override,
					     uint32_t trap_mask_bits,
					     uint32_t trap_mask_request,
					     uint32_t *trap_mask_prev,
					     uint32_t kfd_dbg_trap_cntl_prev);
uint32_t kgd_gfx_v9_set_address_watch(struct amdgpu_device *adev,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t watch_id,
					uint32_t watch_mode,
					uint32_t debug_vmid,
					uint32_t inst);
uint32_t kgd_gfx_v9_clear_address_watch(struct amdgpu_device *adev,
					uint32_t watch_id);
void kgd_gfx_v9_get_iq_wait_times(struct amdgpu_device *adev,
				uint32_t *wait_times,
				uint32_t inst);
void kgd_gfx_v9_build_dequeue_wait_counts_packet_info(struct amdgpu_device *adev,
					       uint32_t wait_times,
					       uint32_t sch_wave,
					       uint32_t que_sleep,
					       uint32_t *reg_offset,
					       uint32_t *reg_data);
uint64_t kgd_gfx_v9_hqd_get_pq_addr(struct amdgpu_device *adev,
				    uint32_t pipe_id,
				    uint32_t queue_id,
				    uint32_t inst);
uint64_t kgd_gfx_v9_hqd_reset(struct amdgpu_device *adev,
			      uint32_t pipe_id,
			      uint32_t queue_id,
			      uint32_t inst,
			      unsigned int utimeout);
uint32_t kgd_gfx_v9_hqd_sdma_get_doorbell(struct amdgpu_device *adev,
					  int engine, int queue);
