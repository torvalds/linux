/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/module.h>

#include "gator.h"

#define NUM_REGIONS 256
#define REGION_SIZE (64*1024)
#define REGION_DEBUG 1
#define REGION_XP 64
#define NUM_XPS 11

// DT (Debug) region
#define PMEVCNTSR0    0x0150
#define PMCCNTRSR     0x0190
#define PMCR          0x01A8
#define PMSR          0x01B0
#define PMSR_REQ      0x01B8
#define PMSR_CLR      0x01C0

// XP region
#define DT_CONFIG     0x0300
#define DT_CONTROL    0x0370

// Multiple
#define PMU_EVENT_SEL 0x0600
#define OLY_ID        0xFF00

#define CCNT 4
#define CNTMAX (CCNT + 1)

#define get_pmu_event_id(event) (((event) >> 0) & 0xFF)
#define get_node_type(event) (((event) >> 8) & 0xFF)
#define get_region(event) (((event) >> 16) & 0xFF)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)

// From kernel/params.c
#define STANDARD_PARAM_DEF(name, type, format, tmptype, strtolfn)      	\
	int param_set_##name(const char *val, struct kernel_param *kp)	\
	{								\
		tmptype l;						\
		int ret;						\
									\
		if (!val) return -EINVAL;				\
		ret = strtolfn(val, 0, &l);				\
		if (ret == -EINVAL || ((type)l != l))			\
			return -EINVAL;					\
		*((type *)kp->arg) = l;					\
		return 0;						\
	}								\
	int param_get_##name(char *buffer, struct kernel_param *kp)	\
	{								\
		return sprintf(buffer, format, *((type *)kp->arg));	\
	}

#else

// From kernel/params.c
#define STANDARD_PARAM_DEF(name, type, format, tmptype, strtolfn)      	\
	int param_set_##name(const char *val, const struct kernel_param *kp) \
	{								\
		tmptype l;						\
		int ret;						\
									\
		ret = strtolfn(val, 0, &l);				\
		if (ret < 0 || ((type)l != l))				\
			return ret < 0 ? ret : -EINVAL;			\
		*((type *)kp->arg) = l;					\
		return 0;						\
	}								\
	int param_get_##name(char *buffer, const struct kernel_param *kp) \
	{								\
		return scnprintf(buffer, PAGE_SIZE, format,		\
				*((type *)kp->arg));			\
	}								\
	struct kernel_param_ops param_ops_##name = {			\
		.set = param_set_##name,				\
		.get = param_get_##name,				\
	};								\
	EXPORT_SYMBOL(param_set_##name);				\
	EXPORT_SYMBOL(param_get_##name);				\
	EXPORT_SYMBOL(param_ops_##name)

#endif

STANDARD_PARAM_DEF(u64, u64, "%llu", u64, strict_strtoull);

// From include/linux/moduleparam.h
#define param_check_u64(name, p) __param_check(name, p, u64)

MODULE_PARM_DESC(ccn504_addr, "CCN-504 physical base address");
static u64 ccn504_addr = 0;
module_param(ccn504_addr, u64, 0444);

static void __iomem *gator_events_ccn504_base;
static bool gator_events_ccn504_global_enabled;
static unsigned long gator_events_ccn504_enabled[CNTMAX];
static unsigned long gator_events_ccn504_event[CNTMAX];
static unsigned long gator_events_ccn504_key[CNTMAX];
static int gator_events_ccn504_buffer[2*CNTMAX];
static int gator_events_ccn504_prev[CNTMAX];

static void gator_events_ccn504_create_shutdown(void)
{
	if (gator_events_ccn504_base != NULL) {
		iounmap(gator_events_ccn504_base);
	}
}

static int gator_events_ccn504_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;
	char buf[32];

	for (i = 0; i < CNTMAX; ++i) {
		if (i == CCNT) {
			snprintf(buf, sizeof(buf), "CCN-504_ccnt");
		} else {
			snprintf(buf, sizeof(buf), "CCN-504_cnt%i", i);
		}
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir) {
			return -1;
		}

		gatorfs_create_ulong(sb, dir, "enabled", &gator_events_ccn504_enabled[i]);
		if (i != CCNT) {
			gatorfs_create_ulong(sb, dir, "event", &gator_events_ccn504_event[i]);
		}
		gatorfs_create_ro_ulong(sb, dir, "key", &gator_events_ccn504_key[i]);
	}

	return 0;
}

static void gator_events_ccn504_set_dt_config(int xp_node_id, int event_num, int value)
{
	u32 dt_config;

	dt_config = readl(gator_events_ccn504_base + (REGION_XP + xp_node_id)*REGION_SIZE + DT_CONFIG);
	dt_config |= (value + event_num) << (4*event_num);
	writel(dt_config, gator_events_ccn504_base + (REGION_XP + xp_node_id)*REGION_SIZE + DT_CONFIG);
}

static int gator_events_ccn504_start(void)
{
	int i;

	gator_events_ccn504_global_enabled = 0;
	for (i = 0; i < CNTMAX; ++i) {
		if (gator_events_ccn504_enabled[i]) {
			gator_events_ccn504_global_enabled = 1;
			break;
		}
	}

	if (!gator_events_ccn504_global_enabled) {
		return 0;
	}

	memset(&gator_events_ccn504_prev, 0x80, sizeof(gator_events_ccn504_prev));

	// Disable INTREQ on overflow
	// [6] ovfl_intr_en = 0
	// perhaps set to 1?
	// [5] cntr_rst = 0
	// No register paring
	// [4:1] cntcfg = 0
	// Enable PMU features
	// [0] pmu_en = 1
	writel(0x1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMCR);

	// Configure the XPs
	for (i = 0; i < NUM_XPS; ++i) {
		int dt_control;

		// Pass on all events
		writel(0, gator_events_ccn504_base + (REGION_XP + i)*REGION_SIZE + DT_CONFIG);

		// Enable PMU capability
		// [0] dt_enable = 1
		dt_control = readl(gator_events_ccn504_base + (REGION_XP + i)*REGION_SIZE + DT_CONTROL);
		dt_control |= 0x1;
		writel(dt_control, gator_events_ccn504_base + (REGION_XP + i)*REGION_SIZE + DT_CONTROL);
	}

	// Assume no other pmu_event_sel registers are set

	// cycle counter does not need to be enabled
	for (i = 0; i < CCNT; ++i) {
		int pmu_event_id;
		int node_type;
		int region;
		u32 pmu_event_sel;
		u32 oly_id_whole;
		u32 oly_id;
		u32 node_id;

		if (!gator_events_ccn504_enabled[i]) {
			continue;
		}

		pmu_event_id = get_pmu_event_id(gator_events_ccn504_event[i]);
		node_type = get_node_type(gator_events_ccn504_event[i]);
		region = get_region(gator_events_ccn504_event[i]);

		// Verify the node_type
		oly_id_whole = readl(gator_events_ccn504_base + region*REGION_SIZE + OLY_ID);
		oly_id = oly_id_whole & 0x1F;
		node_id = (oly_id_whole >> 8) & 0x7F;
		if ((oly_id != node_type) ||
				((node_type == 0x16) && ((oly_id != 0x14) && (oly_id != 0x15) && (oly_id != 0x16) && (oly_id != 0x18) && (oly_id != 0x19) && (oly_id != 0x1A)))) {
			printk(KERN_ERR "gator: oly_id is 0x%x expected 0x%x\n", oly_id, node_type);
			return -1;
		}

		// Set the control register
		pmu_event_sel = readl(gator_events_ccn504_base + region*REGION_SIZE + PMU_EVENT_SEL);
		switch (node_type) {
		case 0x08: // XP
			pmu_event_sel |= pmu_event_id << (7*i);
			gator_events_ccn504_set_dt_config(node_id, i, 0x4);
			break;
		case 0x04: // HN-F
		case 0x16: // RN-I
		case 0x10: // SBAS
			pmu_event_sel |= pmu_event_id << (4*i);
			gator_events_ccn504_set_dt_config(node_id/2, i, (node_id & 1) == 0 ? 0x8 : 0xC);
			break;
		}
		writel(pmu_event_sel, gator_events_ccn504_base + region*REGION_SIZE + PMU_EVENT_SEL);
	}

	return 0;
}

static void gator_events_ccn504_stop(void)
{
	int i;

	if (!gator_events_ccn504_global_enabled) {
		return;
	}

	// cycle counter does not need to be disabled
	for (i = 0; i < CCNT; ++i) {
		int region;

		if (!gator_events_ccn504_enabled[i]) {
			continue;
		}

		region = get_region(gator_events_ccn504_event[i]);

		writel(0, gator_events_ccn504_base + region*REGION_SIZE + PMU_EVENT_SEL);
	}

	// Clear dt_config
	for (i = 0; i < NUM_XPS; ++i) {
		writel(0, gator_events_ccn504_base + (REGION_XP + i)*REGION_SIZE + DT_CONFIG);
	}
}

static int gator_events_ccn504_read(int **buffer)
{
	int i;
	int len = 0;
	int value;

	if (!on_primary_core() || !gator_events_ccn504_global_enabled) {
		return 0;
	}

	// Verify the pmsr register is zero
	while (readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR) != 0);

	// Request a PMU snapshot
	writel(1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR_REQ);

	// Wait for the snapshot
	while (readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR) == 0);

	// Read the shadow registers
	for (i = 0; i < CNTMAX; ++i) {
		if (!gator_events_ccn504_enabled[i]) {
			continue;
		}

		value = readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + (i == CCNT ? PMCCNTRSR : PMEVCNTSR0 + 8*i));
		if (gator_events_ccn504_prev[i] != 0x80808080) {
			gator_events_ccn504_buffer[len++] = gator_events_ccn504_key[i];
			gator_events_ccn504_buffer[len++] = value - gator_events_ccn504_prev[i];
		}
		gator_events_ccn504_prev[i] = value;

		// Are the counters registers cleared when read? Is that what the cntr_rst bit on the pmcr register does?
	}

	// Clear the PMU snapshot status
	writel(1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR_CLR);

	if (buffer)
		*buffer = gator_events_ccn504_buffer;

	return len;
}

static struct gator_interface gator_events_ccn504_interface = {
	.shutdown = gator_events_ccn504_create_shutdown,
	.create_files = gator_events_ccn504_create_files,
	.start = gator_events_ccn504_start,
	.stop = gator_events_ccn504_stop,
	.read = gator_events_ccn504_read,
};

int gator_events_ccn504_init(void)
{
	int i;

	if (ccn504_addr == 0) {
		return -1;
	}

	gator_events_ccn504_base = ioremap(ccn504_addr, NUM_REGIONS*REGION_SIZE);
	if (gator_events_ccn504_base == NULL) {
		printk(KERN_ERR "gator: ioremap returned NULL\n");
		return -1;
	}

	for (i = 0; i < CNTMAX; ++i) {
		gator_events_ccn504_enabled[i] = 0;
		gator_events_ccn504_event[i] = 0;
		gator_events_ccn504_key[i] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_ccn504_interface);
}
