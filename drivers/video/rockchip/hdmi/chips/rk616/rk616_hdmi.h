#ifndef __RK616_HDMI_H__
#define __RK616_HDMI_H__

#include "../../rk_hdmi.h"
#include <linux/mfd/rk616.h>

enum {
	INPUT_IIS,
	INPUT_SPDIF
};

#if defined(CONFIG_SND_RK_SOC_HDMI_SPDIF)
#define HDMI_CODEC_SOURCE_SELECT INPUT_SPDIF
#else
#define HDMI_CODEC_SOURCE_SELECT INPUT_IIS
#endif

extern void rk616_hdmi_control_output(struct hdmi *hdmi, int enable);
extern int rk616_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
					      void (*hdcp_irq_cb)(int status),
					      int (*hdcp_power_on_cb)(void),
					      void (*hdcp_power_off_cb)(void));

struct rk_hdmi_device {
	int clk_on;
	spinlock_t reg_lock;
	struct hdmi driver;
	void __iomem *regbase;
	int regbase_phy;
	int regsize_phy;
	struct clk *pd;
	struct clk *hclk;	/* HDMI AHP clk */
	struct clk *pclk;	/* HDMI APB clk */
	struct delayed_work rk616_delay_work;
	struct work_struct rk616_irq_work_struct;
	struct mfd_rk616 *rk616_drv;
	struct dentry *debugfs_dir;
};

#endif /* __RK616_HDMI_H__ */
