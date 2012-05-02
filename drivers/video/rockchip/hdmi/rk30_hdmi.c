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
	return;
}

static void hdmi_early_resume(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");
	mutex_lock(&hdmi->enable_mutex);
	hdmi->suspend = 0;
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
	
	// internal hclk = hdmi_hclk/20
	HDMIWrReg(0x800, 19);
}

static int __devinit rk30_hdmi_probe (struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct resource *mem;
	
	hdmi = kmalloc(sizeof(struct hdmi), GFP_KERNEL);
	if(!hdmi)
	{
    	dev_err(&pdev->dev, ">>rk30 lcdc inf kmalloc fail!");
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
		
	hdmi_register_display_sysfs(hdmi, hdmi->dev);
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
	return ret;
}

static int __devexit rk30_hdmi_remove(struct platform_device *pdev)
{
	flush_scheduled_work();
	destroy_workqueue(hdmi->workqueue);
	#ifdef CONFIG_SWITCH
	switch_dev_unregister(&(hdmi->switch_hdmi));
	#endif
	#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&hdmi->early_suspend);
	#endif
	iounmap((void*)hdmi->regbase);
//	release_mem_region(res->start,(res->end - res->start) + 1);
	clk_disable(hdmi->hclk);
	kfree(hdmi);
	hdmi_dbg(hdmi->dev, "rk30 hdmi unregistered.\n");
	return 0;
}

static void rk30_hdmi_shutdown(struct platform_device *pdev)
{

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