#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/mfd/syscon.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rockchip_hdmiv2.h"
#include "rockchip_hdmiv2_hw.h"

#define HDMI_SEL_LCDC(x)	((((x)&1)<<4)|(1<<20))
#define grf_writel(v, offset)	writel_relaxed(v, RK_GRF_VIRT + offset)

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

static int rockchip_hdmiv2_reg_show(struct seq_file *s, void *v)
{
	int i = 0, j = 0;
	u32 val = 0;

	seq_puts(s, "\n>>>hdmi_ctl reg ");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);
	seq_puts(s, "\n-----------------------------------------------------------------");

	for (i = 0; i < ARRAY_SIZE(hdmi_reg_table); i++) {
		for (j = hdmi_reg_table[i].reg_base;
		     j <= hdmi_reg_table[i].reg_end; j++) {
			val = hdmi_readl(hdmi_dev, j);
			if ((j - hdmi_reg_table[i].reg_base)%16 == 0)
				seq_printf(s, "\n>>>hdmi_ctl %04x:", j);
			seq_printf(s, " %02x", val);
		}
	}
	seq_puts(s, "\n-----------------------------------------------------------------\n");

	/*rockchip_hdmiv2_dump_phy_regs(hdmi_dev);*/
	return 0;
}

static ssize_t rockchip_hdmiv2_reg_write(struct file *file,
					 const char __user *buf,
					 size_t count, loff_t *ppos)
{
	u32 reg;
	u32 val;
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

static int rockchip_hdmiv2_reg_open(struct inode *inode, struct file *file)
{
	struct hdmi_dev *hdmi_dev = inode->i_private;

	return single_open(file, rockchip_hdmiv2_reg_show, hdmi_dev);
}

static const struct file_operations rockchip_hdmiv2_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= rockchip_hdmiv2_reg_open,
	.read		= seq_read,
	.write		= rockchip_hdmiv2_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rockchip_hdmiv2_early_suspend(struct early_suspend *h)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;
	struct delay_work *delay_work;
	struct pinctrl_state *gpio_state;

	HDMIDBG("hdmi enter early suspend\n");
	delay_work = hdmi_submit_work(hdmi, HDMI_SUSPEND_CTL, 0, NULL);
	if (delay_work)
		flush_delayed_work_sync(delay_work);
	/* iomux to gpio and pull down when suspend */
	gpio_state = pinctrl_lookup_state(hdmi_dev->dev->pins->p, "gpio");
	pinctrl_select_state(hdmi_dev->dev->pins->p, gpio_state);
	rockchip_hdmiv2_clk_disable(hdmi_dev);
}

static void rockchip_hdmiv2_early_resume(struct early_suspend *h)
{
	struct hdmi *hdmi = hdmi_dev->hdmi;

	HDMIDBG("hdmi exit early resume\n");
	/* iomux to default state for hdmi use when resume */
	pinctrl_select_state(hdmi_dev->dev->pins->p,
			     hdmi_dev->dev->pins->default_state);
	rockchip_hdmiv2_clk_enable(hdmi_dev);
	hdmi_dev_initial(hdmi_dev);
	if (hdmi->ops->hdcp_power_on_cb)
		hdmi->ops->hdcp_power_on_cb();
	hdmi_submit_work(hdmi, HDMI_RESUME_CTL, 0, NULL);
}
#endif

#define HDMI_PD_ON			(1 << 0)
#define HDMI_PCLK_ON		(1 << 1)
#define HDMI_HDCPCLK_ON		(1 << 2)
#define HDMI_CECCLK_ON		(1 << 3)

static int rockchip_hdmiv2_clk_enable(struct hdmi_dev *hdmi_dev)
{
	if ((hdmi_dev->clk_on & HDMI_PD_ON) &&
	    (hdmi_dev->clk_on & HDMI_PCLK_ON) &&
	    (hdmi_dev->clk_on & HDMI_HDCPCLK_ON))
		return 0;

	if ((hdmi_dev->clk_on & HDMI_PD_ON) == 0 &&
	    hdmi_dev->soctype == HDMI_SOC_RK3288) {
		if (hdmi_dev->pd == NULL) {
			hdmi_dev->pd = devm_clk_get(hdmi_dev->dev, "pd_hdmi");
			if (IS_ERR(hdmi_dev->pd)) {
				dev_err(hdmi_dev->dev,
					"Unable to get hdmi pd\n");
				return -1;
			}
		}
		clk_prepare_enable(hdmi_dev->pd);
		hdmi_dev->clk_on |= HDMI_PD_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_PCLK_ON) == 0) {
		if (hdmi_dev->pclk == NULL) {
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
		if (hdmi_dev->hdcp_clk == NULL) {
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
		if (hdmi_dev->cec_clk == NULL) {
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
	return 0;
}

static int rockchip_hdmiv2_clk_disable(struct hdmi_dev *hdmi_dev)
{
	if (hdmi_dev->clk_on == 0)
		return 0;

	if ((hdmi_dev->clk_on & HDMI_PD_ON) && (hdmi_dev->pd != NULL)) {
		clk_disable_unprepare(hdmi_dev->pd);
		hdmi_dev->clk_on &= ~HDMI_PD_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_PCLK_ON) &&
	    (hdmi_dev->pclk != NULL)) {
		clk_disable_unprepare(hdmi_dev->pclk);
		hdmi_dev->clk_on &= ~HDMI_PCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_HDCPCLK_ON) &&
	    (hdmi_dev->hdcp_clk != NULL)) {
		clk_disable_unprepare(hdmi_dev->hdcp_clk);
		hdmi_dev->clk_on &= ~HDMI_HDCPCLK_ON;
	}

	return 0;
}

static int rockchip_hdmiv2_fb_event_notify(struct notifier_block *self,
					   unsigned long action, void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);
	struct hdmi *hdmi = hdmi_dev->hdmi;
	struct delayed_work *delay_work;

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			HDMIDBG("suspend hdmi\n");
			if (!hdmi->sleep) {
				delay_work =
					hdmi_submit_work(hdmi,
							 HDMI_SUSPEND_CTL,
							 0, NULL);
				if (delay_work)
					flush_delayed_work(delay_work);
				rockchip_hdmiv2_clk_disable(hdmi_dev);
			}
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			HDMIDBG("resume hdmi\n");
			if (hdmi->sleep) {
				rockchip_hdmiv2_clk_enable(hdmi_dev);
				rockchip_hdmiv2_dev_initial(hdmi_dev);
				if (hdmi->ops->hdcp_power_on_cb)
					hdmi->ops->hdcp_power_on_cb();
				hdmi_submit_work(hdmi, HDMI_RESUME_CTL,
						 0, NULL);
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
				   &(hdmi_dev->delay_work),
				   msecs_to_jiffies(50));
	}
}
#endif

static struct hdmi_ops rk_hdmi_ops;


#if defined(CONFIG_OF)
static const struct of_device_id rk_hdmi_dt_ids[] = {
	{.compatible = "rockchip,rk3288-hdmi",},
	{.compatible = "rockchip,rk3368-hdmi",},
	{}
};

static int rockchip_hdmiv2_parse_dt(struct hdmi_dev *hdmi_dev)
{
	int val = 0;
	struct device_node *np = hdmi_dev->dev->of_node;
	const struct of_device_id *match;

	match = of_match_node(rk_hdmi_dt_ids, np);
	if (!match)
		return PTR_ERR(match);

	if (!strcmp(match->compatible, "rockchip,rk3288-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3288;
	} else if (!strcmp(match->compatible, "rockchip,rk3368-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3368;
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
		pr_info("hdmi support cec\n");
		rk_hdmi_property.feature |= SUPPORT_CEC;
	}
	if (!of_property_read_u32(np, "rockchip,hdcp_enable", &val) &&
	    (val == 1)) {
		pr_info("hdmi support hdcp\n");
		rk_hdmi_property.feature |= SUPPORT_HDCP;
	}
	#ifdef CONFIG_MFD_SYSCON
	hdmi_dev->grf_base =
		syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	#endif
	return 0;
}
#endif

static int rockchip_hdmiv2_probe(struct platform_device *pdev)
{
	int ret = -1;
	struct resource *res;

	HDMIDBG("%s\n", __func__);
	hdmi_dev = kmalloc(sizeof(*hdmi_dev), GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(&pdev->dev, ">>rockchip hdmiv2 kmalloc fail!");
		return -ENOMEM;
	}
	memset(hdmi_dev, 0, sizeof(struct hdmi_dev));
	platform_set_drvdata(pdev, hdmi_dev);
	hdmi_dev->dev = &pdev->dev;

	rockchip_hdmiv2_parse_dt(hdmi_dev);

	/*request and remap iomem*/
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto failed;
	}
	hdmi_dev->regbase_phy = res->start;
	hdmi_dev->regsize_phy = resource_size(res);
	hdmi_dev->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(&pdev->dev,
			"cannot ioremap registers,err=%d\n", ret);
		goto failed;
	}

	/*enable pd and pclk and hdcp_clk*/
	if (rockchip_hdmiv2_clk_enable(hdmi_dev) < 0) {
		ret = -ENXIO;
		goto failed1;
	}
	/*lcdc source select*/
	if (hdmi_dev->soctype == HDMI_SOC_RK3288) {
		grf_writel(HDMI_SEL_LCDC(rk_hdmi_property.videosrc),
			   RK3288_GRF_SOC_CON6);
		/* select GPIO7_C0 as cec pin */
		grf_writel(((1 << 12) | (1 << 28)), RK3288_GRF_SOC_CON8);
	}
	rockchip_hdmiv2_dev_init_ops(&rk_hdmi_ops);
	/* Register HDMI device */
	rk_hdmi_property.name = (char *)pdev->name;
	rk_hdmi_property.priv = hdmi_dev;
	if (hdmi_dev->soctype == HDMI_SOC_RK3288) {
		/*rk_hdmi_property.feature |= SUPPORT_DEEP_10BIT;*/
		if (rk_hdmi_property.videosrc == DISPLAY_SOURCE_LCDC0)
			rk_hdmi_property.feature |=
						SUPPORT_4K |
						SUPPORT_TMDS_600M;
	} else if (hdmi_dev->soctype == HDMI_SOC_RK3368) {
		rk_hdmi_property.feature |=
				SUPPORT_4K |
				SUPPORT_4K_4096 |
				SUPPORT_YUV420;
	}
	hdmi_dev->hdmi =
		rockchip_hdmi_register(&rk_hdmi_property, &rk_hdmi_ops);
	if (hdmi_dev->hdmi == NULL) {
		dev_err(&pdev->dev, "register hdmi device failed\n");
		ret = -ENOMEM;
		goto failed1;
	}
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
	else
		debugfs_create_file("hdmi", S_IRUSR,
				    hdmi_dev->debugfs_dir,
				    hdmi_dev, &rockchip_hdmiv2_reg_fops);
#endif
	if (rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
		rk_display_device_enable(hdmi_dev->hdmi->ddev);

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

	ret =
	    devm_request_irq(hdmi_dev->dev, hdmi_dev->irq,
			     rockchip_hdmiv2_dev_irq,
			     IRQF_TRIGGER_HIGH,
			     dev_name(hdmi_dev->dev), hdmi_dev);
	if (ret) {
		dev_err(hdmi_dev->dev, "hdmi request_irq failed (%d).\n", ret);
		goto failed1;
	}
#else
	hdmi_dev->workqueue =
		create_singlethread_workqueue("rockchip hdmiv2 irq");
	INIT_DELAYED_WORK(&(hdmi_dev->delay_work),
			  rockchip_hdmiv2_irq_work_func);
	rockchip_hdmiv2_irq_work_func(NULL);

#endif
	dev_info(&pdev->dev, "rockchip hdmiv2 probe sucess.\n");
	return 0;

failed1:
	rockchip_hdmi_unregister(hdmi_dev->hdmi);
failed:
	kfree(hdmi_dev);
	hdmi_dev = NULL;
	dev_err(&pdev->dev, "rk3288 hdmi probe error.\n");
	return ret;
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
		if (hdmi->hotplug == HDMI_HPD_ACTIVED &&
		    hdmi->ops->setmute)
			hdmi->ops->setmute(hdmi, HDMI_VIDEO_MUTE);
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
	.shutdown   = rockchip_hdmiv2_shutdown,
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
