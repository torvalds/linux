/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/mtd/physmap.h>
#include <linux/ssb/ssb.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/gpio.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

/* GPIO definitions for the WGT634U */
#define WGT634U_GPIO_LED	3
#define WGT634U_GPIO_RESET	2
#define WGT634U_GPIO_TP1	7
#define WGT634U_GPIO_TP2	6
#define WGT634U_GPIO_TP3	5
#define WGT634U_GPIO_TP4	4
#define WGT634U_GPIO_TP5	1

static struct gpio_led wgt634u_leds[] = {
	{
		.name = "power",
		.gpio = WGT634U_GPIO_LED,
		.active_low = 1,
		.default_trigger = "heartbeat",
	},
};

static struct gpio_led_platform_data wgt634u_led_data = {
	.num_leds =     ARRAY_SIZE(wgt634u_leds),
	.leds =         wgt634u_leds,
};

static struct platform_device wgt634u_gpio_leds = {
	.name =         "leds-gpio",
	.id =           -1,
	.dev = {
		.platform_data = &wgt634u_led_data,
	}
};


/* 8MiB flash. The struct mtd_partition matches original Netgear WGT634U
   firmware. */
static struct mtd_partition wgt634u_partitions[] = {
	{
		.name       = "cfe",
		.offset     = 0,
		.size       = 0x60000,		/* 384k */
		.mask_flags = MTD_WRITEABLE 	/* force read-only */
	},
	{
		.name   = "config",
		.offset = 0x60000,
		.size   = 0x20000		/* 128k */
	},
	{
		.name   = "linux",
		.offset = 0x80000,
		.size   = 0x140000 		/* 1280k */
	},
	{
		.name   = "jffs",
		.offset = 0x1c0000,
		.size   = 0x620000 		/* 6272k */
	},
	{
		.name   = "nvram",
		.offset = 0x7e0000,
		.size   = 0x20000		/* 128k */
	},
};

static struct physmap_flash_data wgt634u_flash_data = {
	.parts    = wgt634u_partitions,
	.nr_parts = ARRAY_SIZE(wgt634u_partitions)
};

static struct resource wgt634u_flash_resource = {
	.flags = IORESOURCE_MEM,
};

static struct platform_device wgt634u_flash = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev           = { .platform_data = &wgt634u_flash_data, },
	.resource      = &wgt634u_flash_resource,
	.num_resources = 1,
};

/* Platform devices */
static struct platform_device *wgt634u_devices[] __initdata = {
	&wgt634u_flash,
	&wgt634u_gpio_leds,
};

static irqreturn_t gpio_interrupt(int irq, void *ignored)
{
	int state;

	/* Interrupts are shared, check if the current one is
	   a GPIO interrupt. */
	if (!ssb_chipco_irq_status(&ssb_bcm47xx.chipco,
				   SSB_CHIPCO_IRQ_GPIO))
		return IRQ_NONE;

	state = gpio_get_value(WGT634U_GPIO_RESET);

	/* Interrupt are level triggered, revert the interrupt polarity
	   to clear the interrupt. */
	gpio_polarity(WGT634U_GPIO_RESET, state);

	if (!state) {
		printk(KERN_INFO "Reset button pressed");
		ctrl_alt_del();
	}

	return IRQ_HANDLED;
}

static int __init wgt634u_init(void)
{
	/* There is no easy way to detect that we are running on a WGT634U
	 * machine. Use the MAC address as an heuristic. Netgear Inc. has
	 * been allocated ranges 00:09:5b:xx:xx:xx and 00:0f:b5:xx:xx:xx.
	 */

	u8 *et0mac = ssb_bcm47xx.sprom.et0mac;

	if (et0mac[0] == 0x00 &&
	    ((et0mac[1] == 0x09 && et0mac[2] == 0x5b) ||
	     (et0mac[1] == 0x0f && et0mac[2] == 0xb5))) {
		struct ssb_mipscore *mcore = &ssb_bcm47xx.mipscore;

		printk(KERN_INFO "WGT634U machine detected.\n");

		if (!request_irq(gpio_to_irq(WGT634U_GPIO_RESET),
				 gpio_interrupt, IRQF_SHARED,
				 "WGT634U GPIO", &ssb_bcm47xx.chipco)) {
			gpio_direction_input(WGT634U_GPIO_RESET);
			gpio_intmask(WGT634U_GPIO_RESET, 1);
			ssb_chipco_irq_mask(&ssb_bcm47xx.chipco,
					    SSB_CHIPCO_IRQ_GPIO,
					    SSB_CHIPCO_IRQ_GPIO);
		}

		wgt634u_flash_data.width = mcore->flash_buswidth;
		wgt634u_flash_resource.start = mcore->flash_window;
		wgt634u_flash_resource.end = mcore->flash_window
					   + mcore->flash_window_size
					   - 1;
		return platform_add_devices(wgt634u_devices,
					    ARRAY_SIZE(wgt634u_devices));
	} else
		return -ENODEV;
}

module_init(wgt634u_init);

