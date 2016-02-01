#ifndef __ROCKCHIP_HDMI_V1_H__
#define __ROCKCHIP_HDMI_V1_H__

#include "../rockchip-hdmi.h"

struct hdmi_dev {
	void __iomem		*regbase;
	int			regbase_phy;
	int			regsize_phy;

	struct clk		*pd;
	struct clk		*hclk;
	unsigned int		hclk_rate;

	struct hdmi		*hdmi;
	struct device		*dev;
	struct dentry		*debugfs_dir;
	int			irq;

	struct work_struct	irq_work;
	struct delayed_work	delay_work;
	struct workqueue_struct *workqueue;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	early_suspend;
#endif
	int			soctype;
	int			audiosrc;
	int			enable;
	unsigned char		clk_disable;
	unsigned char		clk_on;
	spinlock_t		reg_lock;	/* lock for clk */

	unsigned int		tmdsclk;
	unsigned int		pixelrepeat;
	int			pwr_mode;
};
#endif /* __RK3036_HDMI_H__ */
