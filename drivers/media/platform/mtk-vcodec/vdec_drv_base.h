/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 */

#ifndef _VDEC_DRV_BASE_
#define _VDEC_DRV_BASE_

#include "mtk_vcodec_drv.h"

#include "vdec_drv_if.h"

struct vdec_common_if {
	/**
	 * (*init)() - initialize decode driver
	 * @ctx     : [in] mtk v4l2 context
	 * @h_vdec  : [out] driver handle
	 */
	int (*init)(struct mtk_vcodec_ctx *ctx);

	/**
	 * (*decode)() - trigger decode
	 * @h_vdec  : [in] driver handle
	 * @bs      : [in] input bitstream
	 * @fb      : [in] frame buffer to store decoded frame
	 * @res_chg : [out] resolution change happen
	 */
	int (*decode)(void *h_vdec, struct mtk_vcodec_mem *bs,
		      struct vdec_fb *fb, bool *res_chg);

	/**
	 * (*get_param)() - get driver's parameter
	 * @h_vdec : [in] driver handle
	 * @type   : [in] input parameter type
	 * @out    : [out] buffer to store query result
	 */
	int (*get_param)(void *h_vdec, enum vdec_get_param_type type,
			 void *out);

	/**
	 * (*deinit)() - deinitialize driver.
	 * @h_vdec : [in] driver handle to be deinit
	 */
	void (*deinit)(void *h_vdec);
};

#endif
