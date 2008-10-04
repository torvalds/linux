/*
 * arch/arm/mach-ep93xx/core.c
 * Core routines for Cirrus EP93xx chips.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Thanks go to Michael Burian and Ray Lehtiniemi for their key
 * role in the ep93xx linux community.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
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
#include <linux/delay.h>
#include <linux/termios.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <mach/gpio.h>

#include <asm/hardware/vic.h>


/*************************************************************************
 * Static I/O mappings that are needed for all EP93xx platforms
 *************************************************************************/
static struct map_desc ep93xx_io_desc[] __initdata = {
	{
		.virtual	= EP93XX_AHB_VIRT_BASE,
		.pfn		= __phys_to_pfn(EP93XX_AHB_PHYS_BASE),
		.length		= EP93XX_AHB_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= EP93XX_APB_VIRT_BASE,
		.pfn		= __phys_to_pfn(EP93XX_APB_PHYS_BASE),
		.length		= EP93XX_APB_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init ep93xx_map_io(void)
{
	iotable_init(ep93xx_io_desc, ARRAY_SIZE(ep93xx_io_desc));
}


/*************************************************************************
 * Timer handling for EP93xx
 *************************************************************************
 * The ep93xx has four internal timers.  Timers 1, 2 (both 16 bit) and
 * 3 (32 bit) count down at 508 kHz, are self-reloading, and can generate
 * an interrupt on underflow.  Timer 4 (40 bit) counts down at 983.04 kHz,
 * is free-running, and can't generate interrupts.
 *
 * The 508 kHz timers are ideal for use for the timer interrupt, as the
 * most common values of HZ divide 508 kHz nicely.  We pick one of the 16
 * bit timers (timer 1) since we don't need more than 16 bits of reload
 * value as long as HZ >= 8.
 *
 * The higher clock rate of timer 4 makes it a better choice than the
 * other timers for use in gettimeoffset(), while the fact that it can't
 * generate interrupts means we don't have to worry about not being able
 * to use this timer for something else.  We also use timer 4 for keeping
 * track of lost jiffies.
 */
static unsigned int last_jiffy_time;

#define TIMER4_TICKS_PER_JIFFY		((CLOCK_TICK_RATE + (HZ/2)) / HZ)

static int ep93xx_timer_interrupt(int irq, void *dev_id)
{
	__raw_writel(1, EP93XX_TIMER1_CLEAR);
	while ((signed long)
		(__raw_readl(EP93XX_TIMER4_VALUE_LOW) - last_jiffy_time)
						>= TIMER4_TICKS_PER_JIFFY) {
		last_jiffy_time += TIMER4_TICKS_PER_JIFFY;
		timer_tick();
	}

	return IRQ_HANDLED;
}

static struct irqaction ep93xx_timer_irq = {
	.name		= "ep93xx timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= ep93xx_timer_interrupt,
};

static void __init ep93xx_timer_init(void)
{
	/* Enable periodic HZ timer.  */
	__raw_writel(0x48, EP93XX_TIMER1_CONTROL);
	__raw_writel((508469 / HZ) - 1, EP93XX_TIMER1_LOAD);
	__raw_writel(0xc8, EP93XX_TIMER1_CONTROL);

	/* Enable lost jiffy timer.  */
	__raw_writel(0x100, EP93XX_TIMER4_VALUE_HIGH);

	setup_irq(IRQ_EP93XX_TIMER1, &ep93xx_timer_irq);
}

static unsigned long ep93xx_gettimeoffset(void)
{
	int offset;

	offset = __raw_readl(EP93XX_TIMER4_VALUE_LOW) - last_jiffy_time;

	/* Calculate (1000000 / 983040) * offset.  */
	return offset + (53 * offset / 3072);
}

struct sys_timer ep93xx_timer = {
	.init		= ep93xx_timer_init,
	.offset		= ep93xx_gettimeoffset,
};


/*************************************************************************
 * GPIO handling for EP93xx
 *************************************************************************/
static unsigned char gpio_int_unmasked[3];
static unsigned char gpio_int_enabled[3];
static unsigned char gpio_int_type1[3];
static unsigned char gpio_int_type2[3];

/* Port ordering is: A B F */
static const u8 int_type1_register_offset[3]	= { 0x90, 0xac, 0x4c };
static const u8 int_type2_register_offset[3]	= { 0x94, 0xb0, 0x50 };
static const u8 eoi_register_offset[3]		= { 0x98, 0xb4, 0x54 };
static const u8 int_en_register_offset[3]	= { 0x9c, 0xb8, 0x58 };

void ep93xx_gpio_update_int_params(unsigned port)
{
	BUG_ON(port > 2);

	__raw_writeb(0, EP93XX_GPIO_REG(int_en_register_offset[port]));

	__raw_writeb(gpio_int_type2[port],
		EP93XX_GPIO_REG(int_type2_register_offset[port]));

	__raw_writeb(gpio_int_type1[port],
		EP93XX_GPIO_REG(int_type1_register_offset[port]));

	__raw_writeb(gpio_int_unmasked[port] & gpio_int_enabled[port],
		EP93XX_GPIO_REG(int_en_register_offset[port]));
}

void ep93xx_gpio_int_mask(unsigned line)
{
	gpio_int_unmasked[line >> 3] &= ~(1 << (line & 7));
}

/*************************************************************************
 * EP93xx IRQ handling
 *************************************************************************/
static void ep93xx_gpio_ab_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned char status;
	int i;

	status = __raw_readb(EP93XX_GPIO_A_INT_STATUS);
	for (i = 0; i < 8; i++) {
		if (status & (1 << i)) {
			int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_A(0)) + i;
			desc = irq_desc + gpio_irq;
			desc_handle_irq(gpio_irq, desc);
		}
	}

	status = __raw_readb(EP93XX_GPIO_B_INT_STATUS);
	for (i = 0; i < 8; i++) {
		if (status & (1 << i)) {
			int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_B(0)) + i;
			desc = irq_desc + gpio_irq;
			desc_handle_irq(gpio_irq, desc);
		}
	}
}

static void ep93xx_gpio_f_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	/*
	 * map discontiguous hw irq range to continous sw irq range:
	 *
	 *  IRQ_EP93XX_GPIO{0..7}MUX -> gpio_to_irq(EP93XX_GPIO_LINE_F({0..7})
	 */
	int port_f_idx = ((irq + 1) & 7) ^ 4; /* {19..22,47..50} -> {0..7} */
	int gpio_irq = gpio_to_irq(EP93XX_GPIO_LINE_F(0)) + port_f_idx;

	desc_handle_irq(gpio_irq, irq_desc + gpio_irq);
}

static void ep93xx_gpio_irq_ack(unsigned int irq)
{
	int line = irq_to_gpio(irq);
	int port = line >> 3;
	int port_mask = 1 << (line & 7);

	if ((irq_desc[irq].status & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */
		ep93xx_gpio_update_int_params(port);
	}

	__raw_writeb(port_mask, EP93XX_GPIO_REG(eoi_register_offset[port]));
}

static void ep93xx_gpio_irq_mask_ack(unsigned int irq)
{
	int line = irq_to_gpio(irq);
	int port = line >> 3;
	int port_mask = 1 << (line & 7);

	if ((irq_desc[irq].status & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
		gpio_int_type2[port] ^= port_mask; /* switch edge direction */

	gpio_int_unmasked[port] &= ~port_mask;
	ep93xx_gpio_update_int_params(port);

	__raw_writeb(port_mask, EP93XX_GPIO_REG(eoi_register_offset[port]));
}

static void ep93xx_gpio_irq_mask(unsigned int irq)
{
	int line = irq_to_gpio(irq);
	int port = line >> 3;

	gpio_int_unmasked[port] &= ~(1 << (line & 7));
	ep93xx_gpio_update_int_params(port);
}

static void ep93xx_gpio_irq_unmask(unsigned int irq)
{
	int line = irq_to_gpio(irq);
	int port = line >> 3;

	gpio_int_unmasked[port] |= 1 << (line & 7);
	ep93xx_gpio_update_int_params(port);
}


/*
 * gpio_int_type1 controls whether the interrupt is level (0) or
 * edge (1) triggered, while gpio_int_type2 controls whether it
 * triggers on low/falling (0) or high/rising (1).
 */
static int ep93xx_gpio_irq_type(unsigned int irq, unsigned int type)
{
	struct irq_desc *desc = irq_desc + irq;
	const int gpio = irq_to_gpio(irq);
	const int port = gpio >> 3;
	const int port_mask = 1 << (gpio & 7);

	gpio_direction_input(gpio);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		gpio_int_type1[port] |= port_mask;
		gpio_int_type2[port] |= port_mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio_int_type1[port] |= port_mask;
		gpio_int_type2[port] &= ~port_mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		gpio_int_type1[port] &= ~port_mask;
		gpio_int_type2[port] |= port_mask;
		desc->handle_irq = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio_int_type1[port] &= ~port_mask;
		gpio_int_type2[port] &= ~port_mask;
		desc->handle_irq = handle_level_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		gpio_int_type1[port] |= port_mask;
		/* set initial polarity based on current input level */
		if (gpio_get_value(gpio))
			gpio_int_type2[port] &= ~port_mask; /* falling */
		else
			gpio_int_type2[port] |= port_mask; /* rising */
		desc->handle_irq = handle_edge_irq;
		break;
	default:
		pr_err("ep93xx: failed to set irq type %d for gpio %d\n",
		       type, gpio);
		return -EINVAL;
	}

	gpio_int_enabled[port] |= port_mask;

	desc->status &= ~IRQ_TYPE_SENSE_MASK;
	desc->status |= type & IRQ_TYPE_SENSE_MASK;

	ep93xx_gpio_update_int_params(port);

	return 0;
}

static struct irq_chip ep93xx_gpio_irq_chip = {
	.name		= "GPIO",
	.ack		= ep93xx_gpio_irq_ack,
	.mask_ack	= ep93xx_gpio_irq_mask_ack,
	.mask		= ep93xx_gpio_irq_mask,
	.unmask		= ep93xx_gpio_irq_unmask,
	.set_type	= ep93xx_gpio_irq_type,
};


void __init ep93xx_init_irq(void)
{
	int gpio_irq;

	vic_init((void *)EP93XX_VIC1_BASE, 0, EP93XX_VIC1_VALID_IRQ_MASK);
	vic_init((void *)EP93XX_VIC2_BASE, 32, EP93XX_VIC2_VALID_IRQ_MASK);

	for (gpio_irq = gpio_to_irq(0);
	     gpio_irq <= gpio_to_irq(EP93XX_GPIO_LINE_MAX_IRQ); ++gpio_irq) {
		set_irq_chip(gpio_irq, &ep93xx_gpio_irq_chip);
		set_irq_handler(gpio_irq, handle_level_irq);
		set_irq_flags(gpio_irq, IRQF_VALID);
	}

	set_irq_chained_handler(IRQ_EP93XX_GPIO_AB, ep93xx_gpio_ab_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO0MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO1MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO2MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO3MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO4MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO5MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO6MUX, ep93xx_gpio_f_irq_handler);
	set_irq_chained_handler(IRQ_EP93XX_GPIO7MUX, ep93xx_gpio_f_irq_handler);
}


/*************************************************************************
 * EP93xx peripheral handling
 *************************************************************************/
#define EP93XX_UART_MCR_OFFSET		(0x0100)

static void ep93xx_uart_set_mctrl(struct amba_device *dev,
				  void __iomem *base, unsigned int mctrl)
{
	unsigned int mcr;

	mcr = 0;
	if (!(mctrl & TIOCM_RTS))
		mcr |= 2;
	if (!(mctrl & TIOCM_DTR))
		mcr |= 1;

	__raw_writel(mcr, base + EP93XX_UART_MCR_OFFSET);
}

static struct amba_pl010_data ep93xx_uart_data = {
	.set_mctrl	= ep93xx_uart_set_mctrl,
};

static struct amba_device uart1_device = {
	.dev		= {
		.bus_id		= "apb:uart1",
		.platform_data	= &ep93xx_uart_data,
	},
	.res		= {
		.start	= EP93XX_UART1_PHYS_BASE,
		.end	= EP93XX_UART1_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_EP93XX_UART1, NO_IRQ },
	.periphid	= 0x00041010,
};

static struct amba_device uart2_device = {
	.dev		= {
		.bus_id		= "apb:uart2",
		.platform_data	= &ep93xx_uart_data,
	},
	.res		= {
		.start	= EP93XX_UART2_PHYS_BASE,
		.end	= EP93XX_UART2_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_EP93XX_UART2, NO_IRQ },
	.periphid	= 0x00041010,
};

static struct amba_device uart3_device = {
	.dev		= {
		.bus_id		= "apb:uart3",
		.platform_data	= &ep93xx_uart_data,
	},
	.res		= {
		.start	= EP93XX_UART3_PHYS_BASE,
		.end	= EP93XX_UART3_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_EP93XX_UART3, NO_IRQ },
	.periphid	= 0x00041010,
};


static struct platform_device ep93xx_rtc_device = {
       .name           = "ep93xx-rtc",
       .id             = -1,
       .num_resources  = 0,
};


static struct resource ep93xx_ohci_resources[] = {
	[0] = {
		.start	= EP93XX_USB_PHYS_BASE,
		.end	= EP93XX_USB_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_EP93XX_USB,
		.end	= IRQ_EP93XX_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ep93xx_ohci_device = {
	.name		= "ep93xx-ohci",
	.id		= -1,
	.dev		= {
		.dma_mask		= (void *)0xffffffff,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ep93xx_ohci_resources),
	.resource	= ep93xx_ohci_resources,
};

static struct ep93xx_eth_data ep93xx_eth_data;

static struct resource ep93xx_eth_resource[] = {
	{
		.start	= EP93XX_ETHERNET_PHYS_BASE,
		.end	= EP93XX_ETHERNET_PHYS_BASE + 0xffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_EP93XX_ETHERNET,
		.end	= IRQ_EP93XX_ETHERNET,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device ep93xx_eth_device = {
	.name		= "ep93xx-eth",
	.id		= -1,
	.dev		= {
		.platform_data	= &ep93xx_eth_data,
	},
	.num_resources	= ARRAY_SIZE(ep93xx_eth_resource),
	.resource	= ep93xx_eth_resource,
};

void __init ep93xx_register_eth(struct ep93xx_eth_data *data, int copy_addr)
{
	if (copy_addr) {
		memcpy(data->dev_addr,
			(void *)(EP93XX_ETHERNET_BASE + 0x50), 6);
	}

	ep93xx_eth_data = *data;
	platform_device_register(&ep93xx_eth_device);
}

extern void ep93xx_gpio_init(void);

void __init ep93xx_init_devices(void)
{
	unsigned int v;

	/*
	 * Disallow access to MaverickCrunch initially.
	 */
	v = __raw_readl(EP93XX_SYSCON_DEVICE_CONFIG);
	v &= ~EP93XX_SYSCON_DEVICE_CONFIG_CRUNCH_ENABLE;
	__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
	__raw_writel(v, EP93XX_SYSCON_DEVICE_CONFIG);

	ep93xx_gpio_init();

	amba_device_register(&uart1_device, &iomem_resource);
	amba_device_register(&uart2_device, &iomem_resource);
	amba_device_register(&uart3_device, &iomem_resource);

	platform_device_register(&ep93xx_rtc_device);
	platform_device_register(&ep93xx_ohci_device);
}
