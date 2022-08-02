// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - V4l capture device
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "rkisp1-common.h"

/*
 * NOTE: There are two capture video devices in rkisp1, selfpath and mainpath.
 *
 * differences between selfpath and mainpath
 * available mp sink input: isp
 * available sp sink input : isp, dma(TODO)
 * available mp sink pad fmts: yuv422, raw
 * available sp sink pad fmts: yuv422, yuv420......
 * available mp source fmts: yuv, raw, jpeg(TODO)
 * available sp source fmts: yuv, rgb
 */

#define RKISP1_SP_DEV_NAME	RKISP1_DRIVER_NAME "_selfpath"
#define RKISP1_MP_DEV_NAME	RKISP1_DRIVER_NAME "_mainpath"

#define RKISP1_MIN_BUFFERS_NEEDED 3

enum rkisp1_plane {
	RKISP1_PLANE_Y	= 0,
	RKISP1_PLANE_CB	= 1,
	RKISP1_PLANE_CR	= 2
};

/*
 * @fourcc: pixel format
 * @fmt_type: helper filed for pixel format
 * @uv_swap: if cb cr swapped, for yuv
 * @write_format: defines how YCbCr self picture data is written to memory
 * @output_format: defines sp output format
 * @mbus: the mbus code on the src resizer pad that matches the pixel format
 */
struct rkisp1_capture_fmt_cfg {
	u32 fourcc;
	u8 uv_swap;
	u32 write_format;
	u32 output_format;
	u32 mbus;
};

struct rkisp1_capture_ops {
	void (*config)(struct rkisp1_capture *cap);
	void (*stop)(struct rkisp1_capture *cap);
	void (*enable)(struct rkisp1_capture *cap);
	void (*disable)(struct rkisp1_capture *cap);
	void (*set_data_path)(struct rkisp1_capture *cap);
	bool (*is_stopped)(struct rkisp1_capture *cap);
};

struct rkisp1_capture_config {
	const struct rkisp1_capture_fmt_cfg *fmts;
	int fmt_size;
	struct {
		u32 y_size_init;
		u32 cb_size_init;
		u32 cr_size_init;
		u32 y_base_ad_init;
		u32 cb_base_ad_init;
		u32 cr_base_ad_init;
		u32 y_offs_cnt_init;
		u32 cb_offs_cnt_init;
		u32 cr_offs_cnt_init;
	} mi;
};

/*
 * The supported pixel formats for mainpath. NOTE, pixel formats with identical 'mbus'
 * are grouped together. This is assumed and used by the function rkisp1_cap_enum_mbus_codes
 */
static const struct rkisp1_capture_fmt_cfg rkisp1_mp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	},
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_SRGGB8_1X8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_SGRBG8_1X8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_SGBRG8_1X8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.mbus = MEDIA_BUS_FMT_SBGGR8_1X8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SRGGB10_1X10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SGRBG10_1X10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SGBRG10_1X10,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SBGGR10_1X10,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SRGGB12_1X12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SGRBG12_1X12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SGBRG12_1X12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
		.mbus = MEDIA_BUS_FMT_SBGGR12_1X12,
	},
};

/*
 * The supported pixel formats for selfpath. NOTE, pixel formats with identical 'mbus'
 * are grouped together. This is assumed and used by the function rkisp1_cap_enum_mbus_codes
 */
static const struct rkisp1_capture_fmt_cfg rkisp1_sp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	},
	/* rgb */
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB888,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB565,
		.mbus = MEDIA_BUS_FMT_YUYV8_2X8,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
		.mbus = MEDIA_BUS_FMT_YUYV8_1_5X8,
	},
};

static const struct rkisp1_capture_config rkisp1_capture_config_mp = {
	.fmts = rkisp1_mp_fmts,
	.fmt_size = ARRAY_SIZE(rkisp1_mp_fmts),
	.mi = {
		.y_size_init =		RKISP1_CIF_MI_MP_Y_SIZE_INIT,
		.cb_size_init =		RKISP1_CIF_MI_MP_CB_SIZE_INIT,
		.cr_size_init =		RKISP1_CIF_MI_MP_CR_SIZE_INIT,
		.y_base_ad_init =	RKISP1_CIF_MI_MP_Y_BASE_AD_INIT,
		.cb_base_ad_init =	RKISP1_CIF_MI_MP_CB_BASE_AD_INIT,
		.cr_base_ad_init =	RKISP1_CIF_MI_MP_CR_BASE_AD_INIT,
		.y_offs_cnt_init =	RKISP1_CIF_MI_MP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init =	RKISP1_CIF_MI_MP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init =	RKISP1_CIF_MI_MP_CR_OFFS_CNT_INIT,
	},
};

static const struct rkisp1_capture_config rkisp1_capture_config_sp = {
	.fmts = rkisp1_sp_fmts,
	.fmt_size = ARRAY_SIZE(rkisp1_sp_fmts),
	.mi = {
		.y_size_init =		RKISP1_CIF_MI_SP_Y_SIZE_INIT,
		.cb_size_init =		RKISP1_CIF_MI_SP_CB_SIZE_INIT,
		.cr_size_init =		RKISP1_CIF_MI_SP_CR_SIZE_INIT,
		.y_base_ad_init =	RKISP1_CIF_MI_SP_Y_BASE_AD_INIT,
		.cb_base_ad_init =	RKISP1_CIF_MI_SP_CB_BASE_AD_INIT,
		.cr_base_ad_init =	RKISP1_CIF_MI_SP_CR_BASE_AD_INIT,
		.y_offs_cnt_init =	RKISP1_CIF_MI_SP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init =	RKISP1_CIF_MI_SP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init =	RKISP1_CIF_MI_SP_CR_OFFS_CNT_INIT,
	},
};

static inline struct rkisp1_vdev_node *
rkisp1_vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp1_vdev_node, vdev);
}

int rkisp1_cap_enum_mbus_codes(struct rkisp1_capture *cap,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	const struct rkisp1_capture_fmt_cfg *fmts = cap->config->fmts;
	/*
	 * initialize curr_mbus to non existing mbus code 0 to ensure it is
	 * different from fmts[0].mbus
	 */
	u32 curr_mbus = 0;
	int i, n = 0;

	for (i = 0; i < cap->config->fmt_size; i++) {
		if (fmts[i].mbus == curr_mbus)
			continue;

		curr_mbus = fmts[i].mbus;
		if (n++ == code->index) {
			code->code = curr_mbus;
			return 0;
		}
	}
	return -EINVAL;
}

/* ----------------------------------------------------------------------------
 * Stream operations for self-picture path (sp) and main-picture path (mp)
 */

static void rkisp1_mi_config_ctrl(struct rkisp1_capture *cap)
{
	u32 mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~GENMASK(17, 16);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_LUM_64;

	mi_ctrl &= ~GENMASK(19, 18);
	mi_ctrl |= RKISP1_CIF_MI_CTRL_BURST_LEN_CHROM_64;

	mi_ctrl |= RKISP1_CIF_MI_CTRL_INIT_BASE_EN |
		   RKISP1_CIF_MI_CTRL_INIT_OFFSET_EN;

	rkisp1_write(cap->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static u32 rkisp1_pixfmt_comp_size(const struct v4l2_pix_format_mplane *pixm,
				   unsigned int component)
{
	/*
	 * If packed format, then plane_fmt[0].sizeimage is the sum of all
	 * components, so we need to calculate just the size of Y component.
	 * See rkisp1_fill_pixfmt().
	 */
	if (!component && pixm->num_planes == 1)
		return pixm->plane_fmt[0].bytesperline * pixm->height;
	return pixm->plane_fmt[component].sizeimage;
}

static void rkisp1_irq_frame_end_enable(struct rkisp1_capture *cap)
{
	u32 mi_imsc = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_IMSC);

	mi_imsc |= RKISP1_CIF_MI_FRAME(cap);
	rkisp1_write(cap->rkisp1, mi_imsc, RKISP1_CIF_MI_IMSC);
}

static void rkisp1_mp_config(struct rkisp1_capture *cap)
{
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	u32 reg;

	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
		     cap->config->mi.y_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
		     cap->config->mi.cb_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR),
		     cap->config->mi.cr_size_init);

	rkisp1_irq_frame_end_enable(cap);

	/* set uv swapping for semiplanar formats */
	if (cap->pix.info->comp_planes == 2) {
		reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);
		if (cap->pix.cfg->uv_swap)
			reg |= RKISP1_CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP;
		else
			reg &= ~RKISP1_CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP;
		rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_XTD_FORMAT_CTRL);
	}

	rkisp1_mi_config_ctrl(cap);

	reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	reg &= ~RKISP1_MI_CTRL_MP_FMT_MASK;
	reg |= cap->pix.cfg->write_format;
	rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_CTRL);

	reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	reg |= RKISP1_CIF_MI_MP_AUTOUPDATE_ENABLE;
	rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_sp_config(struct rkisp1_capture *cap)
{
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	u32 mi_ctrl, reg;

	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
		     cap->config->mi.y_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
		     cap->config->mi.cb_size_init);
	rkisp1_write(rkisp1, rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR),
		     cap->config->mi.cr_size_init);

	rkisp1_write(rkisp1, pixm->width, RKISP1_CIF_MI_SP_Y_PIC_WIDTH);
	rkisp1_write(rkisp1, pixm->height, RKISP1_CIF_MI_SP_Y_PIC_HEIGHT);
	rkisp1_write(rkisp1, cap->sp_y_stride, RKISP1_CIF_MI_SP_Y_LLENGTH);

	rkisp1_irq_frame_end_enable(cap);

	/* set uv swapping for semiplanar formats */
	if (cap->pix.info->comp_planes == 2) {
		reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);
		if (cap->pix.cfg->uv_swap)
			reg |= RKISP1_CIF_MI_XTD_FMT_CTRL_SP_CB_CR_SWAP;
		else
			reg &= ~RKISP1_CIF_MI_XTD_FMT_CTRL_SP_CB_CR_SWAP;
		rkisp1_write(rkisp1, reg, RKISP1_CIF_MI_XTD_FORMAT_CTRL);
	}

	rkisp1_mi_config_ctrl(cap);

	mi_ctrl = rkisp1_read(rkisp1, RKISP1_CIF_MI_CTRL);
	mi_ctrl &= ~RKISP1_MI_CTRL_SP_FMT_MASK;
	mi_ctrl |= cap->pix.cfg->write_format |
		   RKISP1_MI_CTRL_SP_INPUT_YUV422 |
		   cap->pix.cfg->output_format |
		   RKISP1_CIF_MI_SP_AUTOUPDATE_ENABLE;
	rkisp1_write(rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_mp_disable(struct rkisp1_capture *cap)
{
	u32 mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~(RKISP1_CIF_MI_CTRL_MP_ENABLE |
		     RKISP1_CIF_MI_CTRL_RAW_ENABLE);
	rkisp1_write(cap->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_sp_disable(struct rkisp1_capture *cap)
{
	u32 mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl &= ~RKISP1_CIF_MI_CTRL_SP_ENABLE;
	rkisp1_write(cap->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_mp_enable(struct rkisp1_capture *cap)
{
	u32 mi_ctrl;

	rkisp1_mp_disable(cap);

	mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);
	if (v4l2_is_format_bayer(cap->pix.info))
		mi_ctrl |= RKISP1_CIF_MI_CTRL_RAW_ENABLE;
	/* YUV */
	else
		mi_ctrl |= RKISP1_CIF_MI_CTRL_MP_ENABLE;

	rkisp1_write(cap->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_sp_enable(struct rkisp1_capture *cap)
{
	u32 mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);

	mi_ctrl |= RKISP1_CIF_MI_CTRL_SP_ENABLE;
	rkisp1_write(cap->rkisp1, mi_ctrl, RKISP1_CIF_MI_CTRL);
}

static void rkisp1_mp_sp_stop(struct rkisp1_capture *cap)
{
	if (!cap->is_streaming)
		return;
	rkisp1_write(cap->rkisp1,
		     RKISP1_CIF_MI_FRAME(cap), RKISP1_CIF_MI_ICR);
	cap->ops->disable(cap);
}

static bool rkisp1_mp_is_stopped(struct rkisp1_capture *cap)
{
	u32 en = RKISP1_CIF_MI_CTRL_SHD_MP_IN_ENABLED |
		 RKISP1_CIF_MI_CTRL_SHD_RAW_OUT_ENABLED;

	return !(rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL_SHD) & en);
}

static bool rkisp1_sp_is_stopped(struct rkisp1_capture *cap)
{
	return !(rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL_SHD) &
		 RKISP1_CIF_MI_CTRL_SHD_SP_IN_ENABLED);
}

static void rkisp1_mp_set_data_path(struct rkisp1_capture *cap)
{
	u32 dpcl = rkisp1_read(cap->rkisp1, RKISP1_CIF_VI_DPCL);

	dpcl = dpcl | RKISP1_CIF_VI_DPCL_CHAN_MODE_MP |
	       RKISP1_CIF_VI_DPCL_MP_MUX_MRSZ_MI;
	rkisp1_write(cap->rkisp1, dpcl, RKISP1_CIF_VI_DPCL);
}

static void rkisp1_sp_set_data_path(struct rkisp1_capture *cap)
{
	u32 dpcl = rkisp1_read(cap->rkisp1, RKISP1_CIF_VI_DPCL);

	dpcl |= RKISP1_CIF_VI_DPCL_CHAN_MODE_SP;
	rkisp1_write(cap->rkisp1, dpcl, RKISP1_CIF_VI_DPCL);
}

static const struct rkisp1_capture_ops rkisp1_capture_ops_mp = {
	.config = rkisp1_mp_config,
	.enable = rkisp1_mp_enable,
	.disable = rkisp1_mp_disable,
	.stop = rkisp1_mp_sp_stop,
	.set_data_path = rkisp1_mp_set_data_path,
	.is_stopped = rkisp1_mp_is_stopped,
};

static const struct rkisp1_capture_ops rkisp1_capture_ops_sp = {
	.config = rkisp1_sp_config,
	.enable = rkisp1_sp_enable,
	.disable = rkisp1_sp_disable,
	.stop = rkisp1_mp_sp_stop,
	.set_data_path = rkisp1_sp_set_data_path,
	.is_stopped = rkisp1_sp_is_stopped,
};

/* ----------------------------------------------------------------------------
 * Frame buffer operations
 */

static int rkisp1_dummy_buf_create(struct rkisp1_capture *cap)
{
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	struct rkisp1_dummy_buffer *dummy_buf = &cap->buf.dummy;

	dummy_buf->size = max3(rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y),
			       rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB),
			       rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CR));

	/* The driver never access vaddr, no mapping is required */
	dummy_buf->vaddr = dma_alloc_attrs(cap->rkisp1->dev,
					   dummy_buf->size,
					   &dummy_buf->dma_addr,
					   GFP_KERNEL,
					   DMA_ATTR_NO_KERNEL_MAPPING);
	if (!dummy_buf->vaddr)
		return -ENOMEM;

	return 0;
}

static void rkisp1_dummy_buf_destroy(struct rkisp1_capture *cap)
{
	dma_free_attrs(cap->rkisp1->dev,
		       cap->buf.dummy.size, cap->buf.dummy.vaddr,
		       cap->buf.dummy.dma_addr, DMA_ATTR_NO_KERNEL_MAPPING);
}

static void rkisp1_set_next_buf(struct rkisp1_capture *cap)
{
	cap->buf.curr = cap->buf.next;
	cap->buf.next = NULL;

	if (!list_empty(&cap->buf.queue)) {
		u32 *buff_addr;

		cap->buf.next = list_first_entry(&cap->buf.queue, struct rkisp1_buffer, queue);
		list_del(&cap->buf.next->queue);

		buff_addr = cap->buf.next->buff_addr;

		rkisp1_write(cap->rkisp1,
			     buff_addr[RKISP1_PLANE_Y],
			     cap->config->mi.y_base_ad_init);
		/*
		 * In order to support grey format we capture
		 * YUV422 planar format from the camera and
		 * set the U and V planes to the dummy buffer
		 */
		if (cap->pix.cfg->fourcc == V4L2_PIX_FMT_GREY) {
			rkisp1_write(cap->rkisp1,
				     cap->buf.dummy.dma_addr,
				     cap->config->mi.cb_base_ad_init);
			rkisp1_write(cap->rkisp1,
				     cap->buf.dummy.dma_addr,
				     cap->config->mi.cr_base_ad_init);
		} else {
			rkisp1_write(cap->rkisp1,
				     buff_addr[RKISP1_PLANE_CB],
				     cap->config->mi.cb_base_ad_init);
			rkisp1_write(cap->rkisp1,
				     buff_addr[RKISP1_PLANE_CR],
				     cap->config->mi.cr_base_ad_init);
		}
	} else {
		/*
		 * Use the dummy space allocated by dma_alloc_coherent to
		 * throw data if there is no available buffer.
		 */
		rkisp1_write(cap->rkisp1,
			     cap->buf.dummy.dma_addr,
			     cap->config->mi.y_base_ad_init);
		rkisp1_write(cap->rkisp1,
			     cap->buf.dummy.dma_addr,
			     cap->config->mi.cb_base_ad_init);
		rkisp1_write(cap->rkisp1,
			     cap->buf.dummy.dma_addr,
			     cap->config->mi.cr_base_ad_init);
	}

	/* Set plane offsets */
	rkisp1_write(cap->rkisp1, 0, cap->config->mi.y_offs_cnt_init);
	rkisp1_write(cap->rkisp1, 0, cap->config->mi.cb_offs_cnt_init);
	rkisp1_write(cap->rkisp1, 0, cap->config->mi.cr_offs_cnt_init);
}

/*
 * This function is called when a frame end comes. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static void rkisp1_handle_buffer(struct rkisp1_capture *cap)
{
	struct rkisp1_isp *isp = &cap->rkisp1->isp;
	struct rkisp1_buffer *curr_buf;

	spin_lock(&cap->buf.lock);
	curr_buf = cap->buf.curr;

	if (curr_buf) {
		curr_buf->vb.sequence = isp->frame_sequence;
		curr_buf->vb.vb2_buf.timestamp = ktime_get_boottime_ns();
		curr_buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	} else {
		cap->rkisp1->debug.frame_drop[cap->id]++;
	}

	rkisp1_set_next_buf(cap);
	spin_unlock(&cap->buf.lock);
}

irqreturn_t rkisp1_capture_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);
	unsigned int i;
	u32 status;

	status = rkisp1_read(rkisp1, RKISP1_CIF_MI_MIS);
	if (!status)
		return IRQ_NONE;

	rkisp1_write(rkisp1, status, RKISP1_CIF_MI_ICR);

	for (i = 0; i < ARRAY_SIZE(rkisp1->capture_devs); ++i) {
		struct rkisp1_capture *cap = &rkisp1->capture_devs[i];

		if (!(status & RKISP1_CIF_MI_FRAME(cap)))
			continue;
		if (!cap->is_stopping) {
			rkisp1_handle_buffer(cap);
			continue;
		}
		/*
		 * Make sure stream is actually stopped, whose state
		 * can be read from the shadow register, before
		 * wake_up() thread which would immediately free all
		 * frame buffers. stop() takes effect at the next
		 * frame end that sync the configurations to shadow
		 * regs.
		 */
		if (!cap->ops->is_stopped(cap)) {
			cap->ops->stop(cap);
			continue;
		}
		cap->is_stopping = false;
		cap->is_streaming = false;
		wake_up(&cap->done);
	}

	return IRQ_HANDLED;
}

/* ----------------------------------------------------------------------------
 * Vb2 operations
 */

static int rkisp1_vb2_queue_setup(struct vb2_queue *queue,
				  unsigned int *num_buffers,
				  unsigned int *num_planes,
				  unsigned int sizes[],
				  struct device *alloc_devs[])
{
	struct rkisp1_capture *cap = queue->drv_priv;
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	unsigned int i;

	if (*num_planes) {
		if (*num_planes != pixm->num_planes)
			return -EINVAL;

		for (i = 0; i < pixm->num_planes; i++)
			if (sizes[i] < pixm->plane_fmt[i].sizeimage)
				return -EINVAL;
	} else {
		*num_planes = pixm->num_planes;
		for (i = 0; i < pixm->num_planes; i++)
			sizes[i] = pixm->plane_fmt[i].sizeimage;
	}

	return 0;
}

static int rkisp1_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *ispbuf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct rkisp1_capture *cap = vb->vb2_queue->drv_priv;
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	unsigned int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < pixm->num_planes; i++)
		ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	/* Convert to non-MPLANE */
	if (pixm->num_planes == 1) {
		ispbuf->buff_addr[RKISP1_PLANE_CB] =
			ispbuf->buff_addr[RKISP1_PLANE_Y] +
			rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_Y);
		ispbuf->buff_addr[RKISP1_PLANE_CR] =
			ispbuf->buff_addr[RKISP1_PLANE_CB] +
			rkisp1_pixfmt_comp_size(pixm, RKISP1_PLANE_CB);
	}

	/*
	 * uv swap can be supported for planar formats by switching
	 * the address of cb and cr
	 */
	if (cap->pix.info->comp_planes == 3 && cap->pix.cfg->uv_swap)
		swap(ispbuf->buff_addr[RKISP1_PLANE_CR],
		     ispbuf->buff_addr[RKISP1_PLANE_CB]);
	return 0;
}

static void rkisp1_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *ispbuf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct rkisp1_capture *cap = vb->vb2_queue->drv_priv;

	spin_lock_irq(&cap->buf.lock);
	list_add_tail(&ispbuf->queue, &cap->buf.queue);
	spin_unlock_irq(&cap->buf.lock);
}

static int rkisp1_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct rkisp1_capture *cap = vb->vb2_queue->drv_priv;
	unsigned int i;

	for (i = 0; i < cap->pix.fmt.num_planes; i++) {
		unsigned long size = cap->pix.fmt.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			dev_err(cap->rkisp1->dev,
				"User buffer too small (%ld < %ld)\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void rkisp1_return_all_buffers(struct rkisp1_capture *cap,
				      enum vb2_buffer_state state)
{
	struct rkisp1_buffer *buf;

	spin_lock_irq(&cap->buf.lock);
	if (cap->buf.curr) {
		vb2_buffer_done(&cap->buf.curr->vb.vb2_buf, state);
		cap->buf.curr = NULL;
	}
	if (cap->buf.next) {
		vb2_buffer_done(&cap->buf.next->vb.vb2_buf, state);
		cap->buf.next = NULL;
	}
	while (!list_empty(&cap->buf.queue)) {
		buf = list_first_entry(&cap->buf.queue,
				       struct rkisp1_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irq(&cap->buf.lock);
}

/*
 * Most registers inside the rockchip ISP1 have shadow register since
 * they must not be changed while processing a frame.
 * Usually, each sub-module updates its shadow register after
 * processing the last pixel of a frame.
 */
static void rkisp1_cap_stream_enable(struct rkisp1_capture *cap)
{
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	struct rkisp1_capture *other = &rkisp1->capture_devs[cap->id ^ 1];

	cap->ops->set_data_path(cap);
	cap->ops->config(cap);

	/* Setup a buffer for the next frame */
	spin_lock_irq(&cap->buf.lock);
	rkisp1_set_next_buf(cap);
	cap->ops->enable(cap);
	/* It's safe to configure ACTIVE and SHADOW registers for the
	 * first stream. While when the second is starting, do NOT
	 * force update because it also updates the first one.
	 *
	 * The latter case would drop one more buffer(that is 2) since
	 * there's no buffer in a shadow register when the second FE received.
	 * This's also required because the second FE maybe corrupt
	 * especially when run at 120fps.
	 */
	if (!other->is_streaming) {
		/* force cfg update */
		rkisp1_write(rkisp1,
			     RKISP1_CIF_MI_INIT_SOFT_UPD, RKISP1_CIF_MI_INIT);
		rkisp1_set_next_buf(cap);
	}
	spin_unlock_irq(&cap->buf.lock);
	cap->is_streaming = true;
}

static void rkisp1_cap_stream_disable(struct rkisp1_capture *cap)
{
	int ret;

	/* Stream should stop in interrupt. If it doesn't, stop it by force. */
	cap->is_stopping = true;
	ret = wait_event_timeout(cap->done,
				 !cap->is_streaming,
				 msecs_to_jiffies(1000));
	if (!ret) {
		cap->rkisp1->debug.stop_timeout[cap->id]++;
		cap->ops->stop(cap);
		cap->is_stopping = false;
		cap->is_streaming = false;
	}
}

/*
 * rkisp1_pipeline_stream_disable - disable nodes in the pipeline
 *
 * Call s_stream(false) in the reverse order from
 * rkisp1_pipeline_stream_enable() and disable the DMA engine.
 * Should be called before media_pipeline_stop()
 */
static void rkisp1_pipeline_stream_disable(struct rkisp1_capture *cap)
	__must_hold(&cap->rkisp1->stream_lock)
{
	struct rkisp1_device *rkisp1 = cap->rkisp1;

	rkisp1_cap_stream_disable(cap);

	/*
	 * If the other capture is streaming, isp and sensor nodes shouldn't
	 * be disabled, skip them.
	 */
	if (rkisp1->pipe.streaming_count < 2) {
		v4l2_subdev_call(rkisp1->active_sensor->sd, video, s_stream,
				 false);
		v4l2_subdev_call(&rkisp1->isp.sd, video, s_stream, false);
	}

	v4l2_subdev_call(&rkisp1->resizer_devs[cap->id].sd, video, s_stream,
			 false);
}

/*
 * rkisp1_pipeline_stream_enable - enable nodes in the pipeline
 *
 * Enable the DMA Engine and call s_stream(true) through the pipeline.
 * Should be called after media_pipeline_start()
 */
static int rkisp1_pipeline_stream_enable(struct rkisp1_capture *cap)
	__must_hold(&cap->rkisp1->stream_lock)
{
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	int ret;

	rkisp1_cap_stream_enable(cap);

	ret = v4l2_subdev_call(&rkisp1->resizer_devs[cap->id].sd, video,
			       s_stream, true);
	if (ret)
		goto err_disable_cap;

	/*
	 * If the other capture is streaming, isp and sensor nodes are already
	 * enabled, skip them.
	 */
	if (rkisp1->pipe.streaming_count > 1)
		return 0;

	ret = v4l2_subdev_call(&rkisp1->isp.sd, video, s_stream, true);
	if (ret)
		goto err_disable_rsz;

	ret = v4l2_subdev_call(rkisp1->active_sensor->sd, video, s_stream,
			       true);
	if (ret)
		goto err_disable_isp;

	return 0;

err_disable_isp:
	v4l2_subdev_call(&rkisp1->isp.sd, video, s_stream, false);
err_disable_rsz:
	v4l2_subdev_call(&rkisp1->resizer_devs[cap->id].sd, video, s_stream,
			 false);
err_disable_cap:
	rkisp1_cap_stream_disable(cap);

	return ret;
}

static void rkisp1_vb2_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp1_capture *cap = queue->drv_priv;
	struct rkisp1_vdev_node *node = &cap->vnode;
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	int ret;

	mutex_lock(&cap->rkisp1->stream_lock);

	rkisp1_pipeline_stream_disable(cap);

	rkisp1_return_all_buffers(cap, VB2_BUF_STATE_ERROR);

	v4l2_pipeline_pm_put(&node->vdev.entity);
	ret = pm_runtime_put(rkisp1->dev);
	if (ret < 0)
		dev_err(rkisp1->dev, "power down failed error:%d\n", ret);

	rkisp1_dummy_buf_destroy(cap);

	media_pipeline_stop(&node->vdev.entity);

	mutex_unlock(&cap->rkisp1->stream_lock);
}

static int
rkisp1_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_capture *cap = queue->drv_priv;
	struct media_entity *entity = &cap->vnode.vdev.entity;
	int ret;

	mutex_lock(&cap->rkisp1->stream_lock);

	ret = media_pipeline_start(entity, &cap->rkisp1->pipe);
	if (ret) {
		dev_err(cap->rkisp1->dev, "start pipeline failed %d\n", ret);
		goto err_ret_buffers;
	}

	ret = rkisp1_dummy_buf_create(cap);
	if (ret)
		goto err_pipeline_stop;

	ret = pm_runtime_resume_and_get(cap->rkisp1->dev);
	if (ret < 0) {
		dev_err(cap->rkisp1->dev, "power up failed %d\n", ret);
		goto err_destroy_dummy;
	}
	ret = v4l2_pipeline_pm_get(entity);
	if (ret) {
		dev_err(cap->rkisp1->dev, "open cif pipeline failed %d\n", ret);
		goto err_pipe_pm_put;
	}

	ret = rkisp1_pipeline_stream_enable(cap);
	if (ret)
		goto err_v4l2_pm_put;

	mutex_unlock(&cap->rkisp1->stream_lock);

	return 0;

err_v4l2_pm_put:
	v4l2_pipeline_pm_put(entity);
err_pipe_pm_put:
	pm_runtime_put(cap->rkisp1->dev);
err_destroy_dummy:
	rkisp1_dummy_buf_destroy(cap);
err_pipeline_stop:
	media_pipeline_stop(entity);
err_ret_buffers:
	rkisp1_return_all_buffers(cap, VB2_BUF_STATE_QUEUED);
	mutex_unlock(&cap->rkisp1->stream_lock);

	return ret;
}

static const struct vb2_ops rkisp1_vb2_ops = {
	.queue_setup = rkisp1_vb2_queue_setup,
	.buf_init = rkisp1_vb2_buf_init,
	.buf_queue = rkisp1_vb2_buf_queue,
	.buf_prepare = rkisp1_vb2_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp1_vb2_stop_streaming,
	.start_streaming = rkisp1_vb2_start_streaming,
};

/* ----------------------------------------------------------------------------
 * IOCTLs operations
 */

static const struct v4l2_format_info *
rkisp1_fill_pixfmt(struct v4l2_pix_format_mplane *pixm,
		   enum rkisp1_stream_id id)
{
	struct v4l2_plane_pix_format *plane_y = &pixm->plane_fmt[0];
	const struct v4l2_format_info *info;
	unsigned int i;
	u32 stride;

	memset(pixm->plane_fmt, 0, sizeof(pixm->plane_fmt));
	info = v4l2_format_info(pixm->pixelformat);
	pixm->num_planes = info->mem_planes;
	stride = info->bpp[0] * pixm->width;
	/* Self path supports custom stride but Main path doesn't */
	if (id == RKISP1_MAINPATH || plane_y->bytesperline < stride)
		plane_y->bytesperline = stride;
	plane_y->sizeimage = plane_y->bytesperline * pixm->height;

	/* normalize stride to pixels per line */
	stride = DIV_ROUND_UP(plane_y->bytesperline, info->bpp[0]);

	for (i = 1; i < info->comp_planes; i++) {
		struct v4l2_plane_pix_format *plane = &pixm->plane_fmt[i];

		/* bytesperline for other components derive from Y component */
		plane->bytesperline = DIV_ROUND_UP(stride, info->hdiv) *
				      info->bpp[i];
		plane->sizeimage = plane->bytesperline *
				   DIV_ROUND_UP(pixm->height, info->vdiv);
	}

	/*
	 * If pixfmt is packed, then plane_fmt[0] should contain the total size
	 * considering all components. plane_fmt[i] for i > 0 should be ignored
	 * by userspace as mem_planes == 1, but we are keeping information there
	 * for convenience.
	 */
	if (info->mem_planes == 1)
		for (i = 1; i < info->comp_planes; i++)
			plane_y->sizeimage += pixm->plane_fmt[i].sizeimage;

	return info;
}

static const struct rkisp1_capture_fmt_cfg *
rkisp1_find_fmt_cfg(const struct rkisp1_capture *cap, const u32 pixelfmt)
{
	unsigned int i;

	for (i = 0; i < cap->config->fmt_size; i++) {
		if (cap->config->fmts[i].fourcc == pixelfmt)
			return &cap->config->fmts[i];
	}
	return NULL;
}

static void rkisp1_try_fmt(const struct rkisp1_capture *cap,
			   struct v4l2_pix_format_mplane *pixm,
			   const struct rkisp1_capture_fmt_cfg **fmt_cfg,
			   const struct v4l2_format_info **fmt_info)
{
	const struct rkisp1_capture_config *config = cap->config;
	const struct rkisp1_capture_fmt_cfg *fmt;
	const struct v4l2_format_info *info;
	const unsigned int max_widths[] = { RKISP1_RSZ_MP_SRC_MAX_WIDTH,
					    RKISP1_RSZ_SP_SRC_MAX_WIDTH };
	const unsigned int max_heights[] = { RKISP1_RSZ_MP_SRC_MAX_HEIGHT,
					     RKISP1_RSZ_SP_SRC_MAX_HEIGHT};

	fmt = rkisp1_find_fmt_cfg(cap, pixm->pixelformat);
	if (!fmt) {
		fmt = config->fmts;
		pixm->pixelformat = fmt->fourcc;
	}

	pixm->width = clamp_t(u32, pixm->width,
			      RKISP1_RSZ_SRC_MIN_WIDTH, max_widths[cap->id]);
	pixm->height = clamp_t(u32, pixm->height,
			       RKISP1_RSZ_SRC_MIN_HEIGHT, max_heights[cap->id]);

	pixm->field = V4L2_FIELD_NONE;
	pixm->colorspace = V4L2_COLORSPACE_DEFAULT;
	pixm->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pixm->quantization = V4L2_QUANTIZATION_DEFAULT;

	info = rkisp1_fill_pixfmt(pixm, cap->id);

	if (fmt_cfg)
		*fmt_cfg = fmt;
	if (fmt_info)
		*fmt_info = info;
}

static void rkisp1_set_fmt(struct rkisp1_capture *cap,
			   struct v4l2_pix_format_mplane *pixm)
{
	rkisp1_try_fmt(cap, pixm, &cap->pix.cfg, &cap->pix.info);
	cap->pix.fmt = *pixm;

	/* SP supports custom stride in number of pixels of the Y plane */
	if (cap->id == RKISP1_SELFPATH)
		cap->sp_y_stride = pixm->plane_fmt[0].bytesperline /
				   cap->pix.info->bpp[0];
}

static int rkisp1_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp1_capture *cap = video_drvdata(file);

	rkisp1_try_fmt(cap, &f->fmt.pix_mp, NULL, NULL);

	return 0;
}

static int rkisp1_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp1_capture *cap = video_drvdata(file);
	const struct rkisp1_capture_fmt_cfg *fmt = NULL;
	unsigned int i, n = 0;

	if (!f->mbus_code) {
		if (f->index >= cap->config->fmt_size)
			return -EINVAL;

		fmt = &cap->config->fmts[f->index];
		f->pixelformat = fmt->fourcc;
		return 0;
	}

	for (i = 0; i < cap->config->fmt_size; i++) {
		if (cap->config->fmts[i].mbus != f->mbus_code)
			continue;

		if (n++ == f->index) {
			f->pixelformat = cap->config->fmts[i].fourcc;
			return 0;
		}
	}
	return -EINVAL;
}

static int rkisp1_s_fmt_vid_cap_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp1_capture *cap = video_drvdata(file);
	struct rkisp1_vdev_node *node =
				rkisp1_vdev_to_node(&cap->vnode.vdev);

	if (vb2_is_busy(&node->buf_queue))
		return -EBUSY;

	rkisp1_set_fmt(cap, &f->fmt.pix_mp);

	return 0;
}

static int rkisp1_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp1_capture *cap = video_drvdata(file);

	f->fmt.pix_mp = cap->pix.fmt;

	return 0;
}

static int
rkisp1_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct rkisp1_capture *cap_dev = video_drvdata(file);
	struct rkisp1_device *rkisp1 = cap_dev->rkisp1;

	strscpy(cap->driver, rkisp1->dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, rkisp1->dev->driver->name, sizeof(cap->card));
	strscpy(cap->bus_info, RKISP1_BUS_INFO, sizeof(cap->bus_info));

	return 0;
}

static const struct v4l2_ioctl_ops rkisp1_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_cap_mplane = rkisp1_try_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkisp1_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkisp1_g_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = rkisp1_enum_fmt_vid_cap_mplane,
	.vidioc_querycap = rkisp1_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int rkisp1_capture_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct v4l2_subdev *sd =
		media_entity_to_v4l2_subdev(link->source->entity);
	struct rkisp1_capture *cap = video_get_drvdata(vdev);
	const struct rkisp1_capture_fmt_cfg *fmt =
		rkisp1_find_fmt_cfg(cap, cap->pix.fmt.pixelformat);
	struct v4l2_subdev_format sd_fmt;
	int ret;

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = link->source->index;
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	if (sd_fmt.format.height != cap->pix.fmt.height ||
	    sd_fmt.format.width != cap->pix.fmt.width ||
	    sd_fmt.format.code != fmt->mbus)
		return -EPIPE;

	return 0;
}

/* ----------------------------------------------------------------------------
 * core functions
 */

static const struct media_entity_operations rkisp1_media_ops = {
	.link_validate = rkisp1_capture_link_validate,
};

static const struct v4l2_file_operations rkisp1_fops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static void rkisp1_unregister_capture(struct rkisp1_capture *cap)
{
	media_entity_cleanup(&cap->vnode.vdev.entity);
	vb2_video_unregister_device(&cap->vnode.vdev);
}

void rkisp1_capture_devs_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_capture *mp = &rkisp1->capture_devs[RKISP1_MAINPATH];
	struct rkisp1_capture *sp = &rkisp1->capture_devs[RKISP1_SELFPATH];

	rkisp1_unregister_capture(mp);
	rkisp1_unregister_capture(sp);
}

static int rkisp1_register_capture(struct rkisp1_capture *cap)
{
	const char * const dev_names[] = {RKISP1_MP_DEV_NAME,
					  RKISP1_SP_DEV_NAME};
	struct v4l2_device *v4l2_dev = &cap->rkisp1->v4l2_dev;
	struct video_device *vdev = &cap->vnode.vdev;
	struct rkisp1_vdev_node *node;
	struct vb2_queue *q;
	int ret;

	strscpy(vdev->name, dev_names[cap->id], sizeof(vdev->name));
	node = rkisp1_vdev_to_node(vdev);
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &rkisp1_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp1_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &node->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;
	vdev->entity.ops = &rkisp1_media_ops;
	video_set_drvdata(vdev, cap);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;

	q = &node->buf_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = cap;
	q->ops = &rkisp1_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->min_buffers_needed = RKISP1_MIN_BUFFERS_NEEDED;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;
	q->dev = cap->rkisp1->dev;
	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(cap->rkisp1->dev,
			"vb2 queue init failed (err=%d)\n", ret);
		return ret;
	}

	vdev->queue = q;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(cap->rkisp1->dev,
			"failed to register %s, ret=%d\n", vdev->name, ret);
		return ret;
	}
	v4l2_info(v4l2_dev, "registered %s as /dev/video%d\n", vdev->name,
		  vdev->num);

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret) {
		video_unregister_device(vdev);
		return ret;
	}

	return 0;
}

static void
rkisp1_capture_init(struct rkisp1_device *rkisp1, enum rkisp1_stream_id id)
{
	struct rkisp1_capture *cap = &rkisp1->capture_devs[id];
	struct v4l2_pix_format_mplane pixm;

	memset(cap, 0, sizeof(*cap));
	cap->id = id;
	cap->rkisp1 = rkisp1;

	INIT_LIST_HEAD(&cap->buf.queue);
	init_waitqueue_head(&cap->done);
	spin_lock_init(&cap->buf.lock);
	if (cap->id == RKISP1_SELFPATH) {
		cap->ops = &rkisp1_capture_ops_sp;
		cap->config = &rkisp1_capture_config_sp;
	} else {
		cap->ops = &rkisp1_capture_ops_mp;
		cap->config = &rkisp1_capture_config_mp;
	}

	cap->is_streaming = false;

	memset(&pixm, 0, sizeof(pixm));
	pixm.pixelformat = V4L2_PIX_FMT_YUYV;
	pixm.width = RKISP1_DEFAULT_WIDTH;
	pixm.height = RKISP1_DEFAULT_HEIGHT;
	rkisp1_set_fmt(cap, &pixm);
}

int rkisp1_capture_devs_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_capture *cap;
	unsigned int i, j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(rkisp1->capture_devs); i++) {
		rkisp1_capture_init(rkisp1, i);
		cap = &rkisp1->capture_devs[i];
		cap->rkisp1 = rkisp1;
		ret = rkisp1_register_capture(cap);
		if (ret)
			goto err_unreg_capture_devs;
	}

	return 0;

err_unreg_capture_devs:
	for (j = 0; j < i; j++) {
		cap = &rkisp1->capture_devs[j];
		rkisp1_unregister_capture(cap);
	}

	return ret;
}
