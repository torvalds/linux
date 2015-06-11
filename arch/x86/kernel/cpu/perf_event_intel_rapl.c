/*
 * perf_event_intel_rapl.c: support Intel RAPL energy consumption counters
 * Copyright (C) 2013 Google, Inc., Stephane Eranian
 *
 * Intel RAPL interface is specified in the IA-32 Manual Vol3b
 * section 14.7.1 (September 2013)
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
 * dram counter: consumption of the builtin-gpu domain (client only)
 *	  event: rapl_energy_gpu
 *    perf code: 0x4
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <asm/cpu_device_id.h>
#include "perf_event.h"

/*
 * RAPL energy status counters
 */
#define RAPL_IDX_PP0_NRG_STAT	0	/* all cores */
#define INTEL_RAPL_PP0		0x1	/* pseudo-encoding */
#define RAPL_IDX_PKG_NRG_STAT	1	/* entire package */
#define INTEL_RAPL_PKG		0x2	/* pseudo-encoding */
#define RAPL_IDX_RAM_NRG_STAT	2	/* DRAM */
#define INTEL_RAPL_RAM		0x3	/* pseudo-encoding */
#define RAPL_IDX_PP1_NRG_STAT	3	/* gpu */
#define INTEL_RAPL_PP1		0x4	/* pseudo-encoding */

#define NR_RAPL_DOMAINS         0x4
static const char *rapl_domain_names[NR_RAPL_DOMAINS] __initconst = {
	"pp0-core",
	"package",
	"dram",
	"pp1-gpu",
};

/* Clients have PP0, PKG */
#define RAPL_IDX_CLN	(1<<RAPL_IDX_PP0_NRG_STAT|\
			 1<<RAPL_IDX_PKG_NRG_STAT|\
			 1<<RAPL_IDX_PP1_NRG_STAT)

/* Servers have PP0, PKG, RAM */
#define RAPL_IDX_SRV	(1<<RAPL_IDX_PP0_NRG_STAT|\
			 1<<RAPL_IDX_PKG_NRG_STAT|\
			 1<<RAPL_IDX_RAM_NRG_STAT)

/* Servers have PP0, PKG, RAM, PP1 */
#define RAPL_IDX_HSW	(1<<RAPL_IDX_PP0_NRG_STAT|\
			 1<<RAPL_IDX_PKG_NRG_STAT|\
			 1<<RAPL_IDX_RAM_NRG_STAT|\
			 1<<RAPL_IDX_PP1_NRG_STAT)

/*
 * event code: LSB 8 bits, passed in attr->config
 * any other bit is reserved
 */
#define RAPL_EVENT_MASK	0xFFULL

#define DEFINE_RAPL_FORMAT_ATTR(_var, _name, _format)		\
static ssize_t __rapl_##_var##_show(struct kobject *kobj,	\
				struct kobj_attribute *attr,	\
				char *page)			\
{								\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);		\
	return sprintf(page, _format "\n");			\
}								\
static struct kobj_attribute format_attr_##_var =		\
	__ATTR(_name, 0444, __rapl_##_var##_show, NULL)

#define RAPL_EVENT_DESC(_name, _config)				\
{								\
	.attr	= __ATTR(_name, 0444, rapl_event_show, NULL),	\
	.config	= _config,					\
}

#define RAPL_CNTR_WIDTH 32 /* 32-bit rapl counters */

#define RAPL_EVENT_ATTR_STR(_name, v, str)				\
static struct perf_pmu_events_attr event_attr_##v = {			\
	.attr		= __ATTR(_name, 0444, rapl_sysfs_show, NULL),	\
	.id		= 0,						\
	.event_str	= str,						\
};

struct rapl_pmu {
	spinlock_t	 lock;
	int		 n_active; /* number of active events */
	struct list_head active_list;
	struct pmu	 *pmu; /* pointer to rapl_pmu_class */
	ktime_t		 timer_interval; /* in ktime_t unit */
	struct hrtimer   hrtimer;
};

static int rapl_hw_unit[NR_RAPL_DOMAINS] __read_mostly;  /* 1/2^hw_unit Joule */
static struct pmu rapl_pmu_class;
static cpumask_t rapl_cpu_mask;
static int rapl_cntr_mask;

static DEFINE_PER_CPU(struct rapl_pmu *, rapl_pmu);
static DEFINE_PER_CPU(struct rapl_pmu *, rapl_pmu_to_free);

static struct x86_pmu_quirk *rapl_quirks;
static inline u64 rapl_read_counter(struct perf_event *event)
{
	u64 raw;
	rdmsrl(event->hw.event_base, raw);
	return raw;
}

#define rapl_add_quirk(func_)						\
do {									\
	static struct x86_pmu_quirk __quirk __initdata = {		\
		.func = func_,						\
	};								\
	__quirk.next = rapl_quirks;					\
	rapl_quirks = &__quirk;						\
} while (0)

static inline u64 rapl_scale(u64 v, int cfg)
{
	if (cfg > NR_RAPL_DOMAINS) {
		pr_warn("invalid domain %d, failed to scale data\n", cfg);
		return v;
	}
	/*
	 * scale delta to smallest unit (1/2^32)
	 * users must then scale back: count * 1/(1e9*2^32) to get Joules
	 * or use ldexp(count, -32).
	 * Watts = Joules/Time delta
	 */
	return v << (32 - rapl_hw_unit[cfg - 1]);
}

static u64 rapl_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	s64 delta, sdelta;
	int shift = RAPL_CNTR_WIDTH;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	rdmsrl(event->hw.event_base, new_raw_count);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			    new_raw_count) != prev_raw_count) {
		cpu_relax();
		goto again;
	}

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

	sdelta = rapl_scale(delta, event->hw.config);

	local64_add(sdelta, &event->count);

	return new_raw_count;
}

static void rapl_start_hrtimer(struct rapl_pmu *pmu)
{
	__hrtimer_start_range_ns(&pmu->hrtimer,
			pmu->timer_interval, 0,
			HRTIMER_MODE_REL_PINNED, 0);
}

static void rapl_stop_hrtimer(struct rapl_pmu *pmu)
{
	hrtimer_cancel(&pmu->hrtimer);
}

static enum hrtimer_restart rapl_hrtimer_handle(struct hrtimer *hrtimer)
{
	struct rapl_pmu *pmu = __this_cpu_read(rapl_pmu);
	struct perf_event *event;
	unsigned long flags;

	if (!pmu->n_active)
		return HRTIMER_NORESTART;

	spin_lock_irqsave(&pmu->lock, flags);

	list_for_each_entry(event, &pmu->active_list, active_entry) {
		rapl_event_update(event);
	}

	spin_unlock_irqrestore(&pmu->lock, flags);

	hrtimer_forward_now(hrtimer, pmu->timer_interval);

	return HRTIMER_RESTART;
}

static void rapl_hrtimer_init(struct rapl_pmu *pmu)
{
	struct hrtimer *hr = &pmu->hrtimer;

	hrtimer_init(hr, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr->function = rapl_hrtimer_handle;
}

static void __rapl_pmu_event_start(struct rapl_pmu *pmu,
				   struct perf_event *event)
{
	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	event->hw.state = 0;

	list_add_tail(&event->active_entry, &pmu->active_list);

	local64_set(&event->hw.prev_count, rapl_read_counter(event));

	pmu->n_active++;
	if (pmu->n_active == 1)
		rapl_start_hrtimer(pmu);
}

static void rapl_pmu_event_start(struct perf_event *event, int mode)
{
	struct rapl_pmu *pmu = __this_cpu_read(rapl_pmu);
	unsigned long flags;

	spin_lock_irqsave(&pmu->lock, flags);
	__rapl_pmu_event_start(pmu, event);
	spin_unlock_irqrestore(&pmu->lock, flags);
}

static void rapl_pmu_event_stop(struct perf_event *event, int mode)
{
	struct rapl_pmu *pmu = __this_cpu_read(rapl_pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	spin_lock_irqsave(&pmu->lock, flags);

	/* mark event as deactivated and stopped */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		WARN_ON_ONCE(pmu->n_active <= 0);
		pmu->n_active--;
		if (pmu->n_active == 0)
			rapl_stop_hrtimer(pmu);

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

	spin_unlock_irqrestore(&pmu->lock, flags);
}

static int rapl_pmu_event_add(struct perf_event *event, int mode)
{
	struct rapl_pmu *pmu = __this_cpu_read(rapl_pmu);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flags;

	spin_lock_irqsave(&pmu->lock, flags);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (mode & PERF_EF_START)
		__rapl_pmu_event_start(pmu, event);

	spin_unlock_irqrestore(&pmu->lock, flags);

	return 0;
}

static void rapl_pmu_event_del(struct perf_event *event, int flags)
{
	rapl_pmu_event_stop(event, PERF_EF_UPDATE);
}

static int rapl_pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config & RAPL_EVENT_MASK;
	int bit, msr, ret = 0;

	/* only look at RAPL events */
	if (event->attr.type != rapl_pmu_class.type)
		return -ENOENT;

	/* check only supported bits are set */
	if (event->attr.config & ~RAPL_EVENT_MASK)
		return -EINVAL;

	/*
	 * check event is known (determines counter)
	 */
	switch (cfg) {
	case INTEL_RAPL_PP0:
		bit = RAPL_IDX_PP0_NRG_STAT;
		msr = MSR_PP0_ENERGY_STATUS;
		break;
	case INTEL_RAPL_PKG:
		bit = RAPL_IDX_PKG_NRG_STAT;
		msr = MSR_PKG_ENERGY_STATUS;
		break;
	case INTEL_RAPL_RAM:
		bit = RAPL_IDX_RAM_NRG_STAT;
		msr = MSR_DRAM_ENERGY_STATUS;
		break;
	case INTEL_RAPL_PP1:
		bit = RAPL_IDX_PP1_NRG_STAT;
		msr = MSR_PP1_ENERGY_STATUS;
		break;
	default:
		return -EINVAL;
	}
	/* check event supported */
	if (!(rapl_cntr_mask & (1 << bit)))
		return -EINVAL;

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest  ||
	    event->attr.sample_period) /* no sampling */
		return -EINVAL;

	/* must be done before validate_group */
	event->hw.event_base = msr;
	event->hw.config = cfg;
	event->hw.idx = bit;

	return ret;
}

static void rapl_pmu_event_read(struct perf_event *event)
{
	rapl_event_update(event);
}

static ssize_t rapl_get_attr_cpumask(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &rapl_cpu_mask);
}

static DEVICE_ATTR(cpumask, S_IRUGO, rapl_get_attr_cpumask, NULL);

static struct attribute *rapl_pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group rapl_pmu_attr_group = {
	.attrs = rapl_pmu_attrs,
};

static ssize_t rapl_sysfs_show(struct device *dev,
			       struct device_attribute *attr,
			       char *page)
{
	struct perf_pmu_events_attr *pmu_attr = \
		container_of(attr, struct perf_pmu_events_attr, attr);

	if (pmu_attr->event_str)
		return sprintf(page, "%s", pmu_attr->event_str);

	return 0;
}

RAPL_EVENT_ATTR_STR(energy-cores, rapl_cores, "event=0x01");
RAPL_EVENT_ATTR_STR(energy-pkg  ,   rapl_pkg, "event=0x02");
RAPL_EVENT_ATTR_STR(energy-ram  ,   rapl_ram, "event=0x03");
RAPL_EVENT_ATTR_STR(energy-gpu  ,   rapl_gpu, "event=0x04");

RAPL_EVENT_ATTR_STR(energy-cores.unit, rapl_cores_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-pkg.unit  ,   rapl_pkg_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-ram.unit  ,   rapl_ram_unit, "Joules");
RAPL_EVENT_ATTR_STR(energy-gpu.unit  ,   rapl_gpu_unit, "Joules");

/*
 * we compute in 0.23 nJ increments regardless of MSR
 */
RAPL_EVENT_ATTR_STR(energy-cores.scale, rapl_cores_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-pkg.scale,     rapl_pkg_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-ram.scale,     rapl_ram_scale, "2.3283064365386962890625e-10");
RAPL_EVENT_ATTR_STR(energy-gpu.scale,     rapl_gpu_scale, "2.3283064365386962890625e-10");

static struct attribute *rapl_events_srv_attr[] = {
	EVENT_PTR(rapl_cores),
	EVENT_PTR(rapl_pkg),
	EVENT_PTR(rapl_ram),

	EVENT_PTR(rapl_cores_unit),
	EVENT_PTR(rapl_pkg_unit),
	EVENT_PTR(rapl_ram_unit),

	EVENT_PTR(rapl_cores_scale),
	EVENT_PTR(rapl_pkg_scale),
	EVENT_PTR(rapl_ram_scale),
	NULL,
};

static struct attribute *rapl_events_cln_attr[] = {
	EVENT_PTR(rapl_cores),
	EVENT_PTR(rapl_pkg),
	EVENT_PTR(rapl_gpu),

	EVENT_PTR(rapl_cores_unit),
	EVENT_PTR(rapl_pkg_unit),
	EVENT_PTR(rapl_gpu_unit),

	EVENT_PTR(rapl_cores_scale),
	EVENT_PTR(rapl_pkg_scale),
	EVENT_PTR(rapl_gpu_scale),
	NULL,
};

static struct attribute *rapl_events_hsw_attr[] = {
	EVENT_PTR(rapl_cores),
	EVENT_PTR(rapl_pkg),
	EVENT_PTR(rapl_gpu),
	EVENT_PTR(rapl_ram),

	EVENT_PTR(rapl_cores_unit),
	EVENT_PTR(rapl_pkg_unit),
	EVENT_PTR(rapl_gpu_unit),
	EVENT_PTR(rapl_ram_unit),

	EVENT_PTR(rapl_cores_scale),
	EVENT_PTR(rapl_pkg_scale),
	EVENT_PTR(rapl_gpu_scale),
	EVENT_PTR(rapl_ram_scale),
	NULL,
};

static struct attribute_group rapl_pmu_events_group = {
	.name = "events",
	.attrs = NULL, /* patched at runtime */
};

DEFINE_RAPL_FORMAT_ATTR(event, event, "config:0-7");
static struct attribute *rapl_formats_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group rapl_pmu_format_group = {
	.name = "format",
	.attrs = rapl_formats_attr,
};

const struct attribute_group *rapl_attr_groups[] = {
	&rapl_pmu_attr_group,
	&rapl_pmu_format_group,
	&rapl_pmu_events_group,
	NULL,
};

static struct pmu rapl_pmu_class = {
	.attr_groups	= rapl_attr_groups,
	.task_ctx_nr	= perf_invalid_context, /* system-wide only */
	.event_init	= rapl_pmu_event_init,
	.add		= rapl_pmu_event_add, /* must have */
	.del		= rapl_pmu_event_del, /* must have */
	.start		= rapl_pmu_event_start,
	.stop		= rapl_pmu_event_stop,
	.read		= rapl_pmu_event_read,
};

static void rapl_cpu_exit(int cpu)
{
	struct rapl_pmu *pmu = per_cpu(rapl_pmu, cpu);
	int i, phys_id = topology_physical_package_id(cpu);
	int target = -1;

	/* find a new cpu on same package */
	for_each_online_cpu(i) {
		if (i == cpu)
			continue;
		if (phys_id == topology_physical_package_id(i)) {
			target = i;
			break;
		}
	}
	/*
	 * clear cpu from cpumask
	 * if was set in cpumask and still some cpu on package,
	 * then move to new cpu
	 */
	if (cpumask_test_and_clear_cpu(cpu, &rapl_cpu_mask) && target >= 0)
		cpumask_set_cpu(target, &rapl_cpu_mask);

	WARN_ON(cpumask_empty(&rapl_cpu_mask));
	/*
	 * migrate events and context to new cpu
	 */
	if (target >= 0)
		perf_pmu_migrate_context(pmu->pmu, cpu, target);

	/* cancel overflow polling timer for CPU */
	rapl_stop_hrtimer(pmu);
}

static void rapl_cpu_init(int cpu)
{
	int i, phys_id = topology_physical_package_id(cpu);

	/* check if phys_is is already covered */
	for_each_cpu(i, &rapl_cpu_mask) {
		if (phys_id == topology_physical_package_id(i))
			return;
	}
	/* was not found, so add it */
	cpumask_set_cpu(cpu, &rapl_cpu_mask);
}

static __init void rapl_hsw_server_quirk(void)
{
	/*
	 * DRAM domain on HSW server has fixed energy unit which can be
	 * different than the unit from power unit MSR.
	 * "Intel Xeon Processor E5-1600 and E5-2600 v3 Product Families, V2
	 * of 2. Datasheet, September 2014, Reference Number: 330784-001 "
	 */
	rapl_hw_unit[RAPL_IDX_RAM_NRG_STAT] = 16;
}

static int rapl_cpu_prepare(int cpu)
{
	struct rapl_pmu *pmu = per_cpu(rapl_pmu, cpu);
	int phys_id = topology_physical_package_id(cpu);
	u64 ms;

	if (pmu)
		return 0;

	if (phys_id < 0)
		return -1;

	pmu = kzalloc_node(sizeof(*pmu), GFP_KERNEL, cpu_to_node(cpu));
	if (!pmu)
		return -1;
	spin_lock_init(&pmu->lock);

	INIT_LIST_HEAD(&pmu->active_list);

	pmu->pmu = &rapl_pmu_class;

	/*
	 * use reference of 200W for scaling the timeout
	 * to avoid missing counter overflows.
	 * 200W = 200 Joules/sec
	 * divide interval by 2 to avoid lockstep (2 * 100)
	 * if hw unit is 32, then we use 2 ms 1/200/2
	 */
	if (rapl_hw_unit[0] < 32)
		ms = (1000 / (2 * 100)) * (1ULL << (32 - rapl_hw_unit[0] - 1));
	else
		ms = 2;

	pmu->timer_interval = ms_to_ktime(ms);

	rapl_hrtimer_init(pmu);

	/* set RAPL pmu for this cpu for now */
	per_cpu(rapl_pmu, cpu) = pmu;
	per_cpu(rapl_pmu_to_free, cpu) = NULL;

	return 0;
}

static void rapl_cpu_kfree(int cpu)
{
	struct rapl_pmu *pmu = per_cpu(rapl_pmu_to_free, cpu);

	kfree(pmu);

	per_cpu(rapl_pmu_to_free, cpu) = NULL;
}

static int rapl_cpu_dying(int cpu)
{
	struct rapl_pmu *pmu = per_cpu(rapl_pmu, cpu);

	if (!pmu)
		return 0;

	per_cpu(rapl_pmu, cpu) = NULL;

	per_cpu(rapl_pmu_to_free, cpu) = pmu;

	return 0;
}

static int rapl_cpu_notifier(struct notifier_block *self,
			     unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		rapl_cpu_prepare(cpu);
		break;
	case CPU_STARTING:
		rapl_cpu_init(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_DYING:
		rapl_cpu_dying(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DEAD:
		rapl_cpu_kfree(cpu);
		break;
	case CPU_DOWN_PREPARE:
		rapl_cpu_exit(cpu);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int rapl_check_hw_unit(void)
{
	u64 msr_rapl_power_unit_bits;
	int i;

	/* protect rdmsrl() to handle virtualization */
	if (rdmsrl_safe(MSR_RAPL_POWER_UNIT, &msr_rapl_power_unit_bits))
		return -1;
	for (i = 0; i < NR_RAPL_DOMAINS; i++)
		rapl_hw_unit[i] = (msr_rapl_power_unit_bits >> 8) & 0x1FULL;

	return 0;
}

static const struct x86_cpu_id rapl_cpu_match[] = {
	[0] = { .vendor = X86_VENDOR_INTEL, .family = 6 },
	[1] = {},
};

static int __init rapl_pmu_init(void)
{
	struct rapl_pmu *pmu;
	int cpu, ret;
	struct x86_pmu_quirk *quirk;
	int i;

	/*
	 * check for Intel processor family 6
	 */
	if (!x86_match_cpu(rapl_cpu_match))
		return 0;

	/* check supported CPU */
	switch (boot_cpu_data.x86_model) {
	case 42: /* Sandy Bridge */
	case 58: /* Ivy Bridge */
		rapl_cntr_mask = RAPL_IDX_CLN;
		rapl_pmu_events_group.attrs = rapl_events_cln_attr;
		break;
	case 63: /* Haswell-Server */
		rapl_add_quirk(rapl_hsw_server_quirk);
		rapl_cntr_mask = RAPL_IDX_SRV;
		rapl_pmu_events_group.attrs = rapl_events_srv_attr;
		break;
	case 60: /* Haswell */
	case 69: /* Haswell-Celeron */
	case 61: /* Broadwell */
		rapl_cntr_mask = RAPL_IDX_HSW;
		rapl_pmu_events_group.attrs = rapl_events_hsw_attr;
		break;
	case 45: /* Sandy Bridge-EP */
	case 62: /* IvyTown */
		rapl_cntr_mask = RAPL_IDX_SRV;
		rapl_pmu_events_group.attrs = rapl_events_srv_attr;
		break;

	default:
		/* unsupported */
		return 0;
	}
	ret = rapl_check_hw_unit();
	if (ret)
		return ret;

	/* run cpu model quirks */
	for (quirk = rapl_quirks; quirk; quirk = quirk->next)
		quirk->func();
	cpu_notifier_register_begin();

	for_each_online_cpu(cpu) {
		ret = rapl_cpu_prepare(cpu);
		if (ret)
			goto out;
		rapl_cpu_init(cpu);
	}

	__perf_cpu_notifier(rapl_cpu_notifier);

	ret = perf_pmu_register(&rapl_pmu_class, "power", -1);
	if (WARN_ON(ret)) {
		pr_info("RAPL PMU detected, registration failed (%d), RAPL PMU disabled\n", ret);
		cpu_notifier_register_done();
		return -1;
	}

	pmu = __this_cpu_read(rapl_pmu);

	pr_info("RAPL PMU detected,"
		" API unit is 2^-32 Joules,"
		" %d fixed counters"
		" %llu ms ovfl timer\n",
		hweight32(rapl_cntr_mask),
		ktime_to_ms(pmu->timer_interval));
	for (i = 0; i < NR_RAPL_DOMAINS; i++) {
		if (rapl_cntr_mask & (1 << i)) {
			pr_info("hw unit of domain %s 2^-%d Joules\n",
				rapl_domain_names[i], rapl_hw_unit[i]);
		}
	}
out:
	cpu_notifier_register_done();

	return 0;
}
device_initcall(rapl_pmu_init);
