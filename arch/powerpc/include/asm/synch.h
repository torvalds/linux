/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SYNCH_H 
#define _ASM_POWERPC_SYNCH_H 
#ifdef __KERNEL__

#include <asm/cputable.h>
#include <asm/feature-fixups.h>
#include <asm/ppc-opcode.h>

#ifndef __ASSEMBLY__
extern unsigned int __start___lwsync_fixup, __stop___lwsync_fixup;
extern void do_lwsync_fixups(unsigned long value, void *fixup_start,
			     void *fixup_end);

static inline void eieio(void)
{
	if (IS_ENABLED(CONFIG_BOOKE))
		__asm__ __volatile__ ("mbar" : : : "memory");
	else
		__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}

static inline void ppc_after_tlbiel_barrier(void)
{
	asm volatile("ptesync": : :"memory");
	/*
	 * POWER9, POWER10 need a cp_abort after tlbiel to ensure the copy is
	 * invalidated correctly. If this is not done, the paste can take data
	 * from the physical address that was translated at copy time.
	 *
	 * POWER9 in practice does not need this, because address spaces with
	 * accelerators mapped will use tlbie (which does invalidate the copy)
	 * to invalidate translations. It's not possible to limit POWER10 this
	 * way due to local copy-paste.
	 */
	asm volatile(ASM_FTR_IFSET(PPC_CP_ABORT, "", %0) : : "i" (CPU_FTR_ARCH_31) : "memory");
}
#endif /* __ASSEMBLY__ */

#if defined(__powerpc64__)
#    define LWSYNC	lwsync
#elif defined(CONFIG_PPC_E500)
#    define LWSYNC					\
	START_LWSYNC_SECTION(96);			\
	sync;						\
	MAKE_LWSYNC_SECTION_ENTRY(96, __lwsync_fixup);
#else
#    define LWSYNC	sync
#endif

#ifdef CONFIG_SMP
#define __PPC_ACQUIRE_BARRIER				\
	START_LWSYNC_SECTION(97);			\
	isync;						\
	MAKE_LWSYNC_SECTION_ENTRY(97, __lwsync_fixup);
#define PPC_ACQUIRE_BARRIER	 "\n" stringify_in_c(__PPC_ACQUIRE_BARRIER)
#define PPC_RELEASE_BARRIER	 stringify_in_c(LWSYNC) "\n"
#define PPC_ATOMIC_ENTRY_BARRIER "\n" stringify_in_c(sync) "\n"
#define PPC_ATOMIC_EXIT_BARRIER	 "\n" stringify_in_c(sync) "\n"
#else
#define PPC_ACQUIRE_BARRIER
#define PPC_RELEASE_BARRIER
#define PPC_ATOMIC_ENTRY_BARRIER
#define PPC_ATOMIC_EXIT_BARRIER
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_SYNCH_H */
