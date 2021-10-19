/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#define KUAP_READ	1
#define KUAP_WRITE	2
#define KUAP_READ_WRITE	(KUAP_READ | KUAP_WRITE)

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

void setup_kup(void);
void setup_kuep(bool disabled);

#ifdef CONFIG_PPC_KUAP
void setup_kuap(bool disabled);
#else
static inline void setup_kuap(bool disabled) { }

static inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return false;
}

static inline void __kuap_assert_locked(void) { }
static inline void __kuap_save_and_lock(struct pt_regs *regs) { }
static inline void kuap_user_restore(struct pt_regs *regs) { }
static inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long amr) { }

static inline unsigned long __kuap_get_and_assert_locked(void)
{
	return 0;
}

/*
 * book3s/64/kup-radix.h defines these functions for the !KUAP case to flush
 * the L1D cache after user accesses. Only include the empty stubs for other
 * platforms.
 */
#ifndef CONFIG_PPC_BOOK3S_64
static inline void __allow_user_access(void __user *to, const void __user *from,
				       unsigned long size, unsigned long dir) { }
static inline void __prevent_user_access(unsigned long dir) { }
static inline unsigned long __prevent_user_access_return(void) { return 0UL; }
static inline void __restore_user_access(unsigned long flags) { }
#endif /* CONFIG_PPC_BOOK3S_64 */
#endif /* CONFIG_PPC_KUAP */

static __always_inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return __bad_kuap_fault(regs, address, is_write);
}

static __always_inline void kuap_assert_locked(void)
{
	__kuap_assert_locked();
}

#ifdef CONFIG_PPC32
static __always_inline void kuap_save_and_lock(struct pt_regs *regs)
{
	__kuap_save_and_lock(regs);
}
#endif

static __always_inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long amr)
{
	__kuap_kernel_restore(regs, amr);
}

static __always_inline unsigned long kuap_get_and_assert_locked(void)
{
	return __kuap_get_and_assert_locked();
}

#ifndef CONFIG_PPC_BOOK3S_64
static __always_inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size, unsigned long dir)
{
	__allow_user_access(to, from, size, dir);
}

static __always_inline void prevent_user_access(unsigned long dir)
{
	__prevent_user_access(dir);
}

static __always_inline unsigned long prevent_user_access_return(void)
{
	return __prevent_user_access_return();
}

static __always_inline void restore_user_access(unsigned long flags)
{
	__restore_user_access(flags);
}
#endif /* CONFIG_PPC_BOOK3S_64 */

static __always_inline void allow_read_from_user(const void __user *from, unsigned long size)
{
	barrier_nospec();
	allow_user_access(NULL, from, size, KUAP_READ);
}

static __always_inline void allow_write_to_user(void __user *to, unsigned long size)
{
	allow_user_access(to, NULL, size, KUAP_WRITE);
}

static __always_inline void allow_read_write_user(void __user *to, const void __user *from,
						  unsigned long size)
{
	barrier_nospec();
	allow_user_access(to, from, size, KUAP_READ_WRITE);
}

static __always_inline void prevent_read_from_user(const void __user *from, unsigned long size)
{
	prevent_user_access(KUAP_READ);
}

static __always_inline void prevent_write_to_user(void __user *to, unsigned long size)
{
	prevent_user_access(KUAP_WRITE);
}

static __always_inline void prevent_read_write_user(void __user *to, const void __user *from,
						    unsigned long size)
{
	prevent_user_access(KUAP_READ_WRITE);
}

static __always_inline void prevent_current_access_user(void)
{
	prevent_user_access(KUAP_READ_WRITE);
}

static __always_inline void prevent_current_read_from_user(void)
{
	prevent_user_access(KUAP_READ);
}

static __always_inline void prevent_current_write_to_user(void)
{
	prevent_user_access(KUAP_WRITE);
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_KUAP_H_ */
