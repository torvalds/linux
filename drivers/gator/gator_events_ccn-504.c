/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*******************************************************************************
 * WARNING: This code is an experimental implementation of the CCN-504 hardware
 * counters which has not been tested on the hardware. Commented debug
 * statements are present and can be uncommented for diagnostic purposes.
 ******************************************************************************/

#include <linux/io.h>
#include <linux/module.h>

#include "gator.h"

#define PERIPHBASE 0x2E000000

#define NUM_REGIONS 256
#define REGION_SIZE (64*1024)
#define REGION_DEBUG 1
#define REGION_XP 64

// DT (Debug) region
#define PMEVCNTSR0    0x0150
#define PMCCNTRSR     0x0190
#define PMCR          0x01A8
#define PMSR          0x01B0
#define PMSR_REQ      0x01B8
#define PMSR_CLR      0x01C0

// XP region
#define DT_CONFIG     0x0300

// Multiple
#define PMU_EVENT_SEL 0x0600
#define OLY_ID        0xFF00

#define CCNT 4
#define CNTMAX (4 + 1)

#define get_pmu_event_id(event) (((event) >> 0) & 0xFF)
#define get_node_type(event) (((event) >> 8) & 0xFF)
#define get_region(event) (((event) >> 16) & 0xFF)

MODULE_PARM_DESC(ccn504_addr, "CCN-504 physical base address");
static unsigned long ccn504_addr = 0;
module_param(ccn504_addr, ulong, 0444);

static void __iomem *gator_events_ccn504_base;
static unsigned long gator_events_ccn504_enabled[CNTMAX];
static unsigned long gator_events_ccn504_event[CNTMAX];
static unsigned long gator_events_ccn504_key[CNTMAX];
static int gator_events_ccn504_buffer[2*CNTMAX];

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
	//printk(KERN_ERR "%s(%s:%i) writel %x %x\n", __FUNCTION__, __FILE__, __LINE__, dt_config, (REGION_XP + xp_node_id)*REGION_SIZE + DT_CONFIG);
	writel(dt_config, gator_events_ccn504_base + (REGION_XP + xp_node_id)*REGION_SIZE + DT_CONFIG);
}

static int gator_events_ccn504_start(void)
{
	int i;

	// Disable INTREQ on overflow
	// [6] ovfl_intr_en = 0
	// perhaps set to 1?
	// [5] cntr_rst = 0
	// No register paring
	// [4:1] cntcfg = 0
	// Enable PMU features
	// [0] pmu_en = 1
	//printk(KERN_ERR "%s(%s:%i) writel %x %x\n", __FUNCTION__, __FILE__, __LINE__, 0x1, REGION_DEBUG*REGION_SIZE + PMCR);
	writel(0x1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMCR);

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
		//printk(KERN_ERR "%s(%s:%i) pmu_event_id: %x node_type: %x region: %x\n", __FUNCTION__, __FILE__, __LINE__, pmu_event_id, node_type, region);

		// Verify the node_type
		oly_id_whole = readl(gator_events_ccn504_base + region*REGION_SIZE + OLY_ID);
		oly_id = oly_id_whole & 0x1F;
		node_id = (oly_id_whole >> 8) & 0x7F;
		if ((oly_id != node_type) ||
				((node_type == 0x16) && ((oly_id == 0x14) || (oly_id == 0x15) || (oly_id == 0x16) || (oly_id == 0x18) || (oly_id == 0x19) || (oly_id == 0x1A)))) {
			printk(KERN_ERR "%s(%s:%i) oly_id is %x expected %x\n", __FUNCTION__, __FILE__, __LINE__, oly_id, node_type);
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
		//printk(KERN_ERR "%s(%s:%i) writel %x %x\n", __FUNCTION__, __FILE__, __LINE__, pmu_event_sel, region*REGION_SIZE + PMU_EVENT_SEL);
		writel(pmu_event_sel, gator_events_ccn504_base + region*REGION_SIZE + PMU_EVENT_SEL);
	}

	return 0;
}

static void gator_events_ccn504_stop(void)
{
	int i;

	// cycle counter does not need to be disabled
	for (i = 0; i < CCNT; ++i) {
		int node_type;
		int region;

		node_type = get_node_type(gator_events_ccn504_event[i]);
		region = get_region(gator_events_ccn504_event[i]);

		//printk(KERN_ERR "%s(%s:%i) writel %x %x\n", __FUNCTION__, __FILE__, __LINE__, 0, region*REGION_SIZE + PMU_EVENT_SEL);
		writel(0, gator_events_ccn504_base + region*REGION_SIZE + PMU_EVENT_SEL);
	}

	// Clear dt_config
	for (i = 0; i < 11; ++i) {
		//printk(KERN_ERR "%s(%s:%i) writel %x %x\n", __FUNCTION__, __FILE__, __LINE__, 0, (REGION_XP + i)*REGION_SIZE + DT_CONFIG);
		writel(0, gator_events_ccn504_base + (REGION_XP + i)*REGION_SIZE + DT_CONFIG);
	}
}

static int gator_events_ccn504_read(int **buffer)
{
	int i;
	int len = 0;

	if (!on_primary_core()) {
		return 0;
	}

	// Verify the pmsr register is zero
	//i = 0;
	while (readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR) != 0) {
		//++i;
	}
	//printk(KERN_ERR "%s(%s:%i) %i\n", __FUNCTION__, __FILE__, __LINE__, i);

	// Request a PMU snapshot
	writel(1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR_REQ);

	// Wait for the snapshot
	//i = 0;
	while (readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR) == 0) {
		//++i;
	}
	//printk(KERN_ERR "%s(%s:%i) %i\n", __FUNCTION__, __FILE__, __LINE__, i);

	// Read the shadow registers
	for (i = 0; i < CNTMAX; ++i) {
		if (!gator_events_ccn504_enabled[i]) {
			continue;
		}

		gator_events_ccn504_buffer[len++] = gator_events_ccn504_key[i];
		gator_events_ccn504_buffer[len++] = readl(gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + (i == CCNT ? PMCCNTRSR : PMEVCNTSR0 + 8*i));

		// Are the counters registers cleared when read? Is that what the cntr_rst bit on the pmcr register does?
	}

	// Clear the PMU snapshot status
	writel(1, gator_events_ccn504_base + REGION_DEBUG*REGION_SIZE + PMSR_CLR);

	return len;
}

static void __maybe_unused gator_events_ccn504_enumerate(int pos, int size)
{
	int i;
	u32 oly_id;

	for (i = pos; i < pos + size; ++i) {
		oly_id = readl(gator_events_ccn504_base + i*REGION_SIZE + OLY_ID);
		printk(KERN_ERR "%s(%s:%i) %i %08x\n", __FUNCTION__, __FILE__, __LINE__, i, oly_id);
	}
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
		printk(KERN_ERR "%s(%s:%i) ioremap returned NULL\n", __FUNCTION__, __FILE__, __LINE__);
		return -1;
	}
	//printk(KERN_ERR "%s(%s:%i)\n", __FUNCTION__, __FILE__, __LINE__);

	// Test - can memory be read
	{
		//gator_events_ccn504_enumerate(0, NUM_REGIONS);

#if 0
		// DT
		gator_events_ccn504_enumerate(1, 1);
		// HN-F
		gator_events_ccn504_enumerate(32, 8);
		// XP
		gator_events_ccn504_enumerate(64, 11);
		// RN-I
		gator_events_ccn504_enumerate(128, 1);
		gator_events_ccn504_enumerate(130, 1);
		gator_events_ccn504_enumerate(134, 1);
		gator_events_ccn504_enumerate(140, 1);
		gator_events_ccn504_enumerate(144, 1);
		gator_events_ccn504_enumerate(148, 1);
		// SBAS
		gator_events_ccn504_enumerate(129, 1);
		gator_events_ccn504_enumerate(137, 1);
		gator_events_ccn504_enumerate(139, 1);
		gator_events_ccn504_enumerate(147, 1);
#endif
	}

	for (i = 0; i < CNTMAX; ++i) {
		gator_events_ccn504_enabled[i] = 0;
		gator_events_ccn504_event[i] = 0;
		gator_events_ccn504_key[i] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_ccn504_interface);
}

gator_events_init(gator_events_ccn504_init);
