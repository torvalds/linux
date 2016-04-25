#ifndef _ASM_POWERPC_BOOK3S_32_HASH_H
#define _ASM_POWERPC_BOOK3S_32_HASH_H
#ifdef __KERNEL__

/*
 * The "classic" 32-bit implementation of the PowerPC MMU uses a hash
 * table containing PTEs, together with a set of 16 segment registers,
 * to define the virtual to physical address mapping.
 *
 * We use the hash table as an extended TLB, i.e. a cache of currently
 * active mappings.  We maintain a two-level page table tree, much
 * like that used by the i386, for the sake of the Linux memory
 * management code.  Low-level assembler code in hash_low_32.S
 * (procedure hash_page) is responsible for extracting ptes from the
 * tree and putting them into the hash table when necessary, and
 * updating the accessed and modified bits in the page table tree.
 */

#define _PAGE_PRESENT	0x001	/* software: pte contains a translation */
#define _PAGE_HASHPTE	0x002	/* hash_page has made an HPTE for this pte */
#define _PAGE_USER	0x004	/* usermode access allowed */
#define _PAGE_GUARDED	0x008	/* G: prohibit speculative access */
#define _PAGE_COHERENT	0x010	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define _PAGE_DIRTY	0x080	/* C: page changed */
#define _PAGE_ACCESSED	0x100	/* R: page referenced */
#define _PAGE_RW	0x400	/* software: user write access allowed */
#define _PAGE_SPECIAL	0x800	/* software: Special page */

#ifdef CONFIG_PTE_64BIT
/* We never clear the high word of the pte */
#define _PTE_NONE_MASK	(0xffffffff00000000ULL | _PAGE_HASHPTE)
#else
#define _PTE_NONE_MASK	_PAGE_HASHPTE
#endif

#define _PMD_PRESENT	0
#define _PMD_PRESENT_MASK (PAGE_MASK)
#define _PMD_BAD	(~PAGE_MASK)

/* Hash table based platforms need atomic updates of the linux PTE */
#define PTE_ATOMIC_UPDATES	1

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_BOOK3S_32_HASH_H */
