/*
 * parport.h: platform-specific PC-style parport initialisation
 *
 * Copyright (C) 1999, 2000  Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This file should only be included by drivers/parport/parport_pc.c.
 */

#ifndef _ASM_POWERPC_PARPORT_H
#define _ASM_POWERPC_PARPORT_H
#ifdef __KERNEL__

#include <asm/prom.h>

static int __devinit parport_pc_find_nonpci_ports (int autoirq, int autodma)
{
	struct device_node *np;
	const u32 *prop;
	u32 io1, io2;
	int propsize;
	int count = 0;
	for (np = NULL; (np = of_find_compatible_node(np,
						      "parallel",
						      "pnpPNP,400")) != NULL;) {
		prop = of_get_property(np, "reg", &propsize);
		if (!prop || propsize > 6*sizeof(u32))
			continue;
		io1 = prop[1]; io2 = prop[2];
		prop = of_get_property(np, "interrupts", NULL);
		if (!prop)
			continue;
		if (parport_pc_probe_port(io1, io2, prop[0], autodma, NULL, 0) != NULL)
			count++;
	}
	return count;
}

#endif /* __KERNEL__ */
#endif /* !(_ASM_POWERPC_PARPORT_H) */
