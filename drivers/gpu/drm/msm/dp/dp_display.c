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
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/string_choices.h>
#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/drm_edid.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_ctrl.h"
#include "dp_aux.h"
#include "dp_reg.h"
#include "dp_link.h"
#include "dp_panel.h"
#include "dp_display.h"
#include "dp_drm.h"
#include "dp_audio.h"
#include "dp_debug.h"

static bool psr_enabled = false;
module_param(psr_enabled, bool, 0);
MODULE_PARM_DESC(psr_enabled, "enable PSR for eDP and DP displays");

#define HPD_STRING_SIZE 30

enum {
	ISR_DISCONNECTED,
	ISR_CONNECT_PENDING,
	ISR_CONNECTED,
	ISR_HPD_IO_GLITCH_COUNT,
	ISR_IRQ_HPD_PULSE_COUNT,
	ISR_HPD_REPLUG_COUNT,
};

struct msm_dp_display_private {
	int irq;

	unsigned int id;

	/* state variables */
	bool core_initialized;
	bool phy_initialized;
	bool audio_supported;

	struct mutex plugged_lock;
	bool plugged;

	struct drm_device *drm_dev;

	struct drm_dp_aux *aux;
	struct msm_dp_link    *link;
	struct msm_dp_panel   *panel;
	struct msm_dp_ctrl    *ctrl;

	struct msm_dp_display_mode msm_dp_mode;
	struct msm_dp msm_dp_display;

	/* wait for audio signaling */
	struct completion audio_comp;

	/* HPD IRQ handling */
	spinlock_t irq_thread_lock;
	u32 hpd_isr_status;

	bool wide_bus_supported;

	struct msm_dp_audio *audio;

	void __iomem *ahb_base;
	size_t ahb_len;

	void __iomem *aux_base;
	size_t aux_len;

	void __iomem *link_base;
	size_t link_len;

	void __iomem *p0_base;
	size_t p0_len;
};

struct msm_dp_desc {
	phys_addr_t io_start;
	unsigned int id;
	bool wide_bus_supported;
};

static const struct msm_dp_desc msm_dp_desc_glymur[] = {
	{ .io_start = 0x0af54000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0af5c000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x0af64000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{ .io_start = 0x0af6c000, .id = MSM_DP_CONTROLLER_3, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sa8775p[] = {
	{ .io_start = 0x0af54000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0af5c000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x22154000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x2215c000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sdm845[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0 },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sc7180[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sc7280[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0aea0000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sc8180x[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0ae98000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x0ae9a000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sc8280xp[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0ae98000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x0ae9a000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{ .io_start = 0x0aea0000, .id = MSM_DP_CONTROLLER_3, .wide_bus_supported = true },
	{ .io_start = 0x22090000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x22098000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x2209a000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{ .io_start = 0x220a0000, .id = MSM_DP_CONTROLLER_3, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_sm8650[] = {
	{ .io_start = 0x0af54000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{}
};

static const struct msm_dp_desc msm_dp_desc_x1e80100[] = {
	{ .io_start = 0x0ae90000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0ae98000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x0ae9a000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{ .io_start = 0x0aea0000, .id = MSM_DP_CONTROLLER_3, .wide_bus_supported = true },
	{}
};

static const struct of_device_id msm_dp_dt_match[] = {
	{ .compatible = "qcom,glymur-dp", .data = &msm_dp_desc_glymur },
	{ .compatible = "qcom,sa8775p-dp", .data = &msm_dp_desc_sa8775p },
	{ .compatible = "qcom,sc7180-dp", .data = &msm_dp_desc_sc7180 },
	{ .compatible = "qcom,sc7280-dp", .data = &msm_dp_desc_sc7280 },
	{ .compatible = "qcom,sc7280-edp", .data = &msm_dp_desc_sc7280 },
	{ .compatible = "qcom,sc8180x-dp", .data = &msm_dp_desc_sc8180x },
	{ .compatible = "qcom,sc8180x-edp", .data = &msm_dp_desc_sc8180x },
	{ .compatible = "qcom,sc8280xp-dp", .data = &msm_dp_desc_sc8280xp },
	{ .compatible = "qcom,sc8280xp-edp", .data = &msm_dp_desc_sc8280xp },
	{ .compatible = "qcom,sdm845-dp", .data = &msm_dp_desc_sdm845 },
	{ .compatible = "qcom,sm8350-dp", .data = &msm_dp_desc_sc7180 },
	{ .compatible = "qcom,sm8650-dp", .data = &msm_dp_desc_sm8650 },
	{ .compatible = "qcom,x1e80100-dp", .data = &msm_dp_desc_x1e80100 },
	{}
};
MODULE_DEVICE_TABLE(of, msm_dp_dt_match);

static struct msm_dp_display_private *dev_get_dp_display_private(struct device *dev)
{
	struct msm_dp *dp = dev_get_drvdata(dev);

	return container_of(dp, struct msm_dp_display_private, msm_dp_display);
}

void msm_dp_display_signal_audio_start(struct msm_dp *msm_dp_display)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	reinit_completion(&dp->audio_comp);
}

void msm_dp_display_signal_audio_complete(struct msm_dp *msm_dp_display)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	complete_all(&dp->audio_comp);
}

static int msm_dp_display_bind(struct device *dev, struct device *master,
			   void *data)
{
	int rc = 0;
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);
	struct msm_drm_private *priv = dev_get_drvdata(master);
	struct drm_device *drm = priv->dev;

	dp->msm_dp_display.drm_dev = drm;
	priv->kms->dp[dp->id] = &dp->msm_dp_display;

	dp->drm_dev = drm;
	dp->aux->drm_dev = drm;
	rc = msm_dp_aux_register(dp->aux);
	if (rc) {
		DRM_ERROR("DRM DP AUX register failed\n");
		goto end;
	}

	return 0;
end:
	return rc;
}

static void msm_dp_display_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);
	struct msm_drm_private *priv = dev_get_drvdata(master);

	of_dp_aux_depopulate_bus(dp->aux);

	msm_dp_aux_unregister(dp->aux);
	dp->drm_dev = NULL;
	dp->aux->drm_dev = NULL;
	priv->kms->dp[dp->id] = NULL;
}

static const struct component_ops msm_dp_display_comp_ops = {
	.bind = msm_dp_display_bind,
	.unbind = msm_dp_display_unbind,
};

static int msm_dp_display_lttpr_init(struct msm_dp_display_private *dp, u8 *dpcd)
{
	int rc, lttpr_count;

	if (drm_dp_read_lttpr_common_caps(dp->aux, dpcd, dp->link->lttpr_common_caps))
		return 0;

	lttpr_count = drm_dp_lttpr_count(dp->link->lttpr_common_caps);
	rc = drm_dp_lttpr_init(dp->aux, lttpr_count);
	if (rc) {
		DRM_ERROR("failed to set LTTPRs transparency mode, rc=%d\n", rc);
		return 0;
	}

	return lttpr_count;
}

static int msm_dp_display_process_hpd_high(struct msm_dp_display_private *dp)
{
	struct drm_connector *connector = dp->msm_dp_display.connector;
	const struct drm_display_info *info = &connector->display_info;
	int rc = 0;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	rc = drm_dp_read_dpcd_caps(dp->aux, dpcd);
	if (rc)
		goto end;

	dp->link->lttpr_count = msm_dp_display_lttpr_init(dp, dpcd);

	rc = msm_dp_panel_read_sink_caps(dp->panel, connector);
	if (rc)
		goto end;

	msm_dp_link_process_request(dp->link);

	if (!dp->msm_dp_display.is_edp)
		drm_dp_set_subconnector_property(connector,
						 connector_status_connected,
						 dp->panel->dpcd,
						 dp->panel->downstream_ports);

	dp->msm_dp_display.psr_supported = dp->panel->psr_cap.version && psr_enabled;

	dp->audio_supported = info->has_audio;
	msm_dp_panel_handle_sink_request(dp->panel);

	/*
	 * set sink to normal operation mode -- D0
	 * before dpcd read
	 */
	msm_dp_link_psm_config(dp->link, &dp->panel->link_info, false);

	msm_dp_link_reset_phy_params_vx_px(dp->link);

end:
	return rc;
}

/**
 * msm_dp_display_host_phy_init() - start up DP PHY
 * @dp: main display data structure
 *
 * Prepare DP PHY for the AUX transactions to succeed.
 *
 * Returns: true if this call has initliazed the PHY and false if the PHY has
 * already been setup beforehand.
 */
static bool msm_dp_display_host_phy_init(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	if (!dp->phy_initialized) {
		msm_dp_ctrl_phy_init(dp->ctrl);
		dp->phy_initialized = true;
		return true;
	}

	return false;
}

static void msm_dp_display_host_phy_exit(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	if (dp->phy_initialized) {
		msm_dp_ctrl_phy_exit(dp->ctrl);
		dp->phy_initialized = false;
	}
}

static void msm_dp_display_host_init(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	msm_dp_ctrl_core_clk_enable(dp->ctrl);
	msm_dp_ctrl_reset(dp->ctrl);
	msm_dp_ctrl_enable_irq(dp->ctrl);
	msm_dp_aux_init(dp->aux);
	dp->core_initialized = true;
}

static void msm_dp_display_host_deinit(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	msm_dp_ctrl_reset(dp->ctrl);
	msm_dp_ctrl_disable_irq(dp->ctrl);
	msm_dp_aux_deinit(dp->aux);
	msm_dp_ctrl_core_clk_disable(dp->ctrl);
	dp->core_initialized = false;
}

static void msm_dp_display_handle_video_request(struct msm_dp_display_private *dp)
{
	if (dp->link->sink_request & DP_TEST_LINK_VIDEO_PATTERN) {
		dp->panel->video_test = true;
		msm_dp_link_send_test_response(dp->link);
	}
}

static int msm_dp_display_handle_irq_hpd(struct msm_dp_display_private *dp)
{
	u32 sink_request = dp->link->sink_request;

	drm_dbg_dp(dp->drm_dev, "%d\n", sink_request);

	msm_dp_ctrl_handle_sink_request(dp->ctrl);

	if (sink_request & DP_TEST_LINK_VIDEO_PATTERN)
		msm_dp_display_handle_video_request(dp);

	return 0;
}

static int msm_dp_hpd_plug_handle(struct msm_dp_display_private *dp)
{
	int ret;
	struct platform_device *pdev = dp->msm_dp_display.pdev;

	drm_dbg_dp(dp->drm_dev, "Before, type=%d sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	guard(mutex)(&dp->plugged_lock);

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		return ret;
	}

	msm_dp_aux_enable_xfers(dp->aux, true);

	msm_dp_display_host_phy_init(dp);

	ret = msm_dp_display_process_hpd_high(dp);

	drm_dbg_dp(dp->drm_dev, "After, type=%d sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	dp->plugged = true;

	return ret;
};

static void msm_dp_display_handle_plugged_change(struct msm_dp *msm_dp_display,
		bool plugged)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display,
			struct msm_dp_display_private, msm_dp_display);

	/* notify audio subsystem only if sink supports audio */
	if (dp->audio_supported)
		drm_connector_hdmi_audio_plugged_notify(msm_dp_display->connector,
							plugged);
}

static int msm_dp_hpd_unplug_handle(struct msm_dp_display_private *dp)
{
	struct platform_device *pdev = dp->msm_dp_display.pdev;

	dp->panel->video_test = false;

	msm_dp_aux_enable_xfers(dp->aux, false);

	drm_dbg_dp(dp->drm_dev, "Before, type=%d sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	guard(mutex)(&dp->plugged_lock);
	if (!dp->plugged)
		return 0;

	/* Don't forget modes for eDP */
	if (!dp->msm_dp_display.is_edp)
		msm_dp_panel_unplugged(dp->panel, dp->msm_dp_display.connector);

	/* triggered by irq_hdp with sink_count = 0 */
	if (dp->link->sink_count == 0)
		msm_dp_display_host_phy_exit(dp);

	/*
	 * We don't need separate work for disconnect as
	 * connect/attention interrupts are disabled
	 */
	if (!dp->msm_dp_display.is_edp)
		drm_dp_set_subconnector_property(dp->msm_dp_display.connector,
						 connector_status_disconnected,
						 dp->panel->dpcd,
						 dp->panel->downstream_ports);

	/* signal the disconnect event early to ensure proper teardown */
	msm_dp_display_handle_plugged_change(&dp->msm_dp_display, false);

	drm_dbg_dp(dp->drm_dev, "After, type=%d, sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	if (dp->plugged) {
		pm_runtime_put_sync(&pdev->dev);
		dp->plugged = false;
	}

	return 0;
}

static int msm_dp_irq_hpd_handle(struct msm_dp_display_private *dp)
{
	u32 sink_request;
	int rc = 0;

	/* irq_hpd can happen at either connected or disconnected state */
	drm_dbg_dp(dp->drm_dev, "Before, type=%d, sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	/* check for any test request issued by sink */
	rc = msm_dp_link_process_request(dp->link);
	if (!rc) {
		sink_request = dp->link->sink_request;
		drm_dbg_dp(dp->drm_dev, "sink_request=%d\n", sink_request);
		if (sink_request & DS_PORT_STATUS_CHANGED)
			rc = msm_dp_display_process_hpd_high(dp);
		else
			rc = msm_dp_display_handle_irq_hpd(dp);
	}

	drm_dbg_dp(dp->drm_dev, "After, type=%d, sink_count=%d\n",
			dp->msm_dp_display.connector_type,
			dp->link->sink_count);

	return rc;
}

static void msm_dp_display_deinit_sub_modules(struct msm_dp_display_private *dp)
{
	msm_dp_audio_put(dp->audio);
	msm_dp_panel_put(dp->panel);
	msm_dp_aux_put(dp->aux);
}

static int msm_dp_init_sub_modules(struct msm_dp_display_private *dp)
{
	int rc = 0;
	struct device *dev = &dp->msm_dp_display.pdev->dev;
	struct phy *phy;

	phy = devm_phy_get(dev, "dp");
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	rc = phy_set_mode_ext(phy, PHY_MODE_DP,
			      dp->msm_dp_display.is_edp ? PHY_SUBMODE_EDP : PHY_SUBMODE_DP);
	if (rc) {
		DRM_ERROR("failed to set phy submode, rc = %d\n", rc);
		goto error;
	}

	dp->aux = msm_dp_aux_get(dev, phy, dp->msm_dp_display.is_edp, dp->aux_base);
	if (IS_ERR(dp->aux)) {
		rc = PTR_ERR(dp->aux);
		DRM_ERROR("failed to initialize aux, rc = %d\n", rc);
		dp->aux = NULL;
		goto error;
	}

	dp->link = msm_dp_link_get(dev, dp->aux);
	if (IS_ERR(dp->link)) {
		rc = PTR_ERR(dp->link);
		DRM_ERROR("failed to initialize link, rc = %d\n", rc);
		dp->link = NULL;
		goto error_link;
	}

	dp->panel = msm_dp_panel_get(dev, dp->aux, dp->link, dp->link_base, dp->p0_base);
	if (IS_ERR(dp->panel)) {
		rc = PTR_ERR(dp->panel);
		DRM_ERROR("failed to initialize panel, rc = %d\n", rc);
		dp->panel = NULL;
		goto error_link;
	}

	dp->ctrl = msm_dp_ctrl_get(dev, dp->link, dp->panel, dp->aux,
			       phy, dp->ahb_base, dp->link_base);
	if (IS_ERR(dp->ctrl)) {
		rc = PTR_ERR(dp->ctrl);
		DRM_ERROR("failed to initialize ctrl, rc = %d\n", rc);
		dp->ctrl = NULL;
		goto error_ctrl;
	}

	dp->audio = msm_dp_audio_get(dp->msm_dp_display.pdev, dp->link_base);
	if (IS_ERR(dp->audio)) {
		rc = PTR_ERR(dp->audio);
		pr_err("failed to initialize audio, rc = %d\n", rc);
		dp->audio = NULL;
		goto error_ctrl;
	}

	return rc;

error_ctrl:
	msm_dp_panel_put(dp->panel);
error_link:
	msm_dp_aux_put(dp->aux);
error:
	return rc;
}

static int msm_dp_display_set_mode(struct msm_dp *msm_dp_display,
			       struct msm_dp_display_mode *mode)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	drm_mode_copy(&dp->panel->msm_dp_mode.drm_mode, &mode->drm_mode);
	dp->panel->msm_dp_mode.bpp = mode->bpp;
	dp->panel->msm_dp_mode.out_fmt_is_yuv_420 = mode->out_fmt_is_yuv_420;
	msm_dp_panel_init_panel_info(dp->panel);
	return 0;
}

static int msm_dp_display_enable(struct msm_dp_display_private *dp, bool force_link_train)
{
	int rc = 0;
	struct msm_dp *msm_dp_display = &dp->msm_dp_display;

	drm_dbg_dp(dp->drm_dev, "sink_count=%d\n", dp->link->sink_count);
	if (msm_dp_display->power_on) {
		drm_dbg_dp(dp->drm_dev, "Link already setup, return\n");
		return 0;
	}

	rc = msm_dp_ctrl_on_stream(dp->ctrl, force_link_train);
	if (!rc)
		msm_dp_display->power_on = true;

	return rc;
}

static int msm_dp_display_post_enable(struct msm_dp *msm_dp_display)
{
	struct msm_dp_display_private *dp;
	u32 rate;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	rate = dp->link->link_params.rate;

	if (dp->audio_supported) {
		dp->audio->bw_code = drm_dp_link_rate_to_bw_code(rate);
		dp->audio->lane_count = dp->link->link_params.num_lanes;
	}

	/* signal the connect event late to synchronize video and display */
	msm_dp_display_handle_plugged_change(msm_dp_display, true);

	if (msm_dp_display->psr_supported)
		msm_dp_ctrl_config_psr(dp->ctrl);

	return 0;
}

static int msm_dp_display_disable(struct msm_dp_display_private *dp)
{
	struct msm_dp *msm_dp_display = &dp->msm_dp_display;

	if (!msm_dp_display->power_on)
		return 0;

	/* wait only if audio was enabled */
	if (msm_dp_display->audio_enabled) {
		/* signal the disconnect event */
		msm_dp_display_handle_plugged_change(msm_dp_display, false);
		if (!wait_for_completion_timeout(&dp->audio_comp,
				HZ * 5))
			DRM_ERROR("audio comp timeout\n");
	}

	msm_dp_display->audio_enabled = false;

	if (dp->link->sink_count == 0) {
		/*
		 * irq_hpd with sink_count = 0
		 * hdmi unplugged out of dongle
		 */
		msm_dp_ctrl_off_link_stream(dp->ctrl);
	} else {
		/*
		 * unplugged interrupt
		 * dongle unplugged out of DUT
		 */
		msm_dp_ctrl_off(dp->ctrl);
		msm_dp_display_host_phy_exit(dp);
	}

	msm_dp_display->power_on = false;

	drm_dbg_dp(dp->drm_dev, "sink count: %d\n", dp->link->sink_count);
	return 0;
}

/**
 * msm_dp_bridge_mode_valid - callback to determine if specified mode is valid
 * @bridge: Pointer to drm bridge structure
 * @info: display info
 * @mode: Pointer to drm mode structure
 * Returns: Validity status for specified mode
 */
enum drm_mode_status msm_dp_bridge_mode_valid(struct drm_bridge *bridge,
					  const struct drm_display_info *info,
					  const struct drm_display_mode *mode)
{
	const u32 num_components = 3, default_bpp = 24;
	struct msm_dp_display_private *msm_dp_display;
	struct msm_dp_link_info *link_info;
	u32 mode_rate_khz = 0, supported_rate_khz = 0, mode_bpp = 0;
	struct msm_dp *dp;
	int mode_pclk_khz = mode->clock;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	if (!dp || !mode_pclk_khz || !dp->connector) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);
	link_info = &msm_dp_display->panel->link_info;

	if ((drm_mode_is_420_only(&dp->connector->display_info, mode) &&
	     msm_dp_display->panel->vsc_sdp_supported) ||
	     msm_dp_wide_bus_available(dp))
		mode_pclk_khz /= 2;

	if (mode_pclk_khz > DP_MAX_PIXEL_CLK_KHZ)
		return MODE_CLOCK_HIGH;

	mode_bpp = dp->connector->display_info.bpc * num_components;
	if (!mode_bpp)
		mode_bpp = default_bpp;

	mode_bpp = msm_dp_panel_get_mode_bpp(msm_dp_display->panel,
			mode_bpp, mode_pclk_khz);

	mode_rate_khz = mode_pclk_khz * mode_bpp;
	supported_rate_khz = link_info->num_lanes * link_info->rate * 8;

	if (mode_rate_khz > supported_rate_khz)
		return MODE_BAD;

	return MODE_OK;
}

int msm_dp_display_get_modes(struct msm_dp *dp)
{
	struct msm_dp_display_private *msm_dp_display;

	if (!dp) {
		DRM_ERROR("invalid params\n");
		return 0;
	}

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	return msm_dp_panel_get_modes(msm_dp_display->panel,
		dp->connector);
}

bool msm_dp_display_check_video_test(struct msm_dp *dp)
{
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	return msm_dp_display->panel->video_test;
}

int msm_dp_display_get_test_bpp(struct msm_dp *dp)
{
	struct msm_dp_display_private *msm_dp_display;

	if (!dp) {
		DRM_ERROR("invalid params\n");
		return 0;
	}

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	return msm_dp_link_bit_depth_to_bpp(
		msm_dp_display->link->test_video.test_bit_depth);
}

void msm_dp_snapshot(struct msm_disp_state *disp_state, struct msm_dp *dp)
{
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	/*
	 * if we are reading registers we need the link clocks to be on
	 * however till DP cable is connected this will not happen as we
	 * do not know the resolution to power up with. Hence check the
	 * power_on status before dumping DP registers to avoid crash due
	 * to unclocked access
	 */
	if (!dp->power_on)
		return;

	msm_disp_snapshot_add_block(disp_state, msm_dp_display->ahb_len,
				    msm_dp_display->ahb_base, "dp_ahb");
	msm_disp_snapshot_add_block(disp_state, msm_dp_display->aux_len,
				    msm_dp_display->aux_base, "dp_aux");
	msm_disp_snapshot_add_block(disp_state, msm_dp_display->link_len,
				    msm_dp_display->link_base, "dp_link");
	msm_disp_snapshot_add_block(disp_state, msm_dp_display->p0_len,
				    msm_dp_display->p0_base, "dp_p0");
}

void msm_dp_display_set_psr(struct msm_dp *msm_dp_display, bool enter)
{
	struct msm_dp_display_private *dp;

	if (!msm_dp_display) {
		DRM_ERROR("invalid params\n");
		return;
	}

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);
	msm_dp_ctrl_set_psr(dp->ctrl, enter);
}

/**
 * msm_dp_bridge_detect - callback to determine if connector is connected
 *
 * @bridge: Pointer to drm bridge structure
 * @connector: Pointer to drm connector structure
 *
 * Returns: where there is a display connected to the DPTX (returning
 * disconnected for branch devices without DP Sinks being connected).
 */
enum drm_connector_status msm_dp_bridge_detect(struct drm_bridge *bridge,
					       struct drm_connector *connector)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	int status = connector_status_disconnected;
	struct msm_dp_display_private *priv;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	struct drm_dp_desc desc;
	bool phy_deinit;
	int ret;

	dp = to_dp_bridge(bridge)->msm_dp_display;

	priv = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	guard(mutex)(&priv->plugged_lock);
	ret = pm_runtime_resume_and_get(&dp->pdev->dev);
	if (ret) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		return status;
	}

	phy_deinit = msm_dp_display_host_phy_init(priv);

	msm_dp_aux_enable_xfers(priv->aux, true);

	ret = msm_dp_aux_is_link_connected(priv->aux);
	DRM_DEBUG_DP("aux link status: %x\n", ret);
	if (!priv->plugged && !ret) {
		DRM_DEBUG_DP("aux not connected\n");
		priv->plugged = false;
		goto end;
	}

	ret = drm_dp_read_dpcd_caps(priv->aux, dpcd);
	if (ret) {
		DRM_DEBUG_DP("failed to read caps\n");
		priv->plugged = false;
		goto end;
	}

	ret = drm_dp_read_desc(priv->aux, &desc, drm_dp_is_branch(dpcd));
	if (ret) {
		DRM_DEBUG_DP("failed to read desc\n");
		priv->plugged = false;
		goto end;
	}

	status = connector_status_connected;
	priv->plugged = true;

	if (drm_dp_read_sink_count_cap(connector, dpcd, &desc)) {
		int sink_count = drm_dp_read_sink_count(priv->aux);

		drm_dbg_dp(dp->drm_dev, "sink_count = %d\n", sink_count);

		if (sink_count <= 0)
			status = connector_status_disconnected;
	}

end:
	/*
	 * If we detected the DPRX, leave the controller on so that it doesn't
	 * lose the state.
	 */
	if (!priv->plugged) {
		if (phy_deinit) {
			msm_dp_aux_enable_xfers(priv->aux, false);
			msm_dp_display_host_phy_exit(priv);
		}

		pm_runtime_put_sync(&dp->pdev->dev);
	}

	return status;
}

static irqreturn_t msm_dp_display_irq_handler(int irq, void *dev_id)
{
	struct msm_dp_display_private *dp = dev_id;
	u32 hpd_isr_status;
	unsigned long flags;
	irqreturn_t ret = IRQ_HANDLED;

	hpd_isr_status = msm_dp_aux_get_hpd_intr_status(dp->aux);

	if (hpd_isr_status & 0x0F) {
		drm_dbg_dp(dp->drm_dev, "type=%d isr=0x%x\n",
			dp->msm_dp_display.connector_type, hpd_isr_status);

		spin_lock_irqsave(&dp->irq_thread_lock, flags);
		dp->hpd_isr_status |= hpd_isr_status;
		ret = IRQ_WAKE_THREAD;
		spin_unlock_irqrestore(&dp->irq_thread_lock, flags);
	}

	/* DP controller isr */
	ret |= msm_dp_ctrl_isr(dp->ctrl);

	return ret;
}

static irqreturn_t msm_dp_display_irq_thread(int irq, void *dev_id)
{
	struct msm_dp_display_private *dp = dev_id;
	irqreturn_t ret = IRQ_NONE;
	unsigned long flags;
	u32 hpd_isr_status;

	spin_lock_irqsave(&dp->irq_thread_lock, flags);
	hpd_isr_status = dp->hpd_isr_status;
	dp->hpd_isr_status = 0;
	spin_unlock_irqrestore(&dp->irq_thread_lock, flags);

	if (hpd_isr_status & DP_DP_HPD_UNPLUG_INT_MASK)
		drm_bridge_hpd_notify(dp->msm_dp_display.bridge,
				      connector_status_disconnected);

	if (hpd_isr_status & DP_DP_HPD_PLUG_INT_MASK)
		drm_bridge_hpd_notify(dp->msm_dp_display.bridge,
				      connector_status_connected);

	/* Send HPD as connected and distinguish it in the notifier */
	if (hpd_isr_status & DP_DP_IRQ_HPD_INT_MASK)
		drm_bridge_hpd_notify(dp->msm_dp_display.bridge,
				      connector_status_connected);

	ret = IRQ_HANDLED;

	return ret;
}

static int msm_dp_display_request_irq(struct msm_dp_display_private *dp)
{
	int rc = 0;
	struct platform_device *pdev = dp->msm_dp_display.pdev;

	dp->irq = platform_get_irq(pdev, 0);
	if (dp->irq < 0) {
		DRM_ERROR("failed to get irq\n");
		return dp->irq;
	}

	spin_lock_init(&dp->irq_thread_lock);
	irq_set_status_flags(dp->irq, IRQ_NOAUTOEN);
	rc = devm_request_threaded_irq(&pdev->dev, dp->irq,
				       msm_dp_display_irq_handler,
				       msm_dp_display_irq_thread,
				       IRQ_TYPE_LEVEL_HIGH,
				       "dp_display_isr", dp);

	if (rc < 0) {
		DRM_ERROR("failed to request IRQ%u: %d\n",
				dp->irq, rc);
		return rc;
	}

	return 0;
}

static const struct msm_dp_desc *msm_dp_display_get_desc(struct platform_device *pdev)
{
	const struct msm_dp_desc *descs = of_device_get_match_data(&pdev->dev);
	struct resource *res;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return NULL;

	for (i = 0; i < descs[i].io_start; i++) {
		if (descs[i].io_start == res->start)
			return &descs[i];
	}

	dev_err(&pdev->dev, "unknown displayport instance\n");
	return NULL;
}

static int msm_dp_display_probe_tail(struct device *dev)
{
	struct msm_dp *dp = dev_get_drvdata(dev);
	int ret;

	/*
	 * External bridges are mandatory for eDP interfaces: one has to
	 * provide at least an eDP panel (which gets wrapped into panel-bridge).
	 *
	 * For DisplayPort interfaces external bridges are optional, so
	 * silently ignore an error if one is not present (-ENODEV).
	 */
	dp->next_bridge = devm_drm_of_get_bridge(&dp->pdev->dev, dp->pdev->dev.of_node, 1, 0);
	if (IS_ERR(dp->next_bridge)) {
		ret = PTR_ERR(dp->next_bridge);
		dp->next_bridge = NULL;
		if (dp->is_edp || ret != -ENODEV)
			return ret;
	}

	ret = component_add(dev, &msm_dp_display_comp_ops);
	if (ret)
		DRM_ERROR("component add failed, rc=%d\n", ret);

	return ret;
}

static int msm_dp_auxbus_done_probe(struct drm_dp_aux *aux)
{
	return msm_dp_display_probe_tail(aux->dev);
}

static int msm_dp_display_get_connector_type(struct platform_device *pdev,
					 const struct msm_dp_desc *desc)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *aux_bus = of_get_child_by_name(node, "aux-bus");
	struct device_node *panel = of_get_child_by_name(aux_bus, "panel");
	int connector_type;

	if (panel)
		connector_type = DRM_MODE_CONNECTOR_eDP;
	else
		connector_type = DRM_MODE_SUBCONNECTOR_DisplayPort;

	of_node_put(panel);
	of_node_put(aux_bus);

	return connector_type;
}

static void __iomem *msm_dp_ioremap(struct platform_device *pdev, int idx, size_t *len)
{
	struct resource *res;
	void __iomem *base;

	base = devm_platform_get_and_ioremap_resource(pdev, idx, &res);
	if (!IS_ERR(base))
		*len = resource_size(res);

	return base;
}

#define DP_DEFAULT_AHB_OFFSET	0x0000
#define DP_DEFAULT_AHB_SIZE	0x0200
#define DP_DEFAULT_AUX_OFFSET	0x0200
#define DP_DEFAULT_AUX_SIZE	0x0200
#define DP_DEFAULT_LINK_OFFSET	0x0400
#define DP_DEFAULT_LINK_SIZE	0x0C00
#define DP_DEFAULT_P0_OFFSET	0x1000
#define DP_DEFAULT_P0_SIZE	0x0400

static int msm_dp_display_get_io(struct msm_dp_display_private *display)
{
	struct platform_device *pdev = display->msm_dp_display.pdev;

	display->ahb_base = msm_dp_ioremap(pdev, 0, &display->ahb_len);
	if (IS_ERR(display->ahb_base))
		return PTR_ERR(display->ahb_base);

	display->aux_base = msm_dp_ioremap(pdev, 1, &display->aux_len);
	if (IS_ERR(display->aux_base)) {
		if (display->aux_base != ERR_PTR(-EINVAL)) {
			DRM_ERROR("unable to remap aux region: %pe\n", display->aux_base);
			return PTR_ERR(display->aux_base);
		}

		/*
		 * The initial binding had a single reg, but in order to
		 * support variation in the sub-region sizes this was split.
		 * msm_dp_ioremap() will fail with -EINVAL here if only a single
		 * reg is specified, so fill in the sub-region offsets and
		 * lengths based on this single region.
		 */
		if (display->ahb_len < DP_DEFAULT_P0_OFFSET + DP_DEFAULT_P0_SIZE) {
			DRM_ERROR("legacy memory region not large enough\n");
			return -EINVAL;
		}

		display->ahb_len = DP_DEFAULT_AHB_SIZE;
		display->aux_base = display->ahb_base + DP_DEFAULT_AUX_OFFSET;
		display->aux_len = DP_DEFAULT_AUX_SIZE;
		display->link_base = display->ahb_base + DP_DEFAULT_LINK_OFFSET;
		display->link_len = DP_DEFAULT_LINK_SIZE;
		display->p0_base = display->ahb_base + DP_DEFAULT_P0_OFFSET;
		display->p0_len = DP_DEFAULT_P0_SIZE;

		return 0;
	}

	display->link_base = msm_dp_ioremap(pdev, 2, &display->link_len);
	if (IS_ERR(display->link_base)) {
		DRM_ERROR("unable to remap link region: %pe\n", display->link_base);
		return PTR_ERR(display->link_base);
	}

	display->p0_base = msm_dp_ioremap(pdev, 3, &display->p0_len);
	if (IS_ERR(display->p0_base)) {
		DRM_ERROR("unable to remap p0 region: %pe\n", display->p0_base);
		return PTR_ERR(display->p0_base);
	}

	return 0;
}

static int msm_dp_display_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_dp_display_private *dp;
	const struct msm_dp_desc *desc;

	if (!pdev || !pdev->dev.of_node) {
		DRM_ERROR("pdev not found\n");
		return -ENODEV;
	}

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	desc = msm_dp_display_get_desc(pdev);
	if (!desc)
		return -EINVAL;

	dp->msm_dp_display.pdev = pdev;
	dp->id = desc->id;
	dp->msm_dp_display.connector_type = msm_dp_display_get_connector_type(pdev, desc);
	dp->wide_bus_supported = desc->wide_bus_supported;
	dp->msm_dp_display.is_edp =
		(dp->msm_dp_display.connector_type == DRM_MODE_CONNECTOR_eDP);
	dp->hpd_isr_status = 0;

	mutex_init(&dp->plugged_lock);

	rc = msm_dp_display_get_io(dp);
	if (rc)
		return rc;

	rc = msm_dp_init_sub_modules(dp);
	if (rc) {
		DRM_ERROR("init sub module failed\n");
		return -EPROBE_DEFER;
	}

	/* Store DP audio handle inside DP display */
	dp->msm_dp_display.msm_dp_audio = dp->audio;

	init_completion(&dp->audio_comp);

	platform_set_drvdata(pdev, &dp->msm_dp_display);

	rc = devm_pm_runtime_enable(&pdev->dev);
	if (rc)
		goto err;

	rc = msm_dp_display_request_irq(dp);
	if (rc)
		goto err;

	if (dp->msm_dp_display.is_edp) {
		rc = devm_of_dp_aux_populate_bus(dp->aux, msm_dp_auxbus_done_probe);
		if (rc) {
			DRM_ERROR("eDP auxbus population failed, rc=%d\n", rc);
			goto err;
		}
	} else {
		rc = msm_dp_display_probe_tail(&pdev->dev);
		if (rc)
			goto err;
	}

	return rc;

err:
	msm_dp_display_deinit_sub_modules(dp);
	return rc;
}

static void msm_dp_display_remove(struct platform_device *pdev)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(&pdev->dev);

	component_del(&pdev->dev, &msm_dp_display_comp_ops);
	msm_dp_display_deinit_sub_modules(dp);
	platform_set_drvdata(pdev, NULL);
}

static int msm_dp_pm_runtime_suspend(struct device *dev)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);

	disable_irq(dp->irq);

	if (dp->msm_dp_display.is_edp) {
		msm_dp_display_host_phy_exit(dp);
		msm_dp_aux_hpd_disable(dp->aux);
	}
	msm_dp_display_host_deinit(dp);

	return 0;
}

static int msm_dp_pm_runtime_resume(struct device *dev)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);

	/*
	 * for eDP, host cotroller, HPD block and PHY are enabled here
	 * but with HPD irq disabled
	 *
	 * for DP, only host controller is enabled here.
	 * HPD block is enabled at msm_dp_bridge_hpd_enable()
	 * PHY will be enabled at plugin handler later
	 */
	msm_dp_display_host_init(dp);
	if (dp->msm_dp_display.is_edp) {
		msm_dp_aux_hpd_enable(dp->aux);
		msm_dp_display_host_phy_init(dp);
	}

	enable_irq(dp->irq);
	return 0;
}

static const struct dev_pm_ops msm_dp_pm_ops = {
	SET_RUNTIME_PM_OPS(msm_dp_pm_runtime_suspend, msm_dp_pm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver msm_dp_display_driver = {
	.probe  = msm_dp_display_probe,
	.remove = msm_dp_display_remove,
	.driver = {
		.name = "msm-dp-display",
		.of_match_table = msm_dp_dt_match,
		.suppress_bind_attrs = true,
		.pm = &msm_dp_pm_ops,
	},
};

int __init msm_dp_register(void)
{
	int ret;

	ret = platform_driver_register(&msm_dp_display_driver);
	if (ret)
		DRM_ERROR("Dp display driver register failed");

	return ret;
}

void __exit msm_dp_unregister(void)
{
	platform_driver_unregister(&msm_dp_display_driver);
}

bool msm_dp_is_yuv_420_enabled(const struct msm_dp *msm_dp_display,
			       const struct drm_display_mode *mode)
{
	struct msm_dp_display_private *dp;
	const struct drm_display_info *info;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);
	info = &msm_dp_display->connector->display_info;

	return dp->panel->vsc_sdp_supported && drm_mode_is_420_only(info, mode);
}

bool msm_dp_needs_periph_flush(const struct msm_dp *msm_dp_display,
			       const struct drm_display_mode *mode)
{
	return msm_dp_is_yuv_420_enabled(msm_dp_display, mode);
}

bool msm_dp_wide_bus_available(const struct msm_dp *msm_dp_display)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	if (dp->msm_dp_mode.out_fmt_is_yuv_420)
		return false;

	return dp->wide_bus_supported;
}

void msm_dp_display_debugfs_init(struct msm_dp *msm_dp_display, struct dentry *root, bool is_edp)
{
	struct msm_dp_display_private *dp;
	struct device *dev;
	int rc;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);
	dev = &dp->msm_dp_display.pdev->dev;

	rc = msm_dp_debug_init(dev, dp->panel, dp->link, dp->msm_dp_display.connector, root, is_edp);
	if (rc)
		DRM_ERROR("failed to initialize debug, rc = %d\n", rc);
}

int msm_dp_modeset_init(struct msm_dp *msm_dp_display, struct drm_device *dev,
			struct drm_encoder *encoder, bool yuv_supported)
{
	struct msm_dp_display_private *msm_dp_priv;
	int ret;

	msm_dp_display->drm_dev = dev;

	msm_dp_priv = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	ret = msm_dp_bridge_init(msm_dp_display, dev, encoder, yuv_supported);
	if (ret) {
		DRM_DEV_ERROR(dev->dev,
			"failed to create dp bridge: %d\n", ret);
		return ret;
	}

	msm_dp_display->connector = msm_dp_drm_connector_init(msm_dp_display, encoder);
	if (IS_ERR(msm_dp_display->connector)) {
		ret = PTR_ERR(msm_dp_display->connector);
		DRM_DEV_ERROR(dev->dev,
			"failed to create dp connector: %d\n", ret);
		msm_dp_display->connector = NULL;
		return ret;
	}

	msm_dp_priv->panel->connector = msm_dp_display->connector;

	return 0;
}

void msm_dp_bridge_atomic_enable(struct drm_bridge *drm_bridge,
				 struct drm_atomic_commit *state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	int rc = 0;
	struct msm_dp_display_private *msm_dp_display;
	bool force_link_train = false;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);
	if (!msm_dp_display->msm_dp_mode.drm_mode.clock) {
		DRM_ERROR("invalid params\n");
		return;
	}

	if (dp->is_edp)
		msm_dp_hpd_plug_handle(msm_dp_display);

	if (pm_runtime_resume_and_get(&dp->pdev->dev)) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		return;
	}

	if (msm_dp_display->link->sink_count == 0)
		return;

	rc = msm_dp_display_set_mode(dp, &msm_dp_display->msm_dp_mode);
	if (rc) {
		DRM_ERROR("Failed to perform a mode set, rc=%d\n", rc);
		return;
	}

	if (!dp->power_on) {
		msm_dp_display_host_phy_init(msm_dp_display);
		force_link_train = true;
	}

	rc = msm_dp_ctrl_on_link(msm_dp_display->ctrl);
	if (rc) {
		DRM_ERROR("Failed link training (rc=%d)\n", rc);
		// TODO: schedule drm_connector_set_link_status_property()
		return;
	}

	msm_dp_display_enable(msm_dp_display, force_link_train);

	rc = msm_dp_display_post_enable(dp);
	if (rc) {
		DRM_ERROR("DP display post enable failed, rc=%d\n", rc);
		msm_dp_display_disable(msm_dp_display);
	}

	drm_dbg_dp(dp->drm_dev, "type=%d Done\n", dp->connector_type);
}

void msm_dp_bridge_atomic_disable(struct drm_bridge *drm_bridge,
				  struct drm_atomic_commit *state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	msm_dp_ctrl_push_idle(msm_dp_display->ctrl);
}

void msm_dp_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
				       struct drm_atomic_commit *state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	if (dp->is_edp)
		msm_dp_hpd_unplug_handle(msm_dp_display);

	msm_dp_display_disable(msm_dp_display);

	drm_dbg_dp(dp->drm_dev, "type=%d Done\n", dp->connector_type);

	pm_runtime_put_sync(&dp->pdev->dev);
}

void msm_dp_bridge_mode_set(struct drm_bridge *drm_bridge,
			const struct drm_display_mode *mode,
			const struct drm_display_mode *adjusted_mode)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *msm_dp_display;
	struct msm_dp_panel *msm_dp_panel;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);
	msm_dp_panel = msm_dp_display->panel;

	memset(&msm_dp_display->msm_dp_mode, 0x0, sizeof(struct msm_dp_display_mode));

	if (msm_dp_display_check_video_test(dp))
		msm_dp_display->msm_dp_mode.bpp = msm_dp_display_get_test_bpp(dp);
	else /* Default num_components per pixel = 3 */
		msm_dp_display->msm_dp_mode.bpp = dp->connector->display_info.bpc * 3;

	if (!msm_dp_display->msm_dp_mode.bpp)
		msm_dp_display->msm_dp_mode.bpp = 24; /* Default bpp */

	drm_mode_copy(&msm_dp_display->msm_dp_mode.drm_mode, adjusted_mode);

	msm_dp_display->msm_dp_mode.v_active_low =
		!!(msm_dp_display->msm_dp_mode.drm_mode.flags & DRM_MODE_FLAG_NVSYNC);

	msm_dp_display->msm_dp_mode.h_active_low =
		!!(msm_dp_display->msm_dp_mode.drm_mode.flags & DRM_MODE_FLAG_NHSYNC);

	msm_dp_display->msm_dp_mode.out_fmt_is_yuv_420 =
		drm_mode_is_420_only(&dp->connector->display_info, adjusted_mode) &&
		msm_dp_panel->vsc_sdp_supported;

	/* populate wide_bus_support to different layers */
	msm_dp_display->ctrl->wide_bus_en =
		msm_dp_display->msm_dp_mode.out_fmt_is_yuv_420 ? false : msm_dp_display->wide_bus_supported;
}

void msm_dp_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *msm_dp_display = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	/*
	 * this is for external DP with hpd irq enabled case,
	 * step-1: msm_dp_pm_runtime_resume() enable dp host only
	 * step-2: enable hdp block and have hpd irq enabled here
	 * step-3: waiting for plugin irq while phy is not initialized
	 * step-4: DP PHY is initialized at plugin handler before link training
	 *
	 */
	if (pm_runtime_resume_and_get(&msm_dp_display->pdev->dev)) {
		DRM_ERROR("failed to resume power\n");
		return;
	}

	msm_dp_aux_hpd_enable(dp->aux);
	msm_dp_aux_hpd_intr_enable(dp->aux);
}

void msm_dp_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *msm_dp_display = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	msm_dp_aux_hpd_intr_disable(dp->aux);
	msm_dp_aux_hpd_disable(dp->aux);

	pm_runtime_put_sync(&msm_dp_display->pdev->dev);
}

void msm_dp_bridge_hpd_notify(struct drm_bridge *bridge,
			      struct drm_connector *connector,
			      enum drm_connector_status status)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *msm_dp_display = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);
	u32 hpd_link_status = 0;

	if (pm_runtime_resume_and_get(&msm_dp_display->pdev->dev)) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		return;
	}

	hpd_link_status = msm_dp_aux_is_link_connected(dp->aux);

	drm_dbg_dp(dp->drm_dev, "type=%d link hpd_link_status=0x%x, status=%d\n",
		   msm_dp_display->connector_type, hpd_link_status, status);

	if (status == connector_status_connected) {
		if (hpd_link_status == ISR_HPD_REPLUG_COUNT) {
			msm_dp_hpd_unplug_handle(dp);
			msm_dp_hpd_plug_handle(dp);
		} else if (hpd_link_status == ISR_IRQ_HPD_PULSE_COUNT) {
			msm_dp_irq_hpd_handle(dp);
		} else {
			msm_dp_hpd_plug_handle(dp);
		}
	} else {
		msm_dp_hpd_unplug_handle(dp);
	}

	pm_runtime_put_sync(&msm_dp_display->pdev->dev);
}
