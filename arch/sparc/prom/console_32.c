/*
 * console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Pete Zaitcev <zaitcev@yahoo.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <linux/string.h>

extern void restore_current(void);

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
static int prom_nbputchar(const char *buf)
{
	unsigned long flags;
	int i = -1;

	spin_lock_irqsave(&prom_lock, flags);
	switch(prom_vers) {
	case PROM_V0:
		i = (*(romvec->pv_nbputchar))(*buf);
		break;
	case PROM_V2:
	case PROM_V3:
		if ((*(romvec->pv_v2devops).v2_dev_write)(*romvec->pv_v2bootargs.fd_stdout,
							  buf, 0x1) == 1)
			i = 0;
		break;
	default:
		break;
	};
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return i; /* Ugh, we could spin forever on unsupported proms ;( */
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
