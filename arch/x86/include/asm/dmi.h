/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DMI_H
#define _ASM_X86_DMI_H

#include <linux/compiler.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/setup.h>

static __always_inline __init void *dmi_alloc(unsigned len)
{
	return extend_brk(len, sizeof(int));
}

/* Use early IO mappings for DMI because it's initialized early */
#define dmi_early_remap		early_memremap
#define dmi_early_unmap		early_memunmap
#define dmi_remap(_x, _l)	memremap(_x, _l, MEMREMAP_WB)
#define dmi_unmap(_x)		memunmap(_x)

#endif /* _ASM_X86_DMI_H */
