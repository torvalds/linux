// SPDX-License-Identifier: GPL-2.0

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/prctl.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <asm/cpufeature.h>
#include <asm/pointer_auth.h>

int ptrauth_prctl_reset_keys(struct task_struct *tsk, unsigned long arg)
{
	struct ptrauth_keys_user *keys = &tsk->thread.keys_user;
	unsigned long addr_key_mask = PR_PAC_APIAKEY | PR_PAC_APIBKEY |
				      PR_PAC_APDAKEY | PR_PAC_APDBKEY;
	unsigned long key_mask = addr_key_mask | PR_PAC_APGAKEY;

	if (!system_supports_address_auth() && !system_supports_generic_auth())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(tsk)))
		return -EINVAL;

	if (!arg) {
		ptrauth_keys_init_user(keys);
		return 0;
	}

	if (arg & ~key_mask)
		return -EINVAL;

	if (((arg & addr_key_mask) && !system_supports_address_auth()) ||
	    ((arg & PR_PAC_APGAKEY) && !system_supports_generic_auth()))
		return -EINVAL;

	if (arg & PR_PAC_APIAKEY)
		get_random_bytes(&keys->apia, sizeof(keys->apia));
	if (arg & PR_PAC_APIBKEY)
		get_random_bytes(&keys->apib, sizeof(keys->apib));
	if (arg & PR_PAC_APDAKEY)
		get_random_bytes(&keys->apda, sizeof(keys->apda));
	if (arg & PR_PAC_APDBKEY)
		get_random_bytes(&keys->apdb, sizeof(keys->apdb));
	if (arg & PR_PAC_APGAKEY)
		get_random_bytes(&keys->apga, sizeof(keys->apga));
	ptrauth_keys_install_user(keys);

	return 0;
}

static u64 arg_to_enxx_mask(unsigned long arg)
{
	u64 sctlr_enxx_mask = 0;

	WARN_ON(arg & ~PR_PAC_ENABLED_KEYS_MASK);
	if (arg & PR_PAC_APIAKEY)
		sctlr_enxx_mask |= SCTLR_ELx_ENIA;
	if (arg & PR_PAC_APIBKEY)
		sctlr_enxx_mask |= SCTLR_ELx_ENIB;
	if (arg & PR_PAC_APDAKEY)
		sctlr_enxx_mask |= SCTLR_ELx_ENDA;
	if (arg & PR_PAC_APDBKEY)
		sctlr_enxx_mask |= SCTLR_ELx_ENDB;
	return sctlr_enxx_mask;
}

int ptrauth_set_enabled_keys(struct task_struct *tsk, unsigned long keys,
			     unsigned long enabled)
{
	u64 sctlr;

	if (!system_supports_address_auth())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(tsk)))
		return -EINVAL;

	if ((keys & ~PR_PAC_ENABLED_KEYS_MASK) || (enabled & ~keys))
		return -EINVAL;

	preempt_disable();
	sctlr = tsk->thread.sctlr_user;
	sctlr &= ~arg_to_enxx_mask(keys);
	sctlr |= arg_to_enxx_mask(enabled);
	tsk->thread.sctlr_user = sctlr;
	if (tsk == current)
		update_sctlr_el1(sctlr);
	preempt_enable();

	return 0;
}

int ptrauth_get_enabled_keys(struct task_struct *tsk)
{
	int retval = 0;

	if (!system_supports_address_auth())
		return -EINVAL;

	if (is_compat_thread(task_thread_info(tsk)))
		return -EINVAL;

	if (tsk->thread.sctlr_user & SCTLR_ELx_ENIA)
		retval |= PR_PAC_APIAKEY;
	if (tsk->thread.sctlr_user & SCTLR_ELx_ENIB)
		retval |= PR_PAC_APIBKEY;
	if (tsk->thread.sctlr_user & SCTLR_ELx_ENDA)
		retval |= PR_PAC_APDAKEY;
	if (tsk->thread.sctlr_user & SCTLR_ELx_ENDB)
		retval |= PR_PAC_APDBKEY;

	return retval;
}
