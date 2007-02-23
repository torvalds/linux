/*
 *  Copyright (C) 2004, 2005 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *  Based on the PMC-Sierra Yosemite board support by Ralf Baechle and
 *  Manish Lachwani.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/pgtable-32.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/rm9k-ocd.h>

#include <excite.h>

#define TITAN_UART_CLK	25000000

#if 1
/* normal serial port assignment */
#define REGBASE_SER0	0x0208
#define REGBASE_SER1	0x0238
#define MASK_SER0	0x1
#define MASK_SER1	0x2
#else
/* serial ports swapped */
#define REGBASE_SER0	0x0238
#define REGBASE_SER1	0x0208
#define MASK_SER0	0x2
#define MASK_SER1	0x1
#endif

unsigned long memsize;
char modetty[30];
unsigned int titan_irq = TITAN_IRQ;
static void __iomem * ctl_regs;
u32 unit_id;

volatile void __iomem * const ocd_base = (void *) (EXCITE_ADDR_OCD);
volatile void __iomem * const titan_base = (void *) (EXCITE_ADDR_TITAN);

/* Protect access to shared GPI registers */
spinlock_t titan_lock = SPIN_LOCK_UNLOCKED;
int titan_irqflags;


static void excite_timer_init(void)
{
	const u32 modebit5 = ocd_readl(0x00e4);
	unsigned int
		mult = ((modebit5 >> 11) & 0x1f) + 2,
		div = ((modebit5 >> 16) & 0x1f) + 2;

	if (div == 33) div = 1;
	mips_hpt_frequency = EXCITE_CPU_EXT_CLOCK * mult / div / 2;
}

void __init plat_timer_setup(struct irqaction *irq)
{
	/* The eXcite platform uses the alternate timer interrupt */
	set_c0_intcontrol(0x80);
	setup_irq(TIMER_IRQ, irq);
}

static int __init excite_init_console(void)
{
#if defined(CONFIG_SERIAL_8250)
	static __initdata char serr[] =
		KERN_ERR "Serial port #%u setup failed\n";
	struct uart_port up;

	/* Take the DUART out of reset */
	titan_writel(0x00ff1cff, CPRR);

#if defined(CONFIG_KGDB) || (CONFIG_SERIAL_8250_NR_UARTS > 1)
	/* Enable both ports */
	titan_writel(MASK_SER0 | MASK_SER1, UACFG);
#else
	/* Enable port #0 only */
	titan_writel(MASK_SER0, UACFG);
#endif	/* defined(CONFIG_KGDB) */

 	/*
	 * Set up serial port #0. Do not use autodetection; the result is
	 * not what we want.
 	 */
	memset(&up, 0, sizeof(up));
	up.membase	= (char *) titan_addr(REGBASE_SER0);
	up.irq		= TITAN_IRQ;
	up.uartclk	= TITAN_UART_CLK;
	up.regshift	= 0;
	up.iotype	= UPIO_RM9000;
	up.type		= PORT_RM9000;
	up.flags	= UPF_SHARE_IRQ;
	up.line		= 0;
	if (early_serial_setup(&up))
		printk(serr, up.line);

#if CONFIG_SERIAL_8250_NR_UARTS > 1
	/* And now for port #1. */
	up.membase	= (char *) titan_addr(REGBASE_SER1);
	up.line		= 1;
 	if (early_serial_setup(&up))
		printk(serr, up.line);
#endif /* CONFIG_SERIAL_8250_NR_UARTS > 1 */
#else
	/* Leave the DUART in reset */
	titan_writel(0x00ff3cff, CPRR);
#endif  /* defined(CONFIG_SERIAL_8250) */

	return 0;
}

static int __init excite_platform_init(void)
{
	unsigned int i;
	unsigned char buf[3];
	u8 reg;
	void __iomem * dpr;

	/* BIU buffer allocations */
	ocd_writel(8, CPURSLMT);	/* CPU */
	titan_writel(4, CPGRWL);	/* GPI / Ethernet */

	/* Map control registers located in FPGA */
	ctl_regs = ioremap_nocache(EXCITE_PHYS_FPGA + EXCITE_FPGA_SYSCTL, 16);
	if (!ctl_regs)
		panic("eXcite: failed to map platform control registers\n");
	memcpy_fromio(buf, ctl_regs + 2, ARRAY_SIZE(buf));
	unit_id = buf[0] | (buf[1] << 8) | (buf[2] << 16);

	/* Clear the reboot flag */
	dpr = ioremap_nocache(EXCITE_PHYS_FPGA + EXCITE_FPGA_DPR, 1);
	reg = __raw_readb(dpr);
	__raw_writeb(reg & 0x7f, dpr);
	iounmap(dpr);

	/* Interrupt controller setup */
	for (i = INTP0Status0; i < INTP0Status0 + 0x80; i += 0x10) {
		ocd_writel(0x00000000, i + 0x04);
		ocd_writel(0xffffffff, i + 0x0c);
	}
	ocd_writel(0x2, NMICONFIG);

	ocd_writel(0x1 << (TITAN_MSGINT % 0x20),
		   INTP0Mask0 + (0x10 * (TITAN_MSGINT / 0x20)));
	ocd_writel((0x1 << (FPGA0_MSGINT % 0x20))
		   | ocd_readl(INTP0Mask0 + (0x10 * (FPGA0_MSGINT / 0x20))),
		   INTP0Mask0 + (0x10 * (FPGA0_MSGINT / 0x20)));
	ocd_writel((0x1 << (FPGA1_MSGINT % 0x20))
		   | ocd_readl(INTP0Mask0 + (0x10 * (FPGA1_MSGINT / 0x20))),
		   INTP0Mask0 + (0x10 * (FPGA1_MSGINT / 0x20)));
	ocd_writel((0x1 << (PHY_MSGINT % 0x20))
		   | ocd_readl(INTP0Mask0 + (0x10 * (PHY_MSGINT / 0x20))),
		   INTP0Mask0 + (0x10 * (PHY_MSGINT / 0x20)));
#if USB_IRQ < 10
	ocd_writel((0x1 << (USB_MSGINT % 0x20))
		   | ocd_readl(INTP0Mask0 + (0x10 * (USB_MSGINT / 0x20))),
		   INTP0Mask0 + (0x10 * (USB_MSGINT / 0x20)));
#endif
	/* Enable the packet FIFO, XDMA and XDMA arbiter */
	titan_writel(0x00ff18ff, CPRR);

	/*
	 * Set up the PADMUX. Power down all ethernet slices,
	 * they will be powered up and configured at device startup.
	 */
	titan_writel(0x00878206, CPTC1R);
	titan_writel(0x00001100, CPTC0R); /* latch PADMUX, enable WCIMODE */

	/* Reset and enable the FIFO block */
	titan_writel(0x00000001, SDRXFCIE);
	titan_writel(0x00000001, SDTXFCIE);
	titan_writel(0x00000100, SDRXFCIE);
	titan_writel(0x00000000, SDTXFCIE);

	/*
	 * Initialize the common interrupt shared by all components of
	 * the GPI/Ethernet subsystem.
	 */
	titan_writel((EXCITE_PHYS_OCD >> 12), CPCFG0);
	titan_writel(TITAN_MSGINT, CPCFG1);

	/*
	 * XDMA configuration.
	 * In order for the XDMA to be sharable among multiple drivers,
	 * the setup must be done here in the platform. The reason is that
	 * this setup can only be done while the XDMA is in reset. If this
	 * were done in a driver, it would interrupt all other drivers
	 * using the XDMA.
	 */
	titan_writel(0x80021dff, GXCFG);	/* XDMA reset */
	titan_writel(0x00000000, CPXCISRA);
	titan_writel(0x00000000, CPXCISRB);	/* clear pending interrupts */
#if defined (CONFIG_HIGHMEM)
#	error change for HIGHMEM support!
#else
	titan_writel(0x00000000, GXDMADRPFX);	/* buffer address prefix */
#endif
	titan_writel(0, GXDMA_DESCADR);

	for (i = 0x5040; i <= 0x5300; i += 0x0040)
		titan_writel(0x80080000, i);	/* reset channel */

	titan_writel((0x1 << 29)			/* no sparse tx descr. */
		     | (0x1 << 28)			/* no sparse rx descr. */
		     | (0x1 << 23) | (0x1 << 24)	/* descriptor coherency */
		     | (0x1 << 21) | (0x1 << 22)	/* data coherency */
		     | (0x1 << 17)
		     | 0x1dff,
		     GXCFG);

#if defined(CONFIG_SMP)
#	error No SMP support
#else
	/* All interrupts go to core #0 only. */
	titan_writel(0x1f007fff, CPDST0A);
	titan_writel(0x00000000, CPDST0B);
	titan_writel(0x0000ff3f, CPDST1A);
	titan_writel(0x00000000, CPDST1B);
	titan_writel(0x00ffffff, CPXDSTA);
	titan_writel(0x00000000, CPXDSTB);
#endif

	/* Enable DUART interrupts, disable everything else. */
	titan_writel(0x04000000, CPGIG0ER);
	titan_writel(0x000000c0, CPGIG1ER);

	excite_procfs_init();
	return 0;
}

void __init plat_mem_setup(void)
{
	volatile u32 * const boot_ocd_base = (u32 *) 0xbf7fc000;

	/* Announce RAM to system */
	add_memory_region(0x00000000, memsize, BOOT_MEM_RAM);

	/* Set up timer initialization hooks */
	board_time_init = excite_timer_init;

	/* Set up the peripheral address map */
	*(boot_ocd_base + (LKB9 / sizeof (u32))) = 0;
	*(boot_ocd_base + (LKB10 / sizeof (u32))) = 0;
	*(boot_ocd_base + (LKB11 / sizeof (u32))) = 0;
	*(boot_ocd_base + (LKB12 / sizeof (u32))) = 0;
	wmb();
	*(boot_ocd_base + (LKB0 / sizeof (u32))) = EXCITE_PHYS_OCD >> 4;
	wmb();

	ocd_writel((EXCITE_PHYS_TITAN >> 4) | 0x1UL, LKB5);
	ocd_writel(((EXCITE_SIZE_TITAN >> 4) & 0x7fffff00) - 0x100, LKM5);
	ocd_writel((EXCITE_PHYS_SCRAM >> 4) | 0x1UL, LKB13);
	ocd_writel(((EXCITE_SIZE_SCRAM >> 4) & 0xffffff00) - 0x100, LKM13);

	/* Local bus slot #0 */
	ocd_writel(0x00040510, LDP0);
	ocd_writel((EXCITE_PHYS_BOOTROM >> 4) | 0x1UL, LKB9);
	ocd_writel(((EXCITE_SIZE_BOOTROM >> 4) & 0x03ffff00) - 0x100, LKM9);

	/* Local bus slot #2 */
	ocd_writel(0x00000330, LDP2);
	ocd_writel((EXCITE_PHYS_FPGA >> 4) | 0x1, LKB11);
	ocd_writel(((EXCITE_SIZE_FPGA >> 4) - 0x100) & 0x03ffff00, LKM11);

	/* Local bus slot #3 */
	ocd_writel(0x00123413, LDP3);
	ocd_writel((EXCITE_PHYS_NAND >> 4) | 0x1, LKB12);
	ocd_writel(((EXCITE_SIZE_NAND >> 4) - 0x100) & 0x03ffff00, LKM12);
}



console_initcall(excite_init_console);
arch_initcall(excite_platform_init);

EXPORT_SYMBOL(titan_lock);
EXPORT_SYMBOL(titan_irqflags);
EXPORT_SYMBOL(titan_irq);
EXPORT_SYMBOL(ocd_base);
EXPORT_SYMBOL(titan_base);
