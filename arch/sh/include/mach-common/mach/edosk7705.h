/*
 * include/asm-sh/edosk7705.h
 *
 * Modified version of io_se.h for the EDOSK7705 specific functions.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Hitachi EDOSK7705 development board
 */

#ifndef __ASM_SH_EDOSK7705_IO_H
#define __ASM_SH_EDOSK7705_IO_H

#include <asm/io_generic.h>

extern unsigned char sh_edosk7705_inb(unsigned long port);
extern unsigned int sh_edosk7705_inl(unsigned long port);

extern void sh_edosk7705_outb(unsigned char value, unsigned long port);
extern void sh_edosk7705_outl(unsigned int value, unsigned long port);

extern void sh_edosk7705_insb(unsigned long port, void *addr, unsigned long count);
extern void sh_edosk7705_insl(unsigned long port, void *addr, unsigned long count);
extern void sh_edosk7705_outsb(unsigned long port, const void *addr, unsigned long count);
extern void sh_edosk7705_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned long sh_edosk7705_isa_port2addr(unsigned long offset);

#endif /* __ASM_SH_EDOSK7705_IO_H */
