/*
 * Helpfile for jazzdma.c -- Mips Jazz R4030 DMA controller support
 */
#ifndef _ASM_JAZZDMA_H
#define _ASM_JAZZDMA_H

/*
 * Prototypes and macros
 */
extern unsigned long vdma_alloc(unsigned long paddr, unsigned long size);
extern int vdma_free(unsigned long laddr);
extern int vdma_remap(unsigned long laddr, unsigned long paddr,
		      unsigned long size);
extern unsigned long vdma_phys2log(unsigned long paddr);
extern unsigned long vdma_log2phys(unsigned long laddr);
extern void vdma_stats(void);		/* for debugging only */

extern void vdma_enable(int channel);
extern void vdma_disable(int channel);
extern void vdma_set_mode(int channel, int mode);
extern void vdma_set_addr(int channel, long addr);
extern void vdma_set_count(int channel, int count);
extern int vdma_get_residue(int channel);
extern int vdma_get_enable(int channel);

/*
 * some definitions used by the driver functions
 */
#define VDMA_PAGESIZE		4096
#define VDMA_PGTBL_ENTRIES	4096
#define VDMA_PGTBL_SIZE		(sizeof(VDMA_PGTBL_ENTRY) * VDMA_PGTBL_ENTRIES)
#define VDMA_PAGE_EMPTY		0xff000000

/*
 * Macros to get page no. and offset of a given address
 * Note that VDMA_PAGE() works for physical addresses only
 */
#define VDMA_PAGE(a)		((unsigned int)(a) >> 12)
#define VDMA_OFFSET(a)		((unsigned int)(a) & (VDMA_PAGESIZE-1))

/*
 * error code returned by vdma_alloc()
 * (See also arch/mips/kernel/jazzdma.c)
 */
#define VDMA_ERROR		0xffffffff

/*
 * VDMA pagetable entry description
 */
typedef volatile struct VDMA_PGTBL_ENTRY {
	unsigned int frame;		/* physical frame no. */
	unsigned int owner;		/* owner of this entry (0=free) */
} VDMA_PGTBL_ENTRY;


/*
 * DMA channel control registers
 * in the R4030 MCT_ADR chip
 */
#define JAZZ_R4030_CHNL_MODE	0xE0000100	/* 8 DMA Channel Mode Registers, */
						/* 0xE0000100,120,140... */
#define JAZZ_R4030_CHNL_ENABLE	0xE0000108	/* 8 DMA Channel Enable Regs, */
						/* 0xE0000108,128,148... */
#define JAZZ_R4030_CHNL_COUNT	0xE0000110	/* 8 DMA Channel Byte Cnt Regs, */
						/* 0xE0000110,130,150... */
#define JAZZ_R4030_CHNL_ADDR	0xE0000118	/* 8 DMA Channel Address Regs, */
						/* 0xE0000118,138,158... */

/* channel enable register bits */

#define R4030_CHNL_ENABLE	 (1<<0)
#define R4030_CHNL_WRITE	 (1<<1)
#define R4030_TC_INTR		 (1<<8)
#define R4030_MEM_INTR		 (1<<9)
#define R4030_ADDR_INTR		 (1<<10)

/*
 * Channel mode register bits
 */
#define R4030_MODE_ATIME_40	 (0) /* device access time on remote bus */
#define R4030_MODE_ATIME_80	 (1)
#define R4030_MODE_ATIME_120	 (2)
#define R4030_MODE_ATIME_160	 (3)
#define R4030_MODE_ATIME_200	 (4)
#define R4030_MODE_ATIME_240	 (5)
#define R4030_MODE_ATIME_280	 (6)
#define R4030_MODE_ATIME_320	 (7)
#define R4030_MODE_WIDTH_8	 (1<<3) /* device data bus width */
#define R4030_MODE_WIDTH_16	 (2<<3)
#define R4030_MODE_WIDTH_32	 (3<<3)
#define R4030_MODE_INTR_EN	 (1<<5)
#define R4030_MODE_BURST	 (1<<6) /* Rev. 2 only */
#define R4030_MODE_FAST_ACK	 (1<<7) /* Rev. 2 only */

#endif /* _ASM_JAZZDMA_H */
