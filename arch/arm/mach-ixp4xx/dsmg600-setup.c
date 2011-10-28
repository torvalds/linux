/*
 * DSM-G600 board-setup
 *
 * Copyright (C) 2008 Rod Whitby <rod@whitby.id.au>
 * Copyright (C) 2006 Tower Technologies
 *
 * based on ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 * based on nslu2-power.c:
 *	Copyright (C) 2005 Tower Technologies
 * based on nslu2-io.c:
 *	Copyright (C) 2004 Karen Spearel
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Author: Michael Westerhof <mwester@dls.net>
 * Author: Rod Whitby <rod@whitby.id.au>
 * Maintainers: http://www.nslu2-linux.org/
 */

#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/time.h>
#include <asm/gpio.h>

#define DSMG600_SDA_PIN		5
#define DSMG600_SCL_PIN		4

/* DSM-G600 Timer Setting */
#define DSMG600_FREQ		66000000

/* Buttons */
#define DSMG600_PB_GPIO		15	/* power button */
#define DSMG600_RB_GPIO		3	/* reset button */

/* Power control */
#define DSMG600_PO_GPIO		2	/* power off */

/* LEDs */
#define DSMG600_LED_PWR_GPIO	0
#define DSMG600_LED_WLAN_GPIO	14

static struct flash_platform_data dsmg600_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 2,
};

static struct resource dsmg600_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device dsmg600_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev.platform_data	= &dsmg600_flash_data,
	.num_resources		= 1,
	.resource		= &dsmg600_flash_resource,
};

static struct i2c_gpio_platform_data dsmg600_i2c_gpio_data = {
	.sda_pin		= DSMG600_SDA_PIN,
	.scl_pin		= DSMG600_SCL_PIN,
};

static struct platform_device dsmg600_i2c_gpio = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev	 = {
		.platform_data	= &dsmg600_i2c_gpio_data,
	},
};

static struct i2c_board_info __initdata dsmg600_i2c_board_info [] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

static struct gpio_led dsmg600_led_pins[] = {
	{
		.name		= "dsmg600:green:power",
		.gpio		= DSMG600_LED_PWR_GPIO,
	},
	{
		.name		= "dsmg600:green:wlan",
		.gpio		= DSMG600_LED_WLAN_GPIO,
		.active_low	= true,
	},
};

static struct gpio_led_platform_data dsmg600_led_data = {
	.num_leds		= ARRAY_SIZE(dsmg600_led_pins),
	.leds			= dsmg600_led_pins,
};

static struct platform_device dsmg600_leds = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &dsmg600_led_data,
};

static struct resource dsmg600_uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct plat_serial8250_port dsmg600_uart_data[] = {
	{
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART1_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ }
};

static struct platform_device dsmg600_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= dsmg600_uart_data,
	.num_resources		= ARRAY_SIZE(dsmg600_uart_resources),
	.resource		= dsmg600_uart_resources,
};

static struct platform_device *dsmg600_devices[] __initdata = {
	&dsmg600_i2c_gpio,
	&dsmg600_flash,
	&dsmg600_leds,
};

static void dsmg600_power_off(void)
{
	/* enable the pwr cntl gpio */
	gpio_line_config(DSMG600_PO_GPIO, IXP4XX_GPIO_OUT);

	/* poweroff */
	gpio_line_set(DSMG600_PO_GPIO, IXP4XX_GPIO_HIGH);
}

/* This is used to make sure the power-button pusher is serious.  The button
 * must be held until the value of this counter reaches zero.
 */
static int power_button_countdown;

/* Must hold the button down for at least this many counts to be processed */
#define PBUTTON_HOLDDOWN_COUNT 4 /* 2 secs */

static void dsmg600_power_handler(unsigned long data);
static DEFINE_TIMER(dsmg600_power_timer, dsmg600_power_handler, 0, 0);

static void dsmg600_power_handler(unsigned long data)
{
	/* This routine is called twice per second to check the
	 * state of the power button.
	 */

	if (gpio_get_value(DSMG600_PB_GPIO)) {

		/* IO Pin is 1 (button pushed) */
		if (power_button_countdown > 0)
			power_button_countdown--;

	} else {

		/* Done on button release, to allow for auto-power-on mods. */
		if (power_button_countdown == 0) {
			/* Signal init to do the ctrlaltdel action,
			 * this will bypass init if it hasn't started
			 * and do a kernel_restart.
			 */
			ctrl_alt_del();

			/* Change the state of the power LED to "blink" */
			gpio_line_set(DSMG600_LED_PWR_GPIO, IXP4XX_GPIO_LOW);
		} else {
			power_button_countdown = PBUTTON_HOLDDOWN_COUNT;
		}
	}

	mod_timer(&dsmg600_power_timer, jiffies + msecs_to_jiffies(500));
}

static irqreturn_t dsmg600_reset_handler(int irq, void *dev_id)
{
	/* This is the paper-clip reset, it shuts the machine down directly. */
	machine_power_off();

	return IRQ_HANDLED;
}

static void __init dsmg600_timer_init(void)
{
    /* The xtal on this machine is non-standard. */
    ixp4xx_timer_freq = DSMG600_FREQ;

    /* Call standard timer_init function. */
    ixp4xx_timer_init();
}

static struct sys_timer dsmg600_timer = {
    .init   = dsmg600_timer_init,
};

static void __init dsmg600_init(void)
{
	ixp4xx_sys_init();

	/* Make sure that GPIO14 and GPIO15 are not used as clocks */
	*IXP4XX_GPIO_GPCLKR = 0;

	dsmg600_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	dsmg600_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	i2c_register_board_info(0, dsmg600_i2c_board_info,
				ARRAY_SIZE(dsmg600_i2c_board_info));

	/* The UART is required on the DSM-G600 (Redboot cannot use the
	 * NIC) -- do it here so that it does *not* get removed if
	 * platform_add_devices fails!
         */
        (void)platform_device_register(&dsmg600_uart);

	platform_add_devices(dsmg600_devices, ARRAY_SIZE(dsmg600_devices));

	pm_power_off = dsmg600_power_off;

	if (request_irq(gpio_to_irq(DSMG600_RB_GPIO), &dsmg600_reset_handler,
		IRQF_DISABLED | IRQF_TRIGGER_LOW,
		"DSM-G600 reset button", NULL) < 0) {

		printk(KERN_DEBUG "Reset Button IRQ %d not available\n",
			gpio_to_irq(DSMG600_RB_GPIO));
	}

	/* The power button on the D-Link DSM-G600 is on GPIO 15, but
	 * it cannot handle interrupts on that GPIO line.  So we'll
	 * have to poll it with a kernel timer.
	 */

	/* Make sure that the power button GPIO is set up as an input */
	gpio_line_config(DSMG600_PB_GPIO, IXP4XX_GPIO_IN);

	/* Set the initial value for the power button IRQ handler */
	power_button_countdown = PBUTTON_HOLDDOWN_COUNT;

	mod_timer(&dsmg600_power_timer, jiffies + msecs_to_jiffies(500));
}

MACHINE_START(DSMG600, "D-Link DSM-G600 RevA")
	/* Maintainer: www.nslu2-linux.org */
	.boot_params	= 0x00000100,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &dsmg600_timer,
	.init_machine	= dsmg600_init,
MACHINE_END
