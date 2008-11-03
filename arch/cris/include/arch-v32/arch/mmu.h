#ifndef _ASM_CRIS_ARCH_MMU_H
#define _ASM_CRIS_ARCH_MMU_H

/* MMU context type. */
typedef struct
{
  unsigned int page_id;
} mm_context_t;

/* Kernel memory segments. */
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

/*
 * CRISv32 PTE bits:
 *
 *  Bit:  31-13  12-5     4        3       2        1        0
 *       +-----+------+--------+-------+--------+-------+---------+
 *       | pfn | zero | global | valid | kernel | write | execute |
 *       +-----+------+--------+-------+--------+-------+---------+
 */

/*
 * Defines for accessing the bits. Also define some synonyms for use with
 * the software-based defined bits below.
 */
#define _PAGE_EXECUTE       (1 << 0)	/* Execution bit. */
#define _PAGE_WE            (1 << 1)	/* Write bit. */
#define _PAGE_SILENT_WRITE  (1 << 1)	/* Same as above. */
#define _PAGE_KERNEL        (1 << 2)	/* Kernel mode page. */
#define _PAGE_VALID         (1 << 3)	/* Page is valid. */
#define _PAGE_SILENT_READ   (1 << 3)	/* Same as above. */
#define _PAGE_GLOBAL        (1 << 4)	/* Global page. */

/*
 * The hardware doesn't care about these bits, but the kernel uses them in
 * software.
 */
#define _PAGE_PRESENT   (1 << 5)   /* Page is present in memory. */
#define _PAGE_FILE      (1 << 6)   /* 1=pagecache, 0=swap (when !present) */
#define _PAGE_ACCESSED  (1 << 6)   /* Simulated in software using valid bit. */
#define _PAGE_MODIFIED  (1 << 7)   /* Simulated in software using we bit. */
#define _PAGE_READ      (1 << 8)   /* Read enabled. */
#define _PAGE_WRITE     (1 << 9)   /* Write enabled. */

/* Define some higher level generic page attributes. */
#define __READABLE      (_PAGE_READ | _PAGE_SILENT_READ | _PAGE_ACCESSED)
#define __WRITEABLE     (_PAGE_WRITE | _PAGE_SILENT_WRITE | _PAGE_MODIFIED)

#define _PAGE_TABLE	(_PAGE_PRESENT | __READABLE | __WRITEABLE)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED)

#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_WRITE | \
                                 _PAGE_ACCESSED)
#define PAGE_SHARED_EXEC __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_WRITE | \
                                  _PAGE_ACCESSED | _PAGE_EXECUTE)

#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | __READABLE)
#define PAGE_READONLY_EXEC __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_EXECUTE | _PAGE_ACCESSED)

#define PAGE_COPY       __pgprot(_PAGE_PRESENT | __READABLE)
#define PAGE_COPY_EXEC	__pgprot(_PAGE_PRESENT | __READABLE | _PAGE_EXECUTE)
#define PAGE_KERNEL     __pgprot(_PAGE_GLOBAL | _PAGE_KERNEL | \
                                 _PAGE_PRESENT | __READABLE | __WRITEABLE)
#define PAGE_KERNEL_EXEC __pgprot(_PAGE_GLOBAL | _PAGE_KERNEL | _PAGE_EXECUTE | \
                                 _PAGE_PRESENT | __READABLE | __WRITEABLE)
#define PAGE_SIGNAL_TRAMPOLINE __pgprot(_PAGE_GLOBAL | _PAGE_EXECUTE | \
                                       _PAGE_PRESENT | __READABLE)

#define _KERNPG_TABLE   (_PAGE_TABLE | _PAGE_KERNEL)

/* CRISv32 can do page protection for execute.
 * Write permissions imply read permissions.
 * Note that the numbers are in Execute-Write-Read order!
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY_EXEC
#define __P101  PAGE_READONLY_EXEC
#define __P110  PAGE_COPY_EXEC
#define __P111  PAGE_COPY_EXEC

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY_EXEC
#define __S101  PAGE_READONLY_EXEC
#define __S110  PAGE_SHARED_EXEC
#define __S111  PAGE_SHARED_EXEC

#define PTE_FILE_MAX_BITS	25

#endif /* _ASM_CRIS_ARCH_MMU_H */
