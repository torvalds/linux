/*
 * ST Random Number Generator Driver ST's Platforms
 *
 * Author: Pankaj Dev: <pankaj.dev@st.com>
 *         Lee Jones <lee.jones@linaro.org>
 *
 * Copyright (C) 2015 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Registers */
#define ST_RNG_STATUS_REG		0x20
#define ST_RNG_DATA_REG			0x24

/* Registers fields */
#define ST_RNG_STATUS_BAD_SEQUENCE	BIT(0)
#define ST_RNG_STATUS_BAD_ALTERNANCE	BIT(1)
#define ST_RNG_STATUS_FIFO_FULL		BIT(5)

#define ST_RNG_SAMPLE_SIZE		2 /* 2 Byte (16bit) samples */
#define ST_RNG_FIFO_DEPTH		4
#define ST_RNG_FIFO_SIZE		(ST_RNG_FIFO_DEPTH * ST_RNG_SAMPLE_SIZE)

/*
 * Samples are documented to be available every 0.667us, so in theory
 * the 4 sample deep FIFO should take 2.668us to fill.  However, during
 * thorough testing, it became apparent that filling the FIFO actually
 * takes closer to 12us.  We then multiply by 2 in order to account for
 * the lack of udelay()'s reliability, suggested by Russell King.
 */
#define ST_RNG_FILL_FIFO_TIMEOUT	(12 * 2)

struct st_rng_data {
	void __iomem	*base;
	struct clk	*clk;
	struct hwrng	ops;
};

static int st_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct st_rng_data *ddata = (struct st_rng_data *)rng->priv;
	u32 status;
	int i;

	if (max < sizeof(u16))
		return -EINVAL;

	/* Wait until FIFO is full - max 4uS*/
	for (i = 0; i < ST_RNG_FILL_FIFO_TIMEOUT; i++) {
		status = readl_relaxed(ddata->base + ST_RNG_STATUS_REG);
		if (status & ST_RNG_STATUS_FIFO_FULL)
			break;
		udelay(1);
	}

	if (i == ST_RNG_FILL_FIFO_TIMEOUT)
		return 0;

	for (i = 0; i < ST_RNG_FIFO_SIZE && i < max; i += 2)
		*(u16 *)(data + i) =
			readl_relaxed(ddata->base + ST_RNG_DATA_REG);

	return i;	/* No of bytes read */
}

static int st_rng_probe(struct platform_device *pdev)
{
	struct st_rng_data *ddata;
	struct resource *res;
	struct clk *clk;
	void __iomem *base;
	int ret;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	ddata->ops.priv	= (unsigned long)ddata;
	ddata->ops.read	= st_rng_read;
	ddata->ops.name	= pdev->name;
	ddata->base	= base;
	ddata->clk	= clk;

	dev_set_drvdata(&pdev->dev, ddata);

	ret = hwrng_register(&ddata->ops);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register HW RNG\n");
		return ret;
	}

	dev_info(&pdev->dev, "Successfully registered HW RNG\n");

	return 0;
}

static int st_rng_remove(struct platform_device *pdev)
{
	struct st_rng_data *ddata = dev_get_drvdata(&pdev->dev);

	hwrng_unregister(&ddata->ops);

	clk_disable_unprepare(ddata->clk);

	return 0;
}

static const struct of_device_id st_rng_match[] = {
	{ .compatible = "st,rng" },
	{},
};
MODULE_DEVICE_TABLE(of, st_rng_match);

static struct platform_driver st_rng_driver = {
	.driver = {
		.name = "st-hwrandom",
		.of_match_table = of_match_ptr(st_rng_match),
	},
	.probe = st_rng_probe,
	.remove = st_rng_remove
};

module_platform_driver(st_rng_driver);

MODULE_AUTHOR("Pankaj Dev <pankaj.dev@st.com>");
MODULE_LICENSE("GPL v2");
