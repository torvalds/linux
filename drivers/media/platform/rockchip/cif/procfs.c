// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/v4l2-mediabus.h>

#include "hw.h"
#include "dev.h"
#include "procfs.h"

#ifdef CONFIG_PROC_FS

static const struct {
	const char *name;
	u32 mbus_code;
} mbus_formats[] = {
	/* media bus code */
	{ "RGB444_1X12", MEDIA_BUS_FMT_RGB444_1X12 },
	{ "RGB444_2X8_PADHI_BE", MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE },
	{ "RGB444_2X8_PADHI_LE", MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE },
	{ "RGB555_2X8_PADHI_BE", MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE },
	{ "RGB555_2X8_PADHI_LE", MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE },
	{ "RGB565_1X16", MEDIA_BUS_FMT_RGB565_1X16 },
	{ "BGR565_2X8_BE", MEDIA_BUS_FMT_BGR565_2X8_BE },
	{ "BGR565_2X8_LE", MEDIA_BUS_FMT_BGR565_2X8_LE },
	{ "RGB565_2X8_BE", MEDIA_BUS_FMT_RGB565_2X8_BE },
	{ "RGB565_2X8_LE", MEDIA_BUS_FMT_RGB565_2X8_LE },
	{ "RGB666_1X18", MEDIA_BUS_FMT_RGB666_1X18 },
	{ "RBG888_1X24", MEDIA_BUS_FMT_RBG888_1X24 },
	{ "RGB666_1X24_CPADHI", MEDIA_BUS_FMT_RGB666_1X24_CPADHI },
	{ "RGB666_1X7X3_SPWG", MEDIA_BUS_FMT_RGB666_1X7X3_SPWG },
	{ "BGR888_1X24", MEDIA_BUS_FMT_BGR888_1X24 },
	{ "GBR888_1X24", MEDIA_BUS_FMT_GBR888_1X24 },
	{ "RGB888_1X24", MEDIA_BUS_FMT_RGB888_1X24 },
	{ "RGB888_2X12_BE", MEDIA_BUS_FMT_RGB888_2X12_BE },
	{ "RGB888_2X12_LE", MEDIA_BUS_FMT_RGB888_2X12_LE },
	{ "RGB888_1X7X4_SPWG", MEDIA_BUS_FMT_RGB888_1X7X4_SPWG },
	{ "RGB888_1X7X4_JEIDA", MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA },
	{ "ARGB8888_1X32", MEDIA_BUS_FMT_ARGB8888_1X32 },
	{ "RGB888_1X32_PADHI", MEDIA_BUS_FMT_RGB888_1X32_PADHI },
	{ "RGB101010_1X30", MEDIA_BUS_FMT_RGB101010_1X30 },
	{ "RGB121212_1X36", MEDIA_BUS_FMT_RGB121212_1X36 },
	{ "RGB161616_1X48", MEDIA_BUS_FMT_RGB161616_1X48 },
	{ "Y8_1X8", MEDIA_BUS_FMT_Y8_1X8 },
	{ "UV8_1X8", MEDIA_BUS_FMT_UV8_1X8 },
	{ "UYVY8_1_5X8", MEDIA_BUS_FMT_UYVY8_1_5X8 },
	{ "VYUY8_1_5X8", MEDIA_BUS_FMT_VYUY8_1_5X8 },
	{ "YUYV8_1_5X8", MEDIA_BUS_FMT_YUYV8_1_5X8 },
	{ "YVYU8_1_5X8", MEDIA_BUS_FMT_YVYU8_1_5X8 },
	{ "UYVY8_2X8", MEDIA_BUS_FMT_UYVY8_2X8 },
	{ "VYUY8_2X8", MEDIA_BUS_FMT_VYUY8_2X8 },
	{ "YUYV8_2X8", MEDIA_BUS_FMT_YUYV8_2X8 },
	{ "YVYU8_2X8", MEDIA_BUS_FMT_YVYU8_2X8 },
	{ "Y10_1X10", MEDIA_BUS_FMT_Y10_1X10 },
	{ "Y10_2X8_PADHI_LE", MEDIA_BUS_FMT_Y10_2X8_PADHI_LE },
	{ "UYVY10_2X10", MEDIA_BUS_FMT_UYVY10_2X10 },
	{ "VYUY10_2X10", MEDIA_BUS_FMT_VYUY10_2X10 },
	{ "YUYV10_2X10", MEDIA_BUS_FMT_YUYV10_2X10 },
	{ "YVYU10_2X10", MEDIA_BUS_FMT_YVYU10_2X10 },
	{ "Y12_1X12", MEDIA_BUS_FMT_Y12_1X12 },
	{ "UYVY12_2X12", MEDIA_BUS_FMT_UYVY12_2X12 },
	{ "VYUY12_2X12", MEDIA_BUS_FMT_VYUY12_2X12 },
	{ "YUYV12_2X12", MEDIA_BUS_FMT_YUYV12_2X12 },
	{ "YVYU12_2X12", MEDIA_BUS_FMT_YVYU12_2X12 },
	{ "UYVY8_1X16", MEDIA_BUS_FMT_UYVY8_1X16 },
	{ "VYUY8_1X16", MEDIA_BUS_FMT_VYUY8_1X16 },
	{ "YUYV8_1X16", MEDIA_BUS_FMT_YUYV8_1X16 },
	{ "YVYU8_1X16", MEDIA_BUS_FMT_YVYU8_1X16 },
	{ "YDYUYDYV8_1X16", MEDIA_BUS_FMT_YDYUYDYV8_1X16 },
	{ "UYVY10_1X20", MEDIA_BUS_FMT_UYVY10_1X20 },
	{ "VYUY10_1X20", MEDIA_BUS_FMT_VYUY10_1X20 },
	{ "YUYV10_1X20", MEDIA_BUS_FMT_YUYV10_1X20 },
	{ "YVYU10_1X20", MEDIA_BUS_FMT_YVYU10_1X20 },
	{ "VUY8_1X24", MEDIA_BUS_FMT_VUY8_1X24 },
	{ "YUV8_1X24", MEDIA_BUS_FMT_YUV8_1X24 },
	{ "UYYVYY8_0_5X24", MEDIA_BUS_FMT_UYYVYY8_0_5X24 },
	{ "UYVY12_1X24", MEDIA_BUS_FMT_UYVY12_1X24 },
	{ "VYUY12_1X24", MEDIA_BUS_FMT_VYUY12_1X24 },
	{ "YUYV12_1X24", MEDIA_BUS_FMT_YUYV12_1X24 },
	{ "YVYU12_1X24", MEDIA_BUS_FMT_YVYU12_1X24 },
	{ "YUV10_1X30", MEDIA_BUS_FMT_YUV10_1X30 },
	{ "UYYVYY10_0_5X30", MEDIA_BUS_FMT_UYYVYY10_0_5X30 },
	{ "AYUV8_1X32", MEDIA_BUS_FMT_AYUV8_1X32 },
	{ "UYYVYY12_0_5X36", MEDIA_BUS_FMT_UYYVYY12_0_5X36 },
	{ "YUV12_1X36", MEDIA_BUS_FMT_YUV12_1X36 },
	{ "YUV16_1X48", MEDIA_BUS_FMT_YUV16_1X48 },
	{ "UYYVYY16_0_5X48", MEDIA_BUS_FMT_UYYVYY16_0_5X48 },
	{ "SBGGR8_1X8", MEDIA_BUS_FMT_SBGGR8_1X8 },
	{ "SGBRG8_1X8", MEDIA_BUS_FMT_SGBRG8_1X8 },
	{ "SGRBG8_1X8", MEDIA_BUS_FMT_SGRBG8_1X8 },
	{ "SRGGB8_1X8", MEDIA_BUS_FMT_SRGGB8_1X8 },
	{ "SBGGR10_ALAW8_1X8", MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8 },
	{ "SGBRG10_ALAW8_1X8", MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8 },
	{ "SGRBG10_ALAW8_1X8", MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8 },
	{ "SRGGB10_ALAW8_1X8", MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8 },
	{ "SBGGR10_DPCM8_1X8", MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8 },
	{ "SGBRG10_DPCM8_1X8", MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8 },
	{ "SGRBG10_DPCM8_1X8", MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8 },
	{ "SRGGB10_DPCM8_1X8", MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8 },
	{ "SBGGR10_2X8_PADHI_BE", MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE },
	{ "SBGGR10_2X8_PADHI_LE", MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE },
	{ "SBGGR10_2X8_PADLO_BE", MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE },
	{ "SBGGR10_2X8_PADLO_LE", MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE },
	{ "SBGGR10_1X10", MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ "SGBRG10_1X10", MEDIA_BUS_FMT_SGBRG10_1X10 },
	{ "SGRBG10_1X10", MEDIA_BUS_FMT_SGRBG10_1X10 },
	{ "SRGGB10_1X10", MEDIA_BUS_FMT_SRGGB10_1X10 },
	{ "SBGGR12_1X12", MEDIA_BUS_FMT_SBGGR12_1X12 },
	{ "SGBRG12_1X12", MEDIA_BUS_FMT_SGBRG12_1X12 },
	{ "SGRBG12_1X12", MEDIA_BUS_FMT_SGRBG12_1X12 },
	{ "SRGGB12_1X12", MEDIA_BUS_FMT_SRGGB12_1X12 },
	{ "SBGGR14_1X14", MEDIA_BUS_FMT_SBGGR14_1X14 },
	{ "SGBRG14_1X14", MEDIA_BUS_FMT_SGBRG14_1X14 },
	{ "SGRBG14_1X14", MEDIA_BUS_FMT_SGRBG14_1X14 },
	{ "SRGGB14_1X14", MEDIA_BUS_FMT_SRGGB14_1X14 },
	{ "SBGGR16_1X16", MEDIA_BUS_FMT_SBGGR16_1X16 },
	{ "SGBRG16_1X16", MEDIA_BUS_FMT_SGBRG16_1X16 },
	{ "SGRBG16_1X16", MEDIA_BUS_FMT_SGRBG16_1X16 },
	{ "SRGGB16_1X16", MEDIA_BUS_FMT_SRGGB16_1X16 },
	{ "JPEG_1X8", MEDIA_BUS_FMT_JPEG_1X8 },
	{ "S5C_UYVY_JPEG_1X8", MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8 },
	{ "AHSV8888_1X32", MEDIA_BUS_FMT_AHSV8888_1X32 },
	{ "FIXED", MEDIA_BUS_FMT_FIXED },
	{ "Y8", MEDIA_BUS_FMT_Y8_1X8 },
	{ "Y10", MEDIA_BUS_FMT_Y10_1X10 },
	{ "Y12", MEDIA_BUS_FMT_Y12_1X12 },
	{ "YUYV", MEDIA_BUS_FMT_YUYV8_1X16 },
	{ "YUYV1_5X8", MEDIA_BUS_FMT_YUYV8_1_5X8 },
	{ "YUYV2X8", MEDIA_BUS_FMT_YUYV8_2X8 },
	{ "UYVY", MEDIA_BUS_FMT_UYVY8_1X16 },
	{ "UYVY1_5X8", MEDIA_BUS_FMT_UYVY8_1_5X8 },
	{ "UYVY2X8", MEDIA_BUS_FMT_UYVY8_2X8 },
	{ "VUY24", MEDIA_BUS_FMT_VUY8_1X24 },
	{ "SBGGR8", MEDIA_BUS_FMT_SBGGR8_1X8 },
	{ "SGBRG8", MEDIA_BUS_FMT_SGBRG8_1X8 },
	{ "SGRBG8", MEDIA_BUS_FMT_SGRBG8_1X8 },
	{ "SRGGB8", MEDIA_BUS_FMT_SRGGB8_1X8 },
	{ "SBGGR10", MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ "SGBRG10", MEDIA_BUS_FMT_SGBRG10_1X10 },
	{ "SGRBG10", MEDIA_BUS_FMT_SGRBG10_1X10 },
	{ "SRGGB10", MEDIA_BUS_FMT_SRGGB10_1X10 },
	{ "SBGGR10_DPCM8", MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8 },
	{ "SGBRG10_DPCM8", MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8 },
	{ "SGRBG10_DPCM8", MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8 },
	{ "SRGGB10_DPCM8", MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8 },
	{ "SBGGR12", MEDIA_BUS_FMT_SBGGR12_1X12 },
	{ "SGBRG12", MEDIA_BUS_FMT_SGBRG12_1X12 },
	{ "SGRBG12", MEDIA_BUS_FMT_SGRBG12_1X12 },
	{ "SRGGB12", MEDIA_BUS_FMT_SRGGB12_1X12 },
	{ "AYUV32", MEDIA_BUS_FMT_AYUV8_1X32 },
	{ "RBG24", MEDIA_BUS_FMT_RBG888_1X24 },
	{ "RGB32", MEDIA_BUS_FMT_RGB888_1X32_PADHI },
	{ "ARGB32", MEDIA_BUS_FMT_ARGB8888_1X32 },

	/* v4l2 fourcc code */
	{ "NV16", V4L2_PIX_FMT_NV16},
	{ "NV61", V4L2_PIX_FMT_NV61},
	{ "NV12", V4L2_PIX_FMT_NV12},
	{ "NV21", V4L2_PIX_FMT_NV21},
	{ "YUYV", V4L2_PIX_FMT_YUYV},
	{ "YVYU", V4L2_PIX_FMT_YVYU},
	{ "UYVY", V4L2_PIX_FMT_UYVY},
	{ "VYUY", V4L2_PIX_FMT_VYUY},
	{ "RGB3", V4L2_PIX_FMT_RGB24},
	{ "RGBP", V4L2_PIX_FMT_RGB565},
	{ "BGRH", V4L2_PIX_FMT_BGR666},
	{ "RGGB", V4L2_PIX_FMT_SRGGB8},
	{ "GRBG", V4L2_PIX_FMT_SGRBG8},
	{ "GBRG", V4L2_PIX_FMT_SGBRG8},
	{ "BA81", V4L2_PIX_FMT_SBGGR8},
	{ "RG10", V4L2_PIX_FMT_SRGGB10},
	{ "BA10", V4L2_PIX_FMT_SGRBG10},
	{ "GB10", V4L2_PIX_FMT_SGBRG10},
	{ "BG10", V4L2_PIX_FMT_SBGGR10},
	{ "RG12", V4L2_PIX_FMT_SRGGB12},
	{ "BA12", V4L2_PIX_FMT_SGRBG12},
	{ "GB12", V4L2_PIX_FMT_SGBRG12},
	{ "BG12", V4L2_PIX_FMT_SBGGR12},
	{ "BYR2", V4L2_PIX_FMT_SBGGR16},
	{ "Y16 ", V4L2_PIX_FMT_Y16},
};

static const char *rkcif_pixelcode_to_string(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mbus_formats); ++i) {
		if (mbus_formats[i].mbus_code == mbus_code)
			return mbus_formats[i].name;
	}

	return "unknown";
}

static const char *rkcif_get_monitor_mode(enum rkcif_monitor_mode monitor_mode)
{
	switch (monitor_mode) {
	case RKCIF_MONITOR_MODE_IDLE:
		return "idle";
	case RKCIF_MONITOR_MODE_CONTINUE:
		return "continue";
	case RKCIF_MONITOR_MODE_TRIGGER:
		return "trigger";
	case RKCIF_MONITOR_MODE_HOTPLUG:
		return "hotplug";
	default:
		return "unknown";
	}
}

static void rkcif_show_mixed_info(struct rkcif_device *dev, struct seq_file *f)
{
	enum rkcif_monitor_mode monitor_mode;

	seq_printf(f, "Driver Version:v%02x.%02x.%02x\n",
		   RKCIF_DRIVER_VERSION >> 16,
		   (RKCIF_DRIVER_VERSION & 0xff00) >> 8,
		    RKCIF_DRIVER_VERSION & 0x00ff);
	seq_printf(f, "Work Mode:%s\n",
		   dev->workmode == RKCIF_WORKMODE_ONEFRAME ? "one frame" :
		   dev->workmode == RKCIF_WORKMODE_PINGPONG ? "ping pong" : "line loop");

	monitor_mode = dev->reset_watchdog_timer.monitor_mode;
	seq_printf(f, "Monitor Mode:%s\n",
		   rkcif_get_monitor_mode(monitor_mode));
}

static void rkcif_show_clks(struct rkcif_device *dev, struct seq_file *f)
{
	int i;
	struct rkcif_hw *hw = dev->hw_dev;

	for (i = 0; i < hw->clk_size; i++) {
		seq_printf(f, "%s:%ld\n",
			   hw->match_data->clks[i],
			   clk_get_rate(hw->clks[i]));
	}
}

static void rkcif_show_format(struct rkcif_device *dev, struct seq_file *f)
{
	struct rkcif_stream *stream = &dev->stream[0];
	struct rkcif_pipeline *pipe = &dev->pipe;
	struct rkcif_sensor_info *sensor = &dev->terminal_sensor;
	struct v4l2_rect *rect = &sensor->raw_rect;
	struct v4l2_subdev_frame_interval *interval = &sensor->fi;
	struct v4l2_subdev_selection *sel = &sensor->selection;
	u32 i, mbus_flags;
	u64 fps, timestamp0, timestamp1;
	unsigned long flags;
	u32 time_val = 0;

	if (atomic_read(&pipe->stream_cnt) < 1)
		return;

	if (sensor) {
		seq_puts(f, "Input Info:\n");

		seq_printf(f, "\tsrc subdev:%s\n", sensor->sd->name);
		mbus_flags = sensor->mbus.flags;
		if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		    sensor->mbus.type == V4L2_MBUS_BT656) {
			seq_printf(f, "\tinterface:%s\n",
				   sensor->mbus.type == V4L2_MBUS_PARALLEL ? "BT601" : "BT656/BT1120");
			seq_printf(f, "\thref_pol:%s\n",
				   mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH ? "high active" : "low active");
			seq_printf(f, "\tvsync_pol:%s\n",
				   mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH ? "high active" : "low active");
		} else {
			seq_printf(f, "\tinterface:%s\n",
				   sensor->mbus.type == V4L2_MBUS_CSI2_DPHY ? "mipi csi2 dphy" :
				   sensor->mbus.type == V4L2_MBUS_CSI2_CPHY ? "mipi csi2 cphy" :
				   sensor->mbus.type == V4L2_MBUS_CCP2 ? "lvds" : "unknown");
			seq_printf(f, "\tlanes:%d\n", sensor->lanes);
			seq_puts(f, "\tvc channel:");
			if (mbus_flags & V4L2_MBUS_CSI2_CHANNELS) {
				for (i = 0; i < 4; i++) {
					if ((mbus_flags >> (4 + i)) & 0x1)
						seq_printf(f, " %d", i);
				}
				seq_puts(f, "\n");
			} else {
				seq_puts(f, "unknown\n");
			}
		}

		seq_printf(f, "\thdr mode: %s\n",
			   dev->hdr.hdr_mode == NO_HDR ? "normal" :
			   dev->hdr.hdr_mode == HDR_COMPR ? "hdr_compr" :
			   dev->hdr.hdr_mode == HDR_X2 ? "hdr_x2" : "hdr_x3");

		seq_printf(f, "\tformat:%s/%ux%u@%d\n",
			   rkcif_pixelcode_to_string(stream->cif_fmt_in->mbus_code),
			   rect->width, rect->height,
			   interval->interval.denominator / interval->interval.numerator);

		seq_printf(f, "\tcrop.bounds:(%u, %u)/%ux%u\n",
			   sel->r.left, sel->r.top,
			   sel->r.width, sel->r.height);

		spin_lock_irqsave(&stream->fps_lock, flags);
		timestamp0 = stream->fps_stats.frm0_timestamp;
		timestamp1 = stream->fps_stats.frm1_timestamp;
		spin_unlock_irqrestore(&stream->fps_lock, flags);
		fps = timestamp0 > timestamp1 ?
		      timestamp0 - timestamp1 : timestamp1 - timestamp0;
		fps = div_u64(fps, 1000000);

		seq_puts(f, "Output Info:\n");
		seq_printf(f, "\tformat:%s/%ux%u(%u,%u)\n",
			   rkcif_pixelcode_to_string(stream->cif_fmt_out->fourcc),
			   dev->channels[0].width, dev->channels[0].height,
			   dev->channels[0].crop_st_x, dev->channels[0].crop_st_y);
		seq_printf(f, "\tcompact:%s\n", stream->is_compact ? "enable" : "disabled");
		seq_printf(f, "\tframe amount:%d\n", stream->frame_idx - 1);
		if (dev->inf_id == RKCIF_MIPI_LVDS) {
			time_val = div_u64(stream->readout.early_time, 1000000);
			seq_printf(f, "\tearly:%u ms\n", time_val);
			if (dev->hdr.hdr_mode == NO_HDR ||
			    dev->hdr.hdr_mode == HDR_COMPR) {
				time_val = div_u64(stream->readout.readout_time, 1000000);
				seq_printf(f, "\treadout:%u ms\n", time_val);
			} else {
				time_val = div_u64(stream->readout.readout_time, 1000000);
				seq_printf(f, "\tsingle readout:%u ms\n", time_val);
				time_val = div_u64(stream->readout.total_time, 1000000);
				seq_printf(f, "\ttotal readout:%u ms\n", time_val);

			}
		}
		seq_printf(f, "\trate:%llu ms\n", fps);
		fps = div_u64(1000, fps);
		seq_printf(f, "\tfps:%llu\n", fps);
		seq_puts(f, "\tirq statistics:\n");
		seq_printf(f, "\t\t\ttotal:%llu\n",
			   dev->irq_stats.all_frm_end_cnt + dev->irq_stats.all_err_cnt);
		if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		    sensor->mbus.type == V4L2_MBUS_BT656) {
			seq_printf(f, "\t\t\tdvp bus err:%llu\n", dev->irq_stats.dvp_bus_err_cnt);
			seq_printf(f, "\t\t\tdvp pix err:%llu\n", dev->irq_stats.dvp_pix_err_cnt);
			seq_printf(f, "\t\t\tdvp line err:%llu\n", dev->irq_stats.dvp_line_err_cnt);
			seq_printf(f, "\t\t\tdvp over flow:%llu\n", dev->irq_stats.dvp_overflow_cnt);
			seq_printf(f, "\t\t\tdvp bandwidth lack:%llu\n", dev->irq_stats.dvp_bwidth_lack_cnt);
			seq_printf(f, "\t\t\tdvp size err:%llu\n", dev->irq_stats.dvp_size_err_cnt);
		} else {
			seq_printf(f, "\t\t\tcsi over flow:%llu\n", dev->irq_stats.csi_overflow_cnt);
			seq_printf(f, "\t\t\tcsi bandwidth lack:%llu\n", dev->irq_stats.csi_bwidth_lack_cnt);
			seq_printf(f, "\t\t\tcsi size err:%llu\n", dev->irq_stats.csi_size_err_cnt);
		}
		seq_printf(f, "\t\t\tall err count:%llu\n", dev->irq_stats.all_err_cnt);
		seq_printf(f, "\t\t\tframe dma end:%llu\n", dev->irq_stats.all_frm_end_cnt);
	}
}

static int rkcif_proc_show(struct seq_file *f, void *v)
{
	struct rkcif_device *dev = f->private;

	if (dev) {
		rkcif_show_mixed_info(dev, f);
		rkcif_show_clks(dev, f);
		rkcif_show_format(dev, f);
	} else {
		seq_puts(f, "dev null\n");
	}

	return 0;
}

static int rkcif_proc_open(struct inode *inode, struct file *file)
{
	struct rkcif_device *data = PDE_DATA(inode);

	return single_open(file, rkcif_proc_show, data);
}

static const struct proc_ops rkcif_proc_fops = {
	.proc_open = rkcif_proc_open,
	.proc_release = single_release,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

int rkcif_proc_init(struct rkcif_device *dev)
{

	dev->proc_dir = proc_create_data(dev_name(dev->dev), 0444,
					 NULL, &rkcif_proc_fops,
					 dev);
	if (!dev->proc_dir) {
		dev_err(dev->dev, "create proc/%s failed!\n",
			dev_name(dev->dev));
		return -ENODEV;
	}

	return 0;
}

void rkcif_proc_cleanup(struct rkcif_device *dev)
{
	remove_proc_entry(dev_name(dev->dev), NULL);
}

#endif
