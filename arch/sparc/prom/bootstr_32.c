// SPDX-License-Identifier: GPL-2.0
/*
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <asm/oplib.h>
#include <linux/init.h>

#define BARG_LEN  256
static char barg_buf[BARG_LEN] = { 0 };
static char fetched __initdata = 0;

char * __init
prom_getbootargs(void)
{
	int iter;
	char *cp, *arg;

	/* This check saves us from a panic when bootfd patches args. */
	if (fetched) {
		return barg_buf;
	}

	switch (prom_vers) {
	case PROM_V0:
		cp = barg_buf;
		/* Start from 1 and go over fd(0,0,0)kernel */
		for (iter = 1; iter < 8; iter++) {
			arg = (*(romvec->pv_v0bootargs))->argv[iter];
			if (arg == NULL)
				break;
			while (*arg != 0) {
				/* Leave place for space and null. */
				if (cp >= barg_buf + BARG_LEN - 2)
					/* We might issue a warning here. */
					break;
				*cp++ = *arg++;
			}
			*cp++ = ' ';
			if (cp >= barg_buf + BARG_LEN - 1)
				/* We might issue a warning here. */
				break;
		}
		*cp = 0;
		break;
	case PROM_V2:
	case PROM_V3:
		/*
		 * V3 PROM cannot supply as with more than 128 bytes
		 * of an argument. But a smart bootstrap loader can.
		 */
		strscpy(barg_buf, *romvec->pv_v2bootargs.bootargs, sizeof(barg_buf));
		break;
	default:
		break;
	}

	fetched = 1;
	return barg_buf;
}
