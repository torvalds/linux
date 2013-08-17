/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_regs.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file of the register interface for jpeg hx driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __JPEG_REGS_H__
#define __JPEG_REGS_H__

#include "jpeg_hx_core.h"

void jpeg_hx_sw_reset(void __iomem *base);
void jpeg_hx_set_dma_num(void __iomem *base);
void jpeg_hx_set_enc_dec_mode(void __iomem *base, enum jpeg_mode mode);
void jpeg_hx_clk_on(void __iomem *base);
void jpeg_hx_clk_off(void __iomem *base);
void jpeg_hx_clk_set(void __iomem *base, enum jpeg_clk_mode mode);
void jpeg_hx_set_dec_out_fmt(void __iomem *base,
					enum jpeg_frame_format out_fmt);
void jpeg_hx_set_enc_in_fmt(void __iomem *base,
					enum jpeg_frame_format in_fmt);
void jpeg_hx_set_enc_out_fmt(void __iomem *base,
					enum jpeg_stream_format out_fmt);
void jpeg_hx_set_enc_tbl(void __iomem *base,
					enum jpeg_img_quality_level level);
void jpeg_hx_set_interrupt(void __iomem *base);
unsigned int jpeg_hx_get_int_status(void __iomem *base);
void jpeg_hx_clear_int_status(void __iomem *base, int value);
void jpeg_hx_set_huf_table_enable(void __iomem *base, int value);
void jpeg_hx_set_dec_scaling(void __iomem *base,
		enum jpeg_scale_value scale_value);
void jpeg_hx_set_sys_int_enable(void __iomem *base, int value);
void jpeg_hx_set_stream_buf_address(void __iomem *base, unsigned int address);
void jpeg_hx_set_stream_size(void __iomem *base,
		unsigned int x_value, unsigned int y_value);
void jpeg_hx_set_frame_buf_address(void __iomem *base, enum jpeg_frame_format fmt, unsigned int address, unsigned int width, unsigned int height);
void jpeg_hx_set_encode_tbl_select(void __iomem *base,
		enum jpeg_img_quality_level level);
void jpeg_hx_set_encode_hoff_cnt(void __iomem *base, enum jpeg_stream_format fmt);
void jpeg_hx_set_dec_bitstream_size(void __iomem *base, unsigned int size);
unsigned int jpeg_hx_get_stream_size(void __iomem *base);
void jpeg_hx_get_frame_size(void __iomem *base,
			unsigned int *width, unsigned int *height);
void jpeg_hx_coef(void __iomem *base, unsigned int i);
void jpeg_hx_set_enc_luma_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt);
void jpeg_hx_set_enc_cbcr_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt);
void jpeg_hx_set_y16(void __iomem *base);
void jpeg_hx_set_timer(void __iomem *base,
		unsigned int time_value);
void jpeg_hx_start(void __iomem *base);
void jpeg_hx_re_start(void __iomem *base);
void jpeg_hx_color_mode_select(void __iomem *base, enum jpeg_frame_format out_fmt);
void jpeg_hx_set_dec_luma_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt);
void jpeg_hx_set_dec_cbcr_stride(void __iomem *base, unsigned int w_stride, enum jpeg_frame_format fmt);
void jpeg_hx_color_mode_select(void __iomem *base, enum jpeg_frame_format out_fmt);

enum jpeg_stream_format jpeg_hx_get_frame_fmt(void __iomem *base);

#endif /* __JPEG_REGS_H__ */
