/* SPDX-License-Identifier: MIT */
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
 *
 */

#ifndef __AMDGPU_REG_STATE_H__
#define __AMDGPU_REG_STATE_H__

enum amdgpu_reg_state {
	AMDGPU_REG_STATE_TYPE_INVALID	= 0,
	AMDGPU_REG_STATE_TYPE_XGMI	= 1,
	AMDGPU_REG_STATE_TYPE_WAFL	= 2,
	AMDGPU_REG_STATE_TYPE_PCIE	= 3,
	AMDGPU_REG_STATE_TYPE_USR	= 4,
	AMDGPU_REG_STATE_TYPE_USR_1	= 5
};

enum amdgpu_sysfs_reg_offset {
	AMDGPU_SYS_REG_STATE_XGMI	= 0x0000,
	AMDGPU_SYS_REG_STATE_WAFL	= 0x1000,
	AMDGPU_SYS_REG_STATE_PCIE	= 0x2000,
	AMDGPU_SYS_REG_STATE_USR	= 0x3000,
	AMDGPU_SYS_REG_STATE_USR_1	= 0x4000,
	AMDGPU_SYS_REG_STATE_END	= 0x5000,
};

struct amdgpu_reg_state_header {
	uint16_t		structure_size;
	uint8_t			format_revision;
	uint8_t			content_revision;
	uint8_t			state_type;
	uint8_t			num_instances;
	uint16_t		pad;
};

enum amdgpu_reg_inst_state {
	AMDGPU_INST_S_OK,
	AMDGPU_INST_S_EDISABLED,
	AMDGPU_INST_S_EACCESS,
};

struct amdgpu_smn_reg_data {
	uint64_t addr;
	uint32_t value;
	uint32_t pad;
};

struct amdgpu_reg_inst_header {
	uint16_t	instance;
	uint16_t	state;
	uint16_t	num_smn_regs;
	uint16_t	pad;
};


struct amdgpu_regs_xgmi_v1_0 {
	struct amdgpu_reg_inst_header	inst_header;

	struct amdgpu_smn_reg_data	smn_reg_values[];
};

struct amdgpu_reg_state_xgmi_v1_0 {
	/* common_header.state_type must be AMDGPU_REG_STATE_TYPE_XGMI */
	struct amdgpu_reg_state_header	common_header;

	struct amdgpu_regs_xgmi_v1_0	xgmi_state_regs[];
};

struct amdgpu_regs_wafl_v1_0 {
	struct amdgpu_reg_inst_header	inst_header;

	struct amdgpu_smn_reg_data	smn_reg_values[];
};

struct amdgpu_reg_state_wafl_v1_0 {
	/* common_header.state_type must be AMDGPU_REG_STATE_TYPE_WAFL */
	struct amdgpu_reg_state_header	common_header;

	struct amdgpu_regs_wafl_v1_0	wafl_state_regs[];
};

struct amdgpu_regs_pcie_v1_0 {
	struct amdgpu_reg_inst_header	inst_header;

	uint16_t			device_status;
	uint16_t			link_status;
	uint32_t			sub_bus_number_latency;
	uint32_t			pcie_corr_err_status;
	uint32_t			pcie_uncorr_err_status;

	struct amdgpu_smn_reg_data	smn_reg_values[];
};

struct amdgpu_reg_state_pcie_v1_0 {
	/* common_header.state_type must be AMDGPU_REG_STATE_TYPE_PCIE */
	struct amdgpu_reg_state_header	common_header;

	struct amdgpu_regs_pcie_v1_0	pci_state_regs[];
};

struct amdgpu_regs_usr_v1_0 {
	struct amdgpu_reg_inst_header	inst_header;

	struct amdgpu_smn_reg_data	smn_reg_values[];
};

struct amdgpu_reg_state_usr_v1_0 {
	/* common_header.state_type must be AMDGPU_REG_STATE_TYPE_USR */
	struct amdgpu_reg_state_header	common_header;

	struct amdgpu_regs_usr_v1_0	usr_state_regs[];
};

static inline size_t amdgpu_reginst_size(uint16_t num_inst, size_t inst_size,
					 uint16_t num_regs)
{
	return num_inst *
	       (inst_size + num_regs * sizeof(struct amdgpu_smn_reg_data));
}

#define amdgpu_asic_get_reg_state_supported(adev) \
	(((adev)->asic_funcs && (adev)->asic_funcs->get_reg_state) ? 1 : 0)

#define amdgpu_asic_get_reg_state(adev, state, buf, size)                  \
	((adev)->asic_funcs->get_reg_state ?                               \
		 (adev)->asic_funcs->get_reg_state((adev), (state), (buf), \
						   (size)) :               \
		 0)


int amdgpu_reg_state_sysfs_init(struct amdgpu_device *adev);
void amdgpu_reg_state_sysfs_fini(struct amdgpu_device *adev);

#endif
