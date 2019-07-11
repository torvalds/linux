/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_RLC_H__
#define __AMDGPU_RLC_H__

#include "clearstate_defs.h"

struct amdgpu_rlc_funcs {
	bool (*is_rlc_enabled)(struct amdgpu_device *adev);
	void (*set_safe_mode)(struct amdgpu_device *adev);
	void (*unset_safe_mode)(struct amdgpu_device *adev);
	int  (*init)(struct amdgpu_device *adev);
	u32  (*get_csb_size)(struct amdgpu_device *adev);
	void (*get_csb_buffer)(struct amdgpu_device *adev, volatile u32 *buffer);
	int  (*get_cp_table_num)(struct amdgpu_device *adev);
	int  (*resume)(struct amdgpu_device *adev);
	void (*stop)(struct amdgpu_device *adev);
	void (*reset)(struct amdgpu_device *adev);
	void (*start)(struct amdgpu_device *adev);
};

struct amdgpu_rlc {
	/* for power gating */
	struct amdgpu_bo        *save_restore_obj;
	uint64_t                save_restore_gpu_addr;
	volatile uint32_t       *sr_ptr;
	const u32               *reg_list;
	u32                     reg_list_size;
	/* for clear state */
	struct amdgpu_bo        *clear_state_obj;
	uint64_t                clear_state_gpu_addr;
	volatile uint32_t       *cs_ptr;
	const struct cs_section_def   *cs_data;
	u32                     clear_state_size;
	/* for cp tables */
	struct amdgpu_bo        *cp_table_obj;
	uint64_t                cp_table_gpu_addr;
	volatile uint32_t       *cp_table_ptr;
	u32                     cp_table_size;

	/* safe mode for updating CG/PG state */
	bool in_safe_mode;
	const struct amdgpu_rlc_funcs *funcs;

	/* for firmware data */
	u32 save_and_restore_offset;
	u32 clear_state_descriptor_offset;
	u32 avail_scratch_ram_locations;
	u32 reg_restore_list_size;
	u32 reg_list_format_start;
	u32 reg_list_format_separate_start;
	u32 starting_offsets_start;
	u32 reg_list_format_size_bytes;
	u32 reg_list_size_bytes;
	u32 reg_list_format_direct_reg_list_length;
	u32 save_restore_list_cntl_size_bytes;
	u32 save_restore_list_gpm_size_bytes;
	u32 save_restore_list_srm_size_bytes;

	u32 *register_list_format;
	u32 *register_restore;
	u8 *save_restore_list_cntl;
	u8 *save_restore_list_gpm;
	u8 *save_restore_list_srm;

	bool is_rlc_v2_1;
};

void amdgpu_gfx_rlc_enter_safe_mode(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_exit_safe_mode(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_sr(struct amdgpu_device *adev, u32 dws);
int amdgpu_gfx_rlc_init_csb(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_cpt(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_setup_cp_table(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_fini(struct amdgpu_device *adev);

#endif
