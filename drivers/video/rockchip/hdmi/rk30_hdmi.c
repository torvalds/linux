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
extern struct rk_lcdc_device_driver * rk_get_lcdc_drv(int  id);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void hdmi_early_suspend(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi enter early suspend\n");
	disable_irq(hdmi->irq);
	if(hdmi->hotplug)
		hdmi_sys_remove();
	return;
}

static void hdmi_early_resume(struct early_suspend *h)
{
	hdmi_dbg(hdmi->dev, "hdmi exit early resume\n");
	enable_irq(hdmi->irq);
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
	
	// internal hclk = hdmi_hclk/32
	HDMIWrReg(0x800, 19);
	
	hdmi->lcdc = rk_get_lcdc_drv(HDMI_SOURCE_DEFAULT);
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
	hdmi->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1;
	register_early_suspend(&hdmi->early_suspend);
	#endif

	/* get the IRQ */
	hdmi->irq = platform_get_irq(pdev, 0);
	if(hdmi->irq <= 0) {
		dev_err(hdmi->dev, "failed to get hdmi irq resource (%d).\n", hdmi->irq);
		ret = -ENXIO;
		goto err2;
	}
	hdmi_dbg(hdmi->dev, "[%s] hdmi irq is 0x%x\n", __FUNCTION__, hdmi->irq);
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
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&hdmi->early_suspend);
#endif
	iounmap((void*)hdmi->regbase);
err1:
	release_mem_region(res->start,(res->end - res->start) + 1);
	clk_disable(hdmi->hclk);
err0:
	kfree(hdmi);
	hdmi_dbg(hdmi->dev, "rk30 hdmi probe error.\n");
	return ret;
}

static int __devexit rk30_hdmi_remove(struct platform_device *pdev)
{
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


fs_initcall(rk30_hdmi_init);
//module_init(rk30_hdmi_init);
module_exit(rk30_hdmi_exit);