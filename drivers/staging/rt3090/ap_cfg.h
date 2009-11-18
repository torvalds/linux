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
    ap_cfg.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
*/
#ifndef __AP_CFG_H__
#define __AP_CFG_H__


#include "rt_config.h"

INT RTMPAPPrivIoctlSet(
	IN RTMP_ADAPTER *pAd,
	IN struct iwreq *pIoctlCmdStr);

INT RTMPAPPrivIoctlShow(
	IN RTMP_ADAPTER *pAd,
	IN struct iwreq *pIoctlCmdStr);

INT RTMPAPSetInformation(
	IN	PRTMP_ADAPTER	pAd,
	IN	OUT	struct iwreq	*rq,
	IN	INT				cmd);

INT RTMPAPQueryInformation(
	IN	PRTMP_ADAPTER       pAd,
	IN	OUT	struct iwreq    *rq,
	IN	INT                 cmd);

VOID RTMPIoctlStatistics(
	IN PRTMP_ADAPTER pAd,
	IN struct iwreq *wrq);

VOID RTMPIoctlGetMacTable(
	IN PRTMP_ADAPTER pAd,
	IN struct iwreq *wrq);

#ifdef DBG
VOID RTMPAPIoctlBBP(
    IN  PRTMP_ADAPTER   pAdapter,
    IN  struct iwreq    *wrq);

VOID RTMPAPIoctlMAC(
    IN  PRTMP_ADAPTER   pAdapter,
    IN  struct iwreq    *wrq);

VOID RTMPAPIoctlE2PROM(
    IN  PRTMP_ADAPTER   pAdapter,
    IN  struct iwreq    *wrq);

#ifdef RTMP_RF_RW_SUPPORT
VOID RTMPAPIoctlRF(
	IN	PRTMP_ADAPTER	pAdapter,
	IN	struct iwreq	*wrq);
#endif // RTMP_RF_RW_SUPPORT //

#endif // DBG //

VOID RT28XX_IOCTL_MaxRateGet(
	IN	RTMP_ADAPTER			*pAd,
	IN	PHTTRANSMIT_SETTING	pHtPhyMode,
	OUT	UINT32					*pRate);


#ifdef DOT11_N_SUPPORT
VOID RTMPIoctlQueryBaTable(
	IN	PRTMP_ADAPTER	pAd,
	IN	struct iwreq	*wrq);
#endif // DOT11_N_SUPPORT //

VOID RTMPIoctlStaticWepCopy(
	IN	PRTMP_ADAPTER	pAd,
	IN	struct iwreq	*wrq);

VOID RTMPIoctlRadiusData(
	IN PRTMP_ADAPTER	pAd,
	IN struct iwreq		*wrq);

VOID RTMPIoctlAddWPAKey(
	IN	PRTMP_ADAPTER	pAd,
	IN	struct iwreq	*wrq);

VOID RTMPIoctlAddPMKIDCache(
	IN	PRTMP_ADAPTER	pAd,
	IN	struct iwreq	*wrq);

#endif // __AP_CFG_H__ //
