/*
 * Setup pointers to hardware dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 2004, 05 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 *
 */
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pm.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/gt64120.h>

#include <cobalt.h>

extern void cobalt_machine_restart(char *command);
extern void cobalt_machine_halt(void);
extern void cobalt_machine_power_off(void);

const char *get_system_type(void)
{
	switch (cobalt_board_id) {
		case COBALT_BRD_ID_QUBE1:
			return "Cobalt Qube";
		case COBALT_BRD_ID_RAQ1:
			return "Cobalt RaQ";
		case COBALT_BRD_ID_QUBE2:
			return "Cobalt Qube2";
		case COBALT_BRD_ID_RAQ2:
			return "Cobalt RaQ2";
	}
	return "MIPS Cobalt";
}

void __init plat_timer_setup(struct irqaction *irq)
{
	/* Load timer value for HZ (TCLK is 50MHz) */
	GT_WRITE(GT_TC0_OFS, 50*1000*1000 / HZ);

	/* Enable timer */
	GT_WRITE(GT_TC_CONTROL_OFS, GT_TC_CONTROL_ENTC0_MSK | GT_TC_CONTROL_SELTC0_MSK);

	/* Register interrupt */
	setup_irq(COBALT_GALILEO_IRQ, irq);

	/* Enable interrupt */
	GT_WRITE(GT_INTRMASK_OFS, GT_INTR_T0EXP_MSK | GT_READ(GT_INTRMASK_OFS));
}

/*
 * Cobalt doesn't have PS/2 keyboard/mouse interfaces,
 * keyboard conntroller is never used.
 * Also PCI-ISA bridge DMA contoroller is never used.
 */
static struct resource cobalt_reserved_resources[] = {
	{	/* dma1 */
		.start	= 0x00,
		.end	= 0x1f,
		.name	= "reserved",
		.flags	= IORESOURCE_BUSY | IORESOURCE_IO,
	},
	{	/* keyboard */
		.start	= 0x60,
		.end	= 0x6f,
		.name	= "reserved",
		.flags	= IORESOURCE_BUSY | IORESOURCE_IO,
	},
	{	/* dma page reg */
		.start	= 0x80,
		.end	= 0x8f,
		.name	= "reserved",
		.flags	= IORESOURCE_BUSY | IORESOURCE_IO,
	},
	{	/* dma2 */
		.start	= 0xc0,
		.end	= 0xdf,
		.name	= "reserved",
		.flags	= IORESOURCE_BUSY | IORESOURCE_IO,
	},
};

void __init plat_mem_setup(void)
{
	int i;

	_machine_restart = cobalt_machine_restart;
	_machine_halt = cobalt_machine_halt;
	pm_power_off = cobalt_machine_power_off;

	set_io_port_base(CKSEG1ADDR(GT_DEF_PCI0_IO_BASE));

	/* I/O port resource must include LCD/buttons */
	ioport_resource.end = 0x0fffffff;

	/* These resources have been reserved by VIA SuperI/O chip. */
	for (i = 0; i < ARRAY_SIZE(cobalt_reserved_resources); i++)
		request_resource(&ioport_resource, cobalt_reserved_resources + i);
}

/*
 * Prom init. We read our one and only communication with the firmware.
 * Grab the amount of installed memory.
 * Better boot loaders (CoLo) pass a command line too :-)
 */

void __init prom_init(void)
{
	int narg, indx, posn, nchr;
	unsigned long memsz;
	char **argv;

	mips_machgroup = MACH_GROUP_COBALT;

	memsz = fw_arg0 & 0x7fff0000;
	narg = fw_arg0 & 0x0000ffff;

	if (narg) {
		arcs_cmdline[0] = '\0';
		argv = (char **) fw_arg1;
		posn = 0;
		for (indx = 1; indx < narg; ++indx) {
			nchr = strlen(argv[indx]);
			if (posn + 1 + nchr + 1 > sizeof(arcs_cmdline))
				break;
			if (posn)
				arcs_cmdline[posn++] = ' ';
			strcpy(arcs_cmdline + posn, argv[indx]);
			posn += nchr;
		}
	}

	add_memory_region(0x0, memsz, BOOT_MEM_RAM);
}

void __init prom_free_prom_memory(void)
{
	/* Nothing to do! */
}
