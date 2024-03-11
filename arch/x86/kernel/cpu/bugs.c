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
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/prctl.h>
#include <linux/sched/smt.h>
#include <linux/pgtable.h>
#include <linux/bpf.h>

#include <asm/spec-ctrl.h>
#include <asm/cmdline.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>
#include <asm/vmx.h>
#include <asm/paravirt.h>
#include <asm/intel-family.h>
#include <asm/e820/api.h>
#include <asm/hypervisor.h>
#include <asm/tlbflush.h>

#include "cpu.h"

static void __init spectre_v1_select_mitigation(void);
static void __init spectre_v2_select_mitigation(void);
static void __init retbleed_select_mitigation(void);
static void __init spectre_v2_user_select_mitigation(void);
static void __init ssb_select_mitigation(void);
static void __init l1tf_select_mitigation(void);
static void __init mds_select_mitigation(void);
static void __init md_clear_update_mitigation(void);
static void __init md_clear_select_mitigation(void);
static void __init taa_select_mitigation(void);
static void __init mmio_select_mitigation(void);
static void __init srbds_select_mitigation(void);
static void __init l1d_flush_select_mitigation(void);
static void __init gds_select_mitigation(void);
static void __init srso_select_mitigation(void);

/* The base value of the SPEC_CTRL MSR without task-specific bits set */
u64 x86_spec_ctrl_base;
EXPORT_SYMBOL_GPL(x86_spec_ctrl_base);

/* The current value of the SPEC_CTRL MSR with task-specific bits set */
DEFINE_PER_CPU(u64, x86_spec_ctrl_current);
EXPORT_SYMBOL_GPL(x86_spec_ctrl_current);

u64 x86_pred_cmd __ro_after_init = PRED_CMD_IBPB;
EXPORT_SYMBOL_GPL(x86_pred_cmd);

static DEFINE_MUTEX(spec_ctrl_mutex);

void (*x86_return_thunk)(void) __ro_after_init = &__x86_return_thunk;

/* Update SPEC_CTRL MSR and its cached copy unconditionally */
static void update_spec_ctrl(u64 val)
{
	this_cpu_write(x86_spec_ctrl_current, val);
	wrmsrl(MSR_IA32_SPEC_CTRL, val);
}

/*
 * Keep track of the SPEC_CTRL MSR value for the current task, which may differ
 * from x86_spec_ctrl_base due to STIBP/SSB in __speculation_ctrl_update().
 */
void update_spec_ctrl_cond(u64 val)
{
	if (this_cpu_read(x86_spec_ctrl_current) == val)
		return;

	this_cpu_write(x86_spec_ctrl_current, val);

	/*
	 * When KERNEL_IBRS this MSR is written on return-to-user, unless
	 * forced the update can be delayed until that time.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_KERNEL_IBRS))
		wrmsrl(MSR_IA32_SPEC_CTRL, val);
}

u64 spec_ctrl_current(void)
{
	return this_cpu_read(x86_spec_ctrl_current);
}
EXPORT_SYMBOL_GPL(spec_ctrl_current);

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

/* Control MDS CPU buffer clear before idling (halt, mwait) */
DEFINE_STATIC_KEY_FALSE(mds_idle_clear);
EXPORT_SYMBOL_GPL(mds_idle_clear);

/*
 * Controls whether l1d flush based mitigations are enabled,
 * based on hw features and admin setting via boot parameter
 * defaults to false
 */
DEFINE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);

/* Controls CPU Fill buffer clear before KVM guest MMIO accesses */
DEFINE_STATIC_KEY_FALSE(mmio_stale_data_clear);
EXPORT_SYMBOL_GPL(mmio_stale_data_clear);

void __init cpu_select_mitigations(void)
{
	/*
	 * Read the SPEC_CTRL MSR to account for reserved bits which may
	 * have unknown values. AMD64_LS_CFG MSR is cached in the early AMD
	 * init code as it is not enumerated and depends on the family.
	 */
	if (cpu_feature_enabled(X86_FEATURE_MSR_SPEC_CTRL)) {
		rdmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);

		/*
		 * Previously running kernel (kexec), may have some controls
		 * turned ON. Clear them and let the mitigations setup below
		 * rediscover them based on configuration.
		 */
		x86_spec_ctrl_base &= ~SPEC_CTRL_MITIGATIONS_MASK;
	}

	/* Select the proper CPU mitigations before patching alternatives: */
	spectre_v1_select_mitigation();
	spectre_v2_select_mitigation();
	/*
	 * retbleed_select_mitigation() relies on the state set by
	 * spectre_v2_select_mitigation(); specifically it wants to know about
	 * spectre_v2=ibrs.
	 */
	retbleed_select_mitigation();
	/*
	 * spectre_v2_user_select_mitigation() relies on the state set by
	 * retbleed_select_mitigation(); specifically the STIBP selection is
	 * forced for UNRET or IBPB.
	 */
	spectre_v2_user_select_mitigation();
	ssb_select_mitigation();
	l1tf_select_mitigation();
	md_clear_select_mitigation();
	srbds_select_mitigation();
	l1d_flush_select_mitigation();

	/*
	 * srso_select_mitigation() depends and must run after
	 * retbleed_select_mitigation().
	 */
	srso_select_mitigation();
	gds_select_mitigation();
}

/*
 * NOTE: This function is *only* called for SVM, since Intel uses
 * MSR_IA32_SPEC_CTRL for SSBD.
 */
void
x86_virt_spec_ctrl(u64 guest_virt_spec_ctrl, bool setguest)
{
	u64 guestval, hostval;
	struct thread_info *ti = current_thread_info();

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

		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);

		if (!boot_cpu_has(X86_BUG_MSBDS_ONLY) &&
		    (mds_nosmt || cpu_mitigations_auto_nosmt()))
			cpu_smt_disable(false);
	}
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
		return;
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
		return;

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
	setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);

	if (taa_nosmt || cpu_mitigations_auto_nosmt())
		cpu_smt_disable(false);
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
#define pr_fmt(fmt)	"MMIO Stale Data: " fmt

enum mmio_mitigations {
	MMIO_MITIGATION_OFF,
	MMIO_MITIGATION_UCODE_NEEDED,
	MMIO_MITIGATION_VERW,
};

/* Default mitigation for Processor MMIO Stale Data vulnerabilities */
static enum mmio_mitigations mmio_mitigation __ro_after_init = MMIO_MITIGATION_VERW;
static bool mmio_nosmt __ro_after_init = false;

static const char * const mmio_strings[] = {
	[MMIO_MITIGATION_OFF]		= "Vulnerable",
	[MMIO_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[MMIO_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
};

static void __init mmio_select_mitigation(void)
{
	u64 ia32_cap;

	if (!boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA) ||
	     boot_cpu_has_bug(X86_BUG_MMIO_UNKNOWN) ||
	     cpu_mitigations_off()) {
		mmio_mitigation = MMIO_MITIGATION_OFF;
		return;
	}

	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return;

	ia32_cap = x86_read_arch_cap_msr();

	/*
	 * Enable CPU buffer clear mitigation for host and VMM, if also affected
	 * by MDS or TAA. Otherwise, enable mitigation for VMM only.
	 */
	if (boot_cpu_has_bug(X86_BUG_MDS) || (boot_cpu_has_bug(X86_BUG_TAA) &&
					      boot_cpu_has(X86_FEATURE_RTM)))
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);

	/*
	 * X86_FEATURE_CLEAR_CPU_BUF could be enabled by other VERW based
	 * mitigations, disable KVM-only mitigation in that case.
	 */
	if (boot_cpu_has(X86_FEATURE_CLEAR_CPU_BUF))
		static_branch_disable(&mmio_stale_data_clear);
	else
		static_branch_enable(&mmio_stale_data_clear);

	/*
	 * If Processor-MMIO-Stale-Data bug is present and Fill Buffer data can
	 * be propagated to uncore buffers, clearing the Fill buffers on idle
	 * is required irrespective of SMT state.
	 */
	if (!(ia32_cap & ARCH_CAP_FBSDP_NO))
		static_branch_enable(&mds_idle_clear);

	/*
	 * Check if the system has the right microcode.
	 *
	 * CPU Fill buffer clear mitigation is enumerated by either an explicit
	 * FB_CLEAR or by the presence of both MD_CLEAR and L1D_FLUSH on MDS
	 * affected systems.
	 */
	if ((ia32_cap & ARCH_CAP_FB_CLEAR) ||
	    (boot_cpu_has(X86_FEATURE_MD_CLEAR) &&
	     boot_cpu_has(X86_FEATURE_FLUSH_L1D) &&
	     !(ia32_cap & ARCH_CAP_MDS_NO)))
		mmio_mitigation = MMIO_MITIGATION_VERW;
	else
		mmio_mitigation = MMIO_MITIGATION_UCODE_NEEDED;

	if (mmio_nosmt || cpu_mitigations_auto_nosmt())
		cpu_smt_disable(false);
}

static int __init mmio_stale_data_parse_cmdline(char *str)
{
	if (!boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA))
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off")) {
		mmio_mitigation = MMIO_MITIGATION_OFF;
	} else if (!strcmp(str, "full")) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
	} else if (!strcmp(str, "full,nosmt")) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
		mmio_nosmt = true;
	}

	return 0;
}
early_param("mmio_stale_data", mmio_stale_data_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"Register File Data Sampling: " fmt

enum rfds_mitigations {
	RFDS_MITIGATION_OFF,
	RFDS_MITIGATION_VERW,
	RFDS_MITIGATION_UCODE_NEEDED,
};

/* Default mitigation for Register File Data Sampling */
static enum rfds_mitigations rfds_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_RFDS) ? RFDS_MITIGATION_VERW : RFDS_MITIGATION_OFF;

static const char * const rfds_strings[] = {
	[RFDS_MITIGATION_OFF]			= "Vulnerable",
	[RFDS_MITIGATION_VERW]			= "Mitigation: Clear Register File",
	[RFDS_MITIGATION_UCODE_NEEDED]		= "Vulnerable: No microcode",
};

static void __init rfds_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_RFDS) || cpu_mitigations_off()) {
		rfds_mitigation = RFDS_MITIGATION_OFF;
		return;
	}
	if (rfds_mitigation == RFDS_MITIGATION_OFF)
		return;

	if (x86_read_arch_cap_msr() & ARCH_CAP_RFDS_CLEAR)
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
	else
		rfds_mitigation = RFDS_MITIGATION_UCODE_NEEDED;
}

static __init int rfds_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!boot_cpu_has_bug(X86_BUG_RFDS))
		return 0;

	if (!strcmp(str, "off"))
		rfds_mitigation = RFDS_MITIGATION_OFF;
	else if (!strcmp(str, "on"))
		rfds_mitigation = RFDS_MITIGATION_VERW;

	return 0;
}
early_param("reg_file_data_sampling", rfds_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)     "" fmt

static void __init md_clear_update_mitigation(void)
{
	if (cpu_mitigations_off())
		return;

	if (!boot_cpu_has(X86_FEATURE_CLEAR_CPU_BUF))
		goto out;

	/*
	 * X86_FEATURE_CLEAR_CPU_BUF is now enabled. Update MDS, TAA and MMIO
	 * Stale Data mitigation, if necessary.
	 */
	if (mds_mitigation == MDS_MITIGATION_OFF &&
	    boot_cpu_has_bug(X86_BUG_MDS)) {
		mds_mitigation = MDS_MITIGATION_FULL;
		mds_select_mitigation();
	}
	if (taa_mitigation == TAA_MITIGATION_OFF &&
	    boot_cpu_has_bug(X86_BUG_TAA)) {
		taa_mitigation = TAA_MITIGATION_VERW;
		taa_select_mitigation();
	}
	/*
	 * MMIO_MITIGATION_OFF is not checked here so that mmio_stale_data_clear
	 * gets updated correctly as per X86_FEATURE_CLEAR_CPU_BUF state.
	 */
	if (boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA)) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
		mmio_select_mitigation();
	}
	if (rfds_mitigation == RFDS_MITIGATION_OFF &&
	    boot_cpu_has_bug(X86_BUG_RFDS)) {
		rfds_mitigation = RFDS_MITIGATION_VERW;
		rfds_select_mitigation();
	}
out:
	if (boot_cpu_has_bug(X86_BUG_MDS))
		pr_info("MDS: %s\n", mds_strings[mds_mitigation]);
	if (boot_cpu_has_bug(X86_BUG_TAA))
		pr_info("TAA: %s\n", taa_strings[taa_mitigation]);
	if (boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA))
		pr_info("MMIO Stale Data: %s\n", mmio_strings[mmio_mitigation]);
	else if (boot_cpu_has_bug(X86_BUG_MMIO_UNKNOWN))
		pr_info("MMIO Stale Data: Unknown: No mitigations\n");
	if (boot_cpu_has_bug(X86_BUG_RFDS))
		pr_info("Register File Data Sampling: %s\n", rfds_strings[rfds_mitigation]);
}

static void __init md_clear_select_mitigation(void)
{
	mds_select_mitigation();
	taa_select_mitigation();
	mmio_select_mitigation();
	rfds_select_mitigation();

	/*
	 * As these mitigations are inter-related and rely on VERW instruction
	 * to clear the microarchitural buffers, update and print their status
	 * after mitigation selection is done for each of these vulnerabilities.
	 */
	md_clear_update_mitigation();
}

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

	/*
	 * A MDS_NO CPU for which SRBDS mitigation is not needed due to TSX
	 * being disabled and it hasn't received the SRBDS MSR microcode.
	 */
	if (!boot_cpu_has(X86_FEATURE_SRBDS_CTRL))
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
	 * Check to see if this is one of the MDS_NO systems supporting TSX that
	 * are only exposed to SRBDS when TSX is enabled or when CPU is affected
	 * by Processor MMIO Stale Data vulnerability.
	 */
	ia32_cap = x86_read_arch_cap_msr();
	if ((ia32_cap & ARCH_CAP_MDS_NO) && !boot_cpu_has(X86_FEATURE_RTM) &&
	    !boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA))
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
#define pr_fmt(fmt)     "L1D Flush : " fmt

enum l1d_flush_mitigations {
	L1D_FLUSH_OFF = 0,
	L1D_FLUSH_ON,
};

static enum l1d_flush_mitigations l1d_flush_mitigation __initdata = L1D_FLUSH_OFF;

static void __init l1d_flush_select_mitigation(void)
{
	if (!l1d_flush_mitigation || !boot_cpu_has(X86_FEATURE_FLUSH_L1D))
		return;

	static_branch_enable(&switch_mm_cond_l1d_flush);
	pr_info("Conditional flush on switch_mm() enabled\n");
}

static int __init l1d_flush_parse_cmdline(char *str)
{
	if (!strcmp(str, "on"))
		l1d_flush_mitigation = L1D_FLUSH_ON;

	return 0;
}
early_param("l1d_flush", l1d_flush_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"GDS: " fmt

enum gds_mitigations {
	GDS_MITIGATION_OFF,
	GDS_MITIGATION_UCODE_NEEDED,
	GDS_MITIGATION_FORCE,
	GDS_MITIGATION_FULL,
	GDS_MITIGATION_FULL_LOCKED,
	GDS_MITIGATION_HYPERVISOR,
};

#if IS_ENABLED(CONFIG_GDS_FORCE_MITIGATION)
static enum gds_mitigations gds_mitigation __ro_after_init = GDS_MITIGATION_FORCE;
#else
static enum gds_mitigations gds_mitigation __ro_after_init = GDS_MITIGATION_FULL;
#endif

static const char * const gds_strings[] = {
	[GDS_MITIGATION_OFF]		= "Vulnerable",
	[GDS_MITIGATION_UCODE_NEEDED]	= "Vulnerable: No microcode",
	[GDS_MITIGATION_FORCE]		= "Mitigation: AVX disabled, no microcode",
	[GDS_MITIGATION_FULL]		= "Mitigation: Microcode",
	[GDS_MITIGATION_FULL_LOCKED]	= "Mitigation: Microcode (locked)",
	[GDS_MITIGATION_HYPERVISOR]	= "Unknown: Dependent on hypervisor status",
};

bool gds_ucode_mitigated(void)
{
	return (gds_mitigation == GDS_MITIGATION_FULL ||
		gds_mitigation == GDS_MITIGATION_FULL_LOCKED);
}
EXPORT_SYMBOL_GPL(gds_ucode_mitigated);

void update_gds_msr(void)
{
	u64 mcu_ctrl_after;
	u64 mcu_ctrl;

	switch (gds_mitigation) {
	case GDS_MITIGATION_OFF:
		rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl |= GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_FULL_LOCKED:
		/*
		 * The LOCKED state comes from the boot CPU. APs might not have
		 * the same state. Make sure the mitigation is enabled on all
		 * CPUs.
		 */
	case GDS_MITIGATION_FULL:
		rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl &= ~GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_FORCE:
	case GDS_MITIGATION_UCODE_NEEDED:
	case GDS_MITIGATION_HYPERVISOR:
		return;
	};

	wrmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

	/*
	 * Check to make sure that the WRMSR value was not ignored. Writes to
	 * GDS_MITG_DIS will be ignored if this processor is locked but the boot
	 * processor was not.
	 */
	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl_after);
	WARN_ON_ONCE(mcu_ctrl != mcu_ctrl_after);
}

static void __init gds_select_mitigation(void)
{
	u64 mcu_ctrl;

	if (!boot_cpu_has_bug(X86_BUG_GDS))
		return;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		gds_mitigation = GDS_MITIGATION_HYPERVISOR;
		goto out;
	}

	if (cpu_mitigations_off())
		gds_mitigation = GDS_MITIGATION_OFF;
	/* Will verify below that mitigation _can_ be disabled */

	/* No microcode */
	if (!(x86_read_arch_cap_msr() & ARCH_CAP_GDS_CTRL)) {
		if (gds_mitigation == GDS_MITIGATION_FORCE) {
			/*
			 * This only needs to be done on the boot CPU so do it
			 * here rather than in update_gds_msr()
			 */
			setup_clear_cpu_cap(X86_FEATURE_AVX);
			pr_warn("Microcode update needed! Disabling AVX as mitigation.\n");
		} else {
			gds_mitigation = GDS_MITIGATION_UCODE_NEEDED;
		}
		goto out;
	}

	/* Microcode has mitigation, use it */
	if (gds_mitigation == GDS_MITIGATION_FORCE)
		gds_mitigation = GDS_MITIGATION_FULL;

	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
	if (mcu_ctrl & GDS_MITG_LOCKED) {
		if (gds_mitigation == GDS_MITIGATION_OFF)
			pr_warn("Mitigation locked. Disable failed.\n");

		/*
		 * The mitigation is selected from the boot CPU. All other CPUs
		 * _should_ have the same state. If the boot CPU isn't locked
		 * but others are then update_gds_msr() will WARN() of the state
		 * mismatch. If the boot CPU is locked update_gds_msr() will
		 * ensure the other CPUs have the mitigation enabled.
		 */
		gds_mitigation = GDS_MITIGATION_FULL_LOCKED;
	}

	update_gds_msr();
out:
	pr_info("%s\n", gds_strings[gds_mitigation]);
}

static int __init gds_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!boot_cpu_has_bug(X86_BUG_GDS))
		return 0;

	if (!strcmp(str, "off"))
		gds_mitigation = GDS_MITIGATION_OFF;
	else if (!strcmp(str, "force"))
		gds_mitigation = GDS_MITIGATION_FORCE;

	return 0;
}
early_param("gather_data_sampling", gds_parse_cmdline);

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

static enum spectre_v2_mitigation spectre_v2_enabled __ro_after_init =
	SPECTRE_V2_NONE;

#undef pr_fmt
#define pr_fmt(fmt)     "RETBleed: " fmt

enum retbleed_mitigation {
	RETBLEED_MITIGATION_NONE,
	RETBLEED_MITIGATION_UNRET,
	RETBLEED_MITIGATION_IBPB,
	RETBLEED_MITIGATION_IBRS,
	RETBLEED_MITIGATION_EIBRS,
};

enum retbleed_mitigation_cmd {
	RETBLEED_CMD_OFF,
	RETBLEED_CMD_AUTO,
	RETBLEED_CMD_UNRET,
	RETBLEED_CMD_IBPB,
};

static const char * const retbleed_strings[] = {
	[RETBLEED_MITIGATION_NONE]	= "Vulnerable",
	[RETBLEED_MITIGATION_UNRET]	= "Mitigation: untrained return thunk",
	[RETBLEED_MITIGATION_IBPB]	= "Mitigation: IBPB",
	[RETBLEED_MITIGATION_IBRS]	= "Mitigation: IBRS",
	[RETBLEED_MITIGATION_EIBRS]	= "Mitigation: Enhanced IBRS",
};

static enum retbleed_mitigation retbleed_mitigation __ro_after_init =
	RETBLEED_MITIGATION_NONE;
static enum retbleed_mitigation_cmd retbleed_cmd __ro_after_init =
	RETBLEED_CMD_AUTO;

static int __ro_after_init retbleed_nosmt = false;

static int __init retbleed_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	while (str) {
		char *next = strchr(str, ',');
		if (next) {
			*next = 0;
			next++;
		}

		if (!strcmp(str, "off")) {
			retbleed_cmd = RETBLEED_CMD_OFF;
		} else if (!strcmp(str, "auto")) {
			retbleed_cmd = RETBLEED_CMD_AUTO;
		} else if (!strcmp(str, "unret")) {
			retbleed_cmd = RETBLEED_CMD_UNRET;
		} else if (!strcmp(str, "ibpb")) {
			retbleed_cmd = RETBLEED_CMD_IBPB;
		} else if (!strcmp(str, "nosmt")) {
			retbleed_nosmt = true;
		} else {
			pr_err("Ignoring unknown retbleed option (%s).", str);
		}

		str = next;
	}

	return 0;
}
early_param("retbleed", retbleed_parse_cmdline);

#define RETBLEED_UNTRAIN_MSG "WARNING: BTB untrained return thunk mitigation is only effective on AMD/Hygon!\n"
#define RETBLEED_INTEL_MSG "WARNING: Spectre v2 mitigation leaves CPU vulnerable to RETBleed attacks, data leaks possible!\n"

static void __init retbleed_select_mitigation(void)
{
	bool mitigate_smt = false;

	if (!boot_cpu_has_bug(X86_BUG_RETBLEED) || cpu_mitigations_off())
		return;

	switch (retbleed_cmd) {
	case RETBLEED_CMD_OFF:
		return;

	case RETBLEED_CMD_UNRET:
		if (IS_ENABLED(CONFIG_CPU_UNRET_ENTRY)) {
			retbleed_mitigation = RETBLEED_MITIGATION_UNRET;
		} else {
			pr_err("WARNING: kernel not compiled with CPU_UNRET_ENTRY.\n");
			goto do_cmd_auto;
		}
		break;

	case RETBLEED_CMD_IBPB:
		if (!boot_cpu_has(X86_FEATURE_IBPB)) {
			pr_err("WARNING: CPU does not support IBPB.\n");
			goto do_cmd_auto;
		} else if (IS_ENABLED(CONFIG_CPU_IBPB_ENTRY)) {
			retbleed_mitigation = RETBLEED_MITIGATION_IBPB;
		} else {
			pr_err("WARNING: kernel not compiled with CPU_IBPB_ENTRY.\n");
			goto do_cmd_auto;
		}
		break;

do_cmd_auto:
	case RETBLEED_CMD_AUTO:
	default:
		if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
		    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON) {
			if (IS_ENABLED(CONFIG_CPU_UNRET_ENTRY))
				retbleed_mitigation = RETBLEED_MITIGATION_UNRET;
			else if (IS_ENABLED(CONFIG_CPU_IBPB_ENTRY) && boot_cpu_has(X86_FEATURE_IBPB))
				retbleed_mitigation = RETBLEED_MITIGATION_IBPB;
		}

		/*
		 * The Intel mitigation (IBRS or eIBRS) was already selected in
		 * spectre_v2_select_mitigation().  'retbleed_mitigation' will
		 * be set accordingly below.
		 */

		break;
	}

	switch (retbleed_mitigation) {
	case RETBLEED_MITIGATION_UNRET:
		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		setup_force_cpu_cap(X86_FEATURE_UNRET);

		if (IS_ENABLED(CONFIG_RETHUNK))
			x86_return_thunk = retbleed_return_thunk;

		if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
		    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
			pr_err(RETBLEED_UNTRAIN_MSG);

		mitigate_smt = true;
		break;

	case RETBLEED_MITIGATION_IBPB:
		setup_force_cpu_cap(X86_FEATURE_ENTRY_IBPB);
		mitigate_smt = true;
		break;

	default:
		break;
	}

	if (mitigate_smt && !boot_cpu_has(X86_FEATURE_STIBP) &&
	    (retbleed_nosmt || cpu_mitigations_auto_nosmt()))
		cpu_smt_disable(false);

	/*
	 * Let IBRS trump all on Intel without affecting the effects of the
	 * retbleed= cmdline option.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
		switch (spectre_v2_enabled) {
		case SPECTRE_V2_IBRS:
			retbleed_mitigation = RETBLEED_MITIGATION_IBRS;
			break;
		case SPECTRE_V2_EIBRS:
		case SPECTRE_V2_EIBRS_RETPOLINE:
		case SPECTRE_V2_EIBRS_LFENCE:
			retbleed_mitigation = RETBLEED_MITIGATION_EIBRS;
			break;
		default:
			pr_err(RETBLEED_INTEL_MSG);
		}
	}

	pr_info("%s\n", retbleed_strings[retbleed_mitigation]);
}

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V2 : " fmt

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

#define SPECTRE_V2_LFENCE_MSG "WARNING: LFENCE mitigation is not recommended for this CPU, data leaks possible!\n"
#define SPECTRE_V2_EIBRS_EBPF_MSG "WARNING: Unprivileged eBPF is enabled with eIBRS on, data leaks possible via Spectre v2 BHB attacks!\n"
#define SPECTRE_V2_EIBRS_LFENCE_EBPF_SMT_MSG "WARNING: Unprivileged eBPF is enabled with eIBRS+LFENCE mitigation and SMT, data leaks possible via Spectre v2 BHB attacks!\n"
#define SPECTRE_V2_IBRS_PERF_MSG "WARNING: IBRS mitigation selected on Enhanced IBRS CPU, this may cause unnecessary performance loss\n"

#ifdef CONFIG_BPF_SYSCALL
void unpriv_ebpf_notify(int new_state)
{
	if (new_state)
		return;

	/* Unprivileged eBPF is enabled */

	switch (spectre_v2_enabled) {
	case SPECTRE_V2_EIBRS:
		pr_err(SPECTRE_V2_EIBRS_EBPF_MSG);
		break;
	case SPECTRE_V2_EIBRS_LFENCE:
		if (sched_smt_active())
			pr_err(SPECTRE_V2_EIBRS_LFENCE_EBPF_SMT_MSG);
		break;
	default:
		break;
	}
}
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
	SPECTRE_V2_CMD_EIBRS,
	SPECTRE_V2_CMD_EIBRS_RETPOLINE,
	SPECTRE_V2_CMD_EIBRS_LFENCE,
	SPECTRE_V2_CMD_IBRS,
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

static __ro_after_init enum spectre_v2_mitigation_cmd spectre_v2_cmd;

static enum spectre_v2_user_cmd __init
spectre_v2_parse_user_cmdline(void)
{
	char arg[20];
	int ret, i;

	switch (spectre_v2_cmd) {
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

static inline bool spectre_v2_in_eibrs_mode(enum spectre_v2_mitigation mode)
{
	return mode == SPECTRE_V2_EIBRS ||
	       mode == SPECTRE_V2_EIBRS_RETPOLINE ||
	       mode == SPECTRE_V2_EIBRS_LFENCE;
}

static inline bool spectre_v2_in_ibrs_mode(enum spectre_v2_mitigation mode)
{
	return spectre_v2_in_eibrs_mode(mode) || mode == SPECTRE_V2_IBRS;
}

static void __init
spectre_v2_user_select_mitigation(void)
{
	enum spectre_v2_user_mitigation mode = SPECTRE_V2_USER_NONE;
	bool smt_possible = IS_ENABLED(CONFIG_SMP);
	enum spectre_v2_user_cmd cmd;

	if (!boot_cpu_has(X86_FEATURE_IBPB) && !boot_cpu_has(X86_FEATURE_STIBP))
		return;

	if (cpu_smt_control == CPU_SMT_FORCE_DISABLED ||
	    cpu_smt_control == CPU_SMT_NOT_SUPPORTED)
		smt_possible = false;

	cmd = spectre_v2_parse_user_cmdline();
	switch (cmd) {
	case SPECTRE_V2_USER_CMD_NONE:
		goto set_mode;
	case SPECTRE_V2_USER_CMD_FORCE:
		mode = SPECTRE_V2_USER_STRICT;
		break;
	case SPECTRE_V2_USER_CMD_AUTO:
	case SPECTRE_V2_USER_CMD_PRCTL:
	case SPECTRE_V2_USER_CMD_PRCTL_IBPB:
		mode = SPECTRE_V2_USER_PRCTL;
		break;
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
	 * If no STIBP, Intel enhanced IBRS is enabled, or SMT impossible, STIBP
	 * is not required.
	 *
	 * Intel's Enhanced IBRS also protects against cross-thread branch target
	 * injection in user-mode as the IBRS bit remains always set which
	 * implicitly enables cross-thread protections.  However, in legacy IBRS
	 * mode, the IBRS bit is set only on kernel entry and cleared on return
	 * to userspace.  AMD Automatic IBRS also does not protect userspace.
	 * These modes therefore disable the implicit cross-thread protection,
	 * so allow for STIBP to be selected in those cases.
	 */
	if (!boot_cpu_has(X86_FEATURE_STIBP) ||
	    !smt_possible ||
	    (spectre_v2_in_eibrs_mode(spectre_v2_enabled) &&
	     !boot_cpu_has(X86_FEATURE_AUTOIBRS)))
		return;

	/*
	 * At this point, an STIBP mode other than "off" has been set.
	 * If STIBP support is not being forced, check if STIBP always-on
	 * is preferred.
	 */
	if (mode != SPECTRE_V2_USER_STRICT &&
	    boot_cpu_has(X86_FEATURE_AMD_STIBP_ALWAYS_ON))
		mode = SPECTRE_V2_USER_STRICT_PREFERRED;

	if (retbleed_mitigation == RETBLEED_MITIGATION_UNRET ||
	    retbleed_mitigation == RETBLEED_MITIGATION_IBPB) {
		if (mode != SPECTRE_V2_USER_STRICT &&
		    mode != SPECTRE_V2_USER_STRICT_PREFERRED)
			pr_info("Selecting STIBP always-on mode to complement retbleed mitigation\n");
		mode = SPECTRE_V2_USER_STRICT_PREFERRED;
	}

	spectre_v2_user_stibp = mode;

set_mode:
	pr_info("%s\n", spectre_v2_user_strings[mode]);
}

static const char * const spectre_v2_strings[] = {
	[SPECTRE_V2_NONE]			= "Vulnerable",
	[SPECTRE_V2_RETPOLINE]			= "Mitigation: Retpolines",
	[SPECTRE_V2_LFENCE]			= "Mitigation: LFENCE",
	[SPECTRE_V2_EIBRS]			= "Mitigation: Enhanced / Automatic IBRS",
	[SPECTRE_V2_EIBRS_LFENCE]		= "Mitigation: Enhanced / Automatic IBRS + LFENCE",
	[SPECTRE_V2_EIBRS_RETPOLINE]		= "Mitigation: Enhanced / Automatic IBRS + Retpolines",
	[SPECTRE_V2_IBRS]			= "Mitigation: IBRS",
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
	{ "eibrs",		SPECTRE_V2_CMD_EIBRS,		  false },
	{ "eibrs,lfence",	SPECTRE_V2_CMD_EIBRS_LFENCE,	  false },
	{ "eibrs,retpoline",	SPECTRE_V2_CMD_EIBRS_RETPOLINE,	  false },
	{ "auto",		SPECTRE_V2_CMD_AUTO,		  false },
	{ "ibrs",		SPECTRE_V2_CMD_IBRS,              false },
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
	     cmd == SPECTRE_V2_CMD_RETPOLINE_GENERIC ||
	     cmd == SPECTRE_V2_CMD_EIBRS_LFENCE ||
	     cmd == SPECTRE_V2_CMD_EIBRS_RETPOLINE) &&
	    !IS_ENABLED(CONFIG_RETPOLINE)) {
		pr_err("%s selected but not compiled in. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if ((cmd == SPECTRE_V2_CMD_EIBRS ||
	     cmd == SPECTRE_V2_CMD_EIBRS_LFENCE ||
	     cmd == SPECTRE_V2_CMD_EIBRS_RETPOLINE) &&
	    !boot_cpu_has(X86_FEATURE_IBRS_ENHANCED)) {
		pr_err("%s selected but CPU doesn't have Enhanced or Automatic IBRS. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if ((cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE ||
	     cmd == SPECTRE_V2_CMD_EIBRS_LFENCE) &&
	    !boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
		pr_err("%s selected, but CPU doesn't have a serializing LFENCE. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && !IS_ENABLED(CONFIG_CPU_IBRS_ENTRY)) {
		pr_err("%s selected but not compiled in. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		pr_err("%s selected but not Intel CPU. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && !boot_cpu_has(X86_FEATURE_IBRS)) {
		pr_err("%s selected but CPU doesn't have IBRS. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && boot_cpu_has(X86_FEATURE_XENPV)) {
		pr_err("%s selected but running as XenPV guest. Switching to AUTO select\n",
		       mitigation_options[i].option);
		return SPECTRE_V2_CMD_AUTO;
	}

	spec_v2_print_cond(mitigation_options[i].option,
			   mitigation_options[i].secure);
	return cmd;
}

static enum spectre_v2_mitigation __init spectre_v2_select_retpoline(void)
{
	if (!IS_ENABLED(CONFIG_RETPOLINE)) {
		pr_err("Kernel not compiled with retpoline; no mitigation available!");
		return SPECTRE_V2_NONE;
	}

	return SPECTRE_V2_RETPOLINE;
}

/* Disable in-kernel use of non-RSB RET predictors */
static void __init spec_ctrl_disable_kernel_rrsba(void)
{
	u64 ia32_cap;

	if (!boot_cpu_has(X86_FEATURE_RRSBA_CTRL))
		return;

	ia32_cap = x86_read_arch_cap_msr();

	if (ia32_cap & ARCH_CAP_RRSBA) {
		x86_spec_ctrl_base |= SPEC_CTRL_RRSBA_DIS_S;
		update_spec_ctrl(x86_spec_ctrl_base);
	}
}

static void __init spectre_v2_determine_rsb_fill_type_at_vmexit(enum spectre_v2_mitigation mode)
{
	/*
	 * Similar to context switches, there are two types of RSB attacks
	 * after VM exit:
	 *
	 * 1) RSB underflow
	 *
	 * 2) Poisoned RSB entry
	 *
	 * When retpoline is enabled, both are mitigated by filling/clearing
	 * the RSB.
	 *
	 * When IBRS is enabled, while #1 would be mitigated by the IBRS branch
	 * prediction isolation protections, RSB still needs to be cleared
	 * because of #2.  Note that SMEP provides no protection here, unlike
	 * user-space-poisoned RSB entries.
	 *
	 * eIBRS should protect against RSB poisoning, but if the EIBRS_PBRSB
	 * bug is present then a LITE version of RSB protection is required,
	 * just a single call needs to retire before a RET is executed.
	 */
	switch (mode) {
	case SPECTRE_V2_NONE:
		return;

	case SPECTRE_V2_EIBRS_LFENCE:
	case SPECTRE_V2_EIBRS:
		if (boot_cpu_has_bug(X86_BUG_EIBRS_PBRSB)) {
			setup_force_cpu_cap(X86_FEATURE_RSB_VMEXIT_LITE);
			pr_info("Spectre v2 / PBRSB-eIBRS: Retire a single CALL on VMEXIT\n");
		}
		return;

	case SPECTRE_V2_EIBRS_RETPOLINE:
	case SPECTRE_V2_RETPOLINE:
	case SPECTRE_V2_LFENCE:
	case SPECTRE_V2_IBRS:
		setup_force_cpu_cap(X86_FEATURE_RSB_VMEXIT);
		pr_info("Spectre v2 / SpectreRSB : Filling RSB on VMEXIT\n");
		return;
	}

	pr_warn_once("Unknown Spectre v2 mode, disabling RSB mitigation at VM exit");
	dump_stack();
}

/*
 * Set BHI_DIS_S to prevent indirect branches in kernel to be influenced by
 * branch history in userspace. Not needed if BHI_NO is set.
 */
static bool __init spec_ctrl_bhi_dis(void)
{
	if (!boot_cpu_has(X86_FEATURE_BHI_CTRL))
		return false;

	x86_spec_ctrl_base |= SPEC_CTRL_BHI_DIS_S;
	update_spec_ctrl(x86_spec_ctrl_base);
	setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_HW);

	return true;
}

enum bhi_mitigations {
	BHI_MITIGATION_OFF,
	BHI_MITIGATION_ON,
	BHI_MITIGATION_AUTO,
};

static enum bhi_mitigations bhi_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_SPECTRE_BHI_ON)  ? BHI_MITIGATION_ON  :
	IS_ENABLED(CONFIG_SPECTRE_BHI_OFF) ? BHI_MITIGATION_OFF :
					     BHI_MITIGATION_AUTO;

static int __init spectre_bhi_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		bhi_mitigation = BHI_MITIGATION_OFF;
	else if (!strcmp(str, "on"))
		bhi_mitigation = BHI_MITIGATION_ON;
	else if (!strcmp(str, "auto"))
		bhi_mitigation = BHI_MITIGATION_AUTO;
	else
		pr_err("Ignoring unknown spectre_bhi option (%s)", str);

	return 0;
}
early_param("spectre_bhi", spectre_bhi_parse_cmdline);

static void __init bhi_select_mitigation(void)
{
	if (bhi_mitigation == BHI_MITIGATION_OFF)
		return;

	/* Retpoline mitigates against BHI unless the CPU has RRSBA behavior */
	if (cpu_feature_enabled(X86_FEATURE_RETPOLINE) &&
	    !(x86_read_arch_cap_msr() & ARCH_CAP_RRSBA))
		return;

	if (spec_ctrl_bhi_dis())
		return;

	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	/* Mitigate KVM by default */
	setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_LOOP_ON_VMEXIT);
	pr_info("Spectre BHI mitigation: SW BHB clearing on vm exit\n");

	if (bhi_mitigation == BHI_MITIGATION_AUTO)
		return;

	/* Mitigate syscalls when the mitigation is forced =on */
	setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_LOOP);
	pr_info("Spectre BHI mitigation: SW BHB clearing on syscall\n");
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
			mode = SPECTRE_V2_EIBRS;
			break;
		}

		if (IS_ENABLED(CONFIG_CPU_IBRS_ENTRY) &&
		    boot_cpu_has_bug(X86_BUG_RETBLEED) &&
		    retbleed_cmd != RETBLEED_CMD_OFF &&
		    boot_cpu_has(X86_FEATURE_IBRS) &&
		    boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
			mode = SPECTRE_V2_IBRS;
			break;
		}

		mode = spectre_v2_select_retpoline();
		break;

	case SPECTRE_V2_CMD_RETPOLINE_LFENCE:
		pr_err(SPECTRE_V2_LFENCE_MSG);
		mode = SPECTRE_V2_LFENCE;
		break;

	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
		mode = SPECTRE_V2_RETPOLINE;
		break;

	case SPECTRE_V2_CMD_RETPOLINE:
		mode = spectre_v2_select_retpoline();
		break;

	case SPECTRE_V2_CMD_IBRS:
		mode = SPECTRE_V2_IBRS;
		break;

	case SPECTRE_V2_CMD_EIBRS:
		mode = SPECTRE_V2_EIBRS;
		break;

	case SPECTRE_V2_CMD_EIBRS_LFENCE:
		mode = SPECTRE_V2_EIBRS_LFENCE;
		break;

	case SPECTRE_V2_CMD_EIBRS_RETPOLINE:
		mode = SPECTRE_V2_EIBRS_RETPOLINE;
		break;
	}

	if (mode == SPECTRE_V2_EIBRS && unprivileged_ebpf_enabled())
		pr_err(SPECTRE_V2_EIBRS_EBPF_MSG);

	if (spectre_v2_in_ibrs_mode(mode)) {
		if (boot_cpu_has(X86_FEATURE_AUTOIBRS)) {
			msr_set_bit(MSR_EFER, _EFER_AUTOIBRS);
		} else {
			x86_spec_ctrl_base |= SPEC_CTRL_IBRS;
			update_spec_ctrl(x86_spec_ctrl_base);
		}
	}

	switch (mode) {
	case SPECTRE_V2_NONE:
	case SPECTRE_V2_EIBRS:
		break;

	case SPECTRE_V2_IBRS:
		setup_force_cpu_cap(X86_FEATURE_KERNEL_IBRS);
		if (boot_cpu_has(X86_FEATURE_IBRS_ENHANCED))
			pr_warn(SPECTRE_V2_IBRS_PERF_MSG);
		break;

	case SPECTRE_V2_LFENCE:
	case SPECTRE_V2_EIBRS_LFENCE:
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE_LFENCE);
		fallthrough;

	case SPECTRE_V2_RETPOLINE:
	case SPECTRE_V2_EIBRS_RETPOLINE:
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
		break;
	}

	/*
	 * Disable alternate RSB predictions in kernel when indirect CALLs and
	 * JMPs gets protection against BHI and Intramode-BTI, but RET
	 * prediction from a non-RSB predictor is still a risk.
	 */
	if (mode == SPECTRE_V2_EIBRS_LFENCE ||
	    mode == SPECTRE_V2_EIBRS_RETPOLINE ||
	    mode == SPECTRE_V2_RETPOLINE)
		spec_ctrl_disable_kernel_rrsba();

	if (boot_cpu_has(X86_BUG_BHI))
		bhi_select_mitigation();

	spectre_v2_enabled = mode;
	pr_info("%s\n", spectre_v2_strings[mode]);

	/*
	 * If Spectre v2 protection has been enabled, fill the RSB during a
	 * context switch.  In general there are two types of RSB attacks
	 * across context switches, for which the CALLs/RETs may be unbalanced.
	 *
	 * 1) RSB underflow
	 *
	 *    Some Intel parts have "bottomless RSB".  When the RSB is empty,
	 *    speculated return targets may come from the branch predictor,
	 *    which could have a user-poisoned BTB or BHB entry.
	 *
	 *    AMD has it even worse: *all* returns are speculated from the BTB,
	 *    regardless of the state of the RSB.
	 *
	 *    When IBRS or eIBRS is enabled, the "user -> kernel" attack
	 *    scenario is mitigated by the IBRS branch prediction isolation
	 *    properties, so the RSB buffer filling wouldn't be necessary to
	 *    protect against this type of attack.
	 *
	 *    The "user -> user" attack scenario is mitigated by RSB filling.
	 *
	 * 2) Poisoned RSB entry
	 *
	 *    If the 'next' in-kernel return stack is shorter than 'prev',
	 *    'next' could be tricked into speculating with a user-poisoned RSB
	 *    entry.
	 *
	 *    The "user -> kernel" attack scenario is mitigated by SMEP and
	 *    eIBRS.
	 *
	 *    The "user -> user" scenario, also known as SpectreBHB, requires
	 *    RSB clearing.
	 *
	 * So to mitigate all cases, unconditionally fill RSB on context
	 * switches.
	 *
	 * FIXME: Is this pointless for retbleed-affected AMD?
	 */
	setup_force_cpu_cap(X86_FEATURE_RSB_CTXSW);
	pr_info("Spectre v2 / SpectreRSB mitigation: Filling RSB on context switch\n");

	spectre_v2_determine_rsb_fill_type_at_vmexit(mode);

	/*
	 * Retpoline protects the kernel, but doesn't protect firmware.  IBRS
	 * and Enhanced IBRS protect firmware too, so enable IBRS around
	 * firmware calls only when IBRS / Enhanced / Automatic IBRS aren't
	 * otherwise enabled.
	 *
	 * Use "mode" to check Enhanced IBRS instead of boot_cpu_has(), because
	 * the user might select retpoline on the kernel command line and if
	 * the CPU supports Enhanced IBRS, kernel might un-intentionally not
	 * enable IBRS around firmware calls.
	 */
	if (boot_cpu_has_bug(X86_BUG_RETBLEED) &&
	    boot_cpu_has(X86_FEATURE_IBPB) &&
	    (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	     boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)) {

		if (retbleed_cmd != RETBLEED_CMD_IBPB) {
			setup_force_cpu_cap(X86_FEATURE_USE_IBPB_FW);
			pr_info("Enabling Speculation Barrier for firmware calls\n");
		}

	} else if (boot_cpu_has(X86_FEATURE_IBRS) && !spectre_v2_in_ibrs_mode(mode)) {
		setup_force_cpu_cap(X86_FEATURE_USE_IBRS_FW);
		pr_info("Enabling Restricted Speculation for firmware calls\n");
	}

	/* Set up IBPB and STIBP depending on the general spectre V2 command */
	spectre_v2_cmd = cmd;
}

static void update_stibp_msr(void * __unused)
{
	u64 val = spec_ctrl_current() | (x86_spec_ctrl_base & SPEC_CTRL_STIBP);
	update_spec_ctrl(val);
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
	u64 ia32_cap = x86_read_arch_cap_msr();

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

	if (sched_smt_active()) {
		static_branch_enable(&mds_idle_clear);
	} else if (mmio_mitigation == MMIO_MITIGATION_OFF ||
		   (ia32_cap & ARCH_CAP_FBSDP_NO)) {
		static_branch_disable(&mds_idle_clear);
	}
}

#define MDS_MSG_SMT "MDS CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/mds.html for more details.\n"
#define TAA_MSG_SMT "TAA CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/tsx_async_abort.html for more details.\n"
#define MMIO_MSG_SMT "MMIO Stale Data CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/processor_mmio_stale_data.html for more details.\n"

void cpu_bugs_smt_update(void)
{
	mutex_lock(&spec_ctrl_mutex);

	if (sched_smt_active() && unprivileged_ebpf_enabled() &&
	    spectre_v2_enabled == SPECTRE_V2_EIBRS_LFENCE)
		pr_warn_once(SPECTRE_V2_EIBRS_LFENCE_EBPF_SMT_MSG);

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

	switch (mmio_mitigation) {
	case MMIO_MITIGATION_VERW:
	case MMIO_MITIGATION_UCODE_NEEDED:
		if (sched_smt_active())
			pr_warn_once(MMIO_MSG_SMT);
		break;
	case MMIO_MITIGATION_OFF:
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
	case SPEC_STORE_BYPASS_CMD_AUTO:
	case SPEC_STORE_BYPASS_CMD_PRCTL:
		mode = SPEC_STORE_BYPASS_PRCTL;
		break;
	case SPEC_STORE_BYPASS_CMD_NONE:
		break;
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
			update_spec_ctrl(x86_spec_ctrl_base);
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

static int l1d_flush_prctl_set(struct task_struct *task, unsigned long ctrl)
{

	if (!static_branch_unlikely(&switch_mm_cond_l1d_flush))
		return -EPERM;

	switch (ctrl) {
	case PR_SPEC_ENABLE:
		set_ti_thread_flag(&task->thread_info, TIF_SPEC_L1D_FLUSH);
		return 0;
	case PR_SPEC_DISABLE:
		clear_ti_thread_flag(&task->thread_info, TIF_SPEC_L1D_FLUSH);
		return 0;
	default:
		return -ERANGE;
	}
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
		if (task == current)
			indirect_branch_prediction_barrier();
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
	case PR_SPEC_L1D_FLUSH:
		return l1d_flush_prctl_set(task, ctrl);
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

static int l1d_flush_prctl_get(struct task_struct *task)
{
	if (!static_branch_unlikely(&switch_mm_cond_l1d_flush))
		return PR_SPEC_FORCE_DISABLE;

	if (test_ti_thread_flag(&task->thread_info, TIF_SPEC_L1D_FLUSH))
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	else
		return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
}

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
	case PR_SPEC_L1D_FLUSH:
		return l1d_flush_prctl_get(task);
	default:
		return -ENODEV;
	}
}

void x86_spec_ctrl_setup_ap(void)
{
	if (boot_cpu_has(X86_FEATURE_MSR_SPEC_CTRL))
		update_spec_ctrl(x86_spec_ctrl_base);

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
#define pr_fmt(fmt)	"Speculative Return Stack Overflow: " fmt

enum srso_mitigation {
	SRSO_MITIGATION_NONE,
	SRSO_MITIGATION_MICROCODE,
	SRSO_MITIGATION_SAFE_RET,
	SRSO_MITIGATION_IBPB,
	SRSO_MITIGATION_IBPB_ON_VMEXIT,
};

enum srso_mitigation_cmd {
	SRSO_CMD_OFF,
	SRSO_CMD_MICROCODE,
	SRSO_CMD_SAFE_RET,
	SRSO_CMD_IBPB,
	SRSO_CMD_IBPB_ON_VMEXIT,
};

static const char * const srso_strings[] = {
	[SRSO_MITIGATION_NONE]           = "Vulnerable",
	[SRSO_MITIGATION_MICROCODE]      = "Mitigation: microcode",
	[SRSO_MITIGATION_SAFE_RET]	 = "Mitigation: safe RET",
	[SRSO_MITIGATION_IBPB]		 = "Mitigation: IBPB",
	[SRSO_MITIGATION_IBPB_ON_VMEXIT] = "Mitigation: IBPB on VMEXIT only"
};

static enum srso_mitigation srso_mitigation __ro_after_init = SRSO_MITIGATION_NONE;
static enum srso_mitigation_cmd srso_cmd __ro_after_init = SRSO_CMD_SAFE_RET;

static int __init srso_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		srso_cmd = SRSO_CMD_OFF;
	else if (!strcmp(str, "microcode"))
		srso_cmd = SRSO_CMD_MICROCODE;
	else if (!strcmp(str, "safe-ret"))
		srso_cmd = SRSO_CMD_SAFE_RET;
	else if (!strcmp(str, "ibpb"))
		srso_cmd = SRSO_CMD_IBPB;
	else if (!strcmp(str, "ibpb-vmexit"))
		srso_cmd = SRSO_CMD_IBPB_ON_VMEXIT;
	else
		pr_err("Ignoring unknown SRSO option (%s).", str);

	return 0;
}
early_param("spec_rstack_overflow", srso_parse_cmdline);

#define SRSO_NOTICE "WARNING: See https://kernel.org/doc/html/latest/admin-guide/hw-vuln/srso.html for mitigation options."

static void __init srso_select_mitigation(void)
{
	bool has_microcode;

	if (!boot_cpu_has_bug(X86_BUG_SRSO) || cpu_mitigations_off())
		goto pred_cmd;

	/*
	 * The first check is for the kernel running as a guest in order
	 * for guests to verify whether IBPB is a viable mitigation.
	 */
	has_microcode = boot_cpu_has(X86_FEATURE_IBPB_BRTYPE) || cpu_has_ibpb_brtype_microcode();
	if (!has_microcode) {
		pr_warn("IBPB-extending microcode not applied!\n");
		pr_warn(SRSO_NOTICE);
	} else {
		/*
		 * Enable the synthetic (even if in a real CPUID leaf)
		 * flags for guests.
		 */
		setup_force_cpu_cap(X86_FEATURE_IBPB_BRTYPE);

		/*
		 * Zen1/2 with SMT off aren't vulnerable after the right
		 * IBPB microcode has been applied.
		 */
		if (boot_cpu_data.x86 < 0x19 && !cpu_smt_possible()) {
			setup_force_cpu_cap(X86_FEATURE_SRSO_NO);
			return;
		}
	}

	if (retbleed_mitigation == RETBLEED_MITIGATION_IBPB) {
		if (has_microcode) {
			pr_err("Retbleed IBPB mitigation enabled, using same for SRSO\n");
			srso_mitigation = SRSO_MITIGATION_IBPB;
			goto pred_cmd;
		}
	}

	switch (srso_cmd) {
	case SRSO_CMD_OFF:
		goto pred_cmd;

	case SRSO_CMD_MICROCODE:
		if (has_microcode) {
			srso_mitigation = SRSO_MITIGATION_MICROCODE;
			pr_warn(SRSO_NOTICE);
		}
		break;

	case SRSO_CMD_SAFE_RET:
		if (IS_ENABLED(CONFIG_CPU_SRSO)) {
			/*
			 * Enable the return thunk for generated code
			 * like ftrace, static_call, etc.
			 */
			setup_force_cpu_cap(X86_FEATURE_RETHUNK);
			setup_force_cpu_cap(X86_FEATURE_UNRET);

			if (boot_cpu_data.x86 == 0x19) {
				setup_force_cpu_cap(X86_FEATURE_SRSO_ALIAS);
				x86_return_thunk = srso_alias_return_thunk;
			} else {
				setup_force_cpu_cap(X86_FEATURE_SRSO);
				x86_return_thunk = srso_return_thunk;
			}
			srso_mitigation = SRSO_MITIGATION_SAFE_RET;
		} else {
			pr_err("WARNING: kernel not compiled with CPU_SRSO.\n");
			goto pred_cmd;
		}
		break;

	case SRSO_CMD_IBPB:
		if (IS_ENABLED(CONFIG_CPU_IBPB_ENTRY)) {
			if (has_microcode) {
				setup_force_cpu_cap(X86_FEATURE_ENTRY_IBPB);
				srso_mitigation = SRSO_MITIGATION_IBPB;
			}
		} else {
			pr_err("WARNING: kernel not compiled with CPU_IBPB_ENTRY.\n");
			goto pred_cmd;
		}
		break;

	case SRSO_CMD_IBPB_ON_VMEXIT:
		if (IS_ENABLED(CONFIG_CPU_SRSO)) {
			if (!boot_cpu_has(X86_FEATURE_ENTRY_IBPB) && has_microcode) {
				setup_force_cpu_cap(X86_FEATURE_IBPB_ON_VMEXIT);
				srso_mitigation = SRSO_MITIGATION_IBPB_ON_VMEXIT;
			}
		} else {
			pr_err("WARNING: kernel not compiled with CPU_SRSO.\n");
			goto pred_cmd;
                }
		break;

	default:
		break;
	}

	pr_info("%s%s\n", srso_strings[srso_mitigation], (has_microcode ? "" : ", no microcode"));

pred_cmd:
	if ((!boot_cpu_has_bug(X86_BUG_SRSO) || srso_cmd == SRSO_CMD_OFF) &&
	     boot_cpu_has(X86_FEATURE_SBPB))
		x86_pred_cmd = PRED_CMD_SBPB;
}

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
		return sysfs_emit(buf, "%s\n", L1TF_DEFAULT_MSG);

	if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_EPT_DISABLED ||
	    (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_NEVER &&
	     sched_smt_active())) {
		return sysfs_emit(buf, "%s; VMX: %s\n", L1TF_DEFAULT_MSG,
				  l1tf_vmx_states[l1tf_vmx_mitigation]);
	}

	return sysfs_emit(buf, "%s; VMX: %s, SMT %s\n", L1TF_DEFAULT_MSG,
			  l1tf_vmx_states[l1tf_vmx_mitigation],
			  sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t itlb_multihit_show_state(char *buf)
{
	if (!boot_cpu_has(X86_FEATURE_MSR_IA32_FEAT_CTL) ||
	    !boot_cpu_has(X86_FEATURE_VMX))
		return sysfs_emit(buf, "KVM: Mitigation: VMX unsupported\n");
	else if (!(cr4_read_shadow() & X86_CR4_VMXE))
		return sysfs_emit(buf, "KVM: Mitigation: VMX disabled\n");
	else if (itlb_multihit_kvm_mitigation)
		return sysfs_emit(buf, "KVM: Mitigation: Split huge pages\n");
	else
		return sysfs_emit(buf, "KVM: Vulnerable\n");
}
#else
static ssize_t l1tf_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", L1TF_DEFAULT_MSG);
}

static ssize_t itlb_multihit_show_state(char *buf)
{
	return sysfs_emit(buf, "Processor vulnerable\n");
}
#endif

static ssize_t mds_show_state(char *buf)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sysfs_emit(buf, "%s; SMT Host state unknown\n",
				  mds_strings[mds_mitigation]);
	}

	if (boot_cpu_has(X86_BUG_MSBDS_ONLY)) {
		return sysfs_emit(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
				  (mds_mitigation == MDS_MITIGATION_OFF ? "vulnerable" :
				   sched_smt_active() ? "mitigated" : "disabled"));
	}

	return sysfs_emit(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
			  sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t tsx_async_abort_show_state(char *buf)
{
	if ((taa_mitigation == TAA_MITIGATION_TSX_DISABLED) ||
	    (taa_mitigation == TAA_MITIGATION_OFF))
		return sysfs_emit(buf, "%s\n", taa_strings[taa_mitigation]);

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sysfs_emit(buf, "%s; SMT Host state unknown\n",
				  taa_strings[taa_mitigation]);
	}

	return sysfs_emit(buf, "%s; SMT %s\n", taa_strings[taa_mitigation],
			  sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t mmio_stale_data_show_state(char *buf)
{
	if (boot_cpu_has_bug(X86_BUG_MMIO_UNKNOWN))
		return sysfs_emit(buf, "Unknown: No mitigations\n");

	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return sysfs_emit(buf, "%s\n", mmio_strings[mmio_mitigation]);

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sysfs_emit(buf, "%s; SMT Host state unknown\n",
				  mmio_strings[mmio_mitigation]);
	}

	return sysfs_emit(buf, "%s; SMT %s\n", mmio_strings[mmio_mitigation],
			  sched_smt_active() ? "vulnerable" : "disabled");
}

static ssize_t rfds_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", rfds_strings[rfds_mitigation]);
}

static char *stibp_state(void)
{
	if (spectre_v2_in_eibrs_mode(spectre_v2_enabled) &&
	    !boot_cpu_has(X86_FEATURE_AUTOIBRS))
		return "";

	switch (spectre_v2_user_stibp) {
	case SPECTRE_V2_USER_NONE:
		return "; STIBP: disabled";
	case SPECTRE_V2_USER_STRICT:
		return "; STIBP: forced";
	case SPECTRE_V2_USER_STRICT_PREFERRED:
		return "; STIBP: always-on";
	case SPECTRE_V2_USER_PRCTL:
	case SPECTRE_V2_USER_SECCOMP:
		if (static_key_enabled(&switch_to_cond_stibp))
			return "; STIBP: conditional";
	}
	return "";
}

static char *ibpb_state(void)
{
	if (boot_cpu_has(X86_FEATURE_IBPB)) {
		if (static_key_enabled(&switch_mm_always_ibpb))
			return "; IBPB: always-on";
		if (static_key_enabled(&switch_mm_cond_ibpb))
			return "; IBPB: conditional";
		return "; IBPB: disabled";
	}
	return "";
}

static char *pbrsb_eibrs_state(void)
{
	if (boot_cpu_has_bug(X86_BUG_EIBRS_PBRSB)) {
		if (boot_cpu_has(X86_FEATURE_RSB_VMEXIT_LITE) ||
		    boot_cpu_has(X86_FEATURE_RSB_VMEXIT))
			return "; PBRSB-eIBRS: SW sequence";
		else
			return "; PBRSB-eIBRS: Vulnerable";
	} else {
		return "; PBRSB-eIBRS: Not affected";
	}
}

static const char * const spectre_bhi_state(void)
{
	if (!boot_cpu_has_bug(X86_BUG_BHI))
		return "; BHI: Not affected";
	else if  (boot_cpu_has(X86_FEATURE_CLEAR_BHB_HW))
		return "; BHI: BHI_DIS_S";
	else if  (boot_cpu_has(X86_FEATURE_CLEAR_BHB_LOOP))
		return "; BHI: SW loop, KVM: SW loop";
	else if (boot_cpu_has(X86_FEATURE_RETPOLINE) &&
		 !(x86_read_arch_cap_msr() & ARCH_CAP_RRSBA))
		return "; BHI: Retpoline";
	else if  (boot_cpu_has(X86_FEATURE_CLEAR_BHB_LOOP_ON_VMEXIT))
		return "; BHI: Syscall hardening, KVM: SW loop";

	return "; BHI: Vulnerable (Syscall hardening enabled)";
}

static ssize_t spectre_v2_show_state(char *buf)
{
	if (spectre_v2_enabled == SPECTRE_V2_LFENCE)
		return sysfs_emit(buf, "Vulnerable: LFENCE\n");

	if (spectre_v2_enabled == SPECTRE_V2_EIBRS && unprivileged_ebpf_enabled())
		return sysfs_emit(buf, "Vulnerable: eIBRS with unprivileged eBPF\n");

	if (sched_smt_active() && unprivileged_ebpf_enabled() &&
	    spectre_v2_enabled == SPECTRE_V2_EIBRS_LFENCE)
		return sysfs_emit(buf, "Vulnerable: eIBRS+LFENCE with unprivileged eBPF and SMT\n");

	return sysfs_emit(buf, "%s%s%s%s%s%s%s%s\n",
			  spectre_v2_strings[spectre_v2_enabled],
			  ibpb_state(),
			  boot_cpu_has(X86_FEATURE_USE_IBRS_FW) ? "; IBRS_FW" : "",
			  stibp_state(),
			  boot_cpu_has(X86_FEATURE_RSB_CTXSW) ? "; RSB filling" : "",
			  pbrsb_eibrs_state(),
			  spectre_bhi_state(),
			  /* this should always be at the end */
			  spectre_v2_module_string());
}

static ssize_t srbds_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", srbds_strings[srbds_mitigation]);
}

static ssize_t retbleed_show_state(char *buf)
{
	if (retbleed_mitigation == RETBLEED_MITIGATION_UNRET ||
	    retbleed_mitigation == RETBLEED_MITIGATION_IBPB) {
		if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
		    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
			return sysfs_emit(buf, "Vulnerable: untrained return thunk / IBPB on non-AMD based uarch\n");

		return sysfs_emit(buf, "%s; SMT %s\n", retbleed_strings[retbleed_mitigation],
				  !sched_smt_active() ? "disabled" :
				  spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT ||
				  spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT_PREFERRED ?
				  "enabled with STIBP protection" : "vulnerable");
	}

	return sysfs_emit(buf, "%s\n", retbleed_strings[retbleed_mitigation]);
}

static ssize_t gds_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", gds_strings[gds_mitigation]);
}

static ssize_t srso_show_state(char *buf)
{
	if (boot_cpu_has(X86_FEATURE_SRSO_NO))
		return sysfs_emit(buf, "Mitigation: SMT disabled\n");

	return sysfs_emit(buf, "%s%s\n",
			  srso_strings[srso_mitigation],
			  boot_cpu_has(X86_FEATURE_IBPB_BRTYPE) ? "" : ", no microcode");
}

static ssize_t cpu_show_common(struct device *dev, struct device_attribute *attr,
			       char *buf, unsigned int bug)
{
	if (!boot_cpu_has_bug(bug))
		return sysfs_emit(buf, "Not affected\n");

	switch (bug) {
	case X86_BUG_CPU_MELTDOWN:
		if (boot_cpu_has(X86_FEATURE_PTI))
			return sysfs_emit(buf, "Mitigation: PTI\n");

		if (hypervisor_is_type(X86_HYPER_XEN_PV))
			return sysfs_emit(buf, "Unknown (XEN PV detected, hypervisor mitigation required)\n");

		break;

	case X86_BUG_SPECTRE_V1:
		return sysfs_emit(buf, "%s\n", spectre_v1_strings[spectre_v1_mitigation]);

	case X86_BUG_SPECTRE_V2:
		return spectre_v2_show_state(buf);

	case X86_BUG_SPEC_STORE_BYPASS:
		return sysfs_emit(buf, "%s\n", ssb_strings[ssb_mode]);

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

	case X86_BUG_MMIO_STALE_DATA:
	case X86_BUG_MMIO_UNKNOWN:
		return mmio_stale_data_show_state(buf);

	case X86_BUG_RETBLEED:
		return retbleed_show_state(buf);

	case X86_BUG_GDS:
		return gds_show_state(buf);

	case X86_BUG_SRSO:
		return srso_show_state(buf);

	case X86_BUG_RFDS:
		return rfds_show_state(buf);

	default:
		break;
	}

	return sysfs_emit(buf, "Vulnerable\n");
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

ssize_t cpu_show_mmio_stale_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (boot_cpu_has_bug(X86_BUG_MMIO_UNKNOWN))
		return cpu_show_common(dev, attr, buf, X86_BUG_MMIO_UNKNOWN);
	else
		return cpu_show_common(dev, attr, buf, X86_BUG_MMIO_STALE_DATA);
}

ssize_t cpu_show_retbleed(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_RETBLEED);
}

ssize_t cpu_show_gds(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_GDS);
}

ssize_t cpu_show_spec_rstack_overflow(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SRSO);
}

ssize_t cpu_show_reg_file_data_sampling(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_RFDS);
}
#endif
