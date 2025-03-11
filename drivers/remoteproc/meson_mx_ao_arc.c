// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#include "remoteproc_internal.h"

#define AO_REMAP_REG0						0x0
#define AO_REMAP_REG0_REMAP_AHB_SRAM_BITS_17_14_FOR_ARM_CPU	GENMASK(3, 0)

#define AO_REMAP_REG1						0x4
#define AO_REMAP_REG1_MOVE_AHB_SRAM_TO_0X0_INSTEAD_OF_DDR	BIT(4)
#define AO_REMAP_REG1_REMAP_AHB_SRAM_BITS_17_14_FOR_MEDIA_CPU	GENMASK(3, 0)

#define AO_CPU_CNTL						0x0
#define AO_CPU_CNTL_AHB_SRAM_BITS_31_20				GENMASK(28, 16)
#define AO_CPU_CNTL_HALT					BIT(9)
#define AO_CPU_CNTL_UNKNONWN					BIT(8)
#define AO_CPU_CNTL_RUN						BIT(0)

#define AO_CPU_STAT						0x4

#define AO_SECURE_REG0						0x0
#define AO_SECURE_REG0_AHB_SRAM_BITS_19_12			GENMASK(15, 8)

/* Only bits [31:20] and [17:14] are usable, all other bits must be zero */
#define MESON_AO_RPROC_SRAM_USABLE_BITS				0xfff3c000ULL

#define MESON_AO_RPROC_MEMORY_OFFSET				0x10000000

struct meson_mx_ao_arc_rproc_priv {
	void __iomem		*remap_base;
	void __iomem		*cpu_base;
	unsigned long		sram_va;
	phys_addr_t		sram_pa;
	size_t			sram_size;
	struct gen_pool		*sram_pool;
	struct reset_control	*arc_reset;
	struct clk		*arc_pclk;
	struct regmap		*secbus2_regmap;
};

static int meson_mx_ao_arc_rproc_start(struct rproc *rproc)
{
	struct meson_mx_ao_arc_rproc_priv *priv = rproc->priv;
	phys_addr_t translated_sram_addr;
	u32 tmp;
	int ret;

	ret = clk_prepare_enable(priv->arc_pclk);
	if (ret)
		return ret;

	tmp = FIELD_PREP(AO_REMAP_REG0_REMAP_AHB_SRAM_BITS_17_14_FOR_ARM_CPU,
			 priv->sram_pa >> 14);
	writel(tmp, priv->remap_base + AO_REMAP_REG0);

	/*
	 * The SRAM content as seen by the ARC core always starts at 0x0
	 * regardless of the value given here (this was discovered by trial and
	 * error). For SoCs older than Meson6 we probably have to set
	 * AO_REMAP_REG1_MOVE_AHB_SRAM_TO_0X0_INSTEAD_OF_DDR to achieve the
	 * same. (At least) For Meson8 and newer that bit must not be set.
	 */
	writel(0x0, priv->remap_base + AO_REMAP_REG1);

	regmap_update_bits(priv->secbus2_regmap, AO_SECURE_REG0,
			   AO_SECURE_REG0_AHB_SRAM_BITS_19_12,
			   FIELD_PREP(AO_SECURE_REG0_AHB_SRAM_BITS_19_12,
				      priv->sram_pa >> 12));

	ret = reset_control_reset(priv->arc_reset);
	if (ret) {
		clk_disable_unprepare(priv->arc_pclk);
		return ret;
	}

	usleep_range(10, 100);

	/*
	 * Convert from 0xd9000000 to 0xc9000000 as the vendor driver does.
	 * This only seems to be relevant for the AO_CPU_CNTL register. It is
	 * unknown why this is needed.
	 */
	translated_sram_addr = priv->sram_pa - MESON_AO_RPROC_MEMORY_OFFSET;

	tmp = FIELD_PREP(AO_CPU_CNTL_AHB_SRAM_BITS_31_20,
			 translated_sram_addr >> 20);
	tmp |= AO_CPU_CNTL_UNKNONWN | AO_CPU_CNTL_RUN;
	writel(tmp, priv->cpu_base + AO_CPU_CNTL);

	usleep_range(20, 200);

	return 0;
}

static int meson_mx_ao_arc_rproc_stop(struct rproc *rproc)
{
	struct meson_mx_ao_arc_rproc_priv *priv = rproc->priv;

	writel(AO_CPU_CNTL_HALT, priv->cpu_base + AO_CPU_CNTL);

	clk_disable_unprepare(priv->arc_pclk);

	return 0;
}

static void *meson_mx_ao_arc_rproc_da_to_va(struct rproc *rproc, u64 da,
					    size_t len, bool *is_iomem)
{
	struct meson_mx_ao_arc_rproc_priv *priv = rproc->priv;

	/* The memory from the ARC core's perspective always starts at 0x0. */
	if ((da + len) > priv->sram_size)
		return NULL;

	return (void *)priv->sram_va + da;
}

static struct rproc_ops meson_mx_ao_arc_rproc_ops = {
	.start		= meson_mx_ao_arc_rproc_start,
	.stop		= meson_mx_ao_arc_rproc_stop,
	.da_to_va	= meson_mx_ao_arc_rproc_da_to_va,
	.get_boot_addr	= rproc_elf_get_boot_addr,
	.load		= rproc_elf_load_segments,
	.sanity_check	= rproc_elf_sanity_check,
};

static int meson_mx_ao_arc_rproc_probe(struct platform_device *pdev)
{
	struct meson_mx_ao_arc_rproc_priv *priv;
	struct device *dev = &pdev->dev;
	const char *fw_name = NULL;
	struct rproc *rproc;
	int ret;

	device_property_read_string(dev, "firmware-name", &fw_name);

	rproc = devm_rproc_alloc(dev, "meson-mx-ao-arc",
				 &meson_mx_ao_arc_rproc_ops, fw_name,
				 sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	rproc->has_iommu = false;
	priv = rproc->priv;

	priv->sram_pool = of_gen_pool_get(dev->of_node, "sram", 0);
	if (!priv->sram_pool) {
		dev_err(dev, "Could not get SRAM pool\n");
		return -ENODEV;
	}

	priv->sram_size = gen_pool_avail(priv->sram_pool);

	priv->sram_va = gen_pool_alloc(priv->sram_pool, priv->sram_size);
	if (!priv->sram_va) {
		dev_err(dev, "Could not alloc memory in SRAM pool\n");
		return -ENOMEM;
	}

	priv->sram_pa = gen_pool_virt_to_phys(priv->sram_pool, priv->sram_va);
	if (priv->sram_pa & ~MESON_AO_RPROC_SRAM_USABLE_BITS) {
		dev_err(dev, "SRAM address contains unusable bits\n");
		ret = -EINVAL;
		goto err_free_genpool;
	}

	priv->secbus2_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
							       "amlogic,secbus2");
	if (IS_ERR(priv->secbus2_regmap)) {
		dev_err(dev, "Failed to find SECBUS2 regmap\n");
		ret = PTR_ERR(priv->secbus2_regmap);
		goto err_free_genpool;
	}

	priv->remap_base = devm_platform_ioremap_resource_byname(pdev, "remap");
	if (IS_ERR(priv->remap_base)) {
		ret = PTR_ERR(priv->remap_base);
		goto err_free_genpool;
	}

	priv->cpu_base = devm_platform_ioremap_resource_byname(pdev, "cpu");
	if (IS_ERR(priv->cpu_base)) {
		ret = PTR_ERR(priv->cpu_base);
		goto err_free_genpool;
	}

	priv->arc_reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->arc_reset)) {
		dev_err(dev, "Failed to get ARC reset\n");
		ret = PTR_ERR(priv->arc_reset);
		goto err_free_genpool;
	}

	priv->arc_pclk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->arc_pclk)) {
		dev_err(dev, "Failed to get the ARC PCLK\n");
		ret = PTR_ERR(priv->arc_pclk);
		goto err_free_genpool;
	}

	platform_set_drvdata(pdev, rproc);

	ret = rproc_add(rproc);
	if (ret)
		goto err_free_genpool;

	return 0;

err_free_genpool:
	gen_pool_free(priv->sram_pool, priv->sram_va, priv->sram_size);
	return ret;
}

static void meson_mx_ao_arc_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct meson_mx_ao_arc_rproc_priv *priv = rproc->priv;

	rproc_del(rproc);
	gen_pool_free(priv->sram_pool, priv->sram_va, priv->sram_size);
}

static const struct of_device_id meson_mx_ao_arc_rproc_match[] = {
	{ .compatible = "amlogic,meson8-ao-arc" },
	{ .compatible = "amlogic,meson8b-ao-arc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_mx_ao_arc_rproc_match);

static struct platform_driver meson_mx_ao_arc_rproc_driver = {
	.probe = meson_mx_ao_arc_rproc_probe,
	.remove = meson_mx_ao_arc_rproc_remove,
	.driver = {
		.name = "meson-mx-ao-arc-rproc",
		.of_match_table = meson_mx_ao_arc_rproc_match,
	},
};
module_platform_driver(meson_mx_ao_arc_rproc_driver);

MODULE_DESCRIPTION("Amlogic Meson6/8/8b/8m2 AO ARC remote processor driver");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_LICENSE("GPL v2");
