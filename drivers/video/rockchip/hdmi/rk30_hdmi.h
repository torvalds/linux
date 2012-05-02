#ifndef __RK30_HDMI_H__
#define __RK30_HDMI_H__

#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/display-sys.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include<linux/rk_screen.h>
#include <linux/rk_fb.h>
#include "rk_hdmi.h"

// HDMI video source
enum {
	HDMI_SOURCE_LCDC0 = 0,
	HDMI_SOURCE_LCDC1 = 1
};

/* default HDMI video source */
#define HDMI_SOURCE_DEFAULT		HDMI_SOURCE_LCDC1

/* If HDMI_ENABLE, system will auto configure output mode according to EDID 
 * If HDMI_DISABLE, system will output mode according to macro HDMI_VIDEO_DEFAULT_MODE
 */
#define HDMI_AUTO_CONFIGURE			HDMI_ENABLE

/* default HDMI output video mode */
#define HDMI_VIDEO_DEFAULT_MODE			HDMI_1280x720p_60Hz//HDMI_1920x1080p_60Hz
/* default HDMI output audio mode */
#define HDMI_AUDIO_DEFAULT_CHANNEL		2
#define HDMI_AUDIO_DEFAULT_RATE			HDMI_AUDIO_FS_44100
#define HDMI_AUDIO_DEFAULT_WORD_LENGTH	HDMI_AUDIO_WORD_LENGTH_16bit

struct hdmi {
	struct device	*dev;
	struct clk		*hclk;				//HDMI AHP clk
	int 			regbase;
	int				irq;
	struct rk_lcdc_device_driver *lcdc;
	
	#ifdef CONFIG_SWITCH
	struct switch_dev	switch_hdmi;
	#endif
	
	struct workqueue_struct *workqueue;
	struct delayed_work delay_work;
	
	spinlock_t	irq_lock;
	struct mutex enable_mutex;
	
	int wait;
	struct completion	complete;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	int suspend;
	struct early_suspend	early_suspend;
#endif
	
	struct hdmi_edid edid;
	int enable;					// Enable HDMI output or not
	int vic;					// HDMI output video mode code
	struct hdmi_audio audio;	// HDMI output audio type.
	
	int pwr_mode;				// power mode
	int hotplug;				// hot plug status
	int state;					// hdmi state machine status
	int autoconfig;				// if true, auto config hdmi output mode according to EDID.
	int command;				// HDMI configuration command
	int display;				// HDMI display status	
};

extern struct hdmi *hdmi;

extern int hdmi_sys_init(void);
extern int hdmi_sys_parse_edid(struct hdmi* hdmi);
extern const char *hdmi_get_video_mode_name(unsigned char vic);
extern int hdmi_videomode_to_vic(struct fb_videomode *vmode);
extern const struct fb_videomode* hdmi_vic_to_videomode(int vic);
extern int hdmi_add_videomode(const struct fb_videomode *mode, struct list_head *head);
extern struct hdmi_video_timing * hdmi_find_mode(int vic);
extern int hdmi_find_best_mode(struct hdmi* hdmi, int vic);
extern int hdmi_ouputmode_select(struct hdmi *hdmi, int edid_ok);
extern int hdmi_switch_fb(struct hdmi *hdmi, int vic);
extern void hdmi_sys_remove(void);
#endif /* __RK30_HDMI_H__ */