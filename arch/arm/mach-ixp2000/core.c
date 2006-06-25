/*
 * arch/arm/mach-ixp2000/core.c
 *
 * Common routines used by all IXP2400/2800 based platforms.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (C) MontaVista Software, Inc. 
 *
 * Based on work Copyright (C) 2002-2003 Intel Corporation
 * 
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/bitops.h>
#include <linux/serial_8250.h>
#include <linux/mm.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>

#include <asm/arch/gpio.h>

static DEFINE_SPINLOCK(ixp2000_slowport_lock);
static unsigned long ixp2000_slowport_irq_flags;

/*************************************************************************
 * Slowport access routines
 *************************************************************************/
void ixp2000_acquire_slowport(struct slowport_cfg *new_cfg, struct slowport_cfg *old_cfg)
{
	spin_lock_irqsave(&ixp2000_slowport_lock, ixp2000_slowport_irq_flags);

	old_cfg->CCR = *IXP2000_SLOWPORT_CCR;
	old_cfg->WTC = *IXP2000_SLOWPORT_WTC2;
	old_cfg->RTC = *IXP2000_SLOWPORT_RTC2;
	old_cfg->PCR = *IXP2000_SLOWPORT_PCR;
	old_cfg->ADC = *IXP2000_SLOWPORT_ADC;

	ixp2000_reg_write(IXP2000_SLOWPORT_CCR, new_cfg->CCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_WTC2, new_cfg->WTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_RTC2, new_cfg->RTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_PCR, new_cfg->PCR);
	ixp2000_reg_wrb(IXP2000_SLOWPORT_ADC, new_cfg->ADC);
}

void ixp2000_release_slowport(struct slowport_cfg *old_cfg)
{
	ixp2000_reg_write(IXP2000_SLOWPORT_CCR, old_cfg->CCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_WTC2, old_cfg->WTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_RTC2, old_cfg->RTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_PCR, old_cfg->PCR);
	ixp2000_reg_wrb(IXP2000_SLOWPORT_ADC, old_cfg->ADC);

	spin_unlock_irqrestore(&ixp2000_slowport_lock, 
					ixp2000_slowport_irq_flags);
}

/*************************************************************************
 * Chip specific mappings shared by all IXP2000 systems
 *************************************************************************/
static struct map_desc ixp2000_io_desc[] __initdata = {
	{
		.virtual	= IXP2000_CAP_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_CAP_PHYS_BASE),
		.length		= IXP2000_CAP_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_INTCTL_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_INTCTL_PHYS_BASE),
		.length		= IXP2000_INTCTL_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_PCI_CREG_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_PCI_CREG_PHYS_BASE),
		.length		= IXP2000_PCI_CREG_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_PCI_CSR_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_PCI_CSR_PHYS_BASE),
		.length		= IXP2000_PCI_CSR_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_MSF_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_MSF_PHYS_BASE),
		.length		= IXP2000_MSF_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_SCRATCH_RING_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_SCRATCH_RING_PHYS_BASE),
		.length		= IXP2000_SCRATCH_RING_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_SRAM0_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_SRAM0_PHYS_BASE),
		.length		= IXP2000_SRAM0_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_PCI_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_PCI_IO_PHYS_BASE),
		.length		= IXP2000_PCI_IO_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_PCI_CFG0_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_PCI_CFG0_PHYS_BASE),
		.length		= IXP2000_PCI_CFG0_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}, {
		.virtual	= IXP2000_PCI_CFG1_VIRT_BASE,
		.pfn		= __phys_to_pfn(IXP2000_PCI_CFG1_PHYS_BASE),
		.length		= IXP2000_PCI_CFG1_SIZE,
		.type		= MT_IXP2000_DEVICE,
	}
};

void __init ixp2000_map_io(void)
{
	/*
	 * On IXP2400 CPUs we need to use MT_IXP2000_DEVICE so that
	 * XCB=101 (to avoid triggering erratum #66), and given that
	 * this mode speeds up I/O accesses and we have write buffer
	 * flushes in the right places anyway, it doesn't hurt to use
	 * XCB=101 for all IXP2000s.
	 */
	iotable_init(ixp2000_io_desc, ARRAY_SIZE(ixp2000_io_desc));

	/* Set slowport to 8-bit mode.  */
	ixp2000_reg_wrb(IXP2000_SLOWPORT_FRM, 1);
}


/*************************************************************************
 * Serial port support for IXP2000
 *************************************************************************/
static struct plat_serial8250_port ixp2000_serial_port[] = {
	{
		.mapbase	= IXP2000_UART_PHYS_BASE,
		.membase	= (char *)(IXP2000_UART_VIRT_BASE + 3),
		.irq		= IRQ_IXP2000_UART,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 50000000,
	},
	{ },
};

static struct resource ixp2000_uart_resource = {
	.start		= IXP2000_UART_PHYS_BASE,
	.end		= IXP2000_UART_PHYS_BASE + 0x1f,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixp2000_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= ixp2000_serial_port,
	},
	.num_resources	= 1,
	.resource	= &ixp2000_uart_resource,
};

void __init ixp2000_uart_init(void)
{
	platform_device_register(&ixp2000_serial_device);
}


/*************************************************************************
 * Timer-tick functions for IXP2000
 *************************************************************************/
static unsigned ticks_per_jiffy;
static unsigned ticks_per_usec;
static unsigned next_jiffy_time;
static volatile unsigned long *missing_jiffy_timer_csr;

unsigned long ixp2000_gettimeoffset (void)
{
 	unsigned long offset;

	offset = next_jiffy_time - *missing_jiffy_timer_csr;

	return offset / ticks_per_usec;
}

static int ixp2000_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	/* clear timer 1 */
	ixp2000_reg_wrb(IXP2000_T1_CLR, 1);

	while ((signed long)(next_jiffy_time - *missing_jiffy_timer_csr)
							>= ticks_per_jiffy) {
		timer_tick(regs);
		next_jiffy_time -= ticks_per_jiffy;
	}

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction ixp2000_timer_irq = {
	.name		= "IXP2000 Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= ixp2000_timer_interrupt,
};

void __init ixp2000_init_time(unsigned long tick_rate)
{
	ticks_per_jiffy = (tick_rate + HZ/2) / HZ;
	ticks_per_usec = tick_rate / 1000000;

	/*
	 * We use timer 1 as our timer interrupt.
	 */
	ixp2000_reg_write(IXP2000_T1_CLR, 0);
	ixp2000_reg_write(IXP2000_T1_CLD, ticks_per_jiffy - 1);
	ixp2000_reg_write(IXP2000_T1_CTL, (1 << 7));

	/*
	 * We use a second timer as a monotonic counter for tracking
	 * missed jiffies.  The IXP2000 has four timers, but if we're
	 * on an A-step IXP2800, timer 2 and 3 don't work, so on those
	 * chips we use timer 4.  Timer 4 is the only timer that can
	 * be used for the watchdog, so we use timer 2 if we're on a
	 * non-buggy chip.
	 */
	if ((*IXP2000_PRODUCT_ID & 0x001ffef0) == 0x00000000) {
		printk(KERN_INFO "Enabling IXP2800 erratum #25 workaround\n");

		ixp2000_reg_write(IXP2000_T4_CLR, 0);
		ixp2000_reg_write(IXP2000_T4_CLD, -1);
		ixp2000_reg_wrb(IXP2000_T4_CTL, (1 << 7));
		missing_jiffy_timer_csr = IXP2000_T4_CSR;
	} else {
		ixp2000_reg_write(IXP2000_T2_CLR, 0);
		ixp2000_reg_write(IXP2000_T2_CLD, -1);
		ixp2000_reg_wrb(IXP2000_T2_CTL, (1 << 7));
		missing_jiffy_timer_csr = IXP2000_T2_CSR;
	}
 	next_jiffy_time = 0xffffffff;

	/* register for interrupt */
	setup_irq(IRQ_IXP2000_TIMER1, &ixp2000_timer_irq);
}

/*************************************************************************
 * GPIO helpers
 *************************************************************************/
static unsigned long GPIO_IRQ_falling_edge;
static unsigned long GPIO_IRQ_rising_edge;
static unsigned long GPIO_IRQ_level_low;
static unsigned long GPIO_IRQ_level_high;

static void update_gpio_int_csrs(void)
{
	ixp2000_reg_write(IXP2000_GPIO_FEDR, GPIO_IRQ_falling_edge);
	ixp2000_reg_write(IXP2000_GPIO_REDR, GPIO_IRQ_rising_edge);
	ixp2000_reg_write(IXP2000_GPIO_LSLR, GPIO_IRQ_level_low);
	ixp2000_reg_wrb(IXP2000_GPIO_LSHR, GPIO_IRQ_level_high);
}

void gpio_line_config(int line, int direction)
{
	unsigned long flags;

	local_irq_save(flags);
	if (direction == GPIO_OUT) {
		/* if it's an output, it ain't an interrupt anymore */
		GPIO_IRQ_falling_edge &= ~(1 << line);
		GPIO_IRQ_rising_edge &= ~(1 << line);
		GPIO_IRQ_level_low &= ~(1 << line);
		GPIO_IRQ_level_high &= ~(1 << line);
		update_gpio_int_csrs();

		ixp2000_reg_wrb(IXP2000_GPIO_PDSR, 1 << line);
	} else if (direction == GPIO_IN) {
		ixp2000_reg_wrb(IXP2000_GPIO_PDCR, 1 << line);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_line_config);


/*************************************************************************
 * IRQ handling IXP2000
 *************************************************************************/
static void ixp2000_GPIO_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{                               
	int i;
	unsigned long status = *IXP2000_GPIO_INST;
		   
	for (i = 0; i <= 7; i++) {
		if (status & (1<<i)) {
			desc = irq_desc + i + IRQ_IXP2000_GPIO0;
			desc_handle_irq(i + IRQ_IXP2000_GPIO0, desc, regs);
		}
	}
}

static int ixp2000_GPIO_irq_type(unsigned int irq, unsigned int type)
{
	int line = irq - IRQ_IXP2000_GPIO0;

	/*
	 * First, configure this GPIO line as an input.
	 */
	ixp2000_reg_write(IXP2000_GPIO_PDCR, 1 << line);

	/*
	 * Then, set the proper trigger type.
	 */
	if (type & IRQT_FALLING)
		GPIO_IRQ_falling_edge |= 1 << line;
	else
		GPIO_IRQ_falling_edge &= ~(1 << line);
	if (type & IRQT_RISING)
		GPIO_IRQ_rising_edge |= 1 << line;
	else
		GPIO_IRQ_rising_edge &= ~(1 << line);
	if (type & IRQT_LOW)
		GPIO_IRQ_level_low |= 1 << line;
	else
		GPIO_IRQ_level_low &= ~(1 << line);
	if (type & IRQT_HIGH)
		GPIO_IRQ_level_high |= 1 << line;
	else
		GPIO_IRQ_level_high &= ~(1 << line);
	update_gpio_int_csrs();

	return 0;
}

static void ixp2000_GPIO_irq_mask_ack(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_GPIO_INCR, (1 << (irq - IRQ_IXP2000_GPIO0)));

	ixp2000_reg_write(IXP2000_GPIO_EDSR, (1 << (irq - IRQ_IXP2000_GPIO0)));
	ixp2000_reg_write(IXP2000_GPIO_LDSR, (1 << (irq - IRQ_IXP2000_GPIO0)));
	ixp2000_reg_wrb(IXP2000_GPIO_INST, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static void ixp2000_GPIO_irq_mask(unsigned int irq)
{
	ixp2000_reg_wrb(IXP2000_GPIO_INCR, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static void ixp2000_GPIO_irq_unmask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_GPIO_INSR, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static struct irqchip ixp2000_GPIO_irq_chip = {
	.ack		= ixp2000_GPIO_irq_mask_ack,
	.mask		= ixp2000_GPIO_irq_mask,
	.unmask		= ixp2000_GPIO_irq_unmask,
	.set_type	= ixp2000_GPIO_irq_type,
};

static void ixp2000_pci_irq_mask(unsigned int irq)
{
	unsigned long temp = *IXP2000_PCI_XSCALE_INT_ENABLE;
	if (irq == IRQ_IXP2000_PCIA)
		ixp2000_reg_wrb(IXP2000_PCI_XSCALE_INT_ENABLE, (temp & ~(1 << 26)));
	else if (irq == IRQ_IXP2000_PCIB)
		ixp2000_reg_wrb(IXP2000_PCI_XSCALE_INT_ENABLE, (temp & ~(1 << 27)));
}

static void ixp2000_pci_irq_unmask(unsigned int irq)
{
	unsigned long temp = *IXP2000_PCI_XSCALE_INT_ENABLE;
	if (irq == IRQ_IXP2000_PCIA)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp | (1 << 26)));
	else if (irq == IRQ_IXP2000_PCIB)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp | (1 << 27)));
}

/*
 * Error interrupts. These are used extensively by the microengine drivers
 */
static void ixp2000_err_irq_handler(unsigned int irq, struct irqdesc *desc,  struct pt_regs *regs)
{
	int i;
	unsigned long status = *IXP2000_IRQ_ERR_STATUS;

	for(i = 31; i >= 0; i--) {
		if(status & (1 << i)) {
			desc = irq_desc + IRQ_IXP2000_DRAM0_MIN_ERR + i;
			desc->handle(IRQ_IXP2000_DRAM0_MIN_ERR + i, desc, regs);
		}
	}
}

static void ixp2000_err_irq_mask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_IRQ_ERR_ENABLE_CLR,
			(1 << (irq - IRQ_IXP2000_DRAM0_MIN_ERR)));
}

static void ixp2000_err_irq_unmask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_IRQ_ERR_ENABLE_SET,
			(1 << (irq - IRQ_IXP2000_DRAM0_MIN_ERR)));
}

static struct irqchip ixp2000_err_irq_chip = {
	.ack	= ixp2000_err_irq_mask,
	.mask	= ixp2000_err_irq_mask,
	.unmask	= ixp2000_err_irq_unmask
};

static struct irqchip ixp2000_pci_irq_chip = {
	.ack	= ixp2000_pci_irq_mask,
	.mask	= ixp2000_pci_irq_mask,
	.unmask	= ixp2000_pci_irq_unmask
};

static void ixp2000_irq_mask(unsigned int irq)
{
	ixp2000_reg_wrb(IXP2000_IRQ_ENABLE_CLR, (1 << irq));
}

static void ixp2000_irq_unmask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_SET, (1 << irq));
}

static struct irqchip ixp2000_irq_chip = {
	.ack	= ixp2000_irq_mask,
	.mask	= ixp2000_irq_mask,
	.unmask	= ixp2000_irq_unmask
};

void __init ixp2000_init_irq(void)
{
	int irq;

	/*
	 * Mask all sources
	 */
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_CLR, 0xffffffff);
	ixp2000_reg_write(IXP2000_FIQ_ENABLE_CLR, 0xffffffff);

	/* clear all GPIO edge/level detects */
	ixp2000_reg_write(IXP2000_GPIO_REDR, 0);
	ixp2000_reg_write(IXP2000_GPIO_FEDR, 0);
	ixp2000_reg_write(IXP2000_GPIO_LSHR, 0);
	ixp2000_reg_write(IXP2000_GPIO_LSLR, 0);
	ixp2000_reg_write(IXP2000_GPIO_INCR, -1);

	/* clear PCI interrupt sources */
	ixp2000_reg_wrb(IXP2000_PCI_XSCALE_INT_ENABLE, 0);

	/*
	 * Certain bits in the IRQ status register of the 
	 * IXP2000 are reserved. Instead of trying to map
	 * things non 1:1 from bit position to IRQ number,
	 * we mark the reserved IRQs as invalid. This makes
	 * our mask/unmask code much simpler.
	 */
	for (irq = IRQ_IXP2000_SOFT_INT; irq <= IRQ_IXP2000_THDB3; irq++) {
		if ((1 << irq) & IXP2000_VALID_IRQ_MASK) {
			set_irq_chip(irq, &ixp2000_irq_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, IRQF_VALID);
		} else set_irq_flags(irq, 0);
	}

	for (irq = IRQ_IXP2000_DRAM0_MIN_ERR; irq <= IRQ_IXP2000_SP_INT; irq++) {
		if((1 << (irq - IRQ_IXP2000_DRAM0_MIN_ERR)) &
				IXP2000_VALID_ERR_IRQ_MASK) {
			set_irq_chip(irq, &ixp2000_err_irq_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, IRQF_VALID);
		}
		else
			set_irq_flags(irq, 0);
	}
	set_irq_chained_handler(IRQ_IXP2000_ERRSUM, ixp2000_err_irq_handler);

	for (irq = IRQ_IXP2000_GPIO0; irq <= IRQ_IXP2000_GPIO7; irq++) {
		set_irq_chip(irq, &ixp2000_GPIO_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID);
	}
	set_irq_chained_handler(IRQ_IXP2000_GPIO, ixp2000_GPIO_irq_handler);

	/*
	 * Enable PCI irqs.  The actual PCI[AB] decoding is done in
	 * entry-macro.S, so we don't need a chained handler for the
	 * PCI interrupt source.
	 */
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_SET, (1 << IRQ_IXP2000_PCI));
	for (irq = IRQ_IXP2000_PCIA; irq <= IRQ_IXP2000_PCIB; irq++) {
		set_irq_chip(irq, &ixp2000_pci_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID);
	}
}

