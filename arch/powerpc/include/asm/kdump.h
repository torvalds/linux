#ifndef _PPC64_KDUMP_H
#define _PPC64_KDUMP_H

#include <asm/page.h>

#define KDUMP_KERNELBASE	0x2000000

/* How many bytes to reserve at zero for kdump. The reserve limit should
 * be greater or equal to the trampoline's end address.
 * Reserve to the end of the FWNMI area, see head_64.S */
#define KDUMP_RESERVE_LIMIT	0x10000 /* 64K */

#ifdef CONFIG_CRASH_DUMP

/*
 * On PPC64 translation is disabled during trampoline setup, so we use
 * physical addresses. Though on PPC32 translation is already enabled,
 * so we can't do the same. Luckily create_trampoline() creates relative
 * branches, so we can just add the PAGE_OFFSET and don't worry about it.
 */
#ifdef __powerpc64__
#define KDUMP_TRAMPOLINE_START	0x0100
#define KDUMP_TRAMPOLINE_END	0x3000
#else
#define KDUMP_TRAMPOLINE_START	(0x0100 + PAGE_OFFSET)
#define KDUMP_TRAMPOLINE_END	(0x3000 + PAGE_OFFSET)
#endif /* __powerpc64__ */

#define KDUMP_MIN_TCE_ENTRIES	2048

#endif /* CONFIG_CRASH_DUMP */

#ifndef __ASSEMBLY__

#if defined(CONFIG_CRASH_DUMP) && !defined(CONFIG_RELOCATABLE)
extern void reserve_kdump_trampoline(void);
extern void setup_kdump_trampoline(void);
#else
/* !CRASH_DUMP || RELOCATABLE */
static inline void reserve_kdump_trampoline(void) { ; }
static inline void setup_kdump_trampoline(void) { ; }
#endif

#endif /* __ASSEMBLY__ */

#endif /* __PPC64_KDUMP_H */
