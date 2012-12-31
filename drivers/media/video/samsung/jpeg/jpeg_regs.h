/* linux/drivers/media/video/samsung/jpeg/jpeg_regs.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file of the register interface for jpeg driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __JPEG_REGS_H__
#define __JPEG_REGS_H__

#include "jpeg_core.h"

void jpeg_sw_reset(void __iomem *base);
void jpeg_set_clk_power_on(void __iomem *base);
void jpeg_set_mode(void __iomem *base, int mode);
void jpeg_set_dec_out_fmt(void __iomem *base,
			enum jpeg_frame_format out_fmt);
void jpeg_set_enc_in_fmt(void __iomem *base,
			enum jpeg_frame_format in_fmt);
void jpeg_set_enc_out_fmt(void __iomem *base,
			enum jpeg_stream_format out_fmt);
void jpeg_set_enc_dri(void __iomem *base, unsigned int value);
void jpeg_set_enc_qtbl(void __iomem *base,
			enum jpeg_img_quality_level level);
void jpeg_set_enc_htbl(void __iomem *base);
void jpeg_set_enc_coef(void __iomem *base);
void jpeg_set_frame_addr(void __iomem *base, unsigned int fra_addr);
void jpeg_set_stream_addr(void __iomem *base, unsigned int str_addr);
void jpeg_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height);
void jpeg_set_frame_size(void __iomem *base,
			unsigned int width, unsigned int height);
enum jpeg_stream_format jpeg_get_stream_fmt(void __iomem *base);
unsigned int jpeg_get_stream_size(void __iomem *base);
void jpeg_start_decode(void __iomem *base);
void jpeg_start_encode(void __iomem *base);
unsigned int jpeg_get_int_status(void __iomem *base);
void jpeg_clear_int(void __iomem *base);

#endif /* __JPEG_REGS_H__ */

