/*
 *
 * 2.6 port, Embedded Alley Solutions, Inc
 *
 *  Based on Per Hallsmark, per.hallsmark@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/serial_ip3106.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/time.h>

#include <glb.h>
#include <int.h>
#include <pci.h>
#include <uart.h>
#include <nand.h>

extern void prom_printf(char *fmt, ...);

extern void __init board_setup(void);
extern void pnx8550_machine_restart(char *);
extern void pnx8550_machine_halt(void);
extern void pnx8550_machine_power_off(void);
extern struct resource ioport_resource;
extern struct resource iomem_resource;
extern void (*board_time_init)(void);
extern void pnx8550_time_init(void);
extern void (*board_timer_setup)(struct irqaction *irq);
extern void pnx8550_timer_setup(struct irqaction *irq);
extern void rs_kgdb_hook(int tty_no);
extern void prom_printf(char *fmt, ...);
extern char *prom_getcmdline(void);

struct resource standard_io_resources[] = {
	{"dma1", 0x00, 0x1f, IORESOURCE_BUSY},
	{"timer", 0x40, 0x5f, IORESOURCE_BUSY},
	{"dma page reg", 0x80, 0x8f, IORESOURCE_BUSY},
	{"dma2", 0xc0, 0xdf, IORESOURCE_BUSY},
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

extern struct resource pci_io_resource;
extern struct resource pci_mem_resource;

/* Return the total size of DRAM-memory, (RANK0 + RANK1) */
unsigned long get_system_mem_size(void)
{
	/* Read IP2031_RANK0_ADDR_LO */
	unsigned long dram_r0_lo = inl(PCI_BASE | 0x65010);
	/* Read IP2031_RANK1_ADDR_HI */
	unsigned long dram_r1_hi = inl(PCI_BASE | 0x65018);

	return dram_r1_hi - dram_r0_lo + 1;
}

int pnx8550_console_port = -1;

void __init plat_setup(void)
{
	int i;
	char* argptr;

	board_setup();  /* board specific setup */

        _machine_restart = pnx8550_machine_restart;
        _machine_halt = pnx8550_machine_halt;
        _machine_power_off = pnx8550_machine_power_off;

	board_time_init = pnx8550_time_init;
	board_timer_setup = pnx8550_timer_setup;

	/* Clear the Global 2 Register, PCI Inta Output Enable Registers
	   Bit 1:Enable DAC Powerdown
	  -> 0:DACs are enabled and are working normally
	     1:DACs are powerdown
	   Bit 0:Enable of PCI inta output
	  -> 0 = Disable PCI inta output
	     1 = Enable PCI inta output
	*/
	PNX8550_GLB2_ENAB_INTA_O = 0;

	/* IO/MEM resources. */
	set_io_port_base(KSEG1);
	ioport_resource.start = 0;
	ioport_resource.end = ~0;
	iomem_resource.start = 0;
	iomem_resource.end = ~0;

	/* Request I/O space for devices on this board */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources + i);

	/* Place the Mode Control bit for GPIO pin 16 in primary function */
	/* Pin 16 is used by UART1, UA1_TX                                */
	outl((PNX8550_GPIO_MODE_PRIMOP << PNX8550_GPIO_MC_16_BIT) |
			(PNX8550_GPIO_MODE_PRIMOP << PNX8550_GPIO_MC_17_BIT),
			PNX8550_GPIO_MC1);

	argptr = prom_getcmdline();
	if ((argptr = strstr(argptr, "console=ttyS")) != NULL) {
		argptr += strlen("console=ttyS");
		pnx8550_console_port = *argptr == '0' ? 0 : 1;

		/* We must initialize the UART (console) before prom_printf */
		/* Set LCR to 8-bit and BAUD to 38400 (no 5)                */
		ip3106_lcr(UART_BASE, pnx8550_console_port) =
			IP3106_UART_LCR_8BIT;
		ip3106_baud(UART_BASE, pnx8550_console_port) = 5;
	}

#ifdef CONFIG_KGDB
	argptr = prom_getcmdline();
	if ((argptr = strstr(argptr, "kgdb=ttyS")) != NULL) {
		int line;
		argptr += strlen("kgdb=ttyS");
		line = *argptr == '0' ? 0 : 1;
		rs_kgdb_hook(line);
		prom_printf("KGDB: Using ttyS%i for session, "
				"please connect your debugger\n", line ? 1 : 0);
	}
#endif
	return;
}
