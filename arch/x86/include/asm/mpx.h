#ifndef _ASM_X86_MPX_H
#define _ASM_X86_MPX_H

#include <linux/types.h>
#include <linux/mm_types.h>

#include <asm/ptrace.h>
#include <asm/insn.h>

/*
 * NULL is theoretically a valid place to put the bounds
 * directory, so point this at an invalid address.
 */
#define MPX_INVALID_BOUNDS_DIR	((void __user *)-1)
#define MPX_BNDCFG_ENABLE_FLAG	0x1
#define MPX_BD_ENTRY_VALID_FLAG	0x1

/*
 * The upper 28 bits [47:20] of the virtual address in 64-bit
 * are used to index into bounds directory (BD).
 *
 * The directory is 2G (2^31) in size, and with 8-byte entries
 * it has 2^28 entries.
 */
#define MPX_BD_SIZE_BYTES_64	(1UL<<31)
#define MPX_BD_ENTRY_BYTES_64	8
#define MPX_BD_NR_ENTRIES_64	(MPX_BD_SIZE_BYTES_64/MPX_BD_ENTRY_BYTES_64)

/*
 * The 32-bit directory is 4MB (2^22) in size, and with 4-byte
 * entries it has 2^20 entries.
 */
#define MPX_BD_SIZE_BYTES_32	(1UL<<22)
#define MPX_BD_ENTRY_BYTES_32	4
#define MPX_BD_NR_ENTRIES_32	(MPX_BD_SIZE_BYTES_32/MPX_BD_ENTRY_BYTES_32)

/*
 * A 64-bit table is 4MB total in size, and an entry is
 * 4 64-bit pointers in size.
 */
#define MPX_BT_SIZE_BYTES_64	(1UL<<22)
#define MPX_BT_ENTRY_BYTES_64	32
#define MPX_BT_NR_ENTRIES_64	(MPX_BT_SIZE_BYTES_64/MPX_BT_ENTRY_BYTES_64)

/*
 * A 32-bit table is 16kB total in size, and an entry is
 * 4 32-bit pointers in size.
 */
#define MPX_BT_SIZE_BYTES_32	(1UL<<14)
#define MPX_BT_ENTRY_BYTES_32	16
#define MPX_BT_NR_ENTRIES_32	(MPX_BT_SIZE_BYTES_32/MPX_BT_ENTRY_BYTES_32)

#define MPX_BNDSTA_TAIL		2
#define MPX_BNDCFG_TAIL		12
#define MPX_BNDSTA_ADDR_MASK	(~((1UL<<MPX_BNDSTA_TAIL)-1))
#define MPX_BNDCFG_ADDR_MASK	(~((1UL<<MPX_BNDCFG_TAIL)-1))
#define MPX_BNDSTA_ERROR_CODE	0x3

#ifdef CONFIG_X86_INTEL_MPX
siginfo_t *mpx_generate_siginfo(struct pt_regs *regs);
int mpx_handle_bd_fault(void);
static inline int kernel_managing_mpx_tables(struct mm_struct *mm)
{
	return (mm->context.bd_addr != MPX_INVALID_BOUNDS_DIR);
}
static inline void mpx_mm_init(struct mm_struct *mm)
{
	/*
	 * NULL is theoretically a valid place to put the bounds
	 * directory, so point this at an invalid address.
	 */
	mm->context.bd_addr = MPX_INVALID_BOUNDS_DIR;
}
void mpx_notify_unmap(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long start, unsigned long end);

unsigned long mpx_unmapped_area_check(unsigned long addr, unsigned long len,
		unsigned long flags);
#else
static inline siginfo_t *mpx_generate_siginfo(struct pt_regs *regs)
{
	return NULL;
}
static inline int mpx_handle_bd_fault(void)
{
	return -EINVAL;
}
static inline int kernel_managing_mpx_tables(struct mm_struct *mm)
{
	return 0;
}
static inline void mpx_mm_init(struct mm_struct *mm)
{
}
static inline void mpx_notify_unmap(struct mm_struct *mm,
				    struct vm_area_struct *vma,
				    unsigned long start, unsigned long end)
{
}

static inline unsigned long mpx_unmapped_area_check(unsigned long addr,
		unsigned long len, unsigned long flags)
{
	return addr;
}
#endif /* CONFIG_X86_INTEL_MPX */

#endif /* _ASM_X86_MPX_H */
