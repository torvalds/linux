/*
 * Setup pointers to hardware dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 2004 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 *
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/gt64120.h>

#include <asm/cobalt/cobalt.h>

extern void cobalt_machine_restart(char *command);
extern void cobalt_machine_halt(void);
extern void cobalt_machine_power_off(void);

int cobalt_board_id;

static char my_cmdline[CL_SIZE] = {
 "console=ttyS0,115200 "
#ifdef CONFIG_IP_PNP
 "ip=on "
#endif
#ifdef CONFIG_ROOT_NFS
 "root=/dev/nfs "
#else
 "root=/dev/hda1 "
#endif
 };

const char *get_system_type(void)
{
	return "MIPS Cobalt";
}

static void __init cobalt_timer_setup(struct irqaction *irq)
{
	/* Load timer value for 150 Hz */
	GALILEO_OUTL(500000, GT_TC0_OFS);

	/* Register our timer interrupt */
	setup_irq(COBALT_TIMER_IRQ, irq);

	/* Enable timer ints */
	GALILEO_OUTL((GALILEO_ENTC0 | GALILEO_SELTC0), GT_TC_CONTROL_OFS);
	/* Unmask timer int */
	GALILEO_OUTL(0x100, GT_INTRMASK_OFS);
}

extern struct pci_ops gt64111_pci_ops;

static struct resource cobalt_mem_resource = {
	"GT64111 PCI MEM", GT64111_IO_BASE, 0xffffffffUL, IORESOURCE_MEM
};

static struct resource cobalt_io_resource = {
	"GT64111 IO MEM", 0x00001000UL, 0x0fffffffUL, IORESOURCE_IO
};

static struct resource cobalt_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "keyboard", 0x60, 0x6f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
};

#define COBALT_IO_RESOURCES (sizeof(cobalt_io_resources)/sizeof(struct resource))

static struct pci_controller cobalt_pci_controller = {
	.pci_ops	= &gt64111_pci_ops,
	.mem_resource	= &cobalt_mem_resource,
	.mem_offset	= 0,
	.io_resource	= &cobalt_io_resource,
	.io_offset	= 0x00001000UL - GT64111_IO_BASE
};

static void __init cobalt_setup(void)
{
	unsigned int devfn = PCI_DEVFN(COBALT_PCICONF_VIA, 0);
	int i;

	_machine_restart = cobalt_machine_restart;
	_machine_halt = cobalt_machine_halt;
	_machine_power_off = cobalt_machine_power_off;

	board_timer_setup = cobalt_timer_setup;

        set_io_port_base(KSEG1ADDR(GT64111_IO_BASE));

	/*
	 * This is a prom style console. We just poke at the
	 *  UART to make it talk.
	 * Only use this console if you really screw up and can't
	 *  get to the stage of setting up a real serial console.
	 */
	/*ns16550_setup_console();*/

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < COBALT_IO_RESOURCES; i++)
		request_resource(&ioport_resource, cobalt_io_resources + i);

        /* Read the cobalt id register out of the PCI config space */
        PCI_CFG_SET(devfn, (VIA_COBALT_BRD_ID_REG & ~0x3));
        cobalt_board_id = GALILEO_INL(GT_PCI0_CFGDATA_OFS);
        cobalt_board_id >>= ((VIA_COBALT_BRD_ID_REG & 3) * 8);
        cobalt_board_id = VIA_COBALT_BRD_REG_to_ID(cobalt_board_id);

#ifdef CONFIG_PCI
	register_pci_controller(&cobalt_pci_controller);
#endif
}

early_initcall(cobalt_setup);

/*
 * Prom init. We read our one and only communication with the firmware.
 * Grab the amount of installed memory
 */

void __init prom_init(void)
{
	int argc = fw_arg0;

	strcpy(arcs_cmdline, my_cmdline);

	mips_machgroup = MACH_GROUP_COBALT;

	add_memory_region(0x0, argc & 0x7fffffff, BOOT_MEM_RAM);
}

unsigned long __init prom_free_prom_memory(void)
{
	/* Nothing to do! */
	return 0;
}
