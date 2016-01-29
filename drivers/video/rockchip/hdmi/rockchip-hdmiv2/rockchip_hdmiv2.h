#ifndef __RK32_HDMI_H__
#define __RK32_HDMI_H__
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include "../rockchip-hdmi.h"

#ifdef DEBUG
#define HDMIDBG(format, ...) \
		pr_info(format, ## __VA_ARGS__)
#else
#define HDMIDBG(format, ...)
#endif

struct hdmi_dev_phy_para {
	u32 maxfreq;
	int pre_emphasis;
	int slopeboost;
	int clk_level;
	int data0_level;
	int data1_level;
	int data2_level;
};

struct hdmi_dev {
	void __iomem		*regbase;
	void __iomem		*phybase;
	struct regmap		*grf_base;
	int			grf_reg_offset;
	int			grf_reg_shift;
	struct reset_control	*reset;
	struct clk		*pd;
	struct clk		*pclk;
	struct clk		*hdcp_clk;
	struct clk		*cec_clk;
	struct clk		*pclk_phy;
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
	int			hdcp2_enable;
	unsigned char		clk_disable;
	unsigned char		clk_on;

	unsigned long		pixelclk;
	unsigned int		tmdsclk;
	unsigned int		pixelrepeat;
	unsigned char		colordepth;

	bool			tmdsclk_ratio_change;
	struct mutex		ddc_lock;	/*mutex for ddc operation */

	void			(*hdcp2_en)(int);
	void			(*hdcp2_reset)(void);
	void			(*hdcp2_start)(void);

	struct hdmi_dev_phy_para *phy_table;
	int			phy_table_size;
	const char		*vendor_name;
	const char		*product_name;
	unsigned char		deviceinfo;
};

void ext_pll_set_27m_out(void);

#endif /*__RK32_HDMI_H__*/
