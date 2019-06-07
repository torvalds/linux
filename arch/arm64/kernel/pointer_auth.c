// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/prctl.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <asm/cpufeature.h>
#include <asm/pointer_auth.h>

int ptrauth_prctl_reset_keys(struct task_struct *tsk, unsigned long arg)
{
	struct ptrauth_keys *keys = &tsk->thread.keys_user;
	unsigned long addr_key_mask = PR_PAC_APIAKEY | PR_PAC_APIBKEY |
				      PR_PAC_APDAKEY | PR_PAC_APDBKEY;
	unsigned long key_mask = addr_key_mask | PR_PAC_APGAKEY;

	if (!system_supports_address_auth() && !system_supports_generic_auth())
		return -EINVAL;

	if (!arg) {
		ptrauth_keys_init(keys);
		ptrauth_keys_switch(keys);
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

	ptrauth_keys_switch(keys);

	return 0;
}
