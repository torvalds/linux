/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

Module Name:
ap.h

Abstract:
Miniport generic portion header file

Revision History:
Who         When          What
--------    ----------    ----------------------------------------------
Paul Lin    08-01-2002    created
James Tan   09-06-2002    modified (Revise NTCRegTable)
John Chang  12-22-2004    modified for RT2561/2661. merge with STA driver
*/
#ifndef __AP_H__
#define __AP_H__

/* ap_wpa.c */
void WpaStateMachineInit(struct rt_rtmp_adapter *pAd,
			 struct rt_state_machine *Sm,
			 OUT STATE_MACHINE_FUNC Trans[]);

#ifdef RTMP_MAC_USB
void BeaconUpdateExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3);
#endif /* RTMP_MAC_USB // */

void RTMPSetPiggyBack(struct rt_rtmp_adapter *pAd, IN BOOLEAN bPiggyBack);

void MacTableReset(struct rt_rtmp_adapter *pAd);

struct rt_mac_table_entry *MacTableInsertEntry(struct rt_rtmp_adapter *pAd,
				     u8 *pAddr,
				     u8 apidx, IN BOOLEAN CleanAll);

BOOLEAN MacTableDeleteEntry(struct rt_rtmp_adapter *pAd,
			    u16 wcid, u8 *pAddr);

struct rt_mac_table_entry *MacTableLookup(struct rt_rtmp_adapter *pAd,
								u8 *pAddr);

#endif /* __AP_H__ */
