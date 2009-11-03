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
	chlist.c

	Abstract:

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

static CH_REGION ChRegion[] =
{
		{	// Antigua and Berbuda
			"AG",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Argentina
			"AR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Aruba
			"AW",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Australia
			"AU",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Austria
			"AT",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, IDOR, TRUE},		// 5G, ch 36~48
				{ 52,  4,  23, IDOR, TRUE},		// 5G, ch 52~64
				{ 100, 11, 30, BOTH, TRUE},		// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Bahamas
			"BS",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Barbados
			"BB",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Bermuda
			"BM",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Brazil
			"BR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 24, BOTH, FALSE},	// 5G, ch 100~140
				{ 149, 5,  30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Belgium
			"BE",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  18, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,  18, IDOR, FALSE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Bulgaria
			"BG",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11, 30, ODOR, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Canada
			"CA",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Cayman IsLands
			"KY",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Chile
			"CL",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  20, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  20, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 5,  20, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// China
			"CN",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149, 4,  27, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Colombia
			"CO",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  17, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11, 30, BOTH, FALSE},	// 5G, ch 100~140
				{ 149, 5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Costa Rica
			"CR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  17, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149, 4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Cyprus
			"CY",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,  24, IDOR, TRUE},		// 5G, ch 52~64
				{ 100, 11, 30, BOTH, TRUE},		// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Czech_Republic
			"CZ",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, IDOR, TRUE},		// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Denmark
			"DK",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,  23, IDOR, TRUE},		// 5G, ch 52~64
				{ 100, 11, 30, BOTH, TRUE},		// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Dominican Republic
			"DO",
			CE,
			{
				{ 1,   0,  20, BOTH, FALSE},	// 2.4 G, ch 0
				{ 149, 4,  20, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Equador
			"EC",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 100, 11,  27, BOTH, FALSE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// El Salvador
			"SV",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,   30, BOTH, TRUE},	// 5G, ch 52~64
				{ 149, 4,   36, BOTH, TRUE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Finland
			"FI",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,   23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// France
			"FR",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,   23, IDOR, TRUE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Germany
			"DE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,   23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Greece
			"GR",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,  4,   23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, ODOR, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Guam
			"GU",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 36,  4,   17, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,   24, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, FALSE},	// 5G, ch 100~140
				{ 149,  5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Guatemala
			"GT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   17, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,   24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Haiti
			"HT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,  4,   17, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,  4,   24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Honduras
			"HN",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149,  4,  27, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Hong Kong
			"HK",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, FALSE},	// 5G, ch 52~64
				{ 149,  4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Hungary
			"HU",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Iceland
			"IS",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// India
			"IN",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149, 	4,  24, IDOR, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Indonesia
			"ID",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149, 	4,  27, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Ireland
			"IE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, ODOR, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Israel
			"IL",
			CE,
			{
				{ 1,    3,  20, IDOR, FALSE},	// 2.4 G, ch 1~3
				{ 4, 	6,  20, BOTH, FALSE},	// 2.4 G, ch 4~9
				{ 10, 	4,  20, IDOR, FALSE},	// 2.4 G, ch 10~13
				{ 0},							// end
			}
		},

		{	// Italy
			"IT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, ODOR, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Japan
			"JP",
			JAP,
			{
				{ 1,   14,  20, BOTH, FALSE},	// 2.4 G, ch 1~14
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 0},							// end
			}
		},

		{	// Jordan
			"JO",
			CE,
			{
				{ 1,   13,  20, IDOR, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 149, 	4,  23, IDOR, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Latvia
			"LV",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Liechtenstein
			"LI",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Lithuania
			"LT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Luxemburg
			"LU",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Malaysia
			"MY",
			CE,
			{
				{ 36, 	4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  5,  20, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Malta
			"MT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Marocco
			"MA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  24, IDOR, FALSE},	// 5G, ch 36~48
				{ 0},							// end
			}
		},

		{	// Mexico
			"MX",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  5,  30, IDOR, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Netherlands
			"NL",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  24, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// New Zealand
			"NZ",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  24, BOTH, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  24, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Norway
			"NO",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36, 	4,  24, IDOR, FALSE},	// 5G, ch 36~48
				{ 52, 	4,  24, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Peru
			"PE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149,  4,  27, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Portugal
			"PT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Poland
			"PL",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Romania
			"RO",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Russia
			"RU",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 149,  4,  20, IDOR, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Saudi Arabia
			"SA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  4,  23, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Serbia_and_Montenegro
			"CS",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 0},							// end
			}
		},

		{	// Singapore
			"SG",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 149,  4,  20, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Slovakia
			"SK",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Slovenia
			"SI",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// South Africa
			"ZA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, FALSE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 149,  4,  30, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// South Korea
			"KR",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  20, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,   4,  20, BOTH, FALSE},	// 5G, ch 52~64
				{ 100,  8,  20, BOTH, FALSE},	// 5G, ch 100~128
				{ 149,  4,  20, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Spain
			"ES",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  17, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Sweden
			"SE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Switzerland
			"CH",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~13
				{ 36,   4,  23, IDOR, TRUE},	// 5G, ch 36~48
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Taiwan
			"TW",
			CE,
			{
				{ 1,   11,  30, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 52,   4,  23, IDOR, FALSE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// Turkey
			"TR",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 36,   4,  23, BOTH, FALSE},	// 5G, ch 36~48
				{ 52,   4,  23, BOTH, FALSE},	// 5G, ch 52~64
				{ 0},							// end
			}
		},

		{	// UK
			"GB",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 36,   4,  23, IDOR, FALSE},	// 5G, ch 52~64
				{ 52,   4,  23, IDOR, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 0},							// end
			}
		},

		{	// Ukraine
			"UA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 0},							// end
			}
		},

		{	// United_Arab_Emirates
			"AE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 0},							// end
			}
		},

		{	// United_States
			"US",
			CE,
			{
				{ 1,   11,  30, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 36,   4,  17, IDOR, FALSE},	// 5G, ch 52~64
				{ 52,   4,  24, BOTH, TRUE},	// 5G, ch 52~64
				{ 100, 11,  30, BOTH, TRUE},	// 5G, ch 100~140
				{ 149,  5,  30, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},

		{	// Venezuela
			"VE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 149,  4,  27, BOTH, FALSE},	// 5G, ch 149~161
				{ 0},							// end
			}
		},

		{	// Default
			"",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	// 2.4 G, ch 1~11
				{ 36,   4,  20, BOTH, FALSE},	// 5G, ch 52~64
				{ 52,   4,  20, BOTH, FALSE},	// 5G, ch 52~64
				{ 100, 11,  20, BOTH, FALSE},	// 5G, ch 100~140
				{ 149,  5,  20, BOTH, FALSE},	// 5G, ch 149~165
				{ 0},							// end
			}
		},
};

static inline PCH_REGION GetChRegion(
	IN PUCHAR CntryCode)
{
	INT loop = 0;
	PCH_REGION pChRegion = NULL;

	while (strcmp(ChRegion[loop].CountReg, "") != 0)
	{
		if (strncmp(ChRegion[loop].CountReg, CntryCode, 2) == 0)
		{
			pChRegion = &ChRegion[loop];
			break;
		}
		loop++;
	}

	if (pChRegion == NULL)
		pChRegion = &ChRegion[loop];
	return pChRegion;
}

static inline VOID ChBandCheck(
	IN UCHAR PhyMode,
	OUT PUCHAR pChType)
{
	switch(PhyMode)
	{
		case PHY_11A:
		case PHY_11AN_MIXED:
			*pChType = BAND_5G;
			break;
		case PHY_11ABG_MIXED:
		case PHY_11AGN_MIXED:
		case PHY_11ABGN_MIXED:
			*pChType = BAND_BOTH;
			break;

		default:
			*pChType = BAND_24G;
			break;
	}
}

static inline UCHAR FillChList(
	IN PRTMP_ADAPTER pAd,
	IN PCH_DESP pChDesp,
	IN UCHAR Offset,
	IN UCHAR increment)
{
	INT i, j, l;
	UCHAR channel;

	j = Offset;
	for (i = 0; i < pChDesp->NumOfCh; i++)
	{
		channel = pChDesp->FirstChannel + i * increment;
		for (l=0; l<MAX_NUM_OF_CHANNELS; l++)
		{
			if (channel == pAd->TxPower[l].Channel)
			{
				pAd->ChannelList[j].Power = pAd->TxPower[l].Power;
				pAd->ChannelList[j].Power2 = pAd->TxPower[l].Power2;
				break;
			}
		}
		if (l == MAX_NUM_OF_CHANNELS)
			continue;

		pAd->ChannelList[j].Channel = pChDesp->FirstChannel + i * increment;
		pAd->ChannelList[j].MaxTxPwr = pChDesp->MaxTxPwr;
		pAd->ChannelList[j].DfsReq = pChDesp->DfsReq;
		j++;
	}
	pAd->ChannelListNum = j;

	return j;
}

static inline VOID CreateChList(
	IN PRTMP_ADAPTER pAd,
	IN PCH_REGION pChRegion,
	IN UCHAR Geography)
{
	INT i;
	UCHAR offset = 0;
	PCH_DESP pChDesp;
	UCHAR ChType;
	UCHAR increment;

	if (pChRegion == NULL)
		return;

	ChBandCheck(pAd->CommonCfg.PhyMode, &ChType);

	for (i=0; i<10; i++)
	{
		pChDesp = &pChRegion->ChDesp[i];
		if (pChDesp->FirstChannel == 0)
			break;

		if (ChType == BAND_5G)
		{
			if (pChDesp->FirstChannel <= 14)
				continue;
		}
		else if (ChType == BAND_24G)
		{
			if (pChDesp->FirstChannel > 14)
				continue;
		}

		if ((pChDesp->Geography == BOTH)
			|| (pChDesp->Geography == Geography))
        {
			if (pChDesp->FirstChannel > 14)
                increment = 4;
            else
                increment = 1;
			offset = FillChList(pAd, pChDesp, offset, increment);
        }
	}
}

static inline VOID BuildChannelListEx(
	IN PRTMP_ADAPTER pAd)
{
	PCH_REGION pChReg;

	pChReg = GetChRegion(pAd->CommonCfg.CountryCode);
	CreateChList(pAd, pChReg, pAd->CommonCfg.Geography);
}

static inline VOID BuildBeaconChList(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pBuf,
	OUT	PULONG pBufLen)
{
	INT i;
	ULONG TmpLen;
	PCH_REGION pChRegion;
	PCH_DESP pChDesp;
	UCHAR ChType;

	pChRegion = GetChRegion(pAd->CommonCfg.CountryCode);

	if (pChRegion == NULL)
		return;

	ChBandCheck(pAd->CommonCfg.PhyMode, &ChType);
	*pBufLen = 0;

	for (i=0; i<10; i++)
	{
		pChDesp = &pChRegion->ChDesp[i];
		if (pChDesp->FirstChannel == 0)
			break;

		if (ChType == BAND_5G)
		{
			if (pChDesp->FirstChannel <= 14)
				continue;
		}
		else if (ChType == BAND_24G)
		{
			if (pChDesp->FirstChannel > 14)
				continue;
		}

		if ((pChDesp->Geography == BOTH)
			|| (pChDesp->Geography == pAd->CommonCfg.Geography))
		{
			MakeOutgoingFrame(pBuf + *pBufLen,		&TmpLen,
								1,                 	&pChDesp->FirstChannel,
								1,                 	&pChDesp->NumOfCh,
								1,                 	&pChDesp->MaxTxPwr,
								END_OF_ARGS);
			*pBufLen += TmpLen;
		}
	}
}

static inline BOOLEAN IsValidChannel(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR channel)

{
	INT i;

	for (i = 0; i < pAd->ChannelListNum; i++)
	{
		if (pAd->ChannelList[i].Channel == channel)
			break;
	}

	if (i == pAd->ChannelListNum)
		return FALSE;
	else
		return TRUE;
}


static inline UCHAR GetExtCh(
	IN UCHAR Channel,
	IN UCHAR Direction)
{
	CHAR ExtCh;

	if (Direction == EXTCHA_ABOVE)
		ExtCh = Channel + 4;
	else
		ExtCh = (Channel - 4) > 0 ? (Channel - 4) : 0;

	return ExtCh;
}


static inline VOID N_ChannelCheck(
	IN PRTMP_ADAPTER pAd)
{
	//UCHAR ChannelNum = pAd->ChannelListNum;
	UCHAR Channel = pAd->CommonCfg.Channel;

	if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pAd->CommonCfg.RegTransmitSetting.field.BW  == BW_40))
	{
		if (Channel > 14)
		{
			if ((Channel == 36) || (Channel == 44) || (Channel == 52) || (Channel == 60) || (Channel == 100) || (Channel == 108) ||
			    (Channel == 116) || (Channel == 124) || (Channel == 132) || (Channel == 149) || (Channel == 157))
			{
				pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_ABOVE;
			}
			else if ((Channel == 40) || (Channel == 48) || (Channel == 56) || (Channel == 64) || (Channel == 104) || (Channel == 112) ||
					(Channel == 120) || (Channel == 128) || (Channel == 136) || (Channel == 153) || (Channel == 161))
			{
				pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_BELOW;
			}
			else
			{
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
			}
		}
		else
		{
			do
			{
				UCHAR ExtCh;
				UCHAR Dir = pAd->CommonCfg.RegTransmitSetting.field.EXTCHA;
				ExtCh = GetExtCh(Channel, Dir);
				if (IsValidChannel(pAd, ExtCh))
					break;

				Dir = (Dir == EXTCHA_ABOVE) ? EXTCHA_BELOW : EXTCHA_ABOVE;
				ExtCh = GetExtCh(Channel, Dir);
				if (IsValidChannel(pAd, ExtCh))
				{
					pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = Dir;
					break;
				}
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
			} while(FALSE);

			if (Channel == 14)
			{
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
				//pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_NONE;	// We didn't set the ExtCh as NONE due to it'll set in RTMPSetHT()
			}
		}
	}


}


static inline VOID N_SetCenCh(
	IN PRTMP_ADAPTER pAd)
{
	if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40)
	{
		if (pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_ABOVE)
		{
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel + 2;
		}
		else
		{
			if (pAd->CommonCfg.Channel == 14)
				pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 1;
			else
				pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 2;
		}
	}
	else
	{
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
	}
}

static inline UINT8 GetCuntryMaxTxPwr(
	IN PRTMP_ADAPTER pAd,
	IN UINT8 channel)
{
	int i;
	for (i = 0; i < pAd->ChannelListNum; i++)
	{
		if (pAd->ChannelList[i].Channel == channel)
			break;
	}

	if (i == pAd->ChannelListNum)
		return 0xff;
	else
		return pAd->ChannelList[i].MaxTxPwr;
}
#endif // __CHLIST_H__

