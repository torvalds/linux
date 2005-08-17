/*
 * Helper functions for I/O pins.
 *
 * Copyright (c) 2004 Axis Communications AB.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/arch/pinmux.h>
#include <asm/arch/hwregs/gio_defs.h>

struct crisv32_ioport crisv32_ioports[] =
{
	{
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pa_oe),
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pa_dout),
		(unsigned long*)REG_ADDR(gio, regi_gio, r_pa_din),
		8
	},
	{
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pb_oe),
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pb_dout),
		(unsigned long*)REG_ADDR(gio, regi_gio, r_pb_din),
		18
	},
	{
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pc_oe),
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pc_dout),
		(unsigned long*)REG_ADDR(gio, regi_gio, r_pc_din),
		18
	},
	{
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pd_oe),
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pd_dout),
		(unsigned long*)REG_ADDR(gio, regi_gio, r_pd_din),
		18
	},
	{
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pe_oe),
		(unsigned long*)REG_ADDR(gio, regi_gio, rw_pe_dout),
		(unsigned long*)REG_ADDR(gio, regi_gio, r_pe_din),
		18
	}
};

#define NBR_OF_PORTS sizeof(crisv32_ioports)/sizeof(struct crisv32_ioport)

struct crisv32_iopin crisv32_led1_green;
struct crisv32_iopin crisv32_led1_red;
struct crisv32_iopin crisv32_led2_green;
struct crisv32_iopin crisv32_led2_red;
struct crisv32_iopin crisv32_led3_green;
struct crisv32_iopin crisv32_led3_red;

/* Dummy port used when green LED and red LED is on the same bit */
static unsigned long io_dummy;
static struct crisv32_ioport dummy_port =
{
	&io_dummy,
	&io_dummy,
	&io_dummy,
	18
};
static struct crisv32_iopin dummy_led =
{
	&dummy_port,
	0
};

static int __init crisv32_io_init(void)
{
	int ret = 0;
	/* Initialize LEDs */
	ret += crisv32_io_get_name(&crisv32_led1_green, CONFIG_ETRAX_LED1G);
	ret += crisv32_io_get_name(&crisv32_led1_red, CONFIG_ETRAX_LED1R);
	ret += crisv32_io_get_name(&crisv32_led2_green, CONFIG_ETRAX_LED2G);
	ret += crisv32_io_get_name(&crisv32_led2_red, CONFIG_ETRAX_LED2R);
	ret += crisv32_io_get_name(&crisv32_led3_green, CONFIG_ETRAX_LED3G);
	ret += crisv32_io_get_name(&crisv32_led3_red, CONFIG_ETRAX_LED3R);
	crisv32_io_set_dir(&crisv32_led1_green, crisv32_io_dir_out);
	crisv32_io_set_dir(&crisv32_led1_red, crisv32_io_dir_out);
	crisv32_io_set_dir(&crisv32_led2_green, crisv32_io_dir_out);
	crisv32_io_set_dir(&crisv32_led2_red, crisv32_io_dir_out);
	crisv32_io_set_dir(&crisv32_led3_green, crisv32_io_dir_out);
	crisv32_io_set_dir(&crisv32_led3_red, crisv32_io_dir_out);

	if (!strcmp(CONFIG_ETRAX_LED1G, CONFIG_ETRAX_LED1R))
		crisv32_led1_red = dummy_led;
	if (!strcmp(CONFIG_ETRAX_LED2G, CONFIG_ETRAX_LED2R))
		crisv32_led2_red = dummy_led;

	return ret;
}

__initcall(crisv32_io_init);

int crisv32_io_get(struct crisv32_iopin* iopin,
                   unsigned int port, unsigned int pin)
{
	if (port > NBR_OF_PORTS)
		return -EINVAL;
	if (port > crisv32_ioports[port].pin_count)
		return -EINVAL;

	iopin->bit = 1 << pin;
	iopin->port = &crisv32_ioports[port];

	if (crisv32_pinmux_alloc(port, pin, pin, pinmux_gpio))
		return -EIO;

	return 0;
}

int crisv32_io_get_name(struct crisv32_iopin* iopin,
                         char* name)
{
	int port;
	int pin;

	if (toupper(*name) == 'P')
		name++;

	if (toupper(*name) < 'A' || toupper(*name) > 'E')
		return -EINVAL;

	port = toupper(*name) - 'A';
	name++;
	pin = simple_strtoul(name, NULL, 10);

	if (pin < 0 || pin > crisv32_ioports[port].pin_count)
		return -EINVAL;

	iopin->bit = 1 << pin;
	iopin->port = &crisv32_ioports[port];

	if (crisv32_pinmux_alloc(port, pin, pin, pinmux_gpio))
		return -EIO;

	return 0;
}

#ifdef CONFIG_PCI
/* PCI I/O access stuff */
struct cris_io_operations* cris_iops = NULL;
EXPORT_SYMBOL(cris_iops);
#endif

