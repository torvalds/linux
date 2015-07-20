#include <linux/perf_event.h>

enum perf_msr_id {
	PERF_MSR_TSC			= 0,
	PERF_MSR_APERF			= 1,
	PERF_MSR_MPERF			= 2,
	PERF_MSR_PPERF			= 3,
	PERF_MSR_SMI			= 4,

	PERF_MSR_EVENT_MAX,
};

struct perf_msr {
	int	id;
	u64	msr;
};

static struct perf_msr msr[] = {
	{ PERF_MSR_TSC, 0 },
	{ PERF_MSR_APERF, MSR_IA32_APERF },
	{ PERF_MSR_MPERF, MSR_IA32_MPERF },
	{ PERF_MSR_PPERF, MSR_PPERF },
	{ PERF_MSR_SMI, MSR_SMI_COUNT },
};

PMU_EVENT_ATTR_STRING(tsc,   evattr_tsc,   "event=0x00");
PMU_EVENT_ATTR_STRING(aperf, evattr_aperf, "event=0x01");
PMU_EVENT_ATTR_STRING(mperf, evattr_mperf, "event=0x02");
PMU_EVENT_ATTR_STRING(pperf, evattr_pperf, "event=0x03");
PMU_EVENT_ATTR_STRING(smi,   evattr_smi,   "event=0x04");

static struct attribute *events_attrs[PERF_MSR_EVENT_MAX + 1] = {
	&evattr_tsc.attr.attr,
};

static struct attribute_group events_attr_group = {
	.name = "events",
	.attrs = events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-63");
static struct attribute *format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};
static struct attribute_group format_attr_group = {
	.name = "format",
	.attrs = format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&events_attr_group,
	&format_attr_group,
	NULL,
};

static int msr_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (cfg >= PERF_MSR_EVENT_MAX)
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

	event->hw.idx = -1;
	event->hw.event_base = msr[cfg].msr;
	event->hw.config = cfg;

	return 0;
}

static inline u64 msr_read_counter(struct perf_event *event)
{
	u64 now;

	if (event->hw.event_base)
		rdmsrl(event->hw.event_base, now);
	else
		now = rdtsc();

	return now;
}
static void msr_event_update(struct perf_event *event)
{
	u64 prev, now;
	s64 delta;

	/* Careful, an NMI might modify the previous event value. */
again:
	prev = local64_read(&event->hw.prev_count);
	now = msr_read_counter(event);

	if (local64_cmpxchg(&event->hw.prev_count, prev, now) != prev)
		goto again;

	delta = now - prev;
	if (unlikely(event->hw.event_base == MSR_SMI_COUNT)) {
		delta <<= 32;
		delta >>= 32; /* sign extend */
	}
	local64_add(now - prev, &event->count);
}

static void msr_event_start(struct perf_event *event, int flags)
{
	u64 now;

	now = msr_read_counter(event);
	local64_set(&event->hw.prev_count, now);
}

static void msr_event_stop(struct perf_event *event, int flags)
{
	msr_event_update(event);
}

static void msr_event_del(struct perf_event *event, int flags)
{
	msr_event_stop(event, PERF_EF_UPDATE);
}

static int msr_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		msr_event_start(event, flags);

	return 0;
}

static struct pmu pmu_msr = {
	.task_ctx_nr	= perf_sw_context,
	.attr_groups	= attr_groups,
	.event_init	= msr_event_init,
	.add		= msr_event_add,
	.del		= msr_event_del,
	.start		= msr_event_start,
	.stop		= msr_event_stop,
	.read		= msr_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
};

static int __init intel_msr_init(int idx)
{
	if (boot_cpu_data.x86 != 6)
		return 0;

	switch (boot_cpu_data.x86_model) {
	case 30: /* 45nm Nehalem    */
	case 26: /* 45nm Nehalem-EP */
	case 46: /* 45nm Nehalem-EX */

	case 37: /* 32nm Westmere    */
	case 44: /* 32nm Westmere-EP */
	case 47: /* 32nm Westmere-EX */

	case 42: /* 32nm SandyBridge         */
	case 45: /* 32nm SandyBridge-E/EN/EP */

	case 58: /* 22nm IvyBridge       */
	case 62: /* 22nm IvyBridge-EP/EX */

	case 60: /* 22nm Haswell Core */
	case 63: /* 22nm Haswell Server */
	case 69: /* 22nm Haswell ULT */
	case 70: /* 22nm Haswell + GT3e (Intel Iris Pro graphics) */

	case 61: /* 14nm Broadwell Core-M */
	case 86: /* 14nm Broadwell Xeon D */
	case 71: /* 14nm Broadwell + GT3e (Intel Iris Pro graphics) */
	case 79: /* 14nm Broadwell Server */
		events_attrs[idx++] = &evattr_smi.attr.attr;
		break;

	case 78: /* 14nm Skylake Mobile */
	case 94: /* 14nm Skylake Desktop */
		events_attrs[idx++] = &evattr_pperf.attr.attr;
		events_attrs[idx++] = &evattr_smi.attr.attr;
		break;

	case 55: /* 22nm Atom "Silvermont"                */
	case 76: /* 14nm Atom "Airmont"                   */
	case 77: /* 22nm Atom "Silvermont Avoton/Rangely" */
		events_attrs[idx++] = &evattr_smi.attr.attr;
		break;
	}

	events_attrs[idx] = NULL;

	return 0;
}

static int __init amd_msr_init(int idx)
{
	return 0;
}

static int __init msr_init(void)
{
	int err;
	int idx = 1;

	if (boot_cpu_has(X86_FEATURE_APERFMPERF)) {
		events_attrs[idx++] = &evattr_aperf.attr.attr;
		events_attrs[idx++] = &evattr_mperf.attr.attr;
		events_attrs[idx] = NULL;
	}

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		err = intel_msr_init(idx);
		break;

	case X86_VENDOR_AMD:
		err = amd_msr_init(idx);
		break;

	default:
		err = -ENOTSUPP;
	}

	if (err != 0) {
		pr_cont("no msr PMU driver.\n");
		return 0;
	}

	perf_pmu_register(&pmu_msr, "msr", -1);

	return 0;
}
device_initcall(msr_init);
