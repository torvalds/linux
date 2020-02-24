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
 * @uv_swap: if cb cr swaped, for yuv
 * @write_format: defines how YCbCr self picture data is written to memory
 * @output_format: defines sp output format
 */
struct rkisp1_capture_fmt_cfg {
	u32 fourcc;
	u8 fmt_type;
	u8 uv_swap;
	u32 write_format;
	u32 output_format;
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

static const struct rkisp1_capture_fmt_cfg rkisp1_mp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.fmt_type = RKISP1_FMT_YUV,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUVINT,
	},
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = RKISP1_FMT_BAYER,
		.write_format = RKISP1_MI_CTRL_MP_WRITE_RAW12,
	},
};

static const struct rkisp1_capture_fmt_cfg rkisp1_sp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU422M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV422,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_SPLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 1,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV420,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV444,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = RKISP1_FMT_YUV,
		.uv_swap = 0,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_INT,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_YUV400,
	},
	/* rgb */
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB888,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB565,
	}, {
		.fourcc = V4L2_PIX_FMT_BGR666,
		.fmt_type = RKISP1_FMT_RGB,
		.write_format = RKISP1_MI_CTRL_SP_WRITE_PLA,
		.output_format = RKISP1_MI_CTRL_SP_OUTPUT_RGB666,
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
	if (cap->pix.cfg->uv_swap) {
		reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);

		reg = (reg & ~BIT(0)) |
		      RKISP1_CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP;
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
	u32 mi_ctrl;

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
	if (cap->pix.cfg->uv_swap) {
		u32 reg = rkisp1_read(rkisp1, RKISP1_CIF_MI_XTD_FORMAT_CTRL);

		rkisp1_write(rkisp1, reg & ~BIT(1),
			     RKISP1_CIF_MI_XTD_FORMAT_CTRL);
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
	const struct rkisp1_capture_fmt_cfg *isp_fmt = cap->pix.cfg;
	u32 mi_ctrl;

	rkisp1_mp_disable(cap);

	mi_ctrl = rkisp1_read(cap->rkisp1, RKISP1_CIF_MI_CTRL);
	if (isp_fmt->fmt_type == RKISP1_FMT_BAYER)
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

static struct rkisp1_capture_ops rkisp1_capture_ops_mp = {
	.config = rkisp1_mp_config,
	.enable = rkisp1_mp_enable,
	.disable = rkisp1_mp_disable,
	.stop = rkisp1_mp_sp_stop,
	.set_data_path = rkisp1_mp_set_data_path,
	.is_stopped = rkisp1_mp_is_stopped,
};

static struct rkisp1_capture_ops rkisp1_capture_ops_sp = {
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
	/*
	 * Use the dummy space allocated by dma_alloc_coherent to
	 * throw data if there is no available buffer.
	 */
	if (cap->buf.next) {
		u32 *buff_addr = cap->buf.next->buff_addr;

		rkisp1_write(cap->rkisp1,
			     buff_addr[RKISP1_PLANE_Y],
			     cap->config->mi.y_base_ad_init);
		rkisp1_write(cap->rkisp1,
			     buff_addr[RKISP1_PLANE_CB],
			     cap->config->mi.cb_base_ad_init);
		rkisp1_write(cap->rkisp1,
			     buff_addr[RKISP1_PLANE_CR],
			     cap->config->mi.cr_base_ad_init);
	} else {
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
	struct rkisp1_buffer *curr_buf = cap->buf.curr;
	unsigned long flags;

	spin_lock_irqsave(&cap->buf.lock, flags);

	if (curr_buf) {
		curr_buf->vb.sequence = atomic_read(&isp->frame_sequence);
		curr_buf->vb.vb2_buf.timestamp = ktime_get_boottime_ns();
		curr_buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	} else {
		cap->rkisp1->debug.frame_drop[cap->id]++;
	}

	cap->buf.curr = cap->buf.next;
	cap->buf.next = NULL;

	if (!list_empty(&cap->buf.queue)) {
		cap->buf.next = list_first_entry(&cap->buf.queue,
						 struct rkisp1_buffer,
						 queue);
		list_del(&cap->buf.next->queue);
	}
	spin_unlock_irqrestore(&cap->buf.lock, flags);

	rkisp1_set_next_buf(cap);
}

void rkisp1_capture_isr(struct rkisp1_device *rkisp1)
{
	unsigned int i;
	u32 status;

	status = rkisp1_read(rkisp1, RKISP1_CIF_MI_MIS);
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

static void rkisp1_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *ispbuf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct rkisp1_capture *cap = vb->vb2_queue->drv_priv;
	const struct v4l2_pix_format_mplane *pixm = &cap->pix.fmt;
	unsigned long flags;
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

	spin_lock_irqsave(&cap->buf.lock, flags);

	/*
	 * If there's no next buffer assigned, queue this buffer directly
	 * as the next buffer, and update the memory interface.
	 */
	if (cap->is_streaming && !cap->buf.next &&
	    atomic_read(&cap->rkisp1->isp.frame_sequence) == -1) {
		cap->buf.next = ispbuf;
		rkisp1_set_next_buf(cap);
	} else {
		list_add_tail(&ispbuf->queue, &cap->buf.queue);
	}
	spin_unlock_irqrestore(&cap->buf.lock, flags);
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
	unsigned long flags;
	struct rkisp1_buffer *buf;

	spin_lock_irqsave(&cap->buf.lock, flags);
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
	spin_unlock_irqrestore(&cap->buf.lock, flags);
}

/*
 * rkisp1_pipeline_sink_walk - Walk through the pipeline and call cb
 * @from: entity at which to start pipeline walk
 * @until: entity at which to stop pipeline walk
 *
 * Walk the entities chain starting at the pipeline video node and stop
 * all subdevices in the chain.
 *
 * If the until argument isn't NULL, stop the pipeline walk when reaching the
 * until entity. This is used to disable a partially started pipeline due to a
 * subdev start error.
 */
static int rkisp1_pipeline_sink_walk(struct media_entity *from,
				     struct media_entity *until,
				     int (*cb)(struct media_entity *from,
					       struct media_entity *curr))
{
	struct media_entity *entity = from;
	struct media_pad *pad;
	unsigned int i;
	int ret;

	while (1) {
		pad = NULL;
		/* Find remote source pad */
		for (i = 0; i < entity->num_pads; i++) {
			struct media_pad *spad = &entity->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad && is_media_entity_v4l2_subdev(pad->entity))
				break;
		}
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		if (entity == until)
			break;

		ret = cb(from, entity);
		if (ret)
			return ret;
	}

	return 0;
}

static int rkisp1_pipeline_disable_cb(struct media_entity *from,
				      struct media_entity *curr)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(curr);

	return v4l2_subdev_call(sd, video, s_stream, false);
}

static int rkisp1_pipeline_enable_cb(struct media_entity *from,
				     struct media_entity *curr)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(curr);

	return v4l2_subdev_call(sd, video, s_stream, true);
}

static void rkisp1_stream_stop(struct rkisp1_capture *cap)
{
	int ret;

	/* Stream should stop in interrupt. If it dosn't, stop it by force. */
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

static void rkisp1_vb2_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp1_capture *cap = queue->drv_priv;
	struct rkisp1_vdev_node *node = &cap->vnode;
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	int ret;

	rkisp1_stream_stop(cap);
	media_pipeline_stop(&node->vdev.entity);
	ret = rkisp1_pipeline_sink_walk(&node->vdev.entity, NULL,
					rkisp1_pipeline_disable_cb);
	if (ret)
		dev_err(rkisp1->dev,
			"pipeline stream-off failed error:%d\n", ret);

	rkisp1_return_all_buffers(cap, VB2_BUF_STATE_ERROR);

	ret = v4l2_pipeline_pm_use(&node->vdev.entity, 0);
	if (ret)
		dev_err(rkisp1->dev, "pipeline close failed error:%d\n", ret);

	ret = pm_runtime_put(rkisp1->dev);
	if (ret)
		dev_err(rkisp1->dev, "power down failed error:%d\n", ret);

	rkisp1_dummy_buf_destroy(cap);
}

/*
 * Most of registers inside rockchip ISP1 have shadow register since
 * they must be not be changed during processing a frame.
 * Usually, each sub-module updates its shadow register after
 * processing the last pixel of a frame.
 */
static void rkisp1_stream_start(struct rkisp1_capture *cap)
{
	struct rkisp1_device *rkisp1 = cap->rkisp1;
	struct rkisp1_capture *other = &rkisp1->capture_devs[cap->id ^ 1];

	cap->ops->set_data_path(cap);
	cap->ops->config(cap);

	/* Setup a buffer for the next frame */
	rkisp1_handle_buffer(cap);
	cap->ops->enable(cap);
	/* It's safe to config ACTIVE and SHADOW regs for the
	 * first stream. While when the second is starting, do NOT
	 * force update because it also update the first one.
	 *
	 * The latter case would drop one more buf(that is 2) since
	 * there's not buf in shadow when the second FE received. This's
	 * also required because the second FE maybe corrupt especially
	 * when run at 120fps.
	 */
	if (!other->is_streaming) {
		/* force cfg update */
		rkisp1_write(rkisp1,
			     RKISP1_CIF_MI_INIT_SOFT_UPD, RKISP1_CIF_MI_INIT);
		rkisp1_handle_buffer(cap);
	}
	cap->is_streaming = true;
}

static int
rkisp1_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_capture *cap = queue->drv_priv;
	struct media_entity *entity = &cap->vnode.vdev.entity;
	int ret;

	ret = rkisp1_dummy_buf_create(cap);
	if (ret)
		goto err_ret_buffers;

	ret = pm_runtime_get_sync(cap->rkisp1->dev);
	if (ret) {
		dev_err(cap->rkisp1->dev, "power up failed %d\n", ret);
		goto err_destroy_dummy;
	}
	ret = v4l2_pipeline_pm_use(entity, 1);
	if (ret) {
		dev_err(cap->rkisp1->dev, "open cif pipeline failed %d\n", ret);
		goto err_pipe_pm_put;
	}

	rkisp1_stream_start(cap);

	/* start sub-devices */
	ret = rkisp1_pipeline_sink_walk(entity, NULL,
					rkisp1_pipeline_enable_cb);
	if (ret)
		goto err_stop_stream;

	ret = media_pipeline_start(entity, &cap->rkisp1->pipe);
	if (ret) {
		dev_err(cap->rkisp1->dev, "start pipeline failed %d\n", ret);
		goto err_pipe_disable;
	}

	return 0;

err_pipe_disable:
	rkisp1_pipeline_sink_walk(entity, NULL, rkisp1_pipeline_disable_cb);
err_stop_stream:
	rkisp1_stream_stop(cap);
	v4l2_pipeline_pm_use(entity, 0);
err_pipe_pm_put:
	pm_runtime_put(cap->rkisp1->dev);
err_destroy_dummy:
	rkisp1_dummy_buf_destroy(cap);
err_ret_buffers:
	rkisp1_return_all_buffers(cap, VB2_BUF_STATE_QUEUED);

	return ret;
}

static struct vb2_ops rkisp1_vb2_ops = {
	.queue_setup = rkisp1_vb2_queue_setup,
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
	struct rkisp1_capture *other_cap =
			&cap->rkisp1->capture_devs[cap->id ^ 1];
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

	info = rkisp1_fill_pixfmt(pixm, cap->id);

	/* can not change quantization when stream-on */
	if (other_cap->is_streaming)
		pixm->quantization = other_cap->pix.fmt.quantization;
	/* output full range by default, take effect in params */
	else if (!pixm->quantization ||
		 pixm->quantization > V4L2_QUANTIZATION_LIM_RANGE)
		pixm->quantization = V4L2_QUANTIZATION_FULL_RANGE;

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

	if (f->index >= cap->config->fmt_size)
		return -EINVAL;

	fmt = &cap->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
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
	struct rkisp1_isp *isp = &cap->rkisp1->isp;
	struct v4l2_subdev_format sd_fmt;
	int ret;

	if (cap->id == RKISP1_SELFPATH &&
	    isp->src_fmt->mbus_code != MEDIA_BUS_FMT_YUYV8_2X8) {
		dev_err(cap->rkisp1->dev,
			"selfpath only supports MEDIA_BUS_FMT_YUYV8_2X8\n");
		return -EPIPE;
	}

	if (cap->pix.cfg->fmt_type != isp->src_fmt->fmt_type) {
		dev_err(cap->rkisp1->dev,
			"format type mismatch in link '%s:%d->%s:%d'\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index);
		return -EPIPE;
	}

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = link->source->index;
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	if (sd_fmt.format.height != cap->pix.fmt.height ||
	    sd_fmt.format.width != cap->pix.fmt.width)
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
	video_unregister_device(&cap->vnode.vdev);
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
			    V4L2_CAP_STREAMING;
	vdev->entity.ops = &rkisp1_media_ops;
	video_set_drvdata(vdev, cap);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;

	q = &node->buf_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
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

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
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
