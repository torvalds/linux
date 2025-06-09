/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H

#include <asm/page_types.h>
#include <linux/linkage.h>
#include <linux/init.h>

#ifndef __ASSEMBLER__

#include <linux/mm_types.h>

struct vdso_image {
	void *data;
	unsigned long size;   /* Always a multiple of PAGE_SIZE */

	unsigned long alt, alt_len;
	unsigned long extable_base, extable_len;
	const void *extable;

	long sym_VDSO32_NOTE_MASK;
	long sym___kernel_sigreturn;
	long sym___kernel_rt_sigreturn;
	long sym___kernel_vsyscall;
	long sym_int80_landing_pad;
	long sym_vdso32_sigreturn_landing_pad;
	long sym_vdso32_rt_sigreturn_landing_pad;
};

extern const struct vdso_image vdso_image_64;
extern const struct vdso_image vdso_image_x32;
extern const struct vdso_image vdso_image_32;

extern int __init init_vdso_image(const struct vdso_image *image);

extern int map_vdso_once(const struct vdso_image *image, unsigned long addr);

extern bool fixup_vdso_exception(struct pt_regs *regs, int trapnr,
				 unsigned long error_code,
				 unsigned long fault_addr);
#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_VDSO_H */
