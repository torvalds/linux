/* drivers/adc/chips/rk29_adc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/adc.h>

#include "../adc_priv.h"
#include "rk29_adc.h"

//#define ADC_TEST

struct rk29_adc_device {
	int			 		irq;
	void __iomem		*regs;
	struct clk *		clk;
	struct resource		*ioarea;
	struct adc_host		*adc;
};
static void rk29_adc_start(struct adc_host *adc)
{
	struct rk29_adc_device *dev  = adc_priv(adc);
	int chn = adc->cur->chn;

	writel(0, dev->regs + ADC_CTRL);
	writel(ADC_CTRL_POWER_UP|ADC_CTRL_CH(chn), dev->regs + ADC_CTRL);
	udelay(SAMPLE_RATE);

	writel(readl(dev->regs + ADC_CTRL)|ADC_CTRL_IRQ_ENABLE|ADC_CTRL_START, 
		dev->regs + ADC_CTRL);
	return;
}
static void rk29_adc_stop(struct adc_host *adc)
{
	struct rk29_adc_device *dev  = adc_priv(adc);
	
	writel(0, dev->regs + ADC_CTRL);
}
static int rk29_adc_read(struct adc_host *adc)
{
	struct rk29_adc_device *dev  = adc_priv(adc);

	udelay(SAMPLE_RATE);
	return readl(dev->regs + ADC_DATA) & ADC_DATA_MASK;
}
static irqreturn_t rk29_adc_irq(int irq, void *data)
{
	struct rk29_adc_device *dev = data;
	adc_core_irq_handle(dev->adc);
	return IRQ_HANDLED;
}
static const struct adc_ops rk29_adc_ops = {
	.start		= rk29_adc_start,
	.stop		= rk29_adc_stop,
	.read		= rk29_adc_read,
};
#ifdef ADC_TEST
struct adc_test_data {
	struct adc_client *client;
	struct timer_list timer;
	struct work_struct 	timer_work;
};
static void callback(struct adc_client *client, void *param, int result)
{
	dev_info(client->adc->dev, "[chn%d] async_read = %d\n", client->chn, result);
	return;
}
static void adc_timer(unsigned long data)
{
	//int sync_read = 0;
	 struct adc_test_data *test=(struct adc_test_data *)data;
	
	//sync_read = adc_sync_read(test->client);
	//dev_info(test->client->adc->dev, "[chn%d] sync_read = %d\n", 0, sync_read);
	schedule_work(&test->timer_work);
	add_timer(&test->timer);
}
static void adc_timer_work(struct work_struct *work)
{	
	int sync_read = 0;
	struct adc_test_data *test = container_of(work, struct adc_test_data,
						timer_work);
	adc_async_read(test->client);
	sync_read = adc_sync_read(test->client);
	dev_info(test->client->adc->dev, "[chn%d] sync_read = %d\n", 0, sync_read);
}

static int rk29_adc_test(void)
{
	struct adc_test_data *test = NULL;

	test = kzalloc(sizeof(struct adc_test_data), GFP_KERNEL);
	
	test->client = adc_register(0, callback, NULL);
	INIT_WORK(&test->timer_work, adc_timer_work);
	setup_timer(&test->timer, adc_timer, (unsigned long)test);
	test->timer.expires  = jiffies + 100;
	add_timer(&test->timer);
	
	return 0;

}
#endif

static int rk29_adc_probe(struct platform_device *pdev)
{
	struct adc_host *adc = NULL;
	struct rk29_adc_device *dev;
	struct resource *res;
	int ret;

	adc = adc_alloc_host(&pdev->dev, sizeof(struct rk29_adc_device), SARADC_CHN_MASK);
	if (!adc)
		return -ENOMEM;
	adc->ops = &rk29_adc_ops;
	dev = adc_priv(adc);
	dev->adc = adc;
	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq <= 0) {
		dev_err(&pdev->dev, "failed to get adc irq\n");
		ret = -ENOENT;
		goto err_alloc;
	}

	ret = request_irq(dev->irq, rk29_adc_irq, 0, pdev->name, dev);
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
	dev_info(&pdev->dev, "rk29 adc: driver initialized\n");
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

static int rk29_adc_remove(struct platform_device *pdev)
{
	struct rk29_adc_device *dev = platform_get_drvdata(pdev);

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
static int rk29_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk29_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 1;
	return 0;
}

static int rk29_adc_resume(struct platform_device *pdev)
{
	struct rk29_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 0;
	return 0;
}

#else
#define rk29_adc_suspend NULL
#define rk29_adc_resume NULL
#endif

static struct platform_driver rk29_adc_driver = {
	.driver		= {
		.name	= "rk29-adc",
		.owner	= THIS_MODULE,
	},
	.probe		= rk29_adc_probe,
	.remove		= __devexit_p(rk29_adc_remove),
	.suspend	= rk29_adc_suspend,
	.resume		= rk29_adc_resume,
};

static int __init rk29_adc_init(void)
{
	return platform_driver_register(&rk29_adc_driver);
}
subsys_initcall(rk29_adc_init);

static void __exit rk29_adc_exit(void)
{
	platform_driver_unregister(&rk29_adc_driver);
}
module_exit(rk29_adc_exit);

MODULE_DESCRIPTION("Driver for ADC");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
static int __init adc_test_init(void)
{
#ifdef ADC_TEST	
		rk29_adc_test();
#endif
	return 0;

}
module_init(adc_test_init);
