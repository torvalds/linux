/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation */
#ifndef _ICP_QAT_HW_20_COMP_H_
#define _ICP_QAT_HW_20_COMP_H_

#include "icp_qat_hw_20_comp_defs.h"
#include "icp_qat_fw.h"

struct icp_qat_hw_comp_20_config_csr_lower {
	enum icp_qat_hw_comp_20_extended_delay_match_mode edmm;
	enum icp_qat_hw_comp_20_hw_comp_format algo;
	enum icp_qat_hw_comp_20_search_depth sd;
	enum icp_qat_hw_comp_20_hbs_control hbs;
	enum icp_qat_hw_comp_20_abd abd;
	enum icp_qat_hw_comp_20_lllbd_ctrl lllbd;
	enum icp_qat_hw_comp_20_min_match_control mmctrl;
	enum icp_qat_hw_comp_20_skip_hash_collision hash_col;
	enum icp_qat_hw_comp_20_skip_hash_update hash_update;
	enum icp_qat_hw_comp_20_byte_skip skip_ctrl;
};

static inline __u32
ICP_QAT_FW_COMP_20_BUILD_CONFIG_LOWER(struct icp_qat_hw_comp_20_config_csr_lower csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.algo,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_HW_COMP_FORMAT_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_HW_COMP_FORMAT_MASK);
	QAT_FIELD_SET(val32, csr.sd,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SEARCH_DEPTH_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SEARCH_DEPTH_MASK);
	QAT_FIELD_SET(val32, csr.edmm,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_EXTENDED_DELAY_MATCH_MODE_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_EXTENDED_DELAY_MATCH_MODE_MASK);
	QAT_FIELD_SET(val32, csr.hbs,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_HBS_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_HBS_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.lllbd,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LLLBD_CTRL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LLLBD_CTRL_MASK);
	QAT_FIELD_SET(val32, csr.mmctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_MIN_MATCH_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_MIN_MATCH_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.hash_col,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_COLLISION_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_COLLISION_MASK);
	QAT_FIELD_SET(val32, csr.hash_update,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_UPDATE_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_UPDATE_MASK);
	QAT_FIELD_SET(val32, csr.skip_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_BYTE_SKIP_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_BYTE_SKIP_MASK);
	QAT_FIELD_SET(val32, csr.abd, ICP_QAT_HW_COMP_20_CONFIG_CSR_ABD_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_ABD_MASK);

	return __builtin_bswap32(val32);
}

struct icp_qat_hw_comp_20_config_csr_upper {
	enum icp_qat_hw_comp_20_scb_control scb_ctrl;
	enum icp_qat_hw_comp_20_rmb_control rmb_ctrl;
	enum icp_qat_hw_comp_20_som_control som_ctrl;
	enum icp_qat_hw_comp_20_skip_hash_rd_control skip_hash_ctrl;
	enum icp_qat_hw_comp_20_scb_unload_control scb_unload_ctrl;
	enum icp_qat_hw_comp_20_disable_token_fusion_control disable_token_fusion_ctrl;
	enum icp_qat_hw_comp_20_lbms lbms;
	enum icp_qat_hw_comp_20_scb_mode_reset_mask scb_mode_reset;
	__u16 lazy;
	__u16 nice;
};

static inline __u32
ICP_QAT_FW_COMP_20_BUILD_CONFIG_UPPER(struct icp_qat_hw_comp_20_config_csr_upper csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.scb_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.rmb_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_RMB_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_RMB_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.som_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SOM_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SOM_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.skip_hash_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_RD_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SKIP_HASH_RD_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.scb_unload_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_UNLOAD_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_UNLOAD_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.disable_token_fusion_ctrl,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_DISABLE_TOKEN_FUSION_CONTROL_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_DISABLE_TOKEN_FUSION_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.lbms,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LBMS_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LBMS_MASK);
	QAT_FIELD_SET(val32, csr.scb_mode_reset,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_MODE_RESET_MASK_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_SCB_MODE_RESET_MASK_MASK);
	QAT_FIELD_SET(val32, csr.lazy,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LAZY_PARAM_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_LAZY_PARAM_MASK);
	QAT_FIELD_SET(val32, csr.nice,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_NICE_PARAM_BITPOS,
		      ICP_QAT_HW_COMP_20_CONFIG_CSR_NICE_PARAM_MASK);

	return __builtin_bswap32(val32);
}

struct icp_qat_hw_decomp_20_config_csr_lower {
	enum icp_qat_hw_decomp_20_hbs_control hbs;
	enum icp_qat_hw_decomp_20_lbms lbms;
	enum icp_qat_hw_decomp_20_hw_comp_format algo;
	enum icp_qat_hw_decomp_20_min_match_control mmctrl;
	enum icp_qat_hw_decomp_20_lz4_block_checksum_present lbc;
};

static inline __u32
ICP_QAT_FW_DECOMP_20_BUILD_CONFIG_LOWER(struct icp_qat_hw_decomp_20_config_csr_lower csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.hbs,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_HBS_CONTROL_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_HBS_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.lbms,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_LBMS_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_LBMS_MASK);
	QAT_FIELD_SET(val32, csr.algo,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_HW_DECOMP_FORMAT_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_HW_DECOMP_FORMAT_MASK);
	QAT_FIELD_SET(val32, csr.mmctrl,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_MIN_MATCH_CONTROL_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_MIN_MATCH_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.lbc,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_PRESENT_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_LZ4_BLOCK_CHECKSUM_PRESENT_MASK);

	return __builtin_bswap32(val32);
}

struct icp_qat_hw_decomp_20_config_csr_upper {
	enum icp_qat_hw_decomp_20_speculative_decoder_control sdc;
	enum icp_qat_hw_decomp_20_mini_cam_control mcc;
};

static inline __u32
ICP_QAT_FW_DECOMP_20_BUILD_CONFIG_UPPER(struct icp_qat_hw_decomp_20_config_csr_upper csr)
{
	u32 val32 = 0;

	QAT_FIELD_SET(val32, csr.sdc,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_SPECULATIVE_DECODER_CONTROL_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_SPECULATIVE_DECODER_CONTROL_MASK);
	QAT_FIELD_SET(val32, csr.mcc,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_MINI_CAM_CONTROL_BITPOS,
		      ICP_QAT_HW_DECOMP_20_CONFIG_CSR_MINI_CAM_CONTROL_MASK);

	return __builtin_bswap32(val32);
}

#endif
