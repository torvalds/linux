/*
 * arch/arm/mach-at91rm9200/devices.c
 *
 *  Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
 *  Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/config.h>
#include <linux/platform_device.h>

#include <asm/arch/board.h>
#include <asm/arch/pio.h>


/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = 0xffffffffUL;
static struct at91_usbh_data usbh_data;

static struct resource at91rm9200_usbh_resource[] = {
	[0] = {
		.start	= AT91_UHP_BASE,
		.end	= AT91_UHP_BASE + SZ_1M -1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_UHP,
		.end	= AT91_ID_UHP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91rm9200_usbh_device = {
	.name		= "at91rm9200-ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &usbh_data,
	},
	.resource	= at91rm9200_usbh_resource,
	.num_resources	= ARRAY_SIZE(at91rm9200_usbh_resource),
};

void __init at91_add_device_usbh(struct at91_usbh_data *data)
{
	if (!data)
		return;

	usbh_data = *data;
	platform_device_register(&at91rm9200_usbh_device);
}
#else
void __init at91_add_device_usbh(struct at91_usbh_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  USB Device (Gadget)
 * -------------------------------------------------------------------- */

#ifdef CONFIG_USB_GADGET_AT91
static struct at91_udc_data udc_data;

static struct resource at91_udc_resources[] = {
	{
		.start	= AT91_BASE_UDP,
		.end	= AT91_BASE_UDP + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91rm9200_udc_device = {
	.name		= "at91_udc",
	.id		= -1,
	.dev		= {
				.platform_data		= &udc_data,
	},
	.resource	= at91_udc_resources,
	.num_resources	= ARRAY_SIZE(at91_udc_resources),
};

void __init at91_add_device_udc(struct at91_udc_data *data)
{
	if (!data)
		return;

	if (data->vbus_pin) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
	}
	if (data->pullup_pin) {
		at91_set_gpio_output(data->pullup_pin, 0);
		at91_set_multi_drive(data->pullup_pin, 1);
	}

	udc_data = *data;
	platform_device_register(&at91rm9200_udc_device);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ARM_AT91_ETHER) || defined(CONFIG_ARM_AT91_ETHER_MODULE)
static u64 eth_dmamask = 0xffffffffUL;
static struct at91_eth_data eth_data;

static struct platform_device at91rm9200_eth_device = {
	.name		= "at91_ether",
	.id		= -1,
	.dev		= {
				.dma_mask		= &eth_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &eth_data,
	},
	.num_resources	= 0,
};

void __init at91_add_device_eth(struct at91_eth_data *data)
{
	if (!data)
		return;

	if (data->phy_irq_pin) {
		at91_set_gpio_input(data->phy_irq_pin, 0);
		at91_set_deglitch(data->phy_irq_pin, 1);
	}

	/* Pins used for MII and RMII */
	at91_set_A_periph(AT91_PIN_PA16, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PA15, 0);	/* EMDC */
	at91_set_A_periph(AT91_PIN_PA14, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PA13, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PA12, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PA11, 0);	/* ECRS_ECRSDV */
	at91_set_A_periph(AT91_PIN_PA10, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PA9, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PA8, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PA7, 0);	/* ETXCK_EREFCK */

	if (!data->is_rmii) {
		at91_set_B_periph(AT91_PIN_PB19, 0);	/* ERXCK */
		at91_set_B_periph(AT91_PIN_PB18, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PB17, 0);	/* ERXDV */
		at91_set_B_periph(AT91_PIN_PB16, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PB15, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PB14, 0);	/* ETXER */
		at91_set_B_periph(AT91_PIN_PB13, 0);	/* ETX3 */
		at91_set_B_periph(AT91_PIN_PB12, 0);	/* ETX2 */
	}

	eth_data = *data;
	platform_device_register(&at91rm9200_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Compact Flash / PCMCIA
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91_CF) || defined(CONFIG_AT91_CF_MODULE)
static struct at91_cf_data cf_data;

static struct platform_device at91rm9200_cf_device = {
	.name		= "at91_cf",
	.id		= -1,
	.dev		= {
				.platform_data		= &cf_data,
	},
	.num_resources	= 0,
};

void __init at91_add_device_cf(struct at91_cf_data *data)
{
	if (!data)
		return;

	/* input/irq */
	if (data->irq_pin) {
		at91_set_gpio_input(data->irq_pin, 1);
		at91_set_deglitch(data->irq_pin, 1);
	}
	at91_set_gpio_input(data->det_pin, 1);
	at91_set_deglitch(data->det_pin, 1);

	/* outputs, initially off */
	if (data->vcc_pin)
		at91_set_gpio_output(data->vcc_pin, 0);
	at91_set_gpio_output(data->rst_pin, 0);

	cf_data = *data;
	platform_device_register(&at91rm9200_cf_device);
}
#else
void __init at91_add_device_cf(struct at91_cf_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91RM9200) || defined(CONFIG_MMC_AT91RM9200_MODULE)
static u64 mmc_dmamask = 0xffffffffUL;
static struct at91_mmc_data mmc_data;

static struct resource at91_mmc_resources[] = {
	{
		.start	= AT91_BASE_MCI,
		.end	= AT91_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91rm9200_mmc_device = {
	.name		= "at91rm9200_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= 0xffffffff,
				.platform_data		= &mmc_data,
	},
	.resource	= at91_mmc_resources,
	.num_resources	= ARRAY_SIZE(at91_mmc_resources),
};

void __init at91_add_device_mmc(struct at91_mmc_data *data)
{
	if (!data)
		return;

	/* input/irq */
	if (data->det_pin) {
		at91_set_gpio_input(data->det_pin, 1);
		at91_set_deglitch(data->det_pin, 1);
	}
	if (data->wp_pin)
		at91_set_gpio_input(data->wp_pin, 1);

	/* CLK */
	at91_set_A_periph(AT91_PIN_PA27, 0);

	if (data->is_b) {
		/* CMD */
		at91_set_B_periph(AT91_PIN_PA8, 0);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_B_periph(AT91_PIN_PA9, 0);
		if (data->wire4) {
			at91_set_B_periph(AT91_PIN_PA10, 0);
			at91_set_B_periph(AT91_PIN_PA11, 0);
			at91_set_B_periph(AT91_PIN_PA12, 0);
		}
	} else {
		/* CMD */
		at91_set_A_periph(AT91_PIN_PA28, 0);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA29, 0);
		if (data->wire4) {
			at91_set_B_periph(AT91_PIN_PB3, 0);
			at91_set_B_periph(AT91_PIN_PB4, 0);
			at91_set_B_periph(AT91_PIN_PB5, 0);
		}
	}

	mmc_data = *data;
	platform_device_register(&at91rm9200_mmc_device);
}
#else
void __init at91_add_device_mmc(struct at91_mmc_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  LEDs
 * -------------------------------------------------------------------- */

#if defined(CONFIG_LEDS)
u8 at91_leds_cpu;
u8 at91_leds_timer;

void __init at91_init_leds(u8 cpu_led, u8 timer_led)
{
	at91_leds_cpu   = cpu_led;
	at91_leds_timer = timer_led;
}

#else
void __init at91_init_leds(u8 cpu_led, u8 timer_led) {}
#endif


/* -------------------------------------------------------------------- */
