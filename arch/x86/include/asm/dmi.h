#ifndef _ASM_X86_DMI_H
#define _ASM_X86_DMI_H

#include <asm/io.h>
#include <asm/setup.h>

static inline void *dmi_alloc(unsigned len)
{
	return extend_brk(len, sizeof(int));
}

/* Use early IO mappings for DMI because it's initialized early */
#define dmi_ioremap early_ioremap
#define dmi_iounmap early_iounmap

#endif /* _ASM_X86_DMI_H */
