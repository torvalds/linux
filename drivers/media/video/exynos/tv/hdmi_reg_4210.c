/*
 * Samsung HDMI driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Jiun Yu <jiun.yu@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include <linux/delay.h>

#include "hdmi.h"
#include "regs-hdmi-4210.h"

static const struct hdmi_preset_conf hdmi_conf_480p = {
	.core = {
		.h_blank = {0x8a, 0x00},
		.v_blank = {0x0d, 0x6a, 0x01},
		.h_v_line = {0x0d, 0xa2, 0x35},
		.vsync_pol = {0x01},
		.int_pro_mode = {0x00},
		.v_blank_f = {0x00, 0x00, 0x00},
		.h_sync_gen = {0x0e, 0x30, 0x11},
		.v_sync_gen1 = {0x0f, 0x90, 0x00},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x5a, 0x03, /* h_fsz */
		0x8a, 0x00, 0xd0, 0x02, /* hact */
		0x0d, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0xe0, 0x01, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
	},
	.mbus_fmt = {
		.width = 720,
		.height = 480,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
};

static const struct hdmi_preset_conf hdmi_conf_720p60 = {
	.core = {
		.h_blank = {0x72, 0x01},
		.v_blank = {0xee, 0xf2, 0x00},
		.h_v_line = {0xee, 0x22, 0x67},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f = {0x00, 0x00, 0x00}, /* don't care */
		.h_sync_gen = {0x6c, 0x50, 0x02},
		.v_sync_gen1 = {0x0a, 0x50, 0x00},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x72, 0x06, /* h_fsz */
		0x72, 0x01, 0x00, 0x05, /* hact */
		0xee, 0x02, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x1e, 0x00, 0xd0, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
	},
	.mbus_fmt = {
		.width = 1280,
		.height = 720,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
};

static const struct hdmi_preset_conf hdmi_conf_1080p50 = {
	.core = {
		.h_blank = {0xd0, 0x02},
		.v_blank = {0x65, 0x6c, 0x01},
		.h_v_line = {0x65, 0x04, 0xa5},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f = {0x00, 0x00, 0x00}, /* don't care */
		.h_sync_gen = {0x0e, 0xea, 0x08},
		.v_sync_gen1 = {0x09, 0x40, 0x00},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
};

static const struct hdmi_preset_conf hdmi_conf_1080p60 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v_blank = {0x65, 0x6c, 0x01},
		.h_v_line = {0x65, 0x84, 0x89},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x00},
		.v_blank_f = {0x00, 0x00, 0x00}, /* don't care */
		.h_sync_gen = {0x56, 0x08, 0x02},
		.v_sync_gen1 = {0x09, 0x40, 0x00},
		/* other don't care */
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x18, 0x01, 0x80, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x2d, 0x00, 0x38, 0x04, /* vact */
		0x33, 0x02, /* field_chg */
		0x48, 0x02, /* vact_st2 */
		0x01, 0x00, 0x01, 0x00, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_NONE,
	},
};

static const struct hdmi_preset_conf hdmi_conf_1080i60 = {
	.core = {
		.h_blank = {0x18, 0x01},
		.v_blank = {0x32, 0xb2, 0x00},
		.h_v_line = {0x65, 0x84, 0x89},
		.vsync_pol = {0x00},
		.int_pro_mode = {0x01},
		.v_blank_f = {0x49, 0x2a, 0x23},
		.h_sync_gen = {0x56, 0x08, 0x02},
		.v_sync_gen1 = {0x07, 0x20, 0x00},
		.v_sync_gen2 = {0x39, 0x42, 0x23},
		.v_sync_gen3 = {0xa4, 0x44, 0x4a},
	},
	.tg = {
		0x00, /* cmd */
		0x98, 0x08, /* h_fsz */
		0x17, 0x01, 0x81, 0x07, /* hact */
		0x65, 0x04, /* v_fsz */
		0x01, 0x00, 0x33, 0x02, /* vsync */
		0x16, 0x00, 0x1c, 0x02, /* vact */
		0x33, 0x02, /* field_chg */
		0x49, 0x02, /* vact_st2 */
		0x01, 0x00, 0x33, 0x02, /* vsync top/bot */
		0x01, 0x00, 0x33, 0x02, /* field top/bot */
	},
	.mbus_fmt = {
		.width = 1920,
		.height = 1080,
		.code = V4L2_MBUS_FMT_FIXED, /* means RGB888 */
		.field = V4L2_FIELD_INTERLACED,
	},
};

const struct hdmi_conf hdmi_conf[] = {
	{ V4L2_DV_480P59_94, &hdmi_conf_480p },
	{ V4L2_DV_720P59_94, &hdmi_conf_720p60 },
	{ V4L2_DV_1080P50, &hdmi_conf_1080p50 },
	{ V4L2_DV_1080P30, &hdmi_conf_1080p60 },
	{ V4L2_DV_1080P60, &hdmi_conf_1080p60 },
	{ V4L2_DV_1080I60, &hdmi_conf_1080i60 },
};

const int hdmi_pre_cnt = ARRAY_SIZE(hdmi_conf);

irqreturn_t hdmi_irq_handler(int irq, void *dev_data)
{
	struct hdmi_device *hdev = dev_data;
	u32 intc_flag;

	(void)irq;
	intc_flag = hdmi_read(hdev, HDMI_INTC_FLAG);
	/* clearing flags for HPD plug/unplug */
	if (intc_flag & HDMI_INTC_FLAG_HPD_UNPLUG) {
		printk(KERN_INFO "unplugged\n");
		hdmi_write_mask(hdev, HDMI_INTC_FLAG, ~0,
			HDMI_INTC_FLAG_HPD_UNPLUG);
	}
	if (intc_flag & HDMI_INTC_FLAG_HPD_PLUG) {
		printk(KERN_INFO "plugged\n");
		hdmi_write_mask(hdev, HDMI_INTC_FLAG, ~0,
			HDMI_INTC_FLAG_HPD_PLUG);
	}

	return IRQ_HANDLED;
}

static void hdmi_reg_init(struct hdmi_device *hdev)
{
	/* enable HPD interrupts */
	hdmi_write_mask(hdev, HDMI_INTC_CON, ~0, HDMI_INTC_EN_GLOBAL |
		HDMI_INTC_EN_HPD_PLUG | HDMI_INTC_EN_HPD_UNPLUG);
	/* choose HDMI mode */
	hdmi_write_mask(hdev, HDMI_MODE_SEL,
		HDMI_MODE_HDMI_EN, HDMI_MODE_MASK);
	/* disable bluescreen */
	hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_BLUE_SCR_EN);
	/* choose bluescreen (fecal) color */
	hdmi_writeb(hdev, HDMI_BLUE_SCREEN_0, 0x12);
	hdmi_writeb(hdev, HDMI_BLUE_SCREEN_1, 0x34);
	hdmi_writeb(hdev, HDMI_BLUE_SCREEN_2, 0x56);
	/* enable AVI packet every vsync, fixes purple line problem */
	hdmi_writeb(hdev, HDMI_AVI_CON, 0x02);
	/* force YUV444, look to CEA-861-D, table 7 for more detail */
	hdmi_writeb(hdev, HDMI_AVI_BYTE(0), 2 << 5);
	hdmi_write_mask(hdev, HDMI_CON_1, 2, 3 << 5);
}

static void hdmi_timing_apply(struct hdmi_device *hdev,
	const struct hdmi_preset_conf *conf)
{
	const struct hdmi_core_regs *core = &conf->core;
	const struct hdmi_tg_regs *tg = &conf->tg;

	/* setting core registers */
	hdmi_writeb(hdev, HDMI_H_BLANK_0, core->h_blank[0]);
	hdmi_writeb(hdev, HDMI_H_BLANK_1, core->h_blank[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_0, core->v_blank[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_1, core->v_blank[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_2, core->v_blank[2]);
	hdmi_writeb(hdev, HDMI_H_V_LINE_0, core->h_v_line[0]);
	hdmi_writeb(hdev, HDMI_H_V_LINE_1, core->h_v_line[1]);
	hdmi_writeb(hdev, HDMI_H_V_LINE_2, core->h_v_line[2]);
	hdmi_writeb(hdev, HDMI_VSYNC_POL, core->vsync_pol[0]);
	hdmi_writeb(hdev, HDMI_INT_PRO_MODE, core->int_pro_mode[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F_0, core->v_blank_f[0]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F_1, core->v_blank_f[1]);
	hdmi_writeb(hdev, HDMI_V_BLANK_F_2, core->v_blank_f[2]);
	hdmi_writeb(hdev, HDMI_H_SYNC_GEN_0, core->h_sync_gen[0]);
	hdmi_writeb(hdev, HDMI_H_SYNC_GEN_1, core->h_sync_gen[1]);
	hdmi_writeb(hdev, HDMI_H_SYNC_GEN_2, core->h_sync_gen[2]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_1_0, core->v_sync_gen1[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_1_1, core->v_sync_gen1[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_1_2, core->v_sync_gen1[2]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_2_0, core->v_sync_gen2[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_2_1, core->v_sync_gen2[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_2_2, core->v_sync_gen2[2]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_3_0, core->v_sync_gen3[0]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_3_1, core->v_sync_gen3[1]);
	hdmi_writeb(hdev, HDMI_V_SYNC_GEN_3_2, core->v_sync_gen3[2]);
	/* Timing generator registers */
	hdmi_writeb(hdev, HDMI_TG_H_FSZ_L, tg->h_fsz_l);
	hdmi_writeb(hdev, HDMI_TG_H_FSZ_H, tg->h_fsz_h);
	hdmi_writeb(hdev, HDMI_TG_HACT_ST_L, tg->hact_st_l);
	hdmi_writeb(hdev, HDMI_TG_HACT_ST_H, tg->hact_st_h);
	hdmi_writeb(hdev, HDMI_TG_HACT_SZ_L, tg->hact_sz_l);
	hdmi_writeb(hdev, HDMI_TG_HACT_SZ_H, tg->hact_sz_h);
	hdmi_writeb(hdev, HDMI_TG_V_FSZ_L, tg->v_fsz_l);
	hdmi_writeb(hdev, HDMI_TG_V_FSZ_H, tg->v_fsz_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_L, tg->vsync_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_H, tg->vsync_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC2_L, tg->vsync2_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC2_H, tg->vsync2_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST_L, tg->vact_st_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST_H, tg->vact_st_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_SZ_L, tg->vact_sz_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_SZ_H, tg->vact_sz_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_CHG_L, tg->field_chg_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_CHG_H, tg->field_chg_h);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST2_L, tg->vact_st2_l);
	hdmi_writeb(hdev, HDMI_TG_VACT_ST2_H, tg->vact_st2_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_TOP_HDMI_L, tg->vsync_top_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_TOP_HDMI_H, tg->vsync_top_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_BOT_HDMI_L, tg->vsync_bot_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_VSYNC_BOT_HDMI_H, tg->vsync_bot_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_TOP_HDMI_L, tg->field_top_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_TOP_HDMI_H, tg->field_top_hdmi_h);
	hdmi_writeb(hdev, HDMI_TG_FIELD_BOT_HDMI_L, tg->field_bot_hdmi_l);
	hdmi_writeb(hdev, HDMI_TG_FIELD_BOT_HDMI_H, tg->field_bot_hdmi_h);
}

int hdmi_conf_apply(struct hdmi_device *hdmi_dev)
{
	struct device *dev = hdmi_dev->dev;
	const struct hdmi_preset_conf *conf = hdmi_dev->cur_conf;
	struct v4l2_dv_preset preset;
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	/* reset hdmiphy */
	hdmi_write_mask(hdmi_dev, HDMI_PHY_RSTOUT, ~0, HDMI_PHY_SW_RSTOUT);
	mdelay(10);
	hdmi_write_mask(hdmi_dev, HDMI_PHY_RSTOUT,  0, HDMI_PHY_SW_RSTOUT);
	mdelay(10);

	/* configure presets */
	preset.preset = hdmi_dev->cur_preset;
	ret = v4l2_subdev_call(hdmi_dev->phy_sd, video, s_dv_preset, &preset);
	if (ret) {
		dev_err(dev, "failed to set preset (%u)\n", preset.preset);
		return ret;
	}

	/* resetting HDMI core */
	hdmi_write_mask(hdmi_dev, HDMI_CORE_RSTOUT,  0, HDMI_CORE_SW_RSTOUT);
	mdelay(10);
	hdmi_write_mask(hdmi_dev, HDMI_CORE_RSTOUT, ~0, HDMI_CORE_SW_RSTOUT);
	mdelay(10);

	hdmi_reg_init(hdmi_dev);

	/* setting core registers */
	hdmi_timing_apply(hdmi_dev, conf);

	return 0;
}

int is_hdmiphy_ready(struct hdmi_device *hdev)
{
	u32 val = hdmi_read(hdev, HDMI_PHY_STATUS);
	if (val & HDMI_PHY_STATUS_READY)
		return 1;

	return 0;
}

void hdmi_enable(struct hdmi_device *hdev, int on)
{
	if (on)
		hdmi_write_mask(hdev, HDMI_CON_0, ~0, HDMI_EN);
	else
		hdmi_write_mask(hdev, HDMI_CON_0, 0, HDMI_EN);
}

void hdmi_tg_enable(struct hdmi_device *hdev, int on)
{
	u32 mask;

	mask = (hdev->cur_conf->mbus_fmt.field == V4L2_FIELD_INTERLACED) ?
			HDMI_TG_EN | HDMI_FIELD_EN : HDMI_TG_EN;

	if (on)
		hdmi_write_mask(hdev, HDMI_TG_CMD, ~0, mask);
	else
		hdmi_write_mask(hdev, HDMI_TG_CMD, 0, mask);
}

void hdmi_dumpregs(struct hdmi_device *hdev, char *prefix)
{
#define DUMPREG(reg_id) \
	dev_dbg(hdev->dev, "%s:" #reg_id " = %08x\n", prefix, \
		readl(hdev->regs + reg_id))

	dev_dbg(hdev->dev, "%s: ---- CONTROL REGISTERS ----\n", prefix);
	DUMPREG(HDMI_INTC_FLAG);
	DUMPREG(HDMI_INTC_CON);
	DUMPREG(HDMI_HPD_STATUS);
	DUMPREG(HDMI_PHY_RSTOUT);
	DUMPREG(HDMI_PHY_VPLL);
	DUMPREG(HDMI_PHY_CMU);
	DUMPREG(HDMI_CORE_RSTOUT);

	dev_dbg(hdev->dev, "%s: ---- CORE REGISTERS ----\n", prefix);
	DUMPREG(HDMI_CON_0);
	DUMPREG(HDMI_CON_1);
	DUMPREG(HDMI_CON_2);
	DUMPREG(HDMI_SYS_STATUS);
	DUMPREG(HDMI_PHY_STATUS);
	DUMPREG(HDMI_STATUS_EN);
	DUMPREG(HDMI_HPD);
	DUMPREG(HDMI_MODE_SEL);
	DUMPREG(HDMI_HPD_GEN);
	DUMPREG(HDMI_DC_CONTROL);
	DUMPREG(HDMI_VIDEO_PATTERN_GEN);

	dev_dbg(hdev->dev, "%s: ---- CORE SYNC REGISTERS ----\n", prefix);
	DUMPREG(HDMI_H_BLANK_0);
	DUMPREG(HDMI_H_BLANK_1);
	DUMPREG(HDMI_V_BLANK_0);
	DUMPREG(HDMI_V_BLANK_1);
	DUMPREG(HDMI_V_BLANK_2);
	DUMPREG(HDMI_H_V_LINE_0);
	DUMPREG(HDMI_H_V_LINE_1);
	DUMPREG(HDMI_H_V_LINE_2);
	DUMPREG(HDMI_VSYNC_POL);
	DUMPREG(HDMI_INT_PRO_MODE);
	DUMPREG(HDMI_V_BLANK_F_0);
	DUMPREG(HDMI_V_BLANK_F_1);
	DUMPREG(HDMI_V_BLANK_F_2);
	DUMPREG(HDMI_H_SYNC_GEN_0);
	DUMPREG(HDMI_H_SYNC_GEN_1);
	DUMPREG(HDMI_H_SYNC_GEN_2);
	DUMPREG(HDMI_V_SYNC_GEN_1_0);
	DUMPREG(HDMI_V_SYNC_GEN_1_1);
	DUMPREG(HDMI_V_SYNC_GEN_1_2);
	DUMPREG(HDMI_V_SYNC_GEN_2_0);
	DUMPREG(HDMI_V_SYNC_GEN_2_1);
	DUMPREG(HDMI_V_SYNC_GEN_2_2);
	DUMPREG(HDMI_V_SYNC_GEN_3_0);
	DUMPREG(HDMI_V_SYNC_GEN_3_1);
	DUMPREG(HDMI_V_SYNC_GEN_3_2);

	dev_dbg(hdev->dev, "%s: ---- TG REGISTERS ----\n", prefix);
	DUMPREG(HDMI_TG_CMD);
	DUMPREG(HDMI_TG_H_FSZ_L);
	DUMPREG(HDMI_TG_H_FSZ_H);
	DUMPREG(HDMI_TG_HACT_ST_L);
	DUMPREG(HDMI_TG_HACT_ST_H);
	DUMPREG(HDMI_TG_HACT_SZ_L);
	DUMPREG(HDMI_TG_HACT_SZ_H);
	DUMPREG(HDMI_TG_V_FSZ_L);
	DUMPREG(HDMI_TG_V_FSZ_H);
	DUMPREG(HDMI_TG_VSYNC_L);
	DUMPREG(HDMI_TG_VSYNC_H);
	DUMPREG(HDMI_TG_VSYNC2_L);
	DUMPREG(HDMI_TG_VSYNC2_H);
	DUMPREG(HDMI_TG_VACT_ST_L);
	DUMPREG(HDMI_TG_VACT_ST_H);
	DUMPREG(HDMI_TG_VACT_SZ_L);
	DUMPREG(HDMI_TG_VACT_SZ_H);
	DUMPREG(HDMI_TG_FIELD_CHG_L);
	DUMPREG(HDMI_TG_FIELD_CHG_H);
	DUMPREG(HDMI_TG_VACT_ST2_L);
	DUMPREG(HDMI_TG_VACT_ST2_H);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_TOP_HDMI_H);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_L);
	DUMPREG(HDMI_TG_VSYNC_BOT_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_TOP_HDMI_H);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_L);
	DUMPREG(HDMI_TG_FIELD_BOT_HDMI_H);
#undef DUMPREG
}
