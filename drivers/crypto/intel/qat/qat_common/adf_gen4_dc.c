// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include "adf_accel_devices.h"
#include "icp_qat_fw_comp.h"
#include "icp_qat_hw_20_comp.h"
#include "adf_gen4_dc.h"

static void qat_comp_build_deflate(void *ctx)
{
	struct icp_qat_fw_comp_req *req_tmpl =
				(struct icp_qat_fw_comp_req *)ctx;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comp_req_params *req_pars = &req_tmpl->comp_pars;
	struct icp_qat_hw_comp_20_config_csr_upper hw_comp_upper_csr = {0};
	struct icp_qat_hw_comp_20_config_csr_lower hw_comp_lower_csr = {0};
	struct icp_qat_hw_decomp_20_config_csr_lower hw_decomp_lower_csr = {0};
	u32 upper_val;
	u32 lower_val;

	memset(req_tmpl, 0, sizeof(*req_tmpl));
	header->hdr_flags =
		ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
	header->service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_COMP;
	header->service_cmd_id = ICP_QAT_FW_COMP_CMD_STATIC;
	header->comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_CD_FLD_TYPE_16BYTE_DATA,
					    QAT_COMN_PTR_TYPE_SGL);
	header->serv_specif_flags =
		ICP_QAT_FW_COMP_FLAGS_BUILD(ICP_QAT_FW_COMP_STATELESS_SESSION,
					    ICP_QAT_FW_COMP_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_DISABLE_TYPE0_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_ENABLE_SECURE_RAM_USED_AS_INTMD_BUF);
	hw_comp_lower_csr.skip_ctrl = ICP_QAT_HW_COMP_20_BYTE_SKIP_3BYTE_LITERAL;
	hw_comp_lower_csr.algo = ICP_QAT_HW_COMP_20_HW_COMP_FORMAT_ILZ77;
	hw_comp_lower_csr.lllbd = ICP_QAT_HW_COMP_20_LLLBD_CTRL_LLLBD_ENABLED;
	hw_comp_lower_csr.sd = ICP_QAT_HW_COMP_20_SEARCH_DEPTH_LEVEL_1;
	hw_comp_lower_csr.hash_update = ICP_QAT_HW_COMP_20_SKIP_HASH_UPDATE_DONT_ALLOW;
	hw_comp_lower_csr.edmm = ICP_QAT_HW_COMP_20_EXTENDED_DELAY_MATCH_MODE_EDMM_ENABLED;
	hw_comp_upper_csr.nice = ICP_QAT_HW_COMP_20_CONFIG_CSR_NICE_PARAM_DEFAULT_VAL;
	hw_comp_upper_csr.lazy = ICP_QAT_HW_COMP_20_CONFIG_CSR_LAZY_PARAM_DEFAULT_VAL;

	upper_val = ICP_QAT_FW_COMP_20_BUILD_CONFIG_UPPER(hw_comp_upper_csr);
	lower_val = ICP_QAT_FW_COMP_20_BUILD_CONFIG_LOWER(hw_comp_lower_csr);

	cd_pars->u.sl.comp_slice_cfg_word[0] = lower_val;
	cd_pars->u.sl.comp_slice_cfg_word[1] = upper_val;

	req_pars->crc.legacy.initial_adler = COMP_CPR_INITIAL_ADLER;
	req_pars->crc.legacy.initial_crc32 = COMP_CPR_INITIAL_CRC;
	req_pars->req_par_flags =
		ICP_QAT_FW_COMP_REQ_PARAM_FLAGS_BUILD(ICP_QAT_FW_COMP_SOP,
						      ICP_QAT_FW_COMP_EOP,
						      ICP_QAT_FW_COMP_BFINAL,
						      ICP_QAT_FW_COMP_CNV,
						      ICP_QAT_FW_COMP_CNV_RECOVERY,
						      ICP_QAT_FW_COMP_NO_CNV_DFX,
						      ICP_QAT_FW_COMP_CRC_MODE_LEGACY,
						      ICP_QAT_FW_COMP_NO_XXHASH_ACC,
						      ICP_QAT_FW_COMP_CNV_ERROR_NONE,
						      ICP_QAT_FW_COMP_NO_APPEND_CRC,
						      ICP_QAT_FW_COMP_NO_DROP_DATA);

	/* Fill second half of the template for decompression */
	memcpy(req_tmpl + 1, req_tmpl, sizeof(*req_tmpl));
	req_tmpl++;
	header = &req_tmpl->comn_hdr;
	header->service_cmd_id = ICP_QAT_FW_COMP_CMD_DECOMPRESS;
	cd_pars = &req_tmpl->cd_pars;

	hw_decomp_lower_csr.algo = ICP_QAT_HW_DECOMP_20_HW_DECOMP_FORMAT_DEFLATE;
	lower_val = ICP_QAT_FW_DECOMP_20_BUILD_CONFIG_LOWER(hw_decomp_lower_csr);

	cd_pars->u.sl.comp_slice_cfg_word[0] = lower_val;
	cd_pars->u.sl.comp_slice_cfg_word[1] = 0;
}

void adf_gen4_init_dc_ops(struct adf_dc_ops *dc_ops)
{
	dc_ops->build_deflate_ctx = qat_comp_build_deflate;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_dc_ops);
