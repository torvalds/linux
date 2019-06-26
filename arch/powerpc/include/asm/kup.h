/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#ifdef CONFIG_PPC64
#include <asm/book3s/64/kup-radix.h>
#endif
#ifdef CONFIG_PPC_8xx
#include <asm/nohash/32/kup-8xx.h>
#endif
#ifdef CONFIG_PPC_BOOK3S_32
#include <asm/book3s/32/kup.h>
#endif

#ifdef __ASSEMBLY__
#ifndef CONFIG_PPC_KUAP
.macro kuap_save_and_lock	sp, thread, gpr1, gpr2, gpr3
.endm

.macro kuap_restore	sp, current, gpr1, gpr2, gpr3
.endm

.macro kuap_check	current, gpr
.endm

#endif

#else /* !__ASSEMBLY__ */

#include <asm/pgtable.h>

void setup_kup(void);

#ifdef CONFIG_PPC_KUEP
void setup_kuep(bool disabled);
#else
static inline void setup_kuep(bool disabled) { }
#endif /* CONFIG_PPC_KUEP */

#ifdef CONFIG_PPC_KUAP
void setup_kuap(bool disabled);
#else
static inline void setup_kuap(bool disabled) { }
static inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size) { }
static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size) { }
static inline bool bad_kuap_fault(struct pt_regs *regs, bool is_write) { return false; }
#endif /* CONFIG_PPC_KUAP */

static inline void allow_read_from_user(const void __user *from, unsigned long size)
{
	allow_user_access(NULL, from, size);
}

static inline void allow_write_to_user(void __user *to, unsigned long size)
{
	allow_user_access(to, NULL, size);
}

static inline void prevent_read_from_user(const void __user *from, unsigned long size)
{
	prevent_user_access(NULL, from, size);
}

static inline void prevent_write_to_user(void __user *to, unsigned long size)
{
	prevent_user_access(to, NULL, size);
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_KUP_H_ */
