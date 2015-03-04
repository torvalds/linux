#ifndef _RK1000_TVE_H
#define _RK1000_TVE_H
#include <linux/fb.h>
#include <linux/rk_fb.h>
#include <linux/display-sys.h>
#include <linux/mfd/rk1000.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/******************* TVOUT OUTPUT TYPE **********************/
struct rk1000_monspecs {
	struct rk_display_device *ddev;
	unsigned int enable;
	unsigned int suspend;
	struct fb_videomode *mode;
	struct list_head modelist;
	unsigned int mode_set;
};

struct rk1000_tve {
	struct device *dev;
	struct i2c_client *client;
	#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	#endif
	struct ioctrl io_switch;
	int video_source;
	int property;
	int mode;
	struct rk1000_monspecs *cvbs;
	struct rk1000_monspecs *ypbpr;
};

extern struct rk1000_tve rk1000_tve;

enum {
	TVOUT_CVBS_NTSC = 1,
	TVOUT_CVBS_PAL,
	TVOUT_YPBPR_720X480P_60,
	TVOUT_YPBPR_720X576P_50,
	TVOUT_YPBPR_1280X720P_50,
	TVOUT_YPBPR_1280X720P_60
};

enum {
	RK1000_TVOUT_CVBS = 0,
	RK1000_TVOUT_YC,
	RK1000_TVOUT_YPBPR,
};
#ifdef CONFIG_RK1000_TVOUT_CVBS
#define RK1000_TVOUT_DEAULT TVOUT_CVBS_NTSC
#else
#define RK1000_TVOUT_DEAULT TVOUT_YPBPR_1280X720P_60
#endif

int rk1000_control_write_block(u8 reg, u8 *buf, u8 len);
int rk1000_tv_write_block(u8 reg, u8 *buf, u8 len);
int rk1000_tv_standby(int type);
int rk1000_switch_fb(const struct fb_videomode *modedb, int tv_mode);
int rk1000_register_display(struct device *parent);

#ifdef CONFIG_RK1000_TVOUT_YPBPR
int rk1000_tv_ypbpr480_init(void);
int rk1000_tv_ypbpr576_init(void);
int rk1000_tv_ypbpr720_50_init(void);
int rk1000_tv_ypbpr720_60_init(void);
int rk1000_register_display_ypbpr(struct device *parent);
#endif

#ifdef CONFIG_RK1000_TVOUT_CVBS
int rk1000_tv_ntsc_init(void);
int rk1000_tv_pal_init(void);
int rk1000_register_display_cvbs(struct device *parent);
#endif

#endif

