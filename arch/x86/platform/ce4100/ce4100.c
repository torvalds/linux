// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel CE4100  platform specific setup code
 *
 * (C) Copyright 2010 Intel Corporation
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/reboot.h>
#include <linux/serial_reg.h>
#include <linux/serial_8250.h>

#include <asm/ce4100.h>
#include <asm/prom.h>
#include <asm/setup.h>
#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/io_apic.h>
#include <asm/emergency-restart.h>

/*
 * The CE4100 platform has an internal 8051 Microcontroller which is
 * responsible for signaling to the external Power Management Unit the
 * intention to reset, reboot or power off the system. This 8051 device has
 * its command register mapped at I/O port 0xcf9 and the value 0x4 is used
 * to power off the system.
 */
static void ce4100_power_off(void)
{
	outb(0x4, 0xcf9);
}

#ifdef CONFIG_SERIAL_8250

static unsigned int mem_serial_in(struct uart_port *p, int offset)
{
	offset = offset << p->regshift;
	return readl(p->membase + offset);
}

/*
 * The UART Tx interrupts are not set under some conditions and therefore serial
 * transmission hangs. This is a silicon issue and has not been root caused. The
 * workaround for this silicon issue checks UART_LSR_THRE bit and UART_LSR_TEMT
 * bit of LSR register in interrupt handler to see whether at least one of these
 * two bits is set, if so then process the transmit request. If this workaround
 * is not applied, then the serial transmission may hang. This workaround is for
 * errata number 9 in Errata - B step.
*/

static unsigned int ce4100_mem_serial_in(struct uart_port *p, int offset)
{
	unsigned int ret, ier, lsr;

	if (offset == UART_IIR) {
		offset = offset << p->regshift;
		ret = readl(p->membase + offset);
		if (ret & UART_IIR_NO_INT) {
			/* see if the TX interrupt should have really set */
			ier = mem_serial_in(p, UART_IER);
			/* see if the UART's XMIT interrupt is enabled */
			if (ier & UART_IER_THRI) {
				lsr = mem_serial_in(p, UART_LSR);
				/* now check to see if the UART should be
				   generating an interrupt (but isn't) */
				if (lsr & (UART_LSR_THRE | UART_LSR_TEMT))
					ret &= ~UART_IIR_NO_INT;
			}
		}
	} else
		ret =  mem_serial_in(p, offset);
	return ret;
}

static void ce4100_mem_serial_out(struct uart_port *p, int offset, int value)
{
	offset = offset << p->regshift;
	writel(value, p->membase + offset);
}

static void ce4100_serial_fixup(int port, struct uart_port *up,
	u32 *capabilities)
{
#ifdef CONFIG_EARLY_PRINTK
	/*
	 * Over ride the legacy port configuration that comes from
	 * asm/serial.h. Using the ioport driver then switching to the
	 * PCI memmaped driver hangs the IOAPIC
	 */
	if (up->iotype !=  UPIO_MEM32) {
		up->uartclk  = 14745600;
		up->mapbase = 0xdffe0200;
		set_fixmap_nocache(FIX_EARLYCON_MEM_BASE,
				up->mapbase & PAGE_MASK);
		up->membase =
			(void __iomem *)__fix_to_virt(FIX_EARLYCON_MEM_BASE);
		up->membase += up->mapbase & ~PAGE_MASK;
		up->mapbase += port * 0x100;
		up->membase += port * 0x100;
		up->iotype   = UPIO_MEM32;
		up->regshift = 2;
		up->irq = 4;
	}
#endif
	up->iobase = 0;
	up->serial_in = ce4100_mem_serial_in;
	up->serial_out = ce4100_mem_serial_out;

	*capabilities |= (1 << 12);
}

static __init void sdv_serial_fixup(void)
{
	serial8250_set_isa_configurator(ce4100_serial_fixup);
}

#else
static inline void sdv_serial_fixup(void) {};
#endif

static void __init sdv_arch_setup(void)
{
	sdv_serial_fixup();
}

static void sdv_pci_init(void)
{
	x86_of_pci_init();
}

/*
 * CE4100 specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_ce4100_early_setup(void)
{
	x86_init.oem.arch_setup			= sdv_arch_setup;
	x86_init.resources.probe_roms		= x86_init_noop;
	x86_init.mpparse.find_mptable		= x86_init_noop;
	x86_init.mpparse.early_parse_smp_cfg	= x86_init_noop;
	x86_init.pci.init			= ce4100_pci_init;
	x86_init.pci.init_irq			= sdv_pci_init;

	/*
	 * By default, the reboot method is ACPI which is supported by the
	 * CE4100 bootloader CEFDK using FADT.ResetReg Address and ResetValue
	 * the bootloader will however issue a system power off instead of
	 * reboot. By using BOOT_KBD we ensure proper system reboot as
	 * expected.
	 */
	reboot_type = BOOT_KBD;

	pm_power_off = ce4100_power_off;
}
