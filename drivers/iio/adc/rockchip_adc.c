/*
 *  rk_adc.c - Support for ADC in rockchip SoCs
 *
 *  3channel, 10-bit ADC
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>


#if 0
#define RK_ADC_DBG(x...)		\
	printk(KERN_INFO"[rk_adc]: "x)
#else
#define RK_ADC_DBG(x...)	

#endif

enum host_chn_shift{
        SARADC_CHN_SHIFT = 0,
        CUSTOM_CHN_SHIFT = 28
};

enum host_chn_mask{
        SARADC_CHN_MASK = 0x0000000f,  // saradc: 0 -- 15
        CUSTOM_CHN_MASK = 0xf0000000,
};

#define ADC_DATA			0x00
#define ADC_DATA_MASK		0x3ff

#define ADC_STAS			0x04
#define ADC_STAS_BUSY		(1<<0)

#define ADC_CTRL			0x08
#define ADC_DELAY_PU_SOC		0x0c
#define ADC_CTRL_CH(ch)		(ch >> SARADC_CHN_SHIFT) //(0x07 - ((ch)<<0))
#define ADC_CTRL_POWER_UP	    (1<<3)
#define ADC_CTRL_IRQ_ENABLE	(1<<5)
#define ADC_CTRL_IRQ_STATUS	(1<<6)

#define ADC_CLK_RATE		1  //1M
#define SAMPLE_RATE		(20/ADC_CLK_RATE)  //20 CLK

#define MAX_RK_ADC_CHANNELS	3
#define RK_ADC_TIMEOUT		HZ

struct rk_adc {
	void __iomem	*regs;
	struct clk		*pclk;
	struct clk		*clk;
	struct completion	completion;
	unsigned int		irq;
	u32                 vref_mv;
    u32                 num_channels;
	u32			        value;
};

static const struct of_device_id rk_adc_match[] = {
	{ .compatible = "rockchip,saradc", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, rk_adc_match);

static void rk_adc_dump(struct iio_dev *indio_dev)
{
    struct rk_adc *info = iio_priv(indio_dev);

    printk("saradc-reg[0x00-0x0c]: 0x%08x 0x%08x 0x%08x 0x%08x\n",
                        readl_relaxed(info->regs + 0x00),
                        readl_relaxed(info->regs + 0x04),
                        readl_relaxed(info->regs + 0x08),
                        readl_relaxed(info->regs + 0x0c));
}

static int rk_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	int ret = IIO_VAL_INT;
	struct rk_adc *info = iio_priv(indio_dev);
	unsigned long timeout;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);

	INIT_COMPLETION(info->completion);

	clk_enable(info->clk);
	clk_enable(info->pclk);

        /* Select the channel to be used and Trigger conversion */
        writel_relaxed(0x08, info->regs + ADC_DELAY_PU_SOC);
	writel_relaxed(ADC_CTRL_POWER_UP|ADC_CTRL_CH(chan->channel)|ADC_CTRL_IRQ_ENABLE, info->regs + ADC_CTRL);

	timeout = wait_for_completion_timeout
			(&info->completion, RK_ADC_TIMEOUT);
	*val = info->value;
	
	RK_ADC_DBG("read adc value: %d form channel: %d\n", *val, chan->channel);
	if (timeout == 0)
	{
		rk_adc_dump(indio_dev);
		writel_relaxed(0, info->regs + ADC_CTRL);
		writel_relaxed(0, info->regs + ADC_DELAY_PU_SOC);
		ret = -ETIMEDOUT;
	}

	clk_disable(info->clk);
	clk_disable(info->pclk);

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static irqreturn_t rk_adc_isr(int irq, void *dev_id)
{
	struct rk_adc *info = (struct rk_adc *)dev_id;

	/* Read value */
	info->value = readl_relaxed(info->regs + ADC_DATA) & ADC_DATA_MASK;
	/* clear irq & power down adc*/
        writel_relaxed(0, info->regs + ADC_CTRL);
        writel_relaxed(0, info->regs + ADC_DELAY_PU_SOC);

	complete(&info->completion);

	return IRQ_HANDLED;
}

static int rk_adc_reg_access(struct iio_dev *indio_dev,
			      unsigned reg, unsigned writeval,
			      unsigned *readval)
{
	struct rk_adc *info = iio_priv(indio_dev);

	if (readval == NULL)
		return -EINVAL;

	clk_enable(info->clk);
	clk_enable(info->pclk);

	*readval = readl_relaxed(info->regs + reg);

	clk_disable(info->clk);
	clk_disable(info->pclk);

	return 0;
}

static const struct iio_info rk_adc_iio_info = {
	.read_raw = &rk_read_raw,
	.debugfs_reg_access = &rk_adc_reg_access,
	.driver_module = THIS_MODULE,
};

#define ADC_CHANNEL(_index, _id) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.address = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.datasheet_name = _id,				\
}

static const struct iio_chan_spec rk_adc_iio_channels[] = {
	ADC_CHANNEL(0, "adc0"),
	ADC_CHANNEL(1, "adc1"),
	ADC_CHANNEL(2, "adc2"),
	ADC_CHANNEL(6, "adc6"),
};

static int rk_adc_remove_devices(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int rk_adc_probe(struct platform_device *pdev)
{
	struct rk_adc *info = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct iio_dev *indio_dev = NULL;
	struct resource	*mem;
	int ret = -ENODEV;
	int irq;
	u32 rate;

	if (!np)
		return ret;

	indio_dev = iio_device_alloc(sizeof(struct rk_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}
	info = iio_priv(indio_dev);

    //resource ioremap
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_request_and_ioremap(&pdev->dev, mem);
	if (!info->regs) {
		ret = -ENOMEM;
		goto err_iio;
	}

    //irq request	
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		ret = irq;
		goto err_iio;
	}
	info->irq = irq;

	init_completion(&info->completion);
	ret = devm_request_irq(&pdev->dev, info->irq, rk_adc_isr,
					0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n",
							info->irq);
		goto err_iio;
	}
	
	//clk enable
	info->pclk = devm_clk_get(&pdev->dev, "pclk_saradc");
	if (IS_ERR(info->pclk)) {
	    dev_err(&pdev->dev, "failed to get adc pclk\n");
	    ret = PTR_ERR(info->pclk);
	    goto err_iio;
	}
	clk_prepare(info->pclk);
	
	info->clk = devm_clk_get(&pdev->dev, "saradc");
	if (IS_ERR(info->clk)) {
	    dev_err(&pdev->dev, "failed to get adc clock\n");
	    ret = PTR_ERR(info->clk);
	    goto err_pclk;
	}

	if(of_property_read_u32(np, "clock-frequency", &rate)) {
        dev_err(&pdev->dev, "Missing clock-frequency property in the DT.\n");
	    goto err_pclk;
	}

	ret = clk_set_rate(info->clk, rate);
	    if(ret < 0) {
	    dev_err(&pdev->dev, "failed to set adc clk\n");
	    goto err_pclk;
	}
	clk_prepare(info->clk);

    //device register
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &rk_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = rk_adc_iio_channels;

    if (of_property_read_u32(np, "rockchip,adc-vref", &info->vref_mv)) {
                dev_err(&pdev->dev, "Missing rockchip,adc-vref property in the DT.\n");
                goto err_clk;
    }
    indio_dev->num_channels = sizeof(rk_adc_iio_channels)/sizeof(struct iio_chan_spec);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_iio_dev;

	platform_set_drvdata(pdev, indio_dev);
	ret = of_platform_populate(np, rk_adc_match, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child nodes\n");
		goto err_of_populate;
	}

	return 0;

err_of_populate:
	device_for_each_child(&pdev->dev, NULL,
				rk_adc_remove_devices);
err_iio_dev:
	iio_device_unregister(indio_dev);
	
err_clk:
	clk_unprepare(info->clk);

err_pclk:
	clk_unprepare(info->pclk);
	
err_iio:
	iio_device_free(indio_dev);
	return ret;
}

static int rk_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct rk_adc *info = iio_priv(indio_dev);

	device_for_each_child(&pdev->dev, NULL,
				rk_adc_remove_devices);
	clk_unprepare(info->clk);
	clk_unprepare(info->pclk);
	iio_device_unregister(indio_dev);
	free_irq(info->irq, info);
	iio_device_free(indio_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rk_adc_suspend(struct device *dev)
{
	//struct iio_dev *indio_dev = dev_get_drvdata(dev);
	//struct exynos_adc *info = iio_priv(indio_dev);
	int ret = 0;
	
	return ret;
}

static int rk_adc_resume(struct device *dev)
{
	//struct iio_dev *indio_dev = dev_get_drvdata(dev);
	//struct rk_adc *info = iio_priv(indio_dev);
	int ret = 0;

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(rk_adc_pm_ops,
			rk_adc_suspend,
			rk_adc_resume);

static struct platform_driver rk_adc_driver = {
	.probe		= rk_adc_probe,
	.remove		= rk_adc_remove,
	.driver		= {
		.name	= "rockchip-adc",
		.owner	= THIS_MODULE,
		.of_match_table = rk_adc_match,
		.pm	= &rk_adc_pm_ops,
	},
};

module_platform_driver(rk_adc_driver);


