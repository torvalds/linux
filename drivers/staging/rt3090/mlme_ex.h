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
    mlme_ex.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
	Fonchi		2007-06-25		Extend original mlme APIs to support multi-entries
*/
#ifndef __MLME_EX_H__
#define __MLME_EX_H__

#include "mlme_ex_def.h"


VOID StateMachineInitEx(
	IN STATE_MACHINE_EX *S,
	IN STATE_MACHINE_FUNC_EX Trans[],
	IN ULONG StNr,
	IN ULONG MsgNr,
	IN STATE_MACHINE_FUNC_EX DefFunc,
	IN ULONG InitState,
	IN ULONG Base);

VOID StateMachineSetActionEx(
	IN STATE_MACHINE_EX *S,
	IN ULONG St,
	IN ULONG Msg,
	IN STATE_MACHINE_FUNC_EX Func);

BOOLEAN isValidApCliIf(
	SHORT Idx);

VOID StateMachinePerformActionEx(
	IN PRTMP_ADAPTER pAd,
	IN STATE_MACHINE_EX *S,
	IN MLME_QUEUE_ELEM *Elem,
	USHORT Idx,
	PULONG pCurrState);

BOOLEAN MlmeEnqueueEx(
	IN	PRTMP_ADAPTER pAd,
	IN ULONG Machine,
	IN ULONG MsgType,
	IN ULONG MsgLen,
	IN VOID *Msg,
	IN USHORT Idx);

VOID DropEx(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem,
	PULONG pCurrState,
	USHORT Idx);

#endif /* __MLME_EX_H__ */
