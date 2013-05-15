/*
 * hdmi.c
 *
 * HDMI interface DSS driver setting for TI's OMAP4 family of processor.
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 * Authors: Yong Zhi
 *	Mythri pk <mythripk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "HDMI"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <video/omapdss.h>

#include "ti_hdmi.h"
#include "dss.h"
#include "dss_features.h"

#define HDMI_WP			0x0
#define HDMI_CORE_SYS		0x400
#define HDMI_CORE_AV		0x900
#define HDMI_PLLCTRL		0x200
#define HDMI_PHY		0x300

/* HDMI EDID Length move this */
#define HDMI_EDID_MAX_LENGTH			256
#define EDID_TIMING_DESCRIPTOR_SIZE		0x12
#define EDID_DESCRIPTOR_BLOCK0_ADDRESS		0x36
#define EDID_DESCRIPTOR_BLOCK1_ADDRESS		0x80
#define EDID_SIZE_BLOCK0_TIMING_DESCRIPTOR	4
#define EDID_SIZE_BLOCK1_TIMING_DESCRIPTOR	4

#define HDMI_DEFAULT_REGN 16
#define HDMI_DEFAULT_REGM2 1

static struct {
	struct mutex lock;
	struct platform_device *pdev;

	struct hdmi_ip_data ip_data;

	struct clk *sys_clk;
	struct regulator *vdda_hdmi_dac_reg;

	bool core_enabled;

	struct omap_dss_device output;
} hdmi;

/*
 * Logic for the below structure :
 * user enters the CEA or VESA timings by specifying the HDMI/DVI code.
 * There is a correspondence between CEA/VESA timing and code, please
 * refer to section 6.3 in HDMI 1.3 specification for timing code.
 *
 * In the below structure, cea_vesa_timings corresponds to all OMAP4
 * supported CEA and VESA timing values.code_cea corresponds to the CEA
 * code, It is used to get the timing from cea_vesa_timing array.Similarly
 * with code_vesa. Code_index is used for back mapping, that is once EDID
 * is read from the TV, EDID is parsed to find the timing values and then
 * map it to corresponding CEA or VESA index.
 */

static const struct hdmi_config cea_timings[] = {
	{
		{ 640, 480, 25200, 96, 16, 48, 2, 10, 33,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 1, HDMI_HDMI },
	},
	{
		{ 720, 480, 27027, 62, 16, 60, 6, 9, 30,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 2, HDMI_HDMI },
	},
	{
		{ 1280, 720, 74250, 40, 110, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 4, HDMI_HDMI },
	},
	{
		{ 1920, 540, 74250, 44, 88, 148, 5, 2, 15,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			true, },
		{ 5, HDMI_HDMI },
	},
	{
		{ 1440, 240, 27027, 124, 38, 114, 3, 4, 15,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			true, },
		{ 6, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 148500, 44, 88, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 16, HDMI_HDMI },
	},
	{
		{ 720, 576, 27000, 64, 12, 68, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 17, HDMI_HDMI },
	},
	{
		{ 1280, 720, 74250, 40, 440, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 19, HDMI_HDMI },
	},
	{
		{ 1920, 540, 74250, 44, 528, 148, 5, 2, 15,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			true, },
		{ 20, HDMI_HDMI },
	},
	{
		{ 1440, 288, 27000, 126, 24, 138, 3, 2, 19,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			true, },
		{ 21, HDMI_HDMI },
	},
	{
		{ 1440, 576, 54000, 128, 24, 136, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 29, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 148500, 44, 528, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 31, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 74250, 44, 638, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 32, HDMI_HDMI },
	},
	{
		{ 2880, 480, 108108, 248, 64, 240, 6, 9, 30,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 35, HDMI_HDMI },
	},
	{
		{ 2880, 576, 108000, 256, 48, 272, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 37, HDMI_HDMI },
	},
};

static const struct hdmi_config vesa_timings[] = {
/* VESA From Here */
	{
		{ 640, 480, 25175, 96, 16, 48, 2, 11, 31,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 4, HDMI_DVI },
	},
	{
		{ 800, 600, 40000, 128, 40, 88, 4, 1, 23,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 9, HDMI_DVI },
	},
	{
		{ 848, 480, 33750, 112, 16, 112, 8, 6, 23,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0xE, HDMI_DVI },
	},
	{
		{ 1280, 768, 79500, 128, 64, 192, 7, 3, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x17, HDMI_DVI },
	},
	{
		{ 1280, 800, 83500, 128, 72, 200, 6, 3, 22,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x1C, HDMI_DVI },
	},
	{
		{ 1360, 768, 85500, 112, 64, 256, 6, 3, 18,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x27, HDMI_DVI },
	},
	{
		{ 1280, 960, 108000, 112, 96, 312, 3, 1, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x20, HDMI_DVI },
	},
	{
		{ 1280, 1024, 108000, 112, 48, 248, 3, 1, 38,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x23, HDMI_DVI },
	},
	{
		{ 1024, 768, 65000, 136, 24, 160, 6, 3, 29,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x10, HDMI_DVI },
	},
	{
		{ 1400, 1050, 121750, 144, 88, 232, 4, 3, 32,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x2A, HDMI_DVI },
	},
	{
		{ 1440, 900, 106500, 152, 80, 232, 6, 3, 25,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x2F, HDMI_DVI },
	},
	{
		{ 1680, 1050, 146250, 176 , 104, 280, 6, 3, 30,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x3A, HDMI_DVI },
	},
	{
		{ 1366, 768, 85500, 143, 70, 213, 3, 3, 24,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x51, HDMI_DVI },
	},
	{
		{ 1920, 1080, 148500, 44, 148, 80, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x52, HDMI_DVI },
	},
	{
		{ 1280, 768, 68250, 32, 48, 80, 7, 3, 12,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x16, HDMI_DVI },
	},
	{
		{ 1400, 1050, 101000, 32, 48, 80, 4, 3, 23,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x29, HDMI_DVI },
	},
	{
		{ 1680, 1050, 119000, 32, 48, 80, 6, 3, 21,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x39, HDMI_DVI },
	},
	{
		{ 1280, 800, 79500, 32, 48, 80, 6, 3, 14,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x1B, HDMI_DVI },
	},
	{
		{ 1280, 720, 74250, 40, 110, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x55, HDMI_DVI },
	},
	{
		{ 1920, 1200, 154000, 32, 48, 80, 6, 3, 26,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x44, HDMI_DVI },
	},
};

static int hdmi_runtime_get(void)
{
	int r;

	DSSDBG("hdmi_runtime_get\n");

	r = pm_runtime_get_sync(&hdmi.pdev->dev);
	WARN_ON(r < 0);
	if (r < 0)
		return r;

	return 0;
}

static void hdmi_runtime_put(void)
{
	int r;

	DSSDBG("hdmi_runtime_put\n");

	r = pm_runtime_put_sync(&hdmi.pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static int hdmi_init_regulator(void)
{
	struct regulator *reg;

	if (hdmi.vdda_hdmi_dac_reg != NULL)
		return 0;

	reg = devm_regulator_get(&hdmi.pdev->dev, "vdda_hdmi_dac");

	/* DT HACK: try VDAC to make omapdss work for o4 sdp/panda */
	if (IS_ERR(reg))
		reg = devm_regulator_get(&hdmi.pdev->dev, "VDAC");

	if (IS_ERR(reg)) {
		DSSERR("can't get VDDA_HDMI_DAC regulator\n");
		return PTR_ERR(reg);
	}

	hdmi.vdda_hdmi_dac_reg = reg;

	return 0;
}

static const struct hdmi_config *hdmi_find_timing(
					const struct hdmi_config *timings_arr,
					int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (timings_arr[i].cm.code == hdmi.ip_data.cfg.cm.code)
			return &timings_arr[i];
	}
	return NULL;
}

static const struct hdmi_config *hdmi_get_timings(void)
{
       const struct hdmi_config *arr;
       int len;

       if (hdmi.ip_data.cfg.cm.mode == HDMI_DVI) {
               arr = vesa_timings;
               len = ARRAY_SIZE(vesa_timings);
       } else {
               arr = cea_timings;
               len = ARRAY_SIZE(cea_timings);
       }

       return hdmi_find_timing(arr, len);
}

static bool hdmi_timings_compare(struct omap_video_timings *timing1,
				const struct omap_video_timings *timing2)
{
	int timing1_vsync, timing1_hsync, timing2_vsync, timing2_hsync;

	if ((DIV_ROUND_CLOSEST(timing2->pixel_clock, 1000) ==
			DIV_ROUND_CLOSEST(timing1->pixel_clock, 1000)) &&
		(timing2->x_res == timing1->x_res) &&
		(timing2->y_res == timing1->y_res)) {

		timing2_hsync = timing2->hfp + timing2->hsw + timing2->hbp;
		timing1_hsync = timing1->hfp + timing1->hsw + timing1->hbp;
		timing2_vsync = timing2->vfp + timing2->vsw + timing2->vbp;
		timing1_vsync = timing2->vfp + timing2->vsw + timing2->vbp;

		DSSDBG("timing1_hsync = %d timing1_vsync = %d"\
			"timing2_hsync = %d timing2_vsync = %d\n",
			timing1_hsync, timing1_vsync,
			timing2_hsync, timing2_vsync);

		if ((timing1_hsync == timing2_hsync) &&
			(timing1_vsync == timing2_vsync)) {
			return true;
		}
	}
	return false;
}

static struct hdmi_cm hdmi_get_code(struct omap_video_timings *timing)
{
	int i;
	struct hdmi_cm cm = {-1};
	DSSDBG("hdmi_get_code\n");

	for (i = 0; i < ARRAY_SIZE(cea_timings); i++) {
		if (hdmi_timings_compare(timing, &cea_timings[i].timings)) {
			cm = cea_timings[i].cm;
			goto end;
		}
	}
	for (i = 0; i < ARRAY_SIZE(vesa_timings); i++) {
		if (hdmi_timings_compare(timing, &vesa_timings[i].timings)) {
			cm = vesa_timings[i].cm;
			goto end;
		}
	}

end:	return cm;

}

static void hdmi_compute_pll(struct omap_dss_device *dssdev, int phy,
		struct hdmi_pll_info *pi)
{
	unsigned long clkin, refclk;
	u32 mf;

	clkin = clk_get_rate(hdmi.sys_clk) / 10000;
	/*
	 * Input clock is predivided by N + 1
	 * out put of which is reference clk
	 */

	pi->regn = HDMI_DEFAULT_REGN;

	refclk = clkin / pi->regn;

	pi->regm2 = HDMI_DEFAULT_REGM2;

	/*
	 * multiplier is pixel_clk/ref_clk
	 * Multiplying by 100 to avoid fractional part removal
	 */
	pi->regm = phy * pi->regm2 / refclk;

	/*
	 * fractional multiplier is remainder of the difference between
	 * multiplier and actual phy(required pixel clock thus should be
	 * multiplied by 2^18(262144) divided by the reference clock
	 */
	mf = (phy - pi->regm / pi->regm2 * refclk) * 262144;
	pi->regmf = pi->regm2 * mf / refclk;

	/*
	 * Dcofreq should be set to 1 if required pixel clock
	 * is greater than 1000MHz
	 */
	pi->dcofreq = phy > 1000 * 100;
	pi->regsd = ((pi->regm * clkin / 10) / (pi->regn * 250) + 5) / 10;

	/* Set the reference clock to sysclk reference */
	pi->refsel = HDMI_REFSEL_SYSCLK;

	DSSDBG("M = %d Mf = %d\n", pi->regm, pi->regmf);
	DSSDBG("range = %d sd = %d\n", pi->dcofreq, pi->regsd);
}

static int hdmi_power_on_core(struct omap_dss_device *dssdev)
{
	int r;

	r = regulator_enable(hdmi.vdda_hdmi_dac_reg);
	if (r)
		return r;

	r = hdmi_runtime_get();
	if (r)
		goto err_runtime_get;

	/* Make selection of HDMI in DSS */
	dss_select_hdmi_venc_clk_source(DSS_HDMI_M_PCLK);

	hdmi.core_enabled = true;

	return 0;

err_runtime_get:
	regulator_disable(hdmi.vdda_hdmi_dac_reg);

	return r;
}

static void hdmi_power_off_core(struct omap_dss_device *dssdev)
{
	hdmi.core_enabled = false;

	hdmi_runtime_put();
	regulator_disable(hdmi.vdda_hdmi_dac_reg);
}

static int hdmi_power_on_full(struct omap_dss_device *dssdev)
{
	int r;
	struct omap_video_timings *p;
	struct omap_overlay_manager *mgr = hdmi.output.manager;
	unsigned long phy;

	r = hdmi_power_on_core(dssdev);
	if (r)
		return r;

	dss_mgr_disable(mgr);

	p = &hdmi.ip_data.cfg.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n", p->x_res, p->y_res);

	phy = p->pixel_clock;

	hdmi_compute_pll(dssdev, phy, &hdmi.ip_data.pll_data);

	hdmi.ip_data.ops->video_disable(&hdmi.ip_data);

	/* config the PLL and PHY hdmi_set_pll_pwrfirst */
	r = hdmi.ip_data.ops->pll_enable(&hdmi.ip_data);
	if (r) {
		DSSDBG("Failed to lock PLL\n");
		goto err_pll_enable;
	}

	r = hdmi.ip_data.ops->phy_enable(&hdmi.ip_data);
	if (r) {
		DSSDBG("Failed to start PHY\n");
		goto err_phy_enable;
	}

	hdmi.ip_data.ops->video_configure(&hdmi.ip_data);

	/* bypass TV gamma table */
	dispc_enable_gamma_table(0);

	/* tv size */
	dss_mgr_set_timings(mgr, p);

	r = hdmi.ip_data.ops->video_enable(&hdmi.ip_data);
	if (r)
		goto err_vid_enable;

	r = dss_mgr_enable(mgr);
	if (r)
		goto err_mgr_enable;

	return 0;

err_mgr_enable:
	hdmi.ip_data.ops->video_disable(&hdmi.ip_data);
err_vid_enable:
	hdmi.ip_data.ops->phy_disable(&hdmi.ip_data);
err_phy_enable:
	hdmi.ip_data.ops->pll_disable(&hdmi.ip_data);
err_pll_enable:
	hdmi_power_off_core(dssdev);
	return -EIO;
}

static void hdmi_power_off_full(struct omap_dss_device *dssdev)
{
	struct omap_overlay_manager *mgr = hdmi.output.manager;

	dss_mgr_disable(mgr);

	hdmi.ip_data.ops->video_disable(&hdmi.ip_data);
	hdmi.ip_data.ops->phy_disable(&hdmi.ip_data);
	hdmi.ip_data.ops->pll_disable(&hdmi.ip_data);

	hdmi_power_off_core(dssdev);
}

static int hdmi_display_check_timing(struct omap_dss_device *dssdev,
					struct omap_video_timings *timings)
{
	struct hdmi_cm cm;

	cm = hdmi_get_code(timings);
	if (cm.code == -1) {
		return -EINVAL;
	}

	return 0;

}

static void hdmi_display_set_timing(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct hdmi_cm cm;
	const struct hdmi_config *t;

	mutex_lock(&hdmi.lock);

	cm = hdmi_get_code(timings);
	hdmi.ip_data.cfg.cm = cm;

	t = hdmi_get_timings();
	if (t != NULL)
		hdmi.ip_data.cfg = *t;

	dispc_set_tv_pclk(t->timings.pixel_clock * 1000);

	mutex_unlock(&hdmi.lock);
}

static void hdmi_display_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	const struct hdmi_config *cfg;

	cfg = hdmi_get_timings();
	if (cfg == NULL)
		cfg = &vesa_timings[0];

	memcpy(timings, &cfg->timings, sizeof(cfg->timings));
}

static void hdmi_dump_regs(struct seq_file *s)
{
	mutex_lock(&hdmi.lock);

	if (hdmi_runtime_get()) {
		mutex_unlock(&hdmi.lock);
		return;
	}

	hdmi.ip_data.ops->dump_wrapper(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_pll(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_phy(&hdmi.ip_data, s);
	hdmi.ip_data.ops->dump_core(&hdmi.ip_data, s);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);
}

static int read_edid(u8 *buf, int len)
{
	int r;

	mutex_lock(&hdmi.lock);

	r = hdmi_runtime_get();
	BUG_ON(r);

	r = hdmi.ip_data.ops->read_edid(&hdmi.ip_data, buf, len);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);

	return r;
}

static int hdmi_display_enable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out = &hdmi.output;
	int r = 0;

	DSSDBG("ENTER hdmi_display_enable\n");

	mutex_lock(&hdmi.lock);

	if (out == NULL || out->manager == NULL) {
		DSSERR("failed to enable display: no output/manager\n");
		r = -ENODEV;
		goto err0;
	}

	r = hdmi_power_on_full(dssdev);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err0;
	}

	mutex_unlock(&hdmi.lock);
	return 0;

err0:
	mutex_unlock(&hdmi.lock);
	return r;
}

static void hdmi_display_disable(struct omap_dss_device *dssdev)
{
	DSSDBG("Enter hdmi_display_disable\n");

	mutex_lock(&hdmi.lock);

	hdmi_power_off_full(dssdev);

	mutex_unlock(&hdmi.lock);
}

static int hdmi_core_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	DSSDBG("ENTER omapdss_hdmi_core_enable\n");

	mutex_lock(&hdmi.lock);

	r = hdmi_power_on_core(dssdev);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err0;
	}

	mutex_unlock(&hdmi.lock);
	return 0;

err0:
	mutex_unlock(&hdmi.lock);
	return r;
}

static void hdmi_core_disable(struct omap_dss_device *dssdev)
{
	DSSDBG("Enter omapdss_hdmi_core_disable\n");

	mutex_lock(&hdmi.lock);

	hdmi_power_off_core(dssdev);

	mutex_unlock(&hdmi.lock);
}

static int hdmi_get_clocks(struct platform_device *pdev)
{
	struct clk *clk;

	clk = devm_clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get sys_clk\n");
		return PTR_ERR(clk);
	}

	hdmi.sys_clk = clk;

	return 0;
}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
int hdmi_compute_acr(u32 sample_freq, u32 *n, u32 *cts)
{
	u32 deep_color;
	bool deep_color_correct = false;
	u32 pclk = hdmi.ip_data.cfg.timings.pixel_clock;

	if (n == NULL || cts == NULL)
		return -EINVAL;

	/* TODO: When implemented, query deep color mode here. */
	deep_color = 100;

	/*
	 * When using deep color, the default N value (as in the HDMI
	 * specification) yields to an non-integer CTS. Hence, we
	 * modify it while keeping the restrictions described in
	 * section 7.2.1 of the HDMI 1.4a specification.
	 */
	switch (sample_freq) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		if (deep_color == 125)
			if (pclk == 27027 || pclk == 74250)
				deep_color_correct = true;
		if (deep_color == 150)
			if (pclk == 27027)
				deep_color_correct = true;
		break;
	case 44100:
	case 88200:
	case 176400:
		if (deep_color == 125)
			if (pclk == 27027)
				deep_color_correct = true;
		break;
	default:
		return -EINVAL;
	}

	if (deep_color_correct) {
		switch (sample_freq) {
		case 32000:
			*n = 8192;
			break;
		case 44100:
			*n = 12544;
			break;
		case 48000:
			*n = 8192;
			break;
		case 88200:
			*n = 25088;
			break;
		case 96000:
			*n = 16384;
			break;
		case 176400:
			*n = 50176;
			break;
		case 192000:
			*n = 32768;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (sample_freq) {
		case 32000:
			*n = 4096;
			break;
		case 44100:
			*n = 6272;
			break;
		case 48000:
			*n = 6144;
			break;
		case 88200:
			*n = 12544;
			break;
		case 96000:
			*n = 12288;
			break;
		case 176400:
			*n = 25088;
			break;
		case 192000:
			*n = 24576;
			break;
		default:
			return -EINVAL;
		}
	}
	/* Calculate CTS. See HDMI 1.3a or 1.4a specifications */
	*cts = pclk * (*n / 128) * deep_color / (sample_freq / 10);

	return 0;
}

static bool hdmi_mode_has_audio(void)
{
	if (hdmi.ip_data.cfg.cm.mode == HDMI_HDMI)
		return true;
	else
		return false;
}

#endif

static int hdmi_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct omap_overlay_manager *mgr;
	int r;

	dss_init_hdmi_ip_ops(&hdmi.ip_data, omapdss_get_version());

	r = hdmi_init_regulator();
	if (r)
		return r;

	mgr = omap_dss_get_overlay_manager(dssdev->dispc_channel);
	if (!mgr)
		return -ENODEV;

	r = dss_mgr_connect(mgr, dssdev);
	if (r)
		return r;

	r = omapdss_output_set_device(dssdev, dst);
	if (r) {
		DSSERR("failed to connect output to new device: %s\n",
				dst->name);
		dss_mgr_disconnect(mgr, dssdev);
		return r;
	}

	return 0;
}

static void hdmi_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	WARN_ON(dst != dssdev->device);

	if (dst != dssdev->device)
		return;

	omapdss_output_unset_device(dssdev);

	if (dssdev->manager)
		dss_mgr_disconnect(dssdev->manager, dssdev);
}

static int hdmi_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	bool need_enable;
	int r;

	need_enable = hdmi.core_enabled == false;

	if (need_enable) {
		r = hdmi_core_enable(dssdev);
		if (r)
			return r;
	}

	r = read_edid(edid, len);

	if (need_enable)
		hdmi_core_disable(dssdev);

	return r;
}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
static int hdmi_audio_enable(struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&hdmi.lock);

	if (!hdmi_mode_has_audio()) {
		r = -EPERM;
		goto err;
	}


	r = hdmi.ip_data.ops->audio_enable(&hdmi.ip_data);
	if (r)
		goto err;

	mutex_unlock(&hdmi.lock);
	return 0;

err:
	mutex_unlock(&hdmi.lock);
	return r;
}

static void hdmi_audio_disable(struct omap_dss_device *dssdev)
{
	hdmi.ip_data.ops->audio_disable(&hdmi.ip_data);
}

static int hdmi_audio_start(struct omap_dss_device *dssdev)
{
	return hdmi.ip_data.ops->audio_start(&hdmi.ip_data);
}

static void hdmi_audio_stop(struct omap_dss_device *dssdev)
{
	hdmi.ip_data.ops->audio_stop(&hdmi.ip_data);
}

static bool hdmi_audio_supported(struct omap_dss_device *dssdev)
{
	bool r;

	mutex_lock(&hdmi.lock);

	r = hdmi_mode_has_audio();

	mutex_unlock(&hdmi.lock);
	return r;
}

static int hdmi_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	int r;

	mutex_lock(&hdmi.lock);

	if (!hdmi_mode_has_audio()) {
		r = -EPERM;
		goto err;
	}

	r = hdmi.ip_data.ops->audio_config(&hdmi.ip_data, audio);
	if (r)
		goto err;

	mutex_unlock(&hdmi.lock);
	return 0;

err:
	mutex_unlock(&hdmi.lock);
	return r;
}
#else
static int hdmi_audio_enable(struct omap_dss_device *dssdev)
{
	return -EPERM;
}

static void hdmi_audio_disable(struct omap_dss_device *dssdev)
{
}

static int hdmi_audio_start(struct omap_dss_device *dssdev)
{
	return -EPERM;
}

static void hdmi_audio_stop(struct omap_dss_device *dssdev)
{
}

static bool hdmi_audio_supported(struct omap_dss_device *dssdev)
{
	return false;
}

static int hdmi_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	return -EPERM;
}
#endif

static const struct omapdss_hdmi_ops hdmi_ops = {
	.connect		= hdmi_connect,
	.disconnect		= hdmi_disconnect,

	.enable			= hdmi_display_enable,
	.disable		= hdmi_display_disable,

	.check_timings		= hdmi_display_check_timing,
	.set_timings		= hdmi_display_set_timing,
	.get_timings		= hdmi_display_get_timings,

	.read_edid		= hdmi_read_edid,

	.audio_enable		= hdmi_audio_enable,
	.audio_disable		= hdmi_audio_disable,
	.audio_start		= hdmi_audio_start,
	.audio_stop		= hdmi_audio_stop,
	.audio_supported	= hdmi_audio_supported,
	.audio_config		= hdmi_audio_config,
};

static void hdmi_init_output(struct platform_device *pdev)
{
	struct omap_dss_device *out = &hdmi.output;

	out->dev = &pdev->dev;
	out->id = OMAP_DSS_OUTPUT_HDMI;
	out->output_type = OMAP_DISPLAY_TYPE_HDMI;
	out->name = "hdmi.0";
	out->dispc_channel = OMAP_DSS_CHANNEL_DIGIT;
	out->ops.hdmi = &hdmi_ops;
	out->owner = THIS_MODULE;

	omapdss_register_output(out);
}

static void __exit hdmi_uninit_output(struct platform_device *pdev)
{
	struct omap_dss_device *out = &hdmi.output;

	omapdss_unregister_output(out);
}

/* HDMI HW IP initialisation */
static int omapdss_hdmihw_probe(struct platform_device *pdev)
{
	struct resource *res;
	int r;

	hdmi.pdev = pdev;

	mutex_init(&hdmi.lock);
	mutex_init(&hdmi.ip_data.lock);

	res = platform_get_resource(hdmi.pdev, IORESOURCE_MEM, 0);

	/* Base address taken from platform */
	hdmi.ip_data.base_wp = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hdmi.ip_data.base_wp))
		return PTR_ERR(hdmi.ip_data.base_wp);

	hdmi.ip_data.irq = platform_get_irq(pdev, 0);
	if (hdmi.ip_data.irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	r = hdmi_get_clocks(pdev);
	if (r) {
		DSSERR("can't get clocks\n");
		return r;
	}

	pm_runtime_enable(&pdev->dev);

	hdmi.ip_data.core_sys_offset = HDMI_CORE_SYS;
	hdmi.ip_data.core_av_offset = HDMI_CORE_AV;
	hdmi.ip_data.pll_offset = HDMI_PLLCTRL;
	hdmi.ip_data.phy_offset = HDMI_PHY;

	hdmi_init_output(pdev);

	dss_debugfs_create_file("hdmi", hdmi_dump_regs);

	return 0;
}

static int __exit omapdss_hdmihw_remove(struct platform_device *pdev)
{
	hdmi_uninit_output(pdev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int hdmi_runtime_suspend(struct device *dev)
{
	clk_disable_unprepare(hdmi.sys_clk);

	dispc_runtime_put();

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	int r;

	r = dispc_runtime_get();
	if (r < 0)
		return r;

	clk_prepare_enable(hdmi.sys_clk);

	return 0;
}

static const struct dev_pm_ops hdmi_pm_ops = {
	.runtime_suspend = hdmi_runtime_suspend,
	.runtime_resume = hdmi_runtime_resume,
};

static struct platform_driver omapdss_hdmihw_driver = {
	.probe		= omapdss_hdmihw_probe,
	.remove         = __exit_p(omapdss_hdmihw_remove),
	.driver         = {
		.name   = "omapdss_hdmi",
		.owner  = THIS_MODULE,
		.pm	= &hdmi_pm_ops,
	},
};

int __init hdmi_init_platform_driver(void)
{
	return platform_driver_register(&omapdss_hdmihw_driver);
}

void __exit hdmi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omapdss_hdmihw_driver);
}
