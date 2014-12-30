#ifndef _ASM_X86_MPX_H
#define _ASM_X86_MPX_H

#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/insn.h>

/*
 * NULL is theoretically a valid place to put the bounds
 * directory, so point this at an invalid address.
 */
#define MPX_INVALID_BOUNDS_DIR	((void __user *)-1)
#define MPX_BNDCFG_ENABLE_FLAG	0x1
#define MPX_BD_ENTRY_VALID_FLAG	0x1

#ifdef CONFIG_X86_64

/* upper 28 bits [47:20] of the virtual address in 64-bit used to
 * index into bounds directory (BD).
 */
#define MPX_BD_ENTRY_OFFSET	28
#define MPX_BD_ENTRY_SHIFT	3
/* bits [19:3] of the virtual address in 64-bit used to index into
 * bounds table (BT).
 */
#define MPX_BT_ENTRY_OFFSET	17
#define MPX_BT_ENTRY_SHIFT	5
#define MPX_IGN_BITS		3
#define MPX_BD_ENTRY_TAIL	3

#else

#define MPX_BD_ENTRY_OFFSET	20
#define MPX_BD_ENTRY_SHIFT	2
#define MPX_BT_ENTRY_OFFSET	10
#define MPX_BT_ENTRY_SHIFT	4
#define MPX_IGN_BITS		2
#define MPX_BD_ENTRY_TAIL	2

#endif

#define MPX_BD_SIZE_BYTES (1UL<<(MPX_BD_ENTRY_OFFSET+MPX_BD_ENTRY_SHIFT))
#define MPX_BT_SIZE_BYTES (1UL<<(MPX_BT_ENTRY_OFFSET+MPX_BT_ENTRY_SHIFT))

#define MPX_BNDSTA_TAIL		2
#define MPX_BNDCFG_TAIL		12
#define MPX_BNDSTA_ADDR_MASK	(~((1UL<<MPX_BNDSTA_TAIL)-1))
#define MPX_BNDCFG_ADDR_MASK	(~((1UL<<MPX_BNDCFG_TAIL)-1))
#define MPX_BT_ADDR_MASK	(~((1UL<<MPX_BD_ENTRY_TAIL)-1))

#define MPX_BNDCFG_ADDR_MASK	(~((1UL<<MPX_BNDCFG_TAIL)-1))
#define MPX_BNDSTA_ERROR_CODE	0x3

#define MPX_BD_ENTRY_MASK	((1<<MPX_BD_ENTRY_OFFSET)-1)
#define MPX_BT_ENTRY_MASK	((1<<MPX_BT_ENTRY_OFFSET)-1)
#define MPX_GET_BD_ENTRY_OFFSET(addr)	((((addr)>>(MPX_BT_ENTRY_OFFSET+ \
		MPX_IGN_BITS)) & MPX_BD_ENTRY_MASK) << MPX_BD_ENTRY_SHIFT)
#define MPX_GET_BT_ENTRY_OFFSET(addr)	((((addr)>>MPX_IGN_BITS) & \
		MPX_BT_ENTRY_MASK) << MPX_BT_ENTRY_SHIFT)

#ifdef CONFIG_X86_INTEL_MPX
siginfo_t *mpx_generate_siginfo(struct pt_regs *regs,
				struct xsave_struct *xsave_buf);
int mpx_handle_bd_fault(struct xsave_struct *xsave_buf);
static inline int kernel_managing_mpx_tables(struct mm_struct *mm)
{
	return (mm->bd_addr != MPX_INVALID_BOUNDS_DIR);
}
static inline void mpx_mm_init(struct mm_struct *mm)
{
	/*
	 * NULL is theoretically a valid place to put the bounds
	 * directory, so point this at an invalid address.
	 */
	mm->bd_addr = MPX_INVALID_BOUNDS_DIR;
}
void mpx_notify_unmap(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long start, unsigned long end);
#else
static inline siginfo_t *mpx_generate_siginfo(struct pt_regs *regs,
					      struct xsave_struct *xsave_buf)
{
	return NULL;
}
static inline int mpx_handle_bd_fault(struct xsave_struct *xsave_buf)
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
#endif /* CONFIG_X86_INTEL_MPX */

#endif /* _ASM_X86_MPX_H */
