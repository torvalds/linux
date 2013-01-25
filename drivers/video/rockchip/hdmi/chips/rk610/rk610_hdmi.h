#ifndef __RK610_HDMI_H__
#define __RK610_HDMI_H__
#include "../../rk_hdmi.h"

#if defined(CONFIG_HDMI_SOURCE_LCDC1)
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC1
#else
#define HDMI_SOURCE_DEFAULT HDMI_SOURCE_LCDC0
#endif
//#define HDMI_USE_IRQ

struct rk610_hdmi_pdata {
	int gpio;
	struct i2c_client *client;
	struct delayed_work delay_work;
	#ifdef HDMI_USE_IRQ
	struct work_struct	irq_work;
	#else
	struct workqueue_struct *workqueue;
	#endif
};

extern struct rk610_hdmi_pdata *rk610_hdmi;

extern int rk610_hdmi_sys_init(void);
extern void rk610_hdmi_interrupt(void);
extern int rk610_hdmi_sys_detect_hpd(void);
extern int rk610_hdmi_sys_insert(void);
extern int rk610_hdmi_sys_remove(void);
extern int rk610_hdmi_sys_read_edid(int block, unsigned char *buff);
extern int rk610_hdmi_sys_config_video(struct hdmi_video_para *vpara);
extern int rk610_hdmi_sys_config_audio(struct hdmi_audio *audio);
extern void rk610_hdmi_sys_enalbe_output(int enable);
extern int rk610_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					 void (*hdcp_irq_cb)(int status),
					 int (*hdcp_power_on_cb)(void),
					 void (*hdcp_power_off_cb)(void));
#endif
