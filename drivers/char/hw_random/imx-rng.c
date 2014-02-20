/*
 * RNG driver for Freescale RNG B/C
 *
 * Copyright (C) 2008-2015 Freescale Semiconductor, Inc.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * Hardware driver for the Intel/AMD/VIA Random Number Generators (RNG)
 * (c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>
 *
 * derived from
 *
 * Hardware driver for the AMD 768 Random Number Generator (RNG)
 * (c) Copyright 2001 Red Hat Inc <alan@redhat.com>
 *
 * derived from
 *
 * Hardware driver for Intel i810 Random Number Generator (RNG)
 * Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/slab.h>

#define MODULE_NAME "imx-rng"

#define RNGC_VERSION_MAJOR3 3

#define RNGC_VERSION_ID				0x0000
#define RNGC_COMMAND				0x0004
#define RNGC_CONTROL				0x0008
#define RNGC_STATUS				0x000C
#define RNGC_ERROR				0x0010
#define RNGC_FIFO				0x0014
#define RNGC_VERIF_CTRL				0x0020
#define RNGC_OSC_CTRL_COUNT			0x0028
#define RNGC_OSC_COUNT				0x002C
#define RNGC_OSC_COUNT_STATUS			0x0030

#define RNGC_VERID_ZEROS_MASK			0x0f000000
#define RNGC_VERID_RNG_TYPE_MASK		0xf0000000
#define RNGC_VERID_RNG_TYPE_SHIFT		28
#define RNGC_VERID_CHIP_VERSION_MASK		0x00ff0000
#define RNGC_VERID_CHIP_VERSION_SHIFT		16
#define RNGC_VERID_VERSION_MAJOR_MASK		0x0000ff00
#define RNGC_VERID_VERSION_MAJOR_SHIFT		8
#define RNGC_VERID_VERSION_MINOR_MASK		0x000000ff
#define RNGC_VERID_VERSION_MINOR_SHIFT		0

#define RNGC_CMD_ZEROS_MASK			0xffffff8c
#define RNGC_CMD_SW_RST				0x00000040
#define RNGC_CMD_CLR_ERR			0x00000020
#define RNGC_CMD_CLR_INT			0x00000010
#define RNGC_CMD_SEED				0x00000002
#define RNGC_CMD_SELF_TEST			0x00000001

#define RNGC_CTRL_ZEROS_MASK			0xfffffc8c
#define RNGC_CTRL_CTL_ACC			0x00000200
#define RNGC_CTRL_VERIF_MODE			0x00000100
#define RNGC_CTRL_MASK_ERROR			0x00000040

#define RNGC_CTRL_MASK_DONE			0x00000020
#define RNGC_CTRL_AUTO_SEED			0x00000010
#define RNGC_CTRL_FIFO_UFLOW_MASK		0x00000003
#define RNGC_CTRL_FIFO_UFLOW_SHIFT		0

#define RNGC_CTRL_FIFO_UFLOW_ZEROS_ERROR	0
#define RNGC_CTRL_FIFO_UFLOW_ZEROS_ERROR2	1
#define RNGC_CTRL_FIFO_UFLOW_BUS_XFR		2
#define RNGC_CTRL_FIFO_UFLOW_ZEROS_INTR		3

#define RNGC_STATUS_ST_PF_MASK			0x00c00000
#define RNGC_STATUS_ST_PF_SHIFT			22
#define RNGC_STATUS_ST_PF_TRNG			0x00800000
#define RNGC_STATUS_ST_PF_PRNG			0x00400000
#define RNGC_STATUS_ERROR			0x00010000
#define RNGC_STATUS_FIFO_SIZE_MASK		0x0000f000
#define RNGC_STATUS_FIFO_SIZE_SHIFT		12
#define RNGC_STATUS_FIFO_LEVEL_MASK		0x00000f00
#define RNGC_STATUS_FIFO_LEVEL_SHIFT		8
#define RNGC_STATUS_NEXT_SEED_DONE		0x00000040
#define RNGC_STATUS_SEED_DONE			0x00000020
#define RNGC_STATUS_ST_DONE			0x00000010
#define RNGC_STATUS_RESEED			0x00000008
#define RNGC_STATUS_SLEEP			0x00000004
#define RNGC_STATUS_BUSY			0x00000002
#define RNGC_STATUS_SEC_STATE			0x00000001

#define RNGC_ERROR_STATUS_ZEROS_MASK		0xffffffc0
#define RNGC_ERROR_STATUS_BAD_KEY		0x00000040
#define RNGC_ERROR_STATUS_RAND_ERR		0x00000020
#define RNGC_ERROR_STATUS_FIFO_ERR		0x00000010
#define RNGC_ERROR_STATUS_STAT_ERR		0x00000008
#define RNGC_ERROR_STATUS_ST_ERR		0x00000004
#define RNGC_ERROR_STATUS_OSC_ERR		0x00000002
#define RNGC_ERROR_STATUS_LFSR_ERR		0x00000001

#define RNG_ADDR_RANGE				0x34

static DECLARE_COMPLETION(rng_self_testing);
static DECLARE_COMPLETION(rng_seed_done);

static struct platform_device *imx_rng_dev;

struct imx_rng_priv_data {
	void __iomem *reg_base;
};

int irq_rng;

static int imx_rng_data_present(struct hwrng *rng, int wait)
{
	int level;
	struct imx_rng_priv_data *prv = (struct imx_rng_priv_data *)rng->priv;

	/* how many random numbers are in FIFO? [0-16] */
	level = (readl(prv->reg_base + RNGC_STATUS) &
		 RNGC_STATUS_FIFO_LEVEL_MASK) >> RNGC_STATUS_FIFO_LEVEL_SHIFT;

	return level > 0 ? 1 : 0;
}

static int imx_rng_data_read(struct hwrng *rng, u32 *data)
{
	int err;
	struct imx_rng_priv_data *prv = (struct imx_rng_priv_data *)rng->priv;

	/* retrieve a random number from FIFO */
	*data = readl(prv->reg_base + RNGC_FIFO);

	/* is there some error while reading this random number? */
	err = readl(prv->reg_base + RNGC_STATUS) & RNGC_STATUS_ERROR;

	/* if error happened doesn't return random number */
	return err ? 0 : 4;
}

static irqreturn_t imx_rng_irq(int irq, void *dev)
{
	int handled = 0;
	struct imx_rng_priv_data *prv = (struct imx_rng_priv_data *)dev;

	/* is the seed creation done? */
	if (readl(prv->reg_base + RNGC_STATUS) & RNGC_STATUS_SEED_DONE) {
		complete(&rng_seed_done);
		writel(RNGC_CMD_CLR_INT | RNGC_CMD_CLR_ERR,
		       prv->reg_base + RNGC_COMMAND);
		handled = 1;
	}

	/* is the self test done? */
	if (readl(prv->reg_base + RNGC_STATUS) & RNGC_STATUS_ST_DONE) {
		complete(&rng_self_testing);
		writel(RNGC_CMD_CLR_INT | RNGC_CMD_CLR_ERR,
		       prv->reg_base + RNGC_COMMAND);
		handled = 1;
	}

	/* is there any error? */
	if (readl(prv->reg_base + RNGC_STATUS) & RNGC_STATUS_ERROR) {
		/* clear interrupt */
		writel(RNGC_CMD_CLR_INT | RNGC_CMD_CLR_ERR,
		       prv->reg_base + RNGC_COMMAND);
		handled = 1;
	}

	return handled;
}

static int imx_rng_init(struct hwrng *rng)
{
	int err;
	struct imx_rng_priv_data *prv = (struct imx_rng_priv_data *)rng->priv;
	u32 cmd, ctrl, osc;

	reinit_completion(&rng_self_testing);
	reinit_completion(&rng_seed_done);

	err = readl(prv->reg_base + RNGC_STATUS) & RNGC_STATUS_ERROR;
	if (err) {
		/* is this a bad keys error ? */
		if (readl(prv->reg_base + RNGC_ERROR) &
		    RNGC_ERROR_STATUS_BAD_KEY) {
			dev_err(&imx_rng_dev->dev, "Can't start, Bad Keys.\n");
			return -EIO;
		}
	}

	/* mask all interrupts, will be unmasked soon */
	ctrl = readl(prv->reg_base + RNGC_CONTROL);
	writel(ctrl | RNGC_CTRL_MASK_DONE | RNGC_CTRL_MASK_ERROR,
	       prv->reg_base + RNGC_CONTROL);

	/* verify if oscillator is working */
	osc = readl(prv->reg_base + RNGC_ERROR);
	if (osc & RNGC_ERROR_STATUS_OSC_ERR) {
		dev_err(&imx_rng_dev->dev, "RNGC Oscillator is dead!\n");
		return -EIO;
	}

	err = request_irq(irq_rng, imx_rng_irq,
			  0, "imx-rng", (void *)rng->priv);
	if (err) {
		dev_err(&imx_rng_dev->dev, "Can't get interrupt working.\n");
		return -EIO;
	}

	/* do self test, repeat until get success */
	do {
		/* clear error */
		cmd = readl(prv->reg_base + RNGC_COMMAND);
		writel(cmd | RNGC_CMD_CLR_ERR, prv->reg_base + RNGC_COMMAND);

		/* unmask all interrupt */
		ctrl = readl(prv->reg_base + RNGC_CONTROL);
		writel(ctrl & ~(RNGC_CTRL_MASK_DONE | RNGC_CTRL_MASK_ERROR),
		       prv->reg_base + RNGC_CONTROL);

		/* run self test */
		cmd = readl(prv->reg_base + RNGC_COMMAND);
		writel(cmd | RNGC_CMD_SELF_TEST,
		       prv->reg_base + RNGC_COMMAND);

		wait_for_completion(&rng_self_testing);

	} while (readl(prv->reg_base + RNGC_ERROR) &
		 RNGC_ERROR_STATUS_ST_ERR);

	/* clear interrupt. Is it really necessary here? */
	writel(RNGC_CMD_CLR_INT | RNGC_CMD_CLR_ERR,
	       prv->reg_base + RNGC_COMMAND);

	/* create seed, repeat while there is some statistical error */
	do {
		/* clear error */
		cmd = readl(prv->reg_base + RNGC_COMMAND);
		writel(cmd | RNGC_CMD_CLR_ERR, prv->reg_base + RNGC_COMMAND);

		/* seed creation */
		cmd = readl(prv->reg_base + RNGC_COMMAND);
		writel(cmd | RNGC_CMD_SEED, prv->reg_base + RNGC_COMMAND);

		wait_for_completion(&rng_seed_done);

	} while (readl(prv->reg_base + RNGC_ERROR) &
		 RNGC_ERROR_STATUS_STAT_ERR);

	err = readl(prv->reg_base + RNGC_ERROR) &
	      (RNGC_ERROR_STATUS_STAT_ERR |
	       RNGC_ERROR_STATUS_RAND_ERR |
	       RNGC_ERROR_STATUS_FIFO_ERR |
	       RNGC_ERROR_STATUS_ST_ERR |
	       RNGC_ERROR_STATUS_OSC_ERR |
	       RNGC_ERROR_STATUS_LFSR_ERR);

	if (err) {
		dev_err(&imx_rng_dev->dev, "iMX RNG appears inoperable.\n");
		return -EIO;
	}

	return 0;
}

static struct hwrng imx_rng = {
	.name = "imx-rng",
	.init = imx_rng_init,
	.data_present = imx_rng_data_present,
	.data_read = imx_rng_data_read
};

static int __init imx_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk;
	struct imx_rng_priv_data *priv;
	int err = -ENODEV;

	if (imx_rng_dev)
		return -EBUSY;

	/* Enable the clock */
	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		dev_err(dev, "Can not get clock.\n");
		return PTR_ERR(clk);
	}
	clk_enable(clk);

	/* Allocate private data memory */
	priv = kzalloc(sizeof(struct imx_rng_priv_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	imx_rng.priv = (unsigned long)priv;
	dev_set_drvdata(dev, priv);

	/* ioremap that register space */
	priv->reg_base = of_iomap(np, 0);
	if (!priv->reg_base) {
		kfree(priv);
		dev_err(dev, "Failed to remap register space.\n");
		return -ENODEV;
	}

	irq_rng = platform_get_irq(pdev, 0);

	err = hwrng_register(&imx_rng);
	if (err) {
		iounmap(priv->reg_base);
		kfree(priv);
		dev_err(dev, "failed to register hwrng (%d)\n", err);
		return err;
	}

	imx_rng_dev = pdev;
	dev_info(dev, "iMX RNG Registered.\n");

	return 0;
}

static int __exit imx_rng_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk;
	struct imx_rng_priv_data *priv = dev_get_drvdata(dev);

	/* Disable the clock */
	clk = of_clk_get(np, 0);

	if (IS_ERR(clk))
		dev_err(dev, "Can not get clock.\n");
	else
		clk_disable(clk);

	hwrng_unregister(&imx_rng);

	iounmap(priv->reg_base);

	kfree(priv);

	return 0;
}

static int imx_rng_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef CONFIG_PM
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk = of_clk_get(np, 0);

	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Can not get rng_clk\n");
		return PTR_ERR(clk);
	}

	clk_disable(clk);
#endif

	return 0;
}

static int imx_rng_resume(struct platform_device *pdev)
{
#ifdef CONFIG_PM
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk *clk = of_clk_get(np, 0);

	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Can not get rng_clk\n");
		return PTR_ERR(clk);
	}

	clk_enable(clk);
#endif

	return 0;
}

static struct of_device_id imx_rng_dt_ids[] = {
	{ .compatible = "imx-rng",},
	{ .compatible = "fsl,imx-rng",},
	{ .compatible = "fsl,imx6sl-rng",},
	{ },
};

MODULE_DEVICE_TABLE(of, imx_rng_dt_ids);

static struct platform_driver imx_rng_driver = {
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = imx_rng_dt_ids,
	},
	.remove = __exit_p(imx_rng_remove),
	.suspend = imx_rng_suspend,
	.resume = imx_rng_resume,
};

static int __init mod_init(void)
{
	return platform_driver_probe(&imx_rng_driver, imx_rng_probe);
}

static void __exit mod_exit(void)
{
	platform_driver_unregister(&imx_rng_driver);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("H/W RNG(B/C) driver for i.MX");
MODULE_LICENSE("GPL");
