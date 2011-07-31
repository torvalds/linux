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
	spectrum_def.h

    Abstract:
    Handle association related requests either from WSTA or from local MLME

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
	Fonchi Wu    2008	  	   created for 802.11h
 */

#ifndef __SPECTRUM_DEF_H__
#define __SPECTRUM_DEF_H__

#define MAX_MEASURE_REQ_TAB_SIZE		32
#define MAX_HASH_MEASURE_REQ_TAB_SIZE	MAX_MEASURE_REQ_TAB_SIZE

#define MAX_TPC_REQ_TAB_SIZE			32
#define MAX_HASH_TPC_REQ_TAB_SIZE		MAX_TPC_REQ_TAB_SIZE

#define MIN_RCV_PWR				100	/* Negative value ((dBm) */

#define TPC_REQ_AGE_OUT			500	/* ms */
#define MQ_REQ_AGE_OUT			500	/* ms */

#define TPC_DIALOGTOKEN_HASH_INDEX(_DialogToken)	((_DialogToken) % MAX_HASH_TPC_REQ_TAB_SIZE)
#define MQ_DIALOGTOKEN_HASH_INDEX(_DialogToken)		((_DialogToken) % MAX_MEASURE_REQ_TAB_SIZE)

struct rt_measure_req_entry;

struct rt_measure_req_entry {
	struct rt_measure_req_entry *pNext;
	unsigned long lastTime;
	BOOLEAN Valid;
	u8 DialogToken;
	u8 MeasureDialogToken[3];	/* 0:basic measure, 1: CCA measure, 2: RPI_Histogram measure. */
};

struct rt_measure_req_tab {
	u8 Size;
	struct rt_measure_req_entry *Hash[MAX_HASH_MEASURE_REQ_TAB_SIZE];
	struct rt_measure_req_entry Content[MAX_MEASURE_REQ_TAB_SIZE];
};

struct rt_tpc_req_entry;

struct rt_tpc_req_entry {
	struct rt_tpc_req_entry *pNext;
	unsigned long lastTime;
	BOOLEAN Valid;
	u8 DialogToken;
};

struct rt_tpc_req_tab {
	u8 Size;
	struct rt_tpc_req_entry *Hash[MAX_HASH_TPC_REQ_TAB_SIZE];
	struct rt_tpc_req_entry Content[MAX_TPC_REQ_TAB_SIZE];
};

/* The regulatory information */
struct rt_dot11_channel_set {
	u8 NumberOfChannels;
	u8 MaxTxPwr;
	u8 ChannelList[16];
};

struct rt_dot11_regulatory_information {
	u8 RegulatoryClass;
	struct rt_dot11_channel_set ChannelSet;
};

#define RM_TPC_REQ				0
#define RM_MEASURE_REQ			1

#define RM_BASIC				0
#define RM_CCA					1
#define RM_RPI_HISTOGRAM		2
#define RM_CH_LOAD				3
#define RM_NOISE_HISTOGRAM		4

struct PACKED rt_tpc_report_info {
	u8 TxPwr;
	u8 LinkMargin;
};

struct PACKED rt_ch_sw_ann_info {
	u8 ChSwMode;
	u8 Channel;
	u8 ChSwCnt;
};

typedef union PACKED _MEASURE_REQ_MODE {
	struct PACKED {
		u8 Parallel:1;
		u8 Enable:1;
		u8 Request:1;
		u8 Report:1;
		u8 DurationMandatory:1;
		 u8:3;
	} field;
	u8 word;
} MEASURE_REQ_MODE, *PMEASURE_REQ_MODE;

struct PACKED rt_measure_req {
	u8 ChNum;
	u64 MeasureStartTime;
	u16 MeasureDuration;
};

struct PACKED rt_measure_req_info {
	u8 Token;
	MEASURE_REQ_MODE ReqMode;
	u8 ReqType;
	u8 Oct[0];
};

typedef union PACKED _MEASURE_BASIC_REPORT_MAP {
	struct PACKED {
		u8 BSS:1;

		u8 OfdmPreamble:1;
		u8 UnidentifiedSignal:1;
		u8 Radar:1;
		u8 Unmeasure:1;
		u8 Rev:3;
	} field;
	u8 word;
} MEASURE_BASIC_REPORT_MAP, *PMEASURE_BASIC_REPORT_MAP;

struct PACKED rt_measure_basic_report {
	u8 ChNum;
	u64 MeasureStartTime;
	u16 MeasureDuration;
	MEASURE_BASIC_REPORT_MAP Map;
};

struct PACKED rt_measure_cca_report {
	u8 ChNum;
	u64 MeasureStartTime;
	u16 MeasureDuration;
	u8 CCA_Busy_Fraction;
};

struct PACKED rt_measure_rpi_report {
	u8 ChNum;
	u64 MeasureStartTime;
	u16 MeasureDuration;
	u8 RPI_Density[8];
};

typedef union PACKED _MEASURE_REPORT_MODE {
	struct PACKED {
		u8 Late:1;
		u8 Incapable:1;
		u8 Refused:1;
		u8 Rev:5;
	} field;
	u8 word;
} MEASURE_REPORT_MODE, *PMEASURE_REPORT_MODE;

struct PACKED rt_measure_report_info {
	u8 Token;
	u8 ReportMode;
	u8 ReportType;
	u8 Octect[0];
};

struct PACKED rt_quiet_info {
	u8 QuietCnt;
	u8 QuietPeriod;
	u16 QuietDuration;
	u16 QuietOffset;
};

#endif /* __SPECTRUM_DEF_H__ // */
