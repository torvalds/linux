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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/oplib.h>

#include "suncore.h"

int sunserial_current_minor = 64;

EXPORT_SYMBOL(sunserial_current_minor);

void
sunserial_console_termios(struct console *con)
{
	char mode[16], buf[16], *s;
	char *mode_prop = "ttyX-mode";
	char *cd_prop = "ttyX-ignore-cd";
	char *dtr_prop = "ttyX-rts-dtr-off";
	int baud, bits, stop, cflag;
	char parity;
	int carrier = 0;
	int rtsdtr = 1;
	int topnd, nd;

	if (!serial_console)
		return;

	if (serial_console == 1) {
		mode_prop[3] = 'a';
		cd_prop[3] = 'a';
		dtr_prop[3] = 'a';
	} else {
		mode_prop[3] = 'b';
		cd_prop[3] = 'b';
		dtr_prop[3] = 'b';
	}

	topnd = prom_getchild(prom_root_node);
	nd = prom_searchsiblings(topnd, "options");
	if (!nd) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	if (!prom_node_has_property(nd, mode_prop)) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	memset(mode, 0, sizeof(mode));
	prom_getstring(nd, mode_prop, mode, sizeof(mode));

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			carrier = 1;

		/* XXX: this is unused below. */
	}

	if (prom_node_has_property(nd, dtr_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, dtr_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			rtsdtr = 0;

		/* XXX: this is unused below. */
	}

no_options:
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

EXPORT_SYMBOL(sunserial_console_termios);

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

static void __exit suncore_exit(void)
{
}

module_init(suncore_init);
module_exit(suncore_exit);

MODULE_AUTHOR("Eddie C. Dost, David S. Miller");
MODULE_DESCRIPTION("Sun serial common layer");
MODULE_LICENSE("GPL");
