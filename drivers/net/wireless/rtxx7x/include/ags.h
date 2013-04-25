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


#ifndef __AGS_H__
#define __AGS_H__


extern UCHAR AGS1x1HTRateTable[];

extern UCHAR AGS2x2HTRateTable[];

extern UCHAR AGS3x3HTRateTable[];



#define AGS_TX_QUALITY_WORST_BOUND       8
    
/* */
/* The size, in bytes, of an AGS entry in the rate switch table */
/* */
#define SIZE_OF_AGS_RATE_TABLE_ENTRY	9
    

typedef struct _RTMP_TX_RATE_SWITCH_AGS {
	

	UCHAR	ItemNo;
	
#ifdef RT_BIG_ENDIAN
	UCHAR	Rsv2:2;
	UCHAR	Mode:2;
	UCHAR	Rsv1:1;	
	UCHAR	BW:1;
	UCHAR	ShortGI:1;
	UCHAR	STBC:1;
#else
	UCHAR	STBC:1;
	UCHAR	ShortGI:1;
	UCHAR	BW:1;
	UCHAR	Rsv1:1;
	UCHAR	Mode:2;
	UCHAR	Rsv2:2;
#endif /* RT_BIG_ENDIAN */

	UCHAR	CurrMCS;
	UCHAR	TrainUp;
	UCHAR	TrainDown;
	UCHAR	downMcs;
	UCHAR	upMcs3;
	UCHAR	upMcs2;
	UCHAR	upMcs1;
} RTMP_TX_RATE_SWITCH_AGS, *PRTMP_TX_RATE_SWITCH_AGS;

/* */
/* AGS control */
/* */
typedef struct _AGS_CONTROL {

	UCHAR MCSGroup; /* The MCS group under testing */
	UCHAR lastRateIdx;
} AGS_CONTROL,*PAGS_CONTROL;

/* */
/* The statistics information for AGS */
/* */
typedef struct _AGS_STATISTICS_INFO {

	CHAR	RSSI;
	ULONG	TxErrorRatio;
	ULONG	AccuTxTotalCnt;
	ULONG	TxTotalCnt;
	ULONG	TxSuccess;
	ULONG	TxRetransmit;
	ULONG	TxFailCount;
} AGS_STATISTICS_INFO, *PAGS_STATISTICS_INFO;


/* */
/* Support AGS (Adaptive Group Switching) */
/* */
#define SUPPORT_AGS(__pAd)			(IS_RT3593(__pAd))
    
#define AGS_IS_USING(__pAd, __pRateTable)			\
    (SUPPORT_AGS(__pAd) && \
     ((__pRateTable == AGS1x1HTRateTable) || \
      (__pRateTable == AGS2x2HTRateTable) || \
      (__pRateTable == AGS3x3HTRateTable))) 
 

#endif /* __AGS_H__ */
    
/* End of ags.h */ 
