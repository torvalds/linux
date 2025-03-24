/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * Header file of the register interface for JPEG driver on Exynos4x12.
*/

#ifndef JPEG_HW_EXYNOS4_H_
#define JPEG_HW_EXYNOS4_H_

void exynos4_jpeg_sw_reset(void __iomem *base);
void exynos4_jpeg_set_enc_dec_mode(void __iomem *base, unsigned int mode);
void __exynos4_jpeg_set_img_fmt(void __iomem *base, unsigned int img_fmt,
				unsigned int version);
void __exynos4_jpeg_set_enc_out_fmt(void __iomem *base, unsigned int out_fmt,
				    unsigned int version);
void exynos4_jpeg_set_enc_tbl(void __iomem *base);
void exynos4_jpeg_set_interrupt(void __iomem *base, unsigned int version);
unsigned int exynos4_jpeg_get_int_status(void __iomem *base);
void exynos4_jpeg_set_huf_table_enable(void __iomem *base, int value);
void exynos4_jpeg_set_sys_int_enable(void __iomem *base, int value);
void exynos4_jpeg_set_stream_buf_address(void __iomem *base,
					 unsigned int address);
void exynos4_jpeg_set_stream_size(void __iomem *base,
		unsigned int x_value, unsigned int y_value);
void exynos4_jpeg_set_frame_buf_address(void __iomem *base,
				struct s5p_jpeg_addr *jpeg_addr);
void exynos4_jpeg_set_encode_tbl_select(void __iomem *base,
		enum exynos4_jpeg_img_quality_level level);
void exynos4_jpeg_set_dec_components(void __iomem *base, int n);
void exynos4_jpeg_select_dec_q_tbl(void __iomem *base, char c, char x);
void exynos4_jpeg_select_dec_h_tbl(void __iomem *base, char c, char x);
void exynos4_jpeg_set_encode_hoff_cnt(void __iomem *base, unsigned int fmt);
void exynos4_jpeg_set_dec_bitstream_size(void __iomem *base, unsigned int size);
unsigned int exynos4_jpeg_get_stream_size(void __iomem *base);
unsigned int exynos4_jpeg_get_frame_fmt(void __iomem *base);

#endif /* JPEG_HW_EXYNOS4_H_ */
