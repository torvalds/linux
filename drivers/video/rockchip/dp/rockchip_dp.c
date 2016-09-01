#include "rockchip_dp.h"
#include <linux/delay.h>

static int rockchip_dp_removed(struct hdmi *hdmi_drv)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	cdn_dp_encoder_disable(dp_dev->dp);
	return HDMI_ERROR_SUCCESS;
}

static int rockchip_dp_enable(struct hdmi *hdmi_drv)
{
	hdmi_submit_work(hdmi_drv, HDMI_HPD_CHANGE, 10, 0);
	return 0;
}

static int rockchip_dp_disable(struct hdmi *hdmi_drv)
{
	return 0;
}

static int rockchip_dp_control_output(struct hdmi *hdmi_drv, int enable)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	if (enable == HDMI_AV_UNMUTE)
		cdn_dp_encoder_enable(dp_dev->dp);
	else if (enable & HDMI_VIDEO_MUTE)
		cdn_dp_encoder_disable(dp_dev->dp);

	return 0;
}

static int rockchip_dp_config_audio(struct hdmi *hdmi_drv,
				    struct hdmi_audio *audio)
{
	return 0;
}

static int rockchip_dp_config_video(struct hdmi *hdmi_drv,
				    struct hdmi_video *vpara)
{
	struct hdmi_video_timing *timing = NULL;
	struct dp_dev *dp_dev = hdmi_drv->property->priv;
	struct dp_disp_info *disp_info = &dp_dev->disp_info;

	timing = (struct hdmi_video_timing *)hdmi_vic2timing(vpara->vic);
	if (!timing) {
		dev_err(hdmi_drv->dev,
			"[%s] not found vic %d\n", __func__, vpara->vic);
		return -ENOENT;
	}
	disp_info->mode = &timing->mode;

	disp_info->color_depth = vpara->color_output_depth;
	disp_info->vsync_polarity = 1;
	disp_info->hsync_polarity = 1;

	cdn_dp_encoder_mode_set(dp_dev->dp, disp_info);
	return 0;
}

static int rockchip_dp_detect_hotplug(struct hdmi *hdmi_drv)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	if (cdn_dp_connector_detect(dp_dev->dp))
		return HDMI_HPD_ACTIVED;
	return HDMI_HPD_REMOVED;
}

static int rockchip_dp_read_edid(struct hdmi *hdmi_drv, int block, u8 *buf)
{
	int ret;
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	if (dp_dev->lanes == 4)
		dp_dev->hdmi->property->feature |= SUPPORT_TMDS_600M;
	else
		dp_dev->hdmi->property->feature &= ~SUPPORT_TMDS_600M;

	ret = cdn_dp_get_edid(dp_dev->dp, buf, block);
	if (ret)
		return ret;
	return 0;
}

static int rockchip_dp_insert(struct hdmi *hdmi_drv)
{
	return 0;
}

static int rockchip_dp_config_vsi(struct hdmi *hdmi,
				  unsigned char vic_3d,
				  unsigned char format)
{
	return 0;
}

static void rockchip_dp_dev_init_ops(struct hdmi_ops *ops)
{
	if (ops) {
		ops->disable = rockchip_dp_disable;
		ops->enable = rockchip_dp_enable;
		ops->remove = rockchip_dp_removed;
		ops->setmute = rockchip_dp_control_output;
		ops->setvideo = rockchip_dp_config_video;
		ops->setaudio = rockchip_dp_config_audio;
		ops->getstatus = rockchip_dp_detect_hotplug;
		ops->getedid = rockchip_dp_read_edid;
		ops->insert     = rockchip_dp_insert;
		ops->setvsi = rockchip_dp_config_vsi;
	}
}

void hpd_change(struct device *dev, int lanes)
{
	struct dp_dev *dp_dev = dev_get_drvdata(dev);

	if (lanes)
		dp_dev->lanes = lanes;
	if (dp_dev->hdmi->enable)
		hdmi_submit_work(dp_dev->hdmi, HDMI_HPD_CHANGE, 20, 0);
}

static void rockchip_dp_early_suspend(struct dp_dev *dp_dev)
{
	hdmi_submit_work(dp_dev->hdmi, HDMI_SUSPEND_CTL, 0, 1);
}

static void rockchip_dp_early_resume(struct dp_dev *dp_dev)
{
	hdmi_submit_work(dp_dev->hdmi, HDMI_RESUME_CTL, 0, 0);
}

static int rockchip_dp_fb_event_notify(struct notifier_block *self,
				       unsigned long action,
					   void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);
	struct dp_dev *dp_dev = container_of(self, struct dp_dev, fb_notif);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			if (!dp_dev->hdmi->sleep)
				rockchip_dp_early_suspend(dp_dev);
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			if (dp_dev->hdmi->sleep)
				rockchip_dp_early_resume(dp_dev);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

int cdn_dp_fb_register(struct platform_device *pdev, void *dp)
{
	struct hdmi_ops *rk_dp_ops;
	struct hdmi_property *rk_cdn_dp_prop;
	struct device *dev = &pdev->dev;
	struct dp_dev *dp_dev;
	struct device_node *np = dev->of_node;
	int val = 0;

	rk_dp_ops = devm_kzalloc(dev, sizeof(struct hdmi_ops), GFP_KERNEL);
	if (!rk_dp_ops)
		return -ENOMEM;

	rk_cdn_dp_prop = devm_kzalloc(dev, sizeof(struct hdmi_property),
				      GFP_KERNEL);
	if (!rk_cdn_dp_prop)
		return -ENOMEM;

	dp_dev = devm_kzalloc(dev, sizeof(struct dp_dev), GFP_KERNEL);
	if (!dp_dev)
		return -ENOMEM;

	if (!of_property_read_u32(np, "dp_vop_sel", &val))
		dp_dev->disp_info.vop_sel = val;

	rockchip_dp_dev_init_ops(rk_dp_ops);
	rk_cdn_dp_prop->videosrc = dp_dev->disp_info.vop_sel;
	rk_cdn_dp_prop->display = DISPLAY_MAIN;
	rk_cdn_dp_prop->defaultmode = HDMI_VIDEO_DEFAULT_MODE;
	rk_cdn_dp_prop->name = (char *)pdev->name;
	rk_cdn_dp_prop->priv = dp_dev;
	rk_cdn_dp_prop->feature |=
				SUPPORT_DEEP_10BIT |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_1080I |
				SUPPORT_480I_576I |
				SUPPORT_4K |
				SUPPORT_4K_4096 |
				SUPPORT_YUV420 |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_TMDS_600M |
				SUPPORT_RK_DISCRETE_VR;

	dp_dev->hdmi = rockchip_hdmi_register(rk_cdn_dp_prop,
						rk_dp_ops);
	dp_dev->hdmi->dev = dev;
	dp_dev->hdmi->enable = 1;
	dp_dev->hdmi->sleep = 0;
	dp_dev->hdmi->colormode = HDMI_COLOR_RGB_0_255;
	dp_dev->dp = dp;

	dp_dev->fb_notif.notifier_call = rockchip_dp_fb_event_notify;
	fb_register_client(&dp_dev->fb_notif);
	dev_set_drvdata(dev, dp_dev);

	hdmi_submit_work(dp_dev->hdmi, HDMI_HPD_CHANGE, 20, 0);
	return 0;
}
