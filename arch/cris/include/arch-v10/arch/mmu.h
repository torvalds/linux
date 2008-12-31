/*
 * CRIS MMU constants and PTE layout
 */

#ifndef _CRIS_ARCH_MMU_H
#define _CRIS_ARCH_MMU_H

/* type used in struct mm to couple an MMU context to an active mm */

typedef struct
{
  unsigned int page_id;
} mm_context_t;

/* kernel memory segments */

#define KSEG_F 0xf0000000UL
#define KSEG_E 0xe0000000UL
#define KSEG_D 0xd0000000UL
#define KSEG_C 0xc0000000UL
#define KSEG_B 0xb0000000UL
#define KSEG_A 0xa0000000UL
#define KSEG_9 0x90000000UL
#define KSEG_8 0x80000000UL
#define KSEG_7 0x70000000UL
#define KSEG_6 0x60000000UL
#define KSEG_5 0x50000000UL
#define KSEG_4 0x40000000UL
#define KSEG_3 0x30000000UL
#define KSEG_2 0x20000000UL
#define KSEG_1 0x10000000UL
#define KSEG_0 0x00000000UL

/* CRIS PTE bits (see R_TLB_LO in the register description)
 *
 *   Bit:  31-13 12-------4    3        2       1       0  
 *         ________________________________________________
 *        | pfn | reserved | global | valid | kernel | we  |
 *        |_____|__________|________|_______|________|_____|
 *
 * (pfn = physical frame number)
 */

/* Real HW-based PTE bits. We use some synonym names so that
 * things become less confusing in combination with the SW-based
 * bits further below.
 *
 */

#define _PAGE_WE	   (1<<0) /* page is write-enabled */
#define _PAGE_SILENT_WRITE (1<<0) /* synonym */
#define _PAGE_KERNEL	   (1<<1) /* page is kernel only */
#define _PAGE_VALID	   (1<<2) /* page is valid */
#define _PAGE_SILENT_READ  (1<<2) /* synonym */
#define _PAGE_GLOBAL       (1<<3) /* global page - context is ignored */

/* Bits the HW doesn't care about but the kernel uses them in SW */

#define _PAGE_PRESENT   (1<<4)  /* page present in memory */
#define _PAGE_FILE      (1<<5)  /* set: pagecache, unset: swap (when !PRESENT) */
#define _PAGE_ACCESSED	(1<<5)  /* simulated in software using valid bit */
#define _PAGE_MODIFIED	(1<<6)  /* simulated in software using we bit */
#define _PAGE_READ      (1<<7)  /* read-enabled */
#define _PAGE_WRITE     (1<<8)  /* write-enabled */

/* Define some higher level generic page attributes. */

#define __READABLE      (_PAGE_READ | _PAGE_SILENT_READ | _PAGE_ACCESSED)
#define __WRITEABLE     (_PAGE_WRITE | _PAGE_SILENT_WRITE | _PAGE_MODIFIED)

#define _PAGE_TABLE     (_PAGE_PRESENT | __READABLE | __WRITEABLE)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED)

#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_WRITE | \
				 _PAGE_ACCESSED)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | __READABLE)  // | _PAGE_COW
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | __READABLE)
#define PAGE_KERNEL     __pgprot(_PAGE_GLOBAL | _PAGE_KERNEL | \
				 _PAGE_PRESENT | __READABLE | __WRITEABLE)
#define _KERNPG_TABLE   (_PAGE_TABLE | _PAGE_KERNEL)

/*
 * CRIS can't do page protection for execute, and considers read the same.
 * Also, write permissions imply read permissions. This is the closest we can
 * get..
 */

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

#define PTE_FILE_MAX_BITS	26

#endif
