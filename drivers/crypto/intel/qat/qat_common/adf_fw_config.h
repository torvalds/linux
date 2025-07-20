/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_FW_CONFIG_H_
#define ADF_FW_CONFIG_H_

enum adf_fw_objs {
	ADF_FW_SYM_OBJ,
	ADF_FW_ASYM_OBJ,
	ADF_FW_DC_OBJ,
	ADF_FW_ADMIN_OBJ,
	ADF_FW_CY_OBJ,
};

struct adf_fw_config {
	u32 ae_mask;
	enum adf_fw_objs obj;
};

#endif
