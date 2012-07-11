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
#define TX_SETUP                        1

void i2c_adap_sel(struct rk30_i2c *i2c, int nr, int adap_type)
{
        i2c_writel((1 << I2C_ADAP_SEL_BIT(nr)) | (1 << I2C_ADAP_SEL_MASK(nr)) ,
                        i2c->con_base);
}

#ifdef CONFIG_CPU_FREQ

#define freq_to_i2c(_n) container_of(_n, struct rk30_i2c, freq_transition)

static int rk30_i2c_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
        struct rk30_i2c *i2c = freq_to_i2c(nb);
        struct cpufreq_freqs *freqs = data;

        if (freqs->cpu)
                return 0;

        if(val == CPUFREQ_PRECHANGE)
                mutex_lock(&i2c->m_lock);
        else if(val == CPUFREQ_POSTCHANGE)
                mutex_unlock(&i2c->m_lock);

        return 0;
}

static inline int rk30_i2c_register_cpufreq(struct rk30_i2c *i2c)
{
        if(!i2c->is_div_from_arm[i2c->adap.nr])
	        return 0;
	i2c->freq_transition.notifier_call = rk30_i2c_cpufreq_transition;

	return cpufreq_register_notifier(&i2c->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk30_i2c_deregister_cpufreq(struct rk30_i2c *i2c)
{
        if(!i2c->is_div_from_arm[i2c->adap.nr])
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
	i2c->adap.retries = 2;
        i2c->adap.timeout = msecs_to_jiffies(100);

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);
        mutex_init(&i2c->m_lock); 

	/* find the clock and enable it */

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	i2c_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	//clk_enable(i2c->clk);

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

        i2c->is_div_from_arm[i2c->adap.nr] = pdata->is_div_from_arm;
        if(i2c->is_div_from_arm[i2c->adap.nr])
                wake_lock_init(&i2c->idlelock[i2c->adap.nr], WAKE_LOCK_IDLE, dev_name(&pdev->dev));

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

static int detect_read(struct i2c_client *client, char *buf, int len)
{
        struct i2c_msg msg;

        msg.addr = client->addr;
        msg.flags = client->flags | I2C_M_RD;
        msg.buf = buf;
        msg.len = len;
        msg.scl_rate = 100 * 1000;

        return i2c_transfer(client->adapter, &msg, 1);
}

static void detect_set_client(struct i2c_client *client, __u16 addr, int nr)
{
        client->flags = 0;
        client->addr = addr;
        client->adapter = i2c_get_adapter(nr);
}
static void slave_detect(int nr)
{
        int ret = 0;
        unsigned short addr;
        char val[8];
        char buf[6 * 0x80 + 20];
        struct i2c_client client;

        memset(buf, 0, 6 * 0x80 + 20);
        
        sprintf(buf, "I2c%d slave list: ", nr);
        do {
                for(addr = 0x01; addr < 0x80; addr++){
                        detect_set_client(&client, addr, nr);
                        ret = detect_read(&client, val, 1);
                        if(ret > 0)
                                sprintf(buf, "%s  0x%02x", buf, addr);
                }
                printk("%s\n", buf);
        }
        while(0);
}
static ssize_t i2c_detect_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
        char nr_buf[8];
        int nr = 0, ret;

        if(count > 4)
                return -EFAULT;
        ret = copy_from_user(nr_buf, buf, count);
        if(ret < 0)
                return -EFAULT;

        sscanf(nr_buf, "%d", &nr);
        if(nr >= 5 || nr < 0)
                return -EFAULT;

        slave_detect(nr);

        return count;
}


static const struct file_operations i2c_detect_fops = {
	.write = i2c_detect_write,
};
static struct miscdevice i2c_detect_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "i2c_detect",
	.fops = &i2c_detect_fops,
};
static int __init i2c_detect_init(void)
{
        return misc_register(&i2c_detect_device);
}

static void __exit i2c_detect_exit(void)
{
        misc_deregister(&i2c_detect_device);
}
module_init(i2c_detect_init);
module_exit(i2c_detect_exit);






MODULE_DESCRIPTION("Driver for RK30 I2C Bus");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
