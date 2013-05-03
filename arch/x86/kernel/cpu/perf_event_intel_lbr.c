#include <linux/perf_event.h>
#include <linux/types.h>

#include <asm/perf_event.h>
#include <asm/msr.h>
#include <asm/insn.h>

#include "perf_event.h"

enum {
	LBR_FORMAT_32		= 0x00,
	LBR_FORMAT_LIP		= 0x01,
	LBR_FORMAT_EIP		= 0x02,
	LBR_FORMAT_EIP_FLAGS	= 0x03,
};

/*
 * Intel LBR_SELECT bits
 * Intel Vol3a, April 2011, Section 16.7 Table 16-10
 *
 * Hardware branch filter (not available on all CPUs)
 */
#define LBR_KERNEL_BIT		0 /* do not capture at ring0 */
#define LBR_USER_BIT		1 /* do not capture at ring > 0 */
#define LBR_JCC_BIT		2 /* do not capture conditional branches */
#define LBR_REL_CALL_BIT	3 /* do not capture relative calls */
#define LBR_IND_CALL_BIT	4 /* do not capture indirect calls */
#define LBR_RETURN_BIT		5 /* do not capture near returns */
#define LBR_IND_JMP_BIT		6 /* do not capture indirect jumps */
#define LBR_REL_JMP_BIT		7 /* do not capture relative jumps */
#define LBR_FAR_BIT		8 /* do not capture far branches */

#define LBR_KERNEL	(1 << LBR_KERNEL_BIT)
#define LBR_USER	(1 << LBR_USER_BIT)
#define LBR_JCC		(1 << LBR_JCC_BIT)
#define LBR_REL_CALL	(1 << LBR_REL_CALL_BIT)
#define LBR_IND_CALL	(1 << LBR_IND_CALL_BIT)
#define LBR_RETURN	(1 << LBR_RETURN_BIT)
#define LBR_REL_JMP	(1 << LBR_REL_JMP_BIT)
#define LBR_IND_JMP	(1 << LBR_IND_JMP_BIT)
#define LBR_FAR		(1 << LBR_FAR_BIT)

#define LBR_PLM (LBR_KERNEL | LBR_USER)

#define LBR_SEL_MASK	0x1ff	/* valid bits in LBR_SELECT */
#define LBR_NOT_SUPP	-1	/* LBR filter not supported */
#define LBR_IGN		0	/* ignored */

#define LBR_ANY		 \
	(LBR_JCC	|\
	 LBR_REL_CALL	|\
	 LBR_IND_CALL	|\
	 LBR_RETURN	|\
	 LBR_REL_JMP	|\
	 LBR_IND_JMP	|\
	 LBR_FAR)

#define LBR_FROM_FLAG_MISPRED  (1ULL << 63)

#define for_each_branch_sample_type(x) \
	for ((x) = PERF_SAMPLE_BRANCH_USER; \
	     (x) < PERF_SAMPLE_BRANCH_MAX; (x) <<= 1)

/*
 * x86control flow change classification
 * x86control flow changes include branches, interrupts, traps, faults
 */
enum {
	X86_BR_NONE     = 0,      /* unknown */

	X86_BR_USER     = 1 << 0, /* branch target is user */
	X86_BR_KERNEL   = 1 << 1, /* branch target is kernel */

	X86_BR_CALL     = 1 << 2, /* call */
	X86_BR_RET      = 1 << 3, /* return */
	X86_BR_SYSCALL  = 1 << 4, /* syscall */
	X86_BR_SYSRET   = 1 << 5, /* syscall return */
	X86_BR_INT      = 1 << 6, /* sw interrupt */
	X86_BR_IRET     = 1 << 7, /* return from interrupt */
	X86_BR_JCC      = 1 << 8, /* conditional */
	X86_BR_JMP      = 1 << 9, /* jump */
	X86_BR_IRQ      = 1 << 10,/* hw interrupt or trap or fault */
	X86_BR_IND_CALL = 1 << 11,/* indirect calls */
};

#define X86_BR_PLM (X86_BR_USER | X86_BR_KERNEL)

#define X86_BR_ANY       \
	(X86_BR_CALL    |\
	 X86_BR_RET     |\
	 X86_BR_SYSCALL |\
	 X86_BR_SYSRET  |\
	 X86_BR_INT     |\
	 X86_BR_IRET    |\
	 X86_BR_JCC     |\
	 X86_BR_JMP	 |\
	 X86_BR_IRQ	 |\
	 X86_BR_IND_CALL)

#define X86_BR_ALL (X86_BR_PLM | X86_BR_ANY)

#define X86_BR_ANY_CALL		 \
	(X86_BR_CALL		|\
	 X86_BR_IND_CALL	|\
	 X86_BR_SYSCALL		|\
	 X86_BR_IRQ		|\
	 X86_BR_INT)

static void intel_pmu_lbr_filter(struct cpu_hw_events *cpuc);

/*
 * We only support LBR implementations that have FREEZE_LBRS_ON_PMI
 * otherwise it becomes near impossible to get a reliable stack.
 */

static void __intel_pmu_lbr_enable(void)
{
	u64 debugctl;
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_sel)
		wrmsrl(MSR_LBR_SELECT, cpuc->lbr_sel->config);

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl |= (DEBUGCTLMSR_LBR | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void __intel_pmu_lbr_disable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl &= ~(DEBUGCTLMSR_LBR | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
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

void intel_pmu_lbr_reset(void)
{
	if (!x86_pmu.lbr_nr)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_reset_32();
	else
		intel_pmu_lbr_reset_64();
}

void intel_pmu_lbr_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	/*
	 * Reset the LBR stack if we changed task context to
	 * avoid data leaks.
	 */
	if (event->ctx->task && cpuc->lbr_context != event->ctx) {
		intel_pmu_lbr_reset();
		cpuc->lbr_context = event->ctx;
	}
	cpuc->br_sel = event->hw.branch_reg.reg;

	cpuc->lbr_users++;
}

void intel_pmu_lbr_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);

	if (cpuc->enabled && !cpuc->lbr_users) {
		__intel_pmu_lbr_disable();
		/* avoid stale pointer */
		cpuc->lbr_context = NULL;
	}
}

void intel_pmu_lbr_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_enable();
}

void intel_pmu_lbr_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_disable();
}

/*
 * TOS = most recently recorded branch
 */
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

		cpuc->lbr_entries[i].from	= msr_lastbranch.from;
		cpuc->lbr_entries[i].to		= msr_lastbranch.to;
		cpuc->lbr_entries[i].mispred	= 0;
		cpuc->lbr_entries[i].predicted	= 0;
		cpuc->lbr_entries[i].reserved	= 0;
	}
	cpuc->lbr_stack.nr = i;
}

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
		u64 from, to, mis = 0, pred = 0;

		rdmsrl(x86_pmu.lbr_from + lbr_idx, from);
		rdmsrl(x86_pmu.lbr_to   + lbr_idx, to);

		if (lbr_format == LBR_FORMAT_EIP_FLAGS) {
			mis = !!(from & LBR_FROM_FLAG_MISPRED);
			pred = !mis;
			from = (u64)((((s64)from) << 1) >> 1);
		}

		cpuc->lbr_entries[i].from	= from;
		cpuc->lbr_entries[i].to		= to;
		cpuc->lbr_entries[i].mispred	= mis;
		cpuc->lbr_entries[i].predicted	= pred;
		cpuc->lbr_entries[i].reserved	= 0;
	}
	cpuc->lbr_stack.nr = i;
}

void intel_pmu_lbr_read(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!cpuc->lbr_users)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_read_32(cpuc);
	else
		intel_pmu_lbr_read_64(cpuc);

	intel_pmu_lbr_filter(cpuc);
}

/*
 * SW filter is used:
 * - in case there is no HW filter
 * - in case the HW filter has errata or limitations
 */
static void intel_pmu_setup_sw_lbr_filter(struct perf_event *event)
{
	u64 br_type = event->attr.branch_sample_type;
	int mask = 0;

	if (br_type & PERF_SAMPLE_BRANCH_USER)
		mask |= X86_BR_USER;

	if (br_type & PERF_SAMPLE_BRANCH_KERNEL)
		mask |= X86_BR_KERNEL;

	/* we ignore BRANCH_HV here */

	if (br_type & PERF_SAMPLE_BRANCH_ANY)
		mask |= X86_BR_ANY;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_CALL)
		mask |= X86_BR_ANY_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		mask |= X86_BR_RET | X86_BR_IRET | X86_BR_SYSRET;

	if (br_type & PERF_SAMPLE_BRANCH_IND_CALL)
		mask |= X86_BR_IND_CALL;
	/*
	 * stash actual user request into reg, it may
	 * be used by fixup code for some CPU
	 */
	event->hw.branch_reg.reg = mask;
}

/*
 * setup the HW LBR filter
 * Used only when available, may not be enough to disambiguate
 * all branches, may need the help of the SW filter
 */
static int intel_pmu_setup_hw_lbr_filter(struct perf_event *event)
{
	struct hw_perf_event_extra *reg;
	u64 br_type = event->attr.branch_sample_type;
	u64 mask = 0, m;
	u64 v;

	for_each_branch_sample_type(m) {
		if (!(br_type & m))
			continue;

		v = x86_pmu.lbr_sel_map[m];
		if (v == LBR_NOT_SUPP)
			return -EOPNOTSUPP;

		if (v != LBR_IGN)
			mask |= v;
	}
	reg = &event->hw.branch_reg;
	reg->idx = EXTRA_REG_LBR;

	/* LBR_SELECT operates in suppress mode so invert mask */
	reg->config = ~mask & x86_pmu.lbr_sel_mask;

	return 0;
}

int intel_pmu_setup_lbr_filter(struct perf_event *event)
{
	int ret = 0;

	/*
	 * no LBR on this PMU
	 */
	if (!x86_pmu.lbr_nr)
		return -EOPNOTSUPP;

	/*
	 * setup SW LBR filter
	 */
	intel_pmu_setup_sw_lbr_filter(event);

	/*
	 * setup HW LBR filter, if any
	 */
	if (x86_pmu.lbr_sel_map)
		ret = intel_pmu_setup_hw_lbr_filter(event);

	return ret;
}

/*
 * return the type of control flow change at address "from"
 * intruction is not necessarily a branch (in case of interrupt).
 *
 * The branch type returned also includes the priv level of the
 * target of the control flow change (X86_BR_USER, X86_BR_KERNEL).
 *
 * If a branch type is unknown OR the instruction cannot be
 * decoded (e.g., text page not present), then X86_BR_NONE is
 * returned.
 */
static int branch_type(unsigned long from, unsigned long to)
{
	struct insn insn;
	void *addr;
	int bytes, size = MAX_INSN_SIZE;
	int ret = X86_BR_NONE;
	int ext, to_plm, from_plm;
	u8 buf[MAX_INSN_SIZE];
	int is64 = 0;

	to_plm = kernel_ip(to) ? X86_BR_KERNEL : X86_BR_USER;
	from_plm = kernel_ip(from) ? X86_BR_KERNEL : X86_BR_USER;

	/*
	 * maybe zero if lbr did not fill up after a reset by the time
	 * we get a PMU interrupt
	 */
	if (from == 0 || to == 0)
		return X86_BR_NONE;

	if (from_plm == X86_BR_USER) {
		/*
		 * can happen if measuring at the user level only
		 * and we interrupt in a kernel thread, e.g., idle.
		 */
		if (!current->mm)
			return X86_BR_NONE;

		/* may fail if text not present */
		bytes = copy_from_user_nmi(buf, (void __user *)from, size);
		if (bytes != size)
			return X86_BR_NONE;

		addr = buf;
	} else {
		/*
		 * The LBR logs any address in the IP, even if the IP just
		 * faulted. This means userspace can control the from address.
		 * Ensure we don't blindy read any address by validating it is
		 * a known text address.
		 */
		if (kernel_text_address(from))
			addr = (void *)from;
		else
			return X86_BR_NONE;
	}

	/*
	 * decoder needs to know the ABI especially
	 * on 64-bit systems running 32-bit apps
	 */
#ifdef CONFIG_X86_64
	is64 = kernel_ip((unsigned long)addr) || !test_thread_flag(TIF_IA32);
#endif
	insn_init(&insn, addr, is64);
	insn_get_opcode(&insn);

	switch (insn.opcode.bytes[0]) {
	case 0xf:
		switch (insn.opcode.bytes[1]) {
		case 0x05: /* syscall */
		case 0x34: /* sysenter */
			ret = X86_BR_SYSCALL;
			break;
		case 0x07: /* sysret */
		case 0x35: /* sysexit */
			ret = X86_BR_SYSRET;
			break;
		case 0x80 ... 0x8f: /* conditional */
			ret = X86_BR_JCC;
			break;
		default:
			ret = X86_BR_NONE;
		}
		break;
	case 0x70 ... 0x7f: /* conditional */
		ret = X86_BR_JCC;
		break;
	case 0xc2: /* near ret */
	case 0xc3: /* near ret */
	case 0xca: /* far ret */
	case 0xcb: /* far ret */
		ret = X86_BR_RET;
		break;
	case 0xcf: /* iret */
		ret = X86_BR_IRET;
		break;
	case 0xcc ... 0xce: /* int */
		ret = X86_BR_INT;
		break;
	case 0xe8: /* call near rel */
	case 0x9a: /* call far absolute */
		ret = X86_BR_CALL;
		break;
	case 0xe0 ... 0xe3: /* loop jmp */
		ret = X86_BR_JCC;
		break;
	case 0xe9 ... 0xeb: /* jmp */
		ret = X86_BR_JMP;
		break;
	case 0xff: /* call near absolute, call far absolute ind */
		insn_get_modrm(&insn);
		ext = (insn.modrm.bytes[0] >> 3) & 0x7;
		switch (ext) {
		case 2: /* near ind call */
		case 3: /* far ind call */
			ret = X86_BR_IND_CALL;
			break;
		case 4:
		case 5:
			ret = X86_BR_JMP;
			break;
		}
		break;
	default:
		ret = X86_BR_NONE;
	}
	/*
	 * interrupts, traps, faults (and thus ring transition) may
	 * occur on any instructions. Thus, to classify them correctly,
	 * we need to first look at the from and to priv levels. If they
	 * are different and to is in the kernel, then it indicates
	 * a ring transition. If the from instruction is not a ring
	 * transition instr (syscall, systenter, int), then it means
	 * it was a irq, trap or fault.
	 *
	 * we have no way of detecting kernel to kernel faults.
	 */
	if (from_plm == X86_BR_USER && to_plm == X86_BR_KERNEL
	    && ret != X86_BR_SYSCALL && ret != X86_BR_INT)
		ret = X86_BR_IRQ;

	/*
	 * branch priv level determined by target as
	 * is done by HW when LBR_SELECT is implemented
	 */
	if (ret != X86_BR_NONE)
		ret |= to_plm;

	return ret;
}

/*
 * implement actual branch filter based on user demand.
 * Hardware may not exactly satisfy that request, thus
 * we need to inspect opcodes. Mismatched branches are
 * discarded. Therefore, the number of branches returned
 * in PERF_SAMPLE_BRANCH_STACK sample may vary.
 */
static void
intel_pmu_lbr_filter(struct cpu_hw_events *cpuc)
{
	u64 from, to;
	int br_sel = cpuc->br_sel;
	int i, j, type;
	bool compress = false;

	/* if sampling all branches, then nothing to filter */
	if ((br_sel & X86_BR_ALL) == X86_BR_ALL)
		return;

	for (i = 0; i < cpuc->lbr_stack.nr; i++) {

		from = cpuc->lbr_entries[i].from;
		to = cpuc->lbr_entries[i].to;

		type = branch_type(from, to);

		/* if type does not correspond, then discard */
		if (type == X86_BR_NONE || (br_sel & type) != type) {
			cpuc->lbr_entries[i].from = 0;
			compress = true;
		}
	}

	if (!compress)
		return;

	/* remove all entries with from=0 */
	for (i = 0; i < cpuc->lbr_stack.nr; ) {
		if (!cpuc->lbr_entries[i].from) {
			j = i;
			while (++j < cpuc->lbr_stack.nr)
				cpuc->lbr_entries[j-1] = cpuc->lbr_entries[j];
			cpuc->lbr_stack.nr--;
			if (!cpuc->lbr_entries[i].from)
				continue;
		}
		i++;
	}
}

/*
 * Map interface branch filters onto LBR filters
 */
static const int nhm_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX] = {
	[PERF_SAMPLE_BRANCH_ANY]	= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER]	= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN]	= LBR_RETURN | LBR_REL_JMP
					| LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include REL_JMP+IND_JMP to get CALL branches
	 */
	[PERF_SAMPLE_BRANCH_ANY_CALL] =
	 LBR_REL_CALL | LBR_IND_CALL | LBR_REL_JMP | LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include IND_JMP to capture IND_CALL
	 */
	[PERF_SAMPLE_BRANCH_IND_CALL] = LBR_IND_CALL | LBR_IND_JMP,
};

static const int snb_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX] = {
	[PERF_SAMPLE_BRANCH_ANY]	= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER]	= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN]	= LBR_RETURN | LBR_FAR,
	[PERF_SAMPLE_BRANCH_ANY_CALL]	= LBR_REL_CALL | LBR_IND_CALL
					| LBR_FAR,
	[PERF_SAMPLE_BRANCH_IND_CALL]	= LBR_IND_CALL,
};

/* core */
void intel_pmu_lbr_init_core(void)
{
	x86_pmu.lbr_nr     = 4;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	/*
	 * SW branch filter usage:
	 * - compensate for lack of HW filter
	 */
	pr_cont("4-deep LBR, ");
}

/* nehalem/westmere */
void intel_pmu_lbr_init_nhm(void)
{
	x86_pmu.lbr_nr     = 16;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to     = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = nhm_lbr_sel_map;

	/*
	 * SW branch filter usage:
	 * - workaround LBR_SEL errata (see above)
	 * - support syscall, sysret capture.
	 *   That requires LBR_FAR but that means far
	 *   jmp need to be filtered out
	 */
	pr_cont("16-deep LBR, ");
}

/* sandy bridge */
void intel_pmu_lbr_init_snb(void)
{
	x86_pmu.lbr_nr	 = 16;
	x86_pmu.lbr_tos	 = MSR_LBR_TOS;
	x86_pmu.lbr_from = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to   = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = snb_lbr_sel_map;

	/*
	 * SW branch filter usage:
	 * - support syscall, sysret capture.
	 *   That requires LBR_FAR but that means far
	 *   jmp need to be filtered out
	 */
	pr_cont("16-deep LBR, ");
}

/* atom */
void intel_pmu_lbr_init_atom(void)
{
	/*
	 * only models starting at stepping 10 seems
	 * to have an operational LBR which can freeze
	 * on PMU interrupt
	 */
	if (boot_cpu_data.x86_mask < 10) {
		pr_cont("LBR disabled due to erratum");
		return;
	}

	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	/*
	 * SW branch filter usage:
	 * - compensate for lack of HW filter
	 */
	pr_cont("8-deep LBR, ");
}
