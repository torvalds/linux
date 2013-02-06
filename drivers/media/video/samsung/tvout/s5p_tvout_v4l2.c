/* linux/drivers/media/video/samsung/tvout/s5p_tvout_v4l2.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * V4L2 API file for Samsung TVOOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include <linux/videodev2_exynos_camera.h>
#include <linux/io.h>
#include <asm/cacheflush.h>

#include "s5p_tvout_common_lib.h"
#include "s5p_tvout_ctrl.h"
#include "s5p_tvout_v4l2.h"

#if defined(CONFIG_S5P_SYSMMU_TV)
#include <plat/sysmmu.h>
#endif

#ifdef CONFIG_UMP_VCM_ALLOC
#include "ump_kernel_interface.h"
#endif

#ifdef CONFIG_VCM
#include <plat/s5p-vcm.h>
#endif

#define MAJOR_VERSION	0
#define MINOR_VERSION	3
#define RELEASE_VERSION	0

#if defined(CONFIG_S5P_SYSMMU_TV)
#ifdef CONFIG_S5P_VMEM
/* temporary used for testing system mmu */
extern void *s5p_getaddress(unsigned int cookie);
#endif
#endif

extern struct s5p_tvout_vp_bufferinfo s5ptv_vp_buff;

#define V4L2_STD_ALL_HD				((v4l2_std_id)0xffffffff)

#ifdef CONFIG_CPU_EXYNOS4210
#define S5P_TVOUT_TVIF_MINOR			14
#define S5P_TVOUT_VO_MINOR			21
#else
#define S5P_TVOUT_TVIF_MINOR			16
#define S5P_TVOUT_VO_MINOR			20
#endif

#define V4L2_OUTPUT_TYPE_COMPOSITE		5
#define V4L2_OUTPUT_TYPE_HDMI			10
#define V4L2_OUTPUT_TYPE_HDMI_RGB		11
#define V4L2_OUTPUT_TYPE_DVI			12

#define V4L2_STD_PAL_BDGHI	(V4L2_STD_PAL_B|\
				V4L2_STD_PAL_D|	\
				V4L2_STD_PAL_G|	\
				V4L2_STD_PAL_H|	\
				V4L2_STD_PAL_I)

#define V4L2_STD_480P_60_16_9	((v4l2_std_id)0x04000000)
#define V4L2_STD_480P_60_4_3	((v4l2_std_id)0x05000000)
#define V4L2_STD_576P_50_16_9	((v4l2_std_id)0x06000000)
#define V4L2_STD_576P_50_4_3	((v4l2_std_id)0x07000000)
#define V4L2_STD_720P_60	((v4l2_std_id)0x08000000)
#define V4L2_STD_720P_50	((v4l2_std_id)0x09000000)
#define V4L2_STD_1080P_60	((v4l2_std_id)0x0a000000)
#define V4L2_STD_1080P_50	((v4l2_std_id)0x0b000000)
#define V4L2_STD_1080I_60	((v4l2_std_id)0x0c000000)
#define V4L2_STD_1080I_50	((v4l2_std_id)0x0d000000)
#define V4L2_STD_480P_59	((v4l2_std_id)0x0e000000)
#define V4L2_STD_720P_59	((v4l2_std_id)0x0f000000)
#define V4L2_STD_1080I_59	((v4l2_std_id)0x10000000)
#define V4L2_STD_1080P_59	((v4l2_std_id)0x11000000)
#define V4L2_STD_1080P_30	((v4l2_std_id)0x12000000)

#ifdef	CONFIG_HDMI_14A_3D
#define V4L2_STD_TVOUT_720P_60_SBS_HALF	((v4l2_std_id)0x13000000)
#define V4L2_STD_TVOUT_720P_59_SBS_HALF	((v4l2_std_id)0x14000000)
#define V4L2_STD_TVOUT_720P_50_TB	((v4l2_std_id)0x15000000)
#define V4L2_STD_TVOUT_1080P_24_TB	((v4l2_std_id)0x16000000)
#define V4L2_STD_TVOUT_1080P_23_TB	((v4l2_std_id)0x17000000)
#endif

#define CVBS_S_VIDEO (V4L2_STD_NTSC_M | V4L2_STD_NTSC_M_JP| \
	V4L2_STD_PAL | V4L2_STD_PAL_M | V4L2_STD_PAL_N | V4L2_STD_PAL_Nc | \
	V4L2_STD_PAL_60 | V4L2_STD_NTSC_443)

struct v4l2_vid_overlay_src {
	void			*base_y;
	void			*base_c;
	struct v4l2_pix_format	pix_fmt;
};

static const struct v4l2_output s5p_tvout_tvif_output[] = {
	{
		.index		= 0,
		.name		= "Analog  COMPOSITE",
		.type		= V4L2_OUTPUT_TYPE_COMPOSITE,
		.audioset	= 0,
		.modulator	= 0,
		.std		= CVBS_S_VIDEO,
	}, {
		.index		= 1,
		.name		= "Digital HDMI(YCbCr)",
		.type		= V4L2_OUTPUT_TYPE_HDMI,
		.audioset	= 2,
		.modulator	= 0,
		.std		= V4L2_STD_480P_60_16_9 |
				V4L2_STD_480P_60_16_9 | V4L2_STD_720P_60 |
				V4L2_STD_720P_50
				| V4L2_STD_1080P_60 | V4L2_STD_1080P_50 |
				V4L2_STD_1080I_60 | V4L2_STD_1080I_50 |
				V4L2_STD_480P_59 | V4L2_STD_720P_59 |
				V4L2_STD_1080I_59 | V4L2_STD_1080P_59 |
				V4L2_STD_1080P_30,
	}, {
		.index		= 2,
		.name		= "Digital HDMI(RGB)",
		.type		= V4L2_OUTPUT_TYPE_HDMI_RGB,
		.audioset	= 2,
		.modulator	= 0,
		.std		= V4L2_STD_480P_60_16_9 |
				V4L2_STD_480P_60_16_9 |
				V4L2_STD_720P_60 | V4L2_STD_720P_50
				| V4L2_STD_1080P_60 | V4L2_STD_1080P_50 |
				V4L2_STD_1080I_60 | V4L2_STD_1080I_50 |
				V4L2_STD_480P_59 | V4L2_STD_720P_59 |
				V4L2_STD_1080I_59 | V4L2_STD_1080P_59 |
				V4L2_STD_1080P_30,
	}, {
		.index		= 3,
		.name		= "Digital DVI",
		.type		= V4L2_OUTPUT_TYPE_DVI,
		.audioset	= 2,
		.modulator	= 0,
		.std		= V4L2_STD_480P_60_16_9 |
				V4L2_STD_480P_60_16_9 |
				V4L2_STD_720P_60 | V4L2_STD_720P_50
				| V4L2_STD_1080P_60 | V4L2_STD_1080P_50 |
				V4L2_STD_1080I_60 | V4L2_STD_1080I_50 |
				V4L2_STD_480P_59 | V4L2_STD_720P_59 |
				V4L2_STD_1080I_59 | V4L2_STD_1080P_59 |
				V4L2_STD_1080P_30,
	}

};

#define S5P_TVOUT_TVIF_NO_OF_OUTPUT ARRAY_SIZE(s5p_tvout_tvif_output)

static const struct v4l2_standard s5p_tvout_tvif_standard[] = {
	{
		.index	= 0,
		.id	= V4L2_STD_NTSC_M,
		.name	= "NTSC_M",
	}, {
		.index	= 1,
		.id	= V4L2_STD_PAL_BDGHI,
		.name	= "PAL_BDGHI",
	}, {
		.index	= 2,
		.id	= V4L2_STD_PAL_M,
		.name	= "PAL_M",
	}, {
		.index	= 3,
		.id	= V4L2_STD_PAL_N,
		.name	= "PAL_N",
	}, {
		.index	= 4,
		.id	= V4L2_STD_PAL_Nc,
		.name	= "PAL_Nc",
	}, {
		.index	= 5,
		.id	= V4L2_STD_PAL_60,
		.name	= "PAL_60",
	}, {
		.index	= 6,
		.id	= V4L2_STD_NTSC_443,
		.name	= "NTSC_443",
	}, {
		.index	= 7,
		.id	= V4L2_STD_480P_60_16_9,
		.name	= "480P_60_16_9",
	}, {
		.index	= 8,
		.id	= V4L2_STD_480P_60_4_3,
		.name	= "480P_60_4_3",
	}, {
		.index	= 9,
		.id	= V4L2_STD_576P_50_16_9,
		.name	= "576P_50_16_9",
	}, {
		.index	= 10,
		.id	= V4L2_STD_576P_50_4_3,
		.name	= "576P_50_4_3",
	}, {
		.index	= 11,
		.id	= V4L2_STD_720P_60,
		.name	= "720P_60",
	}, {
		.index	= 12,
		.id	= V4L2_STD_720P_50,
		.name	= "720P_50",
	}, {
		.index	= 13,
		.id	= V4L2_STD_1080P_60,
		.name	= "1080P_60",
	}, {
		.index	= 14,
		.id	= V4L2_STD_1080P_50,
		.name	= "1080P_50",
	}, {
		.index	= 15,
		.id	= V4L2_STD_1080I_60,
		.name	= "1080I_60",
	}, {
		.index	= 16,
		.id	= V4L2_STD_1080I_50,
		.name	= "1080I_50",
	}, {
		.index	= 17,
		.id	= V4L2_STD_480P_59,
		.name	= "480P_59",
	}, {
		.index	= 18,
		.id	= V4L2_STD_720P_59,
		.name	= "720P_59",
	}, {
		.index	= 19,
		.id	= V4L2_STD_1080I_59,
		.name	= "1080I_59",
	}, {
		.index	= 20,
		.id	= V4L2_STD_1080P_59,
		.name	= "1080I_50",
	}, {
		.index	= 21,
		.id	= V4L2_STD_1080P_30,
		.name	= "1080I_30",
	},
#ifdef CONFIG_HDMI_14A_3D
	{
		.index	= 22,
		.id	= V4L2_STD_TVOUT_720P_60_SBS_HALF,
		.name	= "720P_60_SBS_HALF",
	},
	{
		.index	= 23,
		.id	= V4L2_STD_TVOUT_720P_59_SBS_HALF,
		.name	= "720P_59_SBS_HALF",
	},
	{
		.index	= 24,
		.id	= V4L2_STD_TVOUT_720P_50_TB,
		.name	= "720P_50_TB",
	},
	{
		.index	= 25,
		.id	= V4L2_STD_TVOUT_1080P_24_TB,
		.name	= "1080P_24_TB",
	},
	{
		.index	= 26,
		.id	= V4L2_STD_TVOUT_1080P_23_TB,
		.name	= "1080P_23_TB",
	},
#endif
};

#define S5P_TVOUT_TVIF_NO_OF_STANDARD ARRAY_SIZE(s5p_tvout_tvif_standard)


static const struct v4l2_fmtdesc s5p_tvout_vo_fmt_desc[] = {
	{
		.index		= 0,
		.type		= V4L2_BUF_TYPE_PRIVATE,
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.description	= "NV12 (Linear YUV420 2 Planes)",
	}, {
		.index		= 1,
		.type		= V4L2_BUF_TYPE_PRIVATE,
		.pixelformat	= V4L2_PIX_FMT_NV12T,
		.description	= "NV12T (Tiled YUV420 2 Planes)",
	},
/* This block will be used on EXYNOS4210 */
	{
		.index		= 2,
		.type		= V4L2_BUF_TYPE_PRIVATE,
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.description	= "NV21 (Linear YUV420 2 Planes)",
	}, {
		.index		= 3,
		.type		= V4L2_BUF_TYPE_PRIVATE,
		.pixelformat	= V4L2_PIX_FMT_NV21T,
		.description	= "NV21T (Tiled YUV420 2 Planes)",
	},


};


static DEFINE_MUTEX(s5p_tvout_tvif_mutex);
static DEFINE_MUTEX(s5p_tvout_vo_mutex);

struct s5p_tvout_v4l2_private_data {
	struct v4l2_vid_overlay_src	vo_src_fmt;
	struct v4l2_rect		vo_src_rect;
	struct v4l2_window		vo_dst_fmt;
	struct v4l2_framebuffer		vo_dst_plane;

	int				tvif_output_index;
	v4l2_std_id			tvif_standard_id;

	atomic_t			tvif_use;
	atomic_t			vo_use;

#ifdef CONFIG_USE_TVOUT_CMA
	void				*vir_addr;
	dma_addr_t			dma_addr;
#endif

	struct device			*dev;
};

static struct s5p_tvout_v4l2_private_data s5p_tvout_v4l2_private = {
	.tvif_output_index	= -1,
	.tvif_standard_id	= 0,

	.tvif_use		= ATOMIC_INIT(0),
	.vo_use			= ATOMIC_INIT(0),
};

static void s5p_tvout_v4l2_init_private(void)
{
}

static int s5p_tvout_tvif_querycap(
		struct file *file, void *fh, struct v4l2_capability *cap)
{
	strcpy(cap->driver, "s5p-tvout-tvif");
	strcpy(cap->card, "Samsung TVOUT TV Interface");
	cap->capabilities = V4L2_CAP_VIDEO_OUTPUT;
	cap->version = KERNEL_VERSION(
			MAJOR_VERSION, MINOR_VERSION, RELEASE_VERSION);

	return 0;
}

static int s5p_tvout_tvif_g_std(
		struct file *file, void *fh, v4l2_std_id *norm)
{
	if (s5p_tvout_v4l2_private.tvif_standard_id == 0) {
		tvout_err("Standard has not set\n");
		return -1;
	}

	*norm = s5p_tvout_v4l2_private.tvif_standard_id;

	return 0;
}

static int s5p_tvout_tvif_s_std(
		struct file *file, void *fh, v4l2_std_id *norm)
{
	int i;
	v4l2_std_id std_id = *norm;

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif
	for (i = 0; i < S5P_TVOUT_TVIF_NO_OF_STANDARD; i++) {
		if (s5p_tvout_tvif_standard[i].id == std_id)
			break;
	}

	if (i == S5P_TVOUT_TVIF_NO_OF_STANDARD) {
		tvout_err("There is no TV standard(0x%08Lx)\n", std_id);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
		s5p_tvout_mutex_unlock();
#endif
		return -EINVAL;
	}

	s5p_tvout_v4l2_private.tvif_standard_id = std_id;

	tvout_dbg("standard id=0x%X, name=\"%s\"\n",
			(u32) std_id, s5p_tvout_tvif_standard[i].name);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif

	return 0;
}

static int s5p_tvout_tvif_enum_output(
		struct file *file, void *fh, struct v4l2_output *a)
{
	unsigned int index = a->index;

	if (index >= S5P_TVOUT_TVIF_NO_OF_OUTPUT) {
		tvout_err("Invalid index(%d)\n", index);

		return -EINVAL;
	}

	memcpy(a, &s5p_tvout_tvif_output[index], sizeof(struct v4l2_output));

	return 0;
}

static int s5p_tvout_tvif_g_output(
		struct file *file, void *fh, unsigned int *i)
{
	if (s5p_tvout_v4l2_private.tvif_output_index == -1) {
		tvout_err("Output has not set\n");
		return -EINVAL;
	}

	*i = s5p_tvout_v4l2_private.tvif_output_index;

	return 0;
}

static int s5p_tvout_tvif_s_output(
		struct file *file, void *fh, unsigned int i)
{
	enum s5p_tvout_disp_mode	tv_std;
	enum s5p_tvout_o_mode		tv_if;

	if (i >= S5P_TVOUT_TVIF_NO_OF_OUTPUT) {
		tvout_err("Invalid index(%d)\n", i);
		return -EINVAL;
	}

#if defined(CONFIG_HAS_EARLYSUSPEND)
	s5p_tvout_mutex_lock();
#endif
	on_start_process = true;
	s5p_tvout_v4l2_private.tvif_output_index = i;

	tvout_dbg("output id=%d, name=\"%s\"\n",
			(int) i, s5p_tvout_tvif_output[i].name);

	switch (s5p_tvout_tvif_output[i].type) {
	case V4L2_OUTPUT_TYPE_COMPOSITE:
		tv_if =	TVOUT_COMPOSITE;
		break;

	case V4L2_OUTPUT_TYPE_HDMI:
		tv_if =	TVOUT_HDMI;
		break;

	case V4L2_OUTPUT_TYPE_HDMI_RGB:
		tv_if =	TVOUT_HDMI_RGB;
		break;

	case V4L2_OUTPUT_TYPE_DVI:
		tv_if =	TVOUT_DVI;
		break;

	default:
		tvout_err("Invalid output type(%d)\n",
			s5p_tvout_tvif_output[i].type);
		goto error_on_tvif_s_output;
	}

	switch (s5p_tvout_v4l2_private.tvif_standard_id) {
	case V4L2_STD_NTSC_M:
		tv_std = TVOUT_NTSC_M;
		break;

	case V4L2_STD_PAL_BDGHI:
		tv_std = TVOUT_PAL_BDGHI;
		break;

	case V4L2_STD_PAL_M:
		tv_std = TVOUT_PAL_M;
		break;

	case V4L2_STD_PAL_N:
		tv_std = TVOUT_PAL_N;
		break;

	case V4L2_STD_PAL_Nc:
		tv_std = TVOUT_PAL_NC;
		break;

	case V4L2_STD_PAL_60:
		tv_std = TVOUT_PAL_60;
		break;

	case V4L2_STD_NTSC_443:
		tv_std = TVOUT_NTSC_443;
		break;

	case V4L2_STD_480P_60_16_9:
		tv_std = TVOUT_480P_60_16_9;
		break;

	case V4L2_STD_480P_60_4_3:
		tv_std = TVOUT_480P_60_4_3;
		break;

	case V4L2_STD_480P_59:
		tv_std = TVOUT_480P_59;
		break;
	case V4L2_STD_576P_50_16_9:
		tv_std = TVOUT_576P_50_16_9;
		break;

	case V4L2_STD_576P_50_4_3:
		tv_std = TVOUT_576P_50_4_3;
		break;

	case V4L2_STD_720P_60:
		tv_std = TVOUT_720P_60;
		break;

	case V4L2_STD_720P_59:
		tv_std = TVOUT_720P_59;
		break;

	case V4L2_STD_720P_50:
		tv_std = TVOUT_720P_50;
		break;

	case V4L2_STD_1080I_60:
		tv_std = TVOUT_1080I_60;
		break;

	case V4L2_STD_1080I_59:
		tv_std = TVOUT_1080I_59;
		break;

	case V4L2_STD_1080I_50:
		tv_std = TVOUT_1080I_50;
		break;

	case V4L2_STD_1080P_30:
		tv_std = TVOUT_1080P_30;
		break;

	case V4L2_STD_1080P_60:
		tv_std = TVOUT_1080P_60;
		break;

	case V4L2_STD_1080P_59:
		tv_std = TVOUT_1080P_59;
		break;

	case V4L2_STD_1080P_50:
		tv_std = TVOUT_1080P_50;
		break;

#ifdef CONFIG_HDMI_14A_3D
	case V4L2_STD_TVOUT_720P_60_SBS_HALF:
		tv_std = TVOUT_720P_60_SBS_HALF;
		break;
	case V4L2_STD_TVOUT_720P_59_SBS_HALF:
		tv_std = TVOUT_720P_59_SBS_HALF;
		break;
	case V4L2_STD_TVOUT_720P_50_TB:
		tv_std = TVOUT_720P_50_TB;
		break;
	case V4L2_STD_TVOUT_1080P_24_TB:
		tv_std = TVOUT_1080P_24_TB;
		break;
	case V4L2_STD_TVOUT_1080P_23_TB:
		tv_std = TVOUT_1080P_23_TB;
		break;
#endif
	default:
		tvout_err("Invalid standard id(0x%08Lx)\n",
			s5p_tvout_v4l2_private.tvif_standard_id);
		goto error_on_tvif_s_output;
	}

	s5p_tvif_ctrl_start(tv_std, tv_if);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;
error_on_tvif_s_output:
#if defined(CONFIG_HAS_EARLYSUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return -1;
};

static int s5p_tvout_tvif_cropcap(
		struct file *file, void *fh, struct v4l2_cropcap *a)
{
	enum s5p_tvout_disp_mode std;
	enum s5p_tvout_o_mode inf;

	struct v4l2_cropcap *cropcap = a;

	if (cropcap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		tvout_err("Invalid buf type(%d)\n", cropcap->type);
		return -EINVAL;
	}

	/* below part will be modified and moved to tvif ctrl class */
	s5p_tvif_ctrl_get_std_if(&std, &inf);

	switch (std) {
	case TVOUT_NTSC_M:
	case TVOUT_NTSC_443:
	case TVOUT_480P_60_16_9:
	case TVOUT_480P_60_4_3:
	case TVOUT_480P_59:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 720;
		cropcap->bounds.height = 480;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 720;
		cropcap->defrect.height = 480;
		break;

	case TVOUT_PAL_M:
	case TVOUT_PAL_BDGHI:
	case TVOUT_PAL_N:
	case TVOUT_PAL_NC:
	case TVOUT_PAL_60:
	case TVOUT_576P_50_16_9:
	case TVOUT_576P_50_4_3:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 720;
		cropcap->bounds.height = 576;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 720;
		cropcap->defrect.height = 576;
		break;

	case TVOUT_720P_60:
	case TVOUT_720P_59:
	case TVOUT_720P_50:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 1280;
		cropcap->bounds.height = 720;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 1280;
		cropcap->defrect.height = 720;
		break;

	case TVOUT_1080I_60:
	case TVOUT_1080I_59:
	case TVOUT_1080I_50:
	case TVOUT_1080P_60:
	case TVOUT_1080P_59:
	case TVOUT_1080P_50:
	case TVOUT_1080P_30:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 1920;
		cropcap->bounds.height = 1080;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 1920;
		cropcap->defrect.height = 1080;
		break;

#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_720P_60_SBS_HALF:
	case TVOUT_720P_59_SBS_HALF:
	case TVOUT_720P_50_TB:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 1280;
		cropcap->bounds.height = 720;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 1280;
		cropcap->defrect.height = 720;
		break;

	case TVOUT_1080P_24_TB:
	case TVOUT_1080P_23_TB:
		cropcap->bounds.top = 0;
		cropcap->bounds.left = 0;
		cropcap->bounds.width = 1920;
		cropcap->bounds.height = 1080;

		cropcap->defrect.top = 0;
		cropcap->defrect.left = 0;
		cropcap->defrect.width = 1920;
		cropcap->defrect.height = 1080;
		break;
#endif

	default:
		return -EINVAL;
	}

	return 0;
}

static int s5p_tvout_tvif_wait_for_vsync(void)
{
	sleep_on_timeout(&s5ptv_wq, HZ / 10);

	return 0;
}

const struct v4l2_ioctl_ops s5p_tvout_tvif_ioctl_ops = {
	.vidioc_querycap	= s5p_tvout_tvif_querycap,
	.vidioc_g_std		= s5p_tvout_tvif_g_std,
	.vidioc_s_std		= s5p_tvout_tvif_s_std,
	.vidioc_enum_output	= s5p_tvout_tvif_enum_output,
	.vidioc_g_output	= s5p_tvout_tvif_g_output,
	.vidioc_s_output	= s5p_tvout_tvif_s_output,
	.vidioc_cropcap		= s5p_tvout_tvif_cropcap,
};

#define VIDIOC_HDCP_ENABLE		_IOWR('V', 100, unsigned int)
#define VIDIOC_HDCP_STATUS		_IOR('V', 101, unsigned int)
#define VIDIOC_HDCP_PROT_STATUS		_IOR('V', 102, unsigned int)
#define VIDIOC_INIT_AUDIO		_IOR('V', 103, unsigned int)
#define VIDIOC_AV_MUTE			_IOR('V', 104, unsigned int)
#define VIDIOC_G_AVMUTE			_IOR('V', 105, unsigned int)
#define VIDIOC_SET_VSYNC_INT		_IOR('V', 106, unsigned int)
#define VIDIOC_WAITFORVSYNC		_IOR('V', 107, unsigned int)
#define VIDIOC_G_VP_BUFF_INFO		_IOR('V', 108, unsigned int)
#define VIDIOC_S_VP_BUFF_INFO		_IOR('V', 109, unsigned int)
#define VIDIOC_S_AUDIO_CHANNEL		_IOR('V', 110, unsigned int)
#define VIDIOC_S_Q_COLOR_RANGE		_IOR('V', 111, unsigned int)

long s5p_tvout_tvif_ioctl(
		struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	void *argp = (void *) arg;
	int i = 0;

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif

	tvout_dbg("\n");

	switch (cmd) {
	case VIDIOC_INIT_AUDIO:
		tvout_dbg("VIDIOC_INIT_AUDIO(%d)\n", (int) arg);

/*		s5ptv_status.hdmi.audio = (unsigned int) arg; */

		if (arg)
			s5p_tvif_ctrl_set_audio(true);
		else
			s5p_tvif_ctrl_set_audio(false);

		goto end_tvif_ioctl;

	case VIDIOC_AV_MUTE:
		tvout_dbg("VIDIOC_AV_MUTE(%d)\n", (int) arg);

		if (arg)
			s5p_tvif_ctrl_set_av_mute(true);
		else
			s5p_tvif_ctrl_set_av_mute(false);

		goto end_tvif_ioctl;

	case VIDIOC_G_AVMUTE:
		s5p_hdmi_ctrl_get_mute();
		goto end_tvif_ioctl;

	case VIDIOC_HDCP_ENABLE:
		tvout_dbg("VIDIOC_HDCP_ENABLE(%d)\n", (int) arg);

/*		s5ptv_status.hdmi.hdcp_en = (unsigned int) arg; */

		s5p_hdmi_ctrl_set_hdcp((bool) arg);
		goto end_tvif_ioctl;

	case VIDIOC_HDCP_STATUS: {
		unsigned int *status = (unsigned int *)&arg;

		*status = 1;

		goto end_tvif_ioctl;
	}

	case VIDIOC_HDCP_PROT_STATUS: {
		unsigned int *prot = (unsigned int *)&arg;

		*prot = 1;

		goto end_tvif_ioctl;
	}

	case VIDIOC_ENUMSTD: {
		struct v4l2_standard *p = (struct v4l2_standard *)arg;

		if (p->index >= S5P_TVOUT_TVIF_NO_OF_STANDARD) {
			tvout_dbg("VIDIOC_ENUMSTD: Invalid index(%d)\n",
					p->index);

			ret = -EINVAL;
			goto end_tvif_ioctl;
		}

		memcpy(p, &s5p_tvout_tvif_standard[p->index],
			sizeof(struct v4l2_standard));

		goto end_tvif_ioctl;
	}

	case VIDIOC_SET_VSYNC_INT:
		s5p_mixer_ctrl_set_vsync_interrupt((int)argp);
		goto end_tvif_ioctl;

	case VIDIOC_WAITFORVSYNC:
		s5p_tvout_tvif_wait_for_vsync();
		goto end_tvif_ioctl;

	case VIDIOC_G_VP_BUFF_INFO: {
		struct s5ptv_vp_buf_info __user *buff_info =
			(struct s5ptv_vp_buf_info __user *)arg;
		struct s5p_tvout_vp_buff __user *buffs;
		unsigned int tmp = S5PTV_VP_BUFF_CNT;
		ret = copy_to_user(&buff_info->buff_cnt, &tmp, sizeof(tmp));
		if (WARN_ON(ret))
			goto end_tvif_ioctl;
		ret = copy_from_user(&buffs, &buff_info->buffs,
				     sizeof(struct s5p_tvout_vp_buff *));
		if (WARN_ON(ret))
			goto end_tvif_ioctl;
		for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
			ret = copy_to_user(&buffs[i].phy_base,
					   &s5ptv_vp_buff.vp_buffs[i].phy_base,
					   sizeof(unsigned int));
			if (WARN_ON(ret))
				goto end_tvif_ioctl;
			ret = copy_to_user(&buffs[i].vir_base,
					   &s5ptv_vp_buff.vp_buffs[i].vir_base,
					   sizeof(unsigned int));
			if (WARN_ON(ret))
				goto end_tvif_ioctl;
			tmp = S5PTV_VP_BUFF_SIZE;
			ret = copy_to_user(&buffs[i].size, &tmp, sizeof(tmp));
			if (WARN_ON(ret))
				goto end_tvif_ioctl;
		}
		goto end_tvif_ioctl;
	}
	case VIDIOC_S_VP_BUFF_INFO: {
		struct s5ptv_vp_buf_info buff_info;
		struct s5p_tvout_vp_buff buffs[S5PTV_VP_BUFF_CNT];
		ret = copy_from_user(&buff_info,
				     (struct s5ptv_vp_buf_info __user *)arg,
				     sizeof(buff_info));
		if (WARN_ON(ret))
			goto end_tvif_ioctl;
		ret = copy_from_user(buffs, buff_info.buffs, sizeof(buffs));
		if (WARN_ON(ret))
			goto end_tvif_ioctl;

		if (buff_info.buff_cnt != S5PTV_VP_BUFF_CNT) {
			tvout_err("Insufficient buffer count (%d, %d)",
				  buff_info.buff_cnt, S5PTV_VP_BUFF_CNT);
			ret = -EINVAL;
			goto end_tvif_ioctl;
		}
		for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
			s5ptv_vp_buff.vp_buffs[i].phy_base = buffs[i].phy_base;
			s5ptv_vp_buff.vp_buffs[i].vir_base =
				(unsigned int)phys_to_virt(buffs[i].phy_base);
			s5ptv_vp_buff.vp_buffs[i].size = buffs[i].size;
			tvout_dbg("s5ptv_vp_buff phy_base = 0x%x, vir_base = 0x%8x\n",
				  s5ptv_vp_buff.vp_buffs[i].phy_base,
				  s5ptv_vp_buff.vp_buffs[i].vir_base);
		}
		goto end_tvif_ioctl;
	}
	case VIDIOC_S_AUDIO_CHANNEL: {
		if (!arg)
			s5p_tvif_audio_channel(TVOUT_AUDIO_2CH_VAL);
		else
			s5p_tvif_audio_channel(TVOUT_AUDIO_5_1CH_VAL);
		/* TODO Runtime change
		s5p_tvif_ctrl_stop();
		if (s5p_tvif_ctrl_start(TVOUT_720P_60, TVOUT_HDMI) < 0)
			goto end_tvif_ioctl; */
		break;
	}

	case VIDIOC_S_Q_COLOR_RANGE: {
		if ((int)arg != 0 && (int)arg != 1) {
			printk(KERN_ERR "Quantaization range has wrong value!\n");
			goto end_tvif_ioctl;
		}

		s5p_tvif_q_color_range((int)arg);
		break;
	}

	default:
		break;
	}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return video_ioctl2(file, cmd, arg);

end_tvif_ioctl:
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return ret;
}

#ifdef CONFIG_USE_TVOUT_CMA
static inline int alloc_vp_buff(void)
{
	int i;

	s5p_tvout_v4l2_private.vir_addr = dma_alloc_coherent(
				s5p_tvout_v4l2_private.dev,
				S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE,
				&s5p_tvout_v4l2_private.dma_addr, 0);

	if (!s5p_tvout_v4l2_private.vir_addr) {
		printk(KERN_ERR "S5P-TVOUT: %s: dma_alloc_coherent returns "
			"-ENOMEM\n", __func__);
		return -ENOMEM;
	}

	printk(KERN_INFO "%s[%d] size 0x%x, vaddr 0x%x, base 0x%x\n",
				__func__, __LINE__,
				S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE,
				(int) s5p_tvout_v4l2_private.vir_addr,
				(int) s5p_tvout_v4l2_private.dma_addr);

	for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
		s5ptv_vp_buff.vp_buffs[i].phy_base =
			(unsigned int) s5p_tvout_v4l2_private.dma_addr +
					(i * S5PTV_VP_BUFF_SIZE);
		s5ptv_vp_buff.vp_buffs[i].vir_base =
			(unsigned int) s5p_tvout_v4l2_private.vir_addr +
					(i * S5PTV_VP_BUFF_SIZE);
	}

	return 0;
}

static inline void free_vp_buff(void)
{
	dma_free_coherent(s5p_tvout_v4l2_private.dev,
			S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE,
			s5p_tvout_v4l2_private.vir_addr,
			s5p_tvout_v4l2_private.dma_addr);

	printk(KERN_INFO "%s[%d] size 0x%x, vaddr 0x%x, base 0x%x\n",
			__func__, __LINE__,
			S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE,
			(int) s5p_tvout_v4l2_private.vir_addr,
			(int) s5p_tvout_v4l2_private.dma_addr);
}
#else
static inline int alloc_vp_buff(void) { return 0; }
static inline void free_vp_buff(void) { }
#endif

static int s5p_tvout_tvif_open(struct file *file)
{
	int ret = 0;

	mutex_lock(&s5p_tvout_tvif_mutex);

	if (atomic_read(&s5p_tvout_v4l2_private.tvif_use) == 0)
		ret = alloc_vp_buff();

	if (!ret)
		atomic_inc(&s5p_tvout_v4l2_private.tvif_use);

	mutex_unlock(&s5p_tvout_tvif_mutex);

	tvout_dbg("count=%d\n", atomic_read(&s5p_tvout_v4l2_private.tvif_use));

	return ret;
}

static int s5p_tvout_tvif_release(struct file *file)
{
	tvout_dbg("count=%d\n", atomic_read(&s5p_tvout_v4l2_private.tvif_use));

	mutex_lock(&s5p_tvout_tvif_mutex);

	on_start_process = false;
	on_stop_process = true;
	tvout_dbg("on_stop_process(%d)\n", on_stop_process);
	atomic_dec(&s5p_tvout_v4l2_private.tvif_use);

	if (atomic_read(&s5p_tvout_v4l2_private.tvif_use) == 0) {
		s5p_tvout_mutex_lock();
		s5p_tvif_ctrl_stop();
		s5p_tvout_mutex_unlock();

		free_vp_buff();
	}

	on_stop_process = false;
	tvout_dbg("on_stop_process(%d)\n", on_stop_process);
	mutex_unlock(&s5p_tvout_tvif_mutex);

	return 0;
}

static struct v4l2_file_operations s5p_tvout_tvif_fops = {
	.owner		= THIS_MODULE,
	.open		= s5p_tvout_tvif_open,
	.release	= s5p_tvout_tvif_release,
	.ioctl		= s5p_tvout_tvif_ioctl
};


static int s5p_tvout_vo_querycap(
		struct file *file, void *fh, struct v4l2_capability *cap)
{
	strcpy(cap->driver, "s5p-tvout-vo");
	strcpy(cap->card, "Samsung TVOUT Video Overlay");
	cap->capabilities = V4L2_CAP_VIDEO_OVERLAY;
	cap->version = KERNEL_VERSION(
				MAJOR_VERSION, MINOR_VERSION, RELEASE_VERSION);

	return 0;
}

static int s5p_tvout_vo_enum_fmt_type_private(
		struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	int index = f->index;

	if (index >= ARRAY_SIZE(s5p_tvout_vo_fmt_desc)) {
		tvout_err("Invalid index(%d)\n", index);

		return -EINVAL;
	}

	memcpy(f, &s5p_tvout_vo_fmt_desc[index], sizeof(struct v4l2_fmtdesc));

	return 0;
}

static int s5p_tvout_vo_g_fmt_type_private(
		struct file *file, void *fh, struct v4l2_format *a)
{
	memcpy(a->fmt.raw_data,	&s5p_tvout_v4l2_private.vo_src_fmt,
		sizeof(struct v4l2_vid_overlay_src));

	return 0;
}

static int s5p_tvout_vo_s_fmt_type_private(
		struct file *file, void *fh, struct v4l2_format *a)
{
	struct v4l2_vid_overlay_src	vparam;
	struct v4l2_pix_format		*pix_fmt;
	enum s5p_vp_src_color		color;
	enum s5p_vp_field		field;
	unsigned int			src_vir_y_addr;
	unsigned int			src_vir_cb_addr;
	int				y_size;
	int				cbcr_size;
	unsigned int			copy_buff_idx;

#if defined(CONFIG_S5P_SYSMMU_TV)
	unsigned long base_y, base_c;
#endif
	memcpy(&vparam, a->fmt.raw_data, sizeof(struct v4l2_vid_overlay_src));

	pix_fmt = &vparam.pix_fmt;

	tvout_dbg("base_y=0x%X, base_c=0x%X, field=%d\n",
			(u32) vparam.base_y, (u32) vparam.base_c,
			pix_fmt->field);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif
	/* check progressive or not */
	if (pix_fmt->field == V4L2_FIELD_NONE) {
		/* progressive */
		switch (pix_fmt->pixelformat) {
		case V4L2_PIX_FMT_NV12:
			/* linear */
			tvout_dbg("pixelformat=V4L2_PIX_FMT_NV12\n");

			color = VP_SRC_COLOR_NV12;
			break;

		case V4L2_PIX_FMT_NV12T:
			/* tiled */
			tvout_dbg("pixelformat=V4L2_PIX_FMT_NV12T\n");
			color = VP_SRC_COLOR_TILE_NV12;
			break;
		case V4L2_PIX_FMT_NV21:
			/* linear */
			color = VP_SRC_COLOR_NV21;
			break;

		case V4L2_PIX_FMT_NV21T:
			/* tiled */
			color = VP_SRC_COLOR_TILE_NV21;
			break;

		default:
			tvout_err("src img format not supported\n");
			goto error_on_s_fmt_type_private;
		}

		field = VP_TOP_FIELD;
	} else if ((pix_fmt->field == V4L2_FIELD_TOP) ||
			(pix_fmt->field == V4L2_FIELD_BOTTOM)) {
		/* interlaced */
		switch (pix_fmt->pixelformat) {
		case V4L2_PIX_FMT_NV12:
			/* linear */
			tvout_dbg("pixelformat=V4L2_PIX_FMT_NV12\n");
			color = VP_SRC_COLOR_NV12IW;
			break;

		case V4L2_PIX_FMT_NV12T:
			/* tiled */
			tvout_dbg("pixelformat=V4L2_PIX_FMT_NV12T\n");
			color = VP_SRC_COLOR_TILE_NV12IW;
			break;
		case V4L2_PIX_FMT_NV21:
			/* linear */
			color = VP_SRC_COLOR_NV21IW;
			break;

		case V4L2_PIX_FMT_NV21T:
			/* tiled */
			color = VP_SRC_COLOR_TILE_NV21IW;
			break;

		default:
			tvout_err("src img format not supported\n");
			goto error_on_s_fmt_type_private;
		}

		field = (pix_fmt->field == V4L2_FIELD_BOTTOM) ?
				VP_BOTTOM_FIELD : VP_TOP_FIELD;

	} else {
		tvout_err("this field id not supported\n");

		goto error_on_s_fmt_type_private;
	}

	s5p_tvout_v4l2_private.vo_src_fmt = vparam;
#if defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_UMP_VCM_ALLOC)
	/*
	 * For TV system mmu test using UMP and VCMM
	 * vparam.base_y : secure ID
	 * vparam.base_c : offset of base_c from base_y
	 */
	base_y = ump_dd_dev_virtual_get_from_secure_id((unsigned int)
		vparam.base_y);
	base_c = base_y + (unsigned long)vparam.base_c;
	s5p_vp_ctrl_set_src_plane(base_y, base_c, pix_fmt->width,
		pix_fmt->height, color, field);
#elif defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_S5P_VMEM)
	/*
	 * For TV system mmu test
	 * vparam.base_y : cookie
	 * vparam.base_c : offset of base_c from base_y
	 */
	base_y = (unsigned long) s5p_getaddress((unsigned int)vparam.base_y);
	base_c = base_y + (unsigned long)vparam.base_c;
	s5p_vp_ctrl_set_src_plane(base_y, base_c, pix_fmt->width,
		pix_fmt->height, color, field);
#else
	if (pix_fmt->priv) {
		copy_buff_idx =
			s5ptv_vp_buff.
				copy_buff_idxs[s5ptv_vp_buff.curr_copy_idx];

		if ((void *)s5ptv_vp_buff.vp_buffs[copy_buff_idx].vir_base
			== NULL) {
			s5p_vp_ctrl_set_src_plane(
				(u32) vparam.base_y, (u32) vparam.base_c,
				pix_fmt->width, pix_fmt->height, color, field);
		} else {
			if (pix_fmt->pixelformat ==
				V4L2_PIX_FMT_NV12T
				|| pix_fmt->pixelformat == V4L2_PIX_FMT_NV21T) {
				y_size = ALIGN(ALIGN(pix_fmt->width, 128) *
					ALIGN(pix_fmt->height, 32), SZ_8K);
				cbcr_size = ALIGN(ALIGN(pix_fmt->width, 128) *
					ALIGN(pix_fmt->height >> 1, 32), SZ_8K);
			} else {
				y_size = pix_fmt->width * pix_fmt->height;
				cbcr_size =
					pix_fmt->width * (pix_fmt->height >> 1);
			}

			src_vir_y_addr = (unsigned int)phys_to_virt(
				(unsigned long)vparam.base_y);
			src_vir_cb_addr = (unsigned int)phys_to_virt(
				(unsigned long)vparam.base_c);

			memcpy(
				(void *)
				s5ptv_vp_buff.vp_buffs[copy_buff_idx].vir_base,
				(void *)src_vir_y_addr, y_size);
			memcpy(
				(void *)s5ptv_vp_buff.vp_buffs[copy_buff_idx].
					vir_base + y_size,
				(void *)src_vir_cb_addr, cbcr_size);

			flush_all_cpu_caches();
			outer_flush_all();

			s5p_vp_ctrl_set_src_plane(
				(u32) s5ptv_vp_buff.
					vp_buffs[copy_buff_idx].phy_base,
				(u32) s5ptv_vp_buff.
					vp_buffs[copy_buff_idx].phy_base
					+ y_size,
				pix_fmt->width, pix_fmt->height, color, field);

			s5ptv_vp_buff.curr_copy_idx++;
			if (s5ptv_vp_buff.curr_copy_idx >=
				S5PTV_VP_BUFF_CNT - 1)
				s5ptv_vp_buff.curr_copy_idx = 0;
		}
	} else {
		s5p_vp_ctrl_set_src_plane(
			(u32) vparam.base_y, (u32) vparam.base_c,
			pix_fmt->width, pix_fmt->height, color, field);
	}
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;

error_on_s_fmt_type_private:
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return -1;
}

static int s5p_tvout_vo_g_fmt_vid_overlay(
		struct file *file, void	*fh, struct v4l2_format *a)
{
	a->fmt.win = s5p_tvout_v4l2_private.vo_dst_fmt;

	return 0;
}

static int s5p_tvout_vo_s_fmt_vid_overlay(
		struct file *file, void *fh, struct v4l2_format *a)
{
	struct v4l2_rect *rect = &a->fmt.win.w;

	tvout_dbg("l=%d, t=%d, w=%d, h=%d, g_alpha_value=%d\n",
			rect->left, rect->top, rect->width, rect->height,
			a->fmt.win.global_alpha);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif
	s5p_tvout_v4l2_private.vo_dst_fmt = a->fmt.win;

	s5p_vp_ctrl_set_dest_win_alpha_val(a->fmt.win.global_alpha);
	s5p_vp_ctrl_set_dest_win(
		rect->left, rect->top,
		rect->width, rect->height);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;
}

static int s5p_tvout_vo_g_crop(
		struct file *file, void *fh, struct v4l2_crop *a)
{
	switch (a->type) {
	case V4L2_BUF_TYPE_PRIVATE:
		a->c = s5p_tvout_v4l2_private.vo_src_rect;
		break;

	default:
		tvout_err("Invalid buf type(0x%08x)\n", a->type);
		break;
	}

	return 0;
}

static int s5p_tvout_vo_s_crop(
		struct file *file, void *fh, struct v4l2_crop *a)
{
	tvout_dbg("\n");
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif
	switch (a->type) {
	case V4L2_BUF_TYPE_PRIVATE: {
		struct v4l2_rect *rect =
				&s5p_tvout_v4l2_private.vo_src_rect;

		*rect = a->c;

		tvout_dbg("l=%d, t=%d, w=%d, h=%d\n",
				rect->left, rect->top,
				rect->width, rect->height);

		s5p_vp_ctrl_set_src_win(
			rect->left, rect->top,
			rect->width, rect->height);
		break;
	}
	default:
		tvout_err("Invalid buf type(0x%08x)\n", a->type);
		break;
	}
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;
}

static int s5p_tvout_vo_g_fbuf(
		struct file *file, void *fh, struct v4l2_framebuffer *a)
{
	*a = s5p_tvout_v4l2_private.vo_dst_plane;

	a->capability = V4L2_FBUF_CAP_GLOBAL_ALPHA;

	return 0;
}

static int s5p_tvout_vo_s_fbuf(
		struct file *file, void *fh, struct v4l2_framebuffer *a)
{
	s5p_tvout_v4l2_private.vo_dst_plane = *a;

	tvout_dbg("g_alpha_enable=%d, priority=%d\n",
			(a->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) ? 1 : 0,
			a->fmt.priv);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif

	s5p_vp_ctrl_set_dest_win_blend(
		(a->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) ? 1 : 0);

	s5p_vp_ctrl_set_dest_win_priority(a->fmt.priv);

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;
}

static int s5p_tvout_vo_overlay(
		struct file *file, void *fh, unsigned int i)
{
	tvout_dbg("%s\n", (i) ? "start" : "stop");

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_lock();
#endif
	if (i) {
		s5p_vp_ctrl_start();
		/* restore vsync interrupt setting */
		s5p_mixer_set_vsync_interrupt(
			s5p_mixer_ctrl_get_vsync_interrupt());
	} else {
		/* disable vsync interrupt when VP is disabled */
		s5p_mixer_ctrl_disable_vsync_interrupt();
		s5p_vp_ctrl_stop();
	}

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CLOCK_GATING_ON_EARLY_SUSPEND)
	s5p_tvout_mutex_unlock();
#endif
	return 0;
}

const struct v4l2_ioctl_ops s5p_tvout_vo_ioctl_ops = {
	.vidioc_querycap		= s5p_tvout_vo_querycap,

	.vidioc_enum_fmt_type_private	= s5p_tvout_vo_enum_fmt_type_private,
	.vidioc_g_fmt_type_private	= s5p_tvout_vo_g_fmt_type_private,
	.vidioc_s_fmt_type_private	= s5p_tvout_vo_s_fmt_type_private,

	.vidioc_g_fmt_vid_overlay	= s5p_tvout_vo_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay	= s5p_tvout_vo_s_fmt_vid_overlay,

	.vidioc_g_crop			= s5p_tvout_vo_g_crop,
	.vidioc_s_crop			= s5p_tvout_vo_s_crop,

	.vidioc_g_fbuf			= s5p_tvout_vo_g_fbuf,
	.vidioc_s_fbuf			= s5p_tvout_vo_s_fbuf,

	.vidioc_overlay			= s5p_tvout_vo_overlay,
};

static int s5p_tvout_vo_open(struct file *file)
{
	int ret = 0;

	tvout_dbg("\n");

	mutex_lock(&s5p_tvout_vo_mutex);

	if (atomic_read(&s5p_tvout_v4l2_private.vo_use)) {
		tvout_err("Can't open TVOUT TVIF control\n");
		ret = -EBUSY;
	} else
		atomic_inc(&s5p_tvout_v4l2_private.vo_use);

	mutex_unlock(&s5p_tvout_vo_mutex);

	return ret;
}

static int s5p_tvout_vo_release(struct file *file)
{
	tvout_dbg("\n");

	s5p_vp_ctrl_stop();

	s5p_mixer_ctrl_disable_layer(MIXER_VIDEO_LAYER);

	atomic_dec(&s5p_tvout_v4l2_private.vo_use);

	return 0;
}

static struct v4l2_file_operations s5p_tvout_vo_fops = {
	.owner		= THIS_MODULE,
	.open		= s5p_tvout_vo_open,
	.release	= s5p_tvout_vo_release,
	.ioctl		= video_ioctl2
};


/* dummy function for release callback of v4l2 video device */
static void s5p_tvout_video_dev_release(struct video_device *vdev)
{
}

static struct video_device s5p_tvout_video_dev[] = {
	[0] = {
		.name		= "S5P TVOUT TVIF control",
		.fops		= &s5p_tvout_tvif_fops,
		.ioctl_ops	= &s5p_tvout_tvif_ioctl_ops,
		.minor		= S5P_TVOUT_TVIF_MINOR,
		.release	= s5p_tvout_video_dev_release,
		.tvnorms	= V4L2_STD_ALL_HD,
	},
	[1] = {
		.name		= "S5P TVOUT Video Overlay",
		.fops		= &s5p_tvout_vo_fops,
		.ioctl_ops	= &s5p_tvout_vo_ioctl_ops,
		.release	= s5p_tvout_video_dev_release,
		.minor		= S5P_TVOUT_VO_MINOR
	}
};

int s5p_tvout_v4l2_constructor(struct platform_device *pdev)
{
	int i;

	/* v4l2 video device registration */
	for (i = 0; i < ARRAY_SIZE(s5p_tvout_video_dev); i++) {

		if (video_register_device(
				&s5p_tvout_video_dev[i],
				VFL_TYPE_GRABBER,
				s5p_tvout_video_dev[i].minor) != 0) {
			tvout_err("Fail to register v4l2 video device\n");

			return -1;
		}
	}

	s5p_tvout_v4l2_private.dev = &pdev->dev;
	s5p_tvout_v4l2_init_private();

	return 0;
}

void s5p_tvout_v4l2_destructor(void)
{
	mutex_destroy(&s5p_tvout_tvif_mutex);
	mutex_destroy(&s5p_tvout_vo_mutex);
}
