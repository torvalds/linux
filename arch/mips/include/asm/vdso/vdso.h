/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/sgidefs.h>

#if _MIPS_SIM != _MIPS_SIM_ABI64 && defined(CONFIG_64BIT)

/* Building 32-bit VDSO for the 64-bit kernel. Fake a 32-bit Kconfig. */
#undef CONFIG_64BIT
#define CONFIG_32BIT 1
#ifndef __ASSEMBLY__
#include <asm-generic/atomic64.h>
#endif
#endif

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/page.h>
#include <asm/vdso.h>

static inline unsigned long get_vdso_base(void)
{
	unsigned long addr;

	/*
	 * We can't use cpu_has_mips_r6 since it needs the cpu_data[]
	 * kernel symbol.
	 */
#ifdef CONFIG_CPU_MIPSR6
	/*
	 * lapc <symbol> is an alias to addiupc reg, <symbol> - .
	 *
	 * We can't use addiupc because there is no label-label
	 * support for the addiupc reloc
	 */
	__asm__("lapc	%0, _start			\n"
		: "=r" (addr) : :);
#else
	/*
	 * Get the base load address of the VDSO. We have to avoid generating
	 * relocations and references to the GOT because ld.so does not peform
	 * relocations on the VDSO. We use the current offset from the VDSO base
	 * and perform a PC-relative branch which gives the absolute address in
	 * ra, and take the difference. The assembler chokes on
	 * "li %0, _start - .", so embed the offset as a word and branch over
	 * it.
	 *
	 */

	__asm__(
	"	.set push				\n"
	"	.set noreorder				\n"
	"	bal	1f				\n"
	"	 nop					\n"
	"	.word	_start - .			\n"
	"1:	lw	%0, 0($31)			\n"
	"	" STR(PTR_ADDU) " %0, $31, %0		\n"
	"	.set pop				\n"
	: "=r" (addr)
	:
	: "$31");
#endif /* CONFIG_CPU_MIPSR6 */

	return addr;
}

static inline const struct vdso_data *get_vdso_data(void)
{
	return (const struct vdso_data *)(get_vdso_base() - PAGE_SIZE);
}

#ifdef CONFIG_CLKSRC_MIPS_GIC

static inline void __iomem *get_gic(const struct vdso_data *data)
{
	return (void __iomem *)data - PAGE_SIZE;
}

#endif /* CONFIG_CLKSRC_MIPS_GIC */

#endif /* __ASSEMBLY__ */
