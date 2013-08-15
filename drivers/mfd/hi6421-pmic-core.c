/*
 * Device driver for regulators in Hi6421 IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/mfd/hi6421-pmic.h>

#include <asm/mach/irq.h>

/* 8-bit register offset in PMIC */
#define HI6421_REG_IRQ1			1
#define HI6421_REG_IRQ2			2
#define HI6421_REG_IRQ3			3
#define HI6421_REG_IRQM1		4
#define HI6421_REG_IRQM2		5
#define HI6421_REG_IRQM3		6

static struct of_device_id of_hi6421_pmic_child_match_tbl[] = {
	/* regulators */
	{
		.compatible = "hisilicon,hi6421-ldo",
	},
	{
		.compatible = "hisilicon,hi6421-buck012",
	},
	{
		.compatible = "hisilicon,hi6421-buck345",
	},
	{ /* end */ }
};

static struct of_device_id of_hi6421_pmic_match_tbl[] = {
	{
		.compatible = "hisilicon,hi6421-pmic",
	},
	{ /* end */ }
};

/*
 * The PMIC register is only 8-bit.
 * Hisilicon SoC use hardware to map PMIC register into SoC mapping.
 * At here, we are accessing SoC register with 32-bit.
 */
u32 hi6421_pmic_read(struct hi6421_pmic *pmic, int reg)
{
	unsigned long flags;
	u32 ret;
	spin_lock_irqsave(&pmic->lock, flags);
	ret = readl_relaxed(pmic->regs + (reg << 2));
	spin_unlock_irqrestore(&pmic->lock, flags);
	return ret;
}
EXPORT_SYMBOL(hi6421_pmic_read);

void hi6421_pmic_write(struct hi6421_pmic *pmic, int reg, u32 val)
{
	unsigned long flags;
	spin_lock_irqsave(&pmic->lock, flags);
	writel_relaxed(val, pmic->regs + (reg << 2));
	spin_unlock_irqrestore(&pmic->lock, flags);
}
EXPORT_SYMBOL(hi6421_pmic_write);

void hi6421_pmic_rmw(struct hi6421_pmic *pmic, int reg,
		     u32 mask, u32 bits)
{
	u32 data;

	spin_lock(&pmic->lock);
	data = readl_relaxed(pmic->regs + (reg << 2)) & ~mask;
	data |= mask & bits;
	writel_relaxed(data, pmic->regs + (reg << 2));
	spin_unlock(&pmic->lock);
}
EXPORT_SYMBOL(hi6421_pmic_rmw);

static int hi6421_to_irq(struct hi6421_pmic *pmic, unsigned offset)
{
	return irq_find_mapping(pmic->domain, offset);
}

static irqreturn_t hi6421_irq_handler(int irq, void *data)
{
	struct hi6421_pmic *pmic = (struct hi6421_pmic *)data;
	unsigned long pending;
	int i, offset, index;


	for (i = HI6421_REG_IRQ1; i <= HI6421_REG_IRQ3; i++) {
		spin_lock(&pmic->lock);
		pending = readl_relaxed(pmic->regs + (i << 2));
		pending &= HI6421_MASK_FIELD;
		writel_relaxed(pending, pmic->regs + ((i + 3) << 2));
		spin_unlock(&pmic->lock);

		if (pending) {
			for_each_set_bit(offset, &pending, HI6421_BITS) {
				index = offset + (i - HI6421_REG_IRQ1) * HI6421_BITS;
				generic_handle_irq(hi6421_to_irq(pmic, index));
			}
		}

		spin_lock(&pmic->lock);
		writel_relaxed(0, pmic->regs + ((i + 3) << 2));
		writel_relaxed(pending, pmic->regs + (i << 2));
		spin_unlock(&pmic->lock);
	}

	return IRQ_HANDLED;
}

static void hi6421_irq_mask(struct irq_data *d)
{
	struct hi6421_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;

	offset = ((irqd_to_hwirq(d) >> 3) + HI6421_REG_IRQM1) << 2;
	spin_lock(&pmic->lock);
	data = readl_relaxed(pmic->regs + offset);
	data |= irqd_to_hwirq(d) % 8;
	writel_relaxed(data, pmic->regs + offset);
	spin_unlock(&pmic->lock);
}

static void hi6421_irq_unmask(struct irq_data *d)
{
	struct hi6421_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;

	offset = ((irqd_to_hwirq(d) >> 3) + HI6421_REG_IRQM1) << 2;
	spin_lock(&pmic->lock);
	data = readl_relaxed(pmic->regs + offset);
	data &= ~(irqd_to_hwirq(d) % 8);
	writel_relaxed(data, pmic->regs + offset);
	spin_unlock(&pmic->lock);
}

static struct irq_chip hi6421_irqchip = {
	.name		= "pmic",
	.irq_mask	= hi6421_irq_mask,
	.irq_unmask	= hi6421_irq_unmask,
};

static int hi6421_irq_map(struct irq_domain *d, unsigned int virq,
			  irq_hw_number_t hw)
{
	struct hi6421_pmic *pmic = d->host_data;

	irq_set_chip_and_handler_name(virq, &hi6421_irqchip,
				      handle_simple_irq, "hi6421");
	irq_set_chip_data(virq, pmic);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static struct irq_domain_ops hi6421_domain_ops = {
	.map	= hi6421_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int hi6421_pmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi6421_pmic *pmic = NULL;
	enum of_gpio_flags flags;
	int i, ret;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		dev_err(dev, "cannot allocate hi6421_pmic device info\n");
		return -ENOMEM;
	}

	mutex_init(&pmic->enable_mutex);
	/* get resources */
	pmic->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pmic->res) {
		dev_err(dev, "platform_get_resource err\n");
		return -ENOENT;
	}

	if (!devm_request_mem_region(dev, pmic->res->start,
				     resource_size(pmic->res),
				     pdev->name)) {
		dev_err(dev, "cannot claim register memory\n");
		return -ENOMEM;
	}

	pmic->regs = devm_ioremap(dev, pmic->res->start,
				  resource_size(pmic->res));
	if (!pmic->regs) {
		dev_err(dev, "cannot map register memory\n");
		return -ENOMEM;
	}

	/* TODO: get and enable clk request */

	spin_lock_init(&pmic->lock);

	pmic->gpio = of_get_gpio_flags(np, 0, &flags);
	if (pmic->gpio < 0)
		return pmic->gpio;
	if (!gpio_is_valid(pmic->gpio))
		return -EINVAL;
	ret = gpio_request_one(pmic->gpio, GPIOF_IN, "pmic");
	if (ret < 0) {
		dev_err(dev, "failed to request gpio%d\n", pmic->gpio);
		return ret;
	}
	pmic->irq = gpio_to_irq(pmic->gpio);
	/* clear IRQ status */
	spin_lock(&pmic->lock);
	writel_relaxed(0xff, pmic->regs + (HI6421_REG_IRQ1 << 2));
	writel_relaxed(0xff, pmic->regs + (HI6421_REG_IRQ2 << 2));
	writel_relaxed(0xff, pmic->regs + (HI6421_REG_IRQ3 << 2));
	spin_unlock(&pmic->lock);

	pmic->domain = irq_domain_add_simple(np, HI6421_NR_IRQ, 0,
					     &hi6421_domain_ops, pmic);
	if (!pmic->domain)
		return -ENODEV;

	for (i = 0; i < HI6421_NR_IRQ; i++) {
		ret = irq_create_mapping(pmic->domain, i);
		if (ret == NO_IRQ) {
			dev_err(dev, "failed mapping hwirq %d\n", i);
			return -ENOMEM;
		}
	}

	ret = request_threaded_irq(pmic->irq, hi6421_irq_handler, NULL,
		IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
		"pmic", pmic);

	platform_set_drvdata(pdev, pmic);

	/* set over-current protection debounce 8ms*/
	hi6421_pmic_rmw(pmic, OCP_DEB_CTRL_REG, \
		OCP_DEB_SEL_MASK | OCP_EN_DEBOUNCE_MASK | OCP_AUTO_STOP_MASK, \
		OCP_DEB_SEL_8MS | OCP_EN_DEBOUNCE_ENABLE);

	/* populate sub nodes */
	of_platform_populate(np, of_hi6421_pmic_child_match_tbl, NULL, dev);

	return 0;
}

static int hi6421_pmic_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi6421_pmic *pmic = platform_get_drvdata(pdev);

	free_irq(pmic->irq, pmic);
	gpio_free(pmic->gpio);
	devm_iounmap(dev, pmic->regs);
	devm_release_mem_region(dev, pmic->res->start,
				resource_size(pmic->res));
	devm_kfree(dev, pmic);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver hi6421_pmic_driver = {
	.driver = {
		.name	= "hi6421_pmic",
		.owner  = THIS_MODULE,
		.of_match_table = of_hi6421_pmic_match_tbl,
	},
	.probe	= hi6421_pmic_probe,
	.remove	= hi6421_pmic_remove,
};
module_platform_driver(hi6421_pmic_driver);

MODULE_AUTHOR("Guodong Xu <guodong.xu@linaro.org>");
MODULE_DESCRIPTION("Hi6421 PMIC driver");
MODULE_LICENSE("GPL v2");
