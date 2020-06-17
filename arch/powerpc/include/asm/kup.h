/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#define KUAP_READ	1
#define KUAP_WRITE	2
#define KUAP_READ_WRITE	(KUAP_READ | KUAP_WRITE)
/*
 * For prevent_user_access() only.
 * Use the current saved situation instead of the to/from/size params.
 * Used on book3s/32
 */
#define KUAP_CURRENT_READ	4
#define KUAP_CURRENT_WRITE	8
#define KUAP_CURRENT		(KUAP_CURRENT_READ | KUAP_CURRENT_WRITE)

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

#include <linux/pgtable.h>

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
				     unsigned long size, unsigned long dir) { }
static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir) { }
static inline unsigned long prevent_user_access_return(void) { return 0UL; }
static inline void restore_user_access(unsigned long flags) { }
static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return false;
}
#endif /* CONFIG_PPC_KUAP */

static inline void allow_read_from_user(const void __user *from, unsigned long size)
{
	allow_user_access(NULL, from, size, KUAP_READ);
}

static inline void allow_write_to_user(void __user *to, unsigned long size)
{
	allow_user_access(to, NULL, size, KUAP_WRITE);
}

static inline void allow_read_write_user(void __user *to, const void __user *from,
					 unsigned long size)
{
	allow_user_access(to, from, size, KUAP_READ_WRITE);
}

static inline void prevent_read_from_user(const void __user *from, unsigned long size)
{
	prevent_user_access(NULL, from, size, KUAP_READ);
}

static inline void prevent_write_to_user(void __user *to, unsigned long size)
{
	prevent_user_access(to, NULL, size, KUAP_WRITE);
}

static inline void prevent_read_write_user(void __user *to, const void __user *from,
					   unsigned long size)
{
	prevent_user_access(to, from, size, KUAP_READ_WRITE);
}

static inline void prevent_current_access_user(void)
{
	prevent_user_access(NULL, NULL, ~0UL, KUAP_CURRENT);
}

static inline void prevent_current_read_from_user(void)
{
	prevent_user_access(NULL, NULL, ~0UL, KUAP_CURRENT_READ);
}

static inline void prevent_current_write_to_user(void)
{
	prevent_user_access(NULL, NULL, ~0UL, KUAP_CURRENT_WRITE);
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_KUAP_H_ */
