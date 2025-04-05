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
#include <drm/display/drm_dp_aux_bus.h>
#include <drm/drm_edid.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "dp_ctrl.h"
#include "dp_catalog.h"
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
	ISR_HPD_REPLUG_COUNT,
	ISR_IRQ_HPD_PULSE_COUNT,
	ISR_HPD_LO_GLITH_COUNT,
};

/* event thread connection state */
enum {
	ST_DISCONNECTED,
	ST_MAINLINK_READY,
	ST_CONNECTED,
	ST_DISCONNECT_PENDING,
	ST_DISPLAY_OFF,
};

enum {
	EV_NO_EVENT,
	/* hpd events */
	EV_HPD_PLUG_INT,
	EV_IRQ_HPD_INT,
	EV_HPD_UNPLUG_INT,
	EV_USER_NOTIFICATION,
};

#define EVENT_TIMEOUT	(HZ/10)	/* 100ms */
#define DP_EVENT_Q_MAX	8

#define DP_TIMEOUT_NONE		0

#define WAIT_FOR_RESUME_TIMEOUT_JIFFIES (HZ / 2)

struct msm_dp_event {
	u32 event_id;
	u32 data;
	u32 delay;
};

struct msm_dp_display_private {
	int irq;

	unsigned int id;

	/* state variables */
	bool core_initialized;
	bool phy_initialized;
	bool audio_supported;

	struct drm_device *drm_dev;

	struct msm_dp_catalog *catalog;
	struct drm_dp_aux *aux;
	struct msm_dp_link    *link;
	struct msm_dp_panel   *panel;
	struct msm_dp_ctrl    *ctrl;

	struct msm_dp_display_mode msm_dp_mode;
	struct msm_dp msm_dp_display;

	/* wait for audio signaling */
	struct completion audio_comp;

	/* event related only access by event thread */
	struct mutex event_mutex;
	wait_queue_head_t event_q;
	u32 hpd_state;
	u32 event_pndx;
	u32 event_gndx;
	struct task_struct *ev_tsk;
	struct msm_dp_event event_list[DP_EVENT_Q_MAX];
	spinlock_t event_lock;

	bool wide_bus_supported;

	struct msm_dp_audio *audio;
};

struct msm_dp_desc {
	phys_addr_t io_start;
	unsigned int id;
	bool wide_bus_supported;
};

static const struct msm_dp_desc msm_dp_desc_sa8775p[] = {
	{ .io_start = 0x0af54000, .id = MSM_DP_CONTROLLER_0, .wide_bus_supported = true },
	{ .io_start = 0x0af5c000, .id = MSM_DP_CONTROLLER_1, .wide_bus_supported = true },
	{ .io_start = 0x22154000, .id = MSM_DP_CONTROLLER_2, .wide_bus_supported = true },
	{ .io_start = 0x2215c000, .id = MSM_DP_CONTROLLER_3, .wide_bus_supported = true },
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
	{ .compatible = "qcom,sa8775p-dp", .data = &msm_dp_desc_sa8775p },
	{ .compatible = "qcom,sc7180-dp", .data = &msm_dp_desc_sc7180 },
	{ .compatible = "qcom,sc7280-dp", .data = &msm_dp_desc_sc7280 },
	{ .compatible = "qcom,sc7280-edp", .data = &msm_dp_desc_sc7280 },
	{ .compatible = "qcom,sc8180x-dp", .data = &msm_dp_desc_sc8180x },
	{ .compatible = "qcom,sc8180x-edp", .data = &msm_dp_desc_sc8180x },
	{ .compatible = "qcom,sc8280xp-dp", .data = &msm_dp_desc_sc8280xp },
	{ .compatible = "qcom,sc8280xp-edp", .data = &msm_dp_desc_sc8280xp },
	{ .compatible = "qcom,sdm845-dp", .data = &msm_dp_desc_sc7180 },
	{ .compatible = "qcom,sm8350-dp", .data = &msm_dp_desc_sc7180 },
	{ .compatible = "qcom,sm8650-dp", .data = &msm_dp_desc_sm8650 },
	{ .compatible = "qcom,x1e80100-dp", .data = &msm_dp_desc_x1e80100 },
	{}
};

static struct msm_dp_display_private *dev_get_dp_display_private(struct device *dev)
{
	struct msm_dp *dp = dev_get_drvdata(dev);

	return container_of(dp, struct msm_dp_display_private, msm_dp_display);
}

static int msm_dp_add_event(struct msm_dp_display_private *msm_dp_priv, u32 event,
						u32 data, u32 delay)
{
	unsigned long flag;
	struct msm_dp_event *todo;
	int pndx;

	spin_lock_irqsave(&msm_dp_priv->event_lock, flag);
	pndx = msm_dp_priv->event_pndx + 1;
	pndx %= DP_EVENT_Q_MAX;
	if (pndx == msm_dp_priv->event_gndx) {
		pr_err("event_q is full: pndx=%d gndx=%d\n",
			msm_dp_priv->event_pndx, msm_dp_priv->event_gndx);
		spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);
		return -EPERM;
	}
	todo = &msm_dp_priv->event_list[msm_dp_priv->event_pndx++];
	msm_dp_priv->event_pndx %= DP_EVENT_Q_MAX;
	todo->event_id = event;
	todo->data = data;
	todo->delay = delay;
	wake_up(&msm_dp_priv->event_q);
	spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);

	return 0;
}

static int msm_dp_del_event(struct msm_dp_display_private *msm_dp_priv, u32 event)
{
	unsigned long flag;
	struct msm_dp_event *todo;
	u32	gndx;

	spin_lock_irqsave(&msm_dp_priv->event_lock, flag);
	if (msm_dp_priv->event_pndx == msm_dp_priv->event_gndx) {
		spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);
		return -ENOENT;
	}

	gndx = msm_dp_priv->event_gndx;
	while (msm_dp_priv->event_pndx != gndx) {
		todo = &msm_dp_priv->event_list[gndx];
		if (todo->event_id == event) {
			todo->event_id = EV_NO_EVENT;	/* deleted */
			todo->delay = 0;
		}
		gndx++;
		gndx %= DP_EVENT_Q_MAX;
	}
	spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);

	return 0;
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

static int msm_dp_hpd_event_thread_start(struct msm_dp_display_private *msm_dp_priv);

static int msm_dp_display_bind(struct device *dev, struct device *master,
			   void *data)
{
	int rc = 0;
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);
	struct msm_drm_private *priv = dev_get_drvdata(master);
	struct drm_device *drm = priv->dev;

	dp->msm_dp_display.drm_dev = drm;
	priv->dp[dp->id] = &dp->msm_dp_display;



	dp->drm_dev = drm;
	dp->aux->drm_dev = drm;
	rc = msm_dp_aux_register(dp->aux);
	if (rc) {
		DRM_ERROR("DRM DP AUX register failed\n");
		goto end;
	}


	rc = msm_dp_register_audio_driver(dev, dp->audio);
	if (rc) {
		DRM_ERROR("Audio registration Dp failed\n");
		goto end;
	}

	rc = msm_dp_hpd_event_thread_start(dp);
	if (rc) {
		DRM_ERROR("Event thread create failed\n");
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

	kthread_stop(dp->ev_tsk);

	of_dp_aux_depopulate_bus(dp->aux);

	msm_dp_unregister_audio_driver(dev, dp->audio);
	msm_dp_aux_unregister(dp->aux);
	dp->drm_dev = NULL;
	dp->aux->drm_dev = NULL;
	priv->dp[dp->id] = NULL;
}

static const struct component_ops msm_dp_display_comp_ops = {
	.bind = msm_dp_display_bind,
	.unbind = msm_dp_display_unbind,
};

static void msm_dp_display_send_hpd_event(struct msm_dp *msm_dp_display)
{
	struct msm_dp_display_private *dp;
	struct drm_connector *connector;

	dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	connector = dp->msm_dp_display.connector;
	drm_helper_hpd_irq_event(connector->dev);
}

static int msm_dp_display_send_hpd_notification(struct msm_dp_display_private *dp,
					    bool hpd)
{
	if ((hpd && dp->msm_dp_display.link_ready) ||
			(!hpd && !dp->msm_dp_display.link_ready)) {
		drm_dbg_dp(dp->drm_dev, "HPD already %s\n",
				(hpd ? "on" : "off"));
		return 0;
	}

	/* reset video pattern flag on disconnect */
	if (!hpd) {
		dp->panel->video_test = false;
		if (!dp->msm_dp_display.is_edp)
			drm_dp_set_subconnector_property(dp->msm_dp_display.connector,
							 connector_status_disconnected,
							 dp->panel->dpcd,
							 dp->panel->downstream_ports);
	}

	dp->msm_dp_display.link_ready = hpd;

	drm_dbg_dp(dp->drm_dev, "type=%d hpd=%d\n",
			dp->msm_dp_display.connector_type, hpd);
	msm_dp_display_send_hpd_event(&dp->msm_dp_display);

	return 0;
}

static int msm_dp_display_process_hpd_high(struct msm_dp_display_private *dp)
{
	struct drm_connector *connector = dp->msm_dp_display.connector;
	const struct drm_display_info *info = &connector->display_info;
	int rc = 0;

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
	rc = msm_dp_ctrl_on_link(dp->ctrl);
	if (rc) {
		DRM_ERROR("failed to complete DP link training\n");
		goto end;
	}

	msm_dp_add_event(dp, EV_USER_NOTIFICATION, true, 0);

end:
	return rc;
}

static void msm_dp_display_host_phy_init(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	if (!dp->phy_initialized) {
		msm_dp_ctrl_phy_init(dp->ctrl);
		dp->phy_initialized = true;
	}
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
	msm_dp_ctrl_reset_irq_ctrl(dp->ctrl, true);
	msm_dp_aux_init(dp->aux);
	dp->core_initialized = true;
}

static void msm_dp_display_host_deinit(struct msm_dp_display_private *dp)
{
	drm_dbg_dp(dp->drm_dev, "type=%d core_init=%d phy_init=%d\n",
		dp->msm_dp_display.connector_type, dp->core_initialized,
		dp->phy_initialized);

	msm_dp_ctrl_reset_irq_ctrl(dp->ctrl, false);
	msm_dp_aux_deinit(dp->aux);
	msm_dp_ctrl_core_clk_disable(dp->ctrl);
	dp->core_initialized = false;
}

static int msm_dp_display_usbpd_configure_cb(struct device *dev)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);

	msm_dp_display_host_phy_init(dp);

	return msm_dp_display_process_hpd_high(dp);
}

static int msm_dp_display_notify_disconnect(struct device *dev)
{
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);

	msm_dp_add_event(dp, EV_USER_NOTIFICATION, false, 0);

	return 0;
}

static void msm_dp_display_handle_video_request(struct msm_dp_display_private *dp)
{
	if (dp->link->sink_request & DP_TEST_LINK_VIDEO_PATTERN) {
		dp->panel->video_test = true;
		msm_dp_link_send_test_response(dp->link);
	}
}

static int msm_dp_display_handle_port_status_changed(struct msm_dp_display_private *dp)
{
	int rc = 0;

	if (drm_dp_is_branch(dp->panel->dpcd) && dp->link->sink_count == 0) {
		drm_dbg_dp(dp->drm_dev, "sink count is zero, nothing to do\n");
		if (dp->hpd_state != ST_DISCONNECTED) {
			dp->hpd_state = ST_DISCONNECT_PENDING;
			msm_dp_add_event(dp, EV_USER_NOTIFICATION, false, 0);
		}
	} else {
		if (dp->hpd_state == ST_DISCONNECTED) {
			dp->hpd_state = ST_MAINLINK_READY;
			rc = msm_dp_display_process_hpd_high(dp);
			if (rc)
				dp->hpd_state = ST_DISCONNECTED;
		}
	}

	return rc;
}

static int msm_dp_display_handle_irq_hpd(struct msm_dp_display_private *dp)
{
	u32 sink_request = dp->link->sink_request;

	drm_dbg_dp(dp->drm_dev, "%d\n", sink_request);
	if (dp->hpd_state == ST_DISCONNECTED) {
		if (sink_request & DP_LINK_STATUS_UPDATED) {
			drm_dbg_dp(dp->drm_dev, "Disconnected sink_request: %d\n",
							sink_request);
			DRM_ERROR("Disconnected, no DP_LINK_STATUS_UPDATED\n");
			return -EINVAL;
		}
	}

	msm_dp_ctrl_handle_sink_request(dp->ctrl);

	if (sink_request & DP_TEST_LINK_VIDEO_PATTERN)
		msm_dp_display_handle_video_request(dp);

	return 0;
}

static int msm_dp_display_usbpd_attention_cb(struct device *dev)
{
	int rc = 0;
	u32 sink_request;
	struct msm_dp_display_private *dp = dev_get_dp_display_private(dev);

	/* check for any test request issued by sink */
	rc = msm_dp_link_process_request(dp->link);
	if (!rc) {
		sink_request = dp->link->sink_request;
		drm_dbg_dp(dp->drm_dev, "hpd_state=%d sink_request=%d\n",
					dp->hpd_state, sink_request);
		if (sink_request & DS_PORT_STATUS_CHANGED)
			rc = msm_dp_display_handle_port_status_changed(dp);
		else
			rc = msm_dp_display_handle_irq_hpd(dp);
	}

	return rc;
}

static int msm_dp_hpd_plug_handle(struct msm_dp_display_private *dp, u32 data)
{
	u32 state;
	int ret;
	struct platform_device *pdev = dp->msm_dp_display.pdev;

	msm_dp_aux_enable_xfers(dp->aux, true);

	mutex_lock(&dp->event_mutex);

	state =  dp->hpd_state;
	drm_dbg_dp(dp->drm_dev, "Before, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);

	if (state == ST_DISPLAY_OFF) {
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	if (state == ST_MAINLINK_READY || state == ST_CONNECTED) {
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	if (state == ST_DISCONNECT_PENDING) {
		/* wait until ST_DISCONNECTED */
		msm_dp_add_event(dp, EV_HPD_PLUG_INT, 0, 1); /* delay = 1 */
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		mutex_unlock(&dp->event_mutex);
		return ret;
	}

	ret = msm_dp_display_usbpd_configure_cb(&pdev->dev);
	if (ret) {	/* link train failed */
		dp->hpd_state = ST_DISCONNECTED;
		pm_runtime_put_sync(&pdev->dev);
	} else {
		dp->hpd_state = ST_MAINLINK_READY;
	}

	drm_dbg_dp(dp->drm_dev, "After, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);
	mutex_unlock(&dp->event_mutex);

	/* uevent will complete connection part */
	return 0;
};

static void msm_dp_display_handle_plugged_change(struct msm_dp *msm_dp_display,
		bool plugged)
{
	struct msm_dp_display_private *dp;

	dp = container_of(msm_dp_display,
			struct msm_dp_display_private, msm_dp_display);

	/* notify audio subsystem only if sink supports audio */
	if (msm_dp_display->plugged_cb && msm_dp_display->codec_dev &&
			dp->audio_supported)
		msm_dp_display->plugged_cb(msm_dp_display->codec_dev, plugged);
}

static int msm_dp_hpd_unplug_handle(struct msm_dp_display_private *dp, u32 data)
{
	u32 state;
	struct platform_device *pdev = dp->msm_dp_display.pdev;

	msm_dp_aux_enable_xfers(dp->aux, false);

	mutex_lock(&dp->event_mutex);

	state = dp->hpd_state;

	drm_dbg_dp(dp->drm_dev, "Before, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);

	/* unplugged, no more irq_hpd handle */
	msm_dp_del_event(dp, EV_IRQ_HPD_INT);

	if (state == ST_DISCONNECTED) {
		/* triggered by irq_hdp with sink_count = 0 */
		if (dp->link->sink_count == 0) {
			msm_dp_display_host_phy_exit(dp);
		}
		msm_dp_display_notify_disconnect(&dp->msm_dp_display.pdev->dev);
		mutex_unlock(&dp->event_mutex);
		return 0;
	} else if (state == ST_DISCONNECT_PENDING) {
		mutex_unlock(&dp->event_mutex);
		return 0;
	} else if (state == ST_MAINLINK_READY) {
		msm_dp_ctrl_off_link(dp->ctrl);
		msm_dp_display_host_phy_exit(dp);
		dp->hpd_state = ST_DISCONNECTED;
		msm_dp_display_notify_disconnect(&dp->msm_dp_display.pdev->dev);
		pm_runtime_put_sync(&pdev->dev);
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	/*
	 * We don't need separate work for disconnect as
	 * connect/attention interrupts are disabled
	 */
	msm_dp_display_notify_disconnect(&dp->msm_dp_display.pdev->dev);

	if (state == ST_DISPLAY_OFF) {
		dp->hpd_state = ST_DISCONNECTED;
	} else {
		dp->hpd_state = ST_DISCONNECT_PENDING;
	}

	/* signal the disconnect event early to ensure proper teardown */
	msm_dp_display_handle_plugged_change(&dp->msm_dp_display, false);

	drm_dbg_dp(dp->drm_dev, "After, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);

	/* uevent will complete disconnection part */
	pm_runtime_put_sync(&pdev->dev);
	mutex_unlock(&dp->event_mutex);
	return 0;
}

static int msm_dp_irq_hpd_handle(struct msm_dp_display_private *dp, u32 data)
{
	u32 state;

	mutex_lock(&dp->event_mutex);

	/* irq_hpd can happen at either connected or disconnected state */
	state =  dp->hpd_state;
	drm_dbg_dp(dp->drm_dev, "Before, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);

	if (state == ST_DISPLAY_OFF) {
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	if (state == ST_MAINLINK_READY || state == ST_DISCONNECT_PENDING) {
		/* wait until ST_CONNECTED */
		msm_dp_add_event(dp, EV_IRQ_HPD_INT, 0, 1); /* delay = 1 */
		mutex_unlock(&dp->event_mutex);
		return 0;
	}

	msm_dp_display_usbpd_attention_cb(&dp->msm_dp_display.pdev->dev);

	drm_dbg_dp(dp->drm_dev, "After, type=%d hpd_state=%d\n",
			dp->msm_dp_display.connector_type, state);

	mutex_unlock(&dp->event_mutex);

	return 0;
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
		dp->catalog = NULL;
		goto error;
	}

	dp->catalog = msm_dp_catalog_get(dev);
	if (IS_ERR(dp->catalog)) {
		rc = PTR_ERR(dp->catalog);
		DRM_ERROR("failed to initialize catalog, rc = %d\n", rc);
		dp->catalog = NULL;
		goto error;
	}

	dp->aux = msm_dp_aux_get(dev, dp->catalog,
			     phy,
			     dp->msm_dp_display.is_edp);
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

	dp->panel = msm_dp_panel_get(dev, dp->aux, dp->link, dp->catalog);
	if (IS_ERR(dp->panel)) {
		rc = PTR_ERR(dp->panel);
		DRM_ERROR("failed to initialize panel, rc = %d\n", rc);
		dp->panel = NULL;
		goto error_link;
	}

	dp->ctrl = msm_dp_ctrl_get(dev, dp->link, dp->panel, dp->aux,
			       dp->catalog,
			       phy);
	if (IS_ERR(dp->ctrl)) {
		rc = PTR_ERR(dp->ctrl);
		DRM_ERROR("failed to initialize ctrl, rc = %d\n", rc);
		dp->ctrl = NULL;
		goto error_ctrl;
	}

	dp->audio = msm_dp_audio_get(dp->msm_dp_display.pdev, dp->catalog);
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

int msm_dp_display_set_plugged_cb(struct msm_dp *msm_dp_display,
		hdmi_codec_plugged_cb fn, struct device *codec_dev)
{
	bool plugged;

	msm_dp_display->plugged_cb = fn;
	msm_dp_display->codec_dev = codec_dev;
	plugged = msm_dp_display->link_ready;
	msm_dp_display_handle_plugged_change(msm_dp_display, plugged);

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
	mutex_lock(&msm_dp_display->event_mutex);

	if (!dp->power_on) {
		mutex_unlock(&msm_dp_display->event_mutex);
		return;
	}

	msm_dp_catalog_snapshot(msm_dp_display->catalog, disp_state);

	mutex_unlock(&msm_dp_display->event_mutex);
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

static int hpd_event_thread(void *data)
{
	struct msm_dp_display_private *msm_dp_priv;
	unsigned long flag;
	struct msm_dp_event *todo;
	int timeout_mode = 0;

	msm_dp_priv = (struct msm_dp_display_private *)data;

	while (1) {
		if (timeout_mode) {
			wait_event_timeout(msm_dp_priv->event_q,
				(msm_dp_priv->event_pndx == msm_dp_priv->event_gndx) ||
					kthread_should_stop(), EVENT_TIMEOUT);
		} else {
			wait_event_interruptible(msm_dp_priv->event_q,
				(msm_dp_priv->event_pndx != msm_dp_priv->event_gndx) ||
					kthread_should_stop());
		}

		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&msm_dp_priv->event_lock, flag);
		todo = &msm_dp_priv->event_list[msm_dp_priv->event_gndx];
		if (todo->delay) {
			struct msm_dp_event *todo_next;

			msm_dp_priv->event_gndx++;
			msm_dp_priv->event_gndx %= DP_EVENT_Q_MAX;

			/* re enter delay event into q */
			todo_next = &msm_dp_priv->event_list[msm_dp_priv->event_pndx++];
			msm_dp_priv->event_pndx %= DP_EVENT_Q_MAX;
			todo_next->event_id = todo->event_id;
			todo_next->data = todo->data;
			todo_next->delay = todo->delay - 1;

			/* clean up older event */
			todo->event_id = EV_NO_EVENT;
			todo->delay = 0;

			/* switch to timeout mode */
			timeout_mode = 1;
			spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);
			continue;
		}

		/* timeout with no events in q */
		if (msm_dp_priv->event_pndx == msm_dp_priv->event_gndx) {
			spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);
			continue;
		}

		msm_dp_priv->event_gndx++;
		msm_dp_priv->event_gndx %= DP_EVENT_Q_MAX;
		timeout_mode = 0;
		spin_unlock_irqrestore(&msm_dp_priv->event_lock, flag);

		switch (todo->event_id) {
		case EV_HPD_PLUG_INT:
			msm_dp_hpd_plug_handle(msm_dp_priv, todo->data);
			break;
		case EV_HPD_UNPLUG_INT:
			msm_dp_hpd_unplug_handle(msm_dp_priv, todo->data);
			break;
		case EV_IRQ_HPD_INT:
			msm_dp_irq_hpd_handle(msm_dp_priv, todo->data);
			break;
		case EV_USER_NOTIFICATION:
			msm_dp_display_send_hpd_notification(msm_dp_priv,
						todo->data);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int msm_dp_hpd_event_thread_start(struct msm_dp_display_private *msm_dp_priv)
{
	/* set event q to empty */
	msm_dp_priv->event_gndx = 0;
	msm_dp_priv->event_pndx = 0;

	msm_dp_priv->ev_tsk = kthread_run(hpd_event_thread, msm_dp_priv, "dp_hpd_handler");
	if (IS_ERR(msm_dp_priv->ev_tsk))
		return PTR_ERR(msm_dp_priv->ev_tsk);

	return 0;
}

static irqreturn_t msm_dp_display_irq_handler(int irq, void *dev_id)
{
	struct msm_dp_display_private *dp = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 hpd_isr_status;

	if (!dp) {
		DRM_ERROR("invalid data\n");
		return IRQ_NONE;
	}

	hpd_isr_status = msm_dp_catalog_hpd_get_intr_status(dp->catalog);

	if (hpd_isr_status & 0x0F) {
		drm_dbg_dp(dp->drm_dev, "type=%d isr=0x%x\n",
			dp->msm_dp_display.connector_type, hpd_isr_status);
		/* hpd related interrupts */
		if (hpd_isr_status & DP_DP_HPD_PLUG_INT_MASK)
			msm_dp_add_event(dp, EV_HPD_PLUG_INT, 0, 0);

		if (hpd_isr_status & DP_DP_IRQ_HPD_INT_MASK) {
			msm_dp_add_event(dp, EV_IRQ_HPD_INT, 0, 0);
		}

		if (hpd_isr_status & DP_DP_HPD_REPLUG_INT_MASK) {
			msm_dp_add_event(dp, EV_HPD_UNPLUG_INT, 0, 0);
			msm_dp_add_event(dp, EV_HPD_PLUG_INT, 0, 3);
		}

		if (hpd_isr_status & DP_DP_HPD_UNPLUG_INT_MASK)
			msm_dp_add_event(dp, EV_HPD_UNPLUG_INT, 0, 0);

		ret = IRQ_HANDLED;
	}

	/* DP controller isr */
	ret |= msm_dp_ctrl_isr(dp->ctrl);

	/* DP aux isr */
	ret |= msm_dp_aux_isr(dp->aux);

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

	rc = devm_request_irq(&pdev->dev, dp->irq, msm_dp_display_irq_handler,
			      IRQF_TRIGGER_HIGH|IRQF_NO_AUTOEN,
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

	rc = msm_dp_init_sub_modules(dp);
	if (rc) {
		DRM_ERROR("init sub module failed\n");
		return -EPROBE_DEFER;
	}

	/* setup event q */
	mutex_init(&dp->event_mutex);
	init_waitqueue_head(&dp->event_q);
	spin_lock_init(&dp->event_lock);

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
		msm_dp_catalog_ctrl_hpd_disable(dp->catalog);
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
		msm_dp_catalog_ctrl_hpd_enable(dp->catalog);
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
			     struct drm_bridge_state *old_bridge_state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	int rc = 0;
	struct msm_dp_display_private *msm_dp_display;
	u32 state;
	bool force_link_train = false;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);
	if (!msm_dp_display->msm_dp_mode.drm_mode.clock) {
		DRM_ERROR("invalid params\n");
		return;
	}

	if (dp->is_edp)
		msm_dp_hpd_plug_handle(msm_dp_display, 0);

	mutex_lock(&msm_dp_display->event_mutex);
	if (pm_runtime_resume_and_get(&dp->pdev->dev)) {
		DRM_ERROR("failed to pm_runtime_resume\n");
		mutex_unlock(&msm_dp_display->event_mutex);
		return;
	}

	state = msm_dp_display->hpd_state;
	if (state != ST_DISPLAY_OFF && state != ST_MAINLINK_READY) {
		mutex_unlock(&msm_dp_display->event_mutex);
		return;
	}

	rc = msm_dp_display_set_mode(dp, &msm_dp_display->msm_dp_mode);
	if (rc) {
		DRM_ERROR("Failed to perform a mode set, rc=%d\n", rc);
		mutex_unlock(&msm_dp_display->event_mutex);
		return;
	}

	state =  msm_dp_display->hpd_state;

	if (state == ST_DISPLAY_OFF) {
		msm_dp_display_host_phy_init(msm_dp_display);
		force_link_train = true;
	}

	msm_dp_display_enable(msm_dp_display, force_link_train);

	rc = msm_dp_display_post_enable(dp);
	if (rc) {
		DRM_ERROR("DP display post enable failed, rc=%d\n", rc);
		msm_dp_display_disable(msm_dp_display);
	}

	/* completed connection */
	msm_dp_display->hpd_state = ST_CONNECTED;

	drm_dbg_dp(dp->drm_dev, "type=%d Done\n", dp->connector_type);
	mutex_unlock(&msm_dp_display->event_mutex);
}

void msm_dp_bridge_atomic_disable(struct drm_bridge *drm_bridge,
			      struct drm_bridge_state *old_bridge_state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	msm_dp_ctrl_push_idle(msm_dp_display->ctrl);
}

void msm_dp_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
				   struct drm_bridge_state *old_bridge_state)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(drm_bridge);
	struct msm_dp *dp = msm_dp_bridge->msm_dp_display;
	u32 state;
	struct msm_dp_display_private *msm_dp_display;

	msm_dp_display = container_of(dp, struct msm_dp_display_private, msm_dp_display);

	if (dp->is_edp)
		msm_dp_hpd_unplug_handle(msm_dp_display, 0);

	mutex_lock(&msm_dp_display->event_mutex);

	state = msm_dp_display->hpd_state;
	if (state != ST_DISCONNECT_PENDING && state != ST_CONNECTED)
		drm_dbg_dp(dp->drm_dev, "type=%d wrong hpd_state=%d\n",
			   dp->connector_type, state);

	msm_dp_display_disable(msm_dp_display);

	state =  msm_dp_display->hpd_state;
	if (state == ST_DISCONNECT_PENDING) {
		/* completed disconnection */
		msm_dp_display->hpd_state = ST_DISCONNECTED;
	} else {
		msm_dp_display->hpd_state = ST_DISPLAY_OFF;
	}

	drm_dbg_dp(dp->drm_dev, "type=%d Done\n", dp->connector_type);

	pm_runtime_put_sync(&dp->pdev->dev);
	mutex_unlock(&msm_dp_display->event_mutex);
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
	msm_dp_display->catalog->wide_bus_en =
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
	mutex_lock(&dp->event_mutex);
	if (pm_runtime_resume_and_get(&msm_dp_display->pdev->dev)) {
		DRM_ERROR("failed to resume power\n");
		mutex_unlock(&dp->event_mutex);
		return;
	}

	msm_dp_catalog_ctrl_hpd_enable(dp->catalog);

	/* enable HDP interrupts */
	msm_dp_catalog_hpd_config_intr(dp->catalog, DP_DP_HPD_INT_MASK, true);

	msm_dp_display->internal_hpd = true;
	mutex_unlock(&dp->event_mutex);
}

void msm_dp_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *msm_dp_display = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	mutex_lock(&dp->event_mutex);
	/* disable HDP interrupts */
	msm_dp_catalog_hpd_config_intr(dp->catalog, DP_DP_HPD_INT_MASK, false);
	msm_dp_catalog_ctrl_hpd_disable(dp->catalog);

	msm_dp_display->internal_hpd = false;

	pm_runtime_put_sync(&msm_dp_display->pdev->dev);
	mutex_unlock(&dp->event_mutex);
}

void msm_dp_bridge_hpd_notify(struct drm_bridge *bridge,
			  enum drm_connector_status status)
{
	struct msm_dp_bridge *msm_dp_bridge = to_dp_bridge(bridge);
	struct msm_dp *msm_dp_display = msm_dp_bridge->msm_dp_display;
	struct msm_dp_display_private *dp = container_of(msm_dp_display, struct msm_dp_display_private, msm_dp_display);

	/* Without next_bridge interrupts are handled by the DP core directly */
	if (msm_dp_display->internal_hpd)
		return;

	if (!msm_dp_display->link_ready && status == connector_status_connected)
		msm_dp_add_event(dp, EV_HPD_PLUG_INT, 0, 0);
	else if (msm_dp_display->link_ready && status == connector_status_disconnected)
		msm_dp_add_event(dp, EV_HPD_UNPLUG_INT, 0, 0);
}
