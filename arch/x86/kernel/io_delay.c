/*
 * I/O delay strategies for inb_p/outb_p
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <asm/io.h>

/*
 * Allow for a DMI based override of port 0x80 needed for certain HP laptops
 */
#define IO_DELAY_PORT_STD 0x80
#define IO_DELAY_PORT_ALT 0xed

static void standard_io_delay(void)
{
	asm volatile ("outb %%al, %0" : : "N" (IO_DELAY_PORT_STD));
}

static void alternate_io_delay(void)
{
	asm volatile ("outb %%al, %0" : : "N" (IO_DELAY_PORT_ALT));
}

/*
 * 2 usecs is an upper-bound for the outb delay but note that udelay doesn't
 * have the bus-level side-effects that outb does
 */
#define IO_DELAY_USECS 2

/*
 * High on a hill was a lonely goatherd
 */
static void udelay_io_delay(void)
{
	udelay(IO_DELAY_USECS);
}

#ifndef CONFIG_UDELAY_IO_DELAY
static void (*io_delay)(void) = standard_io_delay;
#else
static void (*io_delay)(void) = udelay_io_delay;
#endif

/*
 * Paravirt wants native_io_delay to be a constant.
 */
void native_io_delay(void)
{
	io_delay();
}
EXPORT_SYMBOL(native_io_delay);

#ifndef CONFIG_UDELAY_IO_DELAY
static int __init dmi_alternate_io_delay_port(const struct dmi_system_id *id)
{
	printk(KERN_NOTICE "%s: using alternate I/O delay port\n", id->ident);
	io_delay = alternate_io_delay;
	return 0;
}

static struct dmi_system_id __initdata alternate_io_delay_port_dmi_table[] = {
	{
		.callback	= dmi_alternate_io_delay_port,
		.ident		= "HP Pavilion dv9000z",
		.matches	= {
			DMI_MATCH(DMI_BOARD_VENDOR, "Quanta"),
			DMI_MATCH(DMI_BOARD_NAME, "30B9")
		}
	},
	{
	}
};

static int __initdata io_delay_override;

void __init io_delay_init(void)
{
	if (!io_delay_override)
		dmi_check_system(alternate_io_delay_port_dmi_table);
}
#endif

static int __init io_delay_param(char *s)
{
	if (!s)
		return -EINVAL;

	if (!strcmp(s, "standard"))
		io_delay = standard_io_delay;
	else if (!strcmp(s, "alternate"))
		io_delay = alternate_io_delay;
	else if (!strcmp(s, "udelay"))
		io_delay = udelay_io_delay;
	else
		return -EINVAL;

#ifndef CONFIG_UDELAY_IO_DELAY
	io_delay_override = 1;
#endif
	return 0;
}

early_param("io_delay", io_delay_param);
