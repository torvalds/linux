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

static irqreturn_t hdmi_irq_handler(int irq, void *data)
{
	struct hdmi_wp_data *wp = data;
	u32 irqstatus;

	irqstatus = hdmi_wp_get_irqstatus(wp);
	hdmi_wp_set_irqstatus(wp, irqstatus);

	if ((irqstatus & HDMI_IRQ_LINK_CONNECT) &&
			irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		/*
		 * If we get both connect and disconnect interrupts at the same
		 * time, turn off the PHY, clear interrupts, and restart, which
		 * raises connect interrupt if a cable is connected, or nothing
		 * if cable is not connected.
		 */
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_OFF);

		hdmi_wp_set_irqstatus(wp, HDMI_IRQ_LINK_CONNECT |
				HDMI_IRQ_LINK_DISCONNECT);

		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	} else if (irqstatus & HDMI_IRQ_LINK_CONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_TXON);
	} else if (irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	}

	return IRQ_HANDLED;
}

static int hdmi_init_regulator(void)
{
	int r;
	struct regulator *reg;

	if (hdmi.vdda_hdmi_dac_reg != NULL)
		return 0;

	reg = devm_regulator_get(&hdmi.pdev->dev, "vdda");

	if (IS_ERR(reg)) {
		if (PTR_ERR(reg) != -EPROBE_DEFER)
			DSSERR("can't get VDDA regulator\n");
		return PTR_ERR(reg);
	}

	if (regulator_can_change_voltage(reg)) {
		r = regulator_set_voltage(reg, 1800000, 1800000);
		if (r) {
			devm_regulator_put(reg);
			DSSWARN("can't set the regulator voltage\n");
			return r;
		}
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
	struct hdmi_wp_data *wp = &hdmi.wp;

	r = hdmi_power_on_core(dssdev);
	if (r)
		return r;

	/* disable and clear irqs */
	hdmi_wp_clear_irqenable(wp, 0xffffffff);
	hdmi_wp_set_irqstatus(wp, 0xffffffff);

	p = &hdmi.cfg.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n", p->x_res, p->y_res);

	/* the functions below use kHz pixel clock. TODO: change to Hz */
	phy = p->pixelclock / 1000;

	hdmi_pll_compute(&hdmi.pll, clk_get_rate(hdmi.sys_clk), phy);

	/* config the PLL and PHY hdmi_set_pll_pwrfirst */
	r = hdmi_pll_enable(&hdmi.pll, &hdmi.wp);
	if (r) {
		DSSDBG("Failed to lock PLL\n");
		goto err_pll_enable;
	}

	r = hdmi_phy_configure(&hdmi.phy, &hdmi.cfg);
	if (r) {
		DSSDBG("Failed to configure PHY\n");
		goto err_phy_cfg;
	}

	r = hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	if (r)
		goto err_phy_pwr;

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

	hdmi_wp_set_irqenable(wp,
		HDMI_IRQ_LINK_CONNECT | HDMI_IRQ_LINK_DISCONNECT);

	return 0;

err_mgr_enable:
	hdmi_wp_video_stop(&hdmi.wp);
err_vid_enable:
err_phy_cfg:
	hdmi_wp_set_phy_pwr(&hdmi.wp, HDMI_PHYPWRCMD_OFF);
err_phy_pwr:
	hdmi_pll_disable(&hdmi.pll, &hdmi.wp);
err_pll_enable:
	hdmi_power_off_core(dssdev);
	return -EIO;
}

static void hdmi_power_off_full(struct omap_dss_device *dssdev)
{
	struct omap_overlay_manager *mgr = hdmi.output.manager;

	hdmi_wp_clear_irqenable(&hdmi.wp, 0xffffffff);

	dss_mgr_disable(mgr);

	hdmi_wp_video_stop(&hdmi.wp);

	hdmi_wp_set_phy_pwr(&hdmi.wp, HDMI_PHYPWRCMD_OFF);

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
	mutex_lock(&hdmi.lock);

	hdmi.cfg.timings = *timings;

	dispc_set_tv_pclk(timings->pixelclock);

	mutex_unlock(&hdmi.lock);
}

static void hdmi_display_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = hdmi.cfg.timings;
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

	if (!hdmi_mode_has_audio(hdmi.cfg.hdmi_dvi_mode)) {
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

	r = hdmi_mode_has_audio(hdmi.cfg.hdmi_dvi_mode);

	mutex_unlock(&hdmi.lock);
	return r;
}

static int hdmi_audio_config(struct omap_dss_device *dssdev,
		struct omap_dss_audio *audio)
{
	int r;
	u32 pclk = hdmi.cfg.timings.pixelclock;

	mutex_lock(&hdmi.lock);

	if (!hdmi_mode_has_audio(hdmi.cfg.hdmi_dvi_mode)) {
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

static int hdmi_set_infoframe(struct omap_dss_device *dssdev,
		const struct hdmi_avi_infoframe *avi)
{
	hdmi.cfg.infoframe = *avi;
	return 0;
}

static int hdmi_set_hdmi_mode(struct omap_dss_device *dssdev,
		bool hdmi_mode)
{
	hdmi.cfg.hdmi_dvi_mode = hdmi_mode ? HDMI_HDMI : HDMI_DVI;
	return 0;
}

static const struct omapdss_hdmi_ops hdmi_ops = {
	.connect		= hdmi_connect,
	.disconnect		= hdmi_disconnect,

	.enable			= hdmi_display_enable,
	.disable		= hdmi_display_disable,

	.check_timings		= hdmi_display_check_timing,
	.set_timings		= hdmi_display_set_timing,
	.get_timings		= hdmi_display_get_timings,

	.read_edid		= hdmi_read_edid,
	.set_infoframe		= hdmi_set_infoframe,
	.set_hdmi_mode		= hdmi_set_hdmi_mode,

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

static int hdmi_probe_of(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *ep;
	int r;

	ep = omapdss_of_get_first_endpoint(node);
	if (!ep)
		return 0;

	r = hdmi_parse_lanes_of(pdev, ep, &hdmi.phy);
	if (r)
		goto err;

	of_node_put(ep);
	return 0;

err:
	of_node_put(ep);
	return r;
}

/* HDMI HW IP initialisation */
static int omapdss_hdmihw_probe(struct platform_device *pdev)
{
	int r;
	int irq;

	hdmi.pdev = pdev;

	mutex_init(&hdmi.lock);

	if (pdev->dev.of_node) {
		r = hdmi_probe_of(pdev);
		if (r)
			return r;
	}

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

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	r = devm_request_threaded_irq(&pdev->dev, irq,
			NULL, hdmi_irq_handler,
			IRQF_ONESHOT, "OMAP HDMI", &hdmi.wp);
	if (r) {
		DSSERR("HDMI IRQ request failed\n");
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

static const struct of_device_id hdmi_of_match[] = {
	{ .compatible = "ti,omap4-hdmi", },
	{},
};

static struct platform_driver omapdss_hdmihw_driver = {
	.probe		= omapdss_hdmihw_probe,
	.remove         = __exit_p(omapdss_hdmihw_remove),
	.driver         = {
		.name   = "omapdss_hdmi",
		.owner  = THIS_MODULE,
		.pm	= &hdmi_pm_ops,
		.of_match_table = hdmi_of_match,
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
