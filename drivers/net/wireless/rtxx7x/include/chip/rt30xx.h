/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifndef __RT30XX_H__
#define __RT30XX_H__

#ifdef RT30xx

struct _RTMP_ADAPTER;

#include "rtmp_type.h"

extern REG_PAIR RT3020_RFRegTable[];
extern UCHAR NUM_RF_3020_REG_PARMS;

VOID RT30xx_Init(
	IN struct _RTMP_ADAPTER		*pAd);

VOID RT30xx_ChipSwitchChannel(
	IN struct _RTMP_ADAPTER		*pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan);

VOID RT30xx_ChipBBPAdjust(
	IN struct _RTMP_ADAPTER	*pAd);

VOID RT30xx_RTMPSetAGCInitValue(
	IN struct _RTMP_ADAPTER		*pAd,
	IN UCHAR					BandWidth);

#endif /* RT30xx */

#endif /*__RT30XX_H__ */

