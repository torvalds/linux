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


#ifndef __RT33XX_H__
#define __RT33XX_H__

#ifdef RT33xx

#include "rtmp_type.h"


#ifdef RT3370
extern REG_PAIR RT3370_RFRegTable[];
extern UCHAR RT3370_NUM_RF_REG_PARMS;
#define BW20RFR24	0x4F
#define BW40RFR24	0X68
#define BW20RFR31	0x4F
#define BW40RFR31	0X6F
#endif /* RT3370 */

VOID RT33xx_Init(
        IN struct _RTMP_ADAPTER         *pAd);

VOID RT33xx_ChipSwitchChannel(
	IN struct _RTMP_ADAPTER 	*pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan);

VOID RT33xx_AsicInitDesiredTSSITable(
	IN struct _RTMP_ADAPTER		*pAd);

#endif /* RT33xx */

#endif /*__RT33XX_H__ */

