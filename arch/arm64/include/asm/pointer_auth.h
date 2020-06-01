/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POINTER_AUTH_H
#define __ASM_POINTER_AUTH_H

#include <linux/bitops.h>
#include <linux/random.h>

#include <asm/cpufeature.h>
#include <asm/memory.h>
#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_PTR_AUTH
/*
 * Each key is a 128-bit quantity which is split across a pair of 64-bit
 * registers (Lo and Hi).
 */
struct ptrauth_key {
	unsigned long lo, hi;
};

/*
 * We give each process its own keys, which are shared by all threads. The keys
 * are inherited upon fork(), and reinitialised upon exec*().
 */
struct ptrauth_keys_user {
	struct ptrauth_key apia;
	struct ptrauth_key apib;
	struct ptrauth_key apda;
	struct ptrauth_key apdb;
	struct ptrauth_key apga;
};

struct ptrauth_keys_kernel {
	struct ptrauth_key apia;
};

static inline void ptrauth_keys_init_user(struct ptrauth_keys_user *keys)
{
	if (system_supports_address_auth()) {
		get_random_bytes(&keys->apia, sizeof(keys->apia));
		get_random_bytes(&keys->apib, sizeof(keys->apib));
		get_random_bytes(&keys->apda, sizeof(keys->apda));
		get_random_bytes(&keys->apdb, sizeof(keys->apdb));
	}

	if (system_supports_generic_auth())
		get_random_bytes(&keys->apga, sizeof(keys->apga));
}

#define __ptrauth_key_install_nosync(k, v)			\
do {								\
	struct ptrauth_key __pki_v = (v);			\
	write_sysreg_s(__pki_v.lo, SYS_ ## k ## KEYLO_EL1);	\
	write_sysreg_s(__pki_v.hi, SYS_ ## k ## KEYHI_EL1);	\
} while (0)

static __always_inline void ptrauth_keys_init_kernel(struct ptrauth_keys_kernel *keys)
{
	if (system_supports_address_auth())
		get_random_bytes(&keys->apia, sizeof(keys->apia));
}

static __always_inline void ptrauth_keys_switch_kernel(struct ptrauth_keys_kernel *keys)
{
	if (!system_supports_address_auth())
		return;

	__ptrauth_key_install_nosync(APIA, keys->apia);
	isb();
}

extern int ptrauth_prctl_reset_keys(struct task_struct *tsk, unsigned long arg);

static inline unsigned long ptrauth_strip_insn_pac(unsigned long ptr)
{
	return ptrauth_clear_pac(ptr);
}

#define ptrauth_thread_init_user(tsk)					\
	ptrauth_keys_init_user(&(tsk)->thread.keys_user)
#define ptrauth_thread_init_kernel(tsk)					\
	ptrauth_keys_init_kernel(&(tsk)->thread.keys_kernel)
#define ptrauth_thread_switch_kernel(tsk)				\
	ptrauth_keys_switch_kernel(&(tsk)->thread.keys_kernel)

#else /* CONFIG_ARM64_PTR_AUTH */
#define ptrauth_prctl_reset_keys(tsk, arg)	(-EINVAL)
#define ptrauth_strip_insn_pac(lr)	(lr)
#define ptrauth_thread_init_user(tsk)
#define ptrauth_thread_init_kernel(tsk)
#define ptrauth_thread_switch_kernel(tsk)
#endif /* CONFIG_ARM64_PTR_AUTH */

#endif /* __ASM_POINTER_AUTH_H */
