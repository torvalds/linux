/*
 * drivers/mtd/nand/gpio.c
 *
 * Updated, and converted to generic GPIO based driver by Russell King.
 *
 * Written by Ben Dooks <ben@simtec.co.uk>
 *   Based on 2.4 version by Mark Whittaker
 *
 * © 2004 Simtec Electronics
 *
 * Device driver for NAND connected via GPIO
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand-gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

struct gpiomtd {
	void __iomem		*io_sync;
	struct mtd_info		mtd_info;
	struct nand_chip	nand_chip;
	struct gpio_nand_platdata plat;
};

#define gpio_nand_getpriv(x) container_of(x, struct gpiomtd, mtd_info)


#ifdef CONFIG_ARM
/* gpio_nand_dosync()
 *
 * Make sure the GPIO state changes occur in-order with writes to NAND
 * memory region.
 * Needed on PXA due to bus-reordering within the SoC itself (see section on
 * I/O ordering in PXA manual (section 2.3, p35)
 */
static void gpio_nand_dosync(struct gpiomtd *gpiomtd)
{
	unsigned long tmp;

	if (gpiomtd->io_sync) {
		/*
		 * Linux memory barriers don't cater for what's required here.
		 * What's required is what's here - a read from a separate
		 * region with a dependency on that read.
		 */
		tmp = readl(gpiomtd->io_sync);
		asm volatile("mov %1, %0\n" : "=r" (tmp) : "r" (tmp));
	}
}
#else
static inline void gpio_nand_dosync(struct gpiomtd *gpiomtd) {}
#endif

static void gpio_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(mtd);

	gpio_nand_dosync(gpiomtd);

	if (ctrl & NAND_CTRL_CHANGE) {
		gpio_set_value(gpiomtd->plat.gpio_nce, !(ctrl & NAND_NCE));
		gpio_set_value(gpiomtd->plat.gpio_cle, !!(ctrl & NAND_CLE));
		gpio_set_value(gpiomtd->plat.gpio_ale, !!(ctrl & NAND_ALE));
		gpio_nand_dosync(gpiomtd);
	}
	if (cmd == NAND_CMD_NONE)
		return;

	writeb(cmd, gpiomtd->nand_chip.IO_ADDR_W);
	gpio_nand_dosync(gpiomtd);
}

static int gpio_nand_devready(struct mtd_info *mtd)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(mtd);

	return gpio_get_value(gpiomtd->plat.gpio_rdy);
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_nand_id_table[] = {
	{ .compatible = "gpio-control-nand" },
	{}
};
MODULE_DEVICE_TABLE(of, gpio_nand_id_table);

static int gpio_nand_get_config_of(const struct device *dev,
				   struct gpio_nand_platdata *plat)
{
	u32 val;

	if (!dev->of_node)
		return -ENODEV;

	if (!of_property_read_u32(dev->of_node, "bank-width", &val)) {
		if (val == 2) {
			plat->options |= NAND_BUSWIDTH_16;
		} else if (val != 1) {
			dev_err(dev, "invalid bank-width %u\n", val);
			return -EINVAL;
		}
	}

	plat->gpio_rdy = of_get_gpio(dev->of_node, 0);
	plat->gpio_nce = of_get_gpio(dev->of_node, 1);
	plat->gpio_ale = of_get_gpio(dev->of_node, 2);
	plat->gpio_cle = of_get_gpio(dev->of_node, 3);
	plat->gpio_nwp = of_get_gpio(dev->of_node, 4);

	if (!of_property_read_u32(dev->of_node, "chip-delay", &val))
		plat->chip_delay = val;

	return 0;
}

static struct resource *gpio_nand_get_io_sync_of(struct platform_device *pdev)
{
	struct resource *r;
	u64 addr;

	if (of_property_read_u64(pdev->dev.of_node,
				       "gpio-control-nand,io-sync-reg", &addr))
		return NULL;

	r = devm_kzalloc(&pdev->dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;

	r->start = addr;
	r->end = r->start + 0x3;
	r->flags = IORESOURCE_MEM;

	return r;
}
#else /* CONFIG_OF */
static inline int gpio_nand_get_config_of(const struct device *dev,
					  struct gpio_nand_platdata *plat)
{
	return -ENOSYS;
}

static inline struct resource *
gpio_nand_get_io_sync_of(struct platform_device *pdev)
{
	return NULL;
}
#endif /* CONFIG_OF */

static inline int gpio_nand_get_config(const struct device *dev,
				       struct gpio_nand_platdata *plat)
{
	int ret = gpio_nand_get_config_of(dev, plat);

	if (!ret)
		return ret;

	if (dev_get_platdata(dev)) {
		memcpy(plat, dev_get_platdata(dev), sizeof(*plat));
		return 0;
	}

	return -EINVAL;
}

static inline struct resource *
gpio_nand_get_io_sync(struct platform_device *pdev)
{
	struct resource *r = gpio_nand_get_io_sync_of(pdev);

	if (r)
		return r;

	return platform_get_resource(pdev, IORESOURCE_MEM, 1);
}

static int gpio_nand_remove(struct platform_device *pdev)
{
	struct gpiomtd *gpiomtd = platform_get_drvdata(pdev);

	nand_release(&gpiomtd->mtd_info);

	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_set_value(gpiomtd->plat.gpio_nwp, 0);
	gpio_set_value(gpiomtd->plat.gpio_nce, 1);

	return 0;
}

static int gpio_nand_probe(struct platform_device *pdev)
{
	struct gpiomtd *gpiomtd;
	struct nand_chip *chip;
	struct resource *res;
	struct mtd_part_parser_data ppdata = {};
	int ret = 0;

	if (!pdev->dev.of_node && !dev_get_platdata(&pdev->dev))
		return -EINVAL;

	gpiomtd = devm_kzalloc(&pdev->dev, sizeof(*gpiomtd), GFP_KERNEL);
	if (!gpiomtd)
		return -ENOMEM;

	chip = &gpiomtd->nand_chip;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->IO_ADDR_R = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chip->IO_ADDR_R))
		return PTR_ERR(chip->IO_ADDR_R);

	res = gpio_nand_get_io_sync(pdev);
	if (res) {
		gpiomtd->io_sync = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(gpiomtd->io_sync))
			return PTR_ERR(gpiomtd->io_sync);
	}

	ret = gpio_nand_get_config(&pdev->dev, &gpiomtd->plat);
	if (ret)
		return ret;

	ret = devm_gpio_request(&pdev->dev, gpiomtd->plat.gpio_nce, "NAND NCE");
	if (ret)
		return ret;
	gpio_direction_output(gpiomtd->plat.gpio_nce, 1);

	if (gpio_is_valid(gpiomtd->plat.gpio_nwp)) {
		ret = devm_gpio_request(&pdev->dev, gpiomtd->plat.gpio_nwp,
					"NAND NWP");
		if (ret)
			return ret;
	}

	ret = devm_gpio_request(&pdev->dev, gpiomtd->plat.gpio_ale, "NAND ALE");
	if (ret)
		return ret;
	gpio_direction_output(gpiomtd->plat.gpio_ale, 0);

	ret = devm_gpio_request(&pdev->dev, gpiomtd->plat.gpio_cle, "NAND CLE");
	if (ret)
		return ret;
	gpio_direction_output(gpiomtd->plat.gpio_cle, 0);

	if (gpio_is_valid(gpiomtd->plat.gpio_rdy)) {
		ret = devm_gpio_request(&pdev->dev, gpiomtd->plat.gpio_rdy,
					"NAND RDY");
		if (ret)
			return ret;
		gpio_direction_input(gpiomtd->plat.gpio_rdy);
		chip->dev_ready = gpio_nand_devready;
	}

	chip->IO_ADDR_W		= chip->IO_ADDR_R;
	chip->ecc.mode		= NAND_ECC_SOFT;
	chip->options		= gpiomtd->plat.options;
	chip->chip_delay	= gpiomtd->plat.chip_delay;
	chip->cmd_ctrl		= gpio_nand_cmd_ctrl;

	gpiomtd->mtd_info.priv	= chip;
	gpiomtd->mtd_info.owner	= THIS_MODULE;

	platform_set_drvdata(pdev, gpiomtd);

	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_direction_output(gpiomtd->plat.gpio_nwp, 1);

	if (nand_scan(&gpiomtd->mtd_info, 1)) {
		ret = -ENXIO;
		goto err_wp;
	}

	if (gpiomtd->plat.adjust_parts)
		gpiomtd->plat.adjust_parts(&gpiomtd->plat,
					   gpiomtd->mtd_info.size);

	ppdata.of_node = pdev->dev.of_node;
	ret = mtd_device_parse_register(&gpiomtd->mtd_info, NULL, &ppdata,
					gpiomtd->plat.parts,
					gpiomtd->plat.num_parts);
	if (!ret)
		return 0;

err_wp:
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_set_value(gpiomtd->plat.gpio_nwp, 0);

	return ret;
}

static struct platform_driver gpio_nand_driver = {
	.probe		= gpio_nand_probe,
	.remove		= gpio_nand_remove,
	.driver		= {
		.name	= "gpio-nand",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(gpio_nand_id_table),
	},
};

module_platform_driver(gpio_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("GPIO NAND Driver");
