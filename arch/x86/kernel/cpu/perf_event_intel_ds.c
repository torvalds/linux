#ifdef CONFIG_CPU_SUP_INTEL

/* The maximal number of PEBS events: */
#define MAX_PEBS_EVENTS		4

/* The size of a BTS record in bytes: */
#define BTS_RECORD_SIZE		24

#define BTS_BUFFER_SIZE		(PAGE_SIZE << 4)
#define PEBS_BUFFER_SIZE	PAGE_SIZE

/*
 * pebs_record_32 for p4 and core not supported

struct pebs_record_32 {
	u32 flags, ip;
	u32 ax, bc, cx, dx;
	u32 si, di, bp, sp;
};

 */

struct pebs_record_core {
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
};

struct pebs_record_nhm {
	u64 flags, ip;
	u64 ax, bx, cx, dx;
	u64 si, di, bp, sp;
	u64 r8,  r9,  r10, r11;
	u64 r12, r13, r14, r15;
	u64 status, dla, dse, lat;
};

/*
 * Bits in the debugctlmsr controlling branch tracing.
 */
#define X86_DEBUGCTL_TR			(1 << 6)
#define X86_DEBUGCTL_BTS		(1 << 7)
#define X86_DEBUGCTL_BTINT		(1 << 8)
#define X86_DEBUGCTL_BTS_OFF_OS		(1 << 9)
#define X86_DEBUGCTL_BTS_OFF_USR	(1 << 10)

/*
 * A debug store configuration.
 *
 * We only support architectures that use 64bit fields.
 */
struct debug_store {
	u64	bts_buffer_base;
	u64	bts_index;
	u64	bts_absolute_maximum;
	u64	bts_interrupt_threshold;
	u64	pebs_buffer_base;
	u64	pebs_index;
	u64	pebs_absolute_maximum;
	u64	pebs_interrupt_threshold;
	u64	pebs_event_reset[MAX_PEBS_EVENTS];
};

static void init_debug_store_on_cpu(int cpu)
{
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

	if (!ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA,
		     (u32)((u64)(unsigned long)ds),
		     (u32)((u64)(unsigned long)ds >> 32));
}

static void fini_debug_store_on_cpu(int cpu)
{
	if (!per_cpu(cpu_hw_events, cpu).ds)
		return;

	wrmsr_on_cpu(cpu, MSR_IA32_DS_AREA, 0, 0);
}

static void release_ds_buffers(void)
{
	int cpu;

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return;

	get_online_cpus();

	for_each_online_cpu(cpu)
		fini_debug_store_on_cpu(cpu);

	for_each_possible_cpu(cpu) {
		struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;

		if (!ds)
			continue;

		per_cpu(cpu_hw_events, cpu).ds = NULL;

		kfree((void *)(unsigned long)ds->pebs_buffer_base);
		kfree((void *)(unsigned long)ds->bts_buffer_base);
		kfree(ds);
	}

	put_online_cpus();
}

static int reserve_ds_buffers(void)
{
	int cpu, err = 0;

	if (!x86_pmu.bts && !x86_pmu.pebs)
		return 0;

	get_online_cpus();

	for_each_possible_cpu(cpu) {
		struct debug_store *ds;
		void *buffer;
		int max, thresh;

		err = -ENOMEM;
		ds = kzalloc(sizeof(*ds), GFP_KERNEL);
		if (unlikely(!ds))
			break;
		per_cpu(cpu_hw_events, cpu).ds = ds;

		if (x86_pmu.bts) {
			buffer = kzalloc(BTS_BUFFER_SIZE, GFP_KERNEL);
			if (unlikely(!buffer))
				break;

			max = BTS_BUFFER_SIZE / BTS_RECORD_SIZE;
			thresh = max / 16;

			ds->bts_buffer_base = (u64)(unsigned long)buffer;
			ds->bts_index = ds->bts_buffer_base;
			ds->bts_absolute_maximum = ds->bts_buffer_base +
				max * BTS_RECORD_SIZE;
			ds->bts_interrupt_threshold = ds->bts_absolute_maximum -
				thresh * BTS_RECORD_SIZE;
		}

		if (x86_pmu.pebs) {
			buffer = kzalloc(PEBS_BUFFER_SIZE, GFP_KERNEL);
			if (unlikely(!buffer))
				break;

			max = PEBS_BUFFER_SIZE / x86_pmu.pebs_record_size;

			ds->pebs_buffer_base = (u64)(unsigned long)buffer;
			ds->pebs_index = ds->pebs_buffer_base;
			ds->pebs_absolute_maximum = ds->pebs_buffer_base +
				max * x86_pmu.pebs_record_size;
			/*
			 * Always use single record PEBS
			 */
			ds->pebs_interrupt_threshold = ds->pebs_buffer_base +
				x86_pmu.pebs_record_size;
		}

		err = 0;
	}

	if (err)
		release_ds_buffers();
	else {
		for_each_online_cpu(cpu)
			init_debug_store_on_cpu(cpu);
	}

	put_online_cpus();

	return err;
}

/*
 * BTS
 */

static struct event_constraint bts_constraint =
	EVENT_CONSTRAINT(0, 1ULL << X86_PMC_IDX_FIXED_BTS, 0);

static void intel_pmu_enable_bts(u64 config)
{
	unsigned long debugctlmsr;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr |= X86_DEBUGCTL_TR;
	debugctlmsr |= X86_DEBUGCTL_BTS;
	debugctlmsr |= X86_DEBUGCTL_BTINT;

	if (!(config & ARCH_PERFMON_EVENTSEL_OS))
		debugctlmsr |= X86_DEBUGCTL_BTS_OFF_OS;

	if (!(config & ARCH_PERFMON_EVENTSEL_USR))
		debugctlmsr |= X86_DEBUGCTL_BTS_OFF_USR;

	update_debugctlmsr(debugctlmsr);
}

static void intel_pmu_disable_bts(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long debugctlmsr;

	if (!cpuc->ds)
		return;

	debugctlmsr = get_debugctlmsr();

	debugctlmsr &=
		~(X86_DEBUGCTL_TR | X86_DEBUGCTL_BTS | X86_DEBUGCTL_BTINT |
		  X86_DEBUGCTL_BTS_OFF_OS | X86_DEBUGCTL_BTS_OFF_USR);

	update_debugctlmsr(debugctlmsr);
}

static void intel_pmu_drain_bts_buffer(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct bts_record {
		u64	from;
		u64	to;
		u64	flags;
	};
	struct perf_event *event = cpuc->events[X86_PMC_IDX_FIXED_BTS];
	struct bts_record *at, *top;
	struct perf_output_handle handle;
	struct perf_event_header header;
	struct perf_sample_data data;
	struct pt_regs regs;

	if (!event)
		return;

	if (!ds)
		return;

	at  = (struct bts_record *)(unsigned long)ds->bts_buffer_base;
	top = (struct bts_record *)(unsigned long)ds->bts_index;

	if (top <= at)
		return;

	ds->bts_index = ds->bts_buffer_base;

	perf_sample_data_init(&data, 0);
	data.period = event->hw.last_period;
	regs.ip     = 0;

	/*
	 * Prepare a generic sample, i.e. fill in the invariant fields.
	 * We will overwrite the from and to address before we output
	 * the sample.
	 */
	perf_prepare_sample(&header, &data, event, &regs);

	if (perf_output_begin(&handle, event, header.size * (top - at), 1, 1))
		return;

	for (; at < top; at++) {
		data.ip		= at->from;
		data.addr	= at->to;

		perf_output_sample(&handle, &header, &data, event);
	}

	perf_output_end(&handle);

	/* There's new data available. */
	event->hw.interrupts++;
	event->pending_kill = POLL_IN;
}

/*
 * PEBS
 */

static struct event_constraint intel_core_pebs_events[] = {
	PEBS_EVENT_CONSTRAINT(0x00c0, 0x1), /* INSTR_RETIRED.ANY */
	PEBS_EVENT_CONSTRAINT(0xfec1, 0x1), /* X87_OPS_RETIRED.ANY */
	PEBS_EVENT_CONSTRAINT(0x00c5, 0x1), /* BR_INST_RETIRED.MISPRED */
	PEBS_EVENT_CONSTRAINT(0x1fc7, 0x1), /* SIMD_INST_RETURED.ANY */
	PEBS_EVENT_CONSTRAINT(0x01cb, 0x1), /* MEM_LOAD_RETIRED.L1D_MISS */
	PEBS_EVENT_CONSTRAINT(0x02cb, 0x1), /* MEM_LOAD_RETIRED.L1D_LINE_MISS */
	PEBS_EVENT_CONSTRAINT(0x04cb, 0x1), /* MEM_LOAD_RETIRED.L2_MISS */
	PEBS_EVENT_CONSTRAINT(0x08cb, 0x1), /* MEM_LOAD_RETIRED.L2_LINE_MISS */
	PEBS_EVENT_CONSTRAINT(0x10cb, 0x1), /* MEM_LOAD_RETIRED.DTLB_MISS */
	EVENT_CONSTRAINT_END
};

static struct event_constraint intel_nehalem_pebs_events[] = {
	PEBS_EVENT_CONSTRAINT(0x00c0, 0xf), /* INSTR_RETIRED.ANY */
	PEBS_EVENT_CONSTRAINT(0xfec1, 0xf), /* X87_OPS_RETIRED.ANY */
	PEBS_EVENT_CONSTRAINT(0x00c5, 0xf), /* BR_INST_RETIRED.MISPRED */
	PEBS_EVENT_CONSTRAINT(0x1fc7, 0xf), /* SIMD_INST_RETURED.ANY */
	PEBS_EVENT_CONSTRAINT(0x01cb, 0xf), /* MEM_LOAD_RETIRED.L1D_MISS */
	PEBS_EVENT_CONSTRAINT(0x02cb, 0xf), /* MEM_LOAD_RETIRED.L1D_LINE_MISS */
	PEBS_EVENT_CONSTRAINT(0x04cb, 0xf), /* MEM_LOAD_RETIRED.L2_MISS */
	PEBS_EVENT_CONSTRAINT(0x08cb, 0xf), /* MEM_LOAD_RETIRED.L2_LINE_MISS */
	PEBS_EVENT_CONSTRAINT(0x10cb, 0xf), /* MEM_LOAD_RETIRED.DTLB_MISS */
	EVENT_CONSTRAINT_END
};

static struct event_constraint *
intel_pebs_constraints(struct perf_event *event)
{
	struct event_constraint *c;

	if (!event->attr.precise)
		return NULL;

	if (x86_pmu.pebs_constraints) {
		for_each_event_constraint(c, x86_pmu.pebs_constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &emptyconstraint;
}

static void intel_pmu_pebs_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	hwc->config &= ~ARCH_PERFMON_EVENTSEL_INT;

	cpuc->pebs_enabled |= 1ULL << hwc->idx;
	WARN_ON_ONCE(cpuc->enabled);

	if (x86_pmu.intel_cap.pebs_trap)
		intel_pmu_lbr_enable(event);
}

static void intel_pmu_pebs_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	cpuc->pebs_enabled &= ~(1ULL << hwc->idx);
	if (cpuc->enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);

	hwc->config |= ARCH_PERFMON_EVENTSEL_INT;

	if (x86_pmu.intel_cap.pebs_trap)
		intel_pmu_lbr_disable(event);
}

static void intel_pmu_pebs_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->pebs_enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, cpuc->pebs_enabled);
}

static void intel_pmu_pebs_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->pebs_enabled)
		wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
}

#include <asm/insn.h>

static inline bool kernel_ip(unsigned long ip)
{
#ifdef CONFIG_X86_32
	return ip > PAGE_OFFSET;
#else
	return (long)ip < 0;
#endif
}

static int intel_pmu_pebs_fixup_ip(struct pt_regs *regs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	unsigned long from = cpuc->lbr_entries[0].from;
	unsigned long old_to, to = cpuc->lbr_entries[0].to;
	unsigned long ip = regs->ip;

	/*
	 * We don't need to fixup if the PEBS assist is fault like
	 */
	if (!x86_pmu.intel_cap.pebs_trap)
		return 1;

	/*
	 * No LBR entry, no basic block, no rewinding
	 */
	if (!cpuc->lbr_stack.nr || !from || !to)
		return 0;

	/*
	 * Basic blocks should never cross user/kernel boundaries
	 */
	if (kernel_ip(ip) != kernel_ip(to))
		return 0;

	/*
	 * unsigned math, either ip is before the start (impossible) or
	 * the basic block is larger than 1 page (sanity)
	 */
	if ((ip - to) > PAGE_SIZE)
		return 0;

	/*
	 * We sampled a branch insn, rewind using the LBR stack
	 */
	if (ip == to) {
		regs->ip = from;
		return 1;
	}

	do {
		struct insn insn;
		u8 buf[MAX_INSN_SIZE];
		void *kaddr;

		old_to = to;
		if (!kernel_ip(ip)) {
			int bytes, size = MAX_INSN_SIZE;

			bytes = copy_from_user_nmi(buf, (void __user *)to, size);
			if (bytes != size)
				return 0;

			kaddr = buf;
		} else
			kaddr = (void *)to;

		kernel_insn_init(&insn, kaddr);
		insn_get_length(&insn);
		to += insn.length;
	} while (to < ip);

	if (to == ip) {
		regs->ip = old_to;
		return 1;
	}

	/*
	 * Even though we decoded the basic block, the instruction stream
	 * never matched the given IP, either the TO or the IP got corrupted.
	 */
	return 0;
}

static int intel_pmu_save_and_restart(struct perf_event *event);

static void intel_pmu_drain_pebs_core(struct pt_regs *iregs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct perf_event *event = cpuc->events[0]; /* PMC0 only */
	struct pebs_record_core *at, *top;
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	int n;

	if (!ds || !x86_pmu.pebs)
		return;

	at  = (struct pebs_record_core *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_core *)(unsigned long)ds->pebs_index;

	/*
	 * Whatever else happens, drain the thing
	 */
	ds->pebs_index = ds->pebs_buffer_base;

	if (!test_bit(0, cpuc->active_mask))
		return;

	WARN_ON_ONCE(!event);

	if (!event->attr.precise)
		return;

	n = top - at;
	if (n <= 0)
		return;

	if (!intel_pmu_save_and_restart(event))
		return;

	/*
	 * Should not happen, we program the threshold at 1 and do not
	 * set a reset value.
	 */
	WARN_ON_ONCE(n > 1);
	at += n - 1;

	perf_sample_data_init(&data, 0);
	data.period = event->hw.last_period;

	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		raw.size = x86_pmu.pebs_record_size;
		raw.data = at;
		data.raw = &raw;
	}

	/*
	 * We use the interrupt regs as a base because the PEBS record
	 * does not contain a full regs set, specifically it seems to
	 * lack segment descriptors, which get used by things like
	 * user_mode().
	 *
	 * In the simple case fix up only the IP and BP,SP regs, for
	 * PERF_SAMPLE_IP and PERF_SAMPLE_CALLCHAIN to function properly.
	 * A possible PERF_SAMPLE_REGS will have to transfer all regs.
	 */
	regs = *iregs;
	regs.ip = at->ip;
	regs.bp = at->bp;
	regs.sp = at->sp;

	if (intel_pmu_pebs_fixup_ip(&regs))
		regs.flags |= PERF_EFLAGS_EXACT;
	else
		regs.flags &= ~PERF_EFLAGS_EXACT;

	if (perf_event_overflow(event, 1, &data, &regs))
		x86_pmu_stop(event);
}

static void intel_pmu_drain_pebs_nhm(struct pt_regs *iregs)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct debug_store *ds = cpuc->ds;
	struct pebs_record_nhm *at, *top;
	struct perf_sample_data data;
	struct perf_event *event = NULL;
	struct perf_raw_record raw;
	struct pt_regs regs;
	u64 status = 0;
	int bit, n;

	if (!ds || !x86_pmu.pebs)
		return;

	at  = (struct pebs_record_nhm *)(unsigned long)ds->pebs_buffer_base;
	top = (struct pebs_record_nhm *)(unsigned long)ds->pebs_index;

	ds->pebs_index = ds->pebs_buffer_base;

	n = top - at;
	if (n <= 0)
		return;

	/*
	 * Should not happen, we program the threshold at 1 and do not
	 * set a reset value.
	 */
	WARN_ON_ONCE(n > MAX_PEBS_EVENTS);

	for ( ; at < top; at++) {
		for_each_bit(bit, (unsigned long *)&at->status, MAX_PEBS_EVENTS) {
			event = cpuc->events[bit];
			if (!test_bit(bit, cpuc->active_mask))
				continue;

			WARN_ON_ONCE(!event);

			if (!event->attr.precise)
				continue;

			if (__test_and_set_bit(bit, (unsigned long *)&status))
				continue;

			break;
		}

		if (!event || bit >= MAX_PEBS_EVENTS)
			continue;

		if (!intel_pmu_save_and_restart(event))
			continue;

		perf_sample_data_init(&data, 0);
		data.period = event->hw.last_period;

		if (event->attr.sample_type & PERF_SAMPLE_RAW) {
			raw.size = x86_pmu.pebs_record_size;
			raw.data = at;
			data.raw = &raw;
		}

		/*
		 * See the comment in intel_pmu_drain_pebs_core()
		 */
		regs = *iregs;
		regs.ip = at->ip;
		regs.bp = at->bp;
		regs.sp = at->sp;

		if (intel_pmu_pebs_fixup_ip(&regs))
			regs.flags |= PERF_EFLAGS_EXACT;
		else
			regs.flags &= ~PERF_EFLAGS_EXACT;

		if (perf_event_overflow(event, 1, &data, &regs))
			x86_pmu_stop(event);
	}
}

/*
 * BTS, PEBS probe and setup
 */

static void intel_ds_init(void)
{
	/*
	 * No support for 32bit formats
	 */
	if (!boot_cpu_has(X86_FEATURE_DTES64))
		return;

	x86_pmu.bts  = boot_cpu_has(X86_FEATURE_BTS);
	x86_pmu.pebs = boot_cpu_has(X86_FEATURE_PEBS);
	if (x86_pmu.pebs) {
		char pebs_type = x86_pmu.intel_cap.pebs_trap ?  '+' : '-';
		int format = x86_pmu.intel_cap.pebs_format;

		switch (format) {
		case 0:
			printk(KERN_CONT "PEBS fmt0%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_core);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_core;
			x86_pmu.pebs_constraints = intel_core_pebs_events;
			break;

		case 1:
			printk(KERN_CONT "PEBS fmt1%c, ", pebs_type);
			x86_pmu.pebs_record_size = sizeof(struct pebs_record_nhm);
			x86_pmu.drain_pebs = intel_pmu_drain_pebs_nhm;
			x86_pmu.pebs_constraints = intel_nehalem_pebs_events;
			break;

		default:
			printk(KERN_CONT "no PEBS fmt%d%c, ", format, pebs_type);
			x86_pmu.pebs = 0;
			break;
		}
	}
}

#else /* CONFIG_CPU_SUP_INTEL */

static int reserve_ds_buffers(void)
{
	return 0;
}

static void release_ds_buffers(void)
{
}

#endif /* CONFIG_CPU_SUP_INTEL */
