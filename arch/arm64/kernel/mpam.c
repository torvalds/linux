// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Arm Ltd. */

#include <asm/mpam.h>

#include <linux/arm_mpam.h>
#include <linux/cpu_pm.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>

DEFINE_STATIC_KEY_FALSE(mpam_enabled);
DEFINE_PER_CPU(u64, arm64_mpam_default);
DEFINE_PER_CPU(u64, arm64_mpam_current);

u64 arm64_mpam_global_default;

static int mpam_pm_notifier(struct notifier_block *self,
			    unsigned long cmd, void *v)
{
	u64 regval;
	int cpu = smp_processor_id();

	switch (cmd) {
	case CPU_PM_EXIT:
		/*
		 * Don't use mpam_thread_switch() as the system register
		 * value has changed under our feet.
		 */
		regval = READ_ONCE(per_cpu(arm64_mpam_current, cpu));
		write_sysreg_s(regval | MPAM1_EL1_MPAMEN, SYS_MPAM1_EL1);
		if (system_supports_sme()) {
			write_sysreg_s(regval & (MPAMSM_EL1_PARTID_D | MPAMSM_EL1_PMG_D),
				       SYS_MPAMSM_EL1);
		}
		isb();

		write_sysreg_s(regval, SYS_MPAM0_EL1);

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block mpam_pm_nb = {
	.notifier_call = mpam_pm_notifier,
};

static int __init arm64_mpam_register_cpus(void)
{
	u64 mpamidr = read_sanitised_ftr_reg(SYS_MPAMIDR_EL1);
	u16 partid_max = FIELD_GET(MPAMIDR_EL1_PARTID_MAX, mpamidr);
	u8 pmg_max = FIELD_GET(MPAMIDR_EL1_PMG_MAX, mpamidr);

	if (!system_supports_mpam())
		return 0;

	cpu_pm_register_notifier(&mpam_pm_nb);
	return mpam_register_requestor(partid_max, pmg_max);
}
/* Must occur before mpam_msc_driver_init() from subsys_initcall() */
arch_initcall(arm64_mpam_register_cpus)
