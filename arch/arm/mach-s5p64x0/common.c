/*
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Codes for S5P64X0 machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <clocksource/samsung_pwm.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/reboot.h>

#include <asm/irq.h>
#include <asm/proc-fns.h>
#include <asm/system_misc.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/map.h>
#include <mach/hardware.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/sdhci.h>
#include <plat/adc-core.h>
#include <plat/fb-core.h>
#include <plat/spi-core.h>
#include <plat/gpio-cfg.h>
#include <plat/pwm-core.h>
#include <plat/regs-irqtype.h>
#include <plat/watchdog-reset.h>

#include "common.h"

static const char name_s5p6440[] = "S5P6440";
static const char name_s5p6450[] = "S5P6450";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= S5P6440_CPU_ID,
		.idmask		= S5P64XX_CPU_MASK,
		.map_io		= s5p6440_map_io,
		.init_clocks	= s5p6440_init_clocks,
		.init_uarts	= s5p6440_init_uarts,
		.init		= s5p64x0_init,
		.name		= name_s5p6440,
	}, {
		.idcode		= S5P6450_CPU_ID,
		.idmask		= S5P64XX_CPU_MASK,
		.map_io		= s5p6450_map_io,
		.init_clocks	= s5p6450_init_clocks,
		.init_uarts	= s5p6450_init_uarts,
		.init		= s5p64x0_init,
		.name		= name_s5p6450,
	},
};

/* Initial IO mappings */

static struct map_desc s5p64x0_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_CHIPID,
		.pfn		= __phys_to_pfn(S5P64X0_PA_CHIPID),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S5P64X0_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(S5P64X0_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(S5P64X0_PA_WDT),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5P64X0_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO,
		.pfn		= __phys_to_pfn(S5P64X0_PA_GPIO),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(S5P64X0_PA_VIC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(S5P64X0_PA_VIC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc s5p6440_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S5P6440_PA_UART(0)),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc s5p6450_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S5P6450_PA_UART(0)),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART + SZ_512K,
		.pfn		= __phys_to_pfn(S5P6450_PA_UART(5)),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void s5p64x0_idle(void)
{
	unsigned long val;

	val = __raw_readl(S5P64X0_PWR_CFG);
	val &= ~(0x3 << 5);
	val |= (0x1 << 5);
	__raw_writel(val, S5P64X0_PWR_CFG);

	cpu_do_idle();
}

static struct samsung_pwm_variant s5p64x0_pwm_variant = {
	.bits		= 32,
	.div_base	= 0,
	.has_tint_cstat	= true,
	.tclk_mask	= 0,
};

void __init samsung_set_timer_source(unsigned int event, unsigned int source)
{
	s5p64x0_pwm_variant.output_mask = BIT(SAMSUNG_PWM_NUM) - 1;
	s5p64x0_pwm_variant.output_mask &= ~(BIT(event) | BIT(source));
}

void __init samsung_timer_init(void)
{
	unsigned int timer_irqs[SAMSUNG_PWM_NUM] = {
		IRQ_TIMER0_VIC, IRQ_TIMER1_VIC, IRQ_TIMER2_VIC,
		IRQ_TIMER3_VIC, IRQ_TIMER4_VIC,
	};

	samsung_pwm_clocksource_init(S3C_VA_TIMER,
					timer_irqs, &s5p64x0_pwm_variant);
}

/*
 * s5p64x0_map_io
 *
 * register the standard CPU IO areas
 */

void __init s5p64x0_init_io(struct map_desc *mach_desc, int size)
{
	/* initialize the io descriptors we need for initialization */
	iotable_init(s5p64x0_iodesc, ARRAY_SIZE(s5p64x0_iodesc));
	if (mach_desc)
		iotable_init(mach_desc, size);

	/* detect cpu id and rev. */
	s5p_init_cpu(S5P64X0_SYS_ID);

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
	samsung_wdt_reset_init(S3C_VA_WATCHDOG);

	samsung_pwm_set_platdata(&s5p64x0_pwm_variant);
}

#ifdef CONFIG_CPU_S5P6440
void __init s5p6440_map_io(void)
{
	/* initialize any device information early */
	s3c_adc_setname("s3c64xx-adc");
	s3c_fb_setname("s5p64x0-fb");
	s3c64xx_spi_setname("s5p64x0-spi");

	s5p64x0_default_sdhci0();
	s5p64x0_default_sdhci1();
	s5p6440_default_sdhci2();

	iotable_init(s5p6440_iodesc, ARRAY_SIZE(s5p6440_iodesc));
}
#endif

#ifdef CONFIG_CPU_S5P6450
void __init s5p6450_map_io(void)
{
	/* initialize any device information early */
	s3c_adc_setname("s3c64xx-adc");
	s3c_fb_setname("s5p64x0-fb");
	s3c64xx_spi_setname("s5p64x0-spi");

	s5p64x0_default_sdhci0();
	s5p64x0_default_sdhci1();
	s5p6450_default_sdhci2();

	iotable_init(s5p6450_iodesc, ARRAY_SIZE(s5p6450_iodesc));
}
#endif

/*
 * s5p64x0_init_clocks
 *
 * register and setup the CPU clocks
 */
#ifdef CONFIG_CPU_S5P6440
void __init s5p6440_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6440_register_clocks();
	s5p6440_setup_clocks();
}
#endif

#ifdef CONFIG_CPU_S5P6450
void __init s5p6450_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6450_register_clocks();
	s5p6450_setup_clocks();
}
#endif

/*
 * s5p64x0_init_irq
 *
 * register the CPU interrupts
 */
#ifdef CONFIG_CPU_S5P6440
void __init s5p6440_init_irq(void)
{
	/* S5P6440 supports 2 VIC */
	u32 vic[2];

	/*
	 * VIC0 is missing IRQ_VIC0[3, 4, 8, 10, (12-22)]
	 * VIC1 is missing IRQ VIC1[1, 3, 4, 10, 11, 12, 14, 15, 22]
	 */
	vic[0] = 0xff800ae7;
	vic[1] = 0xffbf23e5;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}
#endif

#ifdef CONFIG_CPU_S5P6450
void __init s5p6450_init_irq(void)
{
	/* S5P6450 supports only 2 VIC */
	u32 vic[2];

	/*
	 * VIC0 is missing IRQ_VIC0[(13-15), (21-22)]
	 * VIC1 is missing IRQ VIC1[12, 14, 23]
	 */
	vic[0] = 0xff9f1fff;
	vic[1] = 0xff7fafff;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}
#endif

struct bus_type s5p64x0_subsys = {
	.name		= "s5p64x0-core",
	.dev_name	= "s5p64x0-core",
};

static struct device s5p64x0_dev = {
	.bus	= &s5p64x0_subsys,
};

static int __init s5p64x0_core_init(void)
{
	return subsys_system_register(&s5p64x0_subsys, NULL);
}
core_initcall(s5p64x0_core_init);

int __init s5p64x0_init(void)
{
	printk(KERN_INFO "S5P64X0(S5P6440/S5P6450): Initializing architecture\n");

	/* set idle function */
	arm_pm_idle = s5p64x0_idle;

	return device_register(&s5p64x0_dev);
}

/* uart registration process */
#ifdef CONFIG_CPU_S5P6440
void __init s5p6440_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	int uart;

	for (uart = 0; uart < no; uart++) {
		s5p_uart_resources[uart].resources->start = S5P6440_PA_UART(uart);
		s5p_uart_resources[uart].resources->end = S5P6440_PA_UART(uart) + S5P_SZ_UART;
	}

	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}
#endif

#ifdef CONFIG_CPU_S5P6450
void __init s5p6450_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}
#endif

#define eint_offset(irq)	((irq) - IRQ_EINT(0))

static int s5p64x0_irq_eint_set_type(struct irq_data *data, unsigned int type)
{
	int offs = eint_offset(data->irq);
	int shift;
	u32 ctrl, mask;
	u32 newvalue = 0;

	if (offs > 15)
		return -EINVAL;

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
		return -EINVAL;
	}

	shift = (offs / 2) * 4;
	mask = 0x7 << shift;

	ctrl = __raw_readl(S5P64X0_EINT0CON0) & ~mask;
	ctrl |= newvalue << shift;
	__raw_writel(ctrl, S5P64X0_EINT0CON0);

	/* Configure the GPIO pin for 6450 or 6440 based on CPU ID */
	if (soc_is_s5p6450())
		s3c_gpio_cfgpin(S5P6450_GPN(offs), S3C_GPIO_SFN(2));
	else
		s3c_gpio_cfgpin(S5P6440_GPN(offs), S3C_GPIO_SFN(2));

	return 0;
}

/*
 * s5p64x0_irq_demux_eint
 *
 * This function demuxes the IRQ from the group0 external interrupts,
 * from IRQ_EINT(0) to IRQ_EINT(15). It is designed to be inlined into
 * the specific handlers s5p64x0_irq_demux_eintX_Y.
 */
static inline void s5p64x0_irq_demux_eint(unsigned int start, unsigned int end)
{
	u32 status = __raw_readl(S5P64X0_EINT0PEND);
	u32 mask = __raw_readl(S5P64X0_EINT0MASK);
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

static void s5p64x0_irq_demux_eint0_3(unsigned int irq, struct irq_desc *desc)
{
	s5p64x0_irq_demux_eint(0, 3);
}

static void s5p64x0_irq_demux_eint4_11(unsigned int irq, struct irq_desc *desc)
{
	s5p64x0_irq_demux_eint(4, 11);
}

static void s5p64x0_irq_demux_eint12_15(unsigned int irq,
					struct irq_desc *desc)
{
	s5p64x0_irq_demux_eint(12, 15);
}

static int s5p64x0_alloc_gc(void)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("s5p64x0-eint", 1, S5P_IRQ_EINT_BASE,
				    S5P_VA_GPIO, handle_level_irq);
	if (!gc) {
		printk(KERN_ERR "%s: irq_alloc_generic_chip for group 0"
			"external interrupts failed\n", __func__);
		return -EINVAL;
	}

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->chip.irq_set_type = s5p64x0_irq_eint_set_type;
	ct->chip.irq_set_wake = s3c_irqext_wake;
	ct->regs.ack = EINT0PEND_OFFSET;
	ct->regs.mask = EINT0MASK_OFFSET;
	irq_setup_generic_chip(gc, IRQ_MSK(16), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
	return 0;
}

static int __init s5p64x0_init_irq_eint(void)
{
	int ret = s5p64x0_alloc_gc();
	irq_set_chained_handler(IRQ_EINT0_3, s5p64x0_irq_demux_eint0_3);
	irq_set_chained_handler(IRQ_EINT4_11, s5p64x0_irq_demux_eint4_11);
	irq_set_chained_handler(IRQ_EINT12_15, s5p64x0_irq_demux_eint12_15);

	return ret;
}
arch_initcall(s5p64x0_init_irq_eint);

void s5p64x0_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode != REBOOT_SOFT)
		samsung_wdt_reset();

	soft_restart(0);
}
