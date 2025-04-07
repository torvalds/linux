/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#include <linux/mm.h>
#include <linux/mm_types.h>
#include <vdso/datapage.h>

#include <asm/barrier.h>

/*
 * struct loongarch_vdso_info - Details of a VDSO image.
 * @vdso: Pointer to VDSO image (page-aligned).
 * @size: Size of the VDSO image (page-aligned).
 * @off_rt_sigreturn: Offset of the rt_sigreturn() trampoline.
 * @code_mapping: Special mapping structure for vdso code.
 * @code_mapping: Special mapping structure for vdso data.
 *
 * This structure contains details of a VDSO image, including the image data
 * and offsets of certain symbols required by the kernel. It is generated as
 * part of the VDSO build process, aside from the mapping page array, which is
 * populated at runtime.
 */
struct loongarch_vdso_info {
	void *vdso;
	unsigned long size;
	unsigned long offset_sigreturn;
	struct vm_special_mapping code_mapping;
};

extern struct loongarch_vdso_info vdso_info;

#endif /* __ASM_VDSO_H */
