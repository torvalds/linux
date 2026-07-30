// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include <media/v4l2-isp.h>
#include <media/videobuf2-v4l2.h>

#include "rppx1.h"

#define RPPX1_PARAMS_BLOCK_INFO(block, data) \
	[RPPX1_PARAMS_BLOCK_TYPE_ ## block] = { \
		.size = sizeof(struct rppx1_ ## data ## _params), \
	}

static const struct v4l2_isp_params_block_type_info
rppx1_ext_params_blocks_info[] = {
	RPPX1_PARAMS_BLOCK_INFO(BLS_PRE1, bls),
	RPPX1_PARAMS_BLOCK_INFO(BLS_PRE2, bls),
	RPPX1_PARAMS_BLOCK_INFO(LIN_PRE1, lin),
	RPPX1_PARAMS_BLOCK_INFO(LIN_PRE2, lin),
	RPPX1_PARAMS_BLOCK_INFO(LSC_PRE1, lsc),
	RPPX1_PARAMS_BLOCK_INFO(LSC_PRE2, lsc),
	RPPX1_PARAMS_BLOCK_INFO(AWBG_PRE1, awbg),
	RPPX1_PARAMS_BLOCK_INFO(AWBG_PRE2, awbg),
	RPPX1_PARAMS_BLOCK_INFO(CCOR_POST, ccor),
	RPPX1_PARAMS_BLOCK_INFO(HIST_PRE1, hist),
	RPPX1_PARAMS_BLOCK_INFO(HIST_PRE2, hist),
	RPPX1_PARAMS_BLOCK_INFO(HIST_POST, hist),
	RPPX1_PARAMS_BLOCK_INFO(EXM_PRE1, exm),
	RPPX1_PARAMS_BLOCK_INFO(EXM_PRE2, exm),
	RPPX1_PARAMS_BLOCK_INFO(WBMEAS_POST, wbmeas),
	RPPX1_PARAMS_BLOCK_INFO(GA_HV, ga),
	RPPX1_PARAMS_BLOCK_INFO(GA_MV, ga),
};

int rppx1_params(struct rppx1 *rpp, struct vb2_buffer *vb, size_t max_size,
		 rppx1_reg_write write, void *priv)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct v4l2_isp_buffer *cfg;
	size_t block_offset;
	int ret;

	ret = v4l2_isp_params_validate_buffer_size(rpp->dev, vb, max_size);
	if (ret)
		return ret;

	cfg = vb2_plane_vaddr(&vbuf->vb2_buf, 0);

	ret = v4l2_isp_params_validate_buffer(rpp->dev, vb, cfg,
					      rppx1_ext_params_blocks_info,
					      ARRAY_SIZE(rppx1_ext_params_blocks_info));
	if (ret)
		return ret;

	/* Walk the list of parameter blocks and process them. */
	block_offset = 0;
	while (block_offset < cfg->data_size) {
		const union rppx1_params_block *block =
			(const union rppx1_params_block *)&cfg->data[block_offset];
		struct rpp_module *module;
		int ret;

		block_offset += block->header.size;

		switch (block->header.type) {
		case RPPX1_PARAMS_BLOCK_TYPE_BLS_PRE1:
			module = &rpp->pre1.bls;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_LIN_PRE1:
			module = &rpp->pre1.lin;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_LSC_PRE1:
			module = &rpp->pre1.lsc;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE1:
			module = &rpp->pre1.awbg;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_CCOR_POST:
			module = &rpp->post.ccor;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_HIST_POST:
			module = &rpp->post.hist;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE1:
			module = &rpp->pre1.exm;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_WBMEAS_POST:
			module = &rpp->post.wbmeas;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_GA_HV:
			module = &rpp->hv.ga;
			break;
		default:
			dev_warn(rpp->dev,
				 "Not handled RPPX1 block type: 0x%04x\n",
				 block->header.type);
			continue;
		}

		ret = rpp_module_call(module, fill_params, block, write, priv);
		if (ret) {
			dev_err(rpp->dev,
				"Error processing RPPX1 block type: 0x%04x\n",
				block->header.type);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_params);
