/* arm/arch/mach-rk2818/adc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <mach/rk2818_iomap.h>
#include <mach/adc.h>

/* This driver is designed to control the usage of the ADC block between
 * the keyboard and any other drivers that may need to use it.
 *
 * Priority will be given to the touchscreen driver, but as this itself is
 * rate limited it should not starve other requests which are processed in
 * order that they are received.
 *
 * Each user registers to get a client block which uniquely identifies it
 * and stores information such as the necessary functions to callback when
 * action is required.
 */
 
#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

struct rk28_adc_client {
	struct platform_device	*pdev;
	struct list_head	 pend;
	wait_queue_head_t	*wait;
	unsigned int		 nr_samples;
	int			 result;
	unsigned char		 is_ts;
	unsigned char		 channel;

	void	(*select_cb)(struct rk28_adc_client *c, unsigned selected);
	void	(*convert_cb)(struct rk28_adc_client *c,
			      unsigned val1, unsigned val2,
			      unsigned *samples_left);
};

struct adc_device {
	struct semaphore	lock;
	struct platform_device	*pdev;
	struct platform_device	*owner;
	struct clk		*clk;
	struct rk28_adc_client	*client;
	struct rk28_adc_client	*cur;
	struct rk28_adc_client	*ts_pend;
	struct workqueue_struct 	*timer_workqueue;
	struct work_struct 	timer_work;
	void __iomem		*regs;
	struct timer_list timer;
	unsigned int		 pre_con;
	int			 irq;
};

static struct adc_device *pAdcDev;
volatile int gAdcChanel = 0;
volatile int gAdcValue[4]={0, 0, 0, 0};	//0->ch0 1->ch1 2->ch2 3->ch3

static LIST_HEAD(adc_pending);

#define adc_dbg(_adc, msg...) dev_dbg(&(_adc)->pdev->dev, msg)

static inline void rk28_adc_int_enable(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con |= RK28_ADC_INT_ENABLE;
	writel(con, adc->regs + RK28_ADCCON);
}

static inline void rk28_adc_int_disable(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con &= RK28_ADC_INT_DISABLE;
	writel(con, adc->regs + RK28_ADCCON);
}

static inline void rk28_adc_int_clear(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con &= RK28_ADC_INT_CLEAR;
	writel(con, adc->regs + RK28_ADCCON);
}

static inline void rk28_adc_power_up(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con |= RK28_ADC_POWER_UP;
	writel(con, adc->regs + RK28_ADCCON);
}

static inline void rk28_adc_power_down(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con &= RK28_ADC_POWER_DOWN;
	writel(con, adc->regs + RK28_ADCCON);
}

static inline void rk28_adc_convert(struct adc_device *adc)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	con |= RK28_ADC_POWER_UP | RK28_ADC_CONV_START | RK28_ADC_INT_ENABLE;
	writel(con, adc->regs + RK28_ADCCON);	
}

static inline void rk28_adc_select(struct adc_device *adc,
				  struct rk28_adc_client *client)
{
	unsigned con = readl(adc->regs + RK28_ADCCON);
	
	client->select_cb(client, 1);
	
	con &= RK28_ADC_MASK_CH;
	con &= RK28_ADC_CONV_STOP;
	
	if (!client->is_ts)
	con |= RK28_ADC_SEL_CH(client->channel);
	
	writel(con, adc->regs + RK28_ADCCON);
}

static void rk28_adc_dbgshow(struct adc_device *adc)
{
	adc_dbg(adc, "CON=0x%x, STAS=0x%x, DATA=0x%x\n",
		readl(adc->regs + RK28_ADCCON),
		readl(adc->regs + RK28_ADCSTAS),
		readl(adc->regs + RK28_ADCDAT));
}

static void rk28_adc_try(struct adc_device *adc)
{
	struct rk28_adc_client *next = adc->ts_pend;

	if (!next && !list_empty(&adc_pending)) {
		next = list_first_entry(&adc_pending,
					struct rk28_adc_client, pend);
		list_del(&next->pend);
	} else
		adc->ts_pend = NULL;

	if (next) {
		adc_dbg(adc, "new client is %p\n", next);
		adc->cur = next;
		rk28_adc_select(adc, next);
		rk28_adc_convert(adc);
		rk28_adc_dbgshow(adc);
	}
}

int rk28_adc_start(struct rk28_adc_client *client,
		  unsigned int channel, unsigned int nr_samples)
{
	struct adc_device *adc = pAdcDev;
	unsigned long flags;

	if (!adc) {
		printk(KERN_ERR "%s: failed to find adc\n", __func__);
		return -EINVAL;
	}

	if (client->is_ts && adc->ts_pend)
		return -EAGAIN;

	local_irq_save(flags);

	client->channel = channel;
	client->nr_samples = nr_samples;

	if (client->is_ts)
		adc->ts_pend = client;
	else
		list_add_tail(&client->pend, &adc_pending);

	if (!adc->cur)
		rk28_adc_try(adc);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL_GPL(rk28_adc_start);

static void rk28_convert_done(struct rk28_adc_client *client,
			     unsigned v, unsigned u, unsigned *left)
{
	client->result = v;
	wake_up(client->wait);
}

int rk28_adc_read(struct rk28_adc_client *client, unsigned int ch)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	int ret;

	client->convert_cb = rk28_convert_done;
	client->wait = &wake;
	client->result = -1;

	ret = rk28_adc_start(client, ch, 1);
	if (ret < 0)
		goto err;

	ret = wait_event_timeout(wake, client->result >= 0, HZ / 2);
	if (client->result < 0) {
		ret = -ETIMEDOUT;
		goto err;
	}

	client->convert_cb = NULL;
	return client->result;

err:
	return ret;
}
EXPORT_SYMBOL_GPL(rk28_adc_read);

static void rk28_adc_default_select(struct rk28_adc_client *client,
				   unsigned select)
{
}

struct rk28_adc_client *rk28_adc_register(struct platform_device *pdev,
					void (*select)(struct rk28_adc_client *client,
						       unsigned int selected),
					void (*conv)(struct rk28_adc_client *client,
						     unsigned d0, unsigned d1,
						     unsigned *samples_left),
					unsigned int is_ts)
{
	struct rk28_adc_client *client;

	WARN_ON(!pdev);

	if (!select)
		select = rk28_adc_default_select;

	if (!pdev)
		return ERR_PTR(-EINVAL);

	client = kzalloc(sizeof(struct rk28_adc_client), GFP_KERNEL);
	if (!client) {
		dev_err(&pdev->dev, "no memory for adc client\n");
		return ERR_PTR(-ENOMEM);
	}

	client->pdev = pdev;
	client->is_ts = is_ts;
	client->select_cb = select;
	client->convert_cb = conv;

	return client;
}
EXPORT_SYMBOL_GPL(rk28_adc_register);

void rk28_adc_release(struct rk28_adc_client *client)
{
	/* We should really check that nothing is in progress. */
	if (pAdcDev->cur == client)
		pAdcDev->cur = NULL;
	if (pAdcDev->ts_pend == client)
		pAdcDev->ts_pend = NULL;
	else {
		struct list_head *p, *n;
		struct rk28_adc_client *tmp;

		list_for_each_safe(p, n, &adc_pending) {
			tmp = list_entry(p, struct rk28_adc_client, pend);
			if (tmp == client)
				list_del(&tmp->pend);
		}
	}

	if (pAdcDev->cur == NULL)
		rk28_adc_try(pAdcDev);
	kfree(client);
}
EXPORT_SYMBOL_GPL(rk28_adc_release);


//read four ADC chanel
static int rk28_read_adc(struct adc_device *adc)
{
	int ret = 0,i;
	
	ret = down_interruptible(&adc->lock);
	if (ret < 0)
		return ret;	

	for(i=0; i<4; i++)
	{
		gAdcValue[i] = rk28_adc_read(adc->client, i);
		DBG("gAdcValue[%d]=%d\n",i,gAdcValue[i]);
	}

	up(&adc->lock);
	return ret;
}

static void adc_timer_work(struct work_struct *work)
{	
	rk28_read_adc(pAdcDev);
}


static void rk28_adcscan_timer(unsigned long data)
{
	pAdcDev->timer.expires  = jiffies + msecs_to_jiffies(30);
	add_timer(&pAdcDev->timer);
	//schedule_work(&pAdcDev->timer_work);
	queue_work(pAdcDev->timer_workqueue, &pAdcDev->timer_work);
}

static irqreturn_t rk28_adc_irq(int irq, void *pw)
{
	struct adc_device *adc = pw;
	struct rk28_adc_client *client = adc->cur;
	unsigned long flags;
	unsigned data0, data1 = 0;
	rk28_adc_int_disable(adc);
	rk28_adc_int_clear(adc);
	
	if (!client) {
		dev_warn(&adc->pdev->dev, "%s: no adc pending\n", __func__);
		return IRQ_HANDLED;
	}
	
	data0 = readl(adc->regs + RK28_ADCDAT);

	adc_dbg(adc, "read %d: 0x%x\n", client->nr_samples, data0);

	client->nr_samples--;

	if (client->convert_cb)
		(client->convert_cb)(client, data0 & 0x3ff, data1 & 0x3ff,
				     &client->nr_samples);

	if (client->nr_samples > 0) {
		/* fire another conversion for this */

		client->select_cb(client, 1);
		rk28_adc_convert(adc);
	} else {
		local_irq_save(flags);
		(client->select_cb)(client, 0);
		adc->cur = NULL;

		rk28_adc_try(adc);
		local_irq_restore(flags);
	}
	
	return IRQ_HANDLED;
}

static int rk28_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct adc_device *adc;
	struct resource *regs;
	int ret;

	adc = kzalloc(sizeof(struct adc_device), GFP_KERNEL);
	if (adc == NULL) {
		dev_err(dev, "failed to allocate adc_device\n");
		return -ENOMEM;
	}

	adc->pdev = pdev;

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq <= 0) {
		dev_err(dev, "failed to get adc irq\n");
		ret = -ENOENT;
		goto err_alloc;
	}

	ret = request_irq(adc->irq, rk28_adc_irq, 0, dev_name(dev), adc);
	if (ret < 0) {
		dev_err(dev, "failed to attach adc irq\n");
		goto err_alloc;
	}

	adc->clk = clk_get(dev, "lsadc");
	if (IS_ERR(adc->clk)) {
		dev_err(dev, "failed to get adc clock\n");
		ret = PTR_ERR(adc->clk);
		goto err_irq;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "failed to find registers\n");
		ret = -ENXIO;
		goto err_clk;
	}

	adc->regs = ioremap(regs->start, resource_size(regs));
	if (!adc->regs) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_clk;
	}

	clk_enable(adc->clk);

	dev_info(dev, "attached adc driver\n");
	
	platform_set_drvdata(pdev, adc);

	init_MUTEX(&adc->lock);
	adc->timer_workqueue = create_freezeable_workqueue("adc timer work");
	if (!adc->timer_workqueue) {
	printk("%s:cannot create workqueue\n",__FUNCTION__);
	return -EBUSY;
	}
	INIT_WORK(&adc->timer_work, adc_timer_work);
	/* Register with the core ADC driver. */
	adc->client = rk28_adc_register(pdev, NULL, NULL, 0);
	if (IS_ERR(adc->client)) {
		dev_err(dev, "cannot register adc\n");
		ret = PTR_ERR(adc->client);
		goto err_clk;
	}

	rk28_adc_int_disable(adc);
	pAdcDev = adc;
	
	setup_timer(&adc->timer, rk28_adcscan_timer, (unsigned long)adc);
	adc->timer.expires  = jiffies + 50;
	add_timer(&adc->timer);
	printk(KERN_INFO "rk2818 adc: driver initialized\n");
	return 0;

 err_clk:
	clk_put(adc->clk);

 err_irq:
	free_irq(adc->irq, adc);

 err_alloc:
	kfree(adc);
	return ret;
}

static int rk28_adc_remove(struct platform_device *pdev)
{
	struct adc_device *adc = platform_get_drvdata(pdev);
	rk28_adc_power_down(adc);
	rk28_adc_release(adc->client);
	iounmap(adc->regs);
	free_irq(adc->irq, adc);
	clk_disable(adc->clk);
	clk_put(adc->clk);
	kfree(adc);

	return 0;
}

#ifdef CONFIG_PM
static int rk28_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct adc_device *adc = platform_get_drvdata(pdev);
	u32 con;
	
	con = readl(adc->regs + RK28_ADCCON);
	adc->pre_con = con;
	con &= RK28_ADC_POWER_DOWN;
	writel(con, adc->regs + RK28_ADCCON);

	clk_disable(adc->clk);

	return 0;
}

static int rk28_adc_resume(struct platform_device *pdev)
{
	struct adc_device *adc = platform_get_drvdata(pdev);

	clk_enable(adc->clk);

	writel(adc->pre_con, adc->regs + RK28_ADCCON);
	return 0;
}

#else
#define rk28_adc_suspend NULL
#define rk28_adc_resume NULL
#endif

static struct platform_driver rk28_adc_driver = {
	.driver		= {
		.name	= "rk2818-adc",
		.owner	= THIS_MODULE,
	},
	.probe		= rk28_adc_probe,
	.remove		= __devexit_p(rk28_adc_remove),
	.suspend	= rk28_adc_suspend,
	.resume		= rk28_adc_resume,
};

static int __init adc_init(void)
{
	int ret;

	ret = platform_driver_register(&rk28_adc_driver);
	if (ret)
		printk(KERN_ERR "%s: failed to add adc driver\n", __func__);
	return ret;
}

module_init(adc_init);
MODULE_DESCRIPTION("rk28xx adc driver");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");

