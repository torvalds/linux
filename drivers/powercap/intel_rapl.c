/*
 * Intel Running Average Power Limit (RAPL) Driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.
 *
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
#include <asm/iosf_mbi.h>

#include <asm/processor.h>
#include <asm/cpu_device_id.h>

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

enum rapl_domain_type {
	RAPL_DOMAIN_PACKAGE, /* entire package/socket */
	RAPL_DOMAIN_PP0, /* core power plane */
	RAPL_DOMAIN_PP1, /* graphics uncore */
	RAPL_DOMAIN_DRAM,/* DRAM control_type */
	RAPL_DOMAIN_MAX,
};

enum rapl_domain_msr_id {
	RAPL_DOMAIN_MSR_LIMIT,
	RAPL_DOMAIN_MSR_STATUS,
	RAPL_DOMAIN_MSR_PERF,
	RAPL_DOMAIN_MSR_POLICY,
	RAPL_DOMAIN_MSR_INFO,
	RAPL_DOMAIN_MSR_MAX,
};

/* per domain data, some are optional */
enum rapl_primitives {
	ENERGY_COUNTER,
	POWER_LIMIT1,
	POWER_LIMIT2,
	FW_LOCK,

	PL1_ENABLE,  /* power limit 1, aka long term */
	PL1_CLAMP,   /* allow frequency to go below OS request */
	PL2_ENABLE,  /* power limit 2, aka short term, instantaneous */
	PL2_CLAMP,

	TIME_WINDOW1, /* long term */
	TIME_WINDOW2, /* short term */
	THERMAL_SPEC_POWER,
	MAX_POWER,

	MIN_POWER,
	MAX_TIME_WINDOW,
	THROTTLED_TIME,
	PRIORITY_LEVEL,

	/* below are not raw primitive data */
	AVERAGE_POWER,
	NR_RAPL_PRIMITIVES,
};

#define NR_RAW_PRIMITIVES (NR_RAPL_PRIMITIVES - 2)

/* Can be expanded to include events, etc.*/
struct rapl_domain_data {
	u64 primitives[NR_RAPL_PRIMITIVES];
	unsigned long timestamp;
};


#define	DOMAIN_STATE_INACTIVE           BIT(0)
#define	DOMAIN_STATE_POWER_LIMIT_SET    BIT(1)
#define DOMAIN_STATE_BIOS_LOCKED        BIT(2)

#define NR_POWER_LIMITS (2)
struct rapl_power_limit {
	struct powercap_zone_constraint *constraint;
	int prim_id; /* primitive ID used to enable */
	struct rapl_domain *domain;
	const char *name;
};

static const char pl1_name[] = "long_term";
static const char pl2_name[] = "short_term";

struct rapl_domain {
	const char *name;
	enum rapl_domain_type id;
	int msrs[RAPL_DOMAIN_MSR_MAX];
	struct powercap_zone power_zone;
	struct rapl_domain_data rdd;
	struct rapl_power_limit rpl[NR_POWER_LIMITS];
	u64 attr_map; /* track capabilities */
	unsigned int state;
	unsigned int domain_energy_unit;
	int package_id;
};
#define power_zone_to_rapl_domain(_zone) \
	container_of(_zone, struct rapl_domain, power_zone)


/* Each physical package contains multiple domains, these are the common
 * data across RAPL domains within a package.
 */
struct rapl_package {
	unsigned int id; /* physical package/socket id */
	unsigned int nr_domains;
	unsigned long domain_map; /* bit map of active domains */
	unsigned int power_unit;
	unsigned int energy_unit;
	unsigned int time_unit;
	struct rapl_domain *domains; /* array of domains, sized at runtime */
	struct powercap_zone *power_zone; /* keep track of parent zone */
	int nr_cpus; /* active cpus on the package, topology info is lost during
		      * cpu hotplug. so we have to track ourselves.
		      */
	unsigned long power_limit_irq; /* keep track of package power limit
					* notify interrupt enable status.
					*/
	struct list_head plist;
};

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
	enum rapl_domain_msr_id id;
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
static u64 rapl_unit_xlate(struct rapl_domain *rd, int package,
			enum unit_type type, u64 value,
			int to_raw);
static void package_power_limit_irq_save(int package_id);

static LIST_HEAD(rapl_packages); /* guarded by CPU hotplug lock */

static const char * const rapl_domain_names[] = {
	"package",
	"core",
	"uncore",
	"dram",
};

static struct powercap_control_type *control_type; /* PowerCap Controller */

/* caller to ensure CPU hotplug lock is held */
static struct rapl_package *find_package_by_id(int id)
{
	struct rapl_package *rp;

	list_for_each_entry(rp, &rapl_packages, plist) {
		if (rp->id == id)
			return rp;
	}

	return NULL;
}

/* caller to ensure CPU hotplug lock is held */
static int find_active_cpu_on_package(int package_id)
{
	int i;

	for_each_online_cpu(i) {
		if (topology_physical_package_id(i) == package_id)
			return i;
	}
	/* all CPUs on this package are offline */

	return -ENODEV;
}

/* caller must hold cpu hotplug lock */
static void rapl_cleanup_data(void)
{
	struct rapl_package *p, *tmp;

	list_for_each_entry_safe(p, tmp, &rapl_packages, plist) {
		kfree(p->domains);
		list_del(&p->plist);
		kfree(p);
	}
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

	*energy = rapl_unit_xlate(rd, 0, ENERGY_UNIT, ENERGY_STATUS_MASK, 0);
	return 0;
}

static int release_zone(struct powercap_zone *power_zone)
{
	struct rapl_domain *rd = power_zone_to_rapl_domain(power_zone);
	struct rapl_package *rp;

	/* package zone is the last zone of a package, we can free
	 * memory here since all children has been unregistered.
	 */
	if (rd->id == RAPL_DOMAIN_PACKAGE) {
		rp = find_package_by_id(rd->package_id);
		if (!rp) {
			dev_warn(&power_zone->dev, "no package id %s\n",
				rd->name);
			return -ENODEV;
		}
		kfree(rd);
		rp->domains = NULL;
	}

	return 0;

}

static int find_nr_power_limit(struct rapl_domain *rd)
{
	int i;

	for (i = 0; i < NR_POWER_LIMITS; i++) {
		if (rd->rpl[i].name == NULL)
			break;
	}

	return i;
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
static struct powercap_zone_ops zone_ops[] = {
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
};

static int set_power_limit(struct powercap_zone *power_zone, int id,
			u64 power_limit)
{
	struct rapl_domain *rd;
	struct rapl_package *rp;
	int ret = 0;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
	rp = find_package_by_id(rd->package_id);
	if (!rp) {
		ret = -ENODEV;
		goto set_exit;
	}

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
		package_power_limit_irq_save(rd->package_id);
set_exit:
	put_online_cpus();
	return ret;
}

static int get_current_power_limit(struct powercap_zone *power_zone, int id,
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

	put_online_cpus();

	return ret;
}

static int set_time_window(struct powercap_zone *power_zone, int id,
								u64 window)
{
	struct rapl_domain *rd;
	int ret = 0;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
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
	put_online_cpus();
	return ret;
}

static int get_time_window(struct powercap_zone *power_zone, int id, u64 *data)
{
	struct rapl_domain *rd;
	u64 val;
	int ret = 0;

	get_online_cpus();
	rd = power_zone_to_rapl_domain(power_zone);
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
	put_online_cpus();

	return ret;
}

static const char *get_constraint_name(struct powercap_zone *power_zone, int id)
{
	struct rapl_power_limit *rpl;
	struct rapl_domain *rd;

	rd = power_zone_to_rapl_domain(power_zone);
	rpl = (struct rapl_power_limit *) &rd->rpl[id];

	return rpl->name;
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

static struct powercap_zone_constraint_ops constraint_ops = {
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
			rd->msrs[0] = MSR_PKG_POWER_LIMIT;
			rd->msrs[1] = MSR_PKG_ENERGY_STATUS;
			rd->msrs[2] = MSR_PKG_PERF_STATUS;
			rd->msrs[3] = 0;
			rd->msrs[4] = MSR_PKG_POWER_INFO;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			rd->rpl[1].prim_id = PL2_ENABLE;
			rd->rpl[1].name = pl2_name;
			break;
		case BIT(RAPL_DOMAIN_PP0):
			rd->name = rapl_domain_names[RAPL_DOMAIN_PP0];
			rd->id = RAPL_DOMAIN_PP0;
			rd->msrs[0] = MSR_PP0_POWER_LIMIT;
			rd->msrs[1] = MSR_PP0_ENERGY_STATUS;
			rd->msrs[2] = 0;
			rd->msrs[3] = MSR_PP0_POLICY;
			rd->msrs[4] = 0;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			break;
		case BIT(RAPL_DOMAIN_PP1):
			rd->name = rapl_domain_names[RAPL_DOMAIN_PP1];
			rd->id = RAPL_DOMAIN_PP1;
			rd->msrs[0] = MSR_PP1_POWER_LIMIT;
			rd->msrs[1] = MSR_PP1_ENERGY_STATUS;
			rd->msrs[2] = 0;
			rd->msrs[3] = MSR_PP1_POLICY;
			rd->msrs[4] = 0;
			rd->rpl[0].prim_id = PL1_ENABLE;
			rd->rpl[0].name = pl1_name;
			break;
		case BIT(RAPL_DOMAIN_DRAM):
			rd->name = rapl_domain_names[RAPL_DOMAIN_DRAM];
			rd->id = RAPL_DOMAIN_DRAM;
			rd->msrs[0] = MSR_DRAM_POWER_LIMIT;
			rd->msrs[1] = MSR_DRAM_ENERGY_STATUS;
			rd->msrs[2] = MSR_DRAM_PERF_STATUS;
			rd->msrs[3] = 0;
			rd->msrs[4] = MSR_DRAM_POWER_INFO;
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
			rd->package_id = rp->id;
			rd++;
		}
	}
}

static u64 rapl_unit_xlate(struct rapl_domain *rd, int package,
			enum unit_type type, u64 value,
			int to_raw)
{
	u64 units = 1;
	struct rapl_package *rp;
	u64 scale = 1;

	rp = find_package_by_id(package);
	if (!rp)
		return value;

	switch (type) {
	case POWER_UNIT:
		units = rp->power_unit;
		break;
	case ENERGY_UNIT:
		scale = ENERGY_UNIT_SCALE;
		/* per domain unit takes precedence */
		if (rd && rd->domain_energy_unit)
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
				RAPL_DOMAIN_MSR_STATUS, ENERGY_UNIT, 0),
	PRIMITIVE_INFO_INIT(POWER_LIMIT1, POWER_LIMIT1_MASK, 0,
				RAPL_DOMAIN_MSR_LIMIT, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(POWER_LIMIT2, POWER_LIMIT2_MASK, 32,
				RAPL_DOMAIN_MSR_LIMIT, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(FW_LOCK, POWER_PP_LOCK, 31,
				RAPL_DOMAIN_MSR_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL1_ENABLE, POWER_LIMIT1_ENABLE, 15,
				RAPL_DOMAIN_MSR_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL1_CLAMP, POWER_LIMIT1_CLAMP, 16,
				RAPL_DOMAIN_MSR_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL2_ENABLE, POWER_LIMIT2_ENABLE, 47,
				RAPL_DOMAIN_MSR_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(PL2_CLAMP, POWER_LIMIT2_CLAMP, 48,
				RAPL_DOMAIN_MSR_LIMIT, ARBITRARY_UNIT, 0),
	PRIMITIVE_INFO_INIT(TIME_WINDOW1, TIME_WINDOW1_MASK, 17,
				RAPL_DOMAIN_MSR_LIMIT, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(TIME_WINDOW2, TIME_WINDOW2_MASK, 49,
				RAPL_DOMAIN_MSR_LIMIT, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(THERMAL_SPEC_POWER, POWER_INFO_THERMAL_SPEC_MASK,
				0, RAPL_DOMAIN_MSR_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MAX_POWER, POWER_INFO_MAX_MASK, 32,
				RAPL_DOMAIN_MSR_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MIN_POWER, POWER_INFO_MIN_MASK, 16,
				RAPL_DOMAIN_MSR_INFO, POWER_UNIT, 0),
	PRIMITIVE_INFO_INIT(MAX_TIME_WINDOW, POWER_INFO_MAX_TIME_WIN_MASK, 48,
				RAPL_DOMAIN_MSR_INFO, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(THROTTLED_TIME, PERF_STATUS_THROTTLE_TIME_MASK, 0,
				RAPL_DOMAIN_MSR_PERF, TIME_UNIT, 0),
	PRIMITIVE_INFO_INIT(PRIORITY_LEVEL, PP_POLICY_MASK, 0,
				RAPL_DOMAIN_MSR_POLICY, ARBITRARY_UNIT, 0),
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

	msr = rd->msrs[rp->id];
	if (!msr)
		return -EINVAL;
	/* use physical package id to look up active cpus */
	cpu = find_active_cpu_on_package(rd->package_id);
	if (cpu < 0)
		return cpu;

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
		*data = rapl_unit_xlate(rd, rd->package_id, rp->unit, final, 0);
	else
		*data = final;

	return 0;
}

/* Similar use of primitive info in the read counterpart */
static int rapl_write_data_raw(struct rapl_domain *rd,
			enum rapl_primitives prim,
			unsigned long long value)
{
	u64 msr_val;
	u32 msr;
	struct rapl_primitive_info *rp = &rpi[prim];
	int cpu;

	cpu = find_active_cpu_on_package(rd->package_id);
	if (cpu < 0)
		return cpu;
	msr = rd->msrs[rp->id];
	if (rdmsrl_safe_on_cpu(cpu, msr, &msr_val)) {
		dev_dbg(&rd->power_zone.dev,
			"failed to read msr 0x%x on cpu %d\n", msr, cpu);
		return -EIO;
	}
	value = rapl_unit_xlate(rd, rd->package_id, rp->unit, value, 1);
	msr_val &= ~rp->mask;
	msr_val |= value << rp->shift;
	if (wrmsrl_safe_on_cpu(cpu, msr, msr_val)) {
		dev_dbg(&rd->power_zone.dev,
			"failed to write msr 0x%x on cpu %d\n", msr, cpu);
		return -EIO;
	}

	return 0;
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

	pr_debug("Core CPU package %d energy=%dpJ, time=%dus, power=%duW\n",
		rp->id, rp->energy_unit, rp->time_unit, rp->power_unit);

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

	pr_debug("Atom package %d energy=%dpJ, time=%dus, power=%duW\n",
		rp->id, rp->energy_unit, rp->time_unit, rp->power_unit);

	return 0;
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

static void package_power_limit_irq_save(int package_id)
{
	u32 l, h = 0;
	int cpu;
	struct rapl_package *rp;

	rp = find_package_by_id(package_id);
	if (!rp)
		return;

	if (!boot_cpu_has(X86_FEATURE_PTS) || !boot_cpu_has(X86_FEATURE_PLN))
		return;

	cpu = find_active_cpu_on_package(package_id);
	if (cpu < 0)
		return;
	/* save the state of PLN irq mask bit before disabling it */
	rdmsr_safe_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, &l, &h);
	if (!(rp->power_limit_irq & PACKAGE_PLN_INT_SAVED)) {
		rp->power_limit_irq = l & PACKAGE_THERM_INT_PLN_ENABLE;
		rp->power_limit_irq |= PACKAGE_PLN_INT_SAVED;
	}
	l &= ~PACKAGE_THERM_INT_PLN_ENABLE;
	wrmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
}

/* restore per package power limit interrupt enable state */
static void package_power_limit_irq_restore(int package_id)
{
	u32 l, h;
	int cpu;
	struct rapl_package *rp;

	rp = find_package_by_id(package_id);
	if (!rp)
		return;

	if (!boot_cpu_has(X86_FEATURE_PTS) || !boot_cpu_has(X86_FEATURE_PLN))
		return;

	cpu = find_active_cpu_on_package(package_id);
	if (cpu < 0)
		return;

	/* irq enable state not saved, nothing to restore */
	if (!(rp->power_limit_irq & PACKAGE_PLN_INT_SAVED))
		return;
	rdmsr_safe_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, &l, &h);

	if (rp->power_limit_irq & PACKAGE_THERM_INT_PLN_ENABLE)
		l |= PACKAGE_THERM_INT_PLN_ENABLE;
	else
		l &= ~PACKAGE_THERM_INT_PLN_ENABLE;

	wrmsr_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, l, h);
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
		iosf_mbi_read(BT_MBI_UNIT_PMC, BT_MBI_PMC_READ,
			rapl_defaults->floor_freq_reg_addr,
				&power_ctrl_orig_val);
	mdata = power_ctrl_orig_val;
	if (enable) {
		mdata &= ~(0x7f << 8);
		mdata |= 1 << 8;
	}
	iosf_mbi_write(BT_MBI_UNIT_PMC, BT_MBI_PMC_WRITE,
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

#define RAPL_CPU(_model, _ops) {			\
		.vendor = X86_VENDOR_INTEL,		\
		.family = 6,				\
		.model = _model,			\
		.driver_data = (kernel_ulong_t)&_ops,	\
		}

static const struct x86_cpu_id rapl_ids[] __initconst = {
	RAPL_CPU(0x2a, rapl_defaults_core),/* Sandy Bridge */
	RAPL_CPU(0x2d, rapl_defaults_core),/* Sandy Bridge EP */
	RAPL_CPU(0x37, rapl_defaults_byt),/* Valleyview */
	RAPL_CPU(0x3a, rapl_defaults_core),/* Ivy Bridge */
	RAPL_CPU(0x3c, rapl_defaults_core),/* Haswell */
	RAPL_CPU(0x3d, rapl_defaults_core),/* Broadwell */
	RAPL_CPU(0x3f, rapl_defaults_hsw_server),/* Haswell servers */
	RAPL_CPU(0x4f, rapl_defaults_hsw_server),/* Broadwell servers */
	RAPL_CPU(0x45, rapl_defaults_core),/* Haswell ULT */
	RAPL_CPU(0x47, rapl_defaults_core),/* Broadwell-H */
	RAPL_CPU(0x4E, rapl_defaults_core),/* Skylake */
	RAPL_CPU(0x4C, rapl_defaults_cht),/* Braswell/Cherryview */
	RAPL_CPU(0x4A, rapl_defaults_tng),/* Tangier */
	RAPL_CPU(0x56, rapl_defaults_core),/* Future Xeon */
	RAPL_CPU(0x5A, rapl_defaults_ann),/* Annidale */
	RAPL_CPU(0x5E, rapl_defaults_core),/* Skylake-H/S */
	RAPL_CPU(0x57, rapl_defaults_hsw_server),/* Knights Landing */
	{}
};
MODULE_DEVICE_TABLE(x86cpu, rapl_ids);

/* read once for all raw primitive data for all packages, domains */
static void rapl_update_domain_data(void)
{
	int dmn, prim;
	u64 val;
	struct rapl_package *rp;

	list_for_each_entry(rp, &rapl_packages, plist) {
		for (dmn = 0; dmn < rp->nr_domains; dmn++) {
			pr_debug("update package %d domain %s data\n", rp->id,
				rp->domains[dmn].name);
			/* exclude non-raw primitives */
			for (prim = 0; prim < NR_RAW_PRIMITIVES; prim++)
				if (!rapl_read_data_raw(&rp->domains[dmn], prim,
								rpi[prim].unit,
								&val))
					rp->domains[dmn].rdd.primitives[prim] =
									val;
		}
	}

}

static int rapl_unregister_powercap(void)
{
	struct rapl_package *rp;
	struct rapl_domain *rd, *rd_package = NULL;

	/* unregister all active rapl packages from the powercap layer,
	 * hotplug lock held
	 */
	list_for_each_entry(rp, &rapl_packages, plist) {
		package_power_limit_irq_restore(rp->id);

		for (rd = rp->domains; rd < rp->domains + rp->nr_domains;
		     rd++) {
			pr_debug("remove package, undo power limit on %d: %s\n",
				rp->id, rd->name);
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
			powercap_unregister_zone(control_type, &rd->power_zone);
		}
		/* do the package zone last */
		if (rd_package)
			powercap_unregister_zone(control_type,
						&rd_package->power_zone);
	}
	powercap_unregister_control_type(control_type);

	return 0;
}

static int rapl_package_register_powercap(struct rapl_package *rp)
{
	struct rapl_domain *rd;
	int ret = 0;
	char dev_name[17]; /* max domain name = 7 + 1 + 8 for int + 1 for null*/
	struct powercap_zone *power_zone = NULL;
	int nr_pl;

	/* first we register package domain as the parent zone*/
	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			nr_pl = find_nr_power_limit(rd);
			pr_debug("register socket %d package domain %s\n",
				rp->id, rd->name);
			memset(dev_name, 0, sizeof(dev_name));
			snprintf(dev_name, sizeof(dev_name), "%s-%d",
				rd->name, rp->id);
			power_zone = powercap_register_zone(&rd->power_zone,
							control_type,
							dev_name, NULL,
							&zone_ops[rd->id],
							nr_pl,
							&constraint_ops);
			if (IS_ERR(power_zone)) {
				pr_debug("failed to register package, %d\n",
					rp->id);
				ret = PTR_ERR(power_zone);
				goto exit_package;
			}
			/* track parent zone in per package/socket data */
			rp->power_zone = power_zone;
			/* done, only one package domain per socket */
			break;
		}
	}
	if (!power_zone) {
		pr_err("no package domain found, unknown topology!\n");
		ret = -ENODEV;
		goto exit_package;
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
			pr_debug("failed to register power_zone, %d:%s:%s\n",
				rp->id, rd->name, dev_name);
			ret = PTR_ERR(power_zone);
			goto err_cleanup;
		}
	}

exit_package:
	return ret;
err_cleanup:
	/* clean up previously initialized domains within the package if we
	 * failed after the first domain setup.
	 */
	while (--rd >= rp->domains) {
		pr_debug("unregister package %d domain %s\n", rp->id, rd->name);
		powercap_unregister_zone(control_type, &rd->power_zone);
	}

	return ret;
}

static int rapl_register_powercap(void)
{
	struct rapl_domain *rd;
	struct rapl_package *rp;
	int ret = 0;

	control_type = powercap_register_control_type(NULL, "intel-rapl", NULL);
	if (IS_ERR(control_type)) {
		pr_debug("failed to register powercap control_type.\n");
		return PTR_ERR(control_type);
	}
	/* read the initial data */
	rapl_update_domain_data();
	list_for_each_entry(rp, &rapl_packages, plist)
		if (rapl_package_register_powercap(rp))
			goto err_cleanup_package;
	return ret;

err_cleanup_package:
	/* clean up previously initialized packages */
	list_for_each_entry_continue_reverse(rp, &rapl_packages, plist) {
		for (rd = rp->domains; rd < rp->domains + rp->nr_domains;
		     rd++) {
			pr_debug("unregister zone/package %d, %s domain\n",
				rp->id, rd->name);
			powercap_unregister_zone(control_type, &rd->power_zone);
		}
	}

	return ret;
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

/* Detect active and valid domains for the given CPU, caller must
 * ensure the CPU belongs to the targeted package and CPU hotlug is disabled.
 */
static int rapl_detect_domains(struct rapl_package *rp, int cpu)
{
	int i;
	int ret = 0;
	struct rapl_domain *rd;
	u64 locked;

	for (i = 0; i < RAPL_DOMAIN_MAX; i++) {
		/* use physical package id to read counters */
		if (!rapl_check_domain(cpu, i)) {
			rp->domain_map |= 1 << i;
			pr_info("Found RAPL domain %s\n", rapl_domain_names[i]);
		}
	}
	rp->nr_domains = bitmap_weight(&rp->domain_map,	RAPL_DOMAIN_MAX);
	if (!rp->nr_domains) {
		pr_err("no valid rapl domains found in package %d\n", rp->id);
		ret = -ENODEV;
		goto done;
	}
	pr_debug("found %d domains on package %d\n", rp->nr_domains, rp->id);

	rp->domains = kcalloc(rp->nr_domains + 1, sizeof(struct rapl_domain),
			GFP_KERNEL);
	if (!rp->domains) {
		ret = -ENOMEM;
		goto done;
	}
	rapl_init_domains(rp);

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		/* check if the domain is locked by BIOS */
		if (rapl_read_data_raw(rd, FW_LOCK, false, &locked)) {
			pr_info("RAPL package %d domain %s locked by BIOS\n",
				rp->id, rd->name);
				rd->state |= DOMAIN_STATE_BIOS_LOCKED;
		}
	}


done:
	return ret;
}

static bool is_package_new(int package)
{
	struct rapl_package *rp;

	/* caller prevents cpu hotplug, there will be no new packages added
	 * or deleted while traversing the package list, no need for locking.
	 */
	list_for_each_entry(rp, &rapl_packages, plist)
		if (package == rp->id)
			return false;

	return true;
}

/* RAPL interface can be made of a two-level hierarchy: package level and domain
 * level. We first detect the number of packages then domains of each package.
 * We have to consider the possiblity of CPU online/offline due to hotplug and
 * other scenarios.
 */
static int rapl_detect_topology(void)
{
	int i;
	int phy_package_id;
	struct rapl_package *new_package, *rp;

	for_each_online_cpu(i) {
		phy_package_id = topology_physical_package_id(i);
		if (is_package_new(phy_package_id)) {
			new_package = kzalloc(sizeof(*rp), GFP_KERNEL);
			if (!new_package) {
				rapl_cleanup_data();
				return -ENOMEM;
			}
			/* add the new package to the list */
			new_package->id = phy_package_id;
			new_package->nr_cpus = 1;

			/* check if the package contains valid domains */
			if (rapl_detect_domains(new_package, i) ||
				rapl_defaults->check_unit(new_package, i)) {
				kfree(new_package->domains);
				kfree(new_package);
				/* free up the packages already initialized */
				rapl_cleanup_data();
				return -ENODEV;
			}
			INIT_LIST_HEAD(&new_package->plist);
			list_add(&new_package->plist, &rapl_packages);
		} else {
			rp = find_package_by_id(phy_package_id);
			if (rp)
				++rp->nr_cpus;
		}
	}

	return 0;
}

/* called from CPU hotplug notifier, hotplug lock held */
static void rapl_remove_package(struct rapl_package *rp)
{
	struct rapl_domain *rd, *rd_package = NULL;

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		if (rd->id == RAPL_DOMAIN_PACKAGE) {
			rd_package = rd;
			continue;
		}
		pr_debug("remove package %d, %s domain\n", rp->id, rd->name);
		powercap_unregister_zone(control_type, &rd->power_zone);
	}
	/* do parent zone last */
	powercap_unregister_zone(control_type, &rd_package->power_zone);
	list_del(&rp->plist);
	kfree(rp);
}

/* called from CPU hotplug notifier, hotplug lock held */
static int rapl_add_package(int cpu)
{
	int ret = 0;
	int phy_package_id;
	struct rapl_package *rp;

	phy_package_id = topology_physical_package_id(cpu);
	rp = kzalloc(sizeof(struct rapl_package), GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	/* add the new package to the list */
	rp->id = phy_package_id;
	rp->nr_cpus = 1;
	/* check if the package contains valid domains */
	if (rapl_detect_domains(rp, cpu) ||
		rapl_defaults->check_unit(rp, cpu)) {
		ret = -ENODEV;
		goto err_free_package;
	}
	if (!rapl_package_register_powercap(rp)) {
		INIT_LIST_HEAD(&rp->plist);
		list_add(&rp->plist, &rapl_packages);
		return ret;
	}

err_free_package:
	kfree(rp->domains);
	kfree(rp);

	return ret;
}

/* Handles CPU hotplug on multi-socket systems.
 * If a CPU goes online as the first CPU of the physical package
 * we add the RAPL package to the system. Similarly, when the last
 * CPU of the package is removed, we remove the RAPL package and its
 * associated domains. Cooling devices are handled accordingly at
 * per-domain level.
 */
static int rapl_cpu_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	unsigned long cpu = (unsigned long)hcpu;
	int phy_package_id;
	struct rapl_package *rp;

	phy_package_id = topology_physical_package_id(cpu);
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		rp = find_package_by_id(phy_package_id);
		if (rp)
			++rp->nr_cpus;
		else
			rapl_add_package(cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		rp = find_package_by_id(phy_package_id);
		if (!rp)
			break;
		if (--rp->nr_cpus == 0)
			rapl_remove_package(rp);
	}

	return NOTIFY_OK;
}

static struct notifier_block rapl_cpu_notifier = {
	.notifier_call = rapl_cpu_callback,
};

static int __init rapl_init(void)
{
	int ret = 0;
	const struct x86_cpu_id *id;

	id = x86_match_cpu(rapl_ids);
	if (!id) {
		pr_err("driver does not support CPU family %d model %d\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);

		return -ENODEV;
	}

	rapl_defaults = (struct rapl_defaults *)id->driver_data;

	cpu_notifier_register_begin();

	/* prevent CPU hotplug during detection */
	get_online_cpus();
	ret = rapl_detect_topology();
	if (ret)
		goto done;

	if (rapl_register_powercap()) {
		rapl_cleanup_data();
		ret = -ENODEV;
		goto done;
	}
	__register_hotcpu_notifier(&rapl_cpu_notifier);
done:
	put_online_cpus();
	cpu_notifier_register_done();

	return ret;
}

static void __exit rapl_exit(void)
{
	cpu_notifier_register_begin();
	get_online_cpus();
	__unregister_hotcpu_notifier(&rapl_cpu_notifier);
	rapl_unregister_powercap();
	rapl_cleanup_data();
	put_online_cpus();
	cpu_notifier_register_done();
}

module_init(rapl_init);
module_exit(rapl_exit);

MODULE_DESCRIPTION("Driver for Intel RAPL (Running Average Power Limit)");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com>");
MODULE_LICENSE("GPL v2");
