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

/* firmware ID used in rlc toc */
typedef enum _FIRMWARE_ID_ {
	FIRMWARE_ID_INVALID					= 0,
	FIRMWARE_ID_RLC_G_UCODE					= 1,
	FIRMWARE_ID_RLC_TOC					= 2,
	FIRMWARE_ID_RLCG_SCRATCH                                = 3,
	FIRMWARE_ID_RLC_SRM_ARAM                                = 4,
	FIRMWARE_ID_RLC_SRM_INDEX_ADDR                          = 5,
	FIRMWARE_ID_RLC_SRM_INDEX_DATA                          = 6,
	FIRMWARE_ID_RLC_P_UCODE                                 = 7,
	FIRMWARE_ID_RLC_V_UCODE                                 = 8,
	FIRMWARE_ID_RLX6_UCODE                                  = 9,
	FIRMWARE_ID_RLX6_DRAM_BOOT                              = 10,
	FIRMWARE_ID_GLOBAL_TAP_DELAYS                           = 11,
	FIRMWARE_ID_SE0_TAP_DELAYS                              = 12,
	FIRMWARE_ID_SE1_TAP_DELAYS                              = 13,
	FIRMWARE_ID_GLOBAL_SE0_SE1_SKEW_DELAYS                  = 14,
	FIRMWARE_ID_SDMA0_UCODE                                 = 15,
	FIRMWARE_ID_SDMA0_JT                                    = 16,
	FIRMWARE_ID_SDMA1_UCODE                                 = 17,
	FIRMWARE_ID_SDMA1_JT                                    = 18,
	FIRMWARE_ID_CP_CE                                       = 19,
	FIRMWARE_ID_CP_PFP                                      = 20,
	FIRMWARE_ID_CP_ME                                       = 21,
	FIRMWARE_ID_CP_MEC                                      = 22,
	FIRMWARE_ID_CP_MES                                      = 23,
	FIRMWARE_ID_MES_STACK                                   = 24,
	FIRMWARE_ID_RLC_SRM_DRAM_SR                             = 25,
	FIRMWARE_ID_RLCG_SCRATCH_SR                             = 26,
	FIRMWARE_ID_RLCP_SCRATCH_SR                             = 27,
	FIRMWARE_ID_RLCV_SCRATCH_SR                             = 28,
	FIRMWARE_ID_RLX6_DRAM_SR                                = 29,
	FIRMWARE_ID_SDMA0_PG_CONTEXT                            = 30,
	FIRMWARE_ID_SDMA1_PG_CONTEXT                            = 31,
	FIRMWARE_ID_GLOBAL_MUX_SELECT_RAM                       = 32,
	FIRMWARE_ID_SE0_MUX_SELECT_RAM                          = 33,
	FIRMWARE_ID_SE1_MUX_SELECT_RAM                          = 34,
	FIRMWARE_ID_ACCUM_CTRL_RAM                              = 35,
	FIRMWARE_ID_RLCP_CAM                                    = 36,
	FIRMWARE_ID_RLC_SPP_CAM_EXT                             = 37,
	FIRMWARE_ID_MAX                                         = 38,
} FIRMWARE_ID;

typedef struct _RLC_TABLE_OF_CONTENT {
	union {
		unsigned int	DW0;
		struct {
			unsigned int	offset		: 25;
			unsigned int	id		: 7;
		};
	};

	union {
		unsigned int	DW1;
		struct {
			unsigned int	load_at_boot		: 1;
			unsigned int	load_at_vddgfx		: 1;
			unsigned int	load_at_reset		: 1;
			unsigned int	memory_destination	: 2;
			unsigned int	vfflr_image_code	: 4;
			unsigned int	load_mode_direct	: 1;
			unsigned int	save_for_vddgfx		: 1;
			unsigned int	save_for_vfflr		: 1;
			unsigned int	reserved		: 1;
			unsigned int	signed_source		: 1;
			unsigned int	size			: 18;
		};
	};

	union {
		unsigned int	DW2;
		struct {
			unsigned int	indirect_addr_reg	: 16;
			unsigned int	index			: 16;
		};
	};

	union {
		unsigned int	DW3;
		struct {
			unsigned int	indirect_data_reg	: 16;
			unsigned int	indirect_start_offset	: 16;
		};
	};
} RLC_TABLE_OF_CONTENT;

#define RLC_TOC_MAX_SIZE		64

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
	void (*update_spm_vmid)(struct amdgpu_device *adev, unsigned vmid);
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

	/* for rlc autoload */
	struct amdgpu_bo	*rlc_autoload_bo;
	u64			rlc_autoload_gpu_addr;
	void			*rlc_autoload_ptr;

	/* rlc toc buffer */
	struct amdgpu_bo	*rlc_toc_bo;
	uint64_t		rlc_toc_gpu_addr;
	void			*rlc_toc_buf;
};

void amdgpu_gfx_rlc_enter_safe_mode(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_exit_safe_mode(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_sr(struct amdgpu_device *adev, u32 dws);
int amdgpu_gfx_rlc_init_csb(struct amdgpu_device *adev);
int amdgpu_gfx_rlc_init_cpt(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_setup_cp_table(struct amdgpu_device *adev);
void amdgpu_gfx_rlc_fini(struct amdgpu_device *adev);

#endif
