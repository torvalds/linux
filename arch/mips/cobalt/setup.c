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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/gt64120.h>

#include <asm/mach-cobalt/cobalt.h>

extern void cobalt_machine_restart(char *command);
extern void cobalt_machine_halt(void);
extern void cobalt_machine_power_off(void);
extern void cobalt_early_console(void);

int cobalt_board_id;

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
	GALILEO_OUTL(50*1000*1000 / HZ, GT_TC0_OFS);

	/* Enable timer */
	GALILEO_OUTL(GALILEO_ENTC0 | GALILEO_SELTC0, GT_TC_CONTROL_OFS);

	/* Register interrupt */
	setup_irq(COBALT_GALILEO_IRQ, irq);

	/* Enable interrupt */
	GALILEO_OUTL(GALILEO_INTR_T0EXP | GALILEO_INL(GT_INTRMASK_OFS), GT_INTRMASK_OFS);
}

extern struct pci_ops gt64111_pci_ops;

static struct resource cobalt_mem_resource = {
	.start	= GT64111_MEM_BASE,
	.end	= GT64111_MEM_END,
	.name	= "PCI memory",
	.flags	= IORESOURCE_MEM
};

static struct resource cobalt_io_resource = {
	.start	= 0x1000,
	.end	= 0xffff,
	.name	= "PCI I/O",
	.flags	= IORESOURCE_IO
};

static struct resource cobalt_io_resources[] = {
	{
		.start	= 0x00,
		.end	= 0x1f,
		.name	= "dma1",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x40,
		.end	= 0x5f,
		.name	= "timer",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x60,
		.end	= 0x6f,
		.name	= "keyboard",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0x80,
		.end	= 0x8f,
		.name	= "dma page reg",
		.flags	= IORESOURCE_BUSY
	}, {
		.start	= 0xc0,
		.end	= 0xdf,
		.name	= "dma2",
		.flags	= IORESOURCE_BUSY
	},
};

#define COBALT_IO_RESOURCES (sizeof(cobalt_io_resources)/sizeof(struct resource))

static struct pci_controller cobalt_pci_controller = {
	.pci_ops	= &gt64111_pci_ops,
	.mem_resource	= &cobalt_mem_resource,
	.mem_offset	= 0,
	.io_resource	= &cobalt_io_resource,
	.io_offset	= 0 - GT64111_IO_BASE
};

void __init plat_mem_setup(void)
{
	static struct uart_port uart;
	unsigned int devfn = PCI_DEVFN(COBALT_PCICONF_VIA, 0);
	int i;

	_machine_restart = cobalt_machine_restart;
	_machine_halt = cobalt_machine_halt;
	pm_power_off = cobalt_machine_power_off;

        set_io_port_base(CKSEG1ADDR(GT64111_IO_BASE));

	/* I/O port resource must include UART and LCD/buttons */
	ioport_resource.end = 0x0fffffff;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < COBALT_IO_RESOURCES; i++)
		request_resource(&ioport_resource, cobalt_io_resources + i);

        /* Read the cobalt id register out of the PCI config space */
        PCI_CFG_SET(devfn, (VIA_COBALT_BRD_ID_REG & ~0x3));
        cobalt_board_id = GALILEO_INL(GT_PCI0_CFGDATA_OFS);
        cobalt_board_id >>= ((VIA_COBALT_BRD_ID_REG & 3) * 8);
        cobalt_board_id = VIA_COBALT_BRD_REG_to_ID(cobalt_board_id);

	printk("Cobalt board ID: %d\n", cobalt_board_id);

#ifdef CONFIG_PCI
	register_pci_controller(&cobalt_pci_controller);
#endif

#ifdef CONFIG_SERIAL_8250
	if (cobalt_board_id > COBALT_BRD_ID_RAQ1) {

#ifdef CONFIG_EARLY_PRINTK
		cobalt_early_console();
#endif

		uart.line	= 0;
		uart.type	= PORT_UNKNOWN;
		uart.uartclk	= 18432000;
		uart.irq	= COBALT_SERIAL_IRQ;
		uart.flags	= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
		uart.iobase	= 0xc800000;
		uart.iotype	= UPIO_PORT;

		early_serial_setup(&uart);
	}
#endif
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

unsigned long __init prom_free_prom_memory(void)
{
	/* Nothing to do! */
	return 0;
}
