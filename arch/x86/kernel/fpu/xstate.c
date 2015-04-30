/*
 * xsave/xrstor support.
 *
 * Author: Suresh Siddha <suresh.b.siddha@intel.com>
 */
#include <linux/compat.h>
#include <linux/cpu.h>

#include <asm/fpu/api.h>
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>

#include <asm/tlbflush.h>

static const char *xfeature_names[] =
{
	"x87 floating point registers"	,
	"SSE registers"			,
	"AVX registers"			,
	"MPX bounds registers"		,
	"MPX CSR"			,
	"AVX-512 opmask"		,
	"AVX-512 Hi256"			,
	"AVX-512 ZMM_Hi256"		,
	"unknown xstate feature"	,
};

/*
 * Mask of xstate features supported by the CPU and the kernel:
 */
u64 xfeatures_mask __read_mostly;

static unsigned int xstate_offsets[XFEATURES_NR_MAX], xstate_sizes[XFEATURES_NR_MAX];
static unsigned int xstate_comp_offsets[sizeof(xfeatures_mask)*8];

/* The number of supported xfeatures in xfeatures_mask: */
static unsigned int xfeatures_nr;

/*
 * Return whether the system supports a given xfeature.
 *
 * Also return the name of the (most advanced) feature that the caller requested:
 */
int cpu_has_xfeatures(u64 xfeatures_needed, const char **feature_name)
{
	u64 xfeatures_missing = xfeatures_needed & ~xfeatures_mask;

	if (unlikely(feature_name)) {
		long xfeature_idx, max_idx;
		u64 xfeatures_print;
		/*
		 * So we use FLS here to be able to print the most advanced
		 * feature that was requested but is missing. So if a driver
		 * asks about "XSTATE_SSE | XSTATE_YMM" we'll print the
		 * missing AVX feature - this is the most informative message
		 * to users:
		 */
		if (xfeatures_missing)
			xfeatures_print = xfeatures_missing;
		else
			xfeatures_print = xfeatures_needed;

		xfeature_idx = fls64(xfeatures_print)-1;
		max_idx = ARRAY_SIZE(xfeature_names)-1;
		xfeature_idx = min(xfeature_idx, max_idx);

		*feature_name = xfeature_names[xfeature_idx];
	}

	if (xfeatures_missing)
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(cpu_has_xfeatures);

/*
 * When executing XSAVEOPT (optimized XSAVE), if a processor implementation
 * detects that an FPU state component is still (or is again) in its
 * initialized state, it may clear the corresponding bit in the header.xfeatures
 * field, and can skip the writeout of registers to the corresponding memory layout.
 *
 * This means that when the bit is zero, the state component might still contain
 * some previous - non-initialized register state.
 *
 * Before writing xstate information to user-space we sanitize those components,
 * to always ensure that the memory layout of a feature will be in the init state
 * if the corresponding header bit is zero. This is to ensure that user-space doesn't
 * see some stale state in the memory layout during signal handling, debugging etc.
 */
void fpstate_sanitize_xstate(struct fpu *fpu)
{
	struct i387_fxsave_struct *fx = &fpu->state.fxsave;
	int feature_bit;
	u64 xfeatures;

	if (!use_xsaveopt())
		return;

	xfeatures = fpu->state.xsave.header.xfeatures;

	/*
	 * None of the feature bits are in init state. So nothing else
	 * to do for us, as the memory layout is up to date.
	 */
	if ((xfeatures & xfeatures_mask) == xfeatures_mask)
		return;

	/*
	 * FP is in init state
	 */
	if (!(xfeatures & XSTATE_FP)) {
		fx->cwd = 0x37f;
		fx->swd = 0;
		fx->twd = 0;
		fx->fop = 0;
		fx->rip = 0;
		fx->rdp = 0;
		memset(&fx->st_space[0], 0, 128);
	}

	/*
	 * SSE is in init state
	 */
	if (!(xfeatures & XSTATE_SSE))
		memset(&fx->xmm_space[0], 0, 256);

	/*
	 * First two features are FPU and SSE, which above we handled
	 * in a special way already:
	 */
	feature_bit = 0x2;
	xfeatures = (xfeatures_mask & ~xfeatures) >> 2;

	/*
	 * Update all the remaining memory layouts according to their
	 * standard xstate layout, if their header bit is in the init
	 * state:
	 */
	while (xfeatures) {
		if (xfeatures & 0x1) {
			int offset = xstate_offsets[feature_bit];
			int size = xstate_sizes[feature_bit];

			memcpy((void *)fx + offset,
			       (void *)&init_fpstate.xsave + offset,
			       size);
		}

		xfeatures >>= 1;
		feature_bit++;
	}
}

/*
 * Enable the extended processor state save/restore feature.
 * Called once per CPU onlining.
 */
void fpu__init_cpu_xstate(void)
{
	if (!cpu_has_xsave || !xfeatures_mask)
		return;

	cr4_set_bits(X86_CR4_OSXSAVE);
	xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask);
}

/*
 * Record the offsets and sizes of different state managed by the xsave
 * memory layout.
 */
static void __init setup_xstate_features(void)
{
	int eax, ebx, ecx, edx, leaf = 0x2;

	xfeatures_nr = fls64(xfeatures_mask);

	do {
		cpuid_count(XSTATE_CPUID, leaf, &eax, &ebx, &ecx, &edx);

		if (eax == 0)
			break;

		xstate_offsets[leaf] = ebx;
		xstate_sizes[leaf] = eax;

		leaf++;
	} while (1);
}

static void print_xstate_feature(u64 xstate_mask)
{
	const char *feature_name;

	if (cpu_has_xfeatures(xstate_mask, &feature_name))
		pr_info("x86/fpu: Supporting XSAVE feature 0x%02Lx: '%s'\n", xstate_mask, feature_name);
}

/*
 * Print out all the supported xstate features:
 */
static void print_xstate_features(void)
{
	print_xstate_feature(XSTATE_FP);
	print_xstate_feature(XSTATE_SSE);
	print_xstate_feature(XSTATE_YMM);
	print_xstate_feature(XSTATE_BNDREGS);
	print_xstate_feature(XSTATE_BNDCSR);
	print_xstate_feature(XSTATE_OPMASK);
	print_xstate_feature(XSTATE_ZMM_Hi256);
	print_xstate_feature(XSTATE_Hi16_ZMM);
}

/*
 * This function sets up offsets and sizes of all extended states in
 * xsave area. This supports both standard format and compacted format
 * of the xsave aread.
 *
 * Input: void
 * Output: void
 */
void setup_xstate_comp(void)
{
	unsigned int xstate_comp_sizes[sizeof(xfeatures_mask)*8];
	int i;

	/*
	 * The FP xstates and SSE xstates are legacy states. They are always
	 * in the fixed offsets in the xsave area in either compacted form
	 * or standard form.
	 */
	xstate_comp_offsets[0] = 0;
	xstate_comp_offsets[1] = offsetof(struct i387_fxsave_struct, xmm_space);

	if (!cpu_has_xsaves) {
		for (i = 2; i < xfeatures_nr; i++) {
			if (test_bit(i, (unsigned long *)&xfeatures_mask)) {
				xstate_comp_offsets[i] = xstate_offsets[i];
				xstate_comp_sizes[i] = xstate_sizes[i];
			}
		}
		return;
	}

	xstate_comp_offsets[2] = FXSAVE_SIZE + XSAVE_HDR_SIZE;

	for (i = 2; i < xfeatures_nr; i++) {
		if (test_bit(i, (unsigned long *)&xfeatures_mask))
			xstate_comp_sizes[i] = xstate_sizes[i];
		else
			xstate_comp_sizes[i] = 0;

		if (i > 2)
			xstate_comp_offsets[i] = xstate_comp_offsets[i-1]
					+ xstate_comp_sizes[i-1];

	}
}

/*
 * setup the xstate image representing the init state
 */
static void setup_init_fpu_buf(void)
{
	if (!cpu_has_xsave)
		return;

	setup_xstate_features();
	print_xstate_features();

	if (cpu_has_xsaves) {
		init_fpstate.xsave.header.xcomp_bv = (u64)1 << 63 | xfeatures_mask;
		init_fpstate.xsave.header.xfeatures = xfeatures_mask;
	}

	/*
	 * Init all the features state with header_bv being 0x0
	 */
	copy_kernel_to_xregs_booting(&init_fpstate.xsave, -1);

	/*
	 * Dump the init state again. This is to identify the init state
	 * of any feature which is not represented by all zero's.
	 */
	copy_xregs_to_kernel_booting(&init_fpstate.xsave);
}

/*
 * Calculate total size of enabled xstates in XCR0/xfeatures_mask.
 */
static void __init init_xstate_size(void)
{
	unsigned int eax, ebx, ecx, edx;
	int i;

	if (!cpu_has_xsaves) {
		cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
		xstate_size = ebx;
		return;
	}

	xstate_size = FXSAVE_SIZE + XSAVE_HDR_SIZE;
	for (i = 2; i < 64; i++) {
		if (test_bit(i, (unsigned long *)&xfeatures_mask)) {
			cpuid_count(XSTATE_CPUID, i, &eax, &ebx, &ecx, &edx);
			xstate_size += eax;
		}
	}
}

/*
 * Enable and initialize the xsave feature.
 * Called once per system bootup.
 *
 * ( Not marked __init because of false positive section warnings. )
 */
void fpu__init_system_xstate(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (!cpu_has_xsave) {
		pr_info("x86/fpu: Legacy x87 FPU detected.\n");
		return;
	}

	if (boot_cpu_data.cpuid_level < XSTATE_CPUID) {
		WARN(1, "x86/fpu: XSTATE_CPUID missing!\n");
		return;
	}

	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
	xfeatures_mask = eax + ((u64)edx << 32);

	if ((xfeatures_mask & XSTATE_FPSSE) != XSTATE_FPSSE) {
		pr_err("x86/fpu: FP/SSE not present amongst the CPU's xstate features: 0x%llx.\n", xfeatures_mask);
		BUG();
	}

	/*
	 * Support only the state known to OS.
	 */
	xfeatures_mask = xfeatures_mask & XCNTXT_MASK;

	/* Enable xstate instructions to be able to continue with initialization: */
	fpu__init_cpu_xstate();

	/*
	 * Recompute the context size for enabled features
	 */
	init_xstate_size();

	update_regset_xstate_info(xstate_size, xfeatures_mask);
	fpu__init_prepare_fx_sw_frame();
	setup_init_fpu_buf();

	pr_info("x86/fpu: Enabled xstate features 0x%llx, context size is 0x%x bytes, using '%s' format.\n",
		xfeatures_mask,
		xstate_size,
		cpu_has_xsaves ? "compacted" : "standard");
}

/*
 * Restore minimal FPU state after suspend:
 */
void fpu__resume_cpu(void)
{
	/*
	 * Restore XCR0 on xsave capable CPUs:
	 */
	if (cpu_has_xsave)
		xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask);
}

/*
 * Given the xsave area and a state inside, this function returns the
 * address of the state.
 *
 * This is the API that is called to get xstate address in either
 * standard format or compacted format of xsave area.
 *
 * Inputs:
 *	xsave: base address of the xsave area;
 *	xstate: state which is defined in xsave.h (e.g. XSTATE_FP, XSTATE_SSE,
 *	etc.)
 * Output:
 *	address of the state in the xsave area.
 */
void *get_xsave_addr(struct xsave_struct *xsave, int xstate)
{
	int feature = fls64(xstate) - 1;
	if (!test_bit(feature, (unsigned long *)&xfeatures_mask))
		return NULL;

	return (void *)xsave + xstate_comp_offsets[feature];
}
EXPORT_SYMBOL_GPL(get_xsave_addr);
