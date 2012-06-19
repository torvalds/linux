#include <linux/kernel.h>
#include <linux/delay.h>
#include "rk30_hdmi.h"
#include "rk30_hdmi_hw.h"

#ifdef CONFIG_HDMI_RK30_CTL_CODEC
extern void codec_set_spk(bool on);
#endif

#define HDMI_MAX_TRY_TIMES	1

static char *envp[] = {"INTERFACE=HDMI", NULL};

static void hdmi_sys_show_state(int state)
{
	switch(state)
	{
		case HDMI_SLEEP:
			dev_printk(KERN_INFO, hdmi->dev, "HDMI_SLEEP\n");
			break;
		case HDMI_INITIAL:
			dev_printk(KERN_INFO, hdmi->dev, "HDMI_INITIAL\n");
			break;
		case WAIT_HOTPLUG:
			dev_printk(KERN_INFO, hdmi->dev, "WAIT_HOTPLUG\n");
			break;
		case READ_PARSE_EDID:
			dev_printk(KERN_INFO, hdmi->dev, "READ_PARSE_EDID\n");
			break;
		case WAIT_HDMI_ENABLE:
			dev_printk(KERN_INFO, hdmi->dev, "WAIT_HDMI_ENABLE\n");
			break;
		case SYSTEM_CONFIG:
			dev_printk(KERN_INFO, hdmi->dev, "SYSTEM_CONFIG\n");
			break;
		case CONFIG_VIDEO:
			dev_printk(KERN_INFO, hdmi->dev, "CONFIG_VIDEO\n");
			break;
		case CONFIG_AUDIO:
			dev_printk(KERN_INFO, hdmi->dev, "CONFIG_AUDIO\n");
			break;
		case PLAY_BACK:
			dev_printk(KERN_INFO, hdmi->dev, "PLAY_BACK\n");
			break;
		default:
			dev_printk(KERN_INFO, hdmi->dev, "Unkown State %d\n", state);
			break;
	}
}

int hdmi_sys_init(void)
{
	hdmi->pwr_mode			= PWR_SAVE_MODE_A;
	hdmi->hotplug			= HDMI_HPD_REMOVED;
	hdmi->state				= HDMI_SLEEP;
	hdmi->enable			= HDMI_ENABLE;
	hdmi->autoconfig		= HDMI_AUTO_CONFIGURE;
	hdmi->display			= HDMI_DISABLE;
	
	hdmi->vic				= HDMI_VIDEO_DEFAULT_MODE;
	hdmi->audio.channel 	= HDMI_AUDIO_DEFAULT_CHANNEL;
	hdmi->audio.rate		= HDMI_AUDIO_DEFAULT_RATE;
	hdmi->audio.word_length	= HDMI_AUDIO_DEFAULT_WORD_LENGTH;
	
	memset(&hdmi->edid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	return 0;
}

void hdmi_sys_remove(void)
{
	fb_destroy_modelist(&hdmi->edid.modelist);
	if(hdmi->edid.audio)
		kfree(hdmi->edid.audio);
	if(hdmi->edid.specs)
	{
		if(hdmi->edid.specs->modedb)
			kfree(hdmi->edid.specs->modedb);
		kfree(hdmi->edid.specs);
	}
	memset(&hdmi->edid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	hdmi->display	= HDMI_DISABLE;
	rk_fb_switch_screen(hdmi->lcdc->screen, 0, HDMI_SOURCE_DEFAULT);
	kobject_uevent_env(&hdmi->dev->kobj, KOBJ_REMOVE, envp);
	#ifdef CONFIG_SWITCH
	switch_set_state(&(hdmi->switch_hdmi), 0);
	#endif
	#if CONFIG_HDMI_RK30_CTL_CODEC
	codec_set_spk(1);
	#endif
}

static void hdmi_sys_sleep(void)
{
	mutex_lock(&hdmi->enable_mutex);
	if(hdmi->enable)
		disable_irq(hdmi->irq);				
	hdmi->state = HDMI_SLEEP;
	rk30_hdmi_removed();
	if(hdmi->enable)
		enable_irq(hdmi->irq);
	mutex_unlock(&hdmi->enable_mutex);
}

static int hdmi_process_command(void)
{
	int change, state = hdmi->state;
	
	change = hdmi->command;
	if(change != HDMI_CONFIG_NONE)	
	{		
		hdmi->command = HDMI_CONFIG_NONE;
		switch(change)
		{	
			case HDMI_CONFIG_ENABLE:
				/* disable HDMI */
				mutex_lock(&hdmi->enable_mutex);
				if(!hdmi->enable || hdmi->suspend)
				{
					if(hdmi->hotplug == HDMI_HPD_ACTIVED)
						hdmi_sys_remove();
					hdmi->state = HDMI_SLEEP;
					hdmi->hotplug = HDMI_HPD_REMOVED;
					rk30_hdmi_removed();
					state = HDMI_SLEEP;
				}
				mutex_unlock(&hdmi->enable_mutex);
				if(hdmi->wait == 1) {
					complete(&hdmi->complete);
					hdmi->wait = 0;	
				}
				break;	
			case HDMI_CONFIG_COLOR:
				if(state > CONFIG_VIDEO)
					state = CONFIG_VIDEO;	
				break;
			case HDMI_CONFIG_HDCP:
				break;
			case HDMI_CONFIG_DISPLAY:
				break;
			case HDMI_CONFIG_AUDIO:
				if(state > CONFIG_AUDIO)
					state = CONFIG_AUDIO;
				break;
			case HDMI_CONFIG_VIDEO:
			default:
				if(state > SYSTEM_CONFIG)
					state = SYSTEM_CONFIG;
				else
				{
					if(hdmi->wait == 1) {
						complete(&hdmi->complete);
						hdmi->wait = 0;	
					}					
				}
				break;
		}
	}
	else if(state == HDMI_SLEEP)
		state = WAIT_HOTPLUG;
	return state;
}

static DEFINE_MUTEX(work_mutex);

void hdmi_work(struct work_struct *work)
{
	int hotplug, state_last;
	int rc = HDMI_ERROR_SUCESS, trytimes = 0;
	struct rk30_hdmi_video_para video;
	
	mutex_lock(&work_mutex);
	/* Process hdmi command */
	hdmi->state = hdmi_process_command();
	
	if(!hdmi->enable || hdmi->suspend) {
		mutex_unlock(&work_mutex);
		return;
	}
	hotplug = rk30_hdmi_detect_hotplug();
	hdmi_dbg(hdmi->dev, "[%s] hotplug %02x curvalue %d\n", __FUNCTION__, hotplug, hdmi->hotplug);
	
	if(hotplug != hdmi->hotplug)
	{
		if(hotplug  == HDMI_HPD_ACTIVED){
			hdmi->hotplug  = hotplug;
			hdmi->state = READ_PARSE_EDID;
		}
		else if(hdmi->hotplug == HDMI_HPD_ACTIVED) {
			hdmi_sys_remove();
			hdmi->hotplug = hotplug;
			if(hotplug == HDMI_HPD_REMOVED)
				hdmi_sys_sleep();
			else {
				hdmi->state = WAIT_HOTPLUG;
				rk30_hdmi_removed();
			}
			if(hdmi->wait == 1) {
				complete(&hdmi->complete);
				hdmi->wait = 0;	
			}
			mutex_unlock(&work_mutex);
			return;
		}
		else if(hotplug == HDMI_HPD_REMOVED) {
			hdmi->state = HDMI_SLEEP;
			rk30_hdmi_removed();
		}
	}
	else if(hotplug == HDMI_HPD_REMOVED)
		hdmi_sys_sleep();
	
	do {
		hdmi_sys_show_state(hdmi->state);
		state_last = hdmi->state;
		switch(hdmi->state)
		{
			case READ_PARSE_EDID:
				rc = hdmi_sys_parse_edid(hdmi);
				if(rc == HDMI_ERROR_SUCESS)
				{
					hdmi->state = SYSTEM_CONFIG;	
					kobject_uevent_env(&hdmi->dev->kobj, KOBJ_ADD, envp);
					#ifdef CONFIG_SWITCH
					switch_set_state(&(hdmi->switch_hdmi), 1);
					#endif
					#ifdef CONFIG_HDMI_RK30_CTL_CODEC
					codec_set_spk(0);
					#endif
				}
				break;
			case SYSTEM_CONFIG:
				if(hdmi->autoconfig)	
					hdmi->vic = hdmi_find_best_mode(hdmi, 0);
				else
					hdmi->vic = hdmi_find_best_mode(hdmi, hdmi->vic);
				rc = hdmi_switch_fb(hdmi, hdmi->vic);
				if(rc == HDMI_ERROR_SUCESS)
					hdmi->state = CONFIG_VIDEO;
				break;
			case CONFIG_VIDEO:
				hdmi->display = HDMI_DISABLE;
				video.vic = hdmi->vic;
				video.input_mode = VIDEO_INPUT_RGB_YCBCR_444;
				video.input_color = VIDEO_INPUT_COLOR_RGB;//VIDEO_INPUT_COLOR_YCBCR
				video.output_mode = hdmi->edid.sink_hdmi;
				if(hdmi->edid.ycbcr444)
					video.output_color = VIDEO_OUTPUT_YCBCR444;
				else if(hdmi->edid.ycbcr422)
					video.output_color = VIDEO_OUTPUT_YCBCR422;
				else
					video.output_color = VIDEO_OUTPUT_RGB444;
				rc = rk30_hdmi_config_video(&video);
				if(rc == HDMI_ERROR_SUCESS)
				{
					if(hdmi->edid.sink_hdmi)
						hdmi->state = CONFIG_AUDIO;
					else
						hdmi->state = PLAY_BACK;
				}
				break;
			case CONFIG_AUDIO:
				rc = rk30_hdmi_config_audio(&(hdmi->audio));
							
				if(rc == HDMI_ERROR_SUCESS)
					hdmi->state = PLAY_BACK;
				break;
			case PLAY_BACK:
				if(hdmi->display != HDMI_ENABLE) {
					rk30_hdmi_control_output(HDMI_ENABLE);
					hdmi->display = HDMI_ENABLE;
					if(hdmi->hdcp_cb) {
						hdmi->hdcp_cb();
					}
				}
				
				if(hdmi->wait == 1) {	
					complete(&hdmi->complete);
					hdmi->wait = 0;						
				}
				break;
			default:
				break;
		}
		if(rc != HDMI_ERROR_SUCESS)
		{
			trytimes++;
			msleep(10);
		}
		if(hdmi->state != state_last) 
			trytimes = 0;
	
	}while((hdmi->state != state_last || (rc != HDMI_ERROR_SUCESS) ) && trytimes < HDMI_MAX_TRY_TIMES);
	
//	if(trytimes == HDMI_MAX_TRY_TIMES)
//	{
//		if(hdmi->hotplug) {
//			hdmi_sys_remove();
//			hdmi->hotplug = HDMI_HPD_REMOVED;
//			hdmi_sys_sleep();
//
//		}
//	}
	hdmi_dbg(hdmi->dev, "[%s] done\n", __FUNCTION__);
	mutex_unlock(&work_mutex);
}