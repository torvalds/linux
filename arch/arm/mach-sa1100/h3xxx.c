/*
 * Support for Compaq iPAQ H3100 and H3600 handheld computers (common code)
 *
 * Copyright (c) 2000,1 Compaq Computer Corporation. (Author: Jamey Hicks)
 * Copyright (c) 2009 Dmitry Artamonow <mad_soft@inbox.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mfd/htc-egpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>

#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <mach/h3xxx.h>
#include <mach/irqs.h>

#include "generic.h"

/*
 * H3xxx flash support
 */
static struct mtd_partition h3xxx_partitions[] = {
	{
		.name		= "H3XXX boot firmware",
		.size		= 0x00040000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "H3XXX rootfs",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0x00040000,
	}
};

static void h3xxx_set_vpp(int vpp)
{
	gpio_set_value(H3XXX_EGPIO_VPP_ON, vpp);
}

static int h3xxx_flash_init(void)
{
	int err = gpio_request(H3XXX_EGPIO_VPP_ON, "Flash Vpp");
	if (err) {
		pr_err("%s: can't request H3XXX_EGPIO_VPP_ON\n", __func__);
		return err;
	}

	err = gpio_direction_output(H3XXX_EGPIO_VPP_ON, 0);
	if (err)
		gpio_free(H3XXX_EGPIO_VPP_ON);

	return err;
}

static void h3xxx_flash_exit(void)
{
	gpio_free(H3XXX_EGPIO_VPP_ON);
}

static struct flash_platform_data h3xxx_flash_data = {
	.map_name	= "cfi_probe",
	.set_vpp	= h3xxx_set_vpp,
	.init		= h3xxx_flash_init,
	.exit		= h3xxx_flash_exit,
	.parts		= h3xxx_partitions,
	.nr_parts	= ARRAY_SIZE(h3xxx_partitions),
};

static struct resource h3xxx_flash_resource =
	DEFINE_RES_MEM(SA1100_CS0_PHYS, SZ_32M);


/*
 * H3xxx uart support
 */
static struct gpio h3xxx_uart_gpio[] = {
	{ H3XXX_GPIO_COM_DCD,	GPIOF_IN,		"COM DCD" },
	{ H3XXX_GPIO_COM_CTS,	GPIOF_IN,		"COM CTS" },
	{ H3XXX_GPIO_COM_RTS,	GPIOF_OUT_INIT_LOW,	"COM RTS" },
};

static bool h3xxx_uart_request_gpios(void)
{
	static bool h3xxx_uart_gpio_ok;
	int rc;

	if (h3xxx_uart_gpio_ok)
		return true;

	rc = gpio_request_array(h3xxx_uart_gpio, ARRAY_SIZE(h3xxx_uart_gpio));
	if (rc)
		pr_err("h3xxx_uart_request_gpios: error %d\n", rc);
	else
		h3xxx_uart_gpio_ok = true;

	return h3xxx_uart_gpio_ok;
}

static void h3xxx_uart_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser3UTCR0) {
		if (!h3xxx_uart_request_gpios())
			return;
		gpio_set_value(H3XXX_GPIO_COM_RTS, !(mctrl & TIOCM_RTS));
	}
}

static u_int h3xxx_uart_get_mctrl(struct uart_port *port)
{
	u_int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	if (port->mapbase == _Ser3UTCR0) {
		if (!h3xxx_uart_request_gpios())
			return ret;
		/*
		 * DCD and CTS bits are inverted in GPLR by RS232 transceiver
		 */
		if (gpio_get_value(H3XXX_GPIO_COM_DCD))
			ret &= ~TIOCM_CD;
		if (gpio_get_value(H3XXX_GPIO_COM_CTS))
			ret &= ~TIOCM_CTS;
	}

	return ret;
}

static void h3xxx_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == _Ser3UTCR0) {
		if (!gpio_request(H3XXX_EGPIO_RS232_ON, "RS232 transceiver")) {
			gpio_direction_output(H3XXX_EGPIO_RS232_ON, !state);
			gpio_free(H3XXX_EGPIO_RS232_ON);
		} else {
			pr_err("%s: can't request H3XXX_EGPIO_RS232_ON\n",
				__func__);
		}
	}
}

/*
 * Enable/Disable wake up events for this serial port.
 * Obviously, we only support this on the normal COM port.
 */
static int h3xxx_uart_set_wake(struct uart_port *port, u_int enable)
{
	int err = -EINVAL;

	if (port->mapbase == _Ser3UTCR0) {
		if (enable)
			PWER |= PWER_GPIO23 | PWER_GPIO25; /* DCD and CTS */
		else
			PWER &= ~(PWER_GPIO23 | PWER_GPIO25); /* DCD and CTS */
		err = 0;
	}
	return err;
}

static struct sa1100_port_fns h3xxx_port_fns __initdata = {
	.set_mctrl	= h3xxx_uart_set_mctrl,
	.get_mctrl	= h3xxx_uart_get_mctrl,
	.pm		= h3xxx_uart_pm,
	.set_wake	= h3xxx_uart_set_wake,
};

/*
 * EGPIO
 */

static struct resource egpio_resources[] = {
	[0] = DEFINE_RES_MEM(H3600_EGPIO_PHYS, 0x4),
};

static struct htc_egpio_chip egpio_chips[] = {
	[0] = {
		.reg_start	= 0,
		.gpio_base	= H3XXX_EGPIO_BASE,
		.num_gpios	= 16,
		.direction	= HTC_EGPIO_OUTPUT,
		.initial_values	= 0x0080, /* H3XXX_EGPIO_RS232_ON */
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width	= 16,
	.bus_width	= 16,
	.chip		= egpio_chips,
	.num_chips	= ARRAY_SIZE(egpio_chips),
};

static struct platform_device h3xxx_egpio = {
	.name		= "htc-egpio",
	.id		= -1,
	.resource	= egpio_resources,
	.num_resources	= ARRAY_SIZE(egpio_resources),
	.dev		= {
		.platform_data = &egpio_info,
	},
};

/*
 * GPIO keys
 */

static struct gpio_keys_button h3xxx_button_table[] = {
	{
		.code		= KEY_POWER,
		.gpio		= H3XXX_GPIO_PWR_BUTTON,
		.desc		= "Power Button",
		.active_low	= 1,
		.type		= EV_KEY,
		.wakeup		= 1,
	}, {
		.code		= KEY_ENTER,
		.gpio		= H3XXX_GPIO_ACTION_BUTTON,
		.active_low	= 1,
		.desc		= "Action button",
		.type		= EV_KEY,
		.wakeup		= 0,
	},
};

static struct gpio_keys_platform_data h3xxx_keys_data = {
	.buttons  = h3xxx_button_table,
	.nbuttons = ARRAY_SIZE(h3xxx_button_table),
};

static struct platform_device h3xxx_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &h3xxx_keys_data,
	},
};

static struct resource h3xxx_micro_resources[] = {
	DEFINE_RES_MEM(0x80010000, SZ_4K),
	DEFINE_RES_MEM(0x80020000, SZ_4K),
	DEFINE_RES_IRQ(IRQ_Ser1UART),
};

struct platform_device h3xxx_micro_asic = {
	.name = "ipaq-h3xxx-micro",
	.id = -1,
	.resource = h3xxx_micro_resources,
	.num_resources = ARRAY_SIZE(h3xxx_micro_resources),
};

static struct platform_device *h3xxx_devices[] = {
	&h3xxx_egpio,
	&h3xxx_keys,
	&h3xxx_micro_asic,
};

void __init h3xxx_mach_init(void)
{
	sa1100_register_uart_fns(&h3xxx_port_fns);
	sa11x0_register_mtd(&h3xxx_flash_data, &h3xxx_flash_resource, 1);
	platform_add_devices(h3xxx_devices, ARRAY_SIZE(h3xxx_devices));
}

static struct map_desc h3600_io_desc[] __initdata = {
	{	/* static memory bank 2  CS#2 */
		.virtual	=  H3600_BANK_2_VIRT,
		.pfn		= __phys_to_pfn(SA1100_CS2_PHYS),
		.length		= 0x02800000,
		.type		= MT_DEVICE
	}, {	/* static memory bank 4  CS#4 */
		.virtual	=  H3600_BANK_4_VIRT,
		.pfn		= __phys_to_pfn(SA1100_CS4_PHYS),
		.length		= 0x00800000,
		.type		= MT_DEVICE
	}, {	/* EGPIO 0		CS#5 */
		.virtual	=  H3600_EGPIO_VIRT,
		.pfn		= __phys_to_pfn(H3600_EGPIO_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

/*
 * Common map_io initialization
 */

void __init h3xxx_map_io(void)
{
	sa1100_map_io();
	iotable_init(h3600_io_desc, ARRAY_SIZE(h3600_io_desc));

	sa1100_register_uart(0, 3); /* Common serial port */
//	sa1100_register_uart(1, 1); /* Microcontroller on 3100/3600 */

	/* Ensure those pins are outputs and driving low  */
	PPDR |= PPC_TXD4 | PPC_SCLK | PPC_SFRM;
	PPSR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);

	/* Configure suspend conditions */
	PGSR = 0;
	PCFR = PCFR_OPDE;
	PSDR = 0;

	GPCR = 0x0fffffff;	/* All outputs are set low by default */
	GPDR = 0;		/* Configure all GPIOs as input */
}

