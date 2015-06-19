/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Common Codes for S3C64XX machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * NOTE: Code in this file is not used when booting with Device Tree support.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/irqchip/arm-vic.h>
#include <clocksource/samsung_pwm.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include <mach/map.h>
#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/gpio-cfg.h>
#include <plat/irq-uart.h>
#include <plat/pwm-core.h>
#include <plat/regs-irqtype.h>
#include <plat/watchdog-reset.h>

#include "common.h"

/* External clock frequency */
static unsigned long xtal_f = 12000000, xusbxti_f = 48000000;

void __init s3c64xx_set_xtal_freq(unsigned long freq)
{
	xtal_f = freq;
}

void __init s3c64xx_set_xusbxti_freq(unsigned long freq)
{
	xusbxti_f = freq;
}

/* uart registration process */

static void __init s3c64xx_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c6400-uart", s3c64xx_uart_resources, cfg, no);
}

/* table of supported CPUs */

static const char name_s3c6400[] = "S3C6400";
static const char name_s3c6410[] = "S3C6410";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= S3C6400_CPU_ID,
		.idmask		= S3C64XX_CPU_MASK,
		.map_io		= s3c6400_map_io,
		.init_uarts	= s3c64xx_init_uarts,
		.init		= s3c6400_init,
		.name		= name_s3c6400,
	}, {
		.idcode		= S3C6410_CPU_ID,
		.idmask		= S3C64XX_CPU_MASK,
		.map_io		= s3c6410_map_io,
		.init_uarts	= s3c64xx_init_uarts,
		.init		= s3c6410_init,
		.name		= name_s3c6410,
	},
};

/* minimal IO mapping */

/* see notes on uart map in arch/arm/mach-s3c64xx/include/mach/debug-macro.S */
#define UART_OFFS (S3C_PA_UART & 0xfffff)

static struct map_desc s3c_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S3C64XX_PA_SYSCON),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_MEM,
		.pfn		= __phys_to_pfn(S3C64XX_PA_SROM),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)(S3C_VA_UART + UART_OFFS),
		.pfn		= __phys_to_pfn(S3C_PA_UART),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(S3C64XX_PA_VIC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(S3C64XX_PA_VIC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(S3C_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C64XX_VA_GPIO,
		.pfn		= __phys_to_pfn(S3C64XX_PA_GPIO),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C64XX_VA_MODEM,
		.pfn		= __phys_to_pfn(S3C64XX_PA_MODEM),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(S3C64XX_PA_WATCHDOG),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(S3C64XX_PA_USB_HSPHY),
		.length		= SZ_1K,
		.type		= MT_DEVICE,
	},
};

static struct bus_type s3c64xx_subsys = {
	.name		= "s3c64xx-core",
	.dev_name	= "s3c64xx-core",
};

static struct device s3c64xx_dev = {
	.bus	= &s3c64xx_subsys,
};

static struct samsung_pwm_variant s3c64xx_pwm_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= (1 << 7) | (1 << 6) | (1 << 5),
};

void __init samsung_set_timer_source(unsigned int event, unsigned int source)
{
	s3c64xx_pwm_variant.output_mask = BIT(SAMSUNG_PWM_NUM) - 1;
	s3c64xx_pwm_variant.output_mask &= ~(BIT(event) | BIT(source));
}

void __init samsung_timer_init(void)
{
	unsigned int timer_irqs[SAMSUNG_PWM_NUM] = {
		IRQ_TIMER0_VIC, IRQ_TIMER1_VIC, IRQ_TIMER2_VIC,
		IRQ_TIMER3_VIC, IRQ_TIMER4_VIC,
	};

	samsung_pwm_clocksource_init(S3C_VA_TIMER,
					timer_irqs, &s3c64xx_pwm_variant);
}

/* read cpu identification code */

void __init s3c64xx_init_io(struct map_desc *mach_desc, int size)
{
	/* initialise the io descriptors we need for initialisation */
	iotable_init(s3c_iodesc, ARRAY_SIZE(s3c_iodesc));
	iotable_init(mach_desc, size);

	/* detect cpu id */
	s3c64xx_init_cpu();

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));

	samsung_pwm_set_platdata(&s3c64xx_pwm_variant);
}

static __init int s3c64xx_dev_init(void)
{
	/* Not applicable when using DT. */
	if (of_have_populated_dt())
		return 0;

	subsys_system_register(&s3c64xx_subsys, NULL);
	return device_register(&s3c64xx_dev);
}
core_initcall(s3c64xx_dev_init);

/*
 * setup the sources the vic should advertise resume
 * for, even though it is not doing the wake
 * (set_irq_wake needs to be valid)
 */
#define IRQ_VIC0_RESUME (1 << (IRQ_RTC_TIC - IRQ_VIC0_BASE))
#define IRQ_VIC1_RESUME (1 << (IRQ_RTC_ALARM - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_PENDN - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC0 - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC1 - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC2 - IRQ_VIC1_BASE))

void __init s3c64xx_init_irq(u32 vic0_valid, u32 vic1_valid)
{
	/*
	 * FIXME: there is no better place to put this at the moment
	 * (s3c64xx_clk_init needs ioremap and must happen before init_time
	 * samsung_wdt_reset_init needs clocks)
	 */
	s3c64xx_clk_init(NULL, xtal_f, xusbxti_f, soc_is_s3c6400(), S3C_VA_SYS);
	samsung_wdt_reset_init(S3C_VA_WATCHDOG);

	printk(KERN_DEBUG "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	vic_init(VA_VIC0, IRQ_VIC0_BASE, vic0_valid, IRQ_VIC0_RESUME);
	vic_init(VA_VIC1, IRQ_VIC1_BASE, vic1_valid, IRQ_VIC1_RESUME);
}

#define eint_offset(irq)	((irq) - IRQ_EINT(0))
#define eint_irq_to_bit(irq)	((u32)(1 << eint_offset(irq)))

static inline void s3c_irq_eint_mask(struct irq_data *data)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask |= (u32)data->chip_data;
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static void s3c_irq_eint_unmask(struct irq_data *data)
{
	u32 mask;

	mask = __raw_readl(S3C64XX_EINT0MASK);
	mask &= ~((u32)data->chip_data);
	__raw_writel(mask, S3C64XX_EINT0MASK);
}

static inline void s3c_irq_eint_ack(struct irq_data *data)
{
	__raw_writel((u32)data->chip_data, S3C64XX_EINT0PEND);
}

static void s3c_irq_eint_maskack(struct irq_data *data)
{
	/* compiler should in-line these */
	s3c_irq_eint_mask(data);
	s3c_irq_eint_ack(data);
}

static int s3c_irq_eint_set_type(struct irq_data *data, unsigned int type)
{
	int offs = eint_offset(data->irq);
	int pin, pin_val;
	int shift;
	u32 ctrl, mask;
	u32 newvalue = 0;
	void __iomem *reg;

	if (offs > 27)
		return -EINVAL;

	if (offs <= 15)
		reg = S3C64XX_EINT0CON0;
	else
		reg = S3C64XX_EINT0CON1;

	switch (type) {
	case IRQ_TYPE_NONE:
		printk(KERN_WARNING "No edge setting!\n");
		break;

	case IRQ_TYPE_EDGE_RISING:
		newvalue = S3C2410_EXTINT_RISEEDGE;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		newvalue = S3C2410_EXTINT_FALLEDGE;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		newvalue = S3C2410_EXTINT_BOTHEDGE;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		newvalue = S3C2410_EXTINT_LOWLEV;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		newvalue = S3C2410_EXTINT_HILEV;
		break;

	default:
		printk(KERN_ERR "No such irq type %d", type);
		return -1;
	}

	if (offs <= 15)
		shift = (offs / 2) * 4;
	else
		shift = ((offs - 16) / 2) * 4;
	mask = 0x7 << shift;

	ctrl = __raw_readl(reg);
	ctrl &= ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, reg);

	/* set the GPIO pin appropriately */

	if (offs < 16) {
		pin = S3C64XX_GPN(offs);
		pin_val = S3C_GPIO_SFN(2);
	} else if (offs < 23) {
		pin = S3C64XX_GPL(offs + 8 - 16);
		pin_val = S3C_GPIO_SFN(3);
	} else {
		pin = S3C64XX_GPM(offs - 23);
		pin_val = S3C_GPIO_SFN(3);
	}

	s3c_gpio_cfgpin(pin, pin_val);

	return 0;
}

static struct irq_chip s3c_irq_eint = {
	.name		= "s3c-eint",
	.irq_mask	= s3c_irq_eint_mask,
	.irq_unmask	= s3c_irq_eint_unmask,
	.irq_mask_ack	= s3c_irq_eint_maskack,
	.irq_ack	= s3c_irq_eint_ack,
	.irq_set_type	= s3c_irq_eint_set_type,
	.irq_set_wake	= s3c_irqext_wake,
};

/* s3c_irq_demux_eint
 *
 * This function demuxes the IRQ from the group0 external interrupts,
 * from IRQ_EINT(0) to IRQ_EINT(27). It is designed to be inlined into
 * the specific handlers s3c_irq_demux_eintX_Y.
 */
static inline void s3c_irq_demux_eint(unsigned int start, unsigned int end)
{
	u32 status = __raw_readl(S3C64XX_EINT0PEND);
	u32 mask = __raw_readl(S3C64XX_EINT0MASK);
	unsigned int irq;

	status &= ~mask;
	status >>= start;
	status &= (1 << (end - start + 1)) - 1;

	for (irq = IRQ_EINT(start); irq <= IRQ_EINT(end); irq++) {
		if (status & 1)
			generic_handle_irq(irq);

		status >>= 1;
	}
}

static void s3c_irq_demux_eint0_3(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(0, 3);
}

static void s3c_irq_demux_eint4_11(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(4, 11);
}

static void s3c_irq_demux_eint12_19(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(12, 19);
}

static void s3c_irq_demux_eint20_27(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_eint(20, 27);
}

static int __init s3c64xx_init_irq_eint(void)
{
	int irq;

	/* On DT-enabled systems EINTs are handled by pinctrl-s3c64xx driver. */
	if (of_have_populated_dt())
		return -ENODEV;

	for (irq = IRQ_EINT(0); irq <= IRQ_EINT(27); irq++) {
		irq_set_chip_and_handler(irq, &s3c_irq_eint, handle_level_irq);
		irq_set_chip_data(irq, (void *)eint_irq_to_bit(irq));
		set_irq_flags(irq, IRQF_VALID);
	}

	irq_set_chained_handler(IRQ_EINT0_3, s3c_irq_demux_eint0_3);
	irq_set_chained_handler(IRQ_EINT4_11, s3c_irq_demux_eint4_11);
	irq_set_chained_handler(IRQ_EINT12_19, s3c_irq_demux_eint12_19);
	irq_set_chained_handler(IRQ_EINT20_27, s3c_irq_demux_eint20_27);

	return 0;
}
arch_initcall(s3c64xx_init_irq_eint);

void s3c64xx_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode != REBOOT_SOFT)
		samsung_wdt_reset();

	/* if all else fails, or mode was for soft, jump to 0 */
	soft_restart(0);
}
