/* linux/drivers/media/video/samsung/jpeg/jpeg_core.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Core file for operation of the jpeg driver encoder/docoder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>

#include "jpeg_core.h"
#include "jpeg_regs.h"
#include "jpeg_mem.h"

int jpeg_int_pending(struct jpeg_control *ctrl)
{
	unsigned int	int_status;

	int_status = jpeg_get_int_status(ctrl->reg_base);
	jpeg_dbg("state(%d)\n", int_status);

	jpeg_clear_int(ctrl->reg_base);

	return int_status;
}

int jpeg_set_dec_param(struct jpeg_control *ctrl)
{
	if (ctrl) {
		jpeg_sw_reset(ctrl->reg_base);
	} else {
		jpeg_err("jpeg ctrl is NULL\n");
		return -1;
	}

	jpeg_set_clk_power_on(ctrl->reg_base);
	jpeg_set_mode(ctrl->reg_base, 1);
	jpeg_set_dec_out_fmt(ctrl->reg_base, ctrl->dec_param.out_fmt);
	jpeg_set_stream_buf(&ctrl->mem.stream_data_addr, ctrl->mem.base);
	jpeg_set_stream_addr(ctrl->reg_base, ctrl->mem.stream_data_addr);
	jpeg_set_frame_buf(&ctrl->mem.frame_data_addr, ctrl->mem.base);
	jpeg_set_frame_addr(ctrl->reg_base, ctrl->mem.frame_data_addr);

	jpeg_info("jpeg_set_dec_param fmt(%d)\
			img_addr(0x%08x) jpeg_addr(0x%08x)\n",
			ctrl->dec_param.out_fmt,
			ctrl->mem.frame_data_addr,
			ctrl->mem.stream_data_addr);

	return 0;
}

int jpeg_set_enc_param(struct jpeg_control *ctrl)
{
	if (ctrl) {
		jpeg_sw_reset(ctrl->reg_base);
	} else {
		jpeg_err("jpeg ctrl is NULL\n");
		return -1;
	}

	jpeg_set_clk_power_on(ctrl->reg_base);
	jpeg_set_mode(ctrl->reg_base, 0);
	jpeg_set_enc_in_fmt(ctrl->reg_base, ctrl->enc_param.in_fmt);
	jpeg_set_enc_out_fmt(ctrl->reg_base, ctrl->enc_param.out_fmt);
	jpeg_set_enc_dri(ctrl->reg_base, 2);
	jpeg_set_frame_size(ctrl->reg_base,
		ctrl->enc_param.width, ctrl->enc_param.height);
	jpeg_set_stream_buf(&ctrl->mem.stream_data_addr, ctrl->mem.base);
	jpeg_set_stream_addr(ctrl->reg_base, ctrl->mem.stream_data_addr);
	jpeg_set_frame_buf(&ctrl->mem.frame_data_addr, ctrl->mem.base);
	jpeg_set_frame_addr(ctrl->reg_base, ctrl->mem.frame_data_addr);
	jpeg_set_enc_coef(ctrl->reg_base);
	jpeg_set_enc_qtbl(ctrl->reg_base, ctrl->enc_param.quality);
	jpeg_set_enc_htbl(ctrl->reg_base);

	return 0;
}

int jpeg_exe_dec(struct jpeg_control *ctrl)
{

	jpeg_start_decode(ctrl->reg_base);

	if (interruptible_sleep_on_timeout(&ctrl->wq, INT_TIMEOUT) == 0)
		jpeg_err("waiting for interrupt is timeout\n");


	if (ctrl->irq_ret != OK_ENC_OR_DEC) {
		jpeg_err("jpeg decode error(%d)\n", ctrl->irq_ret);
		return -1;
	}

	jpeg_get_frame_size(ctrl->reg_base,
		&ctrl->dec_param.width, &ctrl->dec_param.height);

	ctrl->dec_param.in_fmt = jpeg_get_stream_fmt(ctrl->reg_base);

	jpeg_info("decode img in_fmt(%d) width(%d) height(%d)\n",
			ctrl->dec_param.in_fmt , ctrl->dec_param.width,
			ctrl->dec_param.height);
	return 0;
}

int jpeg_exe_enc(struct jpeg_control *ctrl)
{

	jpeg_start_encode(ctrl->reg_base);

	if (interruptible_sleep_on_timeout(&ctrl->wq, INT_TIMEOUT) == 0)
		jpeg_err("waiting for interrupt is timeout\n");

	if (ctrl->irq_ret != OK_ENC_OR_DEC) {
		jpeg_err("jpeg encode error(%d)\n", ctrl->irq_ret);
		return -1;
	}

	ctrl->enc_param.size = jpeg_get_stream_size(ctrl->reg_base);

	return 0;
}

