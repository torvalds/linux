/*
 * Updated, and converted to generic GPIO based driver by Russell King.
 *
 * Written by Ben Dooks <ben@simtec.co.uk>
 *   Based on 2.4 version by Mark Whittaker
 *
 * © 2004 Simtec Electronics
 *
 * Device driver for NAND flash that uses a memory mapped interface to
 * read/write the NAND commands and data, and GPIO pins for control signals
 * (the DT binding refers to this as "GPIO assisted NAND flash")
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand-gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>

struct gpiomtd {
	void __iomem		*io_sync;
	struct nand_chip	nand_chip;
	struct gpio_nand_platdata plat;
	struct gpio_desc *nce; /* Optional chip enable */
	struct gpio_desc *cle;
	struct gpio_desc *ale;
	struct gpio_desc *rdy;
	struct gpio_desc *nwp; /* Optional write protection */
};

static inline struct gpiomtd *gpio_nand_getpriv(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct gpiomtd, nand_chip);
}


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

static void gpio_nand_cmd_ctrl(struct nand_chip *chip, int cmd,
			       unsigned int ctrl)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(nand_to_mtd(chip));

	gpio_nand_dosync(gpiomtd);

	if (ctrl & NAND_CTRL_CHANGE) {
		if (gpiomtd->nce)
			gpiod_set_value(gpiomtd->nce, !(ctrl & NAND_NCE));
		gpiod_set_value(gpiomtd->cle, !!(ctrl & NAND_CLE));
		gpiod_set_value(gpiomtd->ale, !!(ctrl & NAND_ALE));
		gpio_nand_dosync(gpiomtd);
	}
	if (cmd == NAND_CMD_NONE)
		return;

	writeb(cmd, gpiomtd->nand_chip.legacy.IO_ADDR_W);
	gpio_nand_dosync(gpiomtd);
}

static int gpio_nand_devready(struct nand_chip *chip)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(nand_to_mtd(chip));

	return gpiod_get_value(gpiomtd->rdy);
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

	nand_release(&gpiomtd->nand_chip);

	/* Enable write protection and disable the chip */
	if (gpiomtd->nwp && !IS_ERR(gpiomtd->nwp))
		gpiod_set_value(gpiomtd->nwp, 0);
	if (gpiomtd->nce && !IS_ERR(gpiomtd->nce))
		gpiod_set_value(gpiomtd->nce, 0);

	return 0;
}

static int gpio_nand_probe(struct platform_device *pdev)
{
	struct gpiomtd *gpiomtd;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret = 0;

	if (!dev->of_node && !dev_get_platdata(dev))
		return -EINVAL;

	gpiomtd = devm_kzalloc(dev, sizeof(*gpiomtd), GFP_KERNEL);
	if (!gpiomtd)
		return -ENOMEM;

	chip = &gpiomtd->nand_chip;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->legacy.IO_ADDR_R = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->legacy.IO_ADDR_R))
		return PTR_ERR(chip->legacy.IO_ADDR_R);

	res = gpio_nand_get_io_sync(pdev);
	if (res) {
		gpiomtd->io_sync = devm_ioremap_resource(dev, res);
		if (IS_ERR(gpiomtd->io_sync))
			return PTR_ERR(gpiomtd->io_sync);
	}

	ret = gpio_nand_get_config(dev, &gpiomtd->plat);
	if (ret)
		return ret;

	/* Just enable the chip */
	gpiomtd->nce = devm_gpiod_get_optional(dev, "nce", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiomtd->nce))
		return PTR_ERR(gpiomtd->nce);

	/* We disable write protection once we know probe() will succeed */
	gpiomtd->nwp = devm_gpiod_get_optional(dev, "nwp", GPIOD_OUT_LOW);
	if (IS_ERR(gpiomtd->nwp)) {
		ret = PTR_ERR(gpiomtd->nwp);
		goto out_ce;
	}

	gpiomtd->ale = devm_gpiod_get(dev, "ale", GPIOD_OUT_LOW);
	if (IS_ERR(gpiomtd->ale)) {
		ret = PTR_ERR(gpiomtd->ale);
		goto out_ce;
	}

	gpiomtd->cle = devm_gpiod_get(dev, "cle", GPIOD_OUT_LOW);
	if (IS_ERR(gpiomtd->cle)) {
		ret = PTR_ERR(gpiomtd->cle);
		goto out_ce;
	}

	gpiomtd->rdy = devm_gpiod_get_optional(dev, "rdy", GPIOD_IN);
	if (IS_ERR(gpiomtd->rdy)) {
		ret = PTR_ERR(gpiomtd->rdy);
		goto out_ce;
	}
	/* Using RDY pin */
	if (gpiomtd->rdy)
		chip->legacy.dev_ready = gpio_nand_devready;

	nand_set_flash_node(chip, pdev->dev.of_node);
	chip->legacy.IO_ADDR_W	= chip->legacy.IO_ADDR_R;
	chip->ecc.mode		= NAND_ECC_SOFT;
	chip->ecc.algo		= NAND_ECC_HAMMING;
	chip->options		= gpiomtd->plat.options;
	chip->legacy.chip_delay	= gpiomtd->plat.chip_delay;
	chip->legacy.cmd_ctrl	= gpio_nand_cmd_ctrl;

	mtd			= nand_to_mtd(chip);
	mtd->dev.parent		= dev;

	platform_set_drvdata(pdev, gpiomtd);

	/* Disable write protection, if wired up */
	if (gpiomtd->nwp && !IS_ERR(gpiomtd->nwp))
		gpiod_direction_output(gpiomtd->nwp, 1);

	ret = nand_scan(chip, 1);
	if (ret)
		goto err_wp;

	if (gpiomtd->plat.adjust_parts)
		gpiomtd->plat.adjust_parts(&gpiomtd->plat, mtd->size);

	ret = mtd_device_register(mtd, gpiomtd->plat.parts,
				  gpiomtd->plat.num_parts);
	if (!ret)
		return 0;

err_wp:
	if (gpiomtd->nwp && !IS_ERR(gpiomtd->nwp))
		gpiod_set_value(gpiomtd->nwp, 0);
out_ce:
	if (gpiomtd->nce && !IS_ERR(gpiomtd->nce))
		gpiod_set_value(gpiomtd->nce, 0);

	return ret;
}

static struct platform_driver gpio_nand_driver = {
	.probe		= gpio_nand_probe,
	.remove		= gpio_nand_remove,
	.driver		= {
		.name	= "gpio-nand",
		.of_match_table = of_match_ptr(gpio_nand_id_table),
	},
};

module_platform_driver(gpio_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("GPIO NAND Driver");
