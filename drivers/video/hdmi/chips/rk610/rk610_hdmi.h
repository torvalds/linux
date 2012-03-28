#ifndef _RK610_H
#define _RK610_H

#include <linux/hdmi.h>
#include <linux/earlysuspend.h>

/************RK610 device addr***********/
#define RK610_CTRL_ADDR     0x40
#define RK610_TVE_ADDR      0x42
#define RK610_HDMI_ADDR     0x46
#define RK610_CODEC_ADDR    0xc0 // 0x11xxxxxx


/****************HDMI STRUCT********************************/


struct rk610_hdmi_inf{
    int irq;
	int gpio;
	int init;
	struct i2c_client *client;
	struct hdmi *hdmi;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		early_suspend;
#endif
};

/******************TVE STRUCT **************/

/*************RK610 STRUCT**********************************/
//struct rk610_pdata {
//    struct rk610_hdmi_inf hdmi;
//	struct rk610_lcd_info lcd;
//};
/*****************END ***********************************/
#endif
