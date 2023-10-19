/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_DMI_H
#define _ASM_DMI_H 1

#include <linux/slab.h>
#include <asm/io.h>

/* Use normal IO mappings for DMI */
#define dmi_early_remap		ioremap
#define dmi_early_unmap(x, l)	iounmap(x)
#define dmi_remap		ioremap
#define dmi_unmap		iounmap
#define dmi_alloc(l)		kzalloc(l, GFP_ATOMIC)

#endif
