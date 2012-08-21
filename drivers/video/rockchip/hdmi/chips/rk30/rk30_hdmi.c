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

#include <mach/board.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "rk30_hdmi.h"
#include "rk30_hdmi_hw.h"

struct hdmi *hdmi = NULL;

extern irqreturn_t hdmi_irq(int irq, void *priv);
extern void hdmi_work(struct work_struct *work);
extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name);
extern void hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent);
extern void hdmi_unregister_display_sysfs(struct hdmi *hdmi);

int rk30_hdmi_register_hdcp_callbacks(void (*hdcp_cb)(void),
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
	disable_irq(hdmi->irq);
	mutex_unlock(&hdmi->enable_mutex);
	hdmi->command = HDMI_CONFIG_ENABLE;
	init_completion(&hdmi->complete);
	hdmi->wait = 1;
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 0);
	wait_for_completion_interruptible_timeout(&hdmi->complete,
							msecs_to_jiffies(5000));
	flush_delayed_work(&hdmi->delay_work);
	// When HDMI 1.1V and 2.5V power off, DDC channel will be pull down, current is produced
	// from VCC_IO which is pull up outside soc. We need to switch DDC IO to GPIO.
	rk30_mux_api_set(GPIO0A2_HDMII2CSDA_NAME, GPIO0A_GPIO0A2);
	rk30_mux_api_set(GPIO0A1_HDMII2CSCL_NAME, GPIO0A_GPIO0A1);
	return;
}

static void hdmi_early_resume(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");
	mutex_lock(&hdmi->enable_mutex);
	
	rk30_mux_api_set(GPIO0A2_HDMII2CSDA_NAME, GPIO0A_HDMI_I2C_SDA);
	rk30_mux_api_set(GPIO0A1_HDMII2CSCL_NAME, GPIO0A_HDMI_I2C_SCL);
	
	hdmi->suspend = 0;
	rk30_hdmi_initial();
	if(hdmi->enable) {
		enable_irq(hdmi->irq);
	}
	mutex_unlock(&hdmi->enable_mutex);
	return;
}
#endif

static inline void hdmi_io_remap(void)
{
	unsigned int value;
	
	// Remap HDMI IO Pin
	rk30_mux_api_set(GPIO0A2_HDMII2CSDA_NAME, GPIO0A_HDMI_I2C_SDA);
	rk30_mux_api_set(GPIO0A1_HDMII2CSCL_NAME, GPIO0A_HDMI_I2C_SCL);
	rk30_mux_api_set(GPIO0A0_HDMIHOTPLUGIN_NAME, GPIO0A_HDMI_HOT_PLUG_IN);
		
	// Select LCDC0 as video source and enabled.
	value = (HDMI_SOURCE_DEFAULT << 14) | (1 << 30);
	writel(value, GRF_SOC_CON0 + RK30_GRF_BASE);
}

static int __devinit rk30_hdmi_probe (struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct resource *mem;
	
	hdmi = kmalloc(sizeof(struct hdmi), GFP_KERNEL);
	if(!hdmi)
	{
    	dev_err(&pdev->dev, ">>rk30 hdmi kmalloc fail!");
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
	hdmi->xscale = 95;
	hdmi->yscale = 95;
	
	hdmi->hclk = clk_get(NULL,"hclk_hdmi");
	if(IS_ERR(hdmi->hclk))
	{
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
	if (!mem)
	{
    	dev_err(hdmi->dev, "failed to request mem region for hdmi\n");
    	ret = -ENOENT;
    	goto err0;
	}

	
	hdmi->regbase = (int)ioremap(res->start, (res->end - res->start) + 1);
	if (!hdmi->regbase) {
		dev_err(hdmi->dev, "cannot ioremap registers\n");
		ret = -ENXIO;
		goto err1;
	}
	
	ret = rk30_hdmi_initial();
	if(ret != HDMI_ERROR_SUCESS)
		goto err1;
		
	hdmi_io_remap();
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
	
	/* get the IRQ */
	hdmi->irq = platform_get_irq(pdev, 0);
	if(hdmi->irq <= 0) {
		dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
		ret = -ENXIO;
		goto err2;
	}

	/* request the IRQ */
	ret = request_irq(hdmi->irq, hdmi_irq, 0, dev_name(&pdev->dev), hdmi);
	if (ret)
	{
		dev_err(hdmi->dev, "hdmi request_irq failed (%d).\n", ret);
		goto err2;
	}

	hdmi_dbg(hdmi->dev, "rk30 hdmi probe sucess.\n");
	return 0;
err2:
	#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(hdmi->switch_hdmi));
	#endif
	hdmi_unregister_display_sysfs(hdmi);
	#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&hdmi->early_suspend);
	#endif
	iounmap((void*)hdmi->regbase);
err1:
	release_mem_region(res->start,(res->end - res->start) + 1);
	clk_disable(hdmi->hclk);
err0:
	hdmi_dbg(hdmi->dev, "rk30 hdmi probe error.\n");
	kfree(hdmi);
	hdmi = NULL;
	return ret;
}

static int __devexit rk30_hdmi_remove(struct platform_device *pdev)
{
	if(hdmi) {
		mutex_lock(&hdmi->enable_mutex);
		if(!hdmi->suspend && hdmi->enable)
			disable_irq(hdmi->irq);
		mutex_unlock(&hdmi->enable_mutex);
		free_irq(hdmi->irq, NULL);
		flush_workqueue(hdmi->workqueue);
		destroy_workqueue(hdmi->workqueue);
		#ifdef CONFIG_SWITCH
		switch_dev_unregister(&(hdmi->switch_hdmi));
		#endif
		hdmi_unregister_display_sysfs(hdmi);
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
		#endif
		iounmap((void*)hdmi->regbase);
		release_mem_region(hdmi->regbase_phy, hdmi->regsize_phy);
		clk_disable(hdmi->hclk);
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
	printk(KERN_INFO "rk30 hdmi removed.\n");
	return 0;
}

static void rk30_hdmi_shutdown(struct platform_device *pdev)
{
	if(hdmi) {
		#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&hdmi->early_suspend);
		#endif
	}
	printk(KERN_INFO "rk30 hdmi shut down.\n");
}

static struct platform_driver rk30_hdmi_driver = {
	.probe		= rk30_hdmi_probe,
	.remove		= __devexit_p(rk30_hdmi_remove),
	.driver		= {
		.name	= "rk30-hdmi",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk30_hdmi_shutdown,
};

static int __init rk30_hdmi_init(void)
{
    return platform_driver_register(&rk30_hdmi_driver);
}

static void __exit rk30_hdmi_exit(void)
{
    platform_driver_unregister(&rk30_hdmi_driver);
}


//fs_initcall(rk30_hdmi_init);
module_init(rk30_hdmi_init);
module_exit(rk30_hdmi_exit);
