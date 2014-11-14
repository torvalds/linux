#ifndef _ASM_X86_MPX_H
#define _ASM_X86_MPX_H

#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/insn.h>

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

#else

#define MPX_BD_ENTRY_OFFSET	20
#define MPX_BD_ENTRY_SHIFT	2
#define MPX_BT_ENTRY_OFFSET	10
#define MPX_BT_ENTRY_SHIFT	4
#define MPX_IGN_BITS		2

#endif

#define MPX_BD_SIZE_BYTES (1UL<<(MPX_BD_ENTRY_OFFSET+MPX_BD_ENTRY_SHIFT))
#define MPX_BT_SIZE_BYTES (1UL<<(MPX_BT_ENTRY_OFFSET+MPX_BT_ENTRY_SHIFT))

#define MPX_BNDSTA_ERROR_CODE	0x3

#ifdef CONFIG_X86_INTEL_MPX
siginfo_t *mpx_generate_siginfo(struct pt_regs *regs,
				struct xsave_struct *xsave_buf);
#else
static inline siginfo_t *mpx_generate_siginfo(struct pt_regs *regs,
					      struct xsave_struct *xsave_buf)
{
	return NULL;
}
#endif /* CONFIG_X86_INTEL_MPX */

#endif /* _ASM_X86_MPX_H */
