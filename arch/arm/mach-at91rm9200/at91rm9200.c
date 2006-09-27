/*
 * arch/arm/mach-at91rm9200/at91rm9200.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/hardware.h>
#include "generic.h"

static struct map_desc at91rm9200_io_desc[] __initdata = {
	{
		.virtual	= AT91_VA_BASE_SYS,
		.pfn		= __phys_to_pfn(AT91_BASE_SYS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SPI,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_SPI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC2,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_SSC2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC1,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_SSC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_SSC0,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_SSC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US3,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_US3),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US2,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_US2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US1,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_US1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_US0,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_US0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_EMAC,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_EMAC),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TWI,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_TWI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_MCI,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_MCI),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_UDP,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_UDP),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TCB1,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_TCB1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_VA_BASE_TCB0,
		.pfn		= __phys_to_pfn(AT91RM9200_BASE_TCB0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= AT91_SRAM_VIRT_BASE,
		.pfn		= __phys_to_pfn(AT91RM9200_SRAM_BASE),
		.length		= AT91RM9200_SRAM_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init at91rm9200_map_io(void)
{
	iotable_init(at91rm9200_io_desc, ARRAY_SIZE(at91rm9200_io_desc));
}

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91rm9200_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller (FIQ) */
	7,	/* System Peripherals */
	0,	/* Parallel IO Controller A */
	0,	/* Parallel IO Controller B */
	0,	/* Parallel IO Controller C */
	0,	/* Parallel IO Controller D */
	6,	/* USART 0 */
	6,	/* USART 1 */
	6,	/* USART 2 */
	6,	/* USART 3 */
	0,	/* Multimedia Card Interface */
	4,	/* USB Device Port */
	0,	/* Two-Wire Interface */
	6,	/* Serial Peripheral Interface */
	5,	/* Serial Synchronous Controller 0 */
	5,	/* Serial Synchronous Controller 1 */
	5,	/* Serial Synchronous Controller 2 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	0,	/* Timer Counter 3 */
	0,	/* Timer Counter 4 */
	0,	/* Timer Counter 5 */
	3,	/* USB Host port */
	3,	/* Ethernet MAC */
	0,	/* Advanced Interrupt Controller (IRQ0) */
	0,	/* Advanced Interrupt Controller (IRQ1) */
	0,	/* Advanced Interrupt Controller (IRQ2) */
	0,	/* Advanced Interrupt Controller (IRQ3) */
	0,	/* Advanced Interrupt Controller (IRQ4) */
	0,	/* Advanced Interrupt Controller (IRQ5) */
	0	/* Advanced Interrupt Controller (IRQ6) */
};

void __init at91rm9200_init_irq(unsigned int priority[NR_AIC_IRQS])
{
	if (!priority)
		priority = at91rm9200_default_irq_priority;

	at91_aic_init(priority);
}
