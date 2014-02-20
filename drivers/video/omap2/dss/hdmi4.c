/*
 * HDMI interface DSS driver for TI's OMAP4 family of SoCs.
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

#include "hdmi4_core.h"
#include "dss.h"
#include "dss_features.h"

static struct {
	struct mutex lock;
	struct platform_device *pdev;

	struct hdmi_wp_data	wp;
	struct hdmi_pll_data	pll;
	struct hdmi_phy_data	phy;
	struct hdmi_core_data	core;

	struct hdmi_config cfg;

	struct clk *sys_clk;
	struct regulator *vdda_hdmi_dac_reg;

	bool core_enabled;

	struct omap_dss_device output;
} hdmi;

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
		if (PTR_ERR(reg) != -EPROBE_DEFER)
			DSSERR("can't get VDDA_HDMI_DAC regulator\n");
		return PTR_ERR(reg);
	}

	hdmi.vdda_hdmi_dac_reg = reg;

	return 0;
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

	p = &hdmi.cfg.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n", p->x_res, p->y_res);

	phy = p->pixel_clock;

	hdmi_pll_compute(&hdmi.pll, clk_get_rate(hdmi.sys_clk), phy);

	/* config the PLL and PHY hdmi_set_pll_pwrfirst */
	r = hdmi_pll_enable(&hdmi.pll, &hdmi.wp);
	if (r) {
		DSSDBG("Failed to lock PLL\n");
		goto err_pll_enable;
	}

	r = hdmi_phy_enable(&hdmi.phy, &hdmi.wp, &hdmi.cfg);
	if (r) {
		DSSDBG("Failed to start PHY\n");
		goto err_phy_enable;
	}

	hdmi4_configure(&hdmi.core, &hdmi.wp, &hdmi.cfg);

	/* bypass TV gamma table */
	dispc_enable_gamma_table(0);

	/* tv size */
	dss_mgr_set_timings(mgr, p);

	r = hdmi_wp_video_start(&hdmi.wp);
	if (r)
		goto err_vid_enable;

	r = dss_mgr_enable(mgr);
	if (r)
		goto err_mgr_enable;

	return 0;

err_mgr_enable:
	hdmi_wp_video_stop(&hdmi.wp);
err_vid_enable:
	hdmi_phy_disable(&hdmi.phy, &hdmi.wp);
err_phy_enable:
	hdmi_pll_disable(&hdmi.pll, &hdmi.wp);
err_pll_enable:
	hdmi_power_off_core(dssdev);
	return -EIO;
}

static void hdmi_power_off_full(struct omap_dss_device *dssdev)
{
	struct omap_overlay_manager *mgr = hdmi.output.manager;

	dss_mgr_disable(mgr);

	hdmi_wp_video_stop(&hdmi.wp);
	hdmi_phy_disable(&hdmi.phy, &hdmi.wp);
	hdmi_pll_disable(&hdmi.pll, &hdmi.wp);

	hdmi_power_off_core(dssdev);
}

static int hdmi_display_check_timing(struct omap_dss_device *dssdev,
					struct omap_video_timings *timings)
{
	struct omap_dss_device *out = &hdmi.output;

	if (!dispc_mgr_timings_ok(out->dispc_channel, timings))
		return -EINVAL;

	return 0;
}

static void hdmi_display_set_timing(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct hdmi_cm cm;
	const struct hdmi_config *t;

	mutex_lock(&hdmi.lock);

	cm = hdmi_get_code(timings);
	hdmi.cfg.cm = cm;

	t = hdmi_get_timings(cm.mode, cm.code);
	if (t != NULL) {
		hdmi.cfg = *t;

		dispc_set_tv_pclk(t->timings.pixel_clock * 1000);
	} else {
		hdmi.cfg.timings = *timings;
		hdmi.cfg.cm.code = 0;
		hdmi.cfg.cm.mode = HDMI_DVI;

		dispc_set_tv_pclk(timings->pixel_clock * 1000);
	}

	DSSDBG("using mode: %s, code %d\n", hdmi.cfg.cm.mode == HDMI_DVI ?
			"DVI" : "HDMI", hdmi.cfg.cm.code);

	mutex_unlock(&hdmi.lock);
}

static void hdmi_display_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	const struct hdmi_config *cfg;
	struct hdmi_cm cm = hdmi.cfg.cm;

	cfg = hdmi_get_timings(cm.mode, cm.code);
	if (cfg == NULL)
		cfg = hdmi_default_timing();

	memcpy(timings, &cfg->timings, sizeof(cfg->timings));
}

static void hdmi_dump_regs(struct seq_file *s)
{
	mutex_lock(&hdmi.lock);

	if (hdmi_runtime_get()) {
		mutex_unlock(&hdmi.lock);
		return;
	}

	hdmi_wp_dump(&hdmi.wp, s);
	hdmi_pll_dump(&hdmi.pll, s);
	hdmi_phy_dump(&hdmi.phy, s);
	hdmi4_core_dump(&hdmi.core, s);

	hdmi_runtime_put();
	mutex_unlock(&hdmi.lock);
}

static int read_edid(u8 *buf, int len)
{
	int r;

	mutex_lock(&hdmi.lock);

	r = hdmi_runtime_get();
	BUG_ON(r);

	r = hdmi4_read_edid(&hdmi.core,  buf, len);

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

static int hdmi_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct omap_overlay_manager *mgr;
	int r;

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
	WARN_ON(dst != dssdev->dst);

	if (dst != dssdev->dst)
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

	if (!hdmi_mode_has_audio(hdmi.cfg.cm.mode)) {
		r = -EPERM;
		goto err;
	}

	r = hdmi_wp_audio_enable(&hdmi.wp, true);
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
	hdmi_wp_audio_enable(&hdmi.wp, false);
}

static int hdmi_audio_start(struct omap_dss_device *dssdev)
{
	return hdmi4_audio_start(&hdmi.core, &hdmi.wp);
}

static void hdmi_audio_stop(struct omap_dss_device *dssdev)
{
	hdmi4_audio_stop(&hdmi.core, &hdmi.wp);
}

static bool hdmi_audio_supported(struct omap_dss_device *dssdev)
{
	bool r;

	mutex_lock(&hdmi.lock);

	r = hdmi_mode_has_audio(hdmi.cfg.cm.mode);

	mutex_unlock(&hdmi.lock);
	return r;
}

static int hdmi_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	int r;
	u32 pclk = hdmi.cfg.timings.pixel_clock;

	mutex_lock(&hdmi.lock);

	if (!hdmi_mode_has_audio(hdmi.cfg.cm.mode)) {
		r = -EPERM;
		goto err;
	}

	r = hdmi4_audio_config(&hdmi.core, &hdmi.wp, audio, pclk);
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
	int r;

	hdmi.pdev = pdev;

	mutex_init(&hdmi.lock);

	r = hdmi_wp_init(pdev, &hdmi.wp);
	if (r)
		return r;

	r = hdmi_pll_init(pdev, &hdmi.pll);
	if (r)
		return r;

	r = hdmi_phy_init(pdev, &hdmi.phy);
	if (r)
		return r;

	r = hdmi4_core_init(pdev, &hdmi.core);
	if (r)
		return r;

	r = hdmi_get_clocks(pdev);
	if (r) {
		DSSERR("can't get clocks\n");
		return r;
	}

	pm_runtime_enable(&pdev->dev);

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

int __init hdmi4_init_platform_driver(void)
{
	return platform_driver_register(&omapdss_hdmihw_driver);
}

void __exit hdmi4_uninit_platform_driver(void)
{
	platform_driver_unregister(&omapdss_hdmihw_driver);
}
