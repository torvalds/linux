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
#include <asm/cpu_device_id.h>
#include <asm/e820/api.h>
#include <asm/hypervisor.h>
#include <asm/tlbflush.h>
#include <asm/cpu.h>

#include "cpu.h"

/*
 * Speculation Vulnerability Handling
 *
 * Each vulnerability is handled with the following functions:
 *   <vuln>_select_mitigation() -- Selects a mitigation to use.  This should
 *				   take into account all relevant command line
 *				   options.
 *   <vuln>_update_mitigation() -- This is called after all vulnerabilities have
 *				   selected a mitigation, in case the selection
 *				   may want to change based on other choices
 *				   made.  This function is optional.
 *   <vuln>_apply_mitigation() -- Enable the selected mitigation.
 *
 * The compile-time mitigation in all cases should be AUTO.  An explicit
 * command-line option can override AUTO.  If no such option is
 * provided, <vuln>_select_mitigation() will override AUTO to the best
 * mitigation option.
 */

static void __init spectre_v1_select_mitigation(void);
static void __init spectre_v1_apply_mitigation(void);
static void __init spectre_v2_select_mitigation(void);
static void __init spectre_v2_update_mitigation(void);
static void __init spectre_v2_apply_mitigation(void);
static void __init retbleed_select_mitigation(void);
static void __init retbleed_update_mitigation(void);
static void __init retbleed_apply_mitigation(void);
static void __init spectre_v2_user_select_mitigation(void);
static void __init spectre_v2_user_update_mitigation(void);
static void __init spectre_v2_user_apply_mitigation(void);
static void __init ssb_select_mitigation(void);
static void __init ssb_apply_mitigation(void);
static void __init l1tf_select_mitigation(void);
static void __init l1tf_apply_mitigation(void);
static void __init mds_select_mitigation(void);
static void __init mds_update_mitigation(void);
static void __init mds_apply_mitigation(void);
static void __init taa_select_mitigation(void);
static void __init taa_update_mitigation(void);
static void __init taa_apply_mitigation(void);
static void __init mmio_select_mitigation(void);
static void __init mmio_update_mitigation(void);
static void __init mmio_apply_mitigation(void);
static void __init rfds_select_mitigation(void);
static void __init rfds_update_mitigation(void);
static void __init rfds_apply_mitigation(void);
static void __init srbds_select_mitigation(void);
static void __init srbds_apply_mitigation(void);
static void __init l1d_flush_select_mitigation(void);
static void __init srso_select_mitigation(void);
static void __init srso_update_mitigation(void);
static void __init srso_apply_mitigation(void);
static void __init gds_select_mitigation(void);
static void __init gds_apply_mitigation(void);
static void __init bhi_select_mitigation(void);
static void __init bhi_update_mitigation(void);
static void __init bhi_apply_mitigation(void);
static void __init its_select_mitigation(void);
static void __init its_update_mitigation(void);
static void __init its_apply_mitigation(void);
static void __init tsa_select_mitigation(void);
static void __init tsa_apply_mitigation(void);
static void __init vmscape_select_mitigation(void);
static void __init vmscape_update_mitigation(void);
static void __init vmscape_apply_mitigation(void);

/* The base value of the SPEC_CTRL MSR without task-specific bits set */
u64 x86_spec_ctrl_base;
EXPORT_SYMBOL_GPL(x86_spec_ctrl_base);

/* The current value of the SPEC_CTRL MSR with task-specific bits set */
DEFINE_PER_CPU(u64, x86_spec_ctrl_current);
EXPORT_PER_CPU_SYMBOL_GPL(x86_spec_ctrl_current);

/*
 * Set when the CPU has run a potentially malicious guest. An IBPB will
 * be needed to before running userspace. That IBPB will flush the branch
 * predictor content.
 */
DEFINE_PER_CPU(bool, x86_ibpb_exit_to_user);
EXPORT_PER_CPU_SYMBOL_GPL(x86_ibpb_exit_to_user);

u64 x86_pred_cmd __ro_after_init = PRED_CMD_IBPB;

static u64 __ro_after_init x86_arch_cap_msr;

static DEFINE_MUTEX(spec_ctrl_mutex);

void (*x86_return_thunk)(void) __ro_after_init = __x86_return_thunk;

static void __init set_return_thunk(void *thunk)
{
	x86_return_thunk = thunk;

	pr_info("active return thunk: %ps\n", thunk);
}

/* Update SPEC_CTRL MSR and its cached copy unconditionally */
static void update_spec_ctrl(u64 val)
{
	this_cpu_write(x86_spec_ctrl_current, val);
	wrmsrq(MSR_IA32_SPEC_CTRL, val);
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
		wrmsrq(MSR_IA32_SPEC_CTRL, val);
}

noinstr u64 spec_ctrl_current(void)
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

/* Control IBPB on vCPU load */
DEFINE_STATIC_KEY_FALSE(switch_vcpu_ibpb);
EXPORT_SYMBOL_GPL(switch_vcpu_ibpb);

/* Control CPU buffer clear before idling (halt, mwait) */
DEFINE_STATIC_KEY_FALSE(cpu_buf_idle_clear);
EXPORT_SYMBOL_GPL(cpu_buf_idle_clear);

/*
 * Controls whether l1d flush based mitigations are enabled,
 * based on hw features and admin setting via boot parameter
 * defaults to false
 */
DEFINE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);

/*
 * Controls CPU Fill buffer clear before VMenter. This is a subset of
 * X86_FEATURE_CLEAR_CPU_BUF, and should only be enabled when KVM-only
 * mitigation is required.
 */
DEFINE_STATIC_KEY_FALSE(cpu_buf_vm_clear);
EXPORT_SYMBOL_GPL(cpu_buf_vm_clear);

#undef pr_fmt
#define pr_fmt(fmt)	"mitigations: " fmt

static void __init cpu_print_attack_vectors(void)
{
	pr_info("Enabled attack vectors: ");

	if (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL))
		pr_cont("user_kernel, ");

	if (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER))
		pr_cont("user_user, ");

	if (cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST))
		pr_cont("guest_host, ");

	if (cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST))
		pr_cont("guest_guest, ");

	pr_cont("SMT mitigations: ");

	switch (smt_mitigations) {
	case SMT_MITIGATIONS_OFF:
		pr_cont("off\n");
		break;
	case SMT_MITIGATIONS_AUTO:
		pr_cont("auto\n");
		break;
	case SMT_MITIGATIONS_ON:
		pr_cont("on\n");
	}
}

void __init cpu_select_mitigations(void)
{
	/*
	 * Read the SPEC_CTRL MSR to account for reserved bits which may
	 * have unknown values. AMD64_LS_CFG MSR is cached in the early AMD
	 * init code as it is not enumerated and depends on the family.
	 */
	if (cpu_feature_enabled(X86_FEATURE_MSR_SPEC_CTRL)) {
		rdmsrq(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);

		/*
		 * Previously running kernel (kexec), may have some controls
		 * turned ON. Clear them and let the mitigations setup below
		 * rediscover them based on configuration.
		 */
		x86_spec_ctrl_base &= ~SPEC_CTRL_MITIGATIONS_MASK;
	}

	x86_arch_cap_msr = x86_read_arch_cap_msr();

	cpu_print_attack_vectors();

	/* Select the proper CPU mitigations before patching alternatives: */
	spectre_v1_select_mitigation();
	spectre_v2_select_mitigation();
	retbleed_select_mitigation();
	spectre_v2_user_select_mitigation();
	ssb_select_mitigation();
	l1tf_select_mitigation();
	mds_select_mitigation();
	taa_select_mitigation();
	mmio_select_mitigation();
	rfds_select_mitigation();
	srbds_select_mitigation();
	l1d_flush_select_mitigation();
	srso_select_mitigation();
	gds_select_mitigation();
	its_select_mitigation();
	bhi_select_mitigation();
	tsa_select_mitigation();
	vmscape_select_mitigation();

	/*
	 * After mitigations are selected, some may need to update their
	 * choices.
	 */
	spectre_v2_update_mitigation();
	/*
	 * retbleed_update_mitigation() relies on the state set by
	 * spectre_v2_update_mitigation(); specifically it wants to know about
	 * spectre_v2=ibrs.
	 */
	retbleed_update_mitigation();
	/*
	 * its_update_mitigation() depends on spectre_v2_update_mitigation()
	 * and retbleed_update_mitigation().
	 */
	its_update_mitigation();

	/*
	 * spectre_v2_user_update_mitigation() depends on
	 * retbleed_update_mitigation(), specifically the STIBP
	 * selection is forced for UNRET or IBPB.
	 */
	spectre_v2_user_update_mitigation();
	mds_update_mitigation();
	taa_update_mitigation();
	mmio_update_mitigation();
	rfds_update_mitigation();
	bhi_update_mitigation();
	/* srso_update_mitigation() depends on retbleed_update_mitigation(). */
	srso_update_mitigation();
	vmscape_update_mitigation();

	spectre_v1_apply_mitigation();
	spectre_v2_apply_mitigation();
	retbleed_apply_mitigation();
	spectre_v2_user_apply_mitigation();
	ssb_apply_mitigation();
	l1tf_apply_mitigation();
	mds_apply_mitigation();
	taa_apply_mitigation();
	mmio_apply_mitigation();
	rfds_apply_mitigation();
	srbds_apply_mitigation();
	srso_apply_mitigation();
	gds_apply_mitigation();
	its_apply_mitigation();
	bhi_apply_mitigation();
	tsa_apply_mitigation();
	vmscape_apply_mitigation();
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
		wrmsrq(MSR_AMD64_VIRT_SPEC_CTRL, SPEC_CTRL_SSBD);
	else if (boot_cpu_has(X86_FEATURE_LS_CFG_SSBD))
		wrmsrq(MSR_AMD64_LS_CFG, msrval);
}

#undef pr_fmt
#define pr_fmt(fmt)	"MDS: " fmt

/*
 * Returns true if vulnerability should be mitigated based on the
 * selected attack vector controls.
 *
 * See Documentation/admin-guide/hw-vuln/attack_vector_controls.rst
 */
static bool __init should_mitigate_vuln(unsigned int bug)
{
	switch (bug) {
	/*
	 * The only runtime-selected spectre_v1 mitigations in the kernel are
	 * related to SWAPGS protection on kernel entry.  Therefore, protection
	 * is only required for the user->kernel attack vector.
	 */
	case X86_BUG_SPECTRE_V1:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL);

	case X86_BUG_SPECTRE_V2:
	case X86_BUG_RETBLEED:
	case X86_BUG_L1TF:
	case X86_BUG_ITS:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST);

	case X86_BUG_SPECTRE_V2_USER:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST);

	/*
	 * All the vulnerabilities below allow potentially leaking data
	 * across address spaces.  Therefore, mitigation is required for
	 * any of these 4 attack vectors.
	 */
	case X86_BUG_MDS:
	case X86_BUG_TAA:
	case X86_BUG_MMIO_STALE_DATA:
	case X86_BUG_RFDS:
	case X86_BUG_SRBDS:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST);

	case X86_BUG_GDS:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER) ||
		       cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST) ||
		       (smt_mitigations != SMT_MITIGATIONS_OFF);

	case X86_BUG_SPEC_STORE_BYPASS:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER);

	case X86_BUG_VMSCAPE:
		return cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST);

	default:
		WARN(1, "Unknown bug %x\n", bug);
		return false;
	}
}

/* Default mitigation for MDS-affected CPUs */
static enum mds_mitigations mds_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_MDS) ? MDS_MITIGATION_AUTO : MDS_MITIGATION_OFF;
static bool mds_nosmt __ro_after_init = false;

static const char * const mds_strings[] = {
	[MDS_MITIGATION_OFF]	= "Vulnerable",
	[MDS_MITIGATION_FULL]	= "Mitigation: Clear CPU buffers",
	[MDS_MITIGATION_VMWERV]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
};

enum taa_mitigations {
	TAA_MITIGATION_OFF,
	TAA_MITIGATION_AUTO,
	TAA_MITIGATION_UCODE_NEEDED,
	TAA_MITIGATION_VERW,
	TAA_MITIGATION_TSX_DISABLED,
};

/* Default mitigation for TAA-affected CPUs */
static enum taa_mitigations taa_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_TAA) ? TAA_MITIGATION_AUTO : TAA_MITIGATION_OFF;

enum mmio_mitigations {
	MMIO_MITIGATION_OFF,
	MMIO_MITIGATION_AUTO,
	MMIO_MITIGATION_UCODE_NEEDED,
	MMIO_MITIGATION_VERW,
};

/* Default mitigation for Processor MMIO Stale Data vulnerabilities */
static enum mmio_mitigations mmio_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_MMIO_STALE_DATA) ?	MMIO_MITIGATION_AUTO : MMIO_MITIGATION_OFF;

enum rfds_mitigations {
	RFDS_MITIGATION_OFF,
	RFDS_MITIGATION_AUTO,
	RFDS_MITIGATION_VERW,
	RFDS_MITIGATION_UCODE_NEEDED,
};

/* Default mitigation for Register File Data Sampling */
static enum rfds_mitigations rfds_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_RFDS) ? RFDS_MITIGATION_AUTO : RFDS_MITIGATION_OFF;

/*
 * Set if any of MDS/TAA/MMIO/RFDS are going to enable VERW clearing
 * through X86_FEATURE_CLEAR_CPU_BUF on kernel and guest entry.
 */
static bool verw_clear_cpu_buf_mitigation_selected __ro_after_init;

static void __init mds_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MDS)) {
		mds_mitigation = MDS_MITIGATION_OFF;
		return;
	}

	if (mds_mitigation == MDS_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_MDS))
			mds_mitigation = MDS_MITIGATION_FULL;
		else
			mds_mitigation = MDS_MITIGATION_OFF;
	}

	if (mds_mitigation == MDS_MITIGATION_OFF)
		return;

	verw_clear_cpu_buf_mitigation_selected = true;
}

static void __init mds_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MDS))
		return;

	/* If TAA, MMIO, or RFDS are being mitigated, MDS gets mitigated too. */
	if (verw_clear_cpu_buf_mitigation_selected)
		mds_mitigation = MDS_MITIGATION_FULL;

	if (mds_mitigation == MDS_MITIGATION_FULL) {
		if (!boot_cpu_has(X86_FEATURE_MD_CLEAR))
			mds_mitigation = MDS_MITIGATION_VMWERV;
	}

	pr_info("%s\n", mds_strings[mds_mitigation]);
}

static void __init mds_apply_mitigation(void)
{
	if (mds_mitigation == MDS_MITIGATION_FULL ||
	    mds_mitigation == MDS_MITIGATION_VMWERV) {
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
		if (!boot_cpu_has(X86_BUG_MSBDS_ONLY) &&
		    (mds_nosmt || smt_mitigations == SMT_MITIGATIONS_ON))
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

static bool taa_nosmt __ro_after_init;

static const char * const taa_strings[] = {
	[TAA_MITIGATION_OFF]		= "Vulnerable",
	[TAA_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[TAA_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
	[TAA_MITIGATION_TSX_DISABLED]	= "Mitigation: TSX disabled",
};

static bool __init taa_vulnerable(void)
{
	return boot_cpu_has_bug(X86_BUG_TAA) && boot_cpu_has(X86_FEATURE_RTM);
}

static void __init taa_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_TAA)) {
		taa_mitigation = TAA_MITIGATION_OFF;
		return;
	}

	/* TSX previously disabled by tsx=off */
	if (!boot_cpu_has(X86_FEATURE_RTM)) {
		taa_mitigation = TAA_MITIGATION_TSX_DISABLED;
		return;
	}

	/* Microcode will be checked in taa_update_mitigation(). */
	if (taa_mitigation == TAA_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_TAA))
			taa_mitigation = TAA_MITIGATION_VERW;
		else
			taa_mitigation = TAA_MITIGATION_OFF;
	}

	if (taa_mitigation != TAA_MITIGATION_OFF)
		verw_clear_cpu_buf_mitigation_selected = true;
}

static void __init taa_update_mitigation(void)
{
	if (!taa_vulnerable())
		return;

	if (verw_clear_cpu_buf_mitigation_selected)
		taa_mitigation = TAA_MITIGATION_VERW;

	if (taa_mitigation == TAA_MITIGATION_VERW) {
		/* Check if the requisite ucode is available. */
		if (!boot_cpu_has(X86_FEATURE_MD_CLEAR))
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
		if ((x86_arch_cap_msr & ARCH_CAP_MDS_NO) &&
		   !(x86_arch_cap_msr & ARCH_CAP_TSX_CTRL_MSR))
			taa_mitigation = TAA_MITIGATION_UCODE_NEEDED;
	}

	pr_info("%s\n", taa_strings[taa_mitigation]);
}

static void __init taa_apply_mitigation(void)
{
	if (taa_mitigation == TAA_MITIGATION_VERW ||
	    taa_mitigation == TAA_MITIGATION_UCODE_NEEDED) {
		/*
		 * TSX is enabled, select alternate mitigation for TAA which is
		 * the same as MDS. Enable MDS static branch to clear CPU buffers.
		 *
		 * For guests that can't determine whether the correct microcode is
		 * present on host, enable the mitigation for UCODE_NEEDED as well.
		 */
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);

		if (taa_nosmt || smt_mitigations == SMT_MITIGATIONS_ON)
			cpu_smt_disable(false);
	}
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

static bool mmio_nosmt __ro_after_init = false;

static const char * const mmio_strings[] = {
	[MMIO_MITIGATION_OFF]		= "Vulnerable",
	[MMIO_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[MMIO_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
};

static void __init mmio_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA)) {
		mmio_mitigation = MMIO_MITIGATION_OFF;
		return;
	}

	/* Microcode will be checked in mmio_update_mitigation(). */
	if (mmio_mitigation == MMIO_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_MMIO_STALE_DATA))
			mmio_mitigation = MMIO_MITIGATION_VERW;
		else
			mmio_mitigation = MMIO_MITIGATION_OFF;
	}

	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return;

	/*
	 * Enable CPU buffer clear mitigation for host and VMM, if also affected
	 * by MDS or TAA.
	 */
	if (boot_cpu_has_bug(X86_BUG_MDS) || taa_vulnerable())
		verw_clear_cpu_buf_mitigation_selected = true;
}

static void __init mmio_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA))
		return;

	if (verw_clear_cpu_buf_mitigation_selected)
		mmio_mitigation = MMIO_MITIGATION_VERW;

	if (mmio_mitigation == MMIO_MITIGATION_VERW) {
		/*
		 * Check if the system has the right microcode.
		 *
		 * CPU Fill buffer clear mitigation is enumerated by either an explicit
		 * FB_CLEAR or by the presence of both MD_CLEAR and L1D_FLUSH on MDS
		 * affected systems.
		 */
		if (!((x86_arch_cap_msr & ARCH_CAP_FB_CLEAR) ||
		      (boot_cpu_has(X86_FEATURE_MD_CLEAR) &&
		       boot_cpu_has(X86_FEATURE_FLUSH_L1D) &&
		     !(x86_arch_cap_msr & ARCH_CAP_MDS_NO))))
			mmio_mitigation = MMIO_MITIGATION_UCODE_NEEDED;
	}

	pr_info("%s\n", mmio_strings[mmio_mitigation]);
}

static void __init mmio_apply_mitigation(void)
{
	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return;

	/*
	 * Only enable the VMM mitigation if the CPU buffer clear mitigation is
	 * not being used.
	 */
	if (verw_clear_cpu_buf_mitigation_selected) {
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
		static_branch_disable(&cpu_buf_vm_clear);
	} else {
		static_branch_enable(&cpu_buf_vm_clear);
	}

	/*
	 * If Processor-MMIO-Stale-Data bug is present and Fill Buffer data can
	 * be propagated to uncore buffers, clearing the Fill buffers on idle
	 * is required irrespective of SMT state.
	 */
	if (!(x86_arch_cap_msr & ARCH_CAP_FBSDP_NO))
		static_branch_enable(&cpu_buf_idle_clear);

	if (mmio_nosmt || smt_mitigations == SMT_MITIGATIONS_ON)
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

static const char * const rfds_strings[] = {
	[RFDS_MITIGATION_OFF]			= "Vulnerable",
	[RFDS_MITIGATION_VERW]			= "Mitigation: Clear Register File",
	[RFDS_MITIGATION_UCODE_NEEDED]		= "Vulnerable: No microcode",
};

static inline bool __init verw_clears_cpu_reg_file(void)
{
	return (x86_arch_cap_msr & ARCH_CAP_RFDS_CLEAR);
}

static void __init rfds_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_RFDS)) {
		rfds_mitigation = RFDS_MITIGATION_OFF;
		return;
	}

	if (rfds_mitigation == RFDS_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_RFDS))
			rfds_mitigation = RFDS_MITIGATION_VERW;
		else
			rfds_mitigation = RFDS_MITIGATION_OFF;
	}

	if (rfds_mitigation == RFDS_MITIGATION_OFF)
		return;

	if (verw_clears_cpu_reg_file())
		verw_clear_cpu_buf_mitigation_selected = true;
}

static void __init rfds_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_RFDS))
		return;

	if (verw_clear_cpu_buf_mitigation_selected)
		rfds_mitigation = RFDS_MITIGATION_VERW;

	if (rfds_mitigation == RFDS_MITIGATION_VERW) {
		if (!verw_clears_cpu_reg_file())
			rfds_mitigation = RFDS_MITIGATION_UCODE_NEEDED;
	}

	pr_info("%s\n", rfds_strings[rfds_mitigation]);
}

static void __init rfds_apply_mitigation(void)
{
	if (rfds_mitigation == RFDS_MITIGATION_VERW)
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
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
#define pr_fmt(fmt)	"SRBDS: " fmt

enum srbds_mitigations {
	SRBDS_MITIGATION_OFF,
	SRBDS_MITIGATION_AUTO,
	SRBDS_MITIGATION_UCODE_NEEDED,
	SRBDS_MITIGATION_FULL,
	SRBDS_MITIGATION_TSX_OFF,
	SRBDS_MITIGATION_HYPERVISOR,
};

static enum srbds_mitigations srbds_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_SRBDS) ? SRBDS_MITIGATION_AUTO : SRBDS_MITIGATION_OFF;

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

	rdmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

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

	wrmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
}

static void __init srbds_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SRBDS)) {
		srbds_mitigation = SRBDS_MITIGATION_OFF;
		return;
	}

	if (srbds_mitigation == SRBDS_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_SRBDS))
			srbds_mitigation = SRBDS_MITIGATION_FULL;
		else {
			srbds_mitigation = SRBDS_MITIGATION_OFF;
			return;
		}
	}

	/*
	 * Check to see if this is one of the MDS_NO systems supporting TSX that
	 * are only exposed to SRBDS when TSX is enabled or when CPU is affected
	 * by Processor MMIO Stale Data vulnerability.
	 */
	if ((x86_arch_cap_msr & ARCH_CAP_MDS_NO) && !boot_cpu_has(X86_FEATURE_RTM) &&
	    !boot_cpu_has_bug(X86_BUG_MMIO_STALE_DATA))
		srbds_mitigation = SRBDS_MITIGATION_TSX_OFF;
	else if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		srbds_mitigation = SRBDS_MITIGATION_HYPERVISOR;
	else if (!boot_cpu_has(X86_FEATURE_SRBDS_CTRL))
		srbds_mitigation = SRBDS_MITIGATION_UCODE_NEEDED;
	else if (srbds_off)
		srbds_mitigation = SRBDS_MITIGATION_OFF;

	pr_info("%s\n", srbds_strings[srbds_mitigation]);
}

static void __init srbds_apply_mitigation(void)
{
	update_srbds_msr();
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
	GDS_MITIGATION_AUTO,
	GDS_MITIGATION_UCODE_NEEDED,
	GDS_MITIGATION_FORCE,
	GDS_MITIGATION_FULL,
	GDS_MITIGATION_FULL_LOCKED,
	GDS_MITIGATION_HYPERVISOR,
};

static enum gds_mitigations gds_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_GDS) ? GDS_MITIGATION_AUTO : GDS_MITIGATION_OFF;

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
		rdmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl |= GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_FULL_LOCKED:
		/*
		 * The LOCKED state comes from the boot CPU. APs might not have
		 * the same state. Make sure the mitigation is enabled on all
		 * CPUs.
		 */
	case GDS_MITIGATION_FULL:
		rdmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl &= ~GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_FORCE:
	case GDS_MITIGATION_UCODE_NEEDED:
	case GDS_MITIGATION_HYPERVISOR:
	case GDS_MITIGATION_AUTO:
		return;
	}

	wrmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

	/*
	 * Check to make sure that the WRMSR value was not ignored. Writes to
	 * GDS_MITG_DIS will be ignored if this processor is locked but the boot
	 * processor was not.
	 */
	rdmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl_after);
	WARN_ON_ONCE(mcu_ctrl != mcu_ctrl_after);
}

static void __init gds_select_mitigation(void)
{
	u64 mcu_ctrl;

	if (!boot_cpu_has_bug(X86_BUG_GDS))
		return;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		gds_mitigation = GDS_MITIGATION_HYPERVISOR;
		return;
	}

	/* Will verify below that mitigation _can_ be disabled */
	if (gds_mitigation == GDS_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_GDS))
			gds_mitigation = GDS_MITIGATION_FULL;
		else
			gds_mitigation = GDS_MITIGATION_OFF;
	}

	/* No microcode */
	if (!(x86_arch_cap_msr & ARCH_CAP_GDS_CTRL)) {
		if (gds_mitigation != GDS_MITIGATION_FORCE)
			gds_mitigation = GDS_MITIGATION_UCODE_NEEDED;
		return;
	}

	/* Microcode has mitigation, use it */
	if (gds_mitigation == GDS_MITIGATION_FORCE)
		gds_mitigation = GDS_MITIGATION_FULL;

	rdmsrq(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
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
}

static void __init gds_apply_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_GDS))
		return;

	/* Microcode is present */
	if (x86_arch_cap_msr & ARCH_CAP_GDS_CTRL)
		update_gds_msr();
	else if (gds_mitigation == GDS_MITIGATION_FORCE) {
		/*
		 * This only needs to be done on the boot CPU so do it
		 * here rather than in update_gds_msr()
		 */
		setup_clear_cpu_cap(X86_FEATURE_AVX);
		pr_warn("Microcode update needed! Disabling AVX as mitigation.\n");
	}

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
	IS_ENABLED(CONFIG_MITIGATION_SPECTRE_V1) ?
		SPECTRE_V1_MITIGATION_AUTO : SPECTRE_V1_MITIGATION_NONE;

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
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V1))
		spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;

	if (!should_mitigate_vuln(X86_BUG_SPECTRE_V1))
		spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;
}

static void __init spectre_v1_apply_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V1))
		return;

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

enum spectre_v2_mitigation spectre_v2_enabled __ro_after_init = SPECTRE_V2_NONE;

/* Depends on spectre_v2 mitigation selected already */
static inline bool cdt_possible(enum spectre_v2_mitigation mode)
{
	if (!IS_ENABLED(CONFIG_MITIGATION_CALL_DEPTH_TRACKING) ||
	    !IS_ENABLED(CONFIG_MITIGATION_RETPOLINE))
		return false;

	if (mode == SPECTRE_V2_RETPOLINE ||
	    mode == SPECTRE_V2_EIBRS_RETPOLINE)
		return true;

	return false;
}

#undef pr_fmt
#define pr_fmt(fmt)     "RETBleed: " fmt

enum its_mitigation {
	ITS_MITIGATION_OFF,
	ITS_MITIGATION_AUTO,
	ITS_MITIGATION_VMEXIT_ONLY,
	ITS_MITIGATION_ALIGNED_THUNKS,
	ITS_MITIGATION_RETPOLINE_STUFF,
};

static enum its_mitigation its_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_ITS) ? ITS_MITIGATION_AUTO : ITS_MITIGATION_OFF;

enum retbleed_mitigation {
	RETBLEED_MITIGATION_NONE,
	RETBLEED_MITIGATION_AUTO,
	RETBLEED_MITIGATION_UNRET,
	RETBLEED_MITIGATION_IBPB,
	RETBLEED_MITIGATION_IBRS,
	RETBLEED_MITIGATION_EIBRS,
	RETBLEED_MITIGATION_STUFF,
};

static const char * const retbleed_strings[] = {
	[RETBLEED_MITIGATION_NONE]	= "Vulnerable",
	[RETBLEED_MITIGATION_UNRET]	= "Mitigation: untrained return thunk",
	[RETBLEED_MITIGATION_IBPB]	= "Mitigation: IBPB",
	[RETBLEED_MITIGATION_IBRS]	= "Mitigation: IBRS",
	[RETBLEED_MITIGATION_EIBRS]	= "Mitigation: Enhanced IBRS",
	[RETBLEED_MITIGATION_STUFF]	= "Mitigation: Stuffing",
};

static enum retbleed_mitigation retbleed_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_RETBLEED) ? RETBLEED_MITIGATION_AUTO : RETBLEED_MITIGATION_NONE;

static int __ro_after_init retbleed_nosmt = false;

enum srso_mitigation {
	SRSO_MITIGATION_NONE,
	SRSO_MITIGATION_AUTO,
	SRSO_MITIGATION_UCODE_NEEDED,
	SRSO_MITIGATION_SAFE_RET_UCODE_NEEDED,
	SRSO_MITIGATION_MICROCODE,
	SRSO_MITIGATION_NOSMT,
	SRSO_MITIGATION_SAFE_RET,
	SRSO_MITIGATION_IBPB,
	SRSO_MITIGATION_IBPB_ON_VMEXIT,
	SRSO_MITIGATION_BP_SPEC_REDUCE,
};

static enum srso_mitigation srso_mitigation __ro_after_init = SRSO_MITIGATION_AUTO;

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
			retbleed_mitigation = RETBLEED_MITIGATION_NONE;
		} else if (!strcmp(str, "auto")) {
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
		} else if (!strcmp(str, "unret")) {
			retbleed_mitigation = RETBLEED_MITIGATION_UNRET;
		} else if (!strcmp(str, "ibpb")) {
			retbleed_mitigation = RETBLEED_MITIGATION_IBPB;
		} else if (!strcmp(str, "stuff")) {
			retbleed_mitigation = RETBLEED_MITIGATION_STUFF;
		} else if (!strcmp(str, "nosmt")) {
			retbleed_nosmt = true;
		} else if (!strcmp(str, "force")) {
			setup_force_cpu_bug(X86_BUG_RETBLEED);
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
	if (!boot_cpu_has_bug(X86_BUG_RETBLEED)) {
		retbleed_mitigation = RETBLEED_MITIGATION_NONE;
		return;
	}

	switch (retbleed_mitigation) {
	case RETBLEED_MITIGATION_UNRET:
		if (!IS_ENABLED(CONFIG_MITIGATION_UNRET_ENTRY)) {
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
			pr_err("WARNING: kernel not compiled with MITIGATION_UNRET_ENTRY.\n");
		}
		break;
	case RETBLEED_MITIGATION_IBPB:
		if (!boot_cpu_has(X86_FEATURE_IBPB)) {
			pr_err("WARNING: CPU does not support IBPB.\n");
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
		} else if (!IS_ENABLED(CONFIG_MITIGATION_IBPB_ENTRY)) {
			pr_err("WARNING: kernel not compiled with MITIGATION_IBPB_ENTRY.\n");
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
		}
		break;
	case RETBLEED_MITIGATION_STUFF:
		if (!IS_ENABLED(CONFIG_MITIGATION_CALL_DEPTH_TRACKING)) {
			pr_err("WARNING: kernel not compiled with MITIGATION_CALL_DEPTH_TRACKING.\n");
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
		} else if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
			pr_err("WARNING: retbleed=stuff only supported for Intel CPUs.\n");
			retbleed_mitigation = RETBLEED_MITIGATION_AUTO;
		}
		break;
	default:
		break;
	}

	if (retbleed_mitigation != RETBLEED_MITIGATION_AUTO)
		return;

	if (!should_mitigate_vuln(X86_BUG_RETBLEED)) {
		retbleed_mitigation = RETBLEED_MITIGATION_NONE;
		return;
	}

	/* Intel mitigation selected in retbleed_update_mitigation() */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON) {
		if (IS_ENABLED(CONFIG_MITIGATION_UNRET_ENTRY))
			retbleed_mitigation = RETBLEED_MITIGATION_UNRET;
		else if (IS_ENABLED(CONFIG_MITIGATION_IBPB_ENTRY) &&
			 boot_cpu_has(X86_FEATURE_IBPB))
			retbleed_mitigation = RETBLEED_MITIGATION_IBPB;
		else
			retbleed_mitigation = RETBLEED_MITIGATION_NONE;
	} else if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
		/* Final mitigation depends on spectre-v2 selection */
		if (boot_cpu_has(X86_FEATURE_IBRS_ENHANCED))
			retbleed_mitigation = RETBLEED_MITIGATION_EIBRS;
		else if (boot_cpu_has(X86_FEATURE_IBRS))
			retbleed_mitigation = RETBLEED_MITIGATION_IBRS;
		else
			retbleed_mitigation = RETBLEED_MITIGATION_NONE;
	}
}

static void __init retbleed_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_RETBLEED))
		return;

	 /* ITS can also enable stuffing */
	if (its_mitigation == ITS_MITIGATION_RETPOLINE_STUFF)
		retbleed_mitigation = RETBLEED_MITIGATION_STUFF;

	/* If SRSO is using IBPB, that works for retbleed too */
	if (srso_mitigation == SRSO_MITIGATION_IBPB)
		retbleed_mitigation = RETBLEED_MITIGATION_IBPB;

	if (retbleed_mitigation == RETBLEED_MITIGATION_STUFF &&
	    !cdt_possible(spectre_v2_enabled)) {
		pr_err("WARNING: retbleed=stuff depends on retpoline\n");
		retbleed_mitigation = RETBLEED_MITIGATION_NONE;
	}

	/*
	 * Let IBRS trump all on Intel without affecting the effects of the
	 * retbleed= cmdline option except for call depth based stuffing
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
			if (retbleed_mitigation != RETBLEED_MITIGATION_STUFF) {
				if (retbleed_mitigation != RETBLEED_MITIGATION_NONE)
					pr_err(RETBLEED_INTEL_MSG);

				retbleed_mitigation = RETBLEED_MITIGATION_NONE;
			}
		}
	}

	pr_info("%s\n", retbleed_strings[retbleed_mitigation]);
}

static void __init retbleed_apply_mitigation(void)
{
	bool mitigate_smt = false;

	switch (retbleed_mitigation) {
	case RETBLEED_MITIGATION_NONE:
		return;

	case RETBLEED_MITIGATION_UNRET:
		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		setup_force_cpu_cap(X86_FEATURE_UNRET);

		set_return_thunk(retbleed_return_thunk);

		if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
		    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
			pr_err(RETBLEED_UNTRAIN_MSG);

		mitigate_smt = true;
		break;

	case RETBLEED_MITIGATION_IBPB:
		setup_force_cpu_cap(X86_FEATURE_ENTRY_IBPB);
		setup_force_cpu_cap(X86_FEATURE_IBPB_ON_VMEXIT);
		mitigate_smt = true;

		/*
		 * IBPB on entry already obviates the need for
		 * software-based untraining so clear those in case some
		 * other mitigation like SRSO has selected them.
		 */
		setup_clear_cpu_cap(X86_FEATURE_UNRET);
		setup_clear_cpu_cap(X86_FEATURE_RETHUNK);

		/*
		 * There is no need for RSB filling: write_ibpb() ensures
		 * all predictions, including the RSB, are invalidated,
		 * regardless of IBPB implementation.
		 */
		setup_clear_cpu_cap(X86_FEATURE_RSB_VMEXIT);

		break;

	case RETBLEED_MITIGATION_STUFF:
		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		setup_force_cpu_cap(X86_FEATURE_CALL_DEPTH);

		set_return_thunk(call_depth_return_thunk);
		break;

	default:
		break;
	}

	if (mitigate_smt && !boot_cpu_has(X86_FEATURE_STIBP) &&
	    (retbleed_nosmt || smt_mitigations == SMT_MITIGATIONS_ON))
		cpu_smt_disable(false);
}

#undef pr_fmt
#define pr_fmt(fmt)     "ITS: " fmt

static const char * const its_strings[] = {
	[ITS_MITIGATION_OFF]			= "Vulnerable",
	[ITS_MITIGATION_VMEXIT_ONLY]		= "Mitigation: Vulnerable, KVM: Not affected",
	[ITS_MITIGATION_ALIGNED_THUNKS]		= "Mitigation: Aligned branch/return thunks",
	[ITS_MITIGATION_RETPOLINE_STUFF]	= "Mitigation: Retpolines, Stuffing RSB",
};

static int __init its_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!IS_ENABLED(CONFIG_MITIGATION_ITS)) {
		pr_err("Mitigation disabled at compile time, ignoring option (%s)", str);
		return 0;
	}

	if (!strcmp(str, "off")) {
		its_mitigation = ITS_MITIGATION_OFF;
	} else if (!strcmp(str, "on")) {
		its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;
	} else if (!strcmp(str, "force")) {
		its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;
		setup_force_cpu_bug(X86_BUG_ITS);
	} else if (!strcmp(str, "vmexit")) {
		its_mitigation = ITS_MITIGATION_VMEXIT_ONLY;
	} else if (!strcmp(str, "stuff")) {
		its_mitigation = ITS_MITIGATION_RETPOLINE_STUFF;
	} else {
		pr_err("Ignoring unknown indirect_target_selection option (%s).", str);
	}

	return 0;
}
early_param("indirect_target_selection", its_parse_cmdline);

static void __init its_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_ITS)) {
		its_mitigation = ITS_MITIGATION_OFF;
		return;
	}

	if (its_mitigation == ITS_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_ITS))
			its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;
		else
			its_mitigation = ITS_MITIGATION_OFF;
	}

	if (its_mitigation == ITS_MITIGATION_OFF)
		return;

	if (!IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) ||
	    !IS_ENABLED(CONFIG_MITIGATION_RETHUNK)) {
		pr_err("WARNING: ITS mitigation depends on retpoline and rethunk support\n");
		its_mitigation = ITS_MITIGATION_OFF;
		return;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FORCE_FUNCTION_ALIGN_64B)) {
		pr_err("WARNING: ITS mitigation is not compatible with CONFIG_DEBUG_FORCE_FUNCTION_ALIGN_64B\n");
		its_mitigation = ITS_MITIGATION_OFF;
		return;
	}

	if (its_mitigation == ITS_MITIGATION_RETPOLINE_STUFF &&
	    !IS_ENABLED(CONFIG_MITIGATION_CALL_DEPTH_TRACKING)) {
		pr_err("RSB stuff mitigation not supported, using default\n");
		its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;
	}

	if (its_mitigation == ITS_MITIGATION_VMEXIT_ONLY &&
	    !boot_cpu_has_bug(X86_BUG_ITS_NATIVE_ONLY))
		its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;
}

static void __init its_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_ITS))
		return;

	switch (spectre_v2_enabled) {
	case SPECTRE_V2_NONE:
		if (its_mitigation != ITS_MITIGATION_OFF)
			pr_err("WARNING: Spectre-v2 mitigation is off, disabling ITS\n");
		its_mitigation = ITS_MITIGATION_OFF;
		break;
	case SPECTRE_V2_RETPOLINE:
	case SPECTRE_V2_EIBRS_RETPOLINE:
		/* Retpoline+CDT mitigates ITS */
		if (retbleed_mitigation == RETBLEED_MITIGATION_STUFF)
			its_mitigation = ITS_MITIGATION_RETPOLINE_STUFF;
		break;
	case SPECTRE_V2_LFENCE:
	case SPECTRE_V2_EIBRS_LFENCE:
		pr_err("WARNING: ITS mitigation is not compatible with lfence mitigation\n");
		its_mitigation = ITS_MITIGATION_OFF;
		break;
	default:
		break;
	}

	if (its_mitigation == ITS_MITIGATION_RETPOLINE_STUFF &&
	    !cdt_possible(spectre_v2_enabled))
		its_mitigation = ITS_MITIGATION_ALIGNED_THUNKS;

	pr_info("%s\n", its_strings[its_mitigation]);
}

static void __init its_apply_mitigation(void)
{
	switch (its_mitigation) {
	case ITS_MITIGATION_OFF:
	case ITS_MITIGATION_AUTO:
	case ITS_MITIGATION_VMEXIT_ONLY:
		break;
	case ITS_MITIGATION_ALIGNED_THUNKS:
		if (!boot_cpu_has(X86_FEATURE_RETPOLINE))
			setup_force_cpu_cap(X86_FEATURE_INDIRECT_THUNK_ITS);

		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		set_return_thunk(its_return_thunk);
		break;
	case ITS_MITIGATION_RETPOLINE_STUFF:
		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		setup_force_cpu_cap(X86_FEATURE_CALL_DEPTH);
		set_return_thunk(call_depth_return_thunk);
		break;
	}
}

#undef pr_fmt
#define pr_fmt(fmt)	"Transient Scheduler Attacks: " fmt

enum tsa_mitigations {
	TSA_MITIGATION_NONE,
	TSA_MITIGATION_AUTO,
	TSA_MITIGATION_UCODE_NEEDED,
	TSA_MITIGATION_USER_KERNEL,
	TSA_MITIGATION_VM,
	TSA_MITIGATION_FULL,
};

static const char * const tsa_strings[] = {
	[TSA_MITIGATION_NONE]		= "Vulnerable",
	[TSA_MITIGATION_UCODE_NEEDED]	= "Vulnerable: No microcode",
	[TSA_MITIGATION_USER_KERNEL]	= "Mitigation: Clear CPU buffers: user/kernel boundary",
	[TSA_MITIGATION_VM]		= "Mitigation: Clear CPU buffers: VM",
	[TSA_MITIGATION_FULL]		= "Mitigation: Clear CPU buffers",
};

static enum tsa_mitigations tsa_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_TSA) ? TSA_MITIGATION_AUTO : TSA_MITIGATION_NONE;

static int __init tsa_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		tsa_mitigation = TSA_MITIGATION_NONE;
	else if (!strcmp(str, "on"))
		tsa_mitigation = TSA_MITIGATION_FULL;
	else if (!strcmp(str, "user"))
		tsa_mitigation = TSA_MITIGATION_USER_KERNEL;
	else if (!strcmp(str, "vm"))
		tsa_mitigation = TSA_MITIGATION_VM;
	else
		pr_err("Ignoring unknown tsa=%s option.\n", str);

	return 0;
}
early_param("tsa", tsa_parse_cmdline);

static void __init tsa_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_TSA)) {
		tsa_mitigation = TSA_MITIGATION_NONE;
		return;
	}

	if (tsa_mitigation == TSA_MITIGATION_AUTO) {
		bool vm = false, uk = false;

		tsa_mitigation = TSA_MITIGATION_NONE;

		if (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL) ||
		    cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER)) {
			tsa_mitigation = TSA_MITIGATION_USER_KERNEL;
			uk = true;
		}

		if (cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST) ||
		    cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST)) {
			tsa_mitigation = TSA_MITIGATION_VM;
			vm = true;
		}

		if (uk && vm)
			tsa_mitigation = TSA_MITIGATION_FULL;
	}

	if (tsa_mitigation == TSA_MITIGATION_NONE)
		return;

	if (!boot_cpu_has(X86_FEATURE_VERW_CLEAR))
		tsa_mitigation = TSA_MITIGATION_UCODE_NEEDED;

	/*
	 * No need to set verw_clear_cpu_buf_mitigation_selected - it
	 * doesn't fit all cases here and it is not needed because this
	 * is the only VERW-based mitigation on AMD.
	 */
	pr_info("%s\n", tsa_strings[tsa_mitigation]);
}

static void __init tsa_apply_mitigation(void)
{
	switch (tsa_mitigation) {
	case TSA_MITIGATION_USER_KERNEL:
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
		break;
	case TSA_MITIGATION_VM:
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF_VM);
		break;
	case TSA_MITIGATION_FULL:
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF);
		setup_force_cpu_cap(X86_FEATURE_CLEAR_CPU_BUF_VM);
		break;
	default:
		break;
	}
}

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V2 : " fmt

static enum spectre_v2_user_mitigation spectre_v2_user_stibp __ro_after_init =
	SPECTRE_V2_USER_NONE;
static enum spectre_v2_user_mitigation spectre_v2_user_ibpb __ro_after_init =
	SPECTRE_V2_USER_NONE;

#ifdef CONFIG_MITIGATION_RETPOLINE
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

static enum spectre_v2_mitigation_cmd spectre_v2_cmd __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_SPECTRE_V2) ? SPECTRE_V2_CMD_AUTO : SPECTRE_V2_CMD_NONE;

enum spectre_v2_user_mitigation_cmd {
	SPECTRE_V2_USER_CMD_NONE,
	SPECTRE_V2_USER_CMD_AUTO,
	SPECTRE_V2_USER_CMD_FORCE,
	SPECTRE_V2_USER_CMD_PRCTL,
	SPECTRE_V2_USER_CMD_PRCTL_IBPB,
	SPECTRE_V2_USER_CMD_SECCOMP,
	SPECTRE_V2_USER_CMD_SECCOMP_IBPB,
};

static enum spectre_v2_user_mitigation_cmd spectre_v2_user_cmd __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_SPECTRE_V2) ? SPECTRE_V2_USER_CMD_AUTO : SPECTRE_V2_USER_CMD_NONE;

static const char * const spectre_v2_user_strings[] = {
	[SPECTRE_V2_USER_NONE]			= "User space: Vulnerable",
	[SPECTRE_V2_USER_STRICT]		= "User space: Mitigation: STIBP protection",
	[SPECTRE_V2_USER_STRICT_PREFERRED]	= "User space: Mitigation: STIBP always-on protection",
	[SPECTRE_V2_USER_PRCTL]			= "User space: Mitigation: STIBP via prctl",
	[SPECTRE_V2_USER_SECCOMP]		= "User space: Mitigation: STIBP via seccomp and prctl",
};

static int __init spectre_v2_user_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "auto"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_AUTO;
	else if (!strcmp(str, "off"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_NONE;
	else if (!strcmp(str, "on"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_FORCE;
	else if (!strcmp(str, "prctl"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_PRCTL;
	else if (!strcmp(str, "prctl,ibpb"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_PRCTL_IBPB;
	else if (!strcmp(str, "seccomp"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_SECCOMP;
	else if (!strcmp(str, "seccomp,ibpb"))
		spectre_v2_user_cmd = SPECTRE_V2_USER_CMD_SECCOMP_IBPB;
	else
		pr_err("Ignoring unknown spectre_v2_user option (%s).", str);

	return 0;
}
early_param("spectre_v2_user", spectre_v2_user_parse_cmdline);

static inline bool spectre_v2_in_ibrs_mode(enum spectre_v2_mitigation mode)
{
	return spectre_v2_in_eibrs_mode(mode) || mode == SPECTRE_V2_IBRS;
}

static void __init spectre_v2_user_select_mitigation(void)
{
	if (!boot_cpu_has(X86_FEATURE_IBPB) && !boot_cpu_has(X86_FEATURE_STIBP))
		return;

	switch (spectre_v2_user_cmd) {
	case SPECTRE_V2_USER_CMD_NONE:
		return;
	case SPECTRE_V2_USER_CMD_FORCE:
		spectre_v2_user_ibpb  = SPECTRE_V2_USER_STRICT;
		spectre_v2_user_stibp = SPECTRE_V2_USER_STRICT;
		break;
	case SPECTRE_V2_USER_CMD_AUTO:
		if (!should_mitigate_vuln(X86_BUG_SPECTRE_V2_USER))
			break;
		spectre_v2_user_ibpb = SPECTRE_V2_USER_PRCTL;
		if (smt_mitigations == SMT_MITIGATIONS_OFF)
			break;
		spectre_v2_user_stibp = SPECTRE_V2_USER_PRCTL;
		break;
	case SPECTRE_V2_USER_CMD_PRCTL:
		spectre_v2_user_ibpb  = SPECTRE_V2_USER_PRCTL;
		spectre_v2_user_stibp = SPECTRE_V2_USER_PRCTL;
		break;
	case SPECTRE_V2_USER_CMD_PRCTL_IBPB:
		spectre_v2_user_ibpb  = SPECTRE_V2_USER_STRICT;
		spectre_v2_user_stibp = SPECTRE_V2_USER_PRCTL;
		break;
	case SPECTRE_V2_USER_CMD_SECCOMP:
		if (IS_ENABLED(CONFIG_SECCOMP))
			spectre_v2_user_ibpb = SPECTRE_V2_USER_SECCOMP;
		else
			spectre_v2_user_ibpb = SPECTRE_V2_USER_PRCTL;
		spectre_v2_user_stibp = spectre_v2_user_ibpb;
		break;
	case SPECTRE_V2_USER_CMD_SECCOMP_IBPB:
		spectre_v2_user_ibpb = SPECTRE_V2_USER_STRICT;
		if (IS_ENABLED(CONFIG_SECCOMP))
			spectre_v2_user_stibp = SPECTRE_V2_USER_SECCOMP;
		else
			spectre_v2_user_stibp = SPECTRE_V2_USER_PRCTL;
		break;
	}

	/*
	 * At this point, an STIBP mode other than "off" has been set.
	 * If STIBP support is not being forced, check if STIBP always-on
	 * is preferred.
	 */
	if ((spectre_v2_user_stibp == SPECTRE_V2_USER_PRCTL ||
	     spectre_v2_user_stibp == SPECTRE_V2_USER_SECCOMP) &&
	    boot_cpu_has(X86_FEATURE_AMD_STIBP_ALWAYS_ON))
		spectre_v2_user_stibp = SPECTRE_V2_USER_STRICT_PREFERRED;

	if (!boot_cpu_has(X86_FEATURE_IBPB))
		spectre_v2_user_ibpb = SPECTRE_V2_USER_NONE;

	if (!boot_cpu_has(X86_FEATURE_STIBP))
		spectre_v2_user_stibp = SPECTRE_V2_USER_NONE;
}

static void __init spectre_v2_user_update_mitigation(void)
{
	if (!boot_cpu_has(X86_FEATURE_IBPB) && !boot_cpu_has(X86_FEATURE_STIBP))
		return;

	/* The spectre_v2 cmd line can override spectre_v2_user options */
	if (spectre_v2_cmd == SPECTRE_V2_CMD_NONE) {
		spectre_v2_user_ibpb = SPECTRE_V2_USER_NONE;
		spectre_v2_user_stibp = SPECTRE_V2_USER_NONE;
	} else if (spectre_v2_cmd == SPECTRE_V2_CMD_FORCE) {
		spectre_v2_user_ibpb = SPECTRE_V2_USER_STRICT;
		spectre_v2_user_stibp = SPECTRE_V2_USER_STRICT;
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
	    !cpu_smt_possible() ||
	    (spectre_v2_in_eibrs_mode(spectre_v2_enabled) &&
	     !boot_cpu_has(X86_FEATURE_AUTOIBRS))) {
		spectre_v2_user_stibp = SPECTRE_V2_USER_NONE;
		return;
	}

	if (spectre_v2_user_stibp != SPECTRE_V2_USER_NONE &&
	    (retbleed_mitigation == RETBLEED_MITIGATION_UNRET ||
	     retbleed_mitigation == RETBLEED_MITIGATION_IBPB)) {
		if (spectre_v2_user_stibp != SPECTRE_V2_USER_STRICT &&
		    spectre_v2_user_stibp != SPECTRE_V2_USER_STRICT_PREFERRED)
			pr_info("Selecting STIBP always-on mode to complement retbleed mitigation\n");
		spectre_v2_user_stibp = SPECTRE_V2_USER_STRICT_PREFERRED;
	}
	pr_info("%s\n", spectre_v2_user_strings[spectre_v2_user_stibp]);
}

static void __init spectre_v2_user_apply_mitigation(void)
{
	/* Initialize Indirect Branch Prediction Barrier */
	if (spectre_v2_user_ibpb != SPECTRE_V2_USER_NONE) {
		static_branch_enable(&switch_vcpu_ibpb);

		switch (spectre_v2_user_ibpb) {
		case SPECTRE_V2_USER_STRICT:
			static_branch_enable(&switch_mm_always_ibpb);
			break;
		case SPECTRE_V2_USER_PRCTL:
		case SPECTRE_V2_USER_SECCOMP:
			static_branch_enable(&switch_mm_cond_ibpb);
			break;
		default:
			break;
		}

		pr_info("mitigation: Enabling %s Indirect Branch Prediction Barrier\n",
			static_key_enabled(&switch_mm_always_ibpb) ?
			"always-on" : "conditional");
	}
}

static const char * const spectre_v2_strings[] = {
	[SPECTRE_V2_NONE]			= "Vulnerable",
	[SPECTRE_V2_RETPOLINE]			= "Mitigation: Retpolines",
	[SPECTRE_V2_LFENCE]			= "Vulnerable: LFENCE",
	[SPECTRE_V2_EIBRS]			= "Mitigation: Enhanced / Automatic IBRS",
	[SPECTRE_V2_EIBRS_LFENCE]		= "Mitigation: Enhanced / Automatic IBRS + LFENCE",
	[SPECTRE_V2_EIBRS_RETPOLINE]		= "Mitigation: Enhanced / Automatic IBRS + Retpolines",
	[SPECTRE_V2_IBRS]			= "Mitigation: IBRS",
};

static bool nospectre_v2 __ro_after_init;

static int __init nospectre_v2_parse_cmdline(char *str)
{
	nospectre_v2 = true;
	spectre_v2_cmd = SPECTRE_V2_CMD_NONE;
	return 0;
}
early_param("nospectre_v2", nospectre_v2_parse_cmdline);

static int __init spectre_v2_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (nospectre_v2)
		return 0;

	if (!strcmp(str, "off")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_NONE;
	} else if (!strcmp(str, "on")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_FORCE;
		setup_force_cpu_bug(X86_BUG_SPECTRE_V2);
		setup_force_cpu_bug(X86_BUG_SPECTRE_V2_USER);
	} else if (!strcmp(str, "retpoline")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_RETPOLINE;
	} else if (!strcmp(str, "retpoline,amd") ||
		 !strcmp(str, "retpoline,lfence")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_RETPOLINE_LFENCE;
	} else if (!strcmp(str, "retpoline,generic")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_RETPOLINE_GENERIC;
	} else if (!strcmp(str, "eibrs")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_EIBRS;
	} else if (!strcmp(str, "eibrs,lfence")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_EIBRS_LFENCE;
	} else if (!strcmp(str, "eibrs,retpoline")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_EIBRS_RETPOLINE;
	} else if (!strcmp(str, "auto")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	} else if (!strcmp(str, "ibrs")) {
		spectre_v2_cmd = SPECTRE_V2_CMD_IBRS;
	} else {
		pr_err("Ignoring unknown spectre_v2 option (%s).", str);
	}

	return 0;
}
early_param("spectre_v2", spectre_v2_parse_cmdline);

static enum spectre_v2_mitigation __init spectre_v2_select_retpoline(void)
{
	if (!IS_ENABLED(CONFIG_MITIGATION_RETPOLINE)) {
		pr_err("Kernel not compiled with retpoline; no mitigation available!");
		return SPECTRE_V2_NONE;
	}

	return SPECTRE_V2_RETPOLINE;
}

static bool __ro_after_init rrsba_disabled;

/* Disable in-kernel use of non-RSB RET predictors */
static void __init spec_ctrl_disable_kernel_rrsba(void)
{
	if (rrsba_disabled)
		return;

	if (!(x86_arch_cap_msr & ARCH_CAP_RRSBA)) {
		rrsba_disabled = true;
		return;
	}

	if (!boot_cpu_has(X86_FEATURE_RRSBA_CTRL))
		return;

	x86_spec_ctrl_base |= SPEC_CTRL_RRSBA_DIS_S;
	update_spec_ctrl(x86_spec_ctrl_base);
	rrsba_disabled = true;
}

static void __init spectre_v2_select_rsb_mitigation(enum spectre_v2_mitigation mode)
{
	/*
	 * WARNING! There are many subtleties to consider when changing *any*
	 * code related to RSB-related mitigations.  Before doing so, carefully
	 * read the following document, and update if necessary:
	 *
	 *   Documentation/admin-guide/hw-vuln/rsb.rst
	 *
	 * In an overly simplified nutshell:
	 *
	 *   - User->user RSB attacks are conditionally mitigated during
	 *     context switches by cond_mitigation -> write_ibpb().
	 *
	 *   - User->kernel and guest->host attacks are mitigated by eIBRS or
	 *     RSB filling.
	 *
	 *     Though, depending on config, note that other alternative
	 *     mitigations may end up getting used instead, e.g., IBPB on
	 *     entry/vmexit, call depth tracking, or return thunks.
	 */

	switch (mode) {
	case SPECTRE_V2_NONE:
		break;

	case SPECTRE_V2_EIBRS:
	case SPECTRE_V2_EIBRS_LFENCE:
	case SPECTRE_V2_EIBRS_RETPOLINE:
		if (boot_cpu_has_bug(X86_BUG_EIBRS_PBRSB)) {
			pr_info("Spectre v2 / PBRSB-eIBRS: Retire a single CALL on VMEXIT\n");
			setup_force_cpu_cap(X86_FEATURE_RSB_VMEXIT_LITE);
		}
		break;

	case SPECTRE_V2_RETPOLINE:
	case SPECTRE_V2_LFENCE:
	case SPECTRE_V2_IBRS:
		pr_info("Spectre v2 / SpectreRSB: Filling RSB on context switch and VMEXIT\n");
		setup_force_cpu_cap(X86_FEATURE_RSB_CTXSW);
		setup_force_cpu_cap(X86_FEATURE_RSB_VMEXIT);
		break;

	default:
		pr_warn_once("Unknown Spectre v2 mode, disabling RSB mitigation\n");
		dump_stack();
		break;
	}
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
	BHI_MITIGATION_AUTO,
	BHI_MITIGATION_ON,
	BHI_MITIGATION_VMEXIT_ONLY,
};

static enum bhi_mitigations bhi_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_SPECTRE_BHI) ? BHI_MITIGATION_AUTO : BHI_MITIGATION_OFF;

static int __init spectre_bhi_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		bhi_mitigation = BHI_MITIGATION_OFF;
	else if (!strcmp(str, "on"))
		bhi_mitigation = BHI_MITIGATION_ON;
	else if (!strcmp(str, "vmexit"))
		bhi_mitigation = BHI_MITIGATION_VMEXIT_ONLY;
	else
		pr_err("Ignoring unknown spectre_bhi option (%s)", str);

	return 0;
}
early_param("spectre_bhi", spectre_bhi_parse_cmdline);

static void __init bhi_select_mitigation(void)
{
	if (!boot_cpu_has(X86_BUG_BHI))
		bhi_mitigation = BHI_MITIGATION_OFF;

	if (bhi_mitigation != BHI_MITIGATION_AUTO)
		return;

	if (cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST)) {
		if (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL))
			bhi_mitigation = BHI_MITIGATION_ON;
		else
			bhi_mitigation = BHI_MITIGATION_VMEXIT_ONLY;
	} else {
		bhi_mitigation = BHI_MITIGATION_OFF;
	}
}

static void __init bhi_update_mitigation(void)
{
	if (spectre_v2_cmd == SPECTRE_V2_CMD_NONE)
		bhi_mitigation = BHI_MITIGATION_OFF;
}

static void __init bhi_apply_mitigation(void)
{
	if (bhi_mitigation == BHI_MITIGATION_OFF)
		return;

	/* Retpoline mitigates against BHI unless the CPU has RRSBA behavior */
	if (boot_cpu_has(X86_FEATURE_RETPOLINE) &&
	    !boot_cpu_has(X86_FEATURE_RETPOLINE_LFENCE)) {
		spec_ctrl_disable_kernel_rrsba();
		if (rrsba_disabled)
			return;
	}

	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	/* Mitigate in hardware if supported */
	if (spec_ctrl_bhi_dis())
		return;

	if (bhi_mitigation == BHI_MITIGATION_VMEXIT_ONLY) {
		pr_info("Spectre BHI mitigation: SW BHB clearing on VM exit only\n");
		setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_VMEXIT);
		return;
	}

	pr_info("Spectre BHI mitigation: SW BHB clearing on syscall and VM exit\n");
	setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_LOOP);
	setup_force_cpu_cap(X86_FEATURE_CLEAR_BHB_VMEXIT);
}

static void __init spectre_v2_select_mitigation(void)
{
	if ((spectre_v2_cmd == SPECTRE_V2_CMD_RETPOLINE ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_RETPOLINE_GENERIC ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS_LFENCE ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS_RETPOLINE) &&
	    !IS_ENABLED(CONFIG_MITIGATION_RETPOLINE)) {
		pr_err("RETPOLINE selected but not compiled in. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if ((spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS_LFENCE ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS_RETPOLINE) &&
	    !boot_cpu_has(X86_FEATURE_IBRS_ENHANCED)) {
		pr_err("EIBRS selected but CPU doesn't have Enhanced or Automatic IBRS. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if ((spectre_v2_cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE ||
	     spectre_v2_cmd == SPECTRE_V2_CMD_EIBRS_LFENCE) &&
	    !boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
		pr_err("LFENCE selected, but CPU doesn't have a serializing LFENCE. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if (spectre_v2_cmd == SPECTRE_V2_CMD_IBRS && !IS_ENABLED(CONFIG_MITIGATION_IBRS_ENTRY)) {
		pr_err("IBRS selected but not compiled in. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if (spectre_v2_cmd == SPECTRE_V2_CMD_IBRS && boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		pr_err("IBRS selected but not Intel CPU. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if (spectre_v2_cmd == SPECTRE_V2_CMD_IBRS && !boot_cpu_has(X86_FEATURE_IBRS)) {
		pr_err("IBRS selected but CPU doesn't have IBRS. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if (spectre_v2_cmd == SPECTRE_V2_CMD_IBRS && cpu_feature_enabled(X86_FEATURE_XENPV)) {
		pr_err("IBRS selected but running as XenPV guest. Switching to AUTO select\n");
		spectre_v2_cmd = SPECTRE_V2_CMD_AUTO;
	}

	if (!boot_cpu_has_bug(X86_BUG_SPECTRE_V2)) {
		spectre_v2_cmd = SPECTRE_V2_CMD_NONE;
		return;
	}

	switch (spectre_v2_cmd) {
	case SPECTRE_V2_CMD_NONE:
		return;

	case SPECTRE_V2_CMD_AUTO:
		if (!should_mitigate_vuln(X86_BUG_SPECTRE_V2))
			break;
		fallthrough;
	case SPECTRE_V2_CMD_FORCE:
		if (boot_cpu_has(X86_FEATURE_IBRS_ENHANCED)) {
			spectre_v2_enabled = SPECTRE_V2_EIBRS;
			break;
		}

		spectre_v2_enabled = spectre_v2_select_retpoline();
		break;

	case SPECTRE_V2_CMD_RETPOLINE_LFENCE:
		pr_err(SPECTRE_V2_LFENCE_MSG);
		spectre_v2_enabled = SPECTRE_V2_LFENCE;
		break;

	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
		spectre_v2_enabled = SPECTRE_V2_RETPOLINE;
		break;

	case SPECTRE_V2_CMD_RETPOLINE:
		spectre_v2_enabled = spectre_v2_select_retpoline();
		break;

	case SPECTRE_V2_CMD_IBRS:
		spectre_v2_enabled = SPECTRE_V2_IBRS;
		break;

	case SPECTRE_V2_CMD_EIBRS:
		spectre_v2_enabled = SPECTRE_V2_EIBRS;
		break;

	case SPECTRE_V2_CMD_EIBRS_LFENCE:
		spectre_v2_enabled = SPECTRE_V2_EIBRS_LFENCE;
		break;

	case SPECTRE_V2_CMD_EIBRS_RETPOLINE:
		spectre_v2_enabled = SPECTRE_V2_EIBRS_RETPOLINE;
		break;
	}
}

static void __init spectre_v2_update_mitigation(void)
{
	if (spectre_v2_cmd == SPECTRE_V2_CMD_AUTO &&
	    !spectre_v2_in_eibrs_mode(spectre_v2_enabled)) {
		if (IS_ENABLED(CONFIG_MITIGATION_IBRS_ENTRY) &&
		    boot_cpu_has_bug(X86_BUG_RETBLEED) &&
		    retbleed_mitigation != RETBLEED_MITIGATION_NONE &&
		    retbleed_mitigation != RETBLEED_MITIGATION_STUFF &&
		    boot_cpu_has(X86_FEATURE_IBRS) &&
		    boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
			spectre_v2_enabled = SPECTRE_V2_IBRS;
		}
	}

	if (boot_cpu_has_bug(X86_BUG_SPECTRE_V2))
		pr_info("%s\n", spectre_v2_strings[spectre_v2_enabled]);
}

static void __init spectre_v2_apply_mitigation(void)
{
	if (spectre_v2_enabled == SPECTRE_V2_EIBRS && unprivileged_ebpf_enabled())
		pr_err(SPECTRE_V2_EIBRS_EBPF_MSG);

	if (spectre_v2_in_ibrs_mode(spectre_v2_enabled)) {
		if (boot_cpu_has(X86_FEATURE_AUTOIBRS)) {
			msr_set_bit(MSR_EFER, _EFER_AUTOIBRS);
		} else {
			x86_spec_ctrl_base |= SPEC_CTRL_IBRS;
			update_spec_ctrl(x86_spec_ctrl_base);
		}
	}

	switch (spectre_v2_enabled) {
	case SPECTRE_V2_NONE:
		return;

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
	if (spectre_v2_enabled == SPECTRE_V2_EIBRS_LFENCE ||
	    spectre_v2_enabled == SPECTRE_V2_EIBRS_RETPOLINE ||
	    spectre_v2_enabled == SPECTRE_V2_RETPOLINE)
		spec_ctrl_disable_kernel_rrsba();

	spectre_v2_select_rsb_mitigation(spectre_v2_enabled);

	/*
	 * Retpoline protects the kernel, but doesn't protect firmware.  IBRS
	 * and Enhanced IBRS protect firmware too, so enable IBRS around
	 * firmware calls only when IBRS / Enhanced / Automatic IBRS aren't
	 * otherwise enabled.
	 *
	 * Use "spectre_v2_enabled" to check Enhanced IBRS instead of
	 * boot_cpu_has(), because the user might select retpoline on the kernel
	 * command line and if the CPU supports Enhanced IBRS, kernel might
	 * un-intentionally not enable IBRS around firmware calls.
	 */
	if (boot_cpu_has_bug(X86_BUG_RETBLEED) &&
	    boot_cpu_has(X86_FEATURE_IBPB) &&
	    (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	     boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)) {

		if (retbleed_mitigation != RETBLEED_MITIGATION_IBPB) {
			setup_force_cpu_cap(X86_FEATURE_USE_IBPB_FW);
			pr_info("Enabling Speculation Barrier for firmware calls\n");
		}

	} else if (boot_cpu_has(X86_FEATURE_IBRS) &&
		   !spectre_v2_in_ibrs_mode(spectre_v2_enabled)) {
		setup_force_cpu_cap(X86_FEATURE_USE_IBRS_FW);
		pr_info("Enabling Restricted Speculation for firmware calls\n");
	}
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
		static_branch_enable(&cpu_buf_idle_clear);
	} else if (mmio_mitigation == MMIO_MITIGATION_OFF ||
		   (x86_arch_cap_msr & ARCH_CAP_FBSDP_NO)) {
		static_branch_disable(&cpu_buf_idle_clear);
	}
}

#undef pr_fmt
#define pr_fmt(fmt)	"Speculative Store Bypass: " fmt

static enum ssb_mitigation ssb_mode __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_SSB) ? SPEC_STORE_BYPASS_AUTO : SPEC_STORE_BYPASS_NONE;

static const char * const ssb_strings[] = {
	[SPEC_STORE_BYPASS_NONE]	= "Vulnerable",
	[SPEC_STORE_BYPASS_DISABLE]	= "Mitigation: Speculative Store Bypass disabled",
	[SPEC_STORE_BYPASS_PRCTL]	= "Mitigation: Speculative Store Bypass disabled via prctl",
	[SPEC_STORE_BYPASS_SECCOMP]	= "Mitigation: Speculative Store Bypass disabled via prctl and seccomp",
};

static bool nossb __ro_after_init;

static int __init nossb_parse_cmdline(char *str)
{
	nossb = true;
	ssb_mode = SPEC_STORE_BYPASS_NONE;
	return 0;
}
early_param("nospec_store_bypass_disable", nossb_parse_cmdline);

static int __init ssb_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (nossb)
		return 0;

	if (!strcmp(str, "auto"))
		ssb_mode = SPEC_STORE_BYPASS_AUTO;
	else if (!strcmp(str, "on"))
		ssb_mode = SPEC_STORE_BYPASS_DISABLE;
	else if (!strcmp(str, "off"))
		ssb_mode = SPEC_STORE_BYPASS_NONE;
	else if (!strcmp(str, "prctl"))
		ssb_mode = SPEC_STORE_BYPASS_PRCTL;
	else if (!strcmp(str, "seccomp"))
		ssb_mode = IS_ENABLED(CONFIG_SECCOMP) ?
			SPEC_STORE_BYPASS_SECCOMP : SPEC_STORE_BYPASS_PRCTL;
	else
		pr_err("Ignoring unknown spec_store_bypass_disable option (%s).\n",
			str);

	return 0;
}
early_param("spec_store_bypass_disable", ssb_parse_cmdline);

static void __init ssb_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS)) {
		ssb_mode = SPEC_STORE_BYPASS_NONE;
		return;
	}

	if (ssb_mode == SPEC_STORE_BYPASS_AUTO) {
		if (should_mitigate_vuln(X86_BUG_SPEC_STORE_BYPASS))
			ssb_mode = SPEC_STORE_BYPASS_PRCTL;
		else
			ssb_mode = SPEC_STORE_BYPASS_NONE;
	}

	if (!boot_cpu_has(X86_FEATURE_SSBD))
		ssb_mode = SPEC_STORE_BYPASS_NONE;

	pr_info("%s\n", ssb_strings[ssb_mode]);
}

static void __init ssb_apply_mitigation(void)
{
	/*
	 * We have three CPU feature flags that are in play here:
	 *  - X86_BUG_SPEC_STORE_BYPASS - CPU is susceptible.
	 *  - X86_FEATURE_SSBD - CPU is able to turn off speculative store bypass
	 *  - X86_FEATURE_SPEC_STORE_BYPASS_DISABLE - engage the mitigation
	 */
	if (ssb_mode == SPEC_STORE_BYPASS_DISABLE) {
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
	case SPEC_STORE_BYPASS_NONE:
		if (boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS))
			return PR_SPEC_ENABLE;
		return PR_SPEC_NOT_AFFECTED;
	case SPEC_STORE_BYPASS_DISABLE:
		return PR_SPEC_DISABLE;
	case SPEC_STORE_BYPASS_SECCOMP:
	case SPEC_STORE_BYPASS_PRCTL:
	case SPEC_STORE_BYPASS_AUTO:
		if (task_spec_ssb_force_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;
		if (task_spec_ssb_noexec(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE_NOEXEC;
		if (task_spec_ssb_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	}
	BUG();
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
enum l1tf_mitigations l1tf_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_L1TF) ? L1TF_MITIGATION_AUTO : L1TF_MITIGATION_OFF;
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

	switch (c->x86_vfm) {
	case INTEL_NEHALEM:
	case INTEL_WESTMERE:
	case INTEL_SANDYBRIDGE:
	case INTEL_IVYBRIDGE:
	case INTEL_HASWELL:
	case INTEL_HASWELL_L:
	case INTEL_HASWELL_G:
	case INTEL_BROADWELL:
	case INTEL_BROADWELL_G:
	case INTEL_SKYLAKE_L:
	case INTEL_SKYLAKE:
	case INTEL_KABYLAKE_L:
	case INTEL_KABYLAKE:
		if (c->x86_cache_bits < 44)
			c->x86_cache_bits = 44;
		break;
	}
}

static void __init l1tf_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_L1TF)) {
		l1tf_mitigation = L1TF_MITIGATION_OFF;
		return;
	}

	if (l1tf_mitigation != L1TF_MITIGATION_AUTO)
		return;

	if (!should_mitigate_vuln(X86_BUG_L1TF)) {
		l1tf_mitigation = L1TF_MITIGATION_OFF;
		return;
	}

	if (smt_mitigations == SMT_MITIGATIONS_ON)
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOSMT;
	else
		l1tf_mitigation = L1TF_MITIGATION_FLUSH;
}

static void __init l1tf_apply_mitigation(void)
{
	u64 half_pa;

	if (!boot_cpu_has_bug(X86_BUG_L1TF))
		return;

	override_cache_bits(&boot_cpu_data);

	switch (l1tf_mitigation) {
	case L1TF_MITIGATION_OFF:
	case L1TF_MITIGATION_FLUSH_NOWARN:
	case L1TF_MITIGATION_FLUSH:
	case L1TF_MITIGATION_AUTO:
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

static const char * const srso_strings[] = {
	[SRSO_MITIGATION_NONE]			= "Vulnerable",
	[SRSO_MITIGATION_UCODE_NEEDED]		= "Vulnerable: No microcode",
	[SRSO_MITIGATION_SAFE_RET_UCODE_NEEDED]	= "Vulnerable: Safe RET, no microcode",
	[SRSO_MITIGATION_MICROCODE]		= "Vulnerable: Microcode, no safe RET",
	[SRSO_MITIGATION_NOSMT]			= "Mitigation: SMT disabled",
	[SRSO_MITIGATION_SAFE_RET]		= "Mitigation: Safe RET",
	[SRSO_MITIGATION_IBPB]			= "Mitigation: IBPB",
	[SRSO_MITIGATION_IBPB_ON_VMEXIT]	= "Mitigation: IBPB on VMEXIT only",
	[SRSO_MITIGATION_BP_SPEC_REDUCE]	= "Mitigation: Reduced Speculation"
};

static int __init srso_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		srso_mitigation = SRSO_MITIGATION_NONE;
	else if (!strcmp(str, "microcode"))
		srso_mitigation = SRSO_MITIGATION_MICROCODE;
	else if (!strcmp(str, "safe-ret"))
		srso_mitigation = SRSO_MITIGATION_SAFE_RET;
	else if (!strcmp(str, "ibpb"))
		srso_mitigation = SRSO_MITIGATION_IBPB;
	else if (!strcmp(str, "ibpb-vmexit"))
		srso_mitigation = SRSO_MITIGATION_IBPB_ON_VMEXIT;
	else
		pr_err("Ignoring unknown SRSO option (%s).", str);

	return 0;
}
early_param("spec_rstack_overflow", srso_parse_cmdline);

#define SRSO_NOTICE "WARNING: See https://kernel.org/doc/html/latest/admin-guide/hw-vuln/srso.html for mitigation options."

static void __init srso_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SRSO)) {
		srso_mitigation = SRSO_MITIGATION_NONE;
		return;
	}

	if (srso_mitigation == SRSO_MITIGATION_AUTO) {
		/*
		 * Use safe-RET if user->kernel or guest->host protection is
		 * required.  Otherwise the 'microcode' mitigation is sufficient
		 * to protect the user->user and guest->guest vectors.
		 */
		if (cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_HOST) ||
		    (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_KERNEL) &&
		     !boot_cpu_has(X86_FEATURE_SRSO_USER_KERNEL_NO))) {
			srso_mitigation = SRSO_MITIGATION_SAFE_RET;
		} else if (cpu_attack_vector_mitigated(CPU_MITIGATE_USER_USER) ||
			   cpu_attack_vector_mitigated(CPU_MITIGATE_GUEST_GUEST)) {
			srso_mitigation = SRSO_MITIGATION_MICROCODE;
		} else {
			srso_mitigation = SRSO_MITIGATION_NONE;
			return;
		}
	}

	/* Zen1/2 with SMT off aren't vulnerable to SRSO. */
	if (boot_cpu_data.x86 < 0x19 && !cpu_smt_possible()) {
		srso_mitigation = SRSO_MITIGATION_NOSMT;
		return;
	}

	if (!boot_cpu_has(X86_FEATURE_IBPB_BRTYPE)) {
		pr_warn("IBPB-extending microcode not applied!\n");
		pr_warn(SRSO_NOTICE);

		/*
		 * Safe-RET provides partial mitigation without microcode, but
		 * other mitigations require microcode to provide any
		 * mitigations.
		 */
		if (srso_mitigation == SRSO_MITIGATION_SAFE_RET)
			srso_mitigation = SRSO_MITIGATION_SAFE_RET_UCODE_NEEDED;
		else
			srso_mitigation = SRSO_MITIGATION_UCODE_NEEDED;
	}

	switch (srso_mitigation) {
	case SRSO_MITIGATION_SAFE_RET:
	case SRSO_MITIGATION_SAFE_RET_UCODE_NEEDED:
		if (boot_cpu_has(X86_FEATURE_SRSO_USER_KERNEL_NO)) {
			srso_mitigation = SRSO_MITIGATION_IBPB_ON_VMEXIT;
			goto ibpb_on_vmexit;
		}

		if (!IS_ENABLED(CONFIG_MITIGATION_SRSO)) {
			pr_err("WARNING: kernel not compiled with MITIGATION_SRSO.\n");
			srso_mitigation = SRSO_MITIGATION_NONE;
		}
		break;
ibpb_on_vmexit:
	case SRSO_MITIGATION_IBPB_ON_VMEXIT:
		if (boot_cpu_has(X86_FEATURE_SRSO_BP_SPEC_REDUCE)) {
			pr_notice("Reducing speculation to address VM/HV SRSO attack vector.\n");
			srso_mitigation = SRSO_MITIGATION_BP_SPEC_REDUCE;
			break;
		}
		fallthrough;
	case SRSO_MITIGATION_IBPB:
		if (!IS_ENABLED(CONFIG_MITIGATION_IBPB_ENTRY)) {
			pr_err("WARNING: kernel not compiled with MITIGATION_IBPB_ENTRY.\n");
			srso_mitigation = SRSO_MITIGATION_NONE;
		}
		break;
	default:
		break;
	}
}

static void __init srso_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_SRSO))
		return;

	/* If retbleed is using IBPB, that works for SRSO as well */
	if (retbleed_mitigation == RETBLEED_MITIGATION_IBPB &&
	    boot_cpu_has(X86_FEATURE_IBPB_BRTYPE))
		srso_mitigation = SRSO_MITIGATION_IBPB;

	pr_info("%s\n", srso_strings[srso_mitigation]);
}

static void __init srso_apply_mitigation(void)
{
	/*
	 * Clear the feature flag if this mitigation is not selected as that
	 * feature flag controls the BpSpecReduce MSR bit toggling in KVM.
	 */
	if (srso_mitigation != SRSO_MITIGATION_BP_SPEC_REDUCE)
		setup_clear_cpu_cap(X86_FEATURE_SRSO_BP_SPEC_REDUCE);

	if (srso_mitigation == SRSO_MITIGATION_NONE) {
		if (boot_cpu_has(X86_FEATURE_SBPB))
			x86_pred_cmd = PRED_CMD_SBPB;
		return;
	}

	switch (srso_mitigation) {
	case SRSO_MITIGATION_SAFE_RET:
	case SRSO_MITIGATION_SAFE_RET_UCODE_NEEDED:
		/*
		 * Enable the return thunk for generated code
		 * like ftrace, static_call, etc.
		 */
		setup_force_cpu_cap(X86_FEATURE_RETHUNK);
		setup_force_cpu_cap(X86_FEATURE_UNRET);

		if (boot_cpu_data.x86 == 0x19) {
			setup_force_cpu_cap(X86_FEATURE_SRSO_ALIAS);
			set_return_thunk(srso_alias_return_thunk);
		} else {
			setup_force_cpu_cap(X86_FEATURE_SRSO);
			set_return_thunk(srso_return_thunk);
		}
		break;
	case SRSO_MITIGATION_IBPB:
		setup_force_cpu_cap(X86_FEATURE_ENTRY_IBPB);
		/*
		 * IBPB on entry already obviates the need for
		 * software-based untraining so clear those in case some
		 * other mitigation like Retbleed has selected them.
		 */
		setup_clear_cpu_cap(X86_FEATURE_UNRET);
		setup_clear_cpu_cap(X86_FEATURE_RETHUNK);
		fallthrough;
	case SRSO_MITIGATION_IBPB_ON_VMEXIT:
		setup_force_cpu_cap(X86_FEATURE_IBPB_ON_VMEXIT);
		/*
		 * There is no need for RSB filling: entry_ibpb() ensures
		 * all predictions, including the RSB, are invalidated,
		 * regardless of IBPB implementation.
		 */
		setup_clear_cpu_cap(X86_FEATURE_RSB_VMEXIT);
		break;
	default:
		break;
	}
}

#undef pr_fmt
#define pr_fmt(fmt)	"VMSCAPE: " fmt

enum vmscape_mitigations {
	VMSCAPE_MITIGATION_NONE,
	VMSCAPE_MITIGATION_AUTO,
	VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER,
	VMSCAPE_MITIGATION_IBPB_ON_VMEXIT,
};

static const char * const vmscape_strings[] = {
	[VMSCAPE_MITIGATION_NONE]		= "Vulnerable",
	/* [VMSCAPE_MITIGATION_AUTO] */
	[VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER]	= "Mitigation: IBPB before exit to userspace",
	[VMSCAPE_MITIGATION_IBPB_ON_VMEXIT]	= "Mitigation: IBPB on VMEXIT",
};

static enum vmscape_mitigations vmscape_mitigation __ro_after_init =
	IS_ENABLED(CONFIG_MITIGATION_VMSCAPE) ? VMSCAPE_MITIGATION_AUTO : VMSCAPE_MITIGATION_NONE;

static int __init vmscape_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off")) {
		vmscape_mitigation = VMSCAPE_MITIGATION_NONE;
	} else if (!strcmp(str, "ibpb")) {
		vmscape_mitigation = VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER;
	} else if (!strcmp(str, "force")) {
		setup_force_cpu_bug(X86_BUG_VMSCAPE);
		vmscape_mitigation = VMSCAPE_MITIGATION_AUTO;
	} else {
		pr_err("Ignoring unknown vmscape=%s option.\n", str);
	}

	return 0;
}
early_param("vmscape", vmscape_parse_cmdline);

static void __init vmscape_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_VMSCAPE) ||
	    !boot_cpu_has(X86_FEATURE_IBPB)) {
		vmscape_mitigation = VMSCAPE_MITIGATION_NONE;
		return;
	}

	if (vmscape_mitigation == VMSCAPE_MITIGATION_AUTO) {
		if (should_mitigate_vuln(X86_BUG_VMSCAPE))
			vmscape_mitigation = VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER;
		else
			vmscape_mitigation = VMSCAPE_MITIGATION_NONE;
	}
}

static void __init vmscape_update_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_VMSCAPE))
		return;

	if (retbleed_mitigation == RETBLEED_MITIGATION_IBPB ||
	    srso_mitigation == SRSO_MITIGATION_IBPB_ON_VMEXIT)
		vmscape_mitigation = VMSCAPE_MITIGATION_IBPB_ON_VMEXIT;

	pr_info("%s\n", vmscape_strings[vmscape_mitigation]);
}

static void __init vmscape_apply_mitigation(void)
{
	if (vmscape_mitigation == VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER)
		setup_force_cpu_cap(X86_FEATURE_IBPB_EXIT_TO_USER);
}

#undef pr_fmt
#define pr_fmt(fmt) fmt

#define MDS_MSG_SMT "MDS CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/mds.html for more details.\n"
#define TAA_MSG_SMT "TAA CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/tsx_async_abort.html for more details.\n"
#define MMIO_MSG_SMT "MMIO Stale Data CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/processor_mmio_stale_data.html for more details.\n"
#define VMSCAPE_MSG_SMT "VMSCAPE: SMT on, STIBP is required for full protection. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/vmscape.html for more details.\n"

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
	case MDS_MITIGATION_AUTO:
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
	case TAA_MITIGATION_AUTO:
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
	case MMIO_MITIGATION_AUTO:
	case MMIO_MITIGATION_UCODE_NEEDED:
		if (sched_smt_active())
			pr_warn_once(MMIO_MSG_SMT);
		break;
	case MMIO_MITIGATION_OFF:
		break;
	}

	switch (tsa_mitigation) {
	case TSA_MITIGATION_USER_KERNEL:
	case TSA_MITIGATION_VM:
	case TSA_MITIGATION_AUTO:
	case TSA_MITIGATION_FULL:
		/*
		 * TSA-SQ can potentially lead to info leakage between
		 * SMT threads.
		 */
		if (sched_smt_active())
			static_branch_enable(&cpu_buf_idle_clear);
		else
			static_branch_disable(&cpu_buf_idle_clear);
		break;
	case TSA_MITIGATION_NONE:
	case TSA_MITIGATION_UCODE_NEEDED:
		break;
	}

	switch (vmscape_mitigation) {
	case VMSCAPE_MITIGATION_NONE:
	case VMSCAPE_MITIGATION_AUTO:
		break;
	case VMSCAPE_MITIGATION_IBPB_ON_VMEXIT:
	case VMSCAPE_MITIGATION_IBPB_EXIT_TO_USER:
		/*
		 * Hypervisors can be attacked across-threads, warn for SMT when
		 * STIBP is not already enabled system-wide.
		 *
		 * Intel eIBRS (!AUTOIBRS) implies STIBP on.
		 */
		if (!sched_smt_active() ||
		    spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT ||
		    spectre_v2_user_stibp == SPECTRE_V2_USER_STRICT_PREFERRED ||
		    (spectre_v2_in_eibrs_mode(spectre_v2_enabled) &&
		     !boot_cpu_has(X86_FEATURE_AUTOIBRS)))
			break;
		pr_warn_once(VMSCAPE_MSG_SMT);
		break;
	}

	mutex_unlock(&spec_ctrl_mutex);
}

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

static ssize_t old_microcode_show_state(char *buf)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return sysfs_emit(buf, "Unknown: running under hypervisor");

	return sysfs_emit(buf, "Vulnerable\n");
}

static ssize_t its_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", its_strings[its_mitigation]);
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

static const char *spectre_bhi_state(void)
{
	if (!boot_cpu_has_bug(X86_BUG_BHI))
		return "; BHI: Not affected";
	else if (boot_cpu_has(X86_FEATURE_CLEAR_BHB_HW))
		return "; BHI: BHI_DIS_S";
	else if (boot_cpu_has(X86_FEATURE_CLEAR_BHB_LOOP))
		return "; BHI: SW loop, KVM: SW loop";
	else if (boot_cpu_has(X86_FEATURE_RETPOLINE) &&
		 !boot_cpu_has(X86_FEATURE_RETPOLINE_LFENCE) &&
		 rrsba_disabled)
		return "; BHI: Retpoline";
	else if (boot_cpu_has(X86_FEATURE_CLEAR_BHB_VMEXIT))
		return "; BHI: Vulnerable, KVM: SW loop";

	return "; BHI: Vulnerable";
}

static ssize_t spectre_v2_show_state(char *buf)
{
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

static ssize_t srso_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", srso_strings[srso_mitigation]);
}

static ssize_t gds_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", gds_strings[gds_mitigation]);
}

static ssize_t tsa_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", tsa_strings[tsa_mitigation]);
}

static ssize_t vmscape_show_state(char *buf)
{
	return sysfs_emit(buf, "%s\n", vmscape_strings[vmscape_mitigation]);
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
		return mmio_stale_data_show_state(buf);

	case X86_BUG_RETBLEED:
		return retbleed_show_state(buf);

	case X86_BUG_SRSO:
		return srso_show_state(buf);

	case X86_BUG_GDS:
		return gds_show_state(buf);

	case X86_BUG_RFDS:
		return rfds_show_state(buf);

	case X86_BUG_OLD_MICROCODE:
		return old_microcode_show_state(buf);

	case X86_BUG_ITS:
		return its_show_state(buf);

	case X86_BUG_TSA:
		return tsa_show_state(buf);

	case X86_BUG_VMSCAPE:
		return vmscape_show_state(buf);

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
	return cpu_show_common(dev, attr, buf, X86_BUG_MMIO_STALE_DATA);
}

ssize_t cpu_show_retbleed(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_RETBLEED);
}

ssize_t cpu_show_spec_rstack_overflow(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_SRSO);
}

ssize_t cpu_show_gds(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_GDS);
}

ssize_t cpu_show_reg_file_data_sampling(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_RFDS);
}

ssize_t cpu_show_old_microcode(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_OLD_MICROCODE);
}

ssize_t cpu_show_indirect_target_selection(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_ITS);
}

ssize_t cpu_show_tsa(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_TSA);
}

ssize_t cpu_show_vmscape(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpu_show_common(dev, attr, buf, X86_BUG_VMSCAPE);
}
#endif

void __warn_thunk(void)
{
	WARN_ONCE(1, "Unpatched return thunk in use. This should not happen!\n");
}
