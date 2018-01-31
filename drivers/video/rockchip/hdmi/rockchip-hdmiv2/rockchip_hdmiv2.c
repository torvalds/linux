/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <linux/mfd/syscon.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"

static struct hdmi_dev *hdmi_dev;

static struct hdmi_property rk_hdmi_property = {
	.videosrc = DISPLAY_SOURCE_LCDC0,
	.display = DISPLAY_MAIN,
};

#if defined(CONFIG_DEBUG_FS)
static const struct rockchip_hdmiv2_reg_table hdmi_reg_table[] = {
	{IDENTIFICATION_BASE, CONFIG3_ID},
	{INTERRUPT_BASE, IH_MUTE},
	{VIDEO_SAMPLER_BASE, TX_BCBDATA1},
	{VIDEO_PACKETIZER_BASE, VP_MASK},
	{FRAME_COMPOSER_BASE, FC_DBGTMDS2},
	{HDMI_SOURCE_PHY_BASE, PHY_PLLCFGFREQ2},
	{I2C_MASTER_PHY_BASE, PHY_I2CM_SDA_HOLD},
	{AUDIO_SAMPLER_BASE, AHB_DMA_STPADDR_SET1_0},
	{MAIN_CONTROLLER_BASE, MC_SWRSTZREQ_2},
	{COLOR_SPACE_CONVERTER_BASE, CSC_SPARE_2},
	{HDCP_ENCRYPTION_ENGINE_BASE, HDCP_REVOC_LIST},
	{HDCP_BKSV_BASE, HDCPREG_BKSV4},
	{HDCP_AN_BASE, HDCPREG_AN7},
	{HDCP2REG_BASE, HDCP2REG_MUTE},
	{ENCRYPTED_DPK_EMBEDDED_BASE, HDCPREG_DPK6},
	{CEC_ENGINE_BASE, CEC_WKUPCTRL},
	{I2C_MASTER_BASE, I2CM_SCDC_UPDATE1},
};

static int hdmi_regs_ctrl_show(struct seq_file *s, void *v)
{
	u32 i = 0, j = 0, val = 0;

	seq_puts(s, "\n>>>hdmi_ctl reg ");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);
	seq_puts(s, "\n-----------------------------------------------------------------");

	for (i = 0; i < ARRAY_SIZE(hdmi_reg_table); i++) {
		for (j = hdmi_reg_table[i].reg_base;
		     j <= hdmi_reg_table[i].reg_end; j++) {
			val = hdmi_readl(hdmi_dev, j);
			if ((j - hdmi_reg_table[i].reg_base) % 16 == 0)
				seq_printf(s, "\n>>>hdmi_ctl %04x:", j);
			seq_printf(s, " %02x", val);
		}
	}
	seq_puts(s, "\n-----------------------------------------------------------------\n");

	return 0;
}

static ssize_t hdmi_regs_ctrl_write(struct file *file,
				    const char __user *buf,
				    size_t count, loff_t *ppos)
{
	u32 reg, val;
	char kbuf[25];

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	if ((reg < 0) || (reg > I2CM_SCDC_UPDATE1)) {
		dev_info(hdmi_dev->hdmi->dev, "it is no hdmi reg\n");
		return count;
	}
	dev_info(hdmi_dev->hdmi->dev,
		 "/**********hdmi reg config******/");
	dev_info(hdmi_dev->hdmi->dev, "\n reg=%x val=%x\n", reg, val);
	hdmi_writel(hdmi_dev, reg, val);

	return count;
}

static int hdmi_regs_phy_show(struct seq_file *s, void *v)
{
	u32 i, count;

	if (hdmi_dev->soctype == HDMI_SOC_RK322X)
		count = 0xff;
	else
		count = 0x28;
	seq_puts(s, "\n>>>hdmi_phy reg ");
	for (i = 0; i < count; i++)
		seq_printf(s, "regs %02x val %04x\n",
			   i, rockchip_hdmiv2_read_phy(hdmi_dev, i));
	return 0;
}

static ssize_t hdmi_regs_phy_write(struct file *file,
				   const char __user *buf,
				   size_t count, loff_t *ppos)
{
	u32 reg, val;
	char kbuf[25];

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	dev_info(hdmi_dev->hdmi->dev,
		 "/**********hdmi reg phy config******/");
	dev_info(hdmi_dev->hdmi->dev, "\n reg=%x val=%x\n", reg, val);
	rockchip_hdmiv2_write_phy(hdmi_dev, reg, val);
	return count;
}

#define HDMI_DEBUG_ENTRY(name) \
static int hdmi_##name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, hdmi_##name##_show, inode->i_private); \
} \
\
static const struct file_operations hdmi_##name##_fops = { \
	.owner = THIS_MODULE, \
	.open = hdmi_##name##_open, \
	.read = seq_read, \
	.write = hdmi_##name##_write,	\
	.llseek = seq_lseek, \
	.release = single_release, \
}

HDMI_DEBUG_ENTRY(regs_phy);
HDMI_DEBUG_ENTRY(regs_ctrl);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rockchip_hdmiv2_early_suspend(struct early_suspend *h)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;
	struct pinctrl_state *gpio_state;

	HDMIDBG(2, "hdmi enter early suspend\n");
	hdmi_submit_work(hdmi, HDMI_SUSPEND_CTL, 0, 1);
	/* iomux to gpio and pull down when suspend */
	gpio_state = pinctrl_lookup_state(hdmi_dev->dev->pins->p, "gpio");
	pinctrl_select_state(hdmi_dev->dev->pins->p, gpio_state);
	rockchip_hdmiv2_clk_disable(hdmi_dev);
}

static void rockchip_hdmiv2_early_resume(struct early_suspend *h)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;

	HDMIDBG(2, "hdmi exit early resume\n");
	/* iomux to default state for hdmi use when resume */
	pinctrl_select_state(hdmi_dev->dev->pins->p,
			     hdmi_dev->dev->pins->default_state);
	rockchip_hdmiv2_clk_enable(hdmi_dev);
	hdmi_dev_initial(hdmi_dev);
	if (hdmi->ops->hdcp_power_on_cb)
		hdmi->ops->hdcp_power_on_cb();
	hdmi_submit_work(hdmi, HDMI_RESUME_CTL, 0, 0);
}
#endif

void ext_pll_set_27m_out(void)
{
	if (!hdmi_dev || hdmi_dev->soctype != HDMI_SOC_RK322X)
		return;
	/* PHY PLL VCO is 1080MHz, output pclk is 27MHz */
	rockchip_hdmiv2_write_phy(hdmi_dev,
				  EXT_PHY_PLL_PRE_DIVIDER,
				  1);
	rockchip_hdmiv2_write_phy(hdmi_dev,
				  EXT_PHY_PLL_FB_DIVIDER,
				  45);
	rockchip_hdmiv2_write_phy(hdmi_dev,
				  EXT_PHY_PCLK_DIVIDER1,
				  0x61);
	rockchip_hdmiv2_write_phy(hdmi_dev,
				  EXT_PHY_PCLK_DIVIDER2,
				  0x64);
	rockchip_hdmiv2_write_phy(hdmi_dev,
				  EXT_PHY_TMDSCLK_DIVIDER,
				  0x1d);
}

static int rockchip_hdmiv2_clk_enable(struct hdmi_dev *hdmi_dev)
{
	if ((hdmi_dev->clk_on & HDMI_PD_ON) == 0) {
		pm_runtime_get_sync(hdmi_dev->dev);
		hdmi_dev->clk_on |= HDMI_PD_ON;
	}

	if (hdmi_dev->soctype == HDMI_SOC_RK322X ||
	    hdmi_dev->soctype == HDMI_SOC_RK3366 ||
	    hdmi_dev->soctype == HDMI_SOC_RK3399) {
		if ((hdmi_dev->clk_on & HDMI_EXT_PHY_CLK_ON) == 0) {
			if (!hdmi_dev->pclk_phy) {
				if (hdmi_dev->soctype == HDMI_SOC_RK322X)
					hdmi_dev->pclk_phy =
						devm_clk_get(hdmi_dev->dev,
							     "pclk_hdmi_phy");
				else
					hdmi_dev->pclk_phy =
						devm_clk_get(hdmi_dev->dev,
							     "dclk_hdmi_phy");
				if (IS_ERR(hdmi_dev->pclk_phy)) {
					dev_err(hdmi_dev->dev,
						"get hdmi phy pclk error\n");
					return -1;
				}
			}
			clk_prepare_enable(hdmi_dev->pclk_phy);
			hdmi_dev->clk_on |= HDMI_EXT_PHY_CLK_ON;
		}
	}
	if ((hdmi_dev->clk_on & HDMI_PCLK_ON) == 0) {
		if (!hdmi_dev->pclk) {
			hdmi_dev->pclk =
				devm_clk_get(hdmi_dev->dev, "pclk_hdmi");
			if (IS_ERR(hdmi_dev->pclk)) {
				dev_err(hdmi_dev->dev,
					"Unable to get hdmi pclk\n");
				return -1;
			}
		}
		clk_prepare_enable(hdmi_dev->pclk);
		hdmi_dev->clk_on |= HDMI_PCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_HDCPCLK_ON) == 0) {
		if (!hdmi_dev->hdcp_clk) {
			hdmi_dev->hdcp_clk =
				devm_clk_get(hdmi_dev->dev, "hdcp_clk_hdmi");
			if (IS_ERR(hdmi_dev->hdcp_clk)) {
				dev_err(hdmi_dev->dev,
					"Unable to get hdmi hdcp_clk\n");
				return -1;
			}
		}
		clk_prepare_enable(hdmi_dev->hdcp_clk);
		hdmi_dev->clk_on |= HDMI_HDCPCLK_ON;
	}

	if ((rk_hdmi_property.feature & SUPPORT_CEC) &&
	    (hdmi_dev->clk_on & HDMI_CECCLK_ON) == 0) {
		if (!hdmi_dev->cec_clk) {
			hdmi_dev->cec_clk =
				devm_clk_get(hdmi_dev->dev, "cec_clk_hdmi");
			if (IS_ERR(hdmi_dev->cec_clk)) {
				dev_err(hdmi_dev->dev,
					"Unable to get hdmi cec_clk\n");
				return -1;
			}
		}
		clk_prepare_enable(hdmi_dev->cec_clk);
		hdmi_dev->clk_on |= HDMI_CECCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_SFRCLK_ON) == 0) {
		if (!hdmi_dev->sfr_clk) {
			hdmi_dev->sfr_clk =
				devm_clk_get(hdmi_dev->dev, "sclk_hdmi_sfr");
			if (IS_ERR(hdmi_dev->sfr_clk)) {
				dev_err(hdmi_dev->dev,
					"Unable to get hdmi sfr_clk\n");
				return -1;
			}
		}
		clk_prepare_enable(hdmi_dev->sfr_clk);
		hdmi_dev->clk_on |= HDMI_SFRCLK_ON;
	}

	return 0;
}

static int rockchip_hdmiv2_clk_disable(struct hdmi_dev *hdmi_dev)
{
	if (hdmi_dev->clk_on == 0)
		return 0;

	if ((hdmi_dev->clk_on & HDMI_PD_ON)) {
		pm_runtime_put(hdmi_dev->dev);
		hdmi_dev->clk_on &= ~HDMI_PD_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_PCLK_ON) &&
	    hdmi_dev->pclk) {
		clk_disable_unprepare(hdmi_dev->pclk);
		hdmi_dev->clk_on &= ~HDMI_PCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_HDCPCLK_ON) &&
	    hdmi_dev->hdcp_clk) {
		clk_disable_unprepare(hdmi_dev->hdcp_clk);
		hdmi_dev->clk_on &= ~HDMI_HDCPCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_EXT_PHY_CLK_ON) &&
	    hdmi_dev->pclk_phy) {
		clk_disable_unprepare(hdmi_dev->pclk_phy);
		hdmi_dev->clk_on &= ~HDMI_EXT_PHY_CLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_SFRCLK_ON) &&
	    (hdmi_dev->sfr_clk)) {
		clk_disable_unprepare(hdmi_dev->sfr_clk);
		hdmi_dev->clk_on &= ~HDMI_SFRCLK_ON;
	}


	return 0;
}

static int rockchip_hdmiv2_fb_event_notify(struct notifier_block *self,
					   unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct hdmi *hdmi = hdmi_dev->hdmi;
	struct pinctrl_state *gpio_state;
#ifdef CONFIG_PINCTRL
	struct dev_pin_info *pins = hdmi_dev->dev->pins;
#endif

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			HDMIDBG(2, "suspend hdmi\n");
			if (!hdmi->sleep) {
				hdmi_submit_work(hdmi,
						 HDMI_SUSPEND_CTL,
						 0, 1);
				if (hdmi_dev->hdcp2_en)
					hdmi_dev->hdcp2_en(0);
				mutex_lock(&hdmi->pclk_lock);
				rockchip_hdmiv2_clk_disable(hdmi_dev);
				mutex_unlock(&hdmi->pclk_lock);
				#ifdef CONFIG_PINCTRL
				if (hdmi_dev->soctype == HDMI_SOC_RK3288)
					gpio_state =
					pinctrl_lookup_state(pins->p,
							     "sleep");
				else
					gpio_state =
					pinctrl_lookup_state(pins->p,
							     "gpio");
				pinctrl_select_state(pins->p,
						     gpio_state);
				#endif
			}
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			HDMIDBG(2, "resume hdmi\n");
			if (hdmi->sleep) {
				#ifdef CONFIG_PINCTRL
				pinctrl_select_state(pins->p,
						     pins->default_state);
				#endif
				mutex_lock(&hdmi->pclk_lock);
				rockchip_hdmiv2_clk_enable(hdmi_dev);
				mutex_unlock(&hdmi->pclk_lock);
				rockchip_hdmiv2_dev_initial(hdmi_dev);
				if (hdmi->ops->hdcp_power_on_cb)
					hdmi->ops->hdcp_power_on_cb();
				if (hdmi_dev->hdcp2_reset)
					hdmi_dev->hdcp2_reset();
				if (hdmi_dev->hdcp2_en)
					hdmi_dev->hdcp2_en(1);
				hdmi_submit_work(hdmi, HDMI_RESUME_CTL,
						 0, 0);
			}
			break;
		default:
			break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block rockchip_hdmiv2_fb_notifier = {
	.notifier_call = rockchip_hdmiv2_fb_event_notify,
};

#ifdef HDMI_INT_USE_POLL
static void rockchip_hdmiv2_irq_work_func(struct work_struct *work)
{
	if (hdmi_dev->enable) {
		rockchip_hdmiv2_dev_irq(0, hdmi_dev);
		queue_delayed_work(hdmi_dev->workqueue,
				   &hdmi_dev->delay_work,
				   msecs_to_jiffies(50));
	}
}
#endif

static struct hdmi_ops rk_hdmi_ops;

#if defined(CONFIG_OF)
static const struct of_device_id rk_hdmi_dt_ids[] = {
	{.compatible = "rockchip,rk322x-hdmi",},
	{.compatible = "rockchip,rk3288-hdmi",},
	{.compatible = "rockchip,rk3366-hdmi",},
	{.compatible = "rockchip,rk3368-hdmi",},
	{.compatible = "rockchip,rk3399-hdmi",},
	{}
};

static int hdmi_get_prop_dts(struct hdmi *hdmi, struct device_node *np)
{
	const struct property *prop;
	int i = 0, nstates = 0;
	const __be32 *val;
	int value;
	struct edid_prop_value *pval = NULL;

	if (!hdmi || !np) {
		pr_info("%s:line=%d hdmi or np is null\n", __func__, __LINE__);
		return -1;
	}

	if (!of_property_read_u32(np, "hdmi_edid_auto_support", &value))
		hdmi->edid_auto_support = value;

	prop = of_find_property(np, "hdmi_edid_prop_value", NULL);
	if (!prop || !prop->value) {
		pr_info("%s:No edid-prop-value, %d\n", __func__, !prop);
		return -1;
	}

	nstates = (prop->length / sizeof(struct edid_prop_value));
	pval = kcalloc(nstates, sizeof(struct edid_prop_value), GFP_NOWAIT);

	for (i = 0, val = prop->value; i < nstates; i++) {
		pval[i].vid = be32_to_cpup(val++);
		pval[i].pid = be32_to_cpup(val++);
		pval[i].sn = be32_to_cpup(val++);
		pval[i].xres = be32_to_cpup(val++);
		pval[i].yres = be32_to_cpup(val++);
		pval[i].vic = be32_to_cpup(val++);
		pval[i].width = be32_to_cpup(val++);
		pval[i].height = be32_to_cpup(val++);
		pval[i].x_w = be32_to_cpup(val++);
		pval[i].x_h = be32_to_cpup(val++);
		pval[i].hwrotation = be32_to_cpup(val++);
		pval[i].einit = be32_to_cpup(val++);
		pval[i].vsync = be32_to_cpup(val++);
		pval[i].panel = be32_to_cpup(val++);
		pval[i].scan = be32_to_cpup(val++);

		pr_info("%s: 0x%x 0x%x 0x%x %d %d %d %d %d %d %d %d %d %d %d %d\n",
			__func__, pval[i].vid, pval[i].pid, pval[i].sn,
			pval[i].width, pval[i].height, pval[i].xres,
			pval[i].yres, pval[i].vic, pval[i].x_w,
			pval[i].x_h, pval[i].hwrotation, pval[i].einit,
			pval[i].vsync, pval[i].panel, pval[i].scan);
	}

	hdmi->pvalue = pval;
	hdmi->nstates = nstates;

	return 0;
}

static int rockchip_hdmiv2_parse_dt(struct hdmi_dev *hdmi_dev)
{
	int val = 0;
	struct device_node *np = hdmi_dev->dev->of_node;
	const struct of_device_id *match;

	match = of_match_node(rk_hdmi_dt_ids, np);
	if (!match)
		return -EINVAL;

	if (!strcmp(match->compatible, "rockchip,rk3288-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3288;
	} else if (!strcmp(match->compatible, "rockchip,rk3368-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3368;
	} else if (!strcmp(match->compatible, "rockchip,rk322x-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK322X;
	} else if (!strcmp(match->compatible, "rockchip,rk3366-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3366;
	} else if (!strcmp(match->compatible, "rockchip,rk3399-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3399;
	} else {
		pr_err("It is not a valid rockchip soc!");
		return -ENOMEM;
	}

	if (!of_property_read_u32(np, "rockchip,hdmi_video_source", &val))
		rk_hdmi_property.videosrc = val;

	if (!of_property_read_u32(np, "rockchip,hdmi_audio_source", &val))
		hdmi_dev->audiosrc = val;

	if (!of_property_read_u32(np, "rockchip,cec_enable", &val) &&
	    (val == 1)) {
		pr_debug("hdmi support cec\n");
		rk_hdmi_property.feature |= SUPPORT_CEC;
	}
	if (!of_property_read_u32(np, "rockchip,hdcp_enable", &val) &&
	    (val == 1)) {
		pr_debug("hdmi support hdcp\n");
		rk_hdmi_property.feature |= SUPPORT_HDCP;
	}
	if (!of_property_read_u32(np, "rockchip,defaultmode", &val) &&
	    (val > 0)) {
		pr_debug("default mode is %d\n", val);
		rk_hdmi_property.defaultmode = val;
	} else {
		rk_hdmi_property.defaultmode = HDMI_VIDEO_DEFAULT_MODE;
	}
	if (!of_property_read_u32(np, "rockchip,defaultdepth", &val) &&
	    (val > 0)) {
		pr_info("default depth is %d\n", val);
		rk_hdmi_property.defaultdepth = val;
	} else {
		rk_hdmi_property.defaultdepth = HDMI_VIDEO_DEFAULT_COLORDEPTH;
	}
	if (of_get_property(np, "rockchip,phy_table", &val)) {
		hdmi_dev->phy_table = kmalloc(val, GFP_KERNEL);
		if (!hdmi_dev->phy_table) {
			pr_err("kmalloc phy table %d error\n", val);
			return -ENOMEM;
		}
		hdmi_dev->phy_table_size =
				val / sizeof(struct hdmi_dev_phy_para);
		of_property_read_u32_array(np, "rockchip,phy_table",
					   (u32 *)hdmi_dev->phy_table,
					   val / sizeof(u32));
	} else {
		pr_info("hdmi phy_table not exist\n");
	}

	of_property_read_string(np, "rockchip,vendor",
				&hdmi_dev->vendor_name);
	of_property_read_string(np, "rockchip,product",
				&hdmi_dev->product_name);
	if (!of_property_read_u32(np, "rockchip,deviceinfo", &val))
		hdmi_dev->deviceinfo = val & 0xff;

	#ifdef CONFIG_MFD_SYSCON
	hdmi_dev->grf_base =
		syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi_dev->grf_base))
		hdmi_dev->grf_base = NULL;
	#endif
	return 0;
}
#endif

static int rockchip_hdmiv2_probe(struct platform_device *pdev)
{
	int ret = -1;
	struct resource *res;

	HDMIDBG(2, "%s\n", __func__);
	hdmi_dev = kmalloc(sizeof(*hdmi_dev), GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(&pdev->dev, ">>rockchip hdmiv2 kmalloc fail!");
		return -ENOMEM;
	}
	memset(hdmi_dev, 0, sizeof(struct hdmi_dev));
	platform_set_drvdata(pdev, hdmi_dev);
	hdmi_dev->dev = &pdev->dev;

	if (rockchip_hdmiv2_parse_dt(hdmi_dev))
		goto failed;

	/*request and remap iomem*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto failed;
	}
	hdmi_dev->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(&pdev->dev,
			"cannot ioremap registers,err=%d\n", ret);
		goto failed;
	}
	if (hdmi_dev->soctype == HDMI_SOC_RK322X) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res) {
			dev_err(&pdev->dev,
				"Unable to get phy register resource\n");
			ret = -ENXIO;
			goto failed;
		}
		hdmi_dev->phybase = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(hdmi_dev->phybase)) {
			ret = PTR_ERR(hdmi_dev->phybase);
			dev_err(&pdev->dev,
				"cannot ioremap registers,err=%d\n", ret);
			goto failed;
		}
	}

	hdmi_dev->reset = devm_reset_control_get(&pdev->dev, "hdmi");
	if (IS_ERR(hdmi_dev->reset) &&
	    hdmi_dev->soctype != HDMI_SOC_RK3288) {
		ret = PTR_ERR(hdmi_dev->reset);
		dev_err(&pdev->dev, "failed to get hdmi reset: %d\n", ret);
		goto failed;
	}
	pm_runtime_enable(hdmi_dev->dev);
	/*enable pd and clk*/
	if (rockchip_hdmiv2_clk_enable(hdmi_dev) < 0) {
		dev_err(&pdev->dev, "failed to enable hdmi clk\n");
		ret = -ENXIO;
		goto failed1;
	}
	rockchip_hdmiv2_dev_init_ops(&rk_hdmi_ops);
	/* Register HDMI device */
	rk_hdmi_property.name = (char *)pdev->name;
	rk_hdmi_property.priv = hdmi_dev;
	if (hdmi_dev->soctype == HDMI_SOC_RK3288) {
		rk_hdmi_property.feature |= SUPPORT_DEEP_10BIT;
		if (rk_hdmi_property.videosrc == DISPLAY_SOURCE_LCDC0)
			rk_hdmi_property.feature |=
						SUPPORT_4K |
						SUPPORT_TMDS_600M;
	} else if (hdmi_dev->soctype == HDMI_SOC_RK3368) {
		rk_hdmi_property.feature |=
				SUPPORT_4K |
				SUPPORT_4K_4096 |
				SUPPORT_YUV420 |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_VESA_DMT;
	} else if (hdmi_dev->soctype == HDMI_SOC_RK322X) {
		rk_hdmi_property.feature |=
				SUPPORT_4K |
				SUPPORT_4K_4096 |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_1080I |
				SUPPORT_480I_576I;
		/*
		 *if (rockchip_get_cpu_version())
		 *	rk_hdmi_property.feature |=
		 *		SUPPORT_YUV420 |
		 *		SUPPORT_DEEP_10BIT;
		 */
	} else if (hdmi_dev->soctype == HDMI_SOC_RK3366) {
		rk_hdmi_property.feature |=
				SUPPORT_YCBCR_INPUT |
				SUPPORT_1080I |
				SUPPORT_480I_576I;
		if (rk_hdmi_property.videosrc == DISPLAY_SOURCE_LCDC0)
			rk_hdmi_property.feature |=
						SUPPORT_4K |
						SUPPORT_4K_4096 |
						SUPPORT_YUV420 |
						SUPPORT_YCBCR_INPUT |
						SUPPORT_TMDS_600M;
	} else if (hdmi_dev->soctype == HDMI_SOC_RK3399) {
		rk_hdmi_property.feature |=
				SUPPORT_DEEP_10BIT |
				SUPPORT_YCBCR_INPUT |
				SUPPORT_1080I |
				SUPPORT_480I_576I |
				SUPPORT_VESA_DMT |
				SUPPORT_RK_DISCRETE_VR;
		if (rk_hdmi_property.videosrc == DISPLAY_SOURCE_LCDC0)
			rk_hdmi_property.feature |=
						SUPPORT_4K |
						SUPPORT_4K_4096 |
						SUPPORT_YUV420 |
						SUPPORT_YCBCR_INPUT |
						SUPPORT_TMDS_600M;
	} else {
		ret = -ENXIO;
		goto failed1;
	}
	hdmi_dev->hdmi =
		rockchip_hdmi_register(&rk_hdmi_property, &rk_hdmi_ops);
	if (!hdmi_dev->hdmi) {
		dev_err(&pdev->dev, "register hdmi device failed\n");
		ret = -ENOMEM;
		goto failed1;
	}

	hdmi_get_prop_dts(hdmi_dev->hdmi, hdmi_dev->dev->of_node);
	mutex_init(&hdmi_dev->ddc_lock);
	hdmi_dev->hdmi->dev = &pdev->dev;
	hdmi_dev->hdmi->soctype = hdmi_dev->soctype;
	fb_register_client(&rockchip_hdmiv2_fb_notifier);
	rockchip_hdmiv2_dev_initial(hdmi_dev);
	pinctrl_select_state(hdmi_dev->dev->pins->p,
			     hdmi_dev->dev->pins->default_state);
#if defined(CONFIG_DEBUG_FS)
	hdmi_dev->debugfs_dir = debugfs_create_dir("rockchip_hdmiv2", NULL);
	if (IS_ERR(hdmi_dev->debugfs_dir))
		dev_err(hdmi_dev->hdmi->dev,
			"failed to create debugfs dir for rockchip hdmiv2!\n");
	else {
		debugfs_create_file("regs_ctrl", S_IRUSR,
				    hdmi_dev->debugfs_dir,
				    hdmi_dev, &hdmi_regs_ctrl_fops);
		debugfs_create_file("regs_phy", S_IRUSR,
				    hdmi_dev->debugfs_dir,
				    hdmi_dev, &hdmi_regs_phy_fops);
	}
#endif

#ifndef HDMI_INT_USE_POLL
	/* get and request the IRQ */
	hdmi_dev->irq = platform_get_irq(pdev, 0);
	if (hdmi_dev->irq <= 0) {
		dev_err(hdmi_dev->dev,
			"failed to get hdmi irq resource (%d).\n",
			hdmi_dev->irq);
		ret = -ENXIO;
		goto failed1;
	}

	ret = devm_request_irq(hdmi_dev->dev, hdmi_dev->irq,
			       rockchip_hdmiv2_dev_irq,
			       IRQF_TRIGGER_HIGH,
			       dev_name(hdmi_dev->dev), hdmi_dev);
	if (ret) {
		dev_err(hdmi_dev->dev,
			"hdmi request_irq failed (%d).\n",
			ret);
		goto failed1;
	}
#else
	hdmi_dev->workqueue =
		create_singlethread_workqueue("rockchip hdmiv2 irq");
	INIT_DELAYED_WORK(&hdmi_dev->delay_work,
			  rockchip_hdmiv2_irq_work_func);
	rockchip_hdmiv2_irq_work_func(NULL);

#endif
	rk_display_device_enable(hdmi_dev->hdmi->ddev);
	dev_info(&pdev->dev, "rockchip hdmiv2 probe success.\n");
	return 0;

failed1:
	rockchip_hdmi_unregister(hdmi_dev->hdmi);
failed:
	kfree(hdmi_dev->phy_table);
	kfree(hdmi_dev);
	hdmi_dev = NULL;
	dev_err(&pdev->dev, "rockchip hdmiv2 probe error.\n");
	return ret;
}

static int rockchip_hdmiv2_suspend(struct platform_device *pdev,
				   pm_message_t state)
{
	if (hdmi_dev &&
	    hdmi_dev->grf_base &&
	    hdmi_dev->soctype == HDMI_SOC_RK322X) {
		regmap_write(hdmi_dev->grf_base,
			     RK322X_GRF_SOC_CON2,
			     RK322X_PLL_POWER_DOWN);
	}
	return 0;
}

static int rockchip_hdmiv2_resume(struct platform_device *pdev)
{
	if (hdmi_dev &&
	    hdmi_dev->grf_base &&
	    hdmi_dev->soctype == HDMI_SOC_RK322X) {
		regmap_write(hdmi_dev->grf_base,
			     RK322X_GRF_SOC_CON2,
			     RK322X_PLL_POWER_UP);
	}
	return 0;
}

static int rockchip_hdmiv2_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "rk3288 hdmi driver removed.\n");
	return 0;
}

static void rockchip_hdmiv2_shutdown(struct platform_device *pdev)
{
	struct hdmi *hdmi;

	if (hdmi_dev) {
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi_dev->early_suspend);
		#endif
		hdmi = hdmi_dev->hdmi;
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED &&
		    hdmi->ops->setmute)
			hdmi->ops->setmute(hdmi, HDMI_VIDEO_MUTE);
		pm_runtime_disable(hdmi_dev->dev);
	}
}

static struct platform_driver rockchip_hdmiv2_driver = {
	.probe		= rockchip_hdmiv2_probe,
	.remove		= rockchip_hdmiv2_remove,
	.driver		= {
		.name	= "rockchip-hdmiv2",
		.owner	= THIS_MODULE,
		#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(rk_hdmi_dt_ids),
		#endif
	},
	.suspend	= rockchip_hdmiv2_suspend,
	.resume		= rockchip_hdmiv2_resume,
	.shutdown	= rockchip_hdmiv2_shutdown,
};

static int __init rockchip_hdmiv2_init(void)
{
	return platform_driver_register(&rockchip_hdmiv2_driver);
}

static void __exit rockchip_hdmiv2_exit(void)
{
	platform_driver_unregister(&rockchip_hdmiv2_driver);
}

module_init(rockchip_hdmiv2_init);
module_exit(rockchip_hdmiv2_exit);
