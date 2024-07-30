// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2021-2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzysztof.kozlowski@linaro.org>, based on
 *         previous work of Thara Gopinath and msm-4.9 downstream sources.
 */

#include <linux/err.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/sizes.h>

/*
 * The BWMON samples data throughput within 'sample_ms' time. With three
 * configurable thresholds (Low, Medium and High) gives four windows (called
 * zones) of current bandwidth:
 *
 * Zone 0: byte count < THRES_LO
 * Zone 1: THRES_LO < byte count < THRES_MED
 * Zone 2: THRES_MED < byte count < THRES_HIGH
 * Zone 3: THRES_HIGH < byte count
 *
 * Zones 0 and 2 are not used by this driver.
 */

/* Internal sampling clock frequency */
#define HW_TIMER_HZ				19200000

#define BWMON_V4_GLOBAL_IRQ_CLEAR		0x108
#define BWMON_V4_GLOBAL_IRQ_ENABLE		0x10c
/*
 * All values here and further are matching regmap fields, so without absolute
 * register offsets.
 */
#define BWMON_V4_GLOBAL_IRQ_ENABLE_ENABLE	BIT(0)

/*
 * Starting with SDM845, the BWMON4 register space has changed a bit:
 * the global registers were jammed into the beginning of the monitor region.
 * To keep the proper offsets, one would have to map <GLOBAL_BASE 0x200> and
 * <GLOBAL_BASE+0x100 0x300>, which is straight up wrong.
 * To facilitate for that, while allowing the older, arguably more proper
 * implementations to work, offset the global registers by -0x100 to avoid
 * having to map half of the global registers twice.
 */
#define BWMON_V4_845_OFFSET			0x100
#define BWMON_V4_GLOBAL_IRQ_CLEAR_845		(BWMON_V4_GLOBAL_IRQ_CLEAR - BWMON_V4_845_OFFSET)
#define BWMON_V4_GLOBAL_IRQ_ENABLE_845		(BWMON_V4_GLOBAL_IRQ_ENABLE - BWMON_V4_845_OFFSET)

#define BWMON_V4_IRQ_STATUS			0x100
#define BWMON_V4_IRQ_CLEAR			0x108

#define BWMON_V4_IRQ_ENABLE			0x10c
#define BWMON_IRQ_ENABLE_MASK			(BIT(1) | BIT(3))
#define BWMON_V5_IRQ_STATUS			0x000
#define BWMON_V5_IRQ_CLEAR			0x008
#define BWMON_V5_IRQ_ENABLE			0x00c

#define BWMON_V4_ENABLE				0x2a0
#define BWMON_V5_ENABLE				0x010
#define BWMON_ENABLE_ENABLE			BIT(0)

#define BWMON_V4_CLEAR				0x2a4
#define BWMON_V5_CLEAR				0x014
#define BWMON_CLEAR_CLEAR			BIT(0)
#define BWMON_CLEAR_CLEAR_ALL			BIT(1)

#define BWMON_V4_SAMPLE_WINDOW			0x2a8
#define BWMON_V5_SAMPLE_WINDOW			0x020

#define BWMON_V4_THRESHOLD_HIGH			0x2ac
#define BWMON_V4_THRESHOLD_MED			0x2b0
#define BWMON_V4_THRESHOLD_LOW			0x2b4
#define BWMON_V5_THRESHOLD_HIGH			0x024
#define BWMON_V5_THRESHOLD_MED			0x028
#define BWMON_V5_THRESHOLD_LOW			0x02c

#define BWMON_V4_ZONE_ACTIONS			0x2b8
#define BWMON_V5_ZONE_ACTIONS			0x030
/*
 * Actions to perform on some zone 'z' when current zone hits the threshold:
 * Increment counter of zone 'z'
 */
#define BWMON_ZONE_ACTIONS_INCREMENT(z)		(0x2 << ((z) * 2))
/* Clear counter of zone 'z' */
#define BWMON_ZONE_ACTIONS_CLEAR(z)		(0x1 << ((z) * 2))

/* Zone 0 threshold hit: Clear zone count */
#define BWMON_ZONE_ACTIONS_ZONE0		(BWMON_ZONE_ACTIONS_CLEAR(0))

/* Zone 1 threshold hit: Increment zone count & clear lower zones */
#define BWMON_ZONE_ACTIONS_ZONE1		(BWMON_ZONE_ACTIONS_INCREMENT(1) | \
						 BWMON_ZONE_ACTIONS_CLEAR(0))

/* Zone 2 threshold hit: Increment zone count & clear lower zones */
#define BWMON_ZONE_ACTIONS_ZONE2		(BWMON_ZONE_ACTIONS_INCREMENT(2) | \
						 BWMON_ZONE_ACTIONS_CLEAR(1) | \
						 BWMON_ZONE_ACTIONS_CLEAR(0))

/* Zone 3 threshold hit: Increment zone count & clear lower zones */
#define BWMON_ZONE_ACTIONS_ZONE3		(BWMON_ZONE_ACTIONS_INCREMENT(3) | \
						 BWMON_ZONE_ACTIONS_CLEAR(2) | \
						 BWMON_ZONE_ACTIONS_CLEAR(1) | \
						 BWMON_ZONE_ACTIONS_CLEAR(0))

/*
 * There is no clear documentation/explanation of BWMON_V4_THRESHOLD_COUNT
 * register. Based on observations, this is number of times one threshold has to
 * be reached, to trigger interrupt in given zone.
 *
 * 0xff are maximum values meant to ignore the zones 0 and 2.
 */
#define BWMON_V4_THRESHOLD_COUNT		0x2bc
#define BWMON_V5_THRESHOLD_COUNT		0x034
#define BWMON_THRESHOLD_COUNT_ZONE0_DEFAULT	0xff
#define BWMON_THRESHOLD_COUNT_ZONE2_DEFAULT	0xff

#define BWMON_V4_ZONE_MAX(zone)			(0x2e0 + 4 * (zone))
#define BWMON_V5_ZONE_MAX(zone)			(0x044 + 4 * (zone))

/* Quirks for specific BWMON types */
#define BWMON_HAS_GLOBAL_IRQ			BIT(0)
#define BWMON_NEEDS_FORCE_CLEAR			BIT(1)

enum bwmon_fields {
	/* Global region fields, keep them at the top */
	F_GLOBAL_IRQ_CLEAR,
	F_GLOBAL_IRQ_ENABLE,
	F_NUM_GLOBAL_FIELDS,

	/* Monitor region fields */
	F_IRQ_STATUS = F_NUM_GLOBAL_FIELDS,
	F_IRQ_CLEAR,
	F_IRQ_ENABLE,
	F_ENABLE,
	F_CLEAR,
	F_SAMPLE_WINDOW,
	F_THRESHOLD_HIGH,
	F_THRESHOLD_MED,
	F_THRESHOLD_LOW,
	F_ZONE_ACTIONS_ZONE0,
	F_ZONE_ACTIONS_ZONE1,
	F_ZONE_ACTIONS_ZONE2,
	F_ZONE_ACTIONS_ZONE3,
	F_THRESHOLD_COUNT_ZONE0,
	F_THRESHOLD_COUNT_ZONE1,
	F_THRESHOLD_COUNT_ZONE2,
	F_THRESHOLD_COUNT_ZONE3,
	F_ZONE0_MAX,
	F_ZONE1_MAX,
	F_ZONE2_MAX,
	F_ZONE3_MAX,

	F_NUM_FIELDS
};

struct icc_bwmon_data {
	unsigned int sample_ms;
	unsigned int count_unit_kb; /* kbytes */
	u8 zone1_thres_count;
	u8 zone3_thres_count;
	unsigned int quirks;

	const struct regmap_config *regmap_cfg;
	const struct reg_field *regmap_fields;

	const struct regmap_config *global_regmap_cfg;
	const struct reg_field *global_regmap_fields;
};

struct icc_bwmon {
	struct device *dev;
	const struct icc_bwmon_data *data;
	int irq;

	struct regmap_field *regs[F_NUM_FIELDS];
	struct regmap_field *global_regs[F_NUM_GLOBAL_FIELDS];

	unsigned int max_bw_kbps;
	unsigned int min_bw_kbps;
	unsigned int target_kbps;
	unsigned int current_kbps;
};

/* BWMON v4 */
static const struct reg_field msm8998_bwmon_reg_fields[] = {
	[F_GLOBAL_IRQ_CLEAR]	= {},
	[F_GLOBAL_IRQ_ENABLE]	= {},
	[F_IRQ_STATUS]		= REG_FIELD(BWMON_V4_IRQ_STATUS, 4, 7),
	[F_IRQ_CLEAR]		= REG_FIELD(BWMON_V4_IRQ_CLEAR, 4, 7),
	[F_IRQ_ENABLE]		= REG_FIELD(BWMON_V4_IRQ_ENABLE, 4, 7),
	/* F_ENABLE covers entire register to disable other features */
	[F_ENABLE]		= REG_FIELD(BWMON_V4_ENABLE, 0, 31),
	[F_CLEAR]		= REG_FIELD(BWMON_V4_CLEAR, 0, 1),
	[F_SAMPLE_WINDOW]	= REG_FIELD(BWMON_V4_SAMPLE_WINDOW, 0, 23),
	[F_THRESHOLD_HIGH]	= REG_FIELD(BWMON_V4_THRESHOLD_HIGH, 0, 11),
	[F_THRESHOLD_MED]	= REG_FIELD(BWMON_V4_THRESHOLD_MED, 0, 11),
	[F_THRESHOLD_LOW]	= REG_FIELD(BWMON_V4_THRESHOLD_LOW, 0, 11),
	[F_ZONE_ACTIONS_ZONE0]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 0, 7),
	[F_ZONE_ACTIONS_ZONE1]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 8, 15),
	[F_ZONE_ACTIONS_ZONE2]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 16, 23),
	[F_ZONE_ACTIONS_ZONE3]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 24, 31),
	[F_THRESHOLD_COUNT_ZONE0]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 0, 7),
	[F_THRESHOLD_COUNT_ZONE1]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 8, 15),
	[F_THRESHOLD_COUNT_ZONE2]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 16, 23),
	[F_THRESHOLD_COUNT_ZONE3]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 24, 31),
	[F_ZONE0_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(0), 0, 11),
	[F_ZONE1_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(1), 0, 11),
	[F_ZONE2_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(2), 0, 11),
	[F_ZONE3_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(3), 0, 11),
};

static const struct regmap_range msm8998_bwmon_reg_noread_ranges[] = {
	regmap_reg_range(BWMON_V4_IRQ_CLEAR, BWMON_V4_IRQ_CLEAR),
	regmap_reg_range(BWMON_V4_CLEAR, BWMON_V4_CLEAR),
};

static const struct regmap_access_table msm8998_bwmon_reg_read_table = {
	.no_ranges	= msm8998_bwmon_reg_noread_ranges,
	.n_no_ranges	= ARRAY_SIZE(msm8998_bwmon_reg_noread_ranges),
};

static const struct regmap_range msm8998_bwmon_reg_volatile_ranges[] = {
	regmap_reg_range(BWMON_V4_IRQ_STATUS, BWMON_V4_IRQ_STATUS),
	regmap_reg_range(BWMON_V4_ZONE_MAX(0), BWMON_V4_ZONE_MAX(3)),
};

static const struct regmap_access_table msm8998_bwmon_reg_volatile_table = {
	.yes_ranges	= msm8998_bwmon_reg_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(msm8998_bwmon_reg_volatile_ranges),
};

static const struct reg_field msm8998_bwmon_global_reg_fields[] = {
	[F_GLOBAL_IRQ_CLEAR]	= REG_FIELD(BWMON_V4_GLOBAL_IRQ_CLEAR, 0, 0),
	[F_GLOBAL_IRQ_ENABLE]	= REG_FIELD(BWMON_V4_GLOBAL_IRQ_ENABLE, 0, 0),
};

static const struct regmap_range msm8998_bwmon_global_reg_noread_ranges[] = {
	regmap_reg_range(BWMON_V4_GLOBAL_IRQ_CLEAR, BWMON_V4_GLOBAL_IRQ_CLEAR),
};

static const struct regmap_access_table msm8998_bwmon_global_reg_read_table = {
	.no_ranges	= msm8998_bwmon_global_reg_noread_ranges,
	.n_no_ranges	= ARRAY_SIZE(msm8998_bwmon_global_reg_noread_ranges),
};

/*
 * Fill the cache for non-readable registers only as rest does not really
 * matter and can be read from the device.
 */
static const struct reg_default msm8998_bwmon_reg_defaults[] = {
	{ BWMON_V4_IRQ_CLEAR, 0x0 },
	{ BWMON_V4_CLEAR, 0x0 },
};

static const struct reg_default msm8998_bwmon_global_reg_defaults[] = {
	{ BWMON_V4_GLOBAL_IRQ_CLEAR, 0x0 },
};

static const struct regmap_config msm8998_bwmon_regmap_cfg = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	/*
	 * No concurrent access expected - driver has one interrupt handler,
	 * regmap is not shared, no driver or user-space API.
	 */
	.disable_locking	= true,
	.rd_table		= &msm8998_bwmon_reg_read_table,
	.volatile_table		= &msm8998_bwmon_reg_volatile_table,
	.reg_defaults		= msm8998_bwmon_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(msm8998_bwmon_reg_defaults),
	/*
	 * Cache is necessary for using regmap fields with non-readable
	 * registers.
	 */
	.cache_type		= REGCACHE_MAPLE,
};

static const struct regmap_config msm8998_bwmon_global_regmap_cfg = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	/*
	 * No concurrent access expected - driver has one interrupt handler,
	 * regmap is not shared, no driver or user-space API.
	 */
	.disable_locking	= true,
	.rd_table		= &msm8998_bwmon_global_reg_read_table,
	.reg_defaults		= msm8998_bwmon_global_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(msm8998_bwmon_global_reg_defaults),
	/*
	 * Cache is necessary for using regmap fields with non-readable
	 * registers.
	 */
	.cache_type		= REGCACHE_MAPLE,
};

static const struct reg_field sdm845_cpu_bwmon_reg_fields[] = {
	[F_GLOBAL_IRQ_CLEAR]	= REG_FIELD(BWMON_V4_GLOBAL_IRQ_CLEAR_845, 0, 0),
	[F_GLOBAL_IRQ_ENABLE]	= REG_FIELD(BWMON_V4_GLOBAL_IRQ_ENABLE_845, 0, 0),
	[F_IRQ_STATUS]		= REG_FIELD(BWMON_V4_IRQ_STATUS, 4, 7),
	[F_IRQ_CLEAR]		= REG_FIELD(BWMON_V4_IRQ_CLEAR, 4, 7),
	[F_IRQ_ENABLE]		= REG_FIELD(BWMON_V4_IRQ_ENABLE, 4, 7),
	/* F_ENABLE covers entire register to disable other features */
	[F_ENABLE]		= REG_FIELD(BWMON_V4_ENABLE, 0, 31),
	[F_CLEAR]		= REG_FIELD(BWMON_V4_CLEAR, 0, 1),
	[F_SAMPLE_WINDOW]	= REG_FIELD(BWMON_V4_SAMPLE_WINDOW, 0, 23),
	[F_THRESHOLD_HIGH]	= REG_FIELD(BWMON_V4_THRESHOLD_HIGH, 0, 11),
	[F_THRESHOLD_MED]	= REG_FIELD(BWMON_V4_THRESHOLD_MED, 0, 11),
	[F_THRESHOLD_LOW]	= REG_FIELD(BWMON_V4_THRESHOLD_LOW, 0, 11),
	[F_ZONE_ACTIONS_ZONE0]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 0, 7),
	[F_ZONE_ACTIONS_ZONE1]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 8, 15),
	[F_ZONE_ACTIONS_ZONE2]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 16, 23),
	[F_ZONE_ACTIONS_ZONE3]	= REG_FIELD(BWMON_V4_ZONE_ACTIONS, 24, 31),
	[F_THRESHOLD_COUNT_ZONE0]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 0, 7),
	[F_THRESHOLD_COUNT_ZONE1]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 8, 15),
	[F_THRESHOLD_COUNT_ZONE2]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 16, 23),
	[F_THRESHOLD_COUNT_ZONE3]	= REG_FIELD(BWMON_V4_THRESHOLD_COUNT, 24, 31),
	[F_ZONE0_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(0), 0, 11),
	[F_ZONE1_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(1), 0, 11),
	[F_ZONE2_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(2), 0, 11),
	[F_ZONE3_MAX]		= REG_FIELD(BWMON_V4_ZONE_MAX(3), 0, 11),
};

static const struct regmap_range sdm845_cpu_bwmon_reg_noread_ranges[] = {
	regmap_reg_range(BWMON_V4_GLOBAL_IRQ_CLEAR_845, BWMON_V4_GLOBAL_IRQ_CLEAR_845),
	regmap_reg_range(BWMON_V4_IRQ_CLEAR, BWMON_V4_IRQ_CLEAR),
	regmap_reg_range(BWMON_V4_CLEAR, BWMON_V4_CLEAR),
};

static const struct regmap_access_table sdm845_cpu_bwmon_reg_read_table = {
	.no_ranges	= sdm845_cpu_bwmon_reg_noread_ranges,
	.n_no_ranges	= ARRAY_SIZE(sdm845_cpu_bwmon_reg_noread_ranges),
};

/*
 * Fill the cache for non-readable registers only as rest does not really
 * matter and can be read from the device.
 */
static const struct reg_default sdm845_cpu_bwmon_reg_defaults[] = {
	{ BWMON_V4_GLOBAL_IRQ_CLEAR_845, 0x0 },
	{ BWMON_V4_IRQ_CLEAR, 0x0 },
	{ BWMON_V4_CLEAR, 0x0 },
};

static const struct regmap_config sdm845_cpu_bwmon_regmap_cfg = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	/*
	 * No concurrent access expected - driver has one interrupt handler,
	 * regmap is not shared, no driver or user-space API.
	 */
	.disable_locking	= true,
	.rd_table		= &sdm845_cpu_bwmon_reg_read_table,
	.volatile_table		= &msm8998_bwmon_reg_volatile_table,
	.reg_defaults		= sdm845_cpu_bwmon_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sdm845_cpu_bwmon_reg_defaults),
	/*
	 * Cache is necessary for using regmap fields with non-readable
	 * registers.
	 */
	.cache_type		= REGCACHE_MAPLE,
};

/* BWMON v5 */
static const struct reg_field sdm845_llcc_bwmon_reg_fields[] = {
	[F_GLOBAL_IRQ_CLEAR]	= {},
	[F_GLOBAL_IRQ_ENABLE]	= {},
	[F_IRQ_STATUS]		= REG_FIELD(BWMON_V5_IRQ_STATUS, 0, 3),
	[F_IRQ_CLEAR]		= REG_FIELD(BWMON_V5_IRQ_CLEAR, 0, 3),
	[F_IRQ_ENABLE]		= REG_FIELD(BWMON_V5_IRQ_ENABLE, 0, 3),
	/* F_ENABLE covers entire register to disable other features */
	[F_ENABLE]		= REG_FIELD(BWMON_V5_ENABLE, 0, 31),
	[F_CLEAR]		= REG_FIELD(BWMON_V5_CLEAR, 0, 1),
	[F_SAMPLE_WINDOW]	= REG_FIELD(BWMON_V5_SAMPLE_WINDOW, 0, 19),
	[F_THRESHOLD_HIGH]	= REG_FIELD(BWMON_V5_THRESHOLD_HIGH, 0, 11),
	[F_THRESHOLD_MED]	= REG_FIELD(BWMON_V5_THRESHOLD_MED, 0, 11),
	[F_THRESHOLD_LOW]	= REG_FIELD(BWMON_V5_THRESHOLD_LOW, 0, 11),
	[F_ZONE_ACTIONS_ZONE0]	= REG_FIELD(BWMON_V5_ZONE_ACTIONS, 0, 7),
	[F_ZONE_ACTIONS_ZONE1]	= REG_FIELD(BWMON_V5_ZONE_ACTIONS, 8, 15),
	[F_ZONE_ACTIONS_ZONE2]	= REG_FIELD(BWMON_V5_ZONE_ACTIONS, 16, 23),
	[F_ZONE_ACTIONS_ZONE3]	= REG_FIELD(BWMON_V5_ZONE_ACTIONS, 24, 31),
	[F_THRESHOLD_COUNT_ZONE0]	= REG_FIELD(BWMON_V5_THRESHOLD_COUNT, 0, 7),
	[F_THRESHOLD_COUNT_ZONE1]	= REG_FIELD(BWMON_V5_THRESHOLD_COUNT, 8, 15),
	[F_THRESHOLD_COUNT_ZONE2]	= REG_FIELD(BWMON_V5_THRESHOLD_COUNT, 16, 23),
	[F_THRESHOLD_COUNT_ZONE3]	= REG_FIELD(BWMON_V5_THRESHOLD_COUNT, 24, 31),
	[F_ZONE0_MAX]		= REG_FIELD(BWMON_V5_ZONE_MAX(0), 0, 11),
	[F_ZONE1_MAX]		= REG_FIELD(BWMON_V5_ZONE_MAX(1), 0, 11),
	[F_ZONE2_MAX]		= REG_FIELD(BWMON_V5_ZONE_MAX(2), 0, 11),
	[F_ZONE3_MAX]		= REG_FIELD(BWMON_V5_ZONE_MAX(3), 0, 11),
};

static const struct regmap_range sdm845_llcc_bwmon_reg_noread_ranges[] = {
	regmap_reg_range(BWMON_V5_IRQ_CLEAR, BWMON_V5_IRQ_CLEAR),
	regmap_reg_range(BWMON_V5_CLEAR, BWMON_V5_CLEAR),
};

static const struct regmap_access_table sdm845_llcc_bwmon_reg_read_table = {
	.no_ranges	= sdm845_llcc_bwmon_reg_noread_ranges,
	.n_no_ranges	= ARRAY_SIZE(sdm845_llcc_bwmon_reg_noread_ranges),
};

static const struct regmap_range sdm845_llcc_bwmon_reg_volatile_ranges[] = {
	regmap_reg_range(BWMON_V5_IRQ_STATUS, BWMON_V5_IRQ_STATUS),
	regmap_reg_range(BWMON_V5_ZONE_MAX(0), BWMON_V5_ZONE_MAX(3)),
};

static const struct regmap_access_table sdm845_llcc_bwmon_reg_volatile_table = {
	.yes_ranges	= sdm845_llcc_bwmon_reg_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(sdm845_llcc_bwmon_reg_volatile_ranges),
};

/*
 * Fill the cache for non-readable registers only as rest does not really
 * matter and can be read from the device.
 */
static const struct reg_default sdm845_llcc_bwmon_reg_defaults[] = {
	{ BWMON_V5_IRQ_CLEAR, 0x0 },
	{ BWMON_V5_CLEAR, 0x0 },
};

static const struct regmap_config sdm845_llcc_bwmon_regmap_cfg = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	/*
	 * No concurrent access expected - driver has one interrupt handler,
	 * regmap is not shared, no driver or user-space API.
	 */
	.disable_locking	= true,
	.rd_table		= &sdm845_llcc_bwmon_reg_read_table,
	.volatile_table		= &sdm845_llcc_bwmon_reg_volatile_table,
	.reg_defaults		= sdm845_llcc_bwmon_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sdm845_llcc_bwmon_reg_defaults),
	/*
	 * Cache is necessary for using regmap fields with non-readable
	 * registers.
	 */
	.cache_type		= REGCACHE_MAPLE,
};

static void bwmon_clear_counters(struct icc_bwmon *bwmon, bool clear_all)
{
	unsigned int val = BWMON_CLEAR_CLEAR;

	if (clear_all)
		val |= BWMON_CLEAR_CLEAR_ALL;
	/*
	 * Clear counters. The order and barriers are
	 * important. Quoting downstream Qualcomm msm-4.9 tree:
	 *
	 * The counter clear and IRQ clear bits are not in the same 4KB
	 * region. So, we need to make sure the counter clear is completed
	 * before we try to clear the IRQ or do any other counter operations.
	 */
	regmap_field_force_write(bwmon->regs[F_CLEAR], val);
	if (bwmon->data->quirks & BWMON_NEEDS_FORCE_CLEAR)
		regmap_field_force_write(bwmon->regs[F_CLEAR], 0);
}

static void bwmon_clear_irq(struct icc_bwmon *bwmon)
{
	struct regmap_field *global_irq_clr;

	if (bwmon->data->global_regmap_fields)
		global_irq_clr = bwmon->global_regs[F_GLOBAL_IRQ_CLEAR];
	else
		global_irq_clr = bwmon->regs[F_GLOBAL_IRQ_CLEAR];

	/*
	 * Clear zone and global interrupts. The order and barriers are
	 * important. Quoting downstream Qualcomm msm-4.9 tree:
	 *
	 * Synchronize the local interrupt clear in mon_irq_clear()
	 * with the global interrupt clear here. Otherwise, the CPU
	 * may reorder the two writes and clear the global interrupt
	 * before the local interrupt, causing the global interrupt
	 * to be retriggered by the local interrupt still being high.
	 *
	 * Similarly, because the global registers are in a different
	 * region than the local registers, we need to ensure any register
	 * writes to enable the monitor after this call are ordered with the
	 * clearing here so that local writes don't happen before the
	 * interrupt is cleared.
	 */
	regmap_field_force_write(bwmon->regs[F_IRQ_CLEAR], BWMON_IRQ_ENABLE_MASK);
	if (bwmon->data->quirks & BWMON_NEEDS_FORCE_CLEAR)
		regmap_field_force_write(bwmon->regs[F_IRQ_CLEAR], 0);
	if (bwmon->data->quirks & BWMON_HAS_GLOBAL_IRQ)
		regmap_field_force_write(global_irq_clr,
					 BWMON_V4_GLOBAL_IRQ_ENABLE_ENABLE);
}

static void bwmon_disable(struct icc_bwmon *bwmon)
{
	struct regmap_field *global_irq_en;

	if (bwmon->data->global_regmap_fields)
		global_irq_en = bwmon->global_regs[F_GLOBAL_IRQ_ENABLE];
	else
		global_irq_en = bwmon->regs[F_GLOBAL_IRQ_ENABLE];

	/* Disable interrupts. Strict ordering, see bwmon_clear_irq(). */
	if (bwmon->data->quirks & BWMON_HAS_GLOBAL_IRQ)
		regmap_field_write(global_irq_en, 0x0);
	regmap_field_write(bwmon->regs[F_IRQ_ENABLE], 0x0);

	/*
	 * Disable bwmon. Must happen before bwmon_clear_irq() to avoid spurious
	 * IRQ.
	 */
	regmap_field_write(bwmon->regs[F_ENABLE], 0x0);
}

static void bwmon_enable(struct icc_bwmon *bwmon, unsigned int irq_enable)
{
	struct regmap_field *global_irq_en;

	if (bwmon->data->global_regmap_fields)
		global_irq_en = bwmon->global_regs[F_GLOBAL_IRQ_ENABLE];
	else
		global_irq_en = bwmon->regs[F_GLOBAL_IRQ_ENABLE];

	/* Enable interrupts */
	if (bwmon->data->quirks & BWMON_HAS_GLOBAL_IRQ)
		regmap_field_write(global_irq_en,
				   BWMON_V4_GLOBAL_IRQ_ENABLE_ENABLE);

	regmap_field_write(bwmon->regs[F_IRQ_ENABLE], irq_enable);

	/* Enable bwmon */
	regmap_field_write(bwmon->regs[F_ENABLE], BWMON_ENABLE_ENABLE);
}

static unsigned int bwmon_kbps_to_count(struct icc_bwmon *bwmon,
					unsigned int kbps)
{
	return kbps / bwmon->data->count_unit_kb;
}

static void bwmon_set_threshold(struct icc_bwmon *bwmon,
				struct regmap_field *reg, unsigned int kbps)
{
	unsigned int thres;

	thres = mult_frac(bwmon_kbps_to_count(bwmon, kbps),
			  bwmon->data->sample_ms, MSEC_PER_SEC);
	regmap_field_write(reg, thres);
}

static void bwmon_start(struct icc_bwmon *bwmon)
{
	const struct icc_bwmon_data *data = bwmon->data;
	u32 bw_low = 0;
	int window;

	/* No need to check for errors, as this must have succeeded before. */
	dev_pm_opp_put(dev_pm_opp_find_bw_ceil(bwmon->dev, &bw_low, 0));

	bwmon_clear_counters(bwmon, true);

	window = mult_frac(bwmon->data->sample_ms, HW_TIMER_HZ, MSEC_PER_SEC);
	/* Maximum sampling window: 0xffffff for v4 and 0xfffff for v5 */
	regmap_field_write(bwmon->regs[F_SAMPLE_WINDOW], window);

	bwmon_set_threshold(bwmon, bwmon->regs[F_THRESHOLD_HIGH], bw_low);
	bwmon_set_threshold(bwmon, bwmon->regs[F_THRESHOLD_MED], bw_low);
	bwmon_set_threshold(bwmon, bwmon->regs[F_THRESHOLD_LOW], 0);

	regmap_field_write(bwmon->regs[F_THRESHOLD_COUNT_ZONE0],
			   BWMON_THRESHOLD_COUNT_ZONE0_DEFAULT);
	regmap_field_write(bwmon->regs[F_THRESHOLD_COUNT_ZONE1],
			   data->zone1_thres_count);
	regmap_field_write(bwmon->regs[F_THRESHOLD_COUNT_ZONE2],
			   BWMON_THRESHOLD_COUNT_ZONE2_DEFAULT);
	regmap_field_write(bwmon->regs[F_THRESHOLD_COUNT_ZONE3],
			   data->zone3_thres_count);

	regmap_field_write(bwmon->regs[F_ZONE_ACTIONS_ZONE0],
			   BWMON_ZONE_ACTIONS_ZONE0);
	regmap_field_write(bwmon->regs[F_ZONE_ACTIONS_ZONE1],
			   BWMON_ZONE_ACTIONS_ZONE1);
	regmap_field_write(bwmon->regs[F_ZONE_ACTIONS_ZONE2],
			   BWMON_ZONE_ACTIONS_ZONE2);
	regmap_field_write(bwmon->regs[F_ZONE_ACTIONS_ZONE3],
			   BWMON_ZONE_ACTIONS_ZONE3);

	bwmon_clear_irq(bwmon);
	bwmon_enable(bwmon, BWMON_IRQ_ENABLE_MASK);
}

static irqreturn_t bwmon_intr(int irq, void *dev_id)
{
	struct icc_bwmon *bwmon = dev_id;
	unsigned int status, max;
	int zone;

	if (regmap_field_read(bwmon->regs[F_IRQ_STATUS], &status))
		return IRQ_NONE;

	status &= BWMON_IRQ_ENABLE_MASK;
	if (!status) {
		/*
		 * Only zone 1 and zone 3 interrupts are enabled but zone 2
		 * threshold could be hit and trigger interrupt even if not
		 * enabled.
		 * Such spurious interrupt might come with valuable max count or
		 * not, so solution would be to always check all
		 * BWMON_ZONE_MAX() registers to find the highest value.
		 * Such case is currently ignored.
		 */
		return IRQ_NONE;
	}

	bwmon_disable(bwmon);

	zone = get_bitmask_order(status) - 1;
	/*
	 * Zone max bytes count register returns count units within sampling
	 * window.  Downstream kernel for BWMONv4 (called BWMON type 2 in
	 * downstream) always increments the max bytes count by one.
	 */
	if (regmap_field_read(bwmon->regs[F_ZONE0_MAX + zone], &max))
		return IRQ_NONE;

	max += 1;
	max *= bwmon->data->count_unit_kb;
	bwmon->target_kbps = mult_frac(max, MSEC_PER_SEC, bwmon->data->sample_ms);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t bwmon_intr_thread(int irq, void *dev_id)
{
	struct icc_bwmon *bwmon = dev_id;
	unsigned int irq_enable = 0;
	struct dev_pm_opp *opp, *target_opp;
	unsigned int bw_kbps, up_kbps, down_kbps;

	bw_kbps = bwmon->target_kbps;

	target_opp = dev_pm_opp_find_bw_ceil(bwmon->dev, &bw_kbps, 0);
	if (IS_ERR(target_opp) && PTR_ERR(target_opp) == -ERANGE)
		target_opp = dev_pm_opp_find_bw_floor(bwmon->dev, &bw_kbps, 0);

	bwmon->target_kbps = bw_kbps;

	bw_kbps--;
	opp = dev_pm_opp_find_bw_floor(bwmon->dev, &bw_kbps, 0);
	if (IS_ERR(opp) && PTR_ERR(opp) == -ERANGE)
		down_kbps = bwmon->target_kbps;
	else
		down_kbps = bw_kbps;

	up_kbps = bwmon->target_kbps + 1;

	if (bwmon->target_kbps >= bwmon->max_bw_kbps)
		irq_enable = BIT(1);
	else if (bwmon->target_kbps <= bwmon->min_bw_kbps)
		irq_enable = BIT(3);
	else
		irq_enable = BWMON_IRQ_ENABLE_MASK;

	bwmon_set_threshold(bwmon, bwmon->regs[F_THRESHOLD_HIGH],
			    up_kbps);
	bwmon_set_threshold(bwmon, bwmon->regs[F_THRESHOLD_MED],
			    down_kbps);
	bwmon_clear_counters(bwmon, false);
	bwmon_clear_irq(bwmon);
	bwmon_enable(bwmon, irq_enable);

	if (bwmon->target_kbps == bwmon->current_kbps)
		goto out;

	dev_pm_opp_set_opp(bwmon->dev, target_opp);
	bwmon->current_kbps = bwmon->target_kbps;

out:
	dev_pm_opp_put(target_opp);
	if (!IS_ERR(opp))
		dev_pm_opp_put(opp);

	return IRQ_HANDLED;
}

static int bwmon_init_regmap(struct platform_device *pdev,
			     struct icc_bwmon *bwmon)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *map;
	int ret;

	/* Map the monitor base */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "failed to map bwmon registers\n");

	map = devm_regmap_init_mmio(dev, base, bwmon->data->regmap_cfg);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map),
				     "failed to initialize regmap\n");

	BUILD_BUG_ON(ARRAY_SIZE(msm8998_bwmon_global_reg_fields) != F_NUM_GLOBAL_FIELDS);
	BUILD_BUG_ON(ARRAY_SIZE(msm8998_bwmon_reg_fields) != F_NUM_FIELDS);
	BUILD_BUG_ON(ARRAY_SIZE(sdm845_cpu_bwmon_reg_fields) != F_NUM_FIELDS);
	BUILD_BUG_ON(ARRAY_SIZE(sdm845_llcc_bwmon_reg_fields) != F_NUM_FIELDS);

	ret = devm_regmap_field_bulk_alloc(dev, map, bwmon->regs,
					   bwmon->data->regmap_fields,
					   F_NUM_FIELDS);
	if (ret)
		return ret;

	if (bwmon->data->global_regmap_cfg) {
		/* Map the global base, if separate */
		base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(base))
			return dev_err_probe(dev, PTR_ERR(base),
					     "failed to map bwmon global registers\n");

		map = devm_regmap_init_mmio(dev, base, bwmon->data->global_regmap_cfg);
		if (IS_ERR(map))
			return dev_err_probe(dev, PTR_ERR(map),
					     "failed to initialize global regmap\n");

		ret = devm_regmap_field_bulk_alloc(dev, map, bwmon->global_regs,
						   bwmon->data->global_regmap_fields,
						   F_NUM_GLOBAL_FIELDS);
	}

	return ret;
}

static int bwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dev_pm_opp *opp;
	struct icc_bwmon *bwmon;
	int ret;

	bwmon = devm_kzalloc(dev, sizeof(*bwmon), GFP_KERNEL);
	if (!bwmon)
		return -ENOMEM;

	bwmon->data = of_device_get_match_data(dev);

	ret = bwmon_init_regmap(pdev, bwmon);
	if (ret)
		return ret;

	bwmon->irq = platform_get_irq(pdev, 0);
	if (bwmon->irq < 0)
		return bwmon->irq;

	ret = devm_pm_opp_of_add_table(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add OPP table\n");

	bwmon->max_bw_kbps = UINT_MAX;
	opp = dev_pm_opp_find_bw_floor(dev, &bwmon->max_bw_kbps, 0);
	if (IS_ERR(opp))
		return dev_err_probe(dev, PTR_ERR(opp), "failed to find max peak bandwidth\n");
	dev_pm_opp_put(opp);

	bwmon->min_bw_kbps = 0;
	opp = dev_pm_opp_find_bw_ceil(dev, &bwmon->min_bw_kbps, 0);
	if (IS_ERR(opp))
		return dev_err_probe(dev, PTR_ERR(opp), "failed to find min peak bandwidth\n");
	dev_pm_opp_put(opp);

	bwmon->dev = dev;

	bwmon_disable(bwmon);

	/*
	 * SoCs with multiple cpu-bwmon instances can end up using a shared interrupt
	 * line. Using the devm_ variant might result in the IRQ handler being executed
	 * after bwmon_disable in bwmon_remove()
	 */
	ret = request_threaded_irq(bwmon->irq, bwmon_intr, bwmon_intr_thread,
				   IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), bwmon);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	platform_set_drvdata(pdev, bwmon);
	bwmon_start(bwmon);

	return 0;
}

static void bwmon_remove(struct platform_device *pdev)
{
	struct icc_bwmon *bwmon = platform_get_drvdata(pdev);

	bwmon_disable(bwmon);
	free_irq(bwmon->irq, bwmon);
}

static const struct icc_bwmon_data msm8998_bwmon_data = {
	.sample_ms = 4,
	.count_unit_kb = 1024,
	.zone1_thres_count = 16,
	.zone3_thres_count = 1,
	.quirks = BWMON_HAS_GLOBAL_IRQ,
	.regmap_fields = msm8998_bwmon_reg_fields,
	.regmap_cfg = &msm8998_bwmon_regmap_cfg,
	.global_regmap_fields = msm8998_bwmon_global_reg_fields,
	.global_regmap_cfg = &msm8998_bwmon_global_regmap_cfg,
};

static const struct icc_bwmon_data sdm845_cpu_bwmon_data = {
	.sample_ms = 4,
	.count_unit_kb = 64,
	.zone1_thres_count = 16,
	.zone3_thres_count = 1,
	.quirks = BWMON_HAS_GLOBAL_IRQ,
	.regmap_fields = sdm845_cpu_bwmon_reg_fields,
	.regmap_cfg = &sdm845_cpu_bwmon_regmap_cfg,
};

static const struct icc_bwmon_data sdm845_llcc_bwmon_data = {
	.sample_ms = 4,
	.count_unit_kb = 1024,
	.zone1_thres_count = 16,
	.zone3_thres_count = 1,
	.regmap_fields = sdm845_llcc_bwmon_reg_fields,
	.regmap_cfg = &sdm845_llcc_bwmon_regmap_cfg,
};

static const struct icc_bwmon_data sc7280_llcc_bwmon_data = {
	.sample_ms = 4,
	.count_unit_kb = 64,
	.zone1_thres_count = 16,
	.zone3_thres_count = 1,
	.quirks = BWMON_NEEDS_FORCE_CLEAR,
	.regmap_fields = sdm845_llcc_bwmon_reg_fields,
	.regmap_cfg = &sdm845_llcc_bwmon_regmap_cfg,
};

static const struct of_device_id bwmon_of_match[] = {
	/* BWMONv4, separate monitor and global register spaces */
	{ .compatible = "qcom,msm8998-bwmon", .data = &msm8998_bwmon_data },
	/* BWMONv4, unified register space */
	{ .compatible = "qcom,sdm845-bwmon", .data = &sdm845_cpu_bwmon_data },
	/* BWMONv5 */
	{ .compatible = "qcom,sdm845-llcc-bwmon", .data = &sdm845_llcc_bwmon_data },
	{ .compatible = "qcom,sc7280-llcc-bwmon", .data = &sc7280_llcc_bwmon_data },

	/* Compatibles kept for legacy reasons */
	{ .compatible = "qcom,sc7280-cpu-bwmon", .data = &sdm845_cpu_bwmon_data },
	{ .compatible = "qcom,sc8280xp-cpu-bwmon", .data = &sdm845_cpu_bwmon_data },
	{ .compatible = "qcom,sm8550-cpu-bwmon", .data = &sdm845_cpu_bwmon_data },
	{}
};
MODULE_DEVICE_TABLE(of, bwmon_of_match);

static struct platform_driver bwmon_driver = {
	.probe = bwmon_probe,
	.remove_new = bwmon_remove,
	.driver = {
		.name = "qcom-bwmon",
		.of_match_table = bwmon_of_match,
	},
};
module_platform_driver(bwmon_driver);

MODULE_AUTHOR("Krzysztof Kozlowski <krzysztof.kozlowski@linaro.org>");
MODULE_DESCRIPTION("QCOM BWMON driver");
MODULE_LICENSE("GPL");
