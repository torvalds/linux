/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#define KUAP_READ	1
#define KUAP_WRITE	2
#define KUAP_READ_WRITE	(KUAP_READ | KUAP_WRITE)

#ifndef __ASSEMBLY__
#include <linux/types.h>

static __always_inline bool kuap_is_disabled(void);
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/book3s/64/kup.h>
#endif

#ifdef CONFIG_PPC_8xx
#include <asm/nohash/32/kup-8xx.h>
#endif

#ifdef CONFIG_BOOKE_OR_40x
#include <asm/nohash/kup-booke.h>
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

static __always_inline bool kuap_is_disabled(void)
{
	return !mmu_has_feature(MMU_FTR_KUAP);
}
#else
static inline void setup_kuap(bool disabled) { }

static __always_inline bool kuap_is_disabled(void) { return true; }

static __always_inline bool
__bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	return false;
}

static __always_inline void kuap_user_restore(struct pt_regs *regs) { }
static __always_inline void __kuap_kernel_restore(struct pt_regs *regs, unsigned long amr) { }

/*
 * book3s/64/kup-radix.h defines these functions for the !KUAP case to flush
 * the L1D cache after user accesses. Only include the empty stubs for other
 * platforms.
 */
#ifndef CONFIG_PPC_BOOK3S_64
static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size, unsigned long dir) { }
static __always_inline void prevent_user_access(unsigned long dir) { }
static __always_inline unsigned long prevent_user_access_return(void) { return 0UL; }
static __always_inline void restore_user_access(unsigned long flags) { }
#endif /* CONFIG_PPC_BOOK3S_64 */
#endif /* CONFIG_PPC_KUAP */

static __always_inline bool
bad_kuap_fault(struct pt_regs *regs, unsigned long address, bool is_write)
{
	if (kuap_is_disabled())
		return false;

	return __bad_kuap_fault(regs, address, is_write);
}

static __always_inline void kuap_lock(void)
{
#ifdef __kuap_lock
	if (kuap_is_disabled())
		return;

	__kuap_lock();
#endif
}

static __always_inline void kuap_save_and_lock(struct pt_regs *regs)
{
#ifdef __kuap_save_and_lock
	if (kuap_is_disabled())
		return;

	__kuap_save_and_lock(regs);
#endif
}

static __always_inline void kuap_kernel_restore(struct pt_regs *regs, unsigned long amr)
{
	if (kuap_is_disabled())
		return;

	__kuap_kernel_restore(regs, amr);
}

static __always_inline unsigned long kuap_get_and_assert_locked(void)
{
#ifdef __kuap_get_and_assert_locked
	if (!kuap_is_disabled())
		return __kuap_get_and_assert_locked();
#endif
	return 0;
}

static __always_inline void kuap_assert_locked(void)
{
	if (IS_ENABLED(CONFIG_PPC_KUAP_DEBUG))
		kuap_get_and_assert_locked();
}

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
