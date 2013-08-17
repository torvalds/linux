/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/pm_qos.h>
#include <linux/debugfs.h>
#include <linux/syscalls.h>
#include <linux/bug.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#if defined(CONFIG_SOC_EXYNOS5250)
#include <mach/exynos5_bus.h>
#include <plat/bts.h>
#endif

#include "fimc-is-time.h"
#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"
#include "fimc-is-groupmgr.h"
#include "fimc-is-device-ischain.h"

#define SDCARD_FW
#define FIMC_IS_SETFILE_SDCARD_PATH		"/data/"
#define FIMC_IS_FW				"fimc_is_fw2.bin"
#define FIMC_IS_FW_SDCARD			"/data/fimc_is_fw2.bin"

#define FIMC_IS_FW_BASE_MASK			((1 << 26) - 1)
#define FIMC_IS_VERSION_SIZE			43
#define FIMC_IS_SETFILE_VER_OFFSET		0x40
#define FIMC_IS_SETFILE_VER_SIZE		52
#define FIMC_IS_SETFILE_MASK			0xFFFF

#define BINNING(x, y) ((1 << (((x) / (y)) >> 1)) * 1000)

#define FIMC_IS_CAL_SDCARD			"/data/cal_data.bin"
/*#define FIMC_IS_MAX_CAL_SIZE			(20 * 1024)*/
#define FIMC_IS_MAX_FW_SIZE			(2048 * 1024)
#define FIMC_IS_CAL_START_ADDR			(0x012D0000)
#define FIMC_IS_CAL_RETRY_CNT			(2)
#define FIMC_IS_FW_RETRY_CNT			(2)


/* Default setting values */
#define DEFAULT_PREVIEW_STILL_WIDTH		(1280) /* sensor margin : 16 */
#define DEFAULT_PREVIEW_STILL_HEIGHT		(720) /* sensor margin : 12 */
#define DEFAULT_CAPTURE_VIDEO_WIDTH		(1920)
#define DEFAULT_CAPTURE_VIDEO_HEIGHT		(1080)
#define DEFAULT_CAPTURE_STILL_WIDTH		(2560)
#define DEFAULT_CAPTURE_STILL_HEIGHT		(1920)
#define DEFAULT_CAPTURE_STILL_CROP_WIDTH	(2560)
#define DEFAULT_CAPTURE_STILL_CROP_HEIGHT	(1440)
#define DEFAULT_PREVIEW_VIDEO_WIDTH		(640)
#define DEFAULT_PREVIEW_VIDEO_HEIGHT		(480)

#if defined(CONFIG_SOC_EXYNOS5250)
static struct pm_qos_request pm_qos_req_cpu;
static struct exynos5_bus_int_handle *isp_int_handle_min;
static struct exynos5_bus_mif_handle *isp_mif_handle_min;
#elif defined(CONFIG_SOC_EXYNOS5410)
extern struct fimc_is_from_info		*sysfs_finfo;
extern struct fimc_is_from_info		*sysfs_pinfo;
#endif

#ifdef FW_DEBUG
#define DEBUG_FS_ROOT_NAME	"fimc-is"
#define DEBUG_FS_FILE_NAME	"isfw-msg"
static struct dentry		*debugfs_root;
static struct dentry		*debugfs_file;

#define SETFILE_SIZE	0x6000
#define READ_SIZE		0x100

#define HEADER_CRC32_LEN (128 / 2)
#define OEM_CRC32_LEN (192 / 2)
#define AWB_CRC32_LEN (32 / 2)
#define SHADING_CRC32_LEN (2336 / 2)

static char fw_name[100];
static int cam_id = 0;
bool is_dumped_fw_loading_needed = false;

static int isfw_debug_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;

	return 0;
}

static int isfw_debug_read(struct file *file, char __user *user_buf,
	size_t buf_len, loff_t *ppos)
{
	int debug_cnt;
	char *debug;
	int count1, count2;
	struct fimc_is_device_ischain *device =
		(struct fimc_is_device_ischain *)file->private_data;
	struct fimc_is_ishcain_mem *imemory;
	struct fimc_is_core *core;

	BUG_ON(!device);

	count1 = 0;
	count2 = 0;
	debug_cnt = 0;
	imemory = &device->imemory;
	core = (struct fimc_is_core *)device->interface->core;

	if (atomic_read(&core->video_isp.refcount) <= 0) {
		err("isp video node is not open");
		goto exit;
	}

	vb2_ion_sync_for_device(imemory->fw_cookie, DEBUG_OFFSET,
		DEBUG_CNT, DMA_FROM_DEVICE);

	debug_cnt = *((int *)(imemory->kvaddr + DEBUGCTL_OFFSET)) - DEBUG_OFFSET;

	if (core->debug_cnt > debug_cnt) {
		count1 = DEBUG_CNT - core->debug_cnt;
		count2 = debug_cnt;
	} else {
		count1 = debug_cnt - core->debug_cnt;
		count2 = 0;
	}

	if (count1) {
		debug = (char *)(imemory->kvaddr + DEBUG_OFFSET + core->debug_cnt);

		if (count1 > buf_len)
			count1 = buf_len;

		memcpy(user_buf, debug, count1);
		core->debug_cnt += count1;
	}

	if (count1 == buf_len) {
		count2 = 0;
		goto exit;
	}

	if (count2) {
		core->debug_cnt = 0;
		debug = (char *)(imemory->kvaddr + DEBUG_OFFSET);

		if ((count1 + count2) > buf_len)
			count2 = buf_len -  count1;

		memcpy(user_buf, debug, count2);
		core->debug_cnt += count2;
	}

exit:
	return count1 + count2;
}

static const struct file_operations debug_fops = {
	.open	= isfw_debug_open,
	.read	= isfw_debug_read,
	.llseek	= default_llseek
};

#endif

static const struct sensor_param init_sensor_param = {
	.frame_rate = {
#ifdef FIXED_FPS_DEBUG
		.frame_rate = FIXED_FPS_VALUE,
#else
		.frame_rate = 30,
#endif
	},
};

static const struct isp_param init_isp_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_BAYER,
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.frametime_min = 0,
		.frametime_max = 33333,
		.binning_ratio_x = 1000,
		.binning_ratio_y = 1000,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0,
		.height = 0,
		.format = 0,
		.bitwidth = 0,
		.plane = 0,
		.order = 0,
		.buffer_number = 0,
		.buffer_address = 0,
		.binning_ratio_x = 1000,
		.binning_ratio_y = 1000,
		.err = 0,
	},
	.dma2_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0,
		.height = 0,
		.format = 0,
		.bitwidth = 0,
		.plane = 0,
		.order = 0,
		.buffer_number = 0,
		.buffer_address = 0,
		.binning_ratio_x = 1000,
		.binning_ratio_y = 1000,
		.err = 0,
	},
	.aa = {
		.cmd = ISP_AA_COMMAND_START,
		.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB,
		.mode = 0,
		.scene = 0,
		.sleep = 0,
		.uiAfFace = 0,
		.touch_x = 0, .touch_y = 0,
		.manual_af_setting = 0,
		.err = ISP_AF_ERROR_NO,
	},
	.flash = {
		.cmd = ISP_FLASH_COMMAND_DISABLE,
		.redeye = ISP_FLASH_REDEYE_DISABLE,
		.err = ISP_FLASH_ERROR_NO,
	},
	.awb = {
		.cmd = ISP_AWB_COMMAND_AUTO,
		.illumination = 0,
		.err = ISP_AWB_ERROR_NO,
	},
	.effect = {
		.cmd = ISP_IMAGE_EFFECT_DISABLE,
		.err = ISP_IMAGE_EFFECT_ERROR_NO,
	},
	.iso = {
		.cmd = ISP_ISO_COMMAND_AUTO,
		.value = 0,
		.err = ISP_ISO_ERROR_NO,
	},
	.adjust = {
		.cmd = ISP_ADJUST_COMMAND_AUTO,
		.contrast = 0,
		.saturation = 0,
		.sharpness = 0,
		.exposure = 0,
		.brightness = 0,
		.hue = 0,
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_CENTER,
		.win_pos_x = 0, .win_pos_y = 0,
		.win_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.win_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.err = ISP_METERING_ERROR_NO,
	},
	.afc = {
		.cmd = ISP_AFC_COMMAND_AUTO,
		.manual = 0, .err = ISP_AFC_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma1_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.dma_out_mask = 0,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_INPUT_FORMAT_YUV444,
		.bitwidth = DMA_INPUT_BIT_WIDTH_8BIT,
		.plane = DMA_INPUT_PLANE_1,
		.order = DMA_INPUT_ORDER_YCbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_BAYER,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_12BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_GB_BG,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xFFFFFFFF,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_drc_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_INPUT_FORMAT_YUV444,
		.bitwidth = DMA_INPUT_BIT_WIDTH_8BIT,
		.plane = DMA_INPUT_PLANE_1,
		.order = DMA_INPUT_ORDER_YCbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct scalerc_param init_scalerc_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.effect = {
		.cmd = 0,
		.err = 0,
	},
	.input_crop = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_STILL_CROP_WIDTH,
		.crop_height = DEFAULT_CAPTURE_STILL_CROP_HEIGHT,
		.in_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.in_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.out_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.out_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.err = 0,
	},
	.output_crop = {
		.cmd = SCALER_CROP_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.crop_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_CrYCbY,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xffff,
		.reserved[0] = 2, /* unscaled*/
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct odc_param init_odc_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct dis_param init_dis_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};
static const struct tdnr_param init_tdnr_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.frame = {
		.cmd = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV420,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_2,
		.order = DMA_OUTPUT_ORDER_CbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xffff,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct scalerp_param init_scalerp_param = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.effect = {
		.cmd = 0,
		.err = 0,
	},
	.input_crop = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.crop_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.in_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.in_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.out_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.out_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.err = 0,
	},
	.output_crop = {
		.cmd = SCALER_CROP_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.crop_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV420,
		.err = 0,
	},
	.rotation = {
		.cmd = 0,
		.err = 0,
	},
	.flip = {
		.cmd = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.uiCropOffsetX = 0,
		.uiCropOffsetX = 0,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV420,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_3,
		.order = DMA_OUTPUT_ORDER_NO,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xffff,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct fd_param init_fd_param = {
	.control = {
		.cmd = CONTROL_COMMAND_STOP,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.config = {
		.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER |
			FD_CONFIG_COMMAND_ROLL_ANGLE |
			FD_CONFIG_COMMAND_YAW_ANGLE |
			FD_CONFIG_COMMAND_SMILE_MODE |
			FD_CONFIG_COMMAND_BLINK_MODE |
			FD_CONFIG_COMMAND_EYES_DETECT |
			FD_CONFIG_COMMAND_MOUTH_DETECT |
			FD_CONFIG_COMMAND_ORIENTATION |
			FD_CONFIG_COMMAND_ORIENTATION_VALUE,
		.max_number = CAMERA2_MAX_FACES,
		.roll_angle = FD_CONFIG_ROLL_ANGLE_FULL,
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45_90,
		.smile_mode = FD_CONFIG_SMILE_MODE_DISABLE,
		.blink_mode = FD_CONFIG_BLINK_MODE_DISABLE,
		.eye_detect = FD_CONFIG_EYES_DETECT_ENABLE,
		.mouth_detect = FD_CONFIG_MOUTH_DETECT_DISABLE,
		.orientation = FD_CONFIG_ORIENTATION_DISABLE,
		.orientation_value = 0,
		.err = ERROR_FD_NO,
	},
};

#ifndef RESERVED_MEM
static int fimc_is_ishcain_deinitmem(struct fimc_is_device_ischain *device)
{
	int ret = 0;

	vb2_ion_private_free(device->imemory.fw_cookie);

	return ret;
}
#endif

static void fimc_is_ischain_cache_flush(struct fimc_is_device_ischain *this,
	u32 offset, u32 size)
{
	vb2_ion_sync_for_device(this->imemory.fw_cookie,
		offset,
		size,
		DMA_TO_DEVICE);
}

static void fimc_is_ischain_region_invalid(struct fimc_is_device_ischain *device)
{
	vb2_ion_sync_for_device(
		device->imemory.fw_cookie,
		device->imemory.offset_region,
		sizeof(struct is_region),
		DMA_FROM_DEVICE);
}

static void fimc_is_ischain_region_flush(struct fimc_is_device_ischain *device)
{
	vb2_ion_sync_for_device(
		device->imemory.fw_cookie,
		device->imemory.offset_region,
		sizeof(struct is_region),
		DMA_TO_DEVICE);
}

void fimc_is_ischain_meta_flush(struct fimc_is_frame *frame)
{
#ifdef ENABLE_CACHE
	vb2_ion_sync_for_device(
		(void *)frame->cookie_shot,
		0,
		frame->shot_size,
		DMA_TO_DEVICE);
#endif
}

void fimc_is_ischain_meta_invalid(struct fimc_is_frame *frame)
{
#ifdef ENABLE_CACHE
	vb2_ion_sync_for_device(
		(void *)frame->cookie_shot,
		0,
		frame->shot_size,
		DMA_FROM_DEVICE);
#endif
}

static void fimc_is_ischain_version(struct fimc_is_device_ischain *this, char *name, const char *load_bin, u32 size)
{
	struct fimc_is_from_info *pinfo = NULL;
	char version_str[60];


	if (!strcmp(fw_name, name)) {
		memcpy(version_str, &load_bin[size - FIMC_IS_VERSION_SIZE],
			FIMC_IS_VERSION_SIZE);
		version_str[FIMC_IS_VERSION_SIZE] = '\0';

		pinfo = &this->pinfo;
		memcpy(pinfo->header_ver, &version_str[32], 11);
		pinfo->header_ver[11] = '\0';
	} else {
		memcpy(version_str, &load_bin[size - FIMC_IS_SETFILE_VER_OFFSET],
			FIMC_IS_SETFILE_VER_SIZE);
		version_str[FIMC_IS_SETFILE_VER_SIZE] = '\0';

		pinfo = &this->pinfo;
		memcpy(pinfo->setfile_ver, &version_str[17], 4);
		pinfo->setfile_ver[4] = '\0';
	}
	printk(KERN_INFO "%s version : %s\n", name, version_str);
}


static int fimc_is_ischain_loadfirm(struct fimc_is_device_ischain *this)
{
	int ret = 0;
	int location = 0;
	const struct firmware *fw_blob;
	u8 *buf = NULL;
#ifdef SDCARD_FW
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;
	char fw_path[100];

	dbg_ischain("%s\n", __func__);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	fp = filp_open(FIMC_IS_FW_SDCARD, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		goto request_fw;
	}

	location = 1;
	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;
	pr_info("start, file path %s, size %ld Bytes\n",
		is_dumped_fw_loading_needed ? fw_path : FIMC_IS_FW_SDCARD, fsize);
	buf = vmalloc(fsize);
	if (!buf) {
		dev_err(&this->pdev->dev,
			"failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}
	nread = vfs_read(fp, (char __user *)buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		dev_err(&this->pdev->dev,
			"failed to read firmware file, %ld Bytes\n", nread);
		ret = -EIO;
		goto out;
	}

	memcpy((void *)this->imemory.kvaddr, (void *)buf, fsize);
	fimc_is_ischain_cache_flush(this, 0, fsize + 1);
	fimc_is_ischain_version(this, fw_name, buf, fsize);

request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif
		ret = request_firmware(&fw_blob, fw_name, &this->pdev->dev);
		if (ret) {
			err("request_firmware is fail(%d)", ret);
			ret = -EINVAL;
			goto out;
		}

		memcpy((void *)this->imemory.kvaddr, fw_blob->data,
			fw_blob->size);
		fimc_is_ischain_cache_flush(this, 0, fw_blob->size + 1);
		fimc_is_ischain_version(this, fw_name, fw_blob->data,
			fw_blob->size);

		release_firmware(fw_blob);
#ifdef SDCARD_FW
	}
#endif

out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		if(buf)
			vfree(buf);
		if(fp)
			filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif

	if (ret)
		err("firmware loading is fail");
	else
		pr_info("Camera: the %s FW were applied successfully.\n",
			((cam_id == CAMERA_SINGLE_REAR) &&
				is_dumped_fw_loading_needed) ? "dumped" : "default");

	return ret;
}

static int fimc_is_ischain_loadsetf(struct fimc_is_device_ischain *this,
	u32 load_addr, char *setfile_name)
{
	int ret = 0;
	int location = 0;
	void *address;
	const struct firmware *fw_blob;
	u8 *buf = NULL;
#ifdef SDCARD_FW
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;
	char setfile_path[256];
	u32 retry;

	dbg_ischain("%s\n", __func__);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	memset(setfile_path, 0x00, sizeof(setfile_path));
	snprintf(setfile_path, sizeof(setfile_path), "%s%s",
		FIMC_IS_SETFILE_SDCARD_PATH, setfile_name);
	fp = filp_open(setfile_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		goto request_fw;
	}

	location = 1;
	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;
	pr_info("start, file path %s, size %ld Bytes\n",
		setfile_path, fsize);
	buf = vmalloc(fsize);
	if (!buf) {
		dev_err(&this->pdev->dev,
			"failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}
	nread = vfs_read(fp, (char __user *)buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		dev_err(&this->pdev->dev,
			"failed to read firmware file, %ld Bytes\n", nread);
		ret = -EIO;
		goto out;
	}

	address = (void *)(this->imemory.kvaddr + load_addr);
	memcpy((void *)address, (void *)buf, fsize);
	fimc_is_ischain_cache_flush(this, load_addr, fsize + 1);
	fimc_is_ischain_version(this, setfile_name, buf, fsize);

request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif

		retry = 4;
		ret = request_firmware((const struct firmware **)&fw_blob,
			setfile_name, &this->pdev->dev);
		while (--retry && ret) {
			mwarn("request_firmware is fail(%d)", this, ret);
			ret = request_firmware((const struct firmware **)&fw_blob,
				setfile_name, &this->pdev->dev);
		}

		if (!retry) {
			merr("request_firmware is fail(%d)", this, ret);
			ret = -EINVAL;
			goto out;
		}

		address = (void *)(this->imemory.kvaddr + load_addr);
		memcpy(address, fw_blob->data, fw_blob->size);
		fimc_is_ischain_cache_flush(this, load_addr, fw_blob->size + 1);
		fimc_is_ischain_version(this, setfile_name, fw_blob->data,
			(u32)fw_blob->size);

		release_firmware(fw_blob);
#ifdef SDCARD_FW
	}
#endif

out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		if(buf)
			vfree(buf);
		if(fp)
			filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif

	if (ret)
		err("setfile loading is fail");
	else
		pr_info("Camera: the %s Setfile were applied successfully.\n",
			((cam_id == CAMERA_SINGLE_REAR) &&
				is_dumped_fw_loading_needed) ? "dumped" : "default");

	return ret;
}

static void fimc_is_ischain_forcedown(struct fimc_is_device_ischain *this,
	bool on)
{
	if (on) {
		printk(KERN_INFO "Set low poweroff mode\n");
		__raw_writel(0x0, PMUREG_ISP_ARM_OPTION);
		__raw_writel(0x1CF82000, PMUREG_ISP_LOW_POWER_OFF);
		this->force_down = true;
	} else {
		printk(KERN_INFO "Clear low poweroff mode\n");
		__raw_writel(0xFFFFFFFF, PMUREG_ISP_ARM_OPTION);
		__raw_writel(0x8, PMUREG_ISP_LOW_POWER_OFF);
		this->force_down = false;
	}
}

void tdnr_s3d_pixel_async_sw_reset(struct fimc_is_device_ischain *this)
{
	u32 cfg = readl(SYSREG_GSCBLK_CFG1);
	/* S3D pixel async sw reset */
	cfg &= ~(1 << 25);
	writel(cfg, SYSREG_GSCBLK_CFG1);

	cfg = readl(SYSREG_ISPBLK_CFG);
	/* 3DNR pixel async sw reset */
	cfg &= ~(1 << 5);
	writel(cfg, SYSREG_ISPBLK_CFG);
}

int fimc_is_ischain_power(struct fimc_is_device_ischain *this, int on)
{
	int ret = 0;
	u32 timeout;
	u32 debug;

	struct device *dev = &this->pdev->dev;
	struct fimc_is_core *core
		= (struct fimc_is_core *)platform_get_drvdata(this->pdev);
#if defined(CONFIG_SOC_EXYNOS5250)
	struct fimc_is_device_sensor *sensor = &core->sensor;
	struct fimc_is_enum_sensor *sensor_info
		= &sensor->enum_sensor[sensor->id_position];
#endif

	if (on) {
		/* 1. force poweroff setting */
		if (this->force_down)
			fimc_is_ischain_forcedown(this, false);

		/* 2. FIMC-IS local power enable */
#if defined(CONFIG_PM_RUNTIME)
		dbg_ischain("pm_runtime_suspended = %d\n",
			pm_runtime_suspended(dev));
		pm_runtime_get_sync(dev);
#else
		fimc_is_runtime_resume(dev);
		printk(KERN_INFO "%s(%d) - fimc_is runtime resume complete\n", __func__, on);
#endif

		snprintf(fw_name, sizeof(fw_name), "%s", FIMC_IS_FW);

		/* 3. Load IS firmware */
		ret = fimc_is_ischain_loadfirm(this);
		if (ret) {
			err("failed to fimc_is_request_firmware (%d)", ret);
			clear_bit(FIMC_IS_ISCHAIN_LOADED, &this->state);
			ret = -EINVAL;
			goto exit;
		}
		set_bit(FIMC_IS_ISCHAIN_LOADED, &this->state);

		/* 3. S/W reset pixel async bridge */
		if (soc_is_exynos5410())
			tdnr_s3d_pixel_async_sw_reset(this);

		printk(KERN_INFO "%s(%d) - async bridge\n", __func__, on);

		/* 4. A5 start address setting */
		dbg_ischain("imemory.base(dvaddr) : 0x%08x\n",
			this->imemory.dvaddr);
		dbg_ischain("imemory.base(kvaddr) : 0x%08X\n",
			this->imemory.kvaddr);

		if (!this->imemory.dvaddr) {
			err("firmware device virtual is null");
			ret = -ENOMEM;
			goto exit;
		}

		writel(this->imemory.dvaddr, this->regs + BBOAR);

		printk(KERN_INFO "%s(%d) - check dvaddr validate...\n", __func__, on);

		/* 5. A5 power on*/
		writel(0x1, PMUREG_ISP_ARM_CONFIGURATION);

		printk(KERN_INFO "%s(%d) - A5 Power on\n", __func__, on);

		/* 6. enable A5 */
		writel(0x00018000, PMUREG_ISP_ARM_OPTION);
		timeout = 1000;

		printk(KERN_INFO "%s(%d) - A5 enable start...\n", __func__, on);

		while ((__raw_readl(PMUREG_ISP_ARM_STATUS) & 0x1) != 0x1) {
			if (timeout == 0)
				err("A5 power on failed\n");
			timeout--;
			udelay(1);
		}

		printk(KERN_INFO "%s(%d) - A5 enable end...\n", __func__, on);

		set_bit(FIMC_IS_ISCHAIN_POWER_ON, &this->state);

		/* for mideaserver force down */
		set_bit(FIMC_IS_ISCHAIN_POWER_ON, &core->state);

		printk(KERN_INFO "%s(%d) - change A5 state\n", __func__, on);
	} else {
		/* 1. disable A5 */
		if (test_bit(IS_IF_STATE_START, &this->interface->state))
			writel(0x10000, PMUREG_ISP_ARM_OPTION);
		else
			writel(0x00000, PMUREG_ISP_ARM_OPTION);

		/* Check FW state for WFI of A5 */
		debug = readl(this->interface->regs + ISSR6);
		printk(KERN_INFO "%s: A5 state(0x%x)\n", __func__, debug);

		writel(0x0, PMUREG_CMU_RESET_ISP_SYS_PWR_REG);
		writel(0x0, PMUREG_CMU_SYSCLK_ISP_SYS_PWR_REG);
		writel(0x0, PMUREG_ISP_ARM_SYS_PWR_REG);

		/* 2. FIMC-IS local power down */
#if defined(CONFIG_PM_RUNTIME)
		pm_runtime_put_sync(dev);
		dbg_ischain("pm_runtime_suspended = %d\n",
					pm_runtime_suspended(dev));
#else
		fimc_is_runtime_suspend(dev);
#endif
		clear_bit(FIMC_IS_ISCHAIN_POWER_ON, &this->state);

		/* for mideaserver force down */
		clear_bit(FIMC_IS_ISCHAIN_POWER_ON, &core->state);
	}

exit:
	pr_info("%s(%d)\n", __func__,
		test_bit(FIMC_IS_ISCHAIN_POWER_ON, &this->state));

	return ret;
}

static int fimc_is_itf_s_param(struct fimc_is_device_ischain *this,
	u32 indexes, u32 lindex, u32 hindex)
{
	int ret = 0;

	fimc_is_ischain_region_flush(this);

	if (lindex || hindex) {
		ret = fimc_is_hw_s_param(this->interface,
			this->instance,
			indexes,
			lindex,
			hindex);
	}

	return ret;
}

static int fimc_is_itf_a_param(struct fimc_is_device_ischain *device,
	u32 group)
{
	int ret = 0;
	u32 setfile;

	BUG_ON(!device);

	setfile = (device->setfile & FIMC_IS_SETFILE_MASK);

	ret = fimc_is_hw_a_param(device->interface,
		device->instance,
		group,
		setfile);

	return ret;
}

static int fimc_is_itf_f_param(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 setfile;
	u32 group = 0;
#ifdef DEBUG
	u32 navailable = 0;
	struct is_region *region = device->is_region;
#endif

	mdbgd_ischain(" NAME          SIZE    BINNING    FRAMERATE\n", device);
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state) ||
		device->group_3ax.id == GROUP_ID_3A0)
		mdbgd_ischain("SENSOR :  %04dx%04d        %1dx%1d          %3d\n",
			device,
			region->parameter.isp.dma1_input.width + device->margin_width,
			region->parameter.isp.dma1_input.height + device->margin_height,
			(region->parameter.isp.dma1_input.binning_ratio_x / 1000),
			(region->parameter.isp.dma1_input.binning_ratio_y / 1000),
			region->parameter.sensor.frame_rate.frame_rate
			);
	else
		mdbgd_ischain("SENSOR :  %04dx%04d        %1dx%1d          %3d\n",
			device,
			region->parameter.sensor.dma_output.width,
			region->parameter.sensor.dma_output.height,
			(region->parameter.isp.otf_input.binning_ratio_x / 1000),
			(region->parameter.isp.otf_input.binning_ratio_y / 1000),
			region->parameter.sensor.frame_rate.frame_rate
			);
	mdbgd_ischain(" NAME    ON  BYPASS PATH        SIZE FORMAT\n", device);
	mdbgd_ischain("3AX DI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.isp.control.cmd,
		region->parameter.isp.control.bypass,
		region->parameter.isp.dma1_input.cmd,
		region->parameter.isp.dma1_input.width,
		region->parameter.isp.dma1_input.height,
		region->parameter.isp.dma1_input.format
		);
	mdbgd_ischain("3AX DO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.isp.control.cmd,
		region->parameter.isp.control.bypass,
		region->parameter.isp.dma2_output.cmd,
		region->parameter.isp.dma2_output.width,
		region->parameter.isp.dma2_output.height,
		region->parameter.isp.dma2_output.format
		);
	mdbgd_ischain("ISP DI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.isp.control.cmd,
		region->parameter.isp.control.bypass,
		region->parameter.isp.dma2_output.cmd,
		region->parameter.isp.dma2_output.width,
		region->parameter.isp.dma2_output.height,
		region->parameter.isp.dma2_output.format
		);
	mdbgd_ischain("ISP OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.isp.control.cmd,
		region->parameter.isp.control.bypass,
		region->parameter.isp.otf_input.cmd,
		region->parameter.isp.otf_input.width,
		region->parameter.isp.otf_input.height,
		region->parameter.isp.otf_input.format
		);
	mdbgd_ischain("ISP OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.isp.control.cmd,
		region->parameter.isp.control.bypass,
		region->parameter.isp.otf_output.cmd,
		region->parameter.isp.otf_output.width,
		region->parameter.isp.otf_output.height,
		region->parameter.isp.otf_output.format
		);
	mdbgd_ischain("DRC OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.drc.control.cmd,
		region->parameter.drc.control.bypass,
		region->parameter.drc.otf_input.cmd,
		region->parameter.drc.otf_input.width,
		region->parameter.drc.otf_input.height,
		region->parameter.drc.otf_input.format
		);
	mdbgd_ischain("DRC OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.drc.control.cmd,
		region->parameter.drc.control.bypass,
		region->parameter.drc.otf_output.cmd,
		region->parameter.drc.otf_output.width,
		region->parameter.drc.otf_output.height,
		region->parameter.drc.otf_output.format
		);
	mdbgd_ischain("SCC OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.scalerc.control.cmd,
		region->parameter.scalerc.control.bypass,
		region->parameter.scalerc.otf_input.cmd,
		region->parameter.scalerc.otf_input.width,
		region->parameter.scalerc.otf_input.height,
		region->parameter.scalerc.otf_input.format
		);
	mdbgd_ischain("SCC DO : %2d    %4d  %3d   %04dx%04d %4d,%d\n", device,
		region->parameter.scalerc.control.cmd,
		region->parameter.scalerc.control.bypass,
		region->parameter.scalerc.dma_output.cmd,
		region->parameter.scalerc.dma_output.width,
		region->parameter.scalerc.dma_output.height,
		region->parameter.scalerc.dma_output.format,
		region->parameter.scalerc.dma_output.plane
		);
	mdbgd_ischain("SCC OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.scalerc.control.cmd,
		region->parameter.scalerc.control.bypass,
		region->parameter.scalerc.otf_output.cmd,
		region->parameter.scalerc.otf_output.width,
		region->parameter.scalerc.otf_output.height,
		region->parameter.scalerc.otf_output.format
		);
	mdbgd_ischain("ODC OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.odc.control.cmd,
		region->parameter.odc.control.bypass,
		region->parameter.odc.otf_input.cmd,
		region->parameter.odc.otf_input.width,
		region->parameter.odc.otf_input.height,
		region->parameter.odc.otf_input.format
		);
	mdbgd_ischain("ODC OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.odc.control.cmd,
		region->parameter.odc.control.bypass,
		region->parameter.odc.otf_output.cmd,
		region->parameter.odc.otf_output.width,
		region->parameter.odc.otf_output.height,
		region->parameter.odc.otf_output.format
		);
	mdbgd_ischain("DIS OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.dis.control.cmd,
		region->parameter.dis.control.bypass,
		region->parameter.dis.otf_input.cmd,
		region->parameter.dis.otf_input.width,
		region->parameter.dis.otf_input.height,
		region->parameter.dis.otf_input.format
		);
	mdbgd_ischain("DIS OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.dis.control.cmd,
		region->parameter.dis.control.bypass,
		region->parameter.dis.otf_output.cmd,
		region->parameter.dis.otf_output.width,
		region->parameter.dis.otf_output.height,
		region->parameter.dis.otf_output.format
		);
	mdbgd_ischain("DNR OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.tdnr.control.cmd,
		region->parameter.tdnr.control.bypass,
		region->parameter.tdnr.otf_input.cmd,
		region->parameter.tdnr.otf_input.width,
		region->parameter.tdnr.otf_input.height,
		region->parameter.tdnr.otf_input.format
		);
	mdbgd_ischain("DNR OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.tdnr.control.cmd,
		region->parameter.tdnr.control.bypass,
		region->parameter.tdnr.otf_output.cmd,
		region->parameter.tdnr.otf_output.width,
		region->parameter.tdnr.otf_output.height,
		region->parameter.tdnr.otf_output.format
		);
	mdbgd_ischain("SCP OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.scalerp.control.cmd,
		region->parameter.scalerp.control.bypass,
		region->parameter.scalerp.otf_input.cmd,
		region->parameter.scalerp.otf_input.width,
		region->parameter.scalerp.otf_input.height,
		region->parameter.scalerp.otf_input.format
		);
	mdbgd_ischain("SCC DO : %2d    %4d  %3d   %04dx%04d %4d,%d\n", device,
		region->parameter.scalerp.control.cmd,
		region->parameter.scalerp.control.bypass,
		region->parameter.scalerp.dma_output.cmd,
		region->parameter.scalerp.dma_output.width,
		region->parameter.scalerp.dma_output.height,
		region->parameter.scalerp.dma_output.format,
		region->parameter.scalerp.dma_output.plane
		);
	mdbgd_ischain("SCP OO : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.scalerp.control.cmd,
		region->parameter.scalerp.control.bypass,
		region->parameter.scalerp.otf_output.cmd,
		region->parameter.scalerp.otf_output.width,
		region->parameter.scalerp.otf_output.height,
		region->parameter.scalerp.otf_output.format
		);
	mdbgd_ischain("FD  OI : %2d    %4d  %3d   %04dx%04d    %3d\n", device,
		region->parameter.fd.control.cmd,
		region->parameter.fd.control.bypass,
		region->parameter.fd.otf_input.cmd,
		region->parameter.fd.otf_input.width,
		region->parameter.fd.otf_input.height,
		region->parameter.fd.otf_input.format
		);
	mdbgd_ischain(" NAME   CMD    IN_SZIE   OT_SIZE      CROP       POS\n", device);
	mdbgd_ischain("SCC CI :  %d  %04dx%04d %04dx%04d %04dx%04d %04dx%04d\n", device,
		region->parameter.scalerc.input_crop.cmd,
		region->parameter.scalerc.input_crop.in_width,
		region->parameter.scalerc.input_crop.in_height,
		region->parameter.scalerc.input_crop.out_width,
		region->parameter.scalerc.input_crop.out_height,
		region->parameter.scalerc.input_crop.crop_width,
		region->parameter.scalerc.input_crop.crop_height,
		region->parameter.scalerc.input_crop.pos_x,
		region->parameter.scalerc.input_crop.pos_y
		);
	mdbgd_ischain("SCC CO :  %d  %04dx%04d %04dx%04d %04dx%04d %04dx%04d\n", device,
		region->parameter.scalerc.output_crop.cmd,
		navailable,
		navailable,
		navailable,
		navailable,
		region->parameter.scalerc.output_crop.crop_width,
		region->parameter.scalerc.output_crop.crop_height,
		region->parameter.scalerc.output_crop.pos_x,
		region->parameter.scalerc.output_crop.pos_y
		);
	mdbgd_ischain("SCP CI :  %d  %04dx%04d %04dx%04d %04dx%04d %04dx%04d\n", device,
		region->parameter.scalerp.input_crop.cmd,
		region->parameter.scalerp.input_crop.in_width,
		region->parameter.scalerp.input_crop.in_height,
		region->parameter.scalerp.input_crop.out_width,
		region->parameter.scalerp.input_crop.out_height,
		region->parameter.scalerp.input_crop.crop_width,
		region->parameter.scalerp.input_crop.crop_height,
		region->parameter.scalerp.input_crop.pos_x,
		region->parameter.scalerp.input_crop.pos_y
		);
	mdbgd_ischain("SCP CO :  %d  %04dx%04d %04dx%04d %04dx%04d %04dx%04d\n", device,
		region->parameter.scalerp.output_crop.cmd,
		navailable,
		navailable,
		navailable,
		navailable,
		region->parameter.scalerp.output_crop.crop_width,
		region->parameter.scalerp.output_crop.crop_height,
		region->parameter.scalerp.output_crop.pos_x,
		region->parameter.scalerp.output_crop.pos_y
		);

	setfile = (device->setfile & FIMC_IS_SETFILE_MASK);
	group |= GROUP_ID(device->group_3ax.id);
	group |= GROUP_ID(device->group_isp.id);

	ret = fimc_is_hw_a_param(device->interface,
		device->instance,
		group,
		setfile);

	return ret;
}

static int fimc_is_itf_enum(struct fimc_is_device_ischain *this)
{
	int ret = 0;

	dbg_ischain("%s()\n", __func__);

	ret = fimc_is_hw_enum(this->interface);
	if (ret)
		err("fimc_is_itf_enum is fail(%d)", ret);

	return ret;
}

static int fimc_is_itf_open(struct fimc_is_device_ischain *device,
	u32 module, u32 info)
{
	int ret = 0;
	int instance;
	u32 flag, vindex, group, origin;
	struct fimc_is_device_ischain *temp;
	struct fimc_is_groupmgr *groupmgr;
	struct is_region *region = device->is_region;
	struct fimc_is_interface *itf;

	groupmgr = device->groupmgr;
	flag = (module & REPROCESSING_MASK) >> REPROCESSING_SHIFT;
	vindex = (module & BPP_VINDEX_MASK) >> BPP_VINDEX_SHIFT;
	module = module & MODULE_MASK;
	device->module = module;
	origin = device->module;
	itf = device->interface;

	if (flag) {
		instance = device->instance - 1;
		while (instance >= 0) {
			temp = groupmgr->group[instance][GROUP_ID_ISP]->device;
#ifdef DEBUG
			pr_info("i%d(%08X != %08X)\n", instance, temp->module, origin);
#endif
			if (temp->module  == origin)
				break;
			instance--;
		}

		if (instance < 0) {
			merr("original instance can NOT be found", device);
			ret = -EINVAL;
			goto p_err;
		}

		flag = REPROCESSING_FLAG | instance;
		set_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state);
	}

	if (vindex == FIMC_IS_VIDEO_3A0_NUM) {
		group = GROUP_ID_3A0;
		pr_info("[ISP:V:%d] <-> [3A0:V:X]\n", device->instance);
	} else if (vindex == FIMC_IS_VIDEO_3A1_NUM) {
		group = GROUP_ID_3A1;
		pr_info("[ISP:V:%d] <-> [3A1:V:X]\n", device->instance);
		set_bit(FIMC_IS_ISCHAIN_OTF_OPEN, &device->state);
	} else {
		merr("video index is invalid(%d)", device, vindex);
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_ischain_region_flush(device);

	if (test_bit(IS_IF_STATE_SENSOR_CLOSED, &itf->state)) {
		merr("sensor close step already, cannot open sensor anymore", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_hw_open(device->interface, device->instance, module, info,
		group, flag, &device->margin_width, &device->margin_height);
	if (ret) {
		merr("fimc_is_hw_open is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(IS_IF_STATE_SENSOR_OPENED, &itf->state);

	/*hack*/
	device->margin_left = 8;
	device->margin_right = 8;
	device->margin_top = 6;
	device->margin_bottom = 4;
	device->margin_width = device->margin_left + device->margin_right;
	device->margin_height = device->margin_top + device->margin_bottom;
	mdbgd_ischain("margin %dx%d\n", device,
		device->margin_width, device->margin_height);

	fimc_is_ischain_region_invalid(device);

	if (region->shared[MAX_SHARED_COUNT-1] != MAGIC_NUMBER) {
		err("MAGIC NUMBER error\n");
		ret = -EINVAL;
		goto p_err;
	}

	memset(&region->parameter, 0x0, sizeof(struct is_param_region));

	memcpy(&region->parameter.sensor, &init_sensor_param,
		sizeof(struct sensor_param));
	memcpy(&region->parameter.isp, &init_isp_param,
		sizeof(struct isp_param));
	memcpy(&region->parameter.drc, &init_drc_param,
		sizeof(struct drc_param));
	memcpy(&region->parameter.scalerc, &init_scalerc_param,
		sizeof(struct scalerc_param));
	memcpy(&region->parameter.odc, &init_odc_param,
		sizeof(struct odc_param));
	memcpy(&region->parameter.dis, &init_dis_param,
		sizeof(struct dis_param));
	memcpy(&region->parameter.tdnr, &init_tdnr_param,
		sizeof(struct tdnr_param));
	memcpy(&region->parameter.scalerp, &init_scalerp_param,
		sizeof(struct scalerp_param));
	memcpy(&region->parameter.fd, &init_fd_param,
		sizeof(struct fd_param));

p_err:
	return ret;
}

static int fimc_is_itf_close(struct fimc_is_device_ischain *device,
	u32 module, u32 info)
{
	struct fimc_is_interface *itf;

	BUG_ON(!device);
	BUG_ON(!device->interface);

	itf = device->interface;

	/* should be use CLOSE_SENSOR */

	set_bit(IS_IF_STATE_SENSOR_CLOSED, &itf->state);

	return 0;
}

static int fimc_is_itf_setfile(struct fimc_is_device_ischain *this,
	char *setfile_name)
{
	int ret = 0;
	u32 setfile_addr = 0;
	struct fimc_is_interface *itf;

	BUG_ON(!this);
	BUG_ON(!this->interface);
	BUG_ON(!setfile_name);

	itf = this->interface;

	dbg_ischain("%s(setfile : %s)\n", __func__, setfile_name);

	ret = fimc_is_hw_saddr(itf, this->instance, &setfile_addr);
	if (ret) {
		merr("fimc_is_hw_saddr is fail(%d)", this, ret);
		goto p_err;
	}

	if (!setfile_addr) {
		merr("setfile address is NULL", this);
		pr_err("cmd : %08X\n", itf->com_regs->ihcmd);
		pr_err("instance : %08X\n", itf->com_regs->ihc_sensorid);
		pr_err("param1 : %08X\n", itf->com_regs->ihc_param1);
		pr_err("param2 : %08X\n", itf->com_regs->ihc_param2);
		pr_err("param3 : %08X\n", itf->com_regs->ihc_param3);
		pr_err("param4 : %08X\n", itf->com_regs->ihc_param4);
		goto p_err;
	}

	ret = fimc_is_ischain_loadsetf(this, setfile_addr, setfile_name);
	if (ret) {
		merr("fimc_is_ischain_loadsetf is fail(%d)", this, ret);
		goto p_err;
	}

	ret = fimc_is_hw_setfile(itf, this->instance);
	if (ret) {
		merr("fimc_is_hw_setfile is fail(%d)", this, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_itf_cfg_mem(struct fimc_is_device_ischain *this,
	u32 shot_addr, u32 shot_size)
{
	int ret = 0;

	dbg_ischain("%s()\n", __func__);

	ret = fimc_is_hw_cfg_mem(this->interface, this->instance,
		shot_addr, shot_size);

	return ret;
}

int fimc_is_itf_stream_on(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 retry = 1500;
	struct fimc_is_group *group_3ax, *group_isp;

	group_3ax = &device->group_3ax;
	group_isp = &device->group_isp;

	/* 3ax, isp group should be started */
	if (!test_bit(FIMC_IS_GROUP_READY, &group_3ax->state)) {
		merr("group 3ax is not start", device);
		goto p_err;
	}

	if (!test_bit(FIMC_IS_GROUP_READY, &group_isp->state)) {
		merr("group isp is not start", device);
		goto p_err;
	}

	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &group_3ax->state)) {
		while (--retry && (atomic_read(&group_3ax->scount) <
			group_3ax->async_shots)) {
			udelay(100);
		}
	}

	if (retry)
		pr_info("[ISC:D:%d] stream on ready\n", device->instance);
	else
		pr_info("[ISC:D:%d] stream on NOT ready\n", device->instance);

	ret = fimc_is_hw_stream_on(device->interface, device->instance);

p_err:
	return ret;
}

int fimc_is_itf_stream_off(struct fimc_is_device_ischain *device)
{
	int ret = 0;

	pr_info("[ISC:D:%d] stream off ready\n", device->instance);

	ret = fimc_is_hw_stream_off(device->interface, device->instance);

	return ret;
}

int fimc_is_itf_process_start(struct fimc_is_device_ischain *device,
	u32 group)
{
	int ret = 0;

	ret = fimc_is_hw_process_on(device->interface,
		device->instance, group);

	return ret;
}

int fimc_is_itf_process_stop(struct fimc_is_device_ischain *device,
	u32 group)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)device->interface->core;

#ifdef ENABLE_CLOCK_GATE
	/* clock on */
	fimc_is_clock_set(core, GROUP_ID_MAX, true);
#endif

	ret = fimc_is_hw_process_off(device->interface,
		device->instance, group, 0);

	return ret;
}

int fimc_is_itf_force_stop(struct fimc_is_device_ischain *device,
	u32 group)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)device->interface->core;

#ifdef ENABLE_CLOCK_GATE
	/* clock on */
	fimc_is_clock_set(core, GROUP_ID_MAX, true);
#endif

	ret = fimc_is_hw_process_off(device->interface,
		device->instance, group, 1);

	return ret;
}

static int fimc_is_itf_init_process_start(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 group = 0;

	group |= GROUP_ID(device->group_3ax.id);
	group |= GROUP_ID(device->group_isp.id);

	ret = fimc_is_hw_process_on(device->interface,
		device->instance,
		group);

	return ret;
}

static int fimc_is_itf_init_process_stop(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 group = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)device->interface->core;

#ifdef ENABLE_CLOCK_GATE
	/* clock on */
	fimc_is_clock_set(core, GROUP_ID_MAX, true);
#endif

	group |= GROUP_ID(device->group_3ax.id);
	group |= GROUP_ID(device->group_isp.id);

	ret = fimc_is_hw_process_off(device->interface,
		device->instance, group, 0);

	return ret;
}

int fimc_is_itf_i2c_lock(struct fimc_is_device_ischain *this,
			int i2c_clk, bool lock)
{
	int ret = 0;
	struct fimc_is_interface *itf = this->interface;

	if (lock)
		fimc_is_interface_lock(itf);

	ret = fimc_is_hw_i2c_lock(itf, this->instance,
				i2c_clk, lock);

	if (!lock)
		fimc_is_interface_unlock(itf);

	return ret;
}

int fimc_is_itf_g_capability(struct fimc_is_device_ischain *this)
{
	int ret = 0;
#ifdef PRINT_CAPABILITY
	u32 metadata;
	u32 index;
	struct camera2_sm *capability;
#endif

	ret = fimc_is_hw_g_capability(this->interface, this->instance,
		(this->imemory.kvaddr_shared - this->imemory.kvaddr));

	fimc_is_ischain_region_invalid(this);

#ifdef PRINT_CAPABILITY
	memcpy(&this->capability, &this->is_region->shared,
		sizeof(struct camera2_sm));
	capability = &this->capability;

	printk(KERN_INFO "===ColorC================================\n");
	printk(KERN_INFO "===ToneMapping===========================\n");
	metadata = capability->tonemap.maxCurvePoints;
	printk(KERN_INFO "maxCurvePoints : %d\n", metadata);

	printk(KERN_INFO "===Scaler================================\n");
	printk(KERN_INFO "foramt : %d, %d, %d, %d\n",
		capability->scaler.availableFormats[0],
		capability->scaler.availableFormats[1],
		capability->scaler.availableFormats[2],
		capability->scaler.availableFormats[3]);

	printk(KERN_INFO "===StatisTicsG===========================\n");
	index = 0;
	metadata = capability->stats.availableFaceDetectModes[index];
	while (metadata) {
		printk(KERN_INFO "availableFaceDetectModes : %d\n", metadata);
		index++;
		metadata = capability->stats.availableFaceDetectModes[index];
	}
	printk(KERN_INFO "maxFaceCount : %d\n",
		capability->stats.maxFaceCount);
	printk(KERN_INFO "histogrambucketCount : %d\n",
		capability->stats.histogramBucketCount);
	printk(KERN_INFO "maxHistogramCount : %d\n",
		capability->stats.maxHistogramCount);
	printk(KERN_INFO "sharpnessMapSize : %dx%d\n",
		capability->stats.sharpnessMapSize[0],
		capability->stats.sharpnessMapSize[1]);
	printk(KERN_INFO "maxSharpnessMapValue : %d\n",
		capability->stats.maxSharpnessMapValue);

	printk(KERN_INFO "===3A====================================\n");
	printk(KERN_INFO "maxRegions : %d\n", capability->aa.maxRegions);

	index = 0;
	metadata = capability->aa.aeAvailableModes[index];
	while (metadata) {
		printk(KERN_INFO "aeAvailableModes : %d\n", metadata);
		index++;
		metadata = capability->aa.aeAvailableModes[index];
	}
	printk(KERN_INFO "aeCompensationStep : %d,%d\n",
		capability->aa.aeCompensationStep.num,
		capability->aa.aeCompensationStep.den);
	printk(KERN_INFO "aeCompensationRange : %d ~ %d\n",
		capability->aa.aeCompensationRange[0],
		capability->aa.aeCompensationRange[1]);
	index = 0;
	metadata = capability->aa.aeAvailableTargetFpsRanges[index][0];
	while (metadata) {
		printk(KERN_INFO "TargetFpsRanges : %d ~ %d\n", metadata,
			capability->aa.aeAvailableTargetFpsRanges[index][1]);
		index++;
		metadata = capability->aa.aeAvailableTargetFpsRanges[index][0];
	}
	index = 0;
	metadata = capability->aa.aeAvailableAntibandingModes[index];
	while (metadata) {
		printk(KERN_INFO "aeAvailableAntibandingModes : %d\n",
			metadata);
		index++;
		metadata = capability->aa.aeAvailableAntibandingModes[index];
	}
	index = 0;
	metadata = capability->aa.awbAvailableModes[index];
	while (metadata) {
		printk(KERN_INFO "awbAvailableModes : %d\n", metadata);
		index++;
		metadata = capability->aa.awbAvailableModes[index];
	}
	index = 0;
	metadata = capability->aa.afAvailableModes[index];
	while (metadata) {
		printk(KERN_INFO "afAvailableModes : %d\n", metadata);
		index++;
		metadata = capability->aa.afAvailableModes[index];
	}
#endif
	return ret;
}

int fimc_is_itf_power_down(struct fimc_is_interface *interface)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)interface->core;

#ifdef ENABLE_CLOCK_GATE
	/* clock on */
	fimc_is_clock_set(core, GROUP_ID_MAX, true);
#endif

	ret = fimc_is_hw_power_down(interface, 0);

	return ret;
}

static int fimc_is_itf_grp_shot(struct fimc_is_device_ischain *device,
	struct fimc_is_group *group,
	struct fimc_is_frame *frame)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)device->interface->core;

	BUG_ON(!device);
	BUG_ON(!group);
	BUG_ON(!frame);
	BUG_ON(!frame->shot);

	/* Cache Flush */
	fimc_is_ischain_meta_flush(frame);

	if (frame->shot->magicNumber != SHOT_MAGIC_NUMBER) {
		merr("shot magic number error(0x%08X)\n", device, frame->shot->magicNumber);
		merr("shot_ext size : %d", device, sizeof(struct camera2_shot_ext));
		ret = -EINVAL;
		goto p_err;
	}

#ifdef DBG_STREAMING
	if (group->id == GROUP_ID_3A0)
		pr_info("[3A0:D:%d] GRP%d SHOT(%d)\n", device->instance, group->id, frame->fcount);
	else if (group->id == GROUP_ID_3A1)
		pr_info("[3A1:D:%d] GRP%d SHOT(%d)\n", device->instance, group->id, frame->fcount);
	else if (group->id == GROUP_ID_ISP)
		pr_info("[ISP:D:%d] GRP%d SHOT(%d)\n", device->instance, group->id, frame->fcount);
	else if (group->id == GROUP_ID_DIS)
		pr_info("[DIS:D:%d] GRP%d SHOT(%d)\n", device->instance, group->id, frame->fcount);
	else
		pr_info("[ERR:D:%d] GRP%d SHOT(%d)\n", device->instance, group->id, frame->fcount);
#endif

#ifdef MEASURE_TIME
#ifdef INTERNAL_TIME
	do_gettimeofday(&frame->time_shot);
#else
	do_gettimeofday(&frame->tzone[TM_SHOT]);
#endif
#endif

#ifdef ENABLE_CLOCK_GATE
	/* dynamic clock on */
	fimc_is_clock_set(core, group->id, true);
#endif

#ifdef ENABLE_DVFS
	if (device->sensor->framerate == 120) {
		core->clock.dvfs_skipcnt = DVFS_SKIP_FRAME_NUM;
		fimc_is_set_dvfs(core, device, group->id, DVFS_L0, I2C_L0);
	} else if (atomic_read(&core->video_isp.refcount) >= 3) {
		if (device->sensor_width > 2560) {
			core->clock.dvfs_skipcnt = 1000;
			fimc_is_set_dvfs(core, device, group->id, \
			DVFS_L0, I2C_L0);
		} else if (core->clock.dvfs_skipcnt > 0) {
			core->clock.dvfs_skipcnt--;
		}
        } else if (device->module == SENSOR_NAME_IMX135 &&
		test_bit(FIMC_IS_ISCHAIN_REPROCESSING, \
		&device->state)) {
                core->clock.dvfs_skipcnt = DVFS_SKIP_FRAME_NUM;
                fimc_is_set_dvfs(core, device, group->id, DVFS_L0, I2C_L0);
	} else {
		if (core->clock.dvfs_skipcnt > 0)
			core->clock.dvfs_skipcnt--;
	}

	/* update dvfs */
	if (!core->clock.dvfs_skipcnt) {
		if (!pm_qos_request_active(&device->user_qos)) {
			if ((2560 < device->chain0_width) ||
			device->sensor->framerate == 120 ||
			test_bit(FIMC_IS_ISDEV_DSTART, &device->dis.state) ||
			(device->module == SENSOR_NAME_IMX135 &&
				test_bit(FIMC_IS_ISCHAIN_REPROCESSING, \
				&device->state))) {
				fimc_is_set_dvfs(core, device, group->id, \
					DVFS_L0, I2C_L0);
			} else if (atomic_read(&core->video_isp.refcount) >= 3) {
				fimc_is_set_dvfs(core, device, group->id, \
				DVFS_L1_1, I2C_L1_1);
			} else if (device->module == SENSOR_NAME_IMX135 &&
				!test_bit(FIMC_IS_ISCHAIN_REPROCESSING, \
					&device->state)) {
				if ((device->setfile && 0xffff) == \
					ISS_SUB_SCENARIO_VIDEO)
					fimc_is_set_dvfs(core, device, group->id, \
						DVFS_L1, I2C_L1);
				else
					fimc_is_set_dvfs(core, device, group->id, \
						DVFS_L1, I2C_L1);
			} else if (device->module == SENSOR_NAME_S5K6B2) {
				if (((device->setfile && 0xffff) \
					== ISS_SUB_SCENARIO_FRONT_VT1) \
					|| ((device->setfile & 0xffff) == \
					ISS_SUB_SCENARIO_FRONT_VT2))
					fimc_is_set_dvfs(core, device, group->id, \
						DVFS_L1_3, I2C_L2);
				else
					fimc_is_set_dvfs(core, device, group->id, \
						DVFS_L1_3, I2C_L2);
			} else {
				pr_warn("%s: No DVFS senario for this case.\n", \
					__func__);
				fimc_is_set_dvfs(core, device, group->id, DVFS_L0, \
					I2C_L0);
			}
		}
	}
#endif

	ret = fimc_is_hw_shot_nblk(device->interface,
		device->instance,
		GROUP_ID(group->id),
		frame->dvaddr_buffer[0],
		frame->dvaddr_shot,
		frame->fcount,
		frame->rcount);

p_err:
	return ret;
}


int fimc_is_ischain_probe(struct fimc_is_device_ischain *device,
	struct fimc_is_interface *interface,
	struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_mem *mem,
	struct platform_device *pdev,
	u32 instance,
	u32 regs)
{
	int ret = 0;
	struct fimc_is_subdev *scc, *dis, *scp;

	BUG_ON(!interface);
	BUG_ON(!mem);
	BUG_ON(!pdev);
	BUG_ON(!device);

	/*device initialization should be just one time*/
	scc = &device->scc;
	dis = &device->dis;
	scp = &device->scp;

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_SOC_EXYNOS5250)
	device->bus_dev		= dev_get("exynos-busfreq");
#endif

	device->interface		= interface;
	device->mem		= mem;
	device->pdev		= pdev;
	device->pdata		= pdev->dev.platform_data;
	device->regs		= (void *)regs;
	device->instance	= instance;
	device->groupmgr	= groupmgr;
	device->sensor		= NULL;
	device->margin_left	= 0;
	device->margin_right	= 0;
	device->margin_width	= 0;
	device->margin_top	= 0;
	device->margin_bottom	= 0;
	device->margin_height	= 0;
	device->sensor_width	= 0;
	device->sensor_height	= 0;
	device->dis_width	= 0;
	device->dis_height	= 0;
	device->chain0_width	= 0;
	device->chain0_height	= 0;
	device->chain1_width	= 0;
	device->chain1_height	= 0;
	device->chain2_width	= 0;
	device->chain2_height	= 0;
	device->chain3_width	= 0;
	device->chain3_height	= 0;
	device->crop_x		= 0;
	device->crop_y		= 0;
	device->crop_width	= 0;
	device->crop_height	= 0;
	device->setfile		= 0;
	device->dzoom_width	= 0;
	device->force_down	= false;
	device->is_region	= NULL;

	device->group_3ax.leader.entry = ENTRY_ISP;
	device->group_isp.leader.entry = ENTRY_ISP;
	device->group_dis.leader.entry = ENTRY_DIS;
	device->drc.entry = ENTRY_DRC;
	device->scc.entry = ENTRY_SCALERC;
	device->dis.entry = ENTRY_DIS;
	device->dnr.entry = ENTRY_TDNR;
	device->scp.entry = ENTRY_SCALERP;
	device->fd.entry = ENTRY_LHFD;

	clear_bit(FIMC_IS_ISCHAIN_OPEN, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_LOADED, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_POWER_ON, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_OPEN_SENSOR, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_OTF_OPEN, &device->state);

	/* clear group open state */
	clear_bit(FIMC_IS_GROUP_OPEN, &device->group_3ax.state);
	clear_bit(FIMC_IS_GROUP_OPEN, &device->group_isp.state);
	clear_bit(FIMC_IS_GROUP_OPEN, &device->group_dis.state);

	/* clear subdevice state */
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->group_3ax.leader.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->group_isp.leader.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->group_dis.leader.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->drc.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->scc.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->dis.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->dnr.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->scp.state);
	clear_bit(FIMC_IS_ISDEV_DOPEN, &device->fd.state);

	clear_bit(FIMC_IS_ISDEV_DSTART, &device->group_3ax.leader.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->group_isp.leader.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->group_dis.leader.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->drc.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->scc.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->dis.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->dnr.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->scp.state);
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->fd.state);

	mutex_init(&device->mutex_state);
	spin_lock_init(&device->slock_state);

#ifdef FW_DEBUG
	debugfs_root = debugfs_create_dir(DEBUG_FS_ROOT_NAME, NULL);
	if (debugfs_root)
		dbg_ischain("debugfs %s is created", DEBUG_FS_ROOT_NAME);

	debugfs_file = debugfs_create_file(DEBUG_FS_FILE_NAME, S_IRUSR,
		debugfs_root, device, &debug_fops);
	if (debugfs_file)
		dbg_ischain("debugfs %s is created", DEBUG_FS_FILE_NAME);
#endif

	return ret;
}

int fimc_is_ischain_open(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx,
	struct fimc_is_minfo *minfo)
{
	int ret = 0;
	struct fimc_is_ishcain_mem *imemory;
	struct fimc_is_core *core;

	BUG_ON(!device);
	BUG_ON(!device->groupmgr);
	BUG_ON(!vctx);
	BUG_ON(!minfo);

	if (test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
		merr("already open", device);
		ret = -EMFILE;
		goto exit;
	}

#ifndef RESERVED_MEM
	if (device->instance == 0) {
		/* 1. init memory */
		ret = fimc_is_ishcain_initmem(device);
		if (ret) {
			err("fimc_is_ishcain_initmem is fail(%d)\n", ret);
			goto exit;
		}
	}
#endif

	clear_bit(FIMC_IS_ISCHAIN_LOADED, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_POWER_ON, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_OPEN_SENSOR, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_OTF_OPEN, &device->state);

	/* 2. Init variables */
	memset(&device->cur_peri_ctl, 0,
		sizeof(struct camera2_uctl));
	memset(&device->peri_ctls, 0,
		sizeof(struct camera2_uctl)*SENSOR_MAX_CTL);
	memset(&device->capability, 0,
		sizeof(struct camera2_sm));

	/* initial state, it's real apply to setting when opening*/
	device->margin_left	= 0;
	device->margin_right	= 0;
	device->margin_width	= 0;
	device->margin_top	= 0;
	device->margin_bottom	= 0;
	device->margin_height	= 0;
	device->sensor_width	= 0;
	device->sensor_height	= 0;
	device->dis_width	= 0;
	device->dis_height	= 0;
	device->chain0_width	= 0;
	device->chain0_height	= 0;
	device->chain1_width	= 0;
	device->chain1_height	= 0;
	device->chain2_width	= 0;
	device->chain2_height	= 0;
	device->chain3_width	= 0;
	device->chain3_height	= 0;
	device->crop_x		= 0;
	device->crop_y		= 0;
	device->crop_width	= 0;
	device->crop_height	= 0;
	device->setfile		= 0;
	device->dzoom_width	= 0;
	device->force_down	= false;
	device->sensor		= NULL;

	imemory			= &device->imemory;
	imemory->base		= minfo->base;
	imemory->size		= minfo->size;
	imemory->vaddr_base	= minfo->vaddr_base;
	imemory->vaddr_curr	= minfo->vaddr_curr;
	imemory->fw_cookie	= minfo->fw_cookie;
	imemory->dvaddr		= minfo->dvaddr;
	imemory->kvaddr		= minfo->kvaddr;
	imemory->dvaddr_odc	= minfo->dvaddr_odc;
	imemory->kvaddr_odc	= minfo->kvaddr_odc;
	imemory->dvaddr_dis	= minfo->dvaddr_dis;
	imemory->kvaddr_dis	= minfo->kvaddr_dis;
	imemory->dvaddr_3dnr	= minfo->dvaddr_3dnr;
	imemory->kvaddr_3dnr	= minfo->kvaddr_3dnr;
	imemory->offset_region	= (FIMC_IS_A5_MEM_SIZE -
		((device->instance + 1) * FIMC_IS_REGION_SIZE));
	imemory->dvaddr_region	= imemory->dvaddr + imemory->offset_region;
	imemory->kvaddr_region	= imemory->kvaddr + imemory->offset_region;
	imemory->is_region	= (struct is_region *)imemory->kvaddr_region;
	imemory->offset_shared	= (u32)&imemory->is_region->shared[0] -
		imemory->kvaddr;
	imemory->dvaddr_shared	= imemory->dvaddr + imemory->offset_shared;
	imemory->kvaddr_shared	= imemory->kvaddr + imemory->offset_shared;
	device->is_region = imemory->is_region;

	fimc_is_group_open(device->groupmgr, &device->group_isp, GROUP_ID_ISP,
		device->instance, vctx, device, fimc_is_ischain_isp_callback);

	/* subdev open */
	fimc_is_ischain_sub_open(&device->drc, NULL, &init_drc_param.control);
	fimc_is_ischain_sub_open(&device->dis, NULL, &init_dis_param.control);
	fimc_is_ischain_sub_open(&device->dnr, NULL, &init_tdnr_param.control);
	/* FD see only control.command not bypass */
	fimc_is_ischain_sub_open(&device->fd, NULL, NULL);

	/* for mediaserver force close */
	core = (struct fimc_is_core *)device->interface->core;
	ret = fimc_is_resource_get(core);
	if (ret) {
		merr("fimc_is_resource_get is fail", device);
		goto exit;
	}

	if (device->instance == 0) {
		/* 5. A5 power on */
		ret = fimc_is_ischain_power(device, 1);
		if (ret) {
			err("failed to fimc_is_ischain_power (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}

		dbg_ischain("power up and loaded firmware\n");
	}

	set_bit(FIMC_IS_ISCHAIN_OPEN, &device->state);

exit:
	pr_info("[ISC:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}

int fimc_is_ischain_close(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	int refcount;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_subdev *leader;
	struct fimc_is_queue *queue;
	struct fimc_is_core *core;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_isp;
	leader = &group->leader;
	queue = GET_SRC_QUEUE(vctx);
	refcount = atomic_read(&vctx->video->refcount);
	if (refcount < 0) {
		merr("invalid ischain refcount", device);
		ret = -ENODEV;
		goto exit;
	}

	if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
		merr("already close", device);
		ret = -EMFILE;
		goto exit;
	}

	/* 1. Stop all request */
	ret = fimc_is_ischain_isp_stop(device, leader, queue);
	if (ret)
		merr("fimc_is_ischain_isp_stop is fail", device);

	/* group close */
	ret = fimc_is_group_close(groupmgr, group, vctx);
	if (ret)
		merr("fimc_is_group_close is fail", device);

	/* subdev close */
	fimc_is_ischain_sub_close(&device->drc);
	fimc_is_ischain_sub_close(&device->dis);
	fimc_is_ischain_sub_close(&device->dnr);
	fimc_is_ischain_sub_close(&device->fd);

	/* it's not real CLOSE_SENSOR, also arguments are invalid */
	ret = fimc_is_itf_close(device, 0, 0);
	if (ret)
		merr("fimc_is_itf_close is fail", device);

	/* for mediaserver force close */
	core = (struct fimc_is_core *)device->interface->core;
	ret = fimc_is_resource_put(core);
	if (ret) {
		merr("fimc_is_resource_put is fail", device);
		goto exit;
	}

	clear_bit(FIMC_IS_ISCHAIN_OPEN_SENSOR, &device->state);
	clear_bit(FIMC_IS_ISCHAIN_OPEN, &device->state);

exit:
	pm_relax(&device->pdev->dev);
	pr_info("[ISC:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}

int fimc_is_ischain_init(struct fimc_is_device_ischain *device,
	u32 module, u32 channel, struct sensor_open_extended *ext,
	char *setfile_name)
{
	int ret = 0;

	BUG_ON(!device);
	BUG_ON(!device->sensor);
	BUG_ON(!ext);

	dbg_ischain("%s(module : %d, channel : %d, Af : %d)\n",
		__func__, module, channel, ext->actuator_con.product_name);

	if (test_bit(FIMC_IS_ISCHAIN_OPEN_SENSOR, &device->state)) {
		mwarn("sensor is already open", device);
		goto p_err;
	}

	if (!test_bit(FIMC_IS_SENSOR_OPEN, &device->sensor->state)) {
		merr("I2C gpio is not yet set", device);
		ret = -EINVAL;
		goto p_err;
	}

	if ((device->instance) == 0) {
		ret = fimc_is_itf_enum(device);
		if (ret) {
			err("enum fail");
			goto p_err;
		}
	}

	memcpy(&device->is_region->shared[0], ext,
		sizeof(struct sensor_open_extended));

	ret = fimc_is_itf_open(device, module, device->imemory.dvaddr_shared);
	if (ret) {
		merr("open fail", device);
		goto p_err;
	}

	ret = fimc_is_itf_setfile(device, setfile_name);
	if (ret) {
		merr("setfile fail", device);
		goto p_err;
	}

	if (!test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state)) {
		ret = fimc_is_itf_stream_off(device);
		if (ret) {
			merr("streamoff fail", device);
			goto p_err;
		}
	}

	ret = fimc_is_itf_init_process_stop(device);
	if (ret) {
		merr("processoff fail", device);
		goto p_err;
	}

	set_bit(FIMC_IS_ISCHAIN_OPEN_SENSOR, &device->state);

p_err:
	return ret;
}

static int fimc_is_ischain_s_setfile(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 group_id = 0;
	struct fimc_is_group *group_isp;
	struct isp_param *isp_param;
	u32 indexes, lindex, hindex, range;

	BUG_ON(!device);

	isp_param = &device->is_region->parameter.isp;
	group_isp = &device->group_isp;
	indexes = lindex = hindex = 0;

	if (group_isp->smp_shot.count < 1) {
		merr("group%d is working(%d), setfile change is fail",
			device, group_isp->id, group_isp->smp_shot.count);
		goto p_err;
	}

	pr_info("[ISC:D:%d] setfile is %08X\n", device->instance,
		device->setfile);

	group_id |= GROUP_ID(device->group_3ax.id);
	group_id |= GROUP_ID(device->group_isp.id);
	if (test_bit(FIMC_IS_GROUP_ACTIVE, &device->group_dis.state))
		group_id |= GROUP_ID(device->group_dis.id);

	if ((device->setfile & FIMC_IS_SETFILE_MASK) >= ISS_SUB_END) {
		merr("setfile id(%08X) is invalid", device, device->setfile);
		goto p_err;
	}

	ret = fimc_is_itf_process_stop(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_stop fail", device);
		goto p_err;
	}

	/*
	 * range
	 * 0 : wide range
	 * 1 : narrow range
	 */
	range = device->setfile >> 16;
	if (range)
		isp_param->otf_output.format = OTF_OUTPUT_FORMAT_YUV444_TRUNCATED;
	else
		isp_param->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	lindex |= LOWBIT_OF(PARAM_ISP_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_OTF_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_a_param(device, group_id);
	if (ret) {
		merr("fimc_is_itf_a_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_process_start(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_start fail", device);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_ischain_s_chain0_size(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct isp_param *isp_param;
	struct drc_param *drc_param;
	struct scalerc_param *scc_param;

	u32 chain0_width, chain0_height;
	u32 indexes, lindex, hindex;

	isp_param = &device->is_region->parameter.isp;
	drc_param = &device->is_region->parameter.drc;
	scc_param = &device->is_region->parameter.scalerc;
	indexes = lindex = hindex = 0;
	chain0_width = width;
	chain0_height = height;

	dbg_ischain("request chain0 size : %dx%d\n",
		chain0_width, chain0_height);

	isp_param->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
	isp_param->otf_output.width = chain0_width;
	isp_param->otf_output.height = chain0_height;
	isp_param->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	isp_param->otf_output.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT;
	isp_param->otf_output.order = OTF_INPUT_ORDER_BAYER_GR_BG;
	lindex |= LOWBIT_OF(PARAM_ISP_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_OTF_OUTPUT);
	indexes++;

	isp_param->dma1_output.cmd = DMA_OUTPUT_COMMAND_DISABLE;
	lindex |= LOWBIT_OF(PARAM_ISP_DMA1_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_OUTPUT);
	indexes++;

	isp_param->dma2_output.cmd = DMA_OUTPUT_COMMAND_ENABLE;
	isp_param->dma2_output.width = chain0_width;
	isp_param->dma2_output.height = chain0_height;
	isp_param->dma2_output.format = DMA_OUTPUT_FORMAT_BAYER;
	isp_param->dma2_output.bitwidth = DMA_OUTPUT_BIT_WIDTH_12BIT;
	isp_param->dma2_output.buffer_number = 0;
	lindex |= LOWBIT_OF(PARAM_ISP_DMA2_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_DMA2_OUTPUT);
	indexes++;

	/* DRC */
	drc_param->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	drc_param->otf_input.width = chain0_width;
	drc_param->otf_input.height = chain0_height;
	lindex |= LOWBIT_OF(PARAM_DRC_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_DRC_OTF_INPUT);
	indexes++;

	drc_param->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
	drc_param->otf_output.width = chain0_width;
	drc_param->otf_output.height = chain0_height;
	lindex |= LOWBIT_OF(PARAM_DRC_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_DRC_OTF_OUTPUT);
	indexes++;

	/* SCC */
	scc_param->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	scc_param->otf_input.width = chain0_width;
	scc_param->otf_input.height = chain0_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_OTF_INPUT);
	indexes++;

	device->lindex |= lindex;
	device->hindex |= hindex;
	device->indexes += indexes;

	pr_info("[ISC:D:%d] chain0 size(%d x %d)",
		device->instance, chain0_width, chain0_height);

	return ret;
}

static int fimc_is_ischain_s_chain1_size(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct scalerc_param *scc_param;
	struct odc_param *odc_param;
	struct dis_param *dis_param;
	u32 chain0_width, chain0_height;
	u32 chain1_width, chain1_height;
	u32 indexes, lindex, hindex;

	scc_param = &device->is_region->parameter.scalerc;
	odc_param = &device->is_region->parameter.odc;
	dis_param = &device->is_region->parameter.dis;
	indexes = lindex = hindex = 0;
	chain0_width = device->chain0_width;
	chain0_height = device->chain0_height;
	chain1_width = width;
	chain1_height = height;

	dbg_ischain("current chain0 size : %dx%d\n",
		chain0_width, chain0_height);
	dbg_ischain("current chain1 size : %dx%d\n",
		device->chain1_width, device->chain1_height);
	dbg_ischain("request chain1 size : %dx%d\n",
		chain1_width, chain1_height);

	if (!chain0_width) {
		err("chain0 width is zero");
		ret = -EINVAL;
		goto exit;
	}

	if (!chain0_height) {
		err("chain0 height is zero");
		ret = -EINVAL;
		goto exit;
	}

	if (!chain1_width) {
		err("chain1 width is zero");
		ret = -EINVAL;
		goto exit;
	}

	if (!chain1_height) {
		err("chain1 height is zero");
		ret = -EINVAL;
		goto exit;
	}

	/* SCC OUTPUT */
	scc_param->input_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
	scc_param->input_crop.pos_x = 0;
	scc_param->input_crop.pos_y = 0;
	scc_param->input_crop.crop_width = chain0_width;
	scc_param->input_crop.crop_height = chain0_height;
	scc_param->input_crop.in_width = chain0_width;
	scc_param->input_crop.in_height = chain0_height;
	scc_param->input_crop.out_width = chain1_width;
	scc_param->input_crop.out_height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_INPUT_CROP);
	indexes++;

	scc_param->output_crop.cmd = SCALER_CROP_COMMAND_DISABLE;
	scc_param->output_crop.pos_x = 0;
	scc_param->output_crop.pos_y = 0;
	scc_param->output_crop.crop_width = chain1_width;
	scc_param->output_crop.crop_height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_OUTPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_OUTPUT_CROP);
	indexes++;

	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state))
		scc_param->otf_output.cmd = OTF_OUTPUT_COMMAND_DISABLE;
	else
		scc_param->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;

	scc_param->otf_output.width = chain1_width;
	scc_param->otf_output.height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_OTF_OUTPUT);
	indexes++;

	scc_param->dma_output.width = chain0_width;
	scc_param->dma_output.height = chain0_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	indexes++;

	/* ODC */
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state))
		odc_param->control.cmd = CONTROL_COMMAND_STOP;
	else
		odc_param->control.cmd = CONTROL_COMMAND_START;

	lindex |= LOWBIT_OF(PARAM_ODC_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_ODC_CONTROL);
	indexes++;

	odc_param->otf_input.width = chain1_width;
	odc_param->otf_input.height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_ODC_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_ODC_OTF_INPUT);
	indexes++;

	odc_param->otf_output.width = chain1_width;
	odc_param->otf_output.height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_ODC_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_ODC_OTF_OUTPUT);
	indexes++;

	/* DIS INPUT */
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->dis.state);
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state))
		dis_param->control.cmd = CONTROL_COMMAND_STOP;
	else
		dis_param->control.cmd = CONTROL_COMMAND_START;

	dis_param->control.bypass = CONTROL_BYPASS_ENABLE;
	lindex |= LOWBIT_OF(PARAM_DIS_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_DIS_CONTROL);
	indexes++;

	dis_param->otf_input.width = chain1_width;
	dis_param->otf_input.height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_DIS_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_DIS_OTF_INPUT);
	indexes++;

	device->lindex |= lindex;
	device->hindex |= hindex;
	device->indexes += indexes;

	pr_info("[ISC:D:%d] chain1 size(%d x %d)",
		device->instance, chain1_width, chain1_height);

exit:
	return ret;
}

static int fimc_is_ischain_s_chain2_size(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct dis_param *dis_param;
	struct tdnr_param *tdnr_param;
	struct scalerp_param *scp_param;
	u32 chain2_width, chain2_height;
	u32 indexes, lindex, hindex;

	dbg_ischain("request chain2 size : %dx%d\n", width, height);
	dbg_ischain("current chain2 size : %dx%d\n",
		device->chain2_width, device->chain2_height);

	dis_param = &device->is_region->parameter.dis;
	tdnr_param = &device->is_region->parameter.tdnr;
	scp_param = &device->is_region->parameter.scalerp;
	indexes = lindex = hindex = 0;

	/* CALCULATION */
	chain2_width = width;
	chain2_height = height;

	/* DIS OUTPUT */
	dis_param->otf_output.width = chain2_width;
	dis_param->otf_output.height = chain2_height;
	lindex |= LOWBIT_OF(PARAM_DIS_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_DIS_OTF_OUTPUT);
	indexes++;

	/* 3DNR */
	clear_bit(FIMC_IS_ISDEV_DSTART, &device->dnr.state);
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state))
		tdnr_param->control.cmd = CONTROL_COMMAND_STOP;
	else
		tdnr_param->control.cmd = CONTROL_COMMAND_START;

	tdnr_param->control.bypass = CONTROL_BYPASS_ENABLE;
	lindex |= LOWBIT_OF(PARAM_TDNR_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_TDNR_CONTROL);
	indexes++;

	tdnr_param->otf_input.width = chain2_width;
	tdnr_param->otf_input.height = chain2_height;
	lindex |= LOWBIT_OF(PARAM_TDNR_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_TDNR_OTF_INPUT);
	indexes++;

	tdnr_param->dma_output.width = chain2_width;
	tdnr_param->dma_output.height = chain2_height;
	lindex |= LOWBIT_OF(PARAM_TDNR_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_TDNR_DMA_OUTPUT);
	indexes++;

	tdnr_param->otf_output.width = chain2_width;
	tdnr_param->otf_output.height = chain2_height;
	lindex |= LOWBIT_OF(PARAM_TDNR_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_TDNR_OTF_OUTPUT);
	indexes++;

	/* SCALERP INPUT */
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state))
		scp_param->control.cmd = CONTROL_COMMAND_STOP;
	else
		scp_param->control.cmd = CONTROL_COMMAND_START;

	lindex |= LOWBIT_OF(PARAM_SCALERP_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_CONTROL);
	indexes++;

	scp_param->otf_input.width = chain2_width;
	scp_param->otf_input.height = chain2_height;
	lindex |= LOWBIT_OF(PARAM_SCALERP_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_OTF_INPUT);
	indexes++;

	device->lindex |= lindex;
	device->hindex |= hindex;
	device->indexes += indexes;

	pr_info("[ISC:D:%d] chain2 size(%d x %d)",
		device->instance, chain2_width, chain2_height);

	return ret;
}

static int fimc_is_ischain_s_chain3_size(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct scalerp_param *scp_param;
	struct fd_param *fd_param;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_queue *queue;
	u32 chain2_width, chain2_height;
	u32 chain3_width, chain3_height;
	u32 scp_crop_width, scp_crop_height;
	u32 scp_crop_x, scp_crop_y;
	u32 indexes, lindex, hindex;

	scp_param = &device->is_region->parameter.scalerp;
	fd_param = &device->is_region->parameter.fd;
	vctx = device->scp.vctx;
	queue = &vctx->q_dst;
	indexes = lindex = hindex = 0;

	chain2_width = device->chain2_width;
	chain2_height = device->chain2_height;
	chain3_width = width;
	chain3_height = height;

	scp_crop_x = 0;
	scp_crop_y = 0;
	scp_crop_width = chain2_width;
	scp_crop_height = chain2_height;

	dbg_ischain("request chain3 size : %dx%d\n", width, height);
	dbg_ischain("current chain3 size : %dx%d\n",
		device->chain3_width, device->chain3_height);

	/*SCALERP*/
	scp_param->input_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
	scp_param->input_crop.pos_x = scp_crop_x;
	scp_param->input_crop.pos_y = scp_crop_y;
	scp_param->input_crop.crop_width = scp_crop_width;
	scp_param->input_crop.crop_height = scp_crop_height;
	scp_param->input_crop.in_width = chain2_width;
	scp_param->input_crop.in_height = chain2_height;
	scp_param->input_crop.out_width = chain3_width;
	scp_param->input_crop.out_height = chain3_height;
	lindex |= LOWBIT_OF(PARAM_SCALERP_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_INPUT_CROP);
	indexes++;

	/* sclaer can't apply stride to each plane, only y plane.
	cb, cr plane should be half of y plane, it's automatically set
	3 plane : all plane can be 32 stride or 16, 8
	2 plane : y plane only can be 32, 16 stride, other should be half of y
	1 plane : all plane can be 8 plane */
	if (queue->framecfg.width_stride[0]) {
		scp_param->output_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
		scp_param->output_crop.pos_x = 0;
		scp_param->output_crop.pos_y = 0;
		scp_param->output_crop.crop_width = chain3_width +
			queue->framecfg.width_stride[0];
		scp_param->output_crop.crop_height = chain3_height;
		lindex |= LOWBIT_OF(PARAM_SCALERP_OUTPUT_CROP);
		hindex |= HIGHBIT_OF(PARAM_SCALERP_OUTPUT_CROP);
		indexes++;
	} else {
		scp_param->output_crop.cmd = SCALER_CROP_COMMAND_DISABLE;
		lindex |= LOWBIT_OF(PARAM_SCALERP_OUTPUT_CROP);
		hindex |= HIGHBIT_OF(PARAM_SCALERP_OUTPUT_CROP);
		indexes++;
	}

	scp_param->otf_output.width = chain3_width;
	scp_param->otf_output.height = chain3_height;
	lindex |= LOWBIT_OF(PARAM_SCALERP_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_OTF_OUTPUT);
	indexes++;

	scp_param->dma_output.width = chain3_width;
	scp_param->dma_output.height = chain3_height;
	lindex |= LOWBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	indexes++;

	/* FD */
	fd_param->otf_input.width = chain3_width;
	fd_param->otf_input.height = chain3_height;
	lindex |= LOWBIT_OF(PARAM_FD_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_FD_OTF_INPUT);
	indexes++;

	device->lindex |= lindex;
	device->hindex |= hindex;
	device->indexes += indexes;

	pr_info("[ISC:D:%d] chain3 size(%d x %d)",
		device->instance, chain3_width, chain3_height);

	return ret;
}

#ifdef SCALER_CROP_DZOOM
static int fimc_is_ischain_s_dzoom(struct fimc_is_device_ischain *this,
	u32 crop_x, u32 crop_y, u32 crop_width)
{
	int ret = 0;
	u32 indexes, lindex, hindex;
	u32 chain0_width, chain0_height;
	u32 temp_width, temp_height, input_width;
	u32 zoom_input, zoom_target;
	u32 crop_cx, crop_cy, crop_cwidth, crop_cheight;
	struct scalerc_param *scc_param;
	u32 chain0_ratio, preview_ratio;
	u32 chain0_ratio_width, chain0_ratio_height;
#ifdef USE_ADVANCED_DZOOM
	u32 zoom_pre, zoom_post, zoom_pre_max;
	u32 crop_px, crop_py, crop_pwidth, crop_pheight;
	u32 chain1_width, chain1_height;
	u32 chain2_width, chain2_height;
	u32 chain3_width, chain3_height;
	struct scalerp_param *scp_param;

	scc_param = &this->is_region->parameter.scalerc;
	scp_param = &this->is_region->parameter.scalerp;
	indexes = lindex = hindex = 0;
	chain0_width = this->chain0_width;
	chain0_height = this->chain0_height;
	chain1_width = this->chain1_width;
	chain1_height = this->chain1_height;
	chain2_width = this->chain2_width;
	chain2_height = this->chain2_height;
	chain3_width = this->chain3_width;
	chain3_height = this->chain3_height;
#ifdef PRINT_DZOOM
	printk(KERN_INFO "chain0(%d, %d), chain1(%d, %d), chain2(%d, %d)\n",
		chain0_width, chain0_height,
		chain1_width, chain1_height,
		chain2_width, chain2_height);
#endif
#else
	scc_param = &this->is_region->parameter.scalerc;
	indexes = lindex = hindex = 0;
	chain0_width = this->chain0_width;
	chain0_height = this->chain0_height;
#ifdef PRINT_DZOOM
	printk(KERN_INFO "chain0(%d, %d)\n", chain0_width, chain0_height);
#endif
#endif

	/* CHECK */
	input_width = crop_width;
	temp_width = crop_width + (crop_x<<1);
	if (temp_width != chain0_width) {
		err("input width is not valid(%d != %d)",
			temp_width, chain0_width);
		/* if invalid input come, dzoom is not apply and
		shot command is sent to firmware */
		ret = 0;
		goto exit;
	}

	chain0_ratio_width = chain0_width;
	chain0_ratio_height = chain0_height;

	/* ISP dma input crop is not supported in exynos5410 */
	if (soc_is_exynos5410()) {
		chain0_ratio = chain0_width * 1000 / chain0_height;
		preview_ratio = this->chain3_width * 1000 / this->chain3_height;

		if (chain0_ratio < preview_ratio) {
			/* ex: sensor(4:3) --> preview(16:9) */
			chain0_ratio_height =
				(chain0_ratio_width * this->chain3_height) / this->chain3_width;
			chain0_ratio_height = ALIGN(chain0_ratio_height, 2);
		} else if (chain0_ratio > preview_ratio) {
			/* ex: sensor(4:3) --> preview(11:9) */
			chain0_ratio_width =
				(chain0_ratio_height * this->chain3_width) / this->chain3_height;
			chain0_ratio_width = ALIGN(chain0_ratio_width, 4);
		}
	}

#ifdef USE_ADVANCED_DZOOM
	zoom_input = (chain0_ratio_width * 1000) / crop_width;
	zoom_pre_max = (chain0_ratio_width * 1000) / chain1_width;

	if (zoom_pre_max < 1000)
		zoom_pre_max = 1000;

#ifdef PRINT_DZOOM
	printk(KERN_INFO "zoom input : %d, premax-zoom : %d\n",
		zoom_input, zoom_pre_max);
#endif

	if (test_bit(FIMC_IS_ISDEV_DSTART, &this->dis.state))
		zoom_target = (zoom_input * 91 + 34000) / 125;
	else
		zoom_target = zoom_input;

	if (zoom_target > zoom_pre_max) {
		zoom_pre = zoom_pre_max;
		zoom_post = (zoom_target * 1000) / zoom_pre;
	} else {
		zoom_pre = zoom_target;
		zoom_post = 1000;
	}

	/* CALCULATION */
	temp_width = (chain0_ratio_width * 1000) / zoom_pre;
	temp_height = (chain0_ratio_height * 1000) / zoom_pre;
	crop_cx = (chain0_width - temp_width)>>1;
	crop_cy = (chain0_height - temp_height)>>1;
	crop_cwidth = chain0_width - (crop_cx<<1);
	crop_cheight = chain0_height - (crop_cy<<1);

	scc_param->input_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
	scc_param->input_crop.pos_x = crop_cx;
	scc_param->input_crop.pos_y = crop_cy;
	scc_param->input_crop.crop_width = crop_cwidth;
	scc_param->input_crop.crop_height = crop_cheight;
	scc_param->input_crop.in_width = chain0_width;
	scc_param->input_crop.in_height = chain0_height;
	scc_param->input_crop.out_width = chain1_width;
	scc_param->input_crop.out_height = chain1_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_INPUT_CROP);
	indexes++;

#ifdef PRINT_DZOOM
	printk(KERN_INFO "pre-zoom target : %d(%d, %d, %d %d)\n",
		zoom_pre, crop_cx, crop_cy, crop_cwidth, crop_cheight);
#endif

	temp_width = (chain2_width * 1000) / zoom_post;
	temp_height = (chain2_height * 1000) / zoom_post;
	crop_px = (chain2_width - temp_width)>>1;
	crop_py = (chain2_height - temp_height)>>1;
	crop_pwidth = chain2_width - (crop_px<<1);
	crop_pheight = chain2_height - (crop_py<<1);

	scp_param->input_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
	scp_param->input_crop.pos_x = crop_px;
	scp_param->input_crop.pos_y = crop_py;
	scp_param->input_crop.crop_width = crop_pwidth;
	scp_param->input_crop.crop_height = crop_pheight;
	scp_param->input_crop.in_width = chain2_width;
	scp_param->input_crop.in_height = chain2_height;
	scp_param->input_crop.out_width = chain3_width;
	scp_param->input_crop.out_height = chain3_height;
	lindex |= LOWBIT_OF(PARAM_SCALERP_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_INPUT_CROP);
	indexes++;

#ifdef PRINT_DZOOM
	printk(KERN_INFO "post-zoom target : %d(%d, %d, %d %d)\n",
		zoom_post, crop_px, crop_py, crop_pwidth, crop_pheight);
#endif
#else
	zoom_input = (chain0_ratio_width * 1000) / crop_width;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &this->dis.state))
		zoom_target = (zoom_input * 91 + 34000) / 125;
	else
		zoom_target = zoom_input;

	temp_width = (chain0_ratio_width * 1000) / zoom_target;
	temp_height = (chain0_ratio_height * 1000) / zoom_target;
	crop_cx = (chain0_width - temp_width)>>1;
	crop_cy = (chain0_height - temp_height)>>1;
	crop_cwidth = chain0_width - (crop_cx<<1);
	crop_cheight = chain0_height - (crop_cy<<1);

	scc_param->input_crop.cmd = SCALER_CROP_COMMAND_ENABLE;
	scc_param->input_crop.pos_x = crop_cx;
	scc_param->input_crop.pos_y = crop_cy;
	scc_param->input_crop.crop_width = crop_cwidth;
	scc_param->input_crop.crop_height = crop_cheight;
	lindex |= LOWBIT_OF(PARAM_SCALERC_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_INPUT_CROP);
	indexes++;

#ifdef PRINT_DZOOM
	printk(KERN_INFO "zoom input : %d, zoom target : %d(%d, %d, %d %d)\n",
		zoom_input, zoom_target,
		crop_cx, crop_cy, crop_cwidth, crop_cheight);
#endif
#endif

	ret = fimc_is_itf_s_param(this, indexes, lindex, hindex);
	if (ret) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto exit;
	}

	this->crop_x = crop_cx;
	this->crop_y = crop_cy;
	this->crop_width = crop_cwidth;
	this->crop_height = crop_cheight;
	this->dzoom_width = input_width;

exit:
	return ret;
}
#endif

#ifdef BAYER_CROP_DZOOM
static int fimc_is_ischain_s_3ax_size(struct fimc_is_device_ischain *device,
	u32 crop_x, u32 crop_y,
	u32 crop_width, u32 crop_height,
	u32 bds_width, u32 bds_height)
{
	int ret = 0;
	struct isp_param *isp_param;
	u32 min_width, max_width, min_height, max_height;

	u32 sensor_width, sensor_height;
	u32 indexes, lindex, hindex;

	isp_param = &device->is_region->parameter.isp;
	indexes = lindex = hindex = 0;
	sensor_width = device->sensor->width - device->margin_width;
	sensor_height = device->sensor->height - device->margin_height;
	if ((crop_width < bds_width) ||
		(crop_height < bds_height)) {
		bds_width = crop_width;
		bds_height = crop_height;
	}

#ifdef PRINT_DZOOM
	pr_info("[ISP:D:%d] request 3ax input size(%dx%d),\
		Bcrop size(%dx%d), BDS size(%dx%d), zoom(%d)\n",
		device->instance, sensor_width, sensor_height,
		crop_width, crop_height, bds_width, bds_height,
		sensor_width * 1000 / crop_width);
#endif

	/* Check length for center crop */
	min_width = crop_width + crop_x;
	max_width = crop_width + (crop_x << 1);
	min_height = crop_height + crop_y;
	max_height = crop_height + (crop_y << 1);
	if ((min_width > sensor_width) || (max_width < sensor_width)
	|| (min_height > sensor_height) || (max_height < sensor_height)) {
		mwarn("Crop width or height is not valid.\
			Crop region(%d, %d, %d, %d) Input region(%d, %d)",
			device, crop_x, crop_y, crop_width, crop_height,
			sensor_width, sensor_height);
		goto exit;
	}

	/* CHECK align */
	if ((crop_width % 4) || (crop_height % 2)) {
		mwarn("Input width or height align does not fit.(%d x %d)",
			device, crop_width, crop_height);
		goto exit;
	}

	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state) ||
		device->group_3ax.id == GROUP_ID_3A0) {
		/* 3AX DMA INPUT */
		isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_BUF_MNGR;
		isp_param->dma1_input.width = sensor_width;
		isp_param->dma1_input.height = sensor_height;
		isp_param->dma1_input.uiDmaCropWidth = sensor_width;
		isp_param->dma1_input.uiDmaCropHeight = sensor_height;
		isp_param->dma1_input.uiBayerCropOffsetX = crop_x;
		isp_param->dma1_input.uiBayerCropOffsetY = crop_y;
		isp_param->dma1_input.uiBayerCropWidth = crop_width;
		isp_param->dma1_input.uiBayerCropHeight = crop_height;
		isp_param->dma1_input.uiBDSOutWidth = bds_width;
		isp_param->dma1_input.uiBDSOutHeight = bds_height;
		lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
		indexes++;

		/* 3AX DMA OUTPUT */
		isp_param->dma2_output.cmd = DMA_OUTPUT_COMMAND_ENABLE;
		isp_param->dma2_output.width = bds_width;
		isp_param->dma2_output.height = bds_height;
		lindex |= LOWBIT_OF(PARAM_ISP_DMA2_OUTPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_DMA2_OUTPUT);
		indexes++;

		/* ISP OTF OUTPUT */
		isp_param->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
		isp_param->otf_output.width = bds_width;
		isp_param->otf_output.height = bds_height;
		lindex |= LOWBIT_OF(PARAM_ISP_OTF_OUTPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_OTF_OUTPUT);
		indexes++;
	} else {
		/* 3AX DMA INPUT */
		isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		isp_param->dma1_input.width = sensor_width;
		isp_param->dma1_input.height = sensor_height;
		isp_param->dma1_input.uiDmaCropWidth = sensor_width;
		isp_param->dma1_input.uiDmaCropHeight = sensor_height;
		isp_param->dma1_input.uiBDSOutWidth = bds_width;
		isp_param->dma1_input.uiBDSOutHeight = bds_height;
		lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
		indexes++;

		/* 3AX OTF INPUT */
		isp_param->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
		isp_param->otf_input.width = sensor_width;
		isp_param->otf_input.height = sensor_height;
		isp_param->otf_input.crop_offset_x = crop_x;
		isp_param->otf_input.crop_offset_y = crop_y;
		isp_param->otf_input.crop_width = crop_width;
		isp_param->otf_input.crop_height = crop_height;
		isp_param->otf_input.uiBDSOutWidth = bds_width;
		isp_param->otf_input.uiBDSOutHeight = bds_height;
		lindex |= LOWBIT_OF(PARAM_ISP_OTF_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_OTF_INPUT);
		indexes++;
	}

	ret = fimc_is_itf_s_param(device,
		indexes, lindex, hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto exit;
	}

	device->sensor_width = sensor_width;
	device->sensor_height = sensor_height;
	device->dzoom_width = crop_width;

exit:

	return ret;
}
#endif

#ifdef BAYER_CROP_DZOOM
static int fimc_is_ischain_s_bayer_dzoom(struct fimc_is_device_ischain *device,
	u32 bds_width, u32 bds_height)
{
	int ret = 0;
	struct drc_param *drc_param;
	struct scalerc_param *scc_param;

	u32 indexes, lindex, hindex;

	drc_param = &device->is_region->parameter.drc;
	scc_param = &device->is_region->parameter.scalerc;
	indexes = lindex = hindex = 0;

#ifdef PRINT_DZOOM
	pr_info("[ISP:D:%d] request bayer down size : %dx%d\n",
		device->instance, bds_width, bds_height);
#endif

	/* DRC */
	drc_param->otf_input.width = bds_width;
	drc_param->otf_input.height = bds_height;
	lindex |= LOWBIT_OF(PARAM_DRC_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_DRC_OTF_INPUT);
	indexes++;

	drc_param->otf_output.width = bds_width;
	drc_param->otf_output.height = bds_height;
	lindex |= LOWBIT_OF(PARAM_DRC_OTF_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_DRC_OTF_OUTPUT);
	indexes++;

	/* SCC INPUT */
	scc_param->otf_input.width = bds_width;
	scc_param->otf_input.height = bds_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_OTF_INPUT);
	indexes++;

	/* SCC INPUT CROP */
	scc_param->input_crop.crop_width = bds_width;
	scc_param->input_crop.crop_height = bds_height;
	scc_param->input_crop.in_width = bds_width;
	scc_param->input_crop.in_height = bds_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_INPUT_CROP);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_INPUT_CROP);
	indexes++;

	/* SCC UNSCALED DMA OUT */
	scc_param->dma_output.width = bds_width;
	scc_param->dma_output.height = bds_height;
	lindex |= LOWBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device,
		indexes, lindex, hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto exit;
	}

	device->bds_width = bds_width;
	device->bds_height = bds_height;

exit:
	return ret;
}
#endif

#ifdef ENABLE_SENSOR_CTRL
static int fimc_is_ischain_sensor_ctrl(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	struct sensor_param *sensor_param;
	u32 group_id = 0;

	dbg_ischain("%s\n", __func__);

	group_id |= GROUP_ID(device->group_3ax.id);
	group_id |= GROUP_ID(device->group_isp.id);
	if (test_bit(FIMC_IS_GROUP_ACTIVE, &device->group_dis.state))
		group_id |= GROUP_ID(device->group_dis.id);

	if (fimc_is_itf_stream_off(device)) {
		err("fimc_is_itf_stream_off is fail\n");
		return -EINVAL;
	}

	if (fimc_is_itf_process_stop(device, group_id)) {
		err("fimc_is_itf_process_stop is fail\n");
		ret = -EINVAL;
		goto err_p_off;
	}

	sensor_param = &device->is_region->parameter.sensor;
	device->lindex = device->hindex = device->indexes = 0;

#ifdef FIXED_FPS_DEBUG
	sensor_param->frame_rate.frame_rate = FIXED_FPS_VALUE;
#else
	sensor_param->frame_rate.frame_rate = device->sensor->framerate;

	device->lindex |= LOWBIT_OF(PARAM_SENSOR_FRAME_RATE);
	device->hindex |= HIGHBIT_OF(PARAM_SENSOR_FRAME_RATE);
	device->indexes++;
#endif

	sensor_param->dma_output.width = device->sensor->width;
	sensor_param->dma_output.height = device->sensor->height;

	dbg_ischain("%s %dx%d\n", __func__, device->sensor->width,
			device->sensor->height);

	device->lindex |= LOWBIT_OF(PARAM_SENSOR_DMA_OUTPUT);
	device->hindex |= HIGHBIT_OF(PARAM_SENSOR_DMA_OUTPUT);
	device->indexes++;

	if (fimc_is_itf_s_param(device, device->indexes,
			device->lindex, device->hindex)) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto err_s_param;
	}

	if (fimc_is_itf_a_param(device, group_id)) {
		err("fimc_is_itf_a_param is fail\n");
		ret = -EINVAL;
	}

err_s_param:
	if (fimc_is_itf_process_start(device, group_id)) {
		err("fimc_is_itf_process_start is fail\n");
		ret = -EINVAL;
	}

err_p_off:
	if (fimc_is_itf_stream_on(device)) {
		err("fimc_is_itf_stream_on is fail\n");
		ret = -EINVAL;
	}

	return ret;
}
#endif

#ifdef ENABLE_DRC
static int fimc_is_ischain_drc_bypass(struct fimc_is_device_ischain *this,
	bool bypass)
{
	int ret = 0;
	struct drc_param *drc_param;
	u32 indexes, lindex, hindex;

	dbg_ischain("%s\n", __func__);

	drc_param = &this->is_region->parameter.drc;
	indexes = lindex = hindex = 0;

	if (bypass)
		drc_param->control.bypass = CONTROL_BYPASS_ENABLE;
	else
		drc_param->control.bypass = CONTROL_BYPASS_DISABLE;

	lindex |= LOWBIT_OF(PARAM_DRC_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_DRC_CONTROL);
	indexes++;

	ret = fimc_is_itf_s_param(this, indexes, lindex, hindex);
	if (ret) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto exit;
	}

	if (bypass) {
		clear_bit(FIMC_IS_ISDEV_DSTART, &this->drc.state);
		dbg_ischain("DRC off\n");
	} else {
		set_bit(FIMC_IS_ISDEV_DSTART, &this->drc.state);
		dbg_ischain("DRC on\n");
	}

exit:
	return ret;
}
#endif

static int fimc_is_ischain_dnr_bypass(struct fimc_is_device_ischain *this,
	bool bypass)
{
	int ret = 0;
	struct tdnr_param *dnr_param;
	u32 indexes, lindex, hindex;

	dbg_ischain("%s\n", __func__);

	dnr_param = &this->is_region->parameter.tdnr;
	indexes = lindex = hindex = 0;

	if (bypass) {
		dnr_param->control.bypass = CONTROL_BYPASS_ENABLE;
	} else {
		dnr_param->control.bypass = CONTROL_BYPASS_DISABLE;
		dnr_param->control.buffer_number =
			SIZE_DNR_INTERNAL_BUF * NUM_DNR_INTERNAL_BUF;
		dnr_param->control.buffer_address =
			this->imemory.dvaddr_shared + 350 * sizeof(u32);
		this->is_region->shared[350] = this->imemory.dvaddr_3dnr;
	}

	lindex |= LOWBIT_OF(PARAM_TDNR_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_TDNR_CONTROL);
	indexes++;

	ret = fimc_is_itf_s_param(this, indexes, lindex, hindex);
	if (ret) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto exit;
	}

	if (bypass) {
		clear_bit(FIMC_IS_ISDEV_DSTART, &this->dnr.state);
		dbg_ischain("TDNR off\n");
	} else {
		set_bit(FIMC_IS_ISDEV_DSTART, &this->dnr.state);
		dbg_ischain("TNDR on\n");
	}

exit:
	return ret;
}

static int fimc_is_ischain_fd_bypass(struct fimc_is_device_ischain *device,
	bool bypass)
{
	int ret = 0;
	struct fd_param *fd_param;
	struct fimc_is_subdev *fd;
	struct fimc_is_group *group;
	u32 indexes, lindex, hindex;
	u32 group_id = 0;

	BUG_ON(!device);

	mdbgd_ischain("%s(%d)\n", device, __func__, bypass);

	fd = &device->fd;
	group = fd->group;
	if (!group) {
		merr("group is NULL", device);
		ret = -EINVAL;
		goto p_err;
	}

	group_id |= GROUP_ID(group->id);
	fd_param = &device->is_region->parameter.fd;
	indexes = lindex = hindex = 0;

	ret = fimc_is_itf_process_stop(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	if (bypass) {
		fd_param->control.cmd = CONTROL_COMMAND_STOP;
		fd_param->control.bypass = CONTROL_BYPASS_DISABLE;
	} else {
		fd_param->control.cmd = CONTROL_COMMAND_START;
		fd_param->control.bypass = CONTROL_BYPASS_DISABLE;
	}

	lindex |= LOWBIT_OF(PARAM_FD_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_FD_CONTROL);
	indexes++;

	fd_param->otf_input.width = device->chain3_width;
	fd_param->otf_input.height = device->chain3_height;
	lindex |= LOWBIT_OF(PARAM_FD_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_FD_OTF_INPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_process_start(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	if (bypass) {
		clear_bit(FIMC_IS_ISDEV_DSTART, &fd->state);
		mdbgd_ischain("FD off\n", device);
	} else {
		set_bit(FIMC_IS_ISDEV_DSTART, &fd->state);
		mdbgd_ischain("FD on\n", device);
	}

p_err:
	return ret;
}

int fimc_is_ischain_3a0_open(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_open(groupmgr, group, GROUP_ID_3A0,
		device->instance, vctx, device, fimc_is_ischain_3a0_callback);
	if (ret)
		merr("fimc_is_group_open is fail", device);

	return ret;
}

int fimc_is_ischain_3a0_close(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_subdev *leader;
	struct fimc_is_queue *queue;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;
	leader = &group->leader;
	queue = GET_SRC_QUEUE(vctx);

	ret = fimc_is_ischain_3a0_stop(device, leader, queue);
	if (ret)
		merr("fimc_is_ischain_3a0_stop is fail", device);

	ret = fimc_is_group_close(groupmgr, group, vctx);
	if (ret)
		merr("fimc_is_group_close is fail", device);

	return ret;
}

int fimc_is_ischain_3a0_start(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		merr("already start", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_group_process_start(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	return ret;
}

int fimc_is_ischain_3a0_stop(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	if (!test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		mwarn("already stop", device);
		goto p_err;
	}

	ret = fimc_is_group_process_stop(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	pr_info("[3A0:D:%d] %s(%d, %d)\n", device->instance, __func__,
		ret, atomic_read(&group->scount));
	return ret;
}

int fimc_is_ischain_3a0_s_format(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct fimc_is_group *group;
	struct fimc_is_subdev *subdev;

	BUG_ON(!device);

	group = &device->group_3ax;
	subdev = &group->leader;

	subdev->input.width = width;
	subdev->input.height = height;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
		if (((width != device->bds_width) || (height != device->bds_height))) {
			ret = fimc_is_ischain_s_3ax_size(device, 0, 0,
				device->chain0_width, device->chain0_height,
				width, height);
			if (ret) {
				merr("fimc_is_ischain_s_3ax_size is fail:\
					CROP(%d, %d, %d, %d), BDS(%d, %d)\n",
					device, 0, 0,
					device->chain0_width, device->chain0_height,
					width, height);
				ret = -EINVAL;
				goto p_err;
			}
		}
	}

p_err:
	return ret;
}

int fimc_is_ischain_3a0_buffer_queue(struct fimc_is_device_ischain *device,
	struct fimc_is_queue *queue,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state));

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_buffer_queue(groupmgr, group, queue, index);
	if (ret) {
		merr("fimc_is_group_buffer_queue is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ischain_3a0_buffer_finish(struct fimc_is_device_ischain *device,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_buffer_finish(groupmgr, group, index);
	if (ret)
		merr("fimc_is_group_buffer_finish is fail(%d)", device, ret);

	return ret;
}

const struct fimc_is_queue_ops fimc_is_ischain_3a0_ops = {
	.start_streaming	= fimc_is_ischain_3a0_start,
	.stop_streaming		= fimc_is_ischain_3a0_stop
};

int fimc_is_ischain_3a1_open(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_open(groupmgr, group, GROUP_ID_3A1,
		device->instance, vctx, device, fimc_is_ischain_3a1_callback);
	if (ret)
		merr("fimc_is_group_open is fail", device);

	return ret;
}

int fimc_is_ischain_3a1_close(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_subdev *leader;
	struct fimc_is_queue *queue;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;
	leader = &group->leader;
	queue = GET_SRC_QUEUE(vctx);

	ret = fimc_is_ischain_3a1_stop(device, leader, queue);
	if (ret)
		merr("fimc_is_ischain_3a1_stop is fail", device);

	ret = fimc_is_group_close(groupmgr, group, vctx);
	if (ret)
		merr("fimc_is_group_close is fail", device);

	return ret;
}

int fimc_is_ischain_3a1_start(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		merr("already start", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_group_process_start(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	return ret;
}

int fimc_is_ischain_3a1_stop(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	if (!test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		mwarn("already stop", device);
		goto p_err;
	}

	ret = fimc_is_group_process_stop(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	pr_info("[3A1:D:%d] %s(%d, %d)\n", device->instance, __func__,
		ret, atomic_read(&group->scount));
	return ret;
}

static int fimc_is_ischain_s_scalable(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)

{
	int ret = 0;
	struct isp_param *isp_param;
	struct sensor_param *sensor_param;
	struct fimc_is_device_sensor *sensor;
	u32 group_id = 0;
	u32 active_sensor_width, active_sensor_height, binning;
	u32 indexes, lindex, hindex;

	dbg_ischain("%s()\n", __func__);

	sensor_param = &device->is_region->parameter.sensor;
	isp_param = &device->is_region->parameter.isp;
	sensor = device->sensor;

	group_id |= GROUP_ID(device->group_3ax.id);
	group_id |= GROUP_ID(device->group_isp.id);

	ret = fimc_is_itf_process_stop(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

#ifdef USE_OTF_INTERFACE
	device->sensor_width = sensor->width - device->margin_width;
	device->sensor_height = sensor->height - device->margin_height;
#else
	device->sensor_width = leader->input.width - device->margin_width;
	device->sensor_height = leader->input.height - device->margin_height;
#endif
	pr_info("scalable sensor input: %dx%d\n", device->sensor_width,
			device->sensor_height);

	indexes = lindex = hindex = 0;

#ifdef FIXED_FPS_DEBUG
	sensor_param->frame_rate.frame_rate = FIXED_FPS_VALUE;
#else
	sensor_param->frame_rate.frame_rate = sensor->framerate;
#endif
	lindex |= LOWBIT_OF(PARAM_SENSOR_FRAME_RATE);
	hindex |= HIGHBIT_OF(PARAM_SENSOR_FRAME_RATE);
	indexes++;

	sensor_param->dma_output.width = sensor->width;
	sensor_param->dma_output.height = sensor->height;
	lindex |= LOWBIT_OF(PARAM_SENSOR_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SENSOR_DMA_OUTPUT);
	indexes++;

	if (sensor->active_sensor) {
		active_sensor_width = sensor->active_sensor->pixel_width;
		active_sensor_height = sensor->active_sensor->pixel_height;
	} else {
		active_sensor_width = sensor->width;
		active_sensor_height = sensor->height;
	}

	binning = min(
		BINNING(active_sensor_width, sensor->width),
		BINNING(active_sensor_height, sensor->height)
		);

	/* 3AX DMA INPUT */
	isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	isp_param->dma1_input.width = device->sensor_width;
	isp_param->dma1_input.height = device->sensor_height;
	isp_param->dma1_input.uiDmaCropWidth = device->sensor_width;
	isp_param->dma1_input.uiDmaCropHeight = device->sensor_height;
	isp_param->dma1_input.binning_ratio_x = binning;
	isp_param->dma1_input.binning_ratio_y = binning;
	lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
	indexes++;

	/* 3AX OTF INPUT */
	isp_param->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	isp_param->otf_input.width = device->sensor_width;
	isp_param->otf_input.height = device->sensor_height;
	isp_param->otf_input.crop_offset_x = 0;
	isp_param->otf_input.crop_offset_y = 0;
	isp_param->otf_input.crop_width = device->sensor_width;
	isp_param->otf_input.crop_height = device->sensor_height;
	isp_param->otf_input.binning_ratio_x = binning;
	isp_param->otf_input.binning_ratio_y = binning;
	lindex |= LOWBIT_OF(PARAM_ISP_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_OTF_INPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device,
		indexes, lindex, hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_a_param(device, group_id);
	if (ret) {
		merr("fimc_is_itf_a_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_process_start(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ischain_3a1_s_format(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct fimc_is_group *group;
	struct fimc_is_subdev *leader;
	struct fimc_is_queue *queue;

	BUG_ON(!device);
	BUG_ON(!device->sensor);

	group = &device->group_3ax;
	leader = &group->leader;

	leader->input.width = width;
	leader->input.height = height;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		ret = fimc_is_ischain_s_scalable(device, leader, queue);
		if (ret) {
			err("fimc_is_ischain_s_scalable is fail\n");
			goto p_err;
		}
	}

p_err:
	return ret;
}

int fimc_is_ischain_3a1_buffer_queue(struct fimc_is_device_ischain *device,
	struct fimc_is_queue *queue,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state));

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_buffer_queue(groupmgr, group, queue, index);
	if (ret)
		merr("fimc_is_group_buffer_queue is fail(%d)", device, ret);

	return ret;
}

int fimc_is_ischain_3a1_buffer_finish(struct fimc_is_device_ischain *device,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_3ax;

	ret = fimc_is_group_buffer_finish(groupmgr, group, index);
	if (ret)
		merr("fimc_is_group_buffer_finish is fail(%d)", device, ret);

	return ret;
}

const struct fimc_is_queue_ops fimc_is_ischain_3a1_ops = {
	.start_streaming	= fimc_is_ischain_3a1_start,
	.stop_streaming		= fimc_is_ischain_3a1_stop
};

int fimc_is_ischain_isp_start(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct isp_param *isp_param;
	struct sensor_param *sensor_param;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_subdev *leader_3ax;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
#ifdef ENABLE_BAYER_CROP
	u32 crop_x, crop_y, crop_width, crop_height;
	u32 sensor_width, sensor_height, sensor_ratio;
	u32 chain0_width, chain0_height, chain0_ratio;
	u32 chain3_width, chain3_height, chain3_ratio;
	u32 chain1_wmin, chain1_hmin;
#endif
	u32 lindex, hindex, indexes;
	u32 active_sensor_width, active_sensor_height, binning;

	BUG_ON(!device);
	BUG_ON(!device->sensor);
	BUG_ON(!queue);

	mdbgd_isp("%s()\n", device, __func__);

	indexes = lindex = hindex = 0;
	groupmgr = device->groupmgr;
	group = &device->group_isp;
	framemgr = &queue->framemgr;
	leader_3ax = &device->group_3ax.leader;
	sensor = device->sensor;
	isp_param = &device->is_region->parameter.isp;
	sensor_param = &device->is_region->parameter.sensor;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		merr("already start", device);
		ret = -EINVAL;
		goto p_err;
	}

	/* 1. check chain size */
#ifdef USE_OTF_INTERFACE
	device->sensor_width = sensor->width - device->margin_width;
	device->sensor_height = sensor->height - device->margin_height;
#else
	device->sensor_width = leader_3ax->input.width - device->margin_width;
	device->sensor_height = leader_3ax->input.height - device->margin_height;
#endif

	if (leader_3ax->output.width != leader->input.width) {
		merr("width size is invalid(%d != %d)", device,
			leader_3ax->output.width, leader->input.width);
		ret = -EINVAL;
		goto p_err;
	}

	if (leader_3ax->output.height != leader->input.height) {
		merr("height size is invalid(%d != %d)", device,
			leader_3ax->output.height, leader->input.height);
		ret = -EINVAL;
		goto p_err;
	}

	device->chain0_width = leader->input.width;
	device->chain0_height = leader->input.height;

	device->dzoom_width = 0;
	device->bds_width = 0;
	device->bds_height = 0;
#ifdef ENABLE_BAYER_CROP
	/* 2. crop calculation */
	sensor_width = device->sensor_width;
	sensor_height = device->sensor_height;
	chain3_width = device->chain3_width;
	chain3_height = device->chain3_height;
	crop_width = sensor_width;
	crop_height = sensor_height;
	crop_x = crop_y = 0;

	sensor_ratio = sensor_width * 1000 / sensor_height;
	chain3_ratio = chain3_width * 1000 / chain3_height;

	if (sensor_ratio == chain3_ratio) {
		crop_width = sensor_width;
		crop_height = sensor_height;
	} else if (sensor_ratio < chain3_ratio) {
		/*
		 * isp dma input limitation
		 * height : 2 times
		 */
		crop_height =
			(sensor_width * chain3_height) / chain3_width;
		crop_height = ALIGN(crop_height, 2);
		crop_y = ((sensor_height - crop_height) >> 1) & 0xFFFFFFFE;
	} else {
		/*
		 * isp dma input limitation
		 * width : 4 times
		 */
		crop_width =
			(sensor_height * chain3_width) / chain3_height;
		crop_width = ALIGN(crop_width, 4);
		crop_x = ((sensor_width - crop_width) >> 1) & 0xFFFFFFFE;
	}
	device->chain0_width = crop_width;
	device->chain0_height = crop_height;

	device->dzoom_width = crop_width;
	device->crop_width = crop_width;
	device->crop_height = crop_height;
	device->crop_x = crop_x;
	device->crop_y = crop_y;

	dbg_isp("crop_x : %d, crop y : %d\n", crop_x, crop_y);
	dbg_isp("crop width : %d, crop height : %d\n",
		crop_width, crop_height);

	/* 2. scaling calculation */
	chain1_wmin = (crop_width >> 4) & 0xFFFFFFFE;
	chain1_hmin = (crop_height >> 4) & 0xFFFFFFFE;

	if (chain1_wmin > device->chain1_width) {
		printk(KERN_INFO "scc down scale limited : (%d,%d)->(%d,%d)\n",
			device->chain1_width, device->chain1_height,
			chain1_wmin, chain1_hmin);
		device->chain1_width = chain1_wmin;
		device->chain1_height = chain1_hmin;
		device->chain2_width = chain1_wmin;
		device->chain2_height = chain1_hmin;
	}
#endif
	fimc_is_ischain_s_chain0_size(device,
		device->chain0_width, device->chain0_height);

	fimc_is_ischain_s_chain1_size(device,
		device->chain1_width, device->chain1_height);

	fimc_is_ischain_s_chain2_size(device,
		device->chain2_width, device->chain2_height);

	fimc_is_ischain_s_chain3_size(device,
		device->chain3_width, device->chain3_height);

#ifdef FIXED_FPS_DEBUG
	sensor_param->frame_rate.frame_rate = FIXED_FPS_VALUE;
#else
	sensor_param->frame_rate.frame_rate = sensor->framerate;
#endif
	sensor_param->dma_output.width = sensor->width;
	sensor_param->dma_output.height = sensor->height;

	if (sensor->active_sensor) {
		active_sensor_width = sensor->active_sensor->pixel_width;
		active_sensor_height = sensor->active_sensor->pixel_height;
	} else {
		active_sensor_width = sensor->width;
		active_sensor_height = sensor->height;
	}

	binning = min(
		BINNING(active_sensor_width, sensor->width),
		BINNING(active_sensor_height, sensor->height)
		);

#ifdef USE_OTF_INTERFACE
	if (test_bit(FIMC_IS_ISCHAIN_REPROCESSING, &device->state) ||
		device->group_3ax.id == GROUP_ID_3A0) {

		/* reprocessing instnace uses actual sensor size */
		binning = min(
			BINNING(active_sensor_width, device->chain0_width),
			BINNING(active_sensor_height, device->chain0_height)
			);

		isp_param->control.cmd = CONTROL_COMMAND_START;
		isp_param->control.bypass = CONTROL_BYPASS_DISABLE;
		isp_param->control.run_mode = 1;
		lindex |= LOWBIT_OF(PARAM_ISP_CONTROL);
		hindex |= HIGHBIT_OF(PARAM_ISP_CONTROL);
		indexes++;

		isp_param->otf_input.cmd = OTF_INPUT_COMMAND_DISABLE;
		isp_param->otf_input.format = OTF_INPUT_FORMAT_BAYER;
		isp_param->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT;
		isp_param->otf_input.order = OTF_INPUT_ORDER_BAYER_GR_BG;
		isp_param->otf_input.frametime_min = 0;
		isp_param->otf_input.frametime_max = 1000000;
		isp_param->otf_input.width = device->chain0_width;
		isp_param->otf_input.height = device->chain0_height;
		isp_param->otf_input.binning_ratio_x = binning;
		isp_param->otf_input.binning_ratio_y = binning;
		lindex |= LOWBIT_OF(PARAM_ISP_OTF_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_OTF_INPUT);
		indexes++;

		isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_BUF_MNGR;
		isp_param->dma1_input.width = device->sensor_width;
		isp_param->dma1_input.height = device->sensor_height;
		isp_param->dma1_input.uiDmaCropOffsetX = 0;
		isp_param->dma1_input.uiDmaCropOffsetY = 0;
		isp_param->dma1_input.uiDmaCropWidth = device->chain0_width;
		isp_param->dma1_input.uiDmaCropHeight = device->chain0_height;
		isp_param->dma1_input.uiBayerCropOffsetX = 0;
		isp_param->dma1_input.uiBayerCropOffsetY = 0;
		isp_param->dma1_input.uiBayerCropWidth = 0;
		isp_param->dma1_input.uiBayerCropHeight = 0;
		isp_param->dma1_input.uiBDSOutEnable = ISP_BDS_COMMAND_ENABLE;
		isp_param->dma1_input.uiBDSOutWidth = device->chain0_width;
		isp_param->dma1_input.uiBDSOutHeight = device->chain0_height;
		isp_param->dma1_input.uiUserMinFrameTime = 0;
		isp_param->dma1_input.uiUserMaxFrameTime = 1000000;
		isp_param->dma1_input.uiWideFrameGap = 1;
		isp_param->dma1_input.uiFrameGap = 0;
		isp_param->dma1_input.uiLineGap = 50;

		if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR12) {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_12BIT;
		} else if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR16) {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
		} else {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
			mwarn("Invalid bayer format", device);
		}

		isp_param->dma1_input.bitwidth = DMA_INPUT_BIT_WIDTH_10BIT;
		isp_param->dma1_input.order = DMA_INPUT_ORDER_GR_BG;
		isp_param->dma1_input.plane = 1;
		isp_param->dma1_input.buffer_number = 0;
		isp_param->dma1_input.buffer_address = 0;
		isp_param->dma1_input.binning_ratio_x = binning;
		isp_param->dma1_input.binning_ratio_y = binning;
		/*
		 * hidden spec
		 *     [0] : sensor size is dma input size
		 *     [X] : sneosr size is reserved field
		 */
		isp_param->dma1_input.uiReserved[1] = 0;
		isp_param->dma1_input.uiReserved[2] = 0;
		lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
		indexes++;
	} else {
		isp_param->control.cmd = CONTROL_COMMAND_START;
		isp_param->control.bypass = CONTROL_BYPASS_DISABLE;
		isp_param->control.run_mode = 1;
		lindex |= LOWBIT_OF(PARAM_ISP_CONTROL);
		hindex |= HIGHBIT_OF(PARAM_ISP_CONTROL);
		indexes++;

		isp_param->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
		isp_param->otf_input.format = OTF_INPUT_FORMAT_BAYER;
		isp_param->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT;
		isp_param->otf_input.order = OTF_INPUT_ORDER_BAYER_GR_BG;
		isp_param->otf_input.frametime_min = 0;
		isp_param->otf_input.frametime_max = 1000000;
		isp_param->otf_input.width = device->sensor_width;
		isp_param->otf_input.height = device->sensor_height;
		isp_param->otf_input.crop_offset_x = 0;
		isp_param->otf_input.crop_offset_y = 0;
		isp_param->otf_input.crop_width = device->sensor_width;
		isp_param->otf_input.crop_height = device->sensor_height;
		isp_param->otf_input.uiBDSOutEnable = ISP_BDS_COMMAND_ENABLE;
		isp_param->otf_input.uiBDSOutWidth = device->chain0_width;
		isp_param->otf_input.uiBDSOutHeight = device->chain0_height;
		isp_param->otf_input.binning_ratio_x = binning;
		isp_param->otf_input.binning_ratio_y = binning;
		lindex |= LOWBIT_OF(PARAM_ISP_OTF_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_OTF_INPUT);
		indexes++;

		isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_DISABLE;
		isp_param->dma1_input.width = device->sensor_width;
		isp_param->dma1_input.height = device->sensor_height;
		isp_param->dma1_input.uiDmaCropOffsetX = 0;
		isp_param->dma1_input.uiDmaCropOffsetY = 0;
		isp_param->dma1_input.uiDmaCropWidth = device->sensor_width;
		isp_param->dma1_input.uiDmaCropHeight = device->sensor_height;
		isp_param->dma1_input.uiBayerCropOffsetX = 0;
		isp_param->dma1_input.uiBayerCropOffsetY = 0;
		isp_param->dma1_input.uiBayerCropWidth = 0;
		isp_param->dma1_input.uiBayerCropHeight = 0;
		isp_param->dma1_input.uiBDSOutEnable = ISP_BDS_COMMAND_ENABLE;
		isp_param->dma1_input.uiBDSOutWidth = device->chain0_width;
		isp_param->dma1_input.uiBDSOutHeight = device->chain0_height;
		isp_param->dma1_input.uiUserMinFrameTime = 0;
		isp_param->dma1_input.uiUserMaxFrameTime = 1000000;
		isp_param->dma1_input.uiWideFrameGap = 1;
		isp_param->dma1_input.uiFrameGap = 0;
		isp_param->dma1_input.uiLineGap = 50;

		if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR12) {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_12BIT;
		} else if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR16) {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
		} else {
			isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
			mwarn("Invalid bayer format", device);
		}

		isp_param->dma1_input.bitwidth = DMA_INPUT_BIT_WIDTH_10BIT;
		isp_param->dma1_input.order = DMA_INPUT_ORDER_GR_BG;
		isp_param->dma1_input.plane = 1;
		isp_param->dma1_input.buffer_number = 0;
		isp_param->dma1_input.buffer_address = 0;
		isp_param->dma1_input.binning_ratio_x = binning;
		isp_param->dma1_input.binning_ratio_y = binning;
		/*
		 * hidden spec
		 *     [0] : sensor size is dma input size
		 *     [X] : sneosr size is reserved field
		 */
		isp_param->dma1_input.uiReserved[1] = 0;
		isp_param->dma1_input.uiReserved[2] = 0;
		lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
		hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
		indexes++;
	}
#else
	isp_param->control.cmd = CONTROL_COMMAND_START;
	isp_param->control.bypass = CONTROL_BYPASS_DISABLE;
	isp_param->control.run_mode = 1;
	lindex |= LOWBIT_OF(PARAM_ISP_CONTROL);
	hindex |= HIGHBIT_OF(PARAM_ISP_CONTROL);
	indexes++;

	isp_param->otf_input.cmd = OTF_INPUT_COMMAND_DISABLE;
	isp_param->otf_input.format = OTF_INPUT_FORMAT_BAYER;
	isp_param->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT;
	isp_param->otf_input.order = OTF_INPUT_ORDER_BAYER_GR_BG;
	isp_param->otf_input.frametime_min = 0;
	isp_param->otf_input.frametime_max = 1000000;
	isp_param->otf_input.width = device->sensor_width;
	isp_param->otf_input.height = device->sensor_height;
	isp_param->otf_input.binning_ratio_x = binning;
	isp_param->otf_input.binning_ratio_y = binning;
	lindex |= LOWBIT_OF(PARAM_ISP_OTF_INPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_OTF_INPUT);
	indexes++;

	isp_param->dma1_input.cmd = DMA_INPUT_COMMAND_BUF_MNGR;
	isp_param->dma1_input.width = device->sensor_width;
	isp_param->dma1_input.height = device->sensor_height;
	isp_param->dma1_input.uiDmaCropOffsetX = 0;
	isp_param->dma1_input.uiDmaCropOffsetY = 0;
	isp_param->dma1_input.uiDmaCropWidth = device->sensor_width;
	isp_param->dma1_input.uiDmaCropHeight = device->sensor_height;
	isp_param->dma1_input.uiBayerCropOffsetX = 0;
	isp_param->dma1_input.uiBayerCropOffsetY = 0;
	isp_param->dma1_input.uiBayerCropWidth = 0;
	isp_param->dma1_input.uiBayerCropHeight = 0;
	isp_param->dma1_input.uiBDSOutEnable = ISP_BDS_COMMAND_ENABLE;
	isp_param->dma1_input.uiBDSOutWidth = device->chain0_width;
	isp_param->dma1_input.uiBDSOutHeight = device->chain0_height;
	isp_param->dma1_input.uiUserMinFrameTime = 0;
	isp_param->dma1_input.uiUserMaxFrameTime = 1000000;
	isp_param->dma1_input.uiWideFrameGap = 1;
	isp_param->dma1_input.uiFrameGap = 0;
	isp_param->dma1_input.uiLineGap = 50;

	if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR12) {
		isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_12BIT;
	} else if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR16) {
		isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
	} else {
		isp_param->dma1_input.uiMemoryWidthBits = DMA_INPUT_MEMORY_WIDTH_16BIT;
		mwarn("Invalid bayer format", device);
	}

	isp_param->dma1_input.bitwidth = DMA_INPUT_BIT_WIDTH_10BIT;
	isp_param->dma1_input.order = DMA_INPUT_ORDER_GR_BG;
	isp_param->dma1_input.plane = 1;
	isp_param->dma1_input.buffer_number = 0;
	isp_param->dma1_input.buffer_address = 0;
	isp_param->dma1_input.binning_ratio_x = binning;
	isp_param->dma1_input.binning_ratio_y = binning;
	/*
	 * hidden spec
	 *     [0] : sensor size is dma input size
	 *     [X] : sneosr size is reserved field
	 */
	isp_param->dma1_input.uiReserved[1] = 0;
	isp_param->dma1_input.uiReserved[2] = 0;
	lindex |= LOWBIT_OF(PARAM_ISP_DMA1_INPUT);
	hindex |= HIGHBIT_OF(PARAM_ISP_DMA1_INPUT);
	indexes++;
#endif

	lindex = 0xFFFFFFFF;
	hindex = 0xFFFFFFFF;

	ret = fimc_is_itf_s_param(device , indexes, lindex, hindex);
	if (ret) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_f_param(device);
	if (ret) {
		err("fimc_is_itf_f_param is fail\n");
		ret = -EINVAL;
		goto p_err;
	}

	/*
	 * this code is enabled when camera 2.0 feature is enabled
	 * ret = fimc_is_itf_g_capability(device);
	 * if (ret) {
	 *	err("fimc_is_itf_g_capability is fail\n");
	 *	ret = -EINVAL;
	 *	goto p_err;
	 *}
	 */

	ret = fimc_is_itf_init_process_start(device);
	if (ret) {
		err("fimc_is_itf_init_process_start is fail\n");
		return -EINVAL;
	}

	ret = fimc_is_group_process_start(groupmgr, group, queue);
	if (ret)
		merr("fimc_is_group_process_start is fail", device);

	set_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	pr_info("[ISP:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}

int fimc_is_ischain_isp_stop(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);
	BUG_ON(!queue);

	mdbgd_isp("%s\n", device, __func__);

	groupmgr = device->groupmgr;
	group = &device->group_isp;

	if (!test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		mwarn("already stop", device);
		goto p_err;
	}

	ret = fimc_is_group_process_stop(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	pr_info("[ISP:D:%d] %s(%d, %d)\n", device->instance, __func__,
		ret, atomic_read(&group->scount));
	return ret;
}

int fimc_is_ischain_isp_s_format(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;
	struct fimc_is_group *group;
	struct fimc_is_subdev *subdev;

	BUG_ON(!device);

	group = &device->group_isp;
	subdev = &group->leader;

	subdev->input.width = width;
	subdev->input.height = height;

	return ret;
}

int fimc_is_ischain_isp_buffer_queue(struct fimc_is_device_ischain *device,
	struct fimc_is_queue *queue,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state));

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_isp;

	ret = fimc_is_group_buffer_queue(groupmgr, group, queue, index);
	if (ret) {
		merr("fimc_is_group_buffer_queue is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ischain_isp_buffer_finish(struct fimc_is_device_ischain *device,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_isp;

	ret = fimc_is_group_buffer_finish(groupmgr, group, index);
	if (ret)
		merr("fimc_is_group_buffer_finish is fail(%d)", device, ret);

	return ret;
}

const struct fimc_is_queue_ops fimc_is_ischain_isp_ops = {
	.start_streaming	= fimc_is_ischain_isp_start,
	.stop_streaming		= fimc_is_ischain_isp_stop
};

int fimc_is_ischain_scc_start(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 planes, i, j, buf_index;
	u32 indexes, lindex, hindex;
	struct scalerc_param *scc_param;
	struct fimc_is_subdev *scc;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_queue *queue;

	scc = &device->scc;
	vctx = scc->vctx;
	queue = &vctx->q_dst;

	mdbgd_ischain("%s(%dx%d)\n", device, __func__,
		queue->framecfg.width,
		queue->framecfg.height);

	planes = queue->framecfg.format.num_planes;
	for (i = 0; i < queue->buf_maxcount; i++) {
		for (j = 0; j < planes; j++) {
			buf_index = i*planes + j;

			device->is_region->shared[447+buf_index] =
				queue->buf_dva[i][j];
		}
	}

	dbg_ischain("buf_num:%d buf_plane:%d shared[447] : 0x%X\n",
		queue->buf_maxcount,
		queue->framecfg.format.num_planes,
		device->imemory.kvaddr_shared + 447 * sizeof(u32));

	indexes = 0;
	lindex = hindex = 0;

	scc_param = &device->is_region->parameter.scalerc;
	scc_param->dma_output.cmd = DMA_OUTPUT_COMMAND_ENABLE;
	scc_param->dma_output.buffer_number = queue->buf_maxcount;
	scc_param->dma_output.plane = queue->framecfg.format.num_planes - 1;
	scc_param->dma_output.buffer_address =
		device->imemory.dvaddr_shared + 447*sizeof(u32);

	scc_param->dma_output.width = queue->framecfg.width;
	scc_param->dma_output.height = queue->framecfg.height;

	lindex |= LOWBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (!ret)
		set_bit(FIMC_IS_ISDEV_DSTART, &scc->state);
	else
		merr("fimc_is_itf_s_param is fail", device);

	return ret;
}

int fimc_is_ischain_scc_stop(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 indexes, lindex, hindex;
	struct scalerc_param *scc_param;
	struct fimc_is_subdev *scc;

	dbg_ischain("%s\n", __func__);

	indexes = 0;
	lindex = hindex = 0;
	scc = &device->scc;

	scc_param = &device->is_region->parameter.scalerc;
	scc_param->dma_output.cmd = DMA_OUTPUT_COMMAND_DISABLE;

	lindex |= LOWBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERC_DMA_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (!ret)
		clear_bit(FIMC_IS_ISDEV_DSTART, &scc->state);
	else
		merr("fimc_is_itf_s_param is fail", device);

	return ret;
}

int fimc_is_ischain_scc_tag(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_frame *grp_frame)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!device);
	BUG_ON(!subdev);
	BUG_ON(!grp_frame);

	framemgr = GET_SUBDEV_FRAMEMGR(subdev);
	if (!framemgr) {
		merr("framemgr is NULL", device);
		ret = -EINVAL;
		goto p_err;
	}

	if (grp_frame->shot_ext->request_scc) {
		if (!test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_scc_start(device);
			if (ret) {
				merr("scc_start is fail", device);
				goto p_err;
			}
		}

		framemgr_e_barrier_irqs(framemgr, FMGR_IDX_8, flags);

		fimc_is_frame_request_head(framemgr, &frame);
		if (frame) {
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[0] =
				frame->dvaddr_buffer[0];
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[1] =
				frame->dvaddr_buffer[1];
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[2] =
				frame->dvaddr_buffer[2];
			frame->stream->findex = grp_frame->index;
			set_bit(OUT_SCC_FRAME, &grp_frame->out_flag);
			set_bit(REQ_FRAME, &frame->req_flag);
			fimc_is_frame_trans_req_to_pro(framemgr, frame);
		} else {
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[0] = 0;
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[1] = 0;
			grp_frame->shot->uctl.scalerUd.sccTargetAddress[2] = 0;
			grp_frame->shot_ext->request_scc = 0;
			mwarn("scc %d frame is drop", device, grp_frame->fcount);
		}

		framemgr_x_barrier_irqr(framemgr, FMGR_IDX_8, flags);
	} else {
		if (test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_scc_stop(device);
			if (ret) {
				merr("scc_stop is fail", device);
				goto p_err;
			}
		}

		grp_frame->shot->uctl.scalerUd.sccTargetAddress[0] = 0;
		grp_frame->shot->uctl.scalerUd.sccTargetAddress[1] = 0;
		grp_frame->shot->uctl.scalerUd.sccTargetAddress[2] = 0;
		grp_frame->shot_ext->request_scc = 0;
	}

p_err:
	return ret;
}

int fimc_is_ischain_scp_start(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 planes, i, j, buf_index;
	u32 indexes, lindex, hindex;
	struct scalerp_param *scp_param;
	struct fimc_is_subdev *scp;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_queue *queue;

	dbg_ischain("%s\n", __func__);

	scp = &device->scp;
	vctx = scp->vctx;
	queue = &vctx->q_dst;

	planes = queue->framecfg.format.num_planes;
	for (i = 0; i < queue->buf_maxcount; i++) {
		for (j = 0; j < planes; j++) {
			buf_index = i*planes + j;

			device->is_region->shared[400 + buf_index] =
				queue->buf_dva[i][j];
		}
	}

	dbg_ischain("buf_num:%d buf_plane:%d shared[400] : 0x%X\n",
		queue->buf_maxcount,
		queue->framecfg.format.num_planes,
		device->imemory.kvaddr_shared + 400 * sizeof(u32));

	indexes = 0;
	lindex = hindex = 0;

	scp_param = &device->is_region->parameter.scalerp;
	scp_param->dma_output.cmd = DMA_OUTPUT_COMMAND_ENABLE;
	scp_param->dma_output.buffer_number = queue->buf_maxcount;
#ifdef USE_FRAME_SYNC
	scp_param->dma_output.plane = queue->framecfg.format.num_planes - 1;
#else
	scp_param->dma_output.plane = queue->framecfg.format.num_planes;
#endif
	scp_param->dma_output.buffer_address =
		device->imemory.dvaddr_shared + 400 * sizeof(u32);

	scp_param->dma_output.width = queue->framecfg.width;
	scp_param->dma_output.height = queue->framecfg.height;

	switch (queue->framecfg.format.pixelformat) {
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420M:
		scp_param->dma_output.format = OTF_OUTPUT_FORMAT_YUV420,
		scp_param->dma_output.plane = DMA_OUTPUT_PLANE_3;
		scp_param->dma_output.order = DMA_OUTPUT_ORDER_NO;
		break;
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV21:
		scp_param->dma_output.format = OTF_OUTPUT_FORMAT_YUV420,
		scp_param->dma_output.plane = DMA_OUTPUT_PLANE_2;
		scp_param->dma_output.order = DMA_OUTPUT_ORDER_CbCr;
		break;
	default:
		mwarn("unknown preview pixelformat", device);
		break;
	}

	lindex |= LOWBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (!ret)
		set_bit(FIMC_IS_ISDEV_DSTART, &scp->state);
	else
		merr("fimc_is_itf_s_param is fail", device);

	return ret;
}

int fimc_is_ischain_scp_stop(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 indexes, lindex, hindex;
	struct scalerp_param *scp_param;
	struct fimc_is_subdev *scp;

	dbg_ischain("%s\n", __func__);

	indexes = 0;
	lindex = hindex = 0;
	scp = &device->scp;

	scp_param = &device->is_region->parameter.scalerp;
	scp_param->dma_output.cmd = DMA_OUTPUT_COMMAND_DISABLE;

	lindex |= LOWBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	hindex |= HIGHBIT_OF(PARAM_SCALERP_DMA_OUTPUT);
	indexes++;

	ret = fimc_is_itf_s_param(device, indexes, lindex, hindex);
	if (!ret)
		clear_bit(FIMC_IS_ISDEV_DSTART, &scp->state);
	else
		merr("fimc_is_itf_s_param is fail", device);

	return ret;
}

int fimc_is_ischain_scp_s_format(struct fimc_is_device_ischain *this,
	u32 width, u32 height)
{
	int ret = 0;

	this->chain1_width = width;
	this->chain1_height = height;
	this->chain2_width = width;
	this->chain2_height = height;
	this->chain3_width = width;
	this->chain3_height = height;

	return ret;
}

int fimc_is_ischain_scp_tag(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_frame *grp_frame)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!device);
	BUG_ON(!subdev);
	BUG_ON(!grp_frame);

	framemgr = GET_SUBDEV_FRAMEMGR(subdev);
	if (!framemgr) {
		merr("framemgr is NULL", device);
		ret = -EINVAL;
		goto p_err;
	}

	if (grp_frame->shot_ext->request_scp) {
		if (!test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_scp_start(device);
			if (ret) {
				merr("scp_start is fail", device);
				goto p_err;
			}
		}

		framemgr_e_barrier_irqs(framemgr, FMGR_IDX_9, flags);

		fimc_is_frame_request_head(framemgr, &frame);
		if (frame) {
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[0] =
				frame->dvaddr_buffer[0];
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[1] =
				frame->dvaddr_buffer[1];
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[2] =
				frame->dvaddr_buffer[2];
			frame->stream->findex = grp_frame->index;
			set_bit(OUT_SCP_FRAME, &grp_frame->out_flag);
			set_bit(REQ_FRAME, &frame->req_flag);
			fimc_is_frame_trans_req_to_pro(framemgr, frame);
		} else {
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[0] = 0;
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[1] = 0;
			grp_frame->shot->uctl.scalerUd.scpTargetAddress[2] = 0;
			grp_frame->shot_ext->request_scp = 0;
			mwarn("scp %d frame is drop", device, grp_frame->fcount);
		}

		framemgr_x_barrier_irqr(framemgr, FMGR_IDX_9, flags);
	} else {
		if (test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_scp_stop(device);
			if (ret) {
				merr("scp_stop is fail", device);
				goto p_err;
			}
		}

		grp_frame->shot->uctl.scalerUd.scpTargetAddress[0] = 0;
		grp_frame->shot->uctl.scalerUd.scpTargetAddress[1] = 0;
		grp_frame->shot->uctl.scalerUd.scpTargetAddress[2] = 0;
		grp_frame->shot_ext->request_scp = 0;
	}

p_err:
	return ret;
}

int fimc_is_ischain_dis_start(struct fimc_is_device_ischain *device,
	bool bypass)
{
	int ret = 0;
	u32 group_id = 0;
	u32 chain1_width, chain1_height;
	struct dis_param *dis_param;

	dbg_ischain("%s()\n", __func__);

	chain1_width = device->dis_width;
	chain1_height = device->dis_height;
	dis_param = &device->is_region->parameter.dis;
	group_id |= GROUP_ID(device->group_isp.id);
	group_id |= GROUP_ID(device->group_dis.id);

	ret = fimc_is_itf_process_stop(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	device->lindex = device->hindex = device->indexes = 0;
	fimc_is_ischain_s_chain1_size(device, chain1_width, chain1_height);

	if (bypass) {
		dis_param->control.bypass = 2;
	} else {
#ifdef ENABLE_VDIS
		dis_param->control.bypass = CONTROL_BYPASS_DISABLE;
		dis_param->control.buffer_number =
			SIZE_DIS_INTERNAL_BUF * NUM_DIS_INTERNAL_BUF;
		dis_param->control.buffer_address =
			device->imemory.dvaddr_shared + 300 * sizeof(u32);
		device->is_region->shared[300] = device->imemory.dvaddr_dis;
#else
		merr("can't start hw vdis", device);
		BUG_ON(1);
#endif
	}

	device->lindex |= LOWBIT_OF(PARAM_DIS_CONTROL);
	device->hindex |= HIGHBIT_OF(PARAM_DIS_CONTROL);
	device->indexes++;

	ret = fimc_is_itf_s_param(device,
		device->indexes, device->lindex, device->hindex);
	if (ret) {
		merr("fimc_is_itf_s_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_a_param(device, group_id);
	if (ret) {
		merr("fimc_is_itf_a_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_process_start(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(FIMC_IS_ISDEV_DSTART, &device->dis.state);
	dbg_ischain("DIS on\n");

	device->chain1_width = chain1_width;
	device->chain1_height = chain1_height;

p_err:
	return ret;
}

int fimc_is_ischain_dis_stop(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	u32 group_id = 0;
	u32 chain1_width, chain1_height;
	struct dis_param *dis_param;

	dbg_ischain("%s()\n", __func__);

	chain1_width = device->chain2_width;
	chain1_height = device->chain2_height;
	dis_param = &device->is_region->parameter.dis;
	group_id |= GROUP_ID(device->group_isp.id);
	group_id |= GROUP_ID(device->group_dis.id);

	ret = fimc_is_itf_process_stop(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	device->lindex = device->hindex = device->indexes = 0;
	fimc_is_ischain_s_chain1_size(device, chain1_width, chain1_height);

	dis_param->control.bypass = CONTROL_BYPASS_ENABLE;

	device->lindex |= LOWBIT_OF(PARAM_DIS_CONTROL);
	device->hindex |= HIGHBIT_OF(PARAM_DIS_CONTROL);
	device->indexes++;

	ret = fimc_is_itf_s_param(device,
		device->indexes, device->lindex, device->hindex);
	if (ret) {
		err("fimc_is_itf_s_param is fail\n");
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_a_param(device, group_id);
	if (ret) {
		merr("fimc_is_itf_a_param is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	group_id = GROUP_ID(device->group_isp.id);
	ret = fimc_is_itf_process_start(device, group_id);
	if (ret) {
		merr("fimc_is_itf_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_ISDEV_DSTART, &device->dis.state);
	dbg_ischain("DIS off\n");

	device->chain1_width = chain1_width;
	device->chain1_height = chain1_height;

p_err:
	return ret;
}

int fimc_is_ischain_dis_tag(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_frame *ldr_frame)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!device);
	BUG_ON(!subdev);
	BUG_ON(!ldr_frame);

	framemgr = GET_SUBDEV_FRAMEMGR(subdev);
	if (!framemgr) {
		merr("framemgr is NULL", device);
		ret = -EINVAL;
		goto p_err;
	}

	if (ldr_frame->shot_ext->request_dis) {
		if (!test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_dis_start(device,
				ldr_frame->shot_ext->dis_bypass);
			if (ret) {
				merr("vdisc_start is fail", device);
				goto p_err;
			}
		}

		framemgr_e_barrier_irqs(framemgr, 0, flags);

		fimc_is_frame_request_head(framemgr, &frame);
		if (frame) {
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[0] =
				frame->dvaddr_buffer[0];
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[1] =
				frame->dvaddr_buffer[1];
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[2] =
				frame->dvaddr_buffer[2];
			frame->stream->findex = ldr_frame->index;
			set_bit(OUT_DIS_FRAME, &ldr_frame->out_flag);
			set_bit(REQ_FRAME, &frame->req_flag);
			fimc_is_frame_trans_req_to_pro(framemgr, frame);
		} else {
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[0] = 0;
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[1] = 0;
			ldr_frame->shot->uctl.scalerUd.disTargetAddress[2] = 0;
			ldr_frame->shot_ext->request_dis = 0;
			fimc_is_gframe_cancel(device->groupmgr,
				&device->group_dis, ldr_frame->fcount);
			mwarn("dis %d frame is drop", device, ldr_frame->fcount);
		}

		framemgr_x_barrier_irqr(framemgr, 0, flags);
	} else {
		if (test_bit(FIMC_IS_ISDEV_DSTART, &subdev->state)) {
			ret = fimc_is_ischain_dis_stop(device);
			if (ret) {
				merr("vdisc_stop is fail", device);
				goto p_err;
			}
		}
		ldr_frame->shot->uctl.scalerUd.disTargetAddress[0] = 0;
		ldr_frame->shot->uctl.scalerUd.disTargetAddress[1] = 0;
		ldr_frame->shot->uctl.scalerUd.disTargetAddress[2] = 0;
		ldr_frame->shot_ext->request_dis = 0;
	}

p_err:
	return ret;
}

int fimc_is_ischain_vdc_s_format(struct fimc_is_device_ischain *device,
	u32 width, u32 height)
{
	int ret = 0;

	device->dis_width = width;
	device->dis_height = height;

	return ret;
}

int fimc_is_ischain_vdo_open(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_dis;

	ret = fimc_is_group_open(groupmgr, group, GROUP_ID_DIS,
		device->instance, vctx, device, fimc_is_ischain_dis_callback);
	if (ret)
		merr("fimc_is_group_open is fail", device);

	return ret;
}

int fimc_is_ischain_vdo_close(struct fimc_is_device_ischain *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_subdev *leader;
	struct fimc_is_queue *queue;

	BUG_ON(!device);

	groupmgr = device->groupmgr;
	group = &device->group_dis;
	leader = &group->leader;
	queue = GET_SRC_QUEUE(vctx);

	ret = fimc_is_ischain_vdo_stop(device, leader, queue);
	if (ret)
		merr("fimc_is_ischain_vdo_stop is fail", device);

	ret = fimc_is_group_close(groupmgr, group, vctx);
	if (ret)
		merr("fimc_is_group_close is fail", device);

	return ret;
}

int fimc_is_ischain_vdo_start(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);
	BUG_ON(!queue);

	groupmgr = device->groupmgr;
	group = &device->group_dis;

	if (test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		merr("already start", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_group_process_start(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	set_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	return ret;
}

int fimc_is_ischain_vdo_stop(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *leader,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!leader);
	BUG_ON(!queue);

	groupmgr = device->groupmgr;
	group = &device->group_dis;

	if (!test_bit(FIMC_IS_ISDEV_DSTART, &leader->state)) {
		mwarn("already stop", device);
		goto p_err;
	}

	ret = fimc_is_group_process_stop(groupmgr, group, queue);
	if (ret) {
		merr("fimc_is_group_process_start is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_itf_process_stop(device, GROUP_ID_DIS);
	if (ret) {
		merr("fimc_is_itf_process_stop is fail", device);
		ret = -EINVAL;
		goto p_err;
	}

	clear_bit(FIMC_IS_ISDEV_DSTART, &leader->state);

p_err:
	pr_info("[DIS:D:%d] %s(%d, %d)\n", device->instance, __func__,
		ret, atomic_read(&group->scount));
	return ret;
}

int fimc_is_ischain_vdo_s_format(struct fimc_is_device_ischain *this,
	u32 width, u32 height)
{
	int ret = 0;

	return ret;
}

int fimc_is_ischain_vdo_buffer_queue(struct fimc_is_device_ischain *device,
	struct fimc_is_queue *queue,
	u32 index)
{
	int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);
	BUG_ON(!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state));

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_dis;

	ret = fimc_is_group_buffer_queue(groupmgr, group, queue, index);
	if (ret) {
		merr("fimc_is_group_buffer_queue is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ischain_vdo_buffer_finish(struct fimc_is_device_ischain *device,
	u32 index)
{
		int ret = 0;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;

	BUG_ON(!device);

#ifdef DBG_STREAMING
	mdbgd_ischain("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_dis;

	ret = fimc_is_group_buffer_finish(groupmgr, group, index);
	if (ret)
		merr("fimc_is_group_buffer_finish is fail(%d)", device, ret);

	return ret;
}

const struct fimc_is_queue_ops fimc_is_ischain_vdo_ops = {
	.start_streaming	= fimc_is_ischain_vdo_start,
	.stop_streaming		= fimc_is_ischain_vdo_stop
};

int fimc_is_ischain_sub_open(struct fimc_is_subdev *subdev,
	struct fimc_is_video_ctx *vctx,
	const struct param_control *init_ctl)
{
	int ret = 0;

	if (test_bit(FIMC_IS_ISDEV_DOPEN, &subdev->state)) {
		warn("subdev%d already open", subdev->entry);
		goto p_err;
	}

	mutex_init(&subdev->mutex_state);
	subdev->vctx = vctx;
	subdev->input.width = 0;
	subdev->input.height = 0;
	subdev->output.width = 0;
	subdev->output.height = 0;

	if (init_ctl) {
		if (init_ctl->cmd != CONTROL_COMMAND_START) {
			err("%d entry is not start", subdev->entry);
			ret = -EINVAL;
			goto p_err;
		}

		if (init_ctl->bypass == CONTROL_BYPASS_ENABLE)
			clear_bit(FIMC_IS_ISDEV_DSTART, &subdev->state);
		else if (init_ctl->bypass == CONTROL_BYPASS_DISABLE)
			set_bit(FIMC_IS_ISDEV_DSTART, &subdev->state);
		else {
			err("%d entry has invalid bypass value(%d)",
				subdev->entry, init_ctl->bypass);
			ret = -EINVAL;
			goto p_err;
		}
	} else {
		/* isp, scc, scp do not use bypass(memory interface)*/
		clear_bit(FIMC_IS_ISDEV_DSTART, &subdev->state);
	}

	set_bit(FIMC_IS_ISDEV_DOPEN, &subdev->state);

p_err:
	return ret;
}

int fimc_is_ischain_sub_close(struct fimc_is_subdev *subdev)
{
	clear_bit(FIMC_IS_ISDEV_DOPEN, &subdev->state);

	return 0;
}

int fimc_is_ischain_sub_start(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_queue *queue)
{
	return 0;
}

int fimc_is_ischain_sub_stop(struct fimc_is_device_ischain *device,
	struct fimc_is_subdev *subdev,
	struct fimc_is_queue *queue)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!queue);

	framemgr = &queue->framemgr;

	framemgr_e_barrier_irqs(framemgr, FMGR_IDX_4, flags);

	if (framemgr->frame_pro_cnt > 0) {
		framemgr_x_barrier_irqr(framemgr, FMGR_IDX_4, flags);
		merr("being processed, can't stop", device);
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_frame_complete_head(framemgr, &frame);
	while (frame) {
		fimc_is_frame_trans_com_to_fre(framemgr, frame);
		fimc_is_frame_complete_head(framemgr, &frame);
	}

	fimc_is_frame_request_head(framemgr, &frame);
	while (frame) {
		fimc_is_frame_trans_req_to_fre(framemgr, frame);
		fimc_is_frame_request_head(framemgr, &frame);
	}

	framemgr_x_barrier_irqr(framemgr, FMGR_IDX_4, flags);

p_err:
	return ret;
}

int fimc_is_subdev_s_format(struct fimc_is_subdev *subdev,
	u32 width, u32 height)
{
	int ret = 0;

	BUG_ON(!subdev);

	subdev->output.width = width;
	subdev->output.height = height;

	return ret;
}

int fimc_is_subdev_buffer_queue(struct fimc_is_subdev *subdev,
	u32 index)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!subdev);
	BUG_ON(index >= FRAMEMGR_MAX_REQUEST);

#ifdef DBG_STREAMING
	dbg_ischain("%s\n", __func__);
#endif

	vctx = subdev->vctx;
	BUG_ON(!vctx);
	framemgr = GET_DST_FRAMEMGR(vctx);

	/* 1. check frame validation */
	frame = &framemgr->frame[index];
	if (!frame) {
		merr("frame is null\n", vctx);
		ret = EINVAL;
		goto p_err;
	}

	if (frame->init == FRAME_UNI_MEM) {
		merr("frame %d is NOT init", vctx, index);
		ret = EINVAL;
		goto p_err;
	}

	/* 2. update frame manager */
	framemgr_e_barrier_irqs(framemgr, index, flags);

	if (frame->state == FIMC_IS_FRAME_STATE_FREE) {
		if (frame->req_flag) {
			warn("%d request flag is not clear(%08X)\n",
				frame->index, (u32)frame->req_flag);
			frame->req_flag = 0;
		}

		fimc_is_frame_trans_fre_to_req(framemgr, frame);
	} else {
		merr("frame(%d) is invalid state(%d)\n", vctx, index, frame->state);
		fimc_is_frame_print_all(framemgr);
		ret = -EINVAL;
	}

	framemgr_x_barrier_irqr(framemgr, index, flags);

p_err:
	return ret;
}

int fimc_is_subdev_buffer_finish(struct fimc_is_subdev *subdev,
	u32 index)
{
	int ret = 0;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!subdev);
	BUG_ON(index >= FRAMEMGR_MAX_REQUEST);

	framemgr = GET_SUBDEV_FRAMEMGR(subdev);
	if (!framemgr) {
		err("framemgr is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	framemgr_e_barrier_irq(framemgr, index);

	fimc_is_frame_complete_head(framemgr, &frame);
	if (frame) {
		if (frame->index == index) {
			fimc_is_frame_trans_com_to_fre(framemgr, frame);
		} else {
			err("buffer index is NOT matched(%d != %d)\n",
				index, frame->index);
			fimc_is_frame_print_all(framemgr);
			ret = -EINVAL;
		}
	} else {
		err("frame is empty from complete");
		fimc_is_frame_print_all(framemgr);
		ret = -EINVAL;
	}

	framemgr_x_barrier_irq(framemgr, index);

p_err:
	return ret;
}

const struct fimc_is_queue_ops fimc_is_ischain_sub_ops = {
	.start_streaming	= fimc_is_ischain_sub_start,
	.stop_streaming		= fimc_is_ischain_sub_stop
};

int fimc_is_ischain_g_capability(struct fimc_is_device_ischain *this,
	u32 user_ptr)
{
	int ret = 0;

	ret = copy_to_user((void *)user_ptr, &this->capability,
		sizeof(struct camera2_sm));

	return ret;
}

int fimc_is_ischain_print_status(struct fimc_is_device_ischain *device)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_subdev *isp;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_interface *itf;

	isp = &device->group_isp.leader;
	vctx = isp->vctx;
	framemgr = GET_SRC_FRAMEMGR(vctx);
	itf = device->interface;

	fimc_is_frame_print_free_list(framemgr);
	fimc_is_frame_print_request_list(framemgr);
	fimc_is_frame_print_process_list(framemgr);
	fimc_is_frame_print_complete_list(framemgr);
	print_fre_work_list(&itf->work_list[INTR_META_DONE]);
	print_req_work_list(&itf->work_list[INTR_META_DONE]);

	return ret;
}

int fimc_is_ischain_3a0_callback(struct fimc_is_device_ischain *device,
	struct fimc_is_frame *frame)
{
	int ret = 0;
	u32 setfile_save;
	u32 crop_width;
	unsigned long flags;
	struct fimc_is_group *group;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_framemgr *ldr_framemgr, *sub_framemgr;
	struct fimc_is_frame *ldr_frame, *sub_frame;

#ifdef DBG_STREAMING
	dbg_ischain("%s\n", __func__);
#endif

	BUG_ON(!device);
	BUG_ON(!frame);

	group = &device->group_3ax;
	vctx = group->leader.vctx;
	if (!vctx) {
		merr("vctx is NULL, critical error", device);
		ret = -EINVAL;
		return ret;
	}

	ldr_framemgr = GET_SRC_FRAMEMGR(vctx);
	sub_framemgr = GET_DST_FRAMEMGR(vctx);

	fimc_is_frame_request_head(ldr_framemgr, &ldr_frame);

	if (!ldr_frame) {
		merr("ldr_frame is NULL", device);
		return -EINVAL;
	}

	if (ldr_frame != frame) {
		merr("ldr_frame is invalid(%X != %X)", device,
			(u32)ldr_frame, (u32)frame);
		return -EINVAL;
	}

	if (!ldr_frame->shot) {
		merr("ldr_frame->shot is NULL", device);
		return -EINVAL;
	}

	if (ldr_frame->init == FRAME_INI_MEM) {
		fimc_is_itf_cfg_mem(device, ldr_frame->dvaddr_shot,
			ldr_frame->shot_size);
		ldr_frame->init = FRAME_CFG_MEM;
	}

#ifdef ENABLE_SETFILE
	if (frame->shot_ext->setfile != device->setfile) {
		setfile_save = device->setfile;
		device->setfile = frame->shot_ext->setfile;

		ret = fimc_is_ischain_s_setfile(device);
		if (ret) {
			err("fimc_is_ischain_s_setfile is fail");
			device->setfile = setfile_save;
			goto p_err;
		}
	}
#endif

#ifdef BAYER_CROP_DZOOM
	crop_width = frame->shot->ctl.scaler.cropRegion[2];
	if (crop_width && (crop_width != device->dzoom_width)) {
		ret = fimc_is_ischain_s_3ax_size(device,
			frame->shot->ctl.scaler.cropRegion[0],
			frame->shot->ctl.scaler.cropRegion[1],
			frame->shot->ctl.scaler.cropRegion[2],
			frame->shot->ctl.scaler.cropRegion[3],
			device->chain0_width, device->chain0_height);
		if (ret) {
			merr("fimc_is_ischain_s_3ax_size is fail:\
				CROP(%d, %d, %d, %d), BDS(%d, %d), fcount(%d)\n",
				device,
				frame->shot->ctl.scaler.cropRegion[0],
				frame->shot->ctl.scaler.cropRegion[1],
				frame->shot->ctl.scaler.cropRegion[2],
				frame->shot->ctl.scaler.cropRegion[3],
				device->chain0_width, device->chain0_height,
				frame->fcount);
			ret = -EINVAL;
			goto p_err;
		}
#ifdef PRINT_DZOOM
		pr_info("[ISP:D:%d] fcount(%d)", device->instance, frame->fcount);
#endif
	}
#endif

	framemgr_e_barrier_irqs(sub_framemgr, 0, flags);

	fimc_is_frame_request_head(sub_framemgr, &sub_frame);
	if (sub_frame) {
		if (!sub_frame->stream) {
			framemgr_x_barrier_irqr(sub_framemgr, 0, flags);
			merr("sub_frame->stream is NULL", device);
			ret = -EINVAL;
			goto p_err;
		}

		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[0] =
			sub_frame->dvaddr_buffer[0];
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[1] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[2] = 0;
		sub_frame->stream->findex = ldr_frame->index;
		set_bit(OUT_3AX_FRAME, &ldr_frame->out_flag);
		set_bit(REQ_FRAME, &sub_frame->req_flag);
		fimc_is_frame_trans_req_to_pro(sub_framemgr, sub_frame);
	} else {
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[0] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[1] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[2] = 0;
		merr("3a0 %d frame is drop", device, ldr_frame->fcount);
	}

	framemgr_x_barrier_irqr(sub_framemgr, 0, flags);

p_err:
	if (ret) {
		merr("shot(index : %d) is skipped(error : %d)", device,
			ldr_frame->index, ret);
	} else {
		framemgr_e_barrier_irqs(ldr_framemgr, 0, flags);
		fimc_is_frame_trans_req_to_pro(ldr_framemgr, ldr_frame);
		framemgr_x_barrier_irqr(ldr_framemgr, 0, flags);
		set_bit(REQ_3A0_SHOT, &ldr_frame->req_flag);
		fimc_is_itf_grp_shot(device, group, ldr_frame);
	}

	return ret;
}

int fimc_is_ischain_3a1_callback(struct fimc_is_device_ischain *device,
	struct fimc_is_frame *frame)
{
	int ret = 0;
	u32 setfile_save;
	u32 crop_width;
	unsigned long flags;
	struct fimc_is_group *group;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_framemgr *ldr_framemgr, *sub_framemgr;
	struct fimc_is_frame *ldr_frame, *sub_frame;

#ifdef DBG_STREAMING
	dbg_ischain("%s\n", __func__);
#endif

	BUG_ON(!device);
	BUG_ON(!frame);

	group = &device->group_3ax;
	vctx = group->leader.vctx;
	if (!vctx) {
		merr("vctx is NULL, critical error", device);
		ret = -EINVAL;
		return ret;
	}

	ldr_framemgr = GET_SRC_FRAMEMGR(vctx);
	sub_framemgr = GET_DST_FRAMEMGR(vctx);

	fimc_is_frame_request_head(ldr_framemgr, &ldr_frame);

	if (!ldr_frame) {
		merr("ldr_frame is NULL", device);
		return -EINVAL;
	}

	if (ldr_frame != frame) {
		merr("ldr_frame is invalid(%X != %X)", device,
			(u32)ldr_frame, (u32)frame);
		return -EINVAL;
	}

	if (!ldr_frame->shot) {
		merr("ldr_frame->shot is NULL", device);
		return -EINVAL;
	}

	if (ldr_frame->init == FRAME_INI_MEM) {
		fimc_is_itf_cfg_mem(device, ldr_frame->dvaddr_shot,
			ldr_frame->shot_size);
		ldr_frame->init = FRAME_CFG_MEM;
	}

#ifdef ENABLE_SETFILE
	if (frame->shot_ext->setfile != device->setfile) {
		setfile_save = device->setfile;
		device->setfile = frame->shot_ext->setfile;

		ret = fimc_is_ischain_s_setfile(device);
		if (ret) {
			err("fimc_is_ischain_s_setfile is fail");
			device->setfile = setfile_save;
			goto p_err;
		}
	}
#endif

#ifdef ENABLE_FAST_SHOT
		if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &group->state))
			memcpy(&frame->shot->ctl.aa, &group->fast_ctl.aa,
				sizeof(struct camera2_aa_ctl));
			memcpy(&frame->shot->ctl.scaler, &group->fast_ctl.scaler,
				sizeof(struct camera2_scaler_ctl));
#endif

#ifdef BAYER_CROP_DZOOM
	crop_width = frame->shot->ctl.scaler.cropRegion[2];
	if (crop_width && (crop_width != device->dzoom_width)) {
		ret = fimc_is_ischain_s_3ax_size(device,
			frame->shot->ctl.scaler.cropRegion[0],
			frame->shot->ctl.scaler.cropRegion[1],
			frame->shot->ctl.scaler.cropRegion[2],
			frame->shot->ctl.scaler.cropRegion[3],
			device->chain0_width, device->chain0_height);
		if (ret) {
			merr("fimc_is_ischain_s_3ax_size is fail:\
				CROP(%d, %d, %d, %d), BDS(%d, %d), fcount(%d)\n",
				device,
				frame->shot->ctl.scaler.cropRegion[0],
				frame->shot->ctl.scaler.cropRegion[1],
				frame->shot->ctl.scaler.cropRegion[2],
				frame->shot->ctl.scaler.cropRegion[3],
				device->chain0_width, device->chain0_height,
				frame->fcount);
			ret = -EINVAL;
			goto p_err;
		}
#ifdef PRINT_DZOOM
		pr_info("[ISP:D:%d] fcount(%d)", device->instance, frame->fcount);
#endif
	}
#endif

	framemgr_e_barrier_irqs(sub_framemgr, FMGR_IDX_8, flags);

	fimc_is_frame_request_head(sub_framemgr, &sub_frame);
	if (sub_frame) {
		if (!sub_frame->stream) {
			framemgr_x_barrier_irqr(sub_framemgr, 0, flags);
			merr("sub_frame->stream is NULL", device);
			ret = -EINVAL;
			goto p_err;
		}

		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[0] =
			sub_frame->dvaddr_buffer[0];
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[1] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[2] = 0;
		sub_frame->stream->findex = ldr_frame->index;
		set_bit(OUT_3AX_FRAME, &ldr_frame->out_flag);
		set_bit(REQ_FRAME, &sub_frame->req_flag);
		fimc_is_frame_trans_req_to_pro(sub_framemgr, sub_frame);
	} else {
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[0] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[1] = 0;
		ldr_frame->shot->uctl.scalerUd.ispTargetAddress[2] = 0;
		merr("3a1 %d frame is drop", device, ldr_frame->fcount);
	}

	framemgr_x_barrier_irqr(sub_framemgr, FMGR_IDX_8, flags);

p_err:
	if (ret) {
		merr("shot(index : %d) is skipped(error : %d)", device,
			ldr_frame->index, ret);
	} else {
		framemgr_e_barrier_irqs(ldr_framemgr, 0, flags);
		fimc_is_frame_trans_req_to_pro(ldr_framemgr, ldr_frame);
		framemgr_x_barrier_irqr(ldr_framemgr, 0, flags);
		set_bit(REQ_3A1_SHOT, &ldr_frame->req_flag);
		fimc_is_itf_grp_shot(device, group, ldr_frame);
	}

	return ret;
}

int fimc_is_ischain_isp_callback(struct fimc_is_device_ischain *device,
	struct fimc_is_frame *check_frame)
{
	int ret = 0;
#if defined(SCALER_CROP_DZOOM)
	u32 crop_width;
#elif defined(BAYER_CROP_DZOOM)
	u32 bds_width, bds_height;
#endif
	unsigned long flags;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;
	struct fimc_is_subdev *isp, *scc, *dis, *dnr, *scp, *fd;
	struct fimc_is_core *core;
	struct fimc_is_device_sensor *sensor;

	BUG_ON(!device);
	BUG_ON(!check_frame);
	BUG_ON(device->instance_sensor >= FIMC_IS_MAX_NODES);

#ifdef DBG_STREAMING
	mdbgd_isp("%s\n", device, __func__);
#endif

	groupmgr = device->groupmgr;
	group = &device->group_isp;
	core = (struct fimc_is_core *)device->interface->core;
	sensor = &core->sensor[device->instance_sensor];
	isp = &group->leader;
	scc = group->subdev[ENTRY_SCALERC];
	dis = group->subdev[ENTRY_DIS];
	dnr = group->subdev[ENTRY_TDNR];
	scp = group->subdev[ENTRY_SCALERP];
	fd = group->subdev[ENTRY_LHFD];
	vctx = isp->vctx;
	framemgr = GET_SRC_FRAMEMGR(vctx);

	/*
	   BE CAREFUL WITH THIS
	1. buffer queue, all compoenent stop, so it's good
	2. interface callback, all component will be stop until new one is came
	   therefore, i expect lock object is not necessary in here
	*/

	BUG_ON(!framemgr);

	fimc_is_frame_request_head(framemgr, &frame);

	if (frame != check_frame) {
		merr("grp_frame is invalid(%X != %X)", device,
			(u32)frame, (u32)frame);
		return -EINVAL;
	}

	if (frame->init == FRAME_INI_MEM) {
		fimc_is_itf_cfg_mem(device, frame->dvaddr_shot,
			frame->shot_size);
		frame->init = FRAME_CFG_MEM;
	}

#ifdef ENABLE_DRC
	if (frame->shot_ext->drc_bypass) {
		if (test_bit(FIMC_IS_ISDEV_DSTART, &device->drc.state)) {
			ret = fimc_is_ischain_drc_bypass(device, true);
			if (ret) {
				err("fimc_is_ischain_drc_bypass(1) is fail");
				goto exit;
			}
		}
	} else {
		if (!test_bit(FIMC_IS_ISDEV_DSTART, &device->drc.state)) {
			ret = fimc_is_ischain_drc_bypass(device, false);
			if (ret) {
				err("fimc_is_ischain_drc_bypass(0) is fail");
				goto exit;
			}
		}
	}
#endif

#ifdef ENABLE_TDNR
	if (dnr) {
		if (frame->shot_ext->dnr_bypass) {
			if (test_bit(FIMC_IS_ISDEV_DSTART, &dnr->state)) {
				ret = fimc_is_ischain_dnr_bypass(device, true);
				if (ret) {
					merr("dnr_bypass(1) is fail", device);
					goto exit;
				}
			}
		} else {
			if (!test_bit(FIMC_IS_ISDEV_DSTART, &dnr->state)) {
				ret = fimc_is_ischain_dnr_bypass(device, false);
				if (ret) {
					merr("dnr_bypass(0) is fail", device);
					goto exit;
				}
			}
		}
	}
#endif

#ifdef ENABLE_FD
	if (fd) {
		if (frame->shot_ext->fd_bypass) {
			if (test_bit(FIMC_IS_ISDEV_DSTART, &fd->state)) {
				ret = fimc_is_ischain_fd_bypass(device, true);
				if (ret) {
					merr("fd_bypass(1) is fail", device);
					goto exit;
				}
			}
		} else {
			if (!test_bit(FIMC_IS_ISDEV_DSTART, &fd->state)) {
				ret = fimc_is_ischain_fd_bypass(device, false);
				if (ret) {
					merr("fd_bypass(0) is fail", device);
					goto exit;
				}
			}
		}
	}
#endif


#ifdef SCALER_CROP_DZOOM
	crop_width = frame->shot->ctl.scaler.cropRegion[2];
	if (crop_width && (crop_width != device->dzoom_width)) {
		ret = fimc_is_ischain_s_dzoom(device,
			frame->shot->ctl.scaler.cropRegion[0],
			frame->shot->ctl.scaler.cropRegion[1],
			frame->shot->ctl.scaler.cropRegion[2]);
		if (ret) {
			err("fimc_is_ischain_s_dzoom(%d, %d, %d) is fail",
				frame->shot->ctl.scaler.cropRegion[0],
				frame->shot->ctl.scaler.cropRegion[1],
				frame->shot->ctl.scaler.cropRegion[2]);
			goto exit;
		}
	}
#endif

#ifdef BAYER_CROP_DZOOM
	bds_width = frame->shot->udm.bayer.width;
	bds_height = frame->shot->udm.bayer.height;
	if (bds_width && bds_height
	&& (bds_width <= device->chain0_width)
	&& ((bds_width != device->bds_width) || (bds_height != device->bds_height))) {
		ret = fimc_is_ischain_s_bayer_dzoom(device,
			frame->shot->udm.bayer.width,
			frame->shot->udm.bayer.height);
		if (ret) {
			merr("fimc_is_ischain_s_bayer_dzoom is fail:\
				BDS(%d, %d), fcount(%d)\n",
				device,
				frame->shot->udm.bayer.width,
				frame->shot->udm.bayer.height,
				frame->fcount);
			ret = -EINVAL;
			goto exit;
		}
#ifdef PRINT_DZOOM
		pr_info("[ISP:D:%d] fcount(%d)", device->instance, frame->fcount);
#endif
	}
#endif

	if (scc) {
		ret = fimc_is_ischain_scc_tag(device, scc, frame);
		if (ret) {
			merr("scc_tag fail(%d)", device, ret);
			goto exit;
		}
	}

	if (dis) {
		ret = fimc_is_ischain_dis_tag(device, dis, frame);
		if (ret) {
			merr("vdc_tag fail(%d)", device, ret);
			goto exit;
		}
	}

	if (scp) {
		ret = fimc_is_ischain_scp_tag(device, scp, frame);
		if (ret) {
			merr("scp_tag fail(%d)", device, ret);
			goto exit;
		}
	}

exit:
	if (ret) {
		merr("GRP%d shot(index : %d) is skipped(error : %d)",
			device, group->id, frame->index, ret);
	} else {
		framemgr_e_barrier_irqs(framemgr, 0, flags);
		fimc_is_frame_trans_req_to_pro(framemgr, frame);
		framemgr_x_barrier_irqr(framemgr, 0, flags);
		set_bit(REQ_ISP_SHOT, &frame->req_flag);
		fimc_is_itf_grp_shot(device, group, frame);
	}

	return ret;
}

int fimc_is_ischain_dis_callback(struct fimc_is_device_ischain *device,
	struct fimc_is_frame *check_frame)
{
	int ret = 0;
	unsigned long flags;
	bool dis_req, scp_req;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_framemgr *ldr_framemgr;
	struct fimc_is_frame *ldr_frame;
	struct fimc_is_subdev *leader, *dnr, *scp, *fd;
	struct fimc_is_group *group;

#ifdef DBG_STREAMING
	dbg_ischain("%s\n", __func__);
#endif

	BUG_ON(!device);
	BUG_ON(!check_frame);

	group = &device->group_dis;
	vctx = group->leader.vctx;
	ldr_framemgr = GET_SRC_FRAMEMGR(vctx);
	dis_req = scp_req = false;
	leader = &group->leader;
	dnr = group->subdev[ENTRY_TDNR];
	scp = group->subdev[ENTRY_SCALERP];
	fd = group->subdev[ENTRY_LHFD];

	fimc_is_frame_request_head(ldr_framemgr, &ldr_frame);

	if (ldr_frame != check_frame) {
		merr("grp_frame is invalid(%X != %X)", device,
			(u32)ldr_frame, (u32)check_frame);
		return -EINVAL;
	}

#ifdef ENABLE_TDNR
	if (dnr) {
		if (ldr_frame->shot_ext->dnr_bypass) {
			if (test_bit(FIMC_IS_ISDEV_DSTART, &dnr->state)) {
				ret = fimc_is_ischain_dnr_bypass(device, true);
				if (ret) {
					merr("dnr_bypass(1) is fail", device);
					goto exit;
				}
			}
		} else {
			if (!test_bit(FIMC_IS_ISDEV_DSTART, &dnr->state)) {
				ret = fimc_is_ischain_dnr_bypass(device, false);
				if (ret) {
					merr("dnr_bypass(0) is fail", device);
					goto exit;
				}
			}
		}
	}
#endif

#ifdef ENABLE_FD
	if (fd) {
		if (ldr_frame->shot_ext->fd_bypass) {
			if (test_bit(FIMC_IS_ISDEV_DSTART, &fd->state)) {
				ret = fimc_is_ischain_fd_bypass(device, true);
				if (ret) {
					merr("fd_bypass(1) is fail", device);
					goto exit;
				}
			}
		} else {
			if (!test_bit(FIMC_IS_ISDEV_DSTART, &fd->state)) {
				ret = fimc_is_ischain_fd_bypass(device, false);
				if (ret) {
					merr("fd_bypass(0) is fail", device);
					goto exit;
				}
			}
		}
	}
#endif

	if (scp) {
		ret = fimc_is_ischain_scp_tag(device, scp, ldr_frame);
		if (ret) {
			merr("scp_tag fail(%d)", device, ret);
			goto exit;
		}
	}

exit:
	if (ret) {
		err("shot(index : %d) is skipped(error : %d)",
			ldr_frame->index, ret);
	} else {
		framemgr_e_barrier_irqs(ldr_framemgr, 0, flags);
		fimc_is_frame_trans_req_to_pro(ldr_framemgr, ldr_frame);
		framemgr_x_barrier_irqr(ldr_framemgr, 0, flags);
		set_bit(REQ_DIS_SHOT, &ldr_frame->req_flag);
		fimc_is_itf_grp_shot(device, group, ldr_frame);
	}

	return ret;
}

int fimc_is_ischain_camctl(struct fimc_is_device_ischain *this,
	struct fimc_is_frame *frame,
	u32 fcount)
{
	int ret = 0;
#ifdef ENABLE_SENSOR_DRIVER
	struct fimc_is_interface *itf;
	struct camera2_uctl *applied_ctl;

	struct camera2_sensor_ctl *isp_sensor_ctl;
	struct camera2_lens_ctl *isp_lens_ctl;
	struct camera2_flash_ctl *isp_flash_ctl;

	u32 index;

#ifdef DBG_STREAMING
	dbg_ischain("%s\n", __func__);
#endif

	itf = this->interface;
	isp_sensor_ctl = &itf->isp_peri_ctl.sensorUd.ctl;
	isp_lens_ctl = &itf->isp_peri_ctl.lensUd.ctl;
	isp_flash_ctl = &itf->isp_peri_ctl.flashUd.ctl;

	/*lens*/
	index = (fcount + 0) & SENSOR_MAX_CTL_MASK;
	applied_ctl = &this->peri_ctls[index];
	applied_ctl->lensUd.ctl.focusDistance = isp_lens_ctl->focusDistance;

	/*sensor*/
	index = (fcount + 1) & SENSOR_MAX_CTL_MASK;
	applied_ctl = &this->peri_ctls[index];
	applied_ctl->sensorUd.ctl.exposureTime = isp_sensor_ctl->exposureTime;
	applied_ctl->sensorUd.ctl.frameDuration = isp_sensor_ctl->frameDuration;
	applied_ctl->sensorUd.ctl.sensitivity = isp_sensor_ctl->sensitivity;

	/*flash*/
	index = (fcount + 0) & SENSOR_MAX_CTL_MASK;
	applied_ctl = &this->peri_ctls[index];
	applied_ctl->flashUd.ctl.flashMode = isp_flash_ctl->flashMode;
	applied_ctl->flashUd.ctl.firingPower = isp_flash_ctl->firingPower;
	applied_ctl->flashUd.ctl.firingTime = isp_flash_ctl->firingTime;
#endif
	return ret;
}

int fimc_is_ischain_tag(struct fimc_is_device_ischain *ischain,
	struct fimc_is_frame *frame)
{
	int ret = 0;
#ifdef ENABLE_SENSOR_DRIVER
	struct camera2_uctl *applied_ctl;
	struct timeval curtime;
	u32 fcount;

	fcount = frame->fcount;
	applied_ctl = &ischain->peri_ctls[fcount & SENSOR_MAX_CTL_MASK];

	do_gettimeofday(&curtime);

	/* Request */
	frame->shot->dm.request.frameCount = fcount;

	/* Lens */
	frame->shot->dm.lens.focusDistance =
		applied_ctl->lensUd.ctl.focusDistance;

	/* Sensor */
	frame->shot->dm.sensor.exposureTime =
		applied_ctl->sensorUd.ctl.exposureTime;
	frame->shot->dm.sensor.sensitivity =
		applied_ctl->sensorUd.ctl.sensitivity;
	frame->shot->dm.sensor.frameDuration =
		applied_ctl->sensorUd.ctl.frameDuration;
	frame->shot->dm.sensor.timeStamp =
		(uint64_t)curtime.tv_sec*1000000 + curtime.tv_usec;

	/* Flash */
	frame->shot->dm.flash.flashMode =
		applied_ctl->flashUd.ctl.flashMode;
	frame->shot->dm.flash.firingPower =
		applied_ctl->flashUd.ctl.firingPower;
	frame->shot->dm.flash.firingTime =
		applied_ctl->flashUd.ctl.firingTime;
#else
	struct timeval curtime;

	do_gettimeofday(&curtime);

	frame->shot->dm.request.frameCount = frame->fcount;
	frame->shot->dm.sensor.timeStamp =
		(uint64_t)curtime.tv_sec*1000000 + curtime.tv_usec;
#endif
	return ret;
}
