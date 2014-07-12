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

#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"

static struct rk_hdmi_device *hdmi_dev;

#if defined(CONFIG_DEBUG_FS)
static int rk616_hdmi_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;

	seq_puts(s, "\n>>>rk616_ctl reg");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);

	seq_puts(s,
		   "\n-----------------------------------------------------------------");

	for (i = 0; i <= PHY_PRE_DIV_RATIO; i++) {
		hdmi_readl(hdmi_dev, i, &val);
		if (i % 16 == 0)
			seq_printf(s, "\n>>>rk616_ctl %2x:", i);
		seq_printf(s, " %02x", val);

	}
	seq_puts(s,
		   "\n-----------------------------------------------------------------\n");

	return 0;
}

static ssize_t rk616_hdmi_reg_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	u32 reg;
	u32 val;
	char kbuf[25];
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg, &val);
	if ((reg < 0) || (reg > 0xed)) {
		dev_info(hdmi_drv->dev, "it is no hdmi reg\n");
		return count;
	}
	dev_info(hdmi_drv->dev, "/**********rk616 reg config******/");
	dev_info(hdmi_drv->dev, "\n reg=%x val=%x\n", reg, val);
	hdmi_writel(hdmi_dev, reg, val);

	return count;
}

static int rk616_hdmi_reg_open(struct inode *inode, struct file *file)
{
	struct mfd_rk616 *rk616_drv = inode->i_private;

	return single_open(file, rk616_hdmi_reg_show, rk616_drv);
}

static const struct file_operations rk616_hdmi_reg_fops = {
	.owner = THIS_MODULE,
	.open = rk616_hdmi_reg_open,
	.read = seq_read,
	.write = rk616_hdmi_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

#if defined(CONFIG_ARCH_RK3026) || defined(SOC_CONFIG_RK3036)
static int rk616_hdmi_clk_enable(struct rk_hdmi_device *hdmi_dev)
{
	if (!hdmi_dev->clk_on) {
		clk_prepare_enable(hdmi_dev->hclk);
		spin_lock(&hdmi_dev->reg_lock);
		hdmi_dev->clk_on = 1;
		spin_unlock(&hdmi_dev->reg_lock);
	}

	return 0;
}

static int rk616_hdmi_clk_disable(struct rk_hdmi_device *hdmi_dev)
{
	if (!hdmi_dev->clk_on) {
		spin_lock(&hdmi_dev->reg_lock);
		hdmi_dev->clk_on = 0;
		spin_unlock(&hdmi_dev->reg_lock);
		clk_disable_unprepare(hdmi_dev->hclk);
	}

	return 0;
}

#endif

int rk616_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
				       void (*hdcp_irq_cb)(int status),
				       int (*hdcp_power_on_cb)(void),
				       void (*hdcp_power_off_cb)(void))
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if (hdmi_drv == NULL)
		return HDMI_ERROR_FALSE;

	hdmi_drv->hdcp_cb = hdcp_cb;
	hdmi_drv->hdcp_irq_cb = hdcp_irq_cb;
	hdmi_drv->hdcp_power_on_cb = hdcp_power_on_cb;
	hdmi_drv->hdcp_power_off_cb = hdcp_power_off_cb;

	return HDMI_ERROR_SUCESS;
}

static void rk616_hdmi_early_suspend(void)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	hdmi_dbg(hdmi_drv->dev, "hdmi enter early suspend pwr %d state %d\n",
		 hdmi_drv->pwr_mode, hdmi_drv->state);

	flush_delayed_work(&hdmi_drv->delay_work);
	mutex_lock(&hdmi_drv->enable_mutex);
	hdmi_drv->suspend = 1;
	if (!hdmi_drv->enable) {
		mutex_unlock(&hdmi_drv->enable_mutex);
		return;
	}

	if (hdmi_drv->irq)
		disable_irq(hdmi_drv->irq);

	mutex_unlock(&hdmi_drv->enable_mutex);
	hdmi_drv->command = HDMI_CONFIG_ENABLE;
	init_completion(&hdmi_drv->complete);
	hdmi_drv->wait = 1;
	queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work, 0);
	wait_for_completion_interruptible_timeout(&hdmi_drv->complete,
						  msecs_to_jiffies(5000));
	flush_delayed_work(&hdmi_drv->delay_work);

}

static void rk616_hdmi_early_resume(void)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;
	struct mfd_rk616 *rk616_drv = hdmi_dev->rk616_drv;

	hdmi_dbg(hdmi_drv->dev, "hdmi exit early resume\n");

	mutex_lock(&hdmi_drv->enable_mutex);

	hdmi_drv->suspend = 0;
	rk616_hdmi_initial(hdmi_drv);
	if (hdmi_drv->enable && hdmi_drv->irq) {
		enable_irq(hdmi_drv->irq);
		rk616_hdmi_work(hdmi_drv);
	}
	if (rk616_drv && !gpio_is_valid(rk616_drv->pdata->hdmi_irq))
		queue_delayed_work(hdmi_drv->workqueue,
				   &hdmi_dev->rk616_delay_work,
				   msecs_to_jiffies(100));

	queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work,
			   msecs_to_jiffies(10));
	mutex_unlock(&hdmi_drv->enable_mutex);
}

static int rk616_hdmi_fb_event_notify(struct notifier_block *self,
				      unsigned long action, void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			rk616_hdmi_early_suspend();
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			rk616_hdmi_early_resume();
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block rk616_hdmi_fb_notifier = {
	.notifier_call = rk616_hdmi_fb_event_notify,
};


static void rk616_delay_work_func(struct work_struct *work)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;
	struct mfd_rk616 *rk616_drv = hdmi_dev->rk616_drv;

	if (hdmi_drv->suspend == 0) {
		if (hdmi_drv->enable == 1)
			rk616_hdmi_work(hdmi_drv);

		if (rk616_drv && !gpio_is_valid(rk616_drv->pdata->hdmi_irq))
			queue_delayed_work(hdmi_drv->workqueue,
					   &hdmi_dev->rk616_delay_work,
					   msecs_to_jiffies(100));
	}
}

static void __maybe_unused rk616_irq_work_func(struct work_struct *work)
{
	struct hdmi *hdmi_drv = &hdmi_dev->driver;

	if ((hdmi_drv->suspend == 0) && (hdmi_drv->enable == 1))
		rk616_hdmi_work(hdmi_drv);

	dev_info(hdmi_drv->dev, "func: %s, enable_irq\n", __func__);
	enable_irq(hdmi_drv->irq);
}
#if 0
static irqreturn_t rk616_hdmi_irq(int irq, void *dev_id)
{
	struct work_struct *rk616_irq_work_struct;
	struct hdmi *hdmi_drv = &hdmi_dev->driver;
	struct mfd_rk616 *rk616_drv = hdmi_dev->rk616_drv;

	if (rk616_drv) {
		rk616_irq_work_struct = dev_id;
		disable_irq_nosync(hdmi_drv->irq);
		queue_work(hdmi_drv->workqueue, rk616_irq_work_struct);
	} else {
		/* 3028a/3036 hdmi */
		if ((hdmi_drv->suspend == 0) && (hdmi_drv->enable == 1)) {
			hdmi_dbg(hdmi_drv->dev,
				 "line = %d, rk616_hdmi_irq irq triggered.\n",
				 __LINE__);
			rk616_hdmi_work(hdmi_drv);
		}
	}
	return IRQ_HANDLED;
}
#endif
static int rk616_hdmi_drv_init(struct hdmi *hdmi_drv)
{
	int ret = 0;
	int lcdc_id = 0;
	struct rk_screen screen;

	rk_fb_get_prmry_screen(&screen);

	/* hdmi is extend as default,TODO modify if hdmi is primary */
	lcdc_id = (screen.lcdc_id == 0) ? 1 : 0;
	/* lcdc source select */
	/* wait to modify!!
#if defined(CONFIG_ARCH_RK3026) || defined(SOC_CONFIG_RK3036)
	grf_writel(HDMI_SEL_LCDC(lcdc_id), RK3036_GRF_SOC_CON6);
#endif
	*/
	lcdc_id = 0;
	if (lcdc_id == 0)
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
	hdmi_sys_init(hdmi_drv);
	ret = rk616_hdmi_initial(hdmi_drv);

	return ret;
}

#if defined(CONFIG_OF)
static const struct of_device_id rk616_hdmi_of_match[] = {
	{.compatible = "rockchip,rk616-hdmi",},
	{.compatible = "rockchip,rk3036-hdmi",},
	{}
};

MODULE_DEVICE_TABLE(of, rk616_hdmi_of_match);
#endif

static int rk616_hdmi_probe(struct platform_device *pdev)
{
	int ret;
	//struct rk_hdmi_device *hdmi_dev;
	struct hdmi *hdmi_drv;
	struct resource __maybe_unused *mem;
	struct resource __maybe_unused *res;

	hdmi_dev = devm_kzalloc(&pdev->dev, sizeof(struct rk_hdmi_device),
				GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(&pdev->dev, ">>rk616_hdmi kmalloc fail!");
		return -ENOMEM;
	}

	hdmi_drv = &hdmi_dev->driver;
	hdmi_drv->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdmi_dev);
	spin_lock_init(&hdmi_dev->reg_lock);

#if defined(CONFIG_ARCH_RK3026) || defined(SOC_CONFIG_RK3036)
	hdmi_dev->rk616_drv = NULL;
#else
	hdmi_dev->rk616_drv = dev_get_drvdata(pdev->dev.parent);
	if (!(hdmi_dev->rk616_drv)) {
		dev_err(hdmi_drv->dev, "null mfd device rk616!\n");
		goto err0;
	}
#endif

#ifdef CONFIG_SWITCH
	hdmi_drv->switch_hdmi.name = "hdmi";
	switch_dev_register(&(hdmi_drv->switch_hdmi));
#endif
	hdmi_register_display_sysfs(hdmi_drv, NULL);
	fb_register_client(&rk616_hdmi_fb_notifier);

	hdmi_drv->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(hdmi_drv->delay_work), hdmi_work);
	INIT_DELAYED_WORK(&hdmi_dev->rk616_delay_work, rk616_delay_work_func);

#if defined(CONFIG_ARCH_RK3026) || defined(SOC_CONFIG_RK3036)
	/* enable clk */
	hdmi_dev->hclk = devm_clk_get(hdmi_drv->dev, "pclk_hdmi");
	if (IS_ERR(hdmi_dev->hclk)) {
		dev_err(hdmi_drv->dev, "Unable to get hdmi hclk\n");
		ret = -ENXIO;
		goto err1;
	}
	rk616_hdmi_clk_enable(hdmi_dev);	/* enable clk may move to irq func */

	/* request and remap iomem */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(hdmi_drv->dev, "Unable to get register resource\n");
		ret = -ENXIO;
		goto err2;
	}
	hdmi_dev->regbase_phy = res->start;
	hdmi_dev->regsize_phy = resource_size(res);
	hdmi_dev->regbase = devm_ioremap_resource(hdmi_drv->dev, res);
	if (IS_ERR(hdmi_dev->regbase)) {
		ret = PTR_ERR(hdmi_dev->regbase);
		dev_err(hdmi_drv->dev, "cannot ioremap registers,err=%d\n",
			ret);
		goto err2;
	}

	/* get the IRQ */
	hdmi_drv->irq = platform_get_irq(pdev, 0);
	if (hdmi_drv->irq <= 0) {
		dev_err(hdmi_drv->dev, "failed to get hdmi irq resource (%d).\n",
			hdmi_drv->irq);
		hdmi_drv->irq = 0;
	} else {
		/* request the IRQ */
		#if 0
		ret = devm_request_irq(hdmi_drv->dev, hdmi_drv->irq,
				       rk616_hdmi_irq, 0,
				       dev_name(hdmi_drv->dev), hdmi_drv);
		if (ret) {
			dev_err(hdmi_drv->dev, "hdmi request_irq failed (%d)\n",
				ret);
			goto err2;
		}
		#endif
	}
#else
	if (gpio_is_valid(hdmi_dev->rk616_drv->pdata->hdmi_irq)) {
		INIT_WORK(&hdmi_dev->rk616_irq_work_struct,
			  rk616_irq_work_func);
		ret = gpio_request(hdmi_dev->rk616_drv->pdata->hdmi_irq,
				   "rk616_hdmi_irq");
		if (ret < 0) {
			dev_err(hdmi_drv->dev,
				"request gpio for rk616 hdmi irq fail\n");
		}
		gpio_direction_input(hdmi_dev->rk616_drv->pdata->hdmi_irq);
		hdmi_drv->irq =
		    gpio_to_irq(hdmi_dev->rk616_drv->pdata->hdmi_irq);
		if (hdmi_drv->irq <= 0) {
			dev_err(hdmi_drv->dev,
				"failed to get hdmi irq resource (%d).\n",
				hdmi_drv->irq);
			ret = -ENXIO;
			goto err1;
		}

		/* request the IRQ */
		ret = devm_request_irq(hdmi_drv->dev, hdmi_drv->irq,
				       rk616_hdmi_irq, IRQF_TRIGGER_LOW,
				       dev_name(&pdev->dev),
				       &hdmi_dev->rk616_irq_work_struct);
		if (ret) {
			dev_err(hdmi_drv->dev, "hdmi request_irq failed (%d)\n",
				ret);
			goto err1;
		}
	} else {
		/* use roll polling method */
		hdmi_drv->irq = 0;
	}

#endif
	if (rk616_hdmi_drv_init(hdmi_drv))
		goto err0;

	//rk616_hdmi_work(hdmi_drv);

#if defined(CONFIG_DEBUG_FS)
	if (hdmi_dev->rk616_drv && hdmi_dev->rk616_drv->debugfs_dir) {
		debugfs_create_file("hdmi", S_IRUSR,
				    hdmi_dev->rk616_drv->debugfs_dir,
				    hdmi_dev->rk616_drv, &rk616_hdmi_reg_fops);
	} else {
		hdmi_dev->debugfs_dir = debugfs_create_dir("rk616", NULL);
		if (IS_ERR(hdmi_dev->debugfs_dir)) {
			dev_err(hdmi_drv->dev,
				"failed to create debugfs dir for rk616!\n");
		} else {
			debugfs_create_file("hdmi", S_IRUSR,
					    hdmi_dev->debugfs_dir, hdmi_drv,
					    &rk616_hdmi_reg_fops);
		}
	}
#endif

	queue_delayed_work(hdmi_drv->workqueue, &hdmi_dev->rk616_delay_work,
			   msecs_to_jiffies(0));
	dev_info(hdmi_drv->dev, "rk616 hdmi probe success.\n");

	return 0;

#if defined(CONFIG_ARCH_RK3026) || defined(SOC_CONFIG_RK3036)
err2:
	rk616_hdmi_clk_disable(hdmi_dev);
#endif

err1:
	fb_unregister_client(&rk616_hdmi_fb_notifier);
	hdmi_unregister_display_sysfs(hdmi_drv);
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(hdmi_drv->switch_hdmi));
#endif

err0:
	hdmi_dbg(hdmi_drv->dev, "rk616 hdmi probe error.\n");
	kfree(hdmi_dev);
	hdmi_dev = NULL;
	return ret;
}

static int rk616_hdmi_remove(struct platform_device *pdev)
{
	struct rk_hdmi_device *hdmi_dev = platform_get_drvdata(pdev);
	struct hdmi *hdmi_drv = NULL;

	if (hdmi_dev) {
		hdmi_drv = &hdmi_dev->driver;
		mutex_lock(&hdmi_drv->enable_mutex);
		if (!hdmi_drv->suspend && hdmi_drv->enable && hdmi_drv->irq)
			disable_irq(hdmi_drv->irq);
		mutex_unlock(&hdmi_drv->enable_mutex);
		if (hdmi_drv->irq)
			free_irq(hdmi_drv->irq, NULL);

		flush_workqueue(hdmi_drv->workqueue);
		destroy_workqueue(hdmi_drv->workqueue);
#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi_drv->switch_hdmi));
#endif
		hdmi_unregister_display_sysfs(hdmi_drv);
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi_drv->early_suspend);
#endif
		fb_destroy_modelist(&hdmi_drv->edid.modelist);
		if (hdmi_drv->edid.audio)
			kfree(hdmi_drv->edid.audio);
		if (hdmi_drv->edid.specs) {
			if (hdmi_drv->edid.specs->modedb)
				kfree(hdmi_drv->edid.specs->modedb);
			kfree(hdmi_drv->edid.specs);
		}

		hdmi_dbg(hdmi_drv->dev, "rk616 hdmi removed.\n");
		kfree(hdmi_dev);
		hdmi_dev = NULL;
	}

	return 0;
}

static void rk616_hdmi_shutdown(struct platform_device *pdev)
{
	struct rk_hdmi_device *hdmi_dev = platform_get_drvdata(pdev);
	struct hdmi *hdmi_drv = NULL;

	if (hdmi_dev) {
		hdmi_drv = &hdmi_dev->driver;
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi_drv->early_suspend);
#endif
		flush_delayed_work(&hdmi_drv->delay_work);
		mutex_lock(&hdmi_drv->enable_mutex);
		hdmi_drv->suspend = 1;
		if (!hdmi_drv->enable) {
			mutex_unlock(&hdmi_drv->enable_mutex);
			return;
		}
		if (hdmi_drv->irq)
			disable_irq(hdmi_drv->irq);
		mutex_unlock(&hdmi_drv->enable_mutex);
	}
	hdmi_dbg(hdmi_drv->dev, "rk616 hdmi shut down.\n");
}


static struct platform_driver rk616_hdmi_driver = {
	.probe = rk616_hdmi_probe,
	.remove = rk616_hdmi_remove,
	.driver = {
		   .name = "rk616-hdmi",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk616_hdmi_of_match),		   
		   },
	.shutdown = rk616_hdmi_shutdown,
};

static int __init rk616_hdmi_init(void)
{
	return platform_driver_register(&rk616_hdmi_driver);
}

static void __exit rk616_hdmi_exit(void)
{
	platform_driver_unregister(&rk616_hdmi_driver);
}

late_initcall(rk616_hdmi_init);
module_exit(rk616_hdmi_exit);
