/* $Id: debug.c,v 1.5.6.1 2001/09/23 22:24:59 kai Exp $
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

#include <linux/kernel.h>
#include <linux/string.h>

int dbg_level = 0;
static char dbg_funcname[255];

void dbg_endfunc(void)
{
	if (dbg_level) {
		printk("<-- Leaving function %s\n", dbg_funcname);
		strcpy(dbg_funcname, "");
	}
}

void dbg_func(char *func)
{
	strcpy(dbg_funcname, func);
	if(dbg_level)
		printk("--> Entering function %s\n", dbg_funcname);
}

inline void pullphone(char *dn, char *str)
{
	int i = 0;

	while(dn[i] != ',')
		str[i] = dn[i], i++;
	str[i] = 0x0;
}
