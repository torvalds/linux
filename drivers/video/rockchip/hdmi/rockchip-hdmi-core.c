#include <linux/delay.h>
#include <sound/pcm_params.h>
#include "rockchip-hdmi.h"
#include "rockchip-hdmi-cec.h"

#define HW_PARAMS_FLAG_LPCM 0
#define HW_PARAMS_FLAG_NLPCM 1

struct hdmi_delayed_work {
	struct delayed_work work;
	struct hdmi *hdmi;
	int event;
	int sync;
	void *data;
};

struct hdmi_id_ref_info {
	struct hdmi *hdmi;
	int id;
	int ref;
} ref_info[HDMI_MAX_ID];

static int uboot_vic;
static void hdmi_work_queue(struct work_struct *work);

void hdmi_submit_work(struct hdmi *hdmi,
		      int event, int delay, int sync)
{
	struct hdmi_delayed_work *work;

	HDMIDBG(2, "%s event %04x delay %d sync %d\n",
		__func__, event, delay, sync);

	work = kmalloc(sizeof(*work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, hdmi_work_queue);
		work->hdmi = hdmi;
		work->event = event;
		work->data = NULL;
		work->sync = sync;
		queue_delayed_work(hdmi->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
		if (sync) {
			flush_delayed_work(&work->work);
			kfree(work);
		}
	} else {
		pr_warn("HDMI: Cannot allocate memory to create work\n");
	}
}

static void hdmi_send_uevent(struct hdmi *hdmi, int uevent)
{
	char *envp[3];

	envp[0] = "INTERFACE=HDMI";
	envp[1] = kmalloc(32, GFP_KERNEL);
	if (!envp[1])
		return;
	sprintf(envp[1], "SCREEN=%d", hdmi->ddev->property);
	envp[2] = NULL;
	kobject_uevent_env(&hdmi->ddev->dev->kobj, uevent, envp);
	kfree(envp[1]);
}

static inline void hdmi_wq_set_output(struct hdmi *hdmi, int mute)
{
	HDMIDBG(2, "%s mute %d\n", __func__, mute);
	if (hdmi->ops->setmute)
		hdmi->ops->setmute(hdmi, mute);
}

static inline void hdmi_wq_set_audio(struct hdmi *hdmi)
{
	HDMIDBG(2, "%s\n", __func__);
	if (hdmi->ops->setaudio)
		hdmi->ops->setaudio(hdmi, &hdmi->audio);
}

static int hdmi_check_format(struct hdmi *hdmi, struct hdmi_video *vpara)
{
	struct hdmi_video_timing *timing;
	struct fb_videomode *mode;
	int tmdsclk;

	if (!vpara)
		return -ENOENT;
	timing = (struct hdmi_video_timing *)hdmi_vic2timing(vpara->vic);
	if (!timing) {
		pr_err("[%s] not found vic %d\n", __func__, vpara->vic);
		return -ENOENT;
	}
	mode = &timing->mode;
	if (vpara->color_input == HDMI_COLOR_YCBCR420)
		tmdsclk = mode->pixclock / 2;
	else if (vpara->format_3d == HDMI_3D_FRAME_PACKING)
		tmdsclk = 2 * mode->pixclock;
	else
		tmdsclk = mode->pixclock;
	if (vpara->color_output != HDMI_COLOR_YCBCR422) {
		switch (vpara->color_output_depth) {
		case 10:
			tmdsclk += tmdsclk / 4;
			break;
		case 12:
			tmdsclk += tmdsclk / 2;
			break;
		case 16:
			tmdsclk += tmdsclk;
			break;
		case 8:
		default:
			break;
		}
	} else if (vpara->color_output_depth > 12) {
		/* YCbCr422 mode only support up to 12bit */
		vpara->color_output_depth = 12;
	}
	if ((tmdsclk > 594000000) ||
	    (tmdsclk > 340000000 &&
	     tmdsclk > hdmi->edid.maxtmdsclock)) {
		if (vpara->format_3d == HDMI_3D_FRAME_PACKING) {
			pr_err("out of max tmdsclk, disable 3d\n");
			vpara->format_3d = 0;
		} else if (vpara->color_output == HDMI_COLOR_YCBCR444 &&
			   hdmi->edid.ycbcr422) {
			pr_warn("out of max tmdsclk, down to YCbCr422");
			vpara->color_output = HDMI_COLOR_YCBCR422;
		} else {
			pr_warn("out of max tmds clock, limit to 8bit\n");
			vpara->color_output_depth = 8;
		}
	}
	return 0;
}

static void hdmi_wq_set_video(struct hdmi *hdmi)
{
	struct hdmi_video *video = &hdmi->video;
	int	deepcolor;

	HDMIDBG(2, "%s\n", __func__);

	video->sink_hdmi = hdmi->edid.sink_hdmi;
	video->format_3d = hdmi->mode_3d;
	video->colorimetry = hdmi->colorimetry;
	video->color_output_depth = 8;
	if (hdmi->autoset)
		hdmi->vic = hdmi_find_best_mode(hdmi, 0);
	else
		hdmi->vic = hdmi_find_best_mode(hdmi, hdmi->vic);

	if (hdmi->vic == 0)
		hdmi->vic = hdmi->property->defaultmode;

	/* For DVI, output RGB */
	if (video->sink_hdmi == 0) {
		video->color_output = HDMI_COLOR_RGB_0_255;
	} else {
		if (hdmi->colormode == HDMI_COLOR_AUTO) {
			if (hdmi->edid.ycbcr444)
				video->color_output = HDMI_COLOR_YCBCR444;
			else if (hdmi->edid.ycbcr422)
				video->color_output = HDMI_COLOR_YCBCR422;
			else
				video->color_output = HDMI_COLOR_RGB_16_235;
		} else {
			video->color_output = hdmi->colormode;
		}
		if (hdmi->vic & HDMI_VIDEO_YUV420) {
			video->color_output = HDMI_COLOR_YCBCR420;
			deepcolor = hdmi->edid.deepcolor_420;
		} else {
			deepcolor = hdmi->edid.deepcolor;
		}
		if ((hdmi->property->feature & SUPPORT_DEEP_10BIT) &&
		    (deepcolor & HDMI_DEEP_COLOR_30BITS) &&
		     hdmi->colordepth == 10)
			video->color_output_depth = 10;
	}
	pr_info("hdmi output corlor mode is %d\n", video->color_output);
	if ((hdmi->property->feature & SUPPORT_YCBCR_INPUT) &&
	    (video->color_output == HDMI_COLOR_YCBCR444 ||
	     video->color_output == HDMI_COLOR_YCBCR422))
		video->color_input = HDMI_COLOR_YCBCR444;
	else if (video->color_output == HDMI_COLOR_YCBCR420)
		video->color_input = HDMI_COLOR_YCBCR420;
	else
		video->color_input = HDMI_COLOR_RGB_0_255;

	if ((hdmi->vic & HDMI_VIDEO_DMT) || (hdmi->vic & HDMI_VIDEO_DISCRETE_VR)) {
		if (hdmi->edid_auto_support) {
			if (hdmi->prop.value.vic)
				video->vic = hdmi->prop.value.vic;
			else
				video->vic = hdmi->vic;
		} else {
			video->vic = hdmi->vic;
		}
		video->color_output_depth = 8;
		video->eotf = 0;
	} else {
		video->vic = hdmi->vic & HDMI_VIC_MASK;

		if (hdmi->eotf & hdmi->edid.hdr.hdrinfo.eotf)
			video->eotf = hdmi->eotf;
		else
			video->eotf = 0;
		/* ST_2084 must be 10bit and bt2020 */
		if (video->eotf & EOTF_ST_2084) {
			if (deepcolor & HDMI_DEEP_COLOR_30BITS)
				video->color_output_depth = 10;
			if (video->color_output > HDMI_COLOR_RGB_16_235)
				video->colorimetry =
					HDMI_COLORIMETRY_EXTEND_BT_2020_YCC;
			else
				video->colorimetry =
					HDMI_COLORIMETRY_EXTEND_BT_2020_RGB;
		}
	}
	hdmi_check_format(hdmi, video);
	if (hdmi->uboot) {
		if ((uboot_vic & HDMI_UBOOT_VIC_MASK) != hdmi->vic)
			hdmi->uboot = 0;
	}

	hdmi_set_lcdc(hdmi);
	if (hdmi->ops->setvideo)
		hdmi->ops->setvideo(hdmi, video);
}

static void hdmi_wq_set_hdr(struct hdmi *hdmi)
{
	struct hdmi_video *video = &hdmi->video;
	int deepcolor = 8;

	if (hdmi->vic & HDMI_VIDEO_YUV420)
		deepcolor = hdmi->edid.deepcolor_420;
	else
		deepcolor = hdmi->edid.deepcolor;

	if ((hdmi->property->feature & SUPPORT_DEEP_10BIT) &&
	    (deepcolor & HDMI_DEEP_COLOR_30BITS) &&
	     hdmi->colordepth == 10)
		deepcolor = 10;

	if (deepcolor == video->color_output_depth &&
	    hdmi->ops->sethdr && hdmi->ops->setavi &&
	    hdmi->edid.sink_hdmi) {
		if (hdmi->eotf & hdmi->edid.hdr.hdrinfo.eotf)
			video->eotf = hdmi->eotf;
		else
			video->eotf = 0;

		/* ST_2084 must be 10bit and bt2020 */
		if (video->eotf & EOTF_ST_2084) {
			if (video->color_output > HDMI_COLOR_RGB_16_235)
				video->colorimetry =
					HDMI_COLORIMETRY_EXTEND_BT_2020_YCC;
			else
				video->colorimetry =
					HDMI_COLORIMETRY_EXTEND_BT_2020_RGB;
		} else {
			video->colorimetry = hdmi->colorimetry;
		}
		hdmi_set_lcdc(hdmi);
		hdmi->ops->sethdr(hdmi, hdmi->eotf, &hdmi->hdr);
		hdmi->ops->setavi(hdmi, video);
	} else {
		hdmi_submit_work(hdmi, HDMI_SET_COLOR, 0, 0);
	}
}

static void hdmi_wq_parse_edid(struct hdmi *hdmi)
{
	struct hdmi_edid *pedid;

	int rc = HDMI_ERROR_SUCCESS, extendblock = 0, i, trytimes;

	if (!hdmi)
		return;

	HDMIDBG(2, "%s\n", __func__);

	pedid = &hdmi->edid;
	fb_destroy_modelist(&pedid->modelist);
	memset(pedid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&pedid->modelist);

	pedid->raw[0] = kmalloc(HDMI_EDID_BLOCK_SIZE, GFP_KERNEL);
	if (!pedid->raw[0]) {
		rc = HDMI_ERROR_FALSE;
		goto out;
	}

	if (!hdmi->ops->getedid) {
		rc = HDMI_ERROR_FALSE;
		goto out;
	}

	/* Read base block edid.*/
	for (trytimes = 0; trytimes < 3; trytimes++) {
		if (trytimes)
			msleep(50);
		memset(pedid->raw[0], 0, HDMI_EDID_BLOCK_SIZE);
		rc = hdmi->ops->getedid(hdmi, 0, pedid->raw[0]);
		if (rc) {
			dev_err(hdmi->dev,
				"[HDMI] read edid base block error\n");
			continue;
		}

		rc = hdmi_edid_parse_base(hdmi,
					  pedid->raw[0], &extendblock, pedid);
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

	for (i = 1; (i < extendblock + 1) && (i < HDMI_MAX_EDID_BLOCK); i++) {
		pedid->raw[i] = kmalloc(HDMI_EDID_BLOCK_SIZE, GFP_KERNEL);
		if (!pedid->raw[i]) {
			dev_err(hdmi->dev,
				"[%s] can not allocate memory for edid buff.\n",
				__func__);
			rc = HDMI_ERROR_FALSE;
			goto out;
		}
		for (trytimes = 0; trytimes < 3; trytimes++) {
			if (trytimes)
				msleep(20);
			memset(pedid->raw[i], 0, HDMI_EDID_BLOCK_SIZE);
			rc = hdmi->ops->getedid(hdmi, i, pedid->raw[i]);
			if (rc) {
				dev_err(hdmi->dev,
					"[HDMI] read edid block %d error\n",
					i);
				continue;
			}

			rc = hdmi_edid_parse_extensions(pedid->raw[i], pedid);
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
	rc = hdmi_ouputmode_select(hdmi, rc);
}

static void hdmi_wq_insert(struct hdmi *hdmi)
{
	HDMIDBG(2, "%s\n", __func__);
	if (hdmi->ops->insert)
		hdmi->ops->insert(hdmi);
	hdmi_wq_parse_edid(hdmi);
	if (hdmi->property->feature & SUPPORT_CEC)
		rockchip_hdmi_cec_set_pa(hdmi->edid.cecaddress);
	hdmi_send_uevent(hdmi, KOBJ_ADD);
	if (hdmi->enable) {
		/*hdmi->autoset = 0;*/
		hdmi_wq_set_video(hdmi);
		#ifdef CONFIG_SWITCH
		switch_set_state(&hdmi->switchdev, 1);
		#endif
		hdmi_wq_set_audio(hdmi);
		hdmi_wq_set_output(hdmi, hdmi->mute);
		hdmi_submit_work(hdmi, HDMI_ENABLE_HDCP, 100, 0);
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
	int i;

	HDMIDBG(2, "%s\n", __func__);
	if (hdmi->ops->remove)
		hdmi->ops->remove(hdmi);
	if (hdmi->property->feature & SUPPORT_CEC)
		rockchip_hdmi_cec_set_pa(0);
	if (hdmi->hotplug == HDMI_HPD_ACTIVATED) {
		screen.type = SCREEN_HDMI;
		rk_fb_switch_screen(&screen, 0, hdmi->lcdc->id);
	}
	#ifdef CONFIG_SWITCH
	switch_set_state(&hdmi->switchdev, 0);
	#endif
	list_for_each_safe(pos, n, &hdmi->edid.modelist) {
		list_del(pos);
		kfree(pos);
	}
	for (i = 0; i < HDMI_MAX_EDID_BLOCK; i++)
		kfree(hdmi->edid.raw[i]);
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

	mutex_lock(&hdmi->ddev->lock);

	HDMIDBG(2, "\nhdmi_work_queue() - evt= %x %d\n",
		(event & 0xFF00) >> 8, event & 0xFF);

	if ((!hdmi->enable || hdmi->sleep) &&
	    (event != HDMI_ENABLE_CTL) &&
	    (event != HDMI_RESUME_CTL) &&
	    (event != HDMI_DISABLE_CTL) &&
	    (event != HDMI_SUSPEND_CTL))
		goto exit;

	switch (event) {
	case HDMI_ENABLE_CTL:
		if (!hdmi->enable) {
			hdmi->enable = 1;
			if (!hdmi->sleep) {
				if (hdmi->ops->enable)
					hdmi->ops->enable(hdmi);
				if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
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
			if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
				hdmi_wq_set_output(hdmi,
						   HDMI_VIDEO_MUTE |
						   HDMI_AUDIO_MUTE);
			if (!hdmi->sleep) {
				if (hdmi->ops->disable)
					hdmi->ops->disable(hdmi);
				hdmi_wq_remove(hdmi);
			}
			hdmi->enable = 0;
		}
		break;
	case HDMI_SUSPEND_CTL:
		if (!hdmi->sleep) {
			if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
				hdmi_wq_set_output(hdmi,
						   HDMI_VIDEO_MUTE |
						   HDMI_AUDIO_MUTE);
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
		HDMIDBG(2, "hdmi_work_queue() - hpd is %d hotplug is %d\n",
			hpd, hdmi->hotplug);
		if (hpd != hdmi->hotplug) {
			if (hpd == HDMI_HPD_ACTIVATED) {
				hdmi->hotplug = hpd;
				hdmi_wq_insert(hdmi);
			} else if (hdmi->hotplug == HDMI_HPD_ACTIVATED) {
				hdmi_wq_remove(hdmi);
			}
			hdmi->hotplug = hpd;
		}
		break;
	case HDMI_SET_VIDEO:
		hdmi_wq_set_output(hdmi,
				   HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE);
		if (rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
			msleep(2000);
		else
			msleep(1100);
		hdmi_wq_set_video(hdmi);
		hdmi_send_uevent(hdmi, KOBJ_CHANGE);
		hdmi_wq_set_audio(hdmi);
		hdmi_wq_set_output(hdmi, hdmi->mute);
		if (hdmi->ops->hdcp_cb)
			hdmi->ops->hdcp_cb(hdmi);
		break;
	case HDMI_SET_AUDIO:
		if ((hdmi->mute & HDMI_AUDIO_MUTE) == 0) {
			hdmi_wq_set_output(hdmi, HDMI_AUDIO_MUTE);
			hdmi_wq_set_audio(hdmi);
			hdmi_wq_set_output(hdmi, hdmi->mute);
		}
		break;
	case HDMI_MUTE_AUDIO:
	case HDMI_UNMUTE_AUDIO:
		if (hdmi->mute & HDMI_AUDIO_MUTE ||
		    hdmi->hotplug != HDMI_HPD_ACTIVATED)
			break;
		if (event == HDMI_MUTE_AUDIO)
			hdmi_wq_set_output(hdmi, hdmi->mute |
					   HDMI_AUDIO_MUTE);
		else
			hdmi_wq_set_output(hdmi,
					   hdmi->mute & (~HDMI_AUDIO_MUTE));
		break;
	case HDMI_SET_3D:
		if (hdmi->ops->setvsi && hdmi->edid.sink_hdmi) {
			if (hdmi->mode_3d == HDMI_3D_FRAME_PACKING ||
			    hdmi->video.format_3d ==
			    HDMI_3D_FRAME_PACKING) {
				hdmi_wq_set_output(hdmi,
						   HDMI_VIDEO_MUTE |
						   HDMI_AUDIO_MUTE);
				msleep(100);
				hdmi_wq_set_video(hdmi);
				hdmi_wq_set_audio(hdmi);
				hdmi_wq_set_output(hdmi, hdmi->mute);
			} else if (hdmi->mode_3d != HDMI_3D_NONE) {
				hdmi->ops->setvsi(hdmi, hdmi->mode_3d,
						  HDMI_VIDEO_FORMAT_3D);
			} else if ((hdmi->vic & HDMI_TYPE_MASK) == 0) {
				hdmi->ops->setvsi(hdmi, hdmi->vic,
						  HDMI_VIDEO_FORMAT_NORMAL);
			}
		}
		break;
	case HDMI_SET_COLOR:
		hdmi_wq_set_output(hdmi,
				   HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE);
		msleep(100);
		hdmi_wq_set_video(hdmi);
		hdmi_wq_set_audio(hdmi);
		hdmi_wq_set_output(hdmi, hdmi->mute);
		break;
	case HDMI_SET_HDR:
		hdmi_wq_set_hdr(hdmi);
		break;
	case HDMI_ENABLE_HDCP:
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED && hdmi->ops->hdcp_cb)
			hdmi->ops->hdcp_cb(hdmi);
		break;
	case HDMI_HDCP_AUTH_2ND:
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED &&
		    hdmi->ops->hdcp_auth2nd)
			hdmi->ops->hdcp_auth2nd(hdmi);
		break;
	default:
		break;
	}
exit:
	kfree(hdmi_w->data);
	if (!hdmi_w->sync)
		kfree(hdmi_w);

	HDMIDBG(2, "\nhdmi_work_queue() - exit evt= %x %d\n",
		(event & 0xFF00) >> 8, event & 0xFF);
	mutex_unlock(&hdmi->ddev->lock);
}

struct hdmi *rockchip_hdmi_register(struct hdmi_property *property,
				    struct hdmi_ops *ops)
{
	struct hdmi *hdmi;
	char name[32];
	int i;

	if (!property || !ops) {
		pr_err("HDMI: %s invalid parameter\n", __func__);
		return NULL;
	}

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			break;
	}
	if (i == HDMI_MAX_ID)
		return NULL;

	HDMIDBG(2, "hdmi_register() - video source %d display %d\n",
		property->videosrc,  property->display);

	hdmi = kmalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return NULL;

	memset(hdmi, 0, sizeof(struct hdmi));
	mutex_init(&hdmi->lock);
	mutex_init(&hdmi->pclk_lock);

	hdmi->property = property;
	hdmi->ops = ops;
	hdmi->enable = false;
	hdmi->mute = HDMI_AV_UNMUTE;
	hdmi->hotplug = HDMI_HPD_REMOVED;
	hdmi->autoset = HDMI_AUTO_CONFIG;
	if (uboot_vic > 0) {
		hdmi->vic = uboot_vic & HDMI_UBOOT_VIC_MASK;
		if (uboot_vic & HDMI_UBOOT_NOT_INIT)
			hdmi->uboot = 0;
		else
			hdmi->uboot = 1;
		hdmi->autoset = 0;
	} else if (hdmi->autoset) {
		hdmi->vic = 0;
	} else {
		hdmi->vic = hdmi->property->defaultmode;
	}
	hdmi->colormode = HDMI_VIDEO_DEFAULT_COLORMODE;
	hdmi->colordepth = hdmi->property->defaultdepth;
	hdmi->colorimetry = HDMI_COLORIMETRY_NO_DATA;
	hdmi->mode_3d = HDMI_3D_NONE;
	hdmi->audio.type = HDMI_AUDIO_DEFAULT_TYPE;
	hdmi->audio.channel = HDMI_AUDIO_DEFAULT_CHANNEL;
	hdmi->audio.rate = HDMI_AUDIO_DEFAULT_RATE;
	hdmi->audio.word_length = HDMI_AUDIO_DEFAULT_WORDLENGTH;
	hdmi->xscale = 100;
	hdmi->yscale = 100;

	if (hdmi->property->videosrc == DISPLAY_SOURCE_LCDC0)
		hdmi->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi->lcdc = rk_get_lcdc_drv("lcdc1");
	if (!hdmi->lcdc)
		goto err_create_wq;
	if (hdmi->lcdc->prop == EXTEND)
		hdmi->property->display = DISPLAY_AUX;
	else
		hdmi->property->display = DISPLAY_MAIN;
	memset(name, 0, 32);
	sprintf(name, "hdmi-%s", hdmi->property->name);
	hdmi->workqueue = create_singlethread_workqueue(name);
	if (!hdmi->workqueue) {
		pr_err("HDMI,: create workqueue failed.\n");
		goto err_create_wq;
	}
	hdmi->ddev = hdmi_register_display_sysfs(hdmi, NULL);
	if (!hdmi->ddev) {
		pr_err("HDMI : register display sysfs failed.\n");
		goto err_register_display;
	}
	hdmi->id = i;
	hdmi_init_modelist(hdmi);
	#ifdef CONFIG_SWITCH
	if (hdmi->id == 0) {
		hdmi->switchdev.name = "hdmi";
	} else {
		hdmi->switchdev.name = kzalloc(32, GFP_KERNEL);
		memset((char *)hdmi->switchdev.name, 0, 32);
		sprintf((char *)hdmi->switchdev.name, "hdmi%d", hdmi->id);
	}
	switch_dev_register(&hdmi->switchdev);
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
		switch_dev_unregister(&hdmi->switchdev);
		#endif
		hdmi_unregister_display_sysfs(hdmi);
		fb_destroy_modelist(&hdmi->edid.modelist);
		kfree(hdmi->edid.audio);
		if (hdmi->edid.specs) {
			kfree(hdmi->edid.specs->modedb);
			kfree(hdmi->edid.specs);
		}
		ref_info[hdmi->id].ref = 0;
		ref_info[hdmi->id].hdmi = NULL;
		kfree(hdmi);

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

	if (!audio)
		return HDMI_ERROR_FALSE;

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			continue;
		hdmi = ref_info[i].hdmi;
		memcpy(&hdmi->audio, audio, sizeof(struct hdmi_audio));
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
			hdmi_submit_work(hdmi, HDMI_SET_AUDIO, 0, 0);
	}
	return 0;
}

int snd_config_hdmi_audio(struct snd_pcm_hw_params *params)
{
	struct hdmi_audio audio_cfg;
	u32	rate;

	switch (params_rate(params)) {
	case 32000:
		rate = HDMI_AUDIO_FS_32000;
		break;
	case 44100:
		rate = HDMI_AUDIO_FS_44100;
		break;
	case 48000:
		rate = HDMI_AUDIO_FS_48000;
		break;
	case 88200:
		rate = HDMI_AUDIO_FS_88200;
		break;
	case 96000:
		rate = HDMI_AUDIO_FS_96000;
		break;
	case 176400:
		rate = HDMI_AUDIO_FS_176400;
		break;
	case 192000:
		rate = HDMI_AUDIO_FS_192000;
		break;
	default:
		pr_err("rate %d unsupport.\n", params_rate(params));
		rate = HDMI_AUDIO_FS_44100;
	}

	audio_cfg.rate = rate;

	if (params->flags == HW_PARAMS_FLAG_NLPCM)
		audio_cfg.type = HDMI_AUDIO_NLPCM;
	else
		audio_cfg.type = HDMI_AUDIO_LPCM;

	audio_cfg.channel = params_channels(params);
	audio_cfg.word_length = HDMI_AUDIO_WORD_LENGTH_16bit;

	return hdmi_config_audio(&audio_cfg);
}
EXPORT_SYMBOL(snd_config_hdmi_audio);

void hdmi_audio_mute(int mute)
{
	int i;
	struct hdmi *hdmi;

	for (i = 0; i < HDMI_MAX_ID; i++) {
		if (ref_info[i].ref == 0)
			continue;
		hdmi = ref_info[i].hdmi;

		if (mute)
			hdmi_submit_work(hdmi, HDMI_MUTE_AUDIO, 0, 0);
		else
			hdmi_submit_work(hdmi, HDMI_UNMUTE_AUDIO, 0, 0);
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
