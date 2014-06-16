/*
 * drivers/video/rockchip/hdmi/chips/rk3288/rk3188_hdmi.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *Author:zwl<zwl@rock-chips.com>
 *This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#if defined(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_device.h>
#endif
#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rk3288_hdmi_hw.h"
#include "rk3288_hdmi.h"

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)				\
do {							\
	writel_relaxed(v, RK_GRF_VIRT + offset);	\
	dsb();						\
} while (0)
#define HDMI_PD_ON		(1 << 0)
#define HDMI_PCLK_ON		(1 << 1)
#define HDMI_HDCPCLK_ON		(1 << 2)


static struct rk3288_hdmi_device *hdmi_dev;

#if defined(CONFIG_DEBUG_FS)
static const struct rk3288_hdmi_reg_table hdmi_reg_table[] = {
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
	{ENCRYPTED_DPK_EMBEDDED_BASE, HDCPREG_DPK6},
	{CEC_ENGINE_BASE, CEC_WKUPCTRL},
	{I2C_MASTER_BASE, I2CM_SCDC_UPDATE1},
};

static int rk3288_hdmi_reg_show(struct seq_file *s, void *v)
{
	int i = 0, j = 0;
	u32 val = 0;

	seq_puts(s, "\n>>>hdmi_ctl reg");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);

	seq_puts(s,
		   "\n-----------------------------------------------------------------");

	for (i = 0; i < ARRAY_SIZE(hdmi_reg_table); i++) {
		for (j = hdmi_reg_table[i].reg_base;
		     j <= hdmi_reg_table[i].reg_end; j++) {
			val = hdmi_readl(hdmi_dev, j);
			if ((j - hdmi_reg_table[i].reg_base) % 16 == 0)
				seq_printf(s, "\n>>>hdmi_ctl %2x:", j);
			seq_printf(s, " %02x", val);

		}
	}
	seq_puts(s,
		   "\n-----------------------------------------------------------------\n");

	return 0;
}

static ssize_t rk3288_hdmi_reg_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	u32 reg;
	u32 val;
	char kbuf[25];

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg, &val);
	if ((reg < 0) || (reg > I2CM_SCDC_UPDATE1)) {
		dev_info(hdmi_dev->dev, "it is no hdmi reg\n");
		return count;
	}
	dev_info(hdmi_dev->dev, "/**********rk3288 hdmi reg config******/");
	dev_info(hdmi_dev->dev, "\n reg=%x val=%x\n", reg, val);
	hdmi_writel(hdmi_dev, reg, val);

	return count;
}

static int rk3288_hdmi_reg_open(struct inode *inode, struct file *file)
{
	struct rk3288_hdmi_device *hdmi_dev = inode->i_private;

	return single_open(file, rk3288_hdmi_reg_show, hdmi_dev);
}

static const struct file_operations rk3288_hdmi_reg_fops = {
	.owner = THIS_MODULE,
	.open = rk3288_hdmi_reg_open,
	.read = seq_read,
	.write = rk3288_hdmi_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

struct hdmi *rk3288_hdmi_register_hdcp_callbacks(void (*hdcp_cb) (void),
					void (*hdcp_irq_cb) (int status),
					int (*hdcp_power_on_cb) (void),
					void (*hdcp_power_off_cb) (void))
{
	struct hdmi *hdmi_drv = NULL;

	if (hdmi_dev == NULL)
		return NULL;

	hdmi_drv = &hdmi_dev->driver;
	hdmi_drv->hdcp_cb = hdcp_cb;
	hdmi_drv->hdcp_irq_cb = hdcp_irq_cb;
	hdmi_drv->hdcp_power_on_cb = hdcp_power_on_cb;
	hdmi_drv->hdcp_power_off_cb = hdcp_power_off_cb;

	return hdmi_drv;
}

#ifdef HDMI_INT_USE_POLL
#define HDMI_POLL_MDELAY	100
static void rk3288_poll_delay_work(struct work_struct *work)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if (hdmi_drv->suspend == 0) {
		if (hdmi_drv->enable == 1)
			hdmi_irq(0, hdmi_drv);

		if (hdmi_dev->irq == 0)
			queue_delayed_work(hdmi_drv->workqueue,
					   &hdmi_dev->delay_work,
					   msecs_to_jiffies(HDMI_POLL_MDELAY));
	}
}
#endif

static int rk3288_hdmi_clk_enable(struct rk3288_hdmi_device *hdmi_dev)
{
	if ((hdmi_dev->clk_on & HDMI_PD_ON) && (hdmi_dev->clk_on & HDMI_PCLK_ON)
	    && (hdmi_dev->clk_on & HDMI_HDCPCLK_ON))
		return 0;

	if ((hdmi_dev->clk_on & HDMI_PD_ON) == 0) {
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

	return 0;
}

static int rk3288_hdmi_clk_disable(struct rk3288_hdmi_device *hdmi_dev)
{
	if (hdmi_dev->clk_on == 0)
		return 0;

	if ((hdmi_dev->clk_on & HDMI_PD_ON) && (hdmi_dev->pd != NULL)) {
		clk_disable_unprepare(hdmi_dev->pd);
		hdmi_dev->clk_on &= ~HDMI_PD_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_PCLK_ON) && (hdmi_dev->pclk != NULL)) {
		clk_disable_unprepare(hdmi_dev->pclk);
		hdmi_dev->clk_on &= ~HDMI_PCLK_ON;
	}

	if ((hdmi_dev->clk_on & HDMI_HDCPCLK_ON)
	    && (hdmi_dev->hdcp_clk != NULL)) {
		clk_disable_unprepare(hdmi_dev->hdcp_clk);
		hdmi_dev->clk_on &= ~HDMI_HDCPCLK_ON;
	}

	return 0;
}

static int rk3288_hdmi_drv_init(struct hdmi *hdmi_drv)
{
	int ret = 0;
	struct rk_screen screen;

	rk_fb_get_prmry_screen(&screen);

	/* hdmi is extend as default,TODO modify if hdmi is primary */
	hdmi_dev->lcdc_id = (screen.lcdc_id == 0) ? 1 : 0;
	/* lcdc source select */
	grf_writel(HDMI_SEL_LCDC(hdmi_dev->lcdc_id), RK3288_GRF_SOC_CON6);
	if (hdmi_dev->lcdc_id == 0)
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi_drv->lcdc = rk_get_lcdc_drv("lcdc1");
	if (IS_ERR(hdmi_drv->lcdc)) {
		dev_err(hdmi_drv->dev,
			"can not connect to video source lcdc\n");
		ret = -ENXIO;
		return ret;
	}

	hdmi_drv->xscale = 100;
	hdmi_drv->yscale = 100;

	spin_lock_init(&hdmi_drv->irq_lock);
	mutex_init(&hdmi_drv->enable_mutex);

	rk3288_hdmi_initial(hdmi_drv);
	hdmi_sys_init(hdmi_drv);
	hdmi_drv_register(hdmi_drv);

	return ret;
}

static void rk3288_hdmi_early_suspend(void)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if (hdmi_drv->suspend)
		return;

	hdmi_dbg(hdmi_drv->dev, "hdmi enter early suspend pwr %d state %d\n",
		 hdmi_drv->pwr_mode, hdmi_drv->state);
	flush_delayed_work(&hdmi_drv->delay_work);
	mutex_lock(&hdmi_drv->enable_mutex);
	hdmi_drv->suspend = 1;
	if (!hdmi_drv->enable) {
		mutex_unlock(&hdmi_drv->enable_mutex);
		return;
	}
	disable_irq(hdmi_drv->irq);
	mutex_unlock(&hdmi_drv->enable_mutex);
	hdmi_drv->command = HDMI_CONFIG_ENABLE;
	init_completion(&hdmi_drv->complete);
	hdmi_drv->wait = 1;
	queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work, 0);
	wait_for_completion_interruptible_timeout(&hdmi_drv->complete,
						  msecs_to_jiffies(5000));
	flush_delayed_work(&hdmi_drv->delay_work);

	/* iomux to gpio and pull down when suspend */
	pinctrl_select_state(hdmi_dev->dev->pins->p,
			     hdmi_dev->dev->pins->sleep_state);
	rk3288_hdmi_clk_disable(hdmi_dev);
	return;
}

static void rk3288_hdmi_early_resume(void)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if (!hdmi_drv->suspend)
		return;

	hdmi_dbg(hdmi_drv->dev, "hdmi enter early resume\n");
	/* iomux to default state for hdmi use when resume */
	pinctrl_select_state(hdmi_dev->dev->pins->p,
			     hdmi_dev->dev->pins->default_state);
	rk3288_hdmi_clk_enable(hdmi_dev);
	mutex_lock(&hdmi_drv->enable_mutex);
	hdmi_drv->suspend = 0;
	rk3288_hdmi_initial(hdmi_drv);
	if (hdmi_dev->irq == 0) {
#ifdef HDMI_INT_USE_POLL
		queue_delayed_work(hdmi_drv->workqueue, &hdmi_dev->delay_work,
				   msecs_to_jiffies(5));
#endif
	} else if (hdmi_drv->enable) {
		enable_irq(hdmi_drv->irq);
	}
	queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work,
			   msecs_to_jiffies(10));
	mutex_unlock(&hdmi_drv->enable_mutex);
	return;
}

static int rk3288_hdmi_fb_event_notify(struct notifier_block *self,
				       unsigned long action, void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			rk3288_hdmi_early_suspend();
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			rk3288_hdmi_early_resume();
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block rk3288_hdmi_fb_notifier = {
	.notifier_call = rk3288_hdmi_fb_event_notify,
};

#if defined(CONFIG_OF)
static int rk3288_hdmi_parse_dt(struct rk3288_hdmi_device *hdmi_dev)
{
	int val = 0;
	struct device_node *np = hdmi_dev->dev->of_node;

	if (!of_property_read_u32(np, "rockchips,hdmi_audio_source", &val))
		hdmi_dev->driver.audio.type = val;

	return 0;
}

static const struct of_device_id rk3288_hdmi_dt_ids[] = {
	{.compatible = "rockchip,rk3288-hdmi",},
	{}
};
#endif

static int rk3288_hdmi_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct hdmi *dev_drv = NULL;

	hdmi_dev = kzalloc(sizeof(struct rk3288_hdmi_device), GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(&pdev->dev, ">>rk3288_hdmi_device kzalloc fail!");
		return -ENOMEM;
	}

	hdmi_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdmi_dev);
	mutex_init(&hdmi_dev->int_mutex);

	rk3288_hdmi_parse_dt(hdmi_dev);
	/* TODO Daisen wait to add cec iomux */

	/* enable pd and pclk and hdcp_clk */
	if (rk3288_hdmi_clk_enable(hdmi_dev) < 0)
		goto err0;

	/* request and remap iomem */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(hdmi_dev->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto err0;
	}
	hdmi_dev->regbase_phy = res->start;
	hdmi_dev->regsize_phy = resource_size(res);
	hdmi_dev->regbase = devm_ioremap_resource(hdmi_dev->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(hdmi_dev->dev, "cannot ioremap registers,err=%d\n",
			ret);
		goto err0;
	}

	/*init hdmi driver */
	dev_drv = &hdmi_dev->driver;
	dev_drv->dev = &pdev->dev;
	if (rk3288_hdmi_drv_init(dev_drv))
		goto err0;

	dev_drv->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(dev_drv->delay_work), hdmi_work);

	hdmi_register_display_sysfs(dev_drv, NULL);

#ifdef CONFIG_SWITCH
	dev_drv->switch_hdmi.name = "hdmi";
	switch_dev_register(&(dev_drv->switch_hdmi));
#endif

	fb_register_client(&rk3288_hdmi_fb_notifier);

#ifndef HDMI_INT_USE_POLL
	/* get and request the IRQ */
	dev_drv->irq = platform_get_irq(pdev, 0);
	if (dev_drv->irq <= 0) {
		dev_err(hdmi_dev->dev,
			"failed to get hdmi irq resource (%d).\n",
			hdmi_dev->irq);
		ret = -ENXIO;
		goto err1;
	}

	ret =
	    devm_request_irq(hdmi_dev->dev, dev_drv->irq, hdmi_irq, 0,
			     dev_name(hdmi_dev->dev), dev_drv);
	if (ret) {
		dev_err(hdmi_dev->dev, "hdmi request_irq failed (%d).\n", ret);
		goto err1;
	}
#else
	hdmi_dev->irq = 0;
	INIT_DELAYED_WORK(&hdmi_dev->delay_work, rk3288_poll_delay_work);
	queue_delayed_work(dev_drv->workqueue, &hdmi_dev->delay_work,
			   msecs_to_jiffies(1));
#endif

#if defined(CONFIG_DEBUG_FS)
	hdmi_dev->debugfs_dir = debugfs_create_dir("rk3288-hdmi", NULL);
	if (IS_ERR(hdmi_dev->debugfs_dir)) {
		dev_err(hdmi_dev->dev,
			"failed to create debugfs dir for rk3288 hdmi!\n");
	} else {
		debugfs_create_file("hdmi", S_IRUSR, hdmi_dev->debugfs_dir,
				    hdmi_dev, &rk3288_hdmi_reg_fops);
	}
#endif

	dev_info(hdmi_dev->dev, "rk3288 hdmi probe sucess.\n");
	return 0;

#ifndef HDMI_INT_USE_POLL
err1:
#endif
	fb_unregister_client(&rk3288_hdmi_fb_notifier);
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(dev_drv->switch_hdmi));
#endif
	hdmi_unregister_display_sysfs(dev_drv);

	/*
	iounmap((void*)hdmi_dev->regbase);
	release_mem_region(res->start, hdmi_dev->regsize_phy);
	*/
err0:
	rk3288_hdmi_clk_disable(hdmi_dev);
	dev_info(hdmi_dev->dev, "rk3288 hdmi probe error.\n");
	kfree(hdmi_dev);
	hdmi_dev = NULL;
	return ret;
}

static int rk3288_hdmi_remove(struct platform_device *pdev)
{
	struct rk3288_hdmi_device *hdmi_dev = platform_get_drvdata(pdev);
	struct hdmi *hdmi_drv = NULL;

	if (hdmi_dev) {
		hdmi_drv = &hdmi_dev->driver;
		mutex_lock(&hdmi_drv->enable_mutex);
		if (!hdmi_drv->suspend && hdmi_drv->enable)
			disable_irq(hdmi_drv->irq);
		mutex_unlock(&hdmi_drv->enable_mutex);
		free_irq(hdmi_drv->irq, NULL);

		flush_workqueue(hdmi_drv->workqueue);
		destroy_workqueue(hdmi_drv->workqueue);

		fb_unregister_client(&rk3288_hdmi_fb_notifier);

#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi_drv->switch_hdmi));
#endif
		hdmi_unregister_display_sysfs(hdmi_drv);

		/*
		iounmap((void*)hdmi_drv->regbase);
		release_mem_region(hdmi_drv->regbase_phy,
					hdmi_drv->regsize_phy);
		*/
		rk3288_hdmi_clk_disable(hdmi_dev);
		fb_destroy_modelist(&hdmi_drv->edid.modelist);
		kfree(hdmi_drv->edid.audio);
		if (hdmi_drv->edid.specs) {
			kfree(hdmi_drv->edid.specs->modedb);
			kfree(hdmi_drv->edid.specs);
		}

		kfree(hdmi_dev);
		hdmi_dev = NULL;
	}
	dev_info(hdmi_dev->dev, "rk3288 hdmi removed.\n");
	return 0;
}

static void rk3288_hdmi_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver rk3288_hdmi_driver = {
	.probe = rk3288_hdmi_probe,
	.remove = rk3288_hdmi_remove,
	.driver = {
		   .name = "rk3288-hdmi",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3288_hdmi_dt_ids),
		   },
	.shutdown = rk3288_hdmi_shutdown,
};

static int __init rk3288_hdmi_init(void)
{
	return platform_driver_register(&rk3288_hdmi_driver);
}

static void __exit rk3288_hdmi_exit(void)
{
	platform_driver_unregister(&rk3288_hdmi_driver);
}

device_initcall_sync(rk3288_hdmi_init);
module_exit(rk3288_hdmi_exit);
