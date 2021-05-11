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

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/book3s/64/kup.h>
#endif

#ifdef CONFIG_PPC_8xx
#include <asm/nohash/32/kup-8xx.h>
#endif

#ifdef CONFIG_PPC_BOOK3S_32
#include <asm/book3s/32/kup.h>
#endif

#ifdef __ASSEMBLY__
#ifndef CONFIG_PPC_KUAP
.macro kuap_check_amr	gpr1, gpr2
.endm

#endif

#else /* !__ASSEMBLY__ */

extern bool disable_kuep;
extern bool disable_kuap;

#include <linux/pgtable.h>

#ifdef CONFIG_PPC_KUEP
void setup_kuep(bool disabled);
#else
static inline void setup_kuep(bool disabled) { }
#endif /* CONFIG_PPC_KUEP */

#if defined(CONFIG_PPC_KUEP) && defined(CONFIG_PPC_BOOK3S_32)
void kuep_lock(void);
void kuep_unlock(void);
#else
static inline void kuep_lock(void) { }
static inline void kuep_unlock(void) { }
#endif

#ifdef CONFIG_PPC_KUAP
void setup_kuap(bool disabled);
#else
static inline void setup_kuap(bool disabled) { }

static inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return false;
}

static inline void kuap_assert_locked(void) { }
static inline void kuap_save_and_lock(struct pt_regs *regs) { }
static inline void kuap_user_restore(struct pt_regs *regs) { }
static inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long amr) { }

static inline unsigned long kuap_get_and_assert_locked(void)
{
	return 0;
}

/*
 * book3s/64/kup-radix.h defines these functions for the !KUAP case to flush
 * the L1D cache after user accesses. Only include the empty stubs for other
 * platforms.
 */
#ifndef CONFIG_PPC_BOOK3S_64
static inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size, unsigned long dir) { }
static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir) { }
static inline unsigned long prevent_user_access_return(void) { return 0UL; }
static inline void restore_user_access(unsigned long flags) { }
#endif /* CONFIG_PPC_BOOK3S_64 */
#endif /* CONFIG_PPC_KUAP */

static __always_inline void setup_kup(void)
{
	setup_kuep(disable_kuep);
	setup_kuap(disable_kuap);
}

static inline void allow_read_from_user(const void __user *from, unsigned long size)
{
	barrier_nospec();
	allow_user_access(NULL, from, size, KUAP_READ);
}

static inline void allow_write_to_user(void __user *to, unsigned long size)
{
	allow_user_access(to, NULL, size, KUAP_WRITE);
}

static inline void allow_read_write_user(void __user *to, const void __user *from,
					 unsigned long size)
{
	barrier_nospec();
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
