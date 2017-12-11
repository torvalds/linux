// SPDX-License-Identifier: GPL-2.0
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
#include <linux/string.h>

static int __prom_console_write_buf(const char *buf, int len)
{
	unsigned long args[7];
	int ret;

	args[0] = (unsigned long) "write";
	args[1] = 3;
	args[2] = 1;
	args[3] = (unsigned int) prom_stdout;
	args[4] = (unsigned long) buf;
	args[5] = (unsigned int) len;
	args[6] = (unsigned long) -1;

	p1275_cmd_direct(args);

	ret = (int) args[6];
	if (ret < 0)
		return -1;
	return ret;
}

void prom_console_write_buf(const char *buf, int len)
{
	while (len) {
		int n = __prom_console_write_buf(buf, len);
		if (n < 0)
			continue;
		len -= n;
		buf += len;
	}
}
