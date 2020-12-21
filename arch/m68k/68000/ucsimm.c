// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *  Copyright (C) 2001 Georges Menie, Ken Desmet
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <asm/bootstd.h>
#include <asm/machdep.h>
#include <asm/MC68VZ328.h>


#include "m68328.h"

unsigned char *cs8900a_hwaddr;
static int errno;

_bsc0(char *, getserialnum)
_bsc1(unsigned char *, gethwaddr, int, a)
_bsc1(char *, getbenv, char *, a)

void __init init_ucsimm(char *command, int size)
{
	char *p;

	pr_info("uCsimm/uCdimm serial string [%s]\n", getserialnum());
	p = cs8900a_hwaddr = gethwaddr(0);
	pr_info("uCsimm/uCdimm hwaddr %pM\n", p);
	p = getbenv("APPEND");
	if (p)
		strcpy(p, command);
	else
		command[0] = 0;
}
