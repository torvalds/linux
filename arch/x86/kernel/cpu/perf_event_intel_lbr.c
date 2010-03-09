#ifdef CONFIG_CPU_SUP_INTEL

enum {
	LBR_FORMAT_32		= 0x00,
	LBR_FORMAT_LIP		= 0x01,
	LBR_FORMAT_EIP		= 0x02,
	LBR_FORMAT_EIP_FLAGS	= 0x03,
};

/*
 * We only support LBR implementations that have FREEZE_LBRS_ON_PMI
 * otherwise it becomes near impossible to get a reliable stack.
 */

#define X86_DEBUGCTL_LBR               		(1 << 0)
#define X86_DEBUGCTL_FREEZE_LBRS_ON_PMI		(1 << 11)

static void __intel_pmu_lbr_enable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl |= (X86_DEBUGCTL_LBR | X86_DEBUGCTL_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void __intel_pmu_lbr_disable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl &= ~(X86_DEBUGCTL_LBR | X86_DEBUGCTL_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void intel_pmu_lbr_reset_32(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++)
		wrmsrl(x86_pmu.lbr_from + i, 0);
}

static void intel_pmu_lbr_reset_64(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		wrmsrl(x86_pmu.lbr_from + i, 0);
		wrmsrl(x86_pmu.lbr_to   + i, 0);
	}
}

static void intel_pmu_lbr_reset(void)
{
	if (!x86_pmu.lbr_nr)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_reset_32();
	else
		intel_pmu_lbr_reset_64();
}

static void intel_pmu_lbr_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	WARN_ON_ONCE(cpuc->enabled);

	/*
	 * Reset the LBR stack if we changed task context to
	 * avoid data leaks.
	 */

	if (event->ctx->task && cpuc->lbr_context != event->ctx) {
		intel_pmu_lbr_reset();
		cpuc->lbr_context = event->ctx;
	}

	cpuc->lbr_users++;
}

static void intel_pmu_lbr_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);

	if (cpuc->enabled && !cpuc->lbr_users)
		__intel_pmu_lbr_disable();
}

static void intel_pmu_lbr_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_enable();
}

static void intel_pmu_lbr_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_disable();
}

static inline u64 intel_pmu_lbr_tos(void)
{
	u64 tos;

	rdmsrl(x86_pmu.lbr_tos, tos);

	return tos;
}

static void intel_pmu_lbr_read_32(struct cpu_hw_events *cpuc)
{
	unsigned long mask = x86_pmu.lbr_nr - 1;
	u64 tos = intel_pmu_lbr_tos();
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		unsigned long lbr_idx = (tos - i) & mask;
		union {
			struct {
				u32 from;
				u32 to;
			};
			u64     lbr;
		} msr_lastbranch;

		rdmsrl(x86_pmu.lbr_from + lbr_idx, msr_lastbranch.lbr);

		cpuc->lbr_entries[i].from  = msr_lastbranch.from;
		cpuc->lbr_entries[i].to    = msr_lastbranch.to;
		cpuc->lbr_entries[i].flags = 0;
	}
	cpuc->lbr_stack.nr = i;
}

#define LBR_FROM_FLAG_MISPRED  (1ULL << 63)

/*
 * Due to lack of segmentation in Linux the effective address (offset)
 * is the same as the linear address, allowing us to merge the LIP and EIP
 * LBR formats.
 */
static void intel_pmu_lbr_read_64(struct cpu_hw_events *cpuc)
{
	unsigned long mask = x86_pmu.lbr_nr - 1;
	int lbr_format = x86_pmu.intel_cap.lbr_format;
	u64 tos = intel_pmu_lbr_tos();
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		unsigned long lbr_idx = (tos - i) & mask;
		u64 from, to, flags = 0;

		rdmsrl(x86_pmu.lbr_from + lbr_idx, from);
		rdmsrl(x86_pmu.lbr_to   + lbr_idx, to);

		if (lbr_format == LBR_FORMAT_EIP_FLAGS) {
			flags = !!(from & LBR_FROM_FLAG_MISPRED);
			from = (u64)((((s64)from) << 1) >> 1);
		}

		cpuc->lbr_entries[i].from  = from;
		cpuc->lbr_entries[i].to    = to;
		cpuc->lbr_entries[i].flags = flags;
	}
	cpuc->lbr_stack.nr = i;
}

static void intel_pmu_lbr_read(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!cpuc->lbr_users)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_read_32(cpuc);
	else
		intel_pmu_lbr_read_64(cpuc);
}

static void intel_pmu_lbr_init_core(void)
{
	x86_pmu.lbr_nr     = 4;
	x86_pmu.lbr_tos    = 0x01c9;
	x86_pmu.lbr_from   = 0x40;
	x86_pmu.lbr_to     = 0x60;
}

static void intel_pmu_lbr_init_nhm(void)
{
	x86_pmu.lbr_nr     = 16;
	x86_pmu.lbr_tos    = 0x01c9;
	x86_pmu.lbr_from   = 0x680;
	x86_pmu.lbr_to     = 0x6c0;
}

static void intel_pmu_lbr_init_atom(void)
{
	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = 0x01c9;
	x86_pmu.lbr_from   = 0x40;
	x86_pmu.lbr_to     = 0x60;
}

#endif /* CONFIG_CPU_SUP_INTEL */
