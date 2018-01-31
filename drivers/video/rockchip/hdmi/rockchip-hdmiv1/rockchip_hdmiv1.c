/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/uaccess.h>

#include <linux/of_gpio.h>
#include <linux/rk_fb.h>

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rockchip_hdmiv1.h"
#include "rockchip_hdmiv1_hw.h"

static struct hdmi_dev *hdmi_dev;

#if defined(CONFIG_DEBUG_FS)
static int rockchip_hdmiv1_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;

	seq_puts(s, "\n\n>>>rk3036_ctl reg");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);

	seq_puts(s,
		 "\n-----------------------------------------------------------------");

	for (i = 0; i <= PHY_PRE_DIV_RATIO; i++) {
		hdmi_readl(hdmi_dev, i, &val);
		if (i % 16 == 0)
			seq_printf(s, "\n>>>rk3036_ctl %2x:", i);
		seq_printf(s, " %02x", val);
	}
	seq_puts(s,
		 "\n-----------------------------------------------------------------\n");

	return 0;
}

static ssize_t rockchip_hdmiv1_reg_write(struct file *file,
					 const char __user *buf,
					 size_t count,
					 loff_t *ppos)
{
	u32 reg;
	u32 val;
	char kbuf[25];
	static int ret;
	struct hdmi *hdmi_drv =  hdmi_dev->hdmi;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	ret = sscanf(kbuf, "%x%x", &reg, &val);
	if ((reg < 0) || (reg > 0xed)) {
		dev_info(hdmi_drv->dev, "it is no hdmi reg\n");
		return count;
	}
	dev_info(hdmi_drv->dev, "/**********rk3036 reg config******/");
	dev_info(hdmi_drv->dev, "\n reg=%x val=%x\n", reg, val);
	hdmi_writel(hdmi_dev, reg, val);

	return count;
}

static int rockchip_hdmiv1_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, rockchip_hdmiv1_reg_show, NULL);
}

static const struct file_operations rockchip_hdmiv1_reg_fops = {
	.owner = THIS_MODULE,
	.open = rockchip_hdmiv1_reg_open,
	.read = seq_read,
	.write = rockchip_hdmiv1_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int rockchip_hdmiv1_clk_enable(struct hdmi_dev *hdmi_dev)
{
	struct hdmi *hdmi_drv;

	hdmi_drv =  hdmi_dev->hdmi;
	if (!hdmi_dev->clk_on) {
		if (hdmi_dev->soctype == HDMI_SOC_RK312X)
			clk_prepare_enable(hdmi_dev->pd);

		clk_prepare_enable(hdmi_dev->hclk);
		spin_lock(&hdmi_dev->reg_lock);
		hdmi_dev->clk_on = 1;
		spin_unlock(&hdmi_dev->reg_lock);
	}

	return 0;
}

static int rockchip_hdmiv1_clk_disable(struct hdmi_dev *hdmi_dev)
{
	struct hdmi *hdmi_drv;

	hdmi_drv =  hdmi_dev->hdmi;
	if (hdmi_dev->clk_on) {
		spin_lock(&hdmi_dev->reg_lock);
		hdmi_dev->clk_on = 0;
		spin_unlock(&hdmi_dev->reg_lock);
		if (hdmi_dev->soctype == HDMI_SOC_RK312X)
			clk_disable_unprepare(hdmi_dev->pd);
		clk_disable_unprepare(hdmi_dev->hclk);
	}

	return 0;
}

static void rockchip_hdmiv1_early_suspend(void)
{
	struct hdmi *hdmi_drv =  hdmi_dev->hdmi;

	dev_info(hdmi_drv->dev, "hdmi suspend\n");
	hdmi_submit_work(hdmi_drv,
			 HDMI_SUSPEND_CTL, 0, 1);
	mutex_lock(&hdmi_drv->lock);
	if (hdmi_dev->irq)
		disable_irq(hdmi_dev->irq);
	mutex_unlock(&hdmi_drv->lock);
	rockchip_hdmiv1_clk_disable(hdmi_dev);
}

static void rockchip_hdmiv1_early_resume(void)
{
	struct hdmi *hdmi_drv =  hdmi_dev->hdmi;

	dev_info(hdmi_drv->dev, "hdmi resume\n");
	mutex_lock(&hdmi_drv->lock);
	rockchip_hdmiv1_clk_enable(hdmi_dev);
	rockchip_hdmiv1_initial(hdmi_drv);
	if (hdmi_drv->enable && hdmi_dev->irq) {
		rockchip_hdmiv1_irq(hdmi_drv);
		enable_irq(hdmi_dev->irq);
	}
	mutex_unlock(&hdmi_drv->lock);
	hdmi_submit_work(hdmi_drv, HDMI_RESUME_CTL, 0, 0);
}

static int rockchip_hdmiv1_fb_event_notify(struct notifier_block *self,
					   unsigned long action,
					   void *data)
{
	struct fb_event *event = data;

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			if (!hdmi_dev->hdmi->sleep)
				rockchip_hdmiv1_early_suspend();
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			if (hdmi_dev->hdmi->sleep)
				rockchip_hdmiv1_early_resume();
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_hdmiv1_fb_notifier = {
	.notifier_call = rockchip_hdmiv1_fb_event_notify,
};

static irqreturn_t rockchip_hdmiv1_irq_func(int irq, void *dev_id)
{
	struct hdmi *hdmi_drv = hdmi_dev->hdmi;

	rockchip_hdmiv1_irq(hdmi_drv);

	return IRQ_HANDLED;
}

static struct hdmi_property rockchip_hdmiv1_property = {
	.videosrc = DISPLAY_SOURCE_LCDC0,
	.display = DISPLAY_MAIN,
};

static struct hdmi_ops rockchip_hdmiv1_ops;

#if defined(CONFIG_OF)
static const struct of_device_id rockchip_hdmiv1_dt_ids[] = {
	{.compatible = "rockchip,rk3036-hdmi"},
	{.compatible = "rockchip,rk312x-hdmi"},
	{}
};

static int rockchip_hdmiv1_parse_dt(struct hdmi_dev *hdmi_dev)
{
	int val = 0;
	struct device_node *np = hdmi_dev->dev->of_node;
	const struct of_device_id *match;

	match = of_match_node(rockchip_hdmiv1_dt_ids, np);
	if (!match)
		return -EINVAL;

	if (!strcmp(match->compatible, "rockchip,rk3036-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK3036;
	} else if (!strcmp(match->compatible, "rockchip,rk312x-hdmi")) {
		hdmi_dev->soctype = HDMI_SOC_RK312X;
	} else {
		pr_err("It is not a valid rockchip soc!");
		return -ENOMEM;
	}

	if (!of_property_read_u32(np, "rockchip,hdmi_video_source", &val))
		rockchip_hdmiv1_property.videosrc = val;

	if (!of_property_read_u32(np, "rockchip,hdmi_audio_source", &val))
		hdmi_dev->audiosrc = val;

	if (!of_property_read_u32(np, "rockchip,cec_enable", &val) &&
	    (val == 1)) {
		pr_debug("hdmi support cec\n");
		rockchip_hdmiv1_property.feature |= SUPPORT_CEC;
	}
	if (!of_property_read_u32(np, "rockchip,hdcp_enable", &val) &&
	    (val == 1)) {
		pr_debug("hdmi support hdcp\n");
		rockchip_hdmiv1_property.feature |= SUPPORT_HDCP;
	}
	if (!of_property_read_u32(np, "rockchip,defaultmode", &val) &&
	    (val > 0)) {
		pr_debug("default mode is %d\n", val);
		rockchip_hdmiv1_property.defaultmode = val;
	} else {
		rockchip_hdmiv1_property.defaultmode =
						HDMI_VIDEO_DEFAULT_MODE;
	}

	return 0;
}
MODULE_DEVICE_TABLE(of, rockchip_hdmiv1_dt_ids);
#endif

static int rockchip_hdmiv1_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	hdmi_dev = devm_kzalloc(&pdev->dev,
				sizeof(struct hdmi_dev),
				GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(&pdev->dev, ">>rk_hdmi kmalloc fail!");
		return -ENOMEM;
	}
	hdmi_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdmi_dev);
	spin_lock_init(&hdmi_dev->reg_lock);
	rockchip_hdmiv1_parse_dt(hdmi_dev);
	/* request and remap iomem */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(hdmi_dev->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto failed;
	}
	hdmi_dev->regbase_phy = res->start;
	hdmi_dev->regsize_phy = resource_size(res);
	hdmi_dev->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(hdmi_dev->dev, "cannot ioremap registers,err=%d\n",
			ret);
		goto failed;
	}
	if (hdmi_dev->soctype == HDMI_SOC_RK312X) {
		hdmi_dev->pd = devm_clk_get(hdmi_dev->dev, "pd_hdmi");
		if (IS_ERR(hdmi_dev->pd)) {
			dev_err(hdmi_dev->hdmi->dev, "Unable to get hdmi pd\n");
			ret = -ENXIO;
			goto failed;
		}
	}
	hdmi_dev->hclk = devm_clk_get(hdmi_dev->dev, "pclk_hdmi");
	if (IS_ERR(hdmi_dev->hclk)) {
		dev_err(hdmi_dev->hdmi->dev, "Unable to get hdmi hclk\n");
		ret = -ENXIO;
		goto failed;
	}
	/* enable clk */
	rockchip_hdmiv1_clk_enable(hdmi_dev);
	hdmi_dev->hclk_rate = clk_get_rate(hdmi_dev->hclk);

	rockchip_hdmiv1_dev_init_ops(&rockchip_hdmiv1_ops);
	rockchip_hdmiv1_property.name = (char *)pdev->name;
	rockchip_hdmiv1_property.priv = hdmi_dev;
	if (rk_fb_get_display_policy() == DISPLAY_POLICY_BOX)
		rockchip_hdmiv1_property.feature |= SUPPORT_1080I |
						    SUPPORT_480I_576I;
	hdmi_dev->hdmi = rockchip_hdmi_register(&rockchip_hdmiv1_property,
						&rockchip_hdmiv1_ops);
	if (!hdmi_dev->hdmi) {
		dev_err(&pdev->dev, "register hdmi device failed\n");
		ret = -ENOMEM;
		goto failed;
	}
	hdmi_dev->hdmi->dev = &pdev->dev;

	fb_register_client(&rockchip_hdmiv1_fb_notifier);
	rockchip_hdmiv1_initial(hdmi_dev->hdmi);

	rk_display_device_enable(hdmi_dev->hdmi->ddev);
	hdmi_submit_work(hdmi_dev->hdmi, HDMI_HPD_CHANGE, 0, 1);

#if defined(CONFIG_DEBUG_FS)
	hdmi_dev->debugfs_dir = debugfs_create_dir("rockchip_hdmiv1", NULL);
	if (IS_ERR(hdmi_dev->debugfs_dir)) {
		dev_err(hdmi_dev->hdmi->dev,
			"failed to create debugfs dir for hdmi!\n");
	} else {
		debugfs_create_file("hdmi", S_IRUSR,
				    hdmi_dev->debugfs_dir, hdmi_dev->hdmi,
				    &rockchip_hdmiv1_reg_fops);
	}
#endif

	/* get the IRQ */
	hdmi_dev->irq = platform_get_irq(pdev, 0);
	if (hdmi_dev->irq <= 0) {
		dev_err(hdmi_dev->hdmi->dev, "failed to get hdmi irq resource (%d).\n",
			hdmi_dev->irq);
		hdmi_dev->irq = 0;
	} else {
		/* request the IRQ */
		ret = devm_request_irq(hdmi_dev->hdmi->dev,
				       hdmi_dev->irq,
				       rockchip_hdmiv1_irq_func,
				       IRQF_TRIGGER_HIGH,
				       dev_name(hdmi_dev->hdmi->dev),
				       hdmi_dev->hdmi);
		if (ret) {
			dev_err(hdmi_dev->hdmi->dev, "hdmi request_irq failed (%d)\n",
				ret);
			goto failed1;
		}
	}
	dev_info(hdmi_dev->hdmi->dev, "hdmi probe success.\n");
	return 0;

failed1:
	rockchip_hdmi_unregister(hdmi_dev->hdmi);
failed:
	hdmi_dev = NULL;
	dev_err(&pdev->dev, "rk3288 hdmi probe error.\n");
	return ret;
}

static int rockchip_hdmiv1_remove(struct platform_device *pdev)
{
	struct hdmi *hdmi_drv = NULL;

	hdmi_drv = hdmi_dev->hdmi;
	rockchip_hdmi_unregister(hdmi_drv);
	return 0;
}

static void rockchip_hdmiv1_shutdown(struct platform_device *pdev)
{
	struct hdmi_dev *hdmi_dev = platform_get_drvdata(pdev);
	struct hdmi *hdmi_drv = NULL;

	if (hdmi_dev) {
		hdmi_drv = hdmi_dev->hdmi;
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi_drv->early_suspend);
#endif
		mutex_lock(&hdmi_drv->lock);
		hdmi_drv->sleep = 1;
		if (!hdmi_drv->enable) {
			mutex_unlock(&hdmi_drv->lock);
			return;
		}
		if (hdmi_dev->irq)
			disable_irq(hdmi_dev->irq);
		mutex_unlock(&hdmi_drv->lock);
		if (hdmi_drv->hotplug == HDMI_HPD_ACTIVATED)
			hdmi_drv->ops->setmute(hdmi_drv,
					       HDMI_VIDEO_MUTE |
					       HDMI_AUDIO_MUTE);
		rockchip_hdmiv1_clk_disable(hdmi_dev);
	}
	dev_info(hdmi_drv->dev, "rk hdmi shut down.\n");
}

static struct platform_driver rockchip_hdmiv1_driver = {
	.probe = rockchip_hdmiv1_probe,
	.remove = rockchip_hdmiv1_remove,
	.driver = {
		.name = "rk-hdmi",
		.owner = THIS_MODULE,
		#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(rockchip_hdmiv1_dt_ids),
		#endif
	},
	.shutdown = rockchip_hdmiv1_shutdown,
};

static int __init rockchip_hdmiv1_init(void)
{
	return platform_driver_register(&rockchip_hdmiv1_driver);
}

static void __exit rockchip_hdmiv1_exit(void)
{
	platform_driver_unregister(&rockchip_hdmiv1_driver);
}

module_init(rockchip_hdmiv1_init);
module_exit(rockchip_hdmiv1_exit);
