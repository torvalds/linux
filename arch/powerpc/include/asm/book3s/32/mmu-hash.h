/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_MMU_HASH_H_
#define _ASM_POWERPC_BOOK3S_32_MMU_HASH_H_
/*
 * 32-bit hash table MMU support
 */

/*
 * BATs
 */

/* Block size masks */
#define BL_128K	0x000
#define BL_256K 0x001
#define BL_512K 0x003
#define BL_1M   0x007
#define BL_2M   0x00F
#define BL_4M   0x01F
#define BL_8M   0x03F
#define BL_16M  0x07F
#define BL_32M  0x0FF
#define BL_64M  0x1FF
#define BL_128M 0x3FF
#define BL_256M 0x7FF

/* BAT Access Protection */
#define BPP_XX	0x00		/* No access */
#define BPP_RX	0x01		/* Read only */
#define BPP_RW	0x02		/* Read/write */

#ifndef __ASSEMBLY__
/* Contort a phys_addr_t into the right format/bits for a BAT */
#ifdef CONFIG_PHYS_64BIT
#define BAT_PHYS_ADDR(x) ((u32)((x & 0x00000000fffe0000ULL) | \
				((x & 0x0000000e00000000ULL) >> 24) | \
				((x & 0x0000000100000000ULL) >> 30)))
#else
#define BAT_PHYS_ADDR(x) (x)
#endif

struct ppc_bat {
	u32 batu;
	u32 batl;
};
#endif /* !__ASSEMBLY__ */

/*
 * Hash table
 */

/* Values for PP (assumes Ks=0, Kp=1) */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

#ifndef __ASSEMBLY__

/*
 * Hardware Page Table Entry
 * Note that the xpn and x bitfields are used only by processors that
 * support extended addressing; otherwise, those bits are reserved.
 */
struct hash_pte {
	unsigned long v:1;	/* Entry is valid */
	unsigned long vsid:24;	/* Virtual segment identifier */
	unsigned long h:1;	/* Hash algorithm indicator */
	unsigned long api:6;	/* Abbreviated page index */
	unsigned long rpn:20;	/* Real (physical) page number */
	unsigned long xpn:3;	/* Real page number bits 0-2, optional */
	unsigned long r:1;	/* Referenced */
	unsigned long c:1;	/* Changed */
	unsigned long w:1;	/* Write-thru cache mode */
	unsigned long i:1;	/* Cache inhibited */
	unsigned long m:1;	/* Memory coherence */
	unsigned long g:1;	/* Guarded */
	unsigned long x:1;	/* Real page number bit 3, optional */
	unsigned long pp:2;	/* Page protection */
};

typedef struct {
	unsigned long id;
	unsigned long vdso_base;
} mm_context_t;

#endif /* !__ASSEMBLY__ */

/* We happily ignore the smaller BATs on 601, we don't actually use
 * those definitions on hash32 at the moment anyway
 */
#define mmu_virtual_psize	MMU_PAGE_4K
#define mmu_linear_psize	MMU_PAGE_256M

#endif /* _ASM_POWERPC_BOOK3S_32_MMU_HASH_H_ */
