/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#include <linux/videodev2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include "cif_isp10_regs.h"
#include "cif_isp10.h"
#include <linux/pm_runtime.h>
#include <linux/vmalloc.h>

static int cif_isp10_mipi_isr(
	unsigned int mis,
	void *cntxt);
static int cif_isp10_isp_isr(
	unsigned int mis,
	void *cntxt);
static void init_output_formats(void);

struct v4l2_fmtdesc output_formats[MAX_NB_FORMATS];

/* JPEG quantization tables for JPEG encoding */
/* DC luma table according to ISO/IEC 10918-1 annex K */
static const unsigned char dc_luma_table_annex_k[] = {
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b
};

/* DC chroma table according to ISO/IEC 10918-1 annex K */
static const unsigned char dc_chroma_table_annex_k[] = {
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b
};

/* AC luma table according to ISO/IEC 10918-1 annex K */
static const unsigned char ac_luma_table_annex_k[] = {
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

/* AC Chroma table according to ISO/IEC 10918-1 annex K */
static const unsigned char ac_chroma_table_annex_k[] = {
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

/* Standard JPEG quantization tables */
/* luma */
/* According to JPEG spec:
 * [
 *	16, 11, 10, 16,  24,  40,  51,  61,
 *	12, 12, 14, 19,  26,  58,  60,  55,
 *	14, 13, 16, 24,  40,  57,  69,  56,
 *	14, 17, 22, 29,  51,  87,  80,  62,
 *	18, 22, 37, 56,  68, 109, 103,  77,
 *	24, 35, 55, 64,  81, 104, 113,  92,
 *	49, 64, 78, 87, 103, 121, 120, 101,
 *	72, 92, 95, 98, 112, 100, 103,  99
 * ]
 */

/* CIF needs it in zigzag order */
static const unsigned char yq_table_base_zigzag[] = {
	16, 11, 12, 14, 12, 10, 16, 14,
	13, 14, 18, 17, 16, 19, 24, 40,
	26, 24, 22, 22, 24, 49, 35, 37,
	29, 40, 58, 51, 61, 60, 57, 51,
	56, 55, 64, 72, 92, 78, 64, 68,
	87, 69, 55, 56, 80, 109, 81, 87,
	95, 98, 103, 104, 103, 62, 77, 113,
	121, 112, 100, 120, 92, 101, 103, 99
};

/* chroma */
/* According to JPEG spec:
 * [
 *	17, 18, 24, 47, 99, 99, 99, 99,
 *	18, 21, 26, 66, 99, 99, 99, 99,
 *	24, 26, 56, 99, 99, 99, 99, 99,
 *	47, 66, 99, 99, 99, 99, 99, 99,
 *	99, 99, 99, 99, 99, 99, 99, 99,
 *	99, 99, 99, 99, 99, 99, 99, 99,
 *	99, 99, 99, 99, 99, 99, 99, 99,
 *	99, 99, 99, 99, 99, 99, 99, 99
 * ]
 */

/* CIF needs it in zigzag order */
static const unsigned char uvq_table_base_zigzag[] = {
	17, 18, 18, 24, 21, 24, 47, 26,
	26, 47, 99, 66, 56, 66, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99
};

static struct cif_isp10_fmt cif_isp10_output_format[] = {
/* ************* YUV422 ************* */
// index 0
{
	.name		= "YUV422-Interleaved",
	.fourcc	= V4L2_PIX_FMT_YUYV,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
// index 1
{
	.name		= "YUV422-Interleaved",
	.fourcc	= V4L2_PIX_FMT_YUYV,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
// index 2
{
	.name		= "YVU422-Interleaved",
	.fourcc	= V4L2_PIX_FMT_UYVY,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
// index 3
{
	.name		= "YUV422-Planar",
	.fourcc	= V4L2_PIX_FMT_YUV422P,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
// index 4
{
	.name		= "YUV422-Semi-Planar",
	.fourcc	= V4L2_PIX_FMT_NV16,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
/* ************* YUV420 ************* */
// index 5
{
	.name		= "YUV420-Planar",
	.fourcc	= V4L2_PIX_FMT_YUV420,
	.flags	= 0,
	.depth	= 12,
	.rotation = false,
	.overlay = false,
},
// index 6
{
	.name		= "YUV420-Planar",
	.fourcc	= V4L2_PIX_FMT_YUV420,
	.flags	= 0,
	.depth	= 12,
	.rotation = false,
	.overlay = false,
},
// index 7
{
	.name		= "YVU420-Planar",
	.fourcc	= V4L2_PIX_FMT_YVU420,
	.flags	= 0,
	.depth	= 12,
	.rotation = false,
	.overlay = false,
},
// index 8
{
	.name		= "YUV420-Semi-Planar",
	.fourcc	= V4L2_PIX_FMT_NV12,
	.flags	= 0,
	.depth	= 12,
	.rotation = false,
	.overlay = false,
},
// index 9
{
	.name		= "YVU420-Semi-Planar",
	.fourcc	= V4L2_PIX_FMT_NV21,
	.flags	= 0,
	.depth	= 12,
	.rotation = false,
	.overlay = false,
},
/* ************* YUV400 ************* */
// index 10
{
	.name		= "YVU400-Grey-Planar",
	.fourcc	= V4L2_PIX_FMT_GREY,
	.flags	= 0,
	.depth	= 8,
	.rotation = false,
	.overlay = false,
},
/* ************* YUV444 ************* */
// index 11
{
	.name		= "YVU444-Planar",
	.fourcc	= V4L2_PIX_FMT_YUV444,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
// index 12
{
	.name		= "YVU444-Semi-Planar",
	.fourcc	= V4L2_PIX_FMT_NV24,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
/* ************* JPEG ************* */
// index 13
{
	.name		= "JPEG",
	.fourcc	= V4L2_PIX_FMT_JPEG,
	.flags	= 0,
	.depth	= 16,
	.rotation = false,
	.overlay = false,
},
/* ************ RGB565 *********** */
// index 14
{
	.name       = "RGB565",
	.fourcc = V4L2_PIX_FMT_RGB565,
	.flags  = 0,
	.depth  = 16,
	.rotation = false,
	.overlay = false,
},
/* ************ SGRBG8 *********** */
// index 15
{
	.name       = "SGRBG8",
	.fourcc = V4L2_PIX_FMT_SGRBG8,
	.flags  = 0,
	.depth  = 8,
	.rotation = false,
	.overlay = false,
},
};

struct cif_isp10_hw_error {
	char *name;
	unsigned int count;
	unsigned int mask;
	unsigned int type;	/* isp:0 ;mipi:1 */
};

static struct cif_isp10_hw_error cif_isp10_hw_errors[] = {
	{
	 .name = "isp_data_loss",
	 .count = 0,
	 .mask = CIF_ISP_DATA_LOSS,
	 .type = 0,
	 },
	{
	 .name = "isp_pic_size_err",
	 .count = 0,
	 .mask = CIF_ISP_PIC_SIZE_ERROR,
	 .type = 0,
	 },
	{
	 .name = "mipi_fifo_err",
	 .count = 0,
	 .mask = CIF_MIPI_SYNC_FIFO_OVFLW(1),
	 .type = 1,
	 },
	{
	 .name = "dphy_err_sot",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_SOT(3),
	 .type = 1,
	 },
	{
	 .name = "dphy_err_sot_sync",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_SOT_SYNC(3),
	 .type = 1,
	 },
	{
	 .name = "dphy_err_eot_sync",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_EOT_SYNC(3),
	 .type = 1,
	 },
	{
	 .name = "dphy_err_ctrl",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_CTRL(3),
	 .type = 1,
	 },
	{
	 .name = "csi_err_protocol",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_PROTOCOL,
	 .type = 2,
	 },
	{
	 .name = "csi_err_ecc1",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_ECC1,
	 .type = 2,
	 },
	{
	 .name = "csi_err_ecc2",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_ECC2,
	 .type = 2,
	 },
	{
	 .name = "csi_err_cs",
	 .count = 0,
	 .mask = CIF_MIPI_ERR_CS,
	 .type = 2,
	 },
	{
	 .name = "fifo_ovf",
	 .count = 0,
	 .mask = (3 << 0),
	 .type = 2,
	 },
	{
	 .name = "isp_outform",
	 .count = 0,
	 .mask = CIF_ISP_ERR_OUTFORM_SIZE,
	 .type = 0,
	 },
	{
	 .name = "isp_stab",
	 .count = 0,
	 .mask = CIF_ISP_ERR_IS_SIZE,
	 .type = 0,
	 },
	{
	 .name = "isp_inform",
	 .count = 0,
	 .mask = CIF_ISP_ERR_INFORM_SIZE,
	 .type = 0,
	}
};

/* Defines */

#define CIF_ISP10_INVALID_BUFF_ADDR ((u32)~0)
#define CIF_ISP10_MI_IS_BUSY(dev)\
	(dev->config.mi_config.mp.busy ||\
	dev->config.mi_config.sp.busy ||\
	dev->config.mi_config.dma.busy)
enum {
	CIF_ISP10_ASYNC_JPEG = 0x1,
	CIF_ISP10_ASYNC_YCFLT = 0x2,
	CIF_ISP10_ASYNC_ISM = 0x4,
	CIF_ISP10_ASYNC_DMA = 0x8
};

#define CIF_ISP10_ALWAYS_ASYNC 0x00
#define CIF_ISP10_ALWAYS_STALL_ON_NO_BUFS (false)

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))
#endif

#ifndef DIV_TRUNCATE
#define DIV_TRUNCATE(x, y) ((x) / (y))
#endif

/* Structures and Types */

/* Static Functions */
static const char *cif_isp10_interface_string(
	enum pltfrm_cam_itf_type itf)
{
	switch (itf) {
	case PLTFRM_CAM_ITF_MIPI:
		return "MIPI";
	case PLTFRM_CAM_ITF_BT601_8:
		return "DVP_BT601_8Bit";
	case PLTFRM_CAM_ITF_BT656_8:
		return "DVP_BT656_8Bit";
	case PLTFRM_CAM_ITF_BT601_10:
		return "DVP_BT601_10Bit";
	case PLTFRM_CAM_ITF_BT656_10:
		return "DVP_BT656_10Bit";
	case PLTFRM_CAM_ITF_BT601_12:
		return "DVP_BT601_12Bit";
	case PLTFRM_CAM_ITF_BT656_12:
		return "DVP_BT656_12Bit";
	case PLTFRM_CAM_ITF_BT601_16:
		return "DVP_BT601_16Bit";
	case PLTFRM_CAM_ITF_BT656_16:
		return "DVP_BT656_16Bit";
	case PLTFRM_CAM_ITF_BT656_8I:
		return "DVP_BT656_8Bit_interlace";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

static const char *cif_isp10_img_src_state_string(
	enum cif_isp10_img_src_state state)
{
	switch (state) {
	case CIF_ISP10_IMG_SRC_STATE_OFF:
		return "OFF";
	case CIF_ISP10_IMG_SRC_STATE_SW_STNDBY:
		return "SW_STNDBY";
	case CIF_ISP10_IMG_SRC_STATE_STREAMING:
		return "STREAMING";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

static const char *cif_isp10_state_string(
	enum cif_isp10_state state)
{
	switch (state) {
	case CIF_ISP10_STATE_DISABLED:
		return "DISABLED";
	case CIF_ISP10_STATE_INACTIVE:
		return "INACTIVE";
	case CIF_ISP10_STATE_READY:
		return "READY";
	case CIF_ISP10_STATE_STREAMING:
		return "STREAMING";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

static const char *cif_isp10_pm_state_string(
	enum cif_isp10_pm_state pm_state)
{
	switch (pm_state) {
	case CIF_ISP10_PM_STATE_OFF:
		return "OFF";
	case CIF_ISP10_PM_STATE_SW_STNDBY:
		return "STANDBY";
	case CIF_ISP10_PM_STATE_SUSPENDED:
		return "SUSPENDED";
	case CIF_ISP10_PM_STATE_STREAMING:
		return "STREAMING";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

static const char *cif_isp10_stream_id_string(
	enum cif_isp10_stream_id stream_id)
{
	switch (stream_id) {
	case CIF_ISP10_STREAM_SP:
		return "SP";
	case CIF_ISP10_STREAM_MP:
		return "MP";
	case CIF_ISP10_STREAM_DMA:
		return "DMA";
	case CIF_ISP10_STREAM_ISP:
		return "ISP";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

static const char *cif_isp10_inp_string(
	enum cif_isp10_inp inp)
{
	switch (inp) {
	case CIF_ISP10_INP_CSI:
		return "CSI";
	case CIF_ISP10_INP_CPI:
		return "CPI";
	case CIF_ISP10_INP_DMA:
		return "DMA(ISP)";
	case CIF_ISP10_INP_DMA_IE:
		return "DMA(Image Effects)";
	case CIF_ISP10_INP_DMA_SP:
		return "DMA(SP)";
	default:
		return "UNKNOWN/UNSUPPORTED";
	}
}

enum cif_isp10_inp cif_isp10_input_index2inp(
	struct cif_isp10_device *dev,
	unsigned int input)
{
	struct pltfrm_cam_itf itf;

	if (input >= dev->img_src_cnt)
		return input - dev->img_src_cnt + CIF_ISP10_INP_DMA;

	cif_isp10_pltfrm_g_interface_config(
		dev->img_src_array[input],
		&itf);
	if (PLTFRM_CAM_ITF_IS_MIPI(itf.type))
		return CIF_ISP10_INP_CSI;
	if (PLTFRM_CAM_ITF_IS_DVP(itf.type))
		return CIF_ISP10_INP_CPI;

	return -EINVAL;
}

static const char *cif_isp10_pix_fmt_string(int pixfmt)
{
	switch (pixfmt) {
	case CIF_YUV400:
		return "YUV400";
	case CIF_YUV420I:
		return "YUV420I";
	case CIF_YUV420SP:
		return "YUV420SP";
	case CIF_YUV420P:
		return "YUV420P";
	case CIF_YVU420I:
		return "YVU420I";
	case CIF_YVU420SP:
		return "YVU420SP";
	case CIF_YVU420P:
		return "YVU420P";
	case CIF_YUV422I:
		return "YUV422I";
	case CIF_YUV422SP:
		return "YUV422SP";
	case CIF_YUV422P:
		return "YUV422P";
	case CIF_YVU422I:
		return "YVU422I";
	case CIF_YVU422SP:
		return "YVU422SP";
	case CIF_YVU422P:
		return "YVU422P";
	case CIF_YUV444I:
		return "YUV444I";
	case CIF_YUV444SP:
		return "YUV444SP";
	case CIF_YUV444P:
		return "YUV444P";
	case CIF_YVU444I:
		return "YVU444I";
	case CIF_YVU444SP:
		return "YVU444SP";
	case CIF_YVU444P:
		return "YVU444SP";
	case CIF_UYV400:
		return "UYV400";
	case CIF_UYV420I:
		return "UYV420I";
	case CIF_UYV420SP:
		return "UYV420SP";
	case CIF_UYV420P:
		return "UYV420P";
	case CIF_VYU420I:
		return "VYU420I";
	case CIF_VYU420SP:
		return "VYU420SP";
	case CIF_VYU420P:
		return "VYU420P";
	case CIF_UYV422I:
		return "UYV422I";
	case CIF_UYV422SP:
		return "UYV422I";
	case CIF_UYV422P:
		return "UYV422P";
	case CIF_VYU422I:
		return "VYU422I";
	case CIF_VYU422SP:
		return "VYU422SP";
	case CIF_VYU422P:
		return "VYU422P";
	case CIF_UYV444I:
		return "UYV444I";
	case CIF_UYV444SP:
		return "UYV444SP";
	case CIF_UYV444P:
		return "UYV444P";
	case CIF_VYU444I:
		return "VYU444I";
	case CIF_VYU444SP:
		return "VYU444SP";
	case CIF_VYU444P:
		return "VYU444P";
	case CIF_RGB565:
		return "RGB565";
	case CIF_RGB666:
		return "RGB666";
	case CIF_RGB888:
		return "RGB888";
	case CIF_BAYER_SBGGR8:
		return "BAYER BGGR8";
	case CIF_BAYER_SGBRG8:
		return "BAYER GBRG8";
	case CIF_BAYER_SGRBG8:
		return "BAYER GRBG8";
	case CIF_BAYER_SRGGB8:
		return "BAYER RGGB8";
	case CIF_BAYER_SBGGR10:
		return "BAYER BGGR10";
	case CIF_BAYER_SGBRG10:
		return "BAYER GBRG10";
	case CIF_BAYER_SGRBG10:
		return "BAYER GRBG10";
	case CIF_BAYER_SRGGB10:
		return "BAYER RGGB10";
	case CIF_BAYER_SBGGR12:
		return "BAYER BGGR12";
	case CIF_BAYER_SGBRG12:
		return "BAYER GBRG12";
	case CIF_BAYER_SGRBG12:
		return "BAYER GRBG12";
	case CIF_BAYER_SRGGB12:
		return "BAYER RGGB12";
	case CIF_DATA:
		return "DATA";
	case CIF_JPEG:
		return "JPEG";
	default:
		return "unknown/unsupported";
	}
}

static void cif_isp10_debug_print_mi_sp(struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_info(dev->dev,
		"\n  MI_CTRL 0x%08x/0x%08x\n"
		"  MI_STATUS 0x%08x\n"
		"  MI_RIS 0x%08x/0x%08x\n"
		"  MI_IMSC 0x%08x\n"
		"  MI_SP_Y_SIZE %d/%d\n"
		"  MI_SP_CB_SIZE %d/%d\n"
		"  MI_SP_CR_SIZE %d/%d\n"
		"  MI_SP_PIC_WIDTH %d\n"
		"  MI_SP_PIC_HEIGHT %d\n"
		"  MI_SP_PIC_LLENGTH %d\n"
		"  MI_SP_PIC_SIZE %d\n"
		"  MI_SP_Y_BASE_AD 0x%08x/0x%08x\n"
		"  MI_SP_Y_OFFS_CNT %d/%d\n"
		"  MI_SP_Y_OFFS_CNT_START %d\n"
		"  MI_SP_CB_OFFS_CNT %d/%d\n"
		"  MI_SP_CB_OFFS_CNT_START %d\n"
		"  MI_SP_CR_OFFS_CNT %d/%d\n"
		"  MI_SP_CR_OFFS_CNT_START %d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_CTRL_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_STATUS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_RIS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MIS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_IMSC),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CB_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CB_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CR_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CR_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_PIC_WIDTH),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_PIC_HEIGHT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_LLENGTH),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_PIC_SIZE),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_BASE_AD_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_BASE_AD_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_OFFS_CNT_START),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CB_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CB_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CB_OFFS_CNT_START),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CR_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CR_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_CR_OFFS_CNT_START));
}

static void cif_isp10_debug_print_mi_mp(struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_info(dev->dev,
		"\n  MI_CTRL 0x%08x/0x%08x\n"
		"  MI_STATUS 0x%08x\n"
		"  MI_BYTE_CNT %d\n"
		"  MI_RIS 0x%08x/0x%08x\n"
		"  MI_IMSC 0x%08x\n"
		"  MI_MP_Y_SIZE %d/%d\n"
		"  MI_MP_CB_SIZE %d/%d\n"
		"  MI_MP_CR_SIZE %d/%d\n"
		"  MI_MP_Y_BASE_AD 0x%08x/0x%08x\n"
		"  MI_MP_Y_OFFS_CNT %d/%d\n"
		"  MI_MP_Y_OFFS_CNT_START %d\n"
		"  MI_MP_CB_OFFS_CNT %d/%d\n"
		"  MI_MP_CB_OFFS_CNT_START %d\n"
		"  MI_MP_CR_OFFS_CNT %d/%d\n"
		"  MI_MP_CR_OFFS_CNT_START %d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_CTRL_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_STATUS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_BYTE_CNT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_RIS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MIS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_IMSC),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_BASE_AD_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_BASE_AD_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_OFFS_CNT_START),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_OFFS_CNT_START),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_OFFS_CNT_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_OFFS_CNT_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_OFFS_CNT_START));
}

static void cif_isp10_debug_print_srsz(struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_info(dev->dev,
		"\n  SRSZ_CTRL 0x%08x/0x%08x\n"
		"  SRSZ_SCALE_HY %d/%d\n"
		"  SRSZ_SCALE_HCB %d/%d\n"
		"  SRSZ_SCALE_HCR %d/%d\n"
		"  SRSZ_SCALE_VY %d/%d\n"
		"  SRSZ_SCALE_VC %d/%d\n"
		"  SRSZ_PHASE_HY %d/%d\n"
		"  SRSZ_PHASE_HC %d/%d\n"
		"  SRSZ_PHASE_VY %d/%d\n"
		"  SRSZ_PHASE_VC %d/%d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_CTRL_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HY),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HCB),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HCB_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HCR),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_HCR_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_VY),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_VY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_VC),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_SCALE_VC_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_HY),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_HY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_HC),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_HC_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_VY),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_VY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_VC),
		cif_ioread32(dev->config.base_addr +
			CIF_SRSZ_PHASE_VC_SHD));
}

static void cif_isp10_debug_print_mrsz(struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_info(dev->dev,
		"\n  MRSZ_CTRL 0x%08x/0x%08x\n"
		"  MRSZ_SCALE_HY %d/%d\n"
		"  MRSZ_SCALE_HCB %d/%d\n"
		"  MRSZ_SCALE_HCR %d/%d\n"
		"  MRSZ_SCALE_VY %d/%d\n"
		"  MRSZ_SCALE_VC %d/%d\n"
		"  MRSZ_PHASE_HY %d/%d\n"
		"  MRSZ_PHASE_HC %d/%d\n"
		"  MRSZ_PHASE_VY %d/%d\n"
		"  MRSZ_PHASE_VC %d/%d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_CTRL_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HY),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HCB),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HCB_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HCR),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_HCR_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_VY),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_VY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_VC),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_SCALE_VC_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_HY),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_HY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_HC),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_HC_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_VY),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_VY_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_VC),
		cif_ioread32(dev->config.base_addr +
			CIF_MRSZ_PHASE_VC_SHD));
}

static void cif_isp10_debug_print_block(
	struct cif_isp10_device *dev,
	const char *block_name)
{
	if (!strncmp(block_name, "all", 3)) {
		cif_isp10_debug_print_srsz(dev);
		cif_isp10_debug_print_mrsz(dev);
		cif_isp10_debug_print_mi_sp(dev);
		cif_isp10_debug_print_mi_mp(dev);
	} else if (!strncmp(block_name, "srsz", 4)) {
		cif_isp10_debug_print_srsz(dev);
	} else if (!strncmp(block_name, "mrsz", 4)) {
		cif_isp10_debug_print_mrsz(dev);
	} else if (!strncmp(block_name, "mi_sp", 5)) {
		cif_isp10_debug_print_mi_sp(dev);
	} else if (!strncmp(block_name, "mi_mp", 5)) {
		cif_isp10_debug_print_mi_mp(dev);
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown block %s\n", block_name);
	}
}

static u32 cif_isp10_calc_llength(
	u32 width,
	u32 stride,
	enum cif_isp10_pix_fmt pix_fmt)
{
	if (stride == 0)
		return width;

	if (CIF_ISP10_PIX_FMT_IS_YUV(pix_fmt)) {
		u32 num_cplanes =
			CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(pix_fmt);
		if (num_cplanes == 0)
			return 8 * stride / CIF_ISP10_PIX_FMT_GET_BPP(pix_fmt);
		else
			return stride;
	} else if (CIF_ISP10_PIX_FMT_IS_RGB(pix_fmt)) {
		return 8 * stride / CIF_ISP10_PIX_FMT_GET_BPP(pix_fmt);
	} else {
		return width;
	}
}

static int cif_isp10_set_pm_state(
	struct cif_isp10_device *dev,
	enum cif_isp10_pm_state pm_state)
{
	cif_isp10_pltfrm_pr_dbg(dev->dev, "%s -> %s\n",
		cif_isp10_pm_state_string(dev->pm_state),
		cif_isp10_pm_state_string(pm_state));

	if (dev->pm_state == pm_state)
		return 0;

	switch (pm_state) {
	case CIF_ISP10_PM_STATE_OFF:
	case CIF_ISP10_PM_STATE_SUSPENDED:
		if ((dev->pm_state == CIF_ISP10_PM_STATE_SW_STNDBY) ||
			(dev->pm_state == CIF_ISP10_PM_STATE_STREAMING)) {
			pm_runtime_put_sync(dev->dev);
		}
		break;
	case CIF_ISP10_PM_STATE_SW_STNDBY:
	case CIF_ISP10_PM_STATE_STREAMING:
		if ((dev->pm_state == CIF_ISP10_PM_STATE_OFF) ||
			(dev->pm_state == CIF_ISP10_PM_STATE_SUSPENDED)) {
			pm_runtime_get_sync(dev->dev);
		}
		break;
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown or unsupported PM state %d\n", pm_state);
		return -EINVAL;
	}

	dev->pm_state = pm_state;

	return 0;
}

static int cif_isp10_img_src_set_state(
	struct cif_isp10_device *dev,
	enum cif_isp10_img_src_state state)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev, "%s -> %s\n",
		cif_isp10_img_src_state_string(dev->img_src_state),
		cif_isp10_img_src_state_string(state));

	if (dev->img_src_state == state)
		return 0;

	switch (state) {
	case CIF_ISP10_IMG_SRC_STATE_OFF:
		ret = cif_isp10_img_src_s_power(dev->img_src, false);
		break;
	case CIF_ISP10_IMG_SRC_STATE_SW_STNDBY:
		if (dev->img_src_state == CIF_ISP10_IMG_SRC_STATE_STREAMING) {
			ret = cif_isp10_img_src_s_streaming(
				dev->img_src, false);
		} else {
			ret = cif_isp10_img_src_s_power(dev->img_src, true);
		}
		break;
	case CIF_ISP10_IMG_SRC_STATE_STREAMING:
		if (dev->config.flash_mode !=
			CIF_ISP10_FLASH_MODE_OFF)
			cif_isp10_img_src_s_ctrl(dev->img_src,
				CIF_ISP10_CID_FLASH_MODE,
				dev->config.flash_mode);
		ret = cif_isp10_img_src_s_streaming(dev->img_src, true);
		break;
	default:
		break;
	}

	if ((dev->config.flash_mode != CIF_ISP10_FLASH_MODE_OFF) &&
		(IS_ERR_VALUE(ret) ||
		(state == CIF_ISP10_IMG_SRC_STATE_OFF)))
		cif_isp10_img_src_s_ctrl(dev->img_src,
			CIF_ISP10_CID_FLASH_MODE,
			CIF_ISP10_FLASH_MODE_OFF);

	if (!IS_ERR_VALUE(ret))
		dev->img_src_state = state;
	else
		cif_isp10_pltfrm_pr_err(dev->dev,
			"failed with err %d\n", ret);

	return ret;
}

static int cif_isp10_img_srcs_init(
	struct cif_isp10_device *dev)
{
	int ret = 0;

	memset(dev->img_src_array, 0x00, sizeof(dev->img_src_array));
	dev->img_src_cnt = cif_isp10_pltfrm_get_img_src_device(dev->dev,
		dev->img_src_array, CIF_ISP10_NUM_INPUTS);

	if (dev->img_src_cnt > 0)
		return 0;

	dev->img_src_cnt = 0;
	ret = -EFAULT;

	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_img_src_select_strm_fmt(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	u32 index;
	struct cif_isp10_strm_fmt_desc strm_fmt_desc;
	struct cif_isp10_strm_fmt request_strm_fmt;
	bool matching_format_found = false;
	bool better_match = false;
	u32 target_width, target_height;
	u32 img_src_width, img_src_height;
	u32 best_diff = ~0;
	int vblanking;

	if (IS_ERR_OR_NULL(dev->img_src)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"no image source selected as input (call s_input first)\n");
		ret = -EFAULT;
		goto err;
	}

	ret = cif_isp10_get_target_frm_size(dev,
		&target_width, &target_height);
	if (IS_ERR_VALUE(ret))
		goto err;

	/* find the best matching format from the image source */
	/* TODO: frame interval and pixel format handling */
	for (index = 0;; index++) {
		if (IS_ERR_VALUE(cif_isp10_img_src_enum_strm_fmts(dev->img_src,
			index, &strm_fmt_desc)))
			break;
		if (!strm_fmt_desc.discrete_frmsize) {
			if (strm_fmt_desc.min_frmsize.width >= target_width)
				img_src_width = strm_fmt_desc.min_frmsize.width;
			else if (strm_fmt_desc.max_frmsize.width >=
				target_width)
				img_src_width = target_width;
			else
				img_src_width = strm_fmt_desc.max_frmsize.width;
			if (strm_fmt_desc.min_frmsize.height >= target_height)
				img_src_height =
					strm_fmt_desc.min_frmsize.height;
			else if (strm_fmt_desc.max_frmsize.height >=
				target_height)
				img_src_height = target_height;
			else
				img_src_height =
					strm_fmt_desc.max_frmsize.height;
		} else {
			img_src_width = strm_fmt_desc.min_frmsize.width;
			img_src_height = strm_fmt_desc.min_frmsize.height;
		}

		if ((img_src_width >= target_width) &&
			(img_src_height >= target_height)) {
			u32 diff = abs(
				target_height -
				(target_width * img_src_height /
				img_src_width));
			if (matching_format_found) {
				if (dev->config.jpeg_config.enable &&
				((img_src_width >=
				request_strm_fmt.frm_fmt.width) &&
				(img_src_height >
				request_strm_fmt.frm_fmt.height)))
					/*
					 * for image capturing we try to
					 * maximize the size
					 */
					better_match = true;
				else if (!dev->config.jpeg_config.enable &&
				((strm_fmt_desc.min_intrvl.denominator /
				strm_fmt_desc.min_intrvl.numerator) >
				(request_strm_fmt.frm_intrvl.denominator /
				request_strm_fmt.frm_intrvl.numerator)))
					/* maximize fps */
					better_match = true;
				else if (!dev->config.jpeg_config.enable &&
				((strm_fmt_desc.min_intrvl.denominator /
				strm_fmt_desc.min_intrvl.numerator) ==
				(request_strm_fmt.frm_intrvl.denominator /
				request_strm_fmt.frm_intrvl.numerator)) &&
				(diff < best_diff))
					/*
					 * chose better aspect ratio
					 * match if fps equal
					 */
					better_match = true;
				else
					better_match = false;
			}

			if (!matching_format_found ||
				better_match) {
				request_strm_fmt.frm_fmt.width =
					strm_fmt_desc.min_frmsize.width;
				request_strm_fmt.frm_fmt.height =
					strm_fmt_desc.min_frmsize.height;
				request_strm_fmt.frm_fmt.std_id =
					strm_fmt_desc.std_id;
				request_strm_fmt.frm_fmt.pix_fmt =
					strm_fmt_desc.pix_fmt;
				request_strm_fmt.frm_intrvl.numerator =
					strm_fmt_desc.min_intrvl.numerator;
				request_strm_fmt.frm_intrvl.denominator =
					strm_fmt_desc.min_intrvl.denominator;
				request_strm_fmt.frm_fmt.defrect =
					strm_fmt_desc.defrect;
				best_diff = diff;
				matching_format_found = true;
			}
		// FIXME::GST set fomat(any@32768x32768) failed, force pass
		} else {
			request_strm_fmt.frm_fmt.width =
				strm_fmt_desc.min_frmsize.width;
			request_strm_fmt.frm_fmt.height =
				strm_fmt_desc.min_frmsize.height;
			request_strm_fmt.frm_fmt.std_id =
				strm_fmt_desc.std_id;
			request_strm_fmt.frm_fmt.pix_fmt =
				strm_fmt_desc.pix_fmt;
			request_strm_fmt.frm_intrvl.numerator =
				strm_fmt_desc.min_intrvl.numerator;
			request_strm_fmt.frm_intrvl.denominator =
				strm_fmt_desc.min_intrvl.denominator;
			request_strm_fmt.frm_fmt.defrect =
				strm_fmt_desc.defrect;
			matching_format_found = true;
		}
	}

	if (!matching_format_found) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"no matching image source format (%dx%d) found\n",
			target_width, target_height);
		ret = -EINVAL;
		goto err;
	}

	cif_isp10_pltfrm_pr_dbg(
		dev->dev,
		"requesting format %s %dx%d(%d,%d,%d,%d)@%d/%dfps from %s\n",
		cif_isp10_pix_fmt_string(request_strm_fmt.frm_fmt.pix_fmt),
		request_strm_fmt.frm_fmt.width,
		request_strm_fmt.frm_fmt.height,
		request_strm_fmt.frm_fmt.defrect.left,
		request_strm_fmt.frm_fmt.defrect.top,
		request_strm_fmt.frm_fmt.defrect.width,
		request_strm_fmt.frm_fmt.defrect.height,
		request_strm_fmt.frm_intrvl.denominator,
		request_strm_fmt.frm_intrvl.numerator,
		cif_isp10_img_src_g_name(dev->img_src));

	ret = cif_isp10_img_src_s_strm_fmt(dev->img_src, &request_strm_fmt);
	if (IS_ERR_VALUE(ret))
		goto err;

	dev->config.img_src_output = request_strm_fmt;

	ret = cif_isp10_img_src_g_ctrl(dev->img_src,
		CIF_ISP10_CID_VBLANKING, &vblanking);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_dbg(
			dev->dev,
			"get vblanking failed: %d\n", ret);
			vblanking = 0;
	}

	if (vblanking >= 0)
		dev->isp_dev.v_blanking_us = vblanking;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

/*
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void cif_isp10_config_ism(struct cif_isp10_device *dev, bool async)
{
	const struct cif_isp10_ism_config *pconfig =
		&dev->config.isp_config.ism_config;

	if (pconfig->ism_en) {
		cif_isp10_pltfrm_pr_dbg(dev->dev, "%dx%d -> %dx%d@(%d,%d)\n",
			dev->isp_dev.input_width,
			dev->isp_dev.input_height,
			pconfig->ism_params.h_size,
			pconfig->ism_params.v_size,
			pconfig->ism_params.h_offs,
			pconfig->ism_params.v_offs);
		cif_iowrite32(pconfig->ism_params.recenter,
			dev->config.base_addr + CIF_ISP_IS_RECENTER);
		cif_iowrite32(pconfig->ism_params.max_dx,
			dev->config.base_addr + CIF_ISP_IS_MAX_DX);
		cif_iowrite32(pconfig->ism_params.max_dy,
			dev->config.base_addr + CIF_ISP_IS_MAX_DY);
		cif_iowrite32(pconfig->ism_params.displace,
			dev->config.base_addr + CIF_ISP_IS_DISPLACE);
		cif_iowrite32(pconfig->ism_params.h_offs,
			dev->config.base_addr + CIF_ISP_IS_H_OFFS);
		cif_iowrite32(pconfig->ism_params.v_offs,
			dev->config.base_addr + CIF_ISP_IS_V_OFFS);
		cif_iowrite32(pconfig->ism_params.h_size,
			dev->config.base_addr + CIF_ISP_IS_H_SIZE);
		cif_iowrite32(pconfig->ism_params.v_size,
			dev->config.base_addr + CIF_ISP_IS_V_SIZE);
		cif_iowrite32OR(1,
			dev->config.base_addr + CIF_ISP_IS_CTRL);
		dev->config.isp_config.output.width =
			dev->config.isp_config.ism_config.ism_params.h_size;
		dev->config.isp_config.output.height =
			dev->config.isp_config.ism_config.ism_params.v_size;
	} else {
		cif_iowrite32(pconfig->ism_params.recenter,
			dev->config.base_addr + CIF_ISP_IS_RECENTER);
		cif_iowrite32(pconfig->ism_params.max_dx,
			dev->config.base_addr + CIF_ISP_IS_MAX_DX);
		cif_iowrite32(pconfig->ism_params.max_dy,
			dev->config.base_addr + CIF_ISP_IS_MAX_DY);
		cif_iowrite32(pconfig->ism_params.displace,
			dev->config.base_addr + CIF_ISP_IS_DISPLACE);
		cif_iowrite32(0,
			dev->config.base_addr + CIF_ISP_IS_H_OFFS);
		cif_iowrite32(0,
			dev->config.base_addr + CIF_ISP_IS_V_OFFS);
		cif_iowrite32(dev->config.isp_config.output.width,
			dev->config.base_addr + CIF_ISP_IS_H_SIZE);
		cif_iowrite32(dev->config.isp_config.output.height,
			dev->config.base_addr + CIF_ISP_IS_V_SIZE);
		if (PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type))
			cif_iowrite32(dev->config.isp_config.output.height / 2,
				dev->config.base_addr + CIF_ISP_IS_V_SIZE);
		cif_iowrite32(0,
			dev->config.base_addr + CIF_ISP_IS_CTRL);
	}

	if (async)
		cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD,
			dev->config.base_addr + CIF_ISP_CTRL);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  ISP_IS_H_OFFS %d/%d\n"
		"  ISP_IS_V_OFFS %d/%d\n"
		"  ISP_IS_H_SIZE %d/%d\n"
		"  ISP_IS_V_SIZE %d/%d\n"
		"  ISP_IS_RECENTER 0x%08x\n"
		"  ISP_IS_MAX_DX %d\n"
		"  ISP_IS_MAX_DY %d\n"
		"  ISP_IS_DISPLACE 0x%08x\n"
		"  ISP_IS_CTRL 0x%08x\n",
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_H_OFFS),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_H_OFFS_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_V_OFFS),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_V_OFFS_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_H_SIZE),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_H_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_V_SIZE),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_V_SIZE_SHD),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_RECENTER),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_MAX_DX),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_MAX_DY),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_DISPLACE),
		cif_ioread32(dev->config.base_addr +
			CIF_ISP_IS_CTRL));
}

static void cif_isp10_program_jpeg_tables(
	struct cif_isp10_device *dev)
{
	unsigned int ratio = dev->config.jpeg_config.ratio;
	unsigned int i = 0;
	unsigned int q, q_next, scale;

	cif_isp10_pltfrm_pr_dbg(NULL, "ratio %d\n", ratio);

	/* Y q-table 0 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_QUANT0,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	if (ratio != 50) {
		scale = (ratio < 50) ? 5000 / ratio : 200 - (ratio << 1);
		for (i = 0; i < 32; i++) {
			q = yq_table_base_zigzag[i * 2];
			q_next = yq_table_base_zigzag[i * 2 + 1];
			q = (scale * q + 50) / 100;
			q = (q > 1) ? ((q < 255) ? q : 255) : 1;
			q_next = (scale * q_next + 50) / 100;
			q_next = (q_next > 1) ?
				((q_next < 255) ? q_next : 255) : 1;
			cif_iowrite32(q_next + (q << 8),
				dev->config.base_addr +
				CIF_JPE_TABLE_DATA);
		}
	} else {
		for (i = 0; i < 32; i++) {
			q = yq_table_base_zigzag[i * 2];
			q_next = yq_table_base_zigzag[i * 2 + 1];
			cif_iowrite32(q_next + (q << 8),
				dev->config.base_addr +
				CIF_JPE_TABLE_DATA);
		}
	}

	/* U/V q-table 0 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_QUANT1,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	if (ratio != 50) {
		for (i = 0; i < 32; i++) {
			q = uvq_table_base_zigzag[i * 2];
			q_next = uvq_table_base_zigzag[i * 2 + 1];
			q = (scale * q + 50) / 100;
			q = (q > 1) ? ((q < 255) ? q : 255) : 1;
			q_next = (scale * q_next + 50) / 100;
			q_next = (q_next > 1) ?
				((q_next < 255) ? q_next : 255) : 1;
			cif_iowrite32(q_next + (q << 8),
				dev->config.base_addr +
				CIF_JPE_TABLE_DATA);
		}
	} else {
		for (i = 0; i < 32; i++) {
			q = uvq_table_base_zigzag[i * 2];
			q_next = uvq_table_base_zigzag[i * 2 + 1];
			cif_iowrite32(q_next + (q << 8),
				dev->config.base_addr +
				CIF_JPE_TABLE_DATA);
		}
	}

	/* Y AC-table 0 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_HUFFAC0,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	cif_iowrite32(178, dev->config.base_addr + CIF_JPE_TAC0_LEN);
	for (i = 0; i < (178 / 2); i++) {
		cif_iowrite32(ac_luma_table_annex_k[i * 2 + 1] +
			(ac_luma_table_annex_k[i * 2] << 8),
			dev->config.base_addr +
			CIF_JPE_TABLE_DATA);
	}

	/* U/V AC-table 1 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_HUFFAC1,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	cif_iowrite32(178, dev->config.base_addr + CIF_JPE_TAC1_LEN);
	for (i = 0; i < (178 / 2); i++) {
		cif_iowrite32(ac_chroma_table_annex_k[i * 2 + 1] +
			(ac_chroma_table_annex_k[i * 2] << 8),
			dev->config.base_addr +
			CIF_JPE_TABLE_DATA);
	}

	/* Y DC-table 0 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_HUFFDC0,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	cif_iowrite32(28, dev->config.base_addr + CIF_JPE_TDC0_LEN);
	for (i = 0; i < (28 / 2); i++) {
		cif_iowrite32(dc_luma_table_annex_k[i * 2 + 1] +
			(dc_luma_table_annex_k[i * 2] << 8),
			dev->config.base_addr +
			CIF_JPE_TABLE_DATA);
	}

	/* U/V DC-table 1 programming */
	cif_iowrite32(CIF_JPE_TAB_ID_HUFFDC1,
		dev->config.base_addr + CIF_JPE_TABLE_ID);
	cif_iowrite32(28, dev->config.base_addr + CIF_JPE_TDC1_LEN);
	for (i = 0; i < (28 / 2); i++) {
		cif_iowrite32(dc_chroma_table_annex_k[i * 2 + 1] +
		(dc_chroma_table_annex_k[i * 2] << 8),
		dev->config.base_addr +
		CIF_JPE_TABLE_DATA);
	}
}

static void cif_isp10_select_jpeg_tables(
	struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	/* Selects quantization table for Y */
	cif_iowrite32(CIF_JPE_TQ_TAB0,
		dev->config.base_addr + CIF_JPE_TQ_Y_SELECT);
	/* Selects quantization table for U */
	cif_iowrite32(CIF_JPE_TQ_TAB1,
		dev->config.base_addr + CIF_JPE_TQ_U_SELECT);
	/* Selects quantization table for V */
	cif_iowrite32(CIF_JPE_TQ_TAB1,
		dev->config.base_addr + CIF_JPE_TQ_V_SELECT);
	/* Selects Huffman DC table */
	cif_iowrite32(CIF_DC_V_TABLE | CIF_DC_U_TABLE,
		dev->config.base_addr + CIF_JPE_DC_TABLE_SELECT);
	/* Selects Huffman AC table */
	cif_iowrite32(CIF_AC_V_TABLE | CIF_AC_U_TABLE,
		dev->config.base_addr + CIF_JPE_AC_TABLE_SELECT);

	cif_isp10_pltfrm_pr_dbg(NULL,
		"\n  JPE_TQ_Y_SELECT 0x%08x\n"
		"  JPE_TQ_U_SELECT 0x%08x\n"
		"  JPE_TQ_V_SELECT 0x%08x\n"
		"  JPE_DC_TABLE_SELECT 0x%08x\n"
		"  JPE_AC_TABLE_SELECT 0x%08x\n",
		cif_ioread32(dev->config.base_addr + CIF_JPE_TQ_Y_SELECT),
		cif_ioread32(dev->config.base_addr + CIF_JPE_TQ_U_SELECT),
		cif_ioread32(dev->config.base_addr + CIF_JPE_TQ_V_SELECT),
		cif_ioread32(dev->config.base_addr + CIF_JPE_DC_TABLE_SELECT),
		cif_ioread32(dev->config.base_addr + CIF_JPE_AC_TABLE_SELECT));
}

static int cif_isp10_config_img_src(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	struct isp_supplemental_sensor_mode_data sensor_mode;

	cif_isp10_pltfrm_pr_dbg(dev->dev, "\n");

	ret = cif_isp10_img_src_set_state(dev,
		CIF_ISP10_IMG_SRC_STATE_SW_STNDBY);
	if (IS_ERR_VALUE(ret))
		goto err;

	if (!dev->sp_stream.updt_cfg &&
		!dev->mp_stream.updt_cfg)
		return 0;

	ret = cif_isp10_pltfrm_g_interface_config(dev->img_src,
			&dev->config.cam_itf);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = (int)cif_isp10_img_src_ioctl(dev->img_src,
		RK_VIDIOC_SENSOR_MODE_DATA, &sensor_mode);
	if (IS_ERR_VALUE(ret)) {
		dev->img_src_exps.exp_valid_frms = 2;
	} else {
		if ((sensor_mode.exposure_valid_frame[0] < 2) ||
			(sensor_mode.exposure_valid_frame[0] > 6))
			dev->img_src_exps.exp_valid_frms = 2;
		else
			dev->img_src_exps.exp_valid_frms =
				sensor_mode.exposure_valid_frame[0];
	}
	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"cam_itf: (type: 0x%x, dphy: %d, vc: %d, nb_lanes: %d, bitrate: %d)",
		dev->config.cam_itf.type,
		dev->config.cam_itf.cfg.mipi.dphy_index,
		dev->config.cam_itf.cfg.mipi.vc,
		dev->config.cam_itf.cfg.mipi.nb_lanes,
		dev->config.cam_itf.cfg.mipi.bit_rate);
	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_isp(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	u32 h_offs;
	u32 v_offs;
	u32 yuv_seq = 0;
	u32 bpp;
	u32 isp_input_sel = 0;
	u32 isp_bayer_pat = 0;
	u32 acq_mult = 1;
	u32 irq_mask = 0;
	u32 signal = 0;
	enum cif_isp10_pix_fmt in_pix_fmt;
	struct cif_isp10_frm_fmt *output;
	struct pltfrm_cam_itf *cam_itf;

	if (dev->config.input_sel == CIF_ISP10_INP_DMA_IE) {
		dev->config.isp_config.output =
			dev->config.mi_config.dma.output;
		cifisp_disable_isp(&dev->isp_dev);
		return 0;
	} else if (dev->config.input_sel == CIF_ISP10_INP_DMA_SP) {
		cif_iowrite32AND(~CIF_ICCL_ISP_CLK,
			dev->config.base_addr + CIF_ICCL);
		cif_isp10_pltfrm_pr_dbg(NULL,
			"ISP disabled\n");
		return 0;
	}
	cif_iowrite32OR(CIF_ICCL_ISP_CLK,
		dev->config.base_addr + CIF_ICCL);

	in_pix_fmt = dev->config.isp_config.input->pix_fmt;

	output = &dev->config.isp_config.output;
	cam_itf = &dev->config.cam_itf;

	if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER(in_pix_fmt)) {
		if (!dev->config.mi_config.raw_enable) {
			output->pix_fmt = CIF_YUV422I;

			if ((dev->mp_stream.state == CIF_ISP10_STATE_READY) &&
				(dev->sp_stream.state ==
				CIF_ISP10_STATE_READY)) {
				if (dev->config.mi_config.mp.output.
					quantization !=
					dev->config.mi_config.sp.output.
					quantization) {
					cif_isp10_pltfrm_pr_err(dev->dev,
						"colorspace quantization (mp: %d, sp: %d) is not support!\n",
						dev->
						config.mi_config.mp.output.
						quantization,
						dev->
						config.mi_config.sp.output.
						quantization);
				}
			}

			if (dev->sp_stream.state == CIF_ISP10_STATE_READY) {
				output->quantization =
				dev->config.mi_config.sp.output.quantization;
			}

			if (dev->mp_stream.state == CIF_ISP10_STATE_READY) {
				output->quantization =
				dev->config.mi_config.sp.output.quantization;
			}

			cif_iowrite32(0xc,
				dev->config.base_addr + CIF_ISP_DEMOSAIC);

			if (PLTFRM_CAM_ITF_IS_BT656(dev->config.cam_itf.type)) {
				cif_iowrite32(
					CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656,
					dev->config.base_addr + CIF_ISP_CTRL);
			} else {
				cif_iowrite32(
					CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601,
					dev->config.base_addr + CIF_ISP_CTRL);
			}
		} else {
			output->pix_fmt = in_pix_fmt;
			if (PLTFRM_CAM_ITF_IS_BT656(dev->config.cam_itf.type)) {
				cif_iowrite32(
					CIF_ISP_CTRL_ISP_MODE_RAW_PICT_ITU656,
					dev->config.base_addr + CIF_ISP_CTRL);
			} else {
				cif_iowrite32(CIF_ISP_CTRL_ISP_MODE_RAW_PICT,
					dev->config.base_addr + CIF_ISP_CTRL);
			}
		}

		bpp = CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt);
		if (bpp == 8) {
			isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_8B_MSB;
		} else if (bpp == 10) {
			isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_10B_MSB;
		} else if (bpp == 12) {
			isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_12B;
		} else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"%d bits per pixel not supported\n", bpp);
			ret = -EINVAL;
			goto err;
		}
		if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_BGGR(in_pix_fmt)) {
			isp_bayer_pat = CIF_ISP_ACQ_PROP_BAYER_PAT_BGGR;
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GBRG(in_pix_fmt)) {
			isp_bayer_pat = CIF_ISP_ACQ_PROP_BAYER_PAT_GBRG;
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GRBG(in_pix_fmt)) {
			isp_bayer_pat = CIF_ISP_ACQ_PROP_BAYER_PAT_GRBG;
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_RGGB(in_pix_fmt)) {
			isp_bayer_pat = CIF_ISP_ACQ_PROP_BAYER_PAT_RGGB;
		} else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"BAYER pattern not supported\n");
			ret = -EINVAL;
			goto err;
		}
	} else if (CIF_ISP10_PIX_FMT_IS_YUV(in_pix_fmt)) {
		output->pix_fmt = in_pix_fmt;
		acq_mult = 2;
		if (dev->config.input_sel == CIF_ISP10_INP_DMA) {
			bpp = CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt);
			bpp =
				bpp * 4 /
				(4 + (CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
					in_pix_fmt) *
				CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
					in_pix_fmt) / 2));
			if (bpp == 8) {
				isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_8B_MSB;
			} else if (bpp == 10) {
				isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_10B_MSB;
			} else if (bpp == 12) {
				isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_12B;
			} else {
				cif_isp10_pltfrm_pr_err(dev->dev,
					"format %s not supported, invalid bpp %d\n",
					cif_isp10_pix_fmt_string(in_pix_fmt),
					bpp);
				ret = -EINVAL;
				goto err;
			}
			cif_iowrite32(CIF_ISP_CTRL_ISP_MODE_ITU601,
				dev->config.base_addr + CIF_ISP_CTRL);
		} else{
			if (PLTFRM_CAM_ITF_IS_MIPI(
				dev->config.cam_itf.type)) {
				isp_input_sel = CIF_ISP_ACQ_PROP_IN_SEL_12B;
				cif_iowrite32(CIF_ISP_CTRL_ISP_MODE_ITU601,
					dev->config.base_addr + CIF_ISP_CTRL);
			} else if (PLTFRM_CAM_ITF_IS_DVP(
				dev->config.cam_itf.type)) {
				if (PLTFRM_CAM_ITF_IS_BT656(
					dev->config.cam_itf.type)) {
					cif_iowrite32(
					CIF_ISP_CTRL_ISP_MODE_ITU656,
					dev->config.base_addr +
					CIF_ISP_CTRL);
				} else {
					cif_iowrite32(
					CIF_ISP_CTRL_ISP_MODE_ITU601,
					dev->config.base_addr +
					CIF_ISP_CTRL);
				}

				switch (PLTFRM_CAM_ITF_DVP_BW(
				dev->config.cam_itf.type)) {
				case 8:
					isp_input_sel =
					CIF_ISP_ACQ_PROP_IN_SEL_8B_ZERO;
					break;
				case 10:
					isp_input_sel =
					CIF_ISP_ACQ_PROP_IN_SEL_10B_ZERO;
					break;
				case 12:
					isp_input_sel =
					CIF_ISP_ACQ_PROP_IN_SEL_12B;
					break;
				default:
					cif_isp10_pltfrm_pr_err(dev->dev,
						"%s isn't support for cif isp10\n",
						cif_isp10_interface_string(
						dev->config.cam_itf.type));
					break;
				}
			} else {
				cif_isp10_pltfrm_pr_err(dev->dev,
					"%s isn't support for cif isp10\n",
					cif_isp10_interface_string(
					dev->config.cam_itf.type));
			}
			/*
			 * ISP DATA LOSS is only meaningful
			 * when input is not DMA
			 */
			irq_mask |= CIF_ISP_DATA_LOSS;
		}
		if (CIF_ISP10_PIX_FMT_YUV_IS_YC_SWAPPED(in_pix_fmt)) {
			yuv_seq = CIF_ISP_ACQ_PROP_CBYCRY;
			cif_isp10_pix_fmt_set_yc_swapped(output->pix_fmt, 0);
		} else if (CIF_ISP10_PIX_FMT_YUV_IS_UV_SWAPPED(in_pix_fmt)) {
			yuv_seq = CIF_ISP_ACQ_PROP_YCRYCB;
		} else {
			yuv_seq = CIF_ISP_ACQ_PROP_YCBYCR;
		}
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"format %s not supported\n",
			cif_isp10_pix_fmt_string(in_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	/* Set up input acquisition properties*/
	if (PLTFRM_CAM_ITF_IS_DVP(cam_itf->type)) {
		signal =
			((cam_itf->cfg.dvp.pclk == PLTFRM_CAM_SDR_POS_EDG) ?
			CIF_ISP_ACQ_PROP_POS_EDGE : CIF_ISP_ACQ_PROP_NEG_EDGE);

		if (PLTFRM_CAM_ITF_IS_BT601(cam_itf->type)) {
			signal |=
				(cam_itf->cfg.dvp.vsync ==
				PLTFRM_CAM_SIGNAL_HIGH_LEVEL) ?
				CIF_ISP_ACQ_PROP_VSYNC_HIGH :
				CIF_ISP_ACQ_PROP_VSYNC_LOW;

			signal |=
				(cam_itf->cfg.dvp.hsync ==
				PLTFRM_CAM_SIGNAL_HIGH_LEVEL) ?
				CIF_ISP_ACQ_PROP_HSYNC_HIGH :
				CIF_ISP_ACQ_PROP_HSYNC_LOW;

		} else {
			signal |= CIF_ISP_ACQ_PROP_HSYNC_HIGH |
					CIF_ISP_ACQ_PROP_VSYNC_HIGH;
		}
	} else {
		signal = CIF_ISP_ACQ_PROP_NEG_EDGE |
			      CIF_ISP_ACQ_PROP_HSYNC_HIGH |
			      CIF_ISP_ACQ_PROP_VSYNC_HIGH;
	}

	cif_iowrite32(signal |
			      yuv_seq |
			      CIF_ISP_ACQ_PROP_FIELD_SEL_ALL |
			      isp_input_sel |
			      isp_bayer_pat |
			      (0 << 20),  /* input_selection_no_app */
			      dev->config.base_addr + CIF_ISP_ACQ_PROP);
	cif_iowrite32(0,
		dev->config.base_addr + CIF_ISP_ACQ_NR_FRAMES);

	/* Acquisition Size */
	cif_iowrite32(dev->config.isp_config.input->defrect.left,
		dev->config.base_addr + CIF_ISP_ACQ_H_OFFS);
	cif_iowrite32(dev->config.isp_config.input->defrect.top,
		dev->config.base_addr + CIF_ISP_ACQ_V_OFFS);
	cif_iowrite32(
		acq_mult * dev->config.isp_config.input->defrect.width,
		dev->config.base_addr + CIF_ISP_ACQ_H_SIZE);
	cif_iowrite32(
		dev->config.isp_config.input->defrect.height,
		dev->config.base_addr + CIF_ISP_ACQ_V_SIZE);

	/* do cropping to match output aspect ratio */
	ret = cif_isp10_calc_isp_cropping(dev,
		&output->width, &output->height,
		&h_offs, &v_offs);
	if (IS_ERR_VALUE(ret))
		goto err;

	cif_iowrite32(v_offs,
		dev->config.base_addr + CIF_ISP_OUT_V_OFFS);
	cif_iowrite32(h_offs,
		dev->config.base_addr + CIF_ISP_OUT_H_OFFS);
	cif_iowrite32(output->width,
		dev->config.base_addr + CIF_ISP_OUT_H_SIZE);
	cif_iowrite32(output->height,
		dev->config.base_addr + CIF_ISP_OUT_V_SIZE);
	if (PLTFRM_CAM_ITF_INTERLACE(cam_itf->type)) {
		cif_isp10_pltfrm_pr_info(dev->dev,
			"type %s: input.size %dx%d, output.size %dx%d\n",
			cif_isp10_interface_string(cam_itf->type),
			dev->config.isp_config.input->defrect.width,
			dev->config.isp_config.input->defrect.height,
			output->width,
			output->height);
		cif_iowrite32(
			dev->config.isp_config.input->defrect.height / 2,
			dev->config.base_addr + CIF_ISP_ACQ_V_SIZE);
		cif_iowrite32(
			output->height / 2,
			dev->config.base_addr + CIF_ISP_OUT_V_SIZE);
	}

	dev->isp_dev.input_width =
		dev->config.isp_config.input->defrect.width;
	dev->isp_dev.input_height =
		dev->config.isp_config.input->defrect.height;

	/* interrupt mask */
	irq_mask |=
		CIF_ISP_FRAME |
		CIF_ISP_PIC_SIZE_ERROR |
		CIF_ISP_FRAME_IN |
		CIF_ISP_V_START;
	cif_iowrite32(irq_mask,
		dev->config.base_addr + CIF_ISP_IMSC);

	if (!dev->config.mi_config.raw_enable)
		cifisp_configure_isp(&dev->isp_dev,
			in_pix_fmt,
			output->quantization);
	else
		cifisp_disable_isp(&dev->isp_dev);

	cif_isp10_pltfrm_pr_dbg(
		dev->dev,
		"\n  ISP_CTRL 0x%08x\n"
		"  ISP_IMSC 0x%08x\n"
		"  ISP_ACQ_PROP 0x%08x\n"
		"  ISP_ACQ %dx%d@(%d,%d)\n"
		"  ISP_OUT %dx%d@(%d,%d)\n"
		"  ISP_IS %dx%d@(%d,%d)\n",
		cif_ioread32(dev->config.base_addr + CIF_ISP_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IMSC),
		cif_ioread32(dev->config.base_addr + CIF_ISP_ACQ_PROP),
		cif_ioread32(dev->config.base_addr + CIF_ISP_ACQ_H_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_ACQ_V_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_ACQ_H_OFFS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_ACQ_V_OFFS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_OUT_H_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_OUT_V_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_OUT_H_OFFS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_OUT_V_OFFS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IS_H_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IS_V_SIZE),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IS_H_OFFS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IS_V_OFFS));

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_mipi(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	u32 data_type;
	u32 mipi_ctrl;
	u32 shutdown_lanes;
	u32 i;
	enum cif_isp10_pix_fmt in_pix_fmt;

	if (!CIF_ISP10_INP_IS_MIPI(dev->config.input_sel)) {
		cif_iowrite32AND(~CIF_ICCL_MIPI_CLK,
			dev->config.base_addr + CIF_ICCL);
		cif_isp10_pltfrm_pr_dbg(NULL,
			"MIPI disabled\n");
		return 0;
	}
	cif_iowrite32OR(CIF_ICCL_MIPI_CLK,
		dev->config.base_addr + CIF_ICCL);

	in_pix_fmt = dev->config.img_src_output.frm_fmt.pix_fmt;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"input %s, vc = %d, nb_lanes = %d\n",
		cif_isp10_inp_string(dev->config.input_sel),
		dev->config.cam_itf.cfg.mipi.vc,
		dev->config.cam_itf.cfg.mipi.nb_lanes);

	if ((dev->config.cam_itf.cfg.mipi.nb_lanes == 0) ||
		(dev->config.cam_itf.cfg.mipi.nb_lanes > 4)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"invalid number (%d) of MIPI lanes, valid range is [1..4]\n",
			dev->config.cam_itf.cfg.mipi.nb_lanes);
		ret = -EINVAL;
		goto err;
	}

	shutdown_lanes = 0x00;
	for (i = 0; i < dev->config.cam_itf.cfg.mipi.nb_lanes; i++)
		shutdown_lanes |= (1 << i);

	mipi_ctrl =
		CIF_MIPI_CTRL_NUM_LANES(
			dev->config.cam_itf.cfg.mipi.nb_lanes - 1) |
		CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
		CIF_MIPI_CTRL_SHUTDOWNLANES(shutdown_lanes) |
		CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_ENA |
		CIF_MIPI_CTRL_ERR_SOT_HS_ENA |
		CIF_MIPI_CTRL_CLOCKLANE_ENA;

	cif_iowrite32(mipi_ctrl,
		dev->config.base_addr + CIF_MIPI_CTRL);

	/* mipi_dphy */
	cif_isp10_pltfrm_mipi_dphy_config(dev);

	/* Configure Data Type and Virtual Channel */
	if (CIF_ISP10_PIX_FMT_IS_YUV(in_pix_fmt)) {
		if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 12))
			data_type = CSI2_DT_YUV420_8b;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 15))
			data_type = CSI2_DT_YUV420_10b;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 4) &&
			(CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 16))
			data_type = CSI2_DT_YUV422_8b;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 4) &&
			(CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 20))
			data_type = CSI2_DT_YUV422_10b;
		else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"unsupported format %s\n",
				cif_isp10_pix_fmt_string(in_pix_fmt));
			ret = -EINVAL;
			goto err;
		}
	} else if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER(in_pix_fmt)) {
		if (CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 8) {
			data_type = CSI2_DT_RAW8;
		} else if (CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 10) {
			data_type = CSI2_DT_RAW10;
		} else if (CIF_ISP10_PIX_FMT_GET_BPP(in_pix_fmt) == 12) {
			data_type = CSI2_DT_RAW12;
		} else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"unsupported format %s\n",
				cif_isp10_pix_fmt_string(in_pix_fmt));
			ret = -EINVAL;
			goto err;
		}
	} else if (in_pix_fmt == CIF_RGB565) {
		data_type = CSI2_DT_RGB565;
	} else if (in_pix_fmt == CIF_RGB666) {
		data_type = CSI2_DT_RGB666;
	} else if (in_pix_fmt == CIF_RGB888) {
		data_type = CSI2_DT_RGB888;
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unsupported format %s\n",
			cif_isp10_pix_fmt_string(in_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	cif_iowrite32(
		CIF_MIPI_DATA_SEL_DT(data_type) |
		CIF_MIPI_DATA_SEL_VC(
			dev->config.cam_itf.cfg.mipi.vc),
		dev->config.base_addr + CIF_MIPI_IMG_DATA_SEL);

	/* Enable MIPI interrupts */
	cif_iowrite32(~0,
		dev->config.base_addr + CIF_MIPI_ICR);
	/*
	 * Disable CIF_MIPI_ERR_DPHY interrupt here temporary for
	 * isp bus may be dead when switch isp.
	 */
	cif_iowrite32(
		CIF_MIPI_FRAME_END |
		CIF_MIPI_ERR_CSI |
		CIF_MIPI_ERR_DPHY |
		CIF_MIPI_SYNC_FIFO_OVFLW(3) |
		CIF_MIPI_ADD_DATA_OVFLW,
		dev->config.base_addr + CIF_MIPI_IMSC);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MIPI_CTRL 0x%08x\n"
		"  MIPI_IMG_DATA_SEL 0x%08x\n"
		"  MIPI_STATUS 0x%08x\n"
		"  MIPI_IMSC 0x%08x\n",
		cif_ioread32(dev->config.base_addr + CIF_MIPI_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_IMG_DATA_SEL),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_STATUS),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_IMSC));

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_mi_mp(
	struct cif_isp10_device *dev)
{
	enum cif_isp10_pix_fmt out_pix_fmt =
		dev->config.mi_config.mp.output.pix_fmt;
	u32 llength =
		dev->config.mi_config.mp.llength;
	u32 width =
		dev->config.mi_config.mp.output.width;
	u32 height =
		dev->config.mi_config.mp.output.height;
	u32 writeformat = CIF_ISP10_BUFF_FMT_PLANAR;
	u32 swap_cb_cr = 0;
	u32 bpp = CIF_ISP10_PIX_FMT_GET_BPP(out_pix_fmt);
	u32 size = llength * height * bpp / 8;
	u32 mi_ctrl;

	dev->config.mi_config.mp.input =
		&dev->config.mp_config.rsz_config.output;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d, llength = %d\n",
		cif_isp10_pix_fmt_string(out_pix_fmt),
		width,
		height,
		llength);

	dev->config.mi_config.mp.y_size = size;
	dev->config.mi_config.mp.cb_size = 0;
	dev->config.mi_config.mp.cr_size = 0;
	if (CIF_ISP10_PIX_FMT_IS_YUV(out_pix_fmt)) {
		u32 num_cplanes =
			CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(out_pix_fmt);
		if (num_cplanes == 0) {
			writeformat = CIF_ISP10_BUFF_FMT_INTERLEAVED;
		} else {
			dev->config.mi_config.mp.y_size =
				(dev->config.mi_config.mp.y_size * 4) /
				(4 + (CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
					out_pix_fmt) *
				CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
					out_pix_fmt) / 2));
			dev->config.mi_config.mp.cb_size =
				size -
				dev->config.mi_config.mp.y_size;
			if (num_cplanes == 1) {
				writeformat = CIF_ISP10_BUFF_FMT_SEMIPLANAR;
			} else if (num_cplanes == 2) {
				writeformat = CIF_ISP10_BUFF_FMT_PLANAR;
				dev->config.mi_config.mp.cb_size /= 2;
			}
			/* for U<->V swapping: */
			dev->config.mi_config.mp.cr_size =
				dev->config.mi_config.mp.cb_size;
		}
		if (CIF_ISP10_PIX_FMT_YUV_IS_UV_SWAPPED(out_pix_fmt))
			swap_cb_cr = CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP;

		if (writeformat == CIF_ISP10_BUFF_FMT_SEMIPLANAR) {
			dev->config.mi_config.mp.cb_offs =
			    dev->config.mi_config.mp.y_size;
			dev->config.mi_config.mp.cr_offs =
			    dev->config.mi_config.mp.cb_offs;
		} else if (writeformat == CIF_ISP10_BUFF_FMT_PLANAR) {
			if (swap_cb_cr) {
				swap_cb_cr = 0;
				dev->config.mi_config.mp.cr_offs =
					dev->config.mi_config.mp.y_size;
				dev->config.mi_config.mp.cb_offs =
					dev->config.mi_config.mp.cr_offs +
					dev->config.mi_config.mp.cr_size;
			} else {
				dev->config.mi_config.mp.cb_offs =
					dev->config.mi_config.mp.y_size;
				dev->config.mi_config.mp.cr_offs =
					dev->config.mi_config.mp.cb_offs +
					dev->config.mi_config.mp.cb_size;
			}
		}
	} else if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER(out_pix_fmt)) {
		if (CIF_ISP10_PIX_FMT_GET_BPP(out_pix_fmt) > 8) {
			writeformat = CIF_ISP10_BUFF_FMT_RAW12;
			dev->config.mi_config.mp.y_size = width * height * 2;
		} else {
			writeformat = CIF_ISP10_BUFF_FMT_RAW8;
			dev->config.mi_config.mp.y_size = width * height;
		}
		dev->config.mi_config.mp.cb_offs = 0x00;
		dev->config.mi_config.mp.cr_offs = 0x00;
		dev->config.mi_config.mp.cb_size = 0x00;
		dev->config.mi_config.mp.cr_size = 0x00;
	}

	cif_iowrite32_verify(dev->config.mi_config.mp.y_size,
		dev->config.base_addr + CIF_MI_MP_Y_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(dev->config.mi_config.mp.cb_size,
		dev->config.base_addr + CIF_MI_MP_CB_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(dev->config.mi_config.mp.cr_size,
		dev->config.base_addr + CIF_MI_MP_CR_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32OR_verify(CIF_MI_MP_FRAME,
		dev->config.base_addr +
		CIF_MI_IMSC, ~0);

	if (swap_cb_cr) {
		cif_iowrite32OR(swap_cb_cr,
			dev->config.base_addr + CIF_MI_XTD_FORMAT_CTRL);
	}

	mi_ctrl = cif_ioread32(dev->config.base_addr + CIF_MI_CTRL) |
		CIF_MI_CTRL_MP_WRITE_FMT(writeformat) |
		CIF_MI_CTRL_BURST_LEN_LUM_16 |
		CIF_MI_CTRL_BURST_LEN_CHROM_16 |
		CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN |
		CIF_MI_MP_AUTOUPDATE_ENABLE;

	cif_iowrite32_verify(mi_ctrl,
		dev->config.base_addr + CIF_MI_CTRL, ~0);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MI_CTRL 0x%08x\n"
		"  MI_STATUS 0x%08x\n"
		"  MI_MP_Y_SIZE %d\n"
		"  MI_MP_CB_SIZE %d\n"
		"  MI_MP_CR_SIZE %d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_STATUS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CB_SIZE_INIT),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_CR_SIZE_INIT));

	return 0;
}

static int cif_isp10_config_mi_sp(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	enum cif_isp10_pix_fmt out_pix_fmt =
		dev->config.mi_config.sp.output.pix_fmt;
	enum cif_isp10_pix_fmt in_pix_fmt =
		dev->config.sp_config.rsz_config.output.pix_fmt;
	u32 llength =
		dev->config.mi_config.sp.llength;
	u32 width =
		dev->config.mi_config.sp.output.width;
	u32 height =
		dev->config.mi_config.sp.output.height;
	u32 writeformat = CIF_ISP10_BUFF_FMT_PLANAR;
	u32 swap_cb_cr = 0;
	u32 bpp = CIF_ISP10_PIX_FMT_GET_BPP(out_pix_fmt);
	u32 size = llength * height * bpp / 8;
	u32 input_format = 0;
	u32 output_format;
	u32 burst_len;
	u32 mi_ctrl;

	dev->config.mi_config.sp.input =
		&dev->config.sp_config.rsz_config.output;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d, llength = %d\n",
		cif_isp10_pix_fmt_string(out_pix_fmt),
		width,
		height,
		llength);
	if (PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type)) {
		llength = 2 * llength;
		height = height / 2;
		dev->config.mi_config.sp.vir_len_offset =
			width;
	}
	if (llength != width) {
		if (!(width % 128))
			burst_len = CIF_MI_CTRL_BURST_LEN_LUM_16 |
				CIF_MI_CTRL_BURST_LEN_CHROM_16;
		else if (!(width % 64))
			burst_len = CIF_MI_CTRL_BURST_LEN_LUM_8 |
				CIF_MI_CTRL_BURST_LEN_CHROM_8;
		else
			burst_len = CIF_MI_CTRL_BURST_LEN_LUM_4 |
				CIF_MI_CTRL_BURST_LEN_CHROM_4;

		if (width % 32)
			cif_isp10_pltfrm_pr_warn(dev->dev,
				"The width should be aligned to 32\n");
	} else {
		burst_len = CIF_MI_CTRL_BURST_LEN_LUM_16 |
			CIF_MI_CTRL_BURST_LEN_CHROM_16;
	}

	if (!CIF_ISP10_PIX_FMT_IS_YUV(in_pix_fmt)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unsupported format %s (must be YUV)\n",
			cif_isp10_pix_fmt_string(in_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	dev->config.mi_config.sp.y_size = size;
	dev->config.mi_config.sp.cb_size = 0;
	dev->config.mi_config.sp.cr_size = 0;
	if (CIF_ISP10_PIX_FMT_IS_YUV(out_pix_fmt)) {
		u32 num_cplanes =
			CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(out_pix_fmt);
		if (num_cplanes == 0) {
			writeformat = CIF_ISP10_BUFF_FMT_INTERLEAVED;
		} else {
			dev->config.mi_config.sp.y_size =
				(dev->config.mi_config.sp.y_size * 4) /
				(4 + (CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
					out_pix_fmt) *
				CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
					out_pix_fmt) / 2));
			dev->config.mi_config.sp.cb_size =
				size -
				dev->config.mi_config.sp.y_size;
			if (num_cplanes == 1) {
				writeformat = CIF_ISP10_BUFF_FMT_SEMIPLANAR;
			} else if (num_cplanes == 2) {
				writeformat = CIF_ISP10_BUFF_FMT_PLANAR;
				dev->config.mi_config.sp.cb_size /= 2;
			}
			/* for U<->V swapping: */
			dev->config.mi_config.sp.cr_size =
				dev->config.mi_config.sp.cb_size;
		}
		if (CIF_ISP10_PIX_FMT_YUV_IS_UV_SWAPPED(out_pix_fmt))
			swap_cb_cr = CIF_MI_XTD_FMT_CTRL_SP_CB_CR_SWAP;

		if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 0) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 0))
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_YUV400;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 2)) {
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_YUV420;
			dev->config.mi_config.sp.vir_len_offset =
				width;
		} else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 4)) {
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_YUV422;
			dev->config.mi_config.sp.vir_len_offset =
				width * 2;
		} else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 4) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 4))
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_YUV444;
		else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"unsupported YUV output format %s\n",
				cif_isp10_pix_fmt_string(out_pix_fmt));
			ret = -EINVAL;
			goto err;
		}
	} else if (CIF_ISP10_PIX_FMT_IS_RGB(out_pix_fmt)) {
		if (out_pix_fmt == CIF_RGB565) {
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_RGB565;
		} else if (out_pix_fmt == CIF_RGB666) {
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_RGB666;
		} else if (out_pix_fmt == CIF_RGB888) {
			output_format = CIF_MI_CTRL_SP_OUTPUT_FMT_RGB888;
		} else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"unsupported RGB output format %s\n",
				cif_isp10_pix_fmt_string(out_pix_fmt));
			ret = -EINVAL;
			goto err;
		}
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unsupported output format %s\n",
			cif_isp10_pix_fmt_string(out_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 0) &&
		(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 0))
		input_format = CIF_MI_CTRL_SP_INPUT_FMT_YUV400;
	else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
		(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 2))
		input_format = CIF_MI_CTRL_SP_INPUT_FMT_YUV420;
	else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 2) &&
		(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 4))
		input_format = CIF_MI_CTRL_SP_INPUT_FMT_YUV422;
	else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(in_pix_fmt) == 4) &&
		(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(in_pix_fmt) == 4))
		input_format = CIF_MI_CTRL_SP_INPUT_FMT_YUV444;
	else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unsupported YUV input format %s\n",
			cif_isp10_pix_fmt_string(in_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	if (writeformat == CIF_ISP10_BUFF_FMT_SEMIPLANAR) {
		dev->config.mi_config.sp.cb_offs =
		    dev->config.mi_config.sp.y_size;
		dev->config.mi_config.sp.cr_offs =
		    dev->config.mi_config.sp.cb_offs;
	} else if (writeformat == CIF_ISP10_BUFF_FMT_PLANAR) {
		if (swap_cb_cr) {
			swap_cb_cr = 0;
			dev->config.mi_config.sp.cr_offs =
				dev->config.mi_config.sp.y_size;
			dev->config.mi_config.sp.cb_offs =
				dev->config.mi_config.sp.cr_offs +
				dev->config.mi_config.sp.cr_size;
		} else {
			dev->config.mi_config.sp.cb_offs =
				dev->config.mi_config.sp.y_size;
			dev->config.mi_config.sp.cr_offs =
				dev->config.mi_config.sp.cb_offs +
				dev->config.mi_config.sp.cb_size;
		}
	}

	cif_iowrite32_verify(dev->config.mi_config.sp.y_size,
		dev->config.base_addr + CIF_MI_SP_Y_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(dev->config.mi_config.sp.y_size,
		dev->config.base_addr + CIF_MI_SP_Y_PIC_SIZE,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(dev->config.mi_config.sp.cb_size,
		dev->config.base_addr + CIF_MI_SP_CB_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(dev->config.mi_config.sp.cr_size,
		dev->config.base_addr + CIF_MI_SP_CR_SIZE_INIT,
		CIF_MI_ADDR_SIZE_ALIGN_MASK);
	cif_iowrite32_verify(width,
		dev->config.base_addr + CIF_MI_SP_Y_PIC_WIDTH, ~0x3);
	cif_iowrite32_verify(height,
		dev->config.base_addr + CIF_MI_SP_Y_PIC_HEIGHT, ~0x3);
	cif_iowrite32_verify(llength,
		dev->config.base_addr + CIF_MI_SP_Y_LLENGTH, ~0x3);
	cif_iowrite32OR_verify(CIF_MI_SP_FRAME,
		dev->config.base_addr +
		CIF_MI_IMSC, ~0);

	if (swap_cb_cr) {
		cif_iowrite32OR(swap_cb_cr,
			dev->config.base_addr + CIF_MI_XTD_FORMAT_CTRL);
	}

	mi_ctrl = cif_ioread32(dev->config.base_addr + CIF_MI_CTRL) |
		CIF_MI_CTRL_SP_WRITE_FMT(writeformat) |
		input_format |
		output_format |
		burst_len |
		CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN |
		CIF_MI_SP_AUTOUPDATE_ENABLE;
	cif_iowrite32_verify(mi_ctrl,
		dev->config.base_addr + CIF_MI_CTRL, ~0);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MI_CTRL 0x%08x\n"
		"  MI_STATUS 0x%08x\n"
		"  MI_SP_Y_SIZE %d\n"
		"  MI_SP_CB_SIZE %d\n"
		"  MI_SP_CR_SIZE %d\n"
		"  MI_SP_PIC_WIDTH %d\n"
		"  MI_SP_PIC_HEIGHT %d\n"
		"  MI_SP_PIC_LLENGTH %d\n"
		"  MI_SP_PIC_SIZE %d\n",
		cif_ioread32(dev->config.base_addr + CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_MI_STATUS),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_Y_SIZE_INIT),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_CB_SIZE_INIT),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_CR_SIZE_INIT),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_Y_PIC_WIDTH),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_Y_PIC_HEIGHT),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_Y_LLENGTH),
		cif_ioread32(dev->config.base_addr + CIF_MI_SP_Y_PIC_SIZE));

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_mi_dma(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	enum cif_isp10_pix_fmt out_pix_fmt =
		dev->config.mi_config.dma.output.pix_fmt;
	u32 llength =
		dev->config.mi_config.dma.llength;
	u32 width =
		dev->config.mi_config.dma.output.width;
	u32 height =
		dev->config.mi_config.dma.output.height;
	u32 readformat = CIF_ISP10_BUFF_FMT_PLANAR;
	u32 bpp = CIF_ISP10_PIX_FMT_GET_BPP(out_pix_fmt);
	u32 size = llength * height * bpp / 8;
	u32 output_format;
	u32 mi_ctrl;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d, llength = %d\n",
		cif_isp10_pix_fmt_string(out_pix_fmt),
		width,
		height,
		llength);

	dev->config.mi_config.dma.y_size = size;
	dev->config.mi_config.dma.cb_size = 0;
	dev->config.mi_config.dma.cr_size = 0;
	if (CIF_ISP10_PIX_FMT_IS_YUV(out_pix_fmt)) {
		u32 num_cplanes =
			CIF_ISP10_PIX_FMT_YUV_GET_NUM_CPLANES(out_pix_fmt);
		if (num_cplanes == 0) {
			readformat = CIF_ISP10_BUFF_FMT_INTERLEAVED;
		} else {
			dev->config.mi_config.dma.y_size =
				(dev->config.mi_config.dma.y_size * 4) /
				 (4 + (CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
					out_pix_fmt) *
				CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
					out_pix_fmt) / 2));
			dev->config.mi_config.dma.cb_size =
				size -
				dev->config.mi_config.dma.y_size;
			if (num_cplanes == 1) {
				readformat = CIF_ISP10_BUFF_FMT_SEMIPLANAR;
			} else if (num_cplanes == 2) {
				readformat = CIF_ISP10_BUFF_FMT_PLANAR;
				dev->config.mi_config.dma.cb_size /= 2;
			}
			/* for U<->V swapping: */
			dev->config.mi_config.dma.cr_size =
				dev->config.mi_config.dma.cb_size;
		}

		if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 0) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 0))
			output_format = CIF_MI_DMA_CTRL_FMT_YUV400;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 2))
			output_format = CIF_MI_DMA_CTRL_FMT_YUV420;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 2) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 4))
			output_format = CIF_MI_DMA_CTRL_FMT_YUV422;
		else if ((CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(out_pix_fmt) == 4) &&
			(CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(out_pix_fmt) == 4))
			output_format = CIF_MI_DMA_CTRL_FMT_YUV444;
		else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"unsupported YUV output format %s\n",
				cif_isp10_pix_fmt_string(out_pix_fmt));
			ret = -EINVAL;
			goto err;
		}
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unsupported output format %s\n",
			cif_isp10_pix_fmt_string(out_pix_fmt));
		ret = -EINVAL;
		goto err;
	}

	if (readformat == CIF_ISP10_BUFF_FMT_SEMIPLANAR) {
		dev->config.mi_config.dma.cb_offs =
		    dev->config.mi_config.dma.y_size;
		dev->config.mi_config.dma.cr_offs =
		    dev->config.mi_config.dma.cb_offs;
	} else if (readformat == CIF_ISP10_BUFF_FMT_PLANAR) {
		dev->config.mi_config.dma.cb_offs =
			dev->config.mi_config.dma.y_size;
		dev->config.mi_config.dma.cr_offs =
			dev->config.mi_config.dma.cb_offs +
			dev->config.mi_config.dma.cb_size;
	}

	cif_iowrite32_verify(dev->config.mi_config.dma.y_size,
		dev->config.base_addr + CIF_MI_DMA_Y_PIC_SIZE, ~0x3);
	cif_iowrite32_verify(width,
		dev->config.base_addr + CIF_MI_DMA_Y_PIC_WIDTH, ~0x3);
	cif_iowrite32_verify(llength,
		dev->config.base_addr + CIF_MI_DMA_Y_LLENGTH, ~0x3);

	mi_ctrl = cif_ioread32(dev->config.base_addr + CIF_MI_DMA_CTRL) |
		CIF_MI_DMA_CTRL_READ_FMT(readformat) |
		output_format |
		CIF_MI_DMA_CTRL_BURST_LEN_LUM_64 |
		CIF_MI_DMA_CTRL_BURST_LEN_CHROM_64;
	cif_iowrite32_verify(mi_ctrl,
		dev->config.base_addr + CIF_MI_DMA_CTRL, ~0);

	cif_iowrite32OR_verify(CIF_MI_DMA_READY,
		dev->config.base_addr + CIF_MI_IMSC, ~0);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MI_DMA_CTRL 0x%08x\n"
		"  MI_DMA_STATUS 0x%08x\n"
		"  MI_DMA_Y_PIC_WIDTH %d\n"
		"  MI_DMA_Y_LLENGTH %d\n"
		"  MI_DMA_Y_PIC_SIZE %d\n"
		"  MI_DMA_Y_PIC_START_AD %d\n"
		"  MI_DMA_CB_PIC_START_AD %d\n"
		"  MI_DMA_CR_PIC_START_AD %d\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_STATUS),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_Y_PIC_WIDTH),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_Y_LLENGTH),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_Y_PIC_SIZE),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_Y_PIC_START_AD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_CB_PIC_START_AD),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_CR_PIC_START_AD));

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_jpeg_enc(
	struct cif_isp10_device *dev)
{
	struct cif_isp10_frm_fmt *inp_fmt =
		&dev->config.mp_config.rsz_config.output;
	dev->config.jpeg_config.input = inp_fmt;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"%s %dx%d\n",
		cif_isp10_pix_fmt_string(inp_fmt->pix_fmt),
		inp_fmt->width, inp_fmt->height);

	/*
	 * Reset JPEG-Encoder. In contrast to other software
	 * resets this triggers the modules asynchronous reset
	 * resulting in loss of all data
	 */
	cif_iowrite32OR(CIF_IRCL_JPEG_SW_RST,
		dev->config.base_addr + CIF_IRCL);
	cif_iowrite32AND(~CIF_IRCL_JPEG_SW_RST,
		dev->config.base_addr + CIF_IRCL);

	cif_iowrite32(CIF_JPE_ERROR_MASK,
		dev->config.base_addr + CIF_JPE_ERROR_IMSC);

	/* Set configuration for the Jpeg capturing */
	cif_iowrite32(inp_fmt->width,
		dev->config.base_addr + CIF_JPE_ENC_HSIZE);
	cif_iowrite32(inp_fmt->height,
		dev->config.base_addr + CIF_JPE_ENC_VSIZE);

	if (CIF_ISP10_INP_IS_DMA(dev->config.input_sel) ||
		!CIF_ISP10_PIX_FMT_IS_RAW_BAYER(
		dev->config.isp_config.input->pix_fmt)) {
		/*
		 * upscaling of BT601 color space to full range 0..255
		 * TODO: DMA or YUV sensor input in full range.
		 */
		cif_iowrite32(CIF_JPE_LUM_SCALE_ENABLE,
			dev->config.base_addr + CIF_JPE_Y_SCALE_EN);
		cif_iowrite32(CIF_JPE_CHROM_SCALE_ENABLE,
			dev->config.base_addr + CIF_JPE_CBCR_SCALE_EN);
	}

	switch (inp_fmt->pix_fmt) {
	case CIF_YUV422I:
	case CIF_YVU422I:
	case CIF_YUV422SP:
	case CIF_YVU422SP:
	case CIF_YUV422P:
	case CIF_YVU422P:
		cif_iowrite32(CIF_JPE_PIC_FORMAT_YUV422,
			dev->config.base_addr + CIF_JPE_PIC_FORMAT);
		break;
	case CIF_YUV400:
	case CIF_YVU400:
		cif_iowrite32(CIF_JPE_PIC_FORMAT_YUV400,
			dev->config.base_addr + CIF_JPE_PIC_FORMAT);
		break;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"format %s (%.8x) not supported as input for JPEG encoder\n",
			cif_isp10_pix_fmt_string(inp_fmt->pix_fmt),
			inp_fmt->pix_fmt);
		WARN_ON(1);
		break;
	}

	/*
	 * Set to normal operation (wait for encoded image data
	 * to fill output buffer)
	 */
	cif_iowrite32(0, dev->config.base_addr + CIF_JPE_TABLE_FLUSH);

	/*
	 *  CIF Spec 4.7
	 *  3.14 JPEG Encoder Programming
	 *  Do not forget to re-program all AC and DC tables
	 *  after system reset as well as after
	 *  module software reset because after any reset
	 *  the internal RAM is filled with FFH which
	 *  is an illegal symbol. This filling takes
	 *  approximately 400 clock cycles. So do not start
	 *  any table programming during the first 400 clock
	 *  cycles after reset is de-asserted.
	 *  Note: depends on CIF clock setting
	 *  400 clock cycles at 312 Mhz CIF clock-> 1.3 us
	 *  400 clock cycles at 208 Mhz CIF clock-> 1.93 us
	 *  -> 2us ok for both
	 */
	udelay(2);

	/* Program JPEG tables */
	cif_isp10_program_jpeg_tables(dev);
	/* Select JPEG tables */
	cif_isp10_select_jpeg_tables(dev);

	switch (dev->config.jpeg_config.header) {
	case CIF_ISP10_JPEG_HEADER_JFIF:
		cif_isp10_pltfrm_pr_dbg(NULL,
			"generate JFIF header\n");
		cif_iowrite32(CIF_JPE_HEADER_MODE_JFIF,
			dev->config.base_addr +
			CIF_JPE_HEADER_MODE);
		break;
	case CIF_ISP10_JPEG_HEADER_NONE:
		cif_isp10_pltfrm_pr_dbg(NULL,
			"generate no JPEG header\n");
		cif_iowrite32(CIF_JPE_HEADER_MODE_NOAPPN,
			dev->config.base_addr +
			CIF_JPE_HEADER_MODE);
		break;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupport JPEG header type %d\n",
			dev->config.jpeg_config.header);
		WARN_ON(1);
		break;
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  JPE_PIC_FORMAT 0x%08x\n"
		"  JPE_ENC_HSIZE %d\n"
		"  JPE_ENC_VSIZE %d\n"
		"  JPE_Y_SCALE_EN 0x%08x\n"
		"  JPE_CBCR_SCALE_EN 0x%08x\n"
		"  JPE_ERROR_RIS 0x%08x\n"
		"  JPE_ERROR_IMSC 0x%08x\n"
		"  JPE_STATUS_RIS 0x%08x\n"
		"  JPE_STATUS_IMSC 0x%08x\n"
		"  JPE_DEBUG 0x%08x\n",
		cif_ioread32(dev->config.base_addr + CIF_JPE_PIC_FORMAT),
		cif_ioread32(dev->config.base_addr + CIF_JPE_ENC_HSIZE),
		cif_ioread32(dev->config.base_addr + CIF_JPE_ENC_VSIZE),
		cif_ioread32(dev->config.base_addr + CIF_JPE_Y_SCALE_EN),
		cif_ioread32(dev->config.base_addr + CIF_JPE_CBCR_SCALE_EN),
		cif_ioread32(dev->config.base_addr + CIF_JPE_ERROR_RIS),
		cif_ioread32(dev->config.base_addr + CIF_JPE_ERROR_IMSC),
		cif_ioread32(dev->config.base_addr + CIF_JPE_STATUS_RIS),
		cif_ioread32(dev->config.base_addr + CIF_JPE_STATUS_IMSC),
		cif_ioread32(dev->config.base_addr + CIF_JPE_DEBUG));

	return 0;
}

static int cif_isp10_config_path(
	struct cif_isp10_device *dev,
	u32 stream_ids)
{
	u32 dpcl = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev, "\n");

	/* if_sel */
	if (dev->config.input_sel == CIF_ISP10_INP_DMA) {
		dpcl |= CIF_VI_DPCL_DMA_SW_ISP;
	}	else if (dev->config.input_sel == CIF_ISP10_INP_DMA_IE) {
		dpcl |= CIF_VI_DPCL_DMA_IE_MUX_DMA |
			CIF_VI_DPCL_DMA_SW_IE;
	} else if (dev->config.input_sel == CIF_ISP10_INP_DMA_SP) {
		dpcl |= CIF_VI_DPCL_DMA_SP_MUX_DMA;
	} else {
		if (PLTFRM_CAM_ITF_IS_DVP(dev->config.cam_itf.type)) {
			dpcl |= CIF_VI_DPCL_IF_SEL_PARALLEL;
		} else if (PLTFRM_CAM_ITF_IS_MIPI(dev->config.cam_itf.type)) {
			dpcl |= CIF_VI_DPCL_IF_SEL_MIPI;
		} else {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"Sensor Interface: 0x%x isn't support\n",
				dev->config.cam_itf.type);
			return -EINVAL;
		}
	}

	/* chan_mode */
	if (stream_ids & CIF_ISP10_STREAM_SP)
		dpcl |= CIF_VI_DPCL_CHAN_MODE_SP;

	if ((stream_ids & CIF_ISP10_STREAM_MP) &&
		!(dev->config.input_sel == CIF_ISP10_INP_DMA_SP)) {
		dpcl |= CIF_VI_DPCL_CHAN_MODE_MP;
		/* mp_dmux */
		if (dev->config.jpeg_config.enable)
			dpcl |= CIF_VI_DPCL_MP_MUX_MRSZ_JPEG;
		else
			dpcl |= CIF_VI_DPCL_MP_MUX_MRSZ_MI;
	}

	cif_iowrite32(dpcl,
		dev->config.base_addr + CIF_VI_DPCL);

	cif_isp10_pltfrm_pr_dbg(dev->dev, "CIF_DPCL 0x%08x\n", dpcl);

	return 0;
}

int cif_isp10_config_dcrop(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	bool async)
{
	unsigned int dc_ctrl = cif_ioread32(
						dev->config.base_addr +
						CIF_DUAL_CROP_CTRL);

	if (stream_id == CIF_ISP10_STREAM_MP) {
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_M_H_OFFS);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_M_V_OFFS);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_M_H_SIZE);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_M_V_SIZE);

		dc_ctrl |= CIF_DUAL_CROP_MP_MODE_BYPASS;
		if (async)
			dc_ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
		else
			dc_ctrl |= CIF_DUAL_CROP_CFG_UPD;

		cif_iowrite32(dc_ctrl,
			dev->config.base_addr + CIF_DUAL_CROP_CTRL);
	} else if (stream_id == CIF_ISP10_STREAM_SP) {
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_S_H_OFFS);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_S_V_OFFS);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_S_H_SIZE);
		cif_iowrite32(0, dev->config.base_addr +
		CIF_DUAL_CROP_S_V_SIZE);

		dc_ctrl |= CIF_DUAL_CROP_MP_MODE_BYPASS;
		if (async)
			dc_ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
		else
			dc_ctrl |= CIF_DUAL_CROP_CFG_UPD;

		cif_iowrite32(dc_ctrl,
			dev->config.base_addr + CIF_DUAL_CROP_CTRL);
	}

	return 0;
}

int cif_isp10_config_rsz(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	bool async)
{
	int ret;
	u32 i;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR scale_h_y_addr =
		dev->config.base_addr;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR scale_h_cr_addr =
		dev->config.base_addr;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR scale_h_cb_addr =
		dev->config.base_addr;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR scale_v_y_addr =
		dev->config.base_addr;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR scale_v_c_addr =
		dev->config.base_addr;
	CIF_ISP10_PLTFRM_MEM_IO_ADDR rsz_ctrl_addr =
		dev->config.base_addr;
	struct cif_isp10_frm_fmt *rsz_input;
	struct cif_isp10_frm_fmt *rsz_output;
	struct cif_isp10_frm_fmt *mi_output;
	u32 rsz_ctrl;
	u32 input_width_y;
	u32 output_width_y;
	u32 input_height_y;
	u32 output_height_y;
	u32 input_width_c;
	u32 output_width_c;
	u32 input_height_c;
	u32 output_height_c;
	u32 scale_h_c;

	if (stream_id == CIF_ISP10_STREAM_MP) {
		rsz_ctrl_addr += CIF_MRSZ_CTRL;
		scale_h_y_addr += CIF_MRSZ_SCALE_HY;
		scale_v_y_addr += CIF_MRSZ_SCALE_VY;
		scale_h_cb_addr += CIF_MRSZ_SCALE_HCB;
		scale_h_cr_addr += CIF_MRSZ_SCALE_HCR;
		scale_v_c_addr += CIF_MRSZ_SCALE_VC;
		dev->config.mp_config.rsz_config.input =
			&dev->config.isp_config.output;
		rsz_input = dev->config.mp_config.rsz_config.input;
		rsz_output = &dev->config.mp_config.rsz_config.output;
		mi_output = &dev->config.mi_config.mp.output;
		/* No phase offset */
		cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_PHASE_HY);
		cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_PHASE_HC);
		cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_PHASE_VY);
		cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_PHASE_VC);
		/* Linear interpolation */
		for (i = 0; i < 64; i++) {
			cif_iowrite32(i,
				dev->config.base_addr +
					CIF_MRSZ_SCALE_LUT_ADDR);
			cif_iowrite32(i,
				dev->config.base_addr +
					CIF_MRSZ_SCALE_LUT);
		}
	} else {
		rsz_ctrl_addr += CIF_SRSZ_CTRL;
		scale_h_y_addr += CIF_SRSZ_SCALE_HY;
		scale_v_y_addr += CIF_SRSZ_SCALE_VY;
		scale_h_cb_addr += CIF_SRSZ_SCALE_HCB;
		scale_h_cr_addr += CIF_SRSZ_SCALE_HCR;
		scale_v_c_addr += CIF_SRSZ_SCALE_VC;
		if (dev->config.input_sel == CIF_ISP10_INP_DMA_SP)
			dev->config.sp_config.rsz_config.input =
				&dev->config.mi_config.dma.output;
		else
			dev->config.sp_config.rsz_config.input =
				&dev->config.isp_config.output;

		rsz_input = dev->config.sp_config.rsz_config.input;
		rsz_output = &dev->config.sp_config.rsz_config.output;
		mi_output = &dev->config.mi_config.sp.output;
		/* No phase offset */
		cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_PHASE_HY);
		cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_PHASE_HC);
		cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_PHASE_VY);
		cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_PHASE_VC);
		/* Linear interpolation */
		for (i = 0; i < 64; i++) {
			cif_iowrite32(i,
				dev->config.base_addr +
					CIF_SRSZ_SCALE_LUT_ADDR);
			cif_iowrite32(i,
				dev->config.base_addr +
					CIF_SRSZ_SCALE_LUT);
		}
	}

	/* set RSZ input and output */
	rsz_output->width = mi_output->width;
	rsz_output->height = mi_output->height;
	rsz_output->pix_fmt = rsz_input->pix_fmt;
	if (CIF_ISP10_PIX_FMT_IS_YUV(mi_output->pix_fmt)) {
		cif_isp10_pix_fmt_set_y_subs(
			rsz_output->pix_fmt,
			CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(mi_output->pix_fmt));
		cif_isp10_pix_fmt_set_x_subs(
			rsz_output->pix_fmt,
			CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(mi_output->pix_fmt));
		cif_isp10_pix_fmt_set_bpp(
			rsz_output->pix_fmt,
			CIF_ISP10_PIX_FMT_GET_BPP(mi_output->pix_fmt));
	} else if (CIF_ISP10_PIX_FMT_IS_JPEG(mi_output->pix_fmt)) {
		cif_isp10_pix_fmt_set_y_subs(
			rsz_output->pix_fmt, 4);
		cif_isp10_pix_fmt_set_x_subs(
			rsz_output->pix_fmt, 2);
		cif_isp10_pix_fmt_set_bpp(
			rsz_output->pix_fmt, 16);
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %s %dx%d -> %s %dx%d\n",
		cif_isp10_stream_id_string(stream_id),
		cif_isp10_pix_fmt_string(rsz_input->pix_fmt),
		rsz_input->width,
		rsz_input->height,
		cif_isp10_pix_fmt_string(rsz_output->pix_fmt),
		rsz_output->width,
		rsz_output->height);

	/* set input and output sizes for scale calculation */
	input_width_y = rsz_input->width;
	output_width_y = rsz_output->width;
	input_height_y = rsz_input->height;
	output_height_y = rsz_output->height;
	input_width_c = input_width_y;
	output_width_c = output_width_y;
	input_height_c = input_height_y;
	output_height_c = output_height_y;

	if (CIF_ISP10_PIX_FMT_IS_YUV(rsz_output->pix_fmt)) {
		input_width_c = (input_width_c *
			CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
				rsz_input->pix_fmt)) / 4;
		input_height_c = (input_height_c *
			CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
				rsz_input->pix_fmt)) / 4;
		output_width_c = (output_width_c *
			CIF_ISP10_PIX_FMT_YUV_GET_X_SUBS(
				rsz_output->pix_fmt)) / 4;
		output_height_c = (output_height_c *
			CIF_ISP10_PIX_FMT_YUV_GET_Y_SUBS(
				rsz_output->pix_fmt)) / 4;

		cif_isp10_pltfrm_pr_dbg(NULL,
			"chroma scaling %dx%d -> %dx%d\n",
			input_width_c, input_height_c,
			output_width_c, output_height_c);

		if (((input_width_c == 0) && (output_width_c > 0)) ||
			((input_height_c == 0) && (output_height_c > 0))) {
			cif_isp10_pltfrm_pr_err(NULL,
				"input is black and white, cannot output colour\n");
			ret = -EINVAL;
			goto err;
		}
	} else {
		if ((input_width_y != output_width_y) ||
			(input_height_y != output_height_y)) {
			cif_isp10_pltfrm_pr_err(NULL,
				"%dx%d -> %dx%d isn't support, can only scale YUV input\n",
				input_width_y, input_height_y,
				output_width_y, output_height_y);
			ret = -EINVAL;
			goto err;
		}
	}

	/* calculate and set scale */
	rsz_ctrl = 0;
	if (input_width_y < output_width_y) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE |
			CIF_RSZ_CTRL_SCALE_HY_UP;
		cif_iowrite32(
			DIV_TRUNCATE((input_width_y - 1)
			* CIF_RSZ_SCALER_BYPASS,
			output_width_y - 1),
			scale_h_y_addr);
	} else if (input_width_y > output_width_y) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		cif_iowrite32(
			DIV_TRUNCATE((output_width_y - 1)
			* CIF_RSZ_SCALER_BYPASS,
			input_width_y - 1) + 1,
			scale_h_y_addr);
	}
	if (input_width_c < output_width_c) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE |
			CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_h_c = DIV_TRUNCATE((input_width_c - 1)
		* CIF_RSZ_SCALER_BYPASS,
		output_width_c - 1);
		cif_iowrite32(scale_h_c, scale_h_cb_addr);
		cif_iowrite32(scale_h_c, scale_h_cr_addr);
	} else if (input_width_c > output_width_c) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_h_c = DIV_TRUNCATE((output_width_c - 1)
		* CIF_RSZ_SCALER_BYPASS,
		input_width_c - 1) + 1;
		cif_iowrite32(scale_h_c, scale_h_cb_addr);
		cif_iowrite32(scale_h_c, scale_h_cr_addr);
	}

	if (input_height_y < output_height_y) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE |
			CIF_RSZ_CTRL_SCALE_VY_UP;
		cif_iowrite32(
			DIV_TRUNCATE((input_height_y - 1)
			* CIF_RSZ_SCALER_BYPASS,
			output_height_y - 1),
			scale_v_y_addr);
	} else if (input_height_y > output_height_y) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		cif_iowrite32(
			DIV_TRUNCATE((output_height_y - 1)
			* CIF_RSZ_SCALER_BYPASS,
			input_height_y - 1) + 1,
			scale_v_y_addr);
	}

	if (input_height_c < output_height_c) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE |
			CIF_RSZ_CTRL_SCALE_VC_UP;
		cif_iowrite32(
			DIV_TRUNCATE((input_height_c - 1)
			* CIF_RSZ_SCALER_BYPASS,
			output_height_c - 1),
			scale_v_c_addr);
	} else if (input_height_c > output_height_c) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		cif_iowrite32(
			DIV_TRUNCATE((output_height_c - 1)
			* CIF_RSZ_SCALER_BYPASS,
			input_height_c - 1) + 1,
			scale_v_c_addr);
	}

	cif_iowrite32(rsz_ctrl, rsz_ctrl_addr);

	if (stream_id == CIF_ISP10_STREAM_MP) {
		if (async)
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_MRSZ_CTRL);
		dev->config.mp_config.rsz_config.ycflt_adjust = false;
		dev->config.mp_config.rsz_config.ism_adjust = false;
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n  MRSZ_CTRL 0x%08x/0x%08x\n"
			"  MRSZ_SCALE_HY %d/%d\n"
			"  MRSZ_SCALE_HCB %d/%d\n"
			"  MRSZ_SCALE_HCR %d/%d\n"
			"  MRSZ_SCALE_VY %d/%d\n"
			"  MRSZ_SCALE_VC %d/%d\n"
			"  MRSZ_PHASE_HY %d/%d\n"
			"  MRSZ_PHASE_HC %d/%d\n"
			"  MRSZ_PHASE_VY %d/%d\n"
			"  MRSZ_PHASE_VC %d/%d\n",
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_CTRL),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_CTRL_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HY),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HCB),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HCB_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HCR),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_HCR_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_VY),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_VY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_VC),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_SCALE_VC_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_HY),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_HY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_HC),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_HC_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_VY),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_VY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_VC),
			cif_ioread32(dev->config.base_addr +
				CIF_MRSZ_PHASE_VC_SHD));
	} else {
		if (async)
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_SRSZ_CTRL);
		dev->config.sp_config.rsz_config.ycflt_adjust = false;
		dev->config.sp_config.rsz_config.ism_adjust = false;
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n  SRSZ_CTRL 0x%08x/0x%08x\n"
			"  SRSZ_SCALE_HY %d/%d\n"
			"  SRSZ_SCALE_HCB %d/%d\n"
			"  SRSZ_SCALE_HCR %d/%d\n"
			"  SRSZ_SCALE_VY %d/%d\n"
			"  SRSZ_SCALE_VC %d/%d\n"
			"  SRSZ_PHASE_HY %d/%d\n"
			"  SRSZ_PHASE_HC %d/%d\n"
			"  SRSZ_PHASE_VY %d/%d\n"
			"  SRSZ_PHASE_VC %d/%d\n",
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_CTRL),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_CTRL_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HY),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HCB),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HCB_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HCR),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_HCR_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_VY),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_VY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_VC),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_SCALE_VC_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_HY),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_HY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_HC),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_HC_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_VY),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_VY_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_VC),
			cif_ioread32(dev->config.base_addr +
				CIF_SRSZ_PHASE_VC_SHD));
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

static int cif_isp10_config_sp(
	struct cif_isp10_device *dev)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	ret = cif_isp10_config_rsz(dev, CIF_ISP10_STREAM_SP, true);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = cif_isp10_config_dcrop(dev, CIF_ISP10_STREAM_SP, true);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = cif_isp10_config_mi_sp(dev);
	if (IS_ERR_VALUE(ret))
		goto err;

	dev->sp_stream.updt_cfg = false;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_config_mp(
	struct cif_isp10_device *dev)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	ret = cif_isp10_config_rsz(dev, CIF_ISP10_STREAM_MP, true);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = cif_isp10_config_dcrop(dev, CIF_ISP10_STREAM_MP, true);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = cif_isp10_config_mi_mp(dev);
	if (IS_ERR_VALUE(ret))
		goto err;
	if (dev->config.jpeg_config.enable) {
		ret = cif_isp10_config_jpeg_enc(dev);
		if (IS_ERR_VALUE(ret))
			goto err;
		dev->config.jpeg_config.busy = false;
	}

	dev->mp_stream.updt_cfg = false;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static void cif_isp10_config_clk(
	struct cif_isp10_device *dev)
{
	cif_iowrite32(CIF_CCL_CIF_CLK_ENA,
		dev->config.base_addr + CIF_CCL);
	cif_iowrite32(0x0000187B, dev->config.base_addr + CIF_ICCL);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  CIF_CCL 0x%08x\n"
		"  CIF_ICCL 0x%08x\n",
		cif_ioread32(dev->config.base_addr + CIF_CCL),
		cif_ioread32(dev->config.base_addr + CIF_ICCL));
}

static int cif_isp10_config_cif(
	struct cif_isp10_device *dev,
	u32 stream_ids)
{
	int ret = 0;
	u32 cif_id;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"config MP = %d, config SP = %d, img_src state = %s, PM state = %s, SP state = %s, MP state = %s\n",
		(stream_ids & CIF_ISP10_STREAM_MP) == CIF_ISP10_STREAM_MP,
		(stream_ids & CIF_ISP10_STREAM_SP) == CIF_ISP10_STREAM_SP,
		cif_isp10_img_src_state_string(dev->img_src_state),
		cif_isp10_pm_state_string(dev->pm_state),
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state));

	cif_isp10_pltfrm_rtrace_printf(NULL,
		"start configuring CIF...\n");

	if ((stream_ids & CIF_ISP10_STREAM_MP) ||
		(stream_ids & CIF_ISP10_STREAM_SP)) {
		ret = cif_isp10_set_pm_state(dev,
			CIF_ISP10_PM_STATE_SW_STNDBY);
		if (IS_ERR_VALUE(ret))
			goto err;

		if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
			/* configure sensor */
			ret = cif_isp10_config_img_src(dev);
			if (IS_ERR_VALUE(ret))
				goto err;
		}

		cif_id = cif_ioread32(dev->config.base_addr + CIF_VI_ID);
		dev->config.out_of_buffer_stall =
				CIF_ISP10_ALWAYS_STALL_ON_NO_BUFS ? 1 : 0;

		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"CIF_ID 0x%08x\n", cif_id);

		/*
		 * Cancel isp reset internal here temporary for
		 * isp bus may be dead when switch isp.
		 */
		/*
		 * cif_iowrite32(CIF_IRCL_CIF_SW_RST,
		 * dev->config.base_addr + CIF_IRCL);
		 */

		cif_isp10_config_clk(dev);

		/* Decide when to switch to asynchronous mode */
		/*
		 * TODO: remove dev->isp_dev.ycflt_en check for
		 * HW with the scaler fix.
		 */
		dev->config.mi_config.async_updt = CIF_ISP10_ALWAYS_ASYNC;
		if (CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
			dev->config.mi_config.async_updt |= CIF_ISP10_ASYNC_DMA;
			ret = cif_isp10_config_mi_dma(dev);
			if (IS_ERR_VALUE(ret))
				goto err;
		}
		if ((stream_ids & CIF_ISP10_STREAM_MP) &&
			(dev->config.jpeg_config.enable))
			dev->config.mi_config.async_updt |=
				CIF_ISP10_ASYNC_JPEG;
		if (dev->config.isp_config.ism_config.ism_en)
			dev->config.mi_config.async_updt |=
				CIF_ISP10_ASYNC_ISM;

		if (PLTFRM_CAM_ITF_IS_MIPI(dev->config.cam_itf.type)) {
			ret = cif_isp10_config_mipi(dev);
			if (IS_ERR_VALUE(ret))
				goto err;
		}

		ret = cif_isp10_config_isp(dev);
		if (IS_ERR_VALUE(ret))
			goto err;

		cif_isp10_config_ism(dev, true);
		dev->config.isp_config.ism_config.ism_update_needed = false;

		if (stream_ids & CIF_ISP10_STREAM_SP)
			dev->config.sp_config.rsz_config.ism_adjust = true;
		if (stream_ids & CIF_ISP10_STREAM_MP)
			dev->config.mp_config.rsz_config.ism_adjust = true;

		if (stream_ids & CIF_ISP10_STREAM_SP) {
			ret = cif_isp10_config_sp(dev);
			if (IS_ERR_VALUE(ret))
				goto err;
		}

		if (stream_ids & CIF_ISP10_STREAM_MP) {
			ret = cif_isp10_config_mp(dev);
			if (IS_ERR_VALUE(ret))
				goto err;
		}
		ret = cif_isp10_config_path(dev, stream_ids);
		if (IS_ERR_VALUE(ret))
			goto err;
	}

	/* Turn off XNR vertical subsampling when ism cropping is enabled */
	if (dev->config.isp_config.ism_config.ism_en) {
		if (!dev->isp_dev.cif_ism_cropping)
			dev->isp_dev.cif_ism_cropping = true;
	} else {
		if (dev->isp_dev.cif_ism_cropping)
			dev->isp_dev.cif_ism_cropping = false;
	}

	if (dev->config.sp_config.rsz_config.ycflt_adjust ||
		dev->config.sp_config.rsz_config.ism_adjust) {
		if (dev->sp_stream.state == CIF_ISP10_STATE_READY) {
			ret = cif_isp10_config_rsz(dev,
				CIF_ISP10_STREAM_SP, true);
			if (IS_ERR_VALUE(ret))
				goto err;
		} else {
			/* Disable SRSZ if SP is not used */
			cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_CTRL);
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_SRSZ_CTRL);
			dev->config.sp_config.rsz_config.ycflt_adjust = false;
			dev->config.sp_config.rsz_config.ism_adjust = false;
		}
	}

	if (dev->config.mp_config.rsz_config.ycflt_adjust ||
		dev->config.mp_config.rsz_config.ism_adjust) {
		if (dev->mp_stream.state == CIF_ISP10_STATE_READY) {
			ret = cif_isp10_config_rsz(dev,
				CIF_ISP10_STREAM_MP, true);
			if (IS_ERR_VALUE(ret))
				goto err;
		} else {
			/* Disable MRSZ if MP is not used */
			cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_CTRL);
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_MRSZ_CTRL);
			dev->config.mp_config.rsz_config.ycflt_adjust = false;
			dev->config.mp_config.rsz_config.ism_adjust = false;
		}
	}

	if (dev->config.mi_config.async_updt)
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"CIF in asynchronous mode (0x%08x)\n",
			dev->config.mi_config.async_updt);

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static void cif_isp10_init_stream(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id)
{
	int ret = 0;
	struct cif_isp10_stream *stream = NULL;
	struct cif_isp10_frm_fmt frm_fmt;

	switch (stream_id) {
	case CIF_ISP10_STREAM_SP:
		stream = &dev->sp_stream;
		stream->state = CIF_ISP10_STATE_READY;

		dev->config.sp_config.rsz_config.ycflt_adjust = false;
		dev->config.sp_config.rsz_config.ism_adjust = false;
		dev->config.mi_config.sp.busy = false;
		dev->sp_stream.updt_cfg = true;

		ret = cif_isp10_img_src_select_strm_fmt(dev);
		if (IS_ERR_VALUE(ret)) {
			dev->mp_stream.updt_cfg = false;
		} else {
			frm_fmt.pix_fmt = CIF_YUV420SP;
			frm_fmt.width =
				dev->config.img_src_output.frm_fmt.width;
			frm_fmt.height =
				dev->config.img_src_output.frm_fmt.height;
			frm_fmt.stride = 0;
			frm_fmt.quantization = 0;

			// init default formats
			dev->config.mi_config.sp.output = frm_fmt;
			dev->config.mi_config.sp.llength =
				cif_isp10_calc_llength(
				frm_fmt.width,
				frm_fmt.stride,
				frm_fmt.pix_fmt);
		}
		break;
	case CIF_ISP10_STREAM_MP:
		stream = &dev->mp_stream;
		stream->state = CIF_ISP10_STATE_READY;

		dev->config.jpeg_config.ratio = 50;
		dev->config.jpeg_config.header =
			CIF_ISP10_JPEG_HEADER_JFIF;
		dev->config.jpeg_config.enable = false;
		dev->config.mi_config.raw_enable = false;
		dev->config.mp_config.rsz_config.ycflt_adjust = false;
		dev->config.mp_config.rsz_config.ism_adjust = false;
		dev->config.mi_config.mp.busy = false;
		dev->mp_stream.updt_cfg = true;

		ret = cif_isp10_img_src_select_strm_fmt(dev);
		if (IS_ERR_VALUE(ret)) {
			dev->mp_stream.updt_cfg = false;
		} else {
			frm_fmt.pix_fmt = CIF_YUV420SP;
			frm_fmt.width =
				dev->config.img_src_output.frm_fmt.width;
			frm_fmt.height =
				dev->config.img_src_output.frm_fmt.height;
			frm_fmt.stride = 0;
			frm_fmt.quantization = 0;

			// init default formats
			dev->config.mi_config.mp.output = frm_fmt;
			dev->config.mi_config.mp.output.stride = frm_fmt.stride;

			dev->config.mi_config.mp.llength =
				cif_isp10_calc_llength(
					frm_fmt.width,
					frm_fmt.stride,
					frm_fmt.pix_fmt);
		}
		break;
	case CIF_ISP10_STREAM_DMA:
		stream = &dev->dma_stream;
		dev->config.mi_config.dma.busy = false;
		dev->dma_stream.updt_cfg = false;
		break;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported stream ID %d\n", stream_id);
		WARN_ON(1);
		break;
	}

	INIT_LIST_HEAD(&stream->buf_queue);
	stream->next_buf = NULL;
	stream->curr_buf = NULL;
	stream->stop = false;
	stream->stall = false;

	cif_isp10_pltfrm_event_clear(dev->dev, &stream->done);

	if (!stream->updt_cfg)
		stream->state = CIF_ISP10_STATE_INACTIVE;

	return;
}

static int cif_isp10_jpeg_gen_header(
	struct cif_isp10_device *dev)
{
	unsigned int timeout = 10000;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	cif_iowrite32(CIF_JPE_GEN_HEADER_ENABLE,
		dev->config.base_addr + CIF_JPE_GEN_HEADER);

	while (timeout--) {
		if (cif_ioread32(dev->config.base_addr +
			CIF_JPE_STATUS_RIS) &
			CIF_JPE_STATUS_GENHEADER_DONE) {
			cif_isp10_pltfrm_pr_dbg(NULL,
				"JPEG header generated\n");
			cif_iowrite32(CIF_JPE_STATUS_GENHEADER_DONE,
				dev->config.base_addr + CIF_JPE_STATUS_ICR);
			break;
		}
	}

	if (!timeout) {
		cif_isp10_pltfrm_pr_err(NULL,
			"JPEG header generation timeout\n");
		cif_isp10_pltfrm_pr_err(NULL,
			"failed with error %d\n", -ETIMEDOUT);
		return -ETIMEDOUT;
	}

#ifdef CIF_ISP10_VERIFY_JPEG_HEADER
	{
		u32 *buff = (u32 *)phys_to_virt(
			dev->config.mi_config.mp.curr_buff_addr);
		if (buff[0] != 0xe0ffd8ff)
			cif_isp10_pltfrm_pr_err(NULL,
				"JPEG HEADER WRONG: 0x%08x\n"
				"curr_buff_addr 0x%08x\n"
				"MI_MP_Y_SIZE_SHD 0x%08x\n"
				"MI_MP_Y_BASE_AD_SHD 0x%08x\n",
				buff[0],
				dev->config.mi_config.mp.curr_buff_addr,
				cif_ioread32(dev->config.base_addr +
					CIF_MI_MP_Y_SIZE_SHD),
				cif_ioread32(dev->config.base_addr +
					CIF_MI_MP_Y_BASE_AD_SHD));
	}
#endif

	return 0;
}

static void cif_isp10_mi_update_buff_addr(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id strm_id)
{
	if (strm_id == CIF_ISP10_STREAM_SP) {
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr,
			dev->config.base_addr +
			CIF_MI_SP_Y_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr +
			dev->config.mi_config.sp.cb_offs,
			dev->config.base_addr +
			CIF_MI_SP_CB_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr +
			dev->config.mi_config.sp.cr_offs,
			dev->config.base_addr +
			CIF_MI_SP_CR_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		/*
		 * There have bee repeatedly issues with
		 * the offset registers, it is safer to write
		 * them each time, even though it is always
		 * 0 and even though that is the
		 * register's default value
		 */
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_Y_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_CB_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_CR_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n  MI_SP_Y_BASE_AD 0x%08x/0x%08x\n"
			"  MI_SP_CB_BASE_AD 0x%08x/0x%08x\n"
			"  MI_SP_CR_BASE_AD 0x%08x/0x%08x\n",
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_Y_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_Y_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CB_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CB_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CR_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CR_BASE_AD_SHD));
	} else if (strm_id == CIF_ISP10_STREAM_MP) {
		cif_iowrite32_verify(dev->config.mi_config.mp.next_buff_addr,
			dev->config.base_addr +
			CIF_MI_MP_Y_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.mp.next_buff_addr +
			dev->config.mi_config.mp.cb_offs,
			dev->config.base_addr +
			CIF_MI_MP_CB_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.mp.next_buff_addr +
			dev->config.mi_config.mp.cr_offs,
			dev->config.base_addr +
			CIF_MI_MP_CR_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		/*
		 * There have bee repeatedly issues with
		 * the offset registers, it is safer to write
		 * them each time, even though it is always
		 * 0 and even though that is the
		 * register's default value
		 */
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_MP_Y_OFFS_CNT_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_MP_CB_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_MP_CR_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n  MI_MP_Y_BASE_AD 0x%08x/0x%08x\n"
			"  MI_MP_CB_BASE_AD 0x%08x/0x%08x\n"
			"  MI_MP_CR_BASE_AD 0x%08x/0x%08x\n",
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_Y_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_Y_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_CB_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_CB_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_CR_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_CR_BASE_AD_SHD));
	} else { /* DMA */
		cif_iowrite32_verify(dev->config.mi_config.dma.next_buff_addr,
			dev->config.base_addr +
			CIF_MI_DMA_Y_PIC_START_AD, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.dma.next_buff_addr +
			dev->config.mi_config.dma.cb_offs,
			dev->config.base_addr +
			CIF_MI_DMA_CB_PIC_START_AD,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.dma.next_buff_addr +
			dev->config.mi_config.dma.cr_offs,
			dev->config.base_addr +
			CIF_MI_DMA_CR_PIC_START_AD,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n  MI_DMA_Y_PIC_START_AD 0x%08x\n"
			"  MI_DMA_CB_PIC_START_AD 0x%08x\n"
			"  MI_DMA_CR_PIC_START_AD 0x%08x\n",
			cif_ioread32(dev->config.base_addr +
				CIF_MI_DMA_Y_PIC_START_AD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_DMA_CB_PIC_START_AD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_DMA_CR_PIC_START_AD));
	}
}

static int cif_isp10_update_mi_mp(
	struct cif_isp10_device *dev)
{
	int ret = 0;
	enum cif_isp10_pix_fmt out_pix_fmt =
		dev->config.mi_config.mp.output.pix_fmt;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"curr 0x%08x next 0x%08x\n",
		dev->config.mi_config.mp.curr_buff_addr,
		dev->config.mi_config.mp.next_buff_addr);

	if (dev->config.jpeg_config.enable) {
		/*
		 * in case of jpeg encoding, we don't have to disable the
		 * MI, because the encoding
		 * anyway has to be started explicitly
		 */
		if (!dev->config.jpeg_config.busy) {
			if ((dev->config.mi_config.mp.curr_buff_addr !=
				dev->config.mi_config.mp.next_buff_addr) &&
				(dev->config.mi_config.mp.curr_buff_addr !=
				CIF_ISP10_INVALID_BUFF_ADDR)) {
				ret = cif_isp10_jpeg_gen_header(dev);
				if (IS_ERR_VALUE(ret))
					goto err;
				cif_isp10_pltfrm_pr_dbg(NULL,
					"Starting JPEG encoding\n");
				cif_isp10_pltfrm_rtrace_printf(dev->dev,
					"Starting JPEG encoding\n");
				cif_iowrite32(CIF_JPE_ENCODE_ENABLE,
					dev->config.base_addr + CIF_JPE_ENCODE);
				cif_iowrite32(CIF_JPE_INIT_ENABLE,
					dev->config.base_addr +
					CIF_JPE_INIT);
				dev->config.jpeg_config.busy = true;
			}
			if (dev->config.mi_config.mp.next_buff_addr !=
				CIF_ISP10_INVALID_BUFF_ADDR)
				cif_isp10_mi_update_buff_addr(dev,
					CIF_ISP10_STREAM_MP);
			dev->config.mi_config.mp.curr_buff_addr =
				dev->config.mi_config.mp.next_buff_addr;
		}
	} else {
		if (dev->config.mi_config.mp.next_buff_addr !=
			dev->config.mi_config.mp.curr_buff_addr) {
			if (dev->config.mi_config.mp.next_buff_addr ==
				CIF_ISP10_INVALID_BUFF_ADDR) {
				/* disable MI MP */
				cif_isp10_pltfrm_pr_dbg(NULL,
					"disabling MP MI\n");
				cif_iowrite32AND_verify(
				    ~(CIF_MI_CTRL_MP_ENABLE_IN |
					CIF_MI_CTRL_JPEG_ENABLE |
					CIF_MI_CTRL_RAW_ENABLE),
					dev->config.base_addr + CIF_MI_CTRL,
					~0);
			} else if (dev->config.mi_config.mp.curr_buff_addr ==
				CIF_ISP10_INVALID_BUFF_ADDR) {
				/* re-enable MI MP */
				cif_isp10_pltfrm_pr_dbg(NULL,
					"enabling MP MI\n");
				cif_iowrite32(CIF_MI_MP_FRAME,
					dev->config.base_addr + CIF_MI_ICR);
				cif_iowrite32AND_verify(
					    ~(CIF_MI_CTRL_MP_ENABLE_IN |
						CIF_MI_CTRL_JPEG_ENABLE |
						CIF_MI_CTRL_RAW_ENABLE),
						dev->config.base_addr +
						CIF_MI_CTRL, ~0);
				if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER
					(out_pix_fmt)) {
					cif_iowrite32OR_verify(
						CIF_MI_CTRL_RAW_ENABLE,
						dev->config.base_addr +
						CIF_MI_CTRL,
						~0);
				} else if (CIF_ISP10_PIX_FMT_IS_YUV
					(out_pix_fmt)) {
					cif_iowrite32OR_verify(
						CIF_MI_CTRL_MP_ENABLE_IN,
						dev->config.base_addr +
						CIF_MI_CTRL, ~0);
				}
			}
			cif_isp10_mi_update_buff_addr(dev, CIF_ISP10_STREAM_MP);
			dev->config.mi_config.mp.curr_buff_addr =
				dev->config.mi_config.mp.next_buff_addr;
		}
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

static int cif_isp10_update_mi_sp(
	struct cif_isp10_device *dev)
{
	u32 vir_len_offset = dev->config.mi_config.sp.vir_len_offset;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"curr 0x%08x next 0x%08x\n",
		dev->config.mi_config.sp.curr_buff_addr,
		dev->config.mi_config.sp.next_buff_addr);

	if (dev->config.mi_config.sp.next_buff_addr !=
		dev->config.mi_config.sp.curr_buff_addr) {
		if (dev->config.mi_config.sp.next_buff_addr ==
			CIF_ISP10_INVALID_BUFF_ADDR) {
			/* disable MI SP */
			cif_isp10_pltfrm_pr_dbg(NULL, "disabling SP MI\n");
			/* 'switch off' MI interface */
			cif_iowrite32AND_verify(~CIF_MI_CTRL_SP_ENABLE,
				dev->config.base_addr + CIF_MI_CTRL, ~0);
		} else if (dev->config.mi_config.sp.curr_buff_addr ==
			CIF_ISP10_INVALID_BUFF_ADDR) {
			/* re-enable MI SP */
			cif_isp10_pltfrm_pr_dbg(NULL, "enabling SP MI\n");
			cif_iowrite32(CIF_MI_SP_FRAME,
				dev->config.base_addr + CIF_MI_ICR);
			cif_iowrite32OR_verify(CIF_MI_CTRL_SP_ENABLE,
				dev->config.base_addr + CIF_MI_CTRL, ~0);
		}
		cif_isp10_mi_update_buff_addr(dev, CIF_ISP10_STREAM_SP);
		dev->config.mi_config.sp.curr_buff_addr =
			dev->config.mi_config.sp.next_buff_addr;
	} else if (PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type) &&
		dev->config.mi_config.sp.field_flag == FIELD_FLAGS_ODD) {
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr +
			vir_len_offset,
			dev->config.base_addr +
			CIF_MI_SP_Y_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr +
			vir_len_offset +
			dev->config.mi_config.sp.cb_offs,
			dev->config.base_addr +
			CIF_MI_SP_CB_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(dev->config.mi_config.sp.next_buff_addr +
			vir_len_offset +
			dev->config.mi_config.sp.cr_offs,
			dev->config.base_addr +
			CIF_MI_SP_CR_BASE_AD_INIT, CIF_MI_ADDR_SIZE_ALIGN_MASK);
		/*
		 * There have bee repeatedly issues with
		 * the offset registers, it is safer to write
		 * them each time, even though it is always
		 * 0 and even though that is the
		 * register's default value
		 */
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_Y_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_CB_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_iowrite32_verify(0,
			dev->config.base_addr +
			CIF_MI_SP_CR_OFFS_CNT_INIT,
			CIF_MI_ADDR_SIZE_ALIGN_MASK);
		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"\n MI_SP_Y_BASE_AD 0x%08x/0x%08x\n"
			" MI_SP_CB_BASE_AD 0x%08x/0x%08x\n"
			" MI_SP_CR_BASE_AD 0x%08x/0x%08x\n",
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_Y_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_Y_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CB_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CB_BASE_AD_SHD),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CR_BASE_AD_INIT),
			cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_CR_BASE_AD_SHD));
	}

	return 0;
}

static int cif_isp10_s_fmt_mp(
	struct cif_isp10_device *dev,
	struct cif_isp10_strm_fmt *strm_fmt,
	u32 stride)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d@%d/%dfps, stride = %d, quantization: %d\n",
		cif_isp10_pix_fmt_string(strm_fmt->frm_fmt.pix_fmt),
		strm_fmt->frm_fmt.width,
		strm_fmt->frm_fmt.height,
		strm_fmt->frm_intrvl.numerator,
		strm_fmt->frm_intrvl.denominator,
		stride,
		strm_fmt->frm_fmt.quantization);

	/* TBD: check whether format is a valid format for MP */

	if (PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type)) {
		ret = -EINVAL;
		cif_isp10_pltfrm_pr_err(dev->dev,
			"MP doesn't support the interlace format!\n");
		goto err;
	}

	if (CIF_ISP10_PIX_FMT_IS_JPEG(strm_fmt->frm_fmt.pix_fmt)) {
		dev->config.jpeg_config.enable = true;
	}	else if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER
		(strm_fmt->frm_fmt.pix_fmt)) {
		if ((dev->sp_stream.state == CIF_ISP10_STATE_READY) ||
			(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING))
			cif_isp10_pltfrm_pr_warn(dev->dev,
				"cannot output RAW data when SP is active, you will not be able to (re-)start streaming\n");
		dev->config.mi_config.raw_enable = true;
	}

	dev->config.mi_config.mp.output = strm_fmt->frm_fmt;
	dev->config.mi_config.mp.output.stride = stride;

	dev->config.mi_config.mp.llength =
		cif_isp10_calc_llength(
			strm_fmt->frm_fmt.width,
			stride,
			strm_fmt->frm_fmt.pix_fmt);
	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"mp llength=0x%x\n", dev->config.mi_config.mp.llength);

	dev->mp_stream.updt_cfg = true;
	dev->mp_stream.state = CIF_ISP10_STATE_READY;

	if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
		ret = cif_isp10_img_src_select_strm_fmt(dev);
		if (IS_ERR_VALUE(ret)) {
			dev->mp_stream.updt_cfg = false;
			dev->mp_stream.state = CIF_ISP10_STATE_INACTIVE;
			goto err;
		}
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_s_fmt_sp(
	struct cif_isp10_device *dev,
	struct cif_isp10_strm_fmt *strm_fmt,
	u32 stride)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d@%d/%dfps, stride = %d, quantization: %d\n",
		cif_isp10_pix_fmt_string(strm_fmt->frm_fmt.pix_fmt),
		strm_fmt->frm_fmt.width,
		strm_fmt->frm_fmt.height,
		strm_fmt->frm_intrvl.numerator,
		strm_fmt->frm_intrvl.denominator,
		stride,
		strm_fmt->frm_fmt.quantization);

	if (dev->config.mi_config.raw_enable)
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"cannot activate SP when MP is set to RAW data output, you will not be able to (re-)start streaming\n");

	/* TBD: more detailed check whether format is a valid format for SP */
	/* TBD: remove the mode stuff */
	if (!CIF_ISP10_PIX_FMT_IS_YUV(strm_fmt->frm_fmt.pix_fmt) &&
		!CIF_ISP10_PIX_FMT_IS_RGB(strm_fmt->frm_fmt.pix_fmt)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"format %s %dx%d@%d/%dfps, stride = %d not supported on SP\n",
			cif_isp10_pix_fmt_string(strm_fmt->frm_fmt.pix_fmt),
			strm_fmt->frm_fmt.width,
			strm_fmt->frm_fmt.height,
			strm_fmt->frm_intrvl.numerator,
			strm_fmt->frm_intrvl.denominator,
			stride);
		ret = -EINVAL;
		goto err;
	}

	dev->config.mi_config.sp.output = strm_fmt->frm_fmt;
	dev->config.mi_config.sp.llength =
		cif_isp10_calc_llength(
		strm_fmt->frm_fmt.width,
		stride,
		strm_fmt->frm_fmt.pix_fmt);

	dev->sp_stream.updt_cfg = true;
	dev->sp_stream.state = CIF_ISP10_STATE_READY;

	if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
		ret = cif_isp10_img_src_select_strm_fmt(dev);
		if (IS_ERR_VALUE(ret)) {
			dev->sp_stream.updt_cfg = false;
			dev->sp_stream.state = CIF_ISP10_STATE_INACTIVE;
			goto err;
		}
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static int cif_isp10_s_fmt_dma(
	struct cif_isp10_device *dev,
	struct cif_isp10_strm_fmt *strm_fmt,
	u32 stride)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s %dx%d@%d/%dfps, stride = %d\n",
		cif_isp10_pix_fmt_string(strm_fmt->frm_fmt.pix_fmt),
		strm_fmt->frm_fmt.width,
		strm_fmt->frm_fmt.height,
		strm_fmt->frm_intrvl.numerator,
		strm_fmt->frm_intrvl.denominator,
		stride);

	if (!CIF_ISP10_PIX_FMT_IS_YUV(strm_fmt->frm_fmt.pix_fmt) &&
		!CIF_ISP10_PIX_FMT_IS_RAW_BAYER(strm_fmt->frm_fmt.pix_fmt)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"format %s %dx%d@%d/%dfps, stride = %d not supported for DMA\n",
			cif_isp10_pix_fmt_string(strm_fmt->frm_fmt.pix_fmt),
			strm_fmt->frm_fmt.width,
			strm_fmt->frm_fmt.height,
			strm_fmt->frm_intrvl.numerator,
			strm_fmt->frm_intrvl.denominator,
			stride);
		ret = -EINVAL;
		goto err;
	}

	dev->config.mi_config.dma.output = strm_fmt->frm_fmt;
	dev->config.mi_config.dma.llength =
		cif_isp10_calc_llength(
		strm_fmt->frm_fmt.width,
		stride,
		strm_fmt->frm_fmt.pix_fmt);

	dev->dma_stream.updt_cfg = true;
	dev->dma_stream.state = CIF_ISP10_STATE_READY;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

static void cif_isp10_dma_next_buff(
	struct cif_isp10_device *dev)
{
#ifdef CIF_ISP10_MODE_DMA_SG
	struct sg_table *sgt = NULL;
#endif

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	if (!list_empty(&dev->dma_stream.buf_queue) &&
		!dev->dma_stream.stop) {
		if (dev->dma_stream.curr_buf)
			WARN_ON(1);
		dev->dma_stream.curr_buf =
			list_first_entry(&dev->dma_stream.buf_queue,
				struct cif_isp10_buffer, queue);
		list_del(&dev->dma_stream.curr_buf->queue);

#ifdef CIF_ISP10_MODE_DMA_CONTIG
		dev->config.mi_config.dma.next_buff_addr =
			vb2_dma_contig_plane_dma_addr(
				&dev->dma_stream.curr_buf->vb.vb2_buf, 0);
#endif

#ifdef CIF_ISP10_MODE_DMA_SG
		sgt = vb2_dma_sg_plane_desc(
				&dev->dma_stream.curr_buf->vb.vb2_buf, 0);
		dev->config.mi_config.dma.next_buff_addr =
			sg_dma_address(sgt->sgl);
#endif
		cif_isp10_mi_update_buff_addr(dev,
			CIF_ISP10_STREAM_DMA);
		dev->config.mi_config.dma.busy = true;
		if ((dev->sp_stream.state == CIF_ISP10_STATE_STREAMING) &&
			dev->sp_stream.curr_buf)
			dev->config.mi_config.sp.busy = true;
		if ((dev->mp_stream.state == CIF_ISP10_STATE_STREAMING) &&
			dev->mp_stream.curr_buf)
			dev->config.mi_config.mp.busy = true;
		/* workaround for write register failure bug */
		do {
			cif_iowrite32(CIF_MI_DMA_START_ENABLE,
				dev->config.base_addr + CIF_MI_DMA_START);
			udelay(1);
		} while (!cif_ioread32(
			dev->config.base_addr + CIF_MI_DMA_STATUS));
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MI_DMA_CTRL 0x%08x\n"
		"  MI_DMA_STATUS 0x%08x\n",
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_CTRL),
		cif_ioread32(dev->config.base_addr +
			CIF_MI_DMA_STATUS));
}

static void cif_isp10_dma_ready(
	struct cif_isp10_device *dev)
{
	unsigned int mi_mis_tmp;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	cif_iowrite32(CIF_MI_DMA_READY,
		dev->config.base_addr + CIF_MI_ICR);
	mi_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_MI_MIS);
	if (mi_mis_tmp & CIF_MI_DMA_READY)
		cif_isp10_pltfrm_pr_err(dev->dev,
					"dma icr err: 0x%x\n",
					mi_mis_tmp);
	wake_up(&dev->dma_stream.curr_buf->vb.vb2_buf.vb2_queue->done_wq);
	dev->dma_stream.curr_buf = NULL;
	dev->config.mi_config.dma.busy = false;
	cif_isp10_pltfrm_event_signal(dev->dev, &dev->dma_stream.done);
}

static int cif_isp10_mi_frame_end(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id)
{
	struct cif_isp10_stream *stream = NULL;
	u32 cur_buff_addr = -1;
	u32 *next_buff_addr = NULL;
	struct vb2_buffer *vb2_buf;
	dma_addr_t tmp_addr;
#ifdef CIF_ISP10_MODE_DMA_SG
	struct sg_table *sgt = NULL;
	struct scatterlist *sg;
	dma_addr_t tmp_phy_addr;
	int i;
#endif

	CIF_ISP10_PLTFRM_MEM_IO_ADDR y_base_addr;
	int (*update_mi)(
		struct cif_isp10_device *dev);
	struct cif_isp10_isp_readout_work *work;

	cif_isp10_pltfrm_pr_dbg(NULL, "%s\n",
		cif_isp10_stream_id_string(stream_id));

	if (stream_id == CIF_ISP10_STREAM_MP) {
		stream = &dev->mp_stream;
		y_base_addr =
			dev->config.base_addr + CIF_MI_MP_Y_BASE_AD_SHD;
		next_buff_addr = &dev->config.mi_config.mp.next_buff_addr;
		update_mi = cif_isp10_update_mi_mp;
		if (dev->config.jpeg_config.enable) {
			unsigned int jpe_status =
				cif_ioread32(dev->config.base_addr +
					CIF_JPE_STATUS_RIS);
			if (jpe_status & CIF_JPE_STATUS_ENCODE_DONE) {
				cif_iowrite32(CIF_JPE_STATUS_ENCODE_DONE,
					dev->config.base_addr +
						CIF_JPE_STATUS_ICR);
				if (stream->curr_buf) {
					stream->curr_buf->size =
					cif_ioread32(dev->config.base_addr +
						CIF_MI_BYTE_CNT);
					cif_isp10_pltfrm_pr_dbg(NULL,
						"JPEG encoding done, size %lu\n",
						stream->curr_buf->size);
					if (cif_ioread32(dev->config.base_addr +
						CIF_MI_RIS) & CIF_MI_WRAP_MP_Y)
						cif_isp10_pltfrm_pr_err(NULL,
							"buffer wrap around detected, JPEG presumably corrupted (%d/%d/%lu)\n",
							dev->config.mi_config.
							mp.y_size,
							cif_ioread32(
							dev->config.base_addr +
							CIF_MI_MP_Y_SIZE_SHD),
							stream->curr_buf->size);
				}
			}
		}
	} else if (stream_id == CIF_ISP10_STREAM_SP) {
		stream = &dev->sp_stream;
		y_base_addr =
			dev->config.base_addr + CIF_MI_SP_Y_BASE_AD_SHD;
		next_buff_addr = &dev->config.mi_config.sp.next_buff_addr;
		update_mi = cif_isp10_update_mi_sp;
	} else {
		WARN_ON(1);
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s Y_BASE_AD_INIT/Y_BASE_AD_SHD (0x%08x/0x%08x)\n",
		cif_isp10_stream_id_string(stream_id),
		(stream_id & CIF_ISP10_STREAM_MP) ?
			cif_ioread32(dev->config.base_addr +
			CIF_MI_MP_Y_BASE_AD_INIT) :
			cif_ioread32(dev->config.base_addr +
			CIF_MI_SP_Y_BASE_AD_INIT),
		cif_ioread32(y_base_addr));

	if ((!stream->next_buf) &&
		!(dev->config.jpeg_config.enable &&
		(stream_id == CIF_ISP10_STREAM_MP))) {
		stream->stall = dev->config.out_of_buffer_stall;
	} else if ((stream->next_buf)) {
#ifdef CIF_ISP10_MODE_DMA_CONTIG
		tmp_addr = vb2_dma_contig_plane_dma_addr(
			&stream->next_buf->vb.vb2_buf, 0);
#endif
#ifdef CIF_ISP10_MODE_DMA_SG
		sgt = vb2_dma_sg_plane_desc(
			&stream->next_buf->vb.vb2_buf, 0);
		tmp_addr = sg_dma_address(sgt->sgl);
#endif
		if (tmp_addr != cif_ioread32(y_base_addr) &&
			(!PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type) ||
			dev->config.mi_config.sp.field_flag == FIELD_FLAGS_ODD)) {
			cif_isp10_pltfrm_pr_warn(dev->dev,
				"%s buffer queue is not advancing (0x%08x/0x%08x)\n",
				cif_isp10_stream_id_string(stream_id),
				(stream_id & CIF_ISP10_STREAM_MP) ?
					cif_ioread32(dev->config.base_addr +
					CIF_MI_MP_Y_BASE_AD_INIT) :
					cif_ioread32(dev->config.base_addr +
					CIF_MI_SP_Y_BASE_AD_INIT),
				cif_ioread32(y_base_addr));
			stream->stall = true;
		}
	}

	if (!stream->stall) {
		/*
		 * If mi restart after switch off for buffer is empty,
		 * mi may be restart failed. So mi write data to last
		 * buffer, the last buffer isn't been release to user
		 * until new buffer queue;
		 */
		if ((stream->curr_buf) &&
			(stream->next_buf) &&
			(!PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type) ||
			dev->config.mi_config.sp.field_flag == FIELD_FLAGS_EVEN)) {
			bool wake_now;

			vb2_buf = &stream->curr_buf->vb.vb2_buf;
			vb2_buffer_done(vb2_buf, VB2_BUF_STATE_DONE);
			wake_now = false;

			if (stream->metadata.d && dev->isp_dev.streamon) {
				struct v4l2_buffer_metadata_s *metadata;

				metadata = (struct v4l2_buffer_metadata_s *)
					(stream->metadata.d +
					stream->curr_buf->vb.vb2_buf.index *
					CAMERA_METADATA_LEN);
				metadata->frame_id = dev->isp_dev.frame_id;
				metadata->frame_t.vs_t = dev->isp_dev.vs_t;
				metadata->frame_t.fi_t = dev->isp_dev.fi_t;

				work = (struct cif_isp10_isp_readout_work *)
					kmalloc(
					sizeof(
					struct cif_isp10_isp_readout_work),
					GFP_ATOMIC);
				if (work) {
					INIT_WORK((struct work_struct *)work,
						cifisp_isp_readout_work);
					work->readout =
						CIF_ISP10_ISP_READOUT_META;
					work->isp_dev =
						&dev->isp_dev;
					work->frame_id =
						dev->isp_dev.frame_id;
					work->vb =
						&stream->curr_buf->vb.vb2_buf;
					work->stream_id = stream->id;
					if (!queue_work(dev->isp_dev.readout_wq,
						(struct work_struct *)work)) {
						cif_isp10_pltfrm_pr_err(
						dev->dev,
						"Could not schedule work\n");
						wake_now = true;
						kfree((void *)work);
					}
				} else {
					cif_isp10_pltfrm_pr_err(dev->dev,
						"Could not allocate work\n");
					wake_now = true;
				}
			} else {
				wake_now = true;
			}

			if (wake_now) {
				cif_isp10_pltfrm_pr_dbg(NULL,
					"frame done\n");
				vb2_buf = &stream->curr_buf->vb.vb2_buf;
				wake_up(&vb2_buf->vb2_queue->done_wq);
			}
			stream->curr_buf = NULL;
		}

		if (!stream->curr_buf) {
			stream->curr_buf = stream->next_buf;
			stream->next_buf = NULL;
		}
	}

	if (!stream->next_buf) {
		/*
		 * in case of jpeg encoding, we are only programming
		 * a new buffer, if the jpeg header was generated, because
		 * we need the curent buffer for the jpeg encoding
		 * in the current frame period
		 */
		if (!list_empty(&stream->buf_queue)) {
			stream->next_buf =
				list_first_entry(&stream->buf_queue,
					struct cif_isp10_buffer, queue);
			list_del(&stream->next_buf->queue);
#ifdef CIF_ISP10_MODE_DMA_CONTIG
			*next_buff_addr = vb2_dma_contig_plane_dma_addr(
				&stream->next_buf->vb.vb2_buf, 0);
#endif
#ifdef CIF_ISP10_MODE_DMA_SG
			sgt = vb2_dma_sg_plane_desc(
				&stream->next_buf->vb.vb2_buf, 0);
			*next_buff_addr = sg_dma_address(sgt->sgl);
#endif
		} else if (
		(!dev->config.out_of_buffer_stall ||
		(dev->config.jpeg_config.enable &&
		(stream_id == CIF_ISP10_STREAM_MP))) &&
		(stream->curr_buf)) {
			/*
			 * If mi restart after switch off for buffer is empty,
			 * mi may be restart failed. So mi write data to last
			 * buffer, the last buffer isn't been release to user
			 * until new buffer queue;
			 *
			 * if
			 * *next_buff_addr = CIF_ISP10_INVALID_BUFF_ADDR;
			 * mi will stop;
			 */
			vb2_buf = &stream->curr_buf->vb.vb2_buf;
#ifdef CIF_ISP10_MODE_DMA_CONTIG
			*next_buff_addr =
				vb2_dma_contig_plane_dma_addr(vb2_buf, 0);
#endif
#ifdef CIF_ISP10_MODE_DMA_SG
			sgt = vb2_dma_sg_plane_desc(vb2_buf, 0);
			*next_buff_addr = sg_dma_address(sgt->sgl);
#endif
		}
	}
	(void)update_mi(dev);

	stream->stall = false;

	if (stream->curr_buf) {
		// TODO::current vb2 buffer is null
		if (!stream->curr_buf->vb.vb2_buf.planes[0].mem_priv) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"error isp buffer is null\n");
			return 0;
		}

#ifdef CIF_ISP10_MODE_DMA_CONTIG
		cur_buff_addr = vb2_dma_contig_plane_dma_addr
			(&stream->curr_buf->vb.vb2_buf, 0);

#endif
#ifdef CIF_ISP10_MODE_DMA_SG
		sgt = vb2_dma_sg_plane_desc(&stream->curr_buf->vb.vb2_buf, 0);
		cur_buff_addr = sg_dma_address(sgt->sgl);

		cif_isp10_pltfrm_pr_dbg(dev->dev,
			"current buffer scatter list: ");
		for_each_sg(sgt->sgl, sg, sgt->nents, i) {
			tmp_addr = sg_dma_address(sg);
			tmp_phy_addr = sg_phys(sg);
			cif_isp10_pltfrm_pr_dbg(dev->dev,
				"dma_addr: %pad  tmp_phy: %pad length: %.8x\n",
				&tmp_addr, &tmp_phy_addr, (int)sg_dma_len(sg));
		}
#endif
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s curr_buff: %d, 0x%08x next_buf: %d, 0x%x\n",
		cif_isp10_stream_id_string(stream_id),
		(stream->curr_buf) ? stream->curr_buf->vb.vb2_buf.index : -1,
		cur_buff_addr,
		(stream->next_buf) ? stream->next_buf->vb.vb2_buf.index : -1,
		*next_buff_addr);

	return 0;
}

static void cif_isp10_stream_metadata_reset(
	struct cif_isp10_stream *stream_dev
)
{
	unsigned int i;
	struct v4l2_buffer_metadata_s *metadata;
	struct cifisp_isp_metadata *isp_metadata;

	if (stream_dev->metadata.d) {
		for (i = 0; i < stream_dev->metadata.cnt; i++) {
			metadata = (struct v4l2_buffer_metadata_s *)
				(stream_dev->metadata.d +
				i * CAMERA_METADATA_LEN);
			isp_metadata = (struct cifisp_isp_metadata *)
				metadata->isp;
			isp_metadata->other_cfg.s_frame_id = 0xffffffff;
			isp_metadata->meas_cfg.s_frame_id = 0xffffffff;
		}
	}
}

static void cif_isp10_start_mi(
	struct cif_isp10_device *dev,
	bool start_mi_sp,
	bool start_mi_mp)
{
	cif_isp10_pltfrm_pr_dbg(dev->dev, "\n");

	if (start_mi_sp &&
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING))
		start_mi_sp = false;
	if (start_mi_mp &&
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING))
		start_mi_mp = false;
	if (!start_mi_sp && !start_mi_mp)
		return;

	if ((start_mi_sp &&
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)) ||
		(start_mi_mp &&
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)))
		WARN_ON(1);

	if (start_mi_sp) {
		cif_isp10_stream_metadata_reset(&dev->sp_stream);
		dev->config.mi_config.sp.next_buff_addr =
			CIF_ISP10_INVALID_BUFF_ADDR;
		dev->config.mi_config.sp.curr_buff_addr =
			CIF_ISP10_INVALID_BUFF_ADDR;
		spin_lock(&dev->vbq_lock);
		cif_isp10_mi_frame_end(dev, CIF_ISP10_STREAM_SP);
		spin_unlock(&dev->vbq_lock);
		dev->sp_stream.stall = false;
	}

	if (start_mi_mp) {
		cif_isp10_stream_metadata_reset(&dev->mp_stream);
		dev->config.mi_config.mp.next_buff_addr =
			CIF_ISP10_INVALID_BUFF_ADDR;
		dev->config.mi_config.mp.curr_buff_addr =
			CIF_ISP10_INVALID_BUFF_ADDR;
		spin_lock(&dev->vbq_lock);
		cif_isp10_mi_frame_end(dev, CIF_ISP10_STREAM_MP);
		spin_unlock(&dev->vbq_lock);
		dev->mp_stream.stall = false;
	}

	cif_iowrite32OR(CIF_MI_INIT_SOFT_UPD,
		dev->config.base_addr + CIF_MI_INIT);
	cif_isp10_pltfrm_pr_dbg(NULL,
		"CIF_MI_INIT_SOFT_UPD\n");

	if (start_mi_sp) {
		spin_lock(&dev->vbq_lock);
		cif_isp10_mi_frame_end(dev, CIF_ISP10_STREAM_SP);
		spin_unlock(&dev->vbq_lock);
		if (dev->sp_stream.curr_buf &&
			(!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)))
			dev->config.mi_config.sp.busy = true;
	}

	if (start_mi_mp) {
		spin_lock(&dev->vbq_lock);
		cif_isp10_mi_frame_end(dev, CIF_ISP10_STREAM_MP);
		spin_unlock(&dev->vbq_lock);
		if (dev->mp_stream.curr_buf &&
			(!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)))
			dev->config.mi_config.mp.busy = true;
	}

	if (!dev->config.mi_config.async_updt)
		cif_iowrite32OR(CIF_ISP_CTRL_ISP_GEN_CFG_UPD,
			dev->config.base_addr + CIF_ISP_CTRL);
}

static void cif_isp10_stop_mi(
	struct cif_isp10_device *dev,
	bool stop_mi_sp,
	bool stop_mi_mp)
{
	cif_isp10_pltfrm_pr_dbg(dev->dev, "\n");

	if (stop_mi_sp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_STREAMING))
		stop_mi_sp = false;
	if (stop_mi_mp &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING))
		stop_mi_mp = false;

	if (!stop_mi_sp && !stop_mi_mp)
		return;

	if (stop_mi_sp && stop_mi_mp) {
		cif_iowrite32AND_verify(~(CIF_MI_SP_FRAME |
			CIF_MI_MP_FRAME |
			CIF_JPE_STATUS_ENCODE_DONE),
			dev->config.base_addr + CIF_MI_IMSC, ~0);
		cif_iowrite32(CIF_MI_SP_FRAME |
			CIF_MI_MP_FRAME |
			CIF_JPE_STATUS_ENCODE_DONE,
			dev->config.base_addr + CIF_MI_ICR);
		cif_iowrite32AND_verify(~CIF_MI_CTRL_SP_ENABLE,
			dev->config.base_addr + CIF_MI_CTRL, ~0);
		cif_iowrite32AND_verify(~(CIF_MI_CTRL_MP_ENABLE_IN |
			CIF_MI_CTRL_SP_ENABLE |
			CIF_MI_CTRL_JPEG_ENABLE |
			CIF_MI_CTRL_RAW_ENABLE),
			dev->config.base_addr + CIF_MI_CTRL, ~0);
		cif_iowrite32(CIF_MI_INIT_SOFT_UPD,
			dev->config.base_addr + CIF_MI_INIT);
	} else if (stop_mi_sp) {
		cif_iowrite32(CIF_MI_SP_FRAME,
			dev->config.base_addr + CIF_MI_ICR);
		cif_iowrite32AND_verify(~CIF_MI_CTRL_SP_ENABLE,
			dev->config.base_addr + CIF_MI_CTRL, ~0);
	} else if (stop_mi_mp) {
		cif_iowrite32(CIF_MI_MP_FRAME |
			CIF_JPE_STATUS_ENCODE_DONE,
			dev->config.base_addr + CIF_MI_ICR);
		cif_iowrite32AND_verify(~(CIF_MI_CTRL_MP_ENABLE_IN |
			CIF_MI_CTRL_JPEG_ENABLE |
			CIF_MI_CTRL_RAW_ENABLE),
			dev->config.base_addr + CIF_MI_CTRL, ~0);
	}
}

static void cif_isp10_requeue_bufs(
	struct cif_isp10_device *dev,
	struct cif_isp10_stream *stream)
{
	INIT_LIST_HEAD(&stream->buf_queue);
	stream->next_buf = NULL;
	stream->curr_buf = NULL;
	dev->requeue_bufs(dev, stream->id);
}

static void cif_isp10_stop_sp(
	struct cif_isp10_device *dev)
{
	int ret;

	if (dev->sp_stream.state ==
		CIF_ISP10_STATE_STREAMING) {
		dev->sp_stream.stop = true;
		ret = cif_isp10_pltfrm_event_wait_timeout(dev->dev,
			&dev->sp_stream.done,
			dev->sp_stream.state !=
		CIF_ISP10_STATE_STREAMING,
			1000000);
		dev->sp_stream.stop = false;
		if (IS_ERR_VALUE(ret)) {
			cif_isp10_pltfrm_pr_warn(NULL,
				"waiting on event returned with error %d\n",
				ret);
		}
		if (dev->config.mi_config.sp.busy)
			cif_isp10_pltfrm_pr_warn(NULL,
				"SP path still active while stopping it\n");
	}
}

static void cif_isp10_stop_mp(
	struct cif_isp10_device *dev)
{
	int ret;

	if (dev->mp_stream.state ==
		CIF_ISP10_STATE_STREAMING) {
		dev->mp_stream.stop = true;
		ret = cif_isp10_pltfrm_event_wait_timeout(dev->dev,
			&dev->mp_stream.done,
			dev->mp_stream.state !=
		CIF_ISP10_STATE_STREAMING,
			1000000);
		dev->mp_stream.stop = false;
		if (IS_ERR_VALUE(ret)) {
			cif_isp10_pltfrm_pr_warn(NULL,
				"waiting on event returned with error %d\n",
				ret);
		}
		if (dev->config.mi_config.mp.busy ||
			dev->config.jpeg_config.busy)
			cif_isp10_pltfrm_pr_warn(NULL,
				"MP path still active while stopping it\n");
	}
}

static void cif_isp10_stop_dma(
	struct cif_isp10_device *dev)
{
	unsigned long flags = 0;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	if (dev->dma_stream.state ==
		CIF_ISP10_STATE_STREAMING) {
		/* we should not stop during an active DMA transfer */
		dev->dma_stream.stop = true;
		(void)cif_isp10_pltfrm_event_wait_timeout(dev->dev,
			&dev->dma_stream.done,
			dev->dma_stream.state !=
			CIF_ISP10_STATE_STREAMING,
			50000);
		/* intentionally NOT checking dma.busy again */
		if (dev->config.mi_config.dma.busy)
			cif_isp10_pltfrm_pr_warn(NULL,
				"DMA transfer still active while stopping it\n");
		dev->dma_stream.state = CIF_ISP10_STATE_READY;
		spin_lock_irqsave(&dev->vbq_lock, flags);
		cif_isp10_requeue_bufs(dev, &dev->dma_stream);
		spin_unlock_irqrestore(&dev->vbq_lock, flags);
	}
}

static int cif_isp10_stop(
	struct cif_isp10_device *dev,
	bool stop_sp,
	bool stop_mp)
{
	unsigned long flags = 0;
	bool stop_all;
	unsigned long isp_ctrl;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, img_src state = %s, stop_sp = %d, stop_mp = %d\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_img_src_state_string(dev->img_src_state),
		stop_sp,
		stop_mp);

	if (!((stop_mp &&
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)) ||
		(stop_sp &&
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)))) {
		return 0;
	}

	stop_all = ((stop_mp && stop_sp) ||
		(stop_sp &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING)) ||
		(stop_mp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_STREAMING)));

	if (stop_all) {
		/*
		 * Modify ISP stop sequence for isp bus dead:
		 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
		 * Stop ISP(isp) ->wait for ISP isp off
		 */

		cif_isp10_stop_mp(dev);
		cif_isp10_stop_sp(dev);
		cif_isp10_stop_dma(dev);

		/* stop and clear MI, MIPI, and ISP interrupts */
		cif_iowrite32(0, dev->config.base_addr + CIF_MIPI_IMSC);
		cif_iowrite32(~0, dev->config.base_addr + CIF_MIPI_ICR);

		spin_lock_irqsave(&dev->isp_state_lock, flags);
		dev->isp_state = CIF_ISP10_STATE_STOPPING;
		spin_unlock_irqrestore(&dev->isp_state_lock, flags);
		dev->isp_stop_flags = 0;
		cif_iowrite32(CIF_ISP_OFF, dev->config.base_addr + CIF_ISP_IMSC);
		cif_iowrite32(~0, dev->config.base_addr + CIF_ISP_ICR);

		cif_iowrite32_verify(0,
			dev->config.base_addr + CIF_MI_IMSC, ~0);
		cif_iowrite32(~0, dev->config.base_addr + CIF_MI_ICR);

		cif_iowrite32AND(~CIF_MIPI_CTRL_OUTPUT_ENA,
			dev->config.base_addr + CIF_MIPI_CTRL);
		/* stop ISP */
		cif_iowrite32AND(~(CIF_ISP_CTRL_ISP_INFORM_ENABLE |
			CIF_ISP_CTRL_ISP_ENABLE),
			dev->config.base_addr + CIF_ISP_CTRL);
		cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD,
			dev->config.base_addr + CIF_ISP_CTRL);

		wait_event_interruptible_timeout(dev->isp_stop_wait,
						dev->isp_stop_flags != 0,
						HZ);

		isp_ctrl = cif_ioread32(dev->config.base_addr + CIF_ISP_CTRL);
		if ((isp_ctrl & 0x0001) != 0) {
			cif_isp10_pltfrm_pr_err(dev->dev,
				"Stop ISP Failure(0x%lx)!\n", isp_ctrl);
		} else {
			spin_lock_irqsave(&dev->isp_state_lock, flags);
			dev->isp_state = CIF_ISP10_STATE_IDLE;
			spin_unlock_irqrestore(&dev->isp_state_lock, flags);
		}

		if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
			if (IS_ERR_VALUE(cif_isp10_img_src_set_state(dev,
				CIF_ISP10_IMG_SRC_STATE_SW_STNDBY)))
				cif_isp10_pltfrm_pr_dbg(dev->dev,
					"unable to put image source into standby\n");
		}
		if (IS_ERR_VALUE(cif_isp10_set_pm_state(dev,
			CIF_ISP10_PM_STATE_SW_STNDBY)))
			cif_isp10_pltfrm_pr_dbg(dev->dev,
			"unable to put CIF into standby\n");
	} else if (stop_sp) {
		if (!dev->config.mi_config.async_updt) {
			cif_isp10_stop_mi(dev, true, false);
		}
		cif_isp10_stop_sp(dev);
		cif_iowrite32AND_verify(~CIF_MI_SP_FRAME,
			dev->config.base_addr + CIF_MI_IMSC, ~0);

	} else /* stop_mp */ {
		if (!dev->config.mi_config.async_updt) {
			cif_isp10_stop_mi(dev, false, true);
		}
		cif_isp10_stop_mp(dev);
		cif_iowrite32AND_verify(~(CIF_MI_MP_FRAME |
			CIF_JPE_STATUS_ENCODE_DONE),
			dev->config.base_addr + CIF_MI_IMSC, ~0);
	}

	if (stop_mp && (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING))
		dev->mp_stream.state = CIF_ISP10_STATE_READY;

	if (stop_sp && (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING))
		dev->sp_stream.state = CIF_ISP10_STATE_READY;

	spin_lock(&dev->vbq_lock);
	if (stop_sp) {
		dev->config.mi_config.sp.busy = false;
		cif_isp10_requeue_bufs(dev, &dev->sp_stream);
	}
	if (stop_mp) {
		dev->config.mi_config.mp.busy = false;
		cif_isp10_requeue_bufs(dev, &dev->mp_stream);
	}
	spin_unlock(&dev->vbq_lock);

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, img_src state = %s\n"
		"  MI_CTRL 0x%08x\n"
		"  ISP_CTRL 0x%08x\n"
		"  MIPI_CTRL 0x%08x\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		cif_isp10_img_src_state_string(dev->img_src_state),
		cif_ioread32(dev->config.base_addr + CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_ISP_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_CTRL));

	return 0;
}

static int cif_isp10_start(
	struct cif_isp10_device *dev,
	bool start_sp,
	bool start_mp)
{
	unsigned int ret;
	struct vb2_buffer *vb, *n;
	unsigned long flags;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, img_src state = %s, start_sp = %d, start_mp = %d\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		cif_isp10_img_src_state_string(dev->img_src_state),
		start_sp,
		start_mp);

	if (!((start_mp &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING)) ||
		(start_sp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_STREAMING))))
		return 0;

	if (CIF_ISP10_INP_IS_DMA(dev->config.input_sel) &&
		(dev->dma_stream.state < CIF_ISP10_STATE_READY)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"cannot start streaming, input source (DMA) not ready\n");
		ret = -EFAULT;
		goto err;
	}

	/* Activate MI */
	cif_isp10_start_mi(dev, start_sp, start_mp);

	if ((dev->sp_stream.state != CIF_ISP10_STATE_STREAMING) &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING)) {
		/* Activate MIPI */
		if (CIF_ISP10_INP_IS_MIPI(dev->config.input_sel))
			cif_iowrite32OR(CIF_MIPI_CTRL_OUTPUT_ENA,
				dev->config.base_addr + CIF_MIPI_CTRL);

		/* Activate ISP ! */
		if (CIF_ISP10_INP_NEED_ISP(dev->config.input_sel))
			cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD |
				CIF_ISP_CTRL_ISP_INFORM_ENABLE |
				CIF_ISP_CTRL_ISP_ENABLE,
				dev->config.base_addr + CIF_ISP_CTRL);

		spin_lock_irqsave(&dev->isp_state_lock, flags);
		dev->isp_state = CIF_ISP10_STATE_RUNNING;
		spin_unlock_irqrestore(&dev->isp_state_lock, flags);
	}

	if (start_sp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_STREAMING)) {
		dev->sp_stream.state = CIF_ISP10_STATE_STREAMING;
	}
	if (start_mp &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING)) {
		dev->mp_stream.state = CIF_ISP10_STATE_STREAMING;
	}
	ret = cif_isp10_set_pm_state(dev,
		CIF_ISP10_PM_STATE_STREAMING);
	if (IS_ERR_VALUE(ret))
		goto err;

	if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel)) {
		/*
		 * CIF spec says to wait for sufficient time after enabling
		 * the MIPI interface and before starting the sensor output.
		 */
		mdelay(1);
		/* start sensor output! */
		dev->isp_dev.frame_id = 0;
		dev->isp_dev.frame_id_setexp = 0;

		list_for_each_entry_safe(
			vb, n, &dev->isp_dev.stat, queued_entry) {
			if (vb->state == VB2_BUF_STATE_DONE) {
				cif_isp10_pltfrm_pr_info(
					dev->dev,
					"discard vb: %d\n", vb->index);
			}
		}
		// spin_unlock_irq(&dev->isp_dev.lock);

		mutex_lock(&dev->img_src_exps.mutex);
		cif_isp10_img_src_ioctl(dev->img_src,
			RK_VIDIOC_SENSOR_MODE_DATA,
			&dev->img_src_exps.data[0].data);
		cif_isp10_img_src_ioctl(dev->img_src,
			RK_VIDIOC_SENSOR_MODE_DATA,
			&dev->img_src_exps.data[1].data);
		dev->img_src_exps.data[0].v_frame_id = 0;
		dev->img_src_exps.data[1].v_frame_id = 0;
		mutex_unlock(&dev->img_src_exps.mutex);

		cif_isp10_pltfrm_rtrace_printf(dev->dev,
			"starting image source...\n");
		ret = cif_isp10_img_src_set_state(dev,
			CIF_ISP10_IMG_SRC_STATE_STREAMING);
		if (IS_ERR_VALUE(ret))
			goto err;
	} else {
		cif_isp10_pltfrm_rtrace_printf(dev->dev,
			"starting DMA...\n");
		dev->dma_stream.state = CIF_ISP10_STATE_STREAMING;
		dev->dma_stream.stop = false;
		cif_isp10_dma_next_buff(dev);
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, img_src state = %s\n"
		"  MI_CTRL 0x%08x\n"
		"  ISP_CTRL 0x%08x\n"
		"  MIPI_CTRL 0x%08x\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		cif_isp10_img_src_state_string(dev->img_src_state),
		cif_ioread32(dev->config.base_addr + CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_ISP_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_CTRL));

	return 0;
err:
	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, img_src state = %s\n"
		"  MI_CTRL 0x%08x\n"
		"  ISP_CTRL 0x%08x\n"
		"  MIPI_CTRL 0x%08x\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		cif_isp10_img_src_state_string(dev->img_src_state),
		cif_ioread32(dev->config.base_addr + CIF_MI_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_ISP_CTRL),
		cif_ioread32(dev->config.base_addr + CIF_MIPI_CTRL));
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

/* Function to be called inside ISR to update CIF ISM/DCROP/RSZ */
static int cif_isp10_update_ism_dcr_rsz(
	struct cif_isp10_device *dev)
{
	int ret = 0;

	if (dev->config.isp_config.ism_config.ism_update_needed) {
		if (dev->config.isp_config.ism_config.ism_en) {
			if (!dev->isp_dev.cif_ism_cropping)
				dev->isp_dev.cif_ism_cropping = true;
		} else {
			if (dev->isp_dev.cif_ism_cropping)
				dev->isp_dev.cif_ism_cropping = false;
		}
	}

	/*
	 * Update ISM, cif_isp10_config_ism() changes the output size of isp,
	 * so it must be called before cif_isp10_config_rsz()
	 */
	if (dev->config.isp_config.ism_config.ism_update_needed) {
		cif_isp10_config_ism(dev, false);
		if (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)
			dev->config.mp_config.rsz_config.ism_adjust = true;
		if (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)
			dev->config.sp_config.rsz_config.ism_adjust = true;

		dev->config.isp_config.ism_config.ism_update_needed = false;
		cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD,
			dev->config.base_addr + CIF_ISP_CTRL);

		if (dev->config.isp_config.ism_config.ism_en)
			dev->config.mi_config.async_updt |= CIF_ISP10_ASYNC_ISM;
	}

	/* Update RSZ */
	if ((dev->config.mp_config.rsz_config.ycflt_adjust ||
		dev->config.mp_config.rsz_config.ism_adjust)) {
		ret = cif_isp10_config_rsz(dev, CIF_ISP10_STREAM_MP, true);
		if (IS_ERR_VALUE(ret))
			goto err;
	}
	if ((dev->config.sp_config.rsz_config.ycflt_adjust ||
		dev->config.sp_config.rsz_config.ism_adjust)) {
		ret = cif_isp10_config_rsz(dev, CIF_ISP10_STREAM_SP, true);
		if (IS_ERR_VALUE(ret))
			goto err;
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

static int cif_isp10_mi_isr(unsigned int mi_mis, void *cntxt)
{
	struct cif_isp10_device *dev = cntxt;
	unsigned int mi_mis_tmp;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"\n  MI_RIS 0x%08x\n"
		"  MI_IMSC 0x%08x\n"
		"  MI_MIS 0x%08x\n",
		cif_ioread32(dev->config.base_addr + CIF_MI_RIS),
		cif_ioread32(dev->config.base_addr + CIF_MI_IMSC),
		mi_mis);

	cif_isp10_pltfrm_rtrace_printf(dev->dev,
		"MI_MIS %08x, MI_RIS %08x, MI_IMSC %08x\n",
		mi_mis,
		cif_ioread32(dev->config.base_addr + CIF_MI_RIS),
		cif_ioread32(dev->config.base_addr + CIF_MI_IMSC));

	if (mi_mis & CIF_MI_SP_FRAME) {
		dev->config.mi_config.sp.busy = false;
		cif_iowrite32(CIF_MI_SP_FRAME,
			dev->config.base_addr + CIF_MI_ICR);
		mi_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_MI_MIS);
		if (mi_mis_tmp & CIF_MI_SP_FRAME)
			cif_isp10_pltfrm_pr_err(dev->dev,
						"sp icr err: 0x%x\n",
						mi_mis_tmp);
	}

	if (mi_mis & CIF_MI_MP_FRAME) {
		dev->config.mi_config.mp.busy = false;
		cif_iowrite32(CIF_MI_MP_FRAME,
			dev->config.base_addr + CIF_MI_ICR);
		mi_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_MI_MIS);
		if (mi_mis_tmp & CIF_MI_MP_FRAME)
			cif_isp10_pltfrm_pr_err(dev->dev,
						"mp icr err: 0x%x\n",
						mi_mis_tmp);
	}
	if (mi_mis & CIF_MI_DMA_READY)
		(void)cif_isp10_dma_ready(dev);
	if (dev->config.jpeg_config.enable &&
		(cif_ioread32(dev->config.base_addr +
			CIF_JPE_STATUS_RIS) & CIF_JPE_STATUS_ENCODE_DONE))
		dev->config.jpeg_config.busy = false;

	if (!CIF_ISP10_MI_IS_BUSY(dev) &&
		!dev->config.jpeg_config.busy) {
		if (dev->config.mi_config.async_updt) {
			u32 mp_y_off_cnt_shd =
				cif_ioread32(dev->config.base_addr +
				CIF_MI_MP_Y_OFFS_CNT_SHD);
			u32 sp_y_off_cnt_shd =
				cif_ioread32(dev->config.base_addr +
				CIF_MI_SP_Y_OFFS_CNT_SHD);

			cif_iowrite32(CIF_MI_INIT_SOFT_UPD,
				dev->config.base_addr + CIF_MI_INIT);
			cif_isp10_pltfrm_pr_dbg(NULL,
				"CIF_MI_INIT_SOFT_UPD\n");
			if (!dev->config.isp_config.ism_config.ism_en &&
				(dev->config.mi_config.async_updt &
				CIF_ISP10_ASYNC_ISM))
				dev->config.mi_config.async_updt &=
					~CIF_ISP10_ASYNC_ISM;
			if (sp_y_off_cnt_shd != 0) {
				spin_lock(&dev->vbq_lock);
				cif_isp10_requeue_bufs(dev, &dev->sp_stream);
				spin_unlock(&dev->vbq_lock);
			}
			if ((mp_y_off_cnt_shd != 0) &&
				(!dev->config.jpeg_config.enable)) {
				spin_lock(&dev->vbq_lock);
				cif_isp10_requeue_bufs(dev, &dev->mp_stream);
				spin_unlock(&dev->vbq_lock);
			}
			if (((mp_y_off_cnt_shd != 0) &&
				!dev->config.jpeg_config.enable) ||
				(sp_y_off_cnt_shd != 0)) {
				cif_isp10_pltfrm_pr_dbg(dev->dev,
					"soft update too late (SP offset %d, MP offset %d)\n",
					sp_y_off_cnt_shd, mp_y_off_cnt_shd);
			}
		}

		if (dev->mp_stream.stop &&
			(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)) {
			cif_isp10_stop_mi(dev, false, true);
			dev->mp_stream.state = CIF_ISP10_STATE_READY;
			dev->mp_stream.stop = false;

			/* Turn off MRSZ since it is not needed */
			cif_iowrite32(0, dev->config.base_addr + CIF_MRSZ_CTRL);
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_MRSZ_CTRL);

			cif_isp10_pltfrm_pr_dbg(NULL,
				"MP has stopped\n");
			cif_isp10_pltfrm_event_signal(dev->dev,
				&dev->mp_stream.done);
		}
		if (dev->sp_stream.stop &&
			(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)) {
			cif_isp10_stop_mi(dev, true, false);
			dev->sp_stream.state = CIF_ISP10_STATE_READY;
			dev->sp_stream.stop = false;

			/* Turn off SRSZ since it is not needed */
			cif_iowrite32(0, dev->config.base_addr + CIF_SRSZ_CTRL);
			cif_iowrite32OR(CIF_RSZ_CTRL_CFG_UPD,
				dev->config.base_addr + CIF_SRSZ_CTRL);

			cif_isp10_pltfrm_pr_dbg(NULL,
				"SP has stopped\n");
			cif_isp10_pltfrm_event_signal(dev->dev,
				&dev->sp_stream.done);
		}

		if (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING) {
			spin_lock(&dev->vbq_lock);
			(void)cif_isp10_mi_frame_end(dev,
				CIF_ISP10_STREAM_SP);
			spin_unlock(&dev->vbq_lock);
		}
		if (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING) {
			spin_lock(&dev->vbq_lock);
			(void)cif_isp10_mi_frame_end(dev,
				CIF_ISP10_STREAM_MP);
			spin_unlock(&dev->vbq_lock);
		}

		dev->b_mi_frame_end = true;

		if (dev->dma_stream.state == CIF_ISP10_STATE_STREAMING) {
			cif_isp10_dma_next_buff(dev);
		} else {
			if ((dev->sp_stream.state ==
				CIF_ISP10_STATE_STREAMING) &&
				dev->sp_stream.curr_buf)
				dev->config.mi_config.sp.busy = true;
			if ((dev->mp_stream.state ==
				CIF_ISP10_STATE_STREAMING) &&
				dev->mp_stream.curr_buf)
				dev->config.mi_config.mp.busy = true;
		}

		if (dev->b_isp_frame_in &&
		((dev->mp_stream.state == CIF_ISP10_STATE_STREAMING) ||
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)))
			cif_isp10_update_ism_dcr_rsz(dev);
	}

	cif_iowrite32(~(CIF_MI_MP_FRAME |
		CIF_MI_SP_FRAME | CIF_MI_DMA_READY),
		dev->config.base_addr + CIF_MI_ICR);

	return 0;
}

static int cif_isp10_register_isrs(struct cif_isp10_device *dev)
{
	int ret = 0;

	cif_isp10_pltfrm_irq_register_isr(
		dev->dev,
		CIF_ISP_MIS,
		cif_isp10_isp_isr,
		dev);
	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"unable to register ISP ISR, some processing errors may go unnoticed\n");

	cif_isp10_pltfrm_irq_register_isr(
		dev->dev,
		CIF_MIPI_MIS,
		cif_isp10_mipi_isr,
		dev);
	if (IS_ERR_VALUE(ret))
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"unable to register MIPI ISR, MIPI errors may go unnoticed\n");

	ret = cif_isp10_pltfrm_irq_register_isr(
		dev->dev,
		CIF_MI_MIS,
		cif_isp10_mi_isr,
		dev);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unable to register MI ISR, aborting\n");
		goto err;
	}

	return 0;

err:
	cif_isp10_pltfrm_pr_err(dev->dev, "failed with error %d", ret);
	return ret;
}

static void cif_isp10_vs_work(struct work_struct *work)
{
	struct cif_isp10_isp_vs_work *vs_wk =
		container_of(work, struct cif_isp10_isp_vs_work, work);
	struct cif_isp10_device *dev = vs_wk->dev;

	switch (vs_wk->cmd) {
	case CIF_ISP10_VS_EXP: {
		struct cif_isp10_img_src_exp *exp =
			(struct cif_isp10_img_src_exp *)vs_wk->param;
		struct cif_isp10_img_src_ext_ctrl *exp_ctrl = exp->exp;
		struct cif_isp10_img_src_data *new_data;

		if (dev->img_src)
			cif_isp10_img_src_s_ext_ctrls(dev->img_src, exp_ctrl);
		else
			cif_isp10_pltfrm_pr_err(dev->dev,
			"dev->img_src is NULL\n");

		if (dev->img_src_exps.data[0].v_frame_id <
			dev->img_src_exps.data[1].v_frame_id)
			new_data = &dev->img_src_exps.data[0];
		else
			new_data = &dev->img_src_exps.data[1];

		mutex_lock(&dev->img_src_exps.mutex);
		new_data->v_frame_id = dev->isp_dev.frame_id +
			dev->img_src_exps.exp_valid_frms;
		cif_isp10_img_src_ioctl(dev->img_src,
			RK_VIDIOC_SENSOR_MODE_DATA,
			&new_data->data);
		mutex_unlock(&dev->img_src_exps.mutex);

		/*
		 * pr_info("%s: exp_time: %d gain: %d, frame_id: s %d, v %d\n",
		 * __func__,
		 * new_data->data.exp_time,
		 * new_data->data.gain,
		 * dev->isp_dev.frame_id,
		 * new_data->v_frame_id);
		 */

		kfree(exp->exp->ctrls);
		exp->exp->ctrls = NULL;
		kfree(exp->exp);
		exp->exp = NULL;
		kfree(exp);
		exp = NULL;
		break;
	}
	default:
		break;
	}

	kfree(vs_wk);
	vs_wk = NULL;
}

/* Public Functions */
void cif_isp10_sensor_mode_data_sync(
	struct cif_isp10_device *dev,
	unsigned int frame_id,
	struct isp_supplemental_sensor_mode_data *data)
{
	struct cif_isp10_img_src_data *last_data;
	struct cif_isp10_img_src_data *new_data;

	mutex_lock(&dev->img_src_exps.mutex);
	if (dev->img_src_exps.data[0].v_frame_id <
		dev->img_src_exps.data[1].v_frame_id) {
		last_data = &dev->img_src_exps.data[0];
		new_data = &dev->img_src_exps.data[1];
	} else {
		last_data = &dev->img_src_exps.data[1];
		new_data = &dev->img_src_exps.data[0];
	}

	if (frame_id >= new_data->v_frame_id) {
		memcpy(data,
			&new_data->data,
			sizeof(struct isp_supplemental_sensor_mode_data));
	} else {
		memcpy(data,
			&last_data->data,
			sizeof(struct isp_supplemental_sensor_mode_data));
	}
	mutex_unlock(&dev->img_src_exps.mutex);
}

int cif_isp10_streamon(
	struct cif_isp10_device *dev,
	u32 stream_ids)
{
	int ret = 0;
	bool streamon_sp = stream_ids & CIF_ISP10_STREAM_SP;
	bool streamon_mp = stream_ids & CIF_ISP10_STREAM_MP;
	bool streamon_dma = stream_ids & CIF_ISP10_STREAM_DMA;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, streamon SP = %d, streamon MP = %d, streamon DMA = %d\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		streamon_sp, streamon_mp, streamon_dma);

	if (!((streamon_sp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_STREAMING)) ||
		(streamon_mp &&
		(dev->mp_stream.state != CIF_ISP10_STATE_STREAMING))))
		return 0;

	if (streamon_sp &&
		(dev->sp_stream.state != CIF_ISP10_STATE_READY)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"cannot start streaming on SP path, path not yet enabled\n");
		ret = -EFAULT;
		goto err;
	}

	if (streamon_mp && (dev->mp_stream.state != CIF_ISP10_STATE_READY)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"cannot start streaming on MP path, path not yet enabled\n");
		ret = -EFAULT;
		goto err;
	}

	if (streamon_sp && dev->config.mi_config.raw_enable &&
		(streamon_mp ||
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING))) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"cannot start streaming on SP path when MP is active and set to RAW output\n");
		ret = -EBUSY;
		goto err;
	}

	if (streamon_mp &&
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING))
		dev->mp_stream.updt_cfg = true;
	if (streamon_sp &&
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING))
		dev->sp_stream.updt_cfg = true;

	if (streamon_sp && dev->sp_stream.updt_cfg &&
		(dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)) {
		ret = cif_isp10_stop(dev, false, true);
				if (IS_ERR_VALUE(ret))
					goto err;
				streamon_mp = true;
				dev->mp_stream.updt_cfg = true;
	}
	if (streamon_mp && dev->mp_stream.updt_cfg &&
		(dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)) {
		ret = cif_isp10_stop(dev, true, false);
			if (IS_ERR_VALUE(ret))
				goto err;

			streamon_sp = true;
			dev->sp_stream.updt_cfg = true;
	}

	stream_ids = 0;
	if (streamon_mp && dev->mp_stream.updt_cfg)
		stream_ids |= CIF_ISP10_STREAM_MP;
	if (streamon_sp && dev->sp_stream.updt_cfg)
		stream_ids |= CIF_ISP10_STREAM_SP;

	ret = cif_isp10_config_cif(dev, stream_ids);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = cif_isp10_start(dev, streamon_sp, streamon_mp);
	if (IS_ERR_VALUE(ret))
		goto err;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state));

	return 0;
err:
	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state));
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

int cif_isp10_streamoff(
	struct cif_isp10_device *dev,
	u32 stream_ids)
{
	int ret = 0;
	bool streamoff_sp = stream_ids & CIF_ISP10_STREAM_SP;
	bool streamoff_mp = stream_ids & CIF_ISP10_STREAM_MP;
	bool streamoff_dma = stream_ids & CIF_ISP10_STREAM_DMA;
	unsigned int streamoff_all = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, streamoff SP = %d, streamoff MP = %d, streamoff DMA = %d\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		streamoff_sp,
		streamoff_mp,
		streamoff_dma);

	if (dev->config.flash_mode != CIF_ISP10_FLASH_MODE_OFF &&
		((streamoff_sp &&
		(dev->mp_stream.state == CIF_ISP10_STATE_INACTIVE)) ||
		(streamoff_mp &&
		(dev->sp_stream.state == CIF_ISP10_STATE_INACTIVE))))
		cif_isp10_img_src_s_ctrl(dev->img_src,
			CIF_ISP10_CID_FLASH_MODE,
			CIF_ISP10_FLASH_MODE_OFF);

	streamoff_all = 0;
	if (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING) {
		if (streamoff_sp)
			streamoff_all |= CIF_ISP10_STREAM_SP;
	} else {
		streamoff_all |= CIF_ISP10_STREAM_SP;
	}
	if (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING) {
		if (streamoff_mp)
			streamoff_all |= CIF_ISP10_STREAM_MP;
	} else {
		streamoff_all |= CIF_ISP10_STREAM_MP;
	}
	if (streamoff_all == (CIF_ISP10_STREAM_MP | CIF_ISP10_STREAM_SP))
		drain_workqueue(dev->vs_wq);

	ret = cif_isp10_stop(dev, streamoff_sp, streamoff_mp);
	if (IS_ERR_VALUE(ret))
		goto err;
	if ((streamoff_sp) &&
		(dev->sp_stream.state == CIF_ISP10_STATE_READY))
		dev->sp_stream.state = CIF_ISP10_STATE_INACTIVE;
	if (streamoff_mp) {
		dev->config.jpeg_config.enable = false;
		dev->config.mi_config.raw_enable = false;
		dev->config.mi_config.mp.output.width = 0;
		dev->config.mi_config.mp.output.height = 0;
		dev->config.mi_config.mp.output.pix_fmt =
			CIF_UNKNOWN_FORMAT;
		if (dev->mp_stream.state == CIF_ISP10_STATE_READY)
			dev->mp_stream.state = CIF_ISP10_STATE_INACTIVE;
	}
	if (streamoff_dma) {
		cif_isp10_stop_dma(dev);
		if (dev->dma_stream.state == CIF_ISP10_STATE_READY)
			dev->dma_stream.state = CIF_ISP10_STATE_INACTIVE;
	}
	if ((dev->dma_stream.state <= CIF_ISP10_STATE_INACTIVE) &&
		(dev->mp_stream.state <= CIF_ISP10_STATE_INACTIVE) &&
		(dev->sp_stream.state <= CIF_ISP10_STATE_INACTIVE)) {
		dev->isp_dev.input_width = 0;
		dev->isp_dev.input_height = 0;
		dev->config.isp_config.ism_config.ism_en = false;
	}

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s, # frames received = %d\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state),
		dev->isp_dev.frame_id >> 1);

	return 0;
err:
	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s, DMA state = %s\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state),
		cif_isp10_state_string(dev->dma_stream.state));
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

int cif_isp10_suspend(
	struct cif_isp10_device *dev)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state));

	if ((dev->pm_state == CIF_ISP10_PM_STATE_SUSPENDED) ||
		(dev->pm_state == CIF_ISP10_PM_STATE_OFF))
		return 0;

	dev->sp_stream.saved_state = dev->sp_stream.state;
	dev->mp_stream.saved_state = dev->mp_stream.state;
	ret = cif_isp10_stop(dev, true, true);
	if (IS_ERR_VALUE(ret))
		goto err;
	ret = cif_isp10_set_pm_state(dev, CIF_ISP10_PM_STATE_SUSPENDED);
	if (IS_ERR_VALUE(ret))
		goto err;
	ret = cif_isp10_img_src_set_state(dev, CIF_ISP10_IMG_SRC_STATE_OFF);
	if (IS_ERR_VALUE(ret))
		goto err;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

int cif_isp10_resume(
	struct cif_isp10_device *dev)
{
	u32 stream_ids = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"SP state = %s, MP state = %s\n",
		cif_isp10_state_string(dev->sp_stream.state),
		cif_isp10_state_string(dev->mp_stream.state));

	if ((dev->sp_stream.saved_state == CIF_ISP10_STATE_READY) ||
		(dev->sp_stream.saved_state == CIF_ISP10_STATE_STREAMING)) {
		dev->sp_stream.updt_cfg = true;
		dev->sp_stream.state = CIF_ISP10_STATE_READY;
		if (dev->sp_stream.saved_state == CIF_ISP10_STATE_STREAMING)
			stream_ids |= CIF_ISP10_STREAM_SP;
	}
	if ((dev->mp_stream.saved_state == CIF_ISP10_STATE_READY) ||
		(dev->mp_stream.saved_state == CIF_ISP10_STATE_STREAMING)) {
		dev->mp_stream.updt_cfg = true;
		dev->mp_stream.state = CIF_ISP10_STATE_READY;
		if (dev->mp_stream.saved_state == CIF_ISP10_STATE_STREAMING)
			stream_ids |= CIF_ISP10_STREAM_MP;
	}

	if ((dev->dma_stream.saved_state == CIF_ISP10_STATE_READY) ||
		(dev->dma_stream.saved_state == CIF_ISP10_STATE_STREAMING)) {
		dev->dma_stream.state = CIF_ISP10_STATE_READY;
		if (dev->dma_stream.saved_state == CIF_ISP10_STATE_STREAMING)
			stream_ids |= CIF_ISP10_STREAM_DMA;
	}

	return cif_isp10_streamon(dev, stream_ids);
}

int cif_isp10_s_fmt(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	struct cif_isp10_strm_fmt *strm_fmt,
	u32 stride)
{
	int ret;

	cif_isp10_pltfrm_pr_dbg(NULL, "%s\n",
		cif_isp10_stream_id_string(stream_id));

	switch (stream_id) {
	case CIF_ISP10_STREAM_SP:
		ret = cif_isp10_s_fmt_sp(dev, strm_fmt, stride);
		break;
	case CIF_ISP10_STREAM_MP:
		ret = cif_isp10_s_fmt_mp(dev, strm_fmt, stride);
		break;
	case CIF_ISP10_STREAM_DMA:
		ret = cif_isp10_s_fmt_dma(dev, strm_fmt, stride);
		break;
	default:
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported stream ID %d\n", stream_id);
		ret = -EINVAL;
		goto err;
	}

	return ret;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

int cif_isp10_init(
	struct cif_isp10_device *dev,
	u32 stream_ids)
{
	int ret;

	cif_isp10_pltfrm_pr_dbg(NULL, "0x%08x\n", stream_ids);

	if (stream_ids & ~(CIF_ISP10_ALL_STREAMS)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported stream IDs 0x%08x\n",
			stream_ids);
		ret = -EINVAL;
		goto err;
	}

	/* set default input, failure is not fatal here */
	if ((dev->sp_stream.state == CIF_ISP10_STATE_DISABLED) &&
		(dev->mp_stream.state == CIF_ISP10_STATE_DISABLED)) {
		ret = cif_isp10_s_input(dev, 0);
		if (IS_ERR_VALUE(ret))
			goto err;

		dev->config.isp_config.si_enable = false;
		dev->config.isp_config.ie_config.effect =
			CIF_ISP10_IE_NONE;
	}

	if (stream_ids & CIF_ISP10_STREAM_SP)
		cif_isp10_init_stream(dev, CIF_ISP10_STREAM_SP);
	if (stream_ids & CIF_ISP10_STREAM_MP)
		cif_isp10_init_stream(dev, CIF_ISP10_STREAM_MP);
	if (stream_ids & CIF_ISP10_STREAM_DMA)
		cif_isp10_init_stream(dev, CIF_ISP10_STREAM_DMA);

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

int cif_isp10_release(
	struct cif_isp10_device *dev,
	int stream_ids)
{
	int ret;
	struct cif_isp10_stream *strm_dev;

	cif_isp10_pltfrm_pr_dbg(NULL, "0x%08x\n", stream_ids);

	if ((dev->sp_stream.state == CIF_ISP10_STATE_DISABLED) &&
		(dev->mp_stream.state == CIF_ISP10_STATE_DISABLED) &&
		(dev->dma_stream.state == CIF_ISP10_STATE_DISABLED))
		return 0;

	if (stream_ids & ~(CIF_ISP10_ALL_STREAMS)) {
		cif_isp10_pltfrm_pr_err(NULL,
			"unknown/unsupported stream IDs 0x%08x\n",
			stream_ids);
		ret = -EINVAL;
		goto err;
	}

	if (stream_ids == CIF_ISP10_STREAM_MP)
		strm_dev = &dev->mp_stream;
	else if (stream_ids == CIF_ISP10_STREAM_SP)
		strm_dev = &dev->sp_stream;
	else
		strm_dev = NULL;

	if (strm_dev) {
		if (strm_dev->metadata.d) {
			vfree(strm_dev->metadata.d);
			strm_dev->metadata.d = NULL;
			strm_dev->metadata.cnt = 0;
		}
	}

	if (stream_ids & CIF_ISP10_STREAM_SP) {
		if (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING) {
			cif_isp10_pltfrm_pr_warn(dev->dev,
				"CIF SP in streaming state, should be stopped before release, trying to stop it\n");
			ret = cif_isp10_stop(dev, true, false);
			if (IS_ERR_VALUE(ret))
				goto err;
		}
		dev->sp_stream.state = CIF_ISP10_STATE_DISABLED;
	}
	if (stream_ids & CIF_ISP10_STREAM_MP) {
		if (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING) {
			cif_isp10_pltfrm_pr_warn(dev->dev,
				"CIF MP in streaming state, should be stopped before release, trying to stop it\n");
			ret = cif_isp10_stop(dev, false, true);
			if (IS_ERR_VALUE(ret))
				goto err;
		}
		dev->mp_stream.state = CIF_ISP10_STATE_DISABLED;
	}

	if ((dev->sp_stream.state == CIF_ISP10_STATE_DISABLED) &&
		(dev->mp_stream.state == CIF_ISP10_STATE_DISABLED)) {
		if (IS_ERR_VALUE(cif_isp10_set_pm_state(dev,
			CIF_ISP10_PM_STATE_OFF)))
			cif_isp10_pltfrm_pr_warn(dev->dev,
			"CIF power off failed\n");
		if (dev->img_src) {
			if (IS_ERR_VALUE(cif_isp10_img_src_set_state(dev,
				CIF_ISP10_IMG_SRC_STATE_OFF)))
				cif_isp10_pltfrm_pr_warn(dev->dev,
					"image source power off failed\n");
			dev->img_src = NULL;
		}
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with error %d\n", ret);
	return ret;
}

struct cif_isp10_device *cif_isp10_create(
	CIF_ISP10_PLTFRM_DEVICE pdev,
	void (*sof_event)(struct cif_isp10_device *dev, __u32 frame_sequence),
	void (*requeue_bufs)(struct cif_isp10_device *dev,
					enum cif_isp10_stream_id stream_id),
	struct pltfrm_soc_cfg *soc_cfg)
{
	int ret;
	struct cif_isp10_device *dev;

	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	/* Allocate needed structures */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"memory allocation failed\n");
		ret = -ENOMEM;
		goto err;
	}
	dev->sof_event = sof_event;
	dev->requeue_bufs = requeue_bufs;

	ret = cif_isp10_pltfrm_dev_init(dev,
		&pdev, &dev->config.base_addr);
	if (IS_ERR_VALUE(ret))
		goto err;
	cif_isp10_pltfrm_debug_register_print_cb(
		dev->dev,
		(void (*)(void *, const char *))cif_isp10_debug_print_block,
		dev);

	cif_isp10_pltfrm_soc_init(dev, soc_cfg);

	ret = cif_isp10_img_srcs_init(dev);
	if (IS_ERR_VALUE(ret)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
		"cif_isp10_img_srcs_init failed\n");
		goto err;
	}

	ret = cif_isp10_register_isrs(dev);
	if (IS_ERR_VALUE(ret))
		goto err;

	dev->pm_state = CIF_ISP10_PM_STATE_OFF;
	dev->sp_stream.state = CIF_ISP10_STATE_DISABLED;
	dev->sp_stream.id = CIF_ISP10_STREAM_SP;
	dev->mp_stream.state = CIF_ISP10_STATE_DISABLED;
	dev->mp_stream.id = CIF_ISP10_STREAM_MP;
	dev->dma_stream.state = CIF_ISP10_STATE_DISABLED;
	dev->dma_stream.id = CIF_ISP10_STREAM_DMA;
	dev->config.mi_config.async_updt = 0;

	(void)cif_isp10_init(dev, CIF_ISP10_ALL_STREAMS);
	cif_isp10_pltfrm_event_init(dev->dev, &dev->dma_stream.done);
	cif_isp10_pltfrm_event_init(dev->dev, &dev->sp_stream.done);
	cif_isp10_pltfrm_event_init(dev->dev, &dev->mp_stream.done);

	dev->img_src_exps.exp_valid_frms = 2;
	mutex_init(&dev->img_src_exps.mutex);
	memset(&dev->img_src_exps.data, 0x00, sizeof(dev->img_src_exps.data));
	spin_lock_init(&dev->img_src_exps.lock);
	INIT_LIST_HEAD(&dev->img_src_exps.list);
	dev->vs_wq = alloc_workqueue("cif isp10 vs workqueue",
			WQ_UNBOUND | WQ_MEM_RECLAIM, 1);

	/* TBD: clean this up */
	init_output_formats();

	return dev;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	if (!IS_ERR_OR_NULL(dev))
		kfree(dev);
	return ERR_PTR(ret);
}

void cif_isp10_destroy(
	struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_dbg(NULL, "\n");
	if (!IS_ERR_OR_NULL(dev))
		kfree(dev);
}

int cif_isp10_g_input(
	struct cif_isp10_device *dev,
	unsigned int *input)
{
	unsigned int i;

	for (i = 0; i < dev->img_src_cnt; i++) {
		if (dev->img_src != NULL && dev->img_src == dev->img_src_array[i]) {
			*input = i;
			return 0;
		}
	}

	return -EINVAL;
}

int cif_isp10_s_input(
	struct cif_isp10_device *dev,
	unsigned int input)
{
	int ret;
	enum cif_isp10_inp inp;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"setting input to %s, [w-h]=[%d-%d], defrect=[%d-%d-%d-%d]\n",
		cif_isp10_inp_string(
		cif_isp10_input_index2inp(dev, input)),
		dev->config.img_src_output.frm_fmt.width,
		dev->config.img_src_output.frm_fmt.height,
		dev->config.img_src_output.frm_fmt.defrect.left,
		dev->config.img_src_output.frm_fmt.defrect.top,
		dev->config.img_src_output.frm_fmt.defrect.width,
		dev->config.img_src_output.frm_fmt.defrect.height);

	if (input >= dev->img_src_cnt + CIF_ISP10_INP_DMA_CNT()) {
		cif_isp10_pltfrm_pr_err(NULL,
			"invalid input %d\n", input);
		ret = -EINVAL;
		goto err;
	}

	dev->img_src = NULL;

	inp = cif_isp10_input_index2inp(dev, input);

	/* DMA -> ISP or DMA -> IE */
	if ((inp == CIF_ISP10_INP_DMA) || (inp == CIF_ISP10_INP_DMA_IE))
		dev->config.isp_config.input =
			&dev->config.mi_config.dma.output;
	else {
		dev->img_src = dev->img_src_array[input];
		dev->config.isp_config.input =
			&dev->config.img_src_output.frm_fmt;
	}
	dev->config.input_sel = inp;

	return 0;
err:
	cif_isp10_pltfrm_pr_err(NULL,
		"failed with error %d\n", ret);
	return ret;
}

const char *cif_isp10_g_input_name(
	struct cif_isp10_device *dev,
	unsigned int input_index)
{
	if (input_index >=
		dev->img_src_cnt + CIF_ISP10_INP_DMA_CNT()) {
		cif_isp10_pltfrm_pr_dbg(NULL,
			"index %d out of bounds\n",
			input_index);
		return NULL;
	}

	if (input_index < dev->img_src_cnt)
		return cif_isp10_img_src_g_name(
			dev->img_src_array[input_index]);
	else
		return cif_isp10_inp_string(CIF_ISP10_INP_DMA +
			((input_index - dev->img_src_cnt) << 24));
}

int cif_isp10_qbuf(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream,
	struct cif_isp10_buffer *buf)
{
	int ret = 0;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"%s\n",
		cif_isp10_stream_id_string(stream));

	switch (stream) {
	case CIF_ISP10_STREAM_SP:
		list_add_tail(&buf->queue, &dev->sp_stream.buf_queue);
		break;
	case CIF_ISP10_STREAM_MP:
		list_add_tail(&buf->queue, &dev->mp_stream.buf_queue);
		break;
	case CIF_ISP10_STREAM_DMA:
		list_add_tail(&buf->queue, &dev->dma_stream.buf_queue);
		if ((dev->dma_stream.state == CIF_ISP10_STATE_STREAMING) &&
			!CIF_ISP10_MI_IS_BUSY(dev))
			cif_isp10_dma_next_buff(dev);
		break;
	case CIF_ISP10_STREAM_ISP:
		WARN_ON(1);
		break;
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown stream %d\n", stream);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

int cif_isp10_reqbufs(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id strm,
	struct v4l2_requestbuffers *req)
{
	struct cif_isp10_stream *strm_dev = NULL;

	switch (strm) {
	case CIF_ISP10_STREAM_MP:
		strm_dev = &dev->mp_stream;
		break;
	case CIF_ISP10_STREAM_SP:
		strm_dev = &dev->sp_stream;
		break;
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown stream id%d\n", strm);
		break;
	}

	strm_dev->metadata.cnt = req->count;

	return 0;
}

int cif_isp10_s_exp(
	struct cif_isp10_device *dev,
	struct cif_isp10_img_src_ext_ctrl *exp_ctrl)
{
	struct cif_isp10_img_src_exp *exp = NULL;
	unsigned long lock_flags;
	int retval;

	if (!dev->vs_wq)
		return -ENODEV;

	exp = kmalloc(sizeof(*exp), GFP_KERNEL);
	if (!exp) {
		retval = -ENOMEM;
		goto failed;
	}

	exp->exp = exp_ctrl;

	spin_lock_irqsave(&dev->img_src_exps.lock, lock_flags);
	list_add_tail(&exp->list, &dev->img_src_exps.list);
	spin_unlock_irqrestore(&dev->img_src_exps.lock, lock_flags);

	return 0;

failed:
	return retval;
}

int cif_isp10_s_isp_metadata(
	struct cif_isp10_device *dev,
	struct cif_isp10_isp_readout_work *readout_work,
	struct cifisp_isp_other_cfg *new_other,
	struct cifisp_isp_meas_cfg *new_meas,
	struct cifisp_stat_buffer *new_stats)
{
	unsigned int stream_id =
		readout_work->stream_id;
	struct vb2_buffer *vb =
		readout_work->vb;
	struct cif_isp10_stream *strm_dev = NULL;
	struct v4l2_buffer_metadata_s *metadata;
	struct cifisp_isp_metadata *isp_last;

	switch (stream_id) {
	case CIF_ISP10_STREAM_MP:
		strm_dev = &dev->mp_stream;
		break;
	case CIF_ISP10_STREAM_SP:
		strm_dev = &dev->sp_stream;
		break;
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown stream id%d\n", stream_id);
		break;
	}

	if (vb && strm_dev->metadata.d) {
		metadata = (struct v4l2_buffer_metadata_s *)
			(strm_dev->metadata.d +
			vb->index * CAMERA_METADATA_LEN);

		metadata->frame_id = readout_work->frame_id;
		isp_last =
			(struct cifisp_isp_metadata *)metadata->isp;

		if (new_meas) {
			if ((isp_last->meas_cfg.s_frame_id == 0xffffffff) ||
				(isp_last->meas_cfg.s_frame_id <
				new_meas->s_frame_id)) {
				memcpy(&isp_last->meas_cfg,
					new_meas,
					sizeof(struct cifisp_isp_meas_cfg));
			}
		}

		if (new_other) {
			if ((isp_last->other_cfg.s_frame_id == 0xffffffff) ||
				(isp_last->other_cfg.s_frame_id <
				new_other->s_frame_id)) {
				memcpy(&isp_last->other_cfg,
					new_other,
					sizeof(struct cifisp_isp_other_cfg));
			}
		}

		if (new_stats) {
			memcpy(&isp_last->meas_stat,
				new_stats,
				sizeof(struct cifisp_stat_buffer));
			metadata->sensor.exp_time =
				new_stats->sensor_mode.exp_time;
			metadata->sensor.gain =
				new_stats->sensor_mode.gain;
		} else {
			isp_last->meas_stat.meas_type = 0x00;
		}
	}

	if (vb) {
		cif_isp10_pltfrm_pr_dbg(NULL,
				"frame done\n");
		wake_up(&vb->vb2_queue->done_wq);
	}

	return 0;
}

int cif_isp10_mmap(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	struct vm_area_struct *vma)
{
	struct cif_isp10_stream *strm_dev;
	void *mem_vaddr;
	int retval = 0, pages;
	unsigned long mem_size;

	switch (stream_id) {
	case CIF_ISP10_STREAM_MP:
		strm_dev = &dev->mp_stream;
		break;
	case CIF_ISP10_STREAM_SP:
		strm_dev = &dev->sp_stream;
		break;
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown stream id%d\n", stream_id);
		return -ENODEV;
	}

	mem_size = vma->vm_end - vma->vm_start;
	if (mem_size > strm_dev->metadata.cnt * CAMERA_METADATA_LEN) {
		retval = -ENOMEM;
		cif_isp10_pltfrm_pr_err(dev->dev,
			"mmap size(0x%lx) > metadata memory size(0x%lx), so failed!",
			mem_size,
			(unsigned long)(strm_dev->metadata.cnt
			* CAMERA_METADATA_LEN));
		goto done;
	}

	pages = PAGE_ALIGN(vma->vm_end - vma->vm_start);
	mem_vaddr = (struct v4l2_buffer_metadata_s *)vmalloc_user(pages);
	if (!mem_vaddr) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"vmalloc (%d bytes) failed for %s metadata\n",
			pages,
			(stream_id == CIF_ISP10_STREAM_MP) ? "mp" : "sp");
		retval = -ENOMEM;
		goto done;
	}

	/* Try to remap memory */
	retval = remap_vmalloc_range(vma, mem_vaddr, 0);
	if (retval < 0) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"mmap: remap failed with error %d. ", retval);
		vfree(mem_vaddr);
		goto done;
	}

	strm_dev->metadata.d = (unsigned char *)mem_vaddr;

	vma->vm_private_data = (void *)&strm_dev->metadata;

done:
	return retval;
}

int cif_isp10_get_target_frm_size(
	struct cif_isp10_device *dev,
	u32 *target_width,
	u32 *target_height)
{
	if (dev->sp_stream.state >= CIF_ISP10_STATE_READY) {
		if ((dev->mp_stream.state >= CIF_ISP10_STATE_READY) &&
			(dev->config.mi_config.mp.output.width >
			dev->config.mi_config.sp.output.width))
			*target_width =
				dev->config.mi_config.mp.output.width;
		else
			*target_width =
				dev->config.mi_config.sp.output.width;
		if ((dev->mp_stream.state >= CIF_ISP10_STATE_READY) &&
			(dev->config.mi_config.mp.output.height >
			dev->config.mi_config.sp.output.height))
			*target_height =
				dev->config.mi_config.mp.output.height;
		else
			*target_height =
				dev->config.mi_config.sp.output.height;
	} else if (dev->mp_stream.state >= CIF_ISP10_STATE_READY) {
		*target_width = dev->config.mi_config.mp.output.width;
		*target_height = dev->config.mi_config.mp.output.height;
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"cannot get target frame size, no path ready, state(mp[%d]-sp[%d])\n",
			dev->mp_stream.state, dev->sp_stream.state);
		return -EFAULT;
	}
	return 0;
}

int cif_isp10_calc_isp_cropping(
	struct cif_isp10_device *dev,
	u32 *width,
	u32 *height,
	u32 *h_offs,
	u32 *v_offs)
{
	int ret = 0;
	u32 input_width;
	u32 input_height;
	u32 target_width;
	u32 target_height;

	if (IS_ERR_OR_NULL(dev->config.isp_config.input)) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"no input selected for ISP\n");
		ret = -EFAULT;
		goto err;
	}

	input_width = dev->config.isp_config.input->defrect.width;
	input_height = dev->config.isp_config.input->defrect.height;

	ret = cif_isp10_get_target_frm_size(dev,
		&target_width, &target_height);
	if (IS_ERR_VALUE(ret))
		goto err;

	*width = input_width;
	*height = input_width * target_height / target_width;
	*v_offs = 0;
	*h_offs = 0;
	*height &= ~1;
	if (*height < input_height)
		/* vertical cropping */
		*v_offs = (input_height - *height) >> 1;
	else if (*height > input_height) {
		/* horizontal cropping */
		*height = input_height;
		*width = input_height * target_width / target_height;
		*width &= ~1;
		*h_offs = (input_width - *width) >> 1;
	}

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

int cif_isp10_calc_min_out_buff_size(
	struct cif_isp10_device *dev,
	enum cif_isp10_stream_id stream_id,
	u32 *size, bool payload)
{
	int ret = 0;
	enum cif_isp10_pix_fmt pix_fmt;
	u32 llength;
	u32 height;
	u32 bpp;
	struct cif_isp10_mi_path_config *mi_path;
	struct cif_isp10_stream *stream;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"%s\n",
		cif_isp10_stream_id_string(stream_id));

	if (stream_id == CIF_ISP10_STREAM_SP) {
		mi_path = &dev->config.mi_config.sp;
		stream = &dev->sp_stream;
	} else if (stream_id == CIF_ISP10_STREAM_MP) {
		mi_path = &dev->config.mi_config.mp;
		stream = &dev->mp_stream;
	} else if (stream_id == CIF_ISP10_STREAM_DMA) {
		mi_path = &dev->config.mi_config.dma;
		stream = &dev->dma_stream;
	} else {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"cannot calculate buffer size for this stream (%s)\n",
			cif_isp10_stream_id_string(stream_id));
		ret = -EINVAL;
		goto err;
	}

	if (stream->state < CIF_ISP10_STATE_READY) {
		cif_isp10_pltfrm_pr_err(NULL,
			"cannot calculate buffer size, %s stream not ready\n",
			cif_isp10_stream_id_string(stream_id));
		ret = -EINVAL;
		goto err;
	}
	pix_fmt = mi_path->output.pix_fmt;
	llength = mi_path->llength;
	height = mi_path->output.height;
	cif_isp10_pltfrm_pr_dbg(NULL,
		"mi_path->llength: 0x%x\n",
		mi_path->llength);

	if ((CIF_ISP10_PIX_FMT_IS_RAW_BAYER(pix_fmt) &&
		CIF_ISP10_PIX_FMT_GET_BPP(pix_fmt) > 8) || payload)
		/* RAW input > 8BPP is stored with 16BPP by MI */
		bpp = 16;
	else
		bpp = CIF_ISP10_PIX_FMT_GET_BPP(pix_fmt);
	*size = llength * height * bpp / 8;

	cif_isp10_pltfrm_pr_dbg(NULL,
		"calculated buffer size: %d\n",
		*size);

	return 0;
err:
	cif_isp10_pltfrm_pr_err(dev->dev,
		"failed with err %d\n", ret);
	return ret;
}

int cif_isp10_s_ctrl(
	struct cif_isp10_device *dev,
	const enum cif_isp10_cid id,
	int val)
{
	cif_isp10_pltfrm_pr_dbg(NULL,
		"id %d, val %d\n",
		id, val);

	switch (id) {
	case CIF_ISP10_CID_SUPER_IMPOSE:
		dev->config.isp_config.si_enable = val;
		break;
	case CIF_ISP10_CID_IMAGE_EFFECT:
		if ((u32)val > CIF_ISP10_IE_NONE) {
			cif_isp10_pltfrm_pr_err(NULL,
				"unknown/unsupported image effect %d\n", val);
			return -EINVAL;
		}
		dev->config.isp_config.ie_config.effect = val;
		break;
	case CIF_ISP10_CID_JPEG_QUALITY:
		if ((u32)val > 100) {
			cif_isp10_pltfrm_pr_err(NULL,
				"JPEG quality (%d) must be in [1..100]\n", val);
			return -EINVAL;
		}
		dev->config.jpeg_config.ratio = val;
		break;
	case CIF_ISP10_CID_FLASH_MODE:
		if ((u32)val > CIF_ISP10_FLASH_MODE_TORCH) {
			cif_isp10_pltfrm_pr_err(NULL,
				"unknown/unsupported flash mode (%d)\n", val);
			return -EINVAL;
		}
		dev->config.flash_mode = val;
		cif_isp10_img_src_s_ctrl(
			dev->img_src,
			CIF_ISP10_CID_FLASH_MODE,
			dev->config.flash_mode);
		if (dev->config.flash_mode == CIF_ISP10_FLASH_MODE_FLASH) {
			do_gettimeofday(&dev->flash_t.mainflash_start_t);
			dev->flash_t.mainflash_start_t.tv_usec +=
				dev->flash_t.flash_turn_on_time;
			dev->flash_t.mainflash_end_t =
				dev->flash_t.mainflash_start_t;
			dev->flash_t.mainflash_end_t.tv_sec +=
				dev->flash_t.flash_on_timeout;
		} else if (dev->config.flash_mode ==
			   CIF_ISP10_FLASH_MODE_TORCH) {
			do_gettimeofday(&dev->flash_t.preflash_start_t);
			dev->flash_t.preflash_end_t =
				dev->flash_t.preflash_start_t;
			dev->flash_t.preflash_end_t.tv_sec = 0x00;
			dev->flash_t.preflash_end_t.tv_usec = 0x00;
		} else if (dev->config.flash_mode == CIF_ISP10_FLASH_MODE_OFF) {
			do_gettimeofday(&dev->flash_t.preflash_end_t);
			if (dev->flash_t.preflash_end_t.tv_sec * 1000000 +
			    dev->flash_t.preflash_end_t.tv_usec <
			    dev->flash_t.mainflash_end_t.tv_sec * 1000000 +
			    dev->flash_t.mainflash_end_t.tv_usec) {
				dev->flash_t.mainflash_end_t =
					dev->flash_t.preflash_end_t;
			}
		}
		break;
	case CIF_ISP10_CID_WB_TEMPERATURE:
	case CIF_ISP10_CID_ANALOG_GAIN:
	case CIF_ISP10_CID_EXPOSURE_TIME:
	case CIF_ISP10_CID_BLACK_LEVEL:
	case CIF_ISP10_CID_FOCUS_ABSOLUTE:
	case CIF_ISP10_CID_AUTO_N_PRESET_WHITE_BALANCE:
	case CIF_ISP10_CID_SCENE_MODE:
	case CIF_ISP10_CID_AUTO_FPS:
	case CIF_ISP10_CID_HFLIP:
	case CIF_ISP10_CID_VFLIP:
		return cif_isp10_img_src_s_ctrl(dev->img_src,
			id, val);
	default:
		cif_isp10_pltfrm_pr_err(dev->dev,
			"unknown/unsupported control %d\n", id);
		return -EINVAL;
	}

	return 0;
}

/* end */

enum {
	isp_data_loss = 0,
	isp_pic_size_err,
	mipi_fifo_err,
	dphy_err_sot,
	dphy_err_sot_sync,
	dphy_err_eot_sync,
	dphy_err_ctrl,
	csi_err_protocol,
	csi_ecc1_err,
	csi_ecc2_err,
	csi_cs_err,
};

static void cif_isp10_hw_restart(struct cif_isp10_device *dev)
{
	cif_isp10_pltfrm_pr_dbg(NULL, "\n");

	cif_isp10_hw_errors[isp_pic_size_err].count = 0;
	cif_isp10_hw_errors[isp_data_loss].count = 0;
	cif_isp10_hw_errors[csi_err_protocol].count = 0;
	cif_isp10_hw_errors[csi_ecc1_err].count = 0;
	cif_isp10_hw_errors[csi_ecc2_err].count = 0;
	cif_isp10_hw_errors[csi_cs_err].count = 0;
	cif_iowrite32(0x00000841, dev->config.base_addr + CIF_IRCL);
	cif_iowrite32(0x0, dev->config.base_addr + CIF_IRCL);

	/* enable mipi frame end interrupt */
	cif_iowrite32(CIF_MIPI_FRAME_END,
		      dev->config.base_addr + CIF_MIPI_IMSC);
	/* enable csi protocol errors interrupts */
	cif_iowrite32OR(CIF_MIPI_ERR_CSI,
			dev->config.base_addr + CIF_MIPI_IMSC);
	/* enable dphy errors interrupts */
	cif_iowrite32OR(CIF_MIPI_ERR_DPHY,
			dev->config.base_addr + CIF_MIPI_IMSC);
	/* add fifo error */
	cif_iowrite32OR(CIF_MIPI_SYNC_FIFO_OVFLW(3),
			dev->config.base_addr + CIF_MIPI_IMSC);
	/* add data overflow_error */
	cif_iowrite32OR(CIF_MIPI_ADD_DATA_OVFLW,
			dev->config.base_addr + CIF_MIPI_IMSC);

	cif_iowrite32(0x0,
		      dev->config.base_addr + CIF_MI_MP_Y_OFFS_CNT_INIT);
	cif_iowrite32(0x0,
		      dev->config.base_addr +
		      CIF_MI_MP_CR_OFFS_CNT_INIT);
	cif_iowrite32(0x0,
		      dev->config.base_addr +
		      CIF_MI_MP_CB_OFFS_CNT_INIT);
	cif_iowrite32(0x0,
		      dev->config.base_addr + CIF_MI_SP_Y_OFFS_CNT_INIT);
	cif_iowrite32(0x0,
		      dev->config.base_addr +
		      CIF_MI_SP_CR_OFFS_CNT_INIT);
	cif_iowrite32(0x0,
		      dev->config.base_addr +
		      CIF_MI_SP_CB_OFFS_CNT_INIT);
	cif_iowrite32OR(CIF_MI_CTRL_INIT_OFFSET_EN,
			dev->config.base_addr + CIF_MI_CTRL);

	/* Enable ISP ! */
	cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD |
			CIF_ISP_CTRL_ISP_INFORM_ENABLE |
			CIF_ISP_CTRL_ISP_ENABLE,
			dev->config.base_addr + CIF_ISP_CTRL);
	/* enable MIPI */
	cif_iowrite32OR(CIF_MIPI_CTRL_OUTPUT_ENA,
			dev->config.base_addr + CIF_MIPI_CTRL);
}

int cif_isp10_mipi_isr(unsigned int mipi_mis, void *cntxt)
{
	struct cif_isp10_device *dev =
	    (struct cif_isp10_device *)cntxt;
	unsigned int mipi_ris = 0;

	mipi_ris = cif_ioread32(dev->config.base_addr +	CIF_MIPI_RIS);
	mipi_mis = cif_ioread32(dev->config.base_addr + CIF_MIPI_MIS);

	cif_isp10_pltfrm_rtrace_printf(dev->dev,
		"MIPI_MIS %08x, MIPI_RIS %08x, MIPI_IMSC %08x\n",
		mipi_mis,
		mipi_ris,
		cif_ioread32(dev->config.base_addr + CIF_MIPI_IMSC));

	cif_iowrite32(~0,
		dev->config.base_addr + CIF_MIPI_ICR);

	if (mipi_mis & CIF_MIPI_ERR_DPHY) {
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"CIF_MIPI_ERR_DPHY: 0x%x\n", mipi_mis);

		/*
		 * Disable DPHY errctrl interrupt, because this dphy
		 * erctrl signal is assert and until the next changes
		 * in line state. This time is may be too long and cpu
		 * is hold in this interrupt.
		 */
		if (mipi_mis & CIF_MIPI_ERR_CTRL(3))
			cif_iowrite32AND(~(CIF_MIPI_ERR_CTRL(3)),
				dev->config.base_addr + CIF_MIPI_IMSC);
	}

	if (mipi_mis & CIF_MIPI_ERR_CSI) {
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"CIF_MIPI_ERR_CSI: 0x%x\n", mipi_mis);
	}

	if (mipi_mis & CIF_MIPI_SYNC_FIFO_OVFLW(3)) {
		cif_isp10_pltfrm_pr_warn(dev->dev,
			"CIF_MIPI_SYNC_FIFO_OVFLW: 0x%x\n", mipi_mis);
	}

	if (mipi_mis == CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		cif_iowrite32OR(CIF_MIPI_ERR_CTRL(3),
			dev->config.base_addr + CIF_MIPI_IMSC);
	}

	mipi_mis = cif_ioread32(dev->config.base_addr + CIF_MIPI_MIS);

	if (mipi_mis) {
		cif_isp10_pltfrm_pr_err(dev->dev,
			"mipi_mis icr err: 0x%x\n", mipi_mis);
	}

	return 0;
}

/* ======================================================================== */
int cif_isp10_isp_isr(unsigned int isp_mis, void *cntxt)
{
	struct cif_isp10_device *dev =
	    (struct cif_isp10_device *)cntxt;
	unsigned int isp_mis_tmp = 0;
	unsigned int isp_err = 0;
	struct timeval tv;

	cif_isp10_pltfrm_pr_dbg(dev->dev,
		"ISP_MIS %08x, ISP_RIS %08x, ISP_IMSC %08x\n",
		isp_mis,
		cif_ioread32(dev->config.base_addr + CIF_ISP_RIS),
		cif_ioread32(dev->config.base_addr + CIF_ISP_IMSC));

	if (isp_mis & CIF_ISP_OFF) {
		cif_iowrite32(CIF_ISP_OFF,
				dev->config.base_addr + CIF_ISP_ICR);
		cif_isp10_pltfrm_pr_dbg(dev->dev, "ISP Stop Interrupt!\n");
		dev->isp_stop_flags = 1;
		wake_up_interruptible(&dev->isp_stop_wait);
		return 0;
	}

	if (isp_mis & CIF_ISP_V_START) {
		struct cif_isp10_isp_vs_work *vs_wk;
		struct cif_isp10_img_src_exp *exp;

		do_gettimeofday(&tv);
		dev->b_isp_frame_in = false;
		dev->b_mi_frame_end = false;
		cifisp_v_start(&dev->isp_dev, &tv);

		/* BIT 2(current field information): 0 = odd, 1 = even */
		if (PLTFRM_CAM_ITF_INTERLACE(dev->config.cam_itf.type))
			dev->config.mi_config.sp.field_flag =
				(cif_ioread32(dev->config.base_addr +
				 CIF_ISP_FLAGS_SHD) & CIF_ISP_FLAGS_SHD_FIELD_INFO)
				>> CIF_ISP_FLAGS_SHD_FIELD_BIT;

		cif_iowrite32(CIF_ISP_V_START,
		      dev->config.base_addr + CIF_ISP_ICR);
		isp_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_V_START)
			cif_isp10_pltfrm_pr_err(dev->dev,
						"isp icr v_statr err: 0x%x\n",
						isp_mis_tmp);

		if (!dev->config.mi_config.async_updt) {
			cif_iowrite32OR(CIF_ISP_CTRL_ISP_GEN_CFG_UPD,
				dev->config.base_addr + CIF_ISP_CTRL);
			cif_isp10_pltfrm_pr_dbg(NULL,
				"CIF_ISP_CTRL_ISP_GEN_CFG_UPD\n");
		}
		if (dev->sof_event)
			dev->sof_event(dev, dev->isp_dev.frame_id >> 1);
		spin_lock(&dev->img_src_exps.lock);
		if (!list_empty(&dev->img_src_exps.list)) {
			exp = list_first_entry(&dev->img_src_exps.list,
				struct cif_isp10_img_src_exp,
				list);
			list_del(&exp->list);
		} else {
			exp = NULL;
		}
		spin_unlock(&dev->img_src_exps.lock);

		if (exp) {
			vs_wk = kmalloc(sizeof(*vs_wk),
				GFP_KERNEL);
			if (vs_wk) {
				vs_wk->cmd = CIF_ISP10_VS_EXP;
				vs_wk->dev = dev;
				vs_wk->param = (void *)exp;
				INIT_WORK((struct work_struct *)&vs_wk->work,
					cif_isp10_vs_work);
				if (!queue_work(dev->vs_wq,
					(struct work_struct *)&vs_wk->work)) {
					cif_isp10_pltfrm_pr_err(dev->dev,
					"%s: queue work failed\n", __func__);
					kfree(vs_wk);
				}
			}
		}
	}

	if (isp_mis & CIF_ISP_FRAME_IN) {
		do_gettimeofday(&tv);
		cif_iowrite32(CIF_ISP_FRAME_IN,
			      dev->config.base_addr + CIF_ISP_ICR);
		cifisp_frame_in(&dev->isp_dev, &tv);
	}

	if ((isp_mis & (CIF_ISP_DATA_LOSS | CIF_ISP_PIC_SIZE_ERROR))) {
		dev->sp_stream.stall = true;
		dev->mp_stream.stall = true;

		if ((isp_mis & CIF_ISP_PIC_SIZE_ERROR)) {
			/* Clear pic_size_error */
			cif_iowrite32(CIF_ISP_PIC_SIZE_ERROR,
				dev->config.base_addr +
				CIF_ISP_ICR);
			cif_isp10_hw_errors[isp_pic_size_err].count++;
			isp_err =
			    cif_ioread32(dev->config.base_addr +
					 CIF_ISP_ERR);
			dev_err(dev->dev,
				"CIF_ISP_PIC_SIZE_ERROR (0x%08x)",
				isp_err);
			cif_iowrite32(isp_err,
				dev->config.base_addr +
				CIF_ISP_ERR_CLR);
		} else if ((isp_mis & CIF_ISP_DATA_LOSS)) {
			/* Clear data_loss */
			cif_iowrite32(CIF_ISP_DATA_LOSS,
				dev->config.base_addr +
				CIF_ISP_ICR);
			cif_isp10_hw_errors[isp_data_loss].count++;
			dev_err(dev->dev,
				"CIF_ISP_DATA_LOSS\n");
			cif_iowrite32(CIF_ISP_DATA_LOSS,
				      dev->config.base_addr +
				      CIF_ISP_ICR);
		}

		spin_lock(&dev->isp_state_lock);
		if (dev->isp_state == CIF_ISP10_STATE_RUNNING) {
			/* Stop ISP */
			cif_iowrite32AND(~CIF_ISP_CTRL_ISP_INFORM_ENABLE &
					~CIF_ISP_CTRL_ISP_ENABLE,
					dev->config.base_addr + CIF_ISP_CTRL);
			/* isp_update */
			cif_iowrite32OR(CIF_ISP_CTRL_ISP_CFG_UPD,
					dev->config.base_addr + CIF_ISP_CTRL);
			cif_isp10_hw_restart(dev);
		}
		spin_unlock(&dev->isp_state_lock);
	}

	if (isp_mis & CIF_ISP_FRAME_IN) {
		cif_iowrite32(CIF_ISP_FRAME_IN,
			dev->config.base_addr + CIF_ISP_ICR);
		isp_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME_IN)
			cif_isp10_pltfrm_pr_err(dev->dev,
						"isp icr frame_in err: 0x%x\n",
						isp_mis_tmp);

		/* restart MI if CIF has run out of buffers */
		if (!CIF_ISP10_INP_IS_DMA(dev->config.input_sel) &&
			!CIF_ISP10_MI_IS_BUSY(dev) &&
			!dev->config.jpeg_config.busy &&
			(dev->config.mi_config.async_updt ||
			(!dev->sp_stream.next_buf &&
			!dev->mp_stream.next_buf))){
			u32 mi_isr = 0;

			if (dev->sp_stream.state == CIF_ISP10_STATE_STREAMING)
				mi_isr |= CIF_MI_SP_FRAME;
			if (dev->mp_stream.state == CIF_ISP10_STATE_STREAMING)
				mi_isr |= CIF_MI_MP_FRAME;
			cif_iowrite32(mi_isr,
				      dev->config.base_addr + CIF_MI_ISR);
		}
	}

	if (isp_mis & CIF_ISP_FRAME) {
		/* Clear Frame In (ISP) */
		cif_iowrite32(CIF_ISP_FRAME,
			      dev->config.base_addr + CIF_ISP_ICR);
		isp_mis_tmp = cif_ioread32(dev->config.base_addr + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME)
			cif_isp10_pltfrm_pr_err(dev->dev,
						"isp icr frame end err: 0x%x\n",
						isp_mis_tmp);

		if (dev->b_mi_frame_end)
			cif_isp10_update_ism_dcr_rsz(dev);
		dev->b_isp_frame_in = true;
	}

	cifisp_isp_isr(&dev->isp_dev, isp_mis);

	return 0;
}

/* ======================================================================== */

void init_output_formats(void)
{
	unsigned int i = 0;
	int ret = 0;		/* RF*/
	int xgold_num_format = 0;	/*RF*/

	xgold_num_format =
	    (sizeof(cif_isp10_output_format) / sizeof(struct cif_isp10_fmt));

	for (i = 0; i < xgold_num_format; i++) {
		struct v4l2_fmtdesc fmtdesc;

		memset(&fmtdesc, 0, sizeof(fmtdesc));
		fmtdesc.index = i;
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		strlcpy((&fmtdesc)->description,
			cif_isp10_output_format[(&fmtdesc)->index].name,
			sizeof((&fmtdesc)->description));
		(&fmtdesc)->pixelformat =
		    cif_isp10_output_format[(&fmtdesc)->index].fourcc;
		(&fmtdesc)->flags =
		    cif_isp10_output_format[(&fmtdesc)->index].flags;

		if (ret < 0)
			break;

		output_formats[i] = fmtdesc;
	}
}

int get_cif_isp10_output_format_size(void)
{
	return sizeof(cif_isp10_output_format) / sizeof(struct cif_isp10_fmt);
}

struct cif_isp10_fmt *get_cif_isp10_output_format(int index)
{
	struct cif_isp10_fmt *fmt = NULL;

	if ((index >= 0) && (index < get_cif_isp10_output_format_size()))
		fmt = &cif_isp10_output_format[index];

	return fmt;
}

struct v4l2_fmtdesc *get_cif_isp10_output_format_desc(int index)
{
	struct v4l2_fmtdesc *desc = NULL;

	if ((index >= 0) && (index < get_cif_isp10_output_format_desc_size()))
		desc = &output_formats[index];

	return desc;
}

int get_cif_isp10_output_format_desc_size(void)
{
	return ARRAY_SIZE(cif_isp10_output_format);
}
