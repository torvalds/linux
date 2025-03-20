// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive composable cache controller Driver
 *
 * Copyright (C) 2018-2022 SiFive, Inc.
 *
 */

#define pr_fmt(fmt) "CCACHE: " fmt

#include <linux/align.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/bitfield.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <asm/cacheflush.h>
#include <asm/cacheinfo.h>
#include <asm/dma-noncoherent.h>
#include <soc/sifive/sifive_ccache.h>

#define SIFIVE_CCACHE_DIRECCFIX_LOW 0x100
#define SIFIVE_CCACHE_DIRECCFIX_HIGH 0x104
#define SIFIVE_CCACHE_DIRECCFIX_COUNT 0x108

#define SIFIVE_CCACHE_DIRECCFAIL_LOW 0x120
#define SIFIVE_CCACHE_DIRECCFAIL_HIGH 0x124
#define SIFIVE_CCACHE_DIRECCFAIL_COUNT 0x128

#define SIFIVE_CCACHE_DATECCFIX_LOW 0x140
#define SIFIVE_CCACHE_DATECCFIX_HIGH 0x144
#define SIFIVE_CCACHE_DATECCFIX_COUNT 0x148

#define SIFIVE_CCACHE_DATECCFAIL_LOW 0x160
#define SIFIVE_CCACHE_DATECCFAIL_HIGH 0x164
#define SIFIVE_CCACHE_DATECCFAIL_COUNT 0x168

#define SIFIVE_CCACHE_CONFIG 0x00
#define SIFIVE_CCACHE_CONFIG_BANK_MASK GENMASK_ULL(7, 0)
#define SIFIVE_CCACHE_CONFIG_WAYS_MASK GENMASK_ULL(15, 8)
#define SIFIVE_CCACHE_CONFIG_SETS_MASK GENMASK_ULL(23, 16)
#define SIFIVE_CCACHE_CONFIG_BLKS_MASK GENMASK_ULL(31, 24)

#define SIFIVE_CCACHE_FLUSH64 0x200
#define SIFIVE_CCACHE_FLUSH32 0x240

#define SIFIVE_CCACHE_WAYENABLE 0x08
#define SIFIVE_CCACHE_ECCINJECTERR 0x40

#define SIFIVE_CCACHE_MAX_ECCINTR 4
#define SIFIVE_CCACHE_LINE_SIZE 64

static void __iomem *ccache_base;
static int g_irq[SIFIVE_CCACHE_MAX_ECCINTR];
static struct riscv_cacheinfo_ops ccache_cache_ops;
static int level;

enum {
	DIR_CORR = 0,
	DATA_CORR,
	DATA_UNCORR,
	DIR_UNCORR,
};

enum {
	QUIRK_NONSTANDARD_CACHE_OPS	= BIT(0),
	QUIRK_BROKEN_DATA_UNCORR	= BIT(1),
};

#ifdef CONFIG_DEBUG_FS
static struct dentry *sifive_test;

static ssize_t ccache_write(struct file *file, const char __user *data,
			    size_t count, loff_t *ppos)
{
	unsigned int val;

	if (kstrtouint_from_user(data, count, 0, &val))
		return -EINVAL;
	if ((val < 0xFF) || (val >= 0x10000 && val < 0x100FF))
		writel(val, ccache_base + SIFIVE_CCACHE_ECCINJECTERR);
	else
		return -EINVAL;
	return count;
}

static const struct file_operations ccache_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ccache_write
};

static void setup_sifive_debug(void)
{
	sifive_test = debugfs_create_dir("sifive_ccache_cache", NULL);

	debugfs_create_file("sifive_debug_inject_error", 0200,
			    sifive_test, NULL, &ccache_fops);
}
#endif

static void ccache_config_read(void)
{
	u32 cfg;

	cfg = readl(ccache_base + SIFIVE_CCACHE_CONFIG);
	pr_info("%llu banks, %llu ways, sets/bank=%llu, bytes/block=%llu\n",
		FIELD_GET(SIFIVE_CCACHE_CONFIG_BANK_MASK, cfg),
		FIELD_GET(SIFIVE_CCACHE_CONFIG_WAYS_MASK, cfg),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_SETS_MASK, cfg)),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_BLKS_MASK, cfg)));

	cfg = readl(ccache_base + SIFIVE_CCACHE_WAYENABLE);
	pr_info("Index of the largest way enabled: %u\n", cfg);
}

static const struct of_device_id sifive_ccache_ids[] = {
	{ .compatible = "eswin,eic7700-l3-cache",
	  .data = (void *)(QUIRK_NONSTANDARD_CACHE_OPS) },
	{ .compatible = "sifive,fu540-c000-ccache" },
	{ .compatible = "sifive,fu740-c000-ccache" },
	{ .compatible = "starfive,jh7100-ccache",
	  .data = (void *)(QUIRK_NONSTANDARD_CACHE_OPS | QUIRK_BROKEN_DATA_UNCORR) },
	{ .compatible = "sifive,ccache0" },
	{ /* end of table */ }
};

static ATOMIC_NOTIFIER_HEAD(ccache_err_chain);

int register_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(register_sifive_ccache_error_notifier);

int unregister_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_sifive_ccache_error_notifier);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
static void ccache_flush_range(phys_addr_t start, size_t len)
{
	phys_addr_t end = start + len;
	phys_addr_t line;

	if (!len)
		return;

	mb();
	for (line = ALIGN_DOWN(start, SIFIVE_CCACHE_LINE_SIZE); line < end;
			line += SIFIVE_CCACHE_LINE_SIZE) {
#ifdef CONFIG_32BIT
		writel(line >> 4, ccache_base + SIFIVE_CCACHE_FLUSH32);
#else
		writeq(line, ccache_base + SIFIVE_CCACHE_FLUSH64);
#endif
		mb();
	}
}

static const struct riscv_nonstd_cache_ops ccache_mgmt_ops __initconst = {
	.wback = &ccache_flush_range,
	.inv = &ccache_flush_range,
	.wback_inv = &ccache_flush_range,
};
#endif /* CONFIG_RISCV_NONSTANDARD_CACHE_OPS */

static int ccache_largest_wayenabled(void)
{
	return readl(ccache_base + SIFIVE_CCACHE_WAYENABLE) & 0xFF;
}

static ssize_t number_of_ways_enabled_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "%u\n", ccache_largest_wayenabled());
}

static DEVICE_ATTR_RO(number_of_ways_enabled);

static struct attribute *priv_attrs[] = {
	&dev_attr_number_of_ways_enabled.attr,
	NULL,
};

static const struct attribute_group priv_attr_group = {
	.attrs = priv_attrs,
};

static const struct attribute_group *ccache_get_priv_group(struct cacheinfo
							   *this_leaf)
{
	/* We want to use private group for composable cache only */
	if (this_leaf->level == level)
		return &priv_attr_group;
	else
		return NULL;
}

static irqreturn_t ccache_int_handler(int irq, void *device)
{
	unsigned int add_h, add_l;

	if (irq == g_irq[DIR_CORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_LOW);
		pr_err("DirError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DirError interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DirECCFix");
	}
	if (irq == g_irq[DIR_UNCORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_LOW);
		/* Reading this register clears the DirFail interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DirECCFail");
		panic("CCACHE: DirFail @ 0x%08X.%08X\n", add_h, add_l);
	}
	if (irq == g_irq[DATA_CORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_LOW);
		pr_err("DataError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataError interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DatECCFix");
	}
	if (irq == g_irq[DATA_UNCORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_LOW);
		pr_err("DataFail @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataFail interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DatECCFail");
	}

	return IRQ_HANDLED;
}

static int sifive_ccache_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned long quirks;
	int intr_num, rc;

	quirks = (unsigned long)device_get_match_data(dev);

	intr_num = platform_irq_count(pdev);
	if (!intr_num)
		return dev_err_probe(dev, -ENODEV, "No interrupts property\n");

	for (int i = 0; i < intr_num; i++) {
		if (i == DATA_UNCORR && (quirks & QUIRK_BROKEN_DATA_UNCORR))
			continue;

		g_irq[i] = platform_get_irq(pdev, i);
		if (g_irq[i] < 0)
			return g_irq[i];

		rc = devm_request_irq(dev, g_irq[i], ccache_int_handler, 0, "ccache_ecc", NULL);
		if (rc)
			return dev_err_probe(dev, rc, "Could not request IRQ %d\n", g_irq[i]);
	}

	return 0;
}

static struct platform_driver sifive_ccache_driver = {
	.probe	= sifive_ccache_probe,
	.driver	= {
		.name		= "sifive_ccache",
		.of_match_table	= sifive_ccache_ids,
	},
};

static int __init sifive_ccache_init(void)
{
	struct device_node *np;
	struct resource res;
	const struct of_device_id *match;
	unsigned long quirks __maybe_unused;
	int rc;

	np = of_find_matching_node_and_match(NULL, sifive_ccache_ids, &match);
	if (!np)
		return -ENODEV;

	quirks = (uintptr_t)match->data;

	if (of_address_to_resource(np, 0, &res)) {
		rc = -ENODEV;
		goto err_node_put;
	}

	ccache_base = ioremap(res.start, resource_size(&res));
	if (!ccache_base) {
		rc = -ENOMEM;
		goto err_node_put;
	}

	if (of_property_read_u32(np, "cache-level", &level)) {
		rc = -ENOENT;
		goto err_unmap;
	}

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (quirks & QUIRK_NONSTANDARD_CACHE_OPS) {
		riscv_cbom_block_size = SIFIVE_CCACHE_LINE_SIZE;
		riscv_noncoherent_supported();
		riscv_noncoherent_register_cache_ops(&ccache_mgmt_ops);
	}
#endif

	ccache_config_read();

	ccache_cache_ops.get_priv_group = ccache_get_priv_group;
	riscv_set_cacheinfo_ops(&ccache_cache_ops);

#ifdef CONFIG_DEBUG_FS
	setup_sifive_debug();
#endif

	rc = platform_driver_register(&sifive_ccache_driver);
	if (rc)
		goto err_unmap;

	of_node_put(np);

	return 0;

err_unmap:
	iounmap(ccache_base);
err_node_put:
	of_node_put(np);
	return rc;
}

arch_initcall(sifive_ccache_init);
