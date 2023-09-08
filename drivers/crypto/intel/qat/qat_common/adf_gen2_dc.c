// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_gen2_dc.h"
#include "icp_qat_fw_comp.h"

static void qat_comp_build_deflate_ctx(void *ctx)
{
	struct icp_qat_fw_comp_req *req_tmpl = (struct icp_qat_fw_comp_req *)ctx;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comp_req_params *req_pars = &req_tmpl->comp_pars;
	struct icp_qat_fw_comp_cd_hdr *comp_cd_ctrl = &req_tmpl->comp_cd_ctrl;

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
					    ICP_QAT_FW_COMP_NOT_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_DISABLE_TYPE0_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_ENABLE_SECURE_RAM_USED_AS_INTMD_BUF);
	cd_pars->u.sl.comp_slice_cfg_word[0] =
		ICP_QAT_HW_COMPRESSION_CONFIG_BUILD(ICP_QAT_HW_COMPRESSION_DIR_COMPRESS,
						    ICP_QAT_HW_COMPRESSION_DELAYED_MATCH_DISABLED,
						    ICP_QAT_HW_COMPRESSION_ALGO_DEFLATE,
						    ICP_QAT_HW_COMPRESSION_DEPTH_1,
						    ICP_QAT_HW_COMPRESSION_FILE_TYPE_0);
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
	ICP_QAT_FW_COMN_NEXT_ID_SET(comp_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);
	ICP_QAT_FW_COMN_CURR_ID_SET(comp_cd_ctrl, ICP_QAT_FW_SLICE_COMP);

	/* Fill second half of the template for decompression */
	memcpy(req_tmpl + 1, req_tmpl, sizeof(*req_tmpl));
	req_tmpl++;
	header = &req_tmpl->comn_hdr;
	header->service_cmd_id = ICP_QAT_FW_COMP_CMD_DECOMPRESS;
	cd_pars = &req_tmpl->cd_pars;
	cd_pars->u.sl.comp_slice_cfg_word[0] =
		ICP_QAT_HW_COMPRESSION_CONFIG_BUILD(ICP_QAT_HW_COMPRESSION_DIR_DECOMPRESS,
						    ICP_QAT_HW_COMPRESSION_DELAYED_MATCH_DISABLED,
						    ICP_QAT_HW_COMPRESSION_ALGO_DEFLATE,
						    ICP_QAT_HW_COMPRESSION_DEPTH_1,
						    ICP_QAT_HW_COMPRESSION_FILE_TYPE_0);
}

void adf_gen2_init_dc_ops(struct adf_dc_ops *dc_ops)
{
	dc_ops->build_deflate_ctx = qat_comp_build_deflate_ctx;
}
EXPORT_SYMBOL_GPL(adf_gen2_init_dc_ops);
