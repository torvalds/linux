// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support Intel/AMD RAPL energy consumption counters
 * Copyright (C) 2013 Google, Inc., Stephane Eranian
 *
 * Intel RAPL interface is specified in the IA-32 Manual Vol3b
 * section 14.7.1 (September 2013)
 *
 * AMD RAPL interface for Fam17h is described in the public PPR:
 * https://bugzilla.kernel.org/show_bug.cgi?id=206537
 *
 * RAPL provides more controls than just reporting energy consumption
 * however here we only expose the 3 energy consumption free running
 * counters (pp0, pkg, dram).
 *
 * Each of those counters increments in a power unit defined by the
 * RAPL_POWER_UNIT MSR. On SandyBridge, this unit is 1/(2^16) Joules
 * but it can vary.
 *
 * Counter to rapl events mappings:
 *
 *  pp0 counter: consumption of all physical cores (power plane 0)
 * 	  event: rapl_energy_cores
 *    perf code: 0x1
 *
 *  pkg counter: consumption of the whole processor package
 *	  event: rapl_energy_pkg
 *    perf code: 0x2
 *
 * dram counter: consumption of the dram domain (servers only)
 *	  event: rapl_energy_dram
 *    perf code: 0x3
 *
 * gpu counter: consumption of the builtin-gpu domain (client only)
 *	  event: rapl_energy_gpu
 *    perf code: 0x4
 *
 *  psys counter: consumption of the builtin-psys domain (client only)
 *	  event: rapl_energy_psys
 *    perf code: 0x5
 *
 *  core counter: consumption of a single physical core
 *	  event: rapl_energy_core (power_core PMU)
 *    perf code: 0x1
 *
 * We manage those counters as free running (read-only). They may be
 * use simultaneously by other tools, such as turbostat.
 *
 * The events only support system-wide mode counting. There is no
 * sampling support because it does not make sense and is not
 * supported by the RAPL hardware.
 *
 * Because we want to avoid floating-point operations in the kernel,
 * the events are all reported in fixed point arithmetic (32.32).
 * Tools must adjust the counts to convert them to Watts using
 * the duration of the measurement. Tools may use a function such as
 * ldexp(raw_count, -32);
 */

#define pr_fmt(fmt) "RAPL PMU: " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/nospec.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include "perf_event.h"
#include "probe.h"

MODULE_DESCRIPTION("Support Intel/AMD RAPL energy consumption counters");
MODULE_LICENSE("GPL");

/*
 * RAPL energy status counters
 */
enum perf_rapl_pkg_events {
	PERF_RAPL_PP0 = 0,		/* all cores */
	PERF_RAPL_PKG,			/* entire package */
	PERF_RAPL_RAM,			/* DRAM */
	PERF_RAPL_PP1,			/* gpu */
	PERF_RAPL_PSYS,			/* psys */

	PERF_RAPL_PKG_EVENTS_MAX,
	NR_RAPL_PKG_DOMAINS = PERF_RAPL_PKG_EVENTS_MAX,
};

#define PERF_RAPL_CORE			0		/* single core */
#define PERF_RAPL_CORE_EVENTS_MAX	1
#define NR_RAPL_CORE_DOMAINS		PERF_RAPL_CORE_EVENTS_MAX

static const char *const rapl_pkg_domain_names[NR_RAPL_PKG_DOMAINS] __initconst = {
	"pp0-core",
	"package",
	"dram",
	"pp1-gpu",
	"psys",
};

static const char *const rapl_core_domain_name __initconst = "core";

/*
 * event code: LSB 8 bits, passed in attr->config
 * any other bit is reserved
 */
#define RAPL_EVENT_MASK	0xFFULL
#define RAPL_CNTR_WIDTH 32

#define RAPL_EVENT_ATTR_STR(_name, v, str)					\
static struct perf_pmu_events_attr event_attr_##v = {				\
	.attr		= __ATTR(_name, 0444, perf_event_sysfs_show, NULL),	\
	.id		= 0,							\
	.event_str	= str,							\
};

/*
 * RAPL Package energy counter scope:
 * 1. AMD/HYGON platforms have a per-PKG package energy counter
 * 2. For Intel platforms
 *	2.1. CLX-AP is multi-die and its RAPL MSRs are die-scope
 *	2.2. Other Intel platforms are single die systems so the scope can be
 *	     considered as either pkg-scope or die-scope, and we are considering
 *	     them as die-scope.
 */
#define rapl_pkg_pmu_is_pkg_scope()				\
	(boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||	\
	 boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)

struct rapl_pmu {
	raw_spinlock_t		lock;
	int			n_active;
	int			cpu;
	struct list_head	active_list;
	struct pmu		*pmu;
	ktime_t			timer_interval;
	struct hrtimer		hrtimer;
};

struct rapl_pmus {
	struct pmu		pmu;
	unsigned int		nr_rapl_pmu;
	unsigned int		cntr_mask;
	struct rapl_pmu		*rapl_pmu[] __counted_by(nr_rapl_pmu);
};

enum rapl_unit_quirk {
	RAPL_UNIT_QUIRK_NONE,
	RAPL_UNIT_QUIRK_INTEL_HSW,
	RAPL_UNIT_QUIRK_INTEL_SPR,
};

struct rapl_model {
	struct perf_msr *rapl_pkg_msrs;
	struct perf_msr *rapl_core_msrs;
	unsigned long	pkg_events;
	unsigned long	core_events;
	unsigned int	msr_power_unit;
	enum rapl_unit_quirk	unit_quirk;
};

 /* 1/2^hw_unit Joule */
static int rapl_pkg_hw_unit[NR_RAPL_PKG_DOMAINS] __read_mostly;
static int rapl_core_hw_unit __read_mostly;
static struct rapl_pmus *rapl_pmus_pkg;
static struct rapl_pmus *rapl_pmus_core;
static u64 rapl_timer_ms;
static struct rapl_model *rapl_model;

/*
 * Helper function to get the correct topology id according to the
 * RAPL PMU scope.
 */
static inline unsigned int get_rapl_pmu_idx(int cpu, int scope)
{
	/*
	 * Returns unsigned int, which converts the '-1' return value
	 * (for non-existent mappings in topology map) to UINT_MAX, so
	 * the error check in the caller is simplified.
	 */
	switch (scope) {
	case PERF_PMU_SCOPE_PKG:
		return topology_logical_package_id(cpu);
	case PERF_PMU_SCOPE_DIE:
		return topology_logical_die_id(cpu);
	case PERF_PMU_SCOPE_CORE:
		return topology_logical_core_id(cpu);
	default:
		return -EINVAL;
	}
}

static inline u64 rapl_read_counter(struct perf_event *event)
{
	u64 raw;
	rdmsrq(event->hw.event_base, raw);
	return raw;
}

static inline u64 rapl_scale(u64 v, struct perf_event *event)
{
	int hw_unit = rapl_pkg_hw_unit[event->hw.config - 1];

	if (event->pmu->scope == PERF_PMU_SCOPE_CORE)
		hw_unit = rapl_core_hw_unit;

	/*
	 * scale delta to smallest unit (1/2^32)
	 * users must then scale back: count * 1/(1e9*2^32) to get Joules
	 * or use ldexp(count, -32).
	 * Watts = Joules/Time delta
	 */
	return v << (32 - hw_unit);
}

static u64 rapl_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	s64 delta, sdelta;
	int shift = RAPL_CNTR_WIDTH;

	prev_raw_count = local64_read(&hwc->prev_count);
	do {
		rdmsrq(event->hw.event_base, new_raw_count);
	} while (!local64_try_cmpxchg(&hwc->prev_count,
				      &prev_raw_count, new_raw_count));

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (event-)time and add that to the generic event.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	sdelta = rapl_scale(delta, event);

	local64_add(sdelta, &event->count);

	return new_raw_count;
}

static void rapl_start_hrtimer(struct rapl_pmu *pmu)
{
       hrtimer_start(&pmu->hrtimer, pmu->timer_interval,
		     HRTIMER_MODE_REL_PINNED);
}

static enum hrtimer_restart rapl_hrtimer_handle(struct hrtimer *hrtimer)
{
	struct rapl_pmu *rapl_pmu = container_of(hrtimer, struct rapl_pmu, hrtimer);
	struct perf_event *event;
	unsigned long flags;

	if (!rapl_pmu->n_active)
		return HRTIMER_NORESTART;

	raw_spin_lock_irqsave(&rapl_pmu->lock, flags);

	list_for_each_entry(event, &rapl_pmu->active_list, active_entry)
		rapl_event_update(event);

	raw_spin_unlock_irqrestore(&rapl_pmu->lock, flags);

	hrtimer_forward_now(hrtimer, rapl_pmu->timer_interval);

	return HRTIMER_RESTART;
}

static void rapl_hrtimer_init(struct rapl_pmu *rapl_pmu)
{
	struct hrtimer *hr = &rapl_pmu->hrtimer;

	hrtimer_setup(hr, rapl_hrtimer_handle, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}

static void __rapl_pmu_event_start(struct rapl_pmu *rapl_pmu,
				   struct perf_event *event)
{
	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	event->hw.state = 0;

	list_add_tail(&event->active_entry, &rapl_pmu->active_list);

	local64_set(&event->hw.prev_count, rapl_read_counter(event));

	rapl_pmu->n_active++;
	if (rapl_pmu->n_active == 1)
		rapl_start_hrtimer(rapl_pmu);
}

static void rapl_pmu_event_start(struct perf_event *event, int mode)
{
	struct rapl_pmu *rapl_pmu = event->pmu_private;
	unsigned long flags;

	raw_spin_lock_irqsave(&rapl_pmu->lock, flags);
	__rapl_pmu_event_start(rapl_pmu, event);
	raw_spin_unlock_irqrestore(&rapl_pmu->lock, flags);
}

static void rapl_pmu_event_stop(struct perf_event *event, int mode)
{
	struct rapl_pmu *rapl_pmu = event->pmu_private;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	raw_spin_lock_irqsave(&rapl_pmu->lock, flags);

	/* mark event as deactivated and stopped */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		WARN_ON_ONCE(rapl_pmu->n_active <= 0);
		rapl_pmu->n_active--;
		if (rapl_pmu->n_active == 0)
			hrtimer_cancel(&rapl_pmu->hrtimer);

		list_del(&event->active_entry);

		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	/* check if update of sw counter is necessary */
	if ((mode & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		rapl_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	raw_spin_unlock_irqrestore(&rapl_pmu->lock, flags);
}

static int rapl_pmu_event_add(struct perf_event *event, int mode)
{
	struct rapl_pmu *rapl_pmu = event->pmu_private;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	raw_spin_lock_irqsave(&rapl_pmu->lock, flags);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (mode & PERF_EF_START)
		__rapl_pmu_event_start(rapl_pmu, event);

	raw_spin_unlock_irqrestore(&rapl_pmu->lock, flags);

	return 0;
}

static void rapl_pmu_event_del(struct perf_event *event, int flags)
{
	rapl_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int rapl_pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config & RAPL_EVENT_MASK;
	int bit, rapl_pmus_scope, ret = 0;
	struct rapl_pmu *rapl_pmu;
	unsigned int rapl_pmu_idx;
	struct rapl_pmus *rapl_pmus;

	/* only look at RAPL events */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	/* check only supported bits are set */
	if (event->attr.config & ~RAPL_EVENT_MASK)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	rapl_pmus = container_of(event->pmu, struct rapl_pmus, pmu);
	if (!rapl_pmus)
		return -EINVAL;
	rapl_pmus_scope = rapl_pmus->pmu.scope;

	if (rapl_pmus_scope == PERF_PMU_SCOPE_PKG || rapl_pmus_scope == PERF_PMU_SCOPE_DIE) {
		cfg = array_index_nospec((long)cfg, NR_RAPL_PKG_DOMAINS + 1);
		if (!cfg || cfg >= NR_RAPL_PKG_DOMAINS + 1)
			return -EINVAL;

		bit = cfg - 1;
		event->hw.event_base = rapl_model->rapl_pkg_msrs[bit].msr;
	} else if (rapl_pmus_scope == PERF_PMU_SCOPE_CORE) {
		cfg = array_index_nospec((long)cfg, NR_RAPL_CORE_DOMAINS + 1);
		if (!cfg || cfg >= NR_RAPL_PKG_DOMAINS + 1)
			return -EINVAL;

		bit = cfg - 1;
		event->hw.event_base = rapl_model->rapl_core_msrs[bit].msr;
	} else
		return -EINVAL;

	/* check event supported */
	if (!(rapl_pmus->cntr_mask & (1 << bit)))
		return -EINVAL;

	rapl_pmu_idx = get_rapl_pmu_idx(event->cpu, rapl_pmus_scope);
	if (rapl_pmu_idx >= rapl_pmus->nr_rapl_pmu)
		return -EINVAL;
	/* must be done before validate_group */
	rapl_pmu = rapl_pmus->rapl_pmu[rapl_pmu_idx];
	if (!rapl_pmu)
		return -EINVAL;

	event->pmu_private = rapl_pmu;
	event->hw.config = cfg;
	event->hw.idx = bit;

	return ret;
}

static void rapl_pmu_event_read(struct perf_event *event)
{
	rapl_event_update(event);
}

RAPL_EVENT_ATTR_STR(energy-cores, rapl_cores, "event=0x01");
RAPL_EVENT_ATTR_STR(energy-pkg  ,   rapl_pkg, "event=0x02");
RAPL_EVENT_ATTR_STR(energy-ram  ,   rapl_ram, "event=0x03");
RAPL_EVENT_ATTR_STR(energy-gpu  ,   rapl_gpu, "event=0x04");
RAPL_EVENT_ATTR_STR(energy-psys,   rapl_psys, "event=0x05");
RAPL_EVENT_ATTR_STR(energy-core,   rapl_core, "event=0x01");

RAPL_EVENT_ATTR_STR(energy-cores.unit, rapl_cores_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-pkg.unit  ,   rapl_pkg_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-ram.unit  ,   rapl_ram_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-gpu.unit  ,   rapl_gpu_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-psys.unit,   rapl_psys_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-core.unit,   rapl_core_unit, "Joules");

/*
 * we compute in 0.23 nJ increments regardless of MSR
 */
RAPL_EVENT_ATTR_STR(energy-cores.scale, rapl_cores_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-pkg.scale,     rapl_pkg_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-ram.scale,     rapl_ram_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-gpu.scale,     rapl_gpu_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-psys.scale,   rapl_psys_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-core.scale,   rapl_core_scale, "2.3283064365386962890625e-10");

/*
 * There are no default events, but we need to create
 * "events" group (with empty attrs) before updating
 * it with detected events.
 */
static struct attribute *attrs_empty[] = {
	NULL,
};

static struct attribute_group rapl_pmu_events_group = {
	.name = "events",
	.attrs = attrs_empty,
};

PMU_FORMAT_ATTR(event, "config:0-7");
static struct attribute *rapl_formats_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group rapl_pmu_format_group = {
	.name = "format",
	.attrs = rapl_formats_attr,
};

static const struct attribute_group *rapl_attr_groups[] = {
	&rapl_pmu_format_group,
	&rapl_pmu_events_group,
	NULL,
};

static const struct attribute_group *rapl_core_attr_groups[] = {
	&rapl_pmu_format_group,
	&rapl_pmu_events_group,
	NULL,
};

static struct attribute *rapl_events_cores[] = {
	EVENT_PTR(rapl_cores),
	EVENT_PTR(rapl_cores_unit),
	EVENT_PTR(rapl_cores_scale),
	NULL,
};

static struct attribute_group rapl_events_cores_group = {
	.name  = "events",
	.attrs = rapl_events_cores,
};

static struct attribute *rapl_events_pkg[] = {
	EVENT_PTR(rapl_pkg),
	EVENT_PTR(rapl_pkg_unit),
	EVENT_PTR(rapl_pkg_scale),
	NULL,
};

static struct attribute_group rapl_events_pkg_group = {
	.name  = "events",
	.attrs = rapl_events_pkg,
};

static struct attribute *rapl_events_ram[] = {
	EVENT_PTR(rapl_ram),
	EVENT_PTR(rapl_ram_unit),
	EVENT_PTR(rapl_ram_scale),
	NULL,
};

static struct attribute_group rapl_events_ram_group = {
	.name  = "events",
	.attrs = rapl_events_ram,
};

static struct attribute *rapl_events_gpu[] = {
	EVENT_PTR(rapl_gpu),
	EVENT_PTR(rapl_gpu_unit),
	EVENT_PTR(rapl_gpu_scale),
	NULL,
};

static struct attribute_group rapl_events_gpu_group = {
	.name  = "events",
	.attrs = rapl_events_gpu,
};

static struct attribute *rapl_events_psys[] = {
	EVENT_PTR(rapl_psys),
	EVENT_PTR(rapl_psys_unit),
	EVENT_PTR(rapl_psys_scale),
	NULL,
};

static struct attribute_group rapl_events_psys_group = {
	.name  = "events",
	.attrs = rapl_events_psys,
};

static struct attribute *rapl_events_core[] = {
	EVENT_PTR(rapl_core),
	EVENT_PTR(rapl_core_unit),
	EVENT_PTR(rapl_core_scale),
	NULL,
};

static struct attribute_group rapl_events_core_group = {
	.name  = "events",
	.attrs = rapl_events_core,
};

static bool test_msr(int idx, void *data)
{
	return test_bit(idx, (unsigned long *) data);
}

/* Only lower 32bits of the MSR represents the energy counter */
#define RAPL_MSR_MASK 0xFFFFFFFF

static struct perf_msr intel_rapl_msrs[] = {
	[PERF_RAPL_PP0]  = { MSR_PP0_ENERGY_STATUS,      &rapl_events_cores_group, test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PKG]  = { MSR_PKG_ENERGY_STATUS,      &rapl_events_pkg_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_RAM]  = { MSR_DRAM_ENERGY_STATUS,     &rapl_events_ram_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PP1]  = { MSR_PP1_ENERGY_STATUS,      &rapl_events_gpu_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PSYS] = { MSR_PLATFORM_ENERGY_STATUS, &rapl_events_psys_group,  test_msr, false, RAPL_MSR_MASK },
};

static struct perf_msr intel_rapl_spr_msrs[] = {
	[PERF_RAPL_PP0]  = { MSR_PP0_ENERGY_STATUS,      &rapl_events_cores_group, test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PKG]  = { MSR_PKG_ENERGY_STATUS,      &rapl_events_pkg_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_RAM]  = { MSR_DRAM_ENERGY_STATUS,     &rapl_events_ram_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PP1]  = { MSR_PP1_ENERGY_STATUS,      &rapl_events_gpu_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_PSYS] = { MSR_PLATFORM_ENERGY_STATUS, &rapl_events_psys_group,  test_msr, true, RAPL_MSR_MASK },
};

/*
 * Force to PERF_RAPL_PKG_EVENTS_MAX size due to:
 * - perf_msr_probe(PERF_RAPL_PKG_EVENTS_MAX)
 * - want to use same event codes across both architectures
 */
static struct perf_msr amd_rapl_pkg_msrs[] = {
	[PERF_RAPL_PP0]  = { 0, &rapl_events_cores_group, NULL, false, 0 },
	[PERF_RAPL_PKG]  = { MSR_AMD_PKG_ENERGY_STATUS,  &rapl_events_pkg_group,   test_msr, false, RAPL_MSR_MASK },
	[PERF_RAPL_RAM]  = { 0, &rapl_events_ram_group,   NULL, false, 0 },
	[PERF_RAPL_PP1]  = { 0, &rapl_events_gpu_group,   NULL, false, 0 },
	[PERF_RAPL_PSYS] = { 0, &rapl_events_psys_group,  NULL, false, 0 },
};

static struct perf_msr amd_rapl_core_msrs[] = {
	[PERF_RAPL_CORE] = { MSR_AMD_CORE_ENERGY_STATUS, &rapl_events_core_group,
				 test_msr, false, RAPL_MSR_MASK },
};

static int rapl_check_hw_unit(void)
{
	u64 msr_rapl_power_unit_bits;
	int i;

	/* protect rdmsrq() to handle virtualization */
	if (rdmsrq_safe(rapl_model->msr_power_unit, &msr_rapl_power_unit_bits))
		return -1;
	for (i = 0; i < NR_RAPL_PKG_DOMAINS; i++)
		rapl_pkg_hw_unit[i] = (msr_rapl_power_unit_bits >> 8) & 0x1FULL;

	rapl_core_hw_unit = (msr_rapl_power_unit_bits >> 8) & 0x1FULL;

	switch (rapl_model->unit_quirk) {
	/*
	 * DRAM domain on HSW server and KNL has fixed energy unit which can be
	 * different than the unit from power unit MSR. See
	 * "Intel Xeon Processor E5-1600 and E5-2600 v3 Product Families, V2
	 * of 2. Datasheet, September 2014, Reference Number: 330784-001 "
	 */
	case RAPL_UNIT_QUIRK_INTEL_HSW:
		rapl_pkg_hw_unit[PERF_RAPL_RAM] = 16;
		break;
	/* SPR uses a fixed energy unit for Psys domain. */
	case RAPL_UNIT_QUIRK_INTEL_SPR:
		rapl_pkg_hw_unit[PERF_RAPL_PSYS] = 0;
		break;
	default:
		break;
	}

	/*
	 * Calculate the timer rate:
	 * Use reference of 200W for scaling the timeout to avoid counter
	 * overflows. 200W = 200 Joules/sec
	 * Divide interval by 2 to avoid lockstep (2 * 100)
	 * if hw unit is 32, then we use 2 ms 1/200/2
	 */
	rapl_timer_ms = 2;
	if (rapl_pkg_hw_unit[0] < 32) {
		rapl_timer_ms = (1000 / (2 * 100));
		rapl_timer_ms *= (1ULL << (32 - rapl_pkg_hw_unit[0] - 1));
	}
	return 0;
}

static void __init rapl_advertise(void)
{
	int i;
	int num_counters = hweight32(rapl_pmus_pkg->cntr_mask);

	if (rapl_pmus_core)
		num_counters += hweight32(rapl_pmus_core->cntr_mask);

	pr_info("API unit is 2^-32 Joules, %d fixed counters, %llu ms ovfl timer\n",
		num_counters, rapl_timer_ms);

	for (i = 0; i < NR_RAPL_PKG_DOMAINS; i++) {
		if (rapl_pmus_pkg->cntr_mask & (1 << i)) {
			pr_info("hw unit of domain %s 2^-%d Joules\n",
				rapl_pkg_domain_names[i], rapl_pkg_hw_unit[i]);
		}
	}

	if (rapl_pmus_core && (rapl_pmus_core->cntr_mask & (1 << PERF_RAPL_CORE)))
		pr_info("hw unit of domain %s 2^-%d Joules\n",
			rapl_core_domain_name, rapl_core_hw_unit);
}

static void cleanup_rapl_pmus(struct rapl_pmus *rapl_pmus)
{
	int i;

	for (i = 0; i < rapl_pmus->nr_rapl_pmu; i++)
		kfree(rapl_pmus->rapl_pmu[i]);
	kfree(rapl_pmus);
}

static const struct attribute_group *rapl_attr_update[] = {
	&rapl_events_cores_group,
	&rapl_events_pkg_group,
	&rapl_events_ram_group,
	&rapl_events_gpu_group,
	&rapl_events_psys_group,
	NULL,
};

static const struct attribute_group *rapl_core_attr_update[] = {
	&rapl_events_core_group,
	NULL,
};

static int __init init_rapl_pmu(struct rapl_pmus *rapl_pmus)
{
	struct rapl_pmu *rapl_pmu;
	int idx;

	for (idx = 0; idx < rapl_pmus->nr_rapl_pmu; idx++) {
		rapl_pmu = kzalloc(sizeof(*rapl_pmu), GFP_KERNEL);
		if (!rapl_pmu)
			goto free;

		raw_spin_lock_init(&rapl_pmu->lock);
		INIT_LIST_HEAD(&rapl_pmu->active_list);
		rapl_pmu->pmu = &rapl_pmus->pmu;
		rapl_pmu->timer_interval = ms_to_ktime(rapl_timer_ms);
		rapl_hrtimer_init(rapl_pmu);

		rapl_pmus->rapl_pmu[idx] = rapl_pmu;
	}

	return 0;
free:
	for (; idx > 0; idx--)
		kfree(rapl_pmus->rapl_pmu[idx - 1]);
	return -ENOMEM;
}

static int __init init_rapl_pmus(struct rapl_pmus **rapl_pmus_ptr, int rapl_pmu_scope,
				 const struct attribute_group **rapl_attr_groups,
				 const struct attribute_group **rapl_attr_update)
{
	int nr_rapl_pmu = topology_max_packages();
	struct rapl_pmus *rapl_pmus;
	int ret;

	/*
	 * rapl_pmu_scope must be either PKG, DIE or CORE
	 */
	if (rapl_pmu_scope == PERF_PMU_SCOPE_DIE)
		nr_rapl_pmu	*= topology_max_dies_per_package();
	else if (rapl_pmu_scope == PERF_PMU_SCOPE_CORE)
		nr_rapl_pmu	*= topology_num_cores_per_package();
	else if (rapl_pmu_scope != PERF_PMU_SCOPE_PKG)
		return -EINVAL;

	rapl_pmus = kzalloc(struct_size(rapl_pmus, rapl_pmu, nr_rapl_pmu), GFP_KERNEL);
	if (!rapl_pmus)
		return -ENOMEM;

	*rapl_pmus_ptr = rapl_pmus;

	rapl_pmus->nr_rapl_pmu		= nr_rapl_pmu;
	rapl_pmus->pmu.attr_groups	= rapl_attr_groups;
	rapl_pmus->pmu.attr_update	= rapl_attr_update;
	rapl_pmus->pmu.task_ctx_nr	= perf_invalid_context;
	rapl_pmus->pmu.event_init	= rapl_pmu_event_init;
	rapl_pmus->pmu.add		= rapl_pmu_event_add;
	rapl_pmus->pmu.del		= rapl_pmu_event_del;
	rapl_pmus->pmu.start		= rapl_pmu_event_start;
	rapl_pmus->pmu.stop		= rapl_pmu_event_stop;
	rapl_pmus->pmu.read		= rapl_pmu_event_read;
	rapl_pmus->pmu.scope		= rapl_pmu_scope;
	rapl_pmus->pmu.module		= THIS_MODULE;
	rapl_pmus->pmu.capabilities	= PERF_PMU_CAP_NO_EXCLUDE;

	ret = init_rapl_pmu(rapl_pmus);
	if (ret)
		kfree(rapl_pmus);

	return ret;
}

static struct rapl_model model_snb = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_PP1),
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_msrs,
};

static struct rapl_model model_snbep = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM),
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_msrs,
};

static struct rapl_model model_hsw = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM) |
			  BIT(PERF_RAPL_PP1),
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_msrs,
};

static struct rapl_model model_hsx = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM),
	.unit_quirk	= RAPL_UNIT_QUIRK_INTEL_HSW,
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_msrs,
};

static struct rapl_model model_knl = {
	.pkg_events	= BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM),
	.unit_quirk	= RAPL_UNIT_QUIRK_INTEL_HSW,
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_msrs,
};

static struct rapl_model model_skl = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM) |
			  BIT(PERF_RAPL_PP1) |
			  BIT(PERF_RAPL_PSYS),
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs      = intel_rapl_msrs,
};

static struct rapl_model model_spr = {
	.pkg_events	= BIT(PERF_RAPL_PP0) |
			  BIT(PERF_RAPL_PKG) |
			  BIT(PERF_RAPL_RAM) |
			  BIT(PERF_RAPL_PSYS),
	.unit_quirk	= RAPL_UNIT_QUIRK_INTEL_SPR,
	.msr_power_unit = MSR_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= intel_rapl_spr_msrs,
};

static struct rapl_model model_amd_hygon = {
	.pkg_events	= BIT(PERF_RAPL_PKG),
	.core_events	= BIT(PERF_RAPL_CORE),
	.msr_power_unit = MSR_AMD_RAPL_POWER_UNIT,
	.rapl_pkg_msrs	= amd_rapl_pkg_msrs,
	.rapl_core_msrs	= amd_rapl_core_msrs,
};

static const struct x86_cpu_id rapl_model_match[] __initconst = {
	X86_MATCH_FEATURE(X86_FEATURE_RAPL,	&model_amd_hygon),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE,	&model_snb),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE_X,	&model_snbep),
	X86_MATCH_VFM(INTEL_IVYBRIDGE,		&model_snb),
	X86_MATCH_VFM(INTEL_IVYBRIDGE_X,	&model_snbep),
	X86_MATCH_VFM(INTEL_HASWELL,		&model_hsw),
	X86_MATCH_VFM(INTEL_HASWELL_X,		&model_hsx),
	X86_MATCH_VFM(INTEL_HASWELL_L,		&model_hsw),
	X86_MATCH_VFM(INTEL_HASWELL_G,		&model_hsw),
	X86_MATCH_VFM(INTEL_BROADWELL,		&model_hsw),
	X86_MATCH_VFM(INTEL_BROADWELL_G,	&model_hsw),
	X86_MATCH_VFM(INTEL_BROADWELL_X,	&model_hsx),
	X86_MATCH_VFM(INTEL_BROADWELL_D,	&model_hsx),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNL,	&model_knl),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNM,	&model_knl),
	X86_MATCH_VFM(INTEL_SKYLAKE_L,		&model_skl),
	X86_MATCH_VFM(INTEL_SKYLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_SKYLAKE_X,		&model_hsx),
	X86_MATCH_VFM(INTEL_KABYLAKE_L,		&model_skl),
	X86_MATCH_VFM(INTEL_KABYLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_CANNONLAKE_L,	&model_skl),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT,	&model_hsw),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_D,	&model_hsw),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_PLUS,	&model_hsw),
	X86_MATCH_VFM(INTEL_ICELAKE_L,		&model_skl),
	X86_MATCH_VFM(INTEL_ICELAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_ICELAKE_D,		&model_hsx),
	X86_MATCH_VFM(INTEL_ICELAKE_X,		&model_hsx),
	X86_MATCH_VFM(INTEL_COMETLAKE_L,	&model_skl),
	X86_MATCH_VFM(INTEL_COMETLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L,	&model_skl),
	X86_MATCH_VFM(INTEL_TIGERLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_ALDERLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,	&model_skl),
	X86_MATCH_VFM(INTEL_ATOM_GRACEMONT,	&model_skl),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X,	&model_spr),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X,	&model_spr),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,	&model_skl),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,	&model_skl),
	X86_MATCH_VFM(INTEL_METEORLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_METEORLAKE_L,	&model_skl),
	X86_MATCH_VFM(INTEL_ARROWLAKE_H,	&model_skl),
	X86_MATCH_VFM(INTEL_ARROWLAKE,		&model_skl),
	X86_MATCH_VFM(INTEL_ARROWLAKE_U,	&model_skl),
	X86_MATCH_VFM(INTEL_LUNARLAKE_M,	&model_skl),
	{},
};
MODULE_DEVICE_TABLE(x86cpu, rapl_model_match);

static int __init rapl_pmu_init(void)
{
	const struct x86_cpu_id *id;
	int rapl_pkg_pmu_scope = PERF_PMU_SCOPE_DIE;
	int ret;

	if (rapl_pkg_pmu_is_pkg_scope())
		rapl_pkg_pmu_scope = PERF_PMU_SCOPE_PKG;

	id = x86_match_cpu(rapl_model_match);
	if (!id)
		return -ENODEV;

	rapl_model = (struct rapl_model *) id->driver_data;

	ret = rapl_check_hw_unit();
	if (ret)
		return ret;

	ret = init_rapl_pmus(&rapl_pmus_pkg, rapl_pkg_pmu_scope, rapl_attr_groups,
			     rapl_attr_update);
	if (ret)
		return ret;

	rapl_pmus_pkg->cntr_mask = perf_msr_probe(rapl_model->rapl_pkg_msrs,
						  PERF_RAPL_PKG_EVENTS_MAX, false,
						  (void *) &rapl_model->pkg_events);

	ret = perf_pmu_register(&rapl_pmus_pkg->pmu, "power", -1);
	if (ret)
		goto out;

	if (rapl_model->core_events) {
		ret = init_rapl_pmus(&rapl_pmus_core, PERF_PMU_SCOPE_CORE,
				     rapl_core_attr_groups,
				     rapl_core_attr_update);
		if (ret) {
			pr_warn("power-core PMU initialization failed (%d)\n", ret);
			goto core_init_failed;
		}

		rapl_pmus_core->cntr_mask = perf_msr_probe(rapl_model->rapl_core_msrs,
						     PERF_RAPL_CORE_EVENTS_MAX, false,
						     (void *) &rapl_model->core_events);

		ret = perf_pmu_register(&rapl_pmus_core->pmu, "power_core", -1);
		if (ret) {
			pr_warn("power-core PMU registration failed (%d)\n", ret);
			cleanup_rapl_pmus(rapl_pmus_core);
		}
	}

core_init_failed:
	rapl_advertise();
	return 0;

out:
	pr_warn("Initialization failed (%d), disabled\n", ret);
	cleanup_rapl_pmus(rapl_pmus_pkg);
	return ret;
}
module_init(rapl_pmu_init);

static void __exit intel_rapl_exit(void)
{
	if (rapl_pmus_core) {
		perf_pmu_unregister(&rapl_pmus_core->pmu);
		cleanup_rapl_pmus(rapl_pmus_core);
	}
	perf_pmu_unregister(&rapl_pmus_pkg->pmu);
	cleanup_rapl_pmus(rapl_pmus_pkg);
}
module_exit(intel_rapl_exit);
