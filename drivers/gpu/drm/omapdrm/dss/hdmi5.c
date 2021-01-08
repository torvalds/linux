// SPDX-License-Identifier: GPL-2.0-only
/*
 * HDMI driver for OMAP5
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Authors:
 *	Yong Zhi
 *	Mythri pk
 *	Archit Taneja <archit@ti.com>
 *	Tomi Valkeinen <tomi.valkeinen@ti.com>
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
#include <linux/regulator/consumer.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <sound/omap-hdmi-audio.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>

#include "omapdss.h"
#include "hdmi5_core.h"
#include "dss.h"

static int hdmi_runtime_get(struct omap_hdmi *hdmi)
{
	int r;

	DSSDBG("hdmi_runtime_get\n");

	r = pm_runtime_get_sync(&hdmi->pdev->dev);
	if (WARN_ON(r < 0)) {
		pm_runtime_put_noidle(&hdmi->pdev->dev);
		return r;
	}
	return 0;
}

static void hdmi_runtime_put(struct omap_hdmi *hdmi)
{
	int r;

	DSSDBG("hdmi_runtime_put\n");

	r = pm_runtime_put_sync(&hdmi->pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static irqreturn_t hdmi_irq_handler(int irq, void *data)
{
	struct omap_hdmi *hdmi = data;
	struct hdmi_wp_data *wp = &hdmi->wp;
	u32 irqstatus;

	irqstatus = hdmi_wp_get_irqstatus(wp);
	hdmi_wp_set_irqstatus(wp, irqstatus);

	if ((irqstatus & HDMI_IRQ_LINK_CONNECT) &&
			irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		u32 v;
		/*
		 * If we get both connect and disconnect interrupts at the same
		 * time, turn off the PHY, clear interrupts, and restart, which
		 * raises connect interrupt if a cable is connected, or nothing
		 * if cable is not connected.
		 */

		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_OFF);

		/*
		 * We always get bogus CONNECT & DISCONNECT interrupts when
		 * setting the PHY to LDOON. To ignore those, we force the RXDET
		 * line to 0 until the PHY power state has been changed.
		 */
		v = hdmi_read_reg(hdmi->phy.base, HDMI_TXPHY_PAD_CFG_CTRL);
		v = FLD_MOD(v, 1, 15, 15); /* FORCE_RXDET_HIGH */
		v = FLD_MOD(v, 0, 14, 7); /* RXDET_LINE */
		hdmi_write_reg(hdmi->phy.base, HDMI_TXPHY_PAD_CFG_CTRL, v);

		hdmi_wp_set_irqstatus(wp, HDMI_IRQ_LINK_CONNECT |
				HDMI_IRQ_LINK_DISCONNECT);

		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);

		REG_FLD_MOD(hdmi->phy.base, HDMI_TXPHY_PAD_CFG_CTRL, 0, 15, 15);

	} else if (irqstatus & HDMI_IRQ_LINK_CONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_TXON);
	} else if (irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	}

	return IRQ_HANDLED;
}

static int hdmi_power_on_core(struct omap_hdmi *hdmi)
{
	int r;

	r = regulator_enable(hdmi->vdda_reg);
	if (r)
		return r;

	r = hdmi_runtime_get(hdmi);
	if (r)
		goto err_runtime_get;

	/* Make selection of HDMI in DSS */
	dss_select_hdmi_venc_clk_source(hdmi->dss, DSS_HDMI_M_PCLK);

	hdmi->core_enabled = true;

	return 0;

err_runtime_get:
	regulator_disable(hdmi->vdda_reg);

	return r;
}

static void hdmi_power_off_core(struct omap_hdmi *hdmi)
{
	hdmi->core_enabled = false;

	hdmi_runtime_put(hdmi);
	regulator_disable(hdmi->vdda_reg);
}

static int hdmi_power_on_full(struct omap_hdmi *hdmi)
{
	int r;
	const struct videomode *vm;
	struct dss_pll_clock_info hdmi_cinfo = { 0 };
	unsigned int pc;

	r = hdmi_power_on_core(hdmi);
	if (r)
		return r;

	vm = &hdmi->cfg.vm;

	DSSDBG("hdmi_power_on hactive= %d vactive = %d\n", vm->hactive,
	       vm->vactive);

	pc = vm->pixelclock;
	if (vm->flags & DISPLAY_FLAGS_DOUBLECLK)
		pc *= 2;

	/* DSS_HDMI_TCLK is bitclk / 10 */
	pc *= 10;

	dss_pll_calc_b(&hdmi->pll.pll, clk_get_rate(hdmi->pll.pll.clkin),
		pc, &hdmi_cinfo);

	/* disable and clear irqs */
	hdmi_wp_clear_irqenable(&hdmi->wp, 0xffffffff);
	hdmi_wp_set_irqstatus(&hdmi->wp,
			hdmi_wp_get_irqstatus(&hdmi->wp));

	r = dss_pll_enable(&hdmi->pll.pll);
	if (r) {
		DSSERR("Failed to enable PLL\n");
		goto err_pll_enable;
	}

	r = dss_pll_set_config(&hdmi->pll.pll, &hdmi_cinfo);
	if (r) {
		DSSERR("Failed to configure PLL\n");
		goto err_pll_cfg;
	}

	r = hdmi_phy_configure(&hdmi->phy, hdmi_cinfo.clkdco,
		hdmi_cinfo.clkout[0]);
	if (r) {
		DSSDBG("Failed to start PHY\n");
		goto err_phy_cfg;
	}

	r = hdmi_wp_set_phy_pwr(&hdmi->wp, HDMI_PHYPWRCMD_LDOON);
	if (r)
		goto err_phy_pwr;

	hdmi5_configure(&hdmi->core, &hdmi->wp, &hdmi->cfg);

	r = dss_mgr_enable(&hdmi->output);
	if (r)
		goto err_mgr_enable;

	r = hdmi_wp_video_start(&hdmi->wp);
	if (r)
		goto err_vid_enable;

	hdmi_wp_set_irqenable(&hdmi->wp,
			HDMI_IRQ_LINK_CONNECT | HDMI_IRQ_LINK_DISCONNECT);

	return 0;

err_vid_enable:
	dss_mgr_disable(&hdmi->output);
err_mgr_enable:
	hdmi_wp_set_phy_pwr(&hdmi->wp, HDMI_PHYPWRCMD_OFF);
err_phy_pwr:
err_phy_cfg:
err_pll_cfg:
	dss_pll_disable(&hdmi->pll.pll);
err_pll_enable:
	hdmi_power_off_core(hdmi);
	return -EIO;
}

static void hdmi_power_off_full(struct omap_hdmi *hdmi)
{
	hdmi_wp_clear_irqenable(&hdmi->wp, 0xffffffff);

	hdmi_wp_video_stop(&hdmi->wp);

	dss_mgr_disable(&hdmi->output);

	hdmi_wp_set_phy_pwr(&hdmi->wp, HDMI_PHYPWRCMD_OFF);

	dss_pll_disable(&hdmi->pll.pll);

	hdmi_power_off_core(hdmi);
}

static int hdmi_dump_regs(struct seq_file *s, void *p)
{
	struct omap_hdmi *hdmi = s->private;

	mutex_lock(&hdmi->lock);

	if (hdmi_runtime_get(hdmi)) {
		mutex_unlock(&hdmi->lock);
		return 0;
	}

	hdmi_wp_dump(&hdmi->wp, s);
	hdmi_pll_dump(&hdmi->pll, s);
	hdmi_phy_dump(&hdmi->phy, s);
	hdmi5_core_dump(&hdmi->core, s);

	hdmi_runtime_put(hdmi);
	mutex_unlock(&hdmi->lock);
	return 0;
}

static void hdmi_start_audio_stream(struct omap_hdmi *hd)
{
	REG_FLD_MOD(hd->wp.base, HDMI_WP_SYSCONFIG, 1, 3, 2);
	hdmi_wp_audio_enable(&hd->wp, true);
	hdmi_wp_audio_core_req_enable(&hd->wp, true);
}

static void hdmi_stop_audio_stream(struct omap_hdmi *hd)
{
	hdmi_wp_audio_core_req_enable(&hd->wp, false);
	hdmi_wp_audio_enable(&hd->wp, false);
	REG_FLD_MOD(hd->wp.base, HDMI_WP_SYSCONFIG, hd->wp_idlemode, 3, 2);
}

static int hdmi_core_enable(struct omap_hdmi *hdmi)
{
	int r = 0;

	DSSDBG("ENTER omapdss_hdmi_core_enable\n");

	mutex_lock(&hdmi->lock);

	r = hdmi_power_on_core(hdmi);
	if (r) {
		DSSERR("failed to power on device\n");
		goto err0;
	}

	mutex_unlock(&hdmi->lock);
	return 0;

err0:
	mutex_unlock(&hdmi->lock);
	return r;
}

static void hdmi_core_disable(struct omap_hdmi *hdmi)
{
	DSSDBG("Enter omapdss_hdmi_core_disable\n");

	mutex_lock(&hdmi->lock);

	hdmi_power_off_core(hdmi);

	mutex_unlock(&hdmi->lock);
}

/* -----------------------------------------------------------------------------
 * DRM Bridge Operations
 */

static int hdmi5_bridge_attach(struct drm_bridge *bridge,
			       enum drm_bridge_attach_flags flags)
{
	struct omap_hdmi *hdmi = drm_bridge_to_hdmi(bridge);

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	return drm_bridge_attach(bridge->encoder, hdmi->output.next_bridge,
				 bridge, flags);
}

static void hdmi5_bridge_mode_set(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  const struct drm_display_mode *adjusted_mode)
{
	struct omap_hdmi *hdmi = drm_bridge_to_hdmi(bridge);

	mutex_lock(&hdmi->lock);

	drm_display_mode_to_videomode(adjusted_mode, &hdmi->cfg.vm);

	dispc_set_tv_pclk(hdmi->dss->dispc, adjusted_mode->clock * 1000);

	mutex_unlock(&hdmi->lock);
}

static void hdmi5_bridge_enable(struct drm_bridge *bridge,
				struct drm_bridge_state *bridge_state)
{
	struct omap_hdmi *hdmi = drm_bridge_to_hdmi(bridge);
	struct drm_atomic_state *state = bridge_state->base.state;
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_crtc_state *crtc_state;
	unsigned long flags;
	int ret;

	/*
	 * None of these should fail, as the bridge can't be enabled without a
	 * valid CRTC to connector path with fully populated new states.
	 */
	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	if (WARN_ON(!connector))
		return;
	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;
	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	hdmi->cfg.hdmi_dvi_mode = connector->display_info.is_hdmi
				? HDMI_HDMI : HDMI_DVI;

	if (connector->display_info.is_hdmi) {
		const struct drm_display_mode *mode;
		struct hdmi_avi_infoframe avi;

		mode = &crtc_state->adjusted_mode;
		ret = drm_hdmi_avi_infoframe_from_display_mode(&avi, connector,
							       mode);
		if (ret == 0)
			hdmi->cfg.infoframe = avi;
	}

	mutex_lock(&hdmi->lock);

	ret = hdmi_power_on_full(hdmi);
	if (ret) {
		DSSERR("failed to power on device\n");
		goto done;
	}

	if (hdmi->audio_configured) {
		ret = hdmi5_audio_config(&hdmi->core, &hdmi->wp,
					 &hdmi->audio_config,
					 hdmi->cfg.vm.pixelclock);
		if (ret) {
			DSSERR("Error restoring audio configuration: %d", ret);
			hdmi->audio_abort_cb(&hdmi->pdev->dev);
			hdmi->audio_configured = false;
		}
	}

	spin_lock_irqsave(&hdmi->audio_playing_lock, flags);
	if (hdmi->audio_configured && hdmi->audio_playing)
		hdmi_start_audio_stream(hdmi);
	hdmi->display_enabled = true;
	spin_unlock_irqrestore(&hdmi->audio_playing_lock, flags);

done:
	mutex_unlock(&hdmi->lock);
}

static void hdmi5_bridge_disable(struct drm_bridge *bridge,
				 struct drm_bridge_state *bridge_state)
{
	struct omap_hdmi *hdmi = drm_bridge_to_hdmi(bridge);
	unsigned long flags;

	mutex_lock(&hdmi->lock);

	spin_lock_irqsave(&hdmi->audio_playing_lock, flags);
	hdmi_stop_audio_stream(hdmi);
	hdmi->display_enabled = false;
	spin_unlock_irqrestore(&hdmi->audio_playing_lock, flags);

	hdmi_power_off_full(hdmi);

	mutex_unlock(&hdmi->lock);
}

static struct edid *hdmi5_bridge_get_edid(struct drm_bridge *bridge,
					  struct drm_connector *connector)
{
	struct omap_hdmi *hdmi = drm_bridge_to_hdmi(bridge);
	struct edid *edid;
	bool need_enable;
	int idlemode;
	int r;

	need_enable = hdmi->core_enabled == false;

	if (need_enable) {
		r = hdmi_core_enable(hdmi);
		if (r)
			return NULL;
	}

	mutex_lock(&hdmi->lock);
	r = hdmi_runtime_get(hdmi);
	BUG_ON(r);

	idlemode = REG_GET(hdmi->wp.base, HDMI_WP_SYSCONFIG, 3, 2);
	/* No-idle mode */
	REG_FLD_MOD(hdmi->wp.base, HDMI_WP_SYSCONFIG, 1, 3, 2);

	hdmi5_core_ddc_init(&hdmi->core);

	edid = drm_do_get_edid(connector, hdmi5_core_ddc_read, &hdmi->core);

	hdmi5_core_ddc_uninit(&hdmi->core);

	REG_FLD_MOD(hdmi->wp.base, HDMI_WP_SYSCONFIG, idlemode, 3, 2);

	hdmi_runtime_put(hdmi);
	mutex_unlock(&hdmi->lock);

	if (need_enable)
		hdmi_core_disable(hdmi);

	return (struct edid *)edid;
}

static const struct drm_bridge_funcs hdmi5_bridge_funcs = {
	.attach = hdmi5_bridge_attach,
	.mode_set = hdmi5_bridge_mode_set,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = hdmi5_bridge_enable,
	.atomic_disable = hdmi5_bridge_disable,
	.get_edid = hdmi5_bridge_get_edid,
};

static void hdmi5_bridge_init(struct omap_hdmi *hdmi)
{
	hdmi->bridge.funcs = &hdmi5_bridge_funcs;
	hdmi->bridge.of_node = hdmi->pdev->dev.of_node;
	hdmi->bridge.ops = DRM_BRIDGE_OP_EDID;
	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&hdmi->bridge);
}

static void hdmi5_bridge_cleanup(struct omap_hdmi *hdmi)
{
	drm_bridge_remove(&hdmi->bridge);
}

/* -----------------------------------------------------------------------------
 * Audio Callbacks
 */

static int hdmi_audio_startup(struct device *dev,
			      void (*abort_cb)(struct device *dev))
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);

	mutex_lock(&hd->lock);

	WARN_ON(hd->audio_abort_cb != NULL);

	hd->audio_abort_cb = abort_cb;

	mutex_unlock(&hd->lock);

	return 0;
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

	spin_lock_irqsave(&hd->audio_playing_lock, flags);

	if (hd->display_enabled) {
		if (!hdmi_mode_has_audio(&hd->cfg))
			DSSERR("%s: Video mode does not support audio\n",
			       __func__);
		hdmi_start_audio_stream(hd);
	}
	hd->audio_playing = true;

	spin_unlock_irqrestore(&hd->audio_playing_lock, flags);
	return 0;
}

static void hdmi_audio_stop(struct device *dev)
{
	struct omap_hdmi *hd = dev_get_drvdata(dev);
	unsigned long flags;

	if (!hdmi_mode_has_audio(&hd->cfg))
		DSSERR("%s: Video mode does not support audio\n", __func__);

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
	int ret = 0;

	mutex_lock(&hd->lock);

	if (hd->display_enabled) {
		ret = hdmi5_audio_config(&hd->core, &hd->wp, dss_audio,
					 hd->cfg.vm.pixelclock);
		if (ret)
			goto out;
	}

	hd->audio_configured = true;
	hd->audio_config = *dss_audio;
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

static int hdmi_audio_register(struct omap_hdmi *hdmi)
{
	struct omap_hdmi_audio_pdata pdata = {
		.dev = &hdmi->pdev->dev,
		.version = 5,
		.audio_dma_addr = hdmi_wp_get_audio_dma_addr(&hdmi->wp),
		.ops = &hdmi_audio_ops,
	};

	hdmi->audio_pdev = platform_device_register_data(
		&hdmi->pdev->dev, "omap-hdmi-audio", PLATFORM_DEVID_AUTO,
		&pdata, sizeof(pdata));

	if (IS_ERR(hdmi->audio_pdev))
		return PTR_ERR(hdmi->audio_pdev);

	hdmi_runtime_get(hdmi);
	hdmi->wp_idlemode =
		REG_GET(hdmi->wp.base, HDMI_WP_SYSCONFIG, 3, 2);
	hdmi_runtime_put(hdmi);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Component Bind & Unbind
 */

static int hdmi5_bind(struct device *dev, struct device *master, void *data)
{
	struct dss_device *dss = dss_get_device(master);
	struct omap_hdmi *hdmi = dev_get_drvdata(dev);
	int r;

	hdmi->dss = dss;

	r = hdmi_pll_init(dss, hdmi->pdev, &hdmi->pll, &hdmi->wp);
	if (r)
		return r;

	r = hdmi_audio_register(hdmi);
	if (r) {
		DSSERR("Registering HDMI audio failed %d\n", r);
		goto err_pll_uninit;
	}

	hdmi->debugfs = dss_debugfs_create_file(dss, "hdmi", hdmi_dump_regs,
						hdmi);

	return 0;

err_pll_uninit:
	hdmi_pll_uninit(&hdmi->pll);
	return r;
}

static void hdmi5_unbind(struct device *dev, struct device *master, void *data)
{
	struct omap_hdmi *hdmi = dev_get_drvdata(dev);

	dss_debugfs_remove_file(hdmi->debugfs);

	if (hdmi->audio_pdev)
		platform_device_unregister(hdmi->audio_pdev);

	hdmi_pll_uninit(&hdmi->pll);
}

static const struct component_ops hdmi5_component_ops = {
	.bind	= hdmi5_bind,
	.unbind	= hdmi5_unbind,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove, Suspend & Resume
 */

static int hdmi5_init_output(struct omap_hdmi *hdmi)
{
	struct omap_dss_device *out = &hdmi->output;
	int r;

	hdmi5_bridge_init(hdmi);

	out->dev = &hdmi->pdev->dev;
	out->id = OMAP_DSS_OUTPUT_HDMI;
	out->type = OMAP_DISPLAY_TYPE_HDMI;
	out->name = "hdmi.0";
	out->dispc_channel = OMAP_DSS_CHANNEL_DIGIT;
	out->of_port = 0;

	r = omapdss_device_init_output(out, &hdmi->bridge);
	if (r < 0) {
		hdmi5_bridge_cleanup(hdmi);
		return r;
	}

	omapdss_device_register(out);

	return 0;
}

static void hdmi5_uninit_output(struct omap_hdmi *hdmi)
{
	struct omap_dss_device *out = &hdmi->output;

	omapdss_device_unregister(out);
	omapdss_device_cleanup_output(out);

	hdmi5_bridge_cleanup(hdmi);
}

static int hdmi5_probe_of(struct omap_hdmi *hdmi)
{
	struct platform_device *pdev = hdmi->pdev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *ep;
	int r;

	ep = of_graph_get_endpoint_by_regs(node, 0, 0);
	if (!ep)
		return 0;

	r = hdmi_parse_lanes_of(pdev, ep, &hdmi->phy);
	of_node_put(ep);
	return r;
}

static int hdmi5_probe(struct platform_device *pdev)
{
	struct omap_hdmi *hdmi;
	int irq;
	int r;

	hdmi = kzalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->pdev = pdev;

	dev_set_drvdata(&pdev->dev, hdmi);

	mutex_init(&hdmi->lock);
	spin_lock_init(&hdmi->audio_playing_lock);

	r = hdmi5_probe_of(hdmi);
	if (r)
		goto err_free;

	r = hdmi_wp_init(pdev, &hdmi->wp, 5);
	if (r)
		goto err_free;

	r = hdmi_phy_init(pdev, &hdmi->phy, 5);
	if (r)
		goto err_free;

	r = hdmi5_core_init(pdev, &hdmi->core);
	if (r)
		goto err_free;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		DSSERR("platform_get_irq failed\n");
		r = -ENODEV;
		goto err_free;
	}

	r = devm_request_threaded_irq(&pdev->dev, irq,
			NULL, hdmi_irq_handler,
			IRQF_ONESHOT, "OMAP HDMI", hdmi);
	if (r) {
		DSSERR("HDMI IRQ request failed\n");
		goto err_free;
	}

	hdmi->vdda_reg = devm_regulator_get(&pdev->dev, "vdda");
	if (IS_ERR(hdmi->vdda_reg)) {
		r = PTR_ERR(hdmi->vdda_reg);
		if (r != -EPROBE_DEFER)
			DSSERR("can't get VDDA regulator\n");
		goto err_free;
	}

	pm_runtime_enable(&pdev->dev);

	r = hdmi5_init_output(hdmi);
	if (r)
		goto err_pm_disable;

	r = component_add(&pdev->dev, &hdmi5_component_ops);
	if (r)
		goto err_uninit_output;

	return 0;

err_uninit_output:
	hdmi5_uninit_output(hdmi);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_free:
	kfree(hdmi);
	return r;
}

static int hdmi5_remove(struct platform_device *pdev)
{
	struct omap_hdmi *hdmi = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &hdmi5_component_ops);

	hdmi5_uninit_output(hdmi);

	pm_runtime_disable(&pdev->dev);

	kfree(hdmi);
	return 0;
}

static const struct of_device_id hdmi_of_match[] = {
	{ .compatible = "ti,omap5-hdmi", },
	{ .compatible = "ti,dra7-hdmi", },
	{},
};

struct platform_driver omapdss_hdmi5hw_driver = {
	.probe		= hdmi5_probe,
	.remove		= hdmi5_remove,
	.driver         = {
		.name   = "omapdss_hdmi5",
		.of_match_table = hdmi_of_match,
		.suppress_bind_attrs = true,
	},
};
