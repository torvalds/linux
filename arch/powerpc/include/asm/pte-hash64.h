#ifndef _ASM_POWERPC_PTE_HASH64_H
#define _ASM_POWERPC_PTE_HASH64_H
#ifdef __KERNEL__

/*
 * Common bits between 4K and 64K pages in a linux-style PTE.
 * These match the bits in the (hardware-defined) PowerPC PTE as closely
 * as possible. Additional bits may be defined in pgtable-hash64-*.h
 */
#define _PAGE_PRESENT	0x0001 /* software: pte contains a translation */
#define _PAGE_USER	0x0002 /* matches one of the PP bits */
#define _PAGE_FILE	0x0002 /* (!present only) software: pte holds file offset */
#define _PAGE_EXEC	0x0004 /* No execute on POWER4 and newer (we invert) */
#define _PAGE_GUARDED	0x0008
#define _PAGE_COHERENT	0x0010 /* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x0020 /* I: cache inhibit */
#define _PAGE_WRITETHRU	0x0040 /* W: cache write-through */
#define _PAGE_DIRTY	0x0080 /* C: page changed */
#define _PAGE_ACCESSED	0x0100 /* R: page referenced */
#define _PAGE_RW	0x0200 /* software: user write access allowed */
#define _PAGE_BUSY	0x0800 /* software: PTE & hash are busy */

/* Strong Access Ordering */
#define _PAGE_SAO	(_PAGE_WRITETHRU | _PAGE_NO_CACHE | _PAGE_COHERENT)

#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_COHERENT)

#define _PAGE_WRENABLE	(_PAGE_RW | _PAGE_DIRTY)

/* PTEIDX nibble */
#define _PTEIDX_SECONDARY	0x8
#define _PTEIDX_GROUP_IX	0x7

#define PAGE_PROT_BITS	(_PAGE_GUARDED | _PAGE_COHERENT | \
			 _PAGE_NO_CACHE | _PAGE_WRITETHRU |		\
			 _PAGE_4K_PFN | _PAGE_RW | _PAGE_USER |		\
			 _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_EXEC)


#ifdef CONFIG_PPC_64K_PAGES
#include <asm/pte-hash64-64k.h>
#else
#include <asm/pte-hash64-4k.h>
#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_PTE_HASH64_H */
