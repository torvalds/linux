#ifndef __ASM_MACH_PXA168_H
#define __ASM_MACH_PXA168_H

#include <mach/devices.h>

extern struct pxa_device_desc pxa168_device_uart1;
extern struct pxa_device_desc pxa168_device_uart2;

static inline int pxa168_add_uart(int id)
{
	struct pxa_device_desc *d = NULL;

	switch (id) {
	case 1: d = &pxa168_device_uart1; break;
	case 2: d = &pxa168_device_uart2; break;
	}

	if (d == NULL)
		return -EINVAL;

	return pxa_register_device(d, NULL, 0);
}
#endif /* __ASM_MACH_PXA168_H */
