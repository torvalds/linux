/*
 * arch/arm/mach-ixp23xx/core.c
 *
 * Core routines for IXP23xx chips
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Based on 2.4 code Copyright 2004 (c) Intel Corporation
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/bitops.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/timex.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/mach/arch.h>


/*************************************************************************
 * Chip specific mappings shared by all IXP23xx systems
 *************************************************************************/
static struct map_desc ixp23xx_io_desc[] __initdata = {
	{ /* XSI-CPP CSRs */
	 	.virtual	= IXP23XX_XSI2CPP_CSR_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_XSI2CPP_CSR_PHYS),
	 	.length		= IXP23XX_XSI2CPP_CSR_SIZE,
		.type		= MT_DEVICE,
	}, { /* Expansion Bus Config */
	 	.virtual	= IXP23XX_EXP_CFG_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_EXP_CFG_PHYS),
	 	.length		= IXP23XX_EXP_CFG_SIZE,
		.type		= MT_DEVICE,
	}, { /* UART, Interrupt ctrl, GPIO, timers, NPEs, MACS,.... */
	 	.virtual	= IXP23XX_PERIPHERAL_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_PERIPHERAL_PHYS),
	 	.length		= IXP23XX_PERIPHERAL_SIZE,
		.type		= MT_DEVICE,
	}, { /* CAP CSRs */
	 	.virtual	= IXP23XX_CAP_CSR_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_CAP_CSR_PHYS),
	 	.length		= IXP23XX_CAP_CSR_SIZE,
		.type		= MT_DEVICE,
	}, { /* MSF CSRs */
	 	.virtual	= IXP23XX_MSF_CSR_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_MSF_CSR_PHYS),
	 	.length		= IXP23XX_MSF_CSR_SIZE,
		.type		= MT_DEVICE,
	}, { /* PCI I/O Space */
	 	.virtual	= IXP23XX_PCI_IO_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_PCI_IO_PHYS),
	 	.length		= IXP23XX_PCI_IO_SIZE,
		.type		= MT_DEVICE,
	}, { /* PCI Config Space */
	 	.virtual	= IXP23XX_PCI_CFG_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_PCI_CFG_PHYS),
	 	.length		= IXP23XX_PCI_CFG_SIZE,
		.type		= MT_DEVICE,
	}, { /* PCI local CFG CSRs */
	 	.virtual	= IXP23XX_PCI_CREG_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_PCI_CREG_PHYS),
	 	.length		= IXP23XX_PCI_CREG_SIZE,
		.type		= MT_DEVICE,
	}, { /* PCI MEM Space */
	 	.virtual	= IXP23XX_PCI_MEM_VIRT,
	 	.pfn		= __phys_to_pfn(IXP23XX_PCI_MEM_PHYS),
	 	.length		= IXP23XX_PCI_MEM_SIZE,
		.type		= MT_DEVICE,
	}
};

void __init ixp23xx_map_io(void)
{
	iotable_init(ixp23xx_io_desc, ARRAY_SIZE(ixp23xx_io_desc));
}


/***************************************************************************
 * IXP23xx Interrupt Handling
 ***************************************************************************/
enum ixp23xx_irq_type {
	IXP23XX_IRQ_LEVEL, IXP23XX_IRQ_EDGE
};

static void ixp23xx_config_irq(unsigned int, enum ixp23xx_irq_type);

static int ixp23xx_irq_set_type(struct irq_data *d, unsigned int type)
{
	int line = d->irq - IRQ_IXP23XX_GPIO6 + 6;
	u32 int_style;
	enum ixp23xx_irq_type irq_type;
	volatile u32 *int_reg;

	/*
	 * Only GPIOs 6-15 are wired to interrupts on IXP23xx
	 */
	if (line < 6 || line > 15)
		return -EINVAL;

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		int_style = IXP23XX_GPIO_STYLE_TRANSITIONAL;
		irq_type = IXP23XX_IRQ_EDGE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		int_style = IXP23XX_GPIO_STYLE_RISING_EDGE;
		irq_type = IXP23XX_IRQ_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_style = IXP23XX_GPIO_STYLE_FALLING_EDGE;
		irq_type = IXP23XX_IRQ_EDGE;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_style = IXP23XX_GPIO_STYLE_ACTIVE_HIGH;
		irq_type = IXP23XX_IRQ_LEVEL;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_style = IXP23XX_GPIO_STYLE_ACTIVE_LOW;
		irq_type = IXP23XX_IRQ_LEVEL;
		break;
	default:
		return -EINVAL;
	}

	ixp23xx_config_irq(d->irq, irq_type);

	if (line >= 8) {	/* pins 8-15 */
		line -= 8;
		int_reg = (volatile u32 *)IXP23XX_GPIO_GPIT2R;
	} else {		/* pins 0-7 */
		int_reg = (volatile u32 *)IXP23XX_GPIO_GPIT1R;
	}

	/*
	 * Clear pending interrupts
	 */
	*IXP23XX_GPIO_GPISR = (1 << line);

	/* Clear the style for the appropriate pin */
	*int_reg &= ~(IXP23XX_GPIO_STYLE_MASK <<
			(line * IXP23XX_GPIO_STYLE_SIZE));

	/* Set the new style */
	*int_reg |= (int_style << (line * IXP23XX_GPIO_STYLE_SIZE));

	return 0;
}

static void ixp23xx_irq_mask(struct irq_data *d)
{
	volatile unsigned long *intr_reg;
	unsigned int irq = d->irq;

	if (irq >= 56)
		irq += 8;

	intr_reg = IXP23XX_INTR_EN1 + (irq / 32);
	*intr_reg &= ~(1 << (irq % 32));
}

static void ixp23xx_irq_ack(struct irq_data *d)
{
	int line = d->irq - IRQ_IXP23XX_GPIO6 + 6;

	if ((line < 6) || (line > 15))
		return;

	*IXP23XX_GPIO_GPISR = (1 << line);
}

/*
 * Level triggered interrupts on GPIO lines can only be cleared when the
 * interrupt condition disappears.
 */
static void ixp23xx_irq_level_unmask(struct irq_data *d)
{
	volatile unsigned long *intr_reg;
	unsigned int irq = d->irq;

	ixp23xx_irq_ack(d);

	if (irq >= 56)
		irq += 8;

	intr_reg = IXP23XX_INTR_EN1 + (irq / 32);
	*intr_reg |= (1 << (irq % 32));
}

static void ixp23xx_irq_edge_unmask(struct irq_data *d)
{
	volatile unsigned long *intr_reg;
	unsigned int irq = d->irq;

	if (irq >= 56)
		irq += 8;

	intr_reg = IXP23XX_INTR_EN1 + (irq / 32);
	*intr_reg |= (1 << (irq % 32));
}

static struct irq_chip ixp23xx_irq_level_chip = {
	.irq_ack	= ixp23xx_irq_mask,
	.irq_mask	= ixp23xx_irq_mask,
	.irq_unmask	= ixp23xx_irq_level_unmask,
	.irq_set_type	= ixp23xx_irq_set_type
};

static struct irq_chip ixp23xx_irq_edge_chip = {
	.irq_ack	= ixp23xx_irq_ack,
	.irq_mask	= ixp23xx_irq_mask,
	.irq_unmask	= ixp23xx_irq_edge_unmask,
	.irq_set_type	= ixp23xx_irq_set_type
};

static void ixp23xx_pci_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq;

	*IXP23XX_PCI_XSCALE_INT_ENABLE &= ~(1 << (IRQ_IXP23XX_INTA + 27 - irq));
}

static void ixp23xx_pci_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq;

	*IXP23XX_PCI_XSCALE_INT_ENABLE |= (1 << (IRQ_IXP23XX_INTA + 27 - irq));
}

/*
 * TODO: Should this just be done at ASM level?
 */
static void pci_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 pci_interrupt;
	unsigned int irqno;

	pci_interrupt = *IXP23XX_PCI_XSCALE_INT_STATUS;

	desc->irq_data.chip->irq_ack(&desc->irq_data);

	/* See which PCI_INTA, or PCI_INTB interrupted */
	if (pci_interrupt & (1 << 26)) {
		irqno = IRQ_IXP23XX_INTB;
	} else if (pci_interrupt & (1 << 27)) {
		irqno = IRQ_IXP23XX_INTA;
	} else {
		BUG();
	}

	generic_handle_irq(irqno);

	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

static struct irq_chip ixp23xx_pci_irq_chip = {
	.irq_ack	= ixp23xx_pci_irq_mask,
	.irq_mask	= ixp23xx_pci_irq_mask,
	.irq_unmask	= ixp23xx_pci_irq_unmask
};

static void ixp23xx_config_irq(unsigned int irq, enum ixp23xx_irq_type type)
{
	switch (type) {
	case IXP23XX_IRQ_LEVEL:
		irq_set_chip_and_handler(irq, &ixp23xx_irq_level_chip,
					 handle_level_irq);
		break;
	case IXP23XX_IRQ_EDGE:
		irq_set_chip_and_handler(irq, &ixp23xx_irq_edge_chip,
					 handle_edge_irq);
		break;
	}
	set_irq_flags(irq, IRQF_VALID);
}

void __init ixp23xx_init_irq(void)
{
	int irq;

	/* Route everything to IRQ */
	*IXP23XX_INTR_SEL1 = 0x0;
	*IXP23XX_INTR_SEL2 = 0x0;
	*IXP23XX_INTR_SEL3 = 0x0;
	*IXP23XX_INTR_SEL4 = 0x0;

	/* Mask all sources */
	*IXP23XX_INTR_EN1 = 0x0;
	*IXP23XX_INTR_EN2 = 0x0;
	*IXP23XX_INTR_EN3 = 0x0;
	*IXP23XX_INTR_EN4 = 0x0;

	/*
	 * Configure all IRQs for level-sensitive operation
	 */
	for (irq = 0; irq <= NUM_IXP23XX_RAW_IRQS; irq++) {
		ixp23xx_config_irq(irq, IXP23XX_IRQ_LEVEL);
	}

	for (irq = IRQ_IXP23XX_INTA; irq <= IRQ_IXP23XX_INTB; irq++) {
		irq_set_chip_and_handler(irq, &ixp23xx_pci_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	irq_set_chained_handler(IRQ_IXP23XX_PCI_INT_RPH, pci_handler);
}


/*************************************************************************
 * Timer-tick functions for IXP23xx
 *************************************************************************/
#define CLOCK_TICKS_PER_USEC	(CLOCK_TICK_RATE / USEC_PER_SEC)

static unsigned long next_jiffy_time;

static unsigned long
ixp23xx_gettimeoffset(void)
{
	unsigned long elapsed;

	elapsed = *IXP23XX_TIMER_CONT - (next_jiffy_time - LATCH);

	return elapsed / CLOCK_TICKS_PER_USEC;
}

static irqreturn_t
ixp23xx_timer_interrupt(int irq, void *dev_id)
{
	/* Clear Pending Interrupt by writing '1' to it */
	*IXP23XX_TIMER_STATUS = IXP23XX_TIMER1_INT_PEND;
	while ((signed long)(*IXP23XX_TIMER_CONT - next_jiffy_time) >= LATCH) {
		timer_tick();
		next_jiffy_time += LATCH;
	}

	return IRQ_HANDLED;
}

static struct irqaction ixp23xx_timer_irq = {
	.name		= "IXP23xx Timer Tick",
	.handler	= ixp23xx_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
};

void __init ixp23xx_init_timer(void)
{
	/* Clear Pending Interrupt by writing '1' to it */
	*IXP23XX_TIMER_STATUS = IXP23XX_TIMER1_INT_PEND;

	/* Setup the Timer counter value */
	*IXP23XX_TIMER1_RELOAD =
		(LATCH & ~IXP23XX_TIMER_RELOAD_MASK) | IXP23XX_TIMER_ENABLE;

	*IXP23XX_TIMER_CONT = 0;
	next_jiffy_time = LATCH;

	/* Connect the interrupt handler and enable the interrupt */
	setup_irq(IRQ_IXP23XX_TIMER1, &ixp23xx_timer_irq);
}

struct sys_timer ixp23xx_timer = {
	.init		= ixp23xx_init_timer,
	.offset		= ixp23xx_gettimeoffset,
};


/*************************************************************************
 * IXP23xx Platform Initialization
 *************************************************************************/
static struct resource ixp23xx_uart_resources[] = {
	{
		.start		= IXP23XX_UART1_PHYS,
		.end		= IXP23XX_UART1_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	}, {
		.start		= IXP23XX_UART2_PHYS,
		.end		= IXP23XX_UART2_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	}
};

static struct plat_serial8250_port ixp23xx_uart_data[] = {
	{
		.mapbase	= IXP23XX_UART1_PHYS,
		.membase	= (char *)(IXP23XX_UART1_VIRT + 3),
		.irq		= IRQ_IXP23XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP23XX_UART_XTAL,
	}, {
		.mapbase	= IXP23XX_UART2_PHYS,
		.membase	= (char *)(IXP23XX_UART2_VIRT + 3),
		.irq		= IRQ_IXP23XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP23XX_UART_XTAL,
	},
	{ },
};

static struct platform_device ixp23xx_uart = {
	.name			= "serial8250",
	.id			= 0,
	.dev.platform_data	= ixp23xx_uart_data,
	.num_resources		= 2,
	.resource		= ixp23xx_uart_resources,
};

static struct platform_device *ixp23xx_devices[] __initdata = {
	&ixp23xx_uart,
};

void __init ixp23xx_sys_init(void)
{
	*IXP23XX_EXP_UNIT_FUSE |= 0xf;
	platform_add_devices(ixp23xx_devices, ARRAY_SIZE(ixp23xx_devices));
}

void ixp23xx_restart(char mode, const char *cmd)
{
	/* Use on-chip reset capability */
	*IXP23XX_RESET0 |= IXP23XX_RST_ALL;
}
