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
 *
 */
#ifndef __AMDGPU_NBIO_H__
#define __AMDGPU_NBIO_H__

/*
 * amdgpu nbio functions
 */
struct nbio_hdp_flush_reg {
	u32 ref_and_mask_cp0;
	u32 ref_and_mask_cp1;
	u32 ref_and_mask_cp2;
	u32 ref_and_mask_cp3;
	u32 ref_and_mask_cp4;
	u32 ref_and_mask_cp5;
	u32 ref_and_mask_cp6;
	u32 ref_and_mask_cp7;
	u32 ref_and_mask_cp8;
	u32 ref_and_mask_cp9;
	u32 ref_and_mask_sdma0;
	u32 ref_and_mask_sdma1;
	u32 ref_and_mask_sdma2;
	u32 ref_and_mask_sdma3;
	u32 ref_and_mask_sdma4;
	u32 ref_and_mask_sdma5;
	u32 ref_and_mask_sdma6;
	u32 ref_and_mask_sdma7;
};

struct amdgpu_nbio_ras {
	struct amdgpu_ras_block_object ras_block;
	void (*handle_ras_controller_intr_no_bifring)(struct amdgpu_device *adev);
	void (*handle_ras_err_event_athub_intr_no_bifring)(struct amdgpu_device *adev);
	int (*init_ras_controller_interrupt)(struct amdgpu_device *adev);
	int (*init_ras_err_event_athub_interrupt)(struct amdgpu_device *adev);
};

struct amdgpu_nbio_funcs {
	const struct nbio_hdp_flush_reg *hdp_flush_reg;
	u32 (*get_hdp_flush_req_offset)(struct amdgpu_device *adev);
	u32 (*get_hdp_flush_done_offset)(struct amdgpu_device *adev);
	u32 (*get_pcie_index_offset)(struct amdgpu_device *adev);
	u32 (*get_pcie_data_offset)(struct amdgpu_device *adev);
	u32 (*get_pcie_index_hi_offset)(struct amdgpu_device *adev);
	u32 (*get_pcie_port_index_offset)(struct amdgpu_device *adev);
	u32 (*get_pcie_port_data_offset)(struct amdgpu_device *adev);
	u32 (*get_rev_id)(struct amdgpu_device *adev);
	void (*mc_access_enable)(struct amdgpu_device *adev, bool enable);
	u32 (*get_memsize)(struct amdgpu_device *adev);
	void (*sdma_doorbell_range)(struct amdgpu_device *adev, int instance,
			bool use_doorbell, int doorbell_index, int doorbell_size);
	void (*vpe_doorbell_range)(struct amdgpu_device *adev, int instance,
			bool use_doorbell, int doorbell_index, int doorbell_size);
	void (*vcn_doorbell_range)(struct amdgpu_device *adev, bool use_doorbell,
				   int doorbell_index, int instance);
	void (*gc_doorbell_init)(struct amdgpu_device *adev);
	void (*enable_doorbell_aperture)(struct amdgpu_device *adev,
					 bool enable);
	void (*enable_doorbell_selfring_aperture)(struct amdgpu_device *adev,
						  bool enable);
	void (*ih_doorbell_range)(struct amdgpu_device *adev,
				  bool use_doorbell, int doorbell_index);
	void (*enable_doorbell_interrupt)(struct amdgpu_device *adev,
					  bool enable);
	void (*update_medium_grain_clock_gating)(struct amdgpu_device *adev,
						 bool enable);
	void (*update_medium_grain_light_sleep)(struct amdgpu_device *adev,
						bool enable);
	void (*get_clockgating_state)(struct amdgpu_device *adev,
				      u64 *flags);
	void (*ih_control)(struct amdgpu_device *adev);
	void (*init_registers)(struct amdgpu_device *adev);
	void (*remap_hdp_registers)(struct amdgpu_device *adev);
	void (*enable_aspm)(struct amdgpu_device *adev,
			    bool enable);
	void (*program_aspm)(struct amdgpu_device *adev);
	void (*apply_lc_spc_mode_wa)(struct amdgpu_device *adev);
	void (*apply_l1_link_width_reconfig_wa)(struct amdgpu_device *adev);
	void (*clear_doorbell_interrupt)(struct amdgpu_device *adev);
	u32 (*get_rom_offset)(struct amdgpu_device *adev);
	int (*get_compute_partition_mode)(struct amdgpu_device *adev);
	u32 (*get_memory_partition_mode)(struct amdgpu_device *adev,
					 u32 *supp_modes);
	u64 (*get_pcie_replay_count)(struct amdgpu_device *adev);
};

struct amdgpu_nbio {
	const struct nbio_hdp_flush_reg *hdp_flush_reg;
	struct amdgpu_irq_src ras_controller_irq;
	struct amdgpu_irq_src ras_err_event_athub_irq;
	struct ras_common_if *ras_if;
	const struct amdgpu_nbio_funcs *funcs;
	struct amdgpu_nbio_ras  *ras;
};

int amdgpu_nbio_ras_sw_init(struct amdgpu_device *adev);
int amdgpu_nbio_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block);
u64 amdgpu_nbio_get_pcie_replay_count(struct amdgpu_device *adev);

#endif
