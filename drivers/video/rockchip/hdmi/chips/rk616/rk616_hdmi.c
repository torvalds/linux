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

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"

struct hdmi *hdmi = NULL;
struct mfd_rk616 *g_rk616_hdmi = NULL;
// struct work_struct	g_rk616_delay_work;
struct delayed_work     g_rk616_delay_work;

// extern void hdmi_irq(void);
extern void rk616_hdmi_work(void);
extern void hdmi_work(struct work_struct *work);
extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name);
extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);


#if defined(CONFIG_DEBUG_FS)
static int rk616_hdmi_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;
	struct mfd_rk616 *rk616 = s->private;
	if(!rk616)
	{
		dev_err(rk616->dev,"no mfd rk616!\n");
		return 0;
	}


	//seq_printf(s,"------>gpio = %d \n",gpio_get_value(rk616->pdata->hdmi_irq));
	seq_printf(s, "\n>>>rk616_ctl reg");
	for (i = 0; i < 16; i++) {
		seq_printf(s, " %2x", i);
	}
	seq_printf(s, "\n-----------------------------------------------------------------");
	
	for(i=0;i<= (PHY_PRE_DIV_RATIO << 2);i+=4)
	{
		rk616->read_dev(rk616,RK616_HDMI_BASE + i,&val);
		//seq_printf(s,"reg%02x>>0x%08x\n",(i>>2),val);
		if((i>>2)%16==0)
			seq_printf(s,"\n>>>rk616_ctl %2x:",i>>2);
		seq_printf(s," %02x",val);

	}
	
	seq_printf(s, "\n-----------------------------------------------------------------\n");
	
	return 0;
}

static ssize_t rk616_hdmi_reg_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{ 
	struct mfd_rk616 *rk616 = file->f_path.dentry->d_inode->i_private;
	u32 reg;
	u32 val;
	char kbuf[25];
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg, &val);
        if ((reg < 0) || (reg > 0xed))
        {
                printk("it is no hdmi reg\n");
                return count;
        }
	printk("/**********rk616 reg config******/");
	printk("\n reg=%x val=%x\n", reg, val);

	//sscanf(kbuf, "%x%x", &reg, &val);
	dev_dbg(rk616->dev,"%s:reg:0x%04x val:0x%08x\n",__func__,reg, val);
	rk616->write_dev(rk616, RK616_HDMI_BASE + (reg << 2), &val);

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
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");
	mutex_lock(&hdmi->enable_mutex);

	hdmi->suspend = 0;
	rk616_hdmi_initial();
	if(hdmi->enable && hdmi->irq) {
		enable_irq(hdmi->irq);
		// hdmi_irq();
                rk616_hdmi_work();
	}
        if (g_rk616_hdmi->pdata->hdmi_irq == INVALID_GPIO) 
        	queue_delayed_work(hdmi->workqueue, &g_rk616_delay_work, 100);

	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif
static void rk616_delay_work_func(struct work_struct *work)
{
	if(hdmi->suspend == 0) {
		if(hdmi->enable == 1) {
			//hdmi_irq();
                        rk616_hdmi_work();
		}

                if (g_rk616_hdmi->pdata->hdmi_irq == INVALID_GPIO) {
        	        queue_delayed_work(hdmi->workqueue, &g_rk616_delay_work, 100);
                }
	}
}

#if 1
static irqreturn_t rk616_hdmi_irq(int irq, void *dev_id)
{
	printk(KERN_INFO "rk616_hdmi_irq irq triggered.\n");
	queue_delayed_work(hdmi->workqueue, &g_rk616_delay_work, 0);
        return IRQ_HANDLED;
}
#endif
static int __devinit rk616_hdmi_probe (struct platform_device *pdev)
{
	int ret;

	struct mfd_rk616 *rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}

	g_rk616_hdmi = rk616;

	hdmi = kmalloc(sizeof(struct hdmi), GFP_KERNEL);
	if(!hdmi)
	{
		dev_err(&pdev->dev, ">>rk616 hdmi kmalloc fail!");
		return -ENOMEM;
	}
	memset(hdmi, 0, sizeof(struct hdmi));
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

	ret = rk616_hdmi_initial();

	hdmi_sys_init();

	hdmi->workqueue = create_singlethread_workqueue("hdmi");
	INIT_DELAYED_WORK(&(hdmi->delay_work), hdmi_work);

#ifdef CONFIG_HAS_EARLYSUSPEND
	hdmi->early_suspend.suspend = hdmi_early_suspend;
	hdmi->early_suspend.resume = hdmi_early_resume;
	hdmi->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 10;
	register_early_suspend(&hdmi->early_suspend);
#endif

	hdmi_register_display_sysfs(hdmi, NULL);
#ifdef CONFIG_SWITCH
	hdmi->switch_hdmi.name="hdmi";
	switch_dev_register(&(hdmi->switch_hdmi));
#endif

	spin_lock_init(&hdmi->irq_lock);
	mutex_init(&hdmi->enable_mutex);

	INIT_DELAYED_WORK(&g_rk616_delay_work, rk616_delay_work_func);
	/* get the IRQ */
	if(rk616->pdata->hdmi_irq != INVALID_GPIO)
	{
		// INIT_DELAYED_WORK(&g_rk616_delay_work, rk616_delay_work_func);
		ret = gpio_request(rk616->pdata->hdmi_irq,"rk616_hdmi_irq");
		if(ret < 0)
		{
			dev_err(hdmi->dev,"request gpio for rk616 hdmi irq fail\n");
		}
		gpio_direction_input(rk616->pdata->hdmi_irq);
		hdmi->irq = gpio_to_irq(rk616->pdata->hdmi_irq);
		if(hdmi->irq <= 0) {
			dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
			ret = -ENXIO;
			goto err1;
		}

		/* request the IRQ */
		ret = request_irq(hdmi->irq,rk616_hdmi_irq,IRQF_TRIGGER_FALLING,dev_name(&pdev->dev), hdmi);
		if (ret)
		{
			dev_err(hdmi->dev, "hdmi request_irq failed (%d).\n", ret);
			goto err1;
		}
	} else {

                /* use roll polling method */
		hdmi->irq = 0;
        }
#if defined(CONFIG_DEBUG_FS)
	if(rk616->debugfs_dir)
	{
		debugfs_create_file("hdmi", S_IRUSR,rk616->debugfs_dir,rk616,&rk616_hdmi_reg_fops);
	}
#endif
	// rk616_delay_work_func(NULL);
	queue_delayed_work(hdmi->workqueue, &g_rk616_delay_work, msecs_to_jiffies(0));	
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
	printk(KERN_INFO "rk616 hdmi removed.\n");
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
        printk(KERN_INFO "rk616 hdmi shut down.\n");
}

static struct platform_driver rk616_hdmi_driver = {
	.probe		= rk616_hdmi_probe,
	.remove		= __devexit_p(rk616_hdmi_remove),
	.driver		= {
		.name	= "rk616-hdmi",
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
