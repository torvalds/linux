/*
 * arch/arm/mach-ixp4xx/nas100d-setup.c
 *
 * NAS 100d board-setup
 *
 * Copyright (C) 2008 Rod Whitby <rod@whitby.id.au>
 *
 * based on ixdp425-setup.c:
 *      Copyright (C) 2003-2004 MontaVista Software, Inc.
 * based on nas100d-power.c:
 *	Copyright (C) 2005 Tower Technologies
 * based on nas100d-io.c
 *	Copyright (C) 2004 Karen Spearel
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Author: Rod Whitby <rod@whitby.id.au>
 * Maintainers: http://www.nslu2-linux.org/
 *
 */

#include <linux/if_ether.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/gpio.h>

#define NAS100D_SDA_PIN		5
#define NAS100D_SCL_PIN		6

/* Buttons */
#define NAS100D_PB_GPIO         14   /* power button */
#define NAS100D_RB_GPIO         4    /* reset button */

/* Power control */
#define NAS100D_PO_GPIO         12   /* power off */

/* LEDs */
#define NAS100D_LED_WLAN_GPIO	0
#define NAS100D_LED_DISK_GPIO	3
#define NAS100D_LED_PWR_GPIO	15

static struct flash_platform_data nas100d_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 2,
};

static struct resource nas100d_flash_resource = {
	.flags			= IORESOURCE_MEM,
};

static struct platform_device nas100d_flash = {
	.name			= "IXP4XX-Flash",
	.id			= 0,
	.dev.platform_data	= &nas100d_flash_data,
	.num_resources		= 1,
	.resource		= &nas100d_flash_resource,
};

static struct i2c_board_info __initdata nas100d_i2c_board_info [] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

static struct gpio_led nas100d_led_pins[] = {
	{
		.name		= "nas100d:green:wlan",
		.gpio		= NAS100D_LED_WLAN_GPIO,
		.active_low	= true,
	},
	{
		.name		= "nas100d:blue:power",  /* (off=flashing) */
		.gpio		= NAS100D_LED_PWR_GPIO,
		.active_low	= true,
	},
	{
		.name		= "nas100d:yellow:disk",
		.gpio		= NAS100D_LED_DISK_GPIO,
		.active_low	= true,
	},
};

static struct gpio_led_platform_data nas100d_led_data = {
	.num_leds		= ARRAY_SIZE(nas100d_led_pins),
	.leds			= nas100d_led_pins,
};

static struct platform_device nas100d_leds = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &nas100d_led_data,
};

static struct i2c_gpio_platform_data nas100d_i2c_gpio_data = {
	.sda_pin		= NAS100D_SDA_PIN,
	.scl_pin		= NAS100D_SCL_PIN,
};

static struct platform_device nas100d_i2c_gpio = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev	 = {
		.platform_data	= &nas100d_i2c_gpio_data,
	},
};

static struct resource nas100d_uart_resources[] = {
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

static struct plat_serial8250_port nas100d_uart_data[] = {
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

static struct platform_device nas100d_uart = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= nas100d_uart_data,
	.num_resources		= 2,
	.resource		= nas100d_uart_resources,
};

/* Built-in 10/100 Ethernet MAC interfaces */
static struct eth_plat_info nas100d_plat_eth[] = {
	{
		.phy		= 0,
		.rxq		= 3,
		.txreadyq	= 20,
	}
};

static struct platform_device nas100d_eth[] = {
	{
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEB,
		.dev.platform_data	= nas100d_plat_eth,
	}
};

static struct platform_device *nas100d_devices[] __initdata = {
	&nas100d_i2c_gpio,
	&nas100d_flash,
	&nas100d_leds,
	&nas100d_eth[0],
};

static void nas100d_power_off(void)
{
	/* This causes the box to drop the power and go dead. */

	/* enable the pwr cntl gpio */
	gpio_line_config(NAS100D_PO_GPIO, IXP4XX_GPIO_OUT);

	/* do the deed */
	gpio_line_set(NAS100D_PO_GPIO, IXP4XX_GPIO_HIGH);
}

/* This is used to make sure the power-button pusher is serious.  The button
 * must be held until the value of this counter reaches zero.
 */
static int power_button_countdown;

/* Must hold the button down for at least this many counts to be processed */
#define PBUTTON_HOLDDOWN_COUNT 4 /* 2 secs */

static void nas100d_power_handler(unsigned long data);
static DEFINE_TIMER(nas100d_power_timer, nas100d_power_handler, 0, 0);

static void nas100d_power_handler(unsigned long data)
{
	/* This routine is called twice per second to check the
	 * state of the power button.
	 */

	if (gpio_get_value(NAS100D_PB_GPIO)) {

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
			gpio_line_set(NAS100D_LED_PWR_GPIO, IXP4XX_GPIO_LOW);
		} else {
			power_button_countdown = PBUTTON_HOLDDOWN_COUNT;
		}
	}

	mod_timer(&nas100d_power_timer, jiffies + msecs_to_jiffies(500));
}

static irqreturn_t nas100d_reset_handler(int irq, void *dev_id)
{
	/* This is the paper-clip reset, it shuts the machine down directly. */
	machine_power_off();

	return IRQ_HANDLED;
}

static void __init nas100d_init(void)
{
	uint8_t __iomem *f;
	int i;

	ixp4xx_sys_init();

	/* gpio 14 and 15 are _not_ clocks */
	*IXP4XX_GPIO_GPCLKR = 0;

	nas100d_flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	nas100d_flash_resource.end =
		IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	i2c_register_board_info(0, nas100d_i2c_board_info,
				ARRAY_SIZE(nas100d_i2c_board_info));

	/*
	 * This is only useful on a modified machine, but it is valuable
	 * to have it first in order to see debug messages, and so that
	 * it does *not* get removed if platform_add_devices fails!
	 */
	(void)platform_device_register(&nas100d_uart);

	platform_add_devices(nas100d_devices, ARRAY_SIZE(nas100d_devices));

	pm_power_off = nas100d_power_off;

	if (request_irq(gpio_to_irq(NAS100D_RB_GPIO), &nas100d_reset_handler,
		IRQF_DISABLED | IRQF_TRIGGER_LOW,
		"NAS100D reset button", NULL) < 0) {

		printk(KERN_DEBUG "Reset Button IRQ %d not available\n",
			gpio_to_irq(NAS100D_RB_GPIO));
	}

	/* The power button on the Iomega NAS100d is on GPIO 14, but
	 * it cannot handle interrupts on that GPIO line.  So we'll
	 * have to poll it with a kernel timer.
	 */

	/* Make sure that the power button GPIO is set up as an input */
	gpio_line_config(NAS100D_PB_GPIO, IXP4XX_GPIO_IN);

	/* Set the initial value for the power button IRQ handler */
	power_button_countdown = PBUTTON_HOLDDOWN_COUNT;

	mod_timer(&nas100d_power_timer, jiffies + msecs_to_jiffies(500));

	/*
	 * Map in a portion of the flash and read the MAC address.
	 * Since it is stored in BE in the flash itself, we need to
	 * byteswap it if we're in LE mode.
	 */
	f = ioremap(IXP4XX_EXP_BUS_BASE(0), 0x1000000);
	if (f) {
		for (i = 0; i < 6; i++)
#ifdef __ARMEB__
			nas100d_plat_eth[0].hwaddr[i] = readb(f + 0xFC0FD8 + i);
#else
			nas100d_plat_eth[0].hwaddr[i] = readb(f + 0xFC0FD8 + (i^3));
#endif
		iounmap(f);
	}
	printk(KERN_INFO "NAS100D: Using MAC address %pM for port 0\n",
	       nas100d_plat_eth[0].hwaddr);

}

MACHINE_START(NAS100D, "Iomega NAS 100d")
	/* Maintainer: www.nslu2-linux.org */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer          = &ixp4xx_timer,
	.init_machine	= nas100d_init,
MACHINE_END
