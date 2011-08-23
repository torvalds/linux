#ifndef _ANX7150_H
#define _ANX7150_H

#include <linux/hdmi.h>
#include <linux/earlysuspend.h>


#define ANX7150_I2C_ADDR0		0X39
#define ANX7150_I2C_ADDR1		0X3d

#define ANX7150_SCL_RATE 100 * 1000


/* HDMI auto switch */
#define HDMI_AUTO_SWITCH HDMI_ENABLE

/* HDMI reciver status */
#define HDMI_RECIVER_INACTIVE 0
#define HDMI_RECIVER_ACTIVE   1

/* ANX7150 reciver HPD Status */
#define HDMI_RECIVER_UNPLUG 0
#define HDMI_RECIVER_PLUG   1

#define LCD  0
#define HDMI 1

#define RK29_OUTPUT_STATUS_LCD     LCD
#define RK29_OUTPUT_STATUS_HDMI    HDMI

/* HDMI HDCP ENABLE */
#define ANX7150_HDCP_EN  HDMI_DISABLE

/* ANX7150 state machine */
enum{
	HDMI_INITIAL = 1,
	WAIT_HOTPLUG,
	READ_PARSE_EDID,
	WAIT_RX_SENSE,
	WAIT_HDMI_ENABLE,
	SYSTEM_CONFIG,
	CONFIG_VIDEO,
	CONFIG_AUDIO,
	CONFIG_PACKETS,
	HDCP_AUTHENTICATION,
	PLAY_BACK,
	RESET_LINK,
	UNKNOWN,
};


struct anx7150_dev_s{
	struct i2c_driver *i2c_driver;
	struct fasync_struct *async_queue;
	struct workqueue_struct *workqueue;
	struct delayed_work delay_work;
	struct miscdevice *mdev;
	void (*notifier_callback)(struct anx7150_dev_s *);
	int anx7150_detect;
	int resolution_set;
	int resolution_real;
	int i2s_Fs;
	int hdmi_enable;
	int hdmi_auto_switch;
	int reciver_status;
	int HPD_change_cnt;
	int HPD_status;
	int rk29_output_status;
	int hdcp_enable;
	int parameter_config;
	int rate;
	int fb_switch_state;

	struct hdmi *hdmi;
};

struct anx7150_pdata {
	int irq;
	int gpio;
	int init;
	int is_early_suspend;
	int is_changed;
	struct delayed_work		work;
	struct hdmi *hdmi;
	struct i2c_client *client;
	struct anx7150_dev_s dev;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		early_suspend;
#endif
};



int anx7150_i2c_read_p0_reg(struct i2c_client *client, char reg, char *val);
int anx7150_i2c_write_p0_reg(struct i2c_client *client, char reg, char *val);
int anx7150_i2c_read_p1_reg(struct i2c_client *client, char reg, char *val);
int anx7150_i2c_write_p1_reg(struct i2c_client *client, char reg, char *val);

#endif
