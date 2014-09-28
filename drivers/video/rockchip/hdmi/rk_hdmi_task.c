#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/rockchip/common.h>
#include <dt-bindings/clock/rk_system_status.h>
#include "rk_hdmi.h"

#define HDMI_MAX_TRY_TIMES	1
#define HDMI_MAX_ID 1

static char *envp[] = { "INTERFACE=HDMI", NULL };
static int uboot_vic;
static void hdmi_sys_show_state(struct hdmi *hdmi)
{
	switch (hdmi->state) {
	case HDMI_SLEEP:
		hdmi_dbg(hdmi->dev, "HDMI_SLEEP\n");
		break;
	case HDMI_INITIAL:
		hdmi_dbg(hdmi->dev, "HDMI_INITIAL\n");
		break;
	case WAIT_HOTPLUG:
		hdmi_dbg(hdmi->dev, "WAIT_HOTPLUG\n");
		break;
	case READ_PARSE_EDID:
		hdmi_dbg(hdmi->dev, "READ_PARSE_EDID\n");
		break;
	case WAIT_HDMI_ENABLE:
		hdmi_dbg(hdmi->dev, "WAIT_HDMI_ENABLE\n");
		break;
	case SYSTEM_CONFIG:
		hdmi_dbg(hdmi->dev, "SYSTEM_CONFIG\n");
		break;
	case CONFIG_VIDEO:
		hdmi_dbg(hdmi->dev, "CONFIG_VIDEO\n");
		break;
	case CONFIG_AUDIO:
		hdmi_dbg(hdmi->dev, "CONFIG_AUDIO\n");
		break;
	case PLAY_BACK:
		hdmi_dbg(hdmi->dev, "PLAY_BACK\n");
		break;
	default:
		hdmi_dbg(hdmi->dev, "Unkown State %d\n", hdmi->state);
		break;
	}
}

int hdmi_sys_init(struct hdmi *hdmi)
{
	hdmi->uboot_logo = support_uboot_display();
	printk("%s,uboot-logo=%d,uboot_vic=%d\n",__func__,hdmi->uboot_logo,uboot_vic);
	if (hdmi->uboot_logo) {
		hdmi->hotplug = HDMI_HPD_ACTIVED;
		hdmi->state = PLAY_BACK;
		hdmi->enable = HDMI_ENABLE;
		hdmi->display = HDMI_DISABLE;
		hdmi->vic = uboot_vic;
	} else {
		hdmi->hotplug = HDMI_HPD_REMOVED;
		hdmi->state = HDMI_SLEEP;
		hdmi->enable = HDMI_ENABLE;
		hdmi->display = HDMI_DISABLE;
		hdmi->vic = HDMI_VIDEO_DEFAULT_MODE;
	}
	hdmi->autoconfig = HDMI_AUTO_CONFIGURE;
	hdmi->audio.channel = HDMI_AUDIO_DEFAULT_CHANNEL;
	hdmi->audio.rate = HDMI_AUDIO_DEFAULT_RATE;
	hdmi->audio.word_length = HDMI_AUDIO_DEFAULT_WORD_LENGTH;

	memset(&hdmi->edid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	mutex_init(&hdmi->lock);
	return 0;
}

void hdmi_sys_remove(struct hdmi *hdmi)
{
	int audio_need;

	audio_need = hdmi->edid.base_audio_support == 1 &&
			hdmi->edid.sink_hdmi == 1;

	fb_destroy_modelist(&hdmi->edid.modelist);
	kfree(hdmi->edid.audio);
	if (hdmi->edid.specs) {
		kfree(hdmi->edid.specs->modedb);
		kfree(hdmi->edid.specs);
	}
	memset(&hdmi->edid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	hdmi->display = HDMI_DISABLE;
	if (hdmi->set_vif)
		hdmi->set_vif(hdmi, hdmi->lcdc->cur_screen, 0);
	rk_fb_switch_screen(hdmi->lcdc->cur_screen, 0, hdmi->lcdc->id);
	kobject_uevent_env(&hdmi->ddev->dev->kobj, KOBJ_REMOVE, envp);

#ifdef CONFIG_SWITCH
	if (audio_need)
		switch_set_state(&(hdmi->switch_hdmi), 0);
#endif
	rockchip_clear_system_status(SYS_STATUS_HDMI);
}

static void hdmi_sys_sleep(struct hdmi *hdmi)
{
	mutex_lock(&hdmi->enable_mutex);
	if (hdmi->enable && hdmi->irq)
		disable_irq(hdmi->irq);
	hdmi->state = HDMI_SLEEP;
	hdmi->remove(hdmi);
	if (hdmi->enable && hdmi->irq)
		enable_irq(hdmi->irq);
	mutex_unlock(&hdmi->enable_mutex);
}

static int hdmi_process_command(struct hdmi *hdmi)
{
	int change, state = hdmi->state;

	change = hdmi->command;
	if (change != HDMI_CONFIG_NONE) {
		hdmi->command = HDMI_CONFIG_NONE;
		switch (change) {
		case HDMI_CONFIG_ENABLE:
			/* disable HDMI */
			mutex_lock(&hdmi->enable_mutex);
			if (!hdmi->enable || hdmi->suspend) {
				if (hdmi->hotplug != HDMI_HPD_REMOVED) {
					hdmi->hotplug = HDMI_HPD_REMOVED;
					hdmi_sys_remove(hdmi);
				}
				hdmi->state = HDMI_SLEEP;
				hdmi->remove(hdmi);
				state = HDMI_SLEEP;
			}
			mutex_unlock(&hdmi->enable_mutex);
			if (hdmi->wait == 1) {
				complete(&hdmi->complete);
				hdmi->wait = 0;
			}
			break;
		case HDMI_CONFIG_COLOR:
			if (state > CONFIG_VIDEO)
				state = CONFIG_VIDEO;
			break;
		case HDMI_CONFIG_HDCP:
			break;
		case HDMI_CONFIG_DISPLAY:
			break;
		case HDMI_CONFIG_AUDIO:
			if (state > CONFIG_AUDIO)
				state = CONFIG_AUDIO;
			break;
		case HDMI_CONFIG_VIDEO:
		default:
			if (state > SYSTEM_CONFIG) {
				state = SYSTEM_CONFIG;
			} else {
				if (hdmi->wait == 1) {
					complete(&hdmi->complete);
					hdmi->wait = 0;
				}
			}
			break;
		}
	} else if (state == HDMI_SLEEP) {
		state = WAIT_HOTPLUG;
	}
	return state;
}

static DEFINE_MUTEX(work_mutex);

void hdmi_work(struct work_struct *work)
{
	int hotplug, state_last;
	int rc = HDMI_ERROR_SUCESS, trytimes = 0;
	struct hdmi_video_para video;
	struct delayed_work *delay_work =
	    container_of(work, struct delayed_work, work);
	struct hdmi *hdmi = container_of(delay_work, struct hdmi, delay_work);

	mutex_lock(&work_mutex);
	/* Process hdmi command */
	hdmi->state = hdmi_process_command(hdmi);

	if (!hdmi->enable || hdmi->suspend) {
		mutex_unlock(&work_mutex);
		return;
	}
	hotplug = hdmi->detect_hotplug(hdmi);
	hdmi_dbg(hdmi->dev, "[%s] hotplug %02x curvalue %d\n", __func__,
		 hotplug, hdmi->hotplug);

	if (hotplug != hdmi->hotplug) {
		if (hotplug == HDMI_HPD_ACTIVED) {
			if (hdmi->insert)
				hdmi->insert(hdmi);
			hdmi->state = READ_PARSE_EDID;
		} else if (hdmi->hotplug == HDMI_HPD_ACTIVED) {
			hdmi->hotplug = hotplug;
			hdmi_sys_remove(hdmi);
			if (hotplug == HDMI_HPD_REMOVED) {
				hdmi_sys_sleep(hdmi);
			} else {
				hdmi->state = WAIT_HOTPLUG;
				hdmi->remove(hdmi);
			}
			if (hdmi->wait == 1) {
				complete(&hdmi->complete);
				hdmi->wait = 0;
			}
			mutex_unlock(&work_mutex);
			return;
		} else if (hotplug == HDMI_HPD_REMOVED) {
			hdmi->state = HDMI_SLEEP;
			hdmi->remove(hdmi);
		}
		hdmi->hotplug = hotplug;
	} else if (hotplug == HDMI_HPD_REMOVED) {
		hdmi_sys_sleep(hdmi);
	} else if (hotplug == HDMI_HPD_ACTIVED) {
		if (hdmi->insert)
			hdmi->insert(hdmi);
		hdmi->state = READ_PARSE_EDID;
	}
	do {
		hdmi_sys_show_state(hdmi);
		state_last = hdmi->state;
		switch (hdmi->state) {
		case READ_PARSE_EDID:
			rc = hdmi_sys_parse_edid(hdmi);
			if (rc == HDMI_ERROR_SUCESS) {
				if (hdmi->cec_set_device_pa)
					hdmi->cec_set_device_pa(hdmi->edid.cecaddress);
				if (hdmi->cec_enumerate)
					hdmi->cec_enumerate();
				hdmi->state = SYSTEM_CONFIG;
				kobject_uevent_env(&hdmi->ddev->dev->kobj,
						   KOBJ_ADD, envp);
				hdmi_dbg(hdmi->dev,
					 "[%s] base_audio_support =%d,sink_hdmi = %d\n",
					 __func__,
					 hdmi->edid.base_audio_support,
					 hdmi->edid.sink_hdmi);
#ifdef CONFIG_SWITCH
				if (hdmi->edid.base_audio_support == 1 &&
				    hdmi->edid.sink_hdmi == 1)
					switch_set_state(&(hdmi->switch_hdmi),
							 1);
#endif
				rockchip_set_system_status(SYS_STATUS_HDMI);
			}
			if (hdmi->uboot_logo) {
				hdmi->state = CONFIG_AUDIO;
			}
			break;
		case SYSTEM_CONFIG:
			if (hdmi->remove)
				hdmi->remove(hdmi);

			if (hdmi->autoconfig)
				hdmi->vic = hdmi_find_best_mode(hdmi, 0);
			else
				hdmi->vic =
				    hdmi_find_best_mode(hdmi, hdmi->vic);
			rc = hdmi_switch_fb(hdmi, hdmi->vic);
			if (rc == HDMI_ERROR_SUCESS)
				hdmi->state = CONFIG_VIDEO;
			break;
		case CONFIG_VIDEO:
			hdmi->display = HDMI_DISABLE;
			hdmi_init_video_para(hdmi, &video);
			rc = hdmi->config_video(hdmi, &video);
			if (rc == HDMI_ERROR_SUCESS) {
				if (hdmi->edid.sink_hdmi)
					hdmi->state = CONFIG_AUDIO;
				else
					hdmi->state = PLAY_BACK;
			}
			break;
		case CONFIG_AUDIO:
			rc = hdmi->config_audio(hdmi, &(hdmi->audio));

			if (rc == HDMI_ERROR_SUCESS)
				hdmi->state = PLAY_BACK;
			break;
		case PLAY_BACK:
			if (hdmi->display != HDMI_ENABLE) {
				hdmi->control_output(hdmi, HDMI_ENABLE);
				hdmi->display = HDMI_ENABLE;
				if (hdmi->hdcp_cb)
					hdmi->hdcp_cb();
			}

			if (hdmi->wait == 1) {
				complete(&hdmi->complete);
				hdmi->wait = 0;
			}
			break;
		default:
			break;
		}
		if (rc != HDMI_ERROR_SUCESS) {
			trytimes++;
			msleep(20);
		}
		if (hdmi->state != state_last)
			trytimes = 0;

	} while ((hdmi->state != state_last ||
		 (rc != HDMI_ERROR_SUCESS)) &&
		 trytimes < HDMI_MAX_TRY_TIMES);
	hdmi_dbg(hdmi->dev, "[%s] done\n", __func__);
	mutex_unlock(&work_mutex);
}

static int __init bootloader_setup(char *str)
{
       if(str) {
               printk("hdmi init vic is %s\n", str);
               sscanf(str, "%d", &uboot_vic);
       }
       /*uboot_vic = 16;*/
       return 0;
}
early_param("hdmi.vic", bootloader_setup);
