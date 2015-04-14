#ifndef _ASM_POWERPC_PTE_HASH64_H
#define _ASM_POWERPC_PTE_HASH64_H
#ifdef __KERNEL__

/*
 * Common bits between 4K and 64K pages in a linux-style PTE.
 * These match the bits in the (hardware-defined) PowerPC PTE as closely
 * as possible. Additional bits may be defined in pgtable-hash64-*.h
 *
 * Note: We only support user read/write permissions. Supervisor always
 * have full read/write to pages above PAGE_OFFSET (pages below that
 * always use the user access permissions).
 *
 * We could create separate kernel read-only if we used the 3 PP bits
 * combinations that newer processors provide but we currently don't.
 */
#define _PAGE_PRESENT		0x0001 /* software: pte contains a translation */
#define _PAGE_USER		0x0002 /* matches one of the PP bits */
#define _PAGE_EXEC		0x0004 /* No execute on POWER4 and newer (we invert) */
#define _PAGE_GUARDED		0x0008
/* We can derive Memory coherence from _PAGE_NO_CACHE */
#define _PAGE_NO_CACHE		0x0020 /* I: cache inhibit */
#define _PAGE_WRITETHRU		0x0040 /* W: cache write-through */
#define _PAGE_DIRTY		0x0080 /* C: page changed */
#define _PAGE_ACCESSED		0x0100 /* R: page referenced */
#define _PAGE_RW		0x0200 /* software: user write access allowed */
#define _PAGE_BUSY		0x0800 /* software: PTE & hash are busy */

/* No separate kernel read-only */
#define _PAGE_KERNEL_RW		(_PAGE_RW | _PAGE_DIRTY) /* user access blocked by key */
#define _PAGE_KERNEL_RO		 _PAGE_KERNEL_RW

/* Strong Access Ordering */
#define _PAGE_SAO		(_PAGE_WRITETHRU | _PAGE_NO_CACHE | _PAGE_COHERENT)

/* No page size encoding in the linux PTE */
#define _PAGE_PSIZE		0

/* PTEIDX nibble */
#define _PTEIDX_SECONDARY	0x8
#define _PTEIDX_GROUP_IX	0x7

/* Hash table based platforms need atomic updates of the linux PTE */
#define PTE_ATOMIC_UPDATES	1

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/pte-hash64-64k.h>
#else
#include <asm/pte-hash64-4k.h>
#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_PTE_HASH64_H */
