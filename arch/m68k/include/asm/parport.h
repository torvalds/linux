/*
 * parport.h: platform-specific PC-style parport initialisation
 *
 * Copyright (C) 1999, 2000  Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This file should only be included by drivers/parport/parport_pc.c.
 *
 * RZ: for use with Q40 and other ISA machines
 */

#ifndef _ASM_M68K_PARPORT_H
#define _ASM_M68K_PARPORT_H 1

#define insl(port,buf,len)   isa_insb(port,buf,(len)<<2)
#define outsl(port,buf,len)  isa_outsb(port,buf,(len)<<2)

/* no dma, or IRQ autoprobing */
static int __devinit parport_pc_find_isa_ports (int autoirq, int autodma);
static int __devinit parport_pc_find_nonpci_ports (int autoirq, int autodma)
{
        if (! (MACH_IS_Q40))
	  return 0; /* count=0 */
	return parport_pc_find_isa_ports (PARPORT_IRQ_NONE, PARPORT_DMA_NONE);
}

#endif /* !(_ASM_M68K_PARPORT_H) */
