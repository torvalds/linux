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


#include "rt_config.h"
#include <stdarg.h>


UCHAR	CISCO_OUI[] = {0x00, 0x40, 0x96};

UCHAR   RALINK_OUI[]  = {0x00, 0x0c, 0x43};
UCHAR	WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
UCHAR	RSN_OUI[] = {0x00, 0x0f, 0xac};
UCHAR	WAPI_OUI[] = {0x00, 0x14, 0x72};
UCHAR   WME_INFO_ELEM[]  = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
UCHAR   WME_PARM_ELEM[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};
UCHAR   BROADCOM_OUI[]  = {0x00, 0x90, 0x4c};
UCHAR   WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};
#ifdef CONFIG_STA_SUPPORT
#ifdef DOT11_N_SUPPORT
UCHAR	PRE_N_HT_OUI[]	= {0x00, 0x90, 0x4c};
#endif /* DOT11_N_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_APSTA_MIXED_SUPPORT
UINT32 CW_MAX_IN_BITS;
#endif /* CONFIG_APSTA_MIXED_SUPPORT */

UCHAR RateSwitchTable[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x11, 0x00,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 35, 45,
    0x03, 0x00,  3, 20, 45,
    0x04, 0x21,  0, 30, 50,
    0x05, 0x21,  1, 20, 50,
    0x06, 0x21,  2, 20, 50,
    0x07, 0x21,  3, 15, 50,
    0x08, 0x21,  4, 15, 30,
    0x09, 0x21,  5, 10, 25,
    0x0a, 0x21,  6,  8, 25,
    0x0b, 0x21,  7,  8, 25,
    0x0c, 0x20, 12,  15, 30,
    0x0d, 0x20, 13,  8, 20,
    0x0e, 0x20, 14,  8, 20,
    0x0f, 0x20, 15,  8, 25,
    0x10, 0x22, 15,  8, 25,
    0x11, 0x00,  0,  0,  0,
    0x12, 0x00,  0,  0,  0,
    0x13, 0x00,  0,  0,  0,
    0x14, 0x00,  0,  0,  0,
    0x15, 0x00,  0,  0,  0,
    0x16, 0x00,  0,  0,  0,
    0x17, 0x00,  0,  0,  0,
    0x18, 0x00,  0,  0,  0,
    0x19, 0x00,  0,  0,  0,
    0x1a, 0x00,  0,  0,  0,
    0x1b, 0x00,  0,  0,  0,
    0x1c, 0x00,  0,  0,  0,
    0x1d, 0x00,  0,  0,  0,
    0x1e, 0x00,  0,  0,  0,
    0x1f, 0x00,  0,  0,  0,
};

UCHAR RateSwitchTable11B[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x04, 0x03,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 35, 45,
    0x03, 0x00,  3, 20, 45,
};

UCHAR RateSwitchTable11BG[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0a, 0x00,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 35, 45,
    0x03, 0x00,  3, 20, 45,
    0x04, 0x10,  2, 20, 35,
    0x05, 0x10,  3, 16, 35,
    0x06, 0x10,  4, 10, 25,
    0x07, 0x10,  5, 16, 25,
    0x08, 0x10,  6, 10, 25,
    0x09, 0x10,  7, 10, 13,
};

UCHAR RateSwitchTable11G[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x08, 0x00,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x10,  0, 20, 101,
    0x01, 0x10,  1, 20, 35,
    0x02, 0x10,  2, 20, 35,
    0x03, 0x10,  3, 16, 35,
    0x04, 0x10,  4, 10, 25,
    0x05, 0x10,  5, 16, 25,
    0x06, 0x10,  6, 10, 25,
    0x07, 0x10,  7, 10, 13,
};

#ifdef DOT11_N_SUPPORT
UCHAR RateSwitchTable11N1S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0c, 0x0a,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 25, 45,
    0x03, 0x21,  0, 20, 35,
    0x04, 0x21,  1, 20, 35,
    0x05, 0x21,  2, 20, 35,
    0x06, 0x21,  3, 15, 35,
    0x07, 0x21,  4, 15, 30,
    0x08, 0x21,  5, 10, 25,
    0x09, 0x21,  6,  8, 14,
    0x0a, 0x21,  7,  8, 14,
    0x0b, 0x23,  7,  8, 14,
};


UCHAR RateSwitchTable11N2S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0e, 0x0c,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 25, 45,
    0x03, 0x21,  0, 20, 35,
    0x04, 0x21,  1, 20, 35,
    0x05, 0x21,  2, 20, 35,
    0x06, 0x21,  3, 15, 35,
    0x07, 0x21,  4, 15, 30,
    0x08, 0x20, 11, 15, 30,
    0x09, 0x20, 12, 15, 30,
    0x0a, 0x20, 13,  8, 20,
    0x0b, 0x20, 14,  8, 20,
    0x0c, 0x20, 15,  8, 25,
    0x0d, 0x22, 15,  8, 15,
};


#ifdef NEW_RATE_ADAPT_SUPPORT
/* 3x3 rate switch table for new rate adaption (default: for good siganl environment (RSSI > -65))*/
/* Target: Good throughput*/

UCHAR RateSwitchTable11N3S[] = {
	/* item no.     mcs   highPERThrd  upMcs3   upMcs1     Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
	/*        mode    lowPERThrd  downMcs   upMcs2*/
    0x19, 0x18,  0,  0,   0,   0,   0,   0,   0,   0,/* Initial used item after association: the number of rate indexes, the initial mcs*/
    0x00, 0x21,  0, 30, 101,   0,  16,   8,   1,   7,/*mcs0*/
    0x01, 0x21,  1, 20,  50,   0,  16,   9,   2,  13,/*mcs1*/
    0x02, 0x21,  2, 20,  50,   1,  17,   9,   3,  20,/*mcs2*/
    0x03, 0x21,  3, 15,  50,   2,  17,  10,   4,  26,/*mcs3*/
    0x04, 0x21,  4, 15,  30,   3,  18,  11,   5,  39,/*mcs4*/
    0x05, 0x21,  5, 10,  25,   4,  18,  12,   6,  52,/*mcs5*/
    0x06, 0x21,  6,  8,  14,   5,  19,  12,   7,  59,/*mcs6*/
    0x07, 0x21,  7,  8,  14,   6,  19,  12,   7,  65,/*mcs7*/
    0x08, 0x20,  8, 30,  50,   0,  16,   9,   2,  13,/*mcs8*/
    0x09, 0x20,  9, 20,  50,   8,  17,  10,   4,  26,/*mcs9*/
    0x0a, 0x20, 10, 20,  50,   9,  18,  11,   5,  39,/*mcs10*/
    0x0b, 0x20, 11, 15,  30,  10,  18,  12,   6,  52,/*mcs11*/
    0x0c, 0x20, 12, 15,  30,  11,  20,  13,  12,  78,/*mcs12*/
    0x0d, 0x20, 13,  8,  20,  12,  20,  14,  13, 104,/*mcs13*/
    0x0e, 0x20, 14,  8,  18,  13,  21,  15,  14, 117,/*mcs14*/
    0x0f, 0x20, 15,  8,  14,  14,  21,  15,  15, 130,/*mcs15*/
    0x10, 0x20, 16, 30,  50,   8,  17,   9,   3,  20,/*mcs16*/
    0x11, 0x20, 17, 20,  50,  16,  18,  11,   5,  39,/*mcs17*/
    0x12, 0x20, 18, 20,  50,  17,  19,  12,   7,  59,/*mcs18*/
    0x13, 0x20, 19, 15,  30,  18,  20,  13,  19,  78,/*mcs19*/
    0x14, 0x20, 20, 15,  30,  19,  21,  15,  20, 117,/*mcs20*/
    0x15, 0x20, 21,  8,  20,  20,  22,  21,  21, 156,/*mcs21*/
    0x16, 0x20, 22,  8,  20,  21,  23,  22,  22, 176,/*mcs22*/
    0x17, 0x20, 23,  6,  18,  22,  24,  23,  23, 196,/*mcs23*/
    0x18, 0x22, 23,  6,  14,  23,  24,  24,  24, 217,/*mcs23+shortGI*/
		};
#else
UCHAR RateSwitchTable11N3S[] = {
/* Item No.	Mode	Curr-MCS	TrainUp	TrainDown	 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x11, 0x0c,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 25, 45,
    0x03, 0x21,  0, 20, 35,
    0x04, 0x21,  1, 20, 35,
    0x05, 0x21,  2, 20, 35,
    0x06, 0x21,  3, 15, 35,
    0x07, 0x21,  4, 15, 30,
    0x08, 0x20, 11, 15, 30,
    0x09, 0x20, 12, 15, 22,
    0x0a, 0x20, 13,  8, 20,
    0x0b, 0x20, 14,  8, 20,
    0x0c, 0x20, 20,  8, 20,
    0x0d, 0x20, 21,  8, 20,
    0x0e, 0x20, 22,  8, 20,
    0x0f, 0x20, 23,  8, 20,
    0x10, 0x22, 23,  8, 15,
};
#endif /* NEW_RATE_ADAPT_SUPPORT */

/* SYNC with Rory!! In order to solve the issue that the throughput drop dramatically at middle-long rage!! */
/* 3x3 rate switch table for new rate adaption (replacement: for bad siganl environment (RSSI < -65))*/
/* Target: Good sensibility*/

UCHAR RateSwitchTable11N3SReplacement[] = {
	/* item no.     mcs   highPERThrd  upMcs3   upMcs1     Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
	/*        mode    lowPERThrd  downMcs   upMcs2*/
	0x19, 0x18,	 0,   0,    0,	 0,   0,   0,  0,   0,	/* Initial used item after association: the number of rate indexes, the initial mcs*/
	0x00, 0x21,	 0,  30,  101,	 0,  16,   8,  1,   7,	/* MCS 0*/
	0x01, 0x21,	 1,  20,   50,	 0,  16,   9,  2,  13,	/* MCS 1*/
	0x02, 0x21,	 2,  20,   50,	 1,  17,   9,  3,  20,	/* MCS 2*/
	0x03, 0x21,	 3,  15,   50,	 2,  17,  10,  4,  26,	/* MCS 3*/
	0x04, 0x21,	 4,  15,   30,	 3,  18,  11,  5,  39,	/* MCS 4*/
	0x05, 0x21,	 5,  10,   25,	 4,  18,  12,  6,  52,	/* MCS 5*/
	0x06, 0x21,	 6,   8,   14,	 5,  19,  12,  7,  59,	/* MCS 6*/
	0x07, 0x21,	 7,   8,   14,	 6,  19,  12,  7,  65,	/* MCS 7*/
	0x08, 0x20,	 8,  30,   50,	 0,  16,   9,  8,  13,	/* MCS 8*/
	0x09, 0x20,	 9,  20,   50,	 8,  17,  10,  9,  26,	/* MCS 9*/
	0x0a, 0x20,	10,  20,   50,	 9,  18,  11, 10,  39,	/* MCS 10*/
	0x0b, 0x20,	11,  15,   30,	10,  18,  12, 11,  52,	/* MCS 11*/
	0x0c, 0x20,	12,  15,   30,	11,  20,  13, 12,  78,	/* MCS 12*/
	0x0d, 0x20,	13,   8,   20,	12,  20,  14, 13, 104,	/* MCS 13*/
	0x0e, 0x20,	14,   8,   18,	13,  21,  15, 14, 117,	/* MCS 14*/
	0x0f, 0x20,	15,   8,   14,	14,  21,  15, 15, 130,	/* MCS 15*/
	0x10, 0x20,	16,  30,   50,	 8,  17,  16, 16,  20,	/* MCS 16*/
	0x11, 0x20,	17,  20,   50,	16,  18,  17, 17,  39,	/* MCS 17*/
	0x12, 0x20,	18,  20,   50,	17,  19,  18, 18,  59,	/* MCS 18*/
	0x13, 0x20,	19,  15,   30,	18,  20,  19, 19,  78,	/* MCS 19*/
	0x14, 0x20,	20,  15,   30,	19,  21,  20, 20, 117,	/* MCS 20*/
	0x15, 0x20,	21,   8,   20,	20,  22,  21, 21, 156,	/* MCS 21*/
	0x16, 0x20,	22,   8,   20,	21,  23,  22, 22, 176,	/* MCS 22*/
	0x17, 0x20,	23,   6,   18,	22,  24,  23, 23, 196,	/* MCS 23*/
	0x18, 0x22,	23,   6,   14,	23,  24,  24, 24, 217,	/* MCS 23 + Short GI*/
};

UCHAR RateSwitchTable11N2SForABand[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0b, 0x09,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x21,  0, 30, 101,
    0x01, 0x21,  1, 20, 50,
    0x02, 0x21,  2, 20, 50,
    0x03, 0x21,  3, 15, 50,
    0x04, 0x21,  4, 15, 30,
    0x05, 0x21,  5, 15, 30,
    0x06, 0x20, 12,  15, 30,
    0x07, 0x20, 13,  8, 20,
    0x08, 0x20, 14,  8, 20,
    0x09, 0x20, 15,  8, 25,
    0x0a, 0x22, 15,  8, 25,
};

UCHAR RateSwitchTable11N3SForABand[] = { /* 3*3*/
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0e, 0x09,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x21,  0, 30, 101,
    0x01, 0x21,  1, 20, 50,
    0x02, 0x21,  2, 20, 50,
    0x03, 0x21,  3, 15, 50,
    0x04, 0x21,  4, 15, 30,
    0x05, 0x21,  5, 15, 30,
    0x06, 0x20, 12,  15, 30,
    0x07, 0x20, 13,  8, 20,
    0x08, 0x20, 14,  8, 20,
    0x09, 0x20, 15,  8, 25,
    /*0x0a, 0x22, 15,  8, 25,*/
    /*0x0a, 0x20, 20, 15, 30,*/
    0x0a, 0x20, 21,  8, 20,
    0x0b, 0x20, 22,  8, 20,
    0x0c, 0x20, 23,  8, 25,
    0x0d, 0x22, 23,  8, 25,
};

UCHAR RateSwitchTable11BGN1S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0c, 0x0a,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 25, 45,
    0x03, 0x21,  0, 20, 35,
    0x04, 0x21,  1, 20, 35,
    0x05, 0x21,  2, 20, 35,
    0x06, 0x21,  3, 15, 35,
    0x07, 0x21,  4, 15, 30,
    0x08, 0x21,  5, 10, 25,
    0x09, 0x21,  6,  8, 14,
    0x0a, 0x21,  7,  8, 14,
    0x0b, 0x23,  7,  8, 14,
};


UCHAR RateSwitchTable11BGN2S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0e, 0x0c,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x00,  0, 40, 101,
    0x01, 0x00,  1, 40, 50,
    0x02, 0x00,  2, 25, 45,
    0x03, 0x21,  0, 20, 35,
    0x04, 0x21,  1, 20, 35,
    0x05, 0x21,  2, 20, 35,
    0x06, 0x21,  3, 15, 35,
    0x07, 0x21,  4, 15, 30,
    0x08, 0x20, 11, 15, 30,
    0x09, 0x20, 12, 15, 22,
    0x0a, 0x20, 13,  8, 20,
    0x0b, 0x20, 14,  8, 20,
    0x0c, 0x20, 15,  8, 20,
    0x0d, 0x22, 15,  8, 15,
};


UCHAR RateSwitchTable11BGN3S[] = { /* 3*3*/
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0e, 0x00,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x21,  0, 30,101,	/*50*/
    0x01, 0x21,  1, 20, 50,
    0x02, 0x21,  2, 20, 50,
    0x03, 0x21,  3, 20, 50,
    0x04, 0x21,  4, 15, 50,
    0x05, 0x20, 11, 15, 30,
    0x06, 0x20, 12, 15, 30,
    0x07, 0x20, 13,  8, 20,
    0x08, 0x20, 14,  8, 20,
    0x09, 0x20, 15,  8, 25,
    /*0x0a, 0x20, 20, 15, 30,*/
    0x0a, 0x20, 21,  8, 20,
    0x0b, 0x20, 22,  8, 20,
    0x0c, 0x20, 23,  8, 25,
    0x0d, 0x22, 23,  8, 25,
};


UCHAR RateSwitchTable11BGN2SForABand[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0b, 0x09,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x21,  0, 30,101,	/*50*/
    0x01, 0x21,  1, 20, 50,
    0x02, 0x21,  2, 20, 50,
    0x03, 0x21,  3, 15, 50,
    0x04, 0x21,  4, 15, 30,
    0x05, 0x21,  5, 15, 30,
    0x06, 0x20, 12, 15, 30,
    0x07, 0x20, 13,  8, 20,
    0x08, 0x20, 14,  8, 20,
    0x09, 0x20, 15,  8, 25,
    0x0a, 0x22, 15,  8, 25,
};


UCHAR RateSwitchTable11BGN3SForABand[] = { /* 3*3*/
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown		 Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF)*/
    0x0e, 0x09,  0,  0,  0,						/* Initial used item after association*/
    0x00, 0x21,  0, 30,101,	/*50*/
    0x01, 0x21,  1, 20, 50,
    0x02, 0x21,  2, 20, 50,
    0x03, 0x21,  3, 15, 50,
    0x04, 0x21,  4, 15, 30,
    0x05, 0x21,  5, 15, 30,
    0x06, 0x20, 12, 15, 30,
    0x07, 0x20, 13,  8, 20,
    0x08, 0x20, 14,  8, 20,
    0x09, 0x20, 15,  8, 25,
    /*0x0a, 0x22, 15,  8, 25,*/
    /*0x0a, 0x20, 20, 15, 30,*/
    0x0a, 0x20, 21,  8, 20,
    0x0b, 0x20, 22,  8, 20,
    0x0c, 0x20, 23,  8, 25,
    0x0d, 0x22, 23,  8, 25,
};


#endif /* DOT11_N_SUPPORT */


extern UCHAR	 OfdmRateToRxwiMCS[];
/* since RT61 has better RX sensibility, we have to limit TX ACK rate not to exceed our normal data TX rate.*/
/* otherwise the WLAN peer may not be able to receive the ACK thus downgrade its data TX rate*/
ULONG BasicRateMask[12]				= {0xfffff001 /* 1-Mbps */, 0xfffff003 /* 2 Mbps */, 0xfffff007 /* 5.5 */, 0xfffff00f /* 11 */,
									  0xfffff01f /* 6 */	 , 0xfffff03f /* 9 */	  , 0xfffff07f /* 12 */ , 0xfffff0ff /* 18 */,
									  0xfffff1ff /* 24 */	 , 0xfffff3ff /* 36 */	  , 0xfffff7ff /* 48 */ , 0xffffffff /* 54 */};

UCHAR BROADCAST_ADDR[MAC_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
UCHAR ZERO_MAC_ADDR[MAC_ADDR_LEN]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* e.g. RssiSafeLevelForTxRate[RATE_36]" means if the current RSSI is greater than*/
/*		this value, then it's quaranteed capable of operating in 36 mbps TX rate in*/
/*		clean environment.*/
/*								  TxRate: 1   2   5.5	11	 6	  9    12	18	 24   36   48	54	 72  100*/
CHAR RssiSafeLevelForTxRate[] ={  -92, -91, -90, -87, -88, -86, -85, -83, -81, -78, -72, -71, -40, -40 };

UCHAR  RateIdToMbps[]	 = { 1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54, 72, 100};
USHORT RateIdTo500Kbps[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108, 144, 200};

UCHAR  SsidIe	 = IE_SSID;
UCHAR  SupRateIe = IE_SUPP_RATES;
UCHAR  ExtRateIe = IE_EXT_SUPP_RATES;
#ifdef DOT11_N_SUPPORT
UCHAR  HtCapIe = IE_HT_CAP;
UCHAR  AddHtInfoIe = IE_ADD_HT;
UCHAR  NewExtChanIe = IE_SECONDARY_CH_OFFSET;
UCHAR  BssCoexistIe = IE_2040_BSS_COEXIST;
UCHAR  ExtHtCapIe = IE_EXT_CAPABILITY;
#endif /* DOT11_N_SUPPORT */
UCHAR  ExtCapIe = IE_EXT_CAPABILITY;
UCHAR  ErpIe	 = IE_ERP;
UCHAR  DsIe 	 = IE_DS_PARM;
UCHAR  TimIe	 = IE_TIM;
UCHAR  WpaIe	 = IE_WPA;
UCHAR  Wpa2Ie	 = IE_WPA2;
UCHAR  IbssIe	 = IE_IBSS_PARM;
UCHAR  WapiIe	 = IE_WAPI;

extern UCHAR	WPA_OUI[];

UCHAR	SES_OUI[] = {0x00, 0x90, 0x4c};

UCHAR	ZeroSsid[32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};


#ifdef INF_AMAZON_SE	
UINT16 MaxBulkOutsSizeLimit[5][4] =
{
	/* Priority high -> low*/
	{ 24576, 2048, 2048, 2048 },	/* 0 AC	*/
	{ 24576, 2048, 2048, 2048 },	/* 1 AC	 */
	{ 24576, 2048, 2048, 2048 }, 	/* 2 ACs*/
	{ 24576, 6144, 2048, 2048 }, 	/* 3 ACs*/
	{ 24576, 6144, 4096, 2048 }		/* 4 ACs*/
};

VOID SoftwareFlowControl(
	IN PRTMP_ADAPTER pAd) 
{

		BOOLEAN ResetBulkOutSize=FALSE;
		UCHAR i=0,RunningQueueNo=0,QueIdx=0,HighWorkingAcCount=0;
		UINT PacketsInQueueSize=0;
		UCHAR Priority[]={1,0,2,3};
		
		for (i=0;i<NUM_OF_TX_RING;i++)
		{

			if (pAd->TxContext[i].CurWritePosition>=pAd->TxContext[i].NextBulkOutPosition)
			{
				PacketsInQueueSize=pAd->TxContext[i].CurWritePosition-pAd->TxContext[i].NextBulkOutPosition;
			}
			else 
			{
				PacketsInQueueSize=MAX_TXBULK_SIZE-pAd->TxContext[i].NextBulkOutPosition+pAd->TxContext[i].CurWritePosition;
			}		

			if (pAd->BulkOutDataSizeCount[i]>20480 || PacketsInQueueSize>6144)
			{
				RunningQueueNo++;
				pAd->BulkOutDataFlag[i]=TRUE;
			}
			else
				pAd->BulkOutDataFlag[i]=FALSE;

			pAd->BulkOutDataSizeCount[i]=0;
		}

		if (RunningQueueNo>pAd->LastRunningQueueNo)
		{
			DBGPRINT(RT_DEBUG_INFO,("SoftwareFlowControl  reset %d > %d \n",RunningQueueNo,pAd->LastRunningQueueNo));
			
ResetBulkOutSize=TRUE;
			 pAd->RunningQueueNoCount=0;
			 pAd->LastRunningQueueNo=RunningQueueNo;
		}
		else if (RunningQueueNo==pAd->LastRunningQueueNo)
		{
pAd->RunningQueueNoCount=0;
		}
		else if (RunningQueueNo<pAd->LastRunningQueueNo)
		{
			DBGPRINT(RT_DEBUG_INFO,("SoftwareFlowControl  reset %d < %d \n",RunningQueueNo,pAd->LastRunningQueueNo));
			pAd->RunningQueueNoCount++;
			if (pAd->RunningQueueNoCount>=6)
			{
				ResetBulkOutSize=TRUE;
				pAd->RunningQueueNoCount=0;
				pAd->LastRunningQueueNo=RunningQueueNo;
			}
		}

		if (ResetBulkOutSize==TRUE)
		{
			for (QueIdx=0;QueIdx<NUM_OF_TX_RING;QueIdx++)
			{
				HighWorkingAcCount=0;
				for (i=0;i<NUM_OF_TX_RING;i++)
				{
					if (QueIdx==i)
						continue;

					if (pAd->BulkOutDataFlag[i]==TRUE && Priority[i]>Priority[QueIdx])
							HighWorkingAcCount++;
					
				}
				pAd->BulkOutDataSizeLimit[QueIdx]=MaxBulkOutsSizeLimit[RunningQueueNo][HighWorkingAcCount];
			}

				DBGPRINT(RT_DEBUG_TRACE, ("Reset bulkout size AC0(BE):%7d AC1(BK):%7d AC2(VI):%7d AC3(VO):%7d %d\n",pAd->BulkOutDataSizeLimit[0]
				,pAd->BulkOutDataSizeLimit[1]
				,pAd->BulkOutDataSizeLimit[2]
				,pAd->BulkOutDataSizeLimit[3]
				,RunningQueueNo));			
		}

}
#endif /* INF_AMAZON_SE */

/*
	==========================================================================
	Description:
		initialize the MLME task and its data structure (queue, spinlock, 
		timer, state machines).

	IRQL = PASSIVE_LEVEL

	Return:
		always return NDIS_STATUS_SUCCESS

	==========================================================================
*/
NDIS_STATUS MlmeInit(
	IN PRTMP_ADAPTER pAd) 
{
	NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

	DBGPRINT(RT_DEBUG_TRACE, ("--> MLME Initialize\n"));

	do 
	{
		Status = MlmeQueueInit(pAd, &pAd->Mlme.Queue);
		if(Status != NDIS_STATUS_SUCCESS) 
			break;

		pAd->Mlme.bRunning = FALSE;
		NdisAllocateSpinLock(pAd, &pAd->Mlme.TaskLock);

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			BssTableInit(&pAd->ScanTab);

			/* init STA state machines*/
			AssocStateMachineInit(pAd, &pAd->Mlme.AssocMachine, pAd->Mlme.AssocFunc);
			AuthStateMachineInit(pAd, &pAd->Mlme.AuthMachine, pAd->Mlme.AuthFunc);
			AuthRspStateMachineInit(pAd, &pAd->Mlme.AuthRspMachine, pAd->Mlme.AuthRspFunc);
			SyncStateMachineInit(pAd, &pAd->Mlme.SyncMachine, pAd->Mlme.SyncFunc);

#ifdef QOS_DLS_SUPPORT
			DlsStateMachineInit(pAd, &pAd->Mlme.DlsMachine, pAd->Mlme.DlsFunc);
#endif /* QOS_DLS_SUPPORT */




			/* Since we are using switch/case to implement it, the init is different from the above */
			/* state machine init*/
			MlmeCntlInit(pAd, &pAd->Mlme.CntlMachine, NULL);

#ifdef PCIE_PS_SUPPORT
			if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE))
			{
			    /* only PCIe cards need these two timers*/
				RTMPInitTimer(pAd, &pAd->Mlme.PsPollTimer, GET_TIMER_FUNCTION(PsPollWakeExec), pAd, FALSE);
				RTMPInitTimer(pAd, &pAd->Mlme.RadioOnOffTimer, GET_TIMER_FUNCTION(RadioOnExec), pAd, FALSE);
			}
#endif /* PCIE_PS_SUPPORT */

			RTMPInitTimer(pAd, &pAd->Mlme.LinkDownTimer, GET_TIMER_FUNCTION(LinkDownExec), pAd, FALSE);
			RTMPInitTimer(pAd, &pAd->StaCfg.StaQuickResponeForRateUpTimer, GET_TIMER_FUNCTION(StaQuickResponeForRateUpExec), pAd, FALSE);
			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;
			RTMPInitTimer(pAd, &pAd->StaCfg.WpaDisassocAndBlockAssocTimer, GET_TIMER_FUNCTION(WpaDisassocApAndBlockAssoc), pAd, FALSE);

#ifdef RTMP_MAC_USB
			RTMPInitTimer(pAd, &pAd->Mlme.AutoWakeupTimer, GET_TIMER_FUNCTION(RtmpUsbStaAsicForceWakeupTimeout), pAd, FALSE);
			pAd->Mlme.AutoWakeupTimerRunning = FALSE;
#endif /* RTMP_MAC_USB */


		}
#endif /* CONFIG_STA_SUPPORT */
		


		WpaStateMachineInit(pAd, &pAd->Mlme.WpaMachine, pAd->Mlme.WpaFunc);


		ActionStateMachineInit(pAd, &pAd->Mlme.ActMachine, pAd->Mlme.ActFunc);

		/* Init mlme periodic timer*/
		RTMPInitTimer(pAd, &pAd->Mlme.PeriodicTimer, GET_TIMER_FUNCTION(MlmePeriodicExec), pAd, TRUE);

		/* Set mlme periodic timer*/
		RTMPSetTimer(&pAd->Mlme.PeriodicTimer, MLME_TASK_EXEC_INTV);

		/* software-based RX Antenna diversity*/
		RTMPInitTimer(pAd, &pAd->Mlme.RxAntEvalTimer, GET_TIMER_FUNCTION(AsicRxAntEvalTimeout), pAd, FALSE);



	} while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE, ("<-- MLME Initialize\n"));

	return Status;
}
#ifdef RTMP_TEMPERATURE_COMPENSATION
VOID InitLookupTable(
	IN PRTMP_ADAPTER pAd)
{
	int Idx, IdxTmp;
	int i;

	const int Offset = 7;

	EEPROM_WORD_STRUC WordStruct = {{0}};
	UCHAR PlusStepNum[8] = {0, 1, 3, 2, 3, 3, 3, 2};
	UCHAR MinusStepNum[8] = {1, 1, 1, 1, 1, 1, 0, 1};
	UCHAR Step = 10;
	UCHAR TssiGain = 0;
	UCHAR RFValue = 0, BbpValue = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("==> InitLookupTable\n"));

/* rt5392a */
	
	/* Read from EEPROM, as parameters for lookup table */
	
	RT28xx_EEPROM_READ16(pAd, 0x6e, WordStruct.word);
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] EEPROM 6e = %x\n", WordStruct.word));
	PlusStepNum[0] = (WordStruct.field.Byte0 & 0x0F);
	PlusStepNum[1] = (((WordStruct.field.Byte0 & 0xF0) >> 4) & 0x0F);
	PlusStepNum[2] = (WordStruct.field.Byte1 & 0x0F);
	PlusStepNum[3] = (((WordStruct.field.Byte1 & 0xF0) >> 4) & 0x0F);

	RT28xx_EEPROM_READ16(pAd, 0x70, WordStruct.word);
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] EEPROM 70 = %x\n", WordStruct.word));
	PlusStepNum[4] = (WordStruct.field.Byte0 & 0x0F);
	PlusStepNum[5] = (((WordStruct.field.Byte0 & 0xF0) >> 4) & 0x0F);
	PlusStepNum[6] = (WordStruct.field.Byte1 & 0x0F);
	PlusStepNum[7] = (((WordStruct.field.Byte1 & 0xF0) >> 4) & 0x0F);

	RT28xx_EEPROM_READ16(pAd, 0x72, WordStruct.word);
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] EEPROM 72 = %x\n", WordStruct.word));
	MinusStepNum[0] = (WordStruct.field.Byte0 & 0x0F);
	MinusStepNum[1] = (((WordStruct.field.Byte0 & 0xF0) >> 4) & 0x0F);
	MinusStepNum[2] = (WordStruct.field.Byte1 & 0x0F);
	MinusStepNum[3] = (((WordStruct.field.Byte1 & 0xF0) >> 4) & 0x0F);

	RT28xx_EEPROM_READ16(pAd, 0x74, WordStruct.word);
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] EEPROM 74 = %x\n", WordStruct.word));
	MinusStepNum[4] = (WordStruct.field.Byte0 & 0x0F);
	MinusStepNum[5] = (((WordStruct.field.Byte0 & 0xF0) >> 4) & 0x0F);
	MinusStepNum[6] = (WordStruct.field.Byte1 & 0x0F);
	MinusStepNum[7] = (((WordStruct.field.Byte1 & 0xF0) >> 4) & 0x0F);

	RT28xx_EEPROM_READ16(pAd, 0x76, WordStruct.word);
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] EEPROM 76 = %x\n", WordStruct.word));
	TssiGain = (WordStruct.field.Byte0 & 0x0F);
	Step = (WordStruct.field.Byte0 >> 4);
	pAd->TxPowerCtrl.RefTempG = (CHAR)WordStruct.field.Byte1;

	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Plus = %u %u %u %u %u %u %u %u\n",
		PlusStepNum[0],
		PlusStepNum[1],
		PlusStepNum[2],
		PlusStepNum[3],
		PlusStepNum[4],
		PlusStepNum[5],
		PlusStepNum[6],
		PlusStepNum[7]
		));
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Minus = %u %u %u %u %u %u %u %u\n",
		MinusStepNum[0],
		MinusStepNum[1],
		MinusStepNum[2],
		MinusStepNum[3],
		MinusStepNum[4],
		MinusStepNum[5],
		MinusStepNum[6],
		MinusStepNum[7]
		));
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] tssi gain/step = %u\n", TssiGain));
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Step = %u\n", Step));
	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] RefTempG = %d\n", pAd->TxPowerCtrl.RefTempG));

	/* positive */
	i = 0;
	IdxTmp = 1;

	pAd->TxPowerCtrl.LookupTable[1 + Offset] = Step / 2;
	pAd->TxPowerCtrl.LookupTable[0 + Offset] = pAd->TxPowerCtrl.LookupTable[1 + Offset] - Step;
	for (Idx = 2; Idx < 26;)/* Idx++ )*/
	{
		if (PlusStepNum[i] != 0 || i >= 8)
		{
			if (Idx >= IdxTmp + PlusStepNum[i] && i < 8)
			{
				pAd->TxPowerCtrl.LookupTable[Idx + Offset] = pAd->TxPowerCtrl.LookupTable[Idx - 1 + Offset] + (Step - (i+1) + 1);
				IdxTmp = IdxTmp + PlusStepNum[i];
				i += 1;
			}
			else
			{
				pAd->TxPowerCtrl.LookupTable[Idx + Offset] = pAd->TxPowerCtrl.LookupTable[Idx - 1 + Offset] + (Step - (i+1) + 1);
			}
			Idx++;
		}
		else
		{
			i += 1;
		}
	}

	/* negative */
	i = 0;
	IdxTmp = 1;
	for (Idx = 1; Idx < 8;)/* Idx++ )*/
	{
		if (MinusStepNum[i] != 0 || i >= 8)
		{
			if ((Idx + 1) >= IdxTmp + MinusStepNum[i] && i < 8)
			{
				pAd->TxPowerCtrl.LookupTable[-Idx + Offset] = pAd->TxPowerCtrl.LookupTable[-Idx + 1 + Offset] - (Step + (i+1) - 1);
				IdxTmp = IdxTmp + MinusStepNum[i];
				i += 1;
			}
			else
			{
				pAd->TxPowerCtrl.LookupTable[-Idx + Offset] = pAd->TxPowerCtrl.LookupTable[-Idx + 1 + Offset] - (Step + (i+1) - 1);
			}
			Idx++;
		}
		else
		{
			i += 1;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Lookup table as below:\n"));
	for (Idx = 0; Idx < 33; Idx++)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] %d, %d\n", Idx - Offset, pAd->TxPowerCtrl.LookupTable[Idx]));
	}

	
	/* Set BBP_R47 */
	
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpValue);

	/* bit3 = 0 */
	BbpValue = (BbpValue & 0xf7);

	if (pAd->CommonCfg.TempComp == 1)
	{
		/* bit7, bit4 = 1 */
		BbpValue = (BbpValue | 0x90);
	}
	else if (pAd->CommonCfg.TempComp == 2)
	{
		/* bit7 = 1, bit4 = 0 */
		BbpValue = (BbpValue | 0x80);
	}
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpValue);

	
	/*  Set RF_R27 */
	
	RT30xxReadRFRegister(pAd, RF_R27, &RFValue);

	/* Set [3:0] to TssiGain */
	RFValue = (RFValue & 0xf0);
	RFValue = (RFValue | TssiGain);

	/* Set [7:6] to 01. For method 2, it is set at initialization. */
	if (pAd->CommonCfg.TempComp == 2)
	{
		RFValue = (RFValue & 0x7f);
		RFValue = (RFValue | 0x40);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Set RF_R27 to 0x%x\n", RFValue));
	RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
}
#endif /* RTMP_TEMPERATURE_COMPENSATION */
/*
	==========================================================================
	Description:
		main loop of the MLME
	Pre:
		Mlme has to be initialized, and there are something inside the queue
	Note:
		This function is invoked from MPSetInformation and MPReceive;
		This task guarantee only one MlmeHandler will run. 

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID MlmeHandler(
	IN PRTMP_ADAPTER pAd) 
{
	MLME_QUEUE_ELEM 	   *Elem = NULL;
#ifdef APCLI_SUPPORT
	SHORT apcliIfIndex;
#endif /* APCLI_SUPPORT */

	/* Only accept MLME and Frame from peer side, no other (control/data) frame should*/
	/* get into this state machine*/

	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	if(pAd->Mlme.bRunning) 
	{
		NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
		return;
	} 
	else 
	{
		pAd->Mlme.bRunning = TRUE;
	}
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);

	while (!MlmeQueueEmpty(&pAd->Mlme.Queue)) 
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_MLME_RESET_IN_PROGRESS) ||
			RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS) ||
			RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Device Halted or Removed or MlmeRest, exit MlmeHandler! (queue num = %ld)\n", pAd->Mlme.Queue.Num));
			break;
		}
		
#ifdef RALINK_ATE			
		if(ATE_ON(pAd))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("The driver is in ATE mode now in MlmeHandler\n"));
			break;
		}	
#endif /* RALINK_ATE */

		/*From message type, determine which state machine I should drive*/
		if (MlmeDequeue(&pAd->Mlme.Queue, &Elem)) 
		{
#ifdef RTMP_MAC_USB
			if (Elem->MsgType == MT2_RESET_CONF)
			{
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("!!! reset MLME state machine !!!\n"));
				MlmeRestartStateMachine(pAd);
				Elem->Occupied = FALSE;
				Elem->MsgLen = 0;
				continue;
			}
#endif /* RTMP_MAC_USB */

			/* if dequeue success*/
			switch (Elem->Machine) 
			{
				/* STA state machines*/
#ifdef CONFIG_STA_SUPPORT
				case ASSOC_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.AssocMachine,
										Elem, pAd->Mlme.AssocMachine.CurrState);
					break;

				case AUTH_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.AuthMachine,
										Elem, pAd->Mlme.AuthMachine.CurrState);
					break;

				case AUTH_RSP_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.AuthRspMachine,
										Elem, pAd->Mlme.AuthRspMachine.CurrState);
					break;

				case SYNC_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.SyncMachine,
										Elem, pAd->Mlme.SyncMachine.CurrState);
					break;

				case MLME_CNTL_STATE_MACHINE:
					MlmeCntlMachinePerformAction(pAd, &pAd->Mlme.CntlMachine, Elem);
					break;

				case WPA_PSK_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.WpaPskMachine,
										Elem, pAd->Mlme.WpaPskMachine.CurrState);
					break;	

#ifdef QOS_DLS_SUPPORT
				case DLS_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.DlsMachine,
										Elem, pAd->Mlme.DlsMachine.CurrState);
					break;
#endif /* QOS_DLS_SUPPORT */




#endif /* CONFIG_STA_SUPPORT */						

				case ACTION_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.ActMachine,
										Elem, pAd->Mlme.ActMachine.CurrState);
					break;	

				case WPA_STATE_MACHINE:
					StateMachinePerformAction(pAd, &pAd->Mlme.WpaMachine, Elem, pAd->Mlme.WpaMachine.CurrState);
					break;


				default:
					DBGPRINT(RT_DEBUG_TRACE, ("ERROR: Illegal machine %ld in MlmeHandler()\n", Elem->Machine));
					break;
			} /* end of switch*/

			/* free MLME element*/
			Elem->Occupied = FALSE;
			Elem->MsgLen = 0;

		}
		else {
			DBGPRINT_ERR(("MlmeHandler: MlmeQueue empty\n"));
		}
	}

	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	pAd->Mlme.bRunning = FALSE;
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
}

/*
	==========================================================================
	Description:
		Destructor of MLME (Destroy queue, state machine, spin lock and timer)
	Parameters:
		Adapter - NIC Adapter pointer
	Post:
		The MLME task will no longer work properly

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
VOID MlmeHalt(
	IN PRTMP_ADAPTER pAd) 
{
	BOOLEAN 	  Cancelled;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeHalt\n"));

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
	{
		/* disable BEACON generation and other BEACON related hardware timers*/
		AsicDisableSync(pAd);
	}

	RTMPCancelTimer(&pAd->Mlme.PeriodicTimer,		&Cancelled);

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
#ifdef QOS_DLS_SUPPORT
		UCHAR		i;
#endif /* QOS_DLS_SUPPORT */
		/* Cancel pending timers*/
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer,		&Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer,		&Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer,	&Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.AuthTimer,		&Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer,		&Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer,		&Cancelled);


#ifdef PCIE_PS_SUPPORT
	    if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE)
			&&(pAd->StaCfg.PSControl.field.EnableNewPS == TRUE))
	    {
	   	    RTMPCancelTimer(&pAd->Mlme.PsPollTimer,		&Cancelled);
		    RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer,		&Cancelled);
		}
#endif /* PCIE_PS_SUPPORT */

#ifdef QOS_DLS_SUPPORT
		for (i=0; i<MAX_NUM_OF_DLS_ENTRY; i++)
		{
			RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &Cancelled);
		}
#endif /* QOS_DLS_SUPPORT */
		RTMPCancelTimer(&pAd->Mlme.LinkDownTimer,		&Cancelled);

#ifdef RTMP_MAC_USB
		if (pAd->Mlme.AutoWakeupTimerRunning)
		{
			RTMPCancelTimer(&pAd->Mlme.AutoWakeupTimer, &Cancelled);
			pAd->Mlme.AutoWakeupTimerRunning = FALSE;
		}
#endif /* RTMP_MAC_USB */



		if (pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
		{
			RTMPCancelTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, &Cancelled);
			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;
		}
		RTMPCancelTimer(&pAd->StaCfg.WpaDisassocAndBlockAssocTimer, &Cancelled);
	}
#endif /* CONFIG_STA_SUPPORT */

	RTMPCancelTimer(&pAd->Mlme.RxAntEvalTimer,		&Cancelled);





	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
	{
		RTMP_CHIP_OP *pChipOps = &pAd->chipOps;
		
#ifdef LED_CONTROL_SUPPORT		
		/* Set LED*/
		RTMPSetLED(pAd, LED_HALT);
		RTMPSetSignalLED(pAd, -100);	/* Force signal strength Led to be turned off, firmware is not done it.*/
#ifdef RTMP_MAC_USB
		{
			LED_CFG_STRUC LedCfg;
			RTMP_IO_READ32(pAd, LED_CFG, &LedCfg.word);
			LedCfg.field.LedPolar = 0;
			LedCfg.field.RLedMode = 0;
			LedCfg.field.GLedMode = 0;
			LedCfg.field.YLedMode = 0;
			RTMP_IO_WRITE32(pAd, LED_CFG, LedCfg.word);
		}
#endif /* RTMP_MAC_USB */
#endif /* LED_CONTROL_SUPPORT */

		if (pChipOps->AsicHaltAction)
			pChipOps->AsicHaltAction(pAd);
	}

	RTMPusecDelay(5000);    /*  5 msec to gurantee Ant Diversity timer canceled*/

	MlmeQueueDestroy(&pAd->Mlme.Queue);
	NdisFreeSpinLock(&pAd->Mlme.TaskLock);

	DBGPRINT(RT_DEBUG_TRACE, ("<== MlmeHalt\n"));
}

VOID MlmeResetRalinkCounters(
	IN  PRTMP_ADAPTER   pAd)
{
	pAd->RalinkCounters.LastOneSecRxOkDataCnt = pAd->RalinkCounters.OneSecRxOkDataCnt;

#ifdef RALINK_ATE
	if (!ATE_ON(pAd))
#endif /* RALINK_ATE */
		/* for performace enchanement */
		NdisZeroMemory(&pAd->RalinkCounters,
						(UINT32)&pAd->RalinkCounters.OneSecEnd -
						(UINT32)&pAd->RalinkCounters.OneSecStart);

	return;
}


/*
	==========================================================================
	Description:
		This routine is executed periodically to -
		1. Decide if it's a right time to turn on PwrMgmt bit of all 
		   outgoiing frames
		2. Calculate ChannelQuality based on statistics of the last
		   period, so that TX rate won't toggling very frequently between a 
		   successful TX and a failed TX.
		3. If the calculated ChannelQuality indicated current connection not 
		   healthy, then a ROAMing attempt is tried here.

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
#define ADHOC_BEACON_LOST_TIME		(8*OS_HZ)  /* 8 sec*/
VOID MlmePeriodicExec(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	ULONG			TxTotalCnt;
	PRTMP_ADAPTER	pAd = (RTMP_ADAPTER *)FunctionContext;

	/* No More 0x84 MCU CMD from v.30 FW*/

#ifdef INF_AMAZON_SE
	SoftwareFlowControl(pAd);
#endif /* INF_AMAZON_SE */

#ifdef CONFIG_STA_SUPPORT

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		RTMP_MLME_PRE_SANITY_CHECK(pAd);
	}

#ifdef RTMP_MAC_USB
	/* Only when count down to zero, can we go to sleep mode.*/
	/* Count down counter is set after link up. So within 10 seconds after link up, we never go to sleep.*/
	/* 10 seconds period, we can get IP, finish 802.1x authenticaion. and some critical , timing protocol.*/
	if (pAd->CountDowntoPsm > 0)
	{
		pAd->CountDowntoPsm--;
	}

#endif /* RTMP_MAC_USB */

#endif /* CONFIG_STA_SUPPORT */

	/* Do nothing if the driver is starting halt state.*/
	/* This might happen when timer already been fired before cancel timer with mlmehalt*/
	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_HALT_IN_PROGRESS |
								fRTMP_ADAPTER_RADIO_OFF |
								fRTMP_ADAPTER_RADIO_MEASUREMENT |
								fRTMP_ADAPTER_RESET_IN_PROGRESS |
								fRTMP_ADAPTER_NIC_NOT_EXIST))))
		return;

#ifdef RALINK_ATE
	/* Do not show RSSI until "Normal 1 second Mlme PeriodicExec". */
	if (ATE_ON(pAd))
	{
		if (pAd->Mlme.PeriodicRound % MLME_TASK_EXEC_MULTIPLE != (MLME_TASK_EXEC_MULTIPLE - 1))
	{
			pAd->Mlme.PeriodicRound ++;
			return;
		}
	}
#endif /* RALINK_ATE */

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* Do nothing if monitor mode is on*/
		if (MONITOR_ON(pAd))
			return;

		if (pAd->Mlme.PeriodicRound & 0x1)
		{
			/* This is the fix for wifi 11n extension channel overlapping test case.  for 2860D*/
			if (((pAd->MACVersion & 0xffff) == 0x0101) && 
				(STA_TGN_WIFI_ON(pAd)) &&
				(pAd->CommonCfg.IOTestParm.bToggle == FALSE))

				{
					RTMP_IO_WRITE32(pAd, TXOP_CTRL_CFG, 0x24Bf);
					pAd->CommonCfg.IOTestParm.bToggle = TRUE;
				}
				else if ((STA_TGN_WIFI_ON(pAd)) &&
						((pAd->MACVersion & 0xffff) == 0x0101))
				{
					RTMP_IO_WRITE32(pAd, TXOP_CTRL_CFG, 0x243f);
					pAd->CommonCfg.IOTestParm.bToggle = FALSE;
				}
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	pAd->bUpdateBcnCntDone = FALSE;
	
/*	RECBATimerTimeout(SystemSpecific1,FunctionContext,SystemSpecific2,SystemSpecific3);*/
	pAd->Mlme.PeriodicRound ++;
	pAd->Mlme.GPIORound++;

#ifdef RTMP_MAC_USB
	/* execute every 100ms, update the Tx FIFO Cnt for update Tx Rate.*/
	NICUpdateFifoStaCounters(pAd);
#endif /* RTMP_MAC_USB */

	/* execute every 500ms */
	if ((pAd->Mlme.PeriodicRound % 5 == 0) && RTMPAutoRateSwitchCheck(pAd)/*(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED))*/)
	{
#ifdef CONFIG_STA_SUPPORT
		/* perform dynamic tx rate switching based on past TX history*/
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
					)
				&& (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)))
				MlmeDynamicTxRateSwitching(pAd);
		}
#endif /* CONFIG_STA_SUPPORT */
	}

#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
/*	if (IS_RT3593(pAd))*/
/*	{*/
		if ((pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE) && 
		     (INFRA_ON(pAd)))
		{
			RTMP_CHIP_ASIC_FREQ_CAL(pAd);
		}
/*	}*/
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

	/* Normal 1 second Mlme PeriodicExec.*/
	if (pAd->Mlme.PeriodicRound %MLME_TASK_EXEC_MULTIPLE == 0)
	{
		pAd->Mlme.OneSecPeriodicRound ++;

		RTMP_CHIP_ASIC_VCO_CAL(pAd);

#ifdef RALINK_ATE
    	if (ATE_ON(pAd))
    	{
			/* for performace enchanement */
			NdisZeroMemory(&pAd->RalinkCounters,
							(UINT32)&pAd->RalinkCounters.OneSecEnd -
							(UINT32)&pAd->RalinkCounters.OneSecStart);

			/* request from Baron : move this routine from later to here */
			/* for showing Rx error count in ATE RXFRAME */
            NICUpdateRawCounters(pAd);
			if (pAd->ate.bRxFER == 1)
			{
				pAd->ate.RxTotalCnt += pAd->ate.RxCntPerSec;
			    ate_print(KERN_EMERG "MlmePeriodicExec: Rx packet cnt = %d/%d\n", pAd->ate.RxCntPerSec, pAd->ate.RxTotalCnt);
				pAd->ate.RxCntPerSec = 0;

				if (pAd->ate.RxAntennaSel == 0)
					ate_print(KERN_EMERG "MlmePeriodicExec: Rx AvgRssi0=%d, AvgRssi1=%d, AvgRssi2=%d\n\n",
						pAd->ate.AvgRssi0, pAd->ate.AvgRssi1, pAd->ate.AvgRssi2);
				else
					ate_print(KERN_EMERG "MlmePeriodicExec: Rx AvgRssi=%d\n\n", pAd->ate.AvgRssi0);
			}

			MlmeResetRalinkCounters(pAd);

			/* In QA Mode, QA will handle all registers. */
			if (pAd->ate.bQAEnabled == TRUE)
			{
				return;
			}

			if (pAd->ate.bAutoTxAlc == TRUE)
			{
				ATEAsicAdjustTxPower(pAd);
			}
			
#if (defined(RT3052) && !defined(RT3352)) || defined(RT3070)
	/* request by Gary, if Rssi0 > -42, BBP 82 need to be changed from 0x62 to 0x42, , bbp 67 need to be changed from 0x20 to 0x18*/
	if (!pAd->CommonCfg.HighPowerPatchDisabled)
	{
#ifdef RT3070
		 if ((IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201)))
#endif /* RT3070 */
                {
		   		if ((pAd->ate.AvgRssi0 != 0) && (pAd->ate.AvgRssi0 > (pAd->BbpRssiToDbmDelta - 35))) 
                 		{ 
                         		 RT30xxWriteRFRegister(pAd, RF_R27, 0x20); 
                		 } 
                 		else 
                 		{ 
                          		RT30xxWriteRFRegister(pAd, RF_R27, 0x23); 
                 		}
		 }

#ifdef RT3052
		if ((pAd->Antenna.field.RxPath == 2) && IS_RT3052(pAd))
		{
			if ((pAd->ate.AvgRssi0 != 0) && (pAd->ate.AvgRssi0 > (pAd->BbpRssiToDbmDelta - 42) ))
			{
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x42);
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R67, 0x18);
			}
			else
			{
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);
				ATE_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R67, 0x20);
			}
		}
#endif /* RT3052 */
	}
#endif /* RT305x */
			return;
    	}
#endif /* RALINK_ATE */



		/*ORIBATimerTimeout(pAd);*/
		NdisGetSystemUpTime(&pAd->Mlme.Now32);

		/* add the most up-to-date h/w raw counters into software variable, so that*/
		/* the dynamic tuning mechanism below are based on most up-to-date information*/
		/* Hint: throughput impact is very serious in the function */
		NICUpdateRawCounters(pAd);																										

#ifdef RTMP_MAC_USB
#ifndef INF_AMAZON_SE
		RTUSBWatchDog(pAd);
#endif /* INF_AMAZON_SE */
#endif /* RTMP_MAC_USB */

#ifdef DOT11_N_SUPPORT
   		/* Need statistics after read counter. So put after NICUpdateRawCounters*/
		ORIBATimerTimeout(pAd);
#endif /* DOT11_N_SUPPORT */

		/* if MGMT RING is full more than twice within 1 second, we consider there's*/
		/* a hardware problem stucking the TX path. In this case, try a hardware reset*/
		/* to recover the system*/
	/*	if (pAd->RalinkCounters.MgmtRingFullCount >= 2)*/
	/*		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HARDWARE_ERROR);*/
	/*	else*/
	/*		pAd->RalinkCounters.MgmtRingFullCount = 0;*/

		/* The time period for checking antenna is according to traffic*/
		{
			if (pAd->Mlme.bEnableAutoAntennaCheck)
			{
				TxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount + 
								 pAd->RalinkCounters.OneSecTxRetryOkCount + 
								 pAd->RalinkCounters.OneSecTxFailCount;
				
				/* dynamic adjust antenna evaluation period according to the traffic*/
				if (TxTotalCnt > 50)
				{
					if (pAd->Mlme.OneSecPeriodicRound % 10 == 0)
					{
						AsicEvaluateRxAnt(pAd);
					}
				}
				else
				{
					if (pAd->Mlme.OneSecPeriodicRound % 3 == 0)
					{
						AsicEvaluateRxAnt(pAd);
					}
				}
			}
		}

#ifdef VIDEO_TURBINE_SUPPORT
		/*VideoTurbineUpdate(pAd);*/
		/*VideoTurbineDynamicTune(pAd);*/
#endif /* VIDEO_TURBINE_SUPPORT */

#ifdef VCORECAL_SUPPORT
#ifdef RALINK_ATE
                if (!ATE_ON(pAd))
#endif
                {
					if (pAd->chipCap.FlgIsVcoReCalSup == TRUE)
					{
                        if (pAd->RefreshTssi == 1)
                        {
                                UCHAR BbpR49 = 0;

                                RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49);
                                /* Update only when BBP_R49 bit 0 is set to 1 */
                                if ((BbpR49 & 0x01) != 0)
                                {
                                        pAd->LatchTssi = BbpR49 >> 1; /* bit 0 is used for update flag*/
                                        pAd->RefreshTssi = 0;
                                }
                        }

                        if (((pAd->Mlme.OneSecPeriodicRound % 10) == 0) && (pAd->RefreshTssi == 0))
                                AsicVCORecalibration(pAd);
					}
                }
#endif /* VCORECAL_SUPPORT */

#ifdef SPECIFIC_VCORECAL_SUPPORT
   if (((pAd->Mlme.OneSecPeriodicRound % 10) == 0)
      && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)))
		{
#ifdef RTMP_RF_RW_SUPPORT
			UCHAR 	RFValue;
			UCHAR RFIndex=RF_R07;
			/*Enable RF tuning*/
			/*Enable RF tuning*/
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
			if (IS_RT5390(pAd))
				RFIndex = RF_R03/*RF_R06*/;
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
			RT30xxReadRFRegister(pAd, RFIndex, (PUCHAR)&RFValue);
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
			if (IS_RT5390(pAd))
				RFValue = RFValue | 0x80/*0x20*/;
			else
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
			RFValue = RFValue | 0x1;

			RT30xxWriteRFRegister(pAd, RFIndex, (UCHAR)RFValue);

#endif /* RTMP_RF_RW_SUPPORT */
		}
#endif /* SPECIFIC_VCORECAL_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			STAMlmePeriodicExec(pAd);
#endif /* CONFIG_STA_SUPPORT */
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		if (IS_RT5390(pAd))
		{	
			AsicCheckForHwRecovery(pAd);
		}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */



		MlmeResetRalinkCounters(pAd);

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
#ifdef RTMP_MAC_USB
			if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
#endif /* RTMP_MAC_USB */
			{


			UINT32	MacReg = 0;
			
			RTMP_IO_READ32(pAd, 0x10F4, &MacReg);
			if (((MacReg & 0x20000000) && (MacReg & 0x80)) || ((MacReg & 0x20000000) && (MacReg & 0x20)))
			{
				RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x1);
				RTMPusecDelay(1);
				{
				RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0xC);
				}

				DBGPRINT(RT_DEBUG_WARN,("Warning, MAC specific condition occurs \n"));
			}
		}
		}
#endif /* CONFIG_STA_SUPPORT */

		RTMP_MLME_HANDLER(pAd);
	}




	pAd->bUpdateBcnCntDone = FALSE;
}


/*
	==========================================================================
	Validate SSID for connection try and rescan purpose
	Valid SSID will have visible chars only.
	The valid length is from 0 to 32.
	IRQL = DISPATCH_LEVEL
	==========================================================================
 */
BOOLEAN MlmeValidateSSID(
	IN PUCHAR	pSsid,
	IN UCHAR	SsidLen)
{
	int	index;

	if (SsidLen > MAX_LEN_OF_SSID)
		return (FALSE);

	/* Check each character value*/
	for (index = 0; index < SsidLen; index++)
	{
		if (pSsid[index] < 0x20)
			return (FALSE);
	}

	/* All checked*/
	return (TRUE);
}

VOID MlmeSelectRateSwitchTable11N3SReplacement(
		IN PUCHAR               *ppTable)
{
	*ppTable = RateSwitchTable11N3SReplacement;
}

VOID MlmeSelectTxRateTable(
	IN PRTMP_ADAPTER		pAd,
	IN PMAC_TABLE_ENTRY		pEntry,
	IN PUCHAR				*ppTable,
	IN PUCHAR				pTableSize,
	IN PUCHAR				pInitTxRateIdx)
{
#ifdef NEW_RATE_ADAPT_SUPPORT
	UCHAR tempUchar[] = {
							0x0f, 0x20, 15,  8,   25,  14, 21, 16, 15, 130,/*mcs15*/
							0x10, 0x22, 15,  8,   25,  15, 21, 16, 16, 144};/*mcs15+short gi*/
	UCHAR tempUchar1[] = {
							0x07, 0x21,  7,  8, 14,  6,  19,  12,  8, 65,/*mcs7*/
							0x08, 0x23,  7,  8, 14,  7,  19,  12,  8, 72};/*mcs7+short gi  */
#endif /* NEW_RATE_ADAPT_SUPPORT */

	do
	{
		/* decide the rate table for tuning*/
		if (pAd->CommonCfg.TxRateTableSize > 0)
		{
			*ppTable = RateSwitchTable;
			*pTableSize = RateSwitchTable[0];
			*pInitTxRateIdx = RateSwitchTable[1];

			break;
		}

#ifdef CONFIG_STA_SUPPORT
		if ((pAd->OpMode == OPMODE_STA) && ADHOC_ON(pAd))
		{
			/* for ADHOC mode */
#ifdef DOT11_N_SUPPORT
			if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && 
				(pEntry->HTCapability.MCSSet[0] == 0xff) && 
				((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1)))
			{/* 11N 1S Adhoc*/

#ifdef AGS_SUPPORT
				if (SUPPORT_AGS(pAd))
				{
					*ppTable = AGS1x1HTRateTable;
					*pTableSize = AGS1x1HTRateTable[0];
					*pInitTxRateIdx = AGS1x1HTRateTable[1];

					DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 1S adhoc, AGS1x1HTRateTable\n", 
						__FUNCTION__));
				}
				else
#endif /* AGS_SUPPORT */
				{
				*ppTable = RateSwitchTable11N1S;
				*pTableSize = RateSwitchTable11N1S[0];
				*pInitTxRateIdx = RateSwitchTable11N1S[1];
				}
				
			}
			else if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && 
					(pEntry->HTCapability.MCSSet[0] == 0xff) && 
					(pEntry->HTCapability.MCSSet[1] == 0xff) &&
					(((pAd->Antenna.field.TxPath == 3) && (pEntry->HTCapability.MCSSet[2] == 0x00)) || (pAd->Antenna.field.TxPath == 2)))
			{/* 11N 2S Adhoc*/

#ifdef AGS_SUPPORT
				if (SUPPORT_AGS(pAd))
				{
					*ppTable = AGS2x2HTRateTable;
					*pTableSize = AGS2x2HTRateTable[0];
					*pInitTxRateIdx = AGS2x2HTRateTable[1];

					DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 2S adhoc, AGS2x2HTRateTable\n", 
						__FUNCTION__));
				}
				else
#endif /* AGS_SUPPORT */
				{
				if (pAd->LatchRfRegs.Channel <= 14)
				{
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx = RateSwitchTable11N2S[1];
				}
				else
				{
					*ppTable = RateSwitchTable11N2SForABand;
					*pTableSize = RateSwitchTable11N2SForABand[0];
					*pInitTxRateIdx = RateSwitchTable11N2SForABand[1];
				}
				
			}
			}
#ifdef AGS_SUPPORT
			else if (SUPPORT_AGS(pAd) && 
					(pEntry->HTCapability.MCSSet[0] == 0xFF) && 
					(pEntry->HTCapability.MCSSet[1] == 0xFF) && 
					(pEntry->HTCapability.MCSSet[2] == 0xFF) && 
					(pAd->Antenna.field.TxPath == 3))
			{/* 11N 3S Adhoc*/
				*ppTable = AGS3x3HTRateTable;
				*pTableSize = AGS3x3HTRateTable[0];
				*pInitTxRateIdx = AGS3x3HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 3S adhoc, AGS3x3HTRateTable\n", 
					__FUNCTION__));
			}
#endif /* AGS_SUPPORT */
			else
#endif /* DOT11_N_SUPPORT */				
				if ((pEntry->RateLen == 4)
#ifdef DOT11_N_SUPPORT
					&& (pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0)
#endif /* DOT11_N_SUPPORT */
					)
			{
				*ppTable = RateSwitchTable11B;
				*pTableSize = RateSwitchTable11B[0];
				*pInitTxRateIdx = RateSwitchTable11B[1];
				
			}
			else if (pAd->LatchRfRegs.Channel <= 14)
			{
				*ppTable = RateSwitchTable11BG;
				*pTableSize = RateSwitchTable11BG[0];
				*pInitTxRateIdx = RateSwitchTable11BG[1];
				
			}
			else
			{
				*ppTable = RateSwitchTable11G;
				*pTableSize = RateSwitchTable11G[0];
				*pInitTxRateIdx = RateSwitchTable11G[1];
				
			}
			break;
		}
#endif /* CONFIG_STA_SUPPORT */

#ifdef DOT11_N_SUPPORT
		/*if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 12) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) &&*/
		/*	((pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1)))*/
		if (((pEntry->RateLen == 12) || (pAd->OpMode == OPMODE_STA)) && (pEntry->HTCapability.MCSSet[0] == 0xff) &&
			((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->CommonCfg.TxStream == 1)))
		{/* 11BGN 1S AP*/
#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				*ppTable = AGS1x1HTRateTable;
				*pTableSize = AGS1x1HTRateTable[0];
				*pInitTxRateIdx = AGS1x1HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 1S AP, AGS1x1HTRateTable\n", 
					__FUNCTION__));
			}
			else
#endif /* AGS_SUPPORT */
			{
			*ppTable = RateSwitchTable11BGN1S;
			*pTableSize = RateSwitchTable11BGN1S[0];
			*pInitTxRateIdx = RateSwitchTable11BGN1S[1];
			
			}

			break;
		}

#ifdef AGS_SUPPORT
		/* only for station */
		if (SUPPORT_AGS(pAd) && 
			(pEntry->HTCapability.MCSSet[0] == 0xFF) && 
			(pEntry->HTCapability.MCSSet[1] == 0xFF) && 
			(pEntry->HTCapability.MCSSet[2] == 0xFF) && 
			(pAd->CommonCfg.TxStream == 3))
		{/* 11N 3S */
			*ppTable = AGS3x3HTRateTable;
			*pTableSize = AGS3x3HTRateTable[0];
			*pInitTxRateIdx = AGS3x3HTRateTable[1];

			DBGPRINT_RAW(RT_DEBUG_INFO,("AGS: %s: 11N 3S AP, AGS3x3HTRateTable\n", 
				__FUNCTION__));
			break;
		}
		else
#endif /* AGS_SUPPORT */
		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 12) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) &&*/
		/*	(pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0xff) && (pAd->Antenna.field.TxPath == 2))*/
		if (((pEntry->RateLen == 12) || (pAd->OpMode == OPMODE_STA)) && (pEntry->HTCapability.MCSSet[0] == 0xff) &&
			(pEntry->HTCapability.MCSSet[1] == 0xff) && (((pAd->Antenna.field.TxPath == 3) && (pEntry->HTCapability.MCSSet[2] == 0x00)) || (pAd->CommonCfg.TxStream == 2)))
		{/* 11BGN 2S AP*/
#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				*ppTable = AGS2x2HTRateTable;
				*pTableSize = AGS2x2HTRateTable[0];
				*pInitTxRateIdx = AGS2x2HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 2S AP, AGS2x2HTRateTable\n", 
					__FUNCTION__));
			}
			else
#endif /* AGS_SUPPORT */
			{
			if (pAd->LatchRfRegs.Channel <= 14)
			{
				*ppTable = RateSwitchTable11BGN2S;
				*pTableSize = RateSwitchTable11BGN2S[0];
				*pInitTxRateIdx = RateSwitchTable11BGN2S[1];
				
			}
			else
			{
				*ppTable = RateSwitchTable11BGN2SForABand;
				*pTableSize = RateSwitchTable11BGN2SForABand[0];
				*pInitTxRateIdx = RateSwitchTable11BGN2SForABand[1];
                		
			}
			}
			break;
		}

#ifdef DOT11N_SS3_SUPPORT
		if ((pEntry->RateLen == 12) &&
			(pEntry->HTCapability.MCSSet[0] == 0xff) &&
			(pEntry->HTCapability.MCSSet[1] == 0xff) &&
			(pEntry->HTCapability.MCSSet[2] == 0xff) &&
			(pAd->CommonCfg.TxStream == 3))
		{
			*ppTable = RateSwitchTable11N3S;
			*pTableSize = RateSwitchTable11N3S[0];
			*pInitTxRateIdx = RateSwitchTable11N3S[1];
			break;
		}
#endif /* DOT11N_SS3_SUPPORT */

		/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && ((pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1)))*/
		if ((pEntry->HTCapability.MCSSet[0] == 0xff) && ((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->CommonCfg.TxStream == 1)))
		{/* 11N 1S AP*/
#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				*ppTable = AGS1x1HTRateTable;
				*pTableSize = AGS1x1HTRateTable[0];
				*pInitTxRateIdx = AGS1x1HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 1S AP, AGS1x1HTRateTable\n", 
					__FUNCTION__));
			}
			else
#endif /* AGS_SUPPORT */
			{
			*ppTable = RateSwitchTable11N1S;
			*pTableSize = RateSwitchTable11N1S[0];
			*pInitTxRateIdx = RateSwitchTable11N1S[1];
			
			}
			break;
		}

		/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0xff) && (pAd->Antenna.field.TxPath == 2))*/
		if ((pEntry->HTCapability.MCSSet[0] == 0xff) && (pEntry->HTCapability.MCSSet[1] == 0xff) && (pAd->CommonCfg.TxStream >= 2))
		{/* 11N 2S AP*/
#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				*ppTable = AGS2x2HTRateTable;
				*pTableSize = AGS2x2HTRateTable[0];
				*pInitTxRateIdx = AGS2x2HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: 11N 2S AP, AGS2x2HTRateTable\n", 
					__FUNCTION__));
			}
			else
#endif /* AGS_SUPPORT */
			{
			if (pAd->LatchRfRegs.Channel <= 14)
			{
			*ppTable = RateSwitchTable11N2S;
			*pTableSize = RateSwitchTable11N2S[0];
			*pInitTxRateIdx = RateSwitchTable11N2S[1];
			}
			else
			{
				*ppTable = RateSwitchTable11N2SForABand;
				*pTableSize = RateSwitchTable11N2SForABand[0];
				*pInitTxRateIdx = RateSwitchTable11N2SForABand[1];
			}
			
			}
			break;
		}

#ifdef DOT11N_SS3_SUPPORT
		if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
			(pEntry->HTCapability.MCSSet[1] == 0xff) &&
			(pEntry->HTCapability.MCSSet[2] == 0xff) &&
			(pAd->CommonCfg.TxStream == 3))
		{

			*ppTable = RateSwitchTable11N3S;
			*pTableSize = RateSwitchTable11N3S[0];
			*pInitTxRateIdx = RateSwitchTable11N3S[1];
			/*printk("%s(%d):Select 11N 3S AP!\n", __FUNCTION__, __LINE__);*/
			break;
		}
#endif /* DOT11N_SS3_SUPPORT */

#ifdef DOT11N_SS3_SUPPORT
		if (pAd->CommonCfg.TxStream == 3)
		{
			if  (pEntry->HTCapability.MCSSet[0] == 0xff)
			{
				if (pEntry->HTCapability.MCSSet[1] == 0x00)
				{	/* Only support 1SS */
					if (pEntry->RateLen == 12)
					{
						*ppTable = RateSwitchTable11BGN1S;
						*pTableSize = RateSwitchTable11BGN1S[0];
						*pInitTxRateIdx = RateSwitchTable11BGN1S[1];
						/*printk("%s(%d):RT2883-Select 11BGN 1S AP!\n", __FUNCTION__, __LINE__);*/
					}
					else
					{
						*ppTable = RateSwitchTable11N1S;
						*pTableSize = RateSwitchTable11N1S[0];
						*pInitTxRateIdx = RateSwitchTable11N1S[1];
						/*printk("%s(%d):RT2883-Select 11N 1S AP!\n", __FUNCTION__, __LINE__);*/
					}
					break;
				}
				else if (pEntry->HTCapability.MCSSet[2] == 0x00)
				{	/* Only support 2SS */
					if (pEntry->RateLen > 0)
					{
						if (pAd->LatchRfRegs.Channel <= 14)
						{
							*ppTable = RateSwitchTable11BGN2S;
							*pTableSize = RateSwitchTable11BGN2S[0];
							*pInitTxRateIdx = RateSwitchTable11BGN2S[1];
							/*printk("%s(%d):RT2883-Select 11BGN 2S AP!\n", __FUNCTION__, __LINE__);*/
						}
						else
						{
							*ppTable = RateSwitchTable11BGN2SForABand;
							*pTableSize = RateSwitchTable11BGN2SForABand[0];
							*pInitTxRateIdx = RateSwitchTable11BGN2SForABand[1];
							/*printk("%s(%d):RT2883-Select 11N 2S AP!\n", __FUNCTION__, __LINE__);*/
						}
						break;
					}
					else
					{
						if (pAd->LatchRfRegs.Channel <= 14)
						{
							*ppTable = RateSwitchTable11N2S;
							*pTableSize = RateSwitchTable11N2S[0];
							*pInitTxRateIdx = RateSwitchTable11N2S[1];
						}
						else
						{
							*ppTable = RateSwitchTable11N2SForABand;
							*pTableSize = RateSwitchTable11N2SForABand[0];
							*pInitTxRateIdx = RateSwitchTable11N2SForABand[1];
						}
						/*printk("%s(%d):RT2883-Select 11N 2S AP!\n", __FUNCTION__, __LINE__);*/
						break;
					}
				}
				/* For 3SS case, we use the new rate table, so don't care it here */
			}
		}
#endif /* DOT11N_SS3_SUPPORT */
#endif /* DOT11_N_SUPPORT */
		/*else if ((pAd->StaActive.SupRateLen == 4) && (pAd->StaActive.ExtRateLen == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0))*/
		if ((pEntry->RateLen == 4 || pAd->CommonCfg.PhyMode==PHY_11B) 
#ifdef DOT11_N_SUPPORT
		/*Iverson mark for Adhoc b mode,sta will use rate 54  Mbps when connect with sta b/g/n mode */
		/* && (pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0)*/
#endif /* DOT11_N_SUPPORT */
			)
		{/* B only AP*/
			*ppTable = RateSwitchTable11B;
			*pTableSize = RateSwitchTable11B[0];
			*pInitTxRateIdx = RateSwitchTable11B[1];
			
			break;
		}

		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen > 8) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0))*/
		if ((pEntry->RateLen > 8) 
#ifdef DOT11_N_SUPPORT
			&& (pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0)
#endif /* DOT11_N_SUPPORT */
			)
		{/* B/G  mixed AP*/
			*ppTable = RateSwitchTable11BG;
			*pTableSize = RateSwitchTable11BG[0];
			*pInitTxRateIdx = RateSwitchTable11BG[1];
			
			break;
		}

		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 8) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0))*/
		if ((pEntry->RateLen == 8) 
#ifdef DOT11_N_SUPPORT
			&& (pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0)
#endif /* DOT11_N_SUPPORT */
			)
		{/* G only AP*/
			*ppTable = RateSwitchTable11G;
			*pTableSize = RateSwitchTable11G[0];
			*pInitTxRateIdx = RateSwitchTable11G[1];
			
			break;
		}
#ifdef DOT11_N_SUPPORT
#endif /* DOT11_N_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
#ifdef DOT11_N_SUPPORT
			/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0))*/
			if ((pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0))
#endif /* DOT11_N_SUPPORT */
			{	/* Legacy mode*/
				if (pAd->CommonCfg.MaxTxRate <= RATE_11)
				{
					*ppTable = RateSwitchTable11B;
					*pTableSize = RateSwitchTable11B[0];
					*pInitTxRateIdx = RateSwitchTable11B[1];
				}
				else if ((pAd->CommonCfg.MaxTxRate > RATE_11) && (pAd->CommonCfg.MinTxRate > RATE_11))
				{
					*ppTable = RateSwitchTable11G;
					*pTableSize = RateSwitchTable11G[0];
					*pInitTxRateIdx = RateSwitchTable11G[1];
					
				}
				else		
				{
					*ppTable = RateSwitchTable11BG;
					*pTableSize = RateSwitchTable11BG[0];
					*pInitTxRateIdx = RateSwitchTable11BG[1];
				}
				break;
			}
#ifdef DOT11_N_SUPPORT
#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd) && (pAd->CommonCfg.TxStream == 3))
			{
				*ppTable = AGS3x3HTRateTable;
				*pTableSize = AGS3x3HTRateTable[0];
				*pInitTxRateIdx = AGS3x3HTRateTable[1];

				DBGPRINT_RAW(RT_DEBUG_TRACE,("AGS: %s: Unknown adhoc, DLS or AP, AGS3x3HTRateTable\n", 
					__FUNCTION__));
			}
			else
#endif /* AGS_SUPPORT */
			{
			if (pAd->LatchRfRegs.Channel <= 14)
			{
				if (pAd->CommonCfg.TxStream == 1)
				{
					*ppTable = RateSwitchTable11N1S;
					*pTableSize = RateSwitchTable11N1S[0];
					*pInitTxRateIdx = RateSwitchTable11N1S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,("DRS: unkown mode,default use 11N 1S AP \n"));
				}
				else if (pAd->CommonCfg.TxStream == 2)
				{
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx = RateSwitchTable11N2S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,("DRS: unkown mode,default use 11N 2S AP \n"));	
				}
				else
				{
#ifdef DOT11N_SS3_SUPPORT
					*ppTable = RateSwitchTable11N3S;
					*pTableSize = RateSwitchTable11N3S[0];
					*pInitTxRateIdx = RateSwitchTable11N3S[1];
#else
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx = RateSwitchTable11N2S[1];
#endif /* DOT11N_SS3_SUPPORT */
				}
			}
			else
			{
				if (pAd->CommonCfg.TxStream == 1)
				{
					*ppTable = RateSwitchTable11N1S;
					*pTableSize = RateSwitchTable11N1S[0];
					*pInitTxRateIdx = RateSwitchTable11N1S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,("DRS: unkown mode,default use 11N 1S AP \n"));
				}
				else if (pAd->CommonCfg.TxStream == 2)
				{
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx = RateSwitchTable11N2S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,("DRS: unkown mode,default use 11N 2S AP \n"));	
				}
				else
				{
#ifdef DOT11N_SS3_SUPPORT
					*ppTable = RateSwitchTable11N3S;
					*pTableSize = RateSwitchTable11N3S[0];
					*pInitTxRateIdx = RateSwitchTable11N3S[1];
#else
					*ppTable = RateSwitchTable11N2SForABand;
					*pTableSize = RateSwitchTable11N2SForABand[0];
					*pInitTxRateIdx = RateSwitchTable11N2SForABand[1];
#endif /* DOT11N_SS3_SUPPORT */
				}
			}
			}
#endif /* DOT11_N_SUPPORT */
			DBGPRINT_RAW(RT_DEBUG_ERROR,("DRS: unkown mode (SupRateLen=%d, ExtRateLen=%d, MCSSet[0]=0x%x, MCSSet[1]=0x%x)\n",
						pAd->StaActive.SupRateLen,
						pAd->StaActive.ExtRateLen,
						pAd->StaActive.SupportedPhyInfo.MCSSet[0],
						pAd->StaActive.SupportedPhyInfo.MCSSet[1]));
		}
#endif /* CONFIG_STA_SUPPORT */
	} while(FALSE);

#ifdef NEW_RATE_ADAPT_SUPPORT
	if (*ppTable == RateSwitchTable11N3S)
	{

	 	if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
			(pEntry->HTCapability.MCSSet[1] == 0xff) &&
			(pAd->CommonCfg.TxStream > 1) && 
			((pAd->CommonCfg.TxStream == 2) || (pEntry->HTCapability.MCSSet[2] == 0x0)))
			NdisMoveMemory((*ppTable)+10*16, tempUchar, 20);
		else if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
				((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->CommonCfg.TxStream == 1))) 
			NdisMoveMemory((*ppTable)+10*8, tempUchar1, 20);				
	}
#endif /* NEW_RATE_ADAPT_SUPPORT */
}


#ifdef CONFIG_STA_SUPPORT
VOID STAMlmePeriodicExec(
	PRTMP_ADAPTER pAd)
{
	ULONG			    TxTotalCnt;
	int 	i;
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND	
	POS_COOKIE  pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */

	/*
		We return here in ATE mode, because the statistics 
		that ATE need are not collected via this routine.
	*/
#ifdef RALINK_ATE
	if (ATE_ON(pAd))
	return;
#endif /* RALINK_ATE */

	RTMP_CHIP_SPECIFIC(pAd, RTMP_CHIP_SPEC_STATE_STA_PERIODIC,
						RTMP_CHIP_SPEC_HIGH_POWER_PATCH_STA, NULL, 0);

#if defined(RT3070)
	/* request by Gary, if Rssi0 > -42, BBP 82 need to be changed from 0x62 to 0x42, , bbp 67 need to be changed from 0x20 to 0x18*/
	if (!pAd->CommonCfg.HighPowerPatchDisabled)
	{
		UCHAR RFValue;

#ifdef RT3070
		 if (((IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201)) || IS_RT2070(pAd))
#ifdef RTMP_MAC_USB
		 	&& !RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)
#endif /* RTMP_MAC_USB */
		)
                {
                        
		   		if ((pAd->StaCfg.RssiSample.AvgRssi0 != 0) && (pAd->StaCfg.RssiSample.AvgRssi0 > (pAd->BbpRssiToDbmDelta - 35))) 
                 		{ 
					/*RT30xxWriteRFRegister(pAd, RF_R27, 0x20); */
					RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)&RFValue);
					RFValue &= ~0x3;
					RT30xxWriteRFRegister(pAd, RF_R27, (UCHAR)RFValue);
                		 } 
                 		else 
                 		{ 
					/*RT30xxWriteRFRegister(pAd, RF_R27, 0x23); */
					RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)&RFValue);
					RFValue |= 0x3;
					RT30xxWriteRFRegister(pAd, RF_R27, (UCHAR)RFValue);
                 		}
                       
		 }
#endif /* RT3070 */
	}
#endif /* RT3070 */

#ifdef WPA_SUPPLICANT_SUPPORT
    if (pAd->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_DISABLE)    
#endif /* WPA_SUPPLICANT_SUPPORT */        
    {
    	/* WPA MIC error should block association attempt for 60 seconds*/
		if (pAd->StaCfg.bBlockAssoc && 
			RTMP_TIME_AFTER(pAd->Mlme.Now32, pAd->StaCfg.LastMicErrorTime + (60*OS_HZ)))
    		pAd->StaCfg.bBlockAssoc = FALSE;
    }

#ifdef RTMP_MAC_USB
	/* If station is idle, go to sleep*/
	if ( 1
	/*	&& (pAd->StaCfg.PSControl.field.EnablePSinIdle == TRUE)*/
		&& (pAd->StaCfg.WindowsPowerMode > 0)
		&& (pAd->OpMode == OPMODE_STA) && (IDLE_ON(pAd)) 
		&& (pAd->Mlme.SyncMachine.CurrState == SYNC_IDLE)
		&& (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
		&& (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
		&& (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */	
#endif /* CONFIG_PM */
		)
	{
//			RT28xxUsbAsicRadioOff(pAd);
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND	
	//		if(!RTMP_Usb_AutoPM_Put_Interface(pObj->pUsb_Dev,pObj->intf))
	//				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */

		DBGPRINT(RT_DEBUG_TRACE, ("PSM - Issue Sleep command)\n"));
	}

#endif /* RTMP_MAC_USB */


	

	if (ADHOC_ON(pAd))
	{
	}
	else
	{
    	AsicStaBbpTuning(pAd);
	}
	
	TxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount + 
					 pAd->RalinkCounters.OneSecTxRetryOkCount + 
					 pAd->RalinkCounters.OneSecTxFailCount;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) && 
		(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)))
	{
		/* update channel quality for Roaming/Fast-Roaming and UI LinkQuality display*/
		if (pAd->StaCfg.bImprovedScan == FALSE)	/* bImprovedScan True means scan is not completed */
		{
			/* The NIC may lost beacons during scaning operation.*/
			MlmeCalculateChannelQuality(pAd, NULL, pAd->Mlme.Now32);
		}
	}


	/* must be AFTER MlmeDynamicTxRateSwitching() because it needs to know if*/
	/* Radio is currently in noisy environment*/
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) 
	AsicAdjustTxPower(pAd);

	/*
		Driver needs to up date value of LastOneSecTotalTxCount here;
		otherwise UI couldn't do scanning sometimes when STA doesn't connect to AP or peer Ad-Hoc.
	*/
	pAd->RalinkCounters.LastOneSecTotalTxCount = TxTotalCnt;
	

	if (INFRA_ON(pAd))
	{
		/* resume Improved Scanning*/
		if ((pAd->StaCfg.bImprovedScan) &&
			(pAd->Mlme.SyncMachine.CurrState == SCAN_PENDING))
		{
			MLME_SCAN_REQ_STRUCT       ScanReq;

			pAd->StaCfg.LastScanTime = pAd->Mlme.Now32;
			
			ScanParmFill(pAd, &ScanReq, "", 0, BSS_ANY, SCAN_ACTIVE);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
			DBGPRINT(RT_DEBUG_WARN, ("bImprovedScan ............. Resume for bImprovedScan, SCAN_PENDING .............. \n"));
		}

#ifdef QOS_DLS_SUPPORT
		/* Check DLS time out, then tear down those session*/
		RTMPCheckDLSTimeOut(pAd);
#endif /* QOS_DLS_SUPPORT */


		/* Is PSM bit consistent with user power management policy?*/
		/* This is the only place that will set PSM bit ON.*/
		if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		MlmeCheckPsmChange(pAd, pAd->Mlme.Now32);

		/*
			When we are connected and do the scan progress, it's very possible we cannot receive
			the beacon of the AP. So, here we simulate that we received the beacon.
		*/
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS) &&
			(RTMP_TIME_AFTER(pAd->Mlme.Now32, pAd->StaCfg.LastBeaconRxTime + (1*OS_HZ))))
		{
			ULONG BPtoJiffies;
			LONG timeDiff;

			BPtoJiffies = (((pAd->CommonCfg.BeaconPeriod * 1024 / 1000) * OS_HZ) / 1000);
			timeDiff = (pAd->Mlme.Now32 - pAd->StaCfg.LastBeaconRxTime) / BPtoJiffies;
			if (timeDiff > 0) 
				pAd->StaCfg.LastBeaconRxTime += (timeDiff * BPtoJiffies);

			if (RTMP_TIME_AFTER(pAd->StaCfg.LastBeaconRxTime, pAd->Mlme.Now32))
			{
				DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - BeaconRxTime adjust wrong(BeaconRx=0x%lx, Now=0x%lx)\n", 
								pAd->StaCfg.LastBeaconRxTime, pAd->Mlme.Now32));
			}
		}
		
		if ((RTMP_TIME_AFTER(pAd->Mlme.Now32, pAd->StaCfg.LastBeaconRxTime + (1*OS_HZ))) &&
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) &&
			(pAd->StaCfg.bImprovedScan == FALSE) &&
			(((TxTotalCnt + pAd->RalinkCounters.OneSecRxOkCnt) < 600)))
		{
			RTMPSetAGCInitValue(pAd, BW_20);
			DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - No BEACON. restore R66 to the low bound(%d) \n", (0x2E + GET_LNA_GAIN(pAd))));
		}


#ifdef RTMP_MAC_USB
#ifdef DOT11_N_SUPPORT
/*for 1X1 STA pass 11n wifi wmm, need to change txop per case;*/
/* 1x1 device for 802.11n WMM Test*/
	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
	{

		if ((pAd->Antenna.field.TxPath == 1)&&
		(pAd->StaActive.SupportedPhyInfo.bHtEnable == TRUE) && 
		(pAd->CommonCfg.BACapability.field.Policy == BA_NOTUSE))
		{
			EDCA_AC_CFG_STRUC	Ac0Cfg;
			EDCA_AC_CFG_STRUC	Ac2Cfg;								
			RTUSBReadMACRegister(pAd, EDCA_AC2_CFG, &Ac2Cfg.word);									
			RTUSBReadMACRegister(pAd, EDCA_AC0_CFG, &Ac0Cfg.word);
									
			if ((pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VO] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BK] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] < 50) &&								      
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VI] >= 1000))
			{
			/*5.2.27/28 T7: Total throughput need to ~36Mbps*/
				if (Ac2Cfg.field.Aifsn!=0xc)
				{
					Ac2Cfg.field.Aifsn = 0xc;
					RTUSBWriteMACRegister(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
				}
			}
			else if ( IS_RT3070(pAd) && 
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VO] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BK] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VI] == 0) &&								      
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] >= 300)&&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] <= 1500)&&
			(pAd->CommonCfg.IOTestParm.bRTSLongProtOn==FALSE))
			{
				if (Ac0Cfg.field.AcTxop!=0x07)
				{
					Ac0Cfg.field.AcTxop = 0x07;
					Ac0Cfg.field.Aifsn = 0xc;
					RTUSBWriteMACRegister(pAd, EDCA_AC0_CFG, Ac0Cfg.word);
				}									
			}
			else if ((pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VO] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BK] == 0) &&
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VI] == 0) &&								      
			(pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] < 10))
			{
			/* restore default parameter of BE*/
				if ((Ac0Cfg.field.Aifsn!=3) ||(Ac0Cfg.field.AcTxop!=0))
				{
					if(Ac0Cfg.field.Aifsn!=3)
						Ac0Cfg.field.Aifsn = 3;
					if(Ac0Cfg.field.AcTxop!=0)
						Ac0Cfg.field.AcTxop = 0;
					RTUSBWriteMACRegister(pAd, EDCA_AC0_CFG, Ac0Cfg.word);
				}

			/* restore default parameter of VI*/
				if (Ac2Cfg.field.Aifsn!=0x3)
				{
					Ac2Cfg.field.Aifsn = 0x3;
					RTUSBWriteMACRegister(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
				}

			}
		}
	}
#endif /* DOT11_N_SUPPORT */
								
		/* TODO: for debug only. to be removed*/
		pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] = 0;
		pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BK] = 0;
		pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VI] = 0;
		pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VO] = 0;
		pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_BE] = 0;
		pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_BK] = 0;
		pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_VI] = 0;
		pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_VO] = 0;
		pAd->RalinkCounters.OneSecTxDoneCount = 0;
		pAd->RalinkCounters.OneSecTxAggregationCount = 0;
				

#endif /* RTMP_MAC_USB */
				

        /*if ((pAd->RalinkCounters.OneSecTxNoRetryOkCount == 0) &&*/
        /*    (pAd->RalinkCounters.OneSecTxRetryOkCount == 0))*/
       if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)))
        {
    		if (pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable)
    		{
    		    /* When APSD is enabled, the period changes as 20 sec*/
    			if ((pAd->Mlme.OneSecPeriodicRound % 20) == 8)
    			{
    				RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, TRUE);
    			}
    		}
    		else
    		{
    		    /* Send out a NULL frame every 10 sec to inform AP that STA is still alive (Avoid being age out)*/
    			if ((pAd->Mlme.OneSecPeriodicRound % 10) == 8)
			{
				RTMPSendNullFrame(pAd, 
								  pAd->CommonCfg.TxRate, 
								  (pAd->CommonCfg.bWmmCapable & pAd->CommonCfg.APEdcaParm.bValid));
			}
    		}
        }

		if (CQI_IS_DEAD(pAd->Mlme.ChannelQuality))
			{
			DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - No BEACON. Dead CQI. Auto Recovery attempt #%ld\n", pAd->RalinkCounters.BadCQIAutoRecoveryCount));

			if (pAd->StaCfg.bAutoConnectByBssid)
				pAd->StaCfg.bAutoConnectByBssid = FALSE;
			
#ifdef WPA_SUPPLICANT_SUPPORT
			if ((pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) &&
				(pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2))
				pAd->StaCfg.bLostAp = TRUE;
#endif /* WPA_SUPPLICANT_SUPPORT */

			pAd->MlmeAux.CurrReqIsFromNdis = FALSE;
			/* Lost AP, send disconnect & link down event*/
			LinkDown(pAd, FALSE);
			

#ifdef WPA_SUPPLICANT_SUPPORT
#ifndef NATIVE_WPA_SUPPLICANT_SUPPORT
			/*send disassociate event to wpa_supplicant*/
			if (pAd->StaCfg.WpaSupplicantUP) 
			{
				RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CUSTOM, RT_DISASSOC_EVENT_FLAG, NULL, NULL, 0);
			} 
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */
#endif /* WPA_SUPPLICANT_SUPPORT */
			
#ifdef NATIVE_WPA_SUPPLICANT_SUPPORT
#ifndef ANDROID_SUPPORT
		RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CGIWAP, -1, NULL, NULL, 0);
#endif /* ANDROID_SUPPORT */			
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */        			
			/* RTMPPatchMacBbpBug(pAd);*/
			MlmeAutoReconnectLastSSID(pAd);
		}
		else if (CQI_IS_BAD(pAd->Mlme.ChannelQuality))
		{
			pAd->RalinkCounters.BadCQIAutoRecoveryCount ++;
			DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Bad CQI. Auto Recovery attempt #%ld\n", pAd->RalinkCounters.BadCQIAutoRecoveryCount));
			MlmeAutoReconnectLastSSID(pAd);
		}
		
		if (pAd->StaCfg.bAutoRoaming)
		{
			BOOLEAN	rv = FALSE;
			CHAR	dBmToRoam = pAd->StaCfg.dBmToRoam;
			CHAR 	MaxRssi = RTMPMaxRssi(pAd, 
										  pAd->StaCfg.RssiSample.LastRssi0, 
										  pAd->StaCfg.RssiSample.LastRssi1, 
										  pAd->StaCfg.RssiSample.LastRssi2);			
			
			if (pAd->StaCfg.bAutoConnectByBssid)
				pAd->StaCfg.bAutoConnectByBssid = FALSE;
			
			/* Scanning, ignore Roaming*/
			if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS) &&
				(pAd->Mlme.SyncMachine.CurrState == SYNC_IDLE) &&
				(MaxRssi <= dBmToRoam))
			{
				DBGPRINT(RT_DEBUG_TRACE, ("Rssi=%d, dBmToRoam=%d\n", MaxRssi, (CHAR)dBmToRoam));


				/* Add auto seamless roaming*/
				if (rv == FALSE)
					rv = MlmeCheckForFastRoaming(pAd);
				
				if (rv == FALSE)
				{
					if ((pAd->StaCfg.LastScanTime + 10 * OS_HZ) < pAd->Mlme.Now32)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Roaming, No eligable entry, try new scan!\n"));
						pAd->StaCfg.LastScanTime = pAd->Mlme.Now32;
						MlmeAutoScan(pAd);
					}
				}
			}
		}
	}
	else if (ADHOC_ON(pAd))
	{

		/* If all peers leave, and this STA becomes the last one in this IBSS, then change MediaState*/
		/* to DISCONNECTED. But still holding this IBSS (i.e. sending BEACON) so that other STAs can*/
		/* join later.*/
		if (/*(RTMP_TIME_AFTER(pAd->Mlme.Now32, pAd->StaCfg.LastBeaconRxTime + ADHOC_BEACON_LOST_TIME)
			|| (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
			&& */OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
		{

			for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) 
			{
				MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[i];

				if (!IS_ENTRY_CLIENT(pEntry))
					continue;

				if (RTMP_TIME_AFTER(pAd->Mlme.Now32, pEntry->LastBeaconRxTime + ADHOC_BEACON_LOST_TIME))
                    MlmeDeAuthAction(pAd, pEntry, REASON_DISASSOC_STA_LEAVING, FALSE);
			}

            if (pAd->MacTab.Size == 0)
            {			                
    			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
    			RTMP_IndicateMediaState(pAd, NdisMediaStateDisconnected);
            }            
		}
			
	}
	else /* no INFRA nor ADHOC connection*/
	{


#ifdef WPA_SUPPLICANT_SUPPORT
		if (pAd->StaCfg.WpaSupplicantUP & WPA_SUPPLICANT_ENABLE_WPS)
			goto SKIP_AUTO_SCAN_CONN;
#endif /* WPA_SUPPLICANT_SUPPORT */

		if (pAd->StaCfg.bScanReqIsFromWebUI &&
			RTMP_TIME_BEFORE(pAd->Mlme.Now32, pAd->StaCfg.LastScanTime + (30 * OS_HZ)))
			goto SKIP_AUTO_SCAN_CONN;
		else
			pAd->StaCfg.bScanReqIsFromWebUI = FALSE;
        
		if ((pAd->StaCfg.bAutoReconnect == TRUE)
			&& RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP)
			&& (MlmeValidateSSID(pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen) == TRUE))
		{
			if ((pAd->ScanTab.BssNr==0) && (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
				)
			{
				MLME_SCAN_REQ_STRUCT	   ScanReq;

				if (RTMP_TIME_AFTER(pAd->Mlme.Now32, pAd->StaCfg.LastScanTime + (10 * OS_HZ))
					||(pAd->StaCfg.bNotFirstScan == FALSE))
				{
					DBGPRINT(RT_DEBUG_TRACE, ("STAMlmePeriodicExec():CNTL - ScanTab.BssNr==0, start a new ACTIVE scan SSID[%s]\n", pAd->MlmeAux.AutoReconnectSsid));
					if (pAd->StaCfg.BssType == BSS_ADHOC)	
						pAd->StaCfg.bNotFirstScan = TRUE;
					ScanParmFill(pAd, &ScanReq, (PSTRING) pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen, BSS_ANY, SCAN_ACTIVE);
					MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
					pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
					/* Reset Missed scan number*/
					pAd->StaCfg.LastScanTime = pAd->Mlme.Now32;
				}
				else
					MlmeAutoReconnectLastSSID(pAd);
			}
			else if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
			{
#ifdef CARRIER_DETECTION_SUPPORT /* Roger sync Carrier*/
				if (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
				{
					if ((pAd->Mlme.OneSecPeriodicRound % 5) == 1)
						MlmeAutoReconnectLastSSID(pAd);
				}
				else
#endif /* CARRIER_DETECTION_SUPPORT */ 
						{
#ifdef WPA_SUPPLICANT_SUPPORT
                                        if(pAd->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_ENABLE)
#endif // WPA_SUPPLICANT_SUPPORT //
							MlmeAutoReconnectLastSSID(pAd);
						}

			}
		}
	}

SKIP_AUTO_SCAN_CONN:

#ifdef DOT11_N_SUPPORT
    if ((pAd->MacTab.Content[BSSID_WCID].TXBAbitmap !=0) && (pAd->MacTab.fAnyBASession == FALSE)
		&& (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
		pAd->MacTab.fAnyBASession = TRUE;
		AsicUpdateProtect(pAd, HT_FORCERTSCTS,  ALLN_SETPROTECT, FALSE, FALSE);
	}
	else if ((pAd->MacTab.Content[BSSID_WCID].TXBAbitmap ==0) && (pAd->MacTab.fAnyBASession == TRUE)
		&& (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)))
	{
		pAd->MacTab.fAnyBASession = FALSE;
		AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode,  ALLN_SETPROTECT, FALSE, FALSE);
	}
#endif /* DOT11_N_SUPPORT */



#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
	/* Perform 20/40 BSS COEX scan every Dot11BssWidthTriggerScanInt	*/
	if ((OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SCAN_2040)) && 
		(pAd->CommonCfg.Dot11BssWidthTriggerScanInt != 0) && 
		((pAd->Mlme.OneSecPeriodicRound % pAd->CommonCfg.Dot11BssWidthTriggerScanInt) == (pAd->CommonCfg.Dot11BssWidthTriggerScanInt-1)))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - LastOneSecTotalTxCount/LastOneSecRxOkDataCnt  = %d/%d \n", 
								pAd->RalinkCounters.LastOneSecTotalTxCount,
								pAd->RalinkCounters.LastOneSecRxOkDataCnt));
		
		/* Check last scan time at least 30 seconds from now. 		*/
		/* Check traffic is less than about 1.5~2Mbps.*/
		/* it might cause data lost if we enqueue scanning.*/
		/* This criteria needs to be considered*/
		if ((pAd->RalinkCounters.LastOneSecTotalTxCount < 70) && (pAd->RalinkCounters.LastOneSecRxOkDataCnt < 70)
			/*&& ((pAd->StaCfg.LastScanTime + 10 * OS_HZ) < pAd->Mlme.Now32) */)		
		{
			MLME_SCAN_REQ_STRUCT            ScanReq;
			/* Fill out stuff for scan request and kick to scan*/
			ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_2040_BSS_COEXIST);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq, 0);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;
			/* Set InfoReq = 1, So after scan , alwats sebd 20/40 Coexistence frame to AP*/
			pAd->CommonCfg.BSSCoexist2040.field.InfoReq = 1;
			RTMP_MLME_HANDLER(pAd);
		}

		DBGPRINT(RT_DEBUG_TRACE, (" LastOneSecTotalTxCount/LastOneSecRxOkDataCnt  = %d/%d \n", 
							pAd->RalinkCounters.LastOneSecTotalTxCount, 
							pAd->RalinkCounters.LastOneSecRxOkDataCnt));	
	}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

	return;
}

/* Link down report*/
VOID LinkDownExec(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	if (pAd != NULL)
	{
		MLME_DISASSOC_REQ_STRUCT   DisassocReq;
		
		if ((pAd->StaCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED) &&
			(INFRA_ON(pAd)))
		{
			DBGPRINT(RT_DEBUG_TRACE, ("LinkDownExec(): disassociate with current AP...\n"));
			DisassocParmFill(pAd, &DisassocReq, pAd->CommonCfg.Bssid, REASON_DISASSOC_STA_LEAVING);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE, MT2_MLME_DISASSOC_REQ, 
						sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq, 0);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;

			RTMP_IndicateMediaState(pAd, NdisMediaStateDisconnected);
		    pAd->ExtraInfo = GENERAL_LINK_DOWN;
		}	
	}
}

/* IRQL = DISPATCH_LEVEL*/
VOID MlmeAutoScan(
	IN PRTMP_ADAPTER pAd)
{
	/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request*/
	if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Driver auto scan\n"));
		MlmeEnqueue(pAd, 
					MLME_CNTL_STATE_MACHINE, 
					OID_802_11_BSSID_LIST_SCAN, 
					pAd->MlmeAux.AutoReconnectSsidLen, 
					pAd->MlmeAux.AutoReconnectSsid, 0);
		RTMP_MLME_HANDLER(pAd);
	}
}
	
/* IRQL = DISPATCH_LEVEL*/
VOID MlmeAutoReconnectLastSSID(
	IN PRTMP_ADAPTER pAd)
{
	if (pAd->StaCfg.bAutoConnectByBssid)
	{	
		DBGPRINT(RT_DEBUG_TRACE, ("Driver auto reconnect to last OID_802_11_BSSID setting - %02X:%02X:%02X:%02X:%02X:%02X\n",
									pAd->MlmeAux.Bssid[0],
									pAd->MlmeAux.Bssid[1],
									pAd->MlmeAux.Bssid[2],
									pAd->MlmeAux.Bssid[3],
									pAd->MlmeAux.Bssid[4],
									pAd->MlmeAux.Bssid[5]));

		pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
		MlmeEnqueue(pAd,
			 MLME_CNTL_STATE_MACHINE,
			 OID_802_11_BSSID,
			 MAC_ADDR_LEN,
			 pAd->MlmeAux.Bssid, 0);

		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;

		RTMP_MLME_HANDLER(pAd);
	}
	/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request*/
	else if ((pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) && 
		(MlmeValidateSSID(pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen) == TRUE))
	{
		NDIS_802_11_SSID OidSsid;
		OidSsid.SsidLength = pAd->MlmeAux.AutoReconnectSsidLen;
		NdisMoveMemory(OidSsid.Ssid, pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen);

		DBGPRINT(RT_DEBUG_TRACE, ("Driver auto reconnect to last OID_802_11_SSID setting - %s, len - %d\n", pAd->MlmeAux.AutoReconnectSsid, pAd->MlmeAux.AutoReconnectSsidLen));
		MlmeEnqueue(pAd, 
					MLME_CNTL_STATE_MACHINE, 
					OID_802_11_SSID, 
					sizeof(NDIS_802_11_SSID), 
					&OidSsid, 0);
		RTMP_MLME_HANDLER(pAd);
	}
}


/*
	==========================================================================
	Description:
		This routine checks if there're other APs out there capable for
		roaming. Caller should call this routine only when Link up in INFRA mode
		and channel quality is below CQI_GOOD_THRESHOLD.

	IRQL = DISPATCH_LEVEL

	Output:
	==========================================================================
 */
VOID MlmeCheckForRoaming(
	IN PRTMP_ADAPTER pAd,
	IN ULONG	Now32)
{
	USHORT	   i;
	BSS_TABLE  *pRoamTab = &pAd->MlmeAux.RoamTab;
	BSS_ENTRY  *pBss;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeCheckForRoaming\n"));
	/* put all roaming candidates into RoamTab, and sort in RSSI order*/
	BssTableInit(pRoamTab);
	for (i = 0; i < pAd->ScanTab.BssNr; i++)
	{
		pBss = &pAd->ScanTab.BssEntry[i];

		if (RTMP_TIME_AFTER(Now32, pBss->LastBeaconRxTime + pAd->StaCfg.BeaconLostTime))
			continue;	 /* AP disappear*/
		if (pBss->Rssi <= RSSI_THRESHOLD_FOR_ROAMING)
			continue;	 /* RSSI too weak. forget it.*/
		if (MAC_ADDR_EQUAL(pBss->Bssid, pAd->CommonCfg.Bssid))
			continue;	 /* skip current AP*/
		if (pBss->Rssi < (pAd->StaCfg.RssiSample.LastRssi0 + RSSI_DELTA))
			continue;	 /* only AP with stronger RSSI is eligible for roaming*/

		/* AP passing all above rules is put into roaming candidate table		 */
		NdisMoveMemory(&pRoamTab->BssEntry[pRoamTab->BssNr], pBss, sizeof(BSS_ENTRY));
		pRoamTab->BssNr += 1;
	}

	if (pRoamTab->BssNr > 0)
	{
		/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request*/
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
		{
			pAd->RalinkCounters.PoorCQIRoamingCount ++;
			DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Roaming attempt #%ld\n", pAd->RalinkCounters.PoorCQIRoamingCount));
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_MLME_ROAMING_REQ, 0, NULL, 0);
			RTMP_MLME_HANDLER(pAd);
		}
	}
	DBGPRINT(RT_DEBUG_TRACE, ("<== MlmeCheckForRoaming(# of candidate= %d)\n",pRoamTab->BssNr));   
}

/*
	==========================================================================
	Description:
		This routine checks if there're other APs out there capable for
		roaming. Caller should call this routine only when link up in INFRA mode
		and channel quality is below CQI_GOOD_THRESHOLD.

	IRQL = DISPATCH_LEVEL

	Output:
	==========================================================================
 */
BOOLEAN MlmeCheckForFastRoaming(
	IN	PRTMP_ADAPTER	pAd)
{
	USHORT		i;
	BSS_TABLE	*pRoamTab = &pAd->MlmeAux.RoamTab;
	BSS_ENTRY	*pBss;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeCheckForFastRoaming\n"));
	/* put all roaming candidates into RoamTab, and sort in RSSI order*/
	BssTableInit(pRoamTab);
	for (i = 0; i < pAd->ScanTab.BssNr; i++)
	{
		pBss = &pAd->ScanTab.BssEntry[i];

        if ((pBss->Rssi <= -50) && (pBss->Channel == pAd->CommonCfg.Channel))
			continue;	 /* RSSI too weak. forget it.*/
		if (MAC_ADDR_EQUAL(pBss->Bssid, pAd->CommonCfg.Bssid))
			continue;	 /* skip current AP*/
		if (!SSID_EQUAL(pBss->Ssid, pBss->SsidLen, pAd->CommonCfg.Ssid, pAd->CommonCfg.SsidLen))
			continue;	 /* skip different SSID*/
        if (pBss->Rssi < (RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.LastRssi0, pAd->StaCfg.RssiSample.LastRssi1, pAd->StaCfg.RssiSample.LastRssi2) + RSSI_DELTA)) 
			continue;	 /* skip AP without better RSSI*/
		
        DBGPRINT(RT_DEBUG_TRACE, ("LastRssi0 = %d, pBss->Rssi = %d\n", RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.LastRssi0, pAd->StaCfg.RssiSample.LastRssi1, pAd->StaCfg.RssiSample.LastRssi2), pBss->Rssi));
		/* AP passing all above rules is put into roaming candidate table		 */
		NdisMoveMemory(&pRoamTab->BssEntry[pRoamTab->BssNr], pBss, sizeof(BSS_ENTRY));
		pRoamTab->BssNr += 1;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<== MlmeCheckForFastRoaming (BssNr=%d)\n", pRoamTab->BssNr));
	if (pRoamTab->BssNr > 0)
	{
		/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request*/
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
		{
			pAd->RalinkCounters.PoorCQIRoamingCount ++;
			DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Roaming attempt #%ld\n", pAd->RalinkCounters.PoorCQIRoamingCount));
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_MLME_ROAMING_REQ, 0, NULL, 0);
			RTMP_MLME_HANDLER(pAd);
			return TRUE;
		}
	}

	return FALSE;
}

VOID MlmeSetTxRate(
	IN PRTMP_ADAPTER		pAd,
	IN PMAC_TABLE_ENTRY		pEntry,
	IN PRTMP_TX_RATE_SWITCH	pTxRate)
{
	UCHAR	MaxMode = MODE_OFDM;
	
#ifdef DOT11_N_SUPPORT
	MaxMode = MODE_HTGREENFIELD;

	if (pTxRate->STBC && (pAd->StaCfg.MaxHTPhyMode.field.STBC) && (pAd->Antenna.field.TxPath == 2))
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_USE;
	else
#endif /* DOT11_N_SUPPORT */
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_NONE;

	if (pTxRate->CurrMCS < MCS_AUTO)
		pAd->StaCfg.HTPhyMode.field.MCS = pTxRate->CurrMCS;

	if (pAd->StaCfg.HTPhyMode.field.MCS > 7)
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_NONE; 
	
   	if (ADHOC_ON(pAd))
	{
		/* If peer adhoc is b-only mode, we can't send 11g rate.*/
		pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
		pEntry->HTPhyMode.field.STBC	= STBC_NONE;

		
		/* For Adhoc MODE_CCK, driver will use AdhocBOnlyJoined flag to roll back to B only if necessary*/
		
		pEntry->HTPhyMode.field.MODE	= pTxRate->Mode;
		pEntry->HTPhyMode.field.ShortGI	= pAd->StaCfg.HTPhyMode.field.ShortGI;
		pEntry->HTPhyMode.field.MCS		= pAd->StaCfg.HTPhyMode.field.MCS;

		/* Patch speed error in status page*/
		pAd->StaCfg.HTPhyMode.field.MODE = pEntry->HTPhyMode.field.MODE;
	}
	else
    {
#ifdef DOT11_N_SUPPORT
        if ((pAd->CommonCfg.RegTransmitSetting.field.HTMODE == HTMODE_GF) &&
			(pAd->MlmeAux.HtCapability.HtCapInfo.GF == HTMODE_GF))
            pAd->StaCfg.HTPhyMode.field.MODE = MODE_HTGREENFIELD;
		else
#endif /* DOT11_N_SUPPORT */		
		if (pTxRate->Mode <= MaxMode)
		pAd->StaCfg.HTPhyMode.field.MODE = pTxRate->Mode;

#ifdef DOT11_N_SUPPORT
        if (pTxRate->ShortGI && (pAd->StaCfg.MaxHTPhyMode.field.ShortGI))
			pAd->StaCfg.HTPhyMode.field.ShortGI = GI_400;
		else
#endif /* DOT11_N_SUPPORT */
			pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;

#ifdef DOT11_N_SUPPORT
		/* Reexam each bandwidth's SGI support.*/
		if (pAd->StaCfg.HTPhyMode.field.ShortGI == GI_400)
		{
			if ((pEntry->HTPhyMode.field.BW == BW_20) && (!CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_SGI20_CAPABLE)))
				pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
			if ((pEntry->HTPhyMode.field.BW == BW_40) && (!CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_SGI40_CAPABLE)))
				pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
		}

        /* Turn RTS/CTS rate to 6Mbps.*/
		if ((pEntry->HTPhyMode.field.MCS == 0) && (pAd->StaCfg.HTPhyMode.field.MCS != 0))
		{
			pEntry->HTPhyMode.field.MCS		= pAd->StaCfg.HTPhyMode.field.MCS;
			if (pAd->MacTab.fAnyBASession)
			{
				AsicUpdateProtect(pAd, HT_FORCERTSCTS, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);
			}
			else
			{
				AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);
			}
		}
		else if ((pEntry->HTPhyMode.field.MCS == 8) && (pAd->StaCfg.HTPhyMode.field.MCS != 8))
		{
			pEntry->HTPhyMode.field.MCS		= pAd->StaCfg.HTPhyMode.field.MCS;
			if (pAd->MacTab.fAnyBASession)
			{
				AsicUpdateProtect(pAd, HT_FORCERTSCTS, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);
			}
			else
			{
				AsicUpdateProtect(pAd, pAd->MlmeAux.AddHtInfo.AddHtInfo2.OperaionMode, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);
			}
		}
		else if ((pEntry->HTPhyMode.field.MCS != 0) && (pAd->StaCfg.HTPhyMode.field.MCS == 0))
		{
			AsicUpdateProtect(pAd, HT_RTSCTS_6M, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);

		}
		else if ((pEntry->HTPhyMode.field.MCS != 8) && (pAd->StaCfg.HTPhyMode.field.MCS == 8))
		{
			AsicUpdateProtect(pAd, HT_RTSCTS_6M, ALLN_SETPROTECT, TRUE, (BOOLEAN)pAd->MlmeAux.AddHtInfo.AddHtInfo2.NonGfPresent);
		}
#endif /* DOT11_N_SUPPORT */
        
		pEntry->HTPhyMode.field.STBC	= pAd->StaCfg.HTPhyMode.field.STBC;
		pEntry->HTPhyMode.field.ShortGI	= pAd->StaCfg.HTPhyMode.field.ShortGI;
		pEntry->HTPhyMode.field.MCS		= pAd->StaCfg.HTPhyMode.field.MCS;
		pEntry->HTPhyMode.field.MODE	= pAd->StaCfg.HTPhyMode.field.MODE;
    }
	
    pAd->LastTxRate = (USHORT)(pEntry->HTPhyMode.word);
}

/*
	==========================================================================
	Description:
		This routine calculates the acumulated TxPER of eaxh TxRate. And 
		according to the calculation result, change CommonCfg.TxRate which 
		is the stable TX Rate we expect the Radio situation could sustained. 

		CommonCfg.TxRate will change dynamically within {RATE_1/RATE_6, MaxTxRate} 
	Output:
		CommonCfg.TxRate - 

	IRQL = DISPATCH_LEVEL

	NOTE:
		call this routine every second
	==========================================================================
 */
VOID MlmeDynamicTxRateSwitching(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR					UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx;
	ULONG					i, AccuTxTotalCnt = 0, TxTotalCnt;
	ULONG					TxErrorRatio = 0;
	BOOLEAN					bTxRateChanged = FALSE, bUpgradeQuality = FALSE;
	PRTMP_TX_RATE_SWITCH	pCurrTxRate, pNextTxRate = NULL;
	PUCHAR					pTable;
	UCHAR					TableSize = 0;
	UCHAR					InitTxRateIdx = 0, TrainUp, TrainDown;
	CHAR					Rssi, RssiOffset = 0;
	TX_STA_CNT1_STRUC		StaTx1;
	TX_STA_CNT0_STRUC		TxStaCnt0;
	ULONG					TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	MAC_TABLE_ENTRY			*pEntry;
	RSSI_SAMPLE				*pRssi = &pAd->StaCfg.RssiSample;
	UCHAR					tmpTxRate = 0;
#ifdef DOT11N_SS3_SUPPORT
	PRTMP_TX_RATE_SWITCH	pTempTxRate = NULL;
#endif /* DOT11N_SS3_SUPPORT */
#ifdef AGS_SUPPORT
	AGS_STATISTICS_INFO		AGSStatisticsInfo = {0};
#endif /* AGS_SUPPORT */

#ifdef RALINK_ATE
	if (ATE_ON(pAd))
	{
		return;
	}
#endif /* RALINK_ATE */

	/* Update statistic counter*/
	RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
	RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);
	pAd->bUpdateBcnCntDone = TRUE;
	TxRetransmit = StaTx1.field.TxRetransmit;
	TxSuccess = StaTx1.field.TxSuccess;
	TxFailCount = TxStaCnt0.field.TxFailCount;
	TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

	pAd->RalinkCounters.OneSecTxRetryOkCount += StaTx1.field.TxRetransmit;
	pAd->RalinkCounters.OneSecTxNoRetryOkCount += StaTx1.field.TxSuccess;
	pAd->RalinkCounters.OneSecTxFailCount += TxStaCnt0.field.TxFailCount;

#ifdef STATS_COUNT_SUPPORT
	pAd->WlanCounters.TransmittedFragmentCount.u.LowPart += StaTx1.field.TxSuccess;
	pAd->WlanCounters.RetryCount.u.LowPart += StaTx1.field.TxRetransmit;
	pAd->WlanCounters.FailedCount.u.LowPart += TxStaCnt0.field.TxFailCount;
#endif /* STATS_COUNT_SUPPORT */
		

	
	/* walk through MAC table, see if need to change AP's TX rate toward each entry*/
	
   	for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) 
	{
		pEntry = &pAd->MacTab.Content[i];
		
		if (IS_ENTRY_NONE(pEntry))
			continue;

	/* check if this entry need to switch rate automatically*/
		if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pEntry) == FALSE)
			continue;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);

#ifdef NEW_RATE_ADAPT_SUPPORT
		if (pTable == RateSwitchTable11N3S)
		{
			MlmeDynamicTxRateSwitchingAdapt(pAd, i);
			continue;
		}
#endif /* NEW_RATE_ADAPT_SUPPORT */

		if ((pAd->MacTab.Size == 1) || IS_ENTRY_DLS(pEntry))
		{
			Rssi = RTMPMaxRssi(pAd, 
							   pRssi->AvgRssi0, 
							   pRssi->AvgRssi1, 
							   pRssi->AvgRssi2);
					
			/* if no traffic in the past 1-sec period, don't change TX rate,*/
			/* but clear all bad history. because the bad history may affect the next */
			/* Chariot throughput test*/
			AccuTxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount + 
						 pAd->RalinkCounters.OneSecTxRetryOkCount + 
						 pAd->RalinkCounters.OneSecTxFailCount;
			
			if (TxTotalCnt)
				TxErrorRatio = ((TxRetransmit + TxFailCount) * 100) / TxTotalCnt;

#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				
				/* Gather the statistics information*/
				
				AGSStatisticsInfo.RSSI = Rssi;
				AGSStatisticsInfo.TxErrorRatio = TxErrorRatio;
				AGSStatisticsInfo.AccuTxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxSuccess = TxSuccess;
				AGSStatisticsInfo.TxRetransmit = TxRetransmit;
				AGSStatisticsInfo.TxFailCount = TxFailCount;
			}
#endif /* AGS_SUPPORT */
		}
		else
		{
			if (INFRA_ON(pAd) && (i == 1))
				Rssi = RTMPMaxRssi(pAd, 
								   pRssi->AvgRssi0, 
								   pRssi->AvgRssi1, 
								   pRssi->AvgRssi2);
			else
				Rssi = RTMPMaxRssi(pAd, 
								   pEntry->RssiSample.AvgRssi0, 
								   pEntry->RssiSample.AvgRssi1, 
								   pEntry->RssiSample.AvgRssi2);
			
			TxTotalCnt = pEntry->OneSecTxNoRetryOkCount + 
				 pEntry->OneSecTxRetryOkCount + 
				 pEntry->OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio = ((pEntry->OneSecTxRetryOkCount + pEntry->OneSecTxFailCount) * 100) / TxTotalCnt;

#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				
				/* Gather the statistics information*/
				
				AGSStatisticsInfo.RSSI = Rssi;
				AGSStatisticsInfo.TxErrorRatio = TxErrorRatio;
				AGSStatisticsInfo.AccuTxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxSuccess = pEntry->OneSecTxNoRetryOkCount;
				AGSStatisticsInfo.TxRetransmit = pEntry->OneSecTxRetryOkCount;
				AGSStatisticsInfo.TxFailCount = pEntry->OneSecTxFailCount;
			}
#endif /* AGS_SUPPORT */
		}

		if (TxTotalCnt)
		{
			/*
				Three AdHoc connections can not work normally if one AdHoc connection is disappeared from a heavy traffic environment generated by ping tool
				We force to set LongRtyLimit and ShortRtyLimit to 0 to stop retransmitting packet, after a while, resoring original settings
			*/
			if (TxErrorRatio == 100)
			{
				TX_RTY_CFG_STRUC	TxRtyCfg,TxRtyCfgtmp;
				ULONG	Index;
				UINT32	MACValue;			

				RTMP_IO_READ32(pAd, TX_RTY_CFG, &TxRtyCfg.word);
				TxRtyCfgtmp.word = TxRtyCfg.word; 
				TxRtyCfg.field.LongRtyLimit = 0x0;
				TxRtyCfg.field.ShortRtyLimit = 0x0;
				RTMP_IO_WRITE32(pAd, TX_RTY_CFG, TxRtyCfg.word);

				RTMPusecDelay(1);

				Index = 0;
				MACValue = 0;
				do 
				{
					RTMP_IO_READ32(pAd, TXRXQ_PCNT, &MACValue);
					if ((MACValue & 0xffffff) == 0)
						break;
					Index++;
					RTMPusecDelay(1000);
				}while((Index < 330)&&(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)));

				RTMP_IO_READ32(pAd, TX_RTY_CFG, &TxRtyCfg.word);
				TxRtyCfg.field.LongRtyLimit = TxRtyCfgtmp.field.LongRtyLimit;
				TxRtyCfg.field.ShortRtyLimit = TxRtyCfgtmp.field.ShortRtyLimit;
				RTMP_IO_WRITE32(pAd, TX_RTY_CFG, TxRtyCfg.word);							
			}		
		}

		CurrRateIdx = pEntry->CurrTxRateIndex;

		/*MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);*/

#ifdef AGS_SUPPORT
		if (AGS_IS_USING(pAd, pTable))
		{
/*
			*ppTable = AGS3x3HTRateTable;
			*pTableSize = AGS3x3HTRateTable[0];
			*pInitTxRateIdx = AGS3x3HTRateTable[1];
*/
			
			/* The dynamic Tx rate switching for AGS (Adaptive Group Switching)*/
			
			MlmeDynamicTxRateSwitchingAGS(pAd, pEntry, pTable, TableSize, &AGSStatisticsInfo, InitTxRateIdx);

			continue; /* Skip the remaining procedure of the old Tx rate switching*/
		}
#endif /* AGS_SUPPORT */

		if (CurrRateIdx >= TableSize)
		{
			CurrRateIdx = TableSize - 1;
		}

		/* When switch from Fixed rate -> auto rate, the REAL TX rate might be different from pAd->CommonCfg.TxRateIndex.*/
		/* So need to sync here.*/
		pCurrTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(CurrRateIdx+1)*5];
		if ((pEntry->HTPhyMode.field.MCS != pCurrTxRate->CurrMCS) 
			/*&& (pAd->StaCfg.bAutoTxRateSwitch == TRUE)*/
			)
		{
			
			/* Need to sync Real Tx rate and our record. */
			/* Then return for next DRS.*/
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(InitTxRateIdx+1)*5];
			pEntry->CurrTxRateIndex = InitTxRateIdx;
			MlmeSetTxRate(pAd, pEntry, pCurrTxRate);

			/* reset all OneSecTx counters*/
			RESET_ONE_SEC_TX_CNT(pEntry);
			continue;
		}

		/* decide the next upgrade rate and downgrade rate, if any*/
		if ((CurrRateIdx > 0) && (CurrRateIdx < (TableSize - 1)))
		{
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx -1;
		}
		else if (CurrRateIdx == 0)
		{
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx;
		}
		else if (CurrRateIdx == (TableSize - 1))
		{
			UpRateIdx = CurrRateIdx;
			DownRateIdx = CurrRateIdx - 1;
		}

		pCurrTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(CurrRateIdx+1)*5];

#ifdef DOT11_N_SUPPORT

		if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX))
		{
			TrainUp		= (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
			TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
		}
		else
#endif /* DOT11_N_SUPPORT */
		{
			TrainUp		= pCurrTxRate->TrainUp;
			TrainDown	= pCurrTxRate->TrainDown;
		}

		/*pAd->DrsCounters.LastTimeTxRateChangeAction = pAd->DrsCounters.LastSecTxRateChangeAction;*/
		
		
		/* Keep the last time TxRateChangeAction status.*/
		
		pEntry->LastTimeTxRateChangeAction = pEntry->LastSecTxRateChangeAction;
		
		

		
		/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI*/
		/*         (criteria copied from RT2500 for Netopia case)*/
		
		if (TxTotalCnt <= 15)
		{
			CHAR	idx = 0;
			UCHAR	TxRateIdx;
			UCHAR	MCS0 = 0, MCS1 = 0, MCS2 = 0, MCS3 = 0, MCS4 = 0,  MCS5 =0, MCS6 = 0, MCS7 = 0;			
	        UCHAR	MCS12 = 0, MCS13 = 0, MCS14 = 0, MCS15 = 0;
#ifdef DOT11N_SS3_SUPPORT
			UCHAR	MCS20 = 0, MCS21 = 0, MCS22 = 0, MCS23 = 0; /* 3*3*/
#endif /* DOT11N_SS3_SUPPORT */

			/* check the existence and index of each needed MCS*/
			while (idx < pTable[0])
			{
				pCurrTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(idx+1)*5];

				if (pCurrTxRate->CurrMCS == MCS_0)
				{
					MCS0 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_1)
				{
					MCS1 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_2)
				{
					MCS2 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_3)
				{
					MCS3 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_4)
				{
					MCS4 = idx;
				}
	            else if (pCurrTxRate->CurrMCS == MCS_5)
	            {
	                MCS5 = idx;
	            }
	            else if (pCurrTxRate->CurrMCS == MCS_6)
	            {
	                MCS6 = idx;
	            }
				else if ((pCurrTxRate->CurrMCS == MCS_7) && (pCurrTxRate->ShortGI == GI_800))	/* prevent the highest MCS using short GI when 1T and low throughput*/
				{
					MCS7 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_12)
				{
					MCS12 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_13)
				{
					MCS13 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_14)
				{
					MCS14 = idx;
				}
				else if ((pCurrTxRate->CurrMCS == MCS_15) && (pCurrTxRate->ShortGI == GI_800))	/*we hope to use ShortGI as initial rate, however Atheros's chip has bugs when short GI*/
				{
					MCS15 = idx;
				}
#ifdef DOT11N_SS3_SUPPORT
				else if (pCurrTxRate->CurrMCS == MCS_20) /* 3*3*/
				{
					MCS20 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_21)
				{
					MCS21 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_22)
				{
					MCS22 = idx;
				}
				else if (pCurrTxRate->CurrMCS == MCS_23)
				{
					MCS23 = idx;
				}
#endif /* DOT11N_SS3_SUPPORT */
				idx ++;
			}

			if (pAd->LatchRfRegs.Channel <= 14)
			{
				if (pAd->NicConfig2.field.ExternalLNAForG)
				{
					RssiOffset = 2;
				}
				else
				{
					RssiOffset = 5;
				}
			}
			else
			{
				if (pAd->NicConfig2.field.ExternalLNAForA)
				{
					RssiOffset = 5;
				}
				else
				{
					RssiOffset = 8;
				}
			}
#ifdef DOT11_N_SUPPORT			
#ifdef DOT11N_SS3_SUPPORT
			/*if (MCS15)*/
			if ((pTable == RateSwitchTable11BGN3S) ||
				(pTable == RateSwitchTable11BGN3SForABand) ||
				(pTable == RateSwitchTable11N3S))
			{/* N mode with 3 stream  3*3*/
				if (MCS23 && (Rssi >= (-66+RssiOffset)))
					TxRateIdx = MCS23;
				else if (MCS22 && (Rssi >= (-70+RssiOffset)))
					TxRateIdx = MCS22;
				else if (MCS21 && (Rssi >= (-72+RssiOffset)))
					TxRateIdx = MCS21;
				else if (MCS20 && (Rssi >= (-74+RssiOffset)))
					TxRateIdx = MCS20;
				else if (MCS13 && (Rssi >= (-76+RssiOffset)))
					TxRateIdx = MCS13;
				else if (MCS12 && (Rssi >= (-78+RssiOffset)))
					TxRateIdx = MCS12;
				else if (MCS4 && (Rssi >= (-82+RssiOffset)))
				TxRateIdx = MCS4;
				else if (MCS3 && (Rssi >= (-84+RssiOffset)))
				TxRateIdx = MCS3;
				else if (MCS2 && (Rssi >= (-86+RssiOffset)))
				TxRateIdx = MCS2;
				else if (MCS1 && (Rssi >= (-88+RssiOffset)))
				TxRateIdx = MCS1;
			else
				TxRateIdx = MCS0;
		}
		else
#endif /* DOT11N_SS3_SUPPORT */
		if ((pTable == RateSwitchTable11BGN2S) || (pTable == RateSwitchTable11BGN2SForABand) ||(pTable == RateSwitchTable11N2S) ||(pTable == RateSwitchTable11N2SForABand)) /* 3*3*/
			{/* N mode with 2 stream*/
				if (MCS15 && (Rssi >= (-70+RssiOffset)))
					TxRateIdx = MCS15;
				else if (MCS14 && (Rssi >= (-72+RssiOffset)))
					TxRateIdx = MCS14;
				else if (MCS13 && (Rssi >= (-76+RssiOffset)))
					TxRateIdx = MCS13;
				else if (MCS12 && (Rssi >= (-78+RssiOffset)))
					TxRateIdx = MCS12;
				else if (MCS4 && (Rssi >= (-82+RssiOffset)))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi >= (-84+RssiOffset)))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi >= (-86+RssiOffset)))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi >= (-88+RssiOffset)))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			}
			else if ((pTable == RateSwitchTable11BGN1S) || (pTable == RateSwitchTable11N1S))
			{/* N mode with 1 stream*/
				if (MCS7 && (Rssi > (-72+RssiOffset)))
					TxRateIdx = MCS7;
				else if (MCS6 && (Rssi > (-74+RssiOffset)))
					TxRateIdx = MCS6;
				else if (MCS5 && (Rssi > (-77+RssiOffset)))
					TxRateIdx = MCS5;
				else if (MCS4 && (Rssi > (-79+RssiOffset)))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi > (-81+RssiOffset)))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi > (-83+RssiOffset)))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi > (-86+RssiOffset)))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			}
			else
#endif /* DOT11_N_SUPPORT */
			{/* Legacy mode*/
				if (MCS7 && (Rssi > -70))
					TxRateIdx = MCS7;
				else if (MCS6 && (Rssi > -74))
					TxRateIdx = MCS6;
				else if (MCS5 && (Rssi > -78))
					TxRateIdx = MCS5;
				else if (MCS4 && (Rssi > -82))
					TxRateIdx = MCS4;
				else if (MCS4 == 0)	/* for B-only mode*/
					TxRateIdx = MCS3;
				else if (MCS3 && (Rssi > -85))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi > -87))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi > -90))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			}


	/*		if (TxRateIdx != pAd->CommonCfg.TxRateIndex)*/
			{
				pEntry->CurrTxRateIndex = TxRateIdx;
				pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pEntry->CurrTxRateIndex+1)*5];
				MlmeSetTxRate(pAd, pEntry, pNextTxRate);
			}

			NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
			pEntry->fLastSecAccordingRSSI = TRUE;
			/* reset all OneSecTx counters*/
			RESET_ONE_SEC_TX_CNT(pEntry);

			continue;
		}

		if (pEntry->fLastSecAccordingRSSI == TRUE)
		{
			pEntry->fLastSecAccordingRSSI = FALSE;
			pEntry->LastSecTxRateChangeAction = 0;
			/* reset all OneSecTx counters*/
			RESET_ONE_SEC_TX_CNT(pEntry);

			continue;
		}

		do
		{
			BOOLEAN	bTrainUpDown = FALSE;
			
			pEntry->CurrTxRateStableTime ++;

			/* downgrade TX quality if PER >= Rate-Down threshold*/
			if (TxErrorRatio >= TrainDown)
			{
				bTrainUpDown = TRUE;
				pEntry->TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
			}
			/* upgrade TX quality if PER <= Rate-Up threshold*/
			else if (TxErrorRatio <= TrainUp)
			{
				bTrainUpDown = TRUE;
				bUpgradeQuality = TRUE;
				if (pEntry->TxQuality[CurrRateIdx])
					pEntry->TxQuality[CurrRateIdx] --;  /* quality very good in CurrRate*/

				if (pEntry->TxRateUpPenalty)
					pEntry->TxRateUpPenalty --;
				else if (pEntry->TxQuality[UpRateIdx])
					pEntry->TxQuality[UpRateIdx] --;    /* may improve next UP rate's quality*/
			}

			pEntry->PER[CurrRateIdx] = (UCHAR)TxErrorRatio;

			if (bTrainUpDown)
			{
				/* perform DRS - consider TxRate Down first, then rate up.*/
				if ((CurrRateIdx != DownRateIdx) && (pEntry->TxQuality[CurrRateIdx] >= DRS_TX_QUALITY_WORST_BOUND))
				{
					pEntry->CurrTxRateIndex = DownRateIdx;
				}
				else if ((CurrRateIdx != UpRateIdx) && (pEntry->TxQuality[UpRateIdx] <= 0))
				{
					pEntry->CurrTxRateIndex = UpRateIdx;
				}
			}
		} while (FALSE);

		/* if rate-up happen, clear all bad history of all TX rates*/
		if (pEntry->CurrTxRateIndex > CurrRateIdx)
		{
			pEntry->CurrTxRateStableTime = 0;
			pEntry->TxRateUpPenalty = 0;
			pEntry->LastSecTxRateChangeAction = 1; /* rate UP*/
			NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);

			
			/* For TxRate fast train up*/
			/* */
			if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
			{
				RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

				pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
			}
			bTxRateChanged = TRUE;
		}
		/* if rate-down happen, only clear DownRate's bad history*/
		else if (pEntry->CurrTxRateIndex < CurrRateIdx)
		{
			pEntry->CurrTxRateStableTime = 0;
			pEntry->TxRateUpPenalty = 0;           /* no penalty*/
			pEntry->LastSecTxRateChangeAction = 2; /* rate DOWN*/
			pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
			pEntry->PER[pEntry->CurrTxRateIndex] = 0;

			
			/* For TxRate fast train down*/
			/* */
			if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
			{
				RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

				pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
			}
			bTxRateChanged = TRUE;
		}
		else
		{
			pEntry->LastSecTxRateChangeAction = 0; /* rate no change*/
			bTxRateChanged = FALSE;
		}

		pEntry->LastTxOkCount = TxSuccess;

		tmpTxRate = pEntry->CurrTxRateIndex;

		/*turn off RDG when 3s and rx count > tx count*5*/
#ifdef DOT11N_SS3_SUPPORT
		if (((pTable == RateSwitchTable11BGN3S) || (pTable == RateSwitchTable11BGN3SForABand) || (pTable == RateSwitchTable11N3S)) &&
				(pAd->RalinkCounters.OneSecReceivedByteCount > 50000) &&
				(pAd->RalinkCounters.OneSecTransmittedByteCount > 50000) &&
				CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE))
		{
			TX_LINK_CFG_STRUC	TxLinkCfg;
			ULONG				TxOpThres;

			pTempTxRate = (PRTMP_TX_RATE_SWITCH)(&pTable[(tmpTxRate + 1)*5]);
			RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);

			if ((pAd->RalinkCounters.OneSecReceivedByteCount > (pAd->RalinkCounters.OneSecTransmittedByteCount * 5)) &&
				(pTempTxRate->CurrMCS != 23) &&
				(pTempTxRate->ShortGI != 1))
			{
				DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: Rx(%d) > 5*Tx(%d)\n",
						pAd->RalinkCounters.OneSecReceivedByteCount, pAd->RalinkCounters.OneSecTransmittedByteCount));
				if (TxLinkCfg.field.TxRDGEn == 1)
				{
					TxLinkCfg.field.TxRDGEn = 0;
					RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
					RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxOpThres);
					TxOpThres |= 0xff00;
					RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxOpThres);
					DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: RDG off!\n"));
				}
			}
			else
			{
				DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: Rx(%d) <= 5*Tx(%d)\n",
						pAd->RalinkCounters.OneSecReceivedByteCount, pAd->RalinkCounters.OneSecTransmittedByteCount));
				if (TxLinkCfg.field.TxRDGEn == 0)
				{
					TxLinkCfg.field.TxRDGEn = 1;
					RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
					RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxOpThres);
					TxOpThres &= 0xffff00ff;
					RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxOpThres);
					DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: RDG on!\n"));
				}
			}
		}
#endif /* DOT11N_SS3_SUPPORT */

		pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(tmpTxRate+1)*5];
		if (bTxRateChanged && pNextTxRate)
		{
			MlmeSetTxRate(pAd, pEntry, pNextTxRate);
		}

		/* reset all OneSecTx counters*/
		RESET_ONE_SEC_TX_CNT(pEntry);
	}
}

#ifdef TXBF_SUPPORT
VOID staETxBFProbing(
 	IN PRTMP_ADAPTER pAd,
	IN 	MAC_TABLE_ENTRY		*pEntry,
	IN PRTMP_TX_RATE_SWITCH	pNextTxRate)
{
		if (pEntry->eTxBfEnCond !=0)
		{
			/* if ETXBF_EN_COND is not defined, there are two opportunities to activate the procedure:*/
			/* 1: mfb changes*/
			/* 2: timer expires*/
			if (((pEntry->eTxBfEnCond == 1 || pEntry->eTxBfEnCond == 3) && (pEntry->bfState == READY_FOR_SNDG0 && pEntry->noSndgCnt >= pEntry->noSndgCntThrd))
			    ||(pEntry->eTxBfEnCond == 2 && pEntry->LastSecTxRateChangeAction !=0))
			{
/*to be uncommented!!!				txSndgSameMcs(pAd, pEntry, pNextTxRate->CurrMCS);*/
			}
			else if (pEntry->bfState == READY_FOR_SNDG0)
			{
				pEntry->noSndgCnt ++;
			}
			else
				pEntry->noSndgCnt = 0;
		}
}
#endif /* TXBF_SUPPORT */

#ifdef NEW_RATE_ADAPT_SUPPORT
VOID MlmeDynamicTxRateSwitchingAdapt(
    IN PRTMP_ADAPTER pAd,
    IN ULONG i)
{
	UCHAR			  UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx;
	ULONG			  AccuTxTotalCnt = 0, TxTotalCnt;
	ULONG			  TxErrorRatio = 0;
	BOOLEAN			  bTxRateChanged, bUpgradeQuality = FALSE;
	PRTMP_TX_RATE_SWITCH_3S	  pCurrTxRate, pNextTxRateIdx;
	PRTMP_TX_RATE_SWITCH	  pNextTxRate = NULL;
	PUCHAR			  pTable;
	UCHAR			  TableSize = 0;
	UCHAR			  InitTxRateIdx = 0, TrainUp, TrainDown;
	CHAR			  Rssi, RssiOffset = 0;
	TX_STA_CNT1_STRUC	  StaTx1;
	TX_STA_CNT0_STRUC	  TxStaCnt0;
	ULONG			  TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	MAC_TABLE_ENTRY		  *pEntry;
	ULONG			  phyRateLimit20 = 0;
	UCHAR 			  tmpTxRate = 0;
	PRTMP_TX_RATE_SWITCH_3S   pTempTxRate = NULL;

#ifdef RALINK_ATE
	if (ATE_ON(pAd))
	{
		return;
	}
#endif /* RALINK_ATE */

	pEntry = &pAd->MacTab.Content[i];

	MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);

	if ((pAd->MacTab.Size == 1) || (IS_ENTRY_DLS(pEntry)))
	{
		/*Rssi = RTMPMaxRssi(pAd, (CHAR)pAd->StaCfg.RssiSample.AvgRssi0, (CHAR)pAd->StaCfg.RssiSample.AvgRssi1, (CHAR)pAd->StaCfg.RssiSample.AvgRssi2);*/
		/* Sync with Rory.*/

		if(pAd->Antenna.field.RxPath == 3)
		{
			Rssi = ((CHAR)pAd->StaCfg.RssiSample.AvgRssi0 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi1 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi2)/3;
		}
		else if(pAd->Antenna.field.RxPath == 2)
		{
			Rssi = ((CHAR)pAd->StaCfg.RssiSample.AvgRssi0 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi1)>>1;
		}
		else
		{
			Rssi = (CHAR)pAd->StaCfg.RssiSample.AvgRssi0;
		}

		/* Update statistic counter*/
		RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
		RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);
		pAd->bUpdateBcnCntDone = TRUE;
		TxRetransmit = StaTx1.field.TxRetransmit;
		TxSuccess = StaTx1.field.TxSuccess;
		TxFailCount = TxStaCnt0.field.TxFailCount;
		TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

		pAd->RalinkCounters.OneSecTxRetryOkCount += StaTx1.field.TxRetransmit;
		pAd->RalinkCounters.OneSecTxNoRetryOkCount += StaTx1.field.TxSuccess;
		pAd->RalinkCounters.OneSecTxFailCount += TxStaCnt0.field.TxFailCount;
		pAd->WlanCounters.TransmittedFragmentCount.u.LowPart += StaTx1.field.TxSuccess;
		pAd->WlanCounters.RetryCount.u.LowPart += StaTx1.field.TxRetransmit;
		pAd->WlanCounters.FailedCount.u.LowPart += TxStaCnt0.field.TxFailCount;

		/* if no traffic in the past 1-sec period, don't change TX rate,*/
		/* but clear all bad history. because the bad history may affect the next*/
		/* Chariot throughput test*/
		AccuTxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount +
						pAd->RalinkCounters.OneSecTxRetryOkCount +
						pAd->RalinkCounters.OneSecTxFailCount;

		if (TxTotalCnt)
			TxErrorRatio = ((TxRetransmit + TxFailCount) * 100) / TxTotalCnt;
	}
	else
	{
			/*Rssi = RTMPMaxRssi(pAd, (CHAR)pEntry->RssiSample.AvgRssi0, (CHAR)pEntry->RssiSample.AvgRssi1, (CHAR)pEntry->RssiSample.AvgRssi2);*/

			/* Sync with Rory.*/
			if(pAd->Antenna.field.RxPath == 3)
			{
				Rssi = ((CHAR)pEntry->RssiSample.AvgRssi0 + (CHAR)pEntry->RssiSample.AvgRssi1 + (CHAR)pEntry->RssiSample.AvgRssi2)/3;
			}
			else if(pAd->Antenna.field.RxPath == 2)
			{
				Rssi = ((CHAR)pEntry->RssiSample.AvgRssi0 + (CHAR)pEntry->RssiSample.AvgRssi1)>>1;
			}
			else
			{
				Rssi = (CHAR)pEntry->RssiSample.AvgRssi0;
			}

			TxTotalCnt = pEntry->OneSecTxNoRetryOkCount +
				 pEntry->OneSecTxRetryOkCount +
				 pEntry->OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio = ((pEntry->OneSecTxRetryOkCount + pEntry->OneSecTxFailCount) * 100) / TxTotalCnt;
	}


	CurrRateIdx = pEntry->CurrTxRateIndex;

	if (CurrRateIdx >= TableSize)
	{
		CurrRateIdx = TableSize - 1;
	}


#if defined (CONFIG_RALINK_RT2883) || defined (CONFIG_RALINK_RT3883)
	phyRateLimit20 = pEntry->HTPhyMode.field.BW==BW_20? pAd->CommonCfg.PhyRateLimit: pAd->CommonCfg.PhyRateLimit*13/27;
#endif

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(CurrRateIdx+1)*10];

	/* decide the next upgrade rate and downgrade rate, if any*/
	do
	{
		if ((pEntry->HTCapability.MCSSet[2] == 0xff) && (pAd->CommonCfg.TxStream == 3))
		{
			switch (pEntry->mcsGroup)
			{
				case 0:/*improvement: use round robin mcs when group == 0*/
					UpRateIdx = pCurrTxRate->upMcs3;
					if (pEntry->TxQuality[UpRateIdx] > pEntry->TxQuality[pCurrTxRate->upMcs2] && pCurrTxRate->upMcs2 != pCurrTxRate->ItemNo)
						UpRateIdx = pCurrTxRate->upMcs2;

					if (pEntry->TxQuality[UpRateIdx] > pEntry->TxQuality[pCurrTxRate->upMcs1] && pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo)
						UpRateIdx = pCurrTxRate->upMcs1;
					break;

				case 3:
					UpRateIdx = pCurrTxRate->upMcs3;
					break;

				case 2:
					UpRateIdx = pCurrTxRate->upMcs2;
					break;

				case 1:
					UpRateIdx = pCurrTxRate->upMcs1;
					break;

				default:
					DBGPRINT_RAW(RT_DEBUG_TRACE, ("wrong mcsGroup value\n"));
					break;
			}

			if ((pEntry->mcsGroup == 0) && 
				(((pEntry->TxQuality[pCurrTxRate->upMcs3] > pEntry->TxQuality[pCurrTxRate->upMcs2]) && (pCurrTxRate->upMcs2 != pCurrTxRate->ItemNo)) ||
					((pEntry->TxQuality[pCurrTxRate->upMcs3] > pEntry->TxQuality[pCurrTxRate->upMcs1]) && (pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo))))
					DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: Before- mcsGroup=%d, TxQuality2[%d]=%d,  TxQuality1[%d]=%d, TxQuality0[%d]=%d\n",
						pEntry->mcsGroup,
						pCurrTxRate->upMcs3,
						pEntry->TxQuality[pCurrTxRate->upMcs3],
						pCurrTxRate->upMcs2,
						pEntry->TxQuality[pCurrTxRate->upMcs2],
						pCurrTxRate->upMcs1,
						pEntry->TxQuality[pCurrTxRate->upMcs1]));
		}
		else if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
					(pEntry->HTCapability.MCSSet[1] == 0xff) &&
					(pAd->CommonCfg.TxStream > 1) &&
					((pAd->CommonCfg.TxStream == 2) || (pEntry->HTCapability.MCSSet[2] == 0x0)))
		{
			switch (pEntry->mcsGroup)
			{
				case 0:
					UpRateIdx = pCurrTxRate->upMcs2;
					if (pEntry->TxQuality[UpRateIdx] > pEntry->TxQuality[pCurrTxRate->upMcs1] && pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo)
						UpRateIdx = pCurrTxRate->upMcs1;
					break;

				case 2:
					UpRateIdx = pCurrTxRate->upMcs2;
					break;

				case 1:
					UpRateIdx = pCurrTxRate->upMcs1;
					break;

				default:
					DBGPRINT_RAW(RT_DEBUG_TRACE, ("wrong mcsGroup value %d\n", pEntry->mcsGroup));
					break;
			}

			if ((pEntry->mcsGroup == 0) && 
				(pEntry->TxQuality[pCurrTxRate->upMcs2] > pEntry->TxQuality[pCurrTxRate->upMcs1]) &&
				(pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo))
			{
				DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: Before- mcsGroup=%d,  TxQuality1[%d]=%d, TxQuality0[%d]=%d\n",
						pEntry->mcsGroup,
						pCurrTxRate->upMcs2,
						pEntry->TxQuality[pCurrTxRate->upMcs2],
						pCurrTxRate->upMcs1,
						pEntry->TxQuality[pCurrTxRate->upMcs1]));
			}
		}
		else
		{
			switch (pEntry->mcsGroup)
			{
				case 1:
				case 0:
					UpRateIdx = pCurrTxRate->upMcs1;
					break;
	
				default:
					DBGPRINT_RAW(RT_DEBUG_TRACE, ("wrong mcsGroup value %d\n", pEntry->mcsGroup));
					break;
			}
		}

		if (UpRateIdx == pEntry->CurrTxRateIndex)
		{
			pEntry->mcsGroup = 0;
			break;
		}
	
		if ((pEntry->TxQuality[UpRateIdx] > 0) && (pEntry->mcsGroup > 0))
		{
			pEntry->mcsGroup --;
		}
		else
		{
			break;
		}
	} while (1);
	
	DownRateIdx = pCurrTxRate->downMcs;

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(CurrRateIdx+1)*10];/*repeated line and thus redundant!!!*/

	/* Debug Option: Use lower thresholds*/
	if (pAd->CommonCfg.DebugFlags & DBF_LOW_RA_THRESHOLDS)
	{
		TrainUp		= 2;
		TrainDown	= 10;
	}
	else
#ifdef DOT11_N_SUPPORT
	if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX) && pEntry->perThrdAdj == 1)
	{
		TrainUp		= (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
		TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		TrainUp		= pCurrTxRate->TrainUp;
		TrainDown	= pCurrTxRate->TrainDown;
	}

	/*pAd->DrsCounters.LastTimeTxRateChangeAction = pAd->DrsCounters.LastSecTxRateChangeAction;*/

	
	/* Keep the last time TxRateChangeAction status.*/
	
	pEntry->LastTimeTxRateChangeAction = pEntry->LastSecTxRateChangeAction;

#ifdef RELASE_EXCLUDE
	DBGPRINT(RT_DEBUG_TRACE, ("DRS: TxSuccess=%lu, TxRetransmit=%lu, TxFailCount=%lu, TxErrorRatio=%lu\n",
			TxSuccess, TxRetransmit, TxFailCount, TxErrorRatio));

	DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: Before- CurrTxRateIdx=%d, MCS=%d, STBC=%d, ShortGI=%d, Mode=%d, TrainUp=%d, TrainDown=%d, NextUp=%d, NextDown=%d, CurrMCS=%d, mcsGroup=%d, PER=%lu%%, Retry=%lu, NoRetry=%lu\n",
			CurrRateIdx,
			pCurrTxRate->CurrMCS,
			pCurrTxRate->STBC,
			pCurrTxRate->ShortGI,
			pCurrTxRate->Mode,
			TrainUp,
			TrainDown,
			UpRateIdx,
			DownRateIdx,
			pEntry->HTPhyMode.field.MCS,
			pEntry->mcsGroup,
			TxErrorRatio,
			TxRetransmit,
			TxSuccess));
#endif /* RELASE_EXCLUDE */

	if (pEntry->fLastChangeAccordingMfb == TRUE)
	{
		pEntry->fLastChangeAccordingMfb = FALSE;
		pEntry->LastSecTxRateChangeAction = 0;/*not increment or decrement --> set to 0*/
		DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: MCS is according to MFB, and ignore tuning this sec \n"));
		
		/* reset all OneSecTx counters*/
		RESET_ONE_SEC_TX_CNT(pEntry);
/*		continue;*/
		return;
	}

	
	/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI*/
	/*         (criteria copied from RT2500 for Netopia case)*/
	
	if (TxTotalCnt <= 15)
	{
		CHAR	idx = 0;
		UCHAR	TxRateIdx;
		/*UCHAR	MCS0 = 0, MCS1 = 0, MCS2 = 0, MCS3 = 0, MCS4 = 0, MCS7 = 0, MCS12 = 0, MCS13 = 0, MCS14 = 0, MCS15 = 0;*/
		UCHAR	MCS0 = 0, MCS1 = 0, MCS2 = 0, MCS3 = 0, MCS4 = 0,  MCS5 =0, MCS6 = 0, MCS7 = 0;
		UCHAR	MCS8 = 0, MCS9 = 0, MCS10 = 0, MCS11 = 0;
		UCHAR	MCS12 = 0, MCS13 = 0, MCS14 = 0, MCS15 = 0;
		UCHAR	MCS16 = 0, MCS17 = 0, MCS18 = 0, MCS19 = 0;
		UCHAR	MCS20 = 0, MCS21 = 0, MCS22 = 0, MCS23 = 0; /* 3*3*/

		/* check the existence and index of each needed MCS*/
		while (idx < pTable[0])
		{
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(idx+1)*10];

			if (pCurrTxRate->CurrMCS == MCS_0)
			{
				MCS0 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_1)
			{
				MCS1 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_2)
			{
				MCS2 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_3)
			{
				MCS3 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_4)
			{
				MCS4 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_5)
			{
				MCS5 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_6)
			{
				MCS6 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_7)
			{
				MCS7 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_8 && pAd->CommonCfg.TxStream > 1)
			{
				MCS8 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_9 && pAd->CommonCfg.TxStream > 1)
			{
				MCS9 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_10 && pAd->CommonCfg.TxStream > 1)
			{
				MCS10 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_11 && pAd->CommonCfg.TxStream > 1)
			{
				MCS11 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_12 && pAd->CommonCfg.TxStream > 1)
			{
				MCS12 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_13 && pAd->CommonCfg.TxStream > 1)
			{
				MCS13 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_14 && pAd->CommonCfg.TxStream > 1)
			{
				MCS14 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_15 && pAd->CommonCfg.TxStream > 1)
			{
				MCS15 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_16 && pAd->CommonCfg.TxStream > 2)
			{
				MCS16 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_17 && pAd->CommonCfg.TxStream > 2)
			{
				MCS17 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_18 && pAd->CommonCfg.TxStream > 2)
			{
				MCS18 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_19 && pAd->CommonCfg.TxStream > 2)
			{
				MCS19 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_20 && pAd->CommonCfg.TxStream > 2)
			{
				MCS20 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_21 && pAd->CommonCfg.TxStream > 2)
			{
				MCS21 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_22 && pAd->CommonCfg.TxStream > 2)
			{
				MCS22 = idx;
			}
			else if ((pCurrTxRate->CurrMCS == MCS_23) && (pAd->CommonCfg.TxStream > 2) && (pCurrTxRate->ShortGI == GI_800))	/*we hope to use ShortGI as initial rate*/
			{
				MCS23 = idx;
			}

			idx ++;
		}

		if (pAd->LatchRfRegs.Channel <= 14)
		{
			if (pAd->NicConfig2.field.ExternalLNAForG)
			{
				RssiOffset = 2;
			}
			else if (pTable == RateSwitchTable11N3S)
			{
				RssiOffset = 0;
			}
			else
			{
				RssiOffset = 5;
			}
		}
		else
		{
			if (pAd->NicConfig2.field.ExternalLNAForA)
			{
				RssiOffset = 5;
			}
			else if (pTable == RateSwitchTable11N3S)
			{
				RssiOffset = 2;
			}
			else
			{
				RssiOffset = 8;
			}
		}

		/* Debug option: Add 6 dB of margin*/
		if (pAd->CommonCfg.DebugFlags & DBF_INIT_MCS_MARGIN)
			RssiOffset += 6;

		/* Debug Option: Disable highest MCSs when picking initial MCS based on RSSI*/
		if (pAd->CommonCfg.DebugFlags & DBF_INIT_MCS_DIS1)
			MCS23 = MCS15 = MCS7 = 0;
		if (pAd->CommonCfg.DebugFlags & DBF_INIT_MCS_DIS2)
			MCS22 = MCS14 = MCS6 = 0;

		/* Debug Option: If PHY limit disable all PHY > 85. Also check for 65 and 40 Mbps*/
		if (phyRateLimit20 != 0)
		{
			MCS13 = MCS14 = MCS15 = MCS20 = MCS21 = MCS22 = MCS23 = 0;

			if (phyRateLimit20 <= 65)
				MCS7 = MCS12 = MCS19 = 0;

			if (phyRateLimit20 <= 40)
				MCS5 = MCS6 = MCS11 = MCS18 = 0;

			if (phyRateLimit20 <= 20)
				MCS3 = MCS4 = MCS9 = MCS10 = MCS17 = 0;
		}

#ifdef DOT11_N_SUPPORT
			/*if (MCS15)*/
		if ((pTable == RateSwitchTable11BGN3S) ||
			(pTable == RateSwitchTable11BGN3SForABand) ||
			(pTable == RateSwitchTable11N3S))
		{/* N mode with 3 stream  3*3*/
			/* Note we may be using RateSwitchTable11N3S and have only 1 or 2 TX antennas so we need to handle those cases*/
			if (MCS23 && (Rssi >= (-64+RssiOffset)))
				TxRateIdx = MCS23;
			else if (MCS22 && (Rssi >= (-66+RssiOffset)))
				TxRateIdx = MCS22;
			else if (MCS21 && (Rssi >= (-68+RssiOffset)))
				TxRateIdx = MCS21;
			else if (MCS15 && (Rssi >= (-70+RssiOffset)))
				TxRateIdx = MCS15;
			else if (MCS14 && (Rssi >= (-72+RssiOffset)))
				TxRateIdx = MCS14;
			else if (MCS13 && (Rssi >= (-76+RssiOffset)))
				TxRateIdx = MCS13;
			else if (MCS12 && (Rssi >= (-78+RssiOffset)))
				TxRateIdx = MCS12;
			else if (MCS4 && (Rssi >= (-82+RssiOffset)))
				TxRateIdx = MCS4;
			else if (MCS3 && (Rssi >= (-84+RssiOffset)))
				TxRateIdx = MCS3;
			else if (MCS2 && (Rssi >= (-86+RssiOffset)))
				TxRateIdx = MCS2;
			else if (MCS1 && (Rssi >= (-88+RssiOffset)))
				TxRateIdx = MCS1;
			else
				TxRateIdx = MCS0;
		}
/*		else if ((pTable == RateSwitchTable11BGN2S) || (pTable == RateSwitchTable11BGN2SForABand) ||(pTable == RateSwitchTable11N2S) ||(pTable == RateSwitchTable11N2SForABand) || (pTable == RateSwitchTable))*/
		else if ((pTable == RateSwitchTable11BGN2S) ||
				(pTable == RateSwitchTable11BGN2SForABand) ||
				(pTable == RateSwitchTable11N2S) ||
				(pTable == RateSwitchTable11N2SForABand)) /* 3*3*/
		{/* N mode with 2 stream*/
			if (MCS15 && (Rssi >= (-70+RssiOffset)))
				TxRateIdx = MCS15;
			else if (MCS14 && (Rssi >= (-72+RssiOffset)))
				TxRateIdx = MCS14;
			else if (MCS13 && (Rssi >= (-76+RssiOffset)))
				TxRateIdx = MCS13;
			else if (MCS12 && (Rssi >= (-78+RssiOffset)))
				TxRateIdx = MCS12;
			else if (MCS4 && (Rssi >= (-82+RssiOffset)))
				TxRateIdx = MCS4;
			else if (MCS3 && (Rssi >= (-84+RssiOffset)))
				TxRateIdx = MCS3;
			else if (MCS2 && (Rssi >= (-86+RssiOffset)))
				TxRateIdx = MCS2;
			else if (MCS1 && (Rssi >= (-88+RssiOffset)))
				TxRateIdx = MCS1;
			else
				TxRateIdx = MCS0;
		}
		else if ((pTable == RateSwitchTable11BGN1S) || (pTable == RateSwitchTable11N1S))
		{/* N mode with 1 stream*/
			if (MCS7 && (Rssi > (-72+RssiOffset)))
				TxRateIdx = MCS7;
			else if (MCS6 && (Rssi > (-74+RssiOffset)))
				TxRateIdx = MCS6;
			else if (MCS5 && (Rssi > (-77+RssiOffset)))
				TxRateIdx = MCS5;
			else if (MCS4 && (Rssi > (-79+RssiOffset)))
				TxRateIdx = MCS4;
			else if (MCS3 && (Rssi > (-81+RssiOffset)))
				TxRateIdx = MCS3;
			else if (MCS2 && (Rssi > (-83+RssiOffset)))
				TxRateIdx = MCS2;
			else if (MCS1 && (Rssi > (-86+RssiOffset)))
				TxRateIdx = MCS1;
			else
				TxRateIdx = MCS0;
		}
		else if (pTable == RateSwitchTable11N3S)
		{/* N mode with 3 stream*/
			if ((pEntry->HTCapability.MCSSet[2] == 0xff) && (pAd->CommonCfg.TxStream == 3))
			{
				if (MCS23 && (Rssi > (-72+RssiOffset)))
					TxRateIdx = MCS23;
				else if (MCS22 && (Rssi > (-74+RssiOffset)))
					TxRateIdx = MCS22;
				else if (MCS21 && (Rssi > (-77+RssiOffset)))
					TxRateIdx = MCS21;
				else if (MCS20 && (Rssi > (-79+RssiOffset)))
					TxRateIdx = MCS20;
				else if (MCS19 && (Rssi > (-81+RssiOffset)))
					TxRateIdx = MCS19;
				else if (MCS18 && (Rssi > (-83+RssiOffset)))
					TxRateIdx = MCS18;
				else if (MCS17 && (Rssi > (-86+RssiOffset)))
					TxRateIdx = MCS17;
				else
					TxRateIdx = MCS16;

				pEntry->mcsGroup = 3;
			}
			else if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
					(pEntry->HTCapability.MCSSet[1] == 0xff) &&
					(pAd->CommonCfg.TxStream > 1) &&
					((pAd->CommonCfg.TxStream == 2) || (pEntry->HTCapability.MCSSet[2] == 0x0)))
			{
				if (MCS15 && (Rssi > (-72+RssiOffset)))
					TxRateIdx = MCS15;
				else if (MCS14 && (Rssi > (-74+RssiOffset)))
					TxRateIdx = MCS14;
				else if (MCS13 && (Rssi > (-77+RssiOffset)))
					TxRateIdx = MCS13;
				else if (MCS12 && (Rssi > (-79+RssiOffset)))
					TxRateIdx = MCS12;
				else if (MCS11 && (Rssi > (-81+RssiOffset)))
					TxRateIdx = MCS11;
				else if (MCS10 && (Rssi > (-83+RssiOffset)))
					TxRateIdx = MCS10;
				else if (MCS9 && (Rssi > (-86+RssiOffset)))
					TxRateIdx = MCS9;
				else
					TxRateIdx = MCS8;

				pEntry->mcsGroup = 2;
		 	}
			else
			{
				if (MCS7 && (Rssi > (-72+RssiOffset)))
					TxRateIdx = MCS7;
				else if (MCS6 && (Rssi > (-74+RssiOffset)))
					TxRateIdx = MCS6;
				else if (MCS5 && (Rssi > (-77+RssiOffset)))
					TxRateIdx = MCS5;
				else if (MCS4 && (Rssi > (-79+RssiOffset)))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi > (-81+RssiOffset)))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi > (-83+RssiOffset)))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi > (-86+RssiOffset)))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;

				pEntry->mcsGroup = 1;
		 	}
		}
		else
#endif /* DOT11_N_SUPPORT */
		{/* Legacy mode*/
			if (MCS7 && (Rssi > -70))
				TxRateIdx = MCS7;
			else if (MCS6 && (Rssi > -74))
				TxRateIdx = MCS6;
			else if (MCS5 && (Rssi > -78))
				TxRateIdx = MCS5;
			else if (MCS4 && (Rssi > -82))
				TxRateIdx = MCS4;
			else if (MCS4 == 0)	/* for B-only mode*/
				TxRateIdx = MCS3;
			else if (MCS3 && (Rssi > -85))
				TxRateIdx = MCS3;
			else if (MCS2 && (Rssi > -87))
				TxRateIdx = MCS2;
			else if (MCS1 && (Rssi > -90))
				TxRateIdx = MCS1;
			else
				TxRateIdx = MCS0;
		}

		(pEntry->fewPktsCnt) ++;
		DBGPRINT(RT_DEBUG_WARN, ("f-s%d\n", pEntry->fewPktsCnt));

		if (pEntry->fewPktsCnt == FEW_PKTS_CNT_THRD)
		{
			pEntry->fewPktsCnt = 0;
		/*	if (TxRateIdx != pAd->CommonCfg.TxRateIndex)*/
			{
				pEntry->lastRateIdx = pEntry->CurrTxRateIndex;

				if ((pEntry->HTCapability.MCSSet[2] == 0xff) && (pAd->CommonCfg.TxStream == 3))
					pEntry->mcsGroup = 3;
				else if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
						(pEntry->HTCapability.MCSSet[1] == 0xff) &&
						(pAd->CommonCfg.TxStream > 1) &&
						(pAd->CommonCfg.TxStream == 2 || pEntry->HTCapability.MCSSet[2] == 0x0))
					pEntry->mcsGroup = 2;
				else
					pEntry->mcsGroup = 1;

				pEntry->CurrTxRateIndex = TxRateIdx;
				pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pEntry->CurrTxRateIndex+1)*10];

				MlmeSetTxRate(pAd, pEntry, pNextTxRate);
				DBGPRINT(RT_DEBUG_WARN, ("c-s%d\n", pNextTxRate->CurrMCS));
			}

			NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
			pEntry->fLastSecAccordingRSSI = TRUE;
	#ifdef RELASE_EXCLUDE
			DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: AccuTxTotalCnt <= 15, switch TxRateIndex as (%d) according to RSSI(%d), RssiOffset=%d\n", pEntry->CurrTxRateIndex, Rssi, RssiOffset));
	#endif /* RELASE_EXCLUDE */
		}

		/* reset all OneSecTx counters*/
		RESET_ONE_SEC_TX_CNT(pEntry);

		return;
	}

	pEntry->fewPktsCnt = 0;
	if (pEntry->fLastSecAccordingRSSI == TRUE)
	{
		pEntry->fLastSecAccordingRSSI = FALSE;
		pEntry->LastSecTxRateChangeAction = 0;
#ifdef RELASE_EXCLUDE
		DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: MCS is according to RSSI, and ignore tuning this sec \n"));
#endif /* RELASE_EXCLUDE */

		/* reset all OneSecTx counters*/
		RESET_ONE_SEC_TX_CNT(pEntry);

		pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pEntry->CurrTxRateIndex+1)*10];
#ifdef TXBF_SUPPORT
		staETxBFProbing(pAd, pEntry, pNextTxRate);
#endif /* TXBF_SUPPORT */
		return;
	}

	do
	{
		BOOLEAN	bTrainUpDown = FALSE;

		pEntry->CurrTxRateStableTime ++;

		/* downgrade TX quality if PER >= Rate-Down threshold*/
		if (TxErrorRatio >= TrainDown ||
			(phyRateLimit20!=0 && pCurrTxRate->dataRate>=phyRateLimit20) )
		{
			bTrainUpDown = TRUE;
			pEntry->TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
		}
		/* upgrade TX quality if PER <= Rate-Up threshold*/
		else if (TxErrorRatio <= TrainUp)
		{
			bTrainUpDown = TRUE;
			bUpgradeQuality = TRUE;

			if (pEntry->TxQuality[CurrRateIdx])
				pEntry->TxQuality[CurrRateIdx] --;  /* quality very good in CurrRate*/

			if (pEntry->TxRateUpPenalty)
				pEntry->TxRateUpPenalty --;
			else
			{
				if (pEntry->TxQuality[pCurrTxRate->upMcs3] && pCurrTxRate->upMcs3 != CurrRateIdx)
					pEntry->TxQuality[pCurrTxRate->upMcs3] --;

				if (pEntry->TxQuality[pCurrTxRate->upMcs2] && pCurrTxRate->upMcs2 != CurrRateIdx)
					pEntry->TxQuality[pCurrTxRate->upMcs2] --;

				if (pEntry->TxQuality[pCurrTxRate->upMcs1] && pCurrTxRate->upMcs1 != CurrRateIdx)
					pEntry->TxQuality[pCurrTxRate->upMcs1] --;

/*				if (pEntry->TxQuality[UpRateIdx])*/
/*					pEntry->TxQuality[UpRateIdx] --;     may improve next UP rate's quality*/
			}
		}
		else if (pEntry->mcsGroup > 0)/*even if TxErrorRatio > TrainUp*/
		{/*moderate per but some groups are not tried*/
			if (UpRateIdx != 0)
			{
				bTrainUpDown = TRUE;

				if (pEntry->TxQuality[CurrRateIdx])
					pEntry->TxQuality[CurrRateIdx] --;  /* quality very good in CurrRate*/

/*				if (pEntry->TxRateUpPenalty)always == 0, always go to else*/
/*					pEntry->TxRateUpPenalty --;*/
				/*else if (pEntry->TxQuality[UpRateIdx])*/

				if (pEntry->TxQuality[UpRateIdx])
					pEntry->TxQuality[UpRateIdx] --;    /* may improve next UP rate's quality*/
			}
		}

		pEntry->PER[CurrRateIdx] = (UCHAR)TxErrorRatio;

		if (bTrainUpDown)
		{
			PRTMP_TX_RATE_SWITCH_3S pUpRateIdx = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(UpRateIdx+1)*10];

			/* perform DRS - consider TxRate Down first, then rate up.*/
			if ((CurrRateIdx != DownRateIdx) && (pEntry->TxQuality[CurrRateIdx] >= DRS_TX_QUALITY_WORST_BOUND))
			{
				pEntry->CurrTxRateIndex = DownRateIdx;
				pEntry->LastSecTxRateChangeAction = 2; /* rate down*/
			}
			else if ((CurrRateIdx != UpRateIdx) &&
					(pEntry->TxQuality[UpRateIdx] <= 0) &&
					(phyRateLimit20==0 || pUpRateIdx->dataRate<phyRateLimit20))
			{
				pEntry->CurrTxRateIndex = UpRateIdx;
				pEntry->LastSecTxRateChangeAction = 1; /* rate UP*/
			}
		}
	} while (FALSE);

	/* if rate-up happen, clear all bad history of all TX rates*/
	/*if (pEntry->CurrTxRateIndex > CurrRateIdx)*/
	if (pEntry->CurrTxRateIndex != CurrRateIdx && pEntry->LastSecTxRateChangeAction == 1)/*ys*/
	{
#ifdef RELASE_EXCLUDE
		DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: ++TX rate from %d to %d \n", CurrRateIdx, pEntry->CurrTxRateIndex));
#endif /* RELASE_EXCLUDE */
		pEntry->CurrTxRateStableTime = 0;
		pEntry->TxRateUpPenalty = 0;
		pEntry->LastSecTxRateChangeAction = 1; /* rate UP*/
/*		NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);*/
		pNextTxRateIdx = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(pEntry->CurrTxRateIndex+1)*10];
		NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
		pEntry->lastRateIdx = CurrRateIdx;

		
		/* For TxRate fast train up*/
		
		if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
		{
			RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
		}

		bTxRateChanged = TRUE;
	}
	/* if rate-down happen, only clear DownRate's bad history*/
	/*else if (pEntry->CurrTxRateIndex < CurrRateIdx)*/
	else if (pEntry->CurrTxRateIndex != CurrRateIdx && pEntry->LastSecTxRateChangeAction == 2)
	{
#ifdef RELASE_EXCLUDE
		DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: --TX rate from %d to %d \n", CurrRateIdx, pEntry->CurrTxRateIndex));
#endif /* RELASE_EXCLUDE */
		pEntry->CurrTxRateStableTime = 0;
		pEntry->TxRateUpPenalty = 0;           /* no penalty*/
		pEntry->LastSecTxRateChangeAction = 2; /* rate DOWN*/
		pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
		pEntry->PER[pEntry->CurrTxRateIndex] = 0;
		pEntry->lastRateIdx = CurrRateIdx;

		
		/* For TxRate fast train down*/
		
		if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
		{
			RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
		}
		bTxRateChanged = TRUE;
	}
	else
	{
		pEntry->LastSecTxRateChangeAction = 0; /* rate no change*/
		bTxRateChanged = FALSE;
	}

	pEntry->LastTxOkCount = TxSuccess;

	tmpTxRate = pEntry->CurrTxRateIndex;

	/*turn off RDG when 3s and rx count > tx count*5*/
	if (((pTable == RateSwitchTable11BGN3S) || (pTable == RateSwitchTable11BGN3SForABand) || (pTable == RateSwitchTable11N3S)) &&
				pAd->RalinkCounters.OneSecReceivedByteCount > 50000 &&
				pAd->RalinkCounters.OneSecTransmittedByteCount > 50000 &&
				CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_RDG_CAPABLE))
	{
		TX_LINK_CFG_STRUC	TxLinkCfg;
		ULONG				TxOpThres;

		pTempTxRate = (PRTMP_TX_RATE_SWITCH)(&pTable[(tmpTxRate + 1)*10]);
		RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);

		if ((pAd->RalinkCounters.OneSecReceivedByteCount > (pAd->RalinkCounters.OneSecTransmittedByteCount * 5)) &&
			(pTempTxRate->CurrMCS != 23))
		{
			DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: Rx(%d) > 5*Tx(%d)\n",
						pAd->RalinkCounters.OneSecReceivedByteCount, pAd->RalinkCounters.OneSecTransmittedByteCount));

			if (TxLinkCfg.field.TxRDGEn == 1)
			{
				TxLinkCfg.field.TxRDGEn = 0;
				RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
				RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxOpThres);
				TxOpThres |= 0xff00;
				RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxOpThres);
				DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: RDG off!\n"));
			}
		}
		else
		{
			DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: Rx(%d) <= 5*Tx(%d)\n",
						pAd->RalinkCounters.OneSecReceivedByteCount, pAd->RalinkCounters.OneSecTransmittedByteCount));

			if (TxLinkCfg.field.TxRDGEn == 0)
			{
				TxLinkCfg.field.TxRDGEn = 1;
				RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
				RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxOpThres);
				TxOpThres &= 0xffff00ff;
				RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxOpThres);
				DBGPRINT_RAW(RT_DEBUG_WARN,("DRS: RDG on!\n"));
			}
		}
	}

	pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(tmpTxRate+1)*10];
	if (bTxRateChanged && pNextTxRate)
	{
		MlmeSetTxRate(pAd, pEntry, pNextTxRate);
		DBGPRINT(RT_DEBUG_WARN, ("--s%d\n", pNextTxRate->CurrMCS));
	}

	/* reset all OneSecTx counters*/
	RESET_ONE_SEC_TX_CNT(pEntry);
}
#endif /* NEW_RATE_ADAPT_SUPPORT */

/*
	========================================================================
	Routine Description:
		Station side, Auto TxRate faster train up timer call back function.

	Arguments:
		SystemSpecific1			- Not used.
		FunctionContext			- Pointer to our Adapter context.
		SystemSpecific2			- Not used.
		SystemSpecific3			- Not used.

	Return Value:
		None

	========================================================================
*/
VOID StaQuickResponeForRateUpExec(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	PRTMP_ADAPTER			pAd = (PRTMP_ADAPTER)FunctionContext;
	UCHAR					UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx = 0;
	ULONG					TxTotalCnt = 0;
	ULONG					TxErrorRatio = 0;
	BOOLEAN					bTxRateChanged; /*, bUpgradeQuality = FALSE;*/
	PRTMP_TX_RATE_SWITCH	pCurrTxRate, pNextTxRate = NULL;
	PUCHAR					pTable;
	UCHAR					TableSize = 0;
	UCHAR					InitTxRateIdx = 0, TrainUp, TrainDown;
	TX_STA_CNT1_STRUC		StaTx1;
	TX_STA_CNT0_STRUC		TxStaCnt0;
	CHAR					Rssi, ratio;
	ULONG					TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	MAC_TABLE_ENTRY			*pEntry;
	ULONG					i;
#ifdef AGS_SUPPORT
	AGS_STATISTICS_INFO		AGSStatisticsInfo = {0};
#endif /* AGS_SUPPORT */

	pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;

	/*if (pAd->MacTab.Size == 1)*/
	{
		/* Update statistic counter*/
		RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
		RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);

		TxRetransmit = StaTx1.field.TxRetransmit;
		TxSuccess = StaTx1.field.TxSuccess;
		TxFailCount = TxStaCnt0.field.TxFailCount;
		TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

		pAd->RalinkCounters.OneSecTxRetryOkCount += StaTx1.field.TxRetransmit;
		pAd->RalinkCounters.OneSecTxNoRetryOkCount += StaTx1.field.TxSuccess;
		pAd->RalinkCounters.OneSecTxFailCount += TxStaCnt0.field.TxFailCount;

#ifdef STATS_COUNT_SUPPORT
		pAd->WlanCounters.TransmittedFragmentCount.u.LowPart += StaTx1.field.TxSuccess;
		pAd->WlanCounters.RetryCount.u.LowPart += StaTx1.field.TxRetransmit;
		pAd->WlanCounters.FailedCount.u.LowPart += TxStaCnt0.field.TxFailCount;
#endif /* STATS_COUNT_SUPPORT */
	}

    
    /* walk through MAC table, see if need to change AP's TX rate toward each entry*/
    
	for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) 
	{
		pEntry = &pAd->MacTab.Content[i];

		if (IS_ENTRY_NONE(pEntry))
			continue;

		/* check if this entry need to switch rate automatically*/
		if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pEntry) == FALSE)
			continue;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);

#ifdef NEW_RATE_ADAPT_SUPPORT
		if (pTable == RateSwitchTable11N3S)
		{
			StaQuickResponeForRateUpExecAdapt(pAd, i);
			continue;
		}

#endif /* NEW_RATE_ADAPT_SUPPORT */

		if (INFRA_ON(pAd) && (i == 1))
			Rssi = RTMPMaxRssi(pAd, 
							   pAd->StaCfg.RssiSample.AvgRssi0, 
							   pAd->StaCfg.RssiSample.AvgRssi1, 
							   pAd->StaCfg.RssiSample.AvgRssi2);
		else
			Rssi = RTMPMaxRssi(pAd, 
							   pEntry->RssiSample.AvgRssi0, 
							   pEntry->RssiSample.AvgRssi1, 
							   pEntry->RssiSample.AvgRssi2);

	if (pAd->MacTab.Size == 1)
	{
		if (TxTotalCnt)
			TxErrorRatio = ((TxRetransmit + TxFailCount) * 100) / TxTotalCnt;

#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				
				/* Gather the statistics information*/
				
				AGSStatisticsInfo.RSSI = Rssi;
				AGSStatisticsInfo.TxErrorRatio = TxErrorRatio;
				AGSStatisticsInfo.AccuTxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxSuccess = TxSuccess;
				AGSStatisticsInfo.TxRetransmit = TxRetransmit;
				AGSStatisticsInfo.TxFailCount = TxFailCount;
			}
#endif /* AGS_SUPPORT */
	}
	else
	{
			TxTotalCnt = pEntry->OneSecTxNoRetryOkCount + 
				 pEntry->OneSecTxRetryOkCount + 
				 pEntry->OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio = ((pEntry->OneSecTxRetryOkCount + pEntry->OneSecTxFailCount) * 100) / TxTotalCnt;

#ifdef AGS_SUPPORT
			if (SUPPORT_AGS(pAd))
			{
				
				/* Gather the statistics information*/
				
				AGSStatisticsInfo.RSSI = Rssi;
				AGSStatisticsInfo.TxErrorRatio = TxErrorRatio;
				AGSStatisticsInfo.AccuTxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxTotalCnt = TxTotalCnt;
				AGSStatisticsInfo.TxSuccess = pEntry->OneSecTxNoRetryOkCount;
				AGSStatisticsInfo.TxRetransmit = pEntry->OneSecTxRetryOkCount;
				AGSStatisticsInfo.TxFailCount = pEntry->OneSecTxFailCount;
			}
#endif /* AGS_SUPPORT */
	}

		/*CurrRateIdx = pAd->CommonCfg.TxRateIndex;*/
		/*add by woody*/
		CurrRateIdx = pEntry->CurrTxRateIndex;
		

#ifdef AGS_SUPPORT
		if (AGS_IS_USING(pAd, pTable))
		{
			
			/* The dynamic Tx rate switching for AGS (Adaptive Group Switching)*/
			
			StaQuickResponeForRateUpExecAGS(pAd, pEntry, pTable, TableSize, &AGSStatisticsInfo, InitTxRateIdx);
			
			continue; /* Skip the remaining procedure of the old Tx rate switching*/
		}
#endif /* AGS_SUPPORT */

		/* decide the next upgrade rate and downgrade rate, if any*/
		if ((CurrRateIdx > 0) && (CurrRateIdx < (TableSize - 1)))
		{
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx -1;
		}
		else if (CurrRateIdx == 0)
		{
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx;
		}
		else if (CurrRateIdx == (TableSize - 1))
		{
			UpRateIdx = CurrRateIdx;
			DownRateIdx = CurrRateIdx - 1;
		}

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(CurrRateIdx+1)*5];

#ifdef DOT11_N_SUPPORT

	if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX))
	{
		TrainUp		= (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
		TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		TrainUp		= pCurrTxRate->TrainUp;
		TrainDown	= pCurrTxRate->TrainDown;
	}


	
	/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI*/
	/*         (criteria copied from RT2500 for Netopia case)*/
	
	if (TxTotalCnt <= 12)
	{
		NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
		NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);

		if ((pEntry->LastSecTxRateChangeAction == 1) && (CurrRateIdx != DownRateIdx))
		{
			/*pAd->CommonCfg.TxRateIndex = DownRateIdx;*/
			pEntry->CurrTxRateIndex = DownRateIdx;
			pEntry->TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
		}
		else if ((pEntry->LastSecTxRateChangeAction == 2) && (CurrRateIdx != UpRateIdx))
		{
			/*pAd->CommonCfg.TxRateIndex = UpRateIdx;*/
			pEntry->CurrTxRateIndex = UpRateIdx;
		}

		pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pEntry->CurrTxRateIndex+1)*5];
		MlmeSetTxRate(pAd, pEntry, pNextTxRate);
	
		DBGPRINT_RAW(RT_DEBUG_TRACE,("QuickDRS: TxTotalCnt <= 15, train back to original rate \n"));
		return;
	}

	do
	{
		ULONG OneSecTxNoRetryOKRationCount;

		if (pEntry->LastTimeTxRateChangeAction == 0)
			ratio = 5;
		else
			ratio = 4;

		/* downgrade TX quality if PER >= Rate-Down threshold*/
		if (TxErrorRatio >= TrainDown)
		{
			pEntry->TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
		}

		pEntry->PER[CurrRateIdx] = (UCHAR)TxErrorRatio;

		OneSecTxNoRetryOKRationCount = (TxSuccess * ratio);

		/* perform DRS - consider TxRate Down first, then rate up.*/
		if ((pEntry->LastSecTxRateChangeAction == 1) && (CurrRateIdx != DownRateIdx))
		{
			if ((pEntry->LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount)
		{
			/*pAd->CommonCfg.TxRateIndex = DownRateIdx;*/
			pEntry->CurrTxRateIndex = DownRateIdx;
				pEntry->TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
					
			}
				
		}
		else if ((pEntry->LastSecTxRateChangeAction == 2) && (CurrRateIdx != UpRateIdx))
		{
			if ((TxErrorRatio >= 50) || (TxErrorRatio >= TrainDown))
			{
					
			}
			else if ((pEntry->LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount)
			{
				/*pAd->CommonCfg.TxRateIndex = UpRateIdx;*/
				pEntry->CurrTxRateIndex = UpRateIdx;
			}
		}
	}while (FALSE);


	/* if rate-up happen, clear all bad history of all TX rates*/
	if (pEntry->CurrTxRateIndex > CurrRateIdx)
	{
		pEntry->TxRateUpPenalty = 0;
		NdisZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
		NdisZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
			bTxRateChanged = TRUE;
	}
	/* if rate-down happen, only clear DownRate's bad history*/
	else if (pEntry->CurrTxRateIndex < CurrRateIdx)
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE,("QuickDRS: --TX rate from %d to %d \n", CurrRateIdx, pAd->CommonCfg.TxRateIndex));
		
		pEntry->TxRateUpPenalty = 0;           /* no penalty*/
		pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
		pEntry->PER[pEntry->CurrTxRateIndex] = 0;
			bTxRateChanged = TRUE;
	}
	else
	{
		bTxRateChanged = FALSE;
	}

	pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pEntry->CurrTxRateIndex+1)*5];
	if (bTxRateChanged && pNextTxRate)
	{
			MlmeSetTxRate(pAd, pEntry, pNextTxRate);
	}
	}
}

#ifdef NEW_RATE_ADAPT_SUPPORT
VOID StaQuickResponeForRateUpExecAdapt(
	IN PRTMP_ADAPTER	pAd,
	IN ULONG i) 
{
	UCHAR					UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx = 0;
	ULONG					TxTotalCnt;
	ULONG					TxErrorRatio = 0;
	BOOLEAN					bTxRateChanged = TRUE; /*, bUpgradeQuality = FALSE;*/
	PRTMP_TX_RATE_SWITCH_3S	pCurrTxRate;
	PRTMP_TX_RATE_SWITCH	pNextTxRate = NULL;
	PUCHAR					pTable;
	UCHAR					TableSize = 0;
	UCHAR					InitTxRateIdx = 0, TrainUp, TrainDown;
	TX_STA_CNT1_STRUC		StaTx1;
	TX_STA_CNT0_STRUC		TxStaCnt0;
	CHAR					Rssi, ratio;
	ULONG					TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	PMAC_TABLE_ENTRY		pEntry;
	
	pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;

	pEntry = &pAd->MacTab.Content[i];

	MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);

	/*Rssi = RTMPMaxRssi(pAd, (CHAR)pAd->StaCfg.AvgRssi0, (CHAR)pAd->StaCfg.AvgRssi1, (CHAR)pAd->StaCfg.AvgRssi2);*/
	/*
	if (pAd->Antenna.field.TxPath > 1)
		Rssi = (pAd->StaCfg.RssiSample.AvgRssi0 + pAd->StaCfg.RssiSample.AvgRssi1) >> 1;
	else
		Rssi = pAd->StaCfg.RssiSample.AvgRssi0;
	*/
		
	/* Sync with Rory.*/
	if(pAd->Antenna.field.RxPath == 3)
	{
		Rssi = ((CHAR)pAd->StaCfg.RssiSample.AvgRssi0 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi1 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi2)/3;
	}	
	else if(pAd->Antenna.field.RxPath == 2)
	{
		Rssi = ((CHAR)pAd->StaCfg.RssiSample.AvgRssi0 + (CHAR)pAd->StaCfg.RssiSample.AvgRssi1)>>1;
	}
	else
	{
		Rssi = (CHAR)pAd->StaCfg.RssiSample.AvgRssi0;
	}



	pAd->CommonCfg.TxRateIndex = pEntry->CurrTxRateIndex;
	CurrRateIdx = pAd->CommonCfg.TxRateIndex;
	/*CurrRateIdx = pEntry->CurrTxRateIndex;*/

	/*MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize, &InitTxRateIdx);*/

	/* decide the next upgrade rate and downgrade rate, if any*/
	UpRateIdx = DownRateIdx = pEntry->lastRateIdx;

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(CurrRateIdx+1)*10];

#ifdef DOT11_N_SUPPORT

	if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX) && pEntry->perThrdAdj == 1)
	{
		TrainUp		= (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
		TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		TrainUp		= pCurrTxRate->TrainUp;
		TrainDown	= pCurrTxRate->TrainDown;
	}

	if (pAd->MacTab.Size == 1)
	{
		/* Update statistic counter*/
		RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
		RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);

		TxRetransmit = StaTx1.field.TxRetransmit;
		TxSuccess = StaTx1.field.TxSuccess;
		TxFailCount = TxStaCnt0.field.TxFailCount;
		TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

		pAd->RalinkCounters.OneSecTxRetryOkCount += StaTx1.field.TxRetransmit;
		pAd->RalinkCounters.OneSecTxNoRetryOkCount += StaTx1.field.TxSuccess;
		pAd->RalinkCounters.OneSecTxFailCount += TxStaCnt0.field.TxFailCount;
		pAd->WlanCounters.TransmittedFragmentCount.u.LowPart += StaTx1.field.TxSuccess;
		pAd->WlanCounters.RetryCount.u.LowPart += StaTx1.field.TxRetransmit;
		pAd->WlanCounters.FailedCount.u.LowPart += TxStaCnt0.field.TxFailCount;

		if (TxTotalCnt)
			TxErrorRatio = ((TxRetransmit + TxFailCount) * 100) / TxTotalCnt;
	}
	else
	{
		TxTotalCnt = pEntry->OneSecTxNoRetryOkCount + 
				 pEntry->OneSecTxRetryOkCount + 
				 pEntry->OneSecTxFailCount;

		if (TxTotalCnt)
			TxErrorRatio = ((pEntry->OneSecTxRetryOkCount + pEntry->OneSecTxFailCount) * 100) / TxTotalCnt;
	}

	if (pEntry->fLastChangeAccordingMfb == TRUE)
	{
		pEntry->fLastChangeAccordingMfb = FALSE;
		pEntry->LastSecTxRateChangeAction = 0;/*not increment or decrement --> set to 0 */
		DBGPRINT_RAW(RT_DEBUG_TRACE,("DRS: MCS is according to MFB, and ignore tuning this sec \n"));

		/* reset all OneSecTx counters*/
		RESET_ONE_SEC_TX_CNT(pEntry);
		return;
	}


	
	/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI*/
	/*         (criteria copied from RT2500 for Netopia case)*/
	
	if (TxTotalCnt <= 12)
	{
		NdisZeroMemory(pAd->DrsCounters.TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
		NdisZeroMemory(pAd->DrsCounters.PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);

		if ((pAd->DrsCounters.LastSecTxRateChangeAction == 1) && (CurrRateIdx != DownRateIdx))
		{
			pAd->CommonCfg.TxRateIndex = DownRateIdx;
			pAd->DrsCounters.TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
		}
		else if ((pAd->DrsCounters.LastSecTxRateChangeAction == 2) && (CurrRateIdx != UpRateIdx))
		{
			pAd->CommonCfg.TxRateIndex = UpRateIdx;
		}

		DBGPRINT_RAW(RT_DEBUG_TRACE,("QuickDRS: TxTotalCnt <= 15, train back to original rate \n"));
		return;
	}

	do
	{
		ULONG OneSecTxNoRetryOKRationCount;

		if (pAd->DrsCounters.LastTimeTxRateChangeAction == 0)
			ratio = 5;
		else
			ratio = 4;

		/* downgrade TX quality if PER >= Rate-Down threshold*/
		if (TxErrorRatio >= TrainDown)
		{
			pAd->DrsCounters.TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
		}

		pAd->DrsCounters.PER[CurrRateIdx] = (UCHAR)TxErrorRatio;

		OneSecTxNoRetryOKRationCount = (TxSuccess * ratio);

		/* perform DRS - consider TxRate Down first, then rate up.*/
		if ((pAd->DrsCounters.LastSecTxRateChangeAction == 1) && (CurrRateIdx != DownRateIdx))
		{
			if ((pAd->DrsCounters.LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount)
			{
				pAd->CommonCfg.TxRateIndex = DownRateIdx;
				pAd->DrsCounters.TxQuality[CurrRateIdx] = DRS_TX_QUALITY_WORST_BOUND;
					
			}
		}
		else if ((pAd->DrsCounters.LastSecTxRateChangeAction == 2) && (CurrRateIdx != UpRateIdx))
		{
			if ((TxErrorRatio >= 50) || (TxErrorRatio >= TrainDown))
			{
				if ((pEntry->HTCapability.MCSSet[2] == 0xff) && (pAd->CommonCfg.TxStream == 3))
					pEntry->mcsGroup = 3;
				else if ((pEntry->HTCapability.MCSSet[0] == 0xff) &&
						(pEntry->HTCapability.MCSSet[1] == 0xff) &&
						(pAd->CommonCfg.TxStream > 1) &&
						(pAd->CommonCfg.TxStream == 2 || pEntry->HTCapability.MCSSet[2] == 0x0))
					pEntry->mcsGroup = 2;
				else
					pEntry->mcsGroup = 1;
			}
			else if ((pAd->DrsCounters.LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount)
			{
				pAd->CommonCfg.TxRateIndex = UpRateIdx;
			}
		}
	}while (FALSE);


	if (pEntry->LastSecTxRateChangeAction == 1) 
	{/*last action is up*/
		/*looking for the next group with valid mcs*/
		if (pAd->CommonCfg.TxRateIndex != CurrRateIdx && pEntry->mcsGroup > 0)
		{/*move back*/
			pEntry->mcsGroup --;				
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH_3S) &pTable[(DownRateIdx+1)*10];
		}
		/*UpRateIdx is for temp use in this section*/
		switch (pEntry->mcsGroup)
		{
			case 3:
				UpRateIdx = pCurrTxRate->upMcs3;
				break;

			case 2:
				UpRateIdx = pCurrTxRate->upMcs2;
				break;

			case 1:
				UpRateIdx = pCurrTxRate->upMcs1;
				break;

			case 0:
				UpRateIdx = CurrRateIdx;
				break;

			default:
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("wrong mcsGroup value\n"));
				break;
		}

		if (UpRateIdx == pAd->CommonCfg.TxRateIndex)
			pEntry->mcsGroup = 0;

		DBGPRINT_RAW(RT_DEBUG_TRACE,("              QuickDRS: next mcsGroup =%d \n", pEntry->mcsGroup));			
	}

	/* if rate-up happen, clear all bad history of all TX rates*/
	if (pAd->CommonCfg.TxRateIndex != CurrRateIdx && pEntry->LastSecTxRateChangeAction == 2)
	{
		pAd->DrsCounters.TxRateUpPenalty = 0;
		NdisZeroMemory(pAd->DrsCounters.TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
		NdisZeroMemory(pAd->DrsCounters.PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
			bTxRateChanged = TRUE;
	}
	/* if rate-down happen, only clear DownRate's bad history*/
	else if (pAd->CommonCfg.TxRateIndex != CurrRateIdx && pEntry->LastSecTxRateChangeAction == 1) 
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE,("QuickDRS: --TX rate from %d to %d \n", CurrRateIdx, pAd->CommonCfg.TxRateIndex));
		
		pAd->DrsCounters.TxRateUpPenalty = 0;           /* no penalty*/
		pAd->DrsCounters.TxQuality[pAd->CommonCfg.TxRateIndex] = 0;
		pAd->DrsCounters.PER[pAd->CommonCfg.TxRateIndex] = 0;
			bTxRateChanged = TRUE;
	}
	else
	{
		bTxRateChanged = FALSE;
	}

	pNextTxRate = (PRTMP_TX_RATE_SWITCH) &pTable[(pAd->CommonCfg.TxRateIndex+1)*10];
	if (bTxRateChanged && pNextTxRate)
	{
			MlmeSetTxRate(pAd, pEntry, pNextTxRate);
	}
}
#endif /* NEW_RATE_ADAPT_SUPPORT */

/*
	==========================================================================
	Description:
		This routine is executed periodically inside MlmePeriodicExec() after 
		association with an AP.
		It checks if StaCfg.Psm is consistent with user policy (recorded in
		StaCfg.WindowsPowerMode). If not, enforce user policy. However, 
		there're some conditions to consider:
		1. we don't support power-saving in ADHOC mode, so Psm=PWR_ACTIVE all
		   the time when Mibss==TRUE
		2. When link up in INFRA mode, Psm should not be switch to PWR_SAVE
		   if outgoing traffic available in TxRing or MgmtRing.
	Output:
		1. change pAd->StaCfg.Psm to PWR_SAVE or leave it untouched

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID MlmeCheckPsmChange(
	IN PRTMP_ADAPTER pAd,
	IN ULONG	Now32)
{
	ULONG	PowerMode;

	/* condition -*/
	/* 1. Psm maybe ON only happen in INFRASTRUCTURE mode*/
	/* 2. user wants either MAX_PSP or FAST_PSP*/
	/* 3. but current psm is not in PWR_SAVE*/
	/* 4. CNTL state machine is not doing SCANning*/
	/* 5. no TX SUCCESS event for the past 1-sec period*/
	PowerMode = pAd->StaCfg.WindowsPowerMode;

	if (INFRA_ON(pAd) &&
		(PowerMode != Ndis802_11PowerModeCAM) &&
		(pAd->StaCfg.Psm == PWR_ACTIVE) &&
/*		(! RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))*/
		(pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
#ifdef PCIE_PS_SUPPORT
		&& RTMP_TEST_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP)
#endif /* PCIE_PS_SUPPORT */
#ifdef RTMP_MAC_USB
		&&		(pAd->CountDowntoPsm == 0)
#endif /* RTMP_MAC_USB */
		 /*&&
		(pAd->RalinkCounters.OneSecTxNoRetryOkCount == 0) &&
		(pAd->RalinkCounters.OneSecTxRetryOkCount == 0)*/)
	{
		NdisGetSystemUpTime(&pAd->Mlme.LastSendNULLpsmTime);
		pAd->RalinkCounters.RxCountSinceLastNULL = 0;
		RTMP_SET_PSM_BIT(pAd, PWR_SAVE);
		if (!(pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable))
		{
			RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED) ? TRUE:FALSE));
		}
		else
		{
			RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, TRUE);
		}

	}
}

/* IRQL = PASSIVE_LEVEL*/
/* IRQL = DISPATCH_LEVEL*/
VOID MlmeSetPsmBit(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT psm)
{

	pAd->StaCfg.Psm = psm;	  

	DBGPRINT(RT_DEBUG_TRACE, ("MlmeSetPsmBit = %d\n", psm));
}
#endif /* CONFIG_STA_SUPPORT */

/*
	==========================================================================
	Description:
		This routine calculates TxPER, RxPER of the past N-sec period. And 
		according to the calculation result, ChannelQuality is calculated here 
		to decide if current AP is still doing the job. 

		If ChannelQuality is not good, a ROAMing attempt may be tried later.
	Output:
		StaCfg.ChannelQuality - 0..100

	IRQL = DISPATCH_LEVEL

	NOTE: This routine decide channle quality based on RX CRC error ratio.
		Caller should make sure a function call to NICUpdateRawCounters(pAd)
		is performed right before this routine, so that this routine can decide
		channel quality based on the most up-to-date information
	==========================================================================
 */
VOID MlmeCalculateChannelQuality(
	IN PRTMP_ADAPTER pAd,
	IN PMAC_TABLE_ENTRY pMacEntry,
	IN ULONG Now32)
{
	ULONG TxOkCnt, TxCnt, TxPER, TxPRR;
	ULONG RxCnt, RxPER;
	UCHAR NorRssi;
	CHAR  MaxRssi;
	RSSI_SAMPLE *pRssiSample = NULL;
	UINT32 OneSecTxNoRetryOkCount = 0;
	UINT32 OneSecTxRetryOkCount = 0;
	UINT32 OneSecTxFailCount = 0;
	UINT32 OneSecRxOkCnt = 0;
	UINT32 OneSecRxFcsErrCnt = 0;
	ULONG ChannelQuality = 0;  /* 0..100, Channel Quality Indication for Roaming*/
#ifdef CONFIG_STA_SUPPORT
	ULONG BeaconLostTime = pAd->StaCfg.BeaconLostTime;
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
#ifdef CARRIER_DETECTION_SUPPORT /* Roger sync Carrier*/
	/* longer beacon lost time when carrier detection enabled*/
	if (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
	{
		BeaconLostTime = pAd->StaCfg.BeaconLostTime + (pAd->StaCfg.BeaconLostTime/2);
	}
#endif /* CARRIER_DETECTION_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
	if (pAd->OpMode == OPMODE_STA)
	{
		pRssiSample = &pAd->StaCfg.RssiSample;
		OneSecTxNoRetryOkCount = pAd->RalinkCounters.OneSecTxNoRetryOkCount;
		OneSecTxRetryOkCount = pAd->RalinkCounters.OneSecTxRetryOkCount;
		OneSecTxFailCount = pAd->RalinkCounters.OneSecTxFailCount;
		OneSecRxOkCnt = pAd->RalinkCounters.OneSecRxOkCnt;
		OneSecRxFcsErrCnt = pAd->RalinkCounters.OneSecRxFcsErrCnt;
	}
#endif /* CONFIG_STA_SUPPORT */

	if (pRssiSample == NULL)
		return;
	MaxRssi = RTMPMaxRssi(pAd, pRssiSample->LastRssi0,
								pRssiSample->LastRssi1,
								pRssiSample->LastRssi2);

	
	/* calculate TX packet error ratio and TX retry ratio - if too few TX samples, skip TX related statistics*/
	
	TxOkCnt = OneSecTxNoRetryOkCount + OneSecTxRetryOkCount;
	TxCnt = TxOkCnt + OneSecTxFailCount;
	if (TxCnt < 5) 
	{
		TxPER = 0;
		TxPRR = 0;
	}
	else 
	{
		TxPER = (OneSecTxFailCount * 100) / TxCnt; 
		TxPRR = ((TxCnt - OneSecTxNoRetryOkCount) * 100) / TxCnt;
	}

	
	/* calculate RX PER - don't take RxPER into consideration if too few sample*/
	
	RxCnt = OneSecRxOkCnt + OneSecRxFcsErrCnt;
	if (RxCnt < 5)
		RxPER = 0;	
	else
		RxPER = (OneSecRxFcsErrCnt * 100) / RxCnt;

	
	/* decide ChannelQuality based on: 1)last BEACON received time, 2)last RSSI, 3)TxPER, and 4)RxPER*/
	
#ifdef CONFIG_STA_SUPPORT
	if ((pAd->OpMode == OPMODE_STA) &&
		INFRA_ON(pAd) && 
		(OneSecTxNoRetryOkCount < 2) && /* no heavy traffic*/
		RTMP_TIME_AFTER(Now32, pAd->StaCfg.LastBeaconRxTime + BeaconLostTime))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("BEACON lost > %ld msec with TxOkCnt=%ld -> CQI=0\n", BeaconLostTime * (1000 / OS_HZ) , TxOkCnt)); 
		ChannelQuality = 0;
	}
	else
#endif /* CONFIG_STA_SUPPORT */
	{
		/* Normalize Rssi*/
		if (MaxRssi > -40)
			NorRssi = 100;
		else if (MaxRssi < -90)
			NorRssi = 0;
		else
			NorRssi = (MaxRssi + 90) * 2;
		
		/* ChannelQuality = W1*RSSI + W2*TxPRR + W3*RxPER	 (RSSI 0..100), (TxPER 100..0), (RxPER 100..0)*/
		ChannelQuality = (RSSI_WEIGHTING * NorRssi + 
								   TX_WEIGHTING * (100 - TxPRR) + 
								   RX_WEIGHTING* (100 - RxPER)) / 100;
	}


#ifdef CONFIG_STA_SUPPORT
	if (pAd->OpMode == OPMODE_STA)
		pAd->Mlme.ChannelQuality = (ChannelQuality > 100) ? 100 : ChannelQuality;
#endif /* CONFIG_STA_SUPPORT */

	
}


/* IRQL = DISPATCH_LEVEL*/
VOID MlmeSetTxPreamble(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT TxPreamble)
{
	AUTO_RSP_CFG_STRUC csr4;

	
	/* Always use Long preamble before verifiation short preamble functionality works well.*/
	/* Todo: remove the following line if short preamble functionality works*/
	
	/*TxPreamble = Rt802_11PreambleLong;*/
	
	RTMP_IO_READ32(pAd, AUTO_RSP_CFG, &csr4.word);
	if (TxPreamble == Rt802_11PreambleLong)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeSetTxPreamble (= LONG PREAMBLE)\n"));
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED); 
		csr4.field.AutoResponderPreamble = 0;
	}
	else
	{
		/* NOTE: 1Mbps should always use long preamble*/
		DBGPRINT(RT_DEBUG_TRACE, ("MlmeSetTxPreamble (= SHORT PREAMBLE)\n"));
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
		csr4.field.AutoResponderPreamble = 1;
	}

	RTMP_IO_WRITE32(pAd, AUTO_RSP_CFG, csr4.word);
}

/*
    ==========================================================================
    Description:
        Update basic rate bitmap
    ==========================================================================
 */
 
VOID UpdateBasicRateBitmap(
    IN  PRTMP_ADAPTER   pAdapter)
{
    INT  i, j;
                  /* 1  2  5.5, 11,  6,  9, 12, 18, 24, 36, 48,  54 */
    UCHAR rate[] = { 2, 4,  11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    UCHAR *sup_p = pAdapter->CommonCfg.SupRate;
    UCHAR *ext_p = pAdapter->CommonCfg.ExtRate;
    ULONG bitmap = pAdapter->CommonCfg.BasicRateBitmap;

    /* if A mode, always use fix BasicRateBitMap */
    /*if (pAdapter->CommonCfg.Channel == PHY_11A)*/
	if (pAdapter->CommonCfg.Channel > 14)
	{
		if (pAdapter->CommonCfg.BasicRateBitmap & 0xF)
		{
			/* no 11b rate in 5G band */
			pAdapter->CommonCfg.BasicRateBitmapOld = \
										pAdapter->CommonCfg.BasicRateBitmap;
			pAdapter->CommonCfg.BasicRateBitmap &= (~0xF); /* no 11b */
		}

		/* force to 6,12,24M in a-band */
		pAdapter->CommonCfg.BasicRateBitmap |= 0x150; /* 6, 12, 24M */
    }
	else
	{
		/* no need to modify in 2.4G (bg mixed) */
		pAdapter->CommonCfg.BasicRateBitmap = \
										pAdapter->CommonCfg.BasicRateBitmapOld;
	} /* End of if */

    if (pAdapter->CommonCfg.BasicRateBitmap > 4095)
    {
        /* (2 ^ MAX_LEN_OF_SUPPORTED_RATES) -1 */
        return;
    } /* End of if */

    for(i=0; i<MAX_LEN_OF_SUPPORTED_RATES; i++)
    {
        sup_p[i] &= 0x7f;
        ext_p[i] &= 0x7f;
    } /* End of for */

    for(i=0; i<MAX_LEN_OF_SUPPORTED_RATES; i++)
    {
        if (bitmap & (1 << i))
        {
            for(j=0; j<MAX_LEN_OF_SUPPORTED_RATES; j++)
            {
                if (sup_p[j] == rate[i])
                    sup_p[j] |= 0x80;
                /* End of if */
            } /* End of for */

            for(j=0; j<MAX_LEN_OF_SUPPORTED_RATES; j++)
            {
                if (ext_p[j] == rate[i])
                    ext_p[j] |= 0x80;
                /* End of if */
            } /* End of for */
        } /* End of if */
    } /* End of for */
} /* End of UpdateBasicRateBitmap */

/* IRQL = PASSIVE_LEVEL*/
/* IRQL = DISPATCH_LEVEL*/
/* bLinkUp is to identify the inital link speed.*/
/* TRUE indicates the rate update at linkup, we should not try to set the rate at 54Mbps.*/
VOID MlmeUpdateTxRates(
	IN PRTMP_ADAPTER 		pAd,
	IN 	BOOLEAN		 		bLinkUp,
	IN	UCHAR				apidx)
{
	int i, num;
	UCHAR Rate = RATE_6, MaxDesire = RATE_1, MaxSupport = RATE_1;
	UCHAR MinSupport = RATE_54;
	ULONG BasicRateBitmap = 0;
	UCHAR CurrBasicRate = RATE_1;
	UCHAR *pSupRate, SupRateLen, *pExtRate, ExtRateLen;
	PHTTRANSMIT_SETTING		pHtPhy = NULL;
	PHTTRANSMIT_SETTING		pMaxHtPhy = NULL;
	PHTTRANSMIT_SETTING		pMinHtPhy = NULL;	
	BOOLEAN 				*auto_rate_cur_p;
	UCHAR					HtMcs = MCS_AUTO;

	/* find max desired rate*/
	UpdateBasicRateBitmap(pAd);
	
	num = 0;
	auto_rate_cur_p = NULL;
	for (i=0; i<MAX_LEN_OF_SUPPORTED_RATES; i++)
	{
		switch (pAd->CommonCfg.DesireRate[i] & 0x7f)
		{
			case 2:  Rate = RATE_1;   num++;   break;
			case 4:  Rate = RATE_2;   num++;   break;
			case 11: Rate = RATE_5_5; num++;   break;
			case 22: Rate = RATE_11;  num++;   break;
			case 12: Rate = RATE_6;   num++;   break;
			case 18: Rate = RATE_9;   num++;   break;
			case 24: Rate = RATE_12;  num++;   break;
			case 36: Rate = RATE_18;  num++;   break;
			case 48: Rate = RATE_24;  num++;   break;
			case 72: Rate = RATE_36;  num++;   break;
			case 96: Rate = RATE_48;  num++;   break;
			case 108: Rate = RATE_54; num++;   break;
			/*default: Rate = RATE_1;   break;*/
		}
		if (MaxDesire < Rate)  MaxDesire = Rate;
	}

/*===========================================================================*/
/*===========================================================================*/
	do
	{

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			pHtPhy 		= &pAd->StaCfg.HTPhyMode;
			pMaxHtPhy	= &pAd->StaCfg.MaxHTPhyMode;
			pMinHtPhy	= &pAd->StaCfg.MinHTPhyMode;		

			auto_rate_cur_p = &pAd->StaCfg.bAutoTxRateSwitch;
			HtMcs 		= pAd->StaCfg.DesiredTransmitSetting.field.MCS;

			if ((pAd->StaCfg.BssType == BSS_ADHOC) &&
				(pAd->CommonCfg.PhyMode == PHY_11B) && 
				(MaxDesire > RATE_11))
			{
				MaxDesire = RATE_11;
			}
			break;
		}
#endif /* CONFIG_STA_SUPPORT */
	} while(FALSE);


	pAd->CommonCfg.MaxDesiredRate = MaxDesire;
	pMinHtPhy->word = 0;
	pMaxHtPhy->word = 0;
	pHtPhy->word = 0;

	/* Auto rate switching is enabled only if more than one DESIRED RATES are */
	/* specified; otherwise disabled*/
	if (num <= 1)
	{
		/*OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED);*/
		/*pAd->CommonCfg.bAutoTxRateSwitch	= FALSE;*/
		*auto_rate_cur_p = FALSE;
	}
	else
	{
		/*OPSTATUS_SET_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch	= TRUE;*/
		*auto_rate_cur_p = TRUE;
	}

	if (HtMcs != MCS_AUTO)
	{
		/*OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED);*/
		/*pAd->CommonCfg.bAutoTxRateSwitch	= FALSE;*/
		*auto_rate_cur_p = FALSE;
	}
	else
	{
		/*OPSTATUS_SET_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch	= TRUE;*/
		*auto_rate_cur_p = TRUE;
	}

#ifdef CONFIG_STA_SUPPORT
	if ((ADHOC_ON(pAd) || INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA)
		)
	{
		pSupRate = &pAd->StaActive.SupRate[0];
		pExtRate = &pAd->StaActive.ExtRate[0];
		SupRateLen = pAd->StaActive.SupRateLen;
		ExtRateLen = pAd->StaActive.ExtRateLen;
	}
	else
#endif /* CONFIG_STA_SUPPORT */	
	{
		pSupRate = &pAd->CommonCfg.SupRate[0];
		pExtRate = &pAd->CommonCfg.ExtRate[0];
		SupRateLen = pAd->CommonCfg.SupRateLen;
		ExtRateLen = pAd->CommonCfg.ExtRateLen;
	}

	/* find max supported rate*/
	for (i=0; i<SupRateLen; i++)
	{
		switch (pSupRate[i] & 0x7f)
		{
			case 2:   Rate = RATE_1;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0001;	 break;
			case 4:   Rate = RATE_2;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0002;	 break;
			case 11:  Rate = RATE_5_5;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0004;	 break;
			case 22:  Rate = RATE_11;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0008;	 break;
			case 12:  Rate = RATE_6;	/*if (pSupRate[i] & 0x80)*/  BasicRateBitmap |= 0x0010;  break;
			case 18:  Rate = RATE_9;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0020;	 break;
			case 24:  Rate = RATE_12;	/*if (pSupRate[i] & 0x80)*/  BasicRateBitmap |= 0x0040;  break;
			case 36:  Rate = RATE_18;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0080;	 break;
			case 48:  Rate = RATE_24;	/*if (pSupRate[i] & 0x80)*/  BasicRateBitmap |= 0x0100;  break;
			case 72:  Rate = RATE_36;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0200;	 break;
			case 96:  Rate = RATE_48;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0400;	 break;
			case 108: Rate = RATE_54;	if (pSupRate[i] & 0x80) BasicRateBitmap |= 0x0800;	 break;
			default:  Rate = RATE_1;	break;
		}
		if (MaxSupport < Rate)	MaxSupport = Rate;

		if (MinSupport > Rate) MinSupport = Rate;		
	}
	
	for (i=0; i<ExtRateLen; i++)
	{
		switch (pExtRate[i] & 0x7f)
		{
			case 2:   Rate = RATE_1;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0001;	 break;
			case 4:   Rate = RATE_2;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0002;	 break;
			case 11:  Rate = RATE_5_5;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0004;	 break;
			case 22:  Rate = RATE_11;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0008;	 break;
			case 12:  Rate = RATE_6;	/*if (pExtRate[i] & 0x80)*/  BasicRateBitmap |= 0x0010;  break;
			case 18:  Rate = RATE_9;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0020;	 break;
			case 24:  Rate = RATE_12;	/*if (pExtRate[i] & 0x80)*/  BasicRateBitmap |= 0x0040;  break;
			case 36:  Rate = RATE_18;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0080;	 break;
			case 48:  Rate = RATE_24;	/*if (pExtRate[i] & 0x80)*/  BasicRateBitmap |= 0x0100;  break;
			case 72:  Rate = RATE_36;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0200;	 break;
			case 96:  Rate = RATE_48;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0400;	 break;
			case 108: Rate = RATE_54;	if (pExtRate[i] & 0x80) BasicRateBitmap |= 0x0800;	 break;
			default:  Rate = RATE_1;	break;
		}
		if (MaxSupport < Rate)	MaxSupport = Rate;

		if (MinSupport > Rate) MinSupport = Rate;		
	}

	RTMP_IO_WRITE32(pAd, LEGACY_BASIC_RATE, BasicRateBitmap);
	
	for (i=0; i<MAX_LEN_OF_SUPPORTED_RATES; i++)
	{
		if (BasicRateBitmap & (0x01 << i))
			CurrBasicRate = (UCHAR)i;
		pAd->CommonCfg.ExpectedACKRate[i] = CurrBasicRate;
	}

	DBGPRINT(RT_DEBUG_TRACE,("MlmeUpdateTxRates[MaxSupport = %d] = MaxDesire %d Mbps\n", RateIdToMbps[MaxSupport], RateIdToMbps[MaxDesire]));
	/* max tx rate = min {max desire rate, max supported rate}*/
	if (MaxSupport < MaxDesire)
		pAd->CommonCfg.MaxTxRate = MaxSupport;
	else
		pAd->CommonCfg.MaxTxRate = MaxDesire;

	pAd->CommonCfg.MinTxRate = MinSupport;
	/* 2003-07-31 john - 2500 doesn't have good sensitivity at high OFDM rates. to increase the success*/
	/* ratio of initial DHCP packet exchange, TX rate starts from a lower rate depending*/
	/* on average RSSI*/
	/*	 1. RSSI >= -70db, start at 54 Mbps (short distance)*/
	/*	 2. -70 > RSSI >= -75, start at 24 Mbps (mid distance)*/
	/*	 3. -75 > RSSI, start at 11 Mbps (long distance)*/
	if (*auto_rate_cur_p)
	{
		short dbm = 0;
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			dbm = pAd->StaCfg.RssiSample.AvgRssi0 - pAd->BbpRssiToDbmDelta;
#endif /* CONFIG_STA_SUPPORT */
		if (bLinkUp == TRUE)
			pAd->CommonCfg.TxRate = RATE_24;
		else
			pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate; 

		if (dbm < -75)
			pAd->CommonCfg.TxRate = RATE_11;
		else if (dbm < -70)
			pAd->CommonCfg.TxRate = RATE_24;

		/* should never exceed MaxTxRate (consider 11B-only mode)*/
		if (pAd->CommonCfg.TxRate > pAd->CommonCfg.MaxTxRate)
			pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate; 

		pAd->CommonCfg.TxRateIndex = 0;

	}
	else
	{
		pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate;
		/*pHtPhy->field.MCS	= (pAd->CommonCfg.MaxTxRate > 3) ? (pAd->CommonCfg.MaxTxRate - 4) : pAd->CommonCfg.MaxTxRate;*/
		/*pHtPhy->field.MODE	= (pAd->CommonCfg.MaxTxRate > 3) ? MODE_OFDM : MODE_CCK;*/

		/* Choose the Desire Tx MCS in CCK/OFDM mode */
		if (num > RATE_6)
		{
			if (HtMcs <= MCS_7)		
				MaxDesire = RxwiMCSToOfdmRate[HtMcs];
			else
				MaxDesire = MinSupport;
		}
		else
		{
			if (HtMcs <= MCS_3)		
				MaxDesire = HtMcs;
			else
				MaxDesire = MinSupport;
		}
		
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.STBC	= pHtPhy->field.STBC;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.ShortGI	= pHtPhy->field.ShortGI;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MCS		= pHtPhy->field.MCS;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MODE	= pHtPhy->field.MODE;

	}

	if (pAd->CommonCfg.TxRate <= RATE_11)
	{
		pMaxHtPhy->field.MODE = MODE_CCK;

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
		pMaxHtPhy->field.MCS = pAd->CommonCfg.TxRate;
		pMinHtPhy->field.MCS = pAd->CommonCfg.MinTxRate;
	}
#endif /* CONFIG_STA_SUPPORT */

	}
	else
	{
		pMaxHtPhy->field.MODE = MODE_OFDM;

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
		pMaxHtPhy->field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.TxRate];
		if (pAd->CommonCfg.MinTxRate >= RATE_6 && (pAd->CommonCfg.MinTxRate <= RATE_54))
			{pMinHtPhy->field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MinTxRate];}
		else
			{pMinHtPhy->field.MCS = pAd->CommonCfg.MinTxRate;}
	}
#endif /* CONFIG_STA_SUPPORT */

	}

	pHtPhy->word = (pMaxHtPhy->word);
	if (bLinkUp && (pAd->OpMode == OPMODE_STA))
	{
			pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word = pHtPhy->word;
			pAd->MacTab.Content[BSSID_WCID].MaxHTPhyMode.word = pMaxHtPhy->word;
			pAd->MacTab.Content[BSSID_WCID].MinHTPhyMode.word = pMinHtPhy->word;
	}
	else
	{
		switch (pAd->CommonCfg.PhyMode) 
		{
			case PHY_11BG_MIXED:
			case PHY_11B:
#ifdef DOT11_N_SUPPORT
			case PHY_11BGN_MIXED:
#endif /* DOT11_N_SUPPORT */
				pAd->CommonCfg.MlmeRate = RATE_1;
				pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;
				pAd->CommonCfg.MlmeTransmit.field.MCS = RATE_1;
				
/*#ifdef	WIFI_TEST			*/
				pAd->CommonCfg.RtsRate = RATE_11;
/*#else*/
/*				pAd->CommonCfg.RtsRate = RATE_1;*/
/*#endif*/
				break;
			case PHY_11G:
			case PHY_11A:
#ifdef DOT11_N_SUPPORT
			case PHY_11AGN_MIXED:
			case PHY_11GN_MIXED:
			case PHY_11N_2_4G:
			case PHY_11AN_MIXED:
			case PHY_11N_5G:	
#endif /* DOT11_N_SUPPORT */
				pAd->CommonCfg.MlmeRate = RATE_6;
				pAd->CommonCfg.RtsRate = RATE_6;
				pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
				pAd->CommonCfg.MlmeTransmit.field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
				break;
			case PHY_11ABG_MIXED:
#ifdef DOT11_N_SUPPORT
			case PHY_11ABGN_MIXED:
#endif /* DOT11_N_SUPPORT */
				if (pAd->CommonCfg.Channel <= 14)
				{
					pAd->CommonCfg.MlmeRate = RATE_1;
					pAd->CommonCfg.RtsRate = RATE_1;
					pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;
					pAd->CommonCfg.MlmeTransmit.field.MCS = RATE_1;
				}
				else
				{
					pAd->CommonCfg.MlmeRate = RATE_6;
					pAd->CommonCfg.RtsRate = RATE_6;
					pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
					pAd->CommonCfg.MlmeTransmit.field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
				}
				break;
			default: /* error*/
				pAd->CommonCfg.MlmeRate = RATE_6;
                        	pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
				pAd->CommonCfg.MlmeTransmit.field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
				pAd->CommonCfg.RtsRate = RATE_1;
				break;
		}
		
		/* Keep Basic Mlme Rate.*/
		
		pAd->MacTab.Content[MCAST_WCID].HTPhyMode.word = pAd->CommonCfg.MlmeTransmit.word;
		if (pAd->CommonCfg.MlmeTransmit.field.MODE == MODE_OFDM)
			pAd->MacTab.Content[MCAST_WCID].HTPhyMode.field.MCS = OfdmRateToRxwiMCS[RATE_24];
		else
			pAd->MacTab.Content[MCAST_WCID].HTPhyMode.field.MCS = RATE_1;
		pAd->CommonCfg.BasicMlmeRate = pAd->CommonCfg.MlmeRate;

	}

	DBGPRINT(RT_DEBUG_TRACE, (" MlmeUpdateTxRates (MaxDesire=%d, MaxSupport=%d, MaxTxRate=%d, MinRate=%d, Rate Switching =%d)\n", 
			 RateIdToMbps[MaxDesire], RateIdToMbps[MaxSupport], RateIdToMbps[pAd->CommonCfg.MaxTxRate], RateIdToMbps[pAd->CommonCfg.MinTxRate], 
			 /*OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED)*/*auto_rate_cur_p));
	DBGPRINT(RT_DEBUG_TRACE, (" MlmeUpdateTxRates (TxRate=%d, RtsRate=%d, BasicRateBitmap=0x%04lx)\n", 
			 RateIdToMbps[pAd->CommonCfg.TxRate], RateIdToMbps[pAd->CommonCfg.RtsRate], BasicRateBitmap));
	DBGPRINT(RT_DEBUG_TRACE, ("MlmeUpdateTxRates (MlmeTransmit=0x%x, MinHTPhyMode=%x, MaxHTPhyMode=0x%x, HTPhyMode=0x%x)\n", 
			 pAd->CommonCfg.MlmeTransmit.word, pAd->MacTab.Content[BSSID_WCID].MinHTPhyMode.word ,pAd->MacTab.Content[BSSID_WCID].MaxHTPhyMode.word ,pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word ));
}

#ifdef DOT11_N_SUPPORT
/*
	==========================================================================
	Description:
		This function update HT Rate setting.
		Input Wcid value is valid for 2 case :
		1. it's used for Station in infra mode that copy AP rate to Mactable.
		2. OR Station 	in adhoc mode to copy peer's HT rate to Mactable. 

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID MlmeUpdateHtTxRates(
	IN PRTMP_ADAPTER 		pAd,
	IN	UCHAR				apidx)
{
	UCHAR	StbcMcs; /*j, StbcMcs, bitmask;*/
	CHAR 	i; /* 3*3*/
	RT_HT_CAPABILITY 	*pRtHtCap = NULL;
	RT_HT_PHY_INFO		*pActiveHtPhy = NULL;	
	ULONG		BasicMCS;
	UCHAR j, bitmask;
	PRT_HT_PHY_INFO			pDesireHtPhy = NULL;
	PHTTRANSMIT_SETTING		pHtPhy = NULL;
	PHTTRANSMIT_SETTING		pMaxHtPhy = NULL;
	PHTTRANSMIT_SETTING		pMinHtPhy = NULL;	
	BOOLEAN 				*auto_rate_cur_p;
	
	DBGPRINT(RT_DEBUG_TRACE,("MlmeUpdateHtTxRates===> \n"));

	auto_rate_cur_p = NULL;

	do
	{

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{		
			pDesireHtPhy	= &pAd->StaCfg.DesiredHtPhyInfo;
			pActiveHtPhy	= &pAd->StaCfg.DesiredHtPhyInfo;
			pHtPhy 		= &pAd->StaCfg.HTPhyMode;
			pMaxHtPhy	= &pAd->StaCfg.MaxHTPhyMode;
			pMinHtPhy	= &pAd->StaCfg.MinHTPhyMode;		

			auto_rate_cur_p = &pAd->StaCfg.bAutoTxRateSwitch;
			break;
		}		
#endif /* CONFIG_STA_SUPPORT */
	} while (FALSE);


#ifdef CONFIG_STA_SUPPORT	
	if ((ADHOC_ON(pAd) || INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA)
		)
	{
		if (pAd->StaActive.SupportedPhyInfo.bHtEnable == FALSE)
			return;

		pRtHtCap = &pAd->StaActive.SupportedHtPhy;
		pActiveHtPhy = &pAd->StaActive.SupportedPhyInfo;
		StbcMcs = (UCHAR)pAd->MlmeAux.AddHtInfo.AddHtInfo3.StbcMcs;
		BasicMCS =pAd->MlmeAux.AddHtInfo.MCSSet[0]+(pAd->MlmeAux.AddHtInfo.MCSSet[1]<<8)+(StbcMcs<<16);
		if ((pAd->CommonCfg.DesiredHtPhy.TxSTBC) && (pRtHtCap->RxSTBC) && (pAd->Antenna.field.TxPath == 2))
			pMaxHtPhy->field.STBC = STBC_USE;
		else
			pMaxHtPhy->field.STBC = STBC_NONE;
	}
	else
#endif /* CONFIG_STA_SUPPORT */
	{
		if (pDesireHtPhy->bHtEnable == FALSE)
			return;

		pRtHtCap = &pAd->CommonCfg.DesiredHtPhy;
		StbcMcs = (UCHAR)pAd->CommonCfg.AddHTInfo.AddHtInfo3.StbcMcs;
		BasicMCS = pAd->CommonCfg.AddHTInfo.MCSSet[0]+(pAd->CommonCfg.AddHTInfo.MCSSet[1]<<8)+(StbcMcs<<16);
		if ((pAd->CommonCfg.DesiredHtPhy.TxSTBC) && (pRtHtCap->RxSTBC) && (pAd->Antenna.field.TxPath == 2))
			pMaxHtPhy->field.STBC = STBC_USE;
		else
			pMaxHtPhy->field.STBC = STBC_NONE;
	}

	/* Decide MAX ht rate.*/
	if ((pRtHtCap->GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
		pMaxHtPhy->field.MODE = MODE_HTGREENFIELD;
	else
		pMaxHtPhy->field.MODE = MODE_HTMIX;

    if ((pAd->CommonCfg.DesiredHtPhy.ChannelWidth) && (pRtHtCap->ChannelWidth))
		pMaxHtPhy->field.BW = BW_40;
	else
		pMaxHtPhy->field.BW = BW_20;

    if (pMaxHtPhy->field.BW == BW_20)
		pMaxHtPhy->field.ShortGI = (pAd->CommonCfg.DesiredHtPhy.ShortGIfor20 & pRtHtCap->ShortGIfor20);
	else
		pMaxHtPhy->field.ShortGI = (pAd->CommonCfg.DesiredHtPhy.ShortGIfor40 & pRtHtCap->ShortGIfor40);

	if (pDesireHtPhy->MCSSet[4] != 0)
	{
		pMaxHtPhy->field.MCS = 32;	
	}

	for (i=23; i>=0; i--) /* 3*3*/
	{ 
		j = i/8; 
		bitmask = (1<<(i-(j*8)));

		if ((pActiveHtPhy->MCSSet[j] & bitmask) && (pDesireHtPhy->MCSSet[j] & bitmask))
		{
			pMaxHtPhy->field.MCS = i;
			break;
		}

		if (i==0)
			break;
	}

	/* Copy MIN ht rate.  rt2860???*/
	pMinHtPhy->field.BW = BW_20;
	pMinHtPhy->field.MCS = 0;
	pMinHtPhy->field.STBC = 0;
	pMinHtPhy->field.ShortGI = 0;
	/*If STA assigns fixed rate. update to fixed here.*/
#ifdef CONFIG_STA_SUPPORT
	if ( (pAd->OpMode == OPMODE_STA) && (pDesireHtPhy->MCSSet[0] != 0xff)
		)
	{
		if (pDesireHtPhy->MCSSet[4] != 0)
		{
			pMaxHtPhy->field.MCS = 32;
			pMinHtPhy->field.MCS = 32;
			DBGPRINT(RT_DEBUG_TRACE,("MlmeUpdateHtTxRates<=== Use Fixed MCS = %d\n",pMinHtPhy->field.MCS));
		}
		
		for (i=23; (CHAR)i >= 0; i--) /* 3*3*/
		{	
			j = i/8;	
			bitmask = (1<<(i-(j*8)));
			if ( (pDesireHtPhy->MCSSet[j] & bitmask) && (pActiveHtPhy->MCSSet[j] & bitmask))
			{
				pMaxHtPhy->field.MCS = i;
				pMinHtPhy->field.MCS = i;
				break;
			}
			if (i==0)
				break;
		}
	}
#endif /* CONFIG_STA_SUPPORT */
	
	
	/* Decide ht rate*/
	pHtPhy->field.STBC = pMaxHtPhy->field.STBC;
	pHtPhy->field.BW = pMaxHtPhy->field.BW;
	pHtPhy->field.MODE = pMaxHtPhy->field.MODE;
	pHtPhy->field.MCS = pMaxHtPhy->field.MCS;
	pHtPhy->field.ShortGI = pMaxHtPhy->field.ShortGI;

	/* use default now. rt2860*/
	if (pDesireHtPhy->MCSSet[0] != 0xff)
		*auto_rate_cur_p = FALSE;
	else
		*auto_rate_cur_p = TRUE;
	
	DBGPRINT(RT_DEBUG_TRACE, (" MlmeUpdateHtTxRates<---.AMsduSize = %d  \n", pAd->CommonCfg.DesiredHtPhy.AmsduSize ));
	DBGPRINT(RT_DEBUG_TRACE,("TX: MCS[0] = %x (choose %d), BW = %d, ShortGI = %d, MODE = %d,  \n", pActiveHtPhy->MCSSet[0],pHtPhy->field.MCS,
		pHtPhy->field.BW, pHtPhy->field.ShortGI, pHtPhy->field.MODE));
	DBGPRINT(RT_DEBUG_TRACE,("MlmeUpdateHtTxRates<=== \n"));
}


VOID BATableInit(
	IN PRTMP_ADAPTER pAd, 
    IN BA_TABLE *Tab) 
{
	int i;

	Tab->numAsOriginator = 0;
	Tab->numAsRecipient = 0;
	Tab->numDoneOriginator = 0;
	NdisAllocateSpinLock(pAd, &pAd->BATabLock);
	for (i = 0; i < MAX_LEN_OF_BA_REC_TABLE; i++) 
	{
		Tab->BARecEntry[i].REC_BA_Status = Recipient_NONE;
		NdisAllocateSpinLock(pAd, &(Tab->BARecEntry[i].RxReRingLock));
	}
	for (i = 0; i < MAX_LEN_OF_BA_ORI_TABLE; i++) 
	{
		Tab->BAOriEntry[i].ORI_BA_Status = Originator_NONE;
	}
}

VOID BATableExit(
	IN RTMP_ADAPTER *pAd)
{
	int i;
	
	for(i=0; i<MAX_LEN_OF_BA_REC_TABLE; i++)
	{
		NdisFreeSpinLock(&pAd->BATable.BARecEntry[i].RxReRingLock);
	}
	NdisFreeSpinLock(&pAd->BATabLock);
}
#endif /* DOT11_N_SUPPORT */

/* IRQL = DISPATCH_LEVEL*/
VOID MlmeRadioOff(
	IN PRTMP_ADAPTER pAd)
{
	RTMP_MLME_RADIO_OFF(pAd);
}

/* IRQL = DISPATCH_LEVEL*/
VOID MlmeRadioOn(
	IN PRTMP_ADAPTER pAd)
{	
	RTMP_MLME_RADIO_ON(pAd);
}

/* ===========================================================================================*/
/* bss_table.c*/
/* ===========================================================================================*/


/*! \brief initialize BSS table
 *	\param p_tab pointer to the table
 *	\return none
 *	\pre
 *	\post

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL
  
 */
VOID BssTableInit(
	IN BSS_TABLE *Tab) 
{
	int i;

	Tab->BssNr = 0;
    Tab->BssOverlapNr = 0;
	
	for (i = 0; i < MAX_LEN_OF_BSS_TABLE; i++) 
	{
		UCHAR *pOldAddr = Tab->BssEntry[i].pVarIeFromProbRsp;
		NdisZeroMemory(&Tab->BssEntry[i], sizeof(BSS_ENTRY));
		Tab->BssEntry[i].Rssi = -127;	/* initial the rssi as a minimum value */
		if (pOldAddr)
		{
			RTMPZeroMemory(pOldAddr, MAX_VIE_LEN);
			Tab->BssEntry[i].pVarIeFromProbRsp = pOldAddr;
		}
	}
}


/*! \brief search the BSS table by SSID
 *	\param p_tab pointer to the bss table
 *	\param ssid SSID string 
 *	\return index of the table, BSS_NOT_FOUND if not in the table
 *	\pre
 *	\post
 *	\note search by sequential search

 IRQL = DISPATCH_LEVEL

 */
ULONG BssTableSearch(
	IN BSS_TABLE *Tab, 
	IN PUCHAR	 pBssid,
	IN UCHAR	 Channel) 
{
	UCHAR i;

	for (i = 0; i < Tab->BssNr; i++) 
	{
		
		/* Some AP that support A/B/G mode that may used the same BSSID on 11A and 11B/G.*/
		/* We should distinguish this case.*/
		/*		*/
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
			 ((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
			MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid)) 
		{ 
			return i;
		}
	}
	return (ULONG)BSS_NOT_FOUND;
}

ULONG BssSsidTableSearch(
	IN BSS_TABLE *Tab, 
	IN PUCHAR	 pBssid,
	IN PUCHAR	 pSsid,
	IN UCHAR	 SsidLen,
	IN UCHAR	 Channel) 
{
	UCHAR i;

	for (i = 0; i < Tab->BssNr; i++) 
	{
		
		/* Some AP that support A/B/G mode that may used the same BSSID on 11A and 11B/G.*/
		/* We should distinguish this case.*/
		/*		*/
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
			 ((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
			MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid) &&
			SSID_EQUAL(pSsid, SsidLen, Tab->BssEntry[i].Ssid, Tab->BssEntry[i].SsidLen)) 
		{ 
			return i;
		}
	}
	return (ULONG)BSS_NOT_FOUND;
}

ULONG BssTableSearchWithSSID(
	IN BSS_TABLE *Tab, 
	IN PUCHAR	 Bssid,
	IN PUCHAR	 pSsid,
	IN UCHAR	 SsidLen,
	IN UCHAR	 Channel)
{
	UCHAR i;

	for (i = 0; i < Tab->BssNr; i++) 
	{
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
			((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
			MAC_ADDR_EQUAL(&(Tab->BssEntry[i].Bssid), Bssid) &&
			(SSID_EQUAL(pSsid, SsidLen, Tab->BssEntry[i].Ssid, Tab->BssEntry[i].SsidLen) ||
			(NdisEqualMemory(pSsid, ZeroSsid, SsidLen)) || 
			(NdisEqualMemory(Tab->BssEntry[i].Ssid, ZeroSsid, Tab->BssEntry[i].SsidLen))))
		{ 
			return i;
		}
	}
	return (ULONG)BSS_NOT_FOUND;
}


ULONG BssSsidTableSearchBySSID(
	IN BSS_TABLE *Tab,
	IN PUCHAR	 pSsid,
	IN UCHAR	 SsidLen)
{
	UCHAR i;

	for (i = 0; i < Tab->BssNr; i++) 
	{
		if (SSID_EQUAL(pSsid, SsidLen, Tab->BssEntry[i].Ssid, Tab->BssEntry[i].SsidLen)) 
		{ 
			return i;
		}
	}
	return (ULONG)BSS_NOT_FOUND;
}


/* IRQL = DISPATCH_LEVEL*/
VOID BssTableDeleteEntry(
	IN OUT	BSS_TABLE *Tab, 
	IN		PUCHAR	  pBssid,
	IN		UCHAR	  Channel)
{
	UCHAR i, j;

	for (i = 0; i < Tab->BssNr; i++) 
	{
		if ((Tab->BssEntry[i].Channel == Channel) && 
			(MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid)))
		{
			UCHAR *pOldAddr = NULL;
			
			for (j = i; j < Tab->BssNr - 1; j++)
			{
				pOldAddr = Tab->BssEntry[j].pVarIeFromProbRsp;
				NdisMoveMemory(&(Tab->BssEntry[j]), &(Tab->BssEntry[j + 1]), sizeof(BSS_ENTRY));
				if (pOldAddr)
				{
					RTMPZeroMemory(pOldAddr, MAX_VIE_LEN);
					NdisMoveMemory(pOldAddr, 
								   Tab->BssEntry[j + 1].pVarIeFromProbRsp, 
								   Tab->BssEntry[j + 1].VarIeFromProbeRspLen);
					Tab->BssEntry[j].pVarIeFromProbRsp = pOldAddr;
				}
			}

			pOldAddr = Tab->BssEntry[Tab->BssNr - 1].pVarIeFromProbRsp;
			NdisZeroMemory(&(Tab->BssEntry[Tab->BssNr - 1]), sizeof(BSS_ENTRY));
			if (pOldAddr)
			{
				RTMPZeroMemory(pOldAddr, MAX_VIE_LEN);
				Tab->BssEntry[Tab->BssNr - 1].pVarIeFromProbRsp = pOldAddr;
			}
			
			Tab->BssNr -= 1;
			return;
		}
	}
}


/*! \brief
 *	\param 
 *	\return
 *	\pre
 *	\post
	 
 IRQL = DISPATCH_LEVEL
 
 */
VOID BssEntrySet(
	IN PRTMP_ADAPTER	pAd, 
	OUT BSS_ENTRY *pBss, 
	IN PUCHAR pBssid, 
	IN CHAR Ssid[], 
	IN UCHAR SsidLen, 
	IN UCHAR BssType, 
	IN USHORT BeaconPeriod, 
	IN PCF_PARM pCfParm, 
	IN USHORT AtimWin, 
	IN USHORT CapabilityInfo, 
	IN UCHAR SupRate[], 
	IN UCHAR SupRateLen,
	IN UCHAR ExtRate[], 
	IN UCHAR ExtRateLen,
	IN HT_CAPABILITY_IE *pHtCapability,
	IN ADD_HT_INFO_IE *pAddHtInfo,	/* AP might use this additional ht info IE */
	IN UCHAR			HtCapabilityLen,
	IN UCHAR			AddHtInfoLen,
	IN UCHAR			NewExtChanOffset,
	IN UCHAR Channel,
	IN CHAR Rssi,
	IN LARGE_INTEGER TimeStamp,
	IN UCHAR CkipFlag,
	IN PEDCA_PARM pEdcaParm,
	IN PQOS_CAPABILITY_PARM pQosCapability,
	IN PQBSS_LOAD_PARM pQbssLoad,
	IN USHORT LengthVIE,	
	IN PNDIS_802_11_VARIABLE_IEs pVIE) 
{
	COPY_MAC_ADDR(pBss->Bssid, pBssid);
	/* Default Hidden SSID to be TRUE, it will be turned to FALSE after coping SSID*/
	pBss->Hidden = 1;	
	if (SsidLen > 0)
	{
		/* For hidden SSID AP, it might send beacon with SSID len equal to 0*/
		/* Or send beacon /probe response with SSID len matching real SSID length,*/
		/* but SSID is all zero. such as "00-00-00-00" with length 4.*/
		/* We have to prevent this case overwrite correct table*/
		if (NdisEqualMemory(Ssid, ZeroSsid, SsidLen) == 0)
		{
		    NdisZeroMemory(pBss->Ssid, MAX_LEN_OF_SSID);
			NdisMoveMemory(pBss->Ssid, Ssid, SsidLen);
			pBss->SsidLen = SsidLen;
			pBss->Hidden = 0;
		}
	}
	else
	{
		/* avoid  Hidden SSID form beacon to overwirite correct SSID from probe response */
		if (NdisEqualMemory(pBss->Ssid, ZeroSsid, pBss->SsidLen))
		{
			NdisZeroMemory(pBss->Ssid, MAX_LEN_OF_SSID);
			pBss->SsidLen = 0;
		}
	}
	
	pBss->BssType = BssType;
	pBss->BeaconPeriod = BeaconPeriod;
	if (BssType == BSS_INFRA) 
	{
		if (pCfParm->bValid) 
		{
			pBss->CfpCount = pCfParm->CfpCount;
			pBss->CfpPeriod = pCfParm->CfpPeriod;
			pBss->CfpMaxDuration = pCfParm->CfpMaxDuration;
			pBss->CfpDurRemaining = pCfParm->CfpDurRemaining;
		}
	} 
	else 
	{
		pBss->AtimWin = AtimWin;
	}

	NdisGetSystemUpTime(&pBss->LastBeaconRxTime);
	pBss->CapabilityInfo = CapabilityInfo;
	/* The privacy bit indicate security is ON, it maight be WEP, TKIP or AES*/
	/* Combine with AuthMode, they will decide the connection methods.*/
	pBss->Privacy = CAP_IS_PRIVACY_ON(pBss->CapabilityInfo);
	ASSERT(SupRateLen <= MAX_LEN_OF_SUPPORTED_RATES);
	if (SupRateLen <= MAX_LEN_OF_SUPPORTED_RATES)		
		NdisMoveMemory(pBss->SupRate, SupRate, SupRateLen);
	else		
		NdisMoveMemory(pBss->SupRate, SupRate, MAX_LEN_OF_SUPPORTED_RATES);	
	pBss->SupRateLen = SupRateLen;
	ASSERT(ExtRateLen <= MAX_LEN_OF_SUPPORTED_RATES);
	if (ExtRateLen > MAX_LEN_OF_SUPPORTED_RATES)
		ExtRateLen = MAX_LEN_OF_SUPPORTED_RATES;
	NdisMoveMemory(pBss->ExtRate, ExtRate, ExtRateLen);
	pBss->NewExtChanOffset = NewExtChanOffset;
	pBss->ExtRateLen = ExtRateLen;
	pBss->Channel = Channel;
	pBss->CentralChannel = Channel;
	pBss->Rssi = Rssi;
	/* Update CkipFlag. if not exists, the value is 0x0*/
	pBss->CkipFlag = CkipFlag;

	/* New for microsoft Fixed IEs*/
	NdisMoveMemory(pBss->FixIEs.Timestamp, &TimeStamp, 8);
	pBss->FixIEs.BeaconInterval = BeaconPeriod;
	pBss->FixIEs.Capabilities = CapabilityInfo;

	/* New for microsoft Variable IEs*/
	if (LengthVIE != 0)
	{
		pBss->VarIELen = LengthVIE;
		NdisMoveMemory(pBss->VarIEs, pVIE, pBss->VarIELen);
	}
	else
	{
		pBss->VarIELen = 0;
	}

	pBss->AddHtInfoLen = 0;
	pBss->HtCapabilityLen = 0;
#ifdef DOT11_N_SUPPORT
	if (HtCapabilityLen> 0)
	{
		pBss->HtCapabilityLen = HtCapabilityLen;
		NdisMoveMemory(&pBss->HtCapability, pHtCapability, HtCapabilityLen);
		if (AddHtInfoLen > 0)
		{
			pBss->AddHtInfoLen = AddHtInfoLen;
			NdisMoveMemory(&pBss->AddHtInfo, pAddHtInfo, AddHtInfoLen);
			
	 			if ((pAddHtInfo->ControlChan > 2)&& (pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_BELOW) && (pHtCapability->HtCapInfo.ChannelWidth == BW_40))
	 			{
	 				pBss->CentralChannel = pAddHtInfo->ControlChan - 2;
	 			}
	 			else if ((pAddHtInfo->AddHtInfo.ExtChanOffset == EXTCHA_ABOVE) && (pHtCapability->HtCapInfo.ChannelWidth == BW_40))
				{
		 				pBss->CentralChannel = pAddHtInfo->ControlChan + 2;
				}
		}
	}
#endif /* DOT11_N_SUPPORT */
	
	BssCipherParse(pBss);

	/* new for QOS*/
	if (pEdcaParm)
		NdisMoveMemory(&pBss->EdcaParm, pEdcaParm, sizeof(EDCA_PARM));
	else
		pBss->EdcaParm.bValid = FALSE;
	if (pQosCapability)
		NdisMoveMemory(&pBss->QosCapability, pQosCapability, sizeof(QOS_CAPABILITY_PARM));
	else
		pBss->QosCapability.bValid = FALSE;
	if (pQbssLoad)
		NdisMoveMemory(&pBss->QbssLoad, pQbssLoad, sizeof(QBSS_LOAD_PARM));
	else
		pBss->QbssLoad.bValid = FALSE;

	{
		PEID_STRUCT     pEid;
		USHORT          Length = 0;


#ifdef CONFIG_STA_SUPPORT
		NdisZeroMemory(&pBss->WpaIE.IE[0], MAX_CUSTOM_LEN);
		NdisZeroMemory(&pBss->RsnIE.IE[0], MAX_CUSTOM_LEN);
		NdisZeroMemory(&pBss->WpsIE.IE[0], MAX_CUSTOM_LEN);
#ifdef EXT_BUILD_CHANNEL_LIST
		NdisZeroMemory(&pBss->CountryString[0], 3);
		pBss->bHasCountryIE = FALSE;
#endif /* EXT_BUILD_CHANNEL_LIST */
#endif /* CONFIG_STA_SUPPORT */
		pEid = (PEID_STRUCT) pVIE;
		while ((Length + 2 + (USHORT)pEid->Len) <= LengthVIE)    
		{
#define WPS_AP		0x01
			switch(pEid->Eid)
			{
				case IE_WPA:
					if (NdisEqualMemory(pEid->Octet, WPS_OUI, 4))
					{
#ifdef CONFIG_STA_SUPPORT
						if ((pEid->Len + 2) > MAX_CUSTOM_LEN)
						{
							pBss->WpsIE.IELen = 0;
							break;
						}
						pBss->WpsIE.IELen = pEid->Len + 2;
						NdisMoveMemory(pBss->WpsIE.IE, pEid, pBss->WpsIE.IELen);
#endif /* CONFIG_STA_SUPPORT */
						break;
					}
#ifdef CONFIG_STA_SUPPORT
					if (NdisEqualMemory(pEid->Octet, WPA_OUI, 4))
					{
						if ((pEid->Len + 2) > MAX_CUSTOM_LEN)
						{
							pBss->WpaIE.IELen = 0;
							break;
						}
						pBss->WpaIE.IELen = pEid->Len + 2;
						NdisMoveMemory(pBss->WpaIE.IE, pEid, pBss->WpaIE.IELen);
					}
#endif /* CONFIG_STA_SUPPORT */
					break;

#ifdef CONFIG_STA_SUPPORT
				case IE_RSN:
					if (NdisEqualMemory(pEid->Octet + 2, RSN_OUI, 3))
					{
						if ((pEid->Len + 2) > MAX_CUSTOM_LEN)
						{
							pBss->RsnIE.IELen = 0;
							break;
						}
						pBss->RsnIE.IELen = pEid->Len + 2;
						NdisMoveMemory(pBss->RsnIE.IE, pEid, pBss->RsnIE.IELen);
					}
					break;
#ifdef EXT_BUILD_CHANNEL_LIST					
				case IE_COUNTRY:					
					NdisMoveMemory(&pBss->CountryString[0], pEid->Octet, 3);
					pBss->bHasCountryIE = TRUE;
					break;
#endif /* EXT_BUILD_CHANNEL_LIST */
#endif /* CONFIG_STA_SUPPORT */
			}
			Length = Length + 2 + (USHORT)pEid->Len;  /* Eid[1] + Len[1]+ content[Len]*/
			pEid = (PEID_STRUCT)((UCHAR*)pEid + 2 + pEid->Len);        
		}
	}
}

/*! 
 *	\brief insert an entry into the bss table
 *	\param p_tab The BSS table
 *	\param Bssid BSSID
 *	\param ssid SSID
 *	\param ssid_len Length of SSID
 *	\param bss_type
 *	\param beacon_period
 *	\param timestamp
 *	\param p_cf
 *	\param atim_win
 *	\param cap
 *	\param rates
 *	\param rates_len
 *	\param channel_idx
 *	\return none
 *	\pre
 *	\post
 *	\note If SSID is identical, the old entry will be replaced by the new one
	 
 IRQL = DISPATCH_LEVEL
 
 */
ULONG BssTableSetEntry(
	IN	PRTMP_ADAPTER	pAd, 
	OUT BSS_TABLE *Tab, 
	IN PUCHAR pBssid, 
	IN CHAR Ssid[], 
	IN UCHAR SsidLen, 
	IN UCHAR BssType, 
	IN USHORT BeaconPeriod, 
	IN CF_PARM *CfParm, 
	IN USHORT AtimWin, 
	IN USHORT CapabilityInfo, 
	IN UCHAR SupRate[],
	IN UCHAR SupRateLen,
	IN UCHAR ExtRate[],
	IN UCHAR ExtRateLen,
	IN HT_CAPABILITY_IE *pHtCapability,
	IN ADD_HT_INFO_IE *pAddHtInfo,	/* AP might use this additional ht info IE */
	IN UCHAR			HtCapabilityLen,
	IN UCHAR			AddHtInfoLen,
	IN UCHAR			NewExtChanOffset,
	IN UCHAR ChannelNo,
	IN CHAR Rssi,
	IN LARGE_INTEGER TimeStamp,
	IN UCHAR CkipFlag,
	IN PEDCA_PARM pEdcaParm,
	IN PQOS_CAPABILITY_PARM pQosCapability,
	IN PQBSS_LOAD_PARM pQbssLoad,
	IN USHORT LengthVIE,	
	IN PNDIS_802_11_VARIABLE_IEs pVIE)
{
	ULONG	Idx;

	/*Idx = BssTableSearchWithSSID(Tab, pBssid,  (UCHAR *)Ssid, SsidLen, ChannelNo);*/
	Idx = BssTableSearch(Tab, pBssid, ChannelNo);
	if (Idx == BSS_NOT_FOUND) 
	{
		if (Tab->BssNr >= MAX_LEN_OF_BSS_TABLE)
	    {
			
			/* It may happen when BSS Table was full.*/
			/* The desired AP will not be added into BSS Table*/
			/* In this case, if we found the desired AP then overwrite BSS Table.*/
			
			if(!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) ||
				!OPSTATUS_TEST_FLAG(pAd, fOP_AP_STATUS_MEDIA_STATE_CONNECTED))
			{
				if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, pBssid) ||
					SSID_EQUAL(pAd->MlmeAux.Ssid, pAd->MlmeAux.SsidLen, Ssid, SsidLen)
#ifdef APCLI_SUPPORT
					|| MAC_ADDR_EQUAL(pAd->ApCliMlmeAux.Bssid, pBssid)
					|| SSID_EQUAL(pAd->ApCliMlmeAux.Ssid, pAd->ApCliMlmeAux.SsidLen, Ssid, SsidLen)
#endif /* APCLI_SUPPORT */
					)
				{
					Idx = Tab->BssOverlapNr;
					BssEntrySet(pAd, &Tab->BssEntry[Idx], pBssid, Ssid, SsidLen, BssType, BeaconPeriod, CfParm, AtimWin, 
						CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,pHtCapability, pAddHtInfo,HtCapabilityLen, AddHtInfoLen,
						NewExtChanOffset, ChannelNo, Rssi, TimeStamp, CkipFlag, pEdcaParm, pQosCapability, pQbssLoad, LengthVIE, pVIE);
					Tab->BssOverlapNr = Tab->BssOverlapNr + 1;
                    Tab->BssOverlapNr = Tab->BssOverlapNr % MAX_LEN_OF_BSS_TABLE;
				}
				return Idx;
			}
			else
			{
			return BSS_NOT_FOUND;
			}
		}
		Idx = Tab->BssNr;
		BssEntrySet(pAd, &Tab->BssEntry[Idx], pBssid, Ssid, SsidLen, BssType, BeaconPeriod, CfParm, AtimWin, 
					CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,pHtCapability, pAddHtInfo,HtCapabilityLen, AddHtInfoLen,
					NewExtChanOffset, ChannelNo, Rssi, TimeStamp, CkipFlag, pEdcaParm, pQosCapability, pQbssLoad, LengthVIE, pVIE);
		Tab->BssNr++;
	} 
	else
	{
		BssEntrySet(pAd, &Tab->BssEntry[Idx], pBssid, Ssid, SsidLen, BssType, BeaconPeriod,CfParm, AtimWin, 
					CapabilityInfo, SupRate, SupRateLen, ExtRate, ExtRateLen,pHtCapability, pAddHtInfo,HtCapabilityLen, AddHtInfoLen,
					NewExtChanOffset, ChannelNo, Rssi, TimeStamp, CkipFlag, pEdcaParm, pQosCapability, pQbssLoad, LengthVIE, pVIE);
	}

	return Idx;
}

#ifdef CONFIG_STA_SUPPORT
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
VOID  TriEventInit(
	IN	PRTMP_ADAPTER	pAd) 
{
	UCHAR		i;

	for (i = 0;i < MAX_TRIGGER_EVENT;i++)
		pAd->CommonCfg.TriggerEventTab.EventA[i].bValid = FALSE;
	
	pAd->CommonCfg.TriggerEventTab.EventANo = 0;
	pAd->CommonCfg.TriggerEventTab.EventBCountDown = 0;
}

INT TriEventTableSetEntry(
	IN	PRTMP_ADAPTER	pAd, 
	OUT TRIGGER_EVENT_TAB *Tab, 
	IN PUCHAR pBssid, 
	IN HT_CAPABILITY_IE *pHtCapability,
	IN UCHAR			HtCapabilityLen,
	IN UCHAR			RegClass,
	IN UCHAR ChannelNo)
{
	/* Event A, legacy AP exist.*/
	if (HtCapabilityLen == 0)
	{
		UCHAR index;
		
		/*
			Check if we already set this entry in the Event Table.
		*/
		for (index = 0; index<MAX_TRIGGER_EVENT; index++)
		{
			if ((Tab->EventA[index].bValid == TRUE) && 
				(Tab->EventA[index].Channel == ChannelNo) && 
				(Tab->EventA[index].RegClass == RegClass)
			)
			{
				return 0;
			}
		}
		
		/*
			If not set, add it to the Event table
		*/
		if (Tab->EventANo < MAX_TRIGGER_EVENT)
		{
			RTMPMoveMemory(Tab->EventA[Tab->EventANo].BSSID, pBssid, 6);
			Tab->EventA[Tab->EventANo].bValid = TRUE;
			Tab->EventA[Tab->EventANo].Channel = ChannelNo;
			if (RegClass != 0)
			{
				/* Beacon has Regulatory class IE. So use beacon's*/
				Tab->EventA[Tab->EventANo].RegClass = RegClass;
			}
			else
			{
				/* Use Station's Regulatory class instead.*/
				/* If no Reg Class in Beacon, set to "unknown"*/
				/* TODO:  Need to check if this's valid*/
				Tab->EventA[Tab->EventANo].RegClass = 0; /* ????????????????? need to check*/
			}
			Tab->EventANo ++;
		}
	}
	else if (pHtCapability->HtCapInfo.Forty_Mhz_Intolerant)
	{
		/* Event B.   My BSS beacon has Intolerant40 bit set*/
		Tab->EventBCountDown = pAd->CommonCfg.Dot11BssWidthChanTranDelay;
	}

	return 0;
}
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

/* IRQL = DISPATCH_LEVEL*/
VOID BssTableSsidSort(
	IN	PRTMP_ADAPTER	pAd, 
	OUT BSS_TABLE *OutTab, 
	IN	CHAR Ssid[], 
	IN	UCHAR SsidLen) 
{
	INT i;
	BssTableInit(OutTab);

	if ((SsidLen == 0) && 
		(pAd->StaCfg.bAutoConnectIfNoSSID == FALSE))
		return;

	for (i = 0; i < pAd->ScanTab.BssNr; i++) 
	{
		BSS_ENTRY *pInBss = &pAd->ScanTab.BssEntry[i];
		BOOLEAN	bIsHiddenApIncluded = FALSE;

		if (((pAd->CommonCfg.bIEEE80211H == 1) && 
            (pAd->MlmeAux.Channel > 14) && 
             RadarChannelCheck(pAd, pInBss->Channel))
#ifdef CARRIER_DETECTION_SUPPORT /* Roger sync Carrier             */
             || (pAd->CommonCfg.CarrierDetect.Enable == TRUE)
#endif /* CARRIER_DETECTION_SUPPORT */
            )
		{
			if (pInBss->Hidden)
				bIsHiddenApIncluded = TRUE;
		}

		if ((pInBss->BssType == pAd->StaCfg.BssType) && 
			(SSID_EQUAL(Ssid, SsidLen, pInBss->Ssid, pInBss->SsidLen) || bIsHiddenApIncluded))
		{
			BSS_ENTRY *pOutBss = &OutTab->BssEntry[OutTab->BssNr];

#ifdef WPA_SUPPLICANT_SUPPORT
			if (pAd->StaCfg.WpaSupplicantUP & 0x80)
			{
				/* copy matching BSS from InTab to OutTab*/
				NdisMoveMemory(pOutBss, pInBss, sizeof(BSS_ENTRY));
				OutTab->BssNr++;
				continue;
			}
#endif /* WPA_SUPPLICANT_SUPPORT */


#ifdef EXT_BUILD_CHANNEL_LIST
			/* If no Country IE exists no Connection will be established when IEEE80211dClientMode is strict.*/
			if ((pAd->StaCfg.IEEE80211dClientMode == Rt802_11_D_Strict) &&
				(pInBss->bHasCountryIE == FALSE))
			{
				DBGPRINT(RT_DEBUG_TRACE,("StaCfg.IEEE80211dClientMode == Rt802_11_D_Strict, but this AP doesn't have country IE.\n"));
				continue;
			}
#endif /* EXT_BUILD_CHANNEL_LIST */

#ifdef DOT11_N_SUPPORT
			/* 2.4G/5G N only mode*/
			if ((pInBss->HtCapabilityLen == 0) &&
				((pAd->CommonCfg.PhyMode == PHY_11N_2_4G) || (pAd->CommonCfg.PhyMode == PHY_11N_5G)))
			{
				DBGPRINT(RT_DEBUG_TRACE,("STA is in N-only Mode, this AP don't have Ht capability in Beacon.\n"));
				continue;
			}

			if ((pAd->CommonCfg.PhyMode == PHY_11GN_MIXED) &&
				((pInBss->SupRateLen + pInBss->ExtRateLen) < 12))
			{
				DBGPRINT(RT_DEBUG_TRACE,("STA is in GN-only Mode, this AP is in B mode.\n"));
				continue;
			}
#endif /* DOT11_N_SUPPORT */

			/* New for WPA2*/
			/* Check the Authmode first*/
			if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
			{
				/* Check AuthMode and AuthModeAux for matching, in case AP support dual-mode*/
				if ((pAd->StaCfg.AuthMode != pInBss->AuthMode) && (pAd->StaCfg.AuthMode != pInBss->AuthModeAux))
					/* None matched*/
					continue;
				
				/* Check cipher suite, AP must have more secured cipher than station setting*/
				if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK))
				{
					/* If it's not mixed mode, we should only let BSS pass with the same encryption*/
					if (pInBss->WPA.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus != pInBss->WPA.GroupCipher)
							continue;
						
					/* check group cipher*/
					if ((pAd->StaCfg.WepStatus < pInBss->WPA.GroupCipher) &&
						(pInBss->WPA.GroupCipher != Ndis802_11GroupWEP40Enabled) && 
						(pInBss->WPA.GroupCipher != Ndis802_11GroupWEP104Enabled))
						continue;

					/* check pairwise cipher, skip if none matched*/
					/* If profile set to AES, let it pass without question.*/
					/* If profile set to TKIP, we must find one mateched*/
					if ((pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA.PairCipher) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA.PairCipherAux))
						continue;						
				}
				else if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
				{
					/* If it's not mixed mode, we should only let BSS pass with the same encryption*/
					if (pInBss->WPA2.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus != pInBss->WPA2.GroupCipher)
							continue;
						
					/* check group cipher*/
					if ((pAd->StaCfg.WepStatus < pInBss->WPA.GroupCipher) &&
						(pInBss->WPA2.GroupCipher != Ndis802_11GroupWEP40Enabled) && 
						(pInBss->WPA2.GroupCipher != Ndis802_11GroupWEP104Enabled))
						continue;

					/* check pairwise cipher, skip if none matched*/
					/* If profile set to AES, let it pass without question.*/
					/* If profile set to TKIP, we must find one mateched*/
					if ((pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA2.PairCipher) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA2.PairCipherAux))
						continue;						
				}
			}			
			/* Bss Type matched, SSID matched. */
			/* We will check wepstatus for qualification Bss*/
			else if (pAd->StaCfg.WepStatus != pInBss->WepStatus)
			{
				DBGPRINT(RT_DEBUG_TRACE,("StaCfg.WepStatus=%d, while pInBss->WepStatus=%d\n", pAd->StaCfg.WepStatus, pInBss->WepStatus));
				
    /*
        1. For the SESv2 case, we will not qualify WepStatus.
        2. AirPort express AP support OPEN-WEP, Shared-WEP, WPA and WPA2 Mix mode.
            shall not block the BSS with same GroupChiper of WPA/WPA2 contained in RSN IE. */

				
/*				if (!pInBss->bSES)*/
				if ((!pInBss->bSES) && (pInBss->WPA.bMixMode == FALSE))
					continue;
			}

			/* Since the AP is using hidden SSID, and we are trying to connect to ANY*/
			/* It definitely will fail. So, skip it.*/
			/* CCX also require not even try to connect it!!*/
			if (SsidLen == 0)
				continue;
			
			/* copy matching BSS from InTab to OutTab*/
			NdisMoveMemory(pOutBss, pInBss, sizeof(BSS_ENTRY));

			OutTab->BssNr++;
		}
		else if ((pInBss->BssType == pAd->StaCfg.BssType) && (SsidLen == 0))
		{
			BSS_ENTRY *pOutBss = &OutTab->BssEntry[OutTab->BssNr];


#ifdef DOT11_N_SUPPORT
			/* 2.4G/5G N only mode*/
			if ((pInBss->HtCapabilityLen == 0) &&
				((pAd->CommonCfg.PhyMode == PHY_11N_2_4G) || (pAd->CommonCfg.PhyMode == PHY_11N_5G)))
			{
				DBGPRINT(RT_DEBUG_TRACE,("STA is in N-only Mode, this AP don't have Ht capability in Beacon.\n"));
				continue;
			}

			if ((pAd->CommonCfg.PhyMode == PHY_11GN_MIXED) &&
				((pInBss->SupRateLen + pInBss->ExtRateLen) < 12))
			{
				DBGPRINT(RT_DEBUG_TRACE,("STA is in GN-only Mode, this AP is in B mode.\n"));
				continue;
			}
#endif /* DOT11_N_SUPPORT */

			/* New for WPA2*/
			/* Check the Authmode first*/
			if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA)
			{
				/* Check AuthMode and AuthModeAux for matching, in case AP support dual-mode*/
				if ((pAd->StaCfg.AuthMode != pInBss->AuthMode) && (pAd->StaCfg.AuthMode != pInBss->AuthModeAux))
					/* None matched*/
					continue;
				
				/* Check cipher suite, AP must have more secured cipher than station setting*/
				if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPAPSK))
				{
					/* If it's not mixed mode, we should only let BSS pass with the same encryption*/
					if (pInBss->WPA.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus != pInBss->WPA.GroupCipher)
							continue;
						
					/* check group cipher*/
					if (pAd->StaCfg.WepStatus < pInBss->WPA.GroupCipher)
						continue;

					/* check pairwise cipher, skip if none matched*/
					/* If profile set to AES, let it pass without question.*/
					/* If profile set to TKIP, we must find one mateched*/
					if ((pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA.PairCipher) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA.PairCipherAux))
						continue;						
				}
				else if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2) || (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK))
				{
					/* If it's not mixed mode, we should only let BSS pass with the same encryption*/
					if (pInBss->WPA2.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus != pInBss->WPA2.GroupCipher)
							continue;
						
					/* check group cipher*/
					if (pAd->StaCfg.WepStatus < pInBss->WPA2.GroupCipher)
						continue;

					/* check pairwise cipher, skip if none matched*/
					/* If profile set to AES, let it pass without question.*/
					/* If profile set to TKIP, we must find one mateched*/
					if ((pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA2.PairCipher) && 
						(pAd->StaCfg.WepStatus != pInBss->WPA2.PairCipherAux))
						continue;						
				}
			}
			/* Bss Type matched, SSID matched. */
			/* We will check wepstatus for qualification Bss*/
			else if (pAd->StaCfg.WepStatus != pInBss->WepStatus)
					continue;
			
			/* copy matching BSS from InTab to OutTab*/
			NdisMoveMemory(pOutBss, pInBss, sizeof(BSS_ENTRY));

			OutTab->BssNr++;
		}

		if (OutTab->BssNr >= MAX_LEN_OF_BSS_TABLE)
			break;
	}

	BssTableSortByRssi(OutTab);
}


/* IRQL = DISPATCH_LEVEL*/
VOID BssTableSortByRssi(
	IN OUT BSS_TABLE *OutTab) 
{
	INT 	  i, j;
/*	BSS_ENTRY TmpBss;*/
	BSS_ENTRY *pTmpBss = NULL;


	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&pTmpBss, sizeof(BSS_ENTRY));
	if (pTmpBss == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return;
	}

	for (i = 0; i < OutTab->BssNr - 1; i++) 
	{
		for (j = i+1; j < OutTab->BssNr; j++) 
		{
			if (OutTab->BssEntry[j].Rssi > OutTab->BssEntry[i].Rssi) 
			{
				NdisMoveMemory(pTmpBss, &OutTab->BssEntry[j], sizeof(BSS_ENTRY));
				NdisMoveMemory(&OutTab->BssEntry[j], &OutTab->BssEntry[i], sizeof(BSS_ENTRY));
				NdisMoveMemory(&OutTab->BssEntry[i], pTmpBss, sizeof(BSS_ENTRY));
			}
		}
	}

	if (pTmpBss != NULL)
		os_free_mem(NULL, pTmpBss);
}
#endif /* CONFIG_STA_SUPPORT */


VOID BssCipherParse(
	IN OUT	PBSS_ENTRY	pBss)
{
	PEID_STRUCT 		 pEid;
	PUCHAR				pTmp;
	PRSN_IE_HEADER_STRUCT			pRsnHeader;
	PCIPHER_SUITE_STRUCT			pCipher;
	PAKM_SUITE_STRUCT				pAKM;
	USHORT							Count;
	INT								Length;
	NDIS_802_11_ENCRYPTION_STATUS	TmpCipher;

	
	/* WepStatus will be reset later, if AP announce TKIP or AES on the beacon frame.*/
	
	if (pBss->Privacy)
	{
		pBss->WepStatus 	= Ndis802_11WEPEnabled;
	}
	else
	{
		pBss->WepStatus 	= Ndis802_11WEPDisabled;
	}
	/* Set default to disable & open authentication before parsing variable IE*/
	pBss->AuthMode		= Ndis802_11AuthModeOpen;
	pBss->AuthModeAux	= Ndis802_11AuthModeOpen;

	/* Init WPA setting*/
	pBss->WPA.PairCipher	= Ndis802_11WEPDisabled;
	pBss->WPA.PairCipherAux = Ndis802_11WEPDisabled;
	pBss->WPA.GroupCipher	= Ndis802_11WEPDisabled;
	pBss->WPA.RsnCapability = 0;
	pBss->WPA.bMixMode		= FALSE;

	/* Init WPA2 setting*/
	pBss->WPA2.PairCipher	 = Ndis802_11WEPDisabled;
	pBss->WPA2.PairCipherAux = Ndis802_11WEPDisabled;
	pBss->WPA2.GroupCipher	 = Ndis802_11WEPDisabled;
	pBss->WPA2.RsnCapability = 0;
	pBss->WPA2.bMixMode 	 = FALSE;

	
	Length = (INT) pBss->VarIELen;

	while (Length > 0)
	{
		/* Parse cipher suite base on WPA1 & WPA2, they should be parsed differently*/
		pTmp = ((PUCHAR) pBss->VarIEs) + pBss->VarIELen - Length;
		pEid = (PEID_STRUCT) pTmp;
		switch (pEid->Eid)
		{
			case IE_WPA:
				if (NdisEqualMemory(pEid->Octet, SES_OUI, 3) && (pEid->Len == 7))
				{
					pBss->bSES = TRUE;
					break;
				}				
				else if (NdisEqualMemory(pEid->Octet, WPA_OUI, 4) != 1)
				{
					/* if unsupported vendor specific IE*/
					break;
				}				
				/* Skip OUI, version, and multicast suite*/
				/* This part should be improved in the future when AP supported multiple cipher suite.*/
				/* For now, it's OK since almost all APs have fixed cipher suite supported.*/
				/* pTmp = (PUCHAR) pEid->Octet;*/
				pTmp   += 11;

				/* Cipher Suite Selectors from Spec P802.11i/D3.2 P26.*/
				/*	Value	   Meaning*/
				/*	0			None */
				/*	1			WEP-40*/
				/*	2			Tkip*/
				/*	3			WRAP*/
				/*	4			AES*/
				/*	5			WEP-104*/
				/* Parse group cipher*/
				switch (*pTmp)
				{
					case 1:
						pBss->WPA.GroupCipher = Ndis802_11GroupWEP40Enabled;
						break;
					case 5:
						pBss->WPA.GroupCipher = Ndis802_11GroupWEP104Enabled;
						break;
					case 2:
						pBss->WPA.GroupCipher = Ndis802_11Encryption2Enabled;
						break;
					case 4:
						pBss->WPA.GroupCipher = Ndis802_11Encryption3Enabled;
						break;
					default:
						break;
				}
				/* number of unicast suite*/
				pTmp   += 1;

				/* skip all unicast cipher suites*/
				/*Count = *(PUSHORT) pTmp;				*/
				Count = (pTmp[1]<<8) + pTmp[0];
				pTmp   += sizeof(USHORT);

				/* Parsing all unicast cipher suite*/
				while (Count > 0)
				{
					/* Skip OUI*/
					pTmp += 3;
					TmpCipher = Ndis802_11WEPDisabled;
					switch (*pTmp)
					{
						case 1:
						case 5: /* Although WEP is not allowed in WPA related auth mode, we parse it anyway*/
							TmpCipher = Ndis802_11Encryption1Enabled;
							break;
						case 2:
							TmpCipher = Ndis802_11Encryption2Enabled;
							break;
						case 4:
							TmpCipher = Ndis802_11Encryption3Enabled;
							break;
						default:
							break;
					}
					if (TmpCipher > pBss->WPA.PairCipher)
					{
						/* Move the lower cipher suite to PairCipherAux*/
						pBss->WPA.PairCipherAux = pBss->WPA.PairCipher;
						pBss->WPA.PairCipher	= TmpCipher;
					}
					else
					{
						pBss->WPA.PairCipherAux = TmpCipher;
					}
					pTmp++;
					Count--;
				}
				
				/* 4. get AKM suite counts*/
				/*Count	= *(PUSHORT) pTmp;*/
				Count = (pTmp[1]<<8) + pTmp[0];
				pTmp   += sizeof(USHORT);
				pTmp   += 3;
				
				switch (*pTmp)
				{
					case 1:
						/* Set AP support WPA-enterprise mode*/
						if (pBss->AuthMode == Ndis802_11AuthModeOpen)
							pBss->AuthMode = Ndis802_11AuthModeWPA;
						else
							pBss->AuthModeAux = Ndis802_11AuthModeWPA;
						break;
					case 2:
						/* Set AP support WPA-PSK mode*/
						if (pBss->AuthMode == Ndis802_11AuthModeOpen)
							pBss->AuthMode = Ndis802_11AuthModeWPAPSK;
						else
							pBss->AuthModeAux = Ndis802_11AuthModeWPAPSK;
						break;
					default:
						break;
				}
				pTmp   += 1;

				/* Fixed for WPA-None*/
				if (pBss->BssType == BSS_ADHOC)
				{
					pBss->AuthMode	  = Ndis802_11AuthModeWPANone;
					pBss->AuthModeAux = Ndis802_11AuthModeWPANone;
					pBss->WepStatus   = pBss->WPA.GroupCipher;
					/* Patched bugs for old driver*/
					if (pBss->WPA.PairCipherAux == Ndis802_11WEPDisabled)
						pBss->WPA.PairCipherAux = pBss->WPA.GroupCipher;
				}
				else
					pBss->WepStatus   = pBss->WPA.PairCipher;					
				
				/* Check the Pair & Group, if different, turn on mixed mode flag*/
				if (pBss->WPA.GroupCipher != pBss->WPA.PairCipher)
					pBss->WPA.bMixMode = TRUE;
				
				break;

			case IE_RSN:
				pRsnHeader = (PRSN_IE_HEADER_STRUCT) pTmp;
				
				/* 0. Version must be 1*/
				if (le2cpu16(pRsnHeader->Version) != 1)
					break;
				pTmp   += sizeof(RSN_IE_HEADER_STRUCT);

				/* 1. Check group cipher*/
				pCipher = (PCIPHER_SUITE_STRUCT) pTmp;
				if (!RTMPEqualMemory(pTmp, RSN_OUI, 3))
					break;

				/* Parse group cipher*/
				switch (pCipher->Type)
				{
					case 1:
						pBss->WPA2.GroupCipher = Ndis802_11GroupWEP40Enabled;
						break;
					case 5:
						pBss->WPA2.GroupCipher = Ndis802_11GroupWEP104Enabled;
						break;
					case 2:
						pBss->WPA2.GroupCipher = Ndis802_11Encryption2Enabled;
						break;
					case 4:
						pBss->WPA2.GroupCipher = Ndis802_11Encryption3Enabled;
						break;
					default:
						break;
				}
				/* set to correct offset for next parsing*/
				pTmp   += sizeof(CIPHER_SUITE_STRUCT);

				/* 2. Get pairwise cipher counts*/
				/*Count = *(PUSHORT) pTmp;*/
				Count = (pTmp[1]<<8) + pTmp[0];
				pTmp   += sizeof(USHORT);			

				/* 3. Get pairwise cipher*/
				/* Parsing all unicast cipher suite*/
				while (Count > 0)
				{
					/* Skip OUI*/
					pCipher = (PCIPHER_SUITE_STRUCT) pTmp;
					TmpCipher = Ndis802_11WEPDisabled;
					switch (pCipher->Type)
					{
						case 1:
						case 5: /* Although WEP is not allowed in WPA related auth mode, we parse it anyway*/
							TmpCipher = Ndis802_11Encryption1Enabled;
							break;
						case 2:
							TmpCipher = Ndis802_11Encryption2Enabled;
							break;
						case 4:
							TmpCipher = Ndis802_11Encryption3Enabled;
							break;
						default:
							break;
					}
					if (TmpCipher > pBss->WPA2.PairCipher)
					{
						/* Move the lower cipher suite to PairCipherAux*/
						pBss->WPA2.PairCipherAux = pBss->WPA2.PairCipher;
						pBss->WPA2.PairCipher	 = TmpCipher;
					}
					else
					{
						pBss->WPA2.PairCipherAux = TmpCipher;
					}
					pTmp += sizeof(CIPHER_SUITE_STRUCT);
					Count--;
				}
				
				/* 4. get AKM suite counts*/
				/*Count	= *(PUSHORT) pTmp;*/
				Count = (pTmp[1]<<8) + pTmp[0];
				pTmp   += sizeof(USHORT);

				/* 5. Get AKM ciphers*/
				/* Parsing all AKM ciphers*/
				while (Count > 0)
				{					
					pAKM = (PAKM_SUITE_STRUCT) pTmp;
					if (!RTMPEqualMemory(pTmp, RSN_OUI, 3))
						break;

					switch (pAKM->Type)
					{
						case 0:
							if (pBss->AuthMode == Ndis802_11AuthModeOpen)
								pBss->AuthMode = Ndis802_11AuthModeWPANone;
							else
								pBss->AuthModeAux = Ndis802_11AuthModeWPANone;
							break;                                                        
						case 1:
							/* Set AP support WPA-enterprise mode*/
							if (pBss->AuthMode == Ndis802_11AuthModeOpen)
								pBss->AuthMode = Ndis802_11AuthModeWPA2;
							else
								pBss->AuthModeAux = Ndis802_11AuthModeWPA2;
							break;
						case 2:
							/* Set AP support WPA-PSK mode*/
							if (pBss->AuthMode == Ndis802_11AuthModeOpen)
								pBss->AuthMode = Ndis802_11AuthModeWPA2PSK;
							else
								pBss->AuthModeAux = Ndis802_11AuthModeWPA2PSK;
							break;
						default:
							if (pBss->AuthMode == Ndis802_11AuthModeOpen)
								pBss->AuthMode = Ndis802_11AuthModeMax;
							else
								pBss->AuthModeAux = Ndis802_11AuthModeMax;
							break;
					}
					pTmp   += (Count * sizeof(AKM_SUITE_STRUCT));
					Count--;
				}

				/* Fixed for WPA-None*/
				if (pBss->BssType == BSS_ADHOC)
				{
					pBss->WPA.PairCipherAux = pBss->WPA2.PairCipherAux;
					pBss->WPA.GroupCipher	= pBss->WPA2.GroupCipher;
					pBss->WepStatus 		= pBss->WPA.GroupCipher;
					/* Patched bugs for old driver*/
					if (pBss->WPA.PairCipherAux == Ndis802_11WEPDisabled)
						pBss->WPA.PairCipherAux = pBss->WPA.GroupCipher;
				}
				pBss->WepStatus   = pBss->WPA2.PairCipher;					
				
				/* 6. Get RSN capability*/
				/*pBss->WPA2.RsnCapability = *(PUSHORT) pTmp;*/
				pBss->WPA2.RsnCapability = (pTmp[1]<<8) + pTmp[0];
				pTmp += sizeof(USHORT);
				
				/* Check the Pair & Group, if different, turn on mixed mode flag*/
				if (pBss->WPA2.GroupCipher != pBss->WPA2.PairCipher)
					pBss->WPA2.bMixMode = TRUE;
				
				break;
			default:
				break;
		}
		Length -= (pEid->Len + 2);
	}
}

/* ===========================================================================================*/
/* mac_table.c*/
/* ===========================================================================================*/

/*! \brief generates a random mac address value for IBSS BSSID
 *	\param Addr the bssid location
 *	\return none
 *	\pre
 *	\post
 */
VOID MacAddrRandomBssid(
	IN PRTMP_ADAPTER pAd, 
	OUT PUCHAR pAddr) 
{
	INT i;

	for (i = 0; i < MAC_ADDR_LEN; i++) 
	{
		pAddr[i] = RandomByte(pAd);
	}

	pAddr[0] = (pAddr[0] & 0xfe) | 0x02;  /* the first 2 bits must be 01xxxxxxxx*/
}

/*! \brief init the management mac frame header
 *	\param p_hdr mac header
 *	\param subtype subtype of the frame
 *	\param p_ds destination address, don't care if it is a broadcast address
 *	\return none
 *	\pre the station has the following information in the pAd->StaCfg
 *	 - bssid
 *	 - station address
 *	\post
 *	\note this function initializes the following field

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL
  
 */
VOID MgtMacHeaderInit(
	IN	PRTMP_ADAPTER	pAd, 
	IN OUT PHEADER_802_11 pHdr80211, 
	IN UCHAR SubType, 
	IN UCHAR ToDs, 
	IN PUCHAR pDA, 
	IN PUCHAR pBssid) 
{
	NdisZeroMemory(pHdr80211, sizeof(HEADER_802_11));
	
	pHdr80211->FC.Type = BTYPE_MGMT;
	pHdr80211->FC.SubType = SubType;
/*	if (SubType == SUBTYPE_ACK)	 sample, no use, it will conflict with ACTION frame sub type*/
/*		pHdr80211->FC.Type = BTYPE_CNTL;*/
	pHdr80211->FC.ToDs = ToDs;
	COPY_MAC_ADDR(pHdr80211->Addr1, pDA);
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		COPY_MAC_ADDR(pHdr80211->Addr2, pAd->CurrentAddress);
#endif /* CONFIG_STA_SUPPORT */
	COPY_MAC_ADDR(pHdr80211->Addr3, pBssid);
}

/* ===========================================================================================*/
/* mem_mgmt.c*/
/* ===========================================================================================*/

/*!***************************************************************************
 * This routine build an outgoing frame, and fill all information specified 
 * in argument list to the frame body. The actual frame size is the summation 
 * of all arguments.
 * input params:
 *		Buffer - pointer to a pre-allocated memory segment
 *		args - a list of <int arg_size, arg> pairs.
 *		NOTE NOTE NOTE!!!! the last argument must be NULL, otherwise this
 *						   function will FAIL!!!
 * return:
 *		Size of the buffer
 * usage:  
 *		MakeOutgoingFrame(Buffer, output_length, 2, &fc, 2, &dur, 6, p_addr1, 6,p_addr2, END_OF_ARGS);

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL
  
 ****************************************************************************/
ULONG MakeOutgoingFrame(
	OUT UCHAR *Buffer, 
	OUT ULONG *FrameLen, ...) 
{
	UCHAR   *p;
	int 	leng;
	ULONG	TotLeng;
	va_list Args;

	/* calculates the total length*/
	TotLeng = 0;
	va_start(Args, FrameLen);
	do 
	{
		leng = va_arg(Args, int);
		if (leng == END_OF_ARGS) 
		{
			break;
		}
		p = va_arg(Args, PVOID);
		NdisMoveMemory(&Buffer[TotLeng], p, leng);
		TotLeng = TotLeng + leng;
	} while(TRUE);

	va_end(Args); /* clean up */
	*FrameLen = TotLeng;
	return TotLeng;
}

/* ===========================================================================================*/
/* mlme_queue.c*/
/* ===========================================================================================*/

/*! \brief	Initialize The MLME Queue, used by MLME Functions
 *	\param	*Queue	   The MLME Queue
 *	\return Always	   Return NDIS_STATE_SUCCESS in this implementation
 *	\pre
 *	\post
 *	\note	Because this is done only once (at the init stage), no need to be locked

 IRQL = PASSIVE_LEVEL
 
 */
NDIS_STATUS MlmeQueueInit(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE *Queue) 
{
	INT i;

	NdisAllocateSpinLock(pAd, &Queue->Lock);

	Queue->Num	= 0;
	Queue->Head = 0;
	Queue->Tail = 0;

	for (i = 0; i < MAX_LEN_OF_MLME_QUEUE; i++) 
	{
		Queue->Entry[i].Occupied = FALSE;
		Queue->Entry[i].MsgLen = 0;
		NdisZeroMemory(Queue->Entry[i].Msg, MGMT_DMA_BUFFER_SIZE);
	}

	return NDIS_STATUS_SUCCESS;
}

/*! \brief	 Enqueue a message for other threads, if they want to send messages to MLME thread
 *	\param	*Queue	  The MLME Queue
 *	\param	 Machine  The State Machine Id
 *	\param	 MsgType  The Message Type
 *	\param	 MsgLen   The Message length
 *	\param	*Msg	  The message pointer
 *	\return  TRUE if enqueue is successful, FALSE if the queue is full
 *	\pre
 *	\post
 *	\note	 The message has to be initialized

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL
  
 */
BOOLEAN MlmeEnqueue(
	IN	PRTMP_ADAPTER	pAd,
	IN ULONG Machine, 
	IN ULONG MsgType, 
	IN ULONG MsgLen, 
	IN VOID *Msg,
	IN ULONG Priv) 
{
	INT Tail;
	MLME_QUEUE	*Queue = (MLME_QUEUE *)&pAd->Mlme.Queue;

	/* Do nothing if the driver is starting halt state.*/
	/* This might happen when timer already been fired before cancel timer with mlmehalt*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return FALSE;

	/* First check the size, it MUST not exceed the mlme queue size*/
	if (MsgLen > MGMT_DMA_BUFFER_SIZE)
	{
		DBGPRINT_ERR(("MlmeEnqueue: msg too large, size = %ld \n", MsgLen));
		return FALSE;
	}
	
	if (MlmeQueueFull(Queue, 1)) 
	{
		return FALSE;
	}

	NdisAcquireSpinLock(&(Queue->Lock));
	Tail = Queue->Tail;
	Queue->Tail++;
	Queue->Num++;
	if (Queue->Tail == MAX_LEN_OF_MLME_QUEUE) 
	{
		Queue->Tail = 0;
	}
	
	Queue->Entry[Tail].Wcid = RESERVED_WCID;
	Queue->Entry[Tail].Occupied = TRUE;
	Queue->Entry[Tail].Machine = Machine;
	Queue->Entry[Tail].MsgType = MsgType;
	Queue->Entry[Tail].MsgLen  = MsgLen;	
	Queue->Entry[Tail].Priv = Priv;
	
	if (Msg != NULL)
	{
		NdisMoveMemory(Queue->Entry[Tail].Msg, Msg, MsgLen);
	}
		
	NdisReleaseSpinLock(&(Queue->Lock));
	return TRUE;
}

/*! \brief	 This function is used when Recv gets a MLME message
 *	\param	*Queue			 The MLME Queue
 *	\param	 TimeStampHigh	 The upper 32 bit of timestamp
 *	\param	 TimeStampLow	 The lower 32 bit of timestamp
 *	\param	 Rssi			 The receiving RSSI strength
 *	\param	 MsgLen 		 The length of the message
 *	\param	*Msg			 The message pointer
 *	\return  TRUE if everything ok, FALSE otherwise (like Queue Full)
 *	\pre
 *	\post
 
 IRQL = DISPATCH_LEVEL
 
 */
BOOLEAN MlmeEnqueueForRecv(
	IN	PRTMP_ADAPTER	pAd, 
	IN ULONG Wcid, 
	IN ULONG TimeStampHigh, 
	IN ULONG TimeStampLow,
	IN UCHAR Rssi0, 
	IN UCHAR Rssi1, 
	IN UCHAR Rssi2, 
	IN ULONG MsgLen, 
	IN VOID *Msg,
	IN UCHAR Signal,
	IN UCHAR OpMode)
{
	INT 		 Tail, Machine = 0xff;
	PFRAME_802_11 pFrame = (PFRAME_802_11)Msg;
	INT		 MsgType = 0x0;
	MLME_QUEUE	*Queue = (MLME_QUEUE *)&pAd->Mlme.Queue;

#ifdef RALINK_ATE			
	/* Nothing to do in ATE mode */
	if(ATE_ON(pAd))
		return FALSE;
#endif /* RALINK_ATE */

	/* Do nothing if the driver is starting halt state.*/
	/* This might happen when timer already been fired before cancel timer with mlmehalt*/
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
	{
		DBGPRINT_ERR(("MlmeEnqueueForRecv: fRTMP_ADAPTER_HALT_IN_PROGRESS\n"));
		return FALSE;
	}

	/* First check the size, it MUST not exceed the mlme queue size*/
	if (MsgLen > MGMT_DMA_BUFFER_SIZE)
	{
		DBGPRINT_ERR(("MlmeEnqueueForRecv: frame too large, size = %ld \n", MsgLen));
		return FALSE;
	}

	if (MlmeQueueFull(Queue, 0)) 
	{
		return FALSE;
	}

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (!MsgTypeSubst(pAd, pFrame, &Machine, &MsgType)) 
		{
			DBGPRINT_ERR(("MlmeEnqueueForRecv: un-recongnized mgmt->subtype=%d\n",pFrame->Hdr.FC.SubType));
			return FALSE;
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	/* OK, we got all the informations, it is time to put things into queue*/

	NdisAcquireSpinLock(&(Queue->Lock));
	Tail = Queue->Tail;
	Queue->Tail++;
	Queue->Num++;
	if (Queue->Tail == MAX_LEN_OF_MLME_QUEUE) 
	{
		Queue->Tail = 0;
	}
	Queue->Entry[Tail].Occupied = TRUE;
	Queue->Entry[Tail].Machine = Machine;
	Queue->Entry[Tail].MsgType = MsgType;
	Queue->Entry[Tail].MsgLen  = MsgLen;
	Queue->Entry[Tail].TimeStamp.u.LowPart = TimeStampLow;
	Queue->Entry[Tail].TimeStamp.u.HighPart = TimeStampHigh;
	Queue->Entry[Tail].Rssi0 = Rssi0;
	Queue->Entry[Tail].Rssi1 = Rssi1;
	Queue->Entry[Tail].Rssi2 = Rssi2;
	Queue->Entry[Tail].Signal = Signal;
	Queue->Entry[Tail].Wcid = (UCHAR)Wcid;
	Queue->Entry[Tail].OpMode = (ULONG)OpMode;
	Queue->Entry[Tail].Priv = 0;

	Queue->Entry[Tail].Channel = pAd->LatchRfRegs.Channel;
	
	if (Msg != NULL)
	{
		NdisMoveMemory(Queue->Entry[Tail].Msg, Msg, MsgLen);
	}

	NdisReleaseSpinLock(&(Queue->Lock));	
	RTMP_MLME_HANDLER(pAd);

	return TRUE;
}


/*! \brief	 Dequeue a message from the MLME Queue
 *	\param	*Queue	  The MLME Queue
 *	\param	*Elem	  The message dequeued from MLME Queue
 *	\return  TRUE if the Elem contains something, FALSE otherwise
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeDequeue(
	IN MLME_QUEUE *Queue, 
	OUT MLME_QUEUE_ELEM **Elem) 
{
	NdisAcquireSpinLock(&(Queue->Lock));
	*Elem = &(Queue->Entry[Queue->Head]);    
	Queue->Num--;
	Queue->Head++;
	if (Queue->Head == MAX_LEN_OF_MLME_QUEUE) 
	{
		Queue->Head = 0;
	}
	NdisReleaseSpinLock(&(Queue->Lock));
	return TRUE;
}

/* IRQL = DISPATCH_LEVEL*/
VOID	MlmeRestartStateMachine(
	IN	PRTMP_ADAPTER	pAd)
{
#ifdef CONFIG_STA_SUPPORT
	BOOLEAN				Cancelled;
#endif /* CONFIG_STA_SUPPORT */
	
	DBGPRINT(RT_DEBUG_TRACE, ("MlmeRestartStateMachine \n"));


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
#ifdef QOS_DLS_SUPPORT
		UCHAR i;
#endif /* QOS_DLS_SUPPORT */
		/* Cancel all timer events*/
		/* Be careful to cancel new added timer*/
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer,	  &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer,   &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer,  &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.AuthTimer,	   &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer,	   &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer,	   &Cancelled);

#ifdef QOS_DLS_SUPPORT
		for (i=0; i<MAX_NUM_OF_DLS_ENTRY; i++)
		{
			RTMPCancelTimer(&pAd->StaCfg.DLSEntry[i].Timer, &Cancelled);
		}
#endif /* QOS_DLS_SUPPORT */
	}
#endif /* CONFIG_STA_SUPPORT */

	/* Change back to original channel in case of doing scan*/
	AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
	AsicLockChannel(pAd, pAd->CommonCfg.Channel);

	/* Resume MSDU which is turned off durning scan*/
	RTMPResumeMsduTransmission(pAd);

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* Set all state machines back IDLE*/
		pAd->Mlme.CntlMachine.CurrState    = CNTL_IDLE;
		pAd->Mlme.AssocMachine.CurrState   = ASSOC_IDLE;
		pAd->Mlme.AuthMachine.CurrState    = AUTH_REQ_IDLE;
		pAd->Mlme.AuthRspMachine.CurrState = AUTH_RSP_IDLE;
		pAd->Mlme.SyncMachine.CurrState    = SYNC_IDLE;
		pAd->Mlme.ActMachine.CurrState    = ACT_IDLE;
#ifdef QOS_DLS_SUPPORT
		pAd->Mlme.DlsMachine.CurrState    = DLS_IDLE;
#endif /* QOS_DLS_SUPPORT */

	}
#endif /* CONFIG_STA_SUPPORT */
	
}

/*! \brief	test if the MLME Queue is empty
 *	\param	*Queue	  The MLME Queue
 *	\return TRUE if the Queue is empty, FALSE otherwise
 *	\pre
 *	\post
 
 IRQL = DISPATCH_LEVEL
 
 */
BOOLEAN MlmeQueueEmpty(
	IN MLME_QUEUE *Queue) 
{
	BOOLEAN Ans;

	NdisAcquireSpinLock(&(Queue->Lock));
	Ans = (Queue->Num == 0);
	NdisReleaseSpinLock(&(Queue->Lock));

	return Ans;
}

/*! \brief	 test if the MLME Queue is full
 *	\param	 *Queue 	 The MLME Queue
 *	\return  TRUE if the Queue is empty, FALSE otherwise
 *	\pre
 *	\post

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeQueueFull(
	IN MLME_QUEUE *Queue,
	IN UCHAR SendId) 
{
	BOOLEAN Ans;

	NdisAcquireSpinLock(&(Queue->Lock));
	if (SendId == 0)
		Ans = ((Queue->Num >= (MAX_LEN_OF_MLME_QUEUE / 2)) || Queue->Entry[Queue->Tail].Occupied);
	else
		Ans = (Queue->Num == MAX_LEN_OF_MLME_QUEUE);
	NdisReleaseSpinLock(&(Queue->Lock));

	return Ans;
}

/*! \brief	 The destructor of MLME Queue
 *	\param 
 *	\return
 *	\pre
 *	\post
 *	\note	Clear Mlme Queue, Set Queue->Num to Zero.

 IRQL = PASSIVE_LEVEL
 
 */
VOID MlmeQueueDestroy(
	IN MLME_QUEUE *pQueue) 
{
	NdisAcquireSpinLock(&(pQueue->Lock));
	pQueue->Num  = 0;
	pQueue->Head = 0;
	pQueue->Tail = 0;
	NdisReleaseSpinLock(&(pQueue->Lock));
	NdisFreeSpinLock(&(pQueue->Lock));
}


/*! \brief	 To substitute the message type if the message is coming from external
 *	\param	pFrame		   The frame received
 *	\param	*Machine	   The state machine
 *	\param	*MsgType	   the message type for the state machine
 *	\return TRUE if the substitution is successful, FALSE otherwise
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
#ifdef CONFIG_STA_SUPPORT
BOOLEAN MsgTypeSubst(
	IN PRTMP_ADAPTER  pAd,
	IN PFRAME_802_11 pFrame, 
	OUT INT *Machine, 
	OUT INT *MsgType) 
{
	USHORT	Seq, Alg;
	UCHAR	EAPType;
	PUCHAR	pData;

	/* Pointer to start of data frames including SNAP header*/
	pData = (PUCHAR) pFrame + LENGTH_802_11;

	/* The only data type will pass to this function is EAPOL frame*/
	if (pFrame->Hdr.FC.Type == BTYPE_DATA) 
	{
		{
	        *Machine = WPA_STATE_MACHINE;
			EAPType = *((UCHAR*)pFrame + LENGTH_802_11 + LENGTH_802_1_H + 1);
	        return (WpaMsgTypeSubst(EAPType, (INT *) MsgType));		
		}
	}

	switch (pFrame->Hdr.FC.SubType) 
	{
		case SUBTYPE_ASSOC_REQ:
			*Machine = ASSOC_STATE_MACHINE;
			*MsgType = MT2_PEER_ASSOC_REQ;
			break;
		case SUBTYPE_ASSOC_RSP:
			*Machine = ASSOC_STATE_MACHINE;
			*MsgType = MT2_PEER_ASSOC_RSP;
			break;
		case SUBTYPE_REASSOC_REQ:
			*Machine = ASSOC_STATE_MACHINE;
			*MsgType = MT2_PEER_REASSOC_REQ;
			break;
		case SUBTYPE_REASSOC_RSP:
			*Machine = ASSOC_STATE_MACHINE;
			*MsgType = MT2_PEER_REASSOC_RSP;
			break;
		case SUBTYPE_PROBE_REQ:
			*Machine = SYNC_STATE_MACHINE;
			*MsgType = MT2_PEER_PROBE_REQ;
			break;
		case SUBTYPE_PROBE_RSP:
			*Machine = SYNC_STATE_MACHINE;
			*MsgType = MT2_PEER_PROBE_RSP;
			break;
		case SUBTYPE_BEACON:
			*Machine = SYNC_STATE_MACHINE;
			*MsgType = MT2_PEER_BEACON;
			break;
		case SUBTYPE_ATIM:
			*Machine = SYNC_STATE_MACHINE;
			*MsgType = MT2_PEER_ATIM;
			break;
		case SUBTYPE_DISASSOC:
			*Machine = ASSOC_STATE_MACHINE;
			*MsgType = MT2_PEER_DISASSOC_REQ;
			break;
		case SUBTYPE_AUTH:
			/* get the sequence number from payload 24 Mac Header + 2 bytes algorithm*/
			NdisMoveMemory(&Seq, &pFrame->Octet[2], sizeof(USHORT));
			NdisMoveMemory(&Alg, &pFrame->Octet[0], sizeof(USHORT));
			if (Seq == 1 || Seq == 3) 
			{
				*Machine = AUTH_RSP_STATE_MACHINE;
				*MsgType = MT2_PEER_AUTH_ODD;
			} 
			else if (Seq == 2 || Seq == 4) 
			{
				if (Alg == AUTH_MODE_OPEN || Alg == AUTH_MODE_KEY)
				{
					*Machine = AUTH_STATE_MACHINE;
					*MsgType = MT2_PEER_AUTH_EVEN;
				} 
			} 
			else 
			{
				return FALSE;
			}
			break;
		case SUBTYPE_DEAUTH:
			*Machine = AUTH_RSP_STATE_MACHINE;
			*MsgType = MT2_PEER_DEAUTH;
			break;
		case SUBTYPE_ACTION:
			*Machine = ACTION_STATE_MACHINE;
			/*  Sometimes Sta will return with category bytes with MSB = 1, if they receive catogory out of their support*/
			if ((pFrame->Octet[0]&0x7F) > MAX_PEER_CATE_MSG) 
			{
				*MsgType = MT2_ACT_INVALID;
			} 
			else
			{
				*MsgType = (pFrame->Octet[0]&0x7F);
			} 
			break;
		default:
			return FALSE;
			break;
	}

	return TRUE;
}
#endif /* CONFIG_STA_SUPPORT */

/* ===========================================================================================*/
/* state_machine.c*/
/* ===========================================================================================*/

/*! \brief Initialize the state machine.
 *	\param *S			pointer to the state machine 
 *	\param	Trans		State machine transition function
 *	\param	StNr		number of states 
 *	\param	MsgNr		number of messages 
 *	\param	DefFunc 	default function, when there is invalid state/message combination 
 *	\param	InitState	initial state of the state machine 
 *	\param	Base		StateMachine base, internal use only
 *	\pre p_sm should be a legal pointer
 *	\post

 IRQL = PASSIVE_LEVEL
 
 */
VOID StateMachineInit(
	IN STATE_MACHINE *S, 
	IN STATE_MACHINE_FUNC Trans[], 
	IN ULONG StNr, 
	IN ULONG MsgNr, 
	IN STATE_MACHINE_FUNC DefFunc, 
	IN ULONG InitState, 
	IN ULONG Base) 
{
	ULONG i, j;

	/* set number of states and messages*/
	S->NrState = StNr;
	S->NrMsg   = MsgNr;
	S->Base    = Base;

	S->TransFunc  = Trans;

	/* init all state transition to default function*/
	for (i = 0; i < StNr; i++) 
	{
		for (j = 0; j < MsgNr; j++) 
		{
			S->TransFunc[i * MsgNr + j] = DefFunc;
		}
	}

	/* set the starting state*/
	S->CurrState = InitState;
}

/*! \brief This function fills in the function pointer into the cell in the state machine 
 *	\param *S	pointer to the state machine
 *	\param St	state
 *	\param Msg	incoming message
 *	\param f	the function to be executed when (state, message) combination occurs at the state machine
 *	\pre *S should be a legal pointer to the state machine, st, msg, should be all within the range, Base should be set in the initial state
 *	\post

 IRQL = PASSIVE_LEVEL
 
 */
VOID StateMachineSetAction(
	IN STATE_MACHINE *S, 
	IN ULONG St, 
	IN ULONG Msg, 
	IN STATE_MACHINE_FUNC Func) 
{
	ULONG MsgIdx;

	MsgIdx = Msg - S->Base;

	if (St < S->NrState && MsgIdx < S->NrMsg) 
	{
		/* boundary checking before setting the action*/
		S->TransFunc[St * S->NrMsg + MsgIdx] = Func;
	} 
}

/*! \brief	 This function does the state transition
 *	\param	 *Adapter the NIC adapter pointer
 *	\param	 *S 	  the state machine
 *	\param	 *Elem	  the message to be executed
 *	\return   None
 
 IRQL = DISPATCH_LEVEL
 
 */
VOID StateMachinePerformAction(
	IN	PRTMP_ADAPTER	pAd, 
	IN STATE_MACHINE *S, 
	IN MLME_QUEUE_ELEM *Elem,
	IN ULONG CurrState)
{

	if (S->TransFunc[(CurrState) * S->NrMsg + Elem->MsgType - S->Base])
		(*(S->TransFunc[(CurrState) * S->NrMsg + Elem->MsgType - S->Base]))(pAd, Elem);
}

/*
	==========================================================================
	Description:
		The drop function, when machine executes this, the message is simply 
		ignored. This function does nothing, the message is freed in 
		StateMachinePerformAction()
	==========================================================================
 */
VOID Drop(
	IN PRTMP_ADAPTER pAd, 
	IN MLME_QUEUE_ELEM *Elem) 
{
}

/*
	==========================================================================
	Description:
	==========================================================================
 */
UCHAR RandomByte(
	IN PRTMP_ADAPTER pAd) 
{
	ULONG i;
	UCHAR R, Result;

	R = 0;

	if (pAd->Mlme.ShiftReg == 0)
	NdisGetSystemUpTime((ULONG *)&pAd->Mlme.ShiftReg);

	for (i = 0; i < 8; i++) 
	{
		if (pAd->Mlme.ShiftReg & 0x00000001) 
		{
			pAd->Mlme.ShiftReg = ((pAd->Mlme.ShiftReg ^ LFSR_MASK) >> 1) | 0x80000000;
			Result = 1;
		} 
		else 
		{
			pAd->Mlme.ShiftReg = pAd->Mlme.ShiftReg >> 1;
			Result = 0;
		}
		R = (R << 1) | Result;
	}

	return R;
}


UCHAR RandomByte2(
        IN PRTMP_ADAPTER pAd)
{
	UINT32 a,b;
	UCHAR value, value1 = 0, value2 = 0, value3 = 0, value4 = 0, value5 = 0;

	/*MAC statistic related*/
	RTMP_IO_READ32(pAd, RX_STA_CNT1, &a);
	a &= 0x0000ffff;
	RTMP_IO_READ32(pAd, RX_STA_CNT0, &b); 
	b &= 0x0000ffff;
	value = (a<<16)|b;

	/*R50~R54: RSSI or SNR related*/
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R50, &value1);
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R51, &value2);
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R52, &value3);
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R53, &value4);
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R54, &value5);

	return value^value1^value2^value3^value4^value5^RandomByte(pAd);
}


/*
	========================================================================

	Routine Description:
		Verify the support rate for different PHY type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
VOID	RTMPCheckRates(
	IN		PRTMP_ADAPTER	pAd,
	IN OUT	UCHAR			SupRate[],
	IN OUT	UCHAR			*SupRateLen)
{
	UCHAR	RateIdx, i, j;
	UCHAR	NewRate[12], NewRateLen;
	
	NewRateLen = 0;
	
	if (pAd->CommonCfg.PhyMode == PHY_11B)
		RateIdx = 4;
	else
		RateIdx = 12;

	/* Check for support rates exclude basic rate bit	*/
	for (i = 0; i < *SupRateLen; i++)
		for (j = 0; j < RateIdx; j++)
			if ((SupRate[i] & 0x7f) == RateIdTo500Kbps[j])
				NewRate[NewRateLen++] = SupRate[i];
			
	*SupRateLen = NewRateLen;
	NdisMoveMemory(SupRate, NewRate, NewRateLen);
}

#ifdef CONFIG_STA_SUPPORT
#ifdef DOT11_N_SUPPORT
BOOLEAN RTMPCheckChannel(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		CentralChannel,
	IN UCHAR		Channel)
{
	UCHAR		k;
	UCHAR		UpperChannel = 0, LowerChannel = 0;
	UCHAR		NoEffectChannelinList = 0;
	
	/* Find upper and lower channel according to 40MHz current operation. */
	if (CentralChannel < Channel)
	{
		UpperChannel = Channel;
		if (CentralChannel > 2)
			LowerChannel = CentralChannel - 2;
		else
			return FALSE;
	}
	else if (CentralChannel > Channel)
	{
		UpperChannel = CentralChannel + 2;
		LowerChannel = Channel;
	}

	for (k = 0;k < pAd->ChannelListNum;k++)
	{
		if (pAd->ChannelList[k].Channel == UpperChannel)
		{
			NoEffectChannelinList ++;
		}
		if (pAd->ChannelList[k].Channel == LowerChannel)
		{
			NoEffectChannelinList ++;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE,("Total Channel in Channel List = [%d]\n", NoEffectChannelinList));
	if (NoEffectChannelinList == 2)
		return TRUE;
	else
		return FALSE;
}

/*
	========================================================================

	Routine Description:
		Verify the support rate for HT phy type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		FALSE if pAd->CommonCfg.SupportedHtPhy doesn't accept the pHtCapability.  (AP Mode)

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
BOOLEAN 	RTMPCheckHt(
	IN	PRTMP_ADAPTER			pAd,
	IN	UCHAR					Wcid,
	IN 	HT_CAPABILITY_IE		*pHtCapability,
	IN 	ADD_HT_INFO_IE			*pAddHtInfo)
{
	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	/* If use AMSDU, set flag.*/
	if (pAd->CommonCfg.DesiredHtPhy.AmsduEnable)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_AMSDU_INUSED);
	/* Save Peer Capability*/
	if (pHtCapability->HtCapInfo.ShortGIfor20)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_SGI20_CAPABLE);
	if (pHtCapability->HtCapInfo.ShortGIfor40)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_SGI40_CAPABLE);
	if (pHtCapability->HtCapInfo.TxSTBC)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_TxSTBC_CAPABLE);
	if (pHtCapability->HtCapInfo.RxSTBC)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_RxSTBC_CAPABLE);
	if (pAd->CommonCfg.bRdg && pHtCapability->ExtHtCapInfo.RDGSupport)
	{
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid], fCLIENT_STATUS_RDG_CAPABLE);
	}
	
	if (Wcid < MAX_LEN_OF_MAC_TABLE)
	{
		pAd->MacTab.Content[Wcid].MpduDensity = pHtCapability->HtCapParm.MpduDensity;
	}

	/* Will check ChannelWidth for MCSSet[4] below*/
	pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
	pAd->MlmeAux.HtCapability.MCSSet[2] = 0x00;
	pAd->MlmeAux.HtCapability.MCSSet[4] = 0x1;
    switch (pAd->CommonCfg.RxStream)
	{
		case 1:			
			pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
			pAd->MlmeAux.HtCapability.MCSSet[1] = 0x00;
            pAd->MlmeAux.HtCapability.MCSSet[2] = 0x00;
            pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
			break;
		case 2:
			pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
			pAd->MlmeAux.HtCapability.MCSSet[1] = 0xff;
            pAd->MlmeAux.HtCapability.MCSSet[2] = 0x00;
            pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
			break;
		case 3:				
			pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
			pAd->MlmeAux.HtCapability.MCSSet[1] = 0xff;
            pAd->MlmeAux.HtCapability.MCSSet[2] = 0xff;
            pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
			break;
	}	

	pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth = pAddHtInfo->AddHtInfo.RecomWidth & pAd->CommonCfg.DesiredHtPhy.ChannelWidth;
		
	/*
		If both station and AP use 40MHz, still need to check if the 40MHZ band's legality in my country region
		If this 40MHz wideband is not allowed in my country list, use bandwidth 20MHZ instead,
	*/
	if (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_40)
	{
		if (RTMPCheckChannel(pAd, pAd->MlmeAux.CentralChannel, pAd->MlmeAux.Channel) == FALSE)
		{
			pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth = BW_20;
		}
	}
		
    DBGPRINT(RT_DEBUG_TRACE, ("RTMPCheckHt:: HtCapInfo.ChannelWidth=%d, RecomWidth=%d, DesiredHtPhy.ChannelWidth=%d, BW40MAvailForA/G=%d/%d, PhyMode=%d \n",
		pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth, pAddHtInfo->AddHtInfo.RecomWidth, pAd->CommonCfg.DesiredHtPhy.ChannelWidth,
		pAd->NicConfig2.field.BW40MAvailForA, pAd->NicConfig2.field.BW40MAvailForG, pAd->CommonCfg.PhyMode));
    
	pAd->MlmeAux.HtCapability.HtCapInfo.GF =  pHtCapability->HtCapInfo.GF &pAd->CommonCfg.DesiredHtPhy.GF;

	/* Send Assoc Req with my HT capability.*/
	pAd->MlmeAux.HtCapability.HtCapInfo.AMsduSize =  pAd->CommonCfg.DesiredHtPhy.AmsduSize;
	pAd->MlmeAux.HtCapability.HtCapInfo.MimoPs =  pAd->CommonCfg.DesiredHtPhy.MimoPs;
	pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor20 =  (pAd->CommonCfg.DesiredHtPhy.ShortGIfor20) & (pHtCapability->HtCapInfo.ShortGIfor20);
	pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor40 =  (pAd->CommonCfg.DesiredHtPhy.ShortGIfor40) & (pHtCapability->HtCapInfo.ShortGIfor40);
	pAd->MlmeAux.HtCapability.HtCapInfo.TxSTBC =  (pAd->CommonCfg.DesiredHtPhy.TxSTBC)&(pHtCapability->HtCapInfo.RxSTBC);
	pAd->MlmeAux.HtCapability.HtCapInfo.RxSTBC =  (pAd->CommonCfg.DesiredHtPhy.RxSTBC)&(pHtCapability->HtCapInfo.TxSTBC);
	pAd->MlmeAux.HtCapability.HtCapParm.MaxRAmpduFactor = pAd->CommonCfg.DesiredHtPhy.MaxRAmpduFactor;
    pAd->MlmeAux.HtCapability.HtCapParm.MpduDensity = pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity;
	pAd->MlmeAux.HtCapability.ExtHtCapInfo.PlusHTC = pHtCapability->ExtHtCapInfo.PlusHTC;
	pAd->MacTab.Content[Wcid].HTCapability.ExtHtCapInfo.PlusHTC = pHtCapability->ExtHtCapInfo.PlusHTC;
	if (pAd->CommonCfg.bRdg)
	{
		pAd->MlmeAux.HtCapability.ExtHtCapInfo.RDGSupport = pHtCapability->ExtHtCapInfo.RDGSupport;
        pAd->MlmeAux.HtCapability.ExtHtCapInfo.PlusHTC = 1;
	}
	
    if (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_20)
        pAd->MlmeAux.HtCapability.MCSSet[4] = 0x0;  /* BW20 can't transmit MCS32*/

	COPY_AP_HTSETTINGS_FROM_BEACON(pAd, pHtCapability);
	return TRUE;
}
#endif /* DOT11_N_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */

/*
	========================================================================

	Routine Description:
		Verify the support rate for different PHY type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
VOID RTMPUpdateMlmeRate(
	IN PRTMP_ADAPTER	pAd)
{
	UCHAR	MinimumRate;
	UCHAR	ProperMlmeRate; /*= RATE_54;*/
	UCHAR	i, j, RateIdx = 12; /*1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54*/
	BOOLEAN	bMatch = FALSE;

	switch (pAd->CommonCfg.PhyMode) 
	{
		case PHY_11B:
			ProperMlmeRate = RATE_11;
			MinimumRate = RATE_1;
			break;
		case PHY_11BG_MIXED:
#ifdef DOT11_N_SUPPORT
		case PHY_11ABGN_MIXED:
		case PHY_11BGN_MIXED:
#endif /* DOT11_N_SUPPORT */
			if ((pAd->MlmeAux.SupRateLen == 4) &&
				(pAd->MlmeAux.ExtRateLen == 0))
				/* B only AP*/
				ProperMlmeRate = RATE_11;
			else
				ProperMlmeRate = RATE_24;
			
			if (pAd->MlmeAux.Channel <= 14)
			MinimumRate = RATE_1;
			else
				MinimumRate = RATE_6;
			break;
		case PHY_11A:
#ifdef DOT11_N_SUPPORT
		case PHY_11N_2_4G:	/* rt2860 need to check mlmerate for 802.11n*/
		case PHY_11GN_MIXED:
		case PHY_11AGN_MIXED:
		case PHY_11AN_MIXED:
		case PHY_11N_5G:	
#endif /* DOT11_N_SUPPORT */
			ProperMlmeRate = RATE_24;
			MinimumRate = RATE_6;
			break;
		case PHY_11ABG_MIXED:
			ProperMlmeRate = RATE_24;
			if (pAd->MlmeAux.Channel <= 14)
			   MinimumRate = RATE_1;
			else
				MinimumRate = RATE_6;
			break;
		default: /* error*/
			ProperMlmeRate = RATE_1;
			MinimumRate = RATE_1;
			break;
	}

	for (i = 0; i < pAd->MlmeAux.SupRateLen; i++)
	{
		for (j = 0; j < RateIdx; j++)
		{
			if ((pAd->MlmeAux.SupRate[i] & 0x7f) == RateIdTo500Kbps[j])
			{
				if (j == ProperMlmeRate)
				{
					bMatch = TRUE;
					break;
				}
			}			
		}

		if (bMatch)
			break;
	}

	if (bMatch == FALSE)
	{
	for (i = 0; i < pAd->MlmeAux.ExtRateLen; i++)
	{
		for (j = 0; j < RateIdx; j++)
		{
			if ((pAd->MlmeAux.ExtRate[i] & 0x7f) == RateIdTo500Kbps[j])
			{
					if (j == ProperMlmeRate)
					{
						bMatch = TRUE;
						break;
					}
			}				
		}
		
			if (bMatch)
			break;		
	}
	}

	if (bMatch == FALSE)
	{
		ProperMlmeRate = MinimumRate;
	}

	pAd->CommonCfg.MlmeRate = MinimumRate;
	pAd->CommonCfg.RtsRate = ProperMlmeRate;
	if (pAd->CommonCfg.MlmeRate >= RATE_6)
	{
		pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
		pAd->CommonCfg.MlmeTransmit.field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MODE = MODE_OFDM;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
	}
	else
	{
		pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;
		pAd->CommonCfg.MlmeTransmit.field.MCS = pAd->CommonCfg.MlmeRate;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MODE = MODE_CCK;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MCS = pAd->CommonCfg.MlmeRate;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPUpdateMlmeRate ==>   MlmeTransmit = 0x%x  \n" , pAd->CommonCfg.MlmeTransmit.word));
}

CHAR RTMPMaxRssi(
	IN PRTMP_ADAPTER	pAd,
	IN CHAR				Rssi0,
	IN CHAR				Rssi1,
	IN CHAR				Rssi2)
{
	CHAR	larger = -127;
	
	if ((pAd->Antenna.field.RxPath == 1) && (Rssi0 != 0))
	{
		larger = Rssi0;
	}

	if ((pAd->Antenna.field.RxPath >= 2) && (Rssi1 != 0))
	{
		larger = max(Rssi0, Rssi1);
	}
	
	if ((pAd->Antenna.field.RxPath == 3) && (Rssi2 != 0))
	{
		larger = max(larger, Rssi2);
	}

	if (larger == -127)
		larger = 0;

	return larger;
}


CHAR RTMPMinSnr(
	IN PRTMP_ADAPTER	pAd,
	IN CHAR				Snr0,
	IN CHAR				Snr1)
{
	CHAR	smaller = Snr0;
	
	if (pAd->Antenna.field.RxPath == 1) 
	{
		smaller = Snr0;
	}

	if ((pAd->Antenna.field.RxPath >= 2) && (Snr1 != 0))
	{
		smaller = min(Snr0, Snr1);
	}
 
	return smaller;
}

/*
    ========================================================================
    Routine Description:
        Periodic evaluate antenna link status
        
    Arguments:
        pAd         - Adapter pointer
        
    Return Value:
        None
        
    ========================================================================
*/
VOID AsicEvaluateRxAnt(
	IN PRTMP_ADAPTER	pAd)
{
#ifdef CONFIG_STA_SUPPORT
	UCHAR	BBPR3 = 0;
#endif /* CONFIG_STA_SUPPORT */

#ifdef RALINK_ATE
	if (ATE_ON(pAd))
		return;
#endif /* RALINK_ATE */

#ifdef RTMP_MAC_USB
#ifdef CONFIG_STA_SUPPORT
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
		return;
#endif /* CONFIG_STA_SUPPORT */
#endif /* RTMP_MAC_USB */

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS	|
							fRTMP_ADAPTER_HALT_IN_PROGRESS	|
							fRTMP_ADAPTER_RADIO_OFF			|
							fRTMP_ADAPTER_NIC_NOT_EXIST		|
							fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS) ||
							OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) 
							)
		return;
	
	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{

			if (pAd->StaCfg.Psm == PWR_SAVE)
				return;

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPR3);
			BBPR3 &= (~0x18);
			if(pAd->Antenna.field.RxPath == 3)
			{
				BBPR3 |= (0x10);
			}
			else if(pAd->Antenna.field.RxPath == 2)
			{
				BBPR3 |= (0x8);
			}
			else if(pAd->Antenna.field.RxPath == 1)
			{
				BBPR3 |= (0x0);
			}
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPR3);
			if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
			)
			{
				ULONG	TxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount + 
									pAd->RalinkCounters.OneSecTxRetryOkCount + 
									pAd->RalinkCounters.OneSecTxFailCount;

				/* dynamic adjust antenna evaluation period according to the traffic*/
				if (TxTotalCnt > 50)
				{
					RTMPSetTimer(&pAd->Mlme.RxAntEvalTimer, 20);
					pAd->Mlme.bLowThroughput = FALSE;
				}
				else
				{
					RTMPSetTimer(&pAd->Mlme.RxAntEvalTimer, 300);
					pAd->Mlme.bLowThroughput = TRUE;
				}
			}
		}
#endif /* CONFIG_STA_SUPPORT */
	}
}

/*
    ========================================================================
    Routine Description:
        After evaluation, check antenna link status
        
    Arguments:
        pAd         - Adapter pointer
        
    Return Value:
        None
        
    ========================================================================
*/
VOID AsicRxAntEvalTimeout(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	RTMP_ADAPTER	*pAd = (RTMP_ADAPTER *)FunctionContext;
#ifdef CONFIG_STA_SUPPORT
	UCHAR			BBPR3 = 0;
	CHAR			larger = -127, rssi0, rssi1, rssi2;
#endif /* CONFIG_STA_SUPPORT */



#ifdef RALINK_ATE
	if (ATE_ON(pAd))
		return;
#endif /* RALINK_ATE */

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS	|
							fRTMP_ADAPTER_HALT_IN_PROGRESS	|
							fRTMP_ADAPTER_RADIO_OFF			|
							fRTMP_ADAPTER_NIC_NOT_EXIST) ||
							OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) 
							)
		return;

	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if (pAd->StaCfg.Psm == PWR_SAVE)
				return;


			/* if the traffic is low, use average rssi as the criteria*/
			if (pAd->Mlme.bLowThroughput == TRUE)
			{
				rssi0 = pAd->StaCfg.RssiSample.LastRssi0;
				rssi1 = pAd->StaCfg.RssiSample.LastRssi1;
				rssi2 = pAd->StaCfg.RssiSample.LastRssi2;
			}
			else
			{
				rssi0 = pAd->StaCfg.RssiSample.AvgRssi0;
				rssi1 = pAd->StaCfg.RssiSample.AvgRssi1;
				rssi2 = pAd->StaCfg.RssiSample.AvgRssi2;
			}

			if(pAd->Antenna.field.RxPath == 3)
			{
				larger = max(rssi0, rssi1);
#ifdef DOT11N_SS3_SUPPORT
				if (IS_RT2883(pAd) || IS_RT3883(pAd) || IS_RT3593(pAd))
				{
					pAd->Mlme.RealRxPath = 3;
				}
				else
#endif /* DOT11N_SS3_SUPPORT */
				if (larger > (rssi2 + 20))
					pAd->Mlme.RealRxPath = 2;
				else
					pAd->Mlme.RealRxPath = 3;
			}
			else if(pAd->Antenna.field.RxPath == 2)
			{
				if (rssi0 > (rssi1 + 20))
					pAd->Mlme.RealRxPath = 1;
				else
					pAd->Mlme.RealRxPath = 2;
			}

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPR3);
			BBPR3 &= (~0x18);
			if(pAd->Mlme.RealRxPath == 3)
			{
				BBPR3 |= (0x10);
			}
			else if(pAd->Mlme.RealRxPath == 2)
			{
				BBPR3 |= (0x8);
			}
			else if(pAd->Mlme.RealRxPath == 1)
			{
				BBPR3 |= (0x0);
			}
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPR3);
		}
#endif /* CONFIG_STA_SUPPORT */
	}
}


VOID APSDPeriodicExec(
	IN PVOID SystemSpecific1, 
	IN PVOID FunctionContext, 
	IN PVOID SystemSpecific2, 
	IN PVOID SystemSpecific3) 
{
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED) &&
		!OPSTATUS_TEST_FLAG(pAd, fOP_AP_STATUS_MEDIA_STATE_CONNECTED))
		return;

	pAd->CommonCfg.TriggerTimerCount++;

/* Driver should not send trigger frame, it should be send by application layer*/
/*
	if (pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable
		&& (pAd->CommonCfg.bNeedSendTriggerFrame ||
		(((pAd->CommonCfg.TriggerTimerCount%20) == 19) && (!pAd->CommonCfg.bAPSDAC_BE || !pAd->CommonCfg.bAPSDAC_BK || !pAd->CommonCfg.bAPSDAC_VI || !pAd->CommonCfg.bAPSDAC_VO))))
	{
		DBGPRINT(RT_DEBUG_TRACE,("Sending trigger frame and enter service period when support APSD\n"));
		RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, TRUE);
		pAd->CommonCfg.bNeedSendTriggerFrame = FALSE;
		pAd->CommonCfg.TriggerTimerCount = 0;
		pAd->CommonCfg.bInServicePeriod = TRUE;
	}*/
}

/*
    ========================================================================
    Routine Description:
        Set/reset MAC registers according to bPiggyBack parameter
        
    Arguments:
        pAd         - Adapter pointer
        bPiggyBack  - Enable / Disable Piggy-Back

    Return Value:
        None
        
    ========================================================================
*/
VOID RTMPSetPiggyBack(
    IN PRTMP_ADAPTER    pAd,
    IN BOOLEAN          bPiggyBack)
{
	TX_LINK_CFG_STRUC  TxLinkCfg;
    
	RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);

	TxLinkCfg.field.TxCFAckEn = bPiggyBack;
	RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
}

/*
    ========================================================================
    Routine Description:
        check if this entry need to switch rate automatically
        
    Arguments:
        pAd         
        pEntry 	 	

    Return Value:
        TURE
        FALSE
        
    ========================================================================
*/
BOOLEAN RTMPCheckEntryEnableAutoRateSwitch(
	IN PRTMP_ADAPTER    pAd,
	IN PMAC_TABLE_ENTRY	pEntry)	
{
	BOOLEAN		result = TRUE;


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* only associated STA counts*/
		if ((pEntry && IS_ENTRY_CLIENT(pEntry) && (pEntry->Sst == SST_ASSOC))
#ifdef QOS_DLS_SUPPORT
			|| (pEntry && IS_ENTRY_DLS(pEntry))
#endif /* QOS_DLS_SUPPORT */
			)
		{
			result = pAd->StaCfg.bAutoTxRateSwitch;
		}
		else
			result = FALSE;
	}
#endif /* CONFIG_STA_SUPPORT */



	return result;
}


BOOLEAN RTMPAutoRateSwitchCheck(
	IN PRTMP_ADAPTER    pAd)	
{			

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (pAd->StaCfg.bAutoTxRateSwitch)
			return TRUE;
	}
#endif /* CONFIG_STA_SUPPORT */
	return FALSE;
}


/*
    ========================================================================
    Routine Description:
        check if this entry need to fix tx legacy rate
        
    Arguments:
        pAd         
        pEntry 	 	

    Return Value:
        TURE
        FALSE
        
    ========================================================================
*/
UCHAR RTMPStaFixedTxMode(
	IN PRTMP_ADAPTER    pAd,
	IN PMAC_TABLE_ENTRY	pEntry)	
{
	UCHAR	tx_mode = FIXED_TXMODE_HT;


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		tx_mode = (UCHAR)pAd->StaCfg.DesiredTransmitSetting.field.FixedTxMode;
	}
#endif /* CONFIG_STA_SUPPORT */

	return tx_mode;
}

/*
    ========================================================================
    Routine Description:
        Overwrite HT Tx Mode by Fixed Legency Tx Mode, if specified.
        
    Arguments:
        pAd         
        pEntry 	 	

    Return Value:
        TURE
        FALSE
        
    ========================================================================
*/
VOID RTMPUpdateLegacyTxSetting(
		UCHAR				fixed_tx_mode,
		PMAC_TABLE_ENTRY	pEntry)
{
	HTTRANSMIT_SETTING TransmitSetting;
	
	if (fixed_tx_mode == FIXED_TXMODE_HT)
		return;
							 				
	TransmitSetting.word = 0;

	TransmitSetting.field.MODE = pEntry->HTPhyMode.field.MODE;
	TransmitSetting.field.MCS = pEntry->HTPhyMode.field.MCS;
						
	if (fixed_tx_mode == FIXED_TXMODE_CCK)
	{
		TransmitSetting.field.MODE = MODE_CCK;
		/* CCK mode allow MCS 0~3*/
		if (TransmitSetting.field.MCS > MCS_3)
			TransmitSetting.field.MCS = MCS_3;
	}
	else 
	{
		TransmitSetting.field.MODE = MODE_OFDM;
		/* OFDM mode allow MCS 0~7*/
		if (TransmitSetting.field.MCS > MCS_7)
			TransmitSetting.field.MCS = MCS_7;
	}
	
	if (pEntry->HTPhyMode.field.MODE >= TransmitSetting.field.MODE)
	{
		pEntry->HTPhyMode.word = TransmitSetting.word;
		DBGPRINT(RT_DEBUG_TRACE, ("RTMPUpdateLegacyTxSetting : wcid-%d, MODE=%s, MCS=%d \n", 
				pEntry->Aid, GetPhyMode(pEntry->HTPhyMode.field.MODE), pEntry->HTPhyMode.field.MCS));		
	}													
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : the fixed TxMode is invalid \n", __FUNCTION__));	
	}
}

#ifdef CONFIG_STA_SUPPORT
/*
	==========================================================================
	Description:
		dynamic tune BBP R66 to find a balance between sensibility and 
		noise isolation

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AsicStaBbpTuning(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR	OrigR66Value = 0, R66;/*, R66UpperBound = 0x30, R66LowerBound = 0x30;*/
	CHAR	Rssi;

	/* 2860C did not support Fase CCA, therefore can't tune*/
	if (pAd->MACVersion == 0x28600100)
		return;

	
	/* work as a STA*/
	
	if (pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE)  /* no R66 tuning when SCANNING*/
		return;

	if ((pAd->OpMode == OPMODE_STA) 
		&& (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
			)
		&& !(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		)
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &OrigR66Value);
		R66 = OrigR66Value;
		
		if (pAd->Antenna.field.RxPath > 1)
			Rssi = (pAd->StaCfg.RssiSample.AvgRssi0 + pAd->StaCfg.RssiSample.AvgRssi1) >> 1;
		else
			Rssi = pAd->StaCfg.RssiSample.AvgRssi0;

		RTMP_CHIP_ASIC_STA_BBP_ADJUST(pAd, Rssi, R66);


	}
}
#endif /* CONFIG_STA_SUPPORT */

VOID RTMPSetAGCInitValue(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			BandWidth)
{
	RTMP_CHIP_ASIC_AGC_INIT_VALUE_SET(pAd, BandWidth);

}


/*
========================================================================
Routine Description:
	Check if the channel has the property.

Arguments:
	pAd				- WLAN control block pointer
	ChanNum			- channel number
	Property		- channel property, CHANNEL_PASSIVE_SCAN, etc.

Return Value:
	TRUE			- YES
	FALSE			- NO

Note:
========================================================================
*/
BOOLEAN CHAN_PropertyCheck(
	IN PRTMP_ADAPTER	pAd,
	IN UINT32			ChanNum,
	IN UCHAR			Property)
{
	UINT32 IdChan;


	/* look for all registered channels */
	for(IdChan=0; IdChan<pAd->ChannelListNum; IdChan++)
	{
		if (pAd->ChannelList[IdChan].Channel == ChanNum)
		{
			if ((pAd->ChannelList[IdChan].Flags & Property) == Property)
				return TRUE; /* same property */
			/* End of if */

			break;
		} /* End of if */
	} /* End of for */

	return FALSE;
}



/* Enable the stream mode*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID AsicEnableStreamMode(
	IN PRTMP_ADAPTER pAd)
{
	TX_CHAIN_ADDR0_L_STRUC TxChainAddr0L = {{0}};
	TX_CHAIN_ADDR0_H_STRUC TxChainAddr0H = {{0}};
	TX_CHAIN_ADDR1_H_STRUC TxChainAddr1H = {{0}};
	TX_CHAIN_ADDR2_H_STRUC TxChainAddr2H = {{0}};
	TX_CHAIN_ADDR3_H_STRUC TxChainAddr3H = {{0}};

	DBGPRINT(RT_DEBUG_INFO, ("---> %s\n", __FUNCTION__));

	/* Chain #0 for broadcast*/
	TxChainAddr0L.field.TxChainAddr0L_Byte3 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte2 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte1 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte0 = 0xFF;
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR0_L, TxChainAddr0L.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR0_H, &TxChainAddr0H.word);
	TxChainAddr0H.field.TxChainAddr0H_Byte4 = 0xFF;
	TxChainAddr0H.field.TxChainAddr0H_Byte5 = 0xFF;
	TxChainAddr0H.field.TxChainSel0 = 0xF; /* Enable the stream mode for chain #0*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR0_H, TxChainAddr0H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR1_H, &TxChainAddr1H.word);
	TxChainAddr1H.field.TxChainSel0 = 0xF; /* Enable the stream mode for chain #1*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR1_H, TxChainAddr1H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR2_H, &TxChainAddr2H.word);
	TxChainAddr2H.field.TxChainSel0 = 0xF; /* Enable the stream mode for chain #2*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR2_H, TxChainAddr2H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR3_H, &TxChainAddr3H.word);
	TxChainAddr3H.field.TxChainSel0 = 0xF; /* Enable the stream mode for chain #3*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR3_H, TxChainAddr3H.word);
	
	DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));
}


/* Disable the stream mode*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID AsicDisableStreamMode(
	IN PRTMP_ADAPTER pAd)
{
	TX_CHAIN_ADDR0_L_STRUC TxChainAddr0L = {{0}};
	TX_CHAIN_ADDR0_H_STRUC TxChainAddr0H = {{0}};
	TX_CHAIN_ADDR1_H_STRUC TxChainAddr1H = {{0}};
	TX_CHAIN_ADDR2_H_STRUC TxChainAddr2H = {{0}};
	TX_CHAIN_ADDR3_H_STRUC TxChainAddr3H = {{0}};

	DBGPRINT(RT_DEBUG_INFO, ("---> %s\n", __FUNCTION__));

	/* Chain #0 for broadcast*/
	TxChainAddr0L.field.TxChainAddr0L_Byte3 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte2 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte1 = 0xFF;
	TxChainAddr0L.field.TxChainAddr0L_Byte0 = 0xFF;
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR0_L, TxChainAddr0L.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR0_H, &TxChainAddr0H.word);
	TxChainAddr0H.field.TxChainAddr0H_Byte4 = 0xFF;
	TxChainAddr0H.field.TxChainAddr0H_Byte5 = 0xFF;
	TxChainAddr0H.field.TxChainSel0 = 0x0; /* Disable the stream mode for chain #0*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR0_H, TxChainAddr0H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR1_H, &TxChainAddr1H.word);
	TxChainAddr1H.field.TxChainSel0 = 0x0; /* Disable the stream mode for chain #1*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR1_H, TxChainAddr1H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR2_H, &TxChainAddr2H.word);
	TxChainAddr2H.field.TxChainSel0 = 0x0; /* Disable the stream mode for chain #2*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR2_H, TxChainAddr2H.word);

	RTMP_IO_READ32(pAd, TX_CHAIN_ADDR3_H, &TxChainAddr3H.word);
	TxChainAddr3H.field.TxChainSel0 = 0x0; /* Disable the stream mode for chain #3*/
	RTMP_IO_WRITE32(pAd, TX_CHAIN_ADDR3_H, TxChainAddr3H.word);
	
	DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));
}
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
VOID AsicCheckForHwRecovery(
	IN PRTMP_ADAPTER	pAd) 
{
	/*
	 When testing power save features in 2872 in vista, 3 hardware bug happens. Also find those bugs can be recovered from reset. 
	 So in this section, driver make "if decision" for those 3 dead situations and do the correspoding reset action to recover. by JanLee. 2008-April.
	*/
	if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{		
		/* When the NIC performs scan operation, it may not receive any packets on higher channels.
		   This is because fewer APs use higher channel.
		   Therefore, the pAd->SameRxByteCount may accidentally trigger error recovery.
		   If I am GO in scan progress, I still continue to maintain rxbytecount. Let beacon can send out asap.
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)))
*/		
		{
			/*
			 Bug 1. BBP dead. Need to Hard-Reset BBP
			*/
			 if (pAd->RalinkCounters.LastReceivedByteCount == pAd->RalinkCounters.ReceivedByteCount)
			{
				/* If ReceiveByteCount doesn't change,  increase SameRxByteCount by 1. */
				pAd->SameRxByteCount++;
			}
			else
			{
				pAd->SameRxByteCount = 0;
				pAd->BbpResetCount = 0;
			}	


			/* If after BBP, still not work...need to check to reset PBF. */
			/*
			if (pAd->SameRxByteCount == 702)
			{
				pAd->SameRxByteCount = 0;
				AsicResetPBF(pAd);
				AsicResetMAC(pAd);
			}
			*/
			/* If SameRxByteCount keeps happens 
			   for 3 second in infra mode
			   for 5 seconds in idle mode
			   for 1 second in P2PGO */
				if (
#ifdef CONFIG_STA_SUPPORT
					((INFRA_ON(pAd)) && (pAd->SameRxByteCount > 3)) || 
					((IDLE_ON(pAd)) && (pAd->SameRxByteCount > 5)) 
#endif /* CONFIG_STA_SUPPORT */
				)
				{
						DBGPRINT(RT_DEBUG_TRACE, ("AsicCheckForHwRecovery!! \n"));

#ifdef CONFIG_STA_SUPPORT
						if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) 
							|| (RTMP_TEST_PSFLAG(pAd, fRTMP_PS_SET_PCI_CLK_OFF_COMMAND)) 
							|| RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
							AsicForceWakeup(pAd, DOT11POWERSAVE);	
#endif // CONFIG_STA_SUPPORT //

						//Patch Scan no AP issue
						//Firmware will write RF_R08(xtal_vdd_select) to LDOADC. This is HW issue.
						//New RT3572(0x35720223) should not have this issue.
						if (IS_RT5390(pAd))
						{
							UCHAR RfValue;
							RT30xxReadRFRegister(pAd, RF_R42, &RfValue);
							RfValue = (RfValue | 0xC0); // rx_ctb_en, rx_mix2_en
							RT30xxWriteRFRegister(pAd, RF_R42, RfValue);
						}

						//AsicResetBBPAgent(pAd);
						pAd->SameRxByteCount=0;
						pAd->BbpResetCount++;
				}
			}
			
			// Update lastReceiveByteCount.
			pAd->RalinkCounters.LastReceivedByteCount = pAd->RalinkCounters.ReceivedByteCount;			
	}
}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

