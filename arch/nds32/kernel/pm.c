// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2008-2017 Andes Technology Corporation

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/suspend.h>
#include <asm/suspend.h>
#include <nds32_intrinsic.h>

unsigned int resume_addr;
unsigned int *phy_addr_sp_tmp;

static void nds32_suspend2ram(void)
{
	pgd_t *pgdv;
	pud_t *pudv;
	pmd_t *pmdv;
	pte_t *ptev;

	pgdv = (pgd_t *)__va((__nds32__mfsr(NDS32_SR_L1_PPTB) &
		L1_PPTB_mskBASE)) + pgd_index((unsigned int)cpu_resume);

	pudv = pud_offset(pgdv, (unsigned int)cpu_resume);
	pmdv = pmd_offset(pudv, (unsigned int)cpu_resume);
	ptev = pte_offset_map(pmdv, (unsigned int)cpu_resume);

	resume_addr = ((*ptev) & TLB_DATA_mskPPN)
			| ((unsigned int)cpu_resume & 0x00000fff);

	suspend2ram();
}

static void nds32_suspend_cpu(void)
{
	while (!(__nds32__mfsr(NDS32_SR_INT_PEND) & wake_mask))
		__asm__ volatile ("standby no_wake_grant\n\t");
}

static int nds32_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_ON:
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;
	default:
		return 0;
	}
}

static int nds32_pm_enter(suspend_state_t state)
{
	pr_debug("%s:state:%d\n", __func__, state);
	switch (state) {
	case PM_SUSPEND_STANDBY:
		nds32_suspend_cpu();
		return 0;
	case PM_SUSPEND_MEM:
		nds32_suspend2ram();
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct platform_suspend_ops nds32_pm_ops = {
	.valid = nds32_pm_valid,
	.enter = nds32_pm_enter,
};

static int __init nds32_pm_init(void)
{
	pr_debug("Enter %s\n", __func__);
	suspend_set_ops(&nds32_pm_ops);
	return 0;
}
late_initcall(nds32_pm_init);
