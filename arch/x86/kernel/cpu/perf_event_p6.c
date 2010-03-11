#ifdef CONFIG_CPU_SUP_INTEL

/*
 * Not sure about some of these
 */
static const u64 p6_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]		= 0x0079,
  [PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,
  [PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0f2e,
  [PERF_COUNT_HW_CACHE_MISSES]		= 0x012e,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c4,
  [PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c5,
  [PERF_COUNT_HW_BUS_CYCLES]		= 0x0062,
};

static u64 p6_pmu_event_map(int hw_event)
{
	return p6_perfmon_event_map[hw_event];
}

/*
 * Event setting that is specified not to count anything.
 * We use this to effectively disable a counter.
 *
 * L2_RQSTS with 0 MESI unit mask.
 */
#define P6_NOP_EVENT			0x0000002EULL

static u64 p6_pmu_raw_event(u64 hw_event)
{
#define P6_EVNTSEL_EVENT_MASK		0x000000FFULL
#define P6_EVNTSEL_UNIT_MASK		0x0000FF00ULL
#define P6_EVNTSEL_EDGE_MASK		0x00040000ULL
#define P6_EVNTSEL_INV_MASK		0x00800000ULL
#define P6_EVNTSEL_REG_MASK		0xFF000000ULL

#define P6_EVNTSEL_MASK			\
	(P6_EVNTSEL_EVENT_MASK |	\
	 P6_EVNTSEL_UNIT_MASK  |	\
	 P6_EVNTSEL_EDGE_MASK  |	\
	 P6_EVNTSEL_INV_MASK   |	\
	 P6_EVNTSEL_REG_MASK)

	return hw_event & P6_EVNTSEL_MASK;
}

static struct event_constraint p6_event_constraints[] =
{
	INTEL_EVENT_CONSTRAINT(0xc1, 0x1),	/* FLOPS */
	INTEL_EVENT_CONSTRAINT(0x10, 0x1),	/* FP_COMP_OPS_EXE */
	INTEL_EVENT_CONSTRAINT(0x11, 0x1),	/* FP_ASSIST */
	INTEL_EVENT_CONSTRAINT(0x12, 0x2),	/* MUL */
	INTEL_EVENT_CONSTRAINT(0x13, 0x2),	/* DIV */
	INTEL_EVENT_CONSTRAINT(0x14, 0x1),	/* CYCLES_DIV_BUSY */
	EVENT_CONSTRAINT_END
};

static void p6_pmu_disable_all(void)
{
	u64 val;

	/* p6 only has one enable register */
	rdmsrl(MSR_P6_EVNTSEL0, val);
	val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
	wrmsrl(MSR_P6_EVNTSEL0, val);
}

static void p6_pmu_enable_all(void)
{
	unsigned long val;

	/* p6 only has one enable register */
	rdmsrl(MSR_P6_EVNTSEL0, val);
	val |= ARCH_PERFMON_EVENTSEL_ENABLE;
	wrmsrl(MSR_P6_EVNTSEL0, val);
}

static inline void
p6_pmu_disable_event(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	u64 val = P6_NOP_EVENT;

	if (cpuc->enabled)
		val |= ARCH_PERFMON_EVENTSEL_ENABLE;

	(void)checking_wrmsrl(hwc->config_base + hwc->idx, val);
}

static void p6_pmu_enable_event(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	u64 val;

	val = hwc->config;
	if (cpuc->enabled)
		val |= ARCH_PERFMON_EVENTSEL_ENABLE;

	(void)checking_wrmsrl(hwc->config_base + hwc->idx, val);
}

static __initconst struct x86_pmu p6_pmu = {
	.name			= "p6",
	.handle_irq		= x86_pmu_handle_irq,
	.disable_all		= p6_pmu_disable_all,
	.enable_all		= p6_pmu_enable_all,
	.enable			= p6_pmu_enable_event,
	.disable		= p6_pmu_disable_event,
	.hw_config		= x86_hw_config,
	.schedule_events	= x86_schedule_events,
	.eventsel		= MSR_P6_EVNTSEL0,
	.perfctr		= MSR_P6_PERFCTR0,
	.event_map		= p6_pmu_event_map,
	.raw_event		= p6_pmu_raw_event,
	.max_events		= ARRAY_SIZE(p6_perfmon_event_map),
	.apic			= 1,
	.max_period		= (1ULL << 31) - 1,
	.version		= 0,
	.num_events		= 2,
	/*
	 * Events have 40 bits implemented. However they are designed such
	 * that bits [32-39] are sign extensions of bit 31. As such the
	 * effective width of a event for P6-like PMU is 32 bits only.
	 *
	 * See IA-32 Intel Architecture Software developer manual Vol 3B
	 */
	.event_bits		= 32,
	.event_mask		= (1ULL << 32) - 1,
	.get_event_constraints	= x86_get_event_constraints,
	.event_constraints	= p6_event_constraints,
};

static __init int p6_pmu_init(void)
{
	switch (boot_cpu_data.x86_model) {
	case 1:
	case 3:  /* Pentium Pro */
	case 5:
	case 6:  /* Pentium II */
	case 7:
	case 8:
	case 11: /* Pentium III */
	case 9:
	case 13:
		/* Pentium M */
		break;
	default:
		pr_cont("unsupported p6 CPU model %d ",
			boot_cpu_data.x86_model);
		return -ENODEV;
	}

	x86_pmu = p6_pmu;

	return 0;
}

#endif /* CONFIG_CPU_SUP_INTEL */
