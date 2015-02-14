#include <linux/delay.h>
#include "rockchip-hdmi.h"
#include "rockchip-hdmi-cec.h"

struct hdmi_delayed_work {
	struct delayed_work work;
	struct hdmi *hdmi;
	int event;
	void *data;
};

struct hdmi_id_ref_info {
	struct hdmi *hdmi;
	int id;
	int ref;
} ref_info[HDMI_MAX_ID];

static int uboot_vic;
static void hdmi_work_queue(struct work_struct *work);

struct delayed_work *hdmi_submit_work(struct hdmi *hdmi,
				      int event, int delay, void *data)
{
	struct hdmi_delayed_work *work;

	DBG("%s event %04x delay %d", __func__, event, delay);

	work = kmalloc(sizeof(*work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, hdmi_work_queue);
		work->hdmi = hdmi;
		work->event = event;
		work->data = data;
		queue_delayed_work(hdmi->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		pr_warn("HDMI: Cannot allocate memory to create work\n");
		return 0;
	}

	return &work->work;
}

static void hdmi_send_uevent(struct hdmi *hdmi, int uevent)
{
	char *envp[3];

	envp[0] = "INTERFACE=HDMI";
	envp[1] = kmalloc(32, GFP_KERNEL);
	if (envp[1] == NULL)
		return;
	sprintf(envp[1], "SCREEN=%d", hdmi->ddev->property);
	envp[2] = NULL;
	kobject_uevent_env(&hdmi->ddev->dev->kobj, uevent, envp);
	kfree(envp[1]);
}

static inline void hdmi_wq_set_output(struct hdmi *hdmi, int mute)
{
	DBG("%s mute %d", __func__, mute);
	if (hdmi->ops->setmute)
		hdmi->ops->setmute(hdmi, mute);
}

static inline void hdmi_wq_set_audio(struct hdmi *hdmi)
{
	DBG("%s", __func__);
	if (hdmi->ops->setaudio)
		hdmi->ops->setaudio(hdmi, &hdmi->audio);
}

static void hdmi_wq_set_video(struct hdmi *hdmi)
{
	struct hdmi_video	video;

	DBG("%s", __func__);

	video.vic = hdmi->vic & HDMI_VIC_MASK;
	if (hdmi->vic & HDMI_VIDEO_YUV420)
		video.color_input = HDMI_COLOR_YCBCR420;
	else if (hdmi->edid.sink_hdmi == 0)
		video.color_input = HDMI_COLOR_RGB_0_255;
	else
		video.color_input = HDMI_COLOR_YCBCR444;
	video.sink_hdmi = hdmi->edid.sink_hdmi;
	video.format_3d = hdmi->mode_3d;
	/* For DVI, output RGB */
	if (hdmi->edid.sink_hdmi == 0) {
		video.color_output = HDMI_COLOR_RGB_0_255;
	} else {
		if (hdmi->colormode == HDMI_COLOR_AUTO) {
			if (hdmi->edid.ycbcr444)
				video.color_output = HDMI_COLOR_YCBCR444;
			else if (hdmi->edid.ycbcr422)
				video.color_output = HDMI_COLOR_YCBCR422;
			else
				video.color_output = HDMI_COLOR_RGB_16_235;
		} else {
			video.color_output = hdmi->colormode;
		}
	}
	if ((hdmi->property->feature & SUPPORT_DEEP_10BIT) &&
	    (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_30BITS)) {
		if (hdmi->colordepth == HDMI_DEPP_COLOR_AUTO ||
		    hdmi->colordepth == 10)
			video.color_output_depth = 10;
	} else {
		video.color_output_depth = 8;
	}
	if (hdmi->vic & HDMI_VIDEO_YUV420)
		video.color_output = HDMI_COLOR_YCBCR420;
	pr_info("hdmi output corlor mode is %d\n", video.color_output);
	if (hdmi->ops->setvideo)
		hdmi->ops->setvideo(hdmi, &video);
}

static void hdmi_wq_parse_edid(struct hdmi *hdmi)
{
	struct hdmi_edid *pedid;
	unsigned char *buff = NULL;
	int rc = HDMI_ERROR_SUCESS, extendblock = 0, i, trytimes;

	if (hdmi == NULL)
		return;

	DBG("%s", __func__);

	pedid = &(hdmi->edid);
	fb_destroy_modelist(&pedid->modelist);
	memset(pedid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&pedid->modelist);

	buff = kmalloc(HDMI_EDID_BLOCK_SIZE, GFP_KERNEL);
	if (buff == NULL) {
		dev_err(hdmi->dev,
			"[%s] can not allocate memory for edid buff.\n",
			__func__);
		rc = HDMI_ERROR_FALSE;
		goto out;
	}

	if (hdmi->ops->getedid == NULL) {
		rc = HDMI_ERROR_FALSE;
		goto out;
	}

	/* Read base block edid.*/
	for (trytimes = 0; trytimes < 3; trytimes++) {
		if (trytimes)
			msleep(50);
		memset(buff, 0 , HDMI_EDID_BLOCK_SIZE);
		rc = hdmi->ops->getedid(hdmi, 0, buff);
		if (rc) {
			dev_err(hdmi->dev,
				"[HDMI] read edid base block error\n");
			continue;
		}

		rc = hdmi_edid_parse_base(buff, &extendblock, pedid);
		if (rc) {
			dev_err(hdmi->dev,
				"[HDMI] parse edid base block error\n");
			continue;
		}
		if (!rc)
			break;
	}
	if (rc)
		goto out;

	for (i = 1; i < extendblock + 1; i++) {
		for (trytimes = 0; trytimes < 3; trytimes++) {
			if (trytimes)
				msleep(20);
			memset(buff, 0 , HDMI_EDID_BLOCK_SIZE);
			rc = hdmi->ops->getedid(hdmi, i, buff);
			if (rc) {
				dev_err(hdmi->dev,
					"[HDMI] read edid block %d error\n",
					i);
				continue;
			}

			rc = hdmi_edid_parse_extensions(buff, pedid);
			if (rc) {
				dev_err(hdmi->dev,
					"[HDMI] parse edid block %d error\n",
					i);
				continue;
			}

			if (!rc)
				break;
		}
	}
out:
	kfree(buff);
	rc = hdmi_ouputmode_select(hdmi, rc);
}

static void hdmi_wq_insert(struct hdmi *hdmi)
{
	DBG("%s", __func__);
	if (hdmi->ops->insert)
		hdmi->ops->insert(hdmi);
	hdmi_wq_parse_edid(hdmi);
	if (hdmi->property->feature & SUPPORT_CEC)
		rockchip_hdmi_cec_set_pa(hdmi->edid.cecaddress);
	hdmi_send_uevent(hdmi, KOBJ_ADD);
	if (hdmi->enable) {
		hdmi->autoset = 0;
		hdmi_set_lcdc(hdmi);
		hdmi_wq_set_video(hdmi);
		#ifdef CONFIG_SWITCH
		if ((hdmi->edid.baseaudio_support &&
		     hdmi->edid.sink_hdmi) ||
		    rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
			switch_set_state(&(hdmi->switchdev), 1);
		#endif
		hdmi_wq_set_audio(hdmi);
		hdmi_wq_set_output(hdmi, hdmi->mute);
		if (hdmi->ops->hdcp_cb)
			hdmi->ops->hdcp_cb(hdmi);
		if (hdmi->ops->setcec)
			hdmi->ops->setcec(hdmi);
	}
	if (hdmi->uboot)
		hdmi->uboot = 0;
}

static void hdmi_wq_remove(struct hdmi *hdmi)
{
	struct list_head *pos, *n;
	struct rk_screen screen;

	DBG("%s", __func__);
	if (hdmi->ops->remove)
		hdmi->ops->remove(hdmi);
	#ifdef CONFIG_SWITCH
	if ((hdmi->edid.baseaudio_support &&
	     hdmi->edid.sink_hdmi) ||
	     rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
		switch_set_state(&(hdmi->switchdev), 0);
	#endif
	list_for_each_safe(pos, n, &hdmi->edid.modelist) {
		list_del(pos);
		kfree(pos);
	}

	kfree(hdmi->edid.audio);

	if (hdmi->edid.specs) {
		kfree(hdmi->edid.specs->modedb);
		kfree(hdmi->edid.specs);
	}
	memset(&hdmi->edid, 0, sizeof(struct hdmi_edid));
	hdmi_init_modelist(hdmi);
	hdmi->mute	= HDMI_AV_UNMUTE;
	hdmi->mode_3d = HDMI_3D_NONE;
	hdmi->uboot = 0;
	if (hdmi->hotplug == HDMI_HPD_ACTIVED) {
		screen.type = SCREEN_HDMI;
		rk_fb_switch_screen(&screen, 0, hdmi->lcdc->id);
	}
	hdmi->hotplug = HDMI_HPD_REMOVED;
	hdmi_send_uevent(hdmi, KOBJ_REMOVE);
}

static void hdmi_work_queue(struct work_struct *work)
{
	struct hdmi_delayed_work *hdmi_w =
		container_of(work, struct hdmi_delayed_work, work.work);
	struct hdmi *hdmi = hdmi_w->hdmi;
	int event = hdmi_w->event;
	int hpd = HDMI_HPD_REMOVED;

	mutex_lock(&hdmi->lock);

	DBG("\nhdmi_work_queue() - evt= %x %d\n",
	    (event & 0xFF00) >> 8,
	    event & 0xFF);

	switch (event) {
	case HDMI_ENABLE_CTL:
		if (!hdmi->enable) {
			hdmi->enable = 1;
			if (!hdmi->sleep) {
				if (hdmi->ops->enable)
					hdmi->ops->enable(hdmi);
				if (hdmi->hotplug == HDMI_HPD_ACTIVED)
					hdmi_wq_insert(hdmi);
			}
		}
		break;
	case HDMI_RESUME_CTL:
		if (hdmi->sleep) {
			if (hdmi->ops->enable)
				hdmi->ops->enable(hdmi);
			hdmi->sleep = 0;
		}
		break;
	case HDMI_DISABLE_CTL:
		if (hdmi->enable) {
			if (!hdmi->sleep) {
				/*
				if (hdmi->ops->disable)
					hdmi->ops->disable(hdmi);
				*/
				hdmi_wq_remove(hdmi);
			}
			hdmi->enable = 0;
		}
		break;
	case HDMI_SUSPEND_CTL:
		if (!hdmi->sleep) {
			hdmi_wq_set_output(hdmi,
					   HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE);
			if (hdmi->ops->disable)
				hdmi->ops->disable(hdmi);
			if (hdmi->enable)
				hdmi_wq_remove(hdmi);
			hdmi->sleep = 1;
		}
		break;
	case HDMI_HPD_CHANGE:
		if (hdmi->ops->getstatus)
			hpd = hdmi->ops->getstatus(hdmi);
		DBG("hdmi_work_queue() - hpd is %d hotplug is %d",
		    hpd, hdmi->hotplug);
		if (hpd != hdmi->hotplug) {
			if (hpd == HDMI_HPD_ACTIVED) {
				hdmi->hotplug = hpd;
				hdmi_wq_insert(hdmi);
			} else if (hdmi->hotplug == HDMI_HPD_ACTIVED) {
				hdmi_wq_remove(hdmi);
			}
			hdmi->hotplug = hpd;
		}
		break;
	case HDMI_SET_VIDEO:
		if (hdmi->enable && !hdmi->sleep) {
			hdmi_wq_set_output(hdmi,
					   HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE);
			if (rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
				msleep(2000);
			else
				msleep(1000);
			hdmi_set_lcdc(hdmi);
			hdmi_send_uevent(hdmi, KOBJ_CHANGE);
			hdmi_wq_set_video(hdmi);
			hdmi_wq_set_audio(hdmi);
			hdmi_wq_set_output(hdmi, hdmi->mute);
			if (hdmi->ops->hdcp_cb)
				hdmi->ops->hdcp_cb(hdmi);
		}
		break;
	case HDMI_SET_AUDIO:
		if ((hdmi->mute & HDMI_AUDIO_MUTE) == 0 &&
		    hdmi->enable && !hdmi->sleep) {
			hdmi_wq_set_output(hdmi, HDMI_AUDIO_MUTE);
			hdmi_wq_set_audio(hdmi);
			hdmi_wq_set_output(hdmi, hdmi->mute);
		}
		break;
	case HDMI_MUTE_AUDIO:
	case HDMI_UNMUTE_AUDIO:
		if (hdmi->mute & HDMI_AUDIO_MUTE ||
		    !hdmi->enable || hdmi->sleep ||
		    hdmi->hotplug != HDMI_HPD_ACTIVED)
			break;
		if (event == HDMI_MUTE_AUDIO)
			hdmi_wq_set_output(hdmi, hdmi->mute |
					   HDMI_AUDIO_MUTE);
		else
			hdmi_wq_set_output(hdmi,
					   hdmi->mute & (~HDMI_AUDIO_MUTE));
		break;
	case HDMI_SET_3D:
		if (hdmi->ops->setvsi) {
			if (hdmi->mode_3d != HDMI_3D_NONE)
				hdmi->ops->setvsi(hdmi, hdmi->mode_3d,
						  HDMI_VIDEO_FORMAT_3D);
			else if ((hdmi->vic & HDMI_TYPE_MASK) == 0)
				hdmi->ops->setvsi(hdmi, hdmi->vic,
						  HDMI_VIDEO_FORMAT_NORMAL);
		}
		break;
	case HDMI_SET_COLOR:
		hdmi_wq_set_output(hdmi,
				   HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE);
		msleep(100);
		hdmi_wq_set_video(hdmi);
		hdmi_wq_set_output(hdmi, hdmi->mute);
		break;
	case HDMI_ENABLE_HDCP:
		if (hdmi->hotplug == HDMI_HPD_ACTIVED && hdmi->ops->hdcp_cb)
			hdmi->ops->hdcp_cb(hdmi);
		break;
	default:
		pr_err("HDMI: hdmi_work_queue() unkown event\n");
		break;
	}

	kfree(hdmi_w->data);
	kfree(hdmi_w);

	DBG("\nhdmi_work_queue() - exit evt= %x %d\n",
	    (event & 0xFF00) >> 8,
	    event & 0xFF);
	mutex_unlock(&hdmi->lock);
}

struct hdmi *rockchip_hdmi_register(struct hdmi_property *property,
				    struct hdmi_ops *ops)
{
	struct hdmi *hdmi;
	char name[32];
	int i;

	if (property == NULL || ops == NULL) {
		pr_err("HDMI: %s invalid parameter\n", __func__);
		return NULL;
	}

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			break;
	}
	if (i == HDMI_MAX_ID)
		return NULL;

	DBG("hdmi_register() - video source %d display %d",
	    property->videosrc,  property->display);

	hdmi = kmalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi) {
		pr_err("HDMI: no memory to allocate hdmi device.\n");
		return NULL;
	}
	memset(hdmi, 0, sizeof(struct hdmi));
	mutex_init(&hdmi->lock);

	hdmi->property = property;
	hdmi->ops = ops;
	hdmi->enable = false;
	hdmi->mute = HDMI_AV_UNMUTE;
	hdmi->hotplug = HDMI_HPD_REMOVED;
	hdmi->autoset = HDMI_AUTO_CONFIG;
	if (uboot_vic > 0) {
		hdmi->vic = uboot_vic;
		hdmi->uboot = 1;
		hdmi->autoset = 0;
	} else if (hdmi->autoset) {
		hdmi->vic = 0;
	} else {
		hdmi->vic = HDMI_VIDEO_DEFAULT_MODE;
	}
	hdmi->colormode = HDMI_VIDEO_DEFAULT_COLORMODE;
	hdmi->colordepth = HDMI_DEPP_COLOR_AUTO;
	hdmi->mode_3d = HDMI_3D_NONE;
	hdmi->audio.type = HDMI_AUDIO_DEFAULT_TYPE;
	hdmi->audio.channel = HDMI_AUDIO_DEFAULT_CHANNEL;
	hdmi->audio.rate = HDMI_AUDIO_DEFAULT_RATE;
	hdmi->audio.word_length = HDMI_AUDIO_DEFAULT_WORDLENGTH;
	hdmi->xscale = 100;
	hdmi->yscale = 100;
	hdmi_init_modelist(hdmi);

#ifndef CONFIG_ARCH_RK29
	if (hdmi->property->videosrc == DISPLAY_SOURCE_LCDC0)
		hdmi->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi->lcdc = rk_get_lcdc_drv("lcdc1");
#endif
	memset(name, 0, 32);
	sprintf(name, "hdmi-%s", hdmi->property->name);
	hdmi->workqueue = create_singlethread_workqueue(name);
	if (hdmi->workqueue == NULL) {
		pr_err("HDMI,: create workqueue failed.\n");
		goto err_create_wq;
	}
	hdmi->ddev = hdmi_register_display_sysfs(hdmi, NULL);
	if (hdmi->ddev == NULL) {
		pr_err("HDMI : register display sysfs failed.\n");
		goto err_register_display;
	}
	hdmi->id = i;
	#ifdef CONFIG_SWITCH
	if (hdmi->id == 0) {
		hdmi->switchdev.name = "hdmi";
	} else {
		hdmi->switchdev.name = kzalloc(32, GFP_KERNEL);
		memset((char *)hdmi->switchdev.name, 0, 32);
		sprintf((char *)hdmi->switchdev.name, "hdmi%d", hdmi->id);
	}
	switch_dev_register(&(hdmi->switchdev));
	#endif

	ref_info[i].hdmi = hdmi;
	ref_info[i].ref = 1;
	return hdmi;

err_register_display:
	destroy_workqueue(hdmi->workqueue);
err_create_wq:
	kfree(hdmi);
	return NULL;
}

void rockchip_hdmi_unregister(struct hdmi *hdmi)
{
	if (hdmi) {
		flush_workqueue(hdmi->workqueue);
		destroy_workqueue(hdmi->workqueue);
		#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi->switchdev));
		#endif
		hdmi_unregister_display_sysfs(hdmi);
		fb_destroy_modelist(&hdmi->edid.modelist);
		kfree(hdmi->edid.audio);
		if (hdmi->edid.specs) {
			kfree(hdmi->edid.specs->modedb);
			kfree(hdmi->edid.specs);
		}
		kfree(hdmi);

		ref_info[hdmi->id].ref = 0;
		ref_info[hdmi->id].hdmi = NULL;

		hdmi = NULL;
	}
}

int hdmi_get_hotplug(void)
{
	if (ref_info[0].hdmi)
		return ref_info[0].hdmi->hotplug;
	else
		return HDMI_HPD_REMOVED;
}

int hdmi_config_audio(struct hdmi_audio	*audio)
{
	int i;
	struct hdmi *hdmi;

	if (audio == NULL)
		return HDMI_ERROR_FALSE;

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			continue;
		hdmi = ref_info[i].hdmi;

		/*
		if (memcmp(audio, &hdmi->audio, sizeof(struct hdmi_audio)) == 0)
			continue;
		*/
		/*for (j = 0; j < hdmi->edid.audio_num; j++) {
			if (audio->type == hdmi->edid.audio_num)
				break;
		}*/

		/*if ( (j == hdmi->edid.audio_num) ||
			(audio->channel > hdmi->edid.audio[j].channel) ||
			((audio->rate & hdmi->edid.audio[j].rate) == 0)||
			((audio->type == HDMI_AUDIO_LPCM) &&
			((audio->word_length &
			  hdmi->edid.audio[j].word_length) == 0)) ) {
			pr_warn("[%s] warning : input audio type
				not supported in hdmi sink\n", __func__);
			continue;
		}*/
		memcpy(&hdmi->audio, audio, sizeof(struct hdmi_audio));
		hdmi_submit_work(hdmi, HDMI_SET_AUDIO, 0, NULL);
	}
	return 0;
}

void hdmi_audio_mute(int mute)
{
	int i;
	struct hdmi *hdmi;

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			continue;
		hdmi = ref_info[i].hdmi;

		if (mute)
			hdmi_submit_work(hdmi, HDMI_MUTE_AUDIO, 0, NULL);
		else
			hdmi_submit_work(hdmi, HDMI_UNMUTE_AUDIO, 0, NULL);
	}
}

static int __init bootloader_setup(char *str)
{
	if (str) {
		pr_info("hdmi init vic is %s\n", str);
		if (kstrtoint(str, 0, &uboot_vic) < 0)
			uboot_vic = 0;
	}
	return 0;
}

early_param("hdmi.vic", bootloader_setup);

static int __init hdmi_class_init(void)
{
	int i;

	for (i = 0; i < HDMI_MAX_ID; i++) {
		ref_info[i].id = i;
		ref_info[i].ref = 0;
		ref_info[i].hdmi = NULL;
	}
	pr_info("Rockchip hdmi driver version 2.0\n.");
	return 0;
}

subsys_initcall(hdmi_class_init);
