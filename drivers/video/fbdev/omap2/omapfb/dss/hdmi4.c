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
#include <linux/component.h>
#include <video/omapdss.h>
#include <sound/omap-hdmi-audio.h>

#include "hdmi4_core.h"
#include "dss.h"
#include "dss_features.h"
#include "hdmi.h"

static struct omap_hdmi hdmi;

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

	if (hdmi.vdda_reg != NULL)
		return 0;

	reg = devm_regulator_get(&hdmi.pdev->dev, "vdda");

	if (IS_ERR(reg)) {
		if (PTR_ERR(reg) != -EPROBE_DEFER)
			DSSERR("can't get VDDA regulator\n");
		return PTR_ERR(reg);
	}

	r = regulator_set_voltage(reg, 1800000, 1800000);
	if (r) {
		devm_regulator_put(reg);
		DSSWARN("can't set the regulator voltage\n");
		return r;
	}

	hdmi.vdda_reg = reg;

	return 0;
}

static int hdmi_power_on_core(struct omap_dss_device *dssdev)
{
	int r;

	r = regulator_enable(hdmi.vdda_reg);
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
	regulator_disable(hdmi.vdda_reg);

	return r;
}

static void hdmi_power_off_core(struct omap_dss_device *dssdev)
{
	hdmi.core_enabled = false;

	hdmi_runtime_put();
	regulator_disable(hdmi.vdda_reg);
}

static int hdmi_power_on_full(struct omap_dss_device *dssdev)
{
	int r;
	struct omap_video_timings *p;
	struct omap_overlay_manager *mgr = hdmi.output.manager;
	struct hdmi_wp_data *wp = &hdmi.wp;
	struct dss_pll_clock_info hdmi_cinfo = { 0 };

	r = hdmi_power_on_core(dssdev);
	if (r)
		return r;

	/* disable and clear irqs */
	hdmi_wp_clear_irqenable(wp, 0xffffffff);
	hdmi_wp_set_irqstatus(wp, 0xffffffff);

	p = &hdmi.cfg.timings;

	DSSDBG("hdmi_power_on x_res= %d y_res = %d\n", p->x_res, p->y_res);

	hdmi_pll_compute(&hdmi.pll, p->pixelclock, &hdmi_cinfo);

	r = dss_pll_enable(&hdmi.pll.pll);
	if (r) {
		DSSERR("Failed to enable PLL\n");
		goto err_pll_enable;
	}

	r = dss_pll_set_config(&hdmi.pll.pll, &hdmi_cinfo);
	if (r) {
		DSSERR("Failed to configure PLL\n");
		goto err_pll_cfg;
	}

	r = hdmi_phy_configure(&hdmi.phy, hdmi_cinfo.clkdco,
		hdmi_cinfo.clkout[0]);
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
	hdmi_wp_set_phy_pwr(&hdmi.wp, HDMI_PHYPWRCMD_OFF);
err_phy_pwr:
err_phy_cfg:
err_pll_cfg:
	dss_pll_disable(&hdmi.pll.pll);
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

	dss_pll_disable(&hdmi.pll.pll);

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

static void hdmi_start_audio_stream(struct omap_hdmi *hd)
{
	hdmi_wp_audio_enable(&hd->wp, true);
	hdmi4_audio_start(&hd->core, &hd->wp);
}

static void hdmi_stop_audio_stream(struct omap_hdmi *hd)
{
	hdmi4_audio_stop(&hd->core, &hd->wp);
	hdmi_wp_audio_enable(&hd->wp, false);
}

static int hdmi_display_enable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out = &hdmi.output;
	unsigned long flags;
	int r = 0;

	DSSDBG("ENTER hdmi_display_enable\n");

	mutex_lock(&hdmi.lock);

	if (out->manager == NULL) {
		DSSERR("failed to enable display: no output/manager\n");
		r = -ENODEV;
		goto err0;
	}

	r = hdmi_power_on_full(dssdev);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err0;
	}

	if (hdmi.audio_configured) {
		r = hdmi4_audio_config(&hdmi.core, &hdmi.wp, &hdmi.audio_config,
				       hdmi.cfg.timings.pixelclock);
		if (r) {
			DSSERR("Error restoring audio configuration: %d", r);
			hdmi.audio_abort_cb(&hdmi.pdev->dev);
			hdmi.audio_configured = false;
		}
	}

	spin_lock_irqsave(&hdmi.audio_playing_lock, flags);
	if (hdmi.audio_configured && hdmi.audio_playing)
		hdmi_start_audio_stream(&hdmi);
	hdmi.display_enabled = true;
	spin_unlock_irqrestore(&hdmi.audio_playing_lock, flags);

	mutex_unlock(&hdmi.lock);
	return 0;

err0:
	mutex_unlock(&hdmi.lock);
	return r;
}

static void hdmi_display_disable(struct omap_dss_device *dssdev)
{
	unsigned long flags;

	DSSDBG("Enter hdmi_display_disable\n");

	mutex_lock(&hdmi.lock);

	spin_lock_irqsave(&hdmi.audio_playing_lock, flags);
	hdmi_stop_audio_stream(&hdmi);
	hdmi.display_enabled = false;
	spin_unlock_irqrestore(&hdmi.audio_playing_lock, flags);

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

static void hdmi_uninit_output(struct platform_device *pdev)
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

/* Audio callbacks */
static int hdmi_audio_startup(struct device *dev,
			      void (*abort_cb)(struct device *dev))
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&hd->lock);

	if (!hdmi_mode_has_audio(&hd->cfg) || !hd->display_enabled) {
		ret = -EPERM;
		goto out;
	}

	hd->audio_abort_cb = abort_cb;

out:
	mutex_unlock(&hd->lock);

	return ret;
}

static int hdmi_audio_shutdown(struct device *dev)
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);

	mutex_lock(&hd->lock);
	hd->audio_abort_cb = NULL;
	hd->audio_configured = false;
	hd->audio_playing = false;
	mutex_unlock(&hd->lock);

	return 0;
}

static int hdmi_audio_start(struct device *dev)
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);
	unsigned long flags;

	WARN_ON(!hdmi_mode_has_audio(&hd->cfg));

	spin_lock_irqsave(&hd->audio_playing_lock, flags);

	if (hd->display_enabled)
		hdmi_start_audio_stream(hd);
	hd->audio_playing = true;

	spin_unlock_irqrestore(&hd->audio_playing_lock, flags);
	return 0;
}

static void hdmi_audio_stop(struct device *dev)
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);
	unsigned long flags;

	WARN_ON(!hdmi_mode_has_audio(&hd->cfg));

	spin_lock_irqsave(&hd->audio_playing_lock, flags);

	if (hd->display_enabled)
		hdmi_stop_audio_stream(hd);
	hd->audio_playing = false;

	spin_unlock_irqrestore(&hd->audio_playing_lock, flags);
}

static int hdmi_audio_config(struct device *dev,
			     struct omap_dss_audio *dss_audio)
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&hd->lock);

	if (!hdmi_mode_has_audio(&hd->cfg) || !hd->display_enabled) {
		ret = -EPERM;
		goto out;
	}

	ret = hdmi4_audio_config(&hd->core, &hd->wp, dss_audio,
				 hd->cfg.timings.pixelclock);
	if (!ret) {
		hd->audio_configured = true;
		hd->audio_config = *dss_audio;
	}
out:
	mutex_unlock(&hd->lock);

	return ret;
}

static const struct omap_hdmi_audio_ops hdmi_audio_ops = {
	.audio_startup = hdmi_audio_startup,
	.audio_shutdown = hdmi_audio_shutdown,
	.audio_start = hdmi_audio_start,
	.audio_stop = hdmi_audio_stop,
	.audio_config = hdmi_audio_config,
};

static int hdmi_audio_register(struct device *dev)
{
	struct omap_hdmi_audio_pdata pdata = {
		.dev = dev,
		.dss_version = omapdss_get_version(),
		.audio_dma_addr = hdmi_wp_get_audio_dma_addr(&hdmi.wp),
		.ops = &hdmi_audio_ops,
	};

	hdmi.audio_pdev = platform_device_register_data(
		dev, "omap-hdmi-audio", PLATFORM_DEVID_AUTO,
		&pdata, sizeof(pdata));

	if (IS_ERR(hdmi.audio_pdev))
		return PTR_ERR(hdmi.audio_pdev);

	return 0;
}

/* HDMI HW IP initialisation */
static int hdmi4_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	int r;
	int irq;

	hdmi.pdev = pdev;
	dev_set_drvdata(&pdev->dev, &hdmi);

	mutex_init(&hdmi.lock);
	spin_lock_init(&hdmi.audio_playing_lock);

	if (pdev->dev.of_node) {
		r = hdmi_probe_of(pdev);
		if (r)
			return r;
	}

	r = hdmi_wp_init(pdev, &hdmi.wp);
	if (r)
		return r;

	r = hdmi_pll_init(pdev, &hdmi.pll, &hdmi.wp);
	if (r)
		return r;

	r = hdmi_phy_init(pdev, &hdmi.phy);
	if (r)
		goto err;

	r = hdmi4_core_init(pdev, &hdmi.core);
	if (r)
		goto err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		DSSERR("platform_get_irq failed\n");
		r = -ENODEV;
		goto err;
	}

	r = devm_request_threaded_irq(&pdev->dev, irq,
			NULL, hdmi_irq_handler,
			IRQF_ONESHOT, "OMAP HDMI", &hdmi.wp);
	if (r) {
		DSSERR("HDMI IRQ request failed\n");
		goto err;
	}

	pm_runtime_enable(&pdev->dev);

	hdmi_init_output(pdev);

	r = hdmi_audio_register(&pdev->dev);
	if (r) {
		DSSERR("Registering HDMI audio failed\n");
		hdmi_uninit_output(pdev);
		pm_runtime_disable(&pdev->dev);
		return r;
	}

	dss_debugfs_create_file("hdmi", hdmi_dump_regs);

	return 0;
err:
	hdmi_pll_uninit(&hdmi.pll);
	return r;
}

static void hdmi4_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (hdmi.audio_pdev)
		platform_device_unregister(hdmi.audio_pdev);

	hdmi_uninit_output(pdev);

	hdmi_pll_uninit(&hdmi.pll);

	pm_runtime_disable(&pdev->dev);
}

static const struct component_ops hdmi4_component_ops = {
	.bind	= hdmi4_bind,
	.unbind	= hdmi4_unbind,
};

static int hdmi4_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &hdmi4_component_ops);
}

static int hdmi4_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &hdmi4_component_ops);
	return 0;
}

static int hdmi_runtime_suspend(struct device *dev)
{
	dispc_runtime_put();

	return 0;
}

static int hdmi_runtime_resume(struct device *dev)
{
	int r;

	r = dispc_runtime_get();
	if (r < 0)
		return r;

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
	.probe		= hdmi4_probe,
	.remove		= hdmi4_remove,
	.driver         = {
		.name   = "omapdss_hdmi",
		.pm	= &hdmi_pm_ops,
		.of_match_table = hdmi_of_match,
		.suppress_bind_attrs = true,
	},
};

int __init hdmi4_init_platform_driver(void)
{
	return platform_driver_register(&omapdss_hdmihw_driver);
}

void hdmi4_uninit_platform_driver(void)
{
	platform_driver_unregister(&omapdss_hdmihw_driver);
}
