#ifdef CONFIG_X86_32
# include <asm/mutex_32.h>
#else
# include <asm/mutex_64.h>
#endif

#ifndef	__ASM_MUTEX_H
#define	__ASM_MUTEX_H
/*
 * For the x86 architecture, it allows any negative number (besides -1) in
 * the mutex count to indicate that some other threads are waiting on the
 * mutex.
 */
#define __ARCH_ALLOW_ANY_NEGATIVE_MUTEX_COUNT	1
#endif
