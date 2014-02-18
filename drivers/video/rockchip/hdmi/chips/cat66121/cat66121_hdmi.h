#ifndef __cat66121_HDMI_H__
#define __cat66121_HDMI_H__
#include "../../rk_hdmi.h"

#if defined(CONFIG_HDMI_SOURCE_LCDC1)
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC1
#else
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC0
#endif


struct cat66121_hdmi_pdata {
	int gpio;
	struct i2c_client *client;
	struct delayed_work delay_work;
	struct workqueue_struct *workqueue;
	int plug_status;
};

extern struct cat66121_hdmi_pdata *cat66121_hdmi;

extern int cat66121_detect_device(void);
extern int cat66121_hdmi_sys_init(struct hdmi *hdmi_drv);
extern void cat66121_hdmi_interrupt(struct hdmi *hdmi_drv);
extern int cat66121_hdmi_sys_detect_hpd(struct hdmi *hdmi_drv);
extern int cat66121_hdmi_sys_insert(struct hdmi *hdmi_drv);
extern int cat66121_hdmi_sys_remove(struct hdmi *hdmi_drv);
extern int cat66121_hdmi_sys_read_edid(struct hdmi *hdmi_drv, int block, unsigned char *buff);
extern int cat66121_hdmi_sys_config_video(struct hdmi *hdmi_drv, struct hdmi_video_para *vpara);
extern int cat66121_hdmi_sys_config_audio(struct hdmi *hdmi_drv,struct hdmi_audio *audio);
extern void cat66121_hdmi_sys_enalbe_output(struct hdmi *hdmi_drv, int enable);
extern int cat66121_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));
#endif
