/*
 * arch/h8300/kernel/sim-console.c
 *
 *  Copyright (C) 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_core.h>

static void sim_write(struct console *con, const char *s, unsigned n)
{
	register const int fd __asm__("er0") = 1; /* stdout */
	register const char *_ptr __asm__("er1") = s;
	register const unsigned _len __asm__("er2") = n;

	__asm__(".byte 0x5e,0x00,0x00,0xc7\n\t" /* jsr @0xc7 (sys_write) */
		: : "g"(fd), "g"(_ptr), "g"(_len));
}

static int __init sim_setup(struct earlycon_device *device, const char *opt)
{
	device->con->write = sim_write;
	return 0;
}

EARLYCON_DECLARE(h8sim, sim_setup);
