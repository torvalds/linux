/* drivers/adc/chips/rk28_adc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/adc.h>

#include "../adc_priv.h"
#include "rk28_adc.h"

//#define ADC_TEST

struct rk28_adc_device {
	int			 		irq;
	void __iomem		*regs;
	struct clk *		clk;
	struct resource		*ioarea;
	struct adc_host		*adc;
};
static void rk28_adc_start(struct adc_host *adc)
{
	struct rk28_adc_device *dev  = adc_priv(adc);
	int chn = adc->chn;
	
	writel(ADC_CTRL_IRQ_ENABLE|ADC_CTRL_POWER_UP|ADC_CTRL_START|ADC_CTRL_CH(chn),
		dev->regs + ADC_CTRL);
}
static void rk28_adc_stop(struct adc_host *adc)
{
	struct rk28_adc_device *dev  = adc_priv(adc);
	
	writel(ADC_CTRL_IRQ_STATUS, dev->regs + ADC_CTRL);
}
static int rk28_adc_read(struct adc_host *adc)
{
	struct rk28_adc_device *dev  = adc_priv(adc);

	udelay(SAMPLE_RATE);
	return readl(dev->regs + ADC_DATA) & ADC_DATA_MASK;
}
static irqreturn_t rk28_adc_irq(int irq, void *data)
{
	struct rk28_adc_device *dev = data;
	adc_core_irq_handle(dev->adc);
	return IRQ_HANDLED;
}
static const struct adc_ops rk28_adc_ops = {
	.start		= rk28_adc_start,
	.stop		= rk28_adc_stop,
	.read		= rk28_adc_read,
};
#ifdef ADC_TEST
static void callback(struct adc_client *client, void *param, int result)
{
	dev_info(client->adc->dev, "[chn%d] async_read = %d\n", client->chn, result);
	return;
}
static int rk28_adc_test(void)
{
	int sync_read = 0;
	struct adc_client *client = adc_register(1, callback, NULL);

	while(1)
	{
		adc_async_read(client);
		udelay(20);
		sync_read = adc_sync_read(client);
		dev_info(client->adc->dev, "[chn%d] sync_read = %d\n", client->chn, sync_read);
		udelay(20);
	}
	adc_unregister(client);
	return 0;
}
#endif

static int rk28_adc_probe(struct platform_device *pdev)
{
	struct adc_host *adc = NULL;
	struct rk28_adc_device *dev;
	struct resource *res;
	int ret;

	adc = adc_alloc_host(&pdev->dev, sizeof(struct rk28_adc_device), SARADC_CHN_MASK);
	if (!adc)
		return -ENOMEM;
	adc->ops = &rk28_adc_ops;
	dev = adc_priv(adc);
	dev->adc = adc;
	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq <= 0) {
		dev_err(&pdev->dev, "failed to get adc irq\n");
		ret = -ENOENT;
		goto err_alloc;
	}

        ret = request_threaded_irq(dev->irq, NULL, rk28_adc_irq, IRQF_ONESHOT, pdev->name, dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach adc irq\n");
		goto err_alloc;
	}
	dev->clk = clk_get(&pdev->dev, "saradc");
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "failed to get adc clock\n");
		ret = PTR_ERR(dev->clk);
		goto err_irq;
	}

	ret = clk_set_rate(dev->clk, ADC_CLK_RATE * 1000 * 1000);
	if(ret < 0) {
		dev_err(&pdev->dev, "failed to set adc clk\n");
		goto err_clk;
	}
	clk_enable(dev->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}
	dev->ioarea = request_mem_region(res->start, (res->end - res->start) + 1, 
									pdev->name);
	if(dev->ioarea == NULL) {
		dev_err(&pdev->dev, "cannot request IO\n");
		ret = -ENXIO;
		goto err_clk;
	}
	dev->regs = ioremap(res->start, (res->end - res->start) + 1);
	if (!dev->regs) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}
	platform_set_drvdata(pdev, dev);
	dev_info(&pdev->dev, "rk28 adc: driver initialized\n");
#ifdef ADC_TEST	
	rk28_adc_test();
#endif
	return 0;

 err_ioarea:
	release_resource(dev->ioarea);
	kfree(dev->ioarea);
	clk_disable(dev->clk);

 err_clk:
	clk_put(dev->clk);

 err_irq:
	free_irq(dev->irq, dev);

 err_alloc:
	adc_free_host(dev->adc);
	return ret;
}

static int rk28_adc_remove(struct platform_device *pdev)
{
	struct rk28_adc_device *dev = platform_get_drvdata(pdev);

	iounmap(dev->regs);
	release_resource(dev->ioarea);
	kfree(dev->ioarea);
	free_irq(dev->irq, dev);
	clk_disable(dev->clk);
	clk_put(dev->clk);
	adc_free_host(dev->adc);

	return 0;
}

#ifdef CONFIG_PM
static int rk28_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk28_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 1;
	return 0;
}

static int rk28_adc_resume(struct platform_device *pdev)
{
	struct rk28_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 0;
	return 0;
}

#else
#define rk28_adc_suspend NULL
#define rk28_adc_resume NULL
#endif

static struct platform_driver rk28_adc_driver = {
	.driver		= {
		.name	= "rk28-adc",
		.owner	= THIS_MODULE,
	},
	.probe		= rk28_adc_probe,
	.remove		= __devexit_p(rk28_adc_remove),
	.suspend	= rk28_adc_suspend,
	.resume		= rk28_adc_resume,
};

static int __init rk28_adc_init(void)
{
	return platform_driver_register(&rk28_adc_driver);
}
subsys_initcall(rk28_adc_init);

static void __exit rk28_adc_exit(void)
{
	platform_driver_unregister(&rk28_adc_driver);
}
module_exit(rk28_adc_exit);

MODULE_DESCRIPTION("Driver for ADC");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");

