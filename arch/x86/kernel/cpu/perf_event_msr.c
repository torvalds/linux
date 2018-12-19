#include <linux/perf_event.h>
#include <linux/nospec.h>

enum perf_msr_id {
	PERF_MSR_TSC			= 0,
	PERF_MSR_APERF			= 1,
	PERF_MSR_MPERF			= 2,
	PERF_MSR_PPERF			= 3,
	PERF_MSR_SMI			= 4,

	PERF_MSR_EVENT_MAX,
};

static bool test_aperfmperf(int idx)
{
	return boot_cpu_has(X86_FEATURE_APERFMPERF);
}

static bool test_intel(int idx)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return false;

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

	case 55: /* 22nm Atom "Silvermont"                */
	case 77: /* 22nm Atom "Silvermont Avoton/Rangely" */
	case 76: /* 14nm Atom "Airmont"                   */
		if (idx == PERF_MSR_SMI)
			return true;
		break;

	case 78: /* 14nm Skylake Mobile */
	case 94: /* 14nm Skylake Desktop */
		if (idx == PERF_MSR_SMI || idx == PERF_MSR_PPERF)
			return true;
		break;
	}

	return false;
}

struct perf_msr {
	u64	msr;
	struct	perf_pmu_events_attr *attr;
	bool	(*test)(int idx);
};

PMU_EVENT_ATTR_STRING(tsc,   evattr_tsc,   "event=0x00");
PMU_EVENT_ATTR_STRING(aperf, evattr_aperf, "event=0x01");
PMU_EVENT_ATTR_STRING(mperf, evattr_mperf, "event=0x02");
PMU_EVENT_ATTR_STRING(pperf, evattr_pperf, "event=0x03");
PMU_EVENT_ATTR_STRING(smi,   evattr_smi,   "event=0x04");

static struct perf_msr msr[] = {
	[PERF_MSR_TSC]   = { 0,			&evattr_tsc,	NULL,		 },
	[PERF_MSR_APERF] = { MSR_IA32_APERF,	&evattr_aperf,	test_aperfmperf, },
	[PERF_MSR_MPERF] = { MSR_IA32_MPERF,	&evattr_mperf,	test_aperfmperf, },
	[PERF_MSR_PPERF] = { MSR_PPERF,		&evattr_pperf,	test_intel,	 },
	[PERF_MSR_SMI]   = { MSR_SMI_COUNT,	&evattr_smi,	test_intel,	 },
};

static struct attribute *events_attrs[PERF_MSR_EVENT_MAX + 1] = {
	NULL,
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

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest  ||
	    event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (cfg >= PERF_MSR_EVENT_MAX)
		return -EINVAL;

	cfg = array_index_nospec((unsigned long)cfg, PERF_MSR_EVENT_MAX);

	if (!msr[cfg].attr)
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
		rdtscll(now);

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
	if (unlikely(event->hw.event_base == MSR_SMI_COUNT))
		delta = sign_extend64(delta, 31);

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

static int __init msr_init(void)
{
	int i, j = 0;

	if (!boot_cpu_has(X86_FEATURE_TSC)) {
		pr_cont("no MSR PMU driver.\n");
		return 0;
	}

	/* Probe the MSRs. */
	for (i = PERF_MSR_TSC + 1; i < PERF_MSR_EVENT_MAX; i++) {
		u64 val;

		/*
		 * Virt sucks arse; you cannot tell if a R/O MSR is present :/
		 */
		if (!msr[i].test(i) || rdmsrl_safe(msr[i].msr, &val))
			msr[i].attr = NULL;
	}

	/* List remaining MSRs in the sysfs attrs. */
	for (i = 0; i < PERF_MSR_EVENT_MAX; i++) {
		if (msr[i].attr)
			events_attrs[j++] = &msr[i].attr->attr.attr;
	}
	events_attrs[j] = NULL;

	perf_pmu_register(&pmu_msr, "msr", -1);

	return 0;
}
device_initcall(msr_init);
