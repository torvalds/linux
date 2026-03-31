// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common code for Intel Running Average Power Limit (RAPL) support.
 * Copyright (c) 2019, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/intel_rapl.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/powercap.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>

#define ENERGY_STATUS_MASK		GENMASK(31, 0)

#define POWER_UNIT_OFFSET		0x00
#define POWER_UNIT_MASK			GENMASK(3, 0)

#define ENERGY_UNIT_OFFSET		0x08
#define ENERGY_UNIT_MASK		GENMASK(12, 8)

#define TIME_UNIT_OFFSET		0x10
#define TIME_UNIT_MASK			GENMASK(19, 16)

/* Non HW constants */
#define RAPL_PRIMITIVE_DUMMY		BIT(2)

#define ENERGY_UNIT_SCALE		1000	/* scale from driver unit to powercap unit */

/* per domain data, some are optional */
#define NR_RAW_PRIMITIVES		(NR_RAPL_PRIMITIVES - 2)

#define PACKAGE_PLN_INT_SAVED		BIT(0)

#define RAPL_EVENT_MASK			GENMASK(7, 0)

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

static const struct rapl_defaults *get_defaults(struct rapl_package *rp)
{
	return rp->priv->defaults;
}

static void rapl_init_domains(struct rapl_package *rp);
static int rapl_read_data_raw(struct rapl_domain *rd,
			      enum rapl_primitives prim,
			      bool xlate, u64 *data,
			      bool pmu_ctx);
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

	if (!rapl_read_data_raw(rd, ENERGY_COUNTER, true, &energy_now, false)) {
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
	const struct rapl_defaults *defaults = get_defaults(rd->rp);
	u64 val;
	int ret;

	cpus_read_lock();
	ret = rapl_write_pl_data(rd, POWER_LIMIT1, PL_ENABLE, mode);
	if (ret)
		goto end;

	ret = rapl_read_pl_data(rd, POWER_LIMIT1, PL_ENABLE, false, &val);
	if (ret)
		goto end;

	if (mode != val) {
		pr_debug("%s cannot be %s\n", power_zone->name,
			 str_enabled_disabled(mode));
		goto end;
	}

	if (defaults->set_floor_freq)
		defaults->set_floor_freq(rd, mode);

end:
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
	const struct rapl_defaults *defaults = get_defaults(rd->rp);
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

static struct rapl_primitive_info *get_rpi(struct rapl_package *rp, int prim)
{
	struct rapl_primitive_info *rpi = rp->priv->rpi;

	if (prim < 0 || prim >= NR_RAPL_PRIMITIVES || !rpi)
		return NULL;

	return &rpi[prim];
}

static int rapl_config(struct rapl_package *rp)
{
	/* defaults_msr can be NULL on unsupported platforms */
	if (!rp->priv->defaults || !rp->priv->rpi)
		return -ENODEV;

	return 0;
}

static enum rapl_primitives
prim_fixups(struct rapl_domain *rd, enum rapl_primitives prim)
{
	const struct rapl_defaults *defaults = get_defaults(rd->rp);

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
			      enum rapl_primitives prim, bool xlate, u64 *data,
			      bool pmu_ctx)
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

	ra.mask = rpi->mask;

	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra, pmu_ctx)) {
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

	return rapl_read_data_raw(rd, prim, xlate, data, false);
}

static int rapl_write_pl_data(struct rapl_domain *rd, int pl,
			       enum pl_prims pl_prim,
			       unsigned long long value)
{
	enum rapl_primitives prim = get_pl_prim(rd, pl, pl_prim);

	if (!is_pl_valid(rd, pl))
		return -EINVAL;

	if (rd->rpl[pl].locked) {
		pr_debug("%s:%s:%s locked by BIOS\n", rd->rp->name, rd->name, pl_names[pl]);
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
int rapl_default_check_unit(struct rapl_domain *rd)
{
	struct reg_action ra;
	u32 value;

	ra.reg = rd->regs[RAPL_DOMAIN_REG_UNIT];
	ra.mask = ~0;
	if (rd->rp->priv->read_raw(get_rid(rd->rp), &ra, false)) {
		pr_err("Failed to read power unit REG 0x%llx on %s:%s, exit.\n",
			ra.reg.val, rd->rp->name, rd->name);
		return -ENODEV;
	}

	value = (ra.value & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	rd->energy_unit = (ENERGY_UNIT_SCALE * MICROJOULE_PER_JOULE) >> value;

	value = (ra.value & POWER_UNIT_MASK) >> POWER_UNIT_OFFSET;
	rd->power_unit = MICROWATT_PER_WATT >> value;

	value = (ra.value & TIME_UNIT_MASK) >> TIME_UNIT_OFFSET;
	rd->time_unit = USEC_PER_SEC >> value;

	pr_debug("Core CPU %s:%s energy=%dpJ, time=%dus, power=%duW\n",
		 rd->rp->name, rd->name, rd->energy_unit, rd->time_unit, rd->power_unit);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(rapl_default_check_unit, "INTEL_RAPL");

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

void rapl_default_set_floor_freq(struct rapl_domain *rd, bool mode)
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
EXPORT_SYMBOL_NS_GPL(rapl_default_set_floor_freq, "INTEL_RAPL");

u64 rapl_default_compute_time_window(struct rapl_domain *rd, u64 value, bool to_raw)
{
	u64 f, y;		/* fraction and exp. used for time unit */

	/*
	 * Special processing based on 2^Y*(1+F/4), refer
	 * to Intel Software Developer's manual Vol.3B: CH 14.9.3.
	 */
	if (!to_raw) {
		f = (value & 0x60) >> 5;
		y = value & 0x1f;
		value = (1ULL << y) * (4 + f) * rd->time_unit / 4;
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

		f = div64_u64(4 * (value - BIT_ULL(y)), BIT_ULL(y));
		value = (y & 0x1f) | ((f & 0x3) << 5);
	}
	return value;
}
EXPORT_SYMBOL_NS_GPL(rapl_default_compute_time_window, "INTEL_RAPL");

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
						rpi->unit, &val, false))
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
	if (rp->priv->read_raw(get_rid(rp), &ra, false) || !ra.value)
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
	const struct rapl_defaults *defaults = get_defaults(rd->rp);
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

		if (rapl_read_pl_data(rd, i, PL_LIMIT, false, &val64))
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

	rp->domains = kzalloc_objs(struct rapl_domain, rp->nr_domains);
	if (!rp->domains)
		return -ENOMEM;

	rapl_init_domains(rp);

	for (rd = rp->domains; rd < rp->domains + rp->nr_domains; rd++) {
		rapl_get_domain_unit(rd);
		rapl_detect_powerlimit(rd);
	}

	return 0;
}

#ifdef CONFIG_PERF_EVENTS

/*
 * Support for RAPL PMU
 *
 * Register a PMU if any of the registered RAPL Packages have the requirement
 * of exposing its energy counters via Perf PMU.
 *
 * PMU Name:
 *	power
 *
 * Events:
 *	Name		Event id	RAPL Domain
 *	energy_cores	0x01		RAPL_DOMAIN_PP0
 *	energy_pkg	0x02		RAPL_DOMAIN_PACKAGE
 *	energy_ram	0x03		RAPL_DOMAIN_DRAM
 *	energy_gpu	0x04		RAPL_DOMAIN_PP1
 *	energy_psys	0x05		RAPL_DOMAIN_PLATFORM
 *
 * Unit:
 *	Joules
 *
 * Scale:
 *	2.3283064365386962890625e-10
 *	The same RAPL domain in different RAPL Packages may have different
 *	energy units. Use 2.3283064365386962890625e-10 (2^-32) Joules as
 *	the fixed unit for all energy counters, and covert each hardware
 *	counter increase to N times of PMU event counter increases.
 *
 * This is fully compatible with the current MSR RAPL PMU. This means that
 * userspace programs like turbostat can use the same code to handle RAPL Perf
 * PMU, no matter what RAPL Interface driver (MSR/TPMI, etc) is running
 * underlying on the platform.
 *
 * Note that RAPL Packages can be probed/removed dynamically, and the events
 * supported by each TPMI RAPL device can be different. Thus the RAPL PMU
 * support is done on demand, which means
 * 1. PMU is registered only if it is needed by a RAPL Package. PMU events for
 *    unsupported counters are not exposed.
 * 2. PMU is unregistered and registered when a new RAPL Package is probed and
 *    supports new counters that are not supported by current PMU.
 * 3. PMU is unregistered when all registered RAPL Packages don't need PMU.
 */

struct rapl_pmu {
	struct pmu pmu;			/* Perf PMU structure */
	u64 timer_ms;			/* Maximum expiration time to avoid counter overflow */
	unsigned long domain_map;	/* Events supported by current registered PMU */
	bool registered;		/* Whether the PMU has been registered or not */
};

static struct rapl_pmu rapl_pmu;

/* PMU helpers */

static void set_pmu_cpumask(struct rapl_package *rp, cpumask_var_t mask)
{
	int cpu;

	if (!rp->has_pmu)
		return;

	/* Only TPMI & MSR RAPL are supported for now */
	if (rp->priv->type != RAPL_IF_TPMI && rp->priv->type != RAPL_IF_MSR)
		return;

	/* TPMI/MSR RAPL uses any CPU in the package for PMU */
	for_each_online_cpu(cpu)
		if (topology_physical_package_id(cpu) == rp->id)
			cpumask_set_cpu(cpu, mask);
}

static bool is_rp_pmu_cpu(struct rapl_package *rp, int cpu)
{
	if (!rp->has_pmu)
		return false;

	/* Only TPMI & MSR RAPL are supported for now */
	if (rp->priv->type != RAPL_IF_TPMI && rp->priv->type != RAPL_IF_MSR)
		return false;

	/* TPMI/MSR RAPL uses any CPU in the package for PMU */
	return topology_physical_package_id(cpu) == rp->id;
}

static struct rapl_package_pmu_data *event_to_pmu_data(struct perf_event *event)
{
	struct rapl_package *rp = event->pmu_private;

	return &rp->pmu_data;
}

/* PMU event callbacks */

static u64 event_read_counter(struct perf_event *event)
{
	struct rapl_package *rp = event->pmu_private;
	u64 val;
	int ret;

	/* Return 0 for unsupported events */
	if (event->hw.idx < 0)
		return 0;

	ret = rapl_read_data_raw(&rp->domains[event->hw.idx], ENERGY_COUNTER, false, &val, true);

	/* Return 0 for failed read */
	if (ret)
		return 0;

	return val;
}

static void __rapl_pmu_event_start(struct perf_event *event)
{
	struct rapl_package_pmu_data *data = event_to_pmu_data(event);

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	event->hw.state = 0;

	list_add_tail(&event->active_entry, &data->active_list);

	local64_set(&event->hw.prev_count, event_read_counter(event));
	if (++data->n_active == 1)
		hrtimer_start(&data->hrtimer, data->timer_interval,
			      HRTIMER_MODE_REL_PINNED);
}

static void rapl_pmu_event_start(struct perf_event *event, int mode)
{
	struct rapl_package_pmu_data *data = event_to_pmu_data(event);
	unsigned long flags;

	raw_spin_lock_irqsave(&data->lock, flags);
	__rapl_pmu_event_start(event);
	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static u64 rapl_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct rapl_package_pmu_data *data = event_to_pmu_data(event);
	u64 prev_raw_count, new_raw_count;
	s64 delta, sdelta;

	/*
	 * Follow the generic code to drain hwc->prev_count.
	 * The loop is not expected to run for multiple times.
	 */
	prev_raw_count = local64_read(&hwc->prev_count);
	do {
		new_raw_count = event_read_counter(event);
	} while (!local64_try_cmpxchg(&hwc->prev_count,
		&prev_raw_count, new_raw_count));


	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 */
	delta = new_raw_count - prev_raw_count;

	/*
	 * Scale delta to smallest unit (2^-32)
	 * users must then scale back: count * 1/(1e9*2^32) to get Joules
	 * or use ldexp(count, -32).
	 * Watts = Joules/Time delta
	 */
	sdelta = delta * data->scale[event->hw.flags];

	local64_add(sdelta, &event->count);

	return new_raw_count;
}

static void rapl_pmu_event_stop(struct perf_event *event, int mode)
{
	struct rapl_package_pmu_data *data = event_to_pmu_data(event);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	raw_spin_lock_irqsave(&data->lock, flags);

	/* Mark event as deactivated and stopped */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		WARN_ON_ONCE(data->n_active <= 0);
		if (--data->n_active == 0)
			hrtimer_cancel(&data->hrtimer);

		list_del(&event->active_entry);

		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	/* Check if update of sw counter is necessary */
	if ((mode & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		rapl_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	raw_spin_unlock_irqrestore(&data->lock, flags);
}

static int rapl_pmu_event_add(struct perf_event *event, int mode)
{
	struct rapl_package_pmu_data *data = event_to_pmu_data(event);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	raw_spin_lock_irqsave(&data->lock, flags);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (mode & PERF_EF_START)
		__rapl_pmu_event_start(event);

	raw_spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static void rapl_pmu_event_del(struct perf_event *event, int flags)
{
	rapl_pmu_event_stop(event, PERF_EF_UPDATE);
}

/* RAPL PMU event ids, same as shown in sysfs */
enum perf_rapl_events {
	PERF_RAPL_PP0 = 1,	/* all cores */
	PERF_RAPL_PKG,		/* entire package */
	PERF_RAPL_RAM,		/* DRAM */
	PERF_RAPL_PP1,		/* gpu */
	PERF_RAPL_PSYS,		/* psys */
	PERF_RAPL_MAX
};

static const int event_to_domain[PERF_RAPL_MAX] = {
	[PERF_RAPL_PP0]		= RAPL_DOMAIN_PP0,
	[PERF_RAPL_PKG]		= RAPL_DOMAIN_PACKAGE,
	[PERF_RAPL_RAM]		= RAPL_DOMAIN_DRAM,
	[PERF_RAPL_PP1]		= RAPL_DOMAIN_PP1,
	[PERF_RAPL_PSYS]	= RAPL_DOMAIN_PLATFORM,
};

static int rapl_pmu_event_init(struct perf_event *event)
{
	struct rapl_package *pos, *rp = NULL;
	u64 cfg = event->attr.config & RAPL_EVENT_MASK;
	int domain, idx;

	/* Only look at RAPL events */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Check for supported events only */
	if (!cfg || cfg >= PERF_RAPL_MAX)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	/* Find out which Package the event belongs to */
	list_for_each_entry(pos, &rapl_packages, plist) {
		if (is_rp_pmu_cpu(pos, event->cpu)) {
			rp = pos;
			break;
		}
	}
	if (!rp)
		return -ENODEV;

	/* Find out which RAPL Domain the event belongs to */
	domain = event_to_domain[cfg];

	event->event_caps |= PERF_EV_CAP_READ_ACTIVE_PKG;
	event->pmu_private = rp;	/* Which package */
	event->hw.flags = domain;	/* Which domain */

	event->hw.idx = -1;
	/* Find out the index in rp->domains[] to get domain pointer */
	for (idx = 0; idx < rp->nr_domains; idx++) {
		if (rp->domains[idx].id == domain) {
			event->hw.idx = idx;
			break;
		}
	}

	return 0;
}

static void rapl_pmu_event_read(struct perf_event *event)
{
	rapl_event_update(event);
}

static enum hrtimer_restart rapl_hrtimer_handle(struct hrtimer *hrtimer)
{
	struct rapl_package_pmu_data *data =
		container_of(hrtimer, struct rapl_package_pmu_data, hrtimer);
	struct perf_event *event;
	unsigned long flags;

	if (!data->n_active)
		return HRTIMER_NORESTART;

	raw_spin_lock_irqsave(&data->lock, flags);

	list_for_each_entry(event, &data->active_list, active_entry)
		rapl_event_update(event);

	raw_spin_unlock_irqrestore(&data->lock, flags);

	hrtimer_forward_now(hrtimer, data->timer_interval);

	return HRTIMER_RESTART;
}

/* PMU sysfs attributes */

/*
 * There are no default events, but we need to create "events" group (with
 * empty attrs) before updating it with detected events.
 */
static struct attribute *attrs_empty[] = {
	NULL,
};

static struct attribute_group pmu_events_group = {
	.name = "events",
	.attrs = attrs_empty,
};

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct rapl_package *rp;
	cpumask_var_t cpu_mask;
	int ret;

	if (!alloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	cpus_read_lock();

	cpumask_clear(cpu_mask);

	/* Choose a cpu for each RAPL Package */
	list_for_each_entry(rp, &rapl_packages, plist) {
		set_pmu_cpumask(rp, cpu_mask);
	}
	cpus_read_unlock();

	ret = cpumap_print_to_pagebuf(true, buf, cpu_mask);

	free_cpumask_var(cpu_mask);

	return ret;
}

static DEVICE_ATTR_RO(cpumask);

static struct attribute *pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group pmu_cpumask_group = {
	.attrs = pmu_cpumask_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");
static struct attribute *pmu_format_attr[] = {
	&format_attr_event.attr,
	NULL
};

static struct attribute_group pmu_format_group = {
	.name = "format",
	.attrs = pmu_format_attr,
};

static const struct attribute_group *pmu_attr_groups[] = {
	&pmu_events_group,
	&pmu_cpumask_group,
	&pmu_format_group,
	NULL
};

#define RAPL_EVENT_ATTR_STR(_name, v, str)					\
static struct perf_pmu_events_attr event_attr_##v = {				\
	.attr		= __ATTR(_name, 0444, perf_event_sysfs_show, NULL),	\
	.event_str	= str,							\
}

RAPL_EVENT_ATTR_STR(energy-cores,	rapl_cores,	"event=0x01");
RAPL_EVENT_ATTR_STR(energy-pkg,		rapl_pkg,	"event=0x02");
RAPL_EVENT_ATTR_STR(energy-ram,		rapl_ram,	"event=0x03");
RAPL_EVENT_ATTR_STR(energy-gpu,		rapl_gpu,	"event=0x04");
RAPL_EVENT_ATTR_STR(energy-psys,	rapl_psys,	"event=0x05");

RAPL_EVENT_ATTR_STR(energy-cores.unit,	rapl_unit_cores,	"Joules");
RAPL_EVENT_ATTR_STR(energy-pkg.unit,	rapl_unit_pkg,		"Joules");
RAPL_EVENT_ATTR_STR(energy-ram.unit,	rapl_unit_ram,		"Joules");
RAPL_EVENT_ATTR_STR(energy-gpu.unit,	rapl_unit_gpu,		"Joules");
RAPL_EVENT_ATTR_STR(energy-psys.unit,	rapl_unit_psys,		"Joules");

RAPL_EVENT_ATTR_STR(energy-cores.scale,	rapl_scale_cores,	"2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-pkg.scale,	rapl_scale_pkg,		"2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-ram.scale,	rapl_scale_ram,		"2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-gpu.scale,	rapl_scale_gpu,		"2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-psys.scale,	rapl_scale_psys,	"2.3283064365386962890625e-10");

#define RAPL_EVENT_GROUP(_name, domain)			\
static struct attribute *pmu_attr_##_name[] = {		\
	&event_attr_rapl_##_name.attr.attr,		\
	&event_attr_rapl_unit_##_name.attr.attr,	\
	&event_attr_rapl_scale_##_name.attr.attr,	\
	NULL						\
};							\
static umode_t is_visible_##_name(struct kobject *kobj, struct attribute *attr, int event)	\
{											\
	return rapl_pmu.domain_map & BIT(domain) ? attr->mode : 0;	\
}							\
static struct attribute_group pmu_group_##_name = {	\
	.name  = "events",				\
	.attrs = pmu_attr_##_name,			\
	.is_visible = is_visible_##_name,		\
}

RAPL_EVENT_GROUP(cores,	RAPL_DOMAIN_PP0);
RAPL_EVENT_GROUP(pkg,	RAPL_DOMAIN_PACKAGE);
RAPL_EVENT_GROUP(ram,	RAPL_DOMAIN_DRAM);
RAPL_EVENT_GROUP(gpu,	RAPL_DOMAIN_PP1);
RAPL_EVENT_GROUP(psys,	RAPL_DOMAIN_PLATFORM);

static const struct attribute_group *pmu_attr_update[] = {
	&pmu_group_cores,
	&pmu_group_pkg,
	&pmu_group_ram,
	&pmu_group_gpu,
	&pmu_group_psys,
	NULL
};

static int rapl_pmu_update(struct rapl_package *rp)
{
	int ret = 0;

	/* Return if PMU already covers all events supported by current RAPL Package */
	if (rapl_pmu.registered && !(rp->domain_map & (~rapl_pmu.domain_map)))
		goto end;

	/* Unregister previous registered PMU */
	if (rapl_pmu.registered)
		perf_pmu_unregister(&rapl_pmu.pmu);

	rapl_pmu.registered = false;
	rapl_pmu.domain_map |= rp->domain_map;

	memset(&rapl_pmu.pmu, 0, sizeof(struct pmu));
	rapl_pmu.pmu.attr_groups = pmu_attr_groups;
	rapl_pmu.pmu.attr_update = pmu_attr_update;
	rapl_pmu.pmu.task_ctx_nr = perf_invalid_context;
	rapl_pmu.pmu.event_init = rapl_pmu_event_init;
	rapl_pmu.pmu.add = rapl_pmu_event_add;
	rapl_pmu.pmu.del = rapl_pmu_event_del;
	rapl_pmu.pmu.start = rapl_pmu_event_start;
	rapl_pmu.pmu.stop = rapl_pmu_event_stop;
	rapl_pmu.pmu.read = rapl_pmu_event_read;
	rapl_pmu.pmu.module = THIS_MODULE;
	rapl_pmu.pmu.capabilities = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT;
	ret = perf_pmu_register(&rapl_pmu.pmu, "power", -1);
	if (ret) {
		pr_info("Failed to register PMU\n");
		return ret;
	}

	rapl_pmu.registered = true;
end:
	rp->has_pmu = true;
	return ret;
}

int rapl_package_add_pmu_locked(struct rapl_package *rp)
{
	struct rapl_package_pmu_data *data = &rp->pmu_data;
	int idx;

	if (rp->has_pmu)
		return -EEXIST;

	for (idx = 0; idx < rp->nr_domains; idx++) {
		struct rapl_domain *rd = &rp->domains[idx];
		int domain = rd->id;
		u64 val;

		if (!test_bit(domain, &rp->domain_map))
			continue;

		/*
		 * The RAPL PMU granularity is 2^-32 Joules
		 * data->scale[]: times of 2^-32 Joules for each ENERGY COUNTER increase
		 */
		val = rd->energy_unit * (1ULL << 32);
		do_div(val, ENERGY_UNIT_SCALE * 1000000);
		data->scale[domain] = val;

		if (!rapl_pmu.timer_ms) {
			struct rapl_primitive_info *rpi = get_rpi(rp, ENERGY_COUNTER);

			/*
			 * Calculate the timer rate:
			 * Use reference of 200W for scaling the timeout to avoid counter
			 * overflows.
			 *
			 * max_count = rpi->mask >> rpi->shift + 1
			 * max_energy_pj = max_count * rd->energy_unit
			 * max_time_sec = (max_energy_pj / 1000000000) / 200w
			 *
			 * rapl_pmu.timer_ms = max_time_sec * 1000 / 2
			 */
			val = (rpi->mask >> rpi->shift) + 1;
			val *= rd->energy_unit;
			do_div(val, 1000000 * 200 * 2);
			rapl_pmu.timer_ms = val;

			pr_debug("%llu ms overflow timer\n", rapl_pmu.timer_ms);
		}

		pr_debug("Domain %s: hw unit %lld * 2^-32 Joules\n", rd->name, data->scale[domain]);
	}

	/* Initialize per package PMU data */
	raw_spin_lock_init(&data->lock);
	INIT_LIST_HEAD(&data->active_list);
	data->timer_interval = ms_to_ktime(rapl_pmu.timer_ms);
	hrtimer_setup(&data->hrtimer, rapl_hrtimer_handle, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	return rapl_pmu_update(rp);
}
EXPORT_SYMBOL_NS_GPL(rapl_package_add_pmu_locked, "INTEL_RAPL");

int rapl_package_add_pmu(struct rapl_package *rp)
{
	guard(cpus_read_lock)();

	return rapl_package_add_pmu_locked(rp);
}
EXPORT_SYMBOL_NS_GPL(rapl_package_add_pmu, "INTEL_RAPL");

void rapl_package_remove_pmu_locked(struct rapl_package *rp)
{
	struct rapl_package *pos;

	if (!rp->has_pmu)
		return;

	list_for_each_entry(pos, &rapl_packages, plist) {
		/* PMU is still needed */
		if (pos->has_pmu && pos != rp)
			return;
	}

	perf_pmu_unregister(&rapl_pmu.pmu);
	memset(&rapl_pmu, 0, sizeof(struct rapl_pmu));
}
EXPORT_SYMBOL_NS_GPL(rapl_package_remove_pmu_locked, "INTEL_RAPL");

void rapl_package_remove_pmu(struct rapl_package *rp)
{
	guard(cpus_read_lock)();

	rapl_package_remove_pmu_locked(rp);
}
EXPORT_SYMBOL_NS_GPL(rapl_package_remove_pmu, "INTEL_RAPL");
#endif

/* called from CPU hotplug notifier, hotplug lock held */
void rapl_remove_package_cpuslocked(struct rapl_package *rp)
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
EXPORT_SYMBOL_NS_GPL(rapl_remove_package_cpuslocked, "INTEL_RAPL");

void rapl_remove_package(struct rapl_package *rp)
{
	guard(cpus_read_lock)();
	rapl_remove_package_cpuslocked(rp);
}
EXPORT_SYMBOL_NS_GPL(rapl_remove_package, "INTEL_RAPL");

/*
 * RAPL Package energy counter scope:
 * 1. AMD/HYGON platforms use per-PKG package energy counter
 * 2. For Intel platforms
 *	2.1 CLX-AP platform has per-DIE package energy counter
 *	2.2 Other platforms that uses MSR RAPL are single die systems so the
 *          package energy counter can be considered as per-PKG/per-DIE,
 *          here it is considered as per-DIE.
 *	2.3 New platforms that use TPMI RAPL doesn't care about the
 *	    scope because they are not MSR/CPU based.
 */
#define rapl_msrs_are_pkg_scope()				\
	(boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||	\
	 boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)

/* caller to ensure CPU hotplug lock is held */
struct rapl_package *rapl_find_package_domain_cpuslocked(int id, struct rapl_if_priv *priv,
							 bool id_is_cpu)
{
	struct rapl_package *rp;
	int uid;

	if (id_is_cpu) {
		uid = rapl_msrs_are_pkg_scope() ?
		      topology_physical_package_id(id) : topology_logical_die_id(id);
		if (uid < 0) {
			pr_err("topology_logical_(package/die)_id() returned a negative value");
			return NULL;
		}
	}
	else
		uid = id;

	list_for_each_entry(rp, &rapl_packages, plist) {
		if (rp->id == uid
		    && rp->priv->control_type == priv->control_type)
			return rp;
	}

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(rapl_find_package_domain_cpuslocked, "INTEL_RAPL");

struct rapl_package *rapl_find_package_domain(int id, struct rapl_if_priv *priv, bool id_is_cpu)
{
	guard(cpus_read_lock)();
	return rapl_find_package_domain_cpuslocked(id, priv, id_is_cpu);
}
EXPORT_SYMBOL_NS_GPL(rapl_find_package_domain, "INTEL_RAPL");

/* called from CPU hotplug notifier, hotplug lock held */
struct rapl_package *rapl_add_package_cpuslocked(int id, struct rapl_if_priv *priv, bool id_is_cpu)
{
	struct rapl_package *rp;
	int ret;

	rp = kzalloc_obj(struct rapl_package);
	if (!rp)
		return ERR_PTR(-ENOMEM);

	if (id_is_cpu) {
		rp->id = rapl_msrs_are_pkg_scope() ?
			 topology_physical_package_id(id) : topology_logical_die_id(id);
		if ((int)(rp->id) < 0) {
			pr_err("topology_logical_(package/die)_id() returned a negative value");
			return ERR_PTR(-EINVAL);
		}
		rp->lead_cpu = id;
		if (!rapl_msrs_are_pkg_scope() && topology_max_dies_per_package() > 1)
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
EXPORT_SYMBOL_NS_GPL(rapl_add_package_cpuslocked, "INTEL_RAPL");

struct rapl_package *rapl_add_package(int id, struct rapl_if_priv *priv, bool id_is_cpu)
{
	guard(cpus_read_lock)();
	return rapl_add_package_cpuslocked(id, priv, id_is_cpu);
}
EXPORT_SYMBOL_NS_GPL(rapl_add_package, "INTEL_RAPL");

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

static int __init rapl_init(void)
{
	return register_pm_notifier(&rapl_pm_notifier);
}

static void __exit rapl_exit(void)
{
	unregister_pm_notifier(&rapl_pm_notifier);
}

fs_initcall(rapl_init);
module_exit(rapl_exit);

MODULE_DESCRIPTION("Intel Runtime Average Power Limit (RAPL) common code");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com>");
MODULE_LICENSE("GPL v2");
