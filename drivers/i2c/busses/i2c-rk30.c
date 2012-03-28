/* drivers/i2c/busses/i2c-rk30.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "i2c-rk30.h"

#define TX_SETUP                        1 //unit us
void i2c_adap_sel(struct rk30_i2c *i2c, int nr, int adap_type)
{
        unsigned int p = readl(i2c->con_base);

        p = rk30_set_bit(p, adap_type, I2C_ADAP_SEL_BIT(nr));
        p = rk30_set_bit(p, adap_type, I2C_ADAP_SEL_MASK(nr));
        writel(p, i2c->con_base);
}

#ifdef CONFIG_CPU_FREQ

#define freq_to_i2c(_n) container_of(_n, struct rk30_i2c, freq_transition)

static int rk30_i2c_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
        struct rk30_i2c *i2c = freq_to_i2c(nb);
	unsigned long flags;
	int delta_f;

	delta_f = clk_get_rate(i2c->clk) - i2c->i2c_rate;

	if ((val == CPUFREQ_POSTCHANGE && delta_f < 0) ||
	    (val == CPUFREQ_PRECHANGE && delta_f > 0)) 
	{
		spin_lock_irqsave(&i2c->lock, flags);
        i2c->i2c_set_clk(i2c, i2c->scl_rate);
		spin_unlock_irqrestore(&i2c->lock, flags);
	}
	return 0;
}

static inline int rk30_i2c_register_cpufreq(struct rk30_i2c *i2c)
{
    if (i2c->adap.nr != 0)
		return 0;
	i2c->freq_transition.notifier_call = rk30_i2c_cpufreq_transition;

	return cpufreq_register_notifier(&i2c->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk30_i2c_deregister_cpufreq(struct rk30_i2c *i2c)
{
    if (i2c->adap.nr != 0)
		return;
	cpufreq_unregister_notifier(&i2c->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk30_i2c_register_cpufreq(struct rk30_i2c *i2c)
{
	return 0;
}

static inline void rk30_i2c_deregister_cpufreq(struct rk30_i2c *i2c)
{
}
#endif

/* rk30_i2c_probe
 *
 * called by the bus driver when a suitable device is found
*/

static int rk30_i2c_probe(struct platform_device *pdev)
{
	struct rk30_i2c *i2c = NULL;
	struct rk30_i2c_platform_data *pdata = NULL;
	struct resource *res;
        char name[5];
	int ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}


	i2c = kzalloc(sizeof(struct rk30_i2c), GFP_KERNEL);
	if (!i2c) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
        i2c->con_base = (void __iomem *)GRF_I2C_CON_BASE;
        i2c_adap_sel(i2c, pdata->bus_num, pdata->adap_type);

        if(pdata->io_init)
		pdata->io_init();

	strlcpy(i2c->adap.name, "rk30_i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->tx_setup     = TX_SETUP;
	i2c->adap.retries = 5;
        i2c->adap.timeout = msecs_to_jiffies(500);

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	/* find the clock and enable it */

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	i2c_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	clk_enable(i2c->clk);

	/* map the registers */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_get_resource;
	}

	i2c->ioarea = request_mem_region(res->start, resource_size(res),
					 pdev->name);

	if (i2c->ioarea == NULL) {
		dev_err(&pdev->dev, "cannot request IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}

	i2c->regs = ioremap(res->start, resource_size(res));

	if (i2c->regs == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_ioremap;
	}

	i2c_dbg(&pdev->dev, "registers %p (%p, %p)\n",
		i2c->regs, i2c->ioarea, res);

	/* setup info block for the i2c core */

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.nr = pdata->bus_num;
        if(pdata->adap_type == I2C_RK29_ADAP)
                ret = i2c_add_rk29_adapter(&i2c->adap);
        else // I2C_RK30_ADAP
                ret = i2c_add_rk30_adapter(&i2c->adap);

	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add adapter\n");
		goto err_add_adapter;
	}

	/* find the IRQ for this unit (note, this relies on the init call to
	 * ensure no current IRQs pending
	 */

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto err_get_irq;
	}

	ret = request_irq(i2c->irq, i2c->i2c_irq, IRQF_DISABLED,
			  dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
		goto err_request_irq;
	}

	ret = rk30_i2c_register_cpufreq(i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register cpufreq notifier\n");
		goto err_register_cpufreq;
	}

	platform_set_drvdata(pdev, i2c);

        sprintf(name, "%s%d", "i2c", i2c->adap.nr);
        i2c->is_div_from_arm[i2c->adap.nr] = pdata->is_div_from_arm;
        if(i2c->is_div_from_arm[i2c->adap.nr])
                wake_lock_init(&i2c->idlelock[i2c->adap.nr], WAKE_LOCK_IDLE, name);

        i2c->i2c_init_hw(i2c, 100 * 1000);
	dev_info(&pdev->dev, "%s: RK30 I2C adapter\n", dev_name(&i2c->adap.dev));
	return 0;
//err_none:
//	rk30_i2c_deregister_cpufreq(i2c);
err_register_cpufreq:
	free_irq(i2c->irq, i2c);
err_request_irq:
err_get_irq:
	i2c_del_adapter(&i2c->adap);
err_add_adapter:
	iounmap(i2c->regs);
err_ioremap:
	kfree(i2c->ioarea);
err_ioarea:
	release_resource(i2c->ioarea);
err_get_resource:
	clk_put(i2c->clk);
err_noclk:
	kfree(i2c);
	return ret;
}

/* rk30_i2c_remove
 *
 * called when device is removed from the bus
*/

static int rk30_i2c_remove(struct platform_device *pdev)
{
	struct rk30_i2c *i2c = platform_get_drvdata(pdev);

	rk30_i2c_deregister_cpufreq(i2c);
	free_irq(i2c->irq, i2c);
	i2c_del_adapter(&i2c->adap);
	iounmap(i2c->regs);
	kfree(i2c->ioarea);
	release_resource(i2c->ioarea);
	clk_put(i2c->clk);
	kfree(i2c);
	return 0;
}

#ifdef CONFIG_PM
static int rk30_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk30_i2c *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 1;

	return 0;
}

static int rk30_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk30_i2c *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 0;
        i2c->i2c_init_hw(i2c, i2c->scl_rate);

	return 0;
}

static const struct dev_pm_ops rk30_i2c_dev_pm_ops = {
	.suspend_noirq = rk30_i2c_suspend_noirq,
	.resume_noirq = rk30_i2c_resume_noirq,
};

#define rk30_DEV_PM_OPS (&rk30_i2c_dev_pm_ops)
#else
#define rk30_DEV_PM_OPS NULL
#endif

MODULE_DEVICE_TABLE(platform, rk30_driver_ids);

static struct platform_driver rk30_i2c_driver = {
	.probe		= rk30_i2c_probe,
	.remove		= rk30_i2c_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rk30_i2c",
		.pm	= rk30_DEV_PM_OPS,
	},
};

static int __init i2c_adap_init(void)
{
	return platform_driver_register(&rk30_i2c_driver);
}
subsys_initcall(i2c_adap_init);

static void __exit i2c_adap_exit(void)
{
	platform_driver_unregister(&rk30_i2c_driver);
}
module_exit(i2c_adap_exit);

MODULE_DESCRIPTION("Driver for RK30 I2C Bus");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
