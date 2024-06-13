// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nomadik RNG support
 *  Copyright 2009 Alessandro Rubini
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

static int nmk_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	void __iomem *base = (void __iomem *)rng->priv;

	/*
	 * The register is 32 bits and gives 16 random bits (low half).
	 * A subsequent read will delay the core for 400ns, so we just read
	 * once and accept the very unlikely very small delay, even if wait==0.
	 */
	*(u16 *)data = __raw_readl(base + 8) & 0xffff;
	return 2;
}

/* we have at most one RNG per machine, granted */
static struct hwrng nmk_rng = {
	.name		= "nomadik",
	.read		= nmk_rng_read,
};

static int nmk_rng_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct clk *rng_clk;
	void __iomem *base;
	int ret;

	rng_clk = devm_clk_get_enabled(&dev->dev, NULL);
	if (IS_ERR(rng_clk)) {
		dev_err(&dev->dev, "could not get rng clock\n");
		ret = PTR_ERR(rng_clk);
		return ret;
	}

	ret = amba_request_regions(dev, dev->dev.init_name);
	if (ret)
		return ret;
	ret = -ENOMEM;
	base = devm_ioremap(&dev->dev, dev->res.start,
			    resource_size(&dev->res));
	if (!base)
		goto out_release;
	nmk_rng.priv = (unsigned long)base;
	ret = devm_hwrng_register(&dev->dev, &nmk_rng);
	if (ret)
		goto out_release;
	return 0;

out_release:
	amba_release_regions(dev);
	return ret;
}

static void nmk_rng_remove(struct amba_device *dev)
{
	amba_release_regions(dev);
}

static const struct amba_id nmk_rng_ids[] = {
	{
		.id	= 0x000805e1,
		.mask	= 0x000fffff, /* top bits are rev and cfg: accept all */
	},
	{0, 0},
};

MODULE_DEVICE_TABLE(amba, nmk_rng_ids);

static struct amba_driver nmk_rng_driver = {
	.drv = {
		.owner = THIS_MODULE,
		.name = "rng",
		},
	.probe = nmk_rng_probe,
	.remove = nmk_rng_remove,
	.id_table = nmk_rng_ids,
};

module_amba_driver(nmk_rng_driver);

MODULE_LICENSE("GPL");
