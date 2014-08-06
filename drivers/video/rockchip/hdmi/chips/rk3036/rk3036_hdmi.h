#ifndef __RK3036_HDMI_H__
#define __RK3036_HDMI_H__

#include "../../rk_hdmi.h"

enum {
	INPUT_IIS,
	INPUT_SPDIF
};


#if defined(CONFIG_SND_RK_SOC_HDMI_SPDIF)
#define HDMI_CODEC_SOURCE_SELECT INPUT_SPDIF
#else
#define HDMI_CODEC_SOURCE_SELECT INPUT_IIS
#endif

extern void rk3036_hdmi_control_output(struct hdmi *hdmi, int enable);
extern int rk3036_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					      void (*hdcp_irq_cb)(int status),
					      int (*hdcp_power_on_cb)(void),
					      void (*hdcp_power_off_cb)(void));
extern int rk3036_hdmi_register_cec_callbacks(void (*cec_irq)(void),
						      void (*cec_set_device_pa)(int addr),
						      int (*cec_enumerate)(void));


extern struct rk_hdmi_device *hdmi_dev;

struct rk_hdmi_device {
	int clk_on;
	spinlock_t reg_lock;
	struct hdmi driver;
	void __iomem *regbase;
	int regbase_phy;
	int regsize_phy;
	struct clk *pd;
	struct clk *hclk;	/* HDMI AHP clk */
	struct delayed_work rk3036_delay_work;
	struct work_struct rk3036_irq_work_struct;
	struct dentry *debugfs_dir;
	unsigned int hclk_rate;
};

#endif /* __RK3036_HDMI_H__ */
