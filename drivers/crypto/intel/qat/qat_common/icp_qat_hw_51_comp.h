/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ICP_QAT_HW_51_COMP_H_
#define ICP_QAT_HW_51_COMP_H_

#include <linux/types.h>

#include "icp_qat_fw.h"
#include "icp_qat_hw_51_comp_defs.h"

struct icp_qat_hw_comp_51_config_csr_lower {
	enum icp_qat_hw_comp_51_abd abd;
	enum icp_qat_hw_comp_51_lllbd_ctrl lllbd;
	enum icp_qat_hw_comp_51_search_depth sd;
	enum icp_qat_hw_comp_51_min_match_control mmctrl;
	enum icp_qat_hw_comp_51_lz4_block_checksum lbc;
};

static inline u32
ICP_QAT_FW_COMP_51_BUILD_CONFIG_LOWER(struct icp_qat_hw_comp_51_config_csr_lower csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.abd,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_ABD_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_ABD_MASK);
	QAT_FIELD_SET(val32, csr.lllbd,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_LLLBD_CTRL_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_LLLBD_CTRL_MASK);
	QAT_FIELD_SET(val32, csr.sd,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_SEARCH_DEPTH_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_SEARCH_DEPTH_MASK);
	QAT_FIELD_SET(val32, csr.mmctrl,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_MIN_MATCH_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_MIN_MATCH_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.lbc,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_MASK);

	return val32;
}

struct icp_qat_hw_comp_51_config_csr_upper {
	enum icp_qat_hw_comp_51_dmm_algorithm edmm;
	enum icp_qat_hw_comp_51_bms bms;
	enum icp_qat_hw_comp_51_scb_mode_reset_mask scb_mode_reset;
};

static inline u32
ICP_QAT_FW_COMP_51_BUILD_CONFIG_UPPER(struct icp_qat_hw_comp_51_config_csr_upper csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.edmm,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_DMM_ALGORITHM_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_DMM_ALGORITHM_MASK);
	QAT_FIELD_SET(val32, csr.bms,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_BMS_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_BMS_MASK);
	QAT_FIELD_SET(val32, csr.scb_mode_reset,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_SCB_MODE_RESET_MASK_BITPOS,
		      ICP_QAT_HW_COMP_51_CONFIG_CSR_SCB_MODE_RESET_MASK_MASK);

	return val32;
}

struct icp_qat_hw_decomp_51_config_csr_lower {
	enum icp_qat_hw_decomp_51_lz4_block_checksum lbc;
};

static inline u32
ICP_QAT_FW_DECOMP_51_BUILD_CONFIG_LOWER(struct icp_qat_hw_decomp_51_config_csr_lower csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.lbc,
		      ICP_QAT_HW_DECOMP_51_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_BITPOS,
		      ICP_QAT_HW_DECOMP_51_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_MASK);

	return val32;
}

struct icp_qat_hw_decomp_51_config_csr_upper {
	enum icp_qat_hw_decomp_51_bms bms;
};

static inline u32
ICP_QAT_FW_DECOMP_51_BUILD_CONFIG_UPPER(struct icp_qat_hw_decomp_51_config_csr_upper csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.bms,
		      ICP_QAT_HW_DECOMP_51_CONFIG_CSR_BMS_BITPOS,
		      ICP_QAT_HW_DECOMP_51_CONFIG_CSR_BMS_MASK);

	return val32;
}

#endif /* ICP_QAT_HW_51_COMP_H_ */
