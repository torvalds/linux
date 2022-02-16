// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  Cyrix stuff, June 1998 by:
 *	- Rafael R. Reilova (moved everything from head.S),
 *        <rreilova@ececs.uc.edu>
 *	- Channing Corn (tests & fixes),
 *	- Andrew D. Balsa (code cleanup).
 */
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/prctl.h>
#include <linux/sched/smt.h>
#include <linux/pgtable.h>

#include <asm/spec-ctrl.h>
#include <asm/cmdline.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>
#include <asm/fpu/internal.h>
#include <asm/msr.h>
#include <asm/vmx.h>
#include <asm/paravirt.h>
#include <asm/alternative.h>
#include <asm/set_memory.h>
#include <asm/intel-family.h>
#include <asm/e820/api.h>
#include <asm/hypervisor.h>
#include <asm/tlbflush.h>

#include "cpu.h"

static void __init spectre_v1_select_mitigation(void);
static void __init spectre_v2_select_mitigation(void);
static void __init ssb_select_mitigation(void);
static void __init l1tf_select_mitigation(void);
static void __init mds_select_mitigation(void);
static void __init mds_print_mitigation(void);
static void __init taa_select_mitigation(void);
static void __init srbds_select_mitigation(void);

/* The base value of the SPEC_CTRL MSR that always has to be preserved. */
u64 x86_spec_ctrl_base;
EXPORT_SYMBOL_GPL(x86_spec_ctrl_base);
static DEFINE_MUTEX(spec_ctrl_mutex);

/*
 * The vendor and possibly platform specific bits which can be modified in
 * x86_spec_ctrl_base.
 */
static u64 __ro_after_init x86_spec_ctrl_mask = SPEC_CTRL_IBRS;

/*
 * AMD specific MSR info for Speculative Store Bypass control.
 * x86_amd_ls_cfg_ssbd_mask is initialized in identify_boot_cpu().
 */
u64 __ro_after_init x86_amd_ls_cfg_base;
u64 __ro_after_init x86_amd_ls_cfg_ssbd_mask;

/* Control conditional STIBP in switch_to() */
DEFINE_STATIC_KEY_FALSE(switch_to_cond_stibp);
/* Control conditional IBPB in switch_mm() */
DEFINE_STATIC_KEY_FALSE(switch_mm_cond_ibpb);
/* Control unconditional IBPB in switch_mm() */
DEFINE_STATIC_KEY_FALSE(switch_mm_always_ibpb);

/* Control MDS CPU buffer clear before returning to user space */
DEFINE_STATIC_KEY_FALSE(mds_user_clear);
EXPORT_SYMBOL_GPL(mds_user_clear);
/* Control MDS CPU buffer clear before idling (halt, mwait) */
DEFINE_STATIC_KEY_FALSE(mds_idle_clear);
EXPORT_SYMBOL_GPL(mds_idle_clear);

void __init check_bugs(void)
{
	identify_boot_cpu();

	/*
	 * identify_boot_cpu() initialized SMT support information, let the
	 * core code know.
	 */
	cpu_smt_check_topology();

	if (!IS_ENABLED(CONFIG_SMP)) {
		pr_info("CPU: ");
		print_cpu_info(&boot_cpu_data);
	}

	/*
	 * Read the SPEC_CTRL MSR to account for reserved bits which may
	 * have unknown values. AMD64_LS_CFG MSR is cached in the early AMD
	 * init code as it is not enumerated and depends on the family.
	 */
	if (boot_cpu_has(X86_FEATURE_MSR_SPEC_CTRL))
		rdmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);

	/* Allow STIBP in MSR_SPEC_CTRL if supported */
	if (boot_cpu_has(X86_FEATURE_STIBP))
		x86_spec_ctrl_mask |= SPEC_CTRL_STIBP;

	/* Select the proper CPU mitigations before patching alternatives: */
	spectre_v1_select_mitigation();
	spectre_v2_select_mitigation();
	ssb_select_mitigation();
	l1tf_select_mitigation();
	mds_select_mitigation();
	taa_select_mitigation();
	srbds_select_mitigation();

	/*
	 * As MDS and TAA mitigations are inter-related, print MDS
	 * mitigation until after TAA mitigation selection is done.
	 */
	mds_print_mitigation();

	arch_smt_update();

#ifdef CONFIG_X86_32
	/*
	 * Check whether we are able to run this kernel safely on SMP.
	 *
	 * - i386 is no longer supported.
	 * - In order to run on anything without a TSC, we need to be
	 *   compiled for a i486.
	 */
	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);
	alternative_instructions();

	fpu__init_check_bugs();
#else /* CONFIG_X86_64 */
	alternative_instructions();

	/*
	 * Make sure the first 2MB area is not mapped by huge pages
	 * There are typically fixed size MTRRs in there and overlapping
	 * MTRRs into large pages causes slow downs.
	 *
	 * Right now we don't do that with gbpages because there seems
	 * very little benefit for that case.
	 */
	if (!direct_gbpages)
		set_memory_4k((unsigned long)__va(0), 1);
#endif
}

void
x86_virt_spec_ctrl(u64 guest_spec_ctrl, u64 guest_virt_spec_ctrl, bool setguest)
{
	u64 msrval, guestval, hostval = x86_spec_ctrl_base;
	struct thread_info *ti = current_thread_info();

	/* Is MSR_SPEC_CTRL implemented ? */
	if (static_cpu_has(X86_FEATURE_MSR_SPEC_CTRL)) {
		/*
		 * Restrict guest_spec_ctrl to supported values. Clear the
		 * modifiable bits in the host base value and or the
		 * modifiable bits from the guest value.
		 */
		guestval = hostval & ~x86_spec_ctrl_mask;
		guestval |= guest_spec_ctrl & x86_spec_ctrl_mask;

		/* SSBD controlled in MSR_SPEC_CTRL */
		if (static_cpu_has(X86_FEATURE_SPEC_CTRL_SSBD) ||
		    static_cpu_has(X86_FEATURE_AMD_SSBD))
			hostval |= ssbd_tif_to_spec_ctrl(ti->flags);

		/* Conditional STIBP enabled? */
		if (static_branch_unlikely(&switch_to_cond_stibp))
			hostval |= stibp_tif_to_spec_ctrl(ti->flags);

		if (hostval != guestval) {
			msrval = setguest ? guestval : hostval;
			wrmsrl(MSR_IA32_SPEC_CTRL, msrval);
		}
	}

	/*
	 * If SSBD is not handled in MSR_SPEC_CTRL on AMD, update
	 * MSR_AMD64_L2_CFG or MSR_VIRT_SPEC_CTRL if supported.
	 */
	if (!static_cpu_has(X86_FEATURE_LS_CFG_SSBD) &&
	    !static_cpu_has(X86_FEATURE_VIRT_SSBD))
		return;

	/*
	 * If the host has SSBD mitigation enabled, force it in the host's
	 * virtual MSR value. If its not permanently enabled, evaluate
	 * current's TIF_SSBD thread flag.
	 */
	if (static_cpu_has(X86_FEATURE_SPEC_STORE_BYPASS_DISABLE))
		hostval = SPEC_CTRL_SSBD;
	else
		hostval = ssbd_tif_to_spec_ctrl(ti->flags);

	/* Sanitize the guest value */
	guestval = guest_virt_spec_ctrl & SPEC_CTRL_SSBD;

	if (hostval != guestval) {
		unsigned long tif;

		tif = setguest ? ssbd_spec_ctrl_to_tif(guestval) :
				 ssbd_spec_ctrl_to_tif(hostval);

		speculation_ctrl_update(tif);
	}
}
EXPORT_SYMBOL_GPL(x86_virt_spec_ctrl);

static void x86_amd_ssb_disable(void)
{
	u64 msrval = x86_amd_ls_cfg_base | x86_amd_ls_cfg_ssbd_mask;

	if (boot_cpu_has(X86_FEATURE_VIRT_SSBD))
		wrmsrl(MSR_AMD64_VIRT_SPEC_CTRL, SPEC_CTRL_SSBD);
	else if (boot_cpu_has(X86_FEATURE_LS_CFG_SSBD))
		wrmsrl(MSR_AMD64_LS_CFG, msrval);
}

#undef pr_fmt
#define pr_fmt(fmt)	"MDS: " fmt

/* Default mitigation for MDS-affected CPUs */
static enum mds_mitigations mds_mitigation __ro_after_init = MDS_MITIGATION_FULL;
static bool mds_nosmt __ro_after_init = false;

static const char * const mds_strings[] = {
	[MDS_MITIGATION_OFF]	= "Vulnerable",
	[MDS_MITIGATION_FULL]	= "Mitigation: Clear CPU buffers",
	[MDS_MITIGATION_VMWERV]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
};

static void __init mds_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MDS) || cpu_mitigations_off()) {
		mds_mitigation = MDS_MITIGATION_OFF;
		return;
	}

	if (mds_mitigation == MDS_MITIGATION_FULL) {
		if (!boot_cpu_has(X86_FEATURE_MD_CLEAR))
			mds_mitigation = MDS_MITIGATION_VMWERV;

		static_branch_enable(&mds_user_clear);

		if (!boot_cpu_has(X86_BUG_MSBDS_ONLY) &&
		    (mds_nosmt || cpu_mitigations_auto_nosmt()))
			cpu_smt_disable(false);
	}
}

static void __init mds_print_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MDS) || cpu_mitigations_off())
		return;

	pr_info("%s\n", mds_strings[mds_mitigation]);
}

static int __init mds_cmdline(char *str)
{
	if (!boot_cpu_has_bug(X86_BUG_MDS))
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		mds_mitigation = MDS_MITIGATION_OFF;
	else if (!strcmp(str, "full"))
		mds_mitigation = MDS_MITIGATION_FULL;
	else if (!strcmp(str, "full,nosmt")) {
		mds_mitigation = MDS_MITIGATION_FULL;
		mds_nosmt = true;
	}

	return 0;
}
early_param("mds", mds_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"TAA: " fmt

enum taa_mitigations {
	TAA_MITIGATION_OFF,
	TAA_MITIGATION_UCODE_NEEDED,
	TAA_MITIGATION_VERW,
	TAA_MITIGATION_TSX_DISABLED,
};

/* Default mitigation for TAA-affected CPUs */
static enum taa_mitigations taa_mitigation __ro_after_init = TAA_MITIGATION_VERW;
static bool taa_nosmt __ro_after_init;

static const char * const taa_strings[] = {
	[TAA_MITIGATION_OFF]		= "Vulnerable",
	[TAA_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[TAA_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
	[TAA_MITIGATION_TSX_DISABLED]	= "Mitigation: TSX disabled",
};

static void __init taa_select_mitigation(void)
{
	u64 ia32_cap;

	if (!boot_cpu_has_bug(X86_BUG_TAA)) {
		taa_mitigation = TAA_MITIGATION_OFF;
		return;
	}

	/* TSX previously disabled by tsx=off */
	if (!boot_cpu_has(X86_FEATURE_RTM)) {
		taa_mitigation = TAA_MITIGATION_TSX_DISABLED;
		goto out;
	}

	if (cpu_mitigations_off()) {
		taa_mitigation = TAA_MITIGATION_OFF;
		return;
	}

	/*
	 * TAA mitigation via VERW is turned off if both
	 * tsx_async_abort=off and mds=off are specified.
	 */
	if (taa_mitigation == TAA_MITIGATION_OFF &&
	    mds_mitigation == MDS_MITIGATION_OFF)
		goto out;

	if (boot_cpu_has(X86_FEATURE_MD_CLEAR))
		taa_mitigation = TAA_MITIGATION_VERW;
	else
		taa_mitigation = TAA_MITIGATION_UCODE_NEEDED;

	/*
	 * VERW doesn't clear the CPU buffers when MD_CLEAR=1 and MDS_NO=1.
	 * A microcode update fixes this behavior to clear CPU buffers. It also
	 * adds support for MSR_IA32_TSX_CTRL which is enumerated by the
	 * ARCH_CAP_TSX_CTRL_MSR bit.
	 *
	 * On MDS_NO=1 CPUs if ARCH_CAP_TSX_CTRL_MSR is not set, microcode
	 * update is required.
	 */
	ia32_cap = x86_read_arch_cap_msr();
	if ( (ia32_cap & ARCH_CAP_MDS_NO) &&
	    !(ia32_cap & ARCH_CAP_TSX_CTRL_MSR))
		taa_mitigation = TAA_MITIGATION_UCODE_NEEDED;

	/*
	 * TSX is enabled, select alternate mitigation for TAA which is
	 * the same as MDS. Enable MDS static branch to clear CPU buffers.
	 *
	 * For guests that can't determine whether the correct microcode is
	 * present on host, enable the mitigation for UCODE_NEEDED as well.
	 */
	static_branch_enable(&mds_user_clear);

	if (taa_nosmt || cpu_mitigations_auto_nosmt())
		cpu_smt_disable(false);

	/*
	 * Update MDS mitigation, if necessary, as the mds_user_clear is
	 * now enabled for TAA mitigation.
	 */
	if (mds_mitigation == MDS_MITIGATION_OFF &&
	    boot_cpu_has_bug(X86_BUG_MDS)) {
		mds_mitigation = MDS_MITIGATION_FULL;
		mds_select_mitigation();
	}
out:
	pr_info("%s\n", taa_strings[taa_mitigation]);
}

static int __init tsx_async_abort_parse_cmdline(char *str)
{
	if (!boot_cpu_has_bug(X86_BUG_TAA))
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off")) {
		taa_mitigation = TAA_MITIGATION_OFF;
	} else if (!strcmp(str, "full")) {
		taa_mitigation = TAA_MITIGATION_VERW;
	} else if (!strcmp(str, "full,nosmt")) {
		taa_mitigation = TAA_MITIGATION_VERW;
		taa_nosmt = true;
	}

	return 0;
}
early_param("tsx_async_abort", tsx_async_abort_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"SRBDS: " fmt

enum srbds_mitigations {
	SRBDS_MITIGATION_OFF,
	SRBDS_MITIGATION_UCODE_NEEDED,
	SRBDS_MITIGATION_FULL,
	SRBDS_MITIGATION_TSX_OFF,
	SRBDS_MITIGATION_HYPERVISOR,
};

static enum srbds_mitigations srbds_mitigation __ro_after_init = SRBDS_MITIGATION_FULL;

static const char * const srbds_strings[] = {
	[SRBDS_MITIGATION_OFF]		= "Vulnerable",
	[SRBDS_MITIGATION_UCODE_NEEDED]	= "Vulnerable: No microcode",
	[SRBDS_MITIGATION_FULL]		= "Mitigation: Microcode",
	[SRBDS_MITIGATION_TSX_OFF]	= "Mitigation: TSX disabled",
	[SRBDS_MITIGATION_HYPERVISOR]	= "Unknown: Dependent on hypervisor status",
};

static bool srbds_off;

void update_srbds_msr(void)
{
	u64 mcu_ctrl;

	if (!boot_cpu_has_bug(X86_BUG_SRBDS))
		return;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return;

	if (srbds_mitigation == SRBDS_MITIGATION_UCODE_NEEDED)
		return;

	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

	switch (srbds_mitigation) {
	case SRBDS_MITIGATION_OFF:
	case SRBDS_MITIGATION_TSX_OFF:
		mcu_ctrl |= RNGDS_MITG_DIS;
		break;
	case SRBDS_MITIGATION_FULL:
		mcu_ctrl &= ~RNGDS_MITG_DIS;
		break;
	default:
		break;
	}

	wrmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
}

static void __init srbds_select_mitigation(void)
{
	u64 ia32_cap;

	if (!boot_cpu_has_bug(X86_BUG_SRBDS))
		return;

	/*
	 * Check to see if this is one of the MDS_NO systems supporting
	 * TSX that are only exposed to SRBDS when TSX is enabled.
	 */
	ia32_cap = x86_read_arch_cap_msr();
	if ((ia32_cap & ARCH_CAP_MDS_NO) && !boot_cpu_has(X86_FEATURE_RTM))
		srbds_mitigation = SRBDS_MITIGATION_TSX_OFF;
	else if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		srbds_mitigation = SRBDS_MITIGATION_HYPERVISOR;
	else if (!boot_cpu_has(X86_FEATURE_SRBDS_CTRL))
		srbds_mitigation = SRBDS_MITIGATION_UCODE_NEEDED;
	else if (cpu_mitigations_off() || srbds_off)
		srbds_mitigation = SRBDS_MITIGATION_OFF;

	update_srbds_msr();
	pr_info("%s\n", srbds_strings[srbds_mitigation]);
}

static int __init srbds_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!boot_cpu_has_bug(X86_BUG_SRBDS))
		return 0;

	srbds_off = !strcmp(str, "off");
	return 0;
}
early_param("srbds", srbds_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V1 : " fmt

enum spectre_v1_mitigation {
	SPECTRE_V1_MITIGATION_NONE,
	SPECTRE_V1_MITIGATION_AUTO,
};

static enum spectre_v1_mitigation spectre_v1_mitigation __ro_after_init =
	SPECTRE_V1_MITIGATION_AUTO;

static const char * const spectre_v1_strings[] = {
	[SPECTRE_V1_MITIGATION_NONE] = "Vulnerable: __user pointer sanitization and usercopy barriers only; no swapgs barriers",
	[SPECTRE_V1_MITIGATION_AUTO] = "Mitigation: usercopy/swapgs barriers and __user pointer sanitization",
};

/*
 * Does SMAP provide full mitigation against speculative kernel access to
 * userspace?
 */
static bool smap_works_speculatively(void)
{
	if (!boot_cpu_has(X86_FEATURE_SMAP))
		return false;

	/*
	 * On CPUs which are vulnerable to Meltdown, SMAP does not
	 * prevent speculative access to user data in the L1 cache.
	 * Consider SMAP to be non-functional as a mitigation on these
	 * CPUs.
	 */
	if (boot_cpu_has(X86_BUG_CPU_MELTDOWN))
		return false;

	return true;
}

static void __init spectre_v1_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V1) || cpu_mitigations_off()) {
		spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;
		return;
	}

	if (spectre_v1_mitigation == SPECTRE_V1_MITIGATION_AUTO) {
		/*
		 * With Spectre v1, a user can speculatively control either
		 * path of a conditional swapgs with a user-controlled GS
		 * value.  The mitigation is to add lfences to both code paths.
		 *
		 * If FSGSBASE is enabled, the user can put a kernel address in
		 * GS, in which case SMAP provides no protection.
		 *
		 * If FSGSBASE is disabled, the user can only put a user space
		 * address in GS.  That makes an attack harder, but still
		 * possible if there's no SMAP protection.
		 */
		if (boot_cpu_has(X86_FEATURE_FSGSBASE) ||
		    !smap_works_speculatively()) {
			/*
			 * Mitigation can be provided from SWAPGS itself or
			 * PTI as the CR3 write in the Meltdown mitigation
			 * is serializing.
			 *
			 * If neither is there, mitigate with an LFENCE to
			 * stop speculation through swapgs.
			 */
			if (boot_cpu_has_bug(X86_BUG_SWAPGS) &&
			    !boot_cpu_has(X86_FEATURE_PTI))
				setup_force_cpu_cap(X86_FEATURE_FENCE_SWAPGS_USER);

			/*
			 * Enable lfences in the kernel entry (non-swapgs)
			 * paths, to prevent user entry from speculatively
			 * skipping swapgs.
			 */
			setup_force_cpu_cap(X86_FEATURE_FENCE_SWAPGS_KERNEL);
		}
	}

	pr_info("%s\n", spectre_v1_strings[spectre_v1_mitigation]);
}

static int __init nospectre_v1_cmdline(char *str)
{
	spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;
	return 0;
}
early_param("nospectre_v1", nospectre_v1_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V2 : " fmt

static enum spectre_v2_mitigation spectre_v2_enabled __ro_after_init =
	SPECTRE_V2_NONE;

static enum spectre_v2_user_mitigation spectre_v2_user_stibp __ro_after_init =
	SPECTRE_V2_USER_NONE;
static enum spectre_v2_user_mitigation spectre_v2_user_ibpb __ro_after_init =
	SPECTRE_V2_USER_NONE;

#ifdef CONFIG_RETPOLINE
static bool spectre_v2_bad_module;

bool retpoline_module_ok(bool has_retpoline)
{
	if (spectre_v2_enabled == SPECTRE_V2_NONE || has_retpoline)
		return true;

	pr_err("System may be vulnerable to spectre v2\n");
	spectre_v2_bad_module = true;
	return false;
}

static inline const char *spectre_v2_module_string(void)
{
	return spectre_v2_bad_module ? " - vulnerable module loaded" : "";
}
#else
static inline const char *spectre_v2_module_string(void) { return ""; }
#endif

static inline bool match_option(const char *arg, int arglen, const char *opt)
{
	int len = strlen(opt);

	return len == arglen && !strncmp(arg, opt, len);
}

/* The kernel command line selection for spectre v2 */
enum spectre_v2_mitigation_cmd {
	SPECTRE_V2_CMD_NONE,
	SPECTRE_V2_CMD_AUTO,
	SPECTRE_V2_CMD_FORCE,
	SPECTRE_V2_CMD_RETPOLINE,
	SPECTRE_V2_CMD_RETPOLINE_GENERIC,
	SPECTRE_V2_CMD_RETPOLINE_LFENCE,
};

enum spectre_v2_user_cmd {
	SPECTRE_V2_USER_CMD_NONE,
	SPECTRE_V2_USER_CMD_AUTO,
	SPECTRE_V2_USER_CMD_FORCE,
	SPECTRE_V2_USER_CMD_PRCTL,
	SPECTRE_V2_USER_CMD_PRCTL_IBPB,
	SPECTRE_V2_USER_CMD_SECCOMP,
	SPECTRE_V2_USER_CMD_SECCOMP_IBPB,
};

static const char * const spectre_v2_user_strings[] = {
	[SPECTRE_V2_USER_NONE]			= "User space: Vulnerable",
	[SPECTRE_V2_USER_STRICT]		= "User space: Mitigation: STIBP protection",
	[SPECTRE_V2_USER_STRICT_PREFERRED]	= "User space: Mitigation: STIBP always-on protection",
	[SPECTRE_V2_USER_PRCTL]			= "User space: Mitigation: STIBP via prctl",
	[SPECTRE_V2_USER_SECCOMP]		= "User space: Mitigation: STIBP via seccomp and prctl",
};

static const struct {
	const char			*option;
	enum spectre_v2_user_cmd	cmd;
	bool				secure;
} v2_user_options[] __initconst = {
	{ "auto",		SPECTRE_V2_USER_CMD_AUTO,		false },
	{ "off",		SPECTRE_V2_USER_CMD_NONE,		false },
	{ "on",			SPECTRE_V2_USER_CMD_FORCE,		true  },
	{ "prctl",		SPECTRE_V2_USER_CMD_PRCTL,		false },
	{ "prctl,ibpb",		SPECTRE_V2_USER_CMD_PRCTL_IBPB,		false },
	{ "seccomp",		SPECTRE_V2_USER_CMD_SECCOMP,		false },
	{ "seccomp,ibpb",	SPECTRE_V2_USER_CMD_SECCOMP_IBPB,	false },
};

static void __init spec_v2_user_print_cond(const char *reason, bool secure)
{
	if (boot_cpu_has_bug(X86_BUG_SPECTRE_V2) != secure)
		pr_info("spectre_v2_user=%s forced on command line.\n", reason);
}

static enum spectre_v2_user_cmd __init
spectre_v2_parse_user_cmdline(enum spectre_v2_mitigation_cmd v2_cmd)
{
	char arg[20];
	int ret, i;

	switch (v2_cmd) {
	case SPECTRE_V2_CMD_NONE:
		return SPECTRE_V2_USER_CMD_NONE;
	case SPECTRE_V2_CMD_FORCE:
		return SPECTRE_V2_USER_CMD_FORCE;
	default:
		break;
	}

	ret = cmdline_find_option(boot_command_line, "spectre_v2_user",
				  arg, sizeof(arg));
	if (ret < 0)
		return SPECTRE_V2_USER_CMD_AUTO;

	for (i = 0; i < ARRAY_SIZE(v2_user_options); i++) {
		if (match_option(arg, ret, v2_user_options[i].option)) {
			spec_v2_user_print_cond(v2_user_options[i].option,
						v2_user_options[i].secure);
			return v2_user_options[i].cmd;
		}
	}

	pr_err("Unknown user space protection option (%s). Switching to AUTO select\n", arg);
	return SPECTRE_V2_USER_CMD_AUTO;
}

static void __init
spectre_v2_user_select_mitigation(enum spectre_v2_mitigation_cmd v2_cmd)
{
	enum spectre_v2_user_mitigation mode = SPECTRE_V2_USER_NONE;
	bool smt_possible = IS_ENABLED(CONFIG_SMP);
	enum spectre_v2_user_cmd cmd;

	if (!boot_cpu_has(X86_FEATURE_IBPB) && !boot_cpu_has(X86_FEATURE_STIBP))
		return;

	if (cpu_smt_control == CPU_SMT_FORCE_DISABLED ||
	    cpu_smt_control == CPU_SMT_NOT_SUPPORTED)
		smt_possible = false;

	cmd = spectre_v2_parse_user_cmdline(v2_cmd);
	switch (cmd) {
	case SPECTRE_V2_USER_CMD_NONE:
		goto set_mode;
	case SPECTRE_V2_USER_CMD_FORCE:
		mode = SPECTRE_V2_USER_STRICT;
		break;
	case SPECTRE_V2_USER_CMD_PRCTL:
	case SPECTRE_V2_USER_CMD_PRCTL_IBPB:
		mode = SPECTRE_V2_USER_PRCTL;
		break;
	case SPECTRE_V2_USER_CMD_AUTO:
	case SPECTRE_V2_USER_CMD_SECCOMP:
	case SPECTRE_V2_USER_CMD_SECCOMP_IBPB:
		if (IS_ENABLED(CONFIG_SECCOMP))
			mode = SPECTRE_V2_USER_SECCOMP;
		else
			mode = SPECTRE_V2_USER_PRCTL;
		break;
	}

	/* Initialize Indirect Branch Prediction Barrier */
	if (boot_cpu_has(X86_FEATURE_IBPB)) {
		setup_force_cpu_cap(X86_FEATURE_USE_IBPB);

		spectre_v2_user_ibpb = mode;
		switch (cmd) {
		case SPECTRE_V2_USER_CMD_FORCE:
		case SPECTRE_V2_USER_CMD_PRCTL_IBPB:
		case SPECTRE_V2_USER_CMD_SECCOMP_IBPB:
			static_branch_enable(&switch_mm_always_ibpb);
			spectre_v2_user_ibpb = SPECTRE_V2_USER_STRICT;
			break;
		case SPECTRE_V2_USER_CMD_PRCTL:
		case SPECTRE_V2_USER_CMD_AUTO:
		case SPECTRE_V2_USER_CMD_SECCOMP:
			static_branch_enable(&switch_mm_cond_ibpb);
			break;
		default:
			break;
		}

		pr_info("mitigation: Enabling %s Indirect Branch Prediction Barrier\n",
			static_key_enabled(&switch_mm_always_ibpb) ?
			"always-on" : "conditional");
	}

	/*
	 * If no STIBP, enhanced IBRS is enabled or SMT impossible, STIBP is not
	 * required.
	 */
	if (!boot_cpu_has(X86_FEATURE_STIBP) ||
	    !smt_possible ||
	    spectre_v2_enabled == SPECTRE_V2_IBRS_ENHANCED)
		return;

	/*
	 * At this point, an STIBP mode other than "off" has been set.
	 * If STIBP support is not being forced, check if STIBP always-on
	 * is preferred.
	 */
	if (mode != SPECTRE_V2_USER_STRICT &&
	    boot_cpu_has(X86_FEATURE_AMD_STIBP_ALWAYS_ON))
		mode = SPECTRE_V2_USER_STRICT_PREFERRED;

	spectre_v2_user_stibp = mode;

set_mode:
	pr_info("%s\n", spectre_v2_user_strings[mode]);
}

static const char * const spectre_v2_strings[] = {
	[SPECTRE_V2_NONE]			= "Vulnerable",
	[SPECTRE_V2_RETPOLINE]			= "Mitigation: Retpolines",
	[SPECTRE_V2_LFENCE]			= "Mitigation: LFENCE",
	[SPECTRE_V2_IBRS_ENHANCED]		= "Mitigation: Enhanced IBRS",
};

static const struct {
	const char *option;
	enum spectre_v2_mitigation_cmd cmd;
	bool secure;
} mitigation_options[] __initconst = {
	{ "off",		SPECTRE_V2_CMD_NONE,		  false },
	{ "on",			SPECTRE_V2_CMD_FORCE,		  true  },
	{ "retpoline",		SPECTRE_V2_CMD_RETPOLINE,	  false },
	{ "retpoline,amd",	SPECTRE_V2_CMD_RETPOLINE_LFENCE,  false },
	{ "retpoline,lfence",	SPECTRE_V2_CMD_RETPOLINE_LFENCE,  false },
	{ "retpoline,generic",	SPECTRE_V2_CMD_RETPOLINE_GENERIC, false },
	{ "auto",		SPECTRE_V2_CMD_AUTO,		  false },
};

static void __init spec_v2_print_cond(const char *reason, bool secure)
{
	if (boot_cpu_has_bug(X86_BUG_SPECTRE_V2) != secure)
		pr_info("%s selected on command line.\n", reason);
}

static enum spectre_v2_mitigation_cmd __init spectre_v2_parse_cmdline(void)
{
	enum spectre_v2_mitigation_cmd cmd = SPECTRE_V2_CMD_AUTO;
	char arg[20];
	int ret, i;

	if (cmdline_find_option_bool(boot_command_line, "nospectre_v2") ||
	    cpu_mitigations_off())
		return SPECTRE_V2_CMD_NONE;

	ret = cmdline_find_option(boot_command_line, "spectre_v2", arg, sizeof(arg));
	if (ret < 0)
		return SPECTRE_V2_CMD_AUTO;

	for (i = 0; i < ARRAY_SIZE(mitigation_options); i++) {
		if (!match_option(arg, ret, mitigation_options[i].option))
			continue;
		cmd = mitigation_options[i].cmd;
		break;
	}

	if (i >= ARRAY_SIZE(mitigation_options)) {
		pr_err("unknown option (%s). Switching to AUTO select\n", arg);
		return SPECTRE_V2_CMD_AUTO;
	}

	if ((cmd == SPECTRE_V2_CMD_RETPOLINE ||
	     cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE ||
	     cmd == SPECTRE_V2_CMD_RETPOLINE_GENERIC) &&
	    !IS_ENABLED(CONFIG_RETPOLINE)) {
		pr_err("%s selected but not compiled in. Switching to AUTO select\n", mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if ((cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE) &&
	    !boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
		pr_err("%s selected, but CPU doesn't have a serializing LFENCE. Switching to AUTO select\n", mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	spec_v2_print_cond(mitigation_options[i].option,
			   mitigation_options[i].secure);
	return cmd;
}

static void __init spectre_v2_select_mitigation(void)
{
	enum spectre_v2_mitigation_cmd cmd = spectre_v2_parse_cmdline();
	enum spectre_v2_mitigation mode = SPECTRE_V2_NONE;

	/*
	 * If the CPU is not affected and the command line mode is NONE or AUTO
	 * then nothing to do.
	 */
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2) &&
	    (cmd == SPECTRE_V2_CMD_NONE || cmd == SPECTRE_V2_CMD_AUTO))
		return;

	switch (cmd) {
	case SPECTRE_V2_CMD_NONE:
		return;

	case SPECTRE_V2_CMD_FORCE:
	case SPECTRE_V2_CMD_AUTO:
		if (boot_cpu_has(X86_FEATURE_IBRS_ENHANCED)) {
			mode = SPECTRE_V2_IBRS_ENHANCED;
			/* Force it so VMEXIT will restore correctly */
			x86_spec_ctrl_base |= SPEC_CTRL_IBRS;
			wrmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);
			goto specv2_set_mode;
		}
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_auto;
		break;
	case SPECTRE_V2_CMD_RETPOLINE_LFENCE:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_lfence;
		break;
	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_generic;
		break;
	case SPECTRE_V2_CMD_RETPOLINE:
		if (IS_ENABLED(CONFIG_RETPOLINE))
			goto retpoline_auto;
		break;
	}
	pr_err("Spectre mitigation: kernel not compiled with retpoline; no mitigation available!");
	return;

retpoline_auto:
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON) {
	retpoline_lfence:
		if (!boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
			pr_err("Spectre mitigation: LFENCE not serializing, switching to generic retpoline\n");
			goto retpoline_generic;
		}
		mode = SPECTRE_V2_LFENCE;
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE_LFENCE);
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	} else {
	retpoline_generic:
		mode = SPECTRE_V2_RETPOLINE;
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	}

specv2_set_mode:
	spectre_v2_enabled = mode;
	pr_info("%s\n", spectre_v2_strings[mode]);

	/*
	 * If spectre v2 protection has been enabled, unconditionally fill
	 * RSB during a context switch; this protects against two independent
	 * issues:
	 *
	 *	- RSB underflow (and switch to BTB) on Skylake+
	 *	- SpectreRSB variant of spectre v2 on X86_BUG_SPECTRE_V2 CPUs
	 */
	setup_force_cpu_cap(X86_FEATURE_RSB_CTXSW);
	pr_info("Spectre v2 / SpectreRSB mitigation: Filling RSB on context switch\n");

	/*
	 * Retpoline means the kernel is safe because it has no indirect
	 * branches. Enhanced IBRS protects firmware too, so, enable restricted
	 * speculation around firmware calls only when Enhanced IBRS isn't
	 * supported.
	 *
	 * Use "mode" to check Enhanced IBRS instead of boot_cpu_has(), because
	 * the user might select retpoline on the kernel command line and if
	 * the CPU supports Enhanced IBRS, kernel might un-intentionally not
	 * enable IBRS around firmware calls.
	 */
	if (boot_cpu_has(X86_FEATURE_IBRS) && mode != SPECTRE_V2_IBRS_ENHANCED) {
		setup_force_cpu_cap(X86_FEATURE_USE_IBRS_FW);
		pr_info("Enabling Restricted Speculation for firmware calls\n");
	}

	/* Set up IBPB and STIBP depending on the general spectre V2 command */
	spectre_v2_user_select_mitigation(cmd);
}

static void update_stibp_msr(void * __unused)
{
	wrmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);
}

/* Update x86_spec_ctrl_base in case SMT state changed. */
static void update_stibp_strict(void)
{
	u64 mask = x86_spec_ctrl_base & ~SPEC_CTRL_STIBP;

	if (sched_smt_active())
		mask |= SPEC_CTRL_STIBP;

	if (mask == x86_spec_ctrl_base)
		return;

	pr_info("Update user space SMT mitigation: STIBP %s\n",
		mask & SPEC_CTRL_STIBP ? "always-on" : "off");
	x86_spec_ctrl_base = mask;
	on_each_cpu(update_stibp_msr, NULL, 1);
}

/* Update the static key controlling the evaluation of TIF_SPEC_IB */
static void update_indir_branch_cond(void)
{
	if (sched_smt_active())
		static_branch_enable(&switch_to_cond_stibp);
	else
		static_branch_disable(&switch_to_cond_stibp);
}

#undef pr_fmt
#define pr_fmt(fmt) fmt

/* Update the static key controlling the MDS CPU buffer clear in idle */
static void update_mds_branch_idle(void)
{
	/*
	 * Enable the idle clearing if SMT is active on CPUs which are
	 * affected only by MSBDS and not any other MDS variant.
	 *
	 * The other variants cannot be mitigated when SMT is enabled, so
	 * clearing the buffers on idle just to prevent the Store Buffer
	 * repartitioning leak would be a window dressing exercise.
	 */
	if (!boot_cpu_has_bug(X86_BUG_MSBDS_ONLY))
		return;

	if (sched_smt_active())
		static_branch_enable(&mds_idle_clear);
	else
		static_branch_disable(&mds_idle_clear);
}

#define MDS_MSG_SMT "MDS CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/mds.html for more details.\n"
#define TAA_MSG_SMT "TAA CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/tsx_async_abort.html for more details.\n"

void cpu_bugs_smt_update(void)
{
	mutex_lock(&spec_ctrl_mutex);

	switch (spectre_v2_user_stibp) {
	case SPECTRE_V2_USER_NONE:
		break;
	case SPECTRE_V2_USER_STRICT:
	case SPECTRE_V2_USER_STRICT_PREFERRED:
		update_stibp_strict();
		break;
	case SPECTRE_V2_USER_PRCTL:
	case SPECTRE_V2_USER_SECCOMP:
		update_indir_branch_cond();
		break;
	}

	switch (mds_mitigation) {
	case MDS_MITIGATION_FULL:
	case MDS_MITIGATION_VMWERV:
		if (sched_smt_active() && !boot_cpu_has(X86_BUG_MSBDS_ONLY))
			pr_warn_once(MDS_MSG_SMT);
		update_mds_branch_idle();
		break;
	case MDS_MITIGATION_OFF:
		break;
	}

	switch (taa_mitigation) {
	case TAA_MITIGATION_VERW:
	case TAA_MITIGATION_UCODE_NEEDED:
		if (sched_smt_active())
			pr_warn_once(TAA_MSG_SMT);
		break;
	case TAA_MITIGATION_TSX_DISABLED:
	case TAA_MITIGATION_OFF:
		break;
	}

	mutex_unlock(&spec_ctrl_mutex);
}

#undef pr_fmt
#define pr_fmt(fmt)	"Speculative Store Bypass: " fmt

static enum ssb_mitigation ssb_mode __ro_after_init = SPEC_STORE_BYPASS_NONE;

/* The kernel command line selection */
enum ssb_mitigation_cmd {
	SPEC_STORE_BYPASS_CMD_NONE,
	SPEC_STORE_BYPASS_CMD_AUTO,
	SPEC_STORE_BYPASS_CMD_ON,
	SPEC_STORE_BYPASS_CMD_PRCTL,
	SPEC_STORE_BYPASS_CMD_SECCOMP,
};

static const char * const ssb_strings[] = {
	[SPEC_STORE_BYPASS_NONE]	= "Vulnerable",
	[SPEC_STORE_BYPASS_DISABLE]	= "Mitigation: Speculative Store Bypass disabled",
	[SPEC_STORE_BYPASS_PRCTL]	= "Mitigation: Speculative Store Bypass disabled via prctl",
	[SPEC_STORE_BYPASS_SECCOMP]	= "Mitigation: Speculative Store Bypass disabled via prctl and seccomp",
};

static const struct {
	const char *option;
	enum ssb_mitigation_cmd cmd;
} ssb_mitigation_options[]  __initconst = {
	{ "auto",	SPEC_STORE_BYPASS_CMD_AUTO },    /* Platform decides */
	{ "on",		SPEC_STORE_BYPASS_CMD_ON },      /* Disable Speculative Store Bypass */
	{ "off",	SPEC_STORE_BYPASS_CMD_NONE },    /* Don't touch Speculative Store Bypass */
	{ "prctl",	SPEC_STORE_BYPASS_CMD_PRCTL },   /* Disable Speculative Store Bypass via prctl */
	{ "seccomp",	SPEC_STORE_BYPASS_CMD_SECCOMP }, /* Disable Speculative Store Bypass via prctl and seccomp */
};

static enum ssb_mitigation_cmd __init ssb_parse_cmdline(void)
{
	enum ssb_mitigation_cmd cmd = SPEC_STORE_BYPASS_CMD_AUTO;
	char arg[20];
	int ret, i;

	if (cmdline_find_option_bool(boot_command_line, "nospec_store_bypass_disable") ||
	    cpu_mitigations_off()) {
		return SPEC_STORE_BYPASS_CMD_NONE;
	} else {
		ret = cmdline_find_option(boot_command_line, "spec_store_bypass_disable",
					  arg, sizeof(arg));
		if (ret < 0)
			return SPEC_STORE_BYPASS_CMD_AUTO;

		for (i = 0; i < ARRAY_SIZE(ssb_mitigation_options); i++) {
			if (!match_option(arg, ret, ssb_mitigation_options[i].option))
				continue;

			cmd = ssb_mitigation_options[i].cmd;
			break;
		}

		if (i >= ARRAY_SIZE(ssb_mitigation_options)) {
			pr_err("unknown option (%s). Switching to AUTO select\n", arg);
			return SPEC_STORE_BYPASS_CMD_AUTO;
		}
	}

	return cmd;
}

static enum ssb_mitigation __init __ssb_select_mitigation(void)
{
	enum ssb_mitigation mode = SPEC_STORE_BYPASS_NONE;
	enum ssb_mitigation_cmd cmd;

	if (!boot_cpu_has(X86_FEATURE_SSBD))
		return mode;

	cmd = ssb_parse_cmdline();
	if (!boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS) &&
	    (cmd == SPEC_STORE_BYPASS_CMD_NONE ||
	     cmd == SPEC_STORE_BYPASS_CMD_AUTO))
		return mode;

	switch (cmd) {
	case SPEC_STORE_BYPASS_CMD_AUTO:
	case SPEC_STORE_BYPASS_CMD_SECCOMP:
		/*
		 * Choose prctl+seccomp as the default mode if seccomp is
		 * enabled.
		 */
		if (IS_ENABLED(CONFIG_SECCOMP))
			mode = SPEC_STORE_BYPASS_SECCOMP;
		else
			mode = SPEC_STORE_BYPASS_PRCTL;
		break;
	case SPEC_STORE_BYPASS_CMD_ON:
		mode = SPEC_STORE_BYPASS_DISABLE;
		break;
	case SPEC_STORE_BYPASS_CMD_PRCTL:
		mode = SPEC_STORE_BYPASS_PRCTL;
		break;
	case SPEC_STORE_BYPASS_CMD_NONE:
		break;
	}

	/*
	 * If SSBD is controlled by the SPEC_CTRL MSR, then set the proper
	 * bit in the mask to allow guests to use the mitigation even in the
	 * case where the host does not enable it.
	 */
	if (static_cpu_has(X86_FEATURE_SPEC_CTRL_SSBD) ||
	    static_cpu_has(X86_FEATURE_AMD_SSBD)) {
		x86_spec_ctrl_mask |= SPEC_CTRL_SSBD;
	}

	/*
	 * We have three CPU feature flags that are in play here:
	 *  - X86_BUG_SPEC_STORE_BYPASS - CPU is susceptible.
	 *  - X86_FEATURE_SSBD - CPU is able to turn off speculative store bypass
	 *  - X86_FEATURE_SPEC_STORE_BYPASS_DISABLE - engage the mitigation
	 */
	if (mode == SPEC_STORE_BYPASS_DISABLE) {
		setup_force_cpu_cap(X86_FEATURE_SPEC_STORE_BYPASS_DISABLE);
		/*
		 * Intel uses the SPEC CTRL MSR Bit(2) for this, while AMD may
		 * use a completely different MSR and bit dependent on family.
		 */
		if (!static_cpu_has(X86_FEATURE_SPEC_CTRL_SSBD) &&
		    !static_cpu_has(X86_FEATURE_AMD_SSBD)) {
			x86_amd_ssb_disable();
		} else {
			x86_spec_ctrl_base |= SPEC_CTRL_SSBD;
			wrmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);
		}
	}

	return mode;
}

static void ssb_select_mitigation(void)
{
	ssb_mode = __ssb_select_mitigation();

	if (boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS))
		pr_info("%s\n", ssb_strings[ssb_mode]);
}

#undef pr_fmt
#define pr_fmt(fmt)     "Speculation prctl: " fmt

static void task_update_spec_tif(struct task_struct *tsk)
{
	/* Force the update of the real TIF bits */
	set_tsk_thread_flag(tsk, TIF_SPEC_FORCE_UPDATE);

	/*
	 * Immediately update the speculation control MSRs for the current
	 * task, but for a non-current task delay setting the CPU
	 * mitigation until it is scheduled next.
	 *
	 * This can only happen for SECCOMP mitigation. For PRCTL it's
	 * always the current task.
	 */
	if (tsk == current)
		speculation_ctrl_update_current();
}

static int ssb_prctl_set(struct task_struct *task, unsigned long ctrl)
{
	if (ssb_mode != SPEC_STORE_BYPASS_PRCTL &&
	    ssb_mode != SPEC_STORE_BYPASS_SECCOMP)
		return -ENXIO;

	switch (ctrl) {
	case PR_SPEC_ENABLE:
		/* If speculation is force disabled, enable is not allowed */
		if (task_spec_ssb_force_disable(task))
			return -EPERM;
		task_clear_spec_ssb_disable(task);
		task_clear_spec_ssb_noexec(task);
		task_update_spec_tif(task);
		break;
	case PR_SPEC_DISABLE:
		task_set_spec_ssb_disable(task);
		task_clear_spec_ssb_noexec(task);
		task_update_spec_tif(task);
		break;
	case PR_SPEC_FORCE_DISABLE:
		task_set_spec_ssb_disable(task);
		task_set_spec_ssb_force_disable(task);
		task_clear_spec_ssb_noexec(task);
		task_update_spec_tif(task);
		break;
	case PR_SPEC_DISABLE_NOEXEC:
		if (task_spec_ssb_force_disable(task))
			return -EPERM;
		task_set_spec_ssb_disable(task);
		task_set_spec_ssb_noexec(task);
		task_update_spec_tif(task);
		break;
	default:
		return -ERANGE;
	}
	return 0;
}

static bool is_spec_ib_user_controlled(void)
{
	return spectre_v2_user_ibpb == SPECTRE_V2_USER_PRCTL ||
		spectre_v2_user_ibpb == SPECTRE_V2_USER_SECCOMP ||
		spectre_v2_user_stibp == SPECTRE_V2_USER_PRCTL ||
		spectre_v2_user_stibp == SPECTRE_V2_USER_SECCOMP;
}

static int ib_prctl_set(struct task_struct *task, unsigned long ctrl)
{
	switch (ctrl) {
	case PR_SPEC_ENABLE:
		if (spectre_v2_user_ibpb == SPECTRE_V2_USER_NONE &&
		    spectre_v2_user_stibp == SPECTRE_V2_USER_NONE)
			return 0;

		/*
		 * With strict mode for both IBPB and STIBP, the instruction
		 * code paths avoid checking this task flag and instead,
		 * unconditionally run the instruction. However, STIBP and IBPB
		 * are independent and either can be set to conditionally
		 * enabled regardless of the mode of the other.
		 *
		 * If either is set to conditional, allow the task flag to be
		 * updated, unless it was force-disabled by a previous prctl
		 * call. Currently, this is possible on an AMD CPU which has the
		 * feature X86_FEATURE_AMD_STIBP_ALWAYS_ON. In this case, if the
		 * kernel is booted with 'spectre_v2_user=seccomp', then
		 * spectre_v2_user_ibpb == SPECTRE_V2_USER_SECCOMP and
		 * spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT_PREFERRED.
		 */
		if (!is_spec_ib_user_controlled() ||
		    task_spec_ib_force_disable(task))
			return -EPERM;

		task_clear_spec_ib_disable(task);
		task_update_spec_tif(task);
		break;
	case PR_SPEC_DISABLE:
	case PR_SPEC_FORCE_DISABLE:
		/*
		 * Indirect branch speculation is always allowed when
		 * mitigation is force disabled.
		 */
		if (spectre_v2_user_ibpb == SPECTRE_V2_USER_NONE &&
		    spectre_v2_user_stibp == SPECTRE_V2_USER_NONE)
			return -EPERM;

		if (!is_spec_ib_user_controlled())
			return 0;

		task_set_spec_ib_disable(task);
		if (ctrl == PR_SPEC_FORCE_DISABLE)
			task_set_spec_ib_force_disable(task);
		task_update_spec_tif(task);
		break;
	default:
		return -ERANGE;
	}
	return 0;
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssb_prctl_set(task, ctrl);
	case PR_SPEC_INDIRECT_BRANCH:
		return ib_prctl_set(task, ctrl);
	default:
		return -ENODEV;
	}
}

#ifdef CONFIG_SECCOMP
void arch_seccomp_spec_mitigate(struct task_struct *task)
{
	if (ssb_mode == SPEC_STORE_BYPASS_SECCOMP)
		ssb_prctl_set(task, PR_SPEC_FORCE_DISABLE);
	if (spectre_v2_user_ibpb == SPECTRE_V2_USER_SECCOMP ||
	    spectre_v2_user_stibp == SPECTRE_V2_USER_SECCOMP)
		ib_prctl_set(task, PR_SPEC_FORCE_DISABLE);
}
#endif

static int ssb_prctl_get(struct task_struct *task)
{
	switch (ssb_mode) {
	case SPEC_STORE_BYPASS_DISABLE:
		return PR_SPEC_DISABLE;
	case SPEC_STORE_BYPASS_SECCOMP:
	case SPEC_STORE_BYPASS_PRCTL:
		if (task_spec_ssb_force_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;
		if (task_spec_ssb_noexec(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE_NOEXEC;
		if (task_spec_ssb_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	default:
		if (boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS))
			return PR_SPEC_ENABLE;
		return PR_SPEC_NOT_AFFECTED;
	}
}

static int ib_prctl_get(struct task_struct *task)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2))
		return PR_SPEC_NOT_AFFECTED;

	if (spectre_v2_user_ibpb == SPECTRE_V2_USER_NONE &&
	    spectre_v2_user_stibp == SPECTRE_V2_USER_NONE)
		return PR_SPEC_ENABLE;
	else if (is_spec_ib_user_controlled()) {
		if (task_spec_ib_force_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;
		if (task_spec_ib_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	} else if (spectre_v2_user_ibpb == SPECTRE_V2_USER_STRICT ||
	    spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT ||
	    spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT_PREFERRED)
		return PR_SPEC_DISABLE;
	else
		return PR_SPEC_NOT_AFFECTED;
}

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssb_prctl_get(task);
	case PR_SPEC_INDIRECT_BRANCH:
		return ib_prctl_get(task);
	default:
		return -ENODEV;
	}
}

void x86_spec_ctrl_setup_ap(void)
{
	if (boot_cpu_has(X86_FEATURE_MSR_SPEC_CTRL))
		wrmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);

	if (ssb_mode == SPEC_STORE_BYPASS_DISABLE)
		x86_amd_ssb_disable();
}

bool itlb_multihit_kvm_mitigation;
EXPORT_SYMBOL_GPL(itlb_multihit_kvm_mitigation);

#undef pr_fmt
#define pr_fmt(fmt)	"L1TF: " fmt

/* Default mitigation for L1TF-affected CPUs */
enum l1tf_mitigations l1tf_mitigation __ro_after_init = L1TF_MITIGATION_FLUSH;
#if IS_ENABLED(CONFIG_KVM_INTEL)
EXPORT_SYMBOL_GPL(l1tf_mitigation);
#endif
enum vmx_l1d_flush_state l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_AUTO;
EXPORT_SYMBOL_GPL(l1tf_vmx_mitigation);

/*
 * These CPUs all support 44bits physical address space internally in the
 * cache but CPUID can report a smaller number of physical address bits.
 *
 * The L1TF mitigation uses the top most address bit for the inversion of
 * non present PTEs. When the installed memory reaches into the top most
 * address bit due to memory holes, which has been observed on machines
 * which report 36bits physical address bits and have 32G RAM installed,
 * then the mitigation range check in l1tf_select_mitigation() triggers.
 * This is a false positive because the mitigation is still possible due to
 * the fact that the cache uses 44bit internally. Use the cache bits
 * instead of the reported physical bits and adjust them on the affected
 * machines to 44bit if the reported bits are less than 44.
 */
static void override_cache_bits(struct cpuinfo_x86 *c)
{
	if (c->x86 != 6)
		return;

	switch (c->x86_model) {
	case INTEL_FAM6_NEHALEM:
	case INTEL_FAM6_WESTMERE:
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_IVYBRIDGE:
	case INTEL_FAM6_HASWELL:
	case INTEL_FAM6_HASWELL_L:
	case INTEL_FAM6_HASWELL_G:
	case INTEL_FAM6_BROADWELL:
	case INTEL_FAM6_BROADWELL_G:
	case INTEL_FAM6_SKYLAKE_L:
	case INTEL_FAM6_SKYLAKE:
	case INTEL_FAM6_KABYLAKE_L:
	case INTEL_FAM6_KABYLAKE:
		if (c->x86_cache_bits < 44)
			c->x86_cache_bits = 44;
		break;
	}
}

static void __init l1tf_select_mitigation(void)
{
	u64 half_pa;

	if (!boot_cpu_has_bug(X86_BUG_L1TF))
		return;

	if (cpu_mitigations_off())
		l1tf_mitigation = L1TF_MITIGATION_OFF;
	else if (cpu_mitigations_auto_nosmt())
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOSMT;

	override_cache_bits(&boot_cpu_data);

	switch (l1tf_mitigation) {
	case L1TF_MITIGATION_OFF:
	case L1TF_MITIGATION_FLUSH_NOWARN:
	case L1TF_MITIGATION_FLUSH:
		break;
	case L1TF_MITIGATION_FLUSH_NOSMT:
	case L1TF_MITIGATION_FULL:
		cpu_smt_disable(false);
		break;
	case L1TF_MITIGATION_FULL_FORCE:
		cpu_smt_disable(true);
		break;
	}

#if CONFIG_PGTABLE_LEVELS == 2
	pr_warn("Kernel not compiled for PAE. No mitigation for L1TF\n");
	return;
#endif

	half_pa = (u64)l1tf_pfn_limit() << PAGE_SHIFT;
	if (l1tf_mitigation != L1TF_MITIGATION_OFF &&
			e820__mapped_any(half_pa, ULLONG_MAX - half_pa, E820_TYPE_RAM)) {
		pr_warn("System has more than MAX_PA/2 memory. L1TF mitigation not effective.\n");
		pr_info("You may make it effective by booting the kernel with mem=%llu parameter.\n",
				half_pa);
		pr_info("However, doing so will make a part of your RAM unusable.\n");
		pr_info("Reading https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/l1tf.html might help you decide.\n");
		return;
	}

	setup_force_cpu_cap(X86_FEATURE_L1TF_PTEINV);
}

static int __init l1tf_cmdline(char *str)
{
	if (!boot_cpu_has_bug(X86_BUG_L1TF))
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		l1tf_mitigation = L1TF_MITIGATION_OFF;
	else if (!strcmp(str, "flush,nowarn"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOWARN;
	else if (!strcmp(str, "flush"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH;
	else if (!strcmp(str, "flush,nosmt"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOSMT;
	else if (!strcmp(str, "full"))
		l1tf_mitigation = L1TF_MITIGATION_FULL;
	else if (!strcmp(str, "full,force"))
		l1tf_mitigation = L1TF_MITIGATION_FULL_FORCE;

	return 0;
}
early_param("l1tf", l1tf_cmdline);

#undef pr_fmt
#define pr_fmt(fmt) fmt

#ifdef CONFIG_SYSFS

#define L1TF_DEFAULT_MSG "Mitigation: PTE Inversion"

#if IS_ENABLED(CONFIG_KVM_INTEL)
static const char * const l1tf_vmx_states[] = {
	[VMENTER_L1D_FLUSH_AUTO]		= "auto",
	[VMENTER_L1D_FLUSH_NEVER]		= "vulnerable",
	[VMENTER_L1D_FLUSH_COND]		= "conditional cache flushes",
	[VMENTER_L1D_FLUSH_ALWAYS]		= "cache flushes",
	[VMENTER_L1D_FLUSH_EPT_DISABLED]	= "EPT disabled",
	[VMENTER_L1D_FLUSH_NOT_REQUIRED]	= "flush not necessary"
};

static ssize_t l1tf_show_state(char *buf)
{
	if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_AUTO)
		return sprintf(buf, "%s\n", L1TF_DEFAULT_MSG);

	if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_EPT_DISABLED ||
	    (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_NEVER &&
	     sched_smt_active())) {
		return sprintf(buf, "%s; VMX: %s\n", L1TF_DEFAULT_MSG,
			       l1tf_vmx_states[l1tf_vmx_mitigation]);
	}

	return sprintf(buf, "%s; VMX: %s, SMT %s\n", L1TF_DEFAULT_MSG,
		       l1tf_vmx_states[l1tf_vmx_mitigation],
		       sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t itlb_multihit_show_state(char *buf)
{
	if (!boot_cpu_has(X86_FEATURE_MSR_IA32_FEAT_CTL) ||
	    !boot_cpu_has(X86_FEATURE_VMX))
		return sprintf(buf, "KVM: Mitigation: VMX unsupported\n");
	else if (!(cr4_read_shadow() & X86_CR4_VMXE))
		return sprintf(buf, "KVM: Mitigation: VMX disabled\n");
	else if (itlb_multihit_kvm_mitigation)
		return sprintf(buf, "KVM: Mitigation: Split huge pages\n");
	else
		return sprintf(buf, "KVM: Vulnerable\n");
}
#else
static ssize_t l1tf_show_state(char *buf)
{
	return sprintf(buf, "%s\n", L1TF_DEFAULT_MSG);
}

static ssize_t itlb_multihit_show_state(char *buf)
{
	return sprintf(buf, "Processor vulnerable\n");
}
#endif

static ssize_t mds_show_state(char *buf)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sprintf(buf, "%s; SMT Host state unknown\n",
			       mds_strings[mds_mitigation]);
	}

	if (boot_cpu_has(X86_BUG_MSBDS_ONLY)) {
		return sprintf(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
			       (mds_mitigation == MDS_MITIGATION_OFF ? "vulnerable" :
			        sched_smt_active() ? "mitigated" : "disabled"));
	}

	return sprintf(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
		       sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t tsx_async_abort_show_state(char *buf)
{
	if ((taa_mitigation == TAA_MITIGATION_TSX_DISABLED) ||
	    (taa_mitigation == TAA_MITIGATION_OFF))
		return sprintf(buf, "%s\n", taa_strings[taa_mitigation]);

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sprintf(buf, "%s; SMT Host state unknown\n",
			       taa_strings[taa_mitigation]);
	}

	return sprintf(buf, "%s; SMT %s\n", taa_strings[taa_mitigation],
		       sched_smt_active() ? "vulnerable" : "disabled");
}

static char *stibp_state(void)
{
	if (spectre_v2_enabled == SPECTRE_V2_IBRS_ENHANCED)
		return "";

	switch (spectre_v2_user_stibp) {
	case SPECTRE_V2_USER_NONE:
		return ", STIBP: disabled";
	case SPECTRE_V2_USER_STRICT:
		return ", STIBP: forced";
	case SPECTRE_V2_USER_STRICT_PREFERRED:
		return ", STIBP: always-on";
	case SPECTRE_V2_USER_PRCTL:
	case SPECTRE_V2_USER_SECCOMP:
		if (static_key_enabled(&switch_to_cond_stibp))
			return ", STIBP: conditional";
	}
	return "";
}

static char *ibpb_state(void)
{
	if (boot_cpu_has(X86_FEATURE_IBPB)) {
		if (static_key_enabled(&switch_mm_always_ibpb))
			return ", IBPB: always-on";
		if (static_key_enabled(&switch_mm_cond_ibpb))
			return ", IBPB: conditional";
		return ", IBPB: disabled";
	}
	return "";
}

static ssize_t srbds_show_state(char *buf)
{
	return sprintf(buf, "%s\n", srbds_strings[srbds_mitigation]);
}

static ssize_t cpu_show_common(struct device *dev, struct device_attribute *attr,
			       char *buf, unsigned int bug)
{
	if (!boot_cpu_has_bug(bug))
		return sprintf(buf, "Not affected\n");

	switch (bug) {
	case X86_BUG_CPU_MELTDOWN:
		if (boot_cpu_has(X86_FEATURE_PTI))
			return sprintf(buf, "Mitigation: PTI\n");

		if (hypervisor_is_type(X86_HYPER_XEN_PV))
			return sprintf(buf, "Unknown (XEN PV detected, hypervisor mitigation required)\n");

		break;

	case X86_BUG_SPECTRE_V1:
		return sprintf(buf, "%s\n", spectre_v1_strings[spectre_v1_mitigation]);

	case X86_BUG_SPECTRE_V2:
		return sprintf(buf, "%s%s%s%s%s%s\n", spectre_v2_strings[spectre_v2_enabled],
			       ibpb_state(),
			       boot_cpu_has(X86_FEATURE_USE_IBRS_FW) ? ", IBRS_FW" : "",
			       stibp_state(),
			       boot_cpu_has(X86_FEATURE_RSB_CTXSW) ? ", RSB filling" : "",
			       spectre_v2_module_string());

	case X86_BUG_SPEC_STORE_BYPASS:
		return sprintf(buf, "%s\n", ssb_strings[ssb_mode]);

	case X86_BUG_L1TF:
		if (boot_cpu_has(X86_FEATURE_L1TF_PTEINV))
			return l1tf_show_state(buf);
		break;

	case X86_BUG_MDS:
		return mds_show_state(buf);

	case X86_BUG_TAA:
		return tsx_async_abort_show_state(buf);

	case X86_BUG_ITLB_MULTIHIT:
		return itlb_multihit_show_state(buf);

	case X86_BUG_SRBDS:
		return srbds_show_state(buf);

	default:
		break;
	}

	return sprintf(buf, "Vulnerable\n");
}

ssize_t cpu_show_meltdown(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_CPU_MELTDOWN);
}

ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SPECTRE_V1);
}

ssize_t cpu_show_spectre_v2(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SPECTRE_V2);
}

ssize_t cpu_show_spec_store_bypass(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SPEC_STORE_BYPASS);
}

ssize_t cpu_show_l1tf(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_L1TF);
}

ssize_t cpu_show_mds(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_MDS);
}

ssize_t cpu_show_tsx_async_abort(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_TAA);
}

ssize_t cpu_show_itlb_multihit(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_ITLB_MULTIHIT);
}

ssize_t cpu_show_srbds(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SRBDS);
}
#endif
