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

static int serial_open(void)
{
	struct serial_console_data *scdp = console_ops.data;
	return scdp->open();
}

static void serial_write(const char *buf, int len)
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

	devp = serial_get_stdout_devp();
	if (devp == NULL)
		goto err_out;

	if (dt_is_compatible(devp, "ns16550"))
		rc = ns16550_console_init(devp, &serial_cd);
	else if (dt_is_compatible(devp, "marvell,mv64360-mpsc"))
		rc = mpsc_console_init(devp, &serial_cd);
	else if (dt_is_compatible(devp, "fsl,cpm1-scc-uart") ||
	         dt_is_compatible(devp, "fsl,cpm1-smc-uart") ||
	         dt_is_compatible(devp, "fsl,cpm2-scc-uart") ||
	         dt_is_compatible(devp, "fsl,cpm2-smc-uart"))
		rc = cpm_console_init(devp, &serial_cd);
	else if (dt_is_compatible(devp, "fsl,mpc5200-psc-uart"))
		rc = mpc5200_psc_console_init(devp, &serial_cd);
	else if (dt_is_compatible(devp, "xlnx,opb-uartlite-1.00.b") ||
		 dt_is_compatible(devp, "xlnx,xps-uartlite-1.00.a"))
		rc = uartlite_console_init(devp, &serial_cd);

	/* Add other serial console driver calls here */

	if (!rc) {
		console_ops.open = serial_open;
		console_ops.write = serial_write;
		console_ops.close = serial_close;
		console_ops.data = &serial_cd;

		if (serial_cd.getc)
			console_ops.edit_cmdline = serial_edit_cmdline;

		return 0;
	}
err_out:
	return -1;
}
