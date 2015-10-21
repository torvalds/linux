/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#include <linux/mm_types.h>

/**
 * struct mips_vdso_image - Details of a VDSO image.
 * @data: Pointer to VDSO image data (page-aligned).
 * @size: Size of the VDSO image data (page-aligned).
 * @off_sigreturn: Offset of the sigreturn() trampoline.
 * @off_rt_sigreturn: Offset of the rt_sigreturn() trampoline.
 * @mapping: Special mapping structure.
 *
 * This structure contains details of a VDSO image, including the image data
 * and offsets of certain symbols required by the kernel. It is generated as
 * part of the VDSO build process, aside from the mapping page array, which is
 * populated at runtime.
 */
struct mips_vdso_image {
	void *data;
	unsigned long size;

	unsigned long off_sigreturn;
	unsigned long off_rt_sigreturn;

	struct vm_special_mapping mapping;
};

/*
 * The following structures are auto-generated as part of the build for each
 * ABI by genvdso, see arch/mips/vdso/Makefile.
 */

extern struct mips_vdso_image vdso_image;

#ifdef CONFIG_MIPS32_O32
extern struct mips_vdso_image vdso_image_o32;
#endif

#ifdef CONFIG_MIPS32_N32
extern struct mips_vdso_image vdso_image_n32;
#endif

/**
 * union mips_vdso_data - Data provided by the kernel for the VDSO.
 *
 * This structure contains data needed by functions within the VDSO. It is
 * populated by the kernel and mapped read-only into user memory.
 *
 * Note: Care should be taken when modifying as the layout must remain the same
 * for both 64- and 32-bit (for 32-bit userland on 64-bit kernel).
 */
union mips_vdso_data {
	struct {
	};

	u8 page[PAGE_SIZE];
};

#endif /* __ASM_VDSO_H */
