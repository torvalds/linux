// SPDX-License-Identifier: GPL-2.0
#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/proc-fns.h>
#include <asm/spectre.h>
#include <asm/system_misc.h>

#ifdef CONFIG_ARM_PSCI
static int __maybe_unused spectre_v2_get_cpu_fw_mitigation_state(void)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_ARCH_WORKAROUND_1, &res);

	switch ((int)res.a0) {
	case SMCCC_RET_SUCCESS:
		return SPECTRE_MITIGATED;

	case SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED:
		return SPECTRE_UNAFFECTED;

	default:
		return SPECTRE_VULNERABLE;
	}
}
#else
static int __maybe_unused spectre_v2_get_cpu_fw_mitigation_state(void)
{
	return SPECTRE_VULNERABLE;
}
#endif

#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
DEFINE_PER_CPU(harden_branch_predictor_fn_t, harden_branch_predictor_fn);

extern void cpu_v7_iciallu_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
extern void cpu_v7_bpiall_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
extern void cpu_v7_smc_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
extern void cpu_v7_hvc_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);

static void harden_branch_predictor_bpiall(void)
{
	write_sysreg(0, BPIALL);
}

static void harden_branch_predictor_iciallu(void)
{
	write_sysreg(0, ICIALLU);
}

static void __maybe_unused call_smc_arch_workaround_1(void)
{
	arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static void __maybe_unused call_hvc_arch_workaround_1(void)
{
	arm_smccc_1_1_hvc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static unsigned int spectre_v2_install_workaround(unsigned int method)
{
	const char *spectre_v2_method = NULL;
	int cpu = smp_processor_id();

	if (per_cpu(harden_branch_predictor_fn, cpu))
		return SPECTRE_MITIGATED;

	switch (method) {
	case SPECTRE_V2_METHOD_BPIALL:
		per_cpu(harden_branch_predictor_fn, cpu) =
			harden_branch_predictor_bpiall;
		spectre_v2_method = "BPIALL";
		break;

	case SPECTRE_V2_METHOD_ICIALLU:
		per_cpu(harden_branch_predictor_fn, cpu) =
			harden_branch_predictor_iciallu;
		spectre_v2_method = "ICIALLU";
		break;

	case SPECTRE_V2_METHOD_HVC:
		per_cpu(harden_branch_predictor_fn, cpu) =
			call_hvc_arch_workaround_1;
		cpu_do_switch_mm = cpu_v7_hvc_switch_mm;
		spectre_v2_method = "hypervisor";
		break;

	case SPECTRE_V2_METHOD_SMC:
		per_cpu(harden_branch_predictor_fn, cpu) =
			call_smc_arch_workaround_1;
		cpu_do_switch_mm = cpu_v7_smc_switch_mm;
		spectre_v2_method = "firmware";
		break;
	}

	if (spectre_v2_method)
		pr_info("CPU%u: Spectre v2: using %s workaround\n",
			smp_processor_id(), spectre_v2_method);

	return SPECTRE_MITIGATED;
}
#else
static unsigned int spectre_v2_install_workaround(unsigned int method)
{
	pr_info_once("Spectre V2: workarounds disabled by configuration\n");

	return SPECTRE_VULNERABLE;
}
#endif

static void cpu_v7_spectre_v2_init(void)
{
	unsigned int state, method = 0;

	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A8:
	case ARM_CPU_PART_CORTEX_A9:
	case ARM_CPU_PART_CORTEX_A12:
	case ARM_CPU_PART_CORTEX_A17:
	case ARM_CPU_PART_CORTEX_A73:
	case ARM_CPU_PART_CORTEX_A75:
		state = SPECTRE_MITIGATED;
		method = SPECTRE_V2_METHOD_BPIALL;
		break;

	case ARM_CPU_PART_CORTEX_A15:
	case ARM_CPU_PART_BRAHMA_B15:
		state = SPECTRE_MITIGATED;
		method = SPECTRE_V2_METHOD_ICIALLU;
		break;

	case ARM_CPU_PART_BRAHMA_B53:
		/* Requires no workaround */
		state = SPECTRE_UNAFFECTED;
		break;

	default:
		/* Other ARM CPUs require no workaround */
		if (read_cpuid_implementor() == ARM_CPU_IMP_ARM) {
			state = SPECTRE_UNAFFECTED;
			break;
		}

		fallthrough;

	/* Cortex A57/A72 require firmware workaround */
	case ARM_CPU_PART_CORTEX_A57:
	case ARM_CPU_PART_CORTEX_A72:
		state = spectre_v2_get_cpu_fw_mitigation_state();
		if (state != SPECTRE_MITIGATED)
			break;

		switch (arm_smccc_1_1_get_conduit()) {
		case SMCCC_CONDUIT_HVC:
			method = SPECTRE_V2_METHOD_HVC;
			break;

		case SMCCC_CONDUIT_SMC:
			method = SPECTRE_V2_METHOD_SMC;
			break;

		default:
			state = SPECTRE_VULNERABLE;
			break;
		}
	}

	if (state == SPECTRE_MITIGATED)
		state = spectre_v2_install_workaround(method);

	spectre_v2_update_state(state, method);
}

#ifdef CONFIG_HARDEN_BRANCH_HISTORY
static int spectre_bhb_method;

static const char *spectre_bhb_method_name(int method)
{
	switch (method) {
	case SPECTRE_V2_METHOD_LOOP8:
		return "loop";

	case SPECTRE_V2_METHOD_BPIALL:
		return "BPIALL";

	default:
		return "unknown";
	}
}

static int spectre_bhb_install_workaround(int method)
{
	if (spectre_bhb_method != method) {
		if (spectre_bhb_method) {
			pr_err("CPU%u: Spectre BHB: method disagreement, system vulnerable\n",
			       smp_processor_id());

			return SPECTRE_VULNERABLE;
		}

		if (spectre_bhb_update_vectors(method) == SPECTRE_VULNERABLE)
			return SPECTRE_VULNERABLE;

		spectre_bhb_method = method;

		pr_info("CPU%u: Spectre BHB: enabling %s workaround for all CPUs\n",
			smp_processor_id(), spectre_bhb_method_name(method));
	}

	return SPECTRE_MITIGATED;
}
#else
static int spectre_bhb_install_workaround(int method)
{
	return SPECTRE_VULNERABLE;
}
#endif

static void cpu_v7_spectre_bhb_init(void)
{
	unsigned int state, method = 0;

	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A15:
	case ARM_CPU_PART_BRAHMA_B15:
	case ARM_CPU_PART_CORTEX_A57:
	case ARM_CPU_PART_CORTEX_A72:
		state = SPECTRE_MITIGATED;
		method = SPECTRE_V2_METHOD_LOOP8;
		break;

	case ARM_CPU_PART_CORTEX_A73:
	case ARM_CPU_PART_CORTEX_A75:
		state = SPECTRE_MITIGATED;
		method = SPECTRE_V2_METHOD_BPIALL;
		break;

	default:
		state = SPECTRE_UNAFFECTED;
		break;
	}

	if (state == SPECTRE_MITIGATED)
		state = spectre_bhb_install_workaround(method);

	spectre_v2_update_state(state, method);
}

static __maybe_unused bool cpu_v7_check_auxcr_set(bool *warned,
						  u32 mask, const char *msg)
{
	u32 aux_cr;

	asm("mrc p15, 0, %0, c1, c0, 1" : "=r" (aux_cr));

	if ((aux_cr & mask) != mask) {
		if (!*warned)
			pr_err("CPU%u: %s", smp_processor_id(), msg);
		*warned = true;
		return false;
	}
	return true;
}

static DEFINE_PER_CPU(bool, spectre_warned);

static bool check_spectre_auxcr(bool *warned, u32 bit)
{
	return IS_ENABLED(CONFIG_HARDEN_BRANCH_PREDICTOR) &&
		cpu_v7_check_auxcr_set(warned, bit,
				       "Spectre v2: firmware did not set auxiliary control register IBE bit, system vulnerable\n");
}

void cpu_v7_ca8_ibe(void)
{
	if (check_spectre_auxcr(this_cpu_ptr(&spectre_warned), BIT(6)))
		cpu_v7_spectre_v2_init();
}

void cpu_v7_ca15_ibe(void)
{
	if (check_spectre_auxcr(this_cpu_ptr(&spectre_warned), BIT(0)))
		cpu_v7_spectre_v2_init();
	cpu_v7_spectre_bhb_init();
}

void cpu_v7_bugs_init(void)
{
	cpu_v7_spectre_v2_init();
	cpu_v7_spectre_bhb_init();
}
