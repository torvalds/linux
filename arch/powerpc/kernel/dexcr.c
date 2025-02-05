// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/sched.h>

#include <asm/cpu_has_feature.h>
#include <asm/cputable.h>
#include <asm/processor.h>
#include <asm/reg.h>

static int __init init_task_dexcr(void)
{
	if (!early_cpu_has_feature(CPU_FTR_ARCH_31))
		return 0;

	current->thread.dexcr_onexec = mfspr(SPRN_DEXCR);

	return 0;
}
early_initcall(init_task_dexcr)

/* Allow thread local configuration of these by default */
#define DEXCR_PRCTL_EDITABLE ( \
	DEXCR_PR_IBRTPD | \
	DEXCR_PR_SRAPD | \
	DEXCR_PR_NPHIE)

static int prctl_to_aspect(unsigned long which, unsigned int *aspect)
{
	switch (which) {
	case PR_PPC_DEXCR_SBHE:
		*aspect = DEXCR_PR_SBHE;
		break;
	case PR_PPC_DEXCR_IBRTPD:
		*aspect = DEXCR_PR_IBRTPD;
		break;
	case PR_PPC_DEXCR_SRAPD:
		*aspect = DEXCR_PR_SRAPD;
		break;
	case PR_PPC_DEXCR_NPHIE:
		*aspect = DEXCR_PR_NPHIE;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

int get_dexcr_prctl(struct task_struct *task, unsigned long which)
{
	unsigned int aspect;
	int ret;

	ret = prctl_to_aspect(which, &aspect);
	if (ret)
		return ret;

	if (aspect & DEXCR_PRCTL_EDITABLE)
		ret |= PR_PPC_DEXCR_CTRL_EDITABLE;

	if (aspect & mfspr(SPRN_DEXCR))
		ret |= PR_PPC_DEXCR_CTRL_SET;
	else
		ret |= PR_PPC_DEXCR_CTRL_CLEAR;

	if (aspect & task->thread.dexcr_onexec)
		ret |= PR_PPC_DEXCR_CTRL_SET_ONEXEC;
	else
		ret |= PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC;

	return ret;
}

int set_dexcr_prctl(struct task_struct *task, unsigned long which, unsigned long ctrl)
{
	unsigned long dexcr;
	unsigned int aspect;
	int err = 0;

	err = prctl_to_aspect(which, &aspect);
	if (err)
		return err;

	if (!(aspect & DEXCR_PRCTL_EDITABLE))
		return -EPERM;

	if (ctrl & ~PR_PPC_DEXCR_CTRL_MASK)
		return -EINVAL;

	if (ctrl & PR_PPC_DEXCR_CTRL_SET && ctrl & PR_PPC_DEXCR_CTRL_CLEAR)
		return -EINVAL;

	if (ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC && ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC)
		return -EINVAL;

	/*
	 * We do not want an unprivileged process being able to disable
	 * a setuid process's hash check instructions
	 */
	if (aspect == DEXCR_PR_NPHIE &&
	    ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	dexcr = mfspr(SPRN_DEXCR);

	if (ctrl & PR_PPC_DEXCR_CTRL_SET)
		dexcr |= aspect;
	else if (ctrl & PR_PPC_DEXCR_CTRL_CLEAR)
		dexcr &= ~aspect;

	if (ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC)
		task->thread.dexcr_onexec |= aspect;
	else if (ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC)
		task->thread.dexcr_onexec &= ~aspect;

	mtspr(SPRN_DEXCR, dexcr);

	return 0;
}
