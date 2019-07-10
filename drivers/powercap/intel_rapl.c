// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Running Average Power Limit (RAPL) Driver
 * Copyright (c) 2013, Intel Corporation.
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

#include <asm/iosf_mbi.h>
#include <asm/processor.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

/* Local defines */
#define MSR_PLATFORM_POWER_LIMIT	0x0000065C

/* bitmasks for RAPL MSRs, used by primitive access functions */
#define ENERGY_STATUS_MASK      0xffffffff

#define POWER_LIMIT1_MASK       0x7FFF
#define POWER_LIMIT1_ENABLE     BIT(15)
#define POWER_LIMIT1_CLAMP      BIT(16)

#define POWER_LIMIT2_MASK       (0x7FFFULL<<32)
#define POWER_LIMIT2_ENABLE     BIT_ULL(47)
#define POWER_LIMIT2_CLAMP      BIT_ULL(48)
#define POWER_PACKAGE_LOCK      BIT_ULL(63)
#define POWER_PP_LOCK           BIT(31)

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

/* Non HW constants */
#define RAPL_PRIMITIVE_DERIVED       BIT(1) /* not from raw data */
#define RAPL_PRIMITIVE_DUMMY         BIT(2)

#define TIME_WINDOW_MAX_MSEC 40000
#define TIME_WINDOW_MIN_MSEC 250
#define ENERGY_UNIT_SCALE    1000 /* scale from driver unit to powercap unit */
enum unit_type {
	ARBITRARY_UNIT, /* no translation */
	POWER_UNIT,
	ENERGY_UNIT,
	TIME_UNIT,
};

/* per domain data, some are optional */
#define NR_RAW_PRIMITIVES (NR_RAPL_PRIMITIVES - 2)

struct msrl_action {
	u32 msr_no;
	u64 clear_mask;
	u64 set_mask;
	int err;
};

#define	DOMAIN_STATE_INACTIVE           BIT(0)
#define	DOMAIN_STATE_POWER_LIMIT_SET    BIT(1)
#define DOMAIN_STATE_BIOS_LOCKED        BIT(2)

static const char pl1_name[] = "long_term";
static const char pl2_name[] = "short_term";

#define power_zone_to_rapl_domain(_zone) \
	container_of(_zone, struct rapl_domain, power_zone)

struct rapl_defaults {
	u8 floor_freq_reg_addr;
	int (*check_unit)(struct rapl_package *rp, int cpu);
	void (*set_floor_freq)(struct rapl_domain *rd, bool mode);
	u64 (*compute_time_window)(struct rapl_package *rp, u64 val,
				bool to_raw);
	unsigned int dram_domain_energy_unit;
};
static struct rapl_defaults *rapl_defaults;

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
static u64 rapl_unit_xlate(struct rapl_domain *rd,
			enum unit_type type, u64 value,
			int to_raw);
static void package_power_limit_irq_save(struct rapl_package *rp);

static LIST_HEAD(rapl_packages); /* guarded by CPU hotplug lock */

static const char * const rapl_domain_names[] = {
	"package",
	"core",
	"uncore",
	"dram",
	"psys",
};

static struct powercap_control_type *control_type; /* PowerCap Controller */
static struct rapl_domain *platform_rapl_domain; /* Platform (PSys) domain */

/* caller to ensure CPU hotplug lock is held */
static struct rapl_package *rapl_find_package_domain(int cpu)
{
	int id = topology_logical_die_id(cpu);
	struct rapl_package *rp;

	list_for_each_entry(rp, &rapl_packages, plist) {
		if (rp->id == id)
			return rp;
	}

	return NULL;
}

static int get_energy_counter(struct powercap_zone *power_zone, u64 *energy_raw)
{
	struct rapl_domain *rd;
	u64 energy_now;

	/* prevent CPU hotplug, make sure the RAPL domain does not go
	 * away while reading the counter.
	 */
	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);

	if (!rapl_read_data_raw(rd, ENERGY_COUNTER, true, &energy_now)) {
		*energy_raw = energy_now;
		put_online_cpus();

		return 0;
	}
	put_online_cpus();

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
		if (rd->rpl[i].name)
			nr_pl++;
	}

	return nr_pl;
}

static int set_domain_enable(struct powercap_zone *power_zone, bool mode)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);

	if (rd->state & DOMAIN_STATE_BIOS_LOCKED)
		return -EACCES;

	get_online_cpus();
	rapl_write_data_raw(rd, PL1_ENABLE, mode);
	if (rapl_defaults->set_floor_freq)
		rapl_defaults->set_floor_freq(rd, mode);
	put_online_cpus();

	return 0;
}

static int get_domain_enable(struct powercap_zone *power_zone, bool *mode)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);
	u64 val;

	if (rd->state & DOMAIN_STATE_BIOS_LOCKED) {
		*mode = false;
		return 0;
	}
	get_online_cpus();
	if (rapl_read_data_raw(rd, PL1_ENABLE, true, &val)) {
		put_online_cpus();
		return -EIO;
	}
	*mode = val;
	put_online_cpus();

	return 0;
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
 * index in that some  PLs maybe missing due to non-existant MSRs. So we
 * need to convert here by finding the valid PLs only (name populated).
 */
static int contraint_to_pl(struct rapl_domain *rd, int cid)
{
	int i, j;

	for (i = 0, j = 0; i < NR_POWER_LIMITS; i++) {
		if ((rd->rpl[i].name) && j++ == cid) {
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

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id < 0) {
		ret = id;
		goto set_exit;
	}

	rp = rd->rp;

	if (rd->state & DOMAIN_STATE_BIOS_LOCKED) {
		dev_warn(&power_zone->dev, "%s locked by BIOS, monitoring only\n",
			rd->name);
		ret = -EACCES;
		goto set_exit;
	}

	switch (rd->rpl[id].prim_id) {
	case PL1_ENABLE:
		rapl_write_data_raw(rd, POWER_LIMIT1, power_limit);
		break;
	case PL2_ENABLE:
		rapl_write_data_raw(rd, POWER_LIMIT2, power_limit);
		break;
	default:
		ret = -EINVAL;
	}
	if (!ret)
		package_power_limit_irq_save(rp);
set_exit:
	put_online_cpus();
	return ret;
}

static int get_current_power_limit(struct powercap_zone *power_zone, int cid,
					u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int prim;
	int ret = 0;
	int id;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id < 0) {
		ret = id;
		goto get_exit;
	}

	switch (rd->rpl[id].prim_id) {
	case PL1_ENABLE:
		prim = POWER_LIMIT1;
		break;
	case PL2_ENABLE:
		prim = POWER_LIMIT2;
		break;
	default:
		put_online_cpus();
		return -EINVAL;
	}
	if (rapl_read_data_raw(rd, prim, true, &val))
		ret = -EIO;
	else
		*data = val;

get_exit:
	put_online_cpus();

	return ret;
}

static int set_time_window(struct powercap_zone *power_zone, int cid,
								u64 window)
{
	struct rapl_domain *rd;
	int ret = 0;
	int id;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id < 0) {
		ret = id;
		goto set_time_exit;
	}

	switch (rd->rpl[id].prim_id) {
	case PL1_ENABLE:
		rapl_write_data_raw(rd, TIME_WINDOW1, window);
		break;
	case PL2_ENABLE:
		rapl_write_data_raw(rd, TIME_WINDOW2, window);
		break;
	default:
		ret = -EINVAL;
	}

set_time_exit:
	put_online_cpus();
	return ret;
}

static int get_time_window(struct powercap_zone *power_zone, int cid, u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int ret = 0;
	int id;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id < 0) {
		ret = id;
		goto get_time_exit;
	}

	switch (rd->rpl[id].prim_id) {
	case PL1_ENABLE:
		ret = rapl_read_data_raw(rd, TIME_WINDOW1, true, &val);
		break;
	case PL2_ENABLE:
		ret = rapl_read_data_raw(rd, TIME_WINDOW2, true, &val);
		break;
	default:
		put_online_cpus();
		return -EINVAL;
	}
	if (!ret)
		*data = val;

get_time_exit:
	put_online_cpus();

	return ret;
}

static const char *get_constraint_name(struct powercap_zone *power_zone, int cid)
{
	struct rapl_domain *rd;
	int id;

	rd = power_zone_to_rapl_domain(power_zone);
	id = contraint_to_pl(rd, cid);
	if (id >= 0)
		return rd->rpl[id].name;

	return NULL;
}


static int get_max_power(struct powercap_zone *power_zone, int id,
					u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int prim;
	int ret = 0;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	switch (rd->rpl[id].prim_id) {
	case PL1_ENABLE:
		prim = THERMAL_SPEC_POWER;
		break;
	case PL2_ENABLE:
		prim = MAX_POWER;
		break;
	default:
		put_online_cpus();
		return -EINVAL;
	}
	if (rapl_read_data_raw(rd, prim, true, &val))
		ret = -EIO;
	else
		*data = val;

	put_online_cpus();

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

/* called after domain detection and package level data are set */
static void rapl_init_domains(struct rapl_package *rp)
{
	int i;
	struct rapl_domain *rd = rp->domains;

	for (i = 0; i < RAPL_DOMAIN_MAX; i++) {
		unsigned int mask = rp->domain_map & (1 << i);
		switch (mask) {
		case BIT(RAPL_DOMAIN_PACKAGE):
			rd->name = rapl_domain_names[RAPL_DOMAIN_PACKAGE];
			rd->id = RAPL_DOMAIN_PACKAGE;
			rd->regs[RAPL_DOMAIN_REG_LIMIT] = MSR_PKG_POWER_LIMIT;
			rd->regs[RAPL_DOMAIN_REG_STATUS] = MSR_PKG_ENERGY_STATUS;
			rd->regs[RAPL_DOMAIN_REG_PERF] = MSR_PKG_PERF_STATUS;
			rd->regs[RAPL_DOMAIN_REG_POLICY] = 0;
			rd->regs[RAPL_DOMAIN_REG_INFO] = MSR_PKG_POWER_INFO;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			rd->rpl[1].prim_id = PL2_ENABLE;
			rd->rpl[1].name = pl2_name;
			break;
		case BIT(RAPL_DOMAIN_PP0):
			rd->name = rapl_domain_names[RAPL_DOMAIN_PP0];
			rd->id = RAPL_DOMAIN_PP0;
			rd->regs[RAPL_DOMAIN_REG_LIMIT] = MSR_PP0_POWER_LIMIT;
			rd->regs[RAPL_DOMAIN_REG_STATUS] = MSR_PP0_ENERGY_STATUS;
			rd->regs[RAPL_DOMAIN_REG_PERF] = 0;
			rd->regs[RAPL_DOMAIN_REG_POLICY] = MSR_PP0_POLICY;
			rd->regs[RAPL_DOMAIN_REG_INFO] = 0;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			break;
		case BIT(RAPL_DOMAIN_PP1):
			rd->name = rapl_domain_names[RAPL_DOMAIN_PP1];
			rd->id = RAPL_DOMAIN_PP1;
			rd->regs[RAPL_DOMAIN_REG_LIMIT] = MSR_PP1_POWER_LIMIT;
			rd->regs[RAPL_DOMAIN_REG_STATUS] = MSR_PP1_ENERGY_STATUS;
			rd->regs[RAPL_DOMAIN_REG_PERF] = 0;
			rd->regs[RAPL_DOMAIN_REG_POLICY] = MSR_PP1_POLICY;
			rd->regs[RAPL_DOMAIN_REG_INFO] = 0;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			break;
		case BIT(RAPL_DOMAIN_DRAM):
			rd->name = rapl_domain_names[RAPL_DOMAIN_DRAM];
			rd->id = RAPL_DOMAIN_DRAM;
			rd->regs[RAPL_DOMAIN_REG_LIMIT] = MSR_DRAM_POWER_LIMIT;
			rd->regs[RAPL_DOMAIN_REG_STATUS] = MSR_DRAM_ENERGY_STATUS;
			rd->regs[RAPL_DOMAIN_REG_PERF] = MSR_DRAM_PERF_STATUS;
			rd->regs[RAPL_DOMAIN_REG_POLICY] = 0;
			rd->regs[RAPL_DOMAIN_REG_INFO] = MSR_DRAM_POWER_INFO;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			rd->domain_energy_unit =
				rapl_defaults->dram_domain_energy_unit;
			if (rd->domain_energy_unit)
				pr_info("DRAM domain energy unit %dpj\n",
					rd->domain_energy_unit);
			break;
		}
		if (mask) {
			rd->rp = rp;
			rd++;
		}
	}
}

static u64 rapl_unit_xlate(struct rapl_domain *rd, enum unit_type type,
			u64 value, int to_raw)
{
	u64 units = 1;
	struct rapl_package *rp = rd->rp;
	u64 scale = 1;

	switch (type) {
	case POWER_UNIT:
		units = rp->power_unit;
		break;
	case ENERGY_UNIT:
		scale = ENERGY_UNIT_SCALE;
		/* per domain unit takes precedence */
		if (rd->domain_energy_unit)
			units = rd->domain_energy_unit;
		else
			units = rp->energy_unit;
		break;
	case TIME_UNIT:
		return rapl_defaults->compute_time_window(rp, value, to_raw);
	case ARBITRARY_UNIT:
	default:
		return value;
	};

	if (to_raw)
		return div64_u64(value, units) * scale;

	value *= units;

	return div64_u64(value, scale);
}

/* in the order of enum rapl_primitives */
static struct rapl_primitive_info rpi[] = {
	/* name, mask, shift, msr index, unit divisor */
	PRIMITIVE_INFO_INIT(ENERGY_COUNTER, ENERGY_STATUS_MASK, 0,
				RAPL_DOMAIN_REG_STATUS, ENERGY_UNIT, 0),
	PRIMITIVE_INFO_INIT(POWER_LIMIT1, POWER_LIMIT1_MASK, 0,
				RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(POWER_LIMIT2, POWER_LIMIT2_MASK, 32,
				RAPL_DOMAIN_REG_LIMIT, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(FW_LOCK, POWER_PP_LOCK, 31,
				RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL1_ENABLE, POWER_LIMIT1_ENABLE, 15,
				RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL1_CLAMP, POWER_LIMIT1_CLAMP, 16,
				RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL2_ENABLE, POWER_LIMIT2_ENABLE, 47,
				RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL2_CLAMP, POWER_LIMIT2_CLAMP, 48,
				RAPL_DOMAIN_REG_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(TIME_WINDOW1, TIME_WINDOW1_MASK, 17,
				RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(TIME_WINDOW2, TIME_WINDOW2_MASK, 49,
				RAPL_DOMAIN_REG_LIMIT, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER, POWER_INFO_THERMAL_SPEC_MASK,
				0, RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MAX_POWER, POWER_INFO_MAX_MASK, 32,
				RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MIN_POWER, POWER_INFO_MIN_MASK, 16,
				RAPL_DOMAIN_REG_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW, POWER_INFO_MAX_TIME_WIN_MASK, 48,
				RAPL_DOMAIN_REG_INFO, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(THROTTLED_TIME, PERF_STATUS_THROTTLE_TIME_MASK, 0,
				RAPL_DOMAIN_REG_PERF, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(PRIORITY_LEVEL, PP_POLICY_MASK, 0,
				RAPL_DOMAIN_REG_POLICY, ARBITRARY_UNIT, 0),
	/* non-hardware */
	PRIMITIVE_INFO_INIT(AVERAGE_POWER, 0, 0, 0, POWER_UNIT,
				RAPL_PRIMITIVE_DERIVED),
	{NULL, 0, 0, 0},
};

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
			enum rapl_primitives prim,
			bool xlate, u64 *data)
{
	u64 value, final;
	u32 msr;
	struct rapl_primitive_info *rp = &rpi[prim];
	int cpu;

	if (!rp->name || rp->flag & RAPL_PRIMITIVE_DUMMY)
		return -EINVAL;

	msr = rd->regs[rp->id];
	if (!msr)
		return -EINVAL;

	cpu = rd->rp->lead_cpu;

	/* special-case package domain, which uses a different bit*/
	if (prim == FW_LOCK && rd->id == RAPL_DOMAIN_PACKAGE) {
		rp->mask = POWER_PACKAGE_LOCK;
		rp->shift = 63;
	}
	/* non-hardware data are collected by the polling thread */
	if (rp->flag & RAPL_PRIMITIVE_DERIVED) {
		*data = rd->rdd.primitives[prim];
		return 0;
	}

	if (rdmsrl_safe_on_cpu(cpu, msr, &value)) {
		pr_debug("failed to read msr 0x%x on cpu %d\n", msr, cpu);
		return -EIO;
	}

	final = value & rp->mask;
	final = final >> rp->shift;
	if (xlate)
		*data = rapl_unit_xlate(rd, rp->unit, final, 0);
	else
		*data = final;

	return 0;
}


static int msrl_update_safe(u32 msr_no, u64 clear_mask, u64 set_mask)
{
	int err;
	u64 val;

	err = rdmsrl_safe(msr_no, &val);
	if (err)
		goto out;

	val &= ~clear_mask;
	val |= set_mask;

	err = wrmsrl_safe(msr_no, val);

out:
	return err;
}

static void msrl_update_func(void *info)
{
	struct msrl_action *ma = info;

	ma->err = msrl_update_safe(ma->msr_no, ma->clear_mask, ma->set_mask);
}

/* Similar use of primitive info in the read counterpart */
static int rapl_write_data_raw(struct rapl_domain *rd,
			enum rapl_primitives prim,
			unsigned long long value)
{
	struct rapl_primitive_info *rp = &rpi[prim];
	int cpu;
	u64 bits;
	struct msrl_action ma;
	int ret;

	cpu = rd->rp->lead_cpu;
	bits = rapl_unit_xlate(rd, rp->unit, value, 1);
	bits <<= rp->shift;
	bits &= rp->mask;

	memset(&ma, 0, sizeof(ma));

	ma.msr_no = rd->regs[rp->id];
	ma.clear_mask = rp->mask;
	ma.set_mask = bits;

	ret = smp_call_function_single(cpu, msrl_update_func, &ma, 1);
	if (ret)
		WARN_ON_ONCE(ret);
	else
		ret = ma.err;

	return ret;
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
static int rapl_check_unit_core(struct rapl_package *rp, int cpu)
{
	u64 msr_val;
	u32 value;

	if (rdmsrl_safe_on_cpu(cpu, MSR_RAPL_POWER_UNIT, &msr_val)) {
		pr_err("Failed to read power unit MSR 0x%x on CPU %d, exit.\n",
			MSR_RAPL_POWER_UNIT, cpu);
		return -ENODEV;
	}

	value = (msr_val & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rp->energy_unit = ENERGY_UNIT_SCALE * 1000000 / (1 << value);

	value = (msr_val & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rp->power_unit = 1000000 / (1 << value);

	value = (msr_val & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rp->time_unit = 1000000 / (1 << value);

	pr_debug("Core CPU %s energy=%dpJ, time=%dus, power=%duW\n",
		rp->name, rp->energy_unit, rp->time_unit, rp->power_unit);

	return 0;
}

static int rapl_check_unit_atom(struct rapl_package *rp, int cpu)
{
	u64 msr_val;
	u32 value;

	if (rdmsrl_safe_on_cpu(cpu, MSR_RAPL_POWER_UNIT, &msr_val)) {
		pr_err("Failed to read power unit MSR 0x%x on CPU %d, exit.\n",
			MSR_RAPL_POWER_UNIT, cpu);
		return -ENODEV;
	}
	value = (msr_val & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rp->energy_unit = ENERGY_UNIT_SCALE * 1 << value;

	value = (msr_val & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rp->power_unit = (1 << value) * 1000;

	value = (msr_val & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rp->time_unit = 1000000 / (1 << value);

	pr_debug("Atom %s energy=%dpJ, time=%dus, power=%duW\n",
		rp->name, rp->energy_unit, rp->time_unit, rp->power_unit);

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
	int nr_powerlimit = find_nr_power_limit(rd);

	/* always enable clamp such that p-state can go below OS requested
	 * range. power capping priority over guranteed frequency.
	 */
	rapl_write_data_raw(rd, PL1_CLAMP, mode);

	/* some domains have pl2 */
	if (nr_powerlimit > 1) {
		rapl_write_data_raw(rd, PL2_ENABLE, mode);
		rapl_write_data_raw(rd, PL2_CLAMP, mode);
	}
}

static void set_floor_freq_atom(struct rapl_domain *rd, bool enable)
{
	static u32 power_ctrl_orig_val;
	u32 mdata;

	if (!rapl_defaults->floor_freq_reg_addr) {
		pr_err("Invalid floor frequency config register\n");
		return;
	}

	if (!power_ctrl_orig_val)
		iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_CR_READ,
			      rapl_defaults->floor_freq_reg_addr,
			      &power_ctrl_orig_val);
	mdata = power_ctrl_orig_val;
	if (enable) {
		mdata &= ~(0x7f << 8);
		mdata |= 1 << 8;
	}
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_CR_WRITE,
		       rapl_defaults->floor_freq_reg_addr, mdata);
}

static u64 rapl_compute_time_window_core(struct rapl_package *rp, u64 value,
					bool to_raw)
{
	u64 f, y; /* fraction and exp. used for time unit */

	/*
	 * Special processing based on 2^Y*(1+F/4), refer
	 * to Intel Software Developer's manual Vol.3B: CH 14.9.3.
	 */
	if (!to_raw) {
		f = (value & 0x60) >> 5;
		y = value & 0x1f;
		value = (1 << y) * (4 + f) * rp->time_unit / 4;
	} else {
		do_div(value, rp->time_unit);
		y = ilog2(value);
		f = div64_u64(4 * (value - (1 << y)), 1 << y);
		value = (y & 0x1f) | ((f & 0x3) << 5);
	}
	return value;
}

static u64 rapl_compute_time_window_atom(struct rapl_package *rp, u64 value,
					bool to_raw)
{
	/*
	 * Atom time unit encoding is straight forward val * time_unit,
	 * where time_unit is default to 1 sec. Never 0.
	 */
	if (!to_raw)
		return (value) ? value *= rp->time_unit : rp->time_unit;
	else
		value = div64_u64(value, rp->time_unit);

	return value;
}

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

static const struct x86_cpu_id rapl_ids[] __initconst = {
	INTEL_CPU_FAM6(SANDYBRIDGE,		rapl_defaults_core),
	INTEL_CPU_FAM6(SANDYBRIDGE_X,		rapl_defaults_core),

	INTEL_CPU_FAM6(IVYBRIDGE,		rapl_defaults_core),
	INTEL_CPU_FAM6(IVYBRIDGE_X,		rapl_defaults_core),

	INTEL_CPU_FAM6(HASWELL_CORE,		rapl_defaults_core),
	INTEL_CPU_FAM6(HASWELL_ULT,		rapl_defaults_core),
	INTEL_CPU_FAM6(HASWELL_GT3E,		rapl_defaults_core),
	INTEL_CPU_FAM6(HASWELL_X,		rapl_defaults_hsw_server),

	INTEL_CPU_FAM6(BROADWELL_CORE,		rapl_defaults_core),
	INTEL_CPU_FAM6(BROADWELL_GT3E,		rapl_defaults_core),
	INTEL_CPU_FAM6(BROADWELL_XEON_D,	rapl_defaults_core),
	INTEL_CPU_FAM6(BROADWELL_X,		rapl_defaults_hsw_server),

	INTEL_CPU_FAM6(SKYLAKE_DESKTOP,		rapl_defaults_core),
	INTEL_CPU_FAM6(SKYLAKE_MOBILE,		rapl_defaults_core),
	INTEL_CPU_FAM6(SKYLAKE_X,		rapl_defaults_hsw_server),
	INTEL_CPU_FAM6(KABYLAKE_MOBILE,		rapl_defaults_core),
	INTEL_CPU_FAM6(KABYLAKE_DESKTOP,	rapl_defaults_core),
	INTEL_CPU_FAM6(CANNONLAKE_MOBILE,	rapl_defaults_core),
	INTEL_CPU_FAM6(ICELAKE_MOBILE,		rapl_defaults_core),

	INTEL_CPU_FAM6(ATOM_SILVERMONT,		rapl_defaults_byt),
	INTEL_CPU_FAM6(ATOM_AIRMONT,		rapl_defaults_cht),
	INTEL_CPU_FAM6(ATOM_SILVERMONT_MID,	rapl_defaults_tng),
	INTEL_CPU_FAM6(ATOM_AIRMONT_MID,	rapl_defaults_ann),
	INTEL_CPU_FAM6(ATOM_GOLDMONT,		rapl_defaults_core),
	INTEL_CPU_FAM6(ATOM_GOLDMONT_PLUS,	rapl_defaults_core),
	INTEL_CPU_FAM6(ATOM_GOLDMONT_X,		rapl_defaults_core),
	INTEL_CPU_FAM6(ATOM_TREMONT_X,		rapl_defaults_core),

	INTEL_CPU_FAM6(XEON_PHI_KNL,		rapl_defaults_hsw_server),
	INTEL_CPU_FAM6(XEON_PHI_KNM,		rapl_defaults_hsw_server),
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
			if (!rapl_read_data_raw(&rp->domains[dmn], prim,
						rpi[prim].unit, &val))
				rp->domains[dmn].rdd.primitives[prim] =	val;
		}
	}

}

static void rapl_unregister_powercap(void)
{
	if (platform_rapl_domain) {
		powercap_unregister_zone(control_type,
					 &platform_rapl_domain->power_zone);
		kfree(platform_rapl_domain);
	}
	powercap_unregister_control_type(control_type);
}

static int rapl_package_register_powercap(struct rapl_package *rp)
{
	struct rapl_domain *rd;
	struct powercap_zone *power_zone = NULL;
	int nr_pl, ret;

	/* Update the domain data of the new package */
	rapl_update_domain_data(rp);

	/* first we register package domain as the parent zone*/
	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			nr_pl = find_nr_power_limit(rd);
			pr_debug("register package domain %s\n", rp->name);
			power_zone = powercap_register_zone(&rd->power_zone,
							control_type,
							rp->name, NULL,
							&zone_ops[rd->id],
							nr_pl,
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
	/* now register domains as children of the socket/package*/
	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		if (rd->id == RAPL_DOMAIN_PACKAGE)
			continue;
		/* number of power limits per domain varies */
		nr_pl = find_nr_power_limit(rd);
		power_zone = powercap_register_zone(&rd->power_zone,
						control_type, rd->name,
						rp->power_zone,
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
		powercap_unregister_zone(control_type, &rd->power_zone);
	}

	return ret;
}

static int __init rapl_register_psys(void)
{
	struct rapl_domain *rd;
	struct powercap_zone *power_zone;
	u64 val;

	if (rdmsrl_safe_on_cpu(0, MSR_PLATFORM_ENERGY_STATUS, &val) || !val)
		return -ENODEV;

	if (rdmsrl_safe_on_cpu(0, MSR_PLATFORM_POWER_LIMIT, &val) || !val)
		return -ENODEV;

	rd = kzalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->name = rapl_domain_names[RAPL_DOMAIN_PLATFORM];
	rd->id = RAPL_DOMAIN_PLATFORM;
	rd->regs[RAPL_DOMAIN_REG_LIMIT] = MSR_PLATFORM_POWER_LIMIT;
	rd->regs[RAPL_DOMAIN_REG_STATUS] = MSR_PLATFORM_ENERGY_STATUS;
	rd->rpl[0].prim_id = PL1_ENABLE;
	rd->rpl[0].name = pl1_name;
	rd->rpl[1].prim_id = PL2_ENABLE;
	rd->rpl[1].name = pl2_name;
	rd->rp = rapl_find_package_domain(0);

	power_zone = powercap_register_zone(&rd->power_zone, control_type,
					    "psys", NULL,
					    &zone_ops[RAPL_DOMAIN_PLATFORM],
					    2, &constraint_ops);

	if (IS_ERR(power_zone)) {
		kfree(rd);
		return PTR_ERR(power_zone);
	}

	platform_rapl_domain = rd;

	return 0;
}

static int __init rapl_register_powercap(void)
{
	control_type = powercap_register_control_type(NULL, "intel-rapl", NULL);
	if (IS_ERR(control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(control_type);
	}
	return 0;
}

static int rapl_check_domain(int cpu, int domain)
{
	unsigned msr;
	u64 val = 0;

	switch (domain) {
	case RAPL_DOMAIN_PACKAGE:
		msr = MSR_PKG_ENERGY_STATUS;
		break;
	case RAPL_DOMAIN_PP0:
		msr = MSR_PP0_ENERGY_STATUS;
		break;
	case RAPL_DOMAIN_PP1:
		msr = MSR_PP1_ENERGY_STATUS;
		break;
	case RAPL_DOMAIN_DRAM:
		msr = MSR_DRAM_ENERGY_STATUS;
		break;
	case RAPL_DOMAIN_PLATFORM:
		/* PSYS(PLATFORM) is not a CPU domain, so avoid printng error */
		return -EINVAL;
	default:
		pr_err("invalid domain id %d\n", domain);
		return -EINVAL;
	}
	/* make sure domain counters are available and contains non-zero
	 * values, otherwise skip it.
	 */
	if (rdmsrl_safe_on_cpu(cpu, msr, &val) || !val)
		return -ENODEV;

	return 0;
}


/*
 * Check if power limits are available. Two cases when they are not available:
 * 1. Locked by BIOS, in this case we still provide read-only access so that
 *    users can see what limit is set by the BIOS.
 * 2. Some CPUs make some domains monitoring only which means PLx MSRs may not
 *    exist at all. In this case, we do not show the contraints in powercap.
 *
 * Called after domains are detected and initialized.
 */
static void rapl_detect_powerlimit(struct rapl_domain *rd)
{
	u64 val64;
	int i;

	/* check if the domain is locked by BIOS, ignore if MSR doesn't exist */
	if (!rapl_read_data_raw(rd, FW_LOCK, false, &val64)) {
		if (val64) {
			pr_info("RAPL %s domain %s locked by BIOS\n",
				rd->rp->name, rd->name);
			rd->state |= DOMAIN_STATE_BIOS_LOCKED;
		}
	}
	/* check if power limit MSRs exists, otherwise domain is monitoring only */
	for (i = 0; i < NR_POWER_LIMITS; i++) {
		int prim = rd->rpl[i].prim_id;
		if (rapl_read_data_raw(rd, prim, false, &val64))
			rd->rpl[i].name = NULL;
	}
}

/* Detect active and valid domains for the given CPU, caller must
 * ensure the CPU belongs to the targeted package and CPU hotlug is disabled.
 */
static int rapl_detect_domains(struct rapl_package *rp, int cpu)
{
	struct rapl_domain *rd;
	int i;

	for (i = 0; i < RAPL_DOMAIN_MAX; i++) {
		/* use physical package id to read counters */
		if (!rapl_check_domain(cpu, i)) {
			rp->domain_map |= 1 << i;
			pr_info("Found RAPL domain %s\n", rapl_domain_names[i]);
		}
	}
	rp->nr_domains = bitmap_weight(&rp->domain_map,	RAPL_DOMAIN_MAX);
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

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++)
		rapl_detect_powerlimit(rd);

	return 0;
}

/* called from CPU hotplug notifier, hotplug lock held */
static void rapl_remove_package(struct rapl_package *rp)
{
	struct rapl_domain *rd, *rd_package = NULL;

	package_power_limit_irq_restore(rp);

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		rapl_write_data_raw(rd, PL1_ENABLE, 0);
		rapl_write_data_raw(rd, PL1_CLAMP, 0);
		if (find_nr_power_limit(rd) > 1) {
			rapl_write_data_raw(rd, PL2_ENABLE, 0);
			rapl_write_data_raw(rd, PL2_CLAMP, 0);
		}
		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			rd_package = rd;
			continue;
		}
		pr_debug("remove package, undo power limit on %s: %s\n",
			 rp->name, rd->name);
		powercap_unregister_zone(control_type, &rd->power_zone);
	}
	/* do parent zone last */
	powercap_unregister_zone(control_type, &rd_package->power_zone);
	list_del(&rp->plist);
	kfree(rp);
}

/* called from CPU hotplug notifier, hotplug lock held */
static struct rapl_package *rapl_add_package(int cpu)
{
	int id = topology_logical_die_id(cpu);
	struct rapl_package *rp;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	int ret;

	rp = kzalloc(sizeof(struct rapl_package), GFP_KERNEL);
	if (!rp)
		return ERR_PTR(-ENOMEM);

	/* add the new package to the list */
	rp->id = id;
	rp->lead_cpu = cpu;

	if (topology_max_die_per_package() > 1)
		snprintf(rp->name, PACKAGE_DOMAIN_NAME_LENGTH,
			"package-%d-die-%d", c->phys_proc_id, c->cpu_die_id);
	else
		snprintf(rp->name, PACKAGE_DOMAIN_NAME_LENGTH, "package-%d",
			c->phys_proc_id);

	/* check if the package contains valid domains */
	if (rapl_detect_domains(rp, cpu) ||
		rapl_defaults->check_unit(rp, cpu)) {
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

/* Handles CPU hotplug on multi-socket systems.
 * If a CPU goes online as the first CPU of the physical package
 * we add the RAPL package to the system. Similarly, when the last
 * CPU of the package is removed, we remove the RAPL package and its
 * associated domains. Cooling devices are handled accordingly at
 * per-domain level.
 */
static int rapl_cpu_online(unsigned int cpu)
{
	struct rapl_package *rp;

	rp = rapl_find_package_domain(cpu);
	if (!rp) {
		rp = rapl_add_package(cpu);
		if (IS_ERR(rp))
			return PTR_ERR(rp);
	}
	cpumask_set_cpu(cpu, &rp->cpumask);
	return 0;
}

static int rapl_cpu_down_prep(unsigned int cpu)
{
	struct rapl_package *rp;
	int lead_cpu;

	rp = rapl_find_package_domain(cpu);
	if (!rp)
		return 0;

	cpumask_clear_cpu(cpu, &rp->cpumask);
	lead_cpu = cpumask_first(&rp->cpumask);
	if (lead_cpu >= nr_cpu_ids)
		rapl_remove_package(rp);
	else if (rp->lead_cpu == cpu)
		rp->lead_cpu = lead_cpu;
	return 0;
}

static enum cpuhp_state pcap_rapl_online;

static void power_limit_state_save(void)
{
	struct rapl_package *rp;
	struct rapl_domain *rd;
	int nr_pl, ret, i;

	get_online_cpus();
	list_for_each_entry(rp, &rapl_packages, plist) {
		if (!rp->power_zone)
			continue;
		rd = power_zone_to_rapl_domain(rp->power_zone);
		nr_pl = find_nr_power_limit(rd);
		for (i = 0; i < nr_pl; i++) {
			switch (rd->rpl[i].prim_id) {
			case PL1_ENABLE:
				ret = rapl_read_data_raw(rd,
						POWER_LIMIT1,
						true,
						&rd->rpl[i].last_power_limit);
				if (ret)
					rd->rpl[i].last_power_limit = 0;
				break;
			case PL2_ENABLE:
				ret = rapl_read_data_raw(rd,
						POWER_LIMIT2,
						true,
						&rd->rpl[i].last_power_limit);
				if (ret)
					rd->rpl[i].last_power_limit = 0;
				break;
			}
		}
	}
	put_online_cpus();
}

static void power_limit_state_restore(void)
{
	struct rapl_package *rp;
	struct rapl_domain *rd;
	int nr_pl, i;

	get_online_cpus();
	list_for_each_entry(rp, &rapl_packages, plist) {
		if (!rp->power_zone)
			continue;
		rd = power_zone_to_rapl_domain(rp->power_zone);
		nr_pl = find_nr_power_limit(rd);
		for (i = 0; i < nr_pl; i++) {
			switch (rd->rpl[i].prim_id) {
			case PL1_ENABLE:
				if (rd->rpl[i].last_power_limit)
					rapl_write_data_raw(rd,
						POWER_LIMIT1,
						rd->rpl[i].last_power_limit);
				break;
			case PL2_ENABLE:
				if (rd->rpl[i].last_power_limit)
					rapl_write_data_raw(rd,
						POWER_LIMIT2,
						rd->rpl[i].last_power_limit);
				break;
			}
		}
	}
	put_online_cpus();
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

static int __init rapl_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(rapl_ids);
	if (!id) {
		pr_err("driver does not support CPU family %d model %d\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);

		return -ENODEV;
	}

	rapl_defaults = (struct rapl_defaults *)id->driver_data;

	ret = rapl_register_powercap();
	if (ret)
		return ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "powercap/rapl:online",
				rapl_cpu_online, rapl_cpu_down_prep);
	if (ret < 0)
		goto err_unreg;
	pcap_rapl_online = ret;

	/* Don't bail out if PSys is not supported */
	rapl_register_psys();

	ret = register_pm_notifier(&rapl_pm_notifier);
	if (ret)
		goto err_unreg_all;

	return 0;

err_unreg_all:
	cpuhp_remove_state(pcap_rapl_online);

err_unreg:
	rapl_unregister_powercap();
	return ret;
}

static void __exit rapl_exit(void)
{
	unregister_pm_notifier(&rapl_pm_notifier);
	cpuhp_remove_state(pcap_rapl_online);
	rapl_unregister_powercap();
}

module_init(rapl_init);
module_exit(rapl_exit);

MODULE_DESCRIPTION("Driver for Intel RAPL (Running Average Power Limit)");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com>");
MODULE_LICENSE("GPL v2");
