#ifndef _ASM_POWERPC_PTE_BOOK3E_H
#define _ASM_POWERPC_PTE_BOOK3E_H
#ifdef __KERNEL__

/* PTE bit definitions for processors compliant to the Book3E
 * architecture 2.06 or later. The position of the PTE bits
 * matches the HW definition of the optional Embedded Page Table
 * category.
 */

/* Architected bits */
#define _PAGE_PRESENT	0x000001 /* software: pte contains a translation */
#define _PAGE_FILE	0x000002 /* (!present only) software: pte holds file offset */
#define _PAGE_SW1	0x000002
#define _PAGE_BAP_SR	0x000004
#define _PAGE_BAP_UR	0x000008
#define _PAGE_BAP_SW	0x000010
#define _PAGE_BAP_UW	0x000020
#define _PAGE_BAP_SX	0x000040
#define _PAGE_BAP_UX	0x000080
#define _PAGE_PSIZE_MSK	0x000f00
#define _PAGE_PSIZE_4K	0x000200
#define _PAGE_PSIZE_64K	0x000600
#define _PAGE_PSIZE_1M	0x000a00
#define _PAGE_PSIZE_16M	0x000e00
#define _PAGE_DIRTY	0x001000 /* C: page changed */
#define _PAGE_SW0	0x002000
#define _PAGE_U3	0x004000
#define _PAGE_U2	0x008000
#define _PAGE_U1	0x010000
#define _PAGE_U0	0x020000
#define _PAGE_ACCESSED	0x040000
#define _PAGE_LENDIAN	0x080000
#define _PAGE_GUARDED	0x100000
#define _PAGE_COHERENT	0x200000 /* M: enforce memory coherence */
#define _PAGE_NO_CACHE	0x400000 /* I: cache inhibit */
#define _PAGE_WRITETHRU	0x800000 /* W: cache write-through */

/* "Higher level" linux bit combinations */
#define _PAGE_EXEC		_PAGE_BAP_UX /* .. and was cache cleaned */
#define _PAGE_RW		(_PAGE_BAP_SW | _PAGE_BAP_UW) /* User write permission */
#define _PAGE_KERNEL_RW		(_PAGE_BAP_SW | _PAGE_BAP_SR | _PAGE_DIRTY)
#define _PAGE_KERNEL_RO		(_PAGE_BAP_SR)
#define _PAGE_KERNEL_RWX	(_PAGE_BAP_SW | _PAGE_BAP_SR | _PAGE_DIRTY | _PAGE_BAP_SX)
#define _PAGE_KERNEL_ROX	(_PAGE_BAP_SR | _PAGE_BAP_SX)
#define _PAGE_USER		(_PAGE_BAP_UR | _PAGE_BAP_SR) /* Can be read */

#define _PAGE_HASHPTE	0
#define _PAGE_BUSY	0

#define _PAGE_SPECIAL	_PAGE_SW0

/* Flags to be preserved on PTE modifications */
#define _PAGE_HPTEFLAGS	_PAGE_BUSY

/* Base page size */
#ifdef CONFIG_PPC_64K_PAGES
#define _PAGE_PSIZE	_PAGE_PSIZE_64K
#define PTE_RPN_SHIFT	(28)
#else
#define _PAGE_PSIZE	_PAGE_PSIZE_4K
#define	PTE_RPN_SHIFT	(24)
#endif

/* On 32-bit, we never clear the top part of the PTE */
#ifdef CONFIG_PPC32
#define _PTE_NONE_MASK	0xffffffff00000000ULL
#endif

#endif /* __KERNEL__ */
#endif /*  _ASM_POWERPC_PTE_FSL_BOOKE_H */
