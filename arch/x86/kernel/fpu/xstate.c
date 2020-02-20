// SPDX-License-Identifier: GPL-2.0-only
/*
 * xsave/xrstor support.
 *
 * Author: Suresh Siddha <suresh.b.siddha@intel.com>
 */
#include <linux/compat.h>
#include <linux/cpu.h>
#include <linux/mman.h>
#include <linux/pkeys.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include <asm/fpu/api.h>
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/xstate.h>

#include <asm/tlbflush.h>
#include <asm/cpufeature.h>

/*
 * Although we spell it out in here, the Processor Trace
 * xfeature is completely unused.  We use other mechanisms
 * to save/restore PT state in Linux.
 */
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
	"Processor Trace (unused)"	,
	"Protection Keys User registers",
	"unknown xstate feature"	,
};

static short xsave_cpuid_features[] __initdata = {
	X86_FEATURE_FPU,
	X86_FEATURE_XMM,
	X86_FEATURE_AVX,
	X86_FEATURE_MPX,
	X86_FEATURE_MPX,
	X86_FEATURE_AVX512F,
	X86_FEATURE_AVX512F,
	X86_FEATURE_AVX512F,
	X86_FEATURE_INTEL_PT,
	X86_FEATURE_PKU,
};

/*
 * Mask of xstate features supported by the CPU and the kernel:
 */
u64 xfeatures_mask __read_mostly;

static unsigned int xstate_offsets[XFEATURE_MAX] = { [ 0 ... XFEATURE_MAX - 1] = -1};
static unsigned int xstate_sizes[XFEATURE_MAX]   = { [ 0 ... XFEATURE_MAX - 1] = -1};
static unsigned int xstate_comp_offsets[XFEATURE_MAX] = { [ 0 ... XFEATURE_MAX - 1] = -1};

/*
 * The XSAVE area of kernel can be in standard or compacted format;
 * it is always in standard format for user mode. This is the user
 * mode standard format size used for signal and ptrace frames.
 */
unsigned int fpu_user_xstate_size;

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
		 * asks about "XFEATURE_MASK_SSE | XFEATURE_MASK_YMM" we'll print the
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

static int xfeature_is_supervisor(int xfeature_nr)
{
	/*
	 * We currently do not support supervisor states, but if
	 * we did, we could find out like this.
	 *
	 * SDM says: If state component 'i' is a user state component,
	 * ECX[0] return 0; if state component i is a supervisor
	 * state component, ECX[0] returns 1.
	 */
	u32 eax, ebx, ecx, edx;

	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return !!(ecx & 1);
}

static int xfeature_is_user(int xfeature_nr)
{
	return !xfeature_is_supervisor(xfeature_nr);
}

/*
 * When executing XSAVEOPT (or other optimized XSAVE instructions), if
 * a processor implementation detects that an FPU state component is still
 * (or is again) in its initialized state, it may clear the corresponding
 * bit in the header.xfeatures field, and can skip the writeout of registers
 * to the corresponding memory layout.
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
	struct fxregs_state *fx = &fpu->state.fxsave;
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
	if (!(xfeatures & XFEATURE_MASK_FP)) {
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
	if (!(xfeatures & XFEATURE_MASK_SSE))
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
			int offset = xstate_comp_offsets[feature_bit];
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
	if (!boot_cpu_has(X86_FEATURE_XSAVE) || !xfeatures_mask)
		return;
	/*
	 * Make it clear that XSAVES supervisor states are not yet
	 * implemented should anyone expect it to work by changing
	 * bits in XFEATURE_MASK_* macros and XCR0.
	 */
	WARN_ONCE((xfeatures_mask & XFEATURE_MASK_SUPERVISOR),
		"x86/fpu: XSAVES supervisor states are not yet implemented.\n");

	xfeatures_mask &= ~XFEATURE_MASK_SUPERVISOR;

	cr4_set_bits(X86_CR4_OSXSAVE);
	xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask);
}

/*
 * Note that in the future we will likely need a pair of
 * functions here: one for user xstates and the other for
 * system xstates.  For now, they are the same.
 */
static int xfeature_enabled(enum xfeature xfeature)
{
	return !!(xfeatures_mask & (1UL << xfeature));
}

/*
 * Record the offsets and sizes of various xstates contained
 * in the XSAVE state memory layout.
 */
static void __init setup_xstate_features(void)
{
	u32 eax, ebx, ecx, edx, i;
	/* start at the beginnning of the "extended state" */
	unsigned int last_good_offset = offsetof(struct xregs_state,
						 extended_state_area);
	/*
	 * The FP xstates and SSE xstates are legacy states. They are always
	 * in the fixed offsets in the xsave area in either compacted form
	 * or standard form.
	 */
	xstate_offsets[XFEATURE_FP]	= 0;
	xstate_sizes[XFEATURE_FP]	= offsetof(struct fxregs_state,
						   xmm_space);

	xstate_offsets[XFEATURE_SSE]	= xstate_sizes[XFEATURE_FP];
	xstate_sizes[XFEATURE_SSE]	= sizeof_field(struct fxregs_state,
						       xmm_space);

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (!xfeature_enabled(i))
			continue;

		cpuid_count(XSTATE_CPUID, i, &eax, &ebx, &ecx, &edx);

		/*
		 * If an xfeature is supervisor state, the offset
		 * in EBX is invalid. We leave it to -1.
		 */
		if (xfeature_is_user(i))
			xstate_offsets[i] = ebx;

		xstate_sizes[i] = eax;
		/*
		 * In our xstate size checks, we assume that the
		 * highest-numbered xstate feature has the
		 * highest offset in the buffer.  Ensure it does.
		 */
		WARN_ONCE(last_good_offset > xstate_offsets[i],
			"x86/fpu: misordered xstate at %d\n", last_good_offset);
		last_good_offset = xstate_offsets[i];
	}
}

static void __init print_xstate_feature(u64 xstate_mask)
{
	const char *feature_name;

	if (cpu_has_xfeatures(xstate_mask, &feature_name))
		pr_info("x86/fpu: Supporting XSAVE feature 0x%03Lx: '%s'\n", xstate_mask, feature_name);
}

/*
 * Print out all the supported xstate features:
 */
static void __init print_xstate_features(void)
{
	print_xstate_feature(XFEATURE_MASK_FP);
	print_xstate_feature(XFEATURE_MASK_SSE);
	print_xstate_feature(XFEATURE_MASK_YMM);
	print_xstate_feature(XFEATURE_MASK_BNDREGS);
	print_xstate_feature(XFEATURE_MASK_BNDCSR);
	print_xstate_feature(XFEATURE_MASK_OPMASK);
	print_xstate_feature(XFEATURE_MASK_ZMM_Hi256);
	print_xstate_feature(XFEATURE_MASK_Hi16_ZMM);
	print_xstate_feature(XFEATURE_MASK_PKRU);
}

/*
 * This check is important because it is easy to get XSTATE_*
 * confused with XSTATE_BIT_*.
 */
#define CHECK_XFEATURE(nr) do {		\
	WARN_ON(nr < FIRST_EXTENDED_XFEATURE);	\
	WARN_ON(nr >= XFEATURE_MAX);	\
} while (0)

/*
 * We could cache this like xstate_size[], but we only use
 * it here, so it would be a waste of space.
 */
static int xfeature_is_aligned(int xfeature_nr)
{
	u32 eax, ebx, ecx, edx;

	CHECK_XFEATURE(xfeature_nr);
	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	/*
	 * The value returned by ECX[1] indicates the alignment
	 * of state component 'i' when the compacted format
	 * of the extended region of an XSAVE area is used:
	 */
	return !!(ecx & 2);
}

/*
 * This function sets up offsets and sizes of all extended states in
 * xsave area. This supports both standard format and compacted format
 * of the xsave aread.
 */
static void __init setup_xstate_comp(void)
{
	unsigned int xstate_comp_sizes[XFEATURE_MAX];
	int i;

	/*
	 * The FP xstates and SSE xstates are legacy states. They are always
	 * in the fixed offsets in the xsave area in either compacted form
	 * or standard form.
	 */
	xstate_comp_offsets[XFEATURE_FP] = 0;
	xstate_comp_offsets[XFEATURE_SSE] = offsetof(struct fxregs_state,
						     xmm_space);

	if (!boot_cpu_has(X86_FEATURE_XSAVES)) {
		for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
			if (xfeature_enabled(i)) {
				xstate_comp_offsets[i] = xstate_offsets[i];
				xstate_comp_sizes[i] = xstate_sizes[i];
			}
		}
		return;
	}

	xstate_comp_offsets[FIRST_EXTENDED_XFEATURE] =
		FXSAVE_SIZE + XSAVE_HDR_SIZE;

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (xfeature_enabled(i))
			xstate_comp_sizes[i] = xstate_sizes[i];
		else
			xstate_comp_sizes[i] = 0;

		if (i > FIRST_EXTENDED_XFEATURE) {
			xstate_comp_offsets[i] = xstate_comp_offsets[i-1]
					+ xstate_comp_sizes[i-1];

			if (xfeature_is_aligned(i))
				xstate_comp_offsets[i] =
					ALIGN(xstate_comp_offsets[i], 64);
		}
	}
}

/*
 * Print out xstate component offsets and sizes
 */
static void __init print_xstate_offset_size(void)
{
	int i;

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (!xfeature_enabled(i))
			continue;
		pr_info("x86/fpu: xstate_offset[%d]: %4d, xstate_sizes[%d]: %4d\n",
			 i, xstate_comp_offsets[i], i, xstate_sizes[i]);
	}
}

/*
 * setup the xstate image representing the init state
 */
static void __init setup_init_fpu_buf(void)
{
	static int on_boot_cpu __initdata = 1;

	WARN_ON_FPU(!on_boot_cpu);
	on_boot_cpu = 0;

	if (!boot_cpu_has(X86_FEATURE_XSAVE))
		return;

	setup_xstate_features();
	print_xstate_features();

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		init_fpstate.xsave.header.xcomp_bv = (u64)1 << 63 | xfeatures_mask;

	/*
	 * Init all the features state with header.xfeatures being 0x0
	 */
	copy_kernel_to_xregs_booting(&init_fpstate.xsave);

	/*
	 * Dump the init state again. This is to identify the init state
	 * of any feature which is not represented by all zero's.
	 */
	copy_xregs_to_kernel_booting(&init_fpstate.xsave);
}

static int xfeature_uncompacted_offset(int xfeature_nr)
{
	u32 eax, ebx, ecx, edx;

	/*
	 * Only XSAVES supports supervisor states and it uses compacted
	 * format. Checking a supervisor state's uncompacted offset is
	 * an error.
	 */
	if (XFEATURE_MASK_SUPERVISOR & BIT_ULL(xfeature_nr)) {
		WARN_ONCE(1, "No fixed offset for xstate %d\n", xfeature_nr);
		return -1;
	}

	CHECK_XFEATURE(xfeature_nr);
	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return ebx;
}

static int xfeature_size(int xfeature_nr)
{
	u32 eax, ebx, ecx, edx;

	CHECK_XFEATURE(xfeature_nr);
	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return eax;
}

/*
 * 'XSAVES' implies two different things:
 * 1. saving of supervisor/system state
 * 2. using the compacted format
 *
 * Use this function when dealing with the compacted format so
 * that it is obvious which aspect of 'XSAVES' is being handled
 * by the calling code.
 */
int using_compacted_format(void)
{
	return boot_cpu_has(X86_FEATURE_XSAVES);
}

/* Validate an xstate header supplied by userspace (ptrace or sigreturn) */
int validate_xstate_header(const struct xstate_header *hdr)
{
	/* No unknown or supervisor features may be set */
	if (hdr->xfeatures & (~xfeatures_mask | XFEATURE_MASK_SUPERVISOR))
		return -EINVAL;

	/* Userspace must use the uncompacted format */
	if (hdr->xcomp_bv)
		return -EINVAL;

	/*
	 * If 'reserved' is shrunken to add a new field, make sure to validate
	 * that new field here!
	 */
	BUILD_BUG_ON(sizeof(hdr->reserved) != 48);

	/* No reserved bits may be set */
	if (memchr_inv(hdr->reserved, 0, sizeof(hdr->reserved)))
		return -EINVAL;

	return 0;
}

static void __xstate_dump_leaves(void)
{
	int i;
	u32 eax, ebx, ecx, edx;
	static int should_dump = 1;

	if (!should_dump)
		return;
	should_dump = 0;
	/*
	 * Dump out a few leaves past the ones that we support
	 * just in case there are some goodies up there
	 */
	for (i = 0; i < XFEATURE_MAX + 10; i++) {
		cpuid_count(XSTATE_CPUID, i, &eax, &ebx, &ecx, &edx);
		pr_warn("CPUID[%02x, %02x]: eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
			XSTATE_CPUID, i, eax, ebx, ecx, edx);
	}
}

#define XSTATE_WARN_ON(x) do {							\
	if (WARN_ONCE(x, "XSAVE consistency problem, dumping leaves")) {	\
		__xstate_dump_leaves();						\
	}									\
} while (0)

#define XCHECK_SZ(sz, nr, nr_macro, __struct) do {			\
	if ((nr == nr_macro) &&						\
	    WARN_ONCE(sz != sizeof(__struct),				\
		"%s: struct is %zu bytes, cpu state %d bytes\n",	\
		__stringify(nr_macro), sizeof(__struct), sz)) {		\
		__xstate_dump_leaves();					\
	}								\
} while (0)

/*
 * We have a C struct for each 'xstate'.  We need to ensure
 * that our software representation matches what the CPU
 * tells us about the state's size.
 */
static void check_xstate_against_struct(int nr)
{
	/*
	 * Ask the CPU for the size of the state.
	 */
	int sz = xfeature_size(nr);
	/*
	 * Match each CPU state with the corresponding software
	 * structure.
	 */
	XCHECK_SZ(sz, nr, XFEATURE_YMM,       struct ymmh_struct);
	XCHECK_SZ(sz, nr, XFEATURE_BNDREGS,   struct mpx_bndreg_state);
	XCHECK_SZ(sz, nr, XFEATURE_BNDCSR,    struct mpx_bndcsr_state);
	XCHECK_SZ(sz, nr, XFEATURE_OPMASK,    struct avx_512_opmask_state);
	XCHECK_SZ(sz, nr, XFEATURE_ZMM_Hi256, struct avx_512_zmm_uppers_state);
	XCHECK_SZ(sz, nr, XFEATURE_Hi16_ZMM,  struct avx_512_hi16_state);
	XCHECK_SZ(sz, nr, XFEATURE_PKRU,      struct pkru_state);

	/*
	 * Make *SURE* to add any feature numbers in below if
	 * there are "holes" in the xsave state component
	 * numbers.
	 */
	if ((nr < XFEATURE_YMM) ||
	    (nr >= XFEATURE_MAX) ||
	    (nr == XFEATURE_PT_UNIMPLEMENTED_SO_FAR)) {
		WARN_ONCE(1, "no structure for xstate: %d\n", nr);
		XSTATE_WARN_ON(1);
	}
}

/*
 * This essentially double-checks what the cpu told us about
 * how large the XSAVE buffer needs to be.  We are recalculating
 * it to be safe.
 */
static void do_extra_xstate_size_checks(void)
{
	int paranoid_xstate_size = FXSAVE_SIZE + XSAVE_HDR_SIZE;
	int i;

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (!xfeature_enabled(i))
			continue;

		check_xstate_against_struct(i);
		/*
		 * Supervisor state components can be managed only by
		 * XSAVES, which is compacted-format only.
		 */
		if (!using_compacted_format())
			XSTATE_WARN_ON(xfeature_is_supervisor(i));

		/* Align from the end of the previous feature */
		if (xfeature_is_aligned(i))
			paranoid_xstate_size = ALIGN(paranoid_xstate_size, 64);
		/*
		 * The offset of a given state in the non-compacted
		 * format is given to us in a CPUID leaf.  We check
		 * them for being ordered (increasing offsets) in
		 * setup_xstate_features().
		 */
		if (!using_compacted_format())
			paranoid_xstate_size = xfeature_uncompacted_offset(i);
		/*
		 * The compacted-format offset always depends on where
		 * the previous state ended.
		 */
		paranoid_xstate_size += xfeature_size(i);
	}
	XSTATE_WARN_ON(paranoid_xstate_size != fpu_kernel_xstate_size);
}


/*
 * Get total size of enabled xstates in XCR0/xfeatures_mask.
 *
 * Note the SDM's wording here.  "sub-function 0" only enumerates
 * the size of the *user* states.  If we use it to size a buffer
 * that we use 'XSAVES' on, we could potentially overflow the
 * buffer because 'XSAVES' saves system states too.
 *
 * Note that we do not currently set any bits on IA32_XSS so
 * 'XCR0 | IA32_XSS == XCR0' for now.
 */
static unsigned int __init get_xsaves_size(void)
{
	unsigned int eax, ebx, ecx, edx;
	/*
	 * - CPUID function 0DH, sub-function 1:
	 *    EBX enumerates the size (in bytes) required by
	 *    the XSAVES instruction for an XSAVE area
	 *    containing all the state components
	 *    corresponding to bits currently set in
	 *    XCR0 | IA32_XSS.
	 */
	cpuid_count(XSTATE_CPUID, 1, &eax, &ebx, &ecx, &edx);
	return ebx;
}

static unsigned int __init get_xsave_size(void)
{
	unsigned int eax, ebx, ecx, edx;
	/*
	 * - CPUID function 0DH, sub-function 0:
	 *    EBX enumerates the size (in bytes) required by
	 *    the XSAVE instruction for an XSAVE area
	 *    containing all the *user* state components
	 *    corresponding to bits currently set in XCR0.
	 */
	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
	return ebx;
}

/*
 * Will the runtime-enumerated 'xstate_size' fit in the init
 * task's statically-allocated buffer?
 */
static bool is_supported_xstate_size(unsigned int test_xstate_size)
{
	if (test_xstate_size <= sizeof(union fpregs_state))
		return true;

	pr_warn("x86/fpu: xstate buffer too small (%zu < %d), disabling xsave\n",
			sizeof(union fpregs_state), test_xstate_size);
	return false;
}

static int __init init_xstate_size(void)
{
	/* Recompute the context size for enabled features: */
	unsigned int possible_xstate_size;
	unsigned int xsave_size;

	xsave_size = get_xsave_size();

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		possible_xstate_size = get_xsaves_size();
	else
		possible_xstate_size = xsave_size;

	/* Ensure we have the space to store all enabled: */
	if (!is_supported_xstate_size(possible_xstate_size))
		return -EINVAL;

	/*
	 * The size is OK, we are definitely going to use xsave,
	 * make it known to the world that we need more space.
	 */
	fpu_kernel_xstate_size = possible_xstate_size;
	do_extra_xstate_size_checks();

	/*
	 * User space is always in standard format.
	 */
	fpu_user_xstate_size = xsave_size;
	return 0;
}

/*
 * We enabled the XSAVE hardware, but something went wrong and
 * we can not use it.  Disable it.
 */
static void fpu__init_disable_system_xstate(void)
{
	xfeatures_mask = 0;
	cr4_clear_bits(X86_CR4_OSXSAVE);
	setup_clear_cpu_cap(X86_FEATURE_XSAVE);
}

/*
 * Enable and initialize the xsave feature.
 * Called once per system bootup.
 */
void __init fpu__init_system_xstate(void)
{
	unsigned int eax, ebx, ecx, edx;
	static int on_boot_cpu __initdata = 1;
	int err;
	int i;

	WARN_ON_FPU(!on_boot_cpu);
	on_boot_cpu = 0;

	if (!boot_cpu_has(X86_FEATURE_FPU)) {
		pr_info("x86/fpu: No FPU detected\n");
		return;
	}

	if (!boot_cpu_has(X86_FEATURE_XSAVE)) {
		pr_info("x86/fpu: x87 FPU will use %s\n",
			boot_cpu_has(X86_FEATURE_FXSR) ? "FXSAVE" : "FSAVE");
		return;
	}

	if (boot_cpu_data.cpuid_level < XSTATE_CPUID) {
		WARN_ON_FPU(1);
		return;
	}

	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
	xfeatures_mask = eax + ((u64)edx << 32);

	if ((xfeatures_mask & XFEATURE_MASK_FPSSE) != XFEATURE_MASK_FPSSE) {
		/*
		 * This indicates that something really unexpected happened
		 * with the enumeration.  Disable XSAVE and try to continue
		 * booting without it.  This is too early to BUG().
		 */
		pr_err("x86/fpu: FP/SSE not present amongst the CPU's xstate features: 0x%llx.\n", xfeatures_mask);
		goto out_disable;
	}

	/*
	 * Clear XSAVE features that are disabled in the normal CPUID.
	 */
	for (i = 0; i < ARRAY_SIZE(xsave_cpuid_features); i++) {
		if (!boot_cpu_has(xsave_cpuid_features[i]))
			xfeatures_mask &= ~BIT(i);
	}

	xfeatures_mask &= fpu__get_supported_xfeatures_mask();

	/* Enable xstate instructions to be able to continue with initialization: */
	fpu__init_cpu_xstate();
	err = init_xstate_size();
	if (err)
		goto out_disable;

	/*
	 * Update info used for ptrace frames; use standard-format size and no
	 * supervisor xstates:
	 */
	update_regset_xstate_info(fpu_user_xstate_size,	xfeatures_mask & ~XFEATURE_MASK_SUPERVISOR);

	fpu__init_prepare_fx_sw_frame();
	setup_init_fpu_buf();
	setup_xstate_comp();
	print_xstate_offset_size();

	pr_info("x86/fpu: Enabled xstate features 0x%llx, context size is %d bytes, using '%s' format.\n",
		xfeatures_mask,
		fpu_kernel_xstate_size,
		boot_cpu_has(X86_FEATURE_XSAVES) ? "compacted" : "standard");
	return;

out_disable:
	/* something went wrong, try to boot without any XSAVE support */
	fpu__init_disable_system_xstate();
}

/*
 * Restore minimal FPU state after suspend:
 */
void fpu__resume_cpu(void)
{
	/*
	 * Restore XCR0 on xsave capable CPUs:
	 */
	if (boot_cpu_has(X86_FEATURE_XSAVE))
		xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask);
}

/*
 * Given an xstate feature nr, calculate where in the xsave
 * buffer the state is.  Callers should ensure that the buffer
 * is valid.
 */
static void *__raw_xsave_addr(struct xregs_state *xsave, int xfeature_nr)
{
	if (!xfeature_enabled(xfeature_nr)) {
		WARN_ON_FPU(1);
		return NULL;
	}

	return (void *)xsave + xstate_comp_offsets[xfeature_nr];
}
/*
 * Given the xsave area and a state inside, this function returns the
 * address of the state.
 *
 * This is the API that is called to get xstate address in either
 * standard format or compacted format of xsave area.
 *
 * Note that if there is no data for the field in the xsave buffer
 * this will return NULL.
 *
 * Inputs:
 *	xstate: the thread's storage area for all FPU data
 *	xfeature_nr: state which is defined in xsave.h (e.g. XFEATURE_FP,
 *	XFEATURE_SSE, etc...)
 * Output:
 *	address of the state in the xsave area, or NULL if the
 *	field is not present in the xsave buffer.
 */
void *get_xsave_addr(struct xregs_state *xsave, int xfeature_nr)
{
	/*
	 * Do we even *have* xsave state?
	 */
	if (!boot_cpu_has(X86_FEATURE_XSAVE))
		return NULL;

	/*
	 * We should not ever be requesting features that we
	 * have not enabled.  Remember that xfeatures_mask is
	 * what we write to the XCR0 register.
	 */
	WARN_ONCE(!(xfeatures_mask & BIT_ULL(xfeature_nr)),
		  "get of unsupported state");
	/*
	 * This assumes the last 'xsave*' instruction to
	 * have requested that 'xfeature_nr' be saved.
	 * If it did not, we might be seeing and old value
	 * of the field in the buffer.
	 *
	 * This can happen because the last 'xsave' did not
	 * request that this feature be saved (unlikely)
	 * or because the "init optimization" caused it
	 * to not be saved.
	 */
	if (!(xsave->header.xfeatures & BIT_ULL(xfeature_nr)))
		return NULL;

	return __raw_xsave_addr(xsave, xfeature_nr);
}
EXPORT_SYMBOL_GPL(get_xsave_addr);

/*
 * This wraps up the common operations that need to occur when retrieving
 * data from xsave state.  It first ensures that the current task was
 * using the FPU and retrieves the data in to a buffer.  It then calculates
 * the offset of the requested field in the buffer.
 *
 * This function is safe to call whether the FPU is in use or not.
 *
 * Note that this only works on the current task.
 *
 * Inputs:
 *	@xfeature_nr: state which is defined in xsave.h (e.g. XFEATURE_FP,
 *	XFEATURE_SSE, etc...)
 * Output:
 *	address of the state in the xsave area or NULL if the state
 *	is not present or is in its 'init state'.
 */
const void *get_xsave_field_ptr(int xfeature_nr)
{
	struct fpu *fpu = &current->thread.fpu;

	/*
	 * fpu__save() takes the CPU's xstate registers
	 * and saves them off to the 'fpu memory buffer.
	 */
	fpu__save(fpu);

	return get_xsave_addr(&fpu->state.xsave, xfeature_nr);
}

#ifdef CONFIG_ARCH_HAS_PKEYS

#define NR_VALID_PKRU_BITS (CONFIG_NR_PROTECTION_KEYS * 2)
#define PKRU_VALID_MASK (NR_VALID_PKRU_BITS - 1)
/*
 * This will go out and modify PKRU register to set the access
 * rights for @pkey to @init_val.
 */
int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val)
{
	u32 old_pkru;
	int pkey_shift = (pkey * PKRU_BITS_PER_PKEY);
	u32 new_pkru_bits = 0;

	/*
	 * This check implies XSAVE support.  OSPKE only gets
	 * set if we enable XSAVE and we enable PKU in XCR0.
	 */
	if (!boot_cpu_has(X86_FEATURE_OSPKE))
		return -EINVAL;

	/* Set the bits we need in PKRU:  */
	if (init_val & PKEY_DISABLE_ACCESS)
		new_pkru_bits |= PKRU_AD_BIT;
	if (init_val & PKEY_DISABLE_WRITE)
		new_pkru_bits |= PKRU_WD_BIT;

	/* Shift the bits in to the correct place in PKRU for pkey: */
	new_pkru_bits <<= pkey_shift;

	/* Get old PKRU and mask off any old bits in place: */
	old_pkru = read_pkru();
	old_pkru &= ~((PKRU_AD_BIT|PKRU_WD_BIT) << pkey_shift);

	/* Write old part along with new part: */
	write_pkru(old_pkru | new_pkru_bits);

	return 0;
}
#endif /* ! CONFIG_ARCH_HAS_PKEYS */

/*
 * Weird legacy quirk: SSE and YMM states store information in the
 * MXCSR and MXCSR_FLAGS fields of the FP area. That means if the FP
 * area is marked as unused in the xfeatures header, we need to copy
 * MXCSR and MXCSR_FLAGS if either SSE or YMM are in use.
 */
static inline bool xfeatures_mxcsr_quirk(u64 xfeatures)
{
	if (!(xfeatures & (XFEATURE_MASK_SSE|XFEATURE_MASK_YMM)))
		return false;

	if (xfeatures & XFEATURE_MASK_FP)
		return false;

	return true;
}

/*
 * This is similar to user_regset_copyout(), but will not add offset to
 * the source data pointer or increment pos, count, kbuf, and ubuf.
 */
static inline void
__copy_xstate_to_kernel(void *kbuf, const void *data,
			unsigned int offset, unsigned int size, unsigned int size_total)
{
	if (offset < size_total) {
		unsigned int copy = min(size, size_total - offset);

		memcpy(kbuf + offset, data, copy);
	}
}

/*
 * Convert from kernel XSAVES compacted format to standard format and copy
 * to a kernel-space ptrace buffer.
 *
 * It supports partial copy but pos always starts from zero. This is called
 * from xstateregs_get() and there we check the CPU has XSAVES.
 */
int copy_xstate_to_kernel(void *kbuf, struct xregs_state *xsave, unsigned int offset_start, unsigned int size_total)
{
	unsigned int offset, size;
	struct xstate_header header;
	int i;

	/*
	 * Currently copy_regset_to_user() starts from pos 0:
	 */
	if (unlikely(offset_start != 0))
		return -EFAULT;

	/*
	 * The destination is a ptrace buffer; we put in only user xstates:
	 */
	memset(&header, 0, sizeof(header));
	header.xfeatures = xsave->header.xfeatures;
	header.xfeatures &= ~XFEATURE_MASK_SUPERVISOR;

	/*
	 * Copy xregs_state->header:
	 */
	offset = offsetof(struct xregs_state, header);
	size = sizeof(header);

	__copy_xstate_to_kernel(kbuf, &header, offset, size, size_total);

	for (i = 0; i < XFEATURE_MAX; i++) {
		/*
		 * Copy only in-use xstates:
		 */
		if ((header.xfeatures >> i) & 1) {
			void *src = __raw_xsave_addr(xsave, i);

			offset = xstate_offsets[i];
			size = xstate_sizes[i];

			/* The next component has to fit fully into the output buffer: */
			if (offset + size > size_total)
				break;

			__copy_xstate_to_kernel(kbuf, src, offset, size, size_total);
		}

	}

	if (xfeatures_mxcsr_quirk(header.xfeatures)) {
		offset = offsetof(struct fxregs_state, mxcsr);
		size = MXCSR_AND_FLAGS_SIZE;
		__copy_xstate_to_kernel(kbuf, &xsave->i387.mxcsr, offset, size, size_total);
	}

	/*
	 * Fill xsave->i387.sw_reserved value for ptrace frame:
	 */
	offset = offsetof(struct fxregs_state, sw_reserved);
	size = sizeof(xstate_fx_sw_bytes);

	__copy_xstate_to_kernel(kbuf, xstate_fx_sw_bytes, offset, size, size_total);

	return 0;
}

static inline int
__copy_xstate_to_user(void __user *ubuf, const void *data, unsigned int offset, unsigned int size, unsigned int size_total)
{
	if (!size)
		return 0;

	if (offset < size_total) {
		unsigned int copy = min(size, size_total - offset);

		if (__copy_to_user(ubuf + offset, data, copy))
			return -EFAULT;
	}
	return 0;
}

/*
 * Convert from kernel XSAVES compacted format to standard format and copy
 * to a user-space buffer. It supports partial copy but pos always starts from
 * zero. This is called from xstateregs_get() and there we check the CPU
 * has XSAVES.
 */
int copy_xstate_to_user(void __user *ubuf, struct xregs_state *xsave, unsigned int offset_start, unsigned int size_total)
{
	unsigned int offset, size;
	int ret, i;
	struct xstate_header header;

	/*
	 * Currently copy_regset_to_user() starts from pos 0:
	 */
	if (unlikely(offset_start != 0))
		return -EFAULT;

	/*
	 * The destination is a ptrace buffer; we put in only user xstates:
	 */
	memset(&header, 0, sizeof(header));
	header.xfeatures = xsave->header.xfeatures;
	header.xfeatures &= ~XFEATURE_MASK_SUPERVISOR;

	/*
	 * Copy xregs_state->header:
	 */
	offset = offsetof(struct xregs_state, header);
	size = sizeof(header);

	ret = __copy_xstate_to_user(ubuf, &header, offset, size, size_total);
	if (ret)
		return ret;

	for (i = 0; i < XFEATURE_MAX; i++) {
		/*
		 * Copy only in-use xstates:
		 */
		if ((header.xfeatures >> i) & 1) {
			void *src = __raw_xsave_addr(xsave, i);

			offset = xstate_offsets[i];
			size = xstate_sizes[i];

			/* The next component has to fit fully into the output buffer: */
			if (offset + size > size_total)
				break;

			ret = __copy_xstate_to_user(ubuf, src, offset, size, size_total);
			if (ret)
				return ret;
		}

	}

	if (xfeatures_mxcsr_quirk(header.xfeatures)) {
		offset = offsetof(struct fxregs_state, mxcsr);
		size = MXCSR_AND_FLAGS_SIZE;
		__copy_xstate_to_user(ubuf, &xsave->i387.mxcsr, offset, size, size_total);
	}

	/*
	 * Fill xsave->i387.sw_reserved value for ptrace frame:
	 */
	offset = offsetof(struct fxregs_state, sw_reserved);
	size = sizeof(xstate_fx_sw_bytes);

	ret = __copy_xstate_to_user(ubuf, xstate_fx_sw_bytes, offset, size, size_total);
	if (ret)
		return ret;

	return 0;
}

/*
 * Convert from a ptrace standard-format kernel buffer to kernel XSAVES format
 * and copy to the target thread. This is called from xstateregs_set().
 */
int copy_kernel_to_xstate(struct xregs_state *xsave, const void *kbuf)
{
	unsigned int offset, size;
	int i;
	struct xstate_header hdr;

	offset = offsetof(struct xregs_state, header);
	size = sizeof(hdr);

	memcpy(&hdr, kbuf + offset, size);

	if (validate_xstate_header(&hdr))
		return -EINVAL;

	for (i = 0; i < XFEATURE_MAX; i++) {
		u64 mask = ((u64)1 << i);

		if (hdr.xfeatures & mask) {
			void *dst = __raw_xsave_addr(xsave, i);

			offset = xstate_offsets[i];
			size = xstate_sizes[i];

			memcpy(dst, kbuf + offset, size);
		}
	}

	if (xfeatures_mxcsr_quirk(hdr.xfeatures)) {
		offset = offsetof(struct fxregs_state, mxcsr);
		size = MXCSR_AND_FLAGS_SIZE;
		memcpy(&xsave->i387.mxcsr, kbuf + offset, size);
	}

	/*
	 * The state that came in from userspace was user-state only.
	 * Mask all the user states out of 'xfeatures':
	 */
	xsave->header.xfeatures &= XFEATURE_MASK_SUPERVISOR;

	/*
	 * Add back in the features that came in from userspace:
	 */
	xsave->header.xfeatures |= hdr.xfeatures;

	return 0;
}

/*
 * Convert from a ptrace or sigreturn standard-format user-space buffer to
 * kernel XSAVES format and copy to the target thread. This is called from
 * xstateregs_set(), as well as potentially from the sigreturn() and
 * rt_sigreturn() system calls.
 */
int copy_user_to_xstate(struct xregs_state *xsave, const void __user *ubuf)
{
	unsigned int offset, size;
	int i;
	struct xstate_header hdr;

	offset = offsetof(struct xregs_state, header);
	size = sizeof(hdr);

	if (__copy_from_user(&hdr, ubuf + offset, size))
		return -EFAULT;

	if (validate_xstate_header(&hdr))
		return -EINVAL;

	for (i = 0; i < XFEATURE_MAX; i++) {
		u64 mask = ((u64)1 << i);

		if (hdr.xfeatures & mask) {
			void *dst = __raw_xsave_addr(xsave, i);

			offset = xstate_offsets[i];
			size = xstate_sizes[i];

			if (__copy_from_user(dst, ubuf + offset, size))
				return -EFAULT;
		}
	}

	if (xfeatures_mxcsr_quirk(hdr.xfeatures)) {
		offset = offsetof(struct fxregs_state, mxcsr);
		size = MXCSR_AND_FLAGS_SIZE;
		if (__copy_from_user(&xsave->i387.mxcsr, ubuf + offset, size))
			return -EFAULT;
	}

	/*
	 * The state that came in from userspace was user-state only.
	 * Mask all the user states out of 'xfeatures':
	 */
	xsave->header.xfeatures &= XFEATURE_MASK_SUPERVISOR;

	/*
	 * Add back in the features that came in from userspace:
	 */
	xsave->header.xfeatures |= hdr.xfeatures;

	return 0;
}

#ifdef CONFIG_PROC_PID_ARCH_STATUS
/*
 * Report the amount of time elapsed in millisecond since last AVX512
 * use in the task.
 */
static void avx512_status(struct seq_file *m, struct task_struct *task)
{
	unsigned long timestamp = READ_ONCE(task->thread.fpu.avx512_timestamp);
	long delta;

	if (!timestamp) {
		/*
		 * Report -1 if no AVX512 usage
		 */
		delta = -1;
	} else {
		delta = (long)(jiffies - timestamp);
		/*
		 * Cap to LONG_MAX if time difference > LONG_MAX
		 */
		if (delta < 0)
			delta = LONG_MAX;
		delta = jiffies_to_msecs(delta);
	}

	seq_put_decimal_ll(m, "AVX512_elapsed_ms:\t", delta);
	seq_putc(m, '\n');
}

/*
 * Report architecture specific information
 */
int proc_pid_arch_status(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	/*
	 * Report AVX512 state if the processor and build option supported.
	 */
	if (cpu_feature_enabled(X86_FEATURE_AVX512F))
		avx512_status(m, task);

	return 0;
}
#endif /* CONFIG_PROC_PID_ARCH_STATUS */
