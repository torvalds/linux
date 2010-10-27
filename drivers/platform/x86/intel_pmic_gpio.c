/* Moorestown PMIC GPIO (access through IPC) driver
 * Copyright (c) 2008 - 2009, Intel Corporation.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Moorestown platform PMIC chip
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <asm/intel_scu_ipc.h>
#include <linux/device.h>
#include <linux/intel_pmic_gpio.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "pmic_gpio"

/* register offset that IPC driver should use
 * 8 GPIO + 8 GPOSW (6 controllable) + 8GPO
 */
enum pmic_gpio_register {
	GPIO0		= 0xE0,
	GPIO7		= 0xE7,
	GPIOINT		= 0xE8,
	GPOSWCTL0	= 0xEC,
	GPOSWCTL5	= 0xF1,
	GPO		= 0xF4,
};

/* bits definition for GPIO & GPOSW */
#define GPIO_DRV 0x01
#define GPIO_DIR 0x02
#define GPIO_DIN 0x04
#define GPIO_DOU 0x08
#define GPIO_INTCTL 0x30
#define GPIO_DBC 0xc0

#define GPOSW_DRV 0x01
#define GPOSW_DOU 0x08
#define GPOSW_RDRV 0x30


#define NUM_GPIO 24

struct pmic_gpio_irq {
	spinlock_t lock;
	u32 trigger[NUM_GPIO];
	u32 dirty;
	struct work_struct work;
};


struct pmic_gpio {
	struct gpio_chip	chip;
	struct pmic_gpio_irq	irqtypes;
	void			*gpiointr;
	int			irq;
	unsigned		irq_base;
};

static void pmic_program_irqtype(int gpio, int type)
{
	if (type & IRQ_TYPE_EDGE_RISING)
		intel_scu_ipc_update_register(GPIO0 + gpio, 0x20, 0x20);
	else
		intel_scu_ipc_update_register(GPIO0 + gpio, 0x00, 0x20);

	if (type & IRQ_TYPE_EDGE_FALLING)
		intel_scu_ipc_update_register(GPIO0 + gpio, 0x10, 0x10);
	else
		intel_scu_ipc_update_register(GPIO0 + gpio, 0x00, 0x10);
};

static void pmic_irqtype_work(struct work_struct *work)
{
	struct pmic_gpio_irq *t =
		container_of(work, struct pmic_gpio_irq, work);
	unsigned long flags;
	int i;
	u16 type;

	spin_lock_irqsave(&t->lock, flags);
	/* As we drop the lock, we may need multiple scans if we race the
	   pmic_irq_type function */
	while (t->dirty) {
		/*
		 *	For each pin that has the dirty bit set send an IPC
		 *	message to configure the hardware via the PMIC
		 */
		for (i = 0; i < NUM_GPIO; i++) {
			if (!(t->dirty & (1 << i)))
				continue;
			t->dirty &= ~(1 << i);
			/* We can't trust the array entry or dirty
			   once the lock is dropped */
			type = t->trigger[i];
			spin_unlock_irqrestore(&t->lock, flags);
			pmic_program_irqtype(i, type);
			spin_lock_irqsave(&t->lock, flags);
		}
	}
	spin_unlock_irqrestore(&t->lock, flags);
}

static int pmic_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 8) {
		printk(KERN_ERR
			"%s: only pin 0-7 support input\n", __func__);
		return -1;/* we only have 8 GPIO can use as input */
	}
	return intel_scu_ipc_update_register(GPIO0 + offset,
							GPIO_DIR, GPIO_DIR);
}

static int pmic_gpio_direction_output(struct gpio_chip *chip,
			unsigned offset, int value)
{
	int rc = 0;

	if (offset < 8)/* it is GPIO */
		rc = intel_scu_ipc_update_register(GPIO0 + offset,
				GPIO_DRV | GPIO_DOU | GPIO_DIR,
				GPIO_DRV | (value ? GPIO_DOU : 0));
	else if (offset < 16)/* it is GPOSW */
		rc = intel_scu_ipc_update_register(GPOSWCTL0 + offset - 8,
				GPOSW_DRV | GPOSW_DOU | GPOSW_RDRV,
				GPOSW_DRV | (value ? GPOSW_DOU : 0));
	else if (offset > 15 && offset < 24)/* it is GPO */
		rc = intel_scu_ipc_update_register(GPO,
				1 << (offset - 16),
				value ? 1 << (offset - 16) : 0);
	else {
		printk(KERN_ERR
			"%s: invalid PMIC GPIO pin %d!\n", __func__, offset);
		WARN_ON(1);
	}

	return rc;
}

static int pmic_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	u8 r;
	int ret;

	/* we only have 8 GPIO pins we can use as input */
	if (offset > 8)
		return -EOPNOTSUPP;
	ret = intel_scu_ipc_ioread8(GPIO0 + offset, &r);
	if (ret < 0)
		return ret;
	return r & GPIO_DIN;
}

static void pmic_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (offset < 8)/* it is GPIO */
		intel_scu_ipc_update_register(GPIO0 + offset,
			GPIO_DRV | GPIO_DOU,
			GPIO_DRV | (value ? GPIO_DOU : 0));
	else if (offset < 16)/* it is GPOSW */
		intel_scu_ipc_update_register(GPOSWCTL0 + offset - 8,
			GPOSW_DRV | GPOSW_DOU | GPOSW_RDRV,
			GPOSW_DRV | (value ? GPOSW_DOU : 0));
	else if (offset > 15 && offset < 24) /* it is GPO */
		intel_scu_ipc_update_register(GPO,
			1 << (offset - 16),
			value ? 1 << (offset - 16) : 0);
}

static int pmic_irq_type(unsigned irq, unsigned type)
{
	struct pmic_gpio *pg = get_irq_chip_data(irq);
	u32 gpio = irq - pg->irq_base;
	unsigned long flags;

	if (gpio > pg->chip.ngpio)
		return -EINVAL;

	spin_lock_irqsave(&pg->irqtypes.lock, flags);
	pg->irqtypes.trigger[gpio] = type;
	pg->irqtypes.dirty |=  (1 << gpio);
	spin_unlock_irqrestore(&pg->irqtypes.lock, flags);
	schedule_work(&pg->irqtypes.work);
	return 0;
}



static int pmic_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct pmic_gpio *pg = container_of(chip, struct pmic_gpio, chip);

	return pg->irq_base + offset;
}

/* the gpiointr register is read-clear, so just do nothing. */
static void pmic_irq_unmask(unsigned irq)
{
};

static void pmic_irq_mask(unsigned irq)
{
};

static struct irq_chip pmic_irqchip = {
	.name		= "PMIC-GPIO",
	.mask		= pmic_irq_mask,
	.unmask		= pmic_irq_unmask,
	.set_type	= pmic_irq_type,
};

static void pmic_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct pmic_gpio *pg = (struct pmic_gpio *)get_irq_data(irq);
	u8 intsts = *((u8 *)pg->gpiointr + 4);
	int gpio;

	for (gpio = 0; gpio < 8; gpio++) {
		if (intsts & (1 << gpio)) {
			pr_debug("pmic pin %d triggered\n", gpio);
			generic_handle_irq(pg->irq_base + gpio);
		}
	}
	desc->chip->eoi(irq);
}

static int __devinit platform_pmic_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq = platform_get_irq(pdev, 0);
	struct intel_pmic_gpio_platform_data *pdata = dev->platform_data;

	struct pmic_gpio *pg;
	int retval;
	int i;

	if (irq < 0) {
		dev_dbg(dev, "no IRQ line\n");
		return -EINVAL;
	}

	if (!pdata || !pdata->gpio_base || !pdata->irq_base) {
		dev_dbg(dev, "incorrect or missing platform data\n");
		return -EINVAL;
	}

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	dev_set_drvdata(dev, pg);

	pg->irq = irq;
	/* setting up SRAM mapping for GPIOINT register */
	pg->gpiointr = ioremap_nocache(pdata->gpiointr, 8);
	if (!pg->gpiointr) {
		printk(KERN_ERR "%s: Can not map GPIOINT.\n", __func__);
		retval = -EINVAL;
		goto err2;
	}
	pg->irq_base = pdata->irq_base;
	pg->chip.label = "intel_pmic";
	pg->chip.direction_input = pmic_gpio_direction_input;
	pg->chip.direction_output = pmic_gpio_direction_output;
	pg->chip.get = pmic_gpio_get;
	pg->chip.set = pmic_gpio_set;
	pg->chip.to_irq = pmic_gpio_to_irq;
	pg->chip.base = pdata->gpio_base;
	pg->chip.ngpio = NUM_GPIO;
	pg->chip.can_sleep = 1;
	pg->chip.dev = dev;

	INIT_WORK(&pg->irqtypes.work, pmic_irqtype_work);
	spin_lock_init(&pg->irqtypes.lock);

	pg->chip.dev = dev;
	retval = gpiochip_add(&pg->chip);
	if (retval) {
		printk(KERN_ERR "%s: Can not add pmic gpio chip.\n", __func__);
		goto err;
	}
	set_irq_data(pg->irq, pg);
	set_irq_chained_handler(pg->irq, pmic_irq_handler);
	for (i = 0; i < 8; i++) {
		set_irq_chip_and_handler_name(i + pg->irq_base, &pmic_irqchip,
					handle_simple_irq, "demux");
		set_irq_chip_data(i + pg->irq_base, pg);
	}
	return 0;
err:
	iounmap(pg->gpiointr);
err2:
	kfree(pg);
	return retval;
}

/* at the same time, register a platform driver
 * this supports the sfi 0.81 fw */
static struct platform_driver platform_pmic_gpio_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
	.probe		= platform_pmic_gpio_probe,
};

static int __init platform_pmic_gpio_init(void)
{
	return platform_driver_register(&platform_pmic_gpio_driver);
}

subsys_initcall(platform_pmic_gpio_init);

MODULE_AUTHOR("Alek Du <alek.du@intel.com>");
MODULE_DESCRIPTION("Intel Moorestown PMIC GPIO driver");
MODULE_LICENSE("GPL v2");
