/*
 * Generic serial console support
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * Code in serial_edit_cmdline() copied from <file:arch/ppc/boot/simple/misc.c>
 * and was written by Matt Porter <mporter@kernel.crashing.org>.
 *
 * 2001,2006 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"

extern void udelay(long delay);

static int serial_open(void)
{
	struct serial_console_data *scdp = console_ops.data;
	return scdp->open();
}

static void serial_write(char *buf, int len)
{
	struct serial_console_data *scdp = console_ops.data;

	while (*buf != '\0')
		scdp->putc(*buf++);
}

static void serial_edit_cmdline(char *buf, int len)
{
	int timer = 0, count;
	char ch, *cp;
	struct serial_console_data *scdp = console_ops.data;

	cp = buf;
	count = strlen(buf);
	cp = &buf[count];
	count++;

	while (timer++ < 5*1000) {
		if (scdp->tstc()) {
			while (((ch = scdp->getc()) != '\n') && (ch != '\r')) {
				/* Test for backspace/delete */
				if ((ch == '\b') || (ch == '\177')) {
					if (cp != buf) {
						cp--;
						count--;
						printf("\b \b");
					}
				/* Test for ^x/^u (and wipe the line) */
				} else if ((ch == '\030') || (ch == '\025')) {
					while (cp != buf) {
						cp--;
						count--;
						printf("\b \b");
					}
				} else if (count < len) {
						*cp++ = ch;
						count++;
						scdp->putc(ch);
				}
			}
			break;  /* Exit 'timer' loop */
		}
		udelay(1000);  /* 1 msec */
	}
	*cp = 0;
}

static void serial_close(void)
{
	struct serial_console_data *scdp = console_ops.data;

	if (scdp->close)
		scdp->close();
}

static void *serial_get_stdout_devp(void)
{
	void *devp;
	char devtype[MAX_PROP_LEN];
	char path[MAX_PATH_LEN];

	devp = finddevice("/chosen");
	if (devp == NULL)
		goto err_out;

	if (getprop(devp, "linux,stdout-path", path, MAX_PATH_LEN) > 0) {
		devp = finddevice(path);
		if (devp == NULL)
			goto err_out;

		if ((getprop(devp, "device_type", devtype, sizeof(devtype)) > 0)
				&& !strcmp(devtype, "serial"))
			return devp;
	}
err_out:
	return NULL;
}

static struct serial_console_data serial_cd;

/* Node's "compatible" property determines which serial driver to use */
int serial_console_init(void)
{
	void *devp;
	int rc = -1;
	char compat[MAX_PROP_LEN];

	devp = serial_get_stdout_devp();
	if (devp == NULL)
		goto err_out;

	if (getprop(devp, "compatible", compat, sizeof(compat)) < 0)
		goto err_out;

	if (!strcmp(compat, "ns16550"))
		rc = ns16550_console_init(devp, &serial_cd);

	/* Add other serial console driver calls here */

	if (!rc) {
		console_ops.open = serial_open;
		console_ops.write = serial_write;
		console_ops.edit_cmdline = serial_edit_cmdline;
		console_ops.close = serial_close;
		console_ops.data = &serial_cd;

		return 0;
	}
err_out:
	return -1;
}
