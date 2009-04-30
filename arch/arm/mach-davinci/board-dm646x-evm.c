/*
 * TI DaVinci DM646X EVM board
 *
 * Derived from: arch/arm/mach-davinci/board-evm.c
 * Copyright (C) 2006 Texas Instruments.
 *
 * (C) 2007-2008, MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

/**************************************************************************
 * Included Files
 **************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/dma-mapping.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/i2c/pcf857x.h>
#include <linux/etherdevice.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/dm646x.h>
#include <mach/common.h>
#include <mach/psc.h>
#include <mach/serial.h>
#include <mach/i2c.h>

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

/* LEDS */

static struct gpio_led evm_leds[] = {
	{ .name = "DS1", .active_low = 1, },
	{ .name = "DS2", .active_low = 1, },
	{ .name = "DS3", .active_low = 1, },
	{ .name = "DS4", .active_low = 1, },
};

static __initconst struct gpio_led_platform_data evm_led_data = {
	.num_leds = ARRAY_SIZE(evm_leds),
	.leds     = evm_leds,
};

static struct platform_device *evm_led_dev;

static int evm_led_setup(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	struct gpio_led *leds = evm_leds;
	int status;

	while (ngpio--) {
		leds->gpio = gpio++;
		leds++;
	};

	evm_led_dev = platform_device_alloc("leds-gpio", 0);
	platform_device_add_data(evm_led_dev, &evm_led_data,
				sizeof(evm_led_data));

	evm_led_dev->dev.parent = &client->dev;
	status = platform_device_add(evm_led_dev);
	if (status < 0) {
		platform_device_put(evm_led_dev);
		evm_led_dev = NULL;
	}
	return status;
}

static int evm_led_teardown(struct i2c_client *client, int gpio,
				unsigned ngpio, void *c)
{
	if (evm_led_dev) {
		platform_device_unregister(evm_led_dev);
		evm_led_dev = NULL;
	}
	return 0;
}

static int evm_sw_gpio[4] = { -EINVAL, -EINVAL, -EINVAL, -EINVAL };

static int evm_sw_setup(struct i2c_client *client, int gpio,
			unsigned ngpio, void *c)
{
	int status;
	int i;
	char label[10];

	for (i = 0; i < 4; ++i) {
		snprintf(label, 10, "user_sw%d", i);
		status = gpio_request(gpio, label);
		if (status)
			goto out_free;
		evm_sw_gpio[i] = gpio++;

		status = gpio_direction_input(evm_sw_gpio[i]);
		if (status) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
			goto out_free;
		}

		status = gpio_export(evm_sw_gpio[i], 0);
		if (status) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
			goto out_free;
		}
	}
	return status;
out_free:
	for (i = 0; i < 4; ++i) {
		if (evm_sw_gpio[i] != -EINVAL) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
		}
	}
	return status;
}

static int evm_sw_teardown(struct i2c_client *client, int gpio,
			unsigned ngpio, void *c)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if (evm_sw_gpio[i] != -EINVAL) {
			gpio_unexport(evm_sw_gpio[i]);
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
		}
	}
	return 0;
}

static int evm_pcf_setup(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	int status;

	if (ngpio < 8)
		return -EINVAL;

	status = evm_sw_setup(client, gpio, 4, c);
	if (status)
		return status;

	return evm_led_setup(client, gpio+4, 4, c);
}

static int evm_pcf_teardown(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	BUG_ON(ngpio < 8);

	evm_sw_teardown(client, gpio, 4, c);
	evm_led_teardown(client, gpio+4, 4, c);

	return 0;
}

static struct pcf857x_platform_data pcf_data = {
	.gpio_base	= DAVINCI_N_GPIO+1,
	.setup		= evm_pcf_setup,
	.teardown	= evm_pcf_teardown,
};

/* Most of this EEPROM is unused, but U-Boot uses some data:
 *  - 0x7f00, 6 bytes Ethernet Address
 *  - ... newer boards may have more
 */
static struct memory_accessor *at24_mem_acc;

static void at24_setup(struct memory_accessor *mem_acc, void *context)
{
	char mac_addr[6];

	at24_mem_acc = mem_acc;

	/* Read MAC addr from EEPROM */
	if (at24_mem_acc->read(at24_mem_acc, mac_addr, 0x7f00, ETH_ALEN) ==
	    ETH_ALEN)
		pr_info("Read MAC addr from EEPROM: %pM\n", mac_addr);
}

static struct at24_platform_data eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = at24_setup,
};

int dm646xevm_eeprom_read(void *buf, off_t off, size_t count)
{
	if (at24_mem_acc)
		return at24_mem_acc->read(at24_mem_acc, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm646xevm_eeprom_read);

int dm646xevm_eeprom_write(void *buf, off_t off, size_t count)
{
	if (at24_mem_acc)
		return at24_mem_acc->write(at24_mem_acc, buf, off, count);
	return -ENODEV;
}
EXPORT_SYMBOL(dm646xevm_eeprom_write);

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data  = &eeprom_info,
	},
	{
		I2C_BOARD_INFO("pcf8574a", 0x38),
		.platform_data	= &pcf_data,
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq       = 100 /* kHz */,
	.bus_delay      = 0 /* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static void __init davinci_map_io(void)
{
	davinci_map_common_io();
	dm646x_init();
}

static __init void evm_init(void)
{
	evm_init_i2c();
	davinci_serial_init(&uart_config);
}

static __init void davinci_dm646x_evm_irq_init(void)
{
	davinci_irq_init();
}

MACHINE_START(DAVINCI_DM6467_EVM, "DaVinci DM646x EVM")
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (0x80000100),
	.map_io       = davinci_map_io,
	.init_irq     = davinci_dm646x_evm_irq_init,
	.timer        = &davinci_timer,
	.init_machine = evm_init,
MACHINE_END

