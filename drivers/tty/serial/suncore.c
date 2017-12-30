// SPDX-License-Identifier: GPL-2.0
/* suncore.c
 *
 * Common SUN serial routines.  Based entirely
 * upon drivers/sbus/char/sunserial.c which is:
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 *
 * Adaptation to new UART layer is:
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/serial_core.h>
#include <linux/sunserialcore.h>
#include <linux/init.h>

#include <asm/prom.h>


static int sunserial_current_minor = 64;

int sunserial_register_minors(struct uart_driver *drv, int count)
{
	int err = 0;

	drv->minor = sunserial_current_minor;
	drv->nr += count;
	/* Register the driver on the first call */
	if (drv->nr == count)
		err = uart_register_driver(drv);
	if (err == 0) {
		sunserial_current_minor += count;
		drv->tty_driver->name_base = drv->minor - 64;
	}
	return err;
}
EXPORT_SYMBOL(sunserial_register_minors);

void sunserial_unregister_minors(struct uart_driver *drv, int count)
{
	drv->nr -= count;
	sunserial_current_minor -= count;

	if (drv->nr == 0)
		uart_unregister_driver(drv);
}
EXPORT_SYMBOL(sunserial_unregister_minors);

int sunserial_console_match(struct console *con, struct device_node *dp,
			    struct uart_driver *drv, int line, bool ignore_line)
{
	if (!con)
		return 0;

	drv->cons = con;

	if (of_console_device != dp)
		return 0;

	if (!ignore_line) {
		int off = 0;

		if (of_console_options &&
		    *of_console_options == 'b')
			off = 1;

		if ((line & 1) != off)
			return 0;
	}

	if (!console_set_on_cmdline) {
		con->index = line;
		add_preferred_console(con->name, line, NULL);
	}
	return 1;
}
EXPORT_SYMBOL(sunserial_console_match);

void sunserial_console_termios(struct console *con, struct device_node *uart_dp)
{
	const char *mode, *s;
	char mode_prop[] = "ttyX-mode";
	int baud, bits, stop, cflag;
	char parity;

	if (!strcmp(uart_dp->name, "rsc") ||
	    !strcmp(uart_dp->name, "rsc-console") ||
	    !strcmp(uart_dp->name, "rsc-control")) {
		mode = of_get_property(uart_dp,
				       "ssp-console-modes", NULL);
		if (!mode)
			mode = "115200,8,n,1,-";
	} else if (!strcmp(uart_dp->name, "lom-console")) {
		mode = "9600,8,n,1,-";
	} else {
		struct device_node *dp;
		char c;

		c = 'a';
		if (of_console_options)
			c = *of_console_options;

		mode_prop[3] = c;

		dp = of_find_node_by_path("/options");
		mode = of_get_property(dp, mode_prop, NULL);
		if (!mode)
			mode = "9600,8,n,1,-";
	}

	cflag = CREAD | HUPCL | CLOCAL;

	s = mode;
	baud = simple_strtoul(s, NULL, 0);
	s = strchr(s, ',');
	bits = simple_strtoul(++s, NULL, 0);
	s = strchr(s, ',');
	parity = *(++s);
	s = strchr(s, ',');
	stop = simple_strtoul(++s, NULL, 0);
	s = strchr(s, ',');
	/* XXX handshake is not handled here. */

	switch (baud) {
		case 150: cflag |= B150; break;
		case 300: cflag |= B300; break;
		case 600: cflag |= B600; break;
		case 1200: cflag |= B1200; break;
		case 2400: cflag |= B2400; break;
		case 4800: cflag |= B4800; break;
		case 9600: cflag |= B9600; break;
		case 19200: cflag |= B19200; break;
		case 38400: cflag |= B38400; break;
		case 57600: cflag |= B57600; break;
		case 115200: cflag |= B115200; break;
		case 230400: cflag |= B230400; break;
		case 460800: cflag |= B460800; break;
		default: baud = 9600; cflag |= B9600; break;
	}

	switch (bits) {
		case 5: cflag |= CS5; break;
		case 6: cflag |= CS6; break;
		case 7: cflag |= CS7; break;
		case 8: cflag |= CS8; break;
		default: cflag |= CS8; break;
	}

	switch (parity) {
		case 'o': cflag |= (PARENB | PARODD); break;
		case 'e': cflag |= PARENB; break;
		case 'n': default: break;
	}

	switch (stop) {
		case 2: cflag |= CSTOPB; break;
		case 1: default: break;
	}

	con->cflag = cflag;
}

/* Sun serial MOUSE auto baud rate detection.  */
static struct mouse_baud_cflag {
	int baud;
	unsigned int cflag;
} mouse_baud_table[] = {
	{ 1200, B1200 },
	{ 2400, B2400 },
	{ 4800, B4800 },
	{ 9600, B9600 },
	{ -1, ~0 },
	{ -1, ~0 },
};

unsigned int suncore_mouse_baud_cflag_next(unsigned int cflag, int *new_baud)
{
	int i;

	for (i = 0; mouse_baud_table[i].baud != -1; i++)
		if (mouse_baud_table[i].cflag == (cflag & CBAUD))
			break;

	i += 1;
	if (mouse_baud_table[i].baud == -1)
		i = 0;

	*new_baud = mouse_baud_table[i].baud;
	return mouse_baud_table[i].cflag;
}

EXPORT_SYMBOL(suncore_mouse_baud_cflag_next);

/* Basically, when the baud rate is wrong the mouse spits out
 * breaks to us.
 */
int suncore_mouse_baud_detection(unsigned char ch, int is_break)
{
	static int mouse_got_break = 0;
	static int ctr = 0;

	if (is_break) {
		/* Let a few normal bytes go by before we jump the gun
		 * and say we need to try another baud rate.
		 */
		if (mouse_got_break && ctr < 8)
			return 1;

		/* Ok, we need to try another baud. */
		ctr = 0;
		mouse_got_break = 1;
		return 2;
	}
	if (mouse_got_break) {
		ctr++;
		if (ch == 0x87) {
			/* Correct baud rate determined. */
			mouse_got_break = 0;
		}
		return 1;
	}
	return 0;
}

EXPORT_SYMBOL(suncore_mouse_baud_detection);

static int __init suncore_init(void)
{
	return 0;
}
device_initcall(suncore_init);

#if 0 /* ..def MODULE ; never supported as such */
MODULE_AUTHOR("Eddie C. Dost, David S. Miller");
MODULE_DESCRIPTION("Sun serial common layer");
MODULE_LICENSE("GPL");
#endif
