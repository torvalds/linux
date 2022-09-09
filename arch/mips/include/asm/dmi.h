/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_DMI_H
#define _ASM_DMI_H

#include <linux/io.h>
#include <linux/memblock.h>

#define dmi_early_remap(x, l)		ioremap_cache(x, l)
#define dmi_early_unmap(x, l)		iounmap(x)
#define dmi_remap(x, l)			ioremap_cache(x, l)
#define dmi_unmap(x)			iounmap(x)

/* MIPS initialize DMI scan before SLAB is ready, so we use memblock here */
#define dmi_alloc(l)			memblock_alloc_low(l, PAGE_SIZE)

#if defined(CONFIG_MACH_LOONGSON64)
#define SMBIOS_ENTRY_POINT_SCAN_START	0xFFFE000
#endif

#endif /* _ASM_DMI_H */
