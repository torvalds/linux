#include <linux/interrupt.h>
#include "rk3288_hdmi_hw.h"

int rk3288_hdmi_detect_hotplug(struct hdmi *hdmi_drv)
{
	return 0;
}

int rk3288_hdmi_read_edid(struct hdmi *hdmi_drv, int block, unsigned char *buff)
{
	return 0;
}

int rk3288_hdmi_config_video(struct hdmi *hdmi_drv, struct hdmi_video_para *vpara)
{
	return 0;
}

int rk3288_hdmi_config_audio(struct hdmi *hdmi_drv, struct hdmi_audio *audio)
{
	return 0;
}

void rk3288_hdmi_control_output(struct hdmi *hdmi_drv, int enable)
{

}

int rk3288_hdmi_removed(struct hdmi *hdmi_drv)
{
	return 0;
}

int rk3288_hdmi_initial(struct hdmi *hdmi_drv)
{
        int rc = HDMI_ERROR_SUCESS;

        hdmi_drv->remove = rk3288_hdmi_removed;
        hdmi_drv->control_output = rk3288_hdmi_control_output;
        hdmi_drv->config_video = rk3288_hdmi_config_video;
        hdmi_drv->config_audio = rk3288_hdmi_config_audio;
        hdmi_drv->detect_hotplug = rk3288_hdmi_detect_hotplug;
        hdmi_drv->read_edid = rk3288_hdmi_read_edid;

        return rc;
}

irqreturn_t hdmi_irq(int irq, void *priv)
{
	return IRQ_HANDLED;
}

