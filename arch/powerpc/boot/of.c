// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Paul Mackerras 1997.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"

#include "of.h"

/* Value picked to match that used by yaboot */
#define PROG_START	0x01400000	/* only used on 64-bit systems */
#define RAM_END		(512<<20)	/* Fixme: use OF */
#define	ONE_MB		0x100000



static unsigned long claim_base;

void epapr_platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
			 unsigned long r6, unsigned long r7);

static void *of_try_claim(unsigned long size)
{
	unsigned long addr = 0;

	if (claim_base == 0)
		claim_base = _ALIGN_UP((unsigned long)_end, ONE_MB);

	for(; claim_base < RAM_END; claim_base += ONE_MB) {
#ifdef DEBUG
		printf("    trying: 0x%08lx\n\r", claim_base);
#endif
		addr = (unsigned long) of_claim(claim_base, size, 0);
		if (addr != PROM_ERROR)
			break;
	}
	if (addr == 0)
		return NULL;
	claim_base = PAGE_ALIGN(claim_base + size);
	return (void *)addr;
}

static void of_image_hdr(const void *hdr)
{
	const Elf64_Ehdr *elf64 = hdr;

	if (elf64->e_ident[EI_CLASS] == ELFCLASS64) {
		/*
		 * Maintain a "magic" minimum address. This keeps some older
		 * firmware platforms running.
		 */
		if (claim_base < PROG_START)
			claim_base = PROG_START;
	}
}

static void of_platform_init(unsigned long a1, unsigned long a2, void *promptr)
{
	platform_ops.image_hdr = of_image_hdr;
	platform_ops.malloc = of_try_claim;
	platform_ops.exit = of_exit;
	platform_ops.vmlinux_alloc = of_vmlinux_alloc;

	dt_ops.finddevice = of_finddevice;
	dt_ops.getprop = of_getprop;
	dt_ops.setprop = of_setprop;

	of_console_init();

	of_init(promptr);
	loader_info.promptr = promptr;
	if (a1 && a2 && a2 != 0xdeadbeef) {
		loader_info.initrd_addr = a1;
		loader_info.initrd_size = a2;
	}
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	/* Detect OF vs. ePAPR boot */
	if (r5)
		of_platform_init(r3, r4, (void *)r5);
	else
		epapr_platform_init(r3, r4, r5, r6, r7);
}

