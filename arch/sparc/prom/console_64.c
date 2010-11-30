/* console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <linux/string.h>

extern int prom_stdin, prom_stdout;

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
static int prom_nbputchar(const char *buf)
{
	unsigned long args[7];

	args[0] = (unsigned long) "write";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) prom_stdout;
	args[4] = (unsigned long) buf;
	args[5] = 1;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	if (args[6] == 1)
		return 0;
	else
		return -1;
}

/* Blocking version of put character routine above. */
void prom_putchar(const char *buf)
{
	while (1) {
		int err = prom_nbputchar(buf);
		if (!err)
			break;
	}
}
