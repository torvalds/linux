#include "rockchip_dp.h"
#include <linux/delay.h>
#include <linux/of_gpio.h>

static int rockchip_dp_removed(struct hdmi *hdmi_drv)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;
	int ret;

	ret = cdn_dp_encoder_disable(dp_dev->dp);
	if (ret)
		dev_warn(hdmi_drv->dev, "dp has been removed twice:%d\n", ret);
	return HDMI_ERROR_SUCCESS;
}

static int rockchip_dp_enable(struct hdmi *hdmi_drv)
{
	return 0;
}

static int rockchip_dp_disable(struct hdmi *hdmi_drv)
{
	return 0;
}

static int rockchip_dp_control_output(struct hdmi *hdmi_drv, int enable)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;
	int ret;

	if (enable == HDMI_AV_UNMUTE) {
		if (!dp_dev->early_suspended) {
			ret = cdn_dp_encoder_enable(dp_dev->dp);
			if (ret) {
				dev_err(hdmi_drv->dev,
					"dp enable video and audio output error:%d\n", ret);
				return HDMI_ERROR_FALSE;
			}
		} else
			dev_warn(hdmi_drv->dev,
				"don't output video and audio after dp has been suspended!\n");
	} else if (enable & HDMI_VIDEO_MUTE)
		dev_dbg(hdmi_drv->dev, "dp disable video and audio output !\n");

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
	int ret;

	if (dp_dev->early_suspended) {
		dev_warn(hdmi_drv->dev,
			"don't config video after dp has been suspended!\n");
		return 0;
	}

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

	ret = cdn_dp_encoder_mode_set(dp_dev->dp, disp_info);
	if (ret) {
		dev_err(hdmi_drv->dev, "dp config video mode error:%d\n", ret);
		return HDMI_ERROR_FALSE;
	}

	return 0;
}

static int rockchip_dp_detect_hotplug(struct hdmi *hdmi_drv)
{
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	if (cdn_dp_connector_detect(dp_dev->dp))
		return HDMI_HPD_ACTIVATED;
	return HDMI_HPD_REMOVED;
}

static int rockchip_dp_read_edid(struct hdmi *hdmi_drv, int block, u8 *buf)
{
	int ret = 0;
	struct dp_dev *dp_dev = hdmi_drv->property->priv;

	if (dp_dev->lanes == 4)
		dp_dev->hdmi->property->feature |= SUPPORT_TMDS_600M;
	else
		dp_dev->hdmi->property->feature &= ~SUPPORT_TMDS_600M;

	ret = cdn_dp_get_edid(dp_dev->dp, buf, block);
	if (ret)
		dev_err(hdmi_drv->dev, "dp config video mode error:%d\n", ret);

	return ret;
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

	if (dp_dev->hdmi->enable) {
		if (dp_dev->early_suspended)
			dev_warn(dp_dev->hdmi->dev,
				"hpd triggered after early suspend, so don't send hpd change event !\n");
		else
			hdmi_submit_work(dp_dev->hdmi, HDMI_HPD_CHANGE, 10, 0);
	}
}

static void rockchip_dp_early_suspend(struct dp_dev *dp_dev)
{
	hdmi_submit_work(dp_dev->hdmi, HDMI_SUSPEND_CTL, 0, 1);
	cdn_dp_fb_suspend(dp_dev->dp);
}

static void rockchip_dp_early_resume(struct dp_dev *dp_dev)
{
	cdn_dp_fb_resume(dp_dev->dp);
	hdmi_submit_work(dp_dev->hdmi, HDMI_RESUME_CTL, 0, 0);
}

static int rockchip_dp_fb_event_notify(struct notifier_block *self,
				       unsigned long action,
					   void *data)
{
	struct fb_event *event = data;
	struct dp_dev *dp_dev = container_of(self, struct dp_dev, fb_notif);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			if (!dp_dev->hdmi->sleep) {
				rockchip_dp_early_suspend(dp_dev);
				dp_dev->early_suspended = true;
			}
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			if (dp_dev->hdmi->sleep) {
				dp_dev->early_suspended = false;
				rockchip_dp_early_resume(dp_dev);
			}
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static int cdn_dp_get_prop_dts(struct hdmi *hdmi, struct device_node *np)
{
	const struct property *prop;
	int i = 0, nstates = 0;
	const __be32 *val;
	int value;
	struct edid_prop_value *pval = NULL;

	if (!hdmi || !np)
		return -EINVAL;

	if (!of_property_read_u32(np, "dp_edid_auto_support", &value))
		hdmi->edid_auto_support = value;

	prop = of_find_property(np, "dp_edid_prop_value", NULL);
	if (!prop || !prop->value) {
		pr_info("%s:No edid-prop-value, %d\n", __func__, !prop);
		return -EINVAL;
	}

	nstates = (prop->length / sizeof(struct edid_prop_value));
	pval = kcalloc(nstates, sizeof(struct edid_prop_value), GFP_NOWAIT);
	if (!pval)
		return -ENOMEM;

	for (i = 0, val = prop->value; i < nstates; i++) {
		pval[i].vid = be32_to_cpup(val++);
		pval[i].pid = be32_to_cpup(val++);
		pval[i].sn = be32_to_cpup(val++);
		pval[i].xres = be32_to_cpup(val++);
		pval[i].yres = be32_to_cpup(val++);
		pval[i].vic = be32_to_cpup(val++);
		pval[i].width = be32_to_cpup(val++);
		pval[i].height = be32_to_cpup(val++);
		pval[i].x_w = be32_to_cpup(val++);
		pval[i].x_h = be32_to_cpup(val++);
		pval[i].hwrotation = be32_to_cpup(val++);
		pval[i].einit = be32_to_cpup(val++);
		pval[i].vsync = be32_to_cpup(val++);
		pval[i].panel = be32_to_cpup(val++);
		pval[i].scan = be32_to_cpup(val++);

		pr_info("%s: 0x%x 0x%x 0x%x %d %d %d %d %d %d %d %d %d %d %d %d\n",
			__func__, pval[i].vid, pval[i].pid, pval[i].sn,
			pval[i].width, pval[i].height, pval[i].xres,
			pval[i].yres, pval[i].vic, pval[i].x_w,
			pval[i].x_h, pval[i].hwrotation, pval[i].einit,
			pval[i].vsync, pval[i].panel, pval[i].scan);
	}

	hdmi->pvalue = pval;
	hdmi->nstates = nstates;

	return 0;
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
	if (!of_property_read_u32(np, "dp_defaultmode", &val))
		rk_cdn_dp_prop->defaultmode = val;
	else
		rk_cdn_dp_prop->defaultmode = HDMI_VIDEO_DEFAULT_MODE;
	rk_cdn_dp_prop->name = (char *)pdev->name;
	rk_cdn_dp_prop->priv = dp_dev;
	rk_cdn_dp_prop->feature |=
				SUPPORT_DEEP_10BIT |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_1080I |
				SUPPORT_480I_576I |
				SUPPORT_RK_DISCRETE_VR;

	if (!rk_cdn_dp_prop->videosrc) {
		rk_cdn_dp_prop->feature |=
					SUPPORT_4K |
					SUPPORT_4K_4096 |
					SUPPORT_YUV420 |
					SUPPORT_YCBCR_INPUT |
					SUPPORT_TMDS_600M;
	}

	dp_dev->hdmi = rockchip_hdmi_register(rk_cdn_dp_prop,
						rk_dp_ops);
	dp_dev->hdmi->dev = dev;
	dp_dev->hdmi->enable = 1;
	dp_dev->early_suspended = 0;
	dp_dev->hdmi->sleep = 0;
	dp_dev->hdmi->colormode = HDMI_COLOR_RGB_0_255;
	dp_dev->dp = dp;

	cdn_dp_get_prop_dts(dp_dev->hdmi, np);
	dp_dev->fb_notif.notifier_call = rockchip_dp_fb_event_notify;
	fb_register_client(&dp_dev->fb_notif);
	dev_set_drvdata(dev, dp_dev);

	hdmi_submit_work(dp_dev->hdmi, HDMI_HPD_CHANGE, 20, 0);
	return 0;
}
