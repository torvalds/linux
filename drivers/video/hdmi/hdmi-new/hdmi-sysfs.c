#include <linux/ctype.h>
#include <linux/hdmi-new.h>
#include <linux/string.h>


static ssize_t hdmi_show_state_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf) 
{
	struct hdmi *hdmi = dev_get_drvdata(dev);

	return sprintf(buf, "display_on=%d\n"
						"plug=%d\n"
						"auto_switch=%d\n"
						"hdcp_on=%d\n"
						"audio_fs=%d\n"
						"resolution=%d\n"
						"--------------------------\n"
						"resolution support:\n"
						"HDMI_1280x720p_50Hz        0\n"
						"HDMI_1280x720p_60Hz        1\n"
						"HDMI_720x576p_50Hz_4x3     2\n"
						"HDMI_720x576p_50Hz_16x9    3\n"
						"HDMI_720x480p_60Hz_4x3     4\n"
						"HDMI_720x480p_60Hz_16x9    5\n"
						"HDMI_1920x1080p_50Hz       6\n"
						"HDMI_1920x1080p_60Hz       7\n"
						"--------------------------\n", 
						hdmi->display_on,hdmi->ops->hdmi_precent(hdmi),
						hdmi->auto_switch, hdmi->hdcp_on,
						hdmi->audio_fs, hdmi->resolution);
}
static ssize_t hdmi_restore_state_attrs(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	int ret = 0;
	struct hdmi *hdmi = dev_get_drvdata(dev);
	char *p;
	const char *q;
	int auto_switch = -1, hdcp_on = -1, audio_fs = -1, resolution = -1;
	
	q = buf;
	do
	{
		if((p = strstr(q, "auto_switch=")) != NULL)
		{
			q = p + 12;
			if((sscanf(q, "%d", &auto_switch) == 1) &&
			   (auto_switch == 0 || auto_switch == 1))
				hdmi->auto_switch = auto_switch;
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
		}
		else if((p = strstr(q, "hdcp_on=")) != NULL)
		{
			q = p + 8;
			if((sscanf(q, "%d", &hdcp_on) == 1) &&
			   (hdcp_on == 0 || hdcp_on == 1))
				hdmi->hdcp_on = hdcp_on;
			else
			{
				dev_err(dev, "failed to set hdmi configuration\n");
				ret = -EINVAL;
				goto exit;
			}
		}
		else if((p = strstr(q, "audio_fs=")) != NULL)
		{
			q = p + 9;
			if((sscanf(q, "%d", &audio_fs) == 1) &&
			   (audio_fs >= 0))
				hdmi->audio_fs = audio_fs;
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
			if((sscanf(q, "%d", &resolution) == 1) &&
			   (resolution >= 0))
				hdmi->resolution = resolution;
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
	if(auto_switch == -1 &&
	   hdcp_on == -1 &&
	   audio_fs == -1 &&
	   resolution == -1)
	{
		dev_err(dev, "failed to set hdmi configuration\n");
		ret = -EINVAL;
		goto exit;
	}
	if(hdmi->ops->set_param)
		ret = hdmi->ops->set_param(hdmi);
	else
	{
		dev_err(dev, "hdmi device is not exist\n");
		return ret = 0;
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
	int display_on = 0, ret = 0;
	struct hdmi *hdmi = dev_get_drvdata(dev);
	
	sscanf(buf, "%d", &display_on);

	if(hdmi->display_on == HDMI_DISABLE && display_on)
		ret = hdmi->ops->display_on(hdmi);
	else if(hdmi->display_on == HDMI_ENABLE && !display_on)
		ret = hdmi->ops->display_off(hdmi);

	if(ret < 0)
		dev_err(dev, "hdmi_restore_switch_attrs err\n");
	return size;
}
static struct device_attribute hdmi_attrs[] = {
	__ATTR(state, 0664, hdmi_show_state_attrs, hdmi_restore_state_attrs),
	__ATTR(enable, 0664, hdmi_show_switch_attrs, hdmi_restore_switch_attrs),
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


