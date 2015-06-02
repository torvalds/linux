/**
 * l2c310 (L2 Cache Controller) event counters for gator
 *
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#if defined(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_address.h>
#endif
#include <asm/hardware/cache-l2x0.h>

#include "gator.h"

#define L2C310_COUNTERS_NUM 2

static struct {
	unsigned long enabled;
	unsigned long event;
	unsigned long key;
} l2c310_counters[L2C310_COUNTERS_NUM];

static int l2c310_buffer[L2C310_COUNTERS_NUM * 2];

static void __iomem *l2c310_base;

static void gator_events_l2c310_reset_counters(void)
{
	u32 val = readl(l2c310_base + L2X0_EVENT_CNT_CTRL);

	val |= ((1 << L2C310_COUNTERS_NUM) - 1) << 1;

	writel(val, l2c310_base + L2X0_EVENT_CNT_CTRL);
}

static int gator_events_l2c310_create_files(struct super_block *sb,
					    struct dentry *root)
{
	int i;

	for (i = 0; i < L2C310_COUNTERS_NUM; i++) {
		char buf[16];
		struct dentry *dir;

		snprintf(buf, sizeof(buf), "L2C-310_cnt%d", i);
		dir = gatorfs_mkdir(sb, root, buf);
		if (WARN_ON(!dir))
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled",
				     &l2c310_counters[i].enabled);
		gatorfs_create_ulong(sb, dir, "event",
				     &l2c310_counters[i].event);
		gatorfs_create_ro_ulong(sb, dir, "key",
					&l2c310_counters[i].key);
	}

	return 0;
}

static int gator_events_l2c310_start(void)
{
	static const unsigned long l2x0_event_cntx_cfg[L2C310_COUNTERS_NUM] = {
		L2X0_EVENT_CNT0_CFG,
		L2X0_EVENT_CNT1_CFG,
	};
	int i;

	/* Counter event sources */
	for (i = 0; i < L2C310_COUNTERS_NUM; i++)
		writel((l2c310_counters[i].event & 0xf) << 2,
		       l2c310_base + l2x0_event_cntx_cfg[i]);

	gator_events_l2c310_reset_counters();

	/* Event counter enable */
	writel(1, l2c310_base + L2X0_EVENT_CNT_CTRL);

	return 0;
}

static void gator_events_l2c310_stop(void)
{
	/* Event counter disable */
	writel(0, l2c310_base + L2X0_EVENT_CNT_CTRL);
}

static int gator_events_l2c310_read(int **buffer, bool sched_switch)
{
	static const unsigned long l2x0_event_cntx_val[L2C310_COUNTERS_NUM] = {
		L2X0_EVENT_CNT0_VAL,
		L2X0_EVENT_CNT1_VAL,
	};
	int i;
	int len = 0;

	if (!on_primary_core())
		return 0;

	for (i = 0; i < L2C310_COUNTERS_NUM; i++) {
		if (l2c310_counters[i].enabled) {
			l2c310_buffer[len++] = l2c310_counters[i].key;
			l2c310_buffer[len++] = readl(l2c310_base +
						     l2x0_event_cntx_val[i]);
		}
	}

	/* l2c310 counters are saturating, not wrapping in case of overflow */
	gator_events_l2c310_reset_counters();

	if (buffer)
		*buffer = l2c310_buffer;

	return len;
}

static struct gator_interface gator_events_l2c310_interface = {
	.create_files = gator_events_l2c310_create_files,
	.start = gator_events_l2c310_start,
	.stop = gator_events_l2c310_stop,
	.read = gator_events_l2c310_read,
};

#define L2C310_ADDR_PROBE (~0)

MODULE_PARM_DESC(l2c310_addr, "L2C310 physical base address (0 to disable)");
static unsigned long l2c310_addr = L2C310_ADDR_PROBE;
module_param(l2c310_addr, ulong, 0444);

static void __iomem *gator_events_l2c310_probe(void)
{
	phys_addr_t variants[] = {
#if defined(CONFIG_ARCH_EXYNOS4) || defined(CONFIG_ARCH_S5PV310)
		0x10502000,
#endif
#if defined(CONFIG_ARCH_OMAP4)
		0x48242000,
#endif
#if defined(CONFIG_ARCH_TEGRA)
		0x50043000,
#endif
#if defined(CONFIG_ARCH_U8500)
		0xa0412000,
#endif
#if defined(CONFIG_ARCH_VEXPRESS)
		0x1e00a000, /* A9x4 core tile (HBI-0191) */
		0x2c0f0000, /* New memory map tiles */
#endif
	};
	int i;
	void __iomem *base;
#if defined(CONFIG_OF)
	struct device_node *node = of_find_all_nodes(NULL);

	if (node) {
		of_node_put(node);

		node = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
		base = of_iomap(node, 0);
		of_node_put(node);

		return base;
	}
#endif

	for (i = 0; i < ARRAY_SIZE(variants); i++) {
		base = ioremap(variants[i], SZ_4K);
		if (base) {
			u32 cache_id = readl(base + L2X0_CACHE_ID);

			if ((cache_id & 0xff0003c0) == 0x410000c0)
				return base;

			iounmap(base);
		}
	}

	return NULL;
}

int gator_events_l2c310_init(void)
{
	int i;

	if (gator_cpuid() != CORTEX_A5 && gator_cpuid() != CORTEX_A9)
		return -1;

	if (l2c310_addr == L2C310_ADDR_PROBE)
		l2c310_base = gator_events_l2c310_probe();
	else if (l2c310_addr)
		l2c310_base = ioremap(l2c310_addr, SZ_4K);

	if (!l2c310_base)
		return -1;

	for (i = 0; i < L2C310_COUNTERS_NUM; i++) {
		l2c310_counters[i].enabled = 0;
		l2c310_counters[i].key = gator_events_get_key();
	}

	return gator_events_install(&gator_events_l2c310_interface);
}
