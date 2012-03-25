/* drivers/adc/chips/rk30_tadc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/adc.h>
#include <linux/delay.h>
#include <linux/slab.h>


#include "rk30_tsadc.h"

//#define ADC_TEST
#define ADC_POLL	1	//if no tsadc intterupt

struct rk30_tsadc_device {
	int			 irq;
	void __iomem		*regs;
	struct clk *		clk;
	struct resource		*ioarea;
	struct adc_host		*adc;
};
static void rk30_tsadc_start(struct adc_host *adc)
{
	struct rk30_tsadc_device *dev  = adc_priv(adc);	
	int chn = adc->cur->chn;
	
	writel(0, dev->regs + ADC_CTRL);
	writel(ADC_CTRL_POWER_UP|ADC_CTRL_CH(chn), dev->regs + ADC_CTRL);
	udelay(SAMPLE_RATE);

	writel(readl(dev->regs + ADC_CTRL)|ADC_CTRL_IRQ_ENABLE|ADC_CTRL_START, 
		dev->regs + ADC_CTRL);
	return;
}

static void rk30_tsadc_start_poll(struct adc_host *adc)
{
	struct rk30_tsadc_device *dev  = adc_priv(adc);	
	int chn = adc->cur->chn;
	
	writel(0, dev->regs + ADC_CTRL);
	writel(ADC_CTRL_POWER_UP|ADC_CTRL_CH(chn), dev->regs + ADC_CTRL);
	udelay(SAMPLE_RATE);

	writel(readl(dev->regs + ADC_CTRL)|ADC_CTRL_START, 
		dev->regs + ADC_CTRL);
	return;
}

static void rk30_tsadc_stop(struct adc_host *adc)
{
	struct rk30_tsadc_device *dev  = adc_priv(adc);
	
	writel(0, dev->regs + ADC_CTRL);
}
static int rk30_tsadc_read(struct adc_host *adc)
{
	struct rk30_tsadc_device *dev  = adc_priv(adc);

	udelay(SAMPLE_RATE);
	return readl(dev->regs + ADC_DATA) & ADC_DATA_MASK;
}
static irqreturn_t rk30_tsadc_irq(int irq, void *data)
{
	struct rk30_tsadc_device *dev = data;
	adc_core_irq_handle(dev->adc);
	return IRQ_HANDLED;
}
static const struct adc_ops rk30_tsadc_ops = {
	.start		= rk30_tsadc_start,
	.stop		= rk30_tsadc_stop,
	.read		= rk30_tsadc_read,
};


#ifdef ADC_TEST
struct adc_test_data {
	struct adc_client client[2];
	struct timer_list timer;
	struct work_struct 	timer_work;
};
static void callback_test(struct adc_client *client, void *param, int result)
{
	int i = 0;
	for(i=0;i<2;i++)
	dev_info(client[i].adc->dev, "[chn=%d] async_read = %d\n", client[i].chn, result);
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
	struct adc_test_data *test = container_of(work, struct adc_test_data,
						timer_work);
	int i = 0;
#if ADC_POLL
	int ret = 0, count = 0;
	struct adc_host *adc = test->client[i].adc;
	struct rk30_tsadc_device *dev  = adc_priv(adc);
	adc->cur = &test->client[i];
	rk30_tsadc_start(adc);
	while(1)
	{	
		udelay(SAMPLE_RATE);
		ret = readl(dev->regs + ADC_STAS);
		if(!(ret & ADC_STAS_BUSY))
		{	
			rk30_tsadc_stop(adc);
			break;
		}
		if(count++ > 10)
		{
			rk30_tsadc_stop(adc);
			printk("%s:timeout\n",__func__);
			break;
		}
	}
	
	sync_read = readl(dev->regs + ADC_DATA);
	dev_info(test->client[i].adc->dev, "[chn=%d] sync_read = %d\n", i, sync_read);
#else	
	int sync_read = 0;
	for(i=0;i<2;i++)
	{	
		adc_async_read(&test->client[i]);	
		sync_read = adc_sync_read(&test->client[i]);		
		dev_info(test->client[i].adc->dev, "[chn=%d] sync_read = %d\n", i, sync_read);
	}
#endif
}

static int rk30_tsadc_test(void)
{
	struct adc_test_data *test = NULL;
	int i = 0;
	test = kzalloc(sizeof(struct adc_test_data), GFP_KERNEL);
	for(i=0;i<2;i++)
	test->client[i] = *adc_register(i, callback_test, NULL);

	INIT_WORK(&test->timer_work, adc_timer_work);
	setup_timer(&test->timer, adc_timer, (unsigned long)test);
	test->timer.expires  = jiffies + 200;
	add_timer(&test->timer);
	
	return 0;
}
#endif

#if 1
struct temp_sample_data {
	struct adc_client client[2];
	struct timer_list timer;
	struct work_struct 	timer_work;
};
static struct temp_sample_data *gtemp;
static void callback(struct adc_client *client, void *param, int result)
{
	int i = 0;
	for(i=0; i<2; i++)
	dev_info(client[i].adc->dev, "[chn=%d] async_read = %d\n", client[i].chn, result);
	return;
}

static int rk30_temp_sample_init(void)
{
	struct temp_sample_data *temp = NULL;	
	int i = 0;
	temp = kzalloc(sizeof(struct temp_sample_data), GFP_KERNEL);
	if (!temp){
		printk("%s:no memory for adc request\n",__func__);
		return -ENOMEM;
	}
	
	for(i=0; i<2; i++)
	temp->client[i] = *adc_register(i, callback, NULL);
	gtemp = temp;
	return 0;

}


int rk30_temp_sample(int chn, int *result)
{
	int sync_read = 0;
	int i = 0, num = 0;
#if ADC_POLL
	int ret = 0, count = 0;	
	struct temp_sample_data *temp = gtemp;	
	struct adc_host *adc;
	struct rk30_tsadc_device *dev;
	chn &= 0x01;	//0 or 1
	adc = temp->client[chn].adc;
	dev = adc_priv(adc);
	adc->cur = &temp->client[chn];
	rk30_tsadc_start_poll(adc);
	while(1)
	{	
		udelay(SAMPLE_RATE);
		ret = readl(dev->regs + ADC_STAS);
		if(!(ret & ADC_STAS_BUSY))
		{	
			rk30_tsadc_stop(adc);
			break;
		}
		if(count++ > 20)
		{
			rk30_tsadc_stop(adc);
			printk("%s:timeout\n",__func__);
			break;
		}
	}
	
	sync_read = readl(dev->regs + ADC_DATA);
	//dev_info(temp->client[chn].adc->dev, "[chn=%d] sync_read = %d\n", chn, sync_read);
#else
	adc_async_read(&gtemp->client[chn]);
	sync_read = adc_sync_read(&gtemp->client[chn]);
	dev_info(gtemp->client[chn].adc->dev, "[chn=%d] sync_read = %d\n", chn, sync_read);
#endif
	//get temperature according to ADC value
	num = sizeof(table_code_to_temp)/sizeof(struct tsadc_table);	
	for(i=0; i<num-1;i++)
	{
		if((sync_read >= table_code_to_temp[i+1].code) && (sync_read < table_code_to_temp[i].code))
		{
			*result = table_code_to_temp[i+1].temp;
			return 0;
		}
	}

	if(sync_read <= table_code_to_temp[num-1].code)
	{
		*result = table_code_to_temp[num-1].temp;		
		printk("%s:temperature is out of table\n",__func__);
		return -1;
	}
	else if(sync_read >= table_code_to_temp[0].code)
	{
		*result = table_code_to_temp[0].temp;
		printk("%s:temperature is out of table\n",__func__);
		return -1;
	}

	return -1;
		
}

EXPORT_SYMBOL(rk30_temp_sample);

#endif

static int rk30_tsadc_probe(struct platform_device *pdev)
{
	struct adc_host *adc = NULL;
	struct rk30_tsadc_device *dev;
	struct resource *res;
	int ret;

	adc = adc_alloc_host(sizeof(struct rk30_tsadc_device), &pdev->dev);
	if (!adc)
		return -ENOMEM;
	spin_lock_init(&adc->lock);
	adc->dev = &pdev->dev;
	adc->is_suspended = 0;
	adc->ops = &rk30_tsadc_ops;
	dev = adc_priv(adc);
	dev->adc = adc;
	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq <= 0) {
		dev_err(&pdev->dev, "failed to get adc irq\n");
		ret = -ENOENT;
		goto err_alloc;
	}

	ret = request_irq(dev->irq, rk30_tsadc_irq, 0, pdev->name, dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to attach adc irq\n");
		goto err_alloc;
	}

	dev->clk = clk_get(&pdev->dev, "saradc");
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "failed to get adc clock\n");
		ret = PTR_ERR(dev->clk);
		//goto err_irq;
	}

	//ret = clk_set_rate(dev->clk, ADC_CLK_RATE * 1000 * 1000);
	//if(ret < 0) {
	//	dev_err(&pdev->dev, "failed to set adc clk\n");
		//goto err_clk;
	//}
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
// err_iomap:
//	iounmap(dev->regs);

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

static int rk30_tsadc_remove(struct platform_device *pdev)
{
	struct rk30_tsadc_device *dev = platform_get_drvdata(pdev);

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
static int rk30_tsadc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk30_tsadc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 1;
	return 0;
}

static int rk30_tsadc_resume(struct platform_device *pdev)
{
	struct rk30_tsadc_device *dev = platform_get_drvdata(pdev);

	dev->adc->is_suspended = 0;
	return 0;
}

#else
#define rk30_tsadc_suspend NULL
#define rk30_tsadc_resume NULL
#endif

static struct platform_driver rk30_tsadc_driver = {
	.driver		= {
		.name	= "rk30-tsadc",
		.owner	= THIS_MODULE,
	},
	.probe		= rk30_tsadc_probe,
	.remove		= __devexit_p(rk30_tsadc_remove),
	.suspend	= rk30_tsadc_suspend,
	.resume		= rk30_tsadc_resume,
};

static int __init rk30_tsadc_init(void)
{
	return platform_driver_register(&rk30_tsadc_driver);
}
subsys_initcall(rk30_tsadc_init);

static void __exit rk30_tsadc_exit(void)
{
	platform_driver_unregister(&rk30_tsadc_driver);
}
module_exit(rk30_tsadc_exit);

MODULE_DESCRIPTION("Driver for TSADC");
MODULE_AUTHOR("lw, lw@rock-chips.com");
MODULE_LICENSE("GPL");

static int __init rk30_temp_init(void)
{
	int ret = 0;
	ret = rk30_temp_sample_init();
#ifdef ADC_TEST	
	rk30_tsadc_test();
#endif	
	printk("%s:initialized\n",__func__);
	return ret;
}

static void __exit rk30_temp_exit(void)
{
	int i = 0;
	struct temp_sample_data *temp = gtemp;
	for(i=0; i<2; i++)
	adc_unregister(&temp->client[i]);
	kfree(temp);
}

module_init(rk30_temp_init);
module_exit(rk30_temp_exit);

