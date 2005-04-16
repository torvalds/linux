/*
 * linux/arch/mips/vr4181/setup.c
 *
 * VR41xx setup routines
 *
 * Copyright (C) 1999 Bradley D. LaRonde
 * Copyright (C) 1999, 2000 Michael Klar
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/ide.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/reboot.h>
#include <asm/vr4181/vr4181.h>
#include <asm/io.h>


extern void nec_osprey_restart(char* c);
extern void nec_osprey_halt(void);
extern void nec_osprey_power_off(void);

extern void vr4181_init_serial(void);
extern void vr4181_init_time(void);

static void __init nec_osprey_setup(void)
{
	set_io_port_base(VR4181_PORT_BASE);
	isa_slot_offset = VR4181_ISAMEM_BASE;

	vr4181_init_serial();
	vr4181_init_time();

	_machine_restart = nec_osprey_restart;
	_machine_halt = nec_osprey_halt;
	_machine_power_off = nec_osprey_power_off;

	/* setup resource limit */
	ioport_resource.end = 0xffffffff;
	iomem_resource.end = 0xffffffff;

	/* [jsun] hack */
	/*
	printk("[jsun] hack to change external ISA control register, %x -> %x\n",
		(*VR4181_XISACTL),
		(*VR4181_XISACTL) | 0x2);
	*VR4181_XISACTL |= 0x2;
	*/

	// *VR4181_GPHIBSTH = 0x2000;
	// *VR4181_GPMD0REG = 0x00c0;
	// *VR4181_GPINTEN	 = 1<<6;

	/* [jsun] I believe this will get the interrupt type right
	 * for the ether port.
	 */
	*VR4181_GPINTTYPL = 0x3000;
}

early_initcall(nec_osprey_setup);
