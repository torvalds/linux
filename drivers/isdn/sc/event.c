/* $Id: event.c,v 1.4.8.1 2001/09/23 22:24:59 kai Exp $
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

#include "includes.h"
#include "hardware.h"
#include "message.h"
#include "card.h"

#ifdef DEBUG
static char *events[] = { "ISDN_STAT_STAVAIL",
			  "ISDN_STAT_ICALL",
			  "ISDN_STAT_RUN",
			  "ISDN_STAT_STOP",
			  "ISDN_STAT_DCONN",
			  "ISDN_STAT_BCONN",
			  "ISDN_STAT_DHUP",
			  "ISDN_STAT_BHUP",
			  "ISDN_STAT_CINF",
			  "ISDN_STAT_LOAD",
			  "ISDN_STAT_UNLOAD",
			  "ISDN_STAT_BSENT",
			  "ISDN_STAT_NODCH",
			  "ISDN_STAT_ADDCH",
			  "ISDN_STAT_CAUSE" };
#endif

int indicate_status(int card, int event,ulong Channel,char *Data)
{
	isdn_ctrl cmd;

#ifdef DEBUG
	pr_debug("%s: Indicating event %s on Channel %d\n",
		sc_adapter[card]->devicename, events[event-256], Channel);
#endif
	if (Data != NULL){
		pr_debug("%s: Event data: %s\n", sc_adapter[card]->devicename,
			Data);
		switch (event) {
			case ISDN_STAT_BSENT:
				memcpy(&cmd.parm.length, Data, sizeof(cmd.parm.length));
				break;
			case ISDN_STAT_ICALL:
				memcpy(&cmd.parm.setup, Data, sizeof(cmd.parm.setup));
				break;
			default:
				strcpy(cmd.parm.num, Data);
		}
	}

	cmd.command = event;
	cmd.driver = sc_adapter[card]->driverId;
	cmd.arg = Channel;
	return sc_adapter[card]->card->statcallb(&cmd);
}
