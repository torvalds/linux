/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/basic_mmio_gpio.h>

#define GIO_BANK_SIZE           0x20
#define GIO_ODEN(bank)          (((bank) * GIO_BANK_SIZE) + 0x00)
#define GIO_DATA(bank)          (((bank) * GIO_BANK_SIZE) + 0x04)
#define GIO_IODIR(bank)         (((bank) * GIO_BANK_SIZE) + 0x08)
#define GIO_EC(bank)            (((bank) * GIO_BANK_SIZE) + 0x0c)
#define GIO_EI(bank)            (((bank) * GIO_BANK_SIZE) + 0x10)
#define GIO_MASK(bank)          (((bank) * GIO_BANK_SIZE) + 0x14)
#define GIO_LEVEL(bank)         (((bank) * GIO_BANK_SIZE) + 0x18)
#define GIO_STAT(bank)          (((bank) * GIO_BANK_SIZE) + 0x1c)

struct brcmstb_gpio_bank {
	struct list_head node;
	int id;
	struct bgpio_chip bgc;
	struct brcmstb_gpio_priv *parent_priv;
	u32 width;
};

struct brcmstb_gpio_priv {
	struct list_head bank_list;
	void __iomem *reg_base;
	int num_banks;
	struct platform_device *pdev;
	int gpio_base;
};

#define MAX_GPIO_PER_BANK           32
#define GPIO_BANK(gpio)         ((gpio) >> 5)
/* assumes MAX_GPIO_PER_BANK is a multiple of 2 */
#define GPIO_BIT(gpio)          ((gpio) & (MAX_GPIO_PER_BANK - 1))

static inline struct brcmstb_gpio_bank *
brcmstb_gpio_gc_to_bank(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);
	return container_of(bgc, struct brcmstb_gpio_bank, bgc);
}

static inline struct brcmstb_gpio_priv *
brcmstb_gpio_gc_to_priv(struct gpio_chip *gc)
{
	struct brcmstb_gpio_bank *bank = brcmstb_gpio_gc_to_bank(gc);
	return bank->parent_priv;
}

/* Make sure that the number of banks matches up between properties */
static int brcmstb_gpio_sanity_check_banks(struct device *dev,
		struct device_node *np, struct resource *res)
{
	int res_num_banks = resource_size(res) / GIO_BANK_SIZE;
	int num_banks =
		of_property_count_u32_elems(np, "brcm,gpio-bank-widths");

	if (res_num_banks != num_banks) {
		dev_err(dev, "Mismatch in banks: res had %d, bank-widths had %d\n",
				res_num_banks, num_banks);
		return -EINVAL;
	} else {
		return 0;
	}
}

static int brcmstb_gpio_remove(struct platform_device *pdev)
{
	struct brcmstb_gpio_priv *priv = platform_get_drvdata(pdev);
	struct list_head *pos;
	struct brcmstb_gpio_bank *bank;
	int ret = 0;

	if (!priv) {
		dev_err(&pdev->dev, "called %s without drvdata!\n", __func__);
		return -EFAULT;
	}

	/*
	 * You can lose return values below, but we report all errors, and it's
	 * more important to actually perform all of the steps.
	 */
	list_for_each(pos, &priv->bank_list) {
		bank = list_entry(pos, struct brcmstb_gpio_bank, node);
		ret = bgpio_remove(&bank->bgc);
		if (ret)
			dev_err(&pdev->dev, "gpiochip_remove fail in cleanup");
	}
	return ret;
}

static int brcmstb_gpio_of_xlate(struct gpio_chip *gc,
		const struct of_phandle_args *gpiospec, u32 *flags)
{
	struct brcmstb_gpio_priv *priv = brcmstb_gpio_gc_to_priv(gc);
	struct brcmstb_gpio_bank *bank = brcmstb_gpio_gc_to_bank(gc);
	int offset;

	if (gc->of_gpio_n_cells != 2) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(gpiospec->args_count < gc->of_gpio_n_cells))
		return -EINVAL;

	offset = gpiospec->args[0] - (gc->base - priv->gpio_base);
	if (offset >= gc->ngpio)
		return -EINVAL;

	if (unlikely(offset >= bank->width)) {
		dev_warn_ratelimited(&priv->pdev->dev,
			"Received request for invalid GPIO offset %d\n",
			gpiospec->args[0]);
	}

	if (flags)
		*flags = gpiospec->args[1];

	return offset;
}

static int brcmstb_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *reg_base;
	struct brcmstb_gpio_priv *priv;
	struct resource *res;
	struct property *prop;
	const __be32 *p;
	u32 bank_width;
	int err;
	static int gpio_base;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);
	INIT_LIST_HEAD(&priv->bank_list);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	priv->gpio_base = gpio_base;
	priv->reg_base = reg_base;
	priv->pdev = pdev;

	if (brcmstb_gpio_sanity_check_banks(dev, np, res))
		return -EINVAL;

	of_property_for_each_u32(np, "brcm,gpio-bank-widths", prop, p,
			bank_width) {
		struct brcmstb_gpio_bank *bank;
		struct bgpio_chip *bgc;
		struct gpio_chip *gc;

		bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
		if (!bank) {
			err = -ENOMEM;
			goto fail;
		}

		bank->parent_priv = priv;
		bank->id = priv->num_banks;
		if (bank_width <= 0 || bank_width > MAX_GPIO_PER_BANK) {
			dev_err(dev, "Invalid bank width %d\n", bank_width);
			goto fail;
		} else {
			bank->width = bank_width;
		}

		/*
		 * Regs are 4 bytes wide, have data reg, no set/clear regs,
		 * and direction bits have 0 = output and 1 = input
		 */
		bgc = &bank->bgc;
		err = bgpio_init(bgc, dev, 4,
				reg_base + GIO_DATA(bank->id),
				NULL, NULL, NULL,
				reg_base + GIO_IODIR(bank->id), 0);
		if (err) {
			dev_err(dev, "bgpio_init() failed\n");
			goto fail;
		}

		gc = &bgc->gc;
		gc->of_node = np;
		gc->owner = THIS_MODULE;
		gc->label = np->full_name;
		gc->base = gpio_base;
		gc->of_gpio_n_cells = 2;
		gc->of_xlate = brcmstb_gpio_of_xlate;
		/* not all ngpio lines are valid, will use bank width later */
		gc->ngpio = MAX_GPIO_PER_BANK;

		err = gpiochip_add(gc);
		if (err) {
			dev_err(dev, "Could not add gpiochip for bank %d\n",
					bank->id);
			goto fail;
		}
		gpio_base += gc->ngpio;
		dev_dbg(dev, "bank=%d, base=%d, ngpio=%d, width=%d\n", bank->id,
			gc->base, gc->ngpio, bank->width);

		/* Everything looks good, so add bank to list */
		list_add(&bank->node, &priv->bank_list);

		priv->num_banks++;
	}

	dev_info(dev, "Registered %d banks (GPIO(s): %d-%d)\n",
			priv->num_banks, priv->gpio_base, gpio_base - 1);

	return 0;

fail:
	(void) brcmstb_gpio_remove(pdev);
	return err;
}

static const struct of_device_id brcmstb_gpio_of_match[] = {
	{ .compatible = "brcm,brcmstb-gpio" },
	{},
};

MODULE_DEVICE_TABLE(of, brcmstb_gpio_of_match);

static struct platform_driver brcmstb_gpio_driver = {
	.driver = {
		.name = "brcmstb-gpio",
		.of_match_table = brcmstb_gpio_of_match,
	},
	.probe = brcmstb_gpio_probe,
	.remove = brcmstb_gpio_remove,
};
module_platform_driver(brcmstb_gpio_driver);

MODULE_AUTHOR("Gregory Fong");
MODULE_DESCRIPTION("Driver for Broadcom BRCMSTB SoC UPG GPIO");
MODULE_LICENSE("GPL v2");
