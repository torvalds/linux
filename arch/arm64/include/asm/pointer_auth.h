/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POINTER_AUTH_H
#define __ASM_POINTER_AUTH_H

#include <linux/bitops.h>
#include <linux/prctl.h>
#include <linux/random.h>

#include <asm/cpufeature.h>
#include <asm/memory.h>
#include <asm/sysreg.h>

#define PR_PAC_ENABLED_KEYS_MASK                                               \
	(PR_PAC_APIAKEY | PR_PAC_APIBKEY | PR_PAC_APDAKEY | PR_PAC_APDBKEY)

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

#define __ptrauth_key_install_nosync(k, v)			\
do {								\
	struct ptrauth_key __pki_v = (v);			\
	write_sysreg_s(__pki_v.lo, SYS_ ## k ## KEYLO_EL1);	\
	write_sysreg_s(__pki_v.hi, SYS_ ## k ## KEYHI_EL1);	\
} while (0)

#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL

struct ptrauth_keys_kernel {
	struct ptrauth_key apia;
};

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

#endif /* CONFIG_ARM64_PTR_AUTH_KERNEL */

static inline void ptrauth_keys_install_user(struct ptrauth_keys_user *keys)
{
	if (system_supports_address_auth()) {
		__ptrauth_key_install_nosync(APIB, keys->apib);
		__ptrauth_key_install_nosync(APDA, keys->apda);
		__ptrauth_key_install_nosync(APDB, keys->apdb);
	}

	if (system_supports_generic_auth())
		__ptrauth_key_install_nosync(APGA, keys->apga);
}

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

	ptrauth_keys_install_user(keys);
}

extern int ptrauth_prctl_reset_keys(struct task_struct *tsk, unsigned long arg);

extern int ptrauth_set_enabled_keys(struct task_struct *tsk, unsigned long keys,
				    unsigned long enabled);
extern int ptrauth_get_enabled_keys(struct task_struct *tsk);

static inline unsigned long ptrauth_strip_insn_pac(unsigned long ptr)
{
	return ptrauth_clear_pac(ptr);
}

static __always_inline void ptrauth_enable(void)
{
	if (!system_supports_address_auth())
		return;
	sysreg_clear_set(sctlr_el1, 0, (SCTLR_ELx_ENIA | SCTLR_ELx_ENIB |
					SCTLR_ELx_ENDA | SCTLR_ELx_ENDB));
	isb();
}

#define ptrauth_suspend_exit()                                                 \
	ptrauth_keys_install_user(&current->thread.keys_user)

#define ptrauth_thread_init_user()                                             \
	do {                                                                   \
		ptrauth_keys_init_user(&current->thread.keys_user);            \
									       \
		/* enable all keys */                                          \
		if (system_supports_address_auth())                            \
			ptrauth_set_enabled_keys(current,                      \
						 PR_PAC_ENABLED_KEYS_MASK,     \
						 PR_PAC_ENABLED_KEYS_MASK);    \
	} while (0)

#define ptrauth_thread_switch_user(tsk)                                        \
	ptrauth_keys_install_user(&(tsk)->thread.keys_user)

#else /* CONFIG_ARM64_PTR_AUTH */
#define ptrauth_enable()
#define ptrauth_prctl_reset_keys(tsk, arg)	(-EINVAL)
#define ptrauth_set_enabled_keys(tsk, keys, enabled)	(-EINVAL)
#define ptrauth_get_enabled_keys(tsk)	(-EINVAL)
#define ptrauth_strip_insn_pac(lr)	(lr)
#define ptrauth_suspend_exit()
#define ptrauth_thread_init_user()
#define ptrauth_thread_switch_user(tsk)
#endif /* CONFIG_ARM64_PTR_AUTH */

#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL
#define ptrauth_thread_init_kernel(tsk)					\
	ptrauth_keys_init_kernel(&(tsk)->thread.keys_kernel)
#define ptrauth_thread_switch_kernel(tsk)				\
	ptrauth_keys_switch_kernel(&(tsk)->thread.keys_kernel)
#else
#define ptrauth_thread_init_kernel(tsk)
#define ptrauth_thread_switch_kernel(tsk)
#endif /* CONFIG_ARM64_PTR_AUTH_KERNEL */

#endif /* __ASM_POINTER_AUTH_H */
