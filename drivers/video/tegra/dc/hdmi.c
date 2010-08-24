/*
 * drivers/video/tegra/dc/hdmi.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/nvhost_bus.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <mach/clk.h>
#include <mach/dc.h>
#include <mach/fb.h>

#include "dc_reg.h"
#include "dc_priv.h"
#include "hdmi_reg.h"
#include "edid.h"

struct tegra_dc_hdmi_data {
	struct tegra_dc			*dc;
	struct tegra_edid		*edid;
	struct delayed_work		work;

	struct resource			*base_res;
	void __iomem			*base;
	struct clk			*clk;
};

const struct fb_videomode tegra_dc_hdmi_supported_modes[] = {
	/* 1280x720p 60hz: EIA/CEA-861-B Format 4 */
	{
		.xres =		1280,
		.yres =		720,
		.pixclock =	KHZ2PICOS(74250),
		.hsync_len =	40,	/* h_sync_width */
		.vsync_len =	5,	/* v_sync_width */
		.left_margin =	220,	/* h_back_porch */
		.upper_margin =	20,	/* v_back_porch */
		.right_margin =	110,	/* h_front_porch */
		.lower_margin =	5,	/* v_front_porch */
		.vmode =	FB_VMODE_NONINTERLACED,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},

	/* 720x480p 59.94hz: EIA/CEA-861-B Formats 2 & 3 */
	{
		.xres =		720,
		.yres =		480,
		.pixclock =	KHZ2PICOS(27000),
		.hsync_len =	62,	/* h_sync_width */
		.vsync_len =	6,	/* v_sync_width */
		.left_margin =	60,	/* h_back_porch */
		.upper_margin =	30,	/* v_back_porch */
		.right_margin =	16,	/* h_front_porch */
		.lower_margin =	9,	/* v_front_porch */
		.vmode =	FB_VMODE_NONINTERLACED,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},

	/* 640x480p 60hz: EIA/CEA-861-B Format 1 */
	{
		.xres =		640,
		.yres =		480,
		.pixclock =	KHZ2PICOS(25200),
		.hsync_len =	96,	/* h_sync_width */
		.vsync_len =	2,	/* v_sync_width */
		.left_margin =	48,	/* h_back_porch */
		.upper_margin =	33,	/* v_back_porch */
		.right_margin =	16,	/* h_front_porch */
		.lower_margin =	10,	/* v_front_porch */
		.vmode =	FB_VMODE_NONINTERLACED,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},

	/* 720x576p 50hz EIA/CEA-861-B Formats 17 & 18 */
	{
		.xres =		720,
		.yres =		576,
		.pixclock =	KHZ2PICOS(27000),
		.hsync_len =	64,	/* h_sync_width */
		.vsync_len =	5,	/* v_sync_width */
		.left_margin =	68,	/* h_back_porch */
		.upper_margin =	39,	/* v_back_porch */
		.right_margin =	12,	/* h_front_porch */
		.lower_margin =	5,	/* v_front_porch */
		.vmode =	FB_VMODE_NONINTERLACED,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},

	/* 1920x1080p 59.94/60hz EIA/CEA-861-B Format 16 */
	{
		.xres =		1920,
		.yres =		1080,
		.pixclock =	KHZ2PICOS(148500),
		.hsync_len =	44,	/* h_sync_width */
		.vsync_len =	5,	/* v_sync_width */
		.left_margin =	148,	/* h_back_porch */
		.upper_margin =	36,	/* v_back_porch */
		.right_margin =	88,	/* h_front_porch */
		.lower_margin =	4,	/* v_front_porch */
		.vmode =	FB_VMODE_NONINTERLACED,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},
};

static inline unsigned long tegra_hdmi_readl(struct tegra_dc_hdmi_data *hdmi,
					     unsigned long reg)
{
	return readl(hdmi->base + reg * 4);
}

static inline void tegra_hdmi_writel(struct tegra_dc_hdmi_data *hdmi,
				     unsigned long val, unsigned long reg)
{
	writel(val, hdmi->base + reg * 4);
}

static inline void tegra_hdmi_clrsetbits(struct tegra_dc_hdmi_data *hdmi,
					 unsigned long reg, unsigned long clr,
					 unsigned long set)
{
	unsigned long val = tegra_hdmi_readl(hdmi, reg);
	val &= ~clr;
	val |= set;
	tegra_hdmi_writel(hdmi, val, reg);
}

#define DUMP_REG(a) do {						\
		printk("HDMI %-32s\t%03x\t%08lx\n",			\
		       #a, a, tegra_hdmi_readl(hdmi, a));		\
	} while (0)

#ifdef DEBUG
static void hdmi_dumpregs(struct tegra_dc_hdmi_data *hdmi)
{
	DUMP_REG(HDMI_CTXSW);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE0);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE1);
	DUMP_REG(HDMI_NV_PDISP_SOR_STATE2);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AN_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AN_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CN_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CN_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_AKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_BKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_BKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_DKSV_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_DKSV_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CTRL);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CMODE);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_MPRIME_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_MPRIME_LSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB2);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB1);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_RI);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CS_MSB);
	DUMP_REG(HDMI_NV_PDISP_RG_HDCP_CS_LSB);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU_RDATA0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_EMU2);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_HEADER);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_HIGH);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_VSYNC_KEEPOUT);
	DUMP_REG(HDMI_NV_PDISP_HDMI_VSYNC_WINDOW);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_CTRL);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_STATUS);
	DUMP_REG(HDMI_NV_PDISP_HDMI_GCP_SUBPACK);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_CHANNEL_STATUS2);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU0);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_EMU1_RDATA);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPARE);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS1);
	DUMP_REG(HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS2);
	DUMP_REG(HDMI_NV_PDISP_HDCPRIF_ROM_CTRL);
	DUMP_REG(HDMI_NV_PDISP_SOR_CAP);
	DUMP_REG(HDMI_NV_PDISP_SOR_PWR);
	DUMP_REG(HDMI_NV_PDISP_SOR_TEST);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL0);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL1);
	DUMP_REG(HDMI_NV_PDISP_SOR_PLL2);
	DUMP_REG(HDMI_NV_PDISP_SOR_CSTM);
	DUMP_REG(HDMI_NV_PDISP_SOR_LVDS);
	DUMP_REG(HDMI_NV_PDISP_SOR_CRCA);
	DUMP_REG(HDMI_NV_PDISP_SOR_CRCB);
	DUMP_REG(HDMI_NV_PDISP_SOR_BLANK);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_CTL);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST0);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST1);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST2);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST3);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST4);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST5);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST6);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST7);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST8);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INST9);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTA);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTB);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTC);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTD);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTE);
	DUMP_REG(HDMI_NV_PDISP_SOR_SEQ_INSTF);
	DUMP_REG(HDMI_NV_PDISP_SOR_VCRCA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_VCRCA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_CCRCA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_CCRCA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_EDATAA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_EDATAA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_COUNTA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_COUNTA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_DEBUGA0);
	DUMP_REG(HDMI_NV_PDISP_SOR_DEBUGA1);
	DUMP_REG(HDMI_NV_PDISP_SOR_TRIG);
	DUMP_REG(HDMI_NV_PDISP_SOR_MSCHECK);
	DUMP_REG(HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG0);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG1);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_DEBUG2);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS1);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS2);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS3);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS4);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS5);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS6);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_FS7);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_PULSE_WIDTH);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_THRESHOLD);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_CNTRL0);
	DUMP_REG(HDMI_NV_PDISP_AUDIO_N);
	DUMP_REG(HDMI_NV_PDISP_HDCPRIF_ROM_TIMING);
	DUMP_REG(HDMI_NV_PDISP_SOR_REFCLK);
	DUMP_REG(HDMI_NV_PDISP_CRC_CONTROL);
	DUMP_REG(HDMI_NV_PDISP_INPUT_CONTROL);
	DUMP_REG(HDMI_NV_PDISP_SCRATCH);
	DUMP_REG(HDMI_NV_PDISP_PE_CURRENT);
	DUMP_REG(HDMI_NV_PDISP_KEY_CTRL);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG0);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG1);
	DUMP_REG(HDMI_NV_PDISP_KEY_DEBUG2);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_0);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_1);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_2);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_3);
	DUMP_REG(HDMI_NV_PDISP_KEY_HDCP_KEY_TRIG);
	DUMP_REG(HDMI_NV_PDISP_KEY_SKEY_INDEX);
}
#endif

#define PIXCLOCK_TOLERANCE	200

static bool tegra_dc_hdmi_mode_equal(const struct fb_videomode *mode1,
					const struct fb_videomode *mode2)
{
	int diff = (s64)mode1->pixclock - (s64)mode2->pixclock;

	return mode1->xres	== mode2->xres &&
		mode1->yres	== mode2->yres &&
		diff		< PIXCLOCK_TOLERANCE &&
		diff		> -PIXCLOCK_TOLERANCE &&
		mode1->vmode	== mode2->vmode;
}

static bool tegra_dc_hdmi_mode_filter(struct fb_videomode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_dc_hdmi_supported_modes); i++) {
		if (tegra_dc_hdmi_mode_equal(&tegra_dc_hdmi_supported_modes[i],
					     mode))
			return true;
	}

	return false;
}


static bool tegra_dc_hdmi_hpd(struct tegra_dc *dc)
{
	int sense;
	int level;

	level = gpio_get_value(dc->out->hotplug_gpio);

	sense = dc->out->flags & TEGRA_DC_OUT_HOTPLUG_MASK;

	return (sense == TEGRA_DC_OUT_HOTPLUG_HIGH && level) ||
		(sense == TEGRA_DC_OUT_HOTPLUG_LOW && !level);
}

static bool tegra_dc_hdmi_detect(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi_data *hdmi = tegra_dc_get_outdata(dc);
	struct fb_monspecs specs;
	int err;

	if (!tegra_dc_hdmi_hpd(dc))
		return false;

	err = tegra_edid_get_monspecs(hdmi->edid, &specs);
	if (err < 0) {
		dev_err(&dc->ndev->dev, "error reading edid\n");
		return false;
	}

	tegra_fb_update_monspecs(dc->fb, &specs, tegra_dc_hdmi_mode_filter);
	dev_info(&dc->ndev->dev, "display detected\n");
	return true;
}


static void tegra_dc_hdmi_detect_worker(struct work_struct *work)
{
	struct tegra_dc_hdmi_data *hdmi =
		container_of(to_delayed_work(work), struct tegra_dc_hdmi_data, work);
	struct tegra_dc *dc = hdmi->dc;

	if (tegra_dc_hdmi_hpd(dc))
		tegra_dc_hdmi_detect(dc);
}

static irqreturn_t tegra_dc_hdmi_irq(int irq, void *ptr)
{
	struct tegra_dc *dc = ptr;
	struct tegra_dc_hdmi_data *hdmi = tegra_dc_get_outdata(dc);

	if (tegra_dc_hdmi_hpd(dc))
		schedule_delayed_work(&hdmi->work, msecs_to_jiffies(2000));

	return IRQ_HANDLED;
}

static int tegra_dc_hdmi_init(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi_data *hdmi;
	struct resource *res;
	struct resource *base_res;
	void __iomem *base;
	struct clk *clk;
	int err;

	hdmi = kzalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	res = nvhost_get_resource_byname(dc->ndev, IORESOURCE_MEM, "hdmi_regs");
	if (!res) {
		dev_err(&dc->ndev->dev, "hdmi: no mem resource\n");
		err = -ENOENT;
		goto err_free_hdmi;
	}

	base_res = request_mem_region(res->start, resource_size(res), dc->ndev->name);
	if (!base_res) {
		dev_err(&dc->ndev->dev, "hdmi: request_mem_region failed\n");
		err = -EBUSY;
		goto err_free_hdmi;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(&dc->ndev->dev, "hdmi: registers can't be mapped\n");
		err = -EBUSY;
		goto err_release_resource_reg;
	}

	clk = clk_get(&dc->ndev->dev, "hdmi");
	if (IS_ERR_OR_NULL(clk)) {
		dev_err(&dc->ndev->dev, "hdmi: can't get clock\n");
		err = -ENOENT;
		goto err_iounmap_reg;
	}

	/* TODO: support non-hotplug */
	if (request_irq(gpio_to_irq(dc->out->hotplug_gpio), tegra_dc_hdmi_irq,
			IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			dev_name(&dc->ndev->dev), dc)) {
		dev_err(&dc->ndev->dev, "hdmi: request_irq %d failed\n",
			gpio_to_irq(dc->out->hotplug_gpio));
		err = -EBUSY;
		goto err_put_clock;
	}

	hdmi->edid = tegra_edid_create(dc->out->dcc_bus);
	if (IS_ERR_OR_NULL(hdmi->edid)) {
		dev_err(&dc->ndev->dev, "hdmi: can't create edid\n");
		err = PTR_ERR(hdmi->edid);
		goto err_free_irq;
	}

	INIT_DELAYED_WORK(&hdmi->work, tegra_dc_hdmi_detect_worker);

	hdmi->dc = dc;
	hdmi->base = base;
	hdmi->base_res = base_res;
	hdmi->clk = clk;

	tegra_dc_set_outdata(dc, hdmi);

	return 0;

err_free_irq:
	free_irq(gpio_to_irq(dc->out->hotplug_gpio), dc);
err_put_clock:
	clk_put(clk);
err_iounmap_reg:
	iounmap(base);
err_release_resource_reg:
	release_resource(base_res);
err_free_hdmi:
	kfree(hdmi);
	return err;
}

static void tegra_dc_hdmi_destroy(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi_data *hdmi = tegra_dc_get_outdata(dc);

	free_irq(gpio_to_irq(dc->out->hotplug_gpio), dc);
	cancel_delayed_work_sync(&hdmi->work);
	iounmap(hdmi->base);
	release_resource(hdmi->base_res);
	clk_put(hdmi->clk);
	tegra_edid_destroy(hdmi->edid);

	kfree(hdmi);

}

static void tegra_dc_hdmi_enable(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi_data *hdmi = tegra_dc_get_outdata(dc);
	int pulse_start;
	int dispclk_div_8_2;
	int pll0;
	int pll1;
	int ds;
	int retries;
	unsigned long val;

	/* enbale power, clocks, resets, etc. */
	tegra_dc_setup_clk(dc, hdmi->clk);
	clk_set_rate(hdmi->clk, dc->mode.pclk);

	clk_enable(hdmi->clk);
	tegra_periph_reset_assert(hdmi->clk);
	mdelay(1);
	tegra_periph_reset_deassert(hdmi->clk);

	/* TODO: copy HDCP keys from KFUSE to HDMI */

	/* Program display timing registers: handled by dc */

	/* program HDMI registers and SOR sequencer */

	tegra_dc_writel(dc, VSYNC_H_POSITION(1), DC_DISP_DISP_TIMING_OPTIONS);
	tegra_dc_writel(dc, DITHER_CONTROL_DISABLE | BASE_COLOR_SIZE888,
			DC_DISP_DISP_COLOR_CONTROL);

	/* video_preamble uses h_pulse2 */
	pulse_start = dc->mode.h_ref_to_sync + dc->mode.h_sync_width +
		dc->mode.h_back_porch - 10;
	tegra_dc_writel(dc, H_PULSE_2_ENABLE, DC_DISP_DISP_SIGNAL_OPTIONS0);
	tegra_dc_writel(dc,
			PULSE_MODE_NORMAL |
			PULSE_POLARITY_HIGH |
			PULSE_QUAL_VACTIVE |
			PULSE_LAST_END_A,
			DC_DISP_H_PULSE2_CONTROL);
	tegra_dc_writel(dc, PULSE_START(pulse_start) | PULSE_END(pulse_start + 8),
		  DC_DISP_H_PULSE2_POSITION_A);

	tegra_hdmi_writel(hdmi,
			  VSYNC_WINDOW_END(0x210) |
			  VSYNC_WINDOW_START(0x200) |
			  VSYNC_WINDOW_ENABLE,
			  HDMI_NV_PDISP_HDMI_VSYNC_WINDOW);

	/* TODO: scale output to 16-235 */

	tegra_hdmi_writel(hdmi,
			  (dc->ndev->id ? HDMI_SRC_DISPLAYB : HDMI_SRC_DISPLAYA) |
			  ARM_VIDEO_RANGE_LIMITED,
			  HDMI_NV_PDISP_INPUT_CONTROL);

	dispclk_div_8_2 = clk_get_rate(hdmi->clk) / 1000000 * 4;
	tegra_hdmi_writel(hdmi,
			  SOR_REFCLK_DIV_INT(dispclk_div_8_2 >> 2) |
			  SOR_REFCLK_DIV_FRAC(dispclk_div_8_2),
			  HDMI_NV_PDISP_SOR_REFCLK);

	/* TODO: setup audio */

	/* values from harmony board.  Will be replaced when
	 * audio and avi are supported */
	tegra_hdmi_writel(hdmi, 0x00000001, 0x1e);
	tegra_hdmi_writel(hdmi, 0x00000000, 0x20);
	tegra_hdmi_writel(hdmi, 0x000000aa, 0x21);
	tegra_hdmi_writel(hdmi, 0x00000001, 0x23);
	tegra_hdmi_writel(hdmi, 0x00000001, 0x24);
	tegra_hdmi_writel(hdmi, 0x00000000, 0x25);
	tegra_hdmi_writel(hdmi, 0x000445eb, 0x26);
	tegra_hdmi_writel(hdmi, 0x00000004, 0x27);
	tegra_hdmi_writel(hdmi, 0x00002710, 0x2a);
	tegra_hdmi_writel(hdmi, 0x00000000, 0x35);
	tegra_hdmi_writel(hdmi, 0x0015bc10, 0x38);
	tegra_hdmi_writel(hdmi, 0x04c4bb58, 0x39);
	tegra_hdmi_writel(hdmi, 0x0263b9b6, 0x44);
	tegra_hdmi_writel(hdmi, 0x00002713, 0x4f);
	tegra_hdmi_writel(hdmi, 0x01e85426, 0x57);
	tegra_hdmi_writel(hdmi, 0x001136c2, 0x89);
	tegra_hdmi_writel(hdmi, 0x00000730, 0x8a);
	tegra_hdmi_writel(hdmi, 0x0001875b, 0x8c);
	tegra_hdmi_writel(hdmi, 0x00000000, 0x9d);

	tegra_hdmi_writel(hdmi, 0x40090038, HDMI_NV_PDISP_HDMI_CTRL);
	tegra_hdmi_writel(hdmi, 0x0, HDMI_NV_PDISP_HDMI_GENERIC_CTRL);

	/* TMDS CONFIG */
	pll0 = 0x200033f;
	pll1 = 0;

	pll0 &= ~SOR_PLL_PWR & ~SOR_PLL_VCOPD & ~SOR_PLL_PDBG & ~SOR_PLL_PDPORT & ~SOR_PLL_PULLDOWN &
		~SOR_PLL_VCOCAP(~0) & ~SOR_PLL_ICHPMP(~0);
	pll0 |= SOR_PLL_RESISTORSEL;

	if (dc->mode.pclk <= 27000000)
		pll0 |= SOR_PLL_VCOCAP(0);
	else if (dc->mode.pclk <= 74250000)
		pll0 |= SOR_PLL_VCOCAP(1);
	else
		pll0 |= SOR_PLL_VCOCAP(3);

	if (dc->mode.h_active == 1080) {
		pll0 |= SOR_PLL_ICHPMP(1) | SOR_PLL_TX_REG_LOAD(3) |
			SOR_PLL_TX_REG_LOAD(3) | SOR_PLL_BG_V17_S(3);
		pll1 |= SOR_PLL_TMDS_TERM_ENABLE | SOR_PLL_PE_EN;
	} else {
		pll0 |= SOR_PLL_ICHPMP(2);
	}

	tegra_hdmi_writel(hdmi, pll0, HDMI_NV_PDISP_SOR_PLL0);
	tegra_hdmi_writel(hdmi, pll1, HDMI_NV_PDISP_SOR_PLL1);

	if (pll1 & SOR_PLL_PE_EN) {
		tegra_hdmi_writel(hdmi,
				  PE_CURRENT0(0xf) |
				  PE_CURRENT1(0xf) |
				  PE_CURRENT2(0xf) |
				  PE_CURRENT3(0xf),
				  HDMI_NV_PDISP_PE_CURRENT);
	}

	/* enable SOR */
	if (dc->mode.h_active == 1080)
		ds = DRIVE_CURRENT_13_500_mA;
	else
		ds = DRIVE_CURRENT_5_250_mA;

	tegra_hdmi_writel(hdmi,
			  DRIVE_CURRENT_LANE0(ds) |
			  DRIVE_CURRENT_LANE1(ds) |
			  DRIVE_CURRENT_LANE2(ds) |
			  DRIVE_CURRENT_LANE3(ds) |
			  DRIVE_CURRENT_FUSE_OVERRIDE,
			  HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT);

	tegra_hdmi_writel(hdmi,
			  SOR_SEQ_CTL_PU_PC(0) |
			  SOR_SEQ_PU_PC_ALT(0) |
			  SOR_SEQ_PD_PC(8) |
			  SOR_SEQ_PD_PC_ALT(8),
			  HDMI_NV_PDISP_SOR_SEQ_CTL);

	val = SOR_SEQ_INST_WAIT_TIME(1) |
		SOR_SEQ_INST_WAIT_UNITS_VSYNC |
		SOR_SEQ_INST_HALT |
		SOR_SEQ_INST_PIN_A_LOW |
		SOR_SEQ_INST_PIN_B_LOW |
		SOR_SEQ_INST_DRIVE_PWM_OUT_LO;

	tegra_hdmi_writel(hdmi, val, HDMI_NV_PDISP_SOR_SEQ_INST0);
	tegra_hdmi_writel(hdmi, val, HDMI_NV_PDISP_SOR_SEQ_INST8);

	val = 0x1c800;
	val &= ~SOR_CSTM_ROTCLK(~0);
	val |= SOR_CSTM_ROTCLK(2);
	tegra_hdmi_writel(hdmi, val, HDMI_NV_PDISP_SOR_CSTM);


	tegra_dc_writel(dc, DISP_CTRL_MODE_STOP, DC_CMD_DISPLAY_COMMAND);
	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);


	/* start SOR */
	tegra_hdmi_writel(hdmi,
			  SOR_PWR_NORMAL_STATE_PU |
			  SOR_PWR_NORMAL_START_NORMAL |
			  SOR_PWR_SAFE_STATE_PD |
			  SOR_PWR_SETTING_NEW_TRIGGER,
			  HDMI_NV_PDISP_SOR_PWR);
	tegra_hdmi_writel(hdmi,
			  SOR_PWR_NORMAL_STATE_PU |
			  SOR_PWR_NORMAL_START_NORMAL |
			  SOR_PWR_SAFE_STATE_PD |
			  SOR_PWR_SETTING_NEW_DONE,
			  HDMI_NV_PDISP_SOR_PWR);

	retries = 1000;
	do {
		BUG_ON(--retries < 0);
		val = tegra_hdmi_readl(hdmi, HDMI_NV_PDISP_SOR_PWR);
	} while (val & SOR_PWR_SETTING_NEW_PENDING);

	tegra_hdmi_writel(hdmi,
			  SOR_STATE_ASY_CRCMODE_COMPLETE |
			  SOR_STATE_ASY_OWNER_HEAD0 |
			  SOR_STATE_ASY_SUBOWNER_BOTH |
			  SOR_STATE_ASY_PROTOCOL_SINGLE_TMDS_A |
			  /* TODO: to look at hsync polarity */
			  SOR_STATE_ASY_HSYNCPOL_POS |
			  SOR_STATE_ASY_VSYNCPOL_POS |
			  SOR_STATE_ASY_DEPOL_POS,
			  HDMI_NV_PDISP_SOR_STATE2);

	val = SOR_STATE_ASY_HEAD_OPMODE_AWAKE | SOR_STATE_ASY_ORMODE_NORMAL;
	tegra_hdmi_writel(hdmi, val, HDMI_NV_PDISP_SOR_STATE1);

	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_SOR_STATE0);
	tegra_hdmi_writel(hdmi, SOR_STATE_UPDATE, HDMI_NV_PDISP_SOR_STATE0);
	tegra_hdmi_writel(hdmi, val | SOR_STATE_ATTACHED,
			  HDMI_NV_PDISP_SOR_STATE1);
	tegra_hdmi_writel(hdmi, 0, HDMI_NV_PDISP_SOR_STATE0);

	tegra_dc_writel(dc, HDMI_ENABLE, DC_DISP_DISP_WIN_OPTIONS);

	tegra_dc_writel(dc, PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
			PW4_ENABLE | PM0_ENABLE | PM1_ENABLE,
			DC_CMD_DISPLAY_POWER_CONTROL);

	tegra_dc_writel(dc, DISP_CTRL_MODE_C_DISPLAY, DC_CMD_DISPLAY_COMMAND);
	tegra_dc_writel(dc, GENERAL_ACT_REQ << 8, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, GENERAL_ACT_REQ, DC_CMD_STATE_CONTROL);
}

static void tegra_dc_hdmi_disable(struct tegra_dc *dc)
{
	struct tegra_dc_hdmi_data *hdmi = tegra_dc_get_outdata(dc);

	tegra_periph_reset_assert(hdmi->clk);
	clk_disable(hdmi->clk);
}
struct tegra_dc_out_ops tegra_dc_hdmi_ops = {
	.init = tegra_dc_hdmi_init,
	.destroy = tegra_dc_hdmi_destroy,
	.enable = tegra_dc_hdmi_enable,
	.disable = tegra_dc_hdmi_disable,
	.detect = tegra_dc_hdmi_detect,
};

