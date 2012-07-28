/* drivers/adc/chips/rk30_adc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/adc.h>

#include "../adc_priv.h"
#include "rk30_adc.h"

//#define ADC_TEST

struct rk30_adc_device {
	int			 irq;
	void __iomem		*regs;
	struct clk		*clk;
	struct clk		*pclk;
	struct resource		*ioarea;
	struct adc_host		*adc;
};
static void rk30_adc_dump(struct adc_host *adc)
{
	struct rk30_adc_device *dev  = adc_priv(adc);

        dev_info(adc->dev, "[0x00-0x0c]: 0x%08x 0x%08x 0x%08x 0x%08x\n",
                        adc_readl(dev->regs + 0x00),
                        adc_readl(dev->regs + 0x04),
                        adc_readl(dev->regs + 0x08),
                        adc_readl(dev->regs + 0x0c));
}
static void rk30_adc_start(struct adc_host *adc)
{
	struct rk30_adc_device *dev  = adc_priv(adc);
	int chn = adc->chn;

	//adc_writel(0, dev->regs + ADC_CTRL);
        adc_writel(0x08, dev->regs + ADC_DELAY_PU_SOC);
	adc_writel(ADC_CTRL_POWER_UP|ADC_CTRL_CH(chn)|ADC_CTRL_IRQ_ENABLE, dev->regs + ADC_CTRL);

	return;
}
static void rk30_adc_stop(struct adc_host *adc)
{
	struct rk30_adc_device *dev  = adc_priv(adc);
	
	adc_writel(0, dev->regs + ADC_CTRL);
        udelay(SAMPLE_RATE);
}
static int rk30_adc_read(struct adc_host *adc)
{
	struct rk30_adc_device *dev  = adc_priv(adc);

	return adc_readl(dev->regs + ADC_DATA) & ADC_DATA_MASK;
}
static irqreturn_t rk30_adc_irq(int irq, void *data)
{
	struct rk30_adc_device *dev = data;

	adc_core_irq_handle(dev->adc);
	return IRQ_HANDLED;
}
static const struct adc_ops rk30_adc_ops = {
	.start		= rk30_adc_start,
	.stop		= rk30_adc_stop,
	.read		= rk30_adc_read,
	.dump		= rk30_adc_dump,
};
#ifdef ADC_TEST
#define CHN_NR  3
struct workqueue_struct *adc_wq;
struct adc_test_data {
	struct adc_client *client;
	struct timer_list timer;
	struct work_struct 	timer_work;
};
static void callback(struct adc_client *client, void *param, int result)
{
	dev_dbg(client->adc->dev, "[chn%d] async_read = %d\n", client->chn, result);
	return;
}
static void adc_timer(unsigned long data)
{
	 struct adc_test_data *test=(struct adc_test_data *)data;
	
	queue_work(adc_wq, &test->timer_work);
        mod_timer(&test->timer, jiffies+msecs_to_jiffies(20));
}
static void adc_timer_work(struct work_struct *work)
{	
	int sync_read = 0;
	struct adc_test_data *test = container_of(work, struct adc_test_data,
						timer_work);
	adc_async_read(test->client);
	sync_read = adc_sync_read(test->client);
	dev_dbg(test->client->adc->dev, "[chn%d] sync_read = %d\n", test->client->chn, sync_read);
}

static int rk30_adc_test(void)
{
        int i;
	struct adc_test_data *test[CHN_NR];

        adc_wq = create_singlethread_workqueue("adc_test");
	for(i = 0; i < CHN_NR; i++){
	        test[i] = kzalloc(sizeof(struct adc_test_data), GFP_KERNEL);
	        test[i]->client = adc_register(i, callback, NULL);
	        INIT_WORK(&test[i]->timer_work, adc_timer_work);
	        setup_timer(&test[i]->timer, adc_timer, (unsigned long)test[i]);
                mod_timer(&test[i]->timer, jiffies+msecs_to_jiffies(20));
        }
	
	return 0;

}
#endif

static int rk30_adc_probe(struct platform_device *pdev)
{
	struct adc_host *adc = NULL;
	struct rk30_adc_device *dev;
	struct resource *res;
	int ret;

	adc = adc_alloc_host(&pdev->dev, sizeof(struct rk30_adc_device), SARADC_CHN_MASK);
	if (!adc)
		return -ENOMEM;
	adc->ops = &rk30_adc_ops;
	dev = adc_priv(adc);
	dev->adc = adc;
	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq <= 0) {
		dev_err(&pdev->dev, "failed to get adc irq\n");
		ret = -ENOENT;
		goto err_alloc;
	}

	ret = request_threaded_irq(dev->irq, NULL, rk30_adc_irq, IRQF_ONESHOT, pdev->name, dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach adc irq\n");
		goto err_alloc;
	}

	dev->pclk = clk_get(&pdev->dev, "pclk_saradc");
	if (IS_ERR(dev->pclk)) {
		dev_err(&pdev->dev, "failed to get adc pclk\n");
		ret = PTR_ERR(dev->pclk);
		goto err_irq;
	}
	clk_enable(dev->pclk);

	dev->clk = clk_get(&pdev->dev, "saradc");
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "failed to get adc clock\n");
		ret = PTR_ERR(dev->clk);
		goto err_pclk;
	}

	ret = clk_set_rate(dev->clk, ADC_CLK_RATE * 1000 * 1000);
	if(ret < 0) {
		dev_err(&pdev->dev, "failed to set adc clk\n");
		goto err_clk2;
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
	dev_info(&pdev->dev, "rk30 adc: driver initialized\n");
	return 0;

 err_ioarea:
	release_resource(dev->ioarea);
	kfree(dev->ioarea);

 err_clk:
	clk_disable(dev->clk);

 err_pclk:
	clk_disable(dev->pclk);
	clk_put(dev->pclk);

 err_clk2:
	clk_put(dev->clk);

 err_irq:
	free_irq(dev->irq, dev);

 err_alloc:
	adc_free_host(dev->adc);
	return ret;
}

static int rk30_adc_remove(struct platform_device *pdev)
{
	struct rk30_adc_device *dev = platform_get_drvdata(pdev);

	iounmap(dev->regs);
	release_resource(dev->ioarea);
	kfree(dev->ioarea);
	free_irq(dev->irq, dev);
	clk_disable(dev->clk);
	clk_put(dev->clk);
	clk_disable(dev->pclk);
	clk_put(dev->pclk);
	adc_free_host(dev->adc);

	return 0;
}

#ifdef CONFIG_PM
static int rk30_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk30_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 1;
	return 0;
}

static int rk30_adc_resume(struct platform_device *pdev)
{
	struct rk30_adc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 0;
	return 0;
}

#else
#define rk30_adc_suspend NULL
#define rk30_adc_resume NULL
#endif

static struct platform_driver rk30_adc_driver = {
	.driver		= {
		.name	= "rk30-adc",
		.owner	= THIS_MODULE,
	},
	.probe		= rk30_adc_probe,
	.remove		= __devexit_p(rk30_adc_remove),
	.suspend	= rk30_adc_suspend,
	.resume		= rk30_adc_resume,
};

static int __init rk30_adc_init(void)
{
	return platform_driver_register(&rk30_adc_driver);
}
subsys_initcall(rk30_adc_init);

static void __exit rk30_adc_exit(void)
{
	platform_driver_unregister(&rk30_adc_driver);
}
module_exit(rk30_adc_exit);

MODULE_DESCRIPTION("Driver for ADC");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
static int __init adc_test_init(void)
{
#ifdef ADC_TEST	
		rk30_adc_test();
#endif
	return 0;

}
module_init(adc_test_init);
