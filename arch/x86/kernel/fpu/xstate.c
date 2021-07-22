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
	"PASID state",
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
	X86_FEATURE_ENQCMD,
};

/*
 * This represents the full set of bits that should ever be set in a kernel
 * XSAVE buffer, both supervisor and user xstates.
 */
u64 xfeatures_mask_all __ro_after_init;
EXPORT_SYMBOL_GPL(xfeatures_mask_all);

static unsigned int xstate_offsets[XFEATURE_MAX] __ro_after_init =
	{ [ 0 ... XFEATURE_MAX - 1] = -1};
static unsigned int xstate_sizes[XFEATURE_MAX] __ro_after_init =
	{ [ 0 ... XFEATURE_MAX - 1] = -1};
static unsigned int xstate_comp_offsets[XFEATURE_MAX] __ro_after_init =
	{ [ 0 ... XFEATURE_MAX - 1] = -1};
static unsigned int xstate_supervisor_only_offsets[XFEATURE_MAX] __ro_after_init =
	{ [ 0 ... XFEATURE_MAX - 1] = -1};

/*
 * The XSAVE area of kernel can be in standard or compacted format;
 * it is always in standard format for user mode. This is the user
 * mode standard format size used for signal and ptrace frames.
 */
unsigned int fpu_user_xstate_size __ro_after_init;

/*
 * Return whether the system supports a given xfeature.
 *
 * Also return the name of the (most advanced) feature that the caller requested:
 */
int cpu_has_xfeatures(u64 xfeatures_needed, const char **feature_name)
{
	u64 xfeatures_missing = xfeatures_needed & ~xfeatures_mask_all;

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

static bool xfeature_is_supervisor(int xfeature_nr)
{
	/*
	 * Extended State Enumeration Sub-leaves (EAX = 0DH, ECX = n, n > 1)
	 * returns ECX[0] set to (1) for a supervisor state, and cleared (0)
	 * for a user state.
	 */
	u32 eax, ebx, ecx, edx;

	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return ecx & 1;
}

/*
 * Enable the extended processor state save/restore feature.
 * Called once per CPU onlining.
 */
void fpu__init_cpu_xstate(void)
{
	if (!boot_cpu_has(X86_FEATURE_XSAVE) || !xfeatures_mask_all)
		return;

	cr4_set_bits(X86_CR4_OSXSAVE);

	/*
	 * XCR_XFEATURE_ENABLED_MASK (aka. XCR0) sets user features
	 * managed by XSAVE{C, OPT, S} and XRSTOR{S}.  Only XSAVE user
	 * states can be set here.
	 */
	xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask_uabi());

	/*
	 * MSR_IA32_XSS sets supervisor states managed by XSAVES.
	 */
	if (boot_cpu_has(X86_FEATURE_XSAVES)) {
		wrmsrl(MSR_IA32_XSS, xfeatures_mask_supervisor() |
				     xfeatures_mask_independent());
	}
}

static bool xfeature_enabled(enum xfeature xfeature)
{
	return xfeatures_mask_all & BIT_ULL(xfeature);
}

/*
 * Record the offsets and sizes of various xstates contained
 * in the XSAVE state memory layout.
 */
static void __init setup_xstate_features(void)
{
	u32 eax, ebx, ecx, edx, i;
	/* start at the beginning of the "extended state" */
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

		xstate_sizes[i] = eax;

		/*
		 * If an xfeature is supervisor state, the offset in EBX is
		 * invalid, leave it to -1.
		 */
		if (xfeature_is_supervisor(i))
			continue;

		xstate_offsets[i] = ebx;

		/*
		 * In our xstate size checks, we assume that the highest-numbered
		 * xstate feature has the highest offset in the buffer.  Ensure
		 * it does.
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
	print_xstate_feature(XFEATURE_MASK_PASID);
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

	if (!xfeature_enabled(xfeature_nr)) {
		WARN_ONCE(1, "Checking alignment of disabled xfeature %d\n",
			  xfeature_nr);
		return 0;
	}

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
 * of the xsave area.
 */
static void __init setup_xstate_comp_offsets(void)
{
	unsigned int next_offset;
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
			if (xfeature_enabled(i))
				xstate_comp_offsets[i] = xstate_offsets[i];
		}
		return;
	}

	next_offset = FXSAVE_SIZE + XSAVE_HDR_SIZE;

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (!xfeature_enabled(i))
			continue;

		if (xfeature_is_aligned(i))
			next_offset = ALIGN(next_offset, 64);

		xstate_comp_offsets[i] = next_offset;
		next_offset += xstate_sizes[i];
	}
}

/*
 * Setup offsets of a supervisor-state-only XSAVES buffer:
 *
 * The offsets stored in xstate_comp_offsets[] only work for one specific
 * value of the Requested Feature BitMap (RFBM).  In cases where a different
 * RFBM value is used, a different set of offsets is required.  This set of
 * offsets is for when RFBM=xfeatures_mask_supervisor().
 */
static void __init setup_supervisor_only_offsets(void)
{
	unsigned int next_offset;
	int i;

	next_offset = FXSAVE_SIZE + XSAVE_HDR_SIZE;

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		if (!xfeature_enabled(i) || !xfeature_is_supervisor(i))
			continue;

		if (xfeature_is_aligned(i))
			next_offset = ALIGN(next_offset, 64);

		xstate_supervisor_only_offsets[i] = next_offset;
		next_offset += xstate_sizes[i];
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
 * All supported features have either init state all zeros or are
 * handled in setup_init_fpu() individually. This is an explicit
 * feature list and does not use XFEATURE_MASK*SUPPORTED to catch
 * newly added supported features at build time and make people
 * actually look at the init state for the new feature.
 */
#define XFEATURES_INIT_FPSTATE_HANDLED		\
	(XFEATURE_MASK_FP |			\
	 XFEATURE_MASK_SSE |			\
	 XFEATURE_MASK_YMM |			\
	 XFEATURE_MASK_OPMASK |			\
	 XFEATURE_MASK_ZMM_Hi256 |		\
	 XFEATURE_MASK_Hi16_ZMM	 |		\
	 XFEATURE_MASK_PKRU |			\
	 XFEATURE_MASK_BNDREGS |		\
	 XFEATURE_MASK_BNDCSR |			\
	 XFEATURE_MASK_PASID)

/*
 * setup the xstate image representing the init state
 */
static void __init setup_init_fpu_buf(void)
{
	static int on_boot_cpu __initdata = 1;

	BUILD_BUG_ON((XFEATURE_MASK_USER_SUPPORTED |
		      XFEATURE_MASK_SUPERVISOR_SUPPORTED) !=
		     XFEATURES_INIT_FPSTATE_HANDLED);

	WARN_ON_FPU(!on_boot_cpu);
	on_boot_cpu = 0;

	if (!boot_cpu_has(X86_FEATURE_XSAVE))
		return;

	setup_xstate_features();
	print_xstate_features();

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		init_fpstate.xsave.header.xcomp_bv = XCOMP_BV_COMPACTED_FORMAT |
						     xfeatures_mask_all;

	/*
	 * Init all the features state with header.xfeatures being 0x0
	 */
	os_xrstor_booting(&init_fpstate.xsave);

	/*
	 * All components are now in init state. Read the state back so
	 * that init_fpstate contains all non-zero init state. This only
	 * works with XSAVE, but not with XSAVEOPT and XSAVES because
	 * those use the init optimization which skips writing data for
	 * components in init state.
	 *
	 * XSAVE could be used, but that would require to reshuffle the
	 * data when XSAVES is available because XSAVES uses xstate
	 * compaction. But doing so is a pointless exercise because most
	 * components have an all zeros init state except for the legacy
	 * ones (FP and SSE). Those can be saved with FXSAVE into the
	 * legacy area. Adding new features requires to ensure that init
	 * state is all zeroes or if not to add the necessary handling
	 * here.
	 */
	fxsave(&init_fpstate.fxsave);
}

static int xfeature_uncompacted_offset(int xfeature_nr)
{
	u32 eax, ebx, ecx, edx;

	/*
	 * Only XSAVES supports supervisor states and it uses compacted
	 * format. Checking a supervisor state's uncompacted offset is
	 * an error.
	 */
	if (XFEATURE_MASK_SUPERVISOR_ALL & BIT_ULL(xfeature_nr)) {
		WARN_ONCE(1, "No fixed offset for xstate %d\n", xfeature_nr);
		return -1;
	}

	CHECK_XFEATURE(xfeature_nr);
	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return ebx;
}

int xfeature_size(int xfeature_nr)
{
	u32 eax, ebx, ecx, edx;

	CHECK_XFEATURE(xfeature_nr);
	cpuid_count(XSTATE_CPUID, xfeature_nr, &eax, &ebx, &ecx, &edx);
	return eax;
}

/* Validate an xstate header supplied by userspace (ptrace or sigreturn) */
static int validate_user_xstate_header(const struct xstate_header *hdr)
{
	/* No unknown or supervisor features may be set */
	if (hdr->xfeatures & ~xfeatures_mask_uabi())
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
	XCHECK_SZ(sz, nr, XFEATURE_PASID,     struct ia32_pasid_state);

	/*
	 * Make *SURE* to add any feature numbers in below if
	 * there are "holes" in the xsave state component
	 * numbers.
	 */
	if ((nr < XFEATURE_YMM) ||
	    (nr >= XFEATURE_MAX) ||
	    (nr == XFEATURE_PT_UNIMPLEMENTED_SO_FAR) ||
	    ((nr >= XFEATURE_RSRVD_COMP_11) && (nr <= XFEATURE_LBR))) {
		WARN_ONCE(1, "no structure for xstate: %d\n", nr);
		XSTATE_WARN_ON(1);
	}
}

/*
 * This essentially double-checks what the cpu told us about
 * how large the XSAVE buffer needs to be.  We are recalculating
 * it to be safe.
 *
 * Independent XSAVE features allocate their own buffers and are not
 * covered by these checks. Only the size of the buffer for task->fpu
 * is checked here.
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
		 * XSAVES.
		 */
		if (!cpu_feature_enabled(X86_FEATURE_XSAVES))
			XSTATE_WARN_ON(xfeature_is_supervisor(i));

		/* Align from the end of the previous feature */
		if (xfeature_is_aligned(i))
			paranoid_xstate_size = ALIGN(paranoid_xstate_size, 64);
		/*
		 * The offset of a given state in the non-compacted
		 * format is given to us in a CPUID leaf.  We check
		 * them for being ordered (increasing offsets) in
		 * setup_xstate_features(). XSAVES uses compacted format.
		 */
		if (!cpu_feature_enabled(X86_FEATURE_XSAVES))
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
 * Get total size of enabled xstates in XCR0 | IA32_XSS.
 *
 * Note the SDM's wording here.  "sub-function 0" only enumerates
 * the size of the *user* states.  If we use it to size a buffer
 * that we use 'XSAVES' on, we could potentially overflow the
 * buffer because 'XSAVES' saves system states too.
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

/*
 * Get the total size of the enabled xstates without the independent supervisor
 * features.
 */
static unsigned int __init get_xsaves_size_no_independent(void)
{
	u64 mask = xfeatures_mask_independent();
	unsigned int size;

	if (!mask)
		return get_xsaves_size();

	/* Disable independent features. */
	wrmsrl(MSR_IA32_XSS, xfeatures_mask_supervisor());

	/*
	 * Ask the hardware what size is required of the buffer.
	 * This is the size required for the task->fpu buffer.
	 */
	size = get_xsaves_size();

	/* Re-enable independent features so XSAVES will work on them again. */
	wrmsrl(MSR_IA32_XSS, xfeatures_mask_supervisor() | mask);

	return size;
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
		possible_xstate_size = get_xsaves_size_no_independent();
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
	xfeatures_mask_all = 0;
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
	u64 xfeatures;
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

	/*
	 * Find user xstates supported by the processor.
	 */
	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);
	xfeatures_mask_all = eax + ((u64)edx << 32);

	/*
	 * Find supervisor xstates supported by the processor.
	 */
	cpuid_count(XSTATE_CPUID, 1, &eax, &ebx, &ecx, &edx);
	xfeatures_mask_all |= ecx + ((u64)edx << 32);

	if ((xfeatures_mask_uabi() & XFEATURE_MASK_FPSSE) != XFEATURE_MASK_FPSSE) {
		/*
		 * This indicates that something really unexpected happened
		 * with the enumeration.  Disable XSAVE and try to continue
		 * booting without it.  This is too early to BUG().
		 */
		pr_err("x86/fpu: FP/SSE not present amongst the CPU's xstate features: 0x%llx.\n",
		       xfeatures_mask_all);
		goto out_disable;
	}

	/*
	 * Clear XSAVE features that are disabled in the normal CPUID.
	 */
	for (i = 0; i < ARRAY_SIZE(xsave_cpuid_features); i++) {
		if (!boot_cpu_has(xsave_cpuid_features[i]))
			xfeatures_mask_all &= ~BIT_ULL(i);
	}

	xfeatures_mask_all &= XFEATURE_MASK_USER_SUPPORTED |
			      XFEATURE_MASK_SUPERVISOR_SUPPORTED;

	/* Store it for paranoia check at the end */
	xfeatures = xfeatures_mask_all;

	/* Enable xstate instructions to be able to continue with initialization: */
	fpu__init_cpu_xstate();
	err = init_xstate_size();
	if (err)
		goto out_disable;

	/*
	 * Update info used for ptrace frames; use standard-format size and no
	 * supervisor xstates:
	 */
	update_regset_xstate_info(fpu_user_xstate_size, xfeatures_mask_uabi());

	fpu__init_prepare_fx_sw_frame();
	setup_init_fpu_buf();
	setup_xstate_comp_offsets();
	setup_supervisor_only_offsets();

	/*
	 * Paranoia check whether something in the setup modified the
	 * xfeatures mask.
	 */
	if (xfeatures != xfeatures_mask_all) {
		pr_err("x86/fpu: xfeatures modified from 0x%016llx to 0x%016llx during init, disabling XSAVE\n",
		       xfeatures, xfeatures_mask_all);
		goto out_disable;
	}

	print_xstate_offset_size();
	pr_info("x86/fpu: Enabled xstate features 0x%llx, context size is %d bytes, using '%s' format.\n",
		xfeatures_mask_all,
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
	if (cpu_feature_enabled(X86_FEATURE_XSAVE))
		xsetbv(XCR_XFEATURE_ENABLED_MASK, xfeatures_mask_uabi());

	/*
	 * Restore IA32_XSS. The same CPUID bit enumerates support
	 * of XSAVES and MSR_IA32_XSS.
	 */
	if (cpu_feature_enabled(X86_FEATURE_XSAVES)) {
		wrmsrl(MSR_IA32_XSS, xfeatures_mask_supervisor()  |
				     xfeatures_mask_independent());
	}
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
	 * have not enabled.
	 */
	WARN_ONCE(!(xfeatures_mask_all & BIT_ULL(xfeature_nr)),
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

#ifdef CONFIG_ARCH_HAS_PKEYS

/*
 * This will go out and modify PKRU register to set the access
 * rights for @pkey to @init_val.
 */
int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
			      unsigned long init_val)
{
	u32 old_pkru, new_pkru_bits = 0;
	int pkey_shift;

	/*
	 * This check implies XSAVE support.  OSPKE only gets
	 * set if we enable XSAVE and we enable PKU in XCR0.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return -EINVAL;

	/*
	 * This code should only be called with valid 'pkey'
	 * values originating from in-kernel users.  Complain
	 * if a bad value is observed.
	 */
	if (WARN_ON_ONCE(pkey >= arch_max_pkey()))
		return -EINVAL;

	/* Set the bits we need in PKRU:  */
	if (init_val & PKEY_DISABLE_ACCESS)
		new_pkru_bits |= PKRU_AD_BIT;
	if (init_val & PKEY_DISABLE_WRITE)
		new_pkru_bits |= PKRU_WD_BIT;

	/* Shift the bits in to the correct place in PKRU for pkey: */
	pkey_shift = pkey * PKRU_BITS_PER_PKEY;
	new_pkru_bits <<= pkey_shift;

	/* Get old PKRU and mask off any old bits in place: */
	old_pkru = read_pkru();
	old_pkru &= ~((PKRU_AD_BIT|PKRU_WD_BIT) << pkey_shift);

	/* Write old part along with new part: */
	write_pkru(old_pkru | new_pkru_bits);

	return 0;
}
#endif /* ! CONFIG_ARCH_HAS_PKEYS */

static void copy_feature(bool from_xstate, struct membuf *to, void *xstate,
			 void *init_xstate, unsigned int size)
{
	membuf_write(to, from_xstate ? xstate : init_xstate, size);
}

/**
 * copy_xstate_to_uabi_buf - Copy kernel saved xstate to a UABI buffer
 * @to:		membuf descriptor
 * @tsk:	The task from which to copy the saved xstate
 * @copy_mode:	The requested copy mode
 *
 * Converts from kernel XSAVE or XSAVES compacted format to UABI conforming
 * format, i.e. from the kernel internal hardware dependent storage format
 * to the requested @mode. UABI XSTATE is always uncompacted!
 *
 * It supports partial copy but @to.pos always starts from zero.
 */
void copy_xstate_to_uabi_buf(struct membuf to, struct task_struct *tsk,
			     enum xstate_copy_mode copy_mode)
{
	const unsigned int off_mxcsr = offsetof(struct fxregs_state, mxcsr);
	struct xregs_state *xsave = &tsk->thread.fpu.state.xsave;
	struct xregs_state *xinit = &init_fpstate.xsave;
	struct xstate_header header;
	unsigned int zerofrom;
	int i;

	memset(&header, 0, sizeof(header));
	header.xfeatures = xsave->header.xfeatures;

	/* Mask out the feature bits depending on copy mode */
	switch (copy_mode) {
	case XSTATE_COPY_FP:
		header.xfeatures &= XFEATURE_MASK_FP;
		break;

	case XSTATE_COPY_FX:
		header.xfeatures &= XFEATURE_MASK_FP | XFEATURE_MASK_SSE;
		break;

	case XSTATE_COPY_XSAVE:
		header.xfeatures &= xfeatures_mask_uabi();
		break;
	}

	/* Copy FP state up to MXCSR */
	copy_feature(header.xfeatures & XFEATURE_MASK_FP, &to, &xsave->i387,
		     &xinit->i387, off_mxcsr);

	/* Copy MXCSR when SSE or YMM are set in the feature mask */
	copy_feature(header.xfeatures & (XFEATURE_MASK_SSE | XFEATURE_MASK_YMM),
		     &to, &xsave->i387.mxcsr, &xinit->i387.mxcsr,
		     MXCSR_AND_FLAGS_SIZE);

	/* Copy the remaining FP state */
	copy_feature(header.xfeatures & XFEATURE_MASK_FP,
		     &to, &xsave->i387.st_space, &xinit->i387.st_space,
		     sizeof(xsave->i387.st_space));

	/* Copy the SSE state - shared with YMM, but independently managed */
	copy_feature(header.xfeatures & XFEATURE_MASK_SSE,
		     &to, &xsave->i387.xmm_space, &xinit->i387.xmm_space,
		     sizeof(xsave->i387.xmm_space));

	if (copy_mode != XSTATE_COPY_XSAVE)
		goto out;

	/* Zero the padding area */
	membuf_zero(&to, sizeof(xsave->i387.padding));

	/* Copy xsave->i387.sw_reserved */
	membuf_write(&to, xstate_fx_sw_bytes, sizeof(xsave->i387.sw_reserved));

	/* Copy the user space relevant state of @xsave->header */
	membuf_write(&to, &header, sizeof(header));

	zerofrom = offsetof(struct xregs_state, extended_state_area);

	for (i = FIRST_EXTENDED_XFEATURE; i < XFEATURE_MAX; i++) {
		/*
		 * The ptrace buffer is in non-compacted XSAVE format.
		 * In non-compacted format disabled features still occupy
		 * state space, but there is no state to copy from in the
		 * compacted init_fpstate. The gap tracking will zero this
		 * later.
		 */
		if (!(xfeatures_mask_uabi() & BIT_ULL(i)))
			continue;

		/*
		 * If there was a feature or alignment gap, zero the space
		 * in the destination buffer.
		 */
		if (zerofrom < xstate_offsets[i])
			membuf_zero(&to, xstate_offsets[i] - zerofrom);

		if (i == XFEATURE_PKRU) {
			struct pkru_state pkru = {0};
			/*
			 * PKRU is not necessarily up to date in the
			 * thread's XSAVE buffer.  Fill this part from the
			 * per-thread storage.
			 */
			pkru.pkru = tsk->thread.pkru;
			membuf_write(&to, &pkru, sizeof(pkru));
		} else {
			copy_feature(header.xfeatures & BIT_ULL(i), &to,
				     __raw_xsave_addr(xsave, i),
				     __raw_xsave_addr(xinit, i),
				     xstate_sizes[i]);
		}
		/*
		 * Keep track of the last copied state in the non-compacted
		 * target buffer for gap zeroing.
		 */
		zerofrom = xstate_offsets[i] + xstate_sizes[i];
	}

out:
	if (to.left)
		membuf_zero(&to, to.left);
}

static int copy_from_buffer(void *dst, unsigned int offset, unsigned int size,
			    const void *kbuf, const void __user *ubuf)
{
	if (kbuf) {
		memcpy(dst, kbuf + offset, size);
	} else {
		if (copy_from_user(dst, ubuf + offset, size))
			return -EFAULT;
	}
	return 0;
}


static int copy_uabi_to_xstate(struct xregs_state *xsave, const void *kbuf,
			       const void __user *ubuf)
{
	unsigned int offset, size;
	struct xstate_header hdr;
	u64 mask;
	int i;

	offset = offsetof(struct xregs_state, header);
	if (copy_from_buffer(&hdr, offset, sizeof(hdr), kbuf, ubuf))
		return -EFAULT;

	if (validate_user_xstate_header(&hdr))
		return -EINVAL;

	/* Validate MXCSR when any of the related features is in use */
	mask = XFEATURE_MASK_FP | XFEATURE_MASK_SSE | XFEATURE_MASK_YMM;
	if (hdr.xfeatures & mask) {
		u32 mxcsr[2];

		offset = offsetof(struct fxregs_state, mxcsr);
		if (copy_from_buffer(mxcsr, offset, sizeof(mxcsr), kbuf, ubuf))
			return -EFAULT;

		/* Reserved bits in MXCSR must be zero. */
		if (mxcsr[0] & ~mxcsr_feature_mask)
			return -EINVAL;

		/* SSE and YMM require MXCSR even when FP is not in use. */
		if (!(hdr.xfeatures & XFEATURE_MASK_FP)) {
			xsave->i387.mxcsr = mxcsr[0];
			xsave->i387.mxcsr_mask = mxcsr[1];
		}
	}

	for (i = 0; i < XFEATURE_MAX; i++) {
		u64 mask = ((u64)1 << i);

		if (hdr.xfeatures & mask) {
			void *dst = __raw_xsave_addr(xsave, i);

			offset = xstate_offsets[i];
			size = xstate_sizes[i];

			if (copy_from_buffer(dst, offset, size, kbuf, ubuf))
				return -EFAULT;
		}
	}

	/*
	 * The state that came in from userspace was user-state only.
	 * Mask all the user states out of 'xfeatures':
	 */
	xsave->header.xfeatures &= XFEATURE_MASK_SUPERVISOR_ALL;

	/*
	 * Add back in the features that came in from userspace:
	 */
	xsave->header.xfeatures |= hdr.xfeatures;

	return 0;
}

/*
 * Convert from a ptrace standard-format kernel buffer to kernel XSAVE[S]
 * format and copy to the target thread. This is called from
 * xstateregs_set().
 */
int copy_uabi_from_kernel_to_xstate(struct xregs_state *xsave, const void *kbuf)
{
	return copy_uabi_to_xstate(xsave, kbuf, NULL);
}

/*
 * Convert from a sigreturn standard-format user-space buffer to kernel
 * XSAVE[S] format and copy to the target thread. This is called from the
 * sigreturn() and rt_sigreturn() system calls.
 */
int copy_sigframe_from_user_to_xstate(struct xregs_state *xsave,
				      const void __user *ubuf)
{
	return copy_uabi_to_xstate(xsave, NULL, ubuf);
}

static bool validate_xsaves_xrstors(u64 mask)
{
	u64 xchk;

	if (WARN_ON_FPU(!cpu_feature_enabled(X86_FEATURE_XSAVES)))
		return false;
	/*
	 * Validate that this is either a task->fpstate related component
	 * subset or an independent one.
	 */
	if (mask & xfeatures_mask_independent())
		xchk = ~xfeatures_mask_independent();
	else
		xchk = ~xfeatures_mask_all;

	if (WARN_ON_ONCE(!mask || mask & xchk))
		return false;

	return true;
}

/**
 * xsaves - Save selected components to a kernel xstate buffer
 * @xstate:	Pointer to the buffer
 * @mask:	Feature mask to select the components to save
 *
 * The @xstate buffer must be 64 byte aligned and correctly initialized as
 * XSAVES does not write the full xstate header. Before first use the
 * buffer should be zeroed otherwise a consecutive XRSTORS from that buffer
 * can #GP.
 *
 * The feature mask must either be a subset of the independent features or
 * a subset of the task->fpstate related features.
 */
void xsaves(struct xregs_state *xstate, u64 mask)
{
	int err;

	if (!validate_xsaves_xrstors(mask))
		return;

	XSTATE_OP(XSAVES, xstate, (u32)mask, (u32)(mask >> 32), err);
	WARN_ON_ONCE(err);
}

/**
 * xrstors - Restore selected components from a kernel xstate buffer
 * @xstate:	Pointer to the buffer
 * @mask:	Feature mask to select the components to restore
 *
 * The @xstate buffer must be 64 byte aligned and correctly initialized
 * otherwise XRSTORS from that buffer can #GP.
 *
 * Proper usage is to restore the state which was saved with
 * xsaves() into @xstate.
 *
 * The feature mask must either be a subset of the independent features or
 * a subset of the task->fpstate related features.
 */
void xrstors(struct xregs_state *xstate, u64 mask)
{
	int err;

	if (!validate_xsaves_xrstors(mask))
		return;

	XSTATE_OP(XRSTORS, xstate, (u32)mask, (u32)(mask >> 32), err);
	WARN_ON_ONCE(err);
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
