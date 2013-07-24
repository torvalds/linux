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

#include <mach/board.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/rk_fb.h> 

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"

extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);

struct hdmi *hdmi = NULL;

#if defined(CONFIG_DEBUG_FS)
static int rk616_hdmi_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;
	seq_printf(s, "\n>>>rk616_ctl reg");
	for (i = 0; i < 16; i++) {
		seq_printf(s, " %2x", i);
	}
	seq_printf(s, "\n-----------------------------------------------------------------");
	
	for(i=0; i<= PHY_PRE_DIV_RATIO; i++) {
                hdmi_readl(i, &val);
		if(i%16==0)
			seq_printf(s,"\n>>>rk616_ctl %2x:", i);
		seq_printf(s," %02x",val);

	}
	seq_printf(s, "\n-----------------------------------------------------------------\n");
	
	return 0;
}

static ssize_t rk616_hdmi_reg_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{ 
	u32 reg;
	u32 val;
	char kbuf[25];
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg, &val);
        if ((reg < 0) || (reg > 0xed)) {
                dev_info(hdmi->dev, "it is no hdmi reg\n");
                return count;
        }
	dev_info(hdmi->dev, "/**********rk616 reg config******/");
	dev_info(hdmi->dev, "\n reg=%x val=%x\n", reg, val);
        hdmi_writel(reg, val);

	return count;
}

static int rk616_hdmi_reg_open(struct inode *inode, struct file *file)
{
	struct mfd_rk616 *rk616 = inode->i_private;
	return single_open(file,rk616_hdmi_reg_show,rk616);
}

static const struct file_operations rk616_hdmi_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= rk616_hdmi_reg_open,
	.read		= seq_read,
	.write          = rk616_hdmi_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

int rk616_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
		void (*hdcp_irq_cb)(int status),
		int (*hdcp_power_on_cb)(void),
		void (*hdcp_power_off_cb)(void))
{
	if(hdmi == NULL)
		return HDMI_ERROR_FALSE;

	hdmi->hdcp_cb = hdcp_cb;
	hdmi->hdcp_irq_cb = hdcp_irq_cb;
	hdmi->hdcp_power_on_cb = hdcp_power_on_cb;
	hdmi->hdcp_power_off_cb = hdcp_power_off_cb;

	return HDMI_ERROR_SUCESS;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void hdmi_early_suspend(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi enter early suspend pwr %d state %d\n", hdmi->pwr_mode, hdmi->state);

	flush_delayed_work(&hdmi->delay_work);	
	mutex_lock(&hdmi->enable_mutex);
	hdmi->suspend = 1;
	if(!hdmi->enable) {
		mutex_unlock(&hdmi->enable_mutex);
		return;
	}

        if (hdmi->irq)
        	disable_irq(hdmi->irq);

	mutex_unlock(&hdmi->enable_mutex);
	hdmi->command = HDMI_CONFIG_ENABLE;
	init_completion(&hdmi->complete);
	hdmi->wait = 1;
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 0);
	wait_for_completion_interruptible_timeout(&hdmi->complete,
			msecs_to_jiffies(5000));
	flush_delayed_work(&hdmi->delay_work);

	return;
}

static void hdmi_early_resume(struct early_suspend *h)
{
        struct rk616_hdmi *rk616_hdmi;

        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");

	mutex_lock(&hdmi->enable_mutex);

	hdmi->suspend = 0;
	rk616_hdmi_initial();
	if(hdmi->enable && hdmi->irq) {
		enable_irq(hdmi->irq);
		// hdmi_irq();
                rk616_hdmi_work();
	}
        if (rk616_hdmi->rk616_drv && rk616_hdmi->rk616_drv->pdata->hdmi_irq == INVALID_GPIO) 
                queue_delayed_work(hdmi->workqueue, &rk616_hdmi->rk616_delay_work, 100);
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif
static void rk616_delay_work_func(struct work_struct *work)
{
        struct rk616_hdmi *rk616_hdmi;

        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);

	if(hdmi->suspend == 0) {
		if(hdmi->enable == 1) {
			//hdmi_irq();
                        rk616_hdmi_work();
		}

                if (rk616_hdmi->rk616_drv && rk616_hdmi->rk616_drv->pdata->hdmi_irq == INVALID_GPIO) {
                        queue_delayed_work(hdmi->workqueue, &rk616_hdmi->rk616_delay_work, 100);
                }
	}
}

static void __maybe_unused rk616_irq_work_func(struct work_struct *work)
{
	if((hdmi->suspend == 0) && (hdmi->enable == 1)) {
                rk616_hdmi_work();
	}
	dev_info(hdmi->dev, "func: %s, enable_irq\n", __func__);
        enable_irq(hdmi->irq);
}

static irqreturn_t rk616_hdmi_irq(int irq, void *dev_id)
{
        struct work_struct  *rk616_irq_work_struct;
        struct rk616_hdmi *rk616_hdmi;

        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);
        if(rk616_hdmi->rk616_drv) {
                rk616_irq_work_struct  = dev_id;
                disable_irq_nosync(hdmi->irq);
                queue_work(hdmi->workqueue, rk616_irq_work_struct);
        } else {
                /* 3028a hdmi */
                if((hdmi->suspend == 0) && (hdmi->enable == 1)) {
                        printk(KERN_INFO "line = %d, rk616_hdmi_irq irq triggered.\n", __LINE__);
                        rk616_hdmi_work();
                }
        }
        return IRQ_HANDLED;
}

static int __devinit rk616_hdmi_probe (struct platform_device *pdev)
{
	int ret;
        struct rk616_hdmi *rk616_hdmi;
        struct resource __maybe_unused *mem;
        struct resource __maybe_unused *res;

        rk616_hdmi = devm_kzalloc(&pdev->dev, sizeof(*rk616_hdmi), GFP_KERNEL);
        if(!rk616_hdmi)
	{
		dev_err(&pdev->dev, ">>rk616_hdmi kmalloc fail!");
		return -ENOMEM;
	}
        hdmi = &rk616_hdmi->g_hdmi;

#ifdef CONFIG_ARCH_RK3026
	rk616_hdmi->rk616_drv = NULL;
#else
	rk616_hdmi->rk616_drv = dev_get_drvdata(pdev->dev.parent);
	if(!(rk616_hdmi->rk616_drv))
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}

#endif

	hdmi->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdmi);

	if(HDMI_SOURCE_DEFAULT == HDMI_SOURCE_LCDC0)
		hdmi->lcdc = rk_get_lcdc_drv("lcdc0");
	else
		hdmi->lcdc = rk_get_lcdc_drv("lcdc1");
	if(hdmi->lcdc == NULL)
	{
		dev_err(hdmi->dev, "can not connect to video source lcdc\n");
		ret = -ENXIO;
		goto err0;
	}
	hdmi->xscale = 100;
	hdmi->yscale = 100;


	hdmi_sys_init();

	hdmi->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(hdmi->delay_work), hdmi_work);

#ifdef CONFIG_HAS_EARLYSUSPEND
	hdmi->early_suspend.suspend = hdmi_early_suspend;
	hdmi->early_suspend.resume = hdmi_early_resume;
	hdmi->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 10;
	register_early_suspend(&hdmi->early_suspend);
#endif

#ifdef CONFIG_SWITCH
	hdmi->switch_hdmi.name="hdmi";
	switch_dev_register(&(hdmi->switch_hdmi));
#endif

	spin_lock_init(&hdmi->irq_lock);
	mutex_init(&hdmi->enable_mutex);

	INIT_DELAYED_WORK(&rk616_hdmi->rk616_delay_work, rk616_delay_work_func);

	/* get the IRQ */
	// if(rk616->pdata->hdmi_irq != INVALID_GPIO)
        
#ifdef CONFIG_ARCH_RK3026
        hdmi->hclk = clk_get(NULL,"pclk_hdmi");
        if(IS_ERR(hdmi->hclk)) {
                dev_err(hdmi->dev, "Unable to get hdmi hclk\n");
                ret = -ENXIO;
                goto err0;
        }
        clk_enable(hdmi->hclk);
        
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
                dev_err(hdmi->dev, "Unable to get register resource\n");
                ret = -ENXIO;
                goto err0;
        }
        hdmi->regbase_phy = res->start;
        hdmi->regsize_phy = (res->end - res->start) + 1;
        mem = request_mem_region(res->start, (res->end - res->start) + 1, pdev->name);
        if (!mem) {
                dev_err(hdmi->dev, "failed to request mem region for hdmi\n");
                ret = -ENOENT;
                goto err0;
        }
        
        printk("res->start = 0x%x\n, xhc-------res->end = 0x%x\n", res->start, res->end);
        hdmi->regbase = (int)ioremap(res->start, (res->end - res->start) + 1);
        if (!hdmi->regbase) {
                dev_err(hdmi->dev, "cannot ioremap registers\n");
                ret = -ENXIO;
                goto err1;
        }
        
        // rk30_mux_api_set(GPIO0A7_I2C3_SDA_HDMI_DDCSDA_NAME, GPIO0A_HDMI_DDCSDA);
        // rk30_mux_api_set(GPIO0A6_I2C3_SCL_HDMI_DDCSCL_NAME, GPIO0A_HDMI_DDCSCL);
        // rk30_mux_api_set(GPIO0B7_HDMI_HOTPLUGIN_NAME, GPIO0B_HDMI_HOTPLUGIN);
        iomux_set(HDMI_DDCSDA);
        iomux_set(HDMI_DDCSCL);
        iomux_set(HDMI_HOTPLUGIN);
        
        ret = rk616_hdmi_initial();
        /* get the IRQ */
        hdmi->irq = platform_get_irq(pdev, 0);
        if(hdmi->irq <= 0) {
                dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
                hdmi->irq = 0;
        } else {               
                /* request the IRQ */
                ret = request_irq(hdmi->irq, rk616_hdmi_irq, 0, dev_name(&pdev->dev), hdmi);
                if (ret) {
                        dev_err(hdmi->dev, "hdmi request_irq failed (%d).\n", ret);
                        goto err1;
                }
        }
#else
        ret = rk616_hdmi_initial();
        if(rk616_hdmi->rk616_drv->pdata->hdmi_irq != INVALID_GPIO) {               
                INIT_WORK(&rk616_hdmi->rk616_irq_work_struct, rk616_irq_work_func);
                ret = gpio_request(rk616_hdmi->rk616_drv->pdata->hdmi_irq,"rk616_hdmi_irq");
                if(ret < 0) {
                        dev_err(hdmi->dev,"request gpio for rk616 hdmi irq fail\n");
                }
                gpio_direction_input(rk616_hdmi->rk616_drv->pdata->hdmi_irq);
                hdmi->irq = gpio_to_irq(rk616_hdmi->rk616_drv->pdata->hdmi_irq);
                if(hdmi->irq <= 0) {
                        dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
                        ret = -ENXIO;
                        goto err1;
                }
                
                /* request the IRQ */
                ret = request_irq(hdmi->irq, rk616_hdmi_irq, IRQF_TRIGGER_LOW, dev_name(&pdev->dev), &rk616_hdmi->rk616_irq_work_struct);
                if (ret) {
                        dev_err(hdmi->dev, "hdmi request_irq failed (%d).\n", ret);
                        goto err1;
                }
        } else {                
                /* use roll polling method */
                hdmi->irq = 0;
        }

#endif
	hdmi_register_display_sysfs(hdmi, NULL);

#if defined(CONFIG_DEBUG_FS)
	if(rk616_hdmi->rk616_drv && rk616_hdmi->rk616_drv->debugfs_dir) {
		debugfs_create_file("hdmi", S_IRUSR, rk616_hdmi->rk616_drv->debugfs_dir, rk616_hdmi->rk616_drv, &rk616_hdmi_reg_fops);
	} else {
                rk616_hdmi->debugfs_dir = debugfs_create_dir("rk616", NULL);
                if (IS_ERR(rk616_hdmi->debugfs_dir)) {
                        dev_err(hdmi->dev,"failed to create debugfs dir for rk616!\n");
                } else {
                        debugfs_create_file("hdmi", S_IRUSR, rk616_hdmi->debugfs_dir, rk616_hdmi, &rk616_hdmi_reg_fops);
                }
        }
#endif
	// rk616_delay_work_func(NULL);
	queue_delayed_work(hdmi->workqueue, &rk616_hdmi->rk616_delay_work, msecs_to_jiffies(0));
	dev_info(hdmi->dev, "rk616 hdmi probe success.\n");
	return 0;
err1:
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(hdmi->switch_hdmi));
#endif
	hdmi_unregister_display_sysfs(hdmi);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&hdmi->early_suspend);
#endif
err0:
	hdmi_dbg(hdmi->dev, "rk616 hdmi probe error.\n");
	kfree(hdmi);
	hdmi = NULL;
	return ret;
}

static int __devexit rk616_hdmi_remove(struct platform_device *pdev)
{
	if(hdmi) {
		mutex_lock(&hdmi->enable_mutex);
		if(!hdmi->suspend && hdmi->enable && hdmi->irq)
			disable_irq(hdmi->irq);
		mutex_unlock(&hdmi->enable_mutex);
                if (hdmi->irq) {
        		free_irq(hdmi->irq, NULL);
                }
		flush_workqueue(hdmi->workqueue);
		destroy_workqueue(hdmi->workqueue);
#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi->switch_hdmi));
#endif
		hdmi_unregister_display_sysfs(hdmi);
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
#endif
		fb_destroy_modelist(&hdmi->edid.modelist);
		if(hdmi->edid.audio)
			kfree(hdmi->edid.audio);
		if(hdmi->edid.specs)
		{
			if(hdmi->edid.specs->modedb)
				kfree(hdmi->edid.specs->modedb);
			kfree(hdmi->edid.specs);
		}
		kfree(hdmi);
		hdmi = NULL;
	}
	hdmi_dbg(hdmi->dev, "rk616 hdmi removed.\n");
	return 0;
}

static void rk616_hdmi_shutdown(struct platform_device *pdev)
{
	if(hdmi) {
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
#endif 
                flush_delayed_work(&hdmi->delay_work);
                mutex_lock(&hdmi->enable_mutex);
                hdmi->suspend = 1;
                if(!hdmi->enable) {
                        mutex_unlock(&hdmi->enable_mutex);
                        return;
                }
                if (hdmi->irq)
                        disable_irq(hdmi->irq);
                mutex_unlock(&hdmi->enable_mutex);
        }
        hdmi_dbg(hdmi->dev,  "rk616 hdmi shut down.\n");
}

static struct platform_driver rk616_hdmi_driver = {
	.probe		= rk616_hdmi_probe,
	.remove		= __devexit_p(rk616_hdmi_remove),
	.driver		= {
#ifdef CONFIG_ARCH_RK3026
		.name	= "rk3026-hdmi",
#else
		.name	= "rk616-hdmi",
#endif
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk616_hdmi_shutdown,
};

static int __init rk616_hdmi_init(void)
{
	return platform_driver_register(&rk616_hdmi_driver);
}

static void __exit rk616_hdmi_exit(void)
{
	platform_driver_unregister(&rk616_hdmi_driver);
}


//fs_initcall(rk616_hdmi_init);
late_initcall(rk616_hdmi_init);
module_exit(rk616_hdmi_exit);
