/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Daniel Hsiao <daniel.hsiao@mediatek.com>
 *	Jungchang Tsao <jungchang.tsao@mediatek.com>
 *	Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _VENC_DRV_BASE_
#define _VENC_DRV_BASE_

#include "mtk_vcodec_drv.h"

#include "venc_drv_if.h"

struct venc_common_if {
	/**
	 * (*init)() - initialize driver
	 * @ctx:	[in] mtk v4l2 context
	 * @handle: [out] driver handle
	 */
	int (*init)(struct mtk_vcodec_ctx *ctx);

	/**
	 * (*encode)() - trigger encode
	 * @handle: [in] driver handle
	 * @opt: [in] encode option
	 * @frm_buf: [in] frame buffer to store input frame
	 * @bs_buf: [in] bitstream buffer to store output bitstream
	 * @result: [out] encode result
	 */
	int (*encode)(void *handle, enum venc_start_opt opt,
		      struct venc_frm_buf *frm_buf,
		      struct mtk_vcodec_mem *bs_buf,
		      struct venc_done_result *result);

	/**
	 * (*set_param)() - set driver's parameter
	 * @handle: [in] driver handle
	 * @type: [in] parameter type
	 * @in: [in] buffer to store the parameter
	 */
	int (*set_param)(void *handle, enum venc_set_param_type type,
			 struct venc_enc_param *in);

	/**
	 * (*deinit)() - deinitialize driver.
	 * @handle: [in] driver handle
	 */
	int (*deinit)(void *handle);
};

#endif
