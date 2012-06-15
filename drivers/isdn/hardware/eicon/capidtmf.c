
/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "platform.h"









#include "capidtmf.h"

/* #define TRACE_ */

#define FILE_ "CAPIDTMF.C"

/*---------------------------------------------------------------------------*/


#define trace(a)



/*---------------------------------------------------------------------------*/

static short capidtmf_expand_table_alaw[0x0100] =
{
	-5504,   5504,   -344,    344, -22016,  22016,  -1376,   1376,
	-2752,   2752,    -88,     88, -11008,  11008,   -688,    688,
	-7552,   7552,   -472,    472, -30208,  30208,  -1888,   1888,
	-3776,   3776,   -216,    216, -15104,  15104,   -944,    944,
	-4480,   4480,   -280,    280, -17920,  17920,  -1120,   1120,
	-2240,   2240,    -24,     24,  -8960,   8960,   -560,    560,
	-6528,   6528,   -408,    408, -26112,  26112,  -1632,   1632,
	-3264,   3264,   -152,    152, -13056,  13056,   -816,    816,
	-6016,   6016,   -376,    376, -24064,  24064,  -1504,   1504,
	-3008,   3008,   -120,    120, -12032,  12032,   -752,    752,
	-8064,   8064,   -504,    504, -32256,  32256,  -2016,   2016,
	-4032,   4032,   -248,    248, -16128,  16128,  -1008,   1008,
	-4992,   4992,   -312,    312, -19968,  19968,  -1248,   1248,
	-2496,   2496,    -56,     56,  -9984,   9984,   -624,    624,
	-7040,   7040,   -440,    440, -28160,  28160,  -1760,   1760,
	-3520,   3520,   -184,    184, -14080,  14080,   -880,    880,
	-5248,   5248,   -328,    328, -20992,  20992,  -1312,   1312,
	-2624,   2624,    -72,     72, -10496,  10496,   -656,    656,
	-7296,   7296,   -456,    456, -29184,  29184,  -1824,   1824,
	-3648,   3648,   -200,    200, -14592,  14592,   -912,    912,
	-4224,   4224,   -264,    264, -16896,  16896,  -1056,   1056,
	-2112,   2112,     -8,      8,  -8448,   8448,   -528,    528,
	-6272,   6272,   -392,    392, -25088,  25088,  -1568,   1568,
	-3136,   3136,   -136,    136, -12544,  12544,   -784,    784,
	-5760,   5760,   -360,    360, -23040,  23040,  -1440,   1440,
	-2880,   2880,   -104,    104, -11520,  11520,   -720,    720,
	-7808,   7808,   -488,    488, -31232,  31232,  -1952,   1952,
	-3904,   3904,   -232,    232, -15616,  15616,   -976,    976,
	-4736,   4736,   -296,    296, -18944,  18944,  -1184,   1184,
	-2368,   2368,    -40,     40,  -9472,   9472,   -592,    592,
	-6784,   6784,   -424,    424, -27136,  27136,  -1696,   1696,
	-3392,   3392,   -168,    168, -13568,  13568,   -848,    848
};

static short capidtmf_expand_table_ulaw[0x0100] =
{
	-32124,  32124,  -1884,   1884,  -7932,   7932,   -372,    372,
	-15996,  15996,   -876,    876,  -3900,   3900,   -120,    120,
	-23932,  23932,  -1372,   1372,  -5884,   5884,   -244,    244,
	-11900,  11900,   -620,    620,  -2876,   2876,    -56,     56,
	-28028,  28028,  -1628,   1628,  -6908,   6908,   -308,    308,
	-13948,  13948,   -748,    748,  -3388,   3388,    -88,     88,
	-19836,  19836,  -1116,   1116,  -4860,   4860,   -180,    180,
	-9852,   9852,   -492,    492,  -2364,   2364,    -24,     24,
	-30076,  30076,  -1756,   1756,  -7420,   7420,   -340,    340,
	-14972,  14972,   -812,    812,  -3644,   3644,   -104,    104,
	-21884,  21884,  -1244,   1244,  -5372,   5372,   -212,    212,
	-10876,  10876,   -556,    556,  -2620,   2620,    -40,     40,
	-25980,  25980,  -1500,   1500,  -6396,   6396,   -276,    276,
	-12924,  12924,   -684,    684,  -3132,   3132,    -72,     72,
	-17788,  17788,   -988,    988,  -4348,   4348,   -148,    148,
	-8828,   8828,   -428,    428,  -2108,   2108,     -8,      8,
	-31100,  31100,  -1820,   1820,  -7676,   7676,   -356,    356,
	-15484,  15484,   -844,    844,  -3772,   3772,   -112,    112,
	-22908,  22908,  -1308,   1308,  -5628,   5628,   -228,    228,
	-11388,  11388,   -588,    588,  -2748,   2748,    -48,     48,
	-27004,  27004,  -1564,   1564,  -6652,   6652,   -292,    292,
	-13436,  13436,   -716,    716,  -3260,   3260,    -80,     80,
	-18812,  18812,  -1052,   1052,  -4604,   4604,   -164,    164,
	-9340,   9340,   -460,    460,  -2236,   2236,    -16,     16,
	-29052,  29052,  -1692,   1692,  -7164,   7164,   -324,    324,
	-14460,  14460,   -780,    780,  -3516,   3516,    -96,     96,
	-20860,  20860,  -1180,   1180,  -5116,   5116,   -196,    196,
	-10364,  10364,   -524,    524,  -2492,   2492,    -32,     32,
	-24956,  24956,  -1436,   1436,  -6140,   6140,   -260,    260,
	-12412,  12412,   -652,    652,  -3004,   3004,    -64,     64,
	-16764,  16764,   -924,    924,  -4092,   4092,   -132,    132,
	-8316,   8316,   -396,    396,  -1980,   1980,      0,      0
};


/*---------------------------------------------------------------------------*/

static short capidtmf_recv_window_function[CAPIDTMF_RECV_ACCUMULATE_CYCLES] =
{
	-500L,   -999L,  -1499L,  -1998L,  -2496L,  -2994L,  -3491L,  -3988L,
	-4483L,  -4978L,  -5471L,  -5963L,  -6454L,  -6943L,  -7431L,  -7917L,
	-8401L,  -8883L,  -9363L,  -9840L, -10316L, -10789L, -11259L, -11727L,
	-12193L, -12655L, -13115L, -13571L, -14024L, -14474L, -14921L, -15364L,
	-15804L, -16240L, -16672L, -17100L, -17524L, -17944L, -18360L, -18772L,
	-19180L, -19583L, -19981L, -20375L, -20764L, -21148L, -21527L, -21901L,
	-22270L, -22634L, -22993L, -23346L, -23694L, -24037L, -24374L, -24705L,
	-25030L, -25350L, -25664L, -25971L, -26273L, -26568L, -26858L, -27141L,
	-27418L, -27688L, -27952L, -28210L, -28461L, -28705L, -28943L, -29174L,
	-29398L, -29615L, -29826L, -30029L, -30226L, -30415L, -30598L, -30773L,
	-30941L, -31102L, -31256L, -31402L, -31541L, -31673L, -31797L, -31914L,
	-32024L, -32126L, -32221L, -32308L, -32388L, -32460L, -32524L, -32581L,
	-32631L, -32673L, -32707L, -32734L, -32753L, -32764L, -32768L, -32764L,
	-32753L, -32734L, -32707L, -32673L, -32631L, -32581L, -32524L, -32460L,
	-32388L, -32308L, -32221L, -32126L, -32024L, -31914L, -31797L, -31673L,
	-31541L, -31402L, -31256L, -31102L, -30941L, -30773L, -30598L, -30415L,
	-30226L, -30029L, -29826L, -29615L, -29398L, -29174L, -28943L, -28705L,
	-28461L, -28210L, -27952L, -27688L, -27418L, -27141L, -26858L, -26568L,
	-26273L, -25971L, -25664L, -25350L, -25030L, -24705L, -24374L, -24037L,
	-23694L, -23346L, -22993L, -22634L, -22270L, -21901L, -21527L, -21148L,
	-20764L, -20375L, -19981L, -19583L, -19180L, -18772L, -18360L, -17944L,
	-17524L, -17100L, -16672L, -16240L, -15804L, -15364L, -14921L, -14474L,
	-14024L, -13571L, -13115L, -12655L, -12193L, -11727L, -11259L, -10789L,
	-10316L,  -9840L,  -9363L,  -8883L,  -8401L,  -7917L,  -7431L,  -6943L,
	-6454L,  -5963L,  -5471L,  -4978L,  -4483L,  -3988L,  -3491L,  -2994L,
	-2496L,  -1998L,  -1499L,   -999L,   -500L,
};

static byte capidtmf_leading_zeroes_table[0x100] =
{
	8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define capidtmf_byte_leading_zeroes(b)  (capidtmf_leading_zeroes_table[(BYTE)(b)])
#define capidtmf_word_leading_zeroes(w)  (((w) & 0xff00) ? capidtmf_leading_zeroes_table[(w) >> 8] : 8 + capidtmf_leading_zeroes_table[(w)])
#define capidtmf_dword_leading_zeroes(d)  (((d) & 0xffff0000L) ?    (((d) & 0xff000000L) ? capidtmf_leading_zeroes_table[(d) >> 24] : 8 + capidtmf_leading_zeroes_table[(d) >> 16]) :    (((d) & 0xff00) ? 16 + capidtmf_leading_zeroes_table[(d) >> 8] : 24 + capidtmf_leading_zeroes_table[(d)]))


/*---------------------------------------------------------------------------*/


static void capidtmf_goertzel_loop(long *buffer, long *coeffs, short *sample, long count)
{
	int i, j;
	long c, d, q0, q1, q2;

	for (i = 0; i < CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT - 1; i++)
	{
		q1 = buffer[i];
		q2 = buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT];
		d = coeffs[i] >> 1;
		c = d << 1;
		if (c >= 0)
		{
			for (j = 0; j < count; j++)
			{
				q0 = sample[j] - q2 + (c * (q1 >> 16)) + (((dword)(((dword) d) * ((dword)(q1 & 0xffff)))) >> 15);
				q2 = q1;
				q1 = q0;
			}
		}
		else
		{
			c = -c;
			d = -d;
			for (j = 0; j < count; j++)
			{
				q0 = sample[j] - q2 - ((c * (q1 >> 16)) + (((dword)(((dword) d) * ((dword)(q1 & 0xffff)))) >> 15));
				q2 = q1;
				q1 = q0;
			}
		}
		buffer[i] = q1;
		buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] = q2;
	}
	q1 = buffer[i];
	q2 = buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT];
	c = (coeffs[i] >> 1) << 1;
	if (c >= 0)
	{
		for (j = 0; j < count; j++)
		{
			q0 = sample[j] - q2 + (c * (q1 >> 16)) + (((dword)(((dword)(c >> 1)) * ((dword)(q1 & 0xffff)))) >> 15);
			q2 = q1;
			q1 = q0;
			c -= CAPIDTMF_RECV_FUNDAMENTAL_DECREMENT;
		}
	}
	else
	{
		c = -c;
		for (j = 0; j < count; j++)
		{
			q0 = sample[j] - q2 - ((c * (q1 >> 16)) + (((dword)(((dword)(c >> 1)) * ((dword)(q1 & 0xffff)))) >> 15));
			q2 = q1;
			q1 = q0;
			c += CAPIDTMF_RECV_FUNDAMENTAL_DECREMENT;
		}
	}
	coeffs[i] = c;
	buffer[i] = q1;
	buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] = q2;
}


static void capidtmf_goertzel_result(long *buffer, long *coeffs)
{
	int i;
	long d, e, q1, q2, lo, mid, hi;
	dword k;

	for (i = 0; i < CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT; i++)
	{
		q1 = buffer[i];
		q2 = buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT];
		d = coeffs[i] >> 1;
		if (d >= 0)
			d = ((d << 1) * (-q1 >> 16)) + (((dword)(((dword) d) * ((dword)(-q1 & 0xffff)))) >> 15);
		else
			d = ((-d << 1) * (-q1 >> 16)) + (((dword)(((dword) -d) * ((dword)(-q1 & 0xffff)))) >> 15);
		e = (q2 >= 0) ? q2 : -q2;
		if (d >= 0)
		{
			k = ((dword)(d & 0xffff)) * ((dword)(e & 0xffff));
			lo = k & 0xffff;
			mid = k >> 16;
			k = ((dword)(d >> 16)) * ((dword)(e & 0xffff));
			mid += k & 0xffff;
			hi = k >> 16;
			k = ((dword)(d & 0xffff)) * ((dword)(e >> 16));
			mid += k & 0xffff;
			hi += k >> 16;
			hi += ((dword)(d >> 16)) * ((dword)(e >> 16));
		}
		else
		{
			d = -d;
			k = ((dword)(d & 0xffff)) * ((dword)(e & 0xffff));
			lo = -((long)(k & 0xffff));
			mid = -((long)(k >> 16));
			k = ((dword)(d >> 16)) * ((dword)(e & 0xffff));
			mid -= k & 0xffff;
			hi = -((long)(k >> 16));
			k = ((dword)(d & 0xffff)) * ((dword)(e >> 16));
			mid -= k & 0xffff;
			hi -= k >> 16;
			hi -= ((dword)(d >> 16)) * ((dword)(e >> 16));
		}
		if (q2 < 0)
		{
			lo = -lo;
			mid = -mid;
			hi = -hi;
		}
		d = (q1 >= 0) ? q1 : -q1;
		k = ((dword)(d & 0xffff)) * ((dword)(d & 0xffff));
		lo += k & 0xffff;
		mid += k >> 16;
		k = ((dword)(d >> 16)) * ((dword)(d & 0xffff));
		mid += (k & 0xffff) << 1;
		hi += (k >> 16) << 1;
		hi += ((dword)(d >> 16)) * ((dword)(d >> 16));
		d = (q2 >= 0) ? q2 : -q2;
		k = ((dword)(d & 0xffff)) * ((dword)(d & 0xffff));
		lo += k & 0xffff;
		mid += k >> 16;
		k = ((dword)(d >> 16)) * ((dword)(d & 0xffff));
		mid += (k & 0xffff) << 1;
		hi += (k >> 16) << 1;
		hi += ((dword)(d >> 16)) * ((dword)(d >> 16));
		mid += lo >> 16;
		hi += mid >> 16;
		buffer[i] = (lo & 0xffff) | (mid << 16);
		buffer[i + CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] = hi;
	}
}


/*---------------------------------------------------------------------------*/

#define CAPIDTMF_RECV_GUARD_SNR_INDEX_697     0
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_770     1
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_852     2
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_941     3
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1209    4
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1336    5
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1477    6
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1633    7
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_635     8
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1010    9
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1140    10
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1272    11
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1405    12
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1555    13
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1715    14
#define CAPIDTMF_RECV_GUARD_SNR_INDEX_1875    15

#define CAPIDTMF_RECV_GUARD_SNR_DONTCARE      0xc000
#define CAPIDTMF_RECV_NO_DIGIT                0xff
#define CAPIDTMF_RECV_TIME_GRANULARITY        (CAPIDTMF_RECV_ACCUMULATE_CYCLES + 1)

#define CAPIDTMF_RECV_INDICATION_DIGIT        0x0001

static long capidtmf_recv_goertzel_coef_table[CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] =
{
	0xda97L * 2,  /* 697 Hz (Low group 697 Hz) */
	0xd299L * 2,  /* 770 Hz (Low group 770 Hz) */
	0xc8cbL * 2,  /* 852 Hz (Low group 852 Hz) */
	0xbd36L * 2,  /* 941 Hz (Low group 941 Hz) */
	0x9501L * 2,  /* 1209 Hz (High group 1209 Hz) */
	0x7f89L * 2,  /* 1336 Hz (High group 1336 Hz) */
	0x6639L * 2,  /* 1477 Hz (High group 1477 Hz) */
	0x48c6L * 2,  /* 1633 Hz (High group 1633 Hz) */
	0xe14cL * 2,  /* 630 Hz (Lower guard of low group 631 Hz) */
	0xb2e0L * 2,  /* 1015 Hz (Upper guard of low group 1039 Hz) */
	0xa1a0L * 2,  /* 1130 Hz (Lower guard of high group 1140 Hz) */
	0x8a87L * 2,  /* 1272 Hz (Guard between 1209 Hz and 1336 Hz: 1271 Hz) */
	0x7353L * 2,  /* 1405 Hz (2nd harmonics of 697 Hz and guard between 1336 Hz and 1477 Hz: 1405 Hz) */
	0x583bL * 2,  /* 1552 Hz (2nd harmonics of 770 Hz and guard between 1477 Hz and 1633 Hz: 1553 Hz) */
	0x37d8L * 2,  /* 1720 Hz (2nd harmonics of 852 Hz and upper guard of high group: 1715 Hz) */
	0x0000L * 2   /* 100-630 Hz (fundamentals) */
};


static word capidtmf_recv_guard_snr_low_table[CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] =
{
	14,                                    /* Low group peak versus 697 Hz */
	14,                                    /* Low group peak versus 770 Hz */
	16,                                    /* Low group peak versus 852 Hz */
	16,                                    /* Low group peak versus 941 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1209 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1336 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1477 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1633 Hz */
	14,                                    /* Low group peak versus 635 Hz */
	16,                                    /* Low group peak versus 1010 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1140 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* Low group peak versus 1272 Hz */
	DSPDTMF_RX_HARMONICS_SEL_DEFAULT - 8,  /* Low group peak versus 1405 Hz */
	DSPDTMF_RX_HARMONICS_SEL_DEFAULT - 4,  /* Low group peak versus 1555 Hz */
	DSPDTMF_RX_HARMONICS_SEL_DEFAULT - 4,  /* Low group peak versus 1715 Hz */
	12                                     /* Low group peak versus 100-630 Hz */
};


static word capidtmf_recv_guard_snr_high_table[CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT] =
{
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 697 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 770 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 852 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 941 Hz */
	20,                                    /* High group peak versus 1209 Hz */
	20,                                    /* High group peak versus 1336 Hz */
	20,                                    /* High group peak versus 1477 Hz */
	20,                                    /* High group peak versus 1633 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 635 Hz */
	CAPIDTMF_RECV_GUARD_SNR_DONTCARE,      /* High group peak versus 1010 Hz */
	16,                                    /* High group peak versus 1140 Hz */
	4,                                     /* High group peak versus 1272 Hz */
	6,                                     /* High group peak versus 1405 Hz */
	8,                                     /* High group peak versus 1555 Hz */
	16,                                    /* High group peak versus 1715 Hz */
	12                                     /* High group peak versus 100-630 Hz */
};


/*---------------------------------------------------------------------------*/

static void capidtmf_recv_init(t_capidtmf_state *p_state)
{
	p_state->recv.min_gap_duration = 1;
	p_state->recv.min_digit_duration = 1;

	p_state->recv.cycle_counter = 0;
	p_state->recv.current_digit_on_time = 0;
	p_state->recv.current_digit_off_time = 0;
	p_state->recv.current_digit_value = CAPIDTMF_RECV_NO_DIGIT;

	p_state->recv.digit_write_pos = 0;
	p_state->recv.digit_read_pos = 0;
	p_state->recv.indication_state = 0;
	p_state->recv.indication_state_ack = 0;
	p_state->recv.state = CAPIDTMF_RECV_STATE_IDLE;
}


void capidtmf_recv_enable(t_capidtmf_state *p_state, word min_digit_duration, word min_gap_duration)
{
	p_state->recv.indication_state_ack &= CAPIDTMF_RECV_INDICATION_DIGIT;
	p_state->recv.min_digit_duration = (word)(((((dword) min_digit_duration) * 8) +
						   ((dword)(CAPIDTMF_RECV_TIME_GRANULARITY / 2))) / ((dword) CAPIDTMF_RECV_TIME_GRANULARITY));
	if (p_state->recv.min_digit_duration <= 1)
		p_state->recv.min_digit_duration = 1;
	else
		(p_state->recv.min_digit_duration)--;
	p_state->recv.min_gap_duration =
		(word)((((dword) min_gap_duration) * 8) / ((dword) CAPIDTMF_RECV_TIME_GRANULARITY));
	if (p_state->recv.min_gap_duration <= 1)
		p_state->recv.min_gap_duration = 1;
	else
		(p_state->recv.min_gap_duration)--;
	p_state->recv.state |= CAPIDTMF_RECV_STATE_DTMF_ACTIVE;
}


void capidtmf_recv_disable(t_capidtmf_state *p_state)
{
	p_state->recv.state &= ~CAPIDTMF_RECV_STATE_DTMF_ACTIVE;
	if (p_state->recv.state == CAPIDTMF_RECV_STATE_IDLE)
		capidtmf_recv_init(p_state);
	else
	{
		p_state->recv.cycle_counter = 0;
		p_state->recv.current_digit_on_time = 0;
		p_state->recv.current_digit_off_time = 0;
		p_state->recv.current_digit_value = CAPIDTMF_RECV_NO_DIGIT;
	}
}


word capidtmf_recv_indication(t_capidtmf_state *p_state, byte *buffer)
{
	word i, j, k, flags;

	flags = p_state->recv.indication_state ^ p_state->recv.indication_state_ack;
	p_state->recv.indication_state_ack ^= flags & CAPIDTMF_RECV_INDICATION_DIGIT;
	if (p_state->recv.digit_write_pos != p_state->recv.digit_read_pos)
	{
		i = 0;
		k = p_state->recv.digit_write_pos;
		j = p_state->recv.digit_read_pos;
		do
		{
			buffer[i++] = p_state->recv.digit_buffer[j];
			j = (j == CAPIDTMF_RECV_DIGIT_BUFFER_SIZE - 1) ? 0 : j + 1;
		} while (j != k);
		p_state->recv.digit_read_pos = k;
		return (i);
	}
	p_state->recv.indication_state_ack ^= flags;
	return (0);
}


#define CAPIDTMF_RECV_WINDOWED_SAMPLES  32

void capidtmf_recv_block(t_capidtmf_state *p_state, byte *buffer, word length)
{
	byte result_digit;
	word sample_number, cycle_counter, n, i;
	word low_peak, high_peak;
	dword lo, hi;
	byte   *p;
	short *q;
	byte goertzel_result_buffer[CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT];
	short windowed_sample_buffer[CAPIDTMF_RECV_WINDOWED_SAMPLES];


	if (p_state->recv.state & CAPIDTMF_RECV_STATE_DTMF_ACTIVE)
	{
		cycle_counter = p_state->recv.cycle_counter;
		sample_number = 0;
		while (sample_number < length)
		{
			if (cycle_counter < CAPIDTMF_RECV_ACCUMULATE_CYCLES)
			{
				if (cycle_counter == 0)
				{
					for (i = 0; i < CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT; i++)
					{
						p_state->recv.goertzel_buffer[0][i] = 0;
						p_state->recv.goertzel_buffer[1][i] = 0;
					}
				}
				n = CAPIDTMF_RECV_ACCUMULATE_CYCLES - cycle_counter;
				if (n > length - sample_number)
					n = length - sample_number;
				if (n > CAPIDTMF_RECV_WINDOWED_SAMPLES)
					n = CAPIDTMF_RECV_WINDOWED_SAMPLES;
				p = buffer + sample_number;
				q = capidtmf_recv_window_function + cycle_counter;
				if (p_state->ulaw)
				{
					for (i = 0; i < n; i++)
					{
						windowed_sample_buffer[i] =
							(short)((capidtmf_expand_table_ulaw[p[i]] * ((long)(q[i]))) >> 15);
					}
				}
				else
				{
					for (i = 0; i < n; i++)
					{
						windowed_sample_buffer[i] =
							(short)((capidtmf_expand_table_alaw[p[i]] * ((long)(q[i]))) >> 15);
					}
				}
				capidtmf_recv_goertzel_coef_table[CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT - 1] = CAPIDTMF_RECV_FUNDAMENTAL_OFFSET;
				capidtmf_goertzel_loop(p_state->recv.goertzel_buffer[0],
						       capidtmf_recv_goertzel_coef_table, windowed_sample_buffer, n);
				cycle_counter += n;
				sample_number += n;
			}
			else
			{
				capidtmf_goertzel_result(p_state->recv.goertzel_buffer[0],
							 capidtmf_recv_goertzel_coef_table);
				for (i = 0; i < CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT; i++)
				{
					lo = (dword)(p_state->recv.goertzel_buffer[0][i]);
					hi = (dword)(p_state->recv.goertzel_buffer[1][i]);
					if (hi != 0)
					{
						n = capidtmf_dword_leading_zeroes(hi);
						hi = (hi << n) | (lo >> (32 - n));
					}
					else
					{
						n = capidtmf_dword_leading_zeroes(lo);
						hi = lo << n;
						n += 32;
					}
					n = 195 - 3 * n;
					if (hi >= 0xcb300000L)
						n += 2;
					else if (hi >= 0xa1450000L)
						n++;
					goertzel_result_buffer[i] = (byte) n;
				}
				low_peak = DSPDTMF_RX_SENSITIVITY_LOW_DEFAULT;
				result_digit = CAPIDTMF_RECV_NO_DIGIT;
				for (i = 0; i < CAPIDTMF_LOW_GROUP_FREQUENCIES; i++)
				{
					if (goertzel_result_buffer[i] > low_peak)
					{
						low_peak = goertzel_result_buffer[i];
						result_digit = (byte) i;
					}
				}
				high_peak = DSPDTMF_RX_SENSITIVITY_HIGH_DEFAULT;
				n = CAPIDTMF_RECV_NO_DIGIT;
				for (i = CAPIDTMF_LOW_GROUP_FREQUENCIES; i < CAPIDTMF_RECV_BASE_FREQUENCY_COUNT; i++)
				{
					if (goertzel_result_buffer[i] > high_peak)
					{
						high_peak = goertzel_result_buffer[i];
						n = (i - CAPIDTMF_LOW_GROUP_FREQUENCIES) << 2;
					}
				}
				result_digit |= (byte) n;
				if (low_peak + DSPDTMF_RX_HIGH_EXCEEDING_LOW_DEFAULT < high_peak)
					result_digit = CAPIDTMF_RECV_NO_DIGIT;
				if (high_peak + DSPDTMF_RX_LOW_EXCEEDING_HIGH_DEFAULT < low_peak)
					result_digit = CAPIDTMF_RECV_NO_DIGIT;
				n = 0;
				for (i = 0; i < CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT; i++)
				{
					if ((((short)(low_peak - goertzel_result_buffer[i] - capidtmf_recv_guard_snr_low_table[i])) < 0)
					    || (((short)(high_peak - goertzel_result_buffer[i] - capidtmf_recv_guard_snr_high_table[i])) < 0))
					{
						n++;
					}
				}
				if (n != 2)
					result_digit = CAPIDTMF_RECV_NO_DIGIT;

				if (result_digit == CAPIDTMF_RECV_NO_DIGIT)
				{
					if (p_state->recv.current_digit_on_time != 0)
					{
						if (++(p_state->recv.current_digit_off_time) >= p_state->recv.min_gap_duration)
						{
							p_state->recv.current_digit_on_time = 0;
							p_state->recv.current_digit_off_time = 0;
						}
					}
					else
					{
						if (p_state->recv.current_digit_off_time != 0)
							(p_state->recv.current_digit_off_time)--;
					}
				}
				else
				{
					if ((p_state->recv.current_digit_on_time == 0)
					    && (p_state->recv.current_digit_off_time != 0))
					{
						(p_state->recv.current_digit_off_time)--;
					}
					else
					{
						n = p_state->recv.current_digit_off_time;
						if ((p_state->recv.current_digit_on_time != 0)
						    && (result_digit != p_state->recv.current_digit_value))
						{
							p_state->recv.current_digit_on_time = 0;
							n = 0;
						}
						p_state->recv.current_digit_value = result_digit;
						p_state->recv.current_digit_off_time = 0;
						if (p_state->recv.current_digit_on_time != 0xffff)
						{
							p_state->recv.current_digit_on_time += n + 1;
							if (p_state->recv.current_digit_on_time >= p_state->recv.min_digit_duration)
							{
								p_state->recv.current_digit_on_time = 0xffff;
								i = (p_state->recv.digit_write_pos == CAPIDTMF_RECV_DIGIT_BUFFER_SIZE - 1) ?
									0 : p_state->recv.digit_write_pos + 1;
								if (i == p_state->recv.digit_read_pos)
								{
									trace(dprintf("%s,%d: Receive digit overrun",
										      (char *)(FILE_), __LINE__));
								}
								else
								{
									p_state->recv.digit_buffer[p_state->recv.digit_write_pos] = result_digit;
									p_state->recv.digit_write_pos = i;
									p_state->recv.indication_state =
										(p_state->recv.indication_state & ~CAPIDTMF_RECV_INDICATION_DIGIT) |
										(~p_state->recv.indication_state_ack & CAPIDTMF_RECV_INDICATION_DIGIT);
								}
							}
						}
					}
				}
				cycle_counter = 0;
				sample_number++;
			}
		}
		p_state->recv.cycle_counter = cycle_counter;
	}
}


void capidtmf_init(t_capidtmf_state *p_state, byte ulaw)
{
	p_state->ulaw = ulaw;
	capidtmf_recv_init(p_state);
}


/*---------------------------------------------------------------------------*/
