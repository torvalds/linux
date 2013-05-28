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

static void gpio_nand_writebuf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	iowrite8_rep(this->IO_ADDR_W, buf, len);
}

static void gpio_nand_readbuf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	ioread8_rep(this->IO_ADDR_R, buf, len);
}

static void gpio_nand_writebuf16(struct mtd_info *mtd, const u_char *buf,
				 int len)
{
	struct nand_chip *this = mtd->priv;

	if (IS_ALIGNED((unsigned long)buf, 2)) {
		iowrite16_rep(this->IO_ADDR_W, buf, len>>1);
	} else {
		int i;
		unsigned short *ptr = (unsigned short *)buf;

		for (i = 0; i < len; i += 2, ptr++)
			writew(*ptr, this->IO_ADDR_W);
	}
}

static void gpio_nand_readbuf16(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	if (IS_ALIGNED((unsigned long)buf, 2)) {
		ioread16_rep(this->IO_ADDR_R, buf, len>>1);
	} else {
		int i;
		unsigned short *ptr = (unsigned short *)buf;

		for (i = 0; i < len; i += 2, ptr++)
			*ptr = readw(this->IO_ADDR_R);
	}
}

static int gpio_nand_devready(struct mtd_info *mtd)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(mtd);

	if (gpio_is_valid(gpiomtd->plat.gpio_rdy))
		return gpio_get_value(gpiomtd->plat.gpio_rdy);

	return 1;
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
	struct resource *r = devm_kzalloc(&pdev->dev, sizeof(*r), GFP_KERNEL);
	u64 addr;

	if (!r || of_property_read_u64(pdev->dev.of_node,
				       "gpio-control-nand,io-sync-reg", &addr))
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

	if (dev->platform_data) {
		memcpy(plat, dev->platform_data, sizeof(*plat));
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

static int gpio_nand_remove(struct platform_device *dev)
{
	struct gpiomtd *gpiomtd = platform_get_drvdata(dev);
	struct resource *res;

	nand_release(&gpiomtd->mtd_info);

	res = gpio_nand_get_io_sync(dev);
	iounmap(gpiomtd->io_sync);
	if (res)
		release_mem_region(res->start, resource_size(res));

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	iounmap(gpiomtd->nand_chip.IO_ADDR_R);
	release_mem_region(res->start, resource_size(res));

	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_set_value(gpiomtd->plat.gpio_nwp, 0);
	gpio_set_value(gpiomtd->plat.gpio_nce, 1);

	gpio_free(gpiomtd->plat.gpio_cle);
	gpio_free(gpiomtd->plat.gpio_ale);
	gpio_free(gpiomtd->plat.gpio_nce);
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_free(gpiomtd->plat.gpio_nwp);
	if (gpio_is_valid(gpiomtd->plat.gpio_rdy))
		gpio_free(gpiomtd->plat.gpio_rdy);

	return 0;
}

static void __iomem *request_and_remap(struct resource *res, size_t size,
					const char *name, int *err)
{
	void __iomem *ptr;

	if (!request_mem_region(res->start, resource_size(res), name)) {
		*err = -EBUSY;
		return NULL;
	}

	ptr = ioremap(res->start, size);
	if (!ptr) {
		release_mem_region(res->start, resource_size(res));
		*err = -ENOMEM;
	}
	return ptr;
}

static int gpio_nand_probe(struct platform_device *dev)
{
	struct gpiomtd *gpiomtd;
	struct nand_chip *this;
	struct resource *res0, *res1;
	struct mtd_part_parser_data ppdata = {};
	int ret = 0;

	if (!dev->dev.of_node && !dev->dev.platform_data)
		return -EINVAL;

	res0 = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res0)
		return -EINVAL;

	gpiomtd = devm_kzalloc(&dev->dev, sizeof(*gpiomtd), GFP_KERNEL);
	if (gpiomtd == NULL) {
		dev_err(&dev->dev, "failed to create NAND MTD\n");
		return -ENOMEM;
	}

	this = &gpiomtd->nand_chip;
	this->IO_ADDR_R = request_and_remap(res0, 2, "NAND", &ret);
	if (!this->IO_ADDR_R) {
		dev_err(&dev->dev, "unable to map NAND\n");
		goto err_map;
	}

	res1 = gpio_nand_get_io_sync(dev);
	if (res1) {
		gpiomtd->io_sync = request_and_remap(res1, 4, "NAND sync", &ret);
		if (!gpiomtd->io_sync) {
			dev_err(&dev->dev, "unable to map sync NAND\n");
			goto err_sync;
		}
	}

	ret = gpio_nand_get_config(&dev->dev, &gpiomtd->plat);
	if (ret)
		goto err_nce;

	ret = gpio_request(gpiomtd->plat.gpio_nce, "NAND NCE");
	if (ret)
		goto err_nce;
	gpio_direction_output(gpiomtd->plat.gpio_nce, 1);
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp)) {
		ret = gpio_request(gpiomtd->plat.gpio_nwp, "NAND NWP");
		if (ret)
			goto err_nwp;
		gpio_direction_output(gpiomtd->plat.gpio_nwp, 1);
	}
	ret = gpio_request(gpiomtd->plat.gpio_ale, "NAND ALE");
	if (ret)
		goto err_ale;
	gpio_direction_output(gpiomtd->plat.gpio_ale, 0);
	ret = gpio_request(gpiomtd->plat.gpio_cle, "NAND CLE");
	if (ret)
		goto err_cle;
	gpio_direction_output(gpiomtd->plat.gpio_cle, 0);
	if (gpio_is_valid(gpiomtd->plat.gpio_rdy)) {
		ret = gpio_request(gpiomtd->plat.gpio_rdy, "NAND RDY");
		if (ret)
			goto err_rdy;
		gpio_direction_input(gpiomtd->plat.gpio_rdy);
	}


	this->IO_ADDR_W  = this->IO_ADDR_R;
	this->ecc.mode   = NAND_ECC_SOFT;
	this->options    = gpiomtd->plat.options;
	this->chip_delay = gpiomtd->plat.chip_delay;

	/* install our routines */
	this->cmd_ctrl   = gpio_nand_cmd_ctrl;
	this->dev_ready  = gpio_nand_devready;

	if (this->options & NAND_BUSWIDTH_16) {
		this->read_buf   = gpio_nand_readbuf16;
		this->write_buf  = gpio_nand_writebuf16;
	} else {
		this->read_buf   = gpio_nand_readbuf;
		this->write_buf  = gpio_nand_writebuf;
	}

	/* set the mtd private data for the nand driver */
	gpiomtd->mtd_info.priv = this;
	gpiomtd->mtd_info.owner = THIS_MODULE;

	if (nand_scan(&gpiomtd->mtd_info, 1)) {
		dev_err(&dev->dev, "no nand chips found?\n");
		ret = -ENXIO;
		goto err_wp;
	}

	if (gpiomtd->plat.adjust_parts)
		gpiomtd->plat.adjust_parts(&gpiomtd->plat,
					   gpiomtd->mtd_info.size);

	ppdata.of_node = dev->dev.of_node;
	ret = mtd_device_parse_register(&gpiomtd->mtd_info, NULL, &ppdata,
					gpiomtd->plat.parts,
					gpiomtd->plat.num_parts);
	if (ret)
		goto err_wp;
	platform_set_drvdata(dev, gpiomtd);

	return 0;

err_wp:
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_set_value(gpiomtd->plat.gpio_nwp, 0);
	if (gpio_is_valid(gpiomtd->plat.gpio_rdy))
		gpio_free(gpiomtd->plat.gpio_rdy);
err_rdy:
	gpio_free(gpiomtd->plat.gpio_cle);
err_cle:
	gpio_free(gpiomtd->plat.gpio_ale);
err_ale:
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_free(gpiomtd->plat.gpio_nwp);
err_nwp:
	gpio_free(gpiomtd->plat.gpio_nce);
err_nce:
	iounmap(gpiomtd->io_sync);
	if (res1)
		release_mem_region(res1->start, resource_size(res1));
err_sync:
	iounmap(gpiomtd->nand_chip.IO_ADDR_R);
	release_mem_region(res0->start, resource_size(res0));
err_map:
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
