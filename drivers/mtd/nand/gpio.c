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

	writesb(this->IO_ADDR_W, buf, len);
}

static void gpio_nand_readbuf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;

	readsb(this->IO_ADDR_R, buf, len);
}

static int gpio_nand_verifybuf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;
	unsigned char read, *p = (unsigned char *) buf;
	int i, err = 0;

	for (i = 0; i < len; i++) {
		read = readb(this->IO_ADDR_R);
		if (read != p[i]) {
			pr_debug("%s: err at %d (read %04x vs %04x)\n",
			       __func__, i, read, p[i]);
			err = -EFAULT;
		}
	}
	return err;
}

static void gpio_nand_writebuf16(struct mtd_info *mtd, const u_char *buf,
				 int len)
{
	struct nand_chip *this = mtd->priv;

	if (IS_ALIGNED((unsigned long)buf, 2)) {
		writesw(this->IO_ADDR_W, buf, len>>1);
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
		readsw(this->IO_ADDR_R, buf, len>>1);
	} else {
		int i;
		unsigned short *ptr = (unsigned short *)buf;

		for (i = 0; i < len; i += 2, ptr++)
			*ptr = readw(this->IO_ADDR_R);
	}
}

static int gpio_nand_verifybuf16(struct mtd_info *mtd, const u_char *buf,
				 int len)
{
	struct nand_chip *this = mtd->priv;
	unsigned short read, *p = (unsigned short *) buf;
	int i, err = 0;
	len >>= 1;

	for (i = 0; i < len; i++) {
		read = readw(this->IO_ADDR_R);
		if (read != p[i]) {
			pr_debug("%s: err at %d (read %04x vs %04x)\n",
			       __func__, i, read, p[i]);
			err = -EFAULT;
		}
	}
	return err;
}


static int gpio_nand_devready(struct mtd_info *mtd)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(mtd);
	return gpio_get_value(gpiomtd->plat.gpio_rdy);
}

static int __devexit gpio_nand_remove(struct platform_device *dev)
{
	struct gpiomtd *gpiomtd = platform_get_drvdata(dev);
	struct resource *res;

	nand_release(&gpiomtd->mtd_info);

	res = platform_get_resource(dev, IORESOURCE_MEM, 1);
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
	gpio_free(gpiomtd->plat.gpio_rdy);

	kfree(gpiomtd);

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

static int __devinit gpio_nand_probe(struct platform_device *dev)
{
	struct gpiomtd *gpiomtd;
	struct nand_chip *this;
	struct resource *res0, *res1;
	int ret;

	if (!dev->dev.platform_data)
		return -EINVAL;

	res0 = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res0)
		return -EINVAL;

	gpiomtd = kzalloc(sizeof(*gpiomtd), GFP_KERNEL);
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

	res1 = platform_get_resource(dev, IORESOURCE_MEM, 1);
	if (res1) {
		gpiomtd->io_sync = request_and_remap(res1, 4, "NAND sync", &ret);
		if (!gpiomtd->io_sync) {
			dev_err(&dev->dev, "unable to map sync NAND\n");
			goto err_sync;
		}
	}

	memcpy(&gpiomtd->plat, dev->dev.platform_data, sizeof(gpiomtd->plat));

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
	ret = gpio_request(gpiomtd->plat.gpio_rdy, "NAND RDY");
	if (ret)
		goto err_rdy;
	gpio_direction_input(gpiomtd->plat.gpio_rdy);


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
		this->verify_buf = gpio_nand_verifybuf16;
	} else {
		this->read_buf   = gpio_nand_readbuf;
		this->write_buf  = gpio_nand_writebuf;
		this->verify_buf = gpio_nand_verifybuf;
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

	add_mtd_partitions(&gpiomtd->mtd_info, gpiomtd->plat.parts,
			   gpiomtd->plat.num_parts);
	platform_set_drvdata(dev, gpiomtd);

	return 0;

err_wp:
	if (gpio_is_valid(gpiomtd->plat.gpio_nwp))
		gpio_set_value(gpiomtd->plat.gpio_nwp, 0);
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
	kfree(gpiomtd);
	return ret;
}

static struct platform_driver gpio_nand_driver = {
	.probe		= gpio_nand_probe,
	.remove		= gpio_nand_remove,
	.driver		= {
		.name	= "gpio-nand",
	},
};

static int __init gpio_nand_init(void)
{
	printk(KERN_INFO "GPIO NAND driver, © 2004 Simtec Electronics\n");

	return platform_driver_register(&gpio_nand_driver);
}

static void __exit gpio_nand_exit(void)
{
	platform_driver_unregister(&gpio_nand_driver);
}

module_init(gpio_nand_init);
module_exit(gpio_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("GPIO NAND Driver");
