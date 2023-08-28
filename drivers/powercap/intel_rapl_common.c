// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common code for Intel Running Average Power Limit (RAPL) support.
 * Copyright (c) 2019, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/cpu.h>
#include <linux/powercap.h>
#include <linux/suspend.h>
#include <linux/intel_rapl.h>
#include <linux/processor.h>
#include <linux/platform_device.h>

#include <asm/iosf_mbi.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

/* bitmasks for RAPL MSRs, used by primitive access functions */
#define ENERGY_STATUS_MASK      0xffffffff

#define POWER_LIMIT1_MASK       0x7FFF
#define POWER_LIMIT1_ENABLE     BIT(15)
#define POWER_LIMIT1_CLAMP      BIT(16)

#define POWER_LIMIT2_MASK       (0x7FFFULL<<32)
#define POWER_LIMIT2_ENABLE     BIT_ULL(47)
#define POWER_LIMIT2_CLAMP      BIT_ULL(48)
#define POWER_HIGH_LOCK         BIT_ULL(63)
#define POWER_LOW_LOCK          BIT(31)

#define POWER_LIMIT4_MASK		0x1FFF

#define TIME_WINDOW1_MASK       (0x7FULL<<17)
#define TIME_WINDOW2_MASK       (0x7FULL<<49)

#define POWER_UNIT_OFFSET	0
#define POWER_UNIT_MASK		0x0F

#define ENERGY_UNIT_OFFSET	0x08
#define ENERGY_UNIT_MASK	0x1F00

#define TIME_UNIT_OFFSET	0x10
#define TIME_UNIT_MASK		0xF0000

#define POWER_INFO_MAX_MASK     (0x7fffULL<<32)
#define POWER_INFO_MIN_MASK     (0x7fffULL<<16)
#define POWER_INFO_MAX_TIME_WIN_MASK     (0x3fULL<<48)
#define POWER_INFO_THERMAL_SPEC_MASK     0x7fff

#define PERF_STATUS_THROTTLE_TIME_MASK 0xffffffff
#define PP_POLICY_MASK         0x1F

/*
 * SPR has different layout for Psys Domain PowerLimit registers.
 * There are 17 bits of PL1 and PL2 instead of 15 bits.
 * The Enable bits and TimeWindow bits are also shifted as a result.
 */
#define PSYS_POWER_LIMIT1_MASK       0x1FFFF
#define PSYS_POWER_LIMIT1_ENABLE     BIT(17)

#define PSYS_POWER_LIMIT2_MASK       (0x1FFFFULL<<32)
#define PSYS_POWER_LIMIT2_ENABLE     BIT_ULL(49)

#define PSYS_TIME_WINDOW1_MASK       (0x7FULL<<19)
#define PSYS_TIME_WINDOW2_MASK       (0x7FULL<<51)

/* bitmasks for RAPL TPMI, used by primitive access functions */
#define TPMI_POWER_LIMIT_MASK	0x3FFFF
#define TPMI_POWER_LIMIT_ENABLE	BIT_ULL(62)
#define TPMI_TIME_WINDOW_MASK	(0x7FULL<<18)
#define TPMI_INFO_SPEC_MASK	0x3FFFF
#define TPMI_INFO_MIN_MASK	(0x3FFFFULL << 18)
#define TPMI_INFO_MAX_MASK	(0x3FFFFULL << 36)
#define TPMI_INFO_MAX_TIME_WIN_MASK	(0x7FULL << 54)

/* Non HW constants */
#define RAPL_PRIMITIVE_DERIVED       BIT(1)	/* not from raw data */
#define RAPL_PRIMITIVE_DUMMY         BIT(2)

#define TIME_WINDOW_MAX_MSEC 40000
#define TIME_WINDOW_MIN_MSEC 250
#define ENERGY_UNIT_SCALE    1000	/* scale from driver unit to powercap unit */
enum unit_type {
	ARBITRARY_UNIT,		/* no translation */
	POWER_UNIT,
	ENERGY_UNIT,
	TIME_UNIT,
};

/* per domain data, some are optional */
#define NR_RAW_PRIMITIVES (NR_RAPL_PRIMITIVES - 2)

#define	DOMAIN_STATE_INACTIVE           BIT(0)
#define	DOMAIN_STATE_POWER_LIMIT_SET    BIT(1)

static const char *pl_names[NR_POWER_LIMITS] = {
	[POWER_LIMIT1] = "long_term",
	[POWER_LIMIT2] = "short_term",
	[POWER_LIMIT4] = "peak_power",
};

enum pl_prims {
	PL_ENABLE,
	PL_CLAMP,
	PL_LIMIT,
	PL_TIME_WINDOW,
	PL_MAX_POWER,
	PL_LOCK,
};

static bool is_pl_valid(struct rapl_domain *rd, int pl)
{
	if (pl < POWER_LIMIT1 || pl > POWER_LIMIT4)
		return false;
	return rd->rpl[pl].name ? true : false;
}

static int get_pl_lock_prim(struct rapl_domain *rd, int pl)
{
	if (rd->rp->priv->type == RAPL_IF_TPMI) {
		if (pl == POWER_LIMIT1)
			return PL1_LOCK;
		if (pl == POWER_LIMIT2)
			return PL2_LOCK;
		if (pl == POWER_LIMIT4)
			return PL4_LOCK;
	}

	/* MSR/MMIO Interface doesn't have Lock bit for PL4 */
	if (pl == POWER_LIMIT4)
		return -EINVAL;

	/*
	 * Power Limit register that supports two power limits has a different
	 * bit position for the Lock bit.
	 */
	if (rd->rp->priv->limits[rd->id] & BIT(POWER_LIMIT2))
		return FW_HIGH_LOCK;
	return FW_LOCK;
}

static int get_pl_prim(struct rapl_domain *rd, int pl, enum pl_prims prim)
{
	switch (pl) {
	case POWER_LIMIT1:
		if (prim == PL_ENABLE)
			return PL1_ENABLE;
		if (prim == PL_CLAMP && rd->rp->priv->type != RAPL_IF_TPMI)
			return PL1_CLAMP;
		if (prim == PL_LIMIT)
			return POWER_LIMIT1;
		if (prim == PL_TIME_WINDOW)
			return TIME_WINDOW1;
		if (prim == PL_MAX_POWER)
			return THERMAL_SPEC_POWER;
		if (prim == PL_LOCK)
			return get_pl_lock_prim(rd, pl);
		return -EINVAL;
	case POWER_LIMIT2:
		if (prim == PL_ENABLE)
			return PL2_ENABLE;
		if (prim == PL_CLAMP && rd->rp->priv->type != RAPL_IF_TPMI)
			return PL2_CLAMP;
		if (prim == PL_LIMIT)
			return POWER_LIMIT2;
		if (prim == PL_TIME_WINDOW)
			return TIME_WINDOW2;
		if (prim == PL_MAX_POWER)
			return MAX_POWER;
		if (prim == PL_LOCK)
			return get_pl_lock_prim(rd, pl);
		return -EINVAL;
	case POWER_LIMIT4:
		if (prim == PL_LIMIT)
			return POWER_LIMIT4;
		if (prim == PL_ENABLE)
			return PL4_ENABLE;
		/* PL4 would be around two times PL2, use same prim as PL2. */
		if (prim == PL_MAX_POWER)
			return MAX_POWER;
		if (prim == PL_LOCK)
			return get_pl_lock_prim(rd, pl);
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

#define power_zone_to_rapl_domain(_zone) \
	container_of(_zone, struct rapl_domain, power_zone)

struct rapl_defaults {
	u8 floor_freq_reg_addr;
	int (*check_unit)(struct rapl_domain *rd);
	void (*set_floor_freq)(struct rapl_domain *rd, bool mode);
	u64 (*compute_time_window)(struct rapl_domain *rd, u64 val,
				    bool to_raw);
	unsigned int dram_domain_energy_unit;
	unsigned int psys_domain_energy_unit;
	bool spr_psys_bits;
};
static struct rapl_defaults *defaults_msr;
static const struct rapl_defaults defaults_tpmi;

static struct rapl_defaults *get_defaults(struct rapl_package *rp)
{
	return rp->priv->defaults;
}

/* Sideband MBI registers */
#define IOSF_CPU_POWER_BUDGET_CTL_BYT (0x2)
#define IOSF_CPU_POWER_BUDGET_CTL_TNG (0xdf)

#define PACKAGE_PLN_INT_SAVED   BIT(0)
#define MAX_PRIM_NAME (32)

/* per domain data. used to describe individual knobs such that access function
 * can be consolidated into one instead of many inline functions.
 */
struct rapl_primitive_info {
	const char *name;
	u64 mask;
	int shift;
	enum rapl_domain_reg_id id;
	enum unit_type unit;
	u32 flag;
};

#define PRIMITIVE_INFO_INIT(p, m, s, i, u, f) {	\
		.name = #p,			\
		.mask = m,			\
		.shift = s,			\
		.id = i,			\
		.unit = u,			\
		.flag = f			\
	}

static void rapl_init_domains(struct rapl_package *rp);
static int rapl_read_data_raw(struct rapl_domain *rd,
			      enum rapl_primitives prim,
			      bool xlate, u64 *data);
static int rapl_write_data_raw(struct rapl_domain *rd,
			       enum rapl_primitives prim,
			       unsigned long long value);
static int rapl_read_pl_data(struct rapl_domain *rd, int pl,
			      enum pl_prims pl_prim,
			      bool xlate, u64 *data);
static int rapl_write_pl_data(struct rapl_domain *rd, int pl,
			       enum pl_prims pl_prim,
			       unsigned long long value);
static u64 rapl_unit_xlate(struct rapl_domain *rd,
			   enum unit_type type, u64 value, int to_raw);
static void package_power_limit_irq_save(struct rapl_package *rp);

static LIST_HEAD(rapl_packages);	/* guarded by CPU hotplug lock */

static const char *const rapl_domain_names[] = {
	"package",
	"core",
	"uncore",
	"dram",
	"psys",
};

static int get_energy_counter(struct powercap_zone *power_zone,
			      u64 *energy_raw)
{
	struct rapl_domain *rd;
	u64 energy_now;

	/* prevent CPU hotplug, make sure the RAPL domain does not go
	 * away while reading the counter.
	 */
	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);

	if (!rapl_read_data_raw(rd, ENERGY_COUNTER, true, &energy_now)) {
		*energy_raw = energy_now;
		cpus_read_unlock();

		return 0;
	}
	cpus_read_unlock();

	return -EIO;
}

static int get_max_energy_counter(struct powercap_zone *pcd_dev, u64 *energy)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(pcd_dev);

	*energy = rapl_unit_xlate(rd, ENERGY_UNIT, ENERGY_STATUS_MASK, 0);
	return 0;
}

static int release_zone(struct powercap_zone *power_zone)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);
	struct rapl_package *rp = rd->rp;

	/* package zone is the last zone of a package, we can free
	 * memory here since all children has been unregistered.
	 */
	if (rd->id == RAPL_DOMAIN_PACKAGE) {
		kfree(rd);
		rp->domains = NULL;
	}

	return 0;

}

static int find_nr_power_limit(struct rapl_domain *rd)
{
	int i, nr_pl = 0;

	for (i = 0; i < NR_POWER_LIMITS; i++) {
		if (is_pl_valid(rd, i))
			nr_pl++;
	}

	return nr_pl;
}

static int set_domain_enable(struct powercap_zone *power_zone, bool mode)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);
	struct rapl_defaults *defaults = get_defaults(rd->rp);
	int ret;

	cpus_read_lock();
	ret = rapl_write_pl_data(rd, POWER_LIMIT1, PL_ENABLE, mode);
	if (!ret && defaults->set_floor_freq)
		defaults->set_floor_freq(rd, mode);
	cpus_read_unlock();

	return ret;
}

static int get_domain_enable(struct powercap_zone *power_zone, bool *mode)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);
	u64 val;
	int ret;

	if (rd->rpl[POWER_LIMIT1].locked) {
		*mode = false;
		return 0;
	}
	cpus_read_lock();
	ret = rapl_read_pl_data(rd, POWER_LIMIT1, PL_ENABLE, true, &val);
	if (!ret)
		*mode = val;
	cpus_read_unlock();

	return ret;
}

/* per RAPL domain ops, in the order of rapl_domain_type */
static const struct powercap_zone_ops zone_ops[] = {
	/* RAPL_DOMAIN_PACKAGE */
	{
	 .get_energy_uj = get_energy_counter,
	 .get_max_energy_range_uj = get_max_energy_counter,
	 .release = release_zone,
	 .set_enable = set_domain_enable,
	 .get_enable = get_domain_enable,
	 },
	/* RAPL_DOMAIN_PP0 */
	{
	 .get_energy_uj = get_energy_counter,
	 .get_max_energy_range_uj = get_max_energy_counter,
	 .release = release_zone,
	 .set_enable = set_domain_enable,
	 .get_enable = get_domain_enable,
	 },
	/* RAPL_DOMAIN_PP1 */
	{
	 .get_energy_uj = get_energy_counter,
	 .get_max_energy_range_uj = get_max_energy_counter,
	 .release = release_zone,
	 .set_enable = set_domain_enable,
	 .get_enable = get_domain_enable,
	 },
	/* RAPL_DOMAIN_DRAM */
	{
	 .get_energy_uj = get_energy_counter,
	 .get_max_energy_range_uj = get_max_energy_counter,
	 .release = release_zone,
	 .set_enable = set_domain_enable,
	 .get_enable = get_domain_enable,
	 },
	/* RAPL_DOMAIN_PLATFORM */
	{
	 .get_energy_uj = get_energy_counter,
	 .get_max_energy_range_uj = get_max_energy_counter,
	 .release = release_zone,
	 .set_enable = set_domain_enable,
	 .get_enable = get_domain_enable,
	 },
};

/*
 * Constraint index used by powercap can be different than power limit (PL)
 * index in that some  PLs maybe missing due to non-existent MSRs. So we
 * need to convert here by finding the valid PLs only (name populated).
 */
static int contraint_to_pl(struct rapl_domain *rd, int cid)
{
	int i, j;

	for (i = POWER_LIMIT1, j = 0; i < NR_POWER_LIMITS; i++) {
		if (is_pl_valid(rd, i) && j++ == cid) {
			pr_debug("%s: index %d\n", __func__, i);
			return i;
		}
	}
	pr_err("Cannot find matching power limit for constraint %d\n", cid);

	return -EINVAL;
}

static int set_power_limit(struct powercap_zone *power_zone, int cid,
			   u64 power_limit)
{
	struct rapl_domain *rd;
	struct rapl_package *rp;
	int ret = 0;
	int id;

	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	rp = rd->rp;

	ret = rapl_write_pl_data(rd, id, PL_LIMIT, power_limit);
	if (!ret)
		package_power_limit_irq_save(rp);
	cpus_read_unlock();
	return ret;
}

static int get_current_power_limit(struct powercap_zone *power_zone, int cid,
				   u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int ret = 0;
	int id;

	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);

	ret = rapl_read_pl_data(rd, id, PL_LIMIT, true, &val);
	if (!ret)
		*data = val;

	cpus_read_unlock();

	return ret;
}

static int set_time_window(struct powercap_zone *power_zone, int cid,
			   u64 window)
{
	struct rapl_domain *rd;
	int ret = 0;
	int id;

	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);

	ret = rapl_write_pl_data(rd, id, PL_TIME_WINDOW, window);

	cpus_read_unlock();
	return ret;
}

static int get_time_window(struct powercap_zone *power_zone, int cid,
			   u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int ret = 0;
	int id;

	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);

	ret = rapl_read_pl_data(rd, id, PL_TIME_WINDOW, true, &val);
	if (!ret)
		*data = val;

	cpus_read_unlock();

	return ret;
}

static const char *get_constraint_name(struct powercap_zone *power_zone,
				       int cid)
{
	struct rapl_domain *rd;
	int id;

	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id >= 0)
		return rd->rpl[id].name;

	return NULL;
}

static int get_max_power(struct powercap_zone *power_zone, int cid, u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int ret = 0;
	int id;

	cpus_read_lock();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);

	ret = rapl_read_pl_data(rd, id, PL_MAX_POWER, true, &val);
	if (!ret)
		*data = val;

	/* As a generalization rule, PL4 would be around two times PL2. */
	if (id == POWER_LIMIT4)
		*data = *data * 2;

	cpus_read_unlock();

	return ret;
}

static const struct powercap_zone_constraint_ops constraint_ops = {
	.set_power_limit_uw = set_power_limit,
	.get_power_limit_uw = get_current_power_limit,
	.set_time_window_us = set_time_window,
	.get_time_window_us = get_time_window,
	.get_max_power_uw = get_max_power,
	.get_name = get_constraint_name,
};

/* Return the id used for read_raw/write_raw callback */
static int get_rid(struct rapl_package *rp)
{
	return rp->lead_cpu >= 0 ? rp->lead_cpu : rp->id;
}

/* called after domain detection and package level data are set */
static void rapl_init_domains(struct rapl_package *rp)
{
	enum rapl_domain_type i;
	enum rapl_domain_reg_id j;
	struct rapl_domain *rd = rp->domains;

	for (i = 0; i < RAPL_DOMAIN_MAX; i++) {
		unsigned int mask = rp->domain_map & (1 << i);
		int t;

		if (!mask)
			continue;

		rd->rp = rp;

		if (i == RAPL_DOMAIN_PLATFORM && rp->id > 0) {
			snprintf(rd->name, RAPL_DOMAIN_NAME_LENGTH, "psys-%d",
				rp->lead_cpu >= 0 ? topology_physical_package_id(rp->lead_cpu) :
				rp->id);
		} else {
			snprintf(rd->name, RAPL_DOMAIN_NAME_LENGTH, "%s",
				rapl_domain_names[i]);
		}

		rd->id = i;

		/* PL1 is supported by default */
		rp->priv->limits[i] |= BIT(POWER_LIMIT1);

		for (t = POWER_LIMIT1; t < NR_POWER_LIMITS; t++) {
			if (rp->priv->limits[i] & BIT(t))
				rd->rpl[t].name = pl_names[t];
		}

		for (j = 0; j < RAPL_DOMAIN_REG_MAX; j++)
			rd->regs[j] = rp->priv->regs[i][j];

		rd++;
	}
}

static u64 rapl_unit_xlate(struct rapl_domain *rd, enum unit_type type,
			   u64 value, int to_raw)
{
	u64 units = 1;
	struct rapl_defaults *defaults = get_defaults(rd->rp);
	u64 scale = 1;

	switch (type) {
	case POWER_UNIT:
		units = rd->power_unit;
		break;
	case ENERGY_UNIT:
		scale = ENERGY_UNIT_SCALE;
		units = rd->energy_unit;
		break;
	case TIME_UNIT:
		return defaults->compute_time_window(rd, value, to_raw);
	case ARBITRARY_UNIT:
	default:
		return value;
	}

	if (to_raw)
		return div64_u64(value, units) * scale;

	value *= units;

	return div64_u64(value, scale);
}

/* RAPL primitives for MSR and MMIO I/F */
static struct rapl_primitive_info rpi_msr[NR_RAPL_PRIMITIVES] = {
	/* name, mask, shift, msr index, unit divisor */
	[POWER_LIMIT1] = PRIMITIVE_INFO_INIT(POWER_LIMIT1, POWER_LIMIT1_MASK, 0,
			    RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT2] = PRIMITIVE_INFO_INIT(POWER_LIMIT2, POWER_LIMIT2_MASK, 32,
			    RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT4] = PRIMITIVE_INFO_INIT(POWER_LIMIT4, POWER_LIMIT4_MASK, 0,
				RAPL_DOMAIN_REG_PL4, POWER_UNIT, 0),
	[ENERGY_COUNTER] = PRIMITIVE_INFO_INIT(ENERGY_COUNTER, ENERGY_STATUS_MASK, 0,
			    RAPL_DOMAIN_REG_STATUS, ENERGY_UNIT, 0),
	[FW_LOCK] = PRIMITIVE_INFO_INIT(FW_LOCK, POWER_LOW_LOCK, 31,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[FW_HIGH_LOCK] = PRIMITIVE_INFO_INIT(FW_LOCK, POWER_HIGH_LOCK, 63,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_ENABLE] = PRIMITIVE_INFO_INIT(PL1_ENABLE, POWER_LIMIT1_ENABLE, 15,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL1_CLAMP] = PRIMITIVE_INFO_INIT(PL1_CLAMP, POWER_LIMIT1_CLAMP, 16,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_ENABLE] = PRIMITIVE_INFO_INIT(PL2_ENABLE, POWER_LIMIT2_ENABLE, 47,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_CLAMP] = PRIMITIVE_INFO_INIT(PL2_CLAMP, POWER_LIMIT2_CLAMP, 48,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL4_ENABLE] = PRIMITIVE_INFO_INIT(PL4_ENABLE, POWER_LIMIT4_MASK, 0,
				RAPL_DOMAIN_REG_PL4, ARBITRARY_UNIT, 0),
	[TIME_WINDOW1] = PRIMITIVE_INFO_INIT(TIME_WINDOW1, TIME_WINDOW1_MASK, 17,
			    RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[TIME_WINDOW2] = PRIMITIVE_INFO_INIT(TIME_WINDOW2, TIME_WINDOW2_MASK, 49,
			    RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[THERMAL_SPEC_POWER] = PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER, POWER_INFO_THERMAL_SPEC_MASK,
			    0, RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_POWER] = PRIMITIVE_INFO_INIT(MAX_POWER, POWER_INFO_MAX_MASK, 32,
			    RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MIN_POWER] = PRIMITIVE_INFO_INIT(MIN_POWER, POWER_INFO_MIN_MASK, 16,
			    RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_TIME_WINDOW] = PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW, POWER_INFO_MAX_TIME_WIN_MASK, 48,
			    RAPL_DOMAIN_REG_INFO, TIME_UNIT, 0),
	[THROTTLED_TIME] = PRIMITIVE_INFO_INIT(THROTTLED_TIME, PERF_STATUS_THROTTLE_TIME_MASK, 0,
			    RAPL_DOMAIN_REG_PERF, TIME_UNIT, 0),
	[PRIORITY_LEVEL] = PRIMITIVE_INFO_INIT(PRIORITY_LEVEL, PP_POLICY_MASK, 0,
			    RAPL_DOMAIN_REG_POLICY, ARBITRARY_UNIT, 0),
	[PSYS_POWER_LIMIT1] = PRIMITIVE_INFO_INIT(PSYS_POWER_LIMIT1, PSYS_POWER_LIMIT1_MASK, 0,
			    RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[PSYS_POWER_LIMIT2] = PRIMITIVE_INFO_INIT(PSYS_POWER_LIMIT2, PSYS_POWER_LIMIT2_MASK, 32,
			    RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[PSYS_PL1_ENABLE] = PRIMITIVE_INFO_INIT(PSYS_PL1_ENABLE, PSYS_POWER_LIMIT1_ENABLE, 17,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PSYS_PL2_ENABLE] = PRIMITIVE_INFO_INIT(PSYS_PL2_ENABLE, PSYS_POWER_LIMIT2_ENABLE, 49,
			    RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PSYS_TIME_WINDOW1] = PRIMITIVE_INFO_INIT(PSYS_TIME_WINDOW1, PSYS_TIME_WINDOW1_MASK, 19,
			    RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[PSYS_TIME_WINDOW2] = PRIMITIVE_INFO_INIT(PSYS_TIME_WINDOW2, PSYS_TIME_WINDOW2_MASK, 51,
			    RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	/* non-hardware */
	[AVERAGE_POWER] = PRIMITIVE_INFO_INIT(AVERAGE_POWER, 0, 0, 0, POWER_UNIT,
			    RAPL_PRIMITIVE_DERIVED),
};

/* RAPL primitives for TPMI I/F */
static struct rapl_primitive_info rpi_tpmi[NR_RAPL_PRIMITIVES] = {
	/* name, mask, shift, msr index, unit divisor */
	[POWER_LIMIT1] = PRIMITIVE_INFO_INIT(POWER_LIMIT1, TPMI_POWER_LIMIT_MASK, 0,
		RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	[POWER_LIMIT2] = PRIMITIVE_INFO_INIT(POWER_LIMIT2, TPMI_POWER_LIMIT_MASK, 0,
		RAPL_DOMAIN_REG_PL2, POWER_UNIT, 0),
	[POWER_LIMIT4] = PRIMITIVE_INFO_INIT(POWER_LIMIT4, TPMI_POWER_LIMIT_MASK, 0,
		RAPL_DOMAIN_REG_PL4, POWER_UNIT, 0),
	[ENERGY_COUNTER] = PRIMITIVE_INFO_INIT(ENERGY_COUNTER, ENERGY_STATUS_MASK, 0,
		RAPL_DOMAIN_REG_STATUS, ENERGY_UNIT, 0),
	[PL1_LOCK] = PRIMITIVE_INFO_INIT(PL1_LOCK, POWER_HIGH_LOCK, 63,
		RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_LOCK] = PRIMITIVE_INFO_INIT(PL2_LOCK, POWER_HIGH_LOCK, 63,
		RAPL_DOMAIN_REG_PL2, ARBITRARY_UNIT, 0),
	[PL4_LOCK] = PRIMITIVE_INFO_INIT(PL4_LOCK, POWER_HIGH_LOCK, 63,
		RAPL_DOMAIN_REG_PL4, ARBITRARY_UNIT, 0),
	[PL1_ENABLE] = PRIMITIVE_INFO_INIT(PL1_ENABLE, TPMI_POWER_LIMIT_ENABLE, 62,
		RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	[PL2_ENABLE] = PRIMITIVE_INFO_INIT(PL2_ENABLE, TPMI_POWER_LIMIT_ENABLE, 62,
		RAPL_DOMAIN_REG_PL2, ARBITRARY_UNIT, 0),
	[PL4_ENABLE] = PRIMITIVE_INFO_INIT(PL4_ENABLE, TPMI_POWER_LIMIT_ENABLE, 62,
		RAPL_DOMAIN_REG_PL4, ARBITRARY_UNIT, 0),
	[TIME_WINDOW1] = PRIMITIVE_INFO_INIT(TIME_WINDOW1, TPMI_TIME_WINDOW_MASK, 18,
		RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	[TIME_WINDOW2] = PRIMITIVE_INFO_INIT(TIME_WINDOW2, TPMI_TIME_WINDOW_MASK, 18,
		RAPL_DOMAIN_REG_PL2, TIME_UNIT, 0),
	[THERMAL_SPEC_POWER] = PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER, TPMI_INFO_SPEC_MASK, 0,
		RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_POWER] = PRIMITIVE_INFO_INIT(MAX_POWER, TPMI_INFO_MAX_MASK, 36,
		RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MIN_POWER] = PRIMITIVE_INFO_INIT(MIN_POWER, TPMI_INFO_MIN_MASK, 18,
		RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	[MAX_TIME_WINDOW] = PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW, TPMI_INFO_MAX_TIME_WIN_MASK, 54,
		RAPL_DOMAIN_REG_INFO, TIME_UNIT, 0),
	[THROTTLED_TIME] = PRIMITIVE_INFO_INIT(THROTTLED_TIME, PERF_STATUS_THROTTLE_TIME_MASK, 0,
		RAPL_DOMAIN_REG_PERF, TIME_UNIT, 0),
	/* non-hardware */
	[AVERAGE_POWER] = PRIMITIVE_INFO_INIT(AVERAGE_POWER, 0, 0, 0,
		POWER_UNIT, RAPL_PRIMITIVE_DERIVED),
};

static struct rapl_primitive_info *get_rpi(struct rapl_package *rp, int prim)
{
	struct rapl_primitive_info *rpi = rp->priv->rpi;

	if (prim < 0 || prim > NR_RAPL_PRIMITIVES || !rpi)
		return NULL;

	return &rpi[prim];
}

static int rapl_config(struct rapl_package *rp)
{
	switch (rp->priv->type) {
	/* MMIO I/F shares the same register layout as MSR registers */
	case RAPL_IF_MMIO:
	case RAPL_IF_MSR:
		rp->priv->defaults = (void *)defaults_msr;
		rp->priv->rpi = (void *)rpi_msr;
		break;
	case RAPL_IF_TPMI:
		rp->priv->defaults = (void *)&defaults_tpmi;
		rp->priv->rpi = (void *)rpi_tpmi;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum rapl_primitives
prim_fixups(struct rapl_domain *rd, enum rapl_primitives prim)
{
	struct rapl_defaults *defaults = get_defaults(rd->rp);

	if (!defaults->spr_psys_bits)
		return prim;

	if (rd->id != RAPL_DOMAIN_PLATFORM)
		return prim;

	switch (prim) {
	case POWER_LIMIT1:
		return PSYS_POWER_LIMIT1;
	case POWER_LIMIT2:
		return PSYS_POWER_LIMIT2;
	case PL1_ENABLE:
		return PSYS_PL1_ENABLE;
	case PL2_ENABLE:
		return PSYS_PL2_ENABLE;
	case TIME_WINDOW1:
		return PSYS_TIME_WINDOW1;
	case TIME_WINDOW2:
		return PSYS_TIME_WINDOW2;
	default:
		return prim;
	}
}

/* Read primitive data based on its related struct rapl_primitive_info.
 * if xlate flag is set, return translated data based on data units, i.e.
 * time, energy, and power.
 * RAPL MSRs are non-architectual and are laid out not consistently across
 * domains. Here we use primitive info to allow writing consolidated access
 * functions.
 * For a given primitive, it is processed by MSR mask and shift. Unit conversion
 * is pre-assigned based on RAPL unit MSRs read at init time.
 * 63-------------------------- 31--------------------------- 0
 * |                           xxxxx (mask)                   |
 * |                                |<- shift ----------------|
 * 63-------------------------- 31--------------------------- 0
 */
static int rapl_read_data_raw(struct rapl_domain *rd,
			      enum rapl_primitives prim, bool xlate, u64 *data)
{
	u64 value;
	enum rapl_primitives prim_fixed = prim_fixups(rd, prim);
	struct rapl_primitive_info *rpi = get_rpi(rd->rp, prim_fixed);
	struct reg_action ra;

	if (!rpi || !rpi->name || rpi->flag & RAPL_PRIMITIVE_DUMMY)
		return -EINVAL;

	ra.reg = rd->regs[rpi->id];
	if (!ra.reg.val)
		return -EINVAL;

	/* non-hardware data are collected by the polling thread */
	if (rpi->flag & RAPL_PRIMITIVE_DERIVED) {
		*data = rd->rdd.primitives[prim];
		return 0;
	}

	ra.mask = rpi->mask;

	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra)) {
		pr_debug("failed to read reg 0x%llx for %s:%s\n", ra.reg.val, rd->rp->name, rd->name);
		return -EIO;
	}

	value = ra.value >> rpi->shift;

	if (xlate)
		*data = rapl_unit_xlate(rd, rpi->unit, value, 0);
	else
		*data = value;

	return 0;
}

/* Similar use of primitive info in the read counterpart */
static int rapl_write_data_raw(struct rapl_domain *rd,
			       enum rapl_primitives prim,
			       unsigned long long value)
{
	enum rapl_primitives prim_fixed = prim_fixups(rd, prim);
	struct rapl_primitive_info *rpi = get_rpi(rd->rp, prim_fixed);
	u64 bits;
	struct reg_action ra;
	int ret;

	if (!rpi || !rpi->name || rpi->flag & RAPL_PRIMITIVE_DUMMY)
		return -EINVAL;

	bits = rapl_unit_xlate(rd, rpi->unit, value, 1);
	bits <<= rpi->shift;
	bits &= rpi->mask;

	memset(&ra, 0, sizeof(ra));

	ra.reg = rd->regs[rpi->id];
	ra.mask = rpi->mask;
	ra.value = bits;

	ret = rd->rp->priv->write_raw(get_rid(rd->rp), &ra);

	return ret;
}

static int rapl_read_pl_data(struct rapl_domain *rd, int pl,
			      enum pl_prims pl_prim, bool xlate, u64 *data)
{
	enum rapl_primitives prim = get_pl_prim(rd, pl, pl_prim);

	if (!is_pl_valid(rd, pl))
		return -EINVAL;

	return rapl_read_data_raw(rd, prim, xlate, data);
}

static int rapl_write_pl_data(struct rapl_domain *rd, int pl,
			       enum pl_prims pl_prim,
			       unsigned long long value)
{
	enum rapl_primitives prim = get_pl_prim(rd, pl, pl_prim);

	if (!is_pl_valid(rd, pl))
		return -EINVAL;

	if (rd->rpl[pl].locked) {
		pr_warn("%s:%s:%s locked by BIOS\n", rd->rp->name, rd->name, pl_names[pl]);
		return -EACCES;
	}

	return rapl_write_data_raw(rd, prim, value);
}
/*
 * Raw RAPL data stored in MSRs are in certain scales. We need to
 * convert them into standard units based on the units reported in
 * the RAPL unit MSRs. This is specific to CPUs as the method to
 * calculate units differ on different CPUs.
 * We convert the units to below format based on CPUs.
 * i.e.
 * energy unit: picoJoules  : Represented in picoJoules by default
 * power unit : microWatts  : Represented in milliWatts by default
 * time unit  : microseconds: Represented in seconds by default
 */
static int rapl_check_unit_core(struct rapl_domain *rd)
{
	struct reg_action ra;
	u32 value;

	ra.reg = rd->regs[RAPL_DOMAIN_REG_UNIT];
	ra.mask = ~0;
	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra)) {
		pr_err("Failed to read power unit REG 0x%llx on %s:%s, exit.\n",
			ra.reg.val, rd->rp->name, rd->name);
		return -ENODEV;
	}

	value = (ra.value & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rd->energy_unit = ENERGY_UNIT_SCALE * 1000000 / (1 << value);

	value = (ra.value & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rd->power_unit = 1000000 / (1 << value);

	value = (ra.value & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rd->time_unit = 1000000 / (1 << value);

	pr_debug("Core CPU %s:%s energy=%dpJ, time=%dus, power=%duW\n",
		 rd->rp->name, rd->name, rd->energy_unit, rd->time_unit, rd->power_unit);

	return 0;
}

static int rapl_check_unit_atom(struct rapl_domain *rd)
{
	struct reg_action ra;
	u32 value;

	ra.reg = rd->regs[RAPL_DOMAIN_REG_UNIT];
	ra.mask = ~0;
	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra)) {
		pr_err("Failed to read power unit REG 0x%llx on %s:%s, exit.\n",
			ra.reg.val, rd->rp->name, rd->name);
		return -ENODEV;
	}

	value = (ra.value & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rd->energy_unit = ENERGY_UNIT_SCALE * 1 << value;

	value = (ra.value & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rd->power_unit = (1 << value) * 1000;

	value = (ra.value & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rd->time_unit = 1000000 / (1 << value);

	pr_debug("Atom %s:%s energy=%dpJ, time=%dus, power=%duW\n",
		 rd->rp->name, rd->name, rd->energy_unit, rd->time_unit, rd->power_unit);

	return 0;
}

static void power_limit_irq_save_cpu(void *info)
{
	u32 l, h = 0;
	struct rapl_package *rp = (struct rapl_package *)info;

	/* save the state of PLN irq mask bit before disabling it */
	rdmsr_safe(MSR_IA32_PACKAGE_THERM_INTERRUPT, &l, &h);
	if (!(rp->power_limit_irq & PACKAGE_PLN_INT_SAVED)) {
		rp->power_limit_irq = l & PACKAGE_THERM_INT_PLN_ENABLE;
		rp->power_limit_irq |= PACKAGE_PLN_INT_SAVED;
	}
	l &= ~PACKAGE_THERM_INT_PLN_ENABLE;
	wrmsr_safe(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
}

/* REVISIT:
 * When package power limit is set artificially low by RAPL, LVT
 * thermal interrupt for package power limit should be ignored
 * since we are not really exceeding the real limit. The intention
 * is to avoid excessive interrupts while we are trying to save power.
 * A useful feature might be routing the package_power_limit interrupt
 * to userspace via eventfd. once we have a usecase, this is simple
 * to do by adding an atomic notifier.
 */

static void package_power_limit_irq_save(struct rapl_package *rp)
{
	if (rp->lead_cpu < 0)
		return;

	if (!boot_cpu_has(X86_FEATURE_PTS) || !boot_cpu_has(X86_FEATURE_PLN))
		return;

	smp_call_function_single(rp->lead_cpu, power_limit_irq_save_cpu, rp, 1);
}

/*
 * Restore per package power limit interrupt enable state. Called from cpu
 * hotplug code on package removal.
 */
static void package_power_limit_irq_restore(struct rapl_package *rp)
{
	u32 l, h;

	if (rp->lead_cpu < 0)
		return;

	if (!boot_cpu_has(X86_FEATURE_PTS) || !boot_cpu_has(X86_FEATURE_PLN))
		return;

	/* irq enable state not saved, nothing to restore */
	if (!(rp->power_limit_irq & PACKAGE_PLN_INT_SAVED))
		return;

	rdmsr_safe(MSR_IA32_PACKAGE_THERM_INTERRUPT, &l, &h);

	if (rp->power_limit_irq & PACKAGE_THERM_INT_PLN_ENABLE)
		l |= PACKAGE_THERM_INT_PLN_ENABLE;
	else
		l &= ~PACKAGE_THERM_INT_PLN_ENABLE;

	wrmsr_safe(MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
}

static void set_floor_freq_default(struct rapl_domain *rd, bool mode)
{
	int i;

	/* always enable clamp such that p-state can go below OS requested
	 * range. power capping priority over guranteed frequency.
	 */
	rapl_write_pl_data(rd, POWER_LIMIT1, PL_CLAMP, mode);

	for (i = POWER_LIMIT2; i < NR_POWER_LIMITS; i++) {
		rapl_write_pl_data(rd, i, PL_ENABLE, mode);
		rapl_write_pl_data(rd, i, PL_CLAMP, mode);
	}
}

static void set_floor_freq_atom(struct rapl_domain *rd, bool enable)
{
	static u32 power_ctrl_orig_val;
	struct rapl_defaults *defaults = get_defaults(rd->rp);
	u32 mdata;

	if (!defaults->floor_freq_reg_addr) {
		pr_err("Invalid floor frequency config register\n");
		return;
	}

	if (!power_ctrl_orig_val)
		iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_CR_READ,
			      defaults->floor_freq_reg_addr,
			      &power_ctrl_orig_val);
	mdata = power_ctrl_orig_val;
	if (enable) {
		mdata &= ~(0x7f << 8);
		mdata |= 1 << 8;
	}
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_CR_WRITE,
		       defaults->floor_freq_reg_addr, mdata);
}

static u64 rapl_compute_time_window_core(struct rapl_domain *rd, u64 value,
					 bool to_raw)
{
	u64 f, y;		/* fraction and exp. used for time unit */

	/*
	 * Special processing based on 2^Y*(1+F/4), refer
	 * to Intel Software Developer's manual Vol.3B: CH 14.9.3.
	 */
	if (!to_raw) {
		f = (value & 0x60) >> 5;
		y = value & 0x1f;
		value = (1 << y) * (4 + f) * rd->time_unit / 4;
	} else {
		if (value < rd->time_unit)
			return 0;

		do_div(value, rd->time_unit);
		y = ilog2(value);

		/*
		 * The target hardware field is 7 bits wide, so return all ones
		 * if the exponent is too large.
		 */
		if (y > 0x1f)
			return 0x7f;

		f = div64_u64(4 * (value - (1ULL << y)), 1ULL << y);
		value = (y & 0x1f) | ((f & 0x3) << 5);
	}
	return value;
}

static u64 rapl_compute_time_window_atom(struct rapl_domain *rd, u64 value,
					 bool to_raw)
{
	/*
	 * Atom time unit encoding is straight forward val * time_unit,
	 * where time_unit is default to 1 sec. Never 0.
	 */
	if (!to_raw)
		return (value) ? value * rd->time_unit : rd->time_unit;

	value = div64_u64(value, rd->time_unit);

	return value;
}

/* TPMI Unit register has different layout */
#define TPMI_POWER_UNIT_OFFSET	POWER_UNIT_OFFSET
#define TPMI_POWER_UNIT_MASK	POWER_UNIT_MASK
#define TPMI_ENERGY_UNIT_OFFSET	0x06
#define TPMI_ENERGY_UNIT_MASK	0x7C0
#define TPMI_TIME_UNIT_OFFSET	0x0C
#define TPMI_TIME_UNIT_MASK	0xF000

static int rapl_check_unit_tpmi(struct rapl_domain *rd)
{
	struct reg_action ra;
	u32 value;

	ra.reg = rd->regs[RAPL_DOMAIN_REG_UNIT];
	ra.mask = ~0;
	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra)) {
		pr_err("Failed to read power unit REG 0x%llx on %s:%s, exit.\n",
			ra.reg.val, rd->rp->name, rd->name);
		return -ENODEV;
	}

	value = (ra.value & TPMI_ENERGY_UNIT_MASK) >> TPMI_ENERGY_UNIT_OFFSET;
	rd->energy_unit = ENERGY_UNIT_SCALE * 1000000 / (1 << value);

	value = (ra.value & TPMI_POWER_UNIT_MASK) >> TPMI_POWER_UNIT_OFFSET;
	rd->power_unit = 1000000 / (1 << value);

	value = (ra.value & TPMI_TIME_UNIT_MASK) >> TPMI_TIME_UNIT_OFFSET;
	rd->time_unit = 1000000 / (1 << value);

	pr_debug("Core CPU %s:%s energy=%dpJ, time=%dus, power=%duW\n",
		 rd->rp->name, rd->name, rd->energy_unit, rd->time_unit, rd->power_unit);

	return 0;
}

static const struct rapl_defaults defaults_tpmi = {
	.check_unit = rapl_check_unit_tpmi,
	/* Reuse existing logic, ignore the PL_CLAMP failures and enable all Power Limits */
	.set_floor_freq = set_floor_freq_default,
	.compute_time_window = rapl_compute_time_window_core,
};

static const struct rapl_defaults rapl_defaults_core = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_check_unit_core,
	.set_floor_freq = set_floor_freq_default,
	.compute_time_window = rapl_compute_time_window_core,
};

static const struct rapl_defaults rapl_defaults_hsw_server = {
	.check_unit = rapl_check_unit_core,
	.set_floor_freq = set_floor_freq_default,
	.compute_time_window = rapl_compute_time_window_core,
	.dram_domain_energy_unit = 15300,
};

static const struct rapl_defaults rapl_defaults_spr_server = {
	.check_unit = rapl_check_unit_core,
	.set_floor_freq = set_floor_freq_default,
	.compute_time_window = rapl_compute_time_window_core,
	.psys_domain_energy_unit = 1000000000,
	.spr_psys_bits = true,
};

static const struct rapl_defaults rapl_defaults_byt = {
	.floor_freq_reg_addr = IOSF_CPU_POWER_BUDGET_CTL_BYT,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = set_floor_freq_atom,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_tng = {
	.floor_freq_reg_addr = IOSF_CPU_POWER_BUDGET_CTL_TNG,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = set_floor_freq_atom,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_ann = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = NULL,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_cht = {
	.floor_freq_reg_addr = 0,
	.check_unit = rapl_check_unit_atom,
	.set_floor_freq = NULL,
	.compute_time_window = rapl_compute_time_window_atom,
};

static const struct rapl_defaults rapl_defaults_amd = {
	.check_unit = rapl_check_unit_core,
};

static const struct x86_cpu_id rapl_ids[] __initconst = {
	X86_MATCH_INTEL_FAM6_MODEL(SANDYBRIDGE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(SANDYBRIDGE_X,	&rapl_defaults_core),

	X86_MATCH_INTEL_FAM6_MODEL(IVYBRIDGE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(IVYBRIDGE_X,		&rapl_defaults_core),

	X86_MATCH_INTEL_FAM6_MODEL(HASWELL,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_G,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_X,		&rapl_defaults_hsw_server),

	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_G,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_D,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_X,		&rapl_defaults_hsw_server),

	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_X,		&rapl_defaults_hsw_server),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(CANNONLAKE_L,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_NNPI,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_X,		&rapl_defaults_hsw_server),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_D,		&rapl_defaults_hsw_server),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ROCKETLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GRACEMONT,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P,        &rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_S,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(METEORLAKE,		&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(METEORLAKE_L,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(SAPPHIRERAPIDS_X,	&rapl_defaults_spr_server),
	X86_MATCH_INTEL_FAM6_MODEL(EMERALDRAPIDS_X,	&rapl_defaults_spr_server),
	X86_MATCH_INTEL_FAM6_MODEL(LAKEFIELD,		&rapl_defaults_core),

	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT,	&rapl_defaults_byt),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_AIRMONT,	&rapl_defaults_cht),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_MID,	&rapl_defaults_tng),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_AIRMONT_MID,	&rapl_defaults_ann),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT_PLUS,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT_D,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT_D,	&rapl_defaults_core),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT_L,	&rapl_defaults_core),

	X86_MATCH_INTEL_FAM6_MODEL(XEON_PHI_KNL,	&rapl_defaults_hsw_server),
	X86_MATCH_INTEL_FAM6_MODEL(XEON_PHI_KNM,	&rapl_defaults_hsw_server),

	X86_MATCH_VENDOR_FAM(AMD, 0x17, &rapl_defaults_amd),
	X86_MATCH_VENDOR_FAM(AMD, 0x19, &rapl_defaults_amd),
	X86_MATCH_VENDOR_FAM(HYGON, 0x18, &rapl_defaults_amd),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, rapl_ids);

/* Read once for all raw primitive data for domains */
static void rapl_update_domain_data(struct rapl_package *rp)
{
	int dmn, prim;
	u64 val;

	for (dmn = 0; dmn < rp->nr_domains; dmn++) {
		pr_debug("update %s domain %s data\n", rp->name,
			 rp->domains[dmn].name);
		/* exclude non-raw primitives */
		for (prim = 0; prim < NR_RAW_PRIMITIVES; prim++) {
			struct rapl_primitive_info *rpi = get_rpi(rp, prim);

			if (!rapl_read_data_raw(&rp->domains[dmn], prim,
						rpi->unit, &val))
				rp->domains[dmn].rdd.primitives[prim] = val;
		}
	}

}

static int rapl_package_register_powercap(struct rapl_package *rp)
{
	struct rapl_domain *rd;
	struct powercap_zone *power_zone = NULL;
	int nr_pl, ret;

	/* Update the domain data of the new package */
	rapl_update_domain_data(rp);

	/* first we register package domain as the parent zone */
	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			nr_pl = find_nr_power_limit(rd);
			pr_debug("register package domain %s\n", rp->name);
			power_zone = powercap_register_zone(&rd->power_zone,
					    rp->priv->control_type, rp->name,
					    NULL, &zone_ops[rd->id], nr_pl,
					    &constraint_ops);
			if (IS_ERR(power_zone)) {
				pr_debug("failed to register power zone %s\n",
					 rp->name);
				return PTR_ERR(power_zone);
			}
			/* track parent zone in per package/socket data */
			rp->power_zone = power_zone;
			/* done, only one package domain per socket */
			break;
		}
	}
	if (!power_zone) {
		pr_err("no package domain found, unknown topology!\n");
		return -ENODEV;
	}
	/* now register domains as children of the socket/package */
	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		struct powercap_zone *parent = rp->power_zone;

		if (rd->id == RAPL_DOMAIN_PACKAGE)
			continue;
		if (rd->id == RAPL_DOMAIN_PLATFORM)
			parent = NULL;
		/* number of power limits per domain varies */
		nr_pl = find_nr_power_limit(rd);
		power_zone = powercap_register_zone(&rd->power_zone,
						    rp->priv->control_type,
						    rd->name, parent,
						    &zone_ops[rd->id], nr_pl,
						    &constraint_ops);

		if (IS_ERR(power_zone)) {
			pr_debug("failed to register power_zone, %s:%s\n",
				 rp->name, rd->name);
			ret = PTR_ERR(power_zone);
			goto err_cleanup;
		}
	}
	return 0;

err_cleanup:
	/*
	 * Clean up previously initialized domains within the package if we
	 * failed after the first domain setup.
	 */
	while (--rd >= rp->domains) {
		pr_debug("unregister %s domain %s\n", rp->name, rd->name);
		powercap_unregister_zone(rp->priv->control_type,
					 &rd->power_zone);
	}

	return ret;
}

static int rapl_check_domain(int domain, struct rapl_package *rp)
{
	struct reg_action ra;

	switch (domain) {
	case RAPL_DOMAIN_PACKAGE:
	case RAPL_DOMAIN_PP0:
	case RAPL_DOMAIN_PP1:
	case RAPL_DOMAIN_DRAM:
	case RAPL_DOMAIN_PLATFORM:
		ra.reg = rp->priv->regs[domain][RAPL_DOMAIN_REG_STATUS];
		break;
	default:
		pr_err("invalid domain id %d\n", domain);
		return -EINVAL;
	}
	/* make sure domain counters are available and contains non-zero
	 * values, otherwise skip it.
	 */

	ra.mask = ENERGY_STATUS_MASK;
	if (rp->priv->read_raw(get_rid(rp), &ra) || !ra.value)
		return -ENODEV;

	return 0;
}

/*
 * Get per domain energy/power/time unit.
 * RAPL Interfaces without per domain unit register will use the package
 * scope unit register to set per domain units.
 */
static int rapl_get_domain_unit(struct rapl_domain *rd)
{
	struct rapl_defaults *defaults = get_defaults(rd->rp);
	int ret;

	if (!rd->regs[RAPL_DOMAIN_REG_UNIT].val) {
		if (!rd->rp->priv->reg_unit.val) {
			pr_err("No valid Unit register found\n");
			return -ENODEV;
		}
		rd->regs[RAPL_DOMAIN_REG_UNIT] = rd->rp->priv->reg_unit;
	}

	if (!defaults->check_unit) {
		pr_err("missing .check_unit() callback\n");
		return -ENODEV;
	}

	ret = defaults->check_unit(rd);
	if (ret)
		return ret;

	if (rd->id == RAPL_DOMAIN_DRAM && defaults->dram_domain_energy_unit)
		rd->energy_unit = defaults->dram_domain_energy_unit;
	if (rd->id == RAPL_DOMAIN_PLATFORM && defaults->psys_domain_energy_unit)
		rd->energy_unit = defaults->psys_domain_energy_unit;
	return 0;
}

/*
 * Check if power limits are available. Two cases when they are not available:
 * 1. Locked by BIOS, in this case we still provide read-only access so that
 *    users can see what limit is set by the BIOS.
 * 2. Some CPUs make some domains monitoring only which means PLx MSRs may not
 *    exist at all. In this case, we do not show the constraints in powercap.
 *
 * Called after domains are detected and initialized.
 */
static void rapl_detect_powerlimit(struct rapl_domain *rd)
{
	u64 val64;
	int i;

	for (i = POWER_LIMIT1; i < NR_POWER_LIMITS; i++) {
		if (!rapl_read_pl_data(rd, i, PL_LOCK, false, &val64)) {
			if (val64) {
				rd->rpl[i].locked = true;
				pr_info("%s:%s:%s locked by BIOS\n",
					rd->rp->name, rd->name, pl_names[i]);
			}
		}

		if (rapl_read_pl_data(rd, i, PL_ENABLE, false, &val64))
			rd->rpl[i].name = NULL;
	}
}

/* Detect active and valid domains for the given CPU, caller must
 * ensure the CPU belongs to the targeted package and CPU hotlug is disabled.
 */
static int rapl_detect_domains(struct rapl_package *rp)
{
	struct rapl_domain *rd;
	int i;

	for (i = 0; i < RAPL_DOMAIN_MAX; i++) {
		/* use physical package id to read counters */
		if (!rapl_check_domain(i, rp)) {
			rp->domain_map |= 1 << i;
			pr_info("Found RAPL domain %s\n", rapl_domain_names[i]);
		}
	}
	rp->nr_domains = bitmap_weight(&rp->domain_map, RAPL_DOMAIN_MAX);
	if (!rp->nr_domains) {
		pr_debug("no valid rapl domains found in %s\n", rp->name);
		return -ENODEV;
	}
	pr_debug("found %d domains on %s\n", rp->nr_domains, rp->name);

	rp->domains = kcalloc(rp->nr_domains + 1, sizeof(struct rapl_domain),
			      GFP_KERNEL);
	if (!rp->domains)
		return -ENOMEM;

	rapl_init_domains(rp);

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		rapl_get_domain_unit(rd);
		rapl_detect_powerlimit(rd);
	}

	return 0;
}

/* called from CPU hotplug notifier, hotplug lock held */
void rapl_remove_package(struct rapl_package *rp)
{
	struct rapl_domain *rd, *rd_package = NULL;

	package_power_limit_irq_restore(rp);

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		int i;

		for (i = POWER_LIMIT1; i < NR_POWER_LIMITS; i++) {
			rapl_write_pl_data(rd, i, PL_ENABLE, 0);
			rapl_write_pl_data(rd, i, PL_CLAMP, 0);
		}

		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			rd_package = rd;
			continue;
		}
		pr_debug("remove package, undo power limit on %s: %s\n",
			 rp->name, rd->name);
		powercap_unregister_zone(rp->priv->control_type,
					 &rd->power_zone);
	}
	/* do parent zone last */
	powercap_unregister_zone(rp->priv->control_type,
				 &rd_package->power_zone);
	list_del(&rp->plist);
	kfree(rp);
}
EXPORT_SYMBOL_GPL(rapl_remove_package);

/* caller to ensure CPU hotplug lock is held */
struct rapl_package *rapl_find_package_domain(int id, struct rapl_if_priv *priv, bool id_is_cpu)
{
	struct rapl_package *rp;
	int uid;

	if (id_is_cpu)
		uid = topology_logical_die_id(id);
	else
		uid = id;

	list_for_each_entry(rp, &rapl_packages, plist) {
		if (rp->id == uid
		    && rp->priv->control_type == priv->control_type)
			return rp;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(rapl_find_package_domain);

/* called from CPU hotplug notifier, hotplug lock held */
struct rapl_package *rapl_add_package(int id, struct rapl_if_priv *priv, bool id_is_cpu)
{
	struct rapl_package *rp;
	int ret;

	rp = kzalloc(sizeof(struct rapl_package), GFP_KERNEL);
	if (!rp)
		return ERR_PTR(-ENOMEM);

	if (id_is_cpu) {
		rp->id = topology_logical_die_id(id);
		rp->lead_cpu = id;
		if (topology_max_die_per_package() > 1)
			snprintf(rp->name, PACKAGE_DOMAIN_NAME_LENGTH, "package-%d-die-%d",
				 topology_physical_package_id(id), topology_die_id(id));
		else
			snprintf(rp->name, PACKAGE_DOMAIN_NAME_LENGTH, "package-%d",
				 topology_physical_package_id(id));
	} else {
		rp->id = id;
		rp->lead_cpu = -1;
		snprintf(rp->name, PACKAGE_DOMAIN_NAME_LENGTH, "package-%d", id);
	}

	rp->priv = priv;
	ret = rapl_config(rp);
	if (ret)
		goto err_free_package;

	/* check if the package contains valid domains */
	if (rapl_detect_domains(rp)) {
		ret = -ENODEV;
		goto err_free_package;
	}
	ret = rapl_package_register_powercap(rp);
	if (!ret) {
		INIT_LIST_HEAD(&rp->plist);
		list_add(&rp->plist, &rapl_packages);
		return rp;
	}

err_free_package:
	kfree(rp->domains);
	kfree(rp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(rapl_add_package);

static void power_limit_state_save(void)
{
	struct rapl_package *rp;
	struct rapl_domain *rd;
	int ret, i;

	cpus_read_lock();
	list_for_each_entry(rp, &rapl_packages, plist) {
		if (!rp->power_zone)
			continue;
		rd = power_zone_to_rapl_domain(rp->power_zone);
		for (i = POWER_LIMIT1; i < NR_POWER_LIMITS; i++) {
			ret = rapl_read_pl_data(rd, i, PL_LIMIT, true,
						 &rd->rpl[i].last_power_limit);
			if (ret)
				rd->rpl[i].last_power_limit = 0;
		}
	}
	cpus_read_unlock();
}

static void power_limit_state_restore(void)
{
	struct rapl_package *rp;
	struct rapl_domain *rd;
	int i;

	cpus_read_lock();
	list_for_each_entry(rp, &rapl_packages, plist) {
		if (!rp->power_zone)
			continue;
		rd = power_zone_to_rapl_domain(rp->power_zone);
		for (i = POWER_LIMIT1; i < NR_POWER_LIMITS; i++)
			if (rd->rpl[i].last_power_limit)
				rapl_write_pl_data(rd, i, PL_LIMIT,
					       rd->rpl[i].last_power_limit);
	}
	cpus_read_unlock();
}

static int rapl_pm_callback(struct notifier_block *nb,
			    unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_SUSPEND_PREPARE:
		power_limit_state_save();
		break;
	case PM_POST_SUSPEND:
		power_limit_state_restore();
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block rapl_pm_notifier = {
	.notifier_call = rapl_pm_callback,
};

static struct platform_device *rapl_msr_platdev;

static int __init rapl_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(rapl_ids);
	if (id) {
		defaults_msr = (struct rapl_defaults *)id->driver_data;

		rapl_msr_platdev = platform_device_alloc("intel_rapl_msr", 0);
		if (!rapl_msr_platdev)
			return -ENOMEM;

		ret = platform_device_add(rapl_msr_platdev);
		if (ret) {
			platform_device_put(rapl_msr_platdev);
			return ret;
		}
	}

	ret = register_pm_notifier(&rapl_pm_notifier);
	if (ret && rapl_msr_platdev) {
		platform_device_del(rapl_msr_platdev);
		platform_device_put(rapl_msr_platdev);
	}

	return ret;
}

static void __exit rapl_exit(void)
{
	platform_device_unregister(rapl_msr_platdev);
	unregister_pm_notifier(&rapl_pm_notifier);
}

fs_initcall(rapl_init);
module_exit(rapl_exit);

MODULE_DESCRIPTION("Intel Runtime Average Power Limit (RAPL) common code");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com>");
MODULE_LICENSE("GPL v2");
