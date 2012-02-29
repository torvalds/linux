#include <linux/ctype.h>
#include <linux/hdmi.h>
#include <linux/string.h>

int debug_en = 0;

#ifndef CONFIG_HDMI_SAVE_DATA
int hdmi_get_data(void)
{
return 0;
}
int hdmi_set_data(int data)
{
return 0;
}
#endif

static ssize_t hdmi_show_state_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf) 
{
	struct hdmi *hdmi = dev_get_drvdata(dev);

	return sprintf(buf, "display_on=%d\n"
						"plug=%d\n"
						"dual_disp=%d\n"
						"video_mode=%d\n"
						"mode=%d\n"
						"hdcp_on=%d\n"
						"audio_fs=%d\n"
						"scale=%d\n"
						"scale_set=%d\n"
						"resolution=%d\n"
						"--------------------------\n"
						"resolution support:\n"
						"HDMI_1920x1080p_50Hz       0\n"
						"HDMI_1920x1080p_60Hz       1\n"
						"HDMI_1280x720p_50Hz        2\n"
						"HDMI_1280x720p_60Hz        3\n"
						"HDMI_720x576p_50Hz_4x3     4\n"
						"HDMI_720x576p_50Hz_16x9    5\n"
						"HDMI_720x480p_60Hz_4x3     6\n"
						"HDMI_720x480p_60Hz_16x9    7\n"
						"--------------------------\n", 
						hdmi->display_on,hdmi->ops->hdmi_precent(hdmi),
						hdmi->dual_disp,fb_get_video_mode(), hdmi->mode, hdmi->hdcp_on,
						hdmi->audio_fs, (hdmi->ops->hdmi_precent(hdmi) && hdmi->display_on)?hdmi->scale:100, 
						hdmi->scale_set,
						hdmi->resolution);
}
static ssize_t hdmi_restore_state_attrs(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	int ret = 0;
	struct hdmi *hdmi = dev_get_drvdata(dev);
	char *p;
	const char *q;
	int set_param = 0, tmp = 0;
	#ifdef CONFIG_HDMI_SAVE_DATA
    int hdmi_data=0;
    #endif
	if(hdmi->mode == DISP_ON_LCD)
	{
		dev_err(dev, "display on lcd, do not set parameter!\n");
		ret = -EINVAL;
		goto exit;
	}

	q = buf;
	do
	{
		if((p = strstr(q, "mode=")) != NULL)
		{
			q = p + 5;
#if 0
			if((sscanf(q, "%d", &tmp) == 1) && (tmp >= 0 && tmp <= 3))
			{
				if(tmp != hdmi->mode)
				{
					set_param |= 1;
					hdmi->mode = tmp;
				}
			}
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
#endif
		}

		else if((p = strstr(q, "hdcp_on=")) != NULL)
		{
			q = p + 8;
#if 0
			if((sscanf(q, "%d", &tmp) == 1) && (tmp == 0 || tmp ==1))
			{
				if(tmp != hdmi->hdcp_on)
				{
					set_param |= 1;
					hdmi->hdcp_on = tmp;
				}
			}
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
#endif
		}


		else if((p = strstr(q, "scale_set=")) != NULL)
		{
			q = p + 10;
			if((sscanf(q, "%d", &tmp) == 1) && (tmp >=MIN_SCALE && tmp <= 100))
			{
				hdmi->scale_set = tmp;
				hdmi_dbg(dev, "set scale = %d\n", tmp);
				hdmi->scale = tmp;
				#ifdef CONFIG_HDMI_SAVE_DATA
				hdmi_data = hdmi_get_data();
				if(hdmi_data<0)
				    hdmi->ops->init(hdmi);
				hdmi_data = (((hdmi->scale-MIN_SCALE)&0x1f)<<3) | (hdmi_data & 0x7);
				hdmi_set_data(hdmi_data);
				#endif
			}
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
		}
		else if((p = strstr(q, "resolution=")) != NULL)
		{
			q = p + 11;
			if((sscanf(q, "%d", &tmp) == 1) && (tmp >= 0))
			{
				if(hdmi->resolution != tmp)
				{
					set_param |= 1;
					hdmi_dbg(dev, "set resolution = %d\n", tmp);
					hdmi->resolution = tmp;
					#ifdef CONFIG_HDMI_SAVE_DATA
					hdmi_data = hdmi_get_data();
					if(hdmi_data<0)
				        hdmi->ops->init(hdmi);
					hdmi_data = (hdmi->resolution&0x7) | (hdmi_data & 0xf8);
					hdmi_set_data(hdmi_data);
					#endif
				}
			}
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
		}
		else
			break;
		
	}while(*q != 0);
	if(hdmi->ops->set_param && set_param != 0)
	{
		mutex_lock(&hdmi->lock);
		ret = hdmi->ops->set_param(hdmi);
		mutex_unlock(&hdmi->lock);
	}
exit:
	if(ret < 0)
		dev_err(dev, "hdmi_restore_state_attrs err\n");
	return size;
}

static ssize_t hdmi_show_switch_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf) 
{				 
	struct hdmi *hdmi = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdmi->display_on);
}
static ssize_t hdmi_restore_switch_attrs(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	int display_on = 0;
	struct hdmi *hdmi = dev_get_drvdata(dev);
	
	sscanf(buf, "%d", &display_on);
	hdmi_dbg(dev, "hdmi %s\n", (display_on)?"enable":"disable");
	if(display_on ^ hdmi->display_on)
	{
		hdmi->display_on = display_on;
		hdmi_changed(hdmi, 1);
	}
	return size;
}
static ssize_t hdmi_show_debug_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf) 
{				 
	return sprintf(buf, "%d\n", debug_en);
}
static ssize_t hdmi_restore_debug_attrs(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	int tmp;
	
	sscanf(buf, "%d", &tmp);
	
	if(tmp != 0 && tmp != 1)
		dev_err(dev, "hdmi_restore_debug_attrs err\n");
	else
		debug_en = tmp;
	return size;
}
#if 0
static ssize_t hdmi_restore_init_attrs(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	int enable = HDMI_DISABLE, scale = 100, resolution = HDMI_DEFAULT_RESOLUTION;
	struct hdmi *hdmi = dev_get_drvdata(dev);

	sscanf(buf, "%d %d %d\n", &enable, &scale, &resolution);
	
	hdmi_dbg(dev, "hdmi init, set param: enable = %d, scale = %d, resolution = %d\n",
			enable, scale, resolution);

	hdmi->display_on = enable;
	hdmi->resolution = resolution;
	hdmi->scale_set = scale;
	
	if(hdmi->ops->hdmi_precent(hdmi) && hdmi->display_on)
		hdmi->scale = scale;

	if(hdmi->ops->init)
		hdmi->ops->init(hdmi);
	return size;
}
#endif
static struct device_attribute hdmi_attrs[] = {
	__ATTR(state, 0774, hdmi_show_state_attrs, hdmi_restore_state_attrs),
	__ATTR(enable, 0774, hdmi_show_switch_attrs, hdmi_restore_switch_attrs),
	__ATTR(debug, 0774, hdmi_show_debug_attrs, hdmi_restore_debug_attrs),
	//__ATTR(init, 0777, NULL, hdmi_restore_init_attrs),
};
int hdmi_create_attrs(struct hdmi *hdmi)
{
	int rc = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_attrs); i++) {
		rc = device_create_file(hdmi->dev, &hdmi_attrs[i]);
		if (rc)
			goto create_failed;
	}

	goto succeed;

create_failed:
	while (i--)
		device_remove_file(hdmi->dev, &hdmi_attrs[i]);
succeed:
	return rc;
}

void hdmi_remove_attrs(struct hdmi *hdmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_attrs); i++)
		device_remove_file(hdmi->dev, &hdmi_attrs[i]);
}


