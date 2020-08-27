// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/component.h>
#include <linux/of_irq.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_hpd.h"
#include "dp_parser.h"
#include "dp_power.h"
#include "dp_catalog.h"
#include "dp_aux.h"
#include "dp_reg.h"
#include "dp_link.h"
#include "dp_panel.h"
#include "dp_ctrl.h"
#include "dp_display.h"
#include "dp_drm.h"
#include "dp_pll.h"

static struct msm_dp *g_dp_display;
#define HPD_STRING_SIZE 30

struct dp_display_private {
	char *name;
	int irq;

	/* state variables */
	bool core_initialized;
	bool power_on;
	bool hpd_irq_on;
	bool audio_supported;
	atomic_t hpd_isr_status;

	struct platform_device *pdev;
	struct dentry *root;
	struct completion notification_comp;

	struct dp_usbpd   *usbpd;
	struct dp_parser  *parser;
	struct msm_dp_pll *pll;
	struct dp_power   *power;
	struct dp_catalog *catalog;
	struct drm_dp_aux *aux;
	struct dp_link    *link;
	struct dp_panel   *panel;
	struct dp_ctrl    *ctrl;

	struct dp_usbpd_cb usbpd_cb;
	struct dp_display_mode dp_mode;
	struct msm_dp dp_display;

	struct delayed_work config_hpd_work;
};

static const struct of_device_id dp_dt_match[] = {
	{.compatible = "qcom,sc7180-dp"},
	{}
};

static irqreturn_t dp_display_irq(int irq, void *dev_id)
{
	struct dp_display_private *dp = dev_id;
	irqreturn_t ret = IRQ_HANDLED;
	u32 hpd_isr_status;

	if (!dp) {
		DRM_ERROR("invalid data\n");
		return IRQ_NONE;
	}

	hpd_isr_status = dp_catalog_hpd_get_intr_status(dp->catalog);

	if (hpd_isr_status & DP_DP_HPD_INT_MASK) {
		atomic_set(&dp->hpd_isr_status, hpd_isr_status);
		ret = IRQ_WAKE_THREAD;
	}

	/* DP controller isr */
	dp_ctrl_isr(dp->ctrl);

	/* DP aux isr */
	dp_aux_isr(dp->aux);

	return ret;
}

static irqreturn_t dp_display_hpd_isr_work(int irq, void *data)
{
	struct dp_display_private *dp;
	struct dp_usbpd *hpd;
	u32 isr = 0;

	dp = (struct dp_display_private *)data;
	if (!dp)
		return IRQ_NONE;

	isr = atomic_read(&dp->hpd_isr_status);

	/* reset to default */
	atomic_set(&dp->hpd_isr_status, 0);

	hpd = dp->usbpd;
	if (!hpd)
		return IRQ_NONE;

	if (isr & DP_DP_HPD_PLUG_INT_MASK &&
		isr & DP_DP_HPD_STATE_STATUS_CONNECTED) {
		hpd->hpd_high = 1;
		dp->usbpd_cb.configure(&dp->pdev->dev);
	} else if (isr & DP_DP_HPD_UNPLUG_INT_MASK &&
		(isr & DP_DP_HPD_STATE_STATUS_MASK) ==
			 DP_DP_HPD_STATE_STATUS_DISCONNECTED) {

		/* disable HPD plug interrupt until disconnect is done
		 */
		dp_catalog_hpd_config_intr(dp->catalog,
			DP_DP_HPD_PLUG_INT_MASK | DP_DP_IRQ_HPD_INT_MASK,
			false);

		hpd->hpd_high = 0;

		/* We don't need separate work for disconnect as
		 * connect/attention interrupts are disabled
		 */
		dp->usbpd_cb.disconnect(&dp->pdev->dev);

		dp_catalog_hpd_config_intr(dp->catalog,
			DP_DP_HPD_PLUG_INT_MASK | DP_DP_IRQ_HPD_INT_MASK,
			true);
	}

	return IRQ_HANDLED;
}

static int dp_display_bind(struct device *dev, struct device *master,
			   void *data)
{
	int rc = 0;
	struct dp_display_private *dp;
	struct drm_device *drm;
	struct msm_drm_private *priv;
	struct platform_device *pdev = to_platform_device(dev);

	drm = dev_get_drvdata(master);

	dp = platform_get_drvdata(pdev);
	if (!dp) {
		DRM_ERROR("DP driver bind failed. Invalid driver data\n");
		return -EINVAL;
	}

	dp->dp_display.drm_dev = drm;
	priv = drm->dev_private;
	priv->dp = &(dp->dp_display);

	rc = dp->parser->parse(dp->parser);
	if (rc) {
		DRM_ERROR("device tree parsing failed\n");
		goto end;
	}

	rc = dp_aux_register(dp->aux);
	if (rc) {
		DRM_ERROR("DRM DP AUX register failed\n");
		goto end;
	}

	rc = dp_power_client_init(dp->power);
	if (rc) {
		DRM_ERROR("Power client create failed\n");
		goto end;
	}

end:
	return rc;
}

static void dp_display_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct dp_display_private *dp;
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;

	dp = platform_get_drvdata(pdev);
	if (!dp) {
		DRM_ERROR("Invalid DP driver data\n");
		return;
	}

	dp_power_client_deinit(dp->power);
	dp_aux_unregister(dp->aux);
	priv->dp = NULL;
}

static const struct component_ops dp_display_comp_ops = {
	.bind = dp_display_bind,
	.unbind = dp_display_unbind,
};

static bool dp_display_is_ds_bridge(struct dp_panel *panel)
{
	return (panel->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
		DP_DWN_STRM_PORT_PRESENT);
}

static bool dp_display_is_sink_count_zero(struct dp_display_private *dp)
{
	return dp_display_is_ds_bridge(dp->panel) &&
		(dp->link->sink_count == 0);
}

static void dp_display_send_hpd_event(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;
	struct drm_connector *connector;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	connector = dp->dp_display.connector;
	drm_helper_hpd_irq_event(connector->dev);
}

static int dp_display_send_hpd_notification(struct dp_display_private *dp,
					    bool hpd)
{
	static bool encoder_mode_set;
	struct msm_drm_private *priv = dp->dp_display.drm_dev->dev_private;
	struct msm_kms *kms = priv->kms;

	mutex_lock(&dp->dp_display.connect_mutex);
	if ((hpd && dp->dp_display.is_connected) ||
			(!hpd && !dp->dp_display.is_connected)) {
		DRM_DEBUG_DP("HPD already %s\n", (hpd ? "on" : "off"));
		mutex_unlock(&dp->dp_display.connect_mutex);
		return 0;
	}

	/* reset video pattern flag on disconnect */
	if (!hpd)
		dp->panel->video_test = false;

	dp->dp_display.is_connected = hpd;
	reinit_completion(&dp->notification_comp);

	if (dp->dp_display.is_connected && dp->dp_display.encoder
				&& !encoder_mode_set
				&& kms->funcs->set_encoder_mode) {
		kms->funcs->set_encoder_mode(kms,
				dp->dp_display.encoder, false);
		DRM_DEBUG_DP("set_encoder_mode() Completed\n");
		encoder_mode_set = true;
	}

	dp_display_send_hpd_event(&dp->dp_display);

	if (!wait_for_completion_timeout(&dp->notification_comp, HZ * 2)) {
		pr_warn("%s timeout\n", hpd ? "connect" : "disconnect");
		mutex_unlock(&dp->dp_display.connect_mutex);
		return -EINVAL;
	}

	mutex_unlock(&dp->dp_display.connect_mutex);
	return 0;
}

static int dp_display_process_hpd_high(struct dp_display_private *dp)
{
	int rc = 0;
	struct edid *edid;

	if (dp->link->psm_enabled)
		goto notify;

	dp->panel->max_dp_lanes = dp->parser->max_dp_lanes;

	rc = dp_panel_read_sink_caps(dp->panel, dp->dp_display.connector);
	if (rc)
		goto notify;

	dp_link_process_request(dp->link);

	if (dp_display_is_sink_count_zero(dp)) {
		DRM_DEBUG_DP("no downstream devices connected\n");
		rc = -EINVAL;
		goto end;
	}

	edid = dp->panel->edid;

	dp->audio_supported = drm_detect_monitor_audio(edid);
	dp_panel_handle_sink_request(dp->panel);

	dp->dp_display.max_pclk_khz = DP_MAX_PIXEL_CLK_KHZ;
	dp->dp_display.max_dp_lanes = dp->parser->max_dp_lanes;
notify:
	dp_display_send_hpd_notification(dp, true);

end:
	return rc;
}

static void dp_display_host_init(struct dp_display_private *dp)
{
	bool flip = false;

	if (dp->core_initialized) {
		DRM_DEBUG_DP("DP core already initialized\n");
		return;
	}

	if (dp->usbpd->orientation == ORIENTATION_CC2)
		flip = true;

	dp_power_init(dp->power, flip);
	dp_ctrl_host_init(dp->ctrl, flip);
	dp_aux_init(dp->aux);
	dp->core_initialized = true;
}

static void dp_display_host_deinit(struct dp_display_private *dp)
{
	if (!dp->core_initialized) {
		DRM_DEBUG_DP("DP core already off\n");
		return;
	}

	dp->core_initialized = false;
}

static void dp_display_process_hpd_low(struct dp_display_private *dp)
{
	dp_display_send_hpd_notification(dp, false);

	dp_aux_deinit(dp->aux);
}

static int dp_display_usbpd_configure_cb(struct device *dev)
{
	int rc = 0;
	struct dp_display_private *dp;

	if (!dev) {
		DRM_ERROR("invalid dev\n");
		rc = -EINVAL;
		goto end;
	}

	dp = dev_get_drvdata(dev);
	if (!dp) {
		DRM_ERROR("no driver data found\n");
		rc = -ENODEV;
		goto end;
	}

	dp_display_host_init(dp);

	if (dp->usbpd->hpd_high)
		dp_display_process_hpd_high(dp);
end:
	return rc;
}

static void dp_display_clean(struct dp_display_private *dp)
{
	dp_ctrl_push_idle(dp->ctrl);
	dp_ctrl_off(dp->ctrl);
}

static int dp_display_usbpd_disconnect_cb(struct device *dev)
{
	int rc = 0;
	struct dp_display_private *dp;

	dp = dev_get_drvdata(dev);

	rc = dp_display_send_hpd_notification(dp, false);

	/* if cable is disconnected, reset psm_enabled flag */
	if (!dp->usbpd->alt_mode_cfg_done)
		dp->link->psm_enabled = false;

	if ((rc < 0) && dp->power_on)
		dp_display_clean(dp);

	dp_display_host_deinit(dp);
	return rc;
}

static void dp_display_handle_video_request(struct dp_display_private *dp)
{
	if (dp->link->sink_request & DP_TEST_LINK_VIDEO_PATTERN) {
		/* force disconnect followed by connect */
		dp->usbpd->connect(dp->usbpd, false);
		dp->panel->video_test = true;
		dp->usbpd->connect(dp->usbpd, true);
		dp_link_send_test_response(dp->link);
	}
}

static int dp_display_handle_hpd_irq(struct dp_display_private *dp)
{
	if (dp->link->sink_request & DS_PORT_STATUS_CHANGED) {
		dp_display_send_hpd_notification(dp, false);

		if (dp_display_is_sink_count_zero(dp)) {
			DRM_DEBUG_DP("sink count is zero, nothing to do\n");
			return 0;
		}

		return dp_display_process_hpd_high(dp);
	}

	dp_ctrl_handle_sink_request(dp->ctrl);

	dp_display_handle_video_request(dp);

	return 0;
}

static int dp_display_usbpd_attention_cb(struct device *dev)
{
	int rc = 0;
	struct dp_display_private *dp;

	if (!dev) {
		DRM_ERROR("invalid dev\n");
		return -EINVAL;
	}

	dp = dev_get_drvdata(dev);
	if (!dp) {
		DRM_ERROR("no driver data found\n");
		return -ENODEV;
	}

	if (dp->usbpd->hpd_irq) {
		dp->hpd_irq_on = true;

		rc = dp_link_process_request(dp->link);
		/* check for any test request issued by sink */
		if (!rc)
			dp_display_handle_hpd_irq(dp);

		dp->hpd_irq_on = false;
		goto end;
	}

	if (!dp->usbpd->hpd_high) {
		dp_display_process_hpd_low(dp);
		goto end;
	}

	if (dp->usbpd->alt_mode_cfg_done)
		dp_display_process_hpd_high(dp);
end:
	return rc;
}

static void dp_display_deinit_sub_modules(struct dp_display_private *dp)
{
	dp_ctrl_put(dp->ctrl);
	dp_panel_put(dp->panel);
	dp_aux_put(dp->aux);
	dp_pll_put(dp->pll);
}

static int dp_init_sub_modules(struct dp_display_private *dp)
{
	int rc = 0;
	struct device *dev = &dp->pdev->dev;
	struct dp_usbpd_cb *cb = &dp->usbpd_cb;
	struct dp_panel_in panel_in = {
		.dev = dev,
	};
	struct dp_pll_in pll_in = {
		.pdev = dp->pdev,
	};

	/* Callback APIs used for cable status change event */
	cb->configure  = dp_display_usbpd_configure_cb;
	cb->disconnect = dp_display_usbpd_disconnect_cb;
	cb->attention  = dp_display_usbpd_attention_cb;

	dp->usbpd = dp_hpd_get(dev, cb);
	if (IS_ERR(dp->usbpd)) {
		rc = PTR_ERR(dp->usbpd);
		DRM_ERROR("failed to initialize hpd, rc = %d\n", rc);
		dp->usbpd = NULL;
		goto error;
	}

	dp->parser = dp_parser_get(dp->pdev);
	if (IS_ERR(dp->parser)) {
		rc = PTR_ERR(dp->parser);
		DRM_ERROR("failed to initialize parser, rc = %d\n", rc);
		dp->parser = NULL;
		goto error;
	}

	dp->catalog = dp_catalog_get(dev, &dp->parser->io);
	if (IS_ERR(dp->catalog)) {
		rc = PTR_ERR(dp->catalog);
		DRM_ERROR("failed to initialize catalog, rc = %d\n", rc);
		dp->catalog = NULL;
		goto error;
	}

	pll_in.parser = dp->parser;
	dp->pll = dp_pll_get(&pll_in);
	if (IS_ERR_OR_NULL(dp->pll)) {
		rc = -EINVAL;
		DRM_ERROR("failed to initialize pll, rc = %d\n", rc);
		dp->pll = NULL;
		goto error;
	}

	dp->parser->pll = dp->pll;

	dp->power = dp_power_get(dp->parser);
	if (IS_ERR(dp->power)) {
		rc = PTR_ERR(dp->power);
		DRM_ERROR("failed to initialize power, rc = %d\n", rc);
		dp->power = NULL;
		goto error;
	}

	dp->aux = dp_aux_get(dev, dp->catalog);
	if (IS_ERR(dp->aux)) {
		rc = PTR_ERR(dp->aux);
		DRM_ERROR("failed to initialize aux, rc = %d\n", rc);
		dp->aux = NULL;
		goto error;
	}

	dp->link = dp_link_get(dev, dp->aux);
	if (IS_ERR(dp->link)) {
		rc = PTR_ERR(dp->link);
		DRM_ERROR("failed to initialize link, rc = %d\n", rc);
		dp->link = NULL;
		goto error_link;
	}

	panel_in.aux = dp->aux;
	panel_in.catalog = dp->catalog;
	panel_in.link = dp->link;

	dp->panel = dp_panel_get(&panel_in);
	if (IS_ERR(dp->panel)) {
		rc = PTR_ERR(dp->panel);
		DRM_ERROR("failed to initialize panel, rc = %d\n", rc);
		dp->panel = NULL;
		goto error_link;
	}

	dp->ctrl = dp_ctrl_get(dev, dp->link, dp->panel, dp->aux,
			       dp->power, dp->catalog, dp->parser);
	if (IS_ERR(dp->ctrl)) {
		rc = PTR_ERR(dp->ctrl);
		DRM_ERROR("failed to initialize ctrl, rc = %d\n", rc);
		dp->ctrl = NULL;
		goto error_ctrl;
	}

	return rc;
error_ctrl:
	dp_panel_put(dp->panel);
error_link:
	dp_aux_put(dp->aux);
error:
	return rc;
}

static int dp_display_set_mode(struct msm_dp *dp_display,
			       struct dp_display_mode *mode)
{
	struct dp_display_private *dp;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	dp->panel->dp_mode.drm_mode = mode->drm_mode;
	dp->panel->dp_mode.bpp = mode->bpp;
	dp->panel->dp_mode.capabilities = mode->capabilities;
	dp_panel_init_panel_info(dp->panel);
	return 0;
}

static int dp_display_prepare(struct msm_dp *dp)
{
	return 0;
}

static void dp_display_dump(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	dp_panel_dump_regs(dp->panel);
}

static int dp_display_enable(struct msm_dp *dp_display)
{
	int rc = 0;
	struct dp_display_private *dp;
	bool dump_dp = false;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	if (dp->power_on) {
		DRM_DEBUG_DP("Link already setup, return\n");
		return 0;
	}

	rc = dp_ctrl_on(dp->ctrl);
	if (!rc)
		dp->power_on = true;

	if (dump_dp != false)
		dp_display_dump(dp_display);

	return rc;
}

static int dp_display_post_enable(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	complete_all(&dp->notification_comp);
	return 0;
}

static int dp_display_pre_disable(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	if (dp->usbpd->alt_mode_cfg_done)
		dp_link_psm_config(dp->link, &dp->panel->link_info, true);

	dp_ctrl_push_idle(dp->ctrl);
	return 0;
}

static int dp_display_disable(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	if (!dp->power_on || !dp->core_initialized)
		return -EINVAL;

	dp_ctrl_off(dp->ctrl);

	dp->power_on = false;

	complete_all(&dp->notification_comp);
	return 0;
}

int dp_display_request_irq(struct msm_dp *dp_display)
{
	int rc = 0;
	struct dp_display_private *dp;

	if (!dp_display) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	dp->irq = irq_of_parse_and_map(dp->pdev->dev.of_node, 0);
	if (dp->irq < 0) {
		rc = dp->irq;
		DRM_ERROR("failed to get irq: %d\n", rc);
		return rc;
	}

	rc = devm_request_threaded_irq(&dp->pdev->dev, dp->irq,
		dp_display_irq, dp_display_hpd_isr_work,
		IRQF_TRIGGER_HIGH, "dp_display_isr", dp);
	if (rc < 0) {
		DRM_ERROR("failed to request IRQ%u: %d\n",
				dp->irq, rc);
		return rc;
	}
	disable_irq(dp->irq);

	return 0;
}

static int dp_display_unprepare(struct msm_dp *dp)
{
	return 0;
}

int dp_display_validate_mode(struct msm_dp *dp, u32 mode_pclk_khz)
{
	const u32 num_components = 3, default_bpp = 24;
	struct dp_display_private *dp_display;
	struct dp_link_info *link_info;
	u32 mode_rate_khz = 0, supported_rate_khz = 0, mode_bpp = 0;

	if (!dp || !mode_pclk_khz || !dp->connector) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	dp_display = container_of(dp, struct dp_display_private, dp_display);
	link_info = &dp_display->panel->link_info;

	mode_bpp = dp->connector->display_info.bpc * num_components;
	if (!mode_bpp)
		mode_bpp = default_bpp;

	mode_bpp = dp_panel_get_mode_bpp(dp_display->panel,
			mode_bpp, mode_pclk_khz);

	mode_rate_khz = mode_pclk_khz * mode_bpp;
	supported_rate_khz = link_info->num_lanes * link_info->rate * 8;

	if (mode_rate_khz > supported_rate_khz)
		return MODE_BAD;

	return MODE_OK;
}

int dp_display_get_modes(struct msm_dp *dp,
				struct dp_display_mode *dp_mode)
{
	struct dp_display_private *dp_display;
	int ret = 0;

	if (!dp) {
		DRM_ERROR("invalid params\n");
		return 0;
	}

	dp_display = container_of(dp, struct dp_display_private, dp_display);

	ret = dp_panel_get_modes(dp_display->panel,
		dp->connector, dp_mode);
	if (dp_mode->drm_mode.clock)
		dp->max_pclk_khz = dp_mode->drm_mode.clock;
	return ret;
}

bool dp_display_check_video_test(struct msm_dp *dp)
{
	struct dp_display_private *dp_display;

	dp_display = container_of(dp, struct dp_display_private, dp_display);

	return dp_display->panel->video_test;
}

int dp_display_get_test_bpp(struct msm_dp *dp)
{
	struct dp_display_private *dp_display;

	if (!dp) {
		DRM_ERROR("invalid params\n");
		return 0;
	}

	dp_display = container_of(dp, struct dp_display_private, dp_display);

	return dp_link_bit_depth_to_bpp(
		dp_display->link->test_video.test_bit_depth);
}

static int dp_display_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_display_private *dp;

	if (!pdev || !pdev->dev.of_node) {
		DRM_ERROR("pdev not found\n");
		return -ENODEV;
	}

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	init_completion(&dp->notification_comp);

	dp->pdev = pdev;
	dp->name = "drm_dp";

	rc = dp_init_sub_modules(dp);
	if (rc) {
		DRM_ERROR("init sub module failed\n");
		return -EPROBE_DEFER;
	}

	platform_set_drvdata(pdev, dp);

	mutex_init(&dp->dp_display.connect_mutex);
	g_dp_display = &dp->dp_display;

	rc = component_add(&pdev->dev, &dp_display_comp_ops);
	if (rc) {
		DRM_ERROR("component add failed, rc=%d\n", rc);
		dp_display_deinit_sub_modules(dp);
	}

	return rc;
}

static int dp_display_remove(struct platform_device *pdev)
{
	struct dp_display_private *dp;

	dp = platform_get_drvdata(pdev);

	dp_display_deinit_sub_modules(dp);

	component_del(&pdev->dev, &dp_display_comp_ops);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int dp_pm_resume(struct device *dev)
{
	return 0;
}

static int dp_pm_suspend(struct device *dev)
{
	return 0;
}

static int dp_pm_prepare(struct device *dev)
{
	return 0;
}

static void dp_pm_complete(struct device *dev)
{

}

static const struct dev_pm_ops dp_pm_ops = {
	.suspend = dp_pm_suspend,
	.resume =  dp_pm_resume,
	.prepare = dp_pm_prepare,
	.complete = dp_pm_complete,
};

static struct platform_driver dp_display_driver = {
	.probe  = dp_display_probe,
	.remove = dp_display_remove,
	.driver = {
		.name = "msm-dp-display",
		.of_match_table = dp_dt_match,
		.suppress_bind_attrs = true,
		.pm = &dp_pm_ops,
	},
};

int __init msm_dp_register(void)
{
	int ret;

	ret = platform_driver_register(&dp_display_driver);
	if (ret)
		DRM_ERROR("Dp display driver register failed");

	return ret;
}

void __exit msm_dp_unregister(void)
{
	platform_driver_unregister(&dp_display_driver);
}

static void dp_display_config_hpd_work(struct work_struct *work)
{
	struct dp_display_private *dp;
	struct delayed_work *dw = to_delayed_work(work);

	dp = container_of(dw, struct dp_display_private, config_hpd_work);

	dp_display_host_init(dp);
	dp_catalog_ctrl_hpd_config(dp->catalog);

	/* set default to 0 */
	atomic_set(&dp->hpd_isr_status, 0);

	/* Enable interrupt first time
	 * we are leaving dp clocks on during disconnect
	 * and never disable interrupt
	 */
	enable_irq(dp->irq);
}

void msm_dp_irq_postinstall(struct msm_dp *dp_display)
{
	struct dp_display_private *dp;

	if (!dp_display)
		return;

	dp = container_of(dp_display, struct dp_display_private, dp_display);

	INIT_DELAYED_WORK(&dp->config_hpd_work, dp_display_config_hpd_work);
	queue_delayed_work(system_wq, &dp->config_hpd_work, HZ * 10);
}

int msm_dp_modeset_init(struct msm_dp *dp_display, struct drm_device *dev,
			struct drm_encoder *encoder)
{
	struct msm_drm_private *priv;
	int ret;

	if (WARN_ON(!encoder) || WARN_ON(!dp_display) || WARN_ON(!dev))
		return -EINVAL;

	priv = dev->dev_private;
	dp_display->drm_dev = dev;

	ret = dp_display_request_irq(dp_display);
	if (ret) {
		DRM_ERROR("request_irq failed, ret=%d\n", ret);
		return ret;
	}

	dp_display->encoder = encoder;

	dp_display->connector = dp_drm_connector_init(dp_display);
	if (IS_ERR(dp_display->connector)) {
		ret = PTR_ERR(dp_display->connector);
		DRM_DEV_ERROR(dev->dev,
			"failed to create dp connector: %d\n", ret);
		dp_display->connector = NULL;
		return ret;
	}

	priv->connectors[priv->num_connectors++] = dp_display->connector;
	return 0;
}

int msm_dp_display_enable(struct msm_dp *dp, struct drm_encoder *encoder)
{
	int rc = 0;
	struct dp_display_private *dp_display;

	dp_display = container_of(dp, struct dp_display_private, dp_display);
	if (!dp_display->dp_mode.drm_mode.clock) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	rc = dp_display_set_mode(dp, &dp_display->dp_mode);
	if (rc) {
		DRM_ERROR("Failed to perform a mode set, rc=%d\n", rc);
		return rc;
	}

	rc = dp_display_prepare(dp);
	if (rc) {
		DRM_ERROR("DP display prepare failed, rc=%d\n", rc);
		return rc;
	}

	rc = dp_display_enable(dp);
	if (rc) {
		DRM_ERROR("DP display enable failed, rc=%d\n", rc);
		dp_display_unprepare(dp);
		return rc;
	}

	rc = dp_display_post_enable(dp);
	if (rc) {
		DRM_ERROR("DP display post enable failed, rc=%d\n", rc);
		dp_display_disable(dp);
		dp_display_unprepare(dp);
	}
	return rc;
}

int msm_dp_display_disable(struct msm_dp *dp, struct drm_encoder *encoder)
{
	int rc = 0;

	rc = dp_display_pre_disable(dp);
	if (rc) {
		DRM_ERROR("DP display pre disable failed, rc=%d\n", rc);
		return rc;
	}

	rc = dp_display_disable(dp);
	if (rc) {
		DRM_ERROR("DP display disable failed, rc=%d\n", rc);
		return rc;
	}

	rc = dp_display_unprepare(dp);
	if (rc)
		DRM_ERROR("DP display unprepare failed, rc=%d\n", rc);

	return rc;
}

void msm_dp_display_mode_set(struct msm_dp *dp, struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct dp_display_private *dp_display;

	dp_display = container_of(dp, struct dp_display_private, dp_display);

	memset(&dp_display->dp_mode, 0x0, sizeof(struct dp_display_mode));

	if (dp_display_check_video_test(dp))
		dp_display->dp_mode.bpp = dp_display_get_test_bpp(dp);
	else /* Default num_components per pixel = 3 */
		dp_display->dp_mode.bpp = dp->connector->display_info.bpc * 3;

	if (!dp_display->dp_mode.bpp)
		dp_display->dp_mode.bpp = 24; /* Default bpp */

	drm_mode_copy(&dp_display->dp_mode.drm_mode, adjusted_mode);

	dp_display->dp_mode.v_active_low =
		!!(dp_display->dp_mode.drm_mode.flags & DRM_MODE_FLAG_NVSYNC);

	dp_display->dp_mode.h_active_low =
		!!(dp_display->dp_mode.drm_mode.flags & DRM_MODE_FLAG_NHSYNC);
}
