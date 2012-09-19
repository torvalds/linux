/*
 * RNG driver for Freescale RNGA
 *
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Author: Alan Carvalho de Assis <acassis@gmail.com>
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * This driver is based on other RNG drivers.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/io.h>

/* RNGA Registers */
#define RNGA_CONTROL			0x00
#define RNGA_STATUS			0x04
#define RNGA_ENTROPY			0x08
#define RNGA_OUTPUT_FIFO		0x0c
#define RNGA_MODE			0x10
#define RNGA_VERIFICATION_CONTROL	0x14
#define RNGA_OSC_CONTROL_COUNTER	0x18
#define RNGA_OSC1_COUNTER		0x1c
#define RNGA_OSC2_COUNTER		0x20
#define RNGA_OSC_COUNTER_STATUS		0x24

/* RNGA Registers Range */
#define RNG_ADDR_RANGE			0x28

/* RNGA Control Register */
#define RNGA_CONTROL_SLEEP		0x00000010
#define RNGA_CONTROL_CLEAR_INT		0x00000008
#define RNGA_CONTROL_MASK_INTS		0x00000004
#define RNGA_CONTROL_HIGH_ASSURANCE	0x00000002
#define RNGA_CONTROL_GO			0x00000001

#define RNGA_STATUS_LEVEL_MASK		0x0000ff00

/* RNGA Status Register */
#define RNGA_STATUS_OSC_DEAD		0x80000000
#define RNGA_STATUS_SLEEP		0x00000010
#define RNGA_STATUS_ERROR_INT		0x00000008
#define RNGA_STATUS_FIFO_UNDERFLOW	0x00000004
#define RNGA_STATUS_LAST_READ_STATUS	0x00000002
#define RNGA_STATUS_SECURITY_VIOLATION	0x00000001

static struct platform_device *rng_dev;

static int mxc_rnga_data_present(struct hwrng *rng, int wait)
{
	void __iomem *rng_base = (void __iomem *)rng->priv;
	int i;

	for (i = 0; i < 20; i++) {
		/* how many random numbers are in FIFO? [0-16] */
		int level = (__raw_readl(rng_base + RNGA_STATUS) &
				RNGA_STATUS_LEVEL_MASK) >> 8;
		if (level || !wait)
			return !!level;
		udelay(10);
	}
	return 0;
}

static int mxc_rnga_data_read(struct hwrng *rng, u32 * data)
{
	int err;
	u32 ctrl;
	void __iomem *rng_base = (void __iomem *)rng->priv;

	/* retrieve a random number from FIFO */
	*data = __raw_readl(rng_base + RNGA_OUTPUT_FIFO);

	/* some error while reading this random number? */
	err = __raw_readl(rng_base + RNGA_STATUS) & RNGA_STATUS_ERROR_INT;

	/* if error: clear error interrupt, but doesn't return random number */
	if (err) {
		dev_dbg(&rng_dev->dev, "Error while reading random number!\n");
		ctrl = __raw_readl(rng_base + RNGA_CONTROL);
		__raw_writel(ctrl | RNGA_CONTROL_CLEAR_INT,
					rng_base + RNGA_CONTROL);
		return 0;
	} else
		return 4;
}

static int mxc_rnga_init(struct hwrng *rng)
{
	u32 ctrl, osc;
	void __iomem *rng_base = (void __iomem *)rng->priv;

	/* wake up */
	ctrl = __raw_readl(rng_base + RNGA_CONTROL);
	__raw_writel(ctrl & ~RNGA_CONTROL_SLEEP, rng_base + RNGA_CONTROL);

	/* verify if oscillator is working */
	osc = __raw_readl(rng_base + RNGA_STATUS);
	if (osc & RNGA_STATUS_OSC_DEAD) {
		dev_err(&rng_dev->dev, "RNGA Oscillator is dead!\n");
		return -ENODEV;
	}

	/* go running */
	ctrl = __raw_readl(rng_base + RNGA_CONTROL);
	__raw_writel(ctrl | RNGA_CONTROL_GO, rng_base + RNGA_CONTROL);

	return 0;
}

static void mxc_rnga_cleanup(struct hwrng *rng)
{
	u32 ctrl;
	void __iomem *rng_base = (void __iomem *)rng->priv;

	ctrl = __raw_readl(rng_base + RNGA_CONTROL);

	/* stop rnga */
	__raw_writel(ctrl & ~RNGA_CONTROL_GO, rng_base + RNGA_CONTROL);
}

static struct hwrng mxc_rnga = {
	.name = "mxc-rnga",
	.init = mxc_rnga_init,
	.cleanup = mxc_rnga_cleanup,
	.data_present = mxc_rnga_data_present,
	.data_read = mxc_rnga_data_read
};

static int __init mxc_rnga_probe(struct platform_device *pdev)
{
	int err = -ENODEV;
	struct clk *clk;
	struct resource *res, *mem;
	void __iomem *rng_base = NULL;

	if (rng_dev)
		return -EBUSY;

	clk = clk_get(&pdev->dev, "rng");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Could not get rng_clk!\n");
		err = PTR_ERR(clk);
		goto out;
	}

	clk_enable(clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENOENT;
		goto err_region;
	}

	mem = request_mem_region(res->start, resource_size(res), pdev->name);
	if (mem == NULL) {
		err = -EBUSY;
		goto err_region;
	}

	rng_base = ioremap(res->start, resource_size(res));
	if (!rng_base) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	mxc_rnga.priv = (unsigned long)rng_base;

	err = hwrng_register(&mxc_rnga);
	if (err) {
		dev_err(&pdev->dev, "MXC RNGA registering failed (%d)\n", err);
		goto err_register;
	}

	rng_dev = pdev;

	dev_info(&pdev->dev, "MXC RNGA Registered.\n");

	return 0;

err_register:
	iounmap(rng_base);
	rng_base = NULL;

err_ioremap:
	release_mem_region(res->start, resource_size(res));

err_region:
	clk_disable(clk);
	clk_put(clk);

out:
	return err;
}

static int __exit mxc_rnga_remove(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	void __iomem *rng_base = (void __iomem *)mxc_rnga.priv;
	struct clk *clk = clk_get(&pdev->dev, "rng");

	hwrng_unregister(&mxc_rnga);

	iounmap(rng_base);

	release_mem_region(res->start, resource_size(res));

	clk_disable(clk);
	clk_put(clk);

	return 0;
}

static struct platform_driver mxc_rnga_driver = {
	.driver = {
		   .name = "mxc_rnga",
		   .owner = THIS_MODULE,
		   },
	.remove = __exit_p(mxc_rnga_remove),
};

static int __init mod_init(void)
{
	return platform_driver_probe(&mxc_rnga_driver, mxc_rnga_probe);
}

static void __exit mod_exit(void)
{
	platform_driver_unregister(&mxc_rnga_driver);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("H/W RNGA driver for i.MX");
MODULE_LICENSE("GPL");
