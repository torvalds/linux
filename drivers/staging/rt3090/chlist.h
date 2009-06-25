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
    chlist.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
	Fonchi Wu   2007-12-19    created
*/

#ifndef __CHLIST_H__
#define __CHLIST_H__

#include "rtmp_type.h"
#include "rtmp_def.h"


#define ODOR			0
#define IDOR			1
#define BOTH			2

#define BAND_5G         0
#define BAND_24G        1
#define BAND_BOTH       2

typedef struct _CH_DESP {
	UCHAR FirstChannel;
	UCHAR NumOfCh;
	CHAR MaxTxPwr;			// dBm
	UCHAR Geography;			// 0:out door, 1:in door, 2:both
	BOOLEAN DfsReq;			// Dfs require, 0: No, 1: yes.
} CH_DESP, *PCH_DESP;

typedef struct _CH_REGION {
	UCHAR CountReg[3];
	UCHAR DfsType;			// 0: CE, 1: FCC, 2: JAP, 3:JAP_W53, JAP_W56
	CH_DESP ChDesp[10];
} CH_REGION, *PCH_REGION;

extern CH_REGION ChRegion[];

typedef struct _CH_FREQ_MAP_{
	UINT16		channel;
	UINT16		freqKHz;
}CH_FREQ_MAP;

extern CH_FREQ_MAP CH_HZ_ID_MAP[];
extern int CH_HZ_ID_MAP_NUM;


#define     MAP_CHANNEL_ID_TO_KHZ(_ch, _khz)					\
		do{													\
			int _chIdx;											\
			for (_chIdx = 0; _chIdx < CH_HZ_ID_MAP_NUM; _chIdx++)\
			{													\
				if ((_ch) == CH_HZ_ID_MAP[_chIdx].channel)			\
				{												\
					(_khz) = CH_HZ_ID_MAP[_chIdx].freqKHz * 1000;	\
					break;										\
				}												\
			}													\
			if (_chIdx == CH_HZ_ID_MAP_NUM)					\
				(_khz) = 2412000;									\
            }while(0)

#define     MAP_KHZ_TO_CHANNEL_ID(_khz, _ch)                 \
		do{													\
			int _chIdx;											\
			for (_chIdx = 0; _chIdx < CH_HZ_ID_MAP_NUM; _chIdx++)\
			{													\
				if ((_khz) == CH_HZ_ID_MAP[_chIdx].freqKHz)			\
				{												\
					(_ch) = CH_HZ_ID_MAP[_chIdx].channel;			\
					break;										\
				}												\
			}													\
			if (_chIdx == CH_HZ_ID_MAP_NUM)					\
				(_ch) = 1;											\
		}while(0)


VOID BuildChannelListEx(
	IN PRTMP_ADAPTER pAd);

VOID BuildBeaconChList(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pBuf,
	OUT	PULONG pBufLen);

#ifdef DOT11_N_SUPPORT
VOID N_ChannelCheck(
	IN PRTMP_ADAPTER pAd);

VOID N_SetCenCh(
	IN PRTMP_ADAPTER pAd);
#endif // DOT11_N_SUPPORT //

UINT8 GetCuntryMaxTxPwr(
	IN PRTMP_ADAPTER pAd,
	IN UINT8 channel);

#endif // __CHLIST_H__
