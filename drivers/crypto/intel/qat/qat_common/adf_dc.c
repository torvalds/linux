// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_dc.h"
#include "icp_qat_fw_comp.h"

int qat_comp_build_ctx(struct adf_accel_dev *accel_dev, void *ctx, enum adf_dc_algo algo)
{
	struct icp_qat_fw_comp_req *req_tmpl = ctx;
	struct icp_qat_fw_comp_cd_hdr *comp_cd_ctrl = &req_tmpl->comp_cd_ctrl;
	struct icp_qat_fw_comp_req_params *req_pars = &req_tmpl->comp_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	int ret;

	memset(req_tmpl, 0, sizeof(*req_tmpl));
	header->hdr_flags =
		ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
	header->service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_COMP;
	header->comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_CD_FLD_TYPE_16BYTE_DATA,
					    QAT_COMN_PTR_TYPE_SGL);
	header->serv_specif_flags =
		ICP_QAT_FW_COMP_FLAGS_BUILD(ICP_QAT_FW_COMP_STATELESS_SESSION,
					    ICP_QAT_FW_COMP_NOT_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_NOT_DISABLE_TYPE0_ENH_AUTO_SELECT_BEST,
					    ICP_QAT_FW_COMP_ENABLE_SECURE_RAM_USED_AS_INTMD_BUF);

	/* Build HW config block for compression */
	ret = GET_DC_OPS(accel_dev)->build_comp_block(ctx, algo);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to build compression block\n");
		return ret;
	}

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
						      ICP_QAT_FW_COMP_NO_DROP_DATA,
						      ICP_QAT_FW_COMP_NO_PARTIAL_DECOMPRESS);
	ICP_QAT_FW_COMN_NEXT_ID_SET(comp_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);
	ICP_QAT_FW_COMN_CURR_ID_SET(comp_cd_ctrl, ICP_QAT_FW_SLICE_COMP);

	/* Fill second half of the template for decompression */
	memcpy(req_tmpl + 1, req_tmpl, sizeof(*req_tmpl));
	req_tmpl++;

	/* Build HW config block for decompression */
	ret = GET_DC_OPS(accel_dev)->build_decomp_block(req_tmpl, algo);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "Failed to build decompression block\n");

	return ret;
}
