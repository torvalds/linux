/*
 * BRIEF MODULE DESCRIPTION
 *	IT8172/QED5231 board setup.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>
#include <linux/pm.h>

#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/traps.h>
#include <asm/it8172/it8172.h>
#include <asm/it8712.h>

extern struct resource ioport_resource;
#ifdef CONFIG_SERIO_I8042
int init_8712_keyboard(void);
#endif

extern int SearchIT8712(void);
extern void InitLPCInterface(void);
extern char * __init prom_getcmdline(void);
extern void it8172_restart(char *command);
extern void it8172_halt(void);
extern void it8172_power_off(void);

extern void (*board_time_init)(void);
extern void (*board_timer_setup)(struct irqaction *irq);
extern void it8172_time_init(void);
extern void it8172_timer_setup(struct irqaction *irq);

#ifdef CONFIG_IT8172_REVC
struct {
    struct resource ram;
    struct resource pci_mem;
    struct resource pci_io;
    struct resource flash;
    struct resource boot;
} it8172_resources = {
	{
		.start	= 0,				/* to be initted */
		.end	= 0,
		.name	= "RAM",
		.flags	= IORESOURCE_MEM
	}, {
		.start	= 0x10000000,
		.end	= 0x13FFFFFF,
		.name	= "PCI Mem",
		.flags	= IORESOURCE_MEM
	}, {
		.start	= 0x14000000,
		.end	= 0x17FFFFFF
		.name	= "PCI I/O",
	}, {
		.start	= 0x08000000,
		.end	= 0x0CFFFFFF
		.name	= "Flash",
	}, {
		.start	= 0x1FC00000,
		.end	= 0x1FFFFFFF
		.name	= "Boot ROM",
	}
};
#else
struct {
    struct resource ram;
    struct resource pci_mem0;
    struct resource pci_mem1;
    struct resource pci_io;
    struct resource pci_mem2;
    struct resource pci_mem3;
    struct resource flash;
    struct resource boot;
} it8172_resources = {
	{
		.start	= 0,				/* to be initted */
		.end	= 0,
		.name	= "RAM",
		.flags	= IORESOURCE_MEM
	}, {
		.start	= 0x0C000000,
		.end	= 0x0FFFFFFF,
		.name	= "PCI Mem0",
		.flags	= IORESOURCE_MEM
	 }, {
		.start	= 0x10000000,
		.end	= 0x13FFFFFF,
		.name	= "PCI Mem1",
		.flags	= IORESOURCE_MEM
	 }, {
		.start	= 0x14000000,
		.end	= 0x17FFFFFF
		.name	= "PCI I/O",
	}, {
		.start	= 0x1A000000,
		.end	= 0x1BFFFFFF,
		.name	= "PCI Mem2",
		.flags	= IORESOURCE_MEM
	}, {
		.start	= 0x1C000000,
		.end	= 0x1FBFFFFF,
		.name	= "PCI Mem3",
		.flags	= IORESOURCE_MEM
	}, {
		.start	= 0x08000000,
		.end	= 0x0CFFFFFF
		.name	= "Flash",
	}, {
		.start	= 0x1FC00000,
		.end	= 0x1FFFFFFF
		.name	= "Boot ROM",
	}
};
#endif


void __init it8172_init_ram_resource(unsigned long memsize)
{
	it8172_resources.ram.end = memsize;
}

void __init plat_setup(void)
{
	unsigned short dsr;
	char *argptr;

	argptr = prom_getcmdline();
#ifdef CONFIG_SERIAL_CONSOLE
	if ((argptr = strstr(argptr, "console=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

	clear_c0_status(ST0_FR);

	board_time_init = it8172_time_init;
	board_timer_setup = it8172_timer_setup;

	_machine_restart = it8172_restart;
	_machine_halt = it8172_halt;
	pm_power_off = it8172_power_off;

	/*
	 * IO/MEM resources.
	 *
	 * revisit this area.
	 */
	set_io_port_base(KSEG1);
	ioport_resource.start = it8172_resources.pci_io.start;
	ioport_resource.end = it8172_resources.pci_io.end;
#ifdef CONFIG_IT8172_REVC
	iomem_resource.start = it8172_resources.pci_mem.start;
	iomem_resource.end = it8172_resources.pci_mem.end;
#else
	iomem_resource.start = it8172_resources.pci_mem0.start;
	iomem_resource.end = it8172_resources.pci_mem3.end;
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = Root_RAM0;
#endif

	/*
	 * Pull enabled devices out of standby
	 */
	IT_IO_READ16(IT_PM_DSR, dsr);

	/*
	 * Fixme: This breaks when these drivers are modules!!!
	 */
#ifdef CONFIG_SOUND_IT8172
	dsr &= ~IT_PM_DSR_ACSB;
#else
	dsr |= IT_PM_DSR_ACSB;
#endif
#ifdef CONFIG_BLK_DEV_IT8172
	dsr &= ~IT_PM_DSR_IDESB;
#else
	dsr |= IT_PM_DSR_IDESB;
#endif
	IT_IO_WRITE16(IT_PM_DSR, dsr);

	InitLPCInterface();

#ifdef CONFIG_MIPS_ITE8172
	if (SearchIT8712()) {
		printk("Found IT8712 Super IO\n");
		/* enable IT8712 serial port */
		LPCSetConfig(LDN_SERIAL1, 0x30, 0x01); /* enable */
		LPCSetConfig(LDN_SERIAL1, 0x23, 0x01); /* clock selection */
#ifdef CONFIG_SERIO_I8042
		if (init_8712_keyboard()) {
			printk("Unable to initialize keyboard\n");
			LPCSetConfig(LDN_KEYBOARD, 0x30, 0x0); /* disable keyboard */
		} else {
			LPCSetConfig(LDN_KEYBOARD, 0x30, 0x1); /* enable keyboard */
			LPCSetConfig(LDN_KEYBOARD, 0xf0, 0x2);
			LPCSetConfig(LDN_KEYBOARD, 0x71, 0x3);

			LPCSetConfig(LDN_MOUSE, 0x30, 0x1); /* enable mouse */

			LPCSetConfig(0x4, 0x30, 0x1);
			LPCSetConfig(0x4, 0xf4, LPCGetConfig(0x4, 0xf4) | 0x80);

			if ((LPCGetConfig(LDN_KEYBOARD, 0x30) == 0) ||
					(LPCGetConfig(LDN_MOUSE, 0x30) == 0))
				printk("Error: keyboard or mouse not enabled\n");

		}
#endif
	}
	else {
		printk("IT8712 Super IO not found\n");
	}
#endif

#ifdef CONFIG_IT8172_CIR
	{
		unsigned long data;
		//printk("Enabling CIR0\n");
		IT_IO_READ16(IT_PM_DSR, data);
		data &= ~IT_PM_DSR_CIR0SB;
		IT_IO_WRITE16(IT_PM_DSR, data);
		//printk("DSR register: %x\n", (unsigned)IT_IO_READ16(IT_PM_DSR, data));
	}
#endif
#ifdef CONFIG_IT8172_SCR0
	{
		unsigned i;
		/* Enable Smart Card Reader 0 */
		/* First power it up */
		IT_IO_READ16(IT_PM_DSR, i);
		i &= ~IT_PM_DSR_SCR0SB;
		IT_IO_WRITE16(IT_PM_DSR, i);
		/* Then initialize its registers */
		outb(( IT_SCR_SFR_GATE_UART_OFF     << IT_SCR_SFR_GATE_UART_BIT
		      |IT_SCR_SFR_FET_CHARGE_213_US << IT_SCR_SFR_FET_CHARGE_BIT
		      |IT_SCR_SFR_CARD_FREQ_3_5_MHZ << IT_SCR_SFR_CARD_FREQ_BIT
		      |IT_SCR_SFR_FET_ACTIVE_INVERT << IT_SCR_SFR_FET_ACTIVE_BIT
		      |IT_SCR_SFR_ENABLE_ON         << IT_SCR_SFR_ENABLE_BIT),
		     IT8172_PCI_IO_BASE + IT_SCR0_BASE + IT_SCR_SFR);
		outb(IT_SCR_SCDR_RESET_MODE_ASYNC << IT_SCR_SCDR_RESET_MODE_BIT,
		     IT8172_PCI_IO_BASE + IT_SCR0_BASE + IT_SCR_SCDR);
	}
#endif /* CONFIG_IT8172_SCR0 */
#ifdef CONFIG_IT8172_SCR1
	{
		unsigned i;
		/* Enable Smart Card Reader 1 */
		/* First power it up */
		IT_IO_READ16(IT_PM_DSR, i);
		i &= ~IT_PM_DSR_SCR1SB;
		IT_IO_WRITE16(IT_PM_DSR, i);
		/* Then initialize its registers */
		outb(( IT_SCR_SFR_GATE_UART_OFF     << IT_SCR_SFR_GATE_UART_BIT
		      |IT_SCR_SFR_FET_CHARGE_213_US << IT_SCR_SFR_FET_CHARGE_BIT
		      |IT_SCR_SFR_CARD_FREQ_3_5_MHZ << IT_SCR_SFR_CARD_FREQ_BIT
		      |IT_SCR_SFR_FET_ACTIVE_INVERT << IT_SCR_SFR_FET_ACTIVE_BIT
		      |IT_SCR_SFR_ENABLE_ON         << IT_SCR_SFR_ENABLE_BIT),
		     IT8172_PCI_IO_BASE + IT_SCR1_BASE + IT_SCR_SFR);
		outb(IT_SCR_SCDR_RESET_MODE_ASYNC << IT_SCR_SCDR_RESET_MODE_BIT,
		     IT8172_PCI_IO_BASE + IT_SCR1_BASE + IT_SCR_SCDR);
	}
#endif /* CONFIG_IT8172_SCR1 */
}

#ifdef CONFIG_SERIO_I8042
/*
 * According to the ITE Special BIOS Note for waking up the
 * keyboard controller...
 */
static int init_8712_keyboard(void)
{
	unsigned int cmd_port = 0x14000064;
	unsigned int data_port = 0x14000060;
	                         ^^^^^^^^^^^
	Somebody here doesn't grok the concept of io ports.

	unsigned char data;
	int i;

	outb(0xaa, cmd_port); /* send self-test cmd */
	i = 0;
	while (!(inb(cmd_port) & 0x1)) { /* wait output buffer full */
		i++;
		if (i > 0xffffff)
			return 1;
	}

	data = inb(data_port);
	outb(0xcb, cmd_port); /* set ps2 mode */
	while (inb(cmd_port) & 0x2) { /* wait while input buffer full */
		i++;
		if (i > 0xffffff)
			return 1;
	}
	outb(0x01, data_port);
	while (inb(cmd_port) & 0x2) { /* wait while input buffer full */
		i++;
		if (i > 0xffffff)
			return 1;
	}

	outb(0x60, cmd_port); /* write 8042 command byte */
	while (inb(cmd_port) & 0x2) { /* wait while input buffer full */
		i++;
		if (i > 0xffffff)
			return 1;
	}
	outb(0x45, data_port); /* at interface, keyboard enabled, system flag */
	while (inb(cmd_port) & 0x2) { /* wait while input buffer full */
		i++;
		if (i > 0xffffff)
			return 1;
	}

	outb(0xae, cmd_port); /* enable interface */
	return 0;
}
#endif
