/*
 * vpif - Video Port Interface driver
 * VPIF is a receiver and transmitter for video data. It has two channels(0, 1)
 * that receiveing video byte stream and two channels(2, 3) for video output.
 * The hardware supports SDTV, HDTV formats, raw data capture.
 * Currently, the driver supports NTSC and PAL standards.
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/v4l2-dv-timings.h>

#include <mach/hardware.h>

#include "vpif.h"

MODULE_DESCRIPTION("TI DaVinci Video Port Interface driver");
MODULE_LICENSE("GPL");

#define VPIF_CH0_MAX_MODES	(22)
#define VPIF_CH1_MAX_MODES	(02)
#define VPIF_CH2_MAX_MODES	(15)
#define VPIF_CH3_MAX_MODES	(02)

static resource_size_t	res_len;
static struct resource	*res;
spinlock_t vpif_lock;

void __iomem *vpif_base;
struct clk *vpif_clk;

/**
 * ch_params: video standard configuration parameters for vpif
 * The table must include all presets from supported subdevices.
 */
const struct vpif_channel_config_params ch_params[] = {
	/* HDTV formats */
	{
		.name = "480p59_94",
		.width = 720,
		.height = 480,
		.frm_fmt = 1,
		.ycmux_mode = 0,
		.eav2sav = 138-8,
		.sav2eav = 720,
		.l1 = 1,
		.l3 = 43,
		.l5 = 523,
		.vsize = 525,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_720X480P59_94,
	},
	{
		.name = "576p50",
		.width = 720,
		.height = 576,
		.frm_fmt = 1,
		.ycmux_mode = 0,
		.eav2sav = 144-8,
		.sav2eav = 720,
		.l1 = 1,
		.l3 = 45,
		.l5 = 621,
		.vsize = 625,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_720X576P50,
	},
	{
		.name = "720p50",
		.width = 1280,
		.height = 720,
		.frm_fmt = 1,
		.ycmux_mode = 0,
		.eav2sav = 700-8,
		.sav2eav = 1280,
		.l1 = 1,
		.l3 = 26,
		.l5 = 746,
		.vsize = 750,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_1280X720P50,
	},
	{
		.name = "720p60",
		.width = 1280,
		.height = 720,
		.frm_fmt = 1,
		.ycmux_mode = 0,
		.eav2sav = 370 - 8,
		.sav2eav = 1280,
		.l1 = 1,
		.l3 = 26,
		.l5 = 746,
		.vsize = 750,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_1280X720P60,
	},
	{
		.name = "1080I50",
		.width = 1920,
		.height = 1080,
		.frm_fmt = 0,
		.ycmux_mode = 0,
		.eav2sav = 720 - 8,
		.sav2eav = 1920,
		.l1 = 1,
		.l3 = 21,
		.l5 = 561,
		.l7 = 563,
		.l9 = 584,
		.l11 = 1124,
		.vsize = 1125,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_1920X1080I50,
	},
	{
		.name = "1080I60",
		.width = 1920,
		.height = 1080,
		.frm_fmt = 0,
		.ycmux_mode = 0,
		.eav2sav = 280 - 8,
		.sav2eav = 1920,
		.l1 = 1,
		.l3 = 21,
		.l5 = 561,
		.l7 = 563,
		.l9 = 584,
		.l11 = 1124,
		.vsize = 1125,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_1920X1080I60,
	},
	{
		.name = "1080p60",
		.width = 1920,
		.height = 1080,
		.frm_fmt = 1,
		.ycmux_mode = 0,
		.eav2sav = 280 - 8,
		.sav2eav = 1920,
		.l1 = 1,
		.l3 = 42,
		.l5 = 1122,
		.vsize = 1125,
		.capture_format = 0,
		.vbi_supported = 0,
		.hd_sd = 1,
		.dv_timings = V4L2_DV_BT_CEA_1920X1080P60,
	},

	/* SDTV formats */
	{
		.name = "NTSC_M",
		.width = 720,
		.height = 480,
		.frm_fmt = 0,
		.ycmux_mode = 1,
		.eav2sav = 268,
		.sav2eav = 1440,
		.l1 = 1,
		.l3 = 23,
		.l5 = 263,
		.l7 = 266,
		.l9 = 286,
		.l11 = 525,
		.vsize = 525,
		.capture_format = 0,
		.vbi_supported = 1,
		.hd_sd = 0,
		.stdid = V4L2_STD_525_60,
	},
	{
		.name = "PAL_BDGHIK",
		.width = 720,
		.height = 576,
		.frm_fmt = 0,
		.ycmux_mode = 1,
		.eav2sav = 280,
		.sav2eav = 1440,
		.l1 = 1,
		.l3 = 23,
		.l5 = 311,
		.l7 = 313,
		.l9 = 336,
		.l11 = 624,
		.vsize = 625,
		.capture_format = 0,
		.vbi_supported = 1,
		.hd_sd = 0,
		.stdid = V4L2_STD_625_50,
	},
};

const unsigned int vpif_ch_params_count = ARRAY_SIZE(ch_params);

static inline void vpif_wr_bit(u32 reg, u32 bit, u32 val)
{
	if (val)
		vpif_set_bit(reg, bit);
	else
		vpif_clr_bit(reg, bit);
}

/* This structure is used to keep track of VPIF size register's offsets */
struct vpif_registers {
	u32 h_cfg, v_cfg_00, v_cfg_01, v_cfg_02, v_cfg, ch_ctrl;
	u32 line_offset, vanc0_strt, vanc0_size, vanc1_strt;
	u32 vanc1_size, width_mask, len_mask;
	u8 max_modes;
};

static const struct vpif_registers vpifregs[VPIF_NUM_CHANNELS] = {
	/* Channel0 */
	{
		VPIF_CH0_H_CFG, VPIF_CH0_V_CFG_00, VPIF_CH0_V_CFG_01,
		VPIF_CH0_V_CFG_02, VPIF_CH0_V_CFG_03, VPIF_CH0_CTRL,
		VPIF_CH0_IMG_ADD_OFST, 0, 0, 0, 0, 0x1FFF, 0xFFF,
		VPIF_CH0_MAX_MODES,
	},
	/* Channel1 */
	{
		VPIF_CH1_H_CFG, VPIF_CH1_V_CFG_00, VPIF_CH1_V_CFG_01,
		VPIF_CH1_V_CFG_02, VPIF_CH1_V_CFG_03, VPIF_CH1_CTRL,
		VPIF_CH1_IMG_ADD_OFST, 0, 0, 0, 0, 0x1FFF, 0xFFF,
		VPIF_CH1_MAX_MODES,
	},
	/* Channel2 */
	{
		VPIF_CH2_H_CFG, VPIF_CH2_V_CFG_00, VPIF_CH2_V_CFG_01,
		VPIF_CH2_V_CFG_02, VPIF_CH2_V_CFG_03, VPIF_CH2_CTRL,
		VPIF_CH2_IMG_ADD_OFST, VPIF_CH2_VANC0_STRT, VPIF_CH2_VANC0_SIZE,
		VPIF_CH2_VANC1_STRT, VPIF_CH2_VANC1_SIZE, 0x7FF, 0x7FF,
		VPIF_CH2_MAX_MODES
	},
	/* Channel3 */
	{
		VPIF_CH3_H_CFG, VPIF_CH3_V_CFG_00, VPIF_CH3_V_CFG_01,
		VPIF_CH3_V_CFG_02, VPIF_CH3_V_CFG_03, VPIF_CH3_CTRL,
		VPIF_CH3_IMG_ADD_OFST, VPIF_CH3_VANC0_STRT, VPIF_CH3_VANC0_SIZE,
		VPIF_CH3_VANC1_STRT, VPIF_CH3_VANC1_SIZE, 0x7FF, 0x7FF,
		VPIF_CH3_MAX_MODES
	},
};

/* vpif_set_mode_info:
 * This function is used to set horizontal and vertical config parameters
 * As per the standard in the channel, configure the values of L1, L3,
 * L5, L7  L9, L11 in VPIF Register , also write width and height
 */
static void vpif_set_mode_info(const struct vpif_channel_config_params *config,
				u8 channel_id, u8 config_channel_id)
{
	u32 value;

	value = (config->eav2sav & vpifregs[config_channel_id].width_mask);
	value <<= VPIF_CH_LEN_SHIFT;
	value |= (config->sav2eav & vpifregs[config_channel_id].width_mask);
	regw(value, vpifregs[channel_id].h_cfg);

	value = (config->l1 & vpifregs[config_channel_id].len_mask);
	value <<= VPIF_CH_LEN_SHIFT;
	value |= (config->l3 & vpifregs[config_channel_id].len_mask);
	regw(value, vpifregs[channel_id].v_cfg_00);

	value = (config->l5 & vpifregs[config_channel_id].len_mask);
	value <<= VPIF_CH_LEN_SHIFT;
	value |= (config->l7 & vpifregs[config_channel_id].len_mask);
	regw(value, vpifregs[channel_id].v_cfg_01);

	value = (config->l9 & vpifregs[config_channel_id].len_mask);
	value <<= VPIF_CH_LEN_SHIFT;
	value |= (config->l11 & vpifregs[config_channel_id].len_mask);
	regw(value, vpifregs[channel_id].v_cfg_02);

	value = (config->vsize & vpifregs[config_channel_id].len_mask);
	regw(value, vpifregs[channel_id].v_cfg);
}

/* config_vpif_params
 * Function to set the parameters of a channel
 * Mainly modifies the channel ciontrol register
 * It sets frame format, yc mux mode
 */
static void config_vpif_params(struct vpif_params *vpifparams,
				u8 channel_id, u8 found)
{
	const struct vpif_channel_config_params *config = &vpifparams->std_info;
	u32 value, ch_nip, reg;
	u8 start, end;
	int i;

	start = channel_id;
	end = channel_id + found;

	for (i = start; i < end; i++) {
		reg = vpifregs[i].ch_ctrl;
		if (channel_id < 2)
			ch_nip = VPIF_CAPTURE_CH_NIP;
		else
			ch_nip = VPIF_DISPLAY_CH_NIP;

		vpif_wr_bit(reg, ch_nip, config->frm_fmt);
		vpif_wr_bit(reg, VPIF_CH_YC_MUX_BIT, config->ycmux_mode);
		vpif_wr_bit(reg, VPIF_CH_INPUT_FIELD_FRAME_BIT,
					vpifparams->video_params.storage_mode);

		/* Set raster scanning SDR Format */
		vpif_clr_bit(reg, VPIF_CH_SDR_FMT_BIT);
		vpif_wr_bit(reg, VPIF_CH_DATA_MODE_BIT, config->capture_format);

		if (channel_id > 1)	/* Set the Pixel enable bit */
			vpif_set_bit(reg, VPIF_DISPLAY_PIX_EN_BIT);
		else if (config->capture_format) {
			/* Set the polarity of various pins */
			vpif_wr_bit(reg, VPIF_CH_FID_POLARITY_BIT,
					vpifparams->iface.fid_pol);
			vpif_wr_bit(reg, VPIF_CH_V_VALID_POLARITY_BIT,
					vpifparams->iface.vd_pol);
			vpif_wr_bit(reg, VPIF_CH_H_VALID_POLARITY_BIT,
					vpifparams->iface.hd_pol);

			value = regr(reg);
			/* Set data width */
			value &= ~(0x3u <<
					VPIF_CH_DATA_WIDTH_BIT);
			value |= ((vpifparams->params.data_sz) <<
						     VPIF_CH_DATA_WIDTH_BIT);
			regw(value, reg);
		}

		/* Write the pitch in the driver */
		regw((vpifparams->video_params.hpitch),
						vpifregs[i].line_offset);
	}
}

/* vpif_set_video_params
 * This function is used to set video parameters in VPIF register
 */
int vpif_set_video_params(struct vpif_params *vpifparams, u8 channel_id)
{
	const struct vpif_channel_config_params *config = &vpifparams->std_info;
	int found = 1;

	vpif_set_mode_info(config, channel_id, channel_id);
	if (!config->ycmux_mode) {
		/* YC are on separate channels (HDTV formats) */
		vpif_set_mode_info(config, channel_id + 1, channel_id);
		found = 2;
	}

	config_vpif_params(vpifparams, channel_id, found);

	regw(0x80, VPIF_REQ_SIZE);
	regw(0x01, VPIF_EMULATION_CTRL);

	return found;
}
EXPORT_SYMBOL(vpif_set_video_params);

void vpif_set_vbi_display_params(struct vpif_vbi_params *vbiparams,
				u8 channel_id)
{
	u32 value;

	value = 0x3F8 & (vbiparams->hstart0);
	value |= 0x3FFFFFF & ((vbiparams->vstart0) << 16);
	regw(value, vpifregs[channel_id].vanc0_strt);

	value = 0x3F8 & (vbiparams->hstart1);
	value |= 0x3FFFFFF & ((vbiparams->vstart1) << 16);
	regw(value, vpifregs[channel_id].vanc1_strt);

	value = 0x3F8 & (vbiparams->hsize0);
	value |= 0x3FFFFFF & ((vbiparams->vsize0) << 16);
	regw(value, vpifregs[channel_id].vanc0_size);

	value = 0x3F8 & (vbiparams->hsize1);
	value |= 0x3FFFFFF & ((vbiparams->vsize1) << 16);
	regw(value, vpifregs[channel_id].vanc1_size);

}
EXPORT_SYMBOL(vpif_set_vbi_display_params);

int vpif_channel_getfid(u8 channel_id)
{
	return (regr(vpifregs[channel_id].ch_ctrl) & VPIF_CH_FID_MASK)
					>> VPIF_CH_FID_SHIFT;
}
EXPORT_SYMBOL(vpif_channel_getfid);

static int vpif_probe(struct platform_device *pdev)
{
	int status = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	res_len = resource_size(res);

	res = request_mem_region(res->start, res_len, res->name);
	if (!res)
		return -EBUSY;

	vpif_base = ioremap(res->start, res_len);
	if (!vpif_base) {
		status = -EBUSY;
		goto fail;
	}

	vpif_clk = clk_get(&pdev->dev, "vpif");
	if (IS_ERR(vpif_clk)) {
		status = PTR_ERR(vpif_clk);
		goto clk_fail;
	}
	clk_prepare_enable(vpif_clk);

	spin_lock_init(&vpif_lock);
	dev_info(&pdev->dev, "vpif probe success\n");
	return 0;

clk_fail:
	iounmap(vpif_base);
fail:
	release_mem_region(res->start, res_len);
	return status;
}

static int vpif_remove(struct platform_device *pdev)
{
	if (vpif_clk) {
		clk_disable_unprepare(vpif_clk);
		clk_put(vpif_clk);
	}

	iounmap(vpif_base);
	release_mem_region(res->start, res_len);
	return 0;
}

#ifdef CONFIG_PM
static int vpif_suspend(struct device *dev)
{
	clk_disable_unprepare(vpif_clk);
	return 0;
}

static int vpif_resume(struct device *dev)
{
	clk_prepare_enable(vpif_clk);
	return 0;
}

static const struct dev_pm_ops vpif_pm = {
	.suspend        = vpif_suspend,
	.resume         = vpif_resume,
};

#define vpif_pm_ops (&vpif_pm)
#else
#define vpif_pm_ops NULL
#endif

static struct platform_driver vpif_driver = {
	.driver = {
		.name	= "vpif",
		.owner = THIS_MODULE,
		.pm	= vpif_pm_ops,
	},
	.remove = vpif_remove,
	.probe = vpif_probe,
};

static void vpif_exit(void)
{
	platform_driver_unregister(&vpif_driver);
}

static int __init vpif_init(void)
{
	return platform_driver_register(&vpif_driver);
}
subsys_initcall(vpif_init);
module_exit(vpif_exit);

