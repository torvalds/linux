#ifndef _ASM_POWERPC_SYNCH_H 
#define _ASM_POWERPC_SYNCH_H 
#ifdef __KERNEL__

#include <linux/stringify.h>
#include <asm/feature-fixups.h>

#if defined(__powerpc64__) || defined(CONFIG_PPC_E500MC)
#define __SUBARCH_HAS_LWSYNC
#endif

#ifndef __ASSEMBLY__
extern unsigned int __start___lwsync_fixup, __stop___lwsync_fixup;
extern void do_lwsync_fixups(unsigned long value, void *fixup_start,
			     void *fixup_end);

static inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}
#endif /* __ASSEMBLY__ */

#if defined(__powerpc64__)
#    define LWSYNC	lwsync
#elif defined(CONFIG_E500)
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
