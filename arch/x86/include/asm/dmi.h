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
#define dmi_early_remap		early_ioremap
#define dmi_early_unmap		early_iounmap
#define dmi_remap		ioremap_cache
#define dmi_unmap		iounmap

#endif /* _ASM_X86_DMI_H */
