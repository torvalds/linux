// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive composable cache controller Driver
 *
 * Copyright (C) 2018-2022 SiFive, Inc.
 *
 */

#define pr_fmt(fmt) "CCACHE: " fmt

#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/bitfield.h>
#include <asm/cacheinfo.h>
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

#define SIFIVE_CCACHE_WAYENABLE 0x08
#define SIFIVE_CCACHE_ECCINJECTERR 0x40

#define SIFIVE_CCACHE_MAX_ECCINTR 4

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
	{ .compatible = "sifive,fu540-c000-ccache" },
	{ .compatible = "sifive,fu740-c000-ccache" },
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

static int __init sifive_ccache_init(void)
{
	struct device_node *np;
	struct resource res;
	int i, rc, intr_num;

	np = of_find_matching_node(NULL, sifive_ccache_ids);
	if (!np)
		return -ENODEV;

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;

	ccache_base = ioremap(res.start, resource_size(&res));
	if (!ccache_base)
		return -ENOMEM;

	if (of_property_read_u32(np, "cache-level", &level))
		return -ENOENT;

	intr_num = of_property_count_u32_elems(np, "interrupts");
	if (!intr_num) {
		pr_err("No interrupts property\n");
		return -ENODEV;
	}

	for (i = 0; i < intr_num; i++) {
		g_irq[i] = irq_of_parse_and_map(np, i);
		rc = request_irq(g_irq[i], ccache_int_handler, 0, "ccache_ecc",
				 NULL);
		if (rc) {
			pr_err("Could not request IRQ %d\n", g_irq[i]);
			return rc;
		}
	}

	ccache_config_read();

	ccache_cache_ops.get_priv_group = ccache_get_priv_group;
	riscv_set_cacheinfo_ops(&ccache_cache_ops);

#ifdef CONFIG_DEBUG_FS
	setup_sifive_debug();
#endif
	return 0;
}

device_initcall(sifive_ccache_init);
