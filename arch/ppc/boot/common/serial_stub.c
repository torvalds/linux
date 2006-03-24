/*
 * This is a few stub routines to make the boot code cleaner looking when
 * there is no serial port support doesn't need to be closed, for example.
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * 2003 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.  This program is licensed "as
 * is" without any warranty of any kind, whether express or implied.
 */

unsigned long __attribute__ ((weak))
serial_init(int chan, void *ignored)
{
	return 0;
}

void __attribute__ ((weak))
serial_close(unsigned long com_port)
{
}
