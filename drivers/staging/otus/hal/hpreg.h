/*
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*  Module Name : hpreg.h                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains Regulatory Table definitions.              */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#ifndef _HPREG_H
#define _HPREG_H

typedef u16_t HAL_CTRY_CODE;		/* country code */
typedef u16_t HAL_REG_DOMAIN;		/* regulatory domain code */
typedef enum {
	AH_FALSE = 0,		/* NB: lots of code assumes false is zero */
	AH_TRUE  = 1,
} HAL_BOOL;


/*
 * Country/Region Codes from MS WINNLS.H
 * Numbering from ISO 3166
 */
enum CountryCode {
    CTRY_ALBANIA              = 8,       /* Albania */
    CTRY_ALGERIA              = 12,      /* Algeria */
    CTRY_ARGENTINA            = 32,      /* Argentina */
    CTRY_ARMENIA              = 51,      /* Armenia */
    CTRY_AUSTRALIA            = 36,      /* Australia */
    CTRY_AUSTRIA              = 40,      /* Austria */
    CTRY_AZERBAIJAN           = 31,      /* Azerbaijan */
    CTRY_BAHRAIN              = 48,      /* Bahrain */
    CTRY_BELARUS              = 112,     /* Belarus */
    CTRY_BELGIUM              = 56,      /* Belgium */
    CTRY_BELIZE               = 84,      /* Belize */
    CTRY_BOLIVIA              = 68,      /* Bolivia */
    CTRY_BOSNIA               = 70,      /* Bosnia */
    CTRY_BRAZIL               = 76,      /* Brazil */
    CTRY_BRUNEI_DARUSSALAM    = 96,      /* Brunei Darussalam */
    CTRY_BULGARIA             = 100,     /* Bulgaria */
    CTRY_CANADA               = 124,     /* Canada */
    CTRY_CHILE                = 152,     /* Chile */
    CTRY_CHINA                = 156,     /* People's Republic of China */
    CTRY_COLOMBIA             = 170,     /* Colombia */
    CTRY_COSTA_RICA           = 188,     /* Costa Rica */
    CTRY_CROATIA              = 191,     /* Croatia */
    CTRY_CYPRUS               = 196,     /* Cyprus */
    CTRY_CZECH                = 203,     /* Czech Republic */
    CTRY_DENMARK              = 208,     /* Denmark */
    CTRY_DOMINICAN_REPUBLIC   = 214,     /* Dominican Republic */
    CTRY_ECUADOR              = 218,     /* Ecuador */
    CTRY_EGYPT                = 818,     /* Egypt */
    CTRY_EL_SALVADOR          = 222,     /* El Salvador */
    CTRY_ESTONIA              = 233,     /* Estonia */
    CTRY_FAEROE_ISLANDS       = 234,     /* Faeroe Islands */
    CTRY_FINLAND              = 246,     /* Finland */
    CTRY_FRANCE               = 250,     /* France */
    CTRY_FRANCE2              = 255,     /* France2 */
    CTRY_GEORGIA              = 268,     /* Georgia */
    CTRY_GERMANY              = 276,     /* Germany */
    CTRY_GREECE               = 300,     /* Greece */
    CTRY_GUATEMALA            = 320,     /* Guatemala */
    CTRY_HONDURAS             = 340,     /* Honduras */
    CTRY_HONG_KONG            = 344,     /* Hong Kong S.A.R., P.R.C. */
    CTRY_HUNGARY              = 348,     /* Hungary */
    CTRY_ICELAND              = 352,     /* Iceland */
    CTRY_INDIA                = 356,     /* India */
    CTRY_INDONESIA            = 360,     /* Indonesia */
    CTRY_IRAN                 = 364,     /* Iran */
    CTRY_IRAQ                 = 368,     /* Iraq */
    CTRY_IRELAND              = 372,     /* Ireland */
    CTRY_ISRAEL               = 376,     /* Israel */
    CTRY_ISRAEL2              = 377,     /* Israel2 */
    CTRY_ITALY                = 380,     /* Italy */
    CTRY_JAMAICA              = 388,     /* Jamaica */
    CTRY_JAPAN                = 392,     /* Japan */
    CTRY_JAPAN1               = 393,     /* Japan (JP1) */
    CTRY_JAPAN2               = 394,     /* Japan (JP0) */
    CTRY_JAPAN3               = 395,     /* Japan (JP1-1) */
    CTRY_JAPAN4               = 396,     /* Japan (JE1) */
    CTRY_JAPAN5               = 397,     /* Japan (JE2) */
    CTRY_JAPAN6               = 399,     /* Japan (JP6) */

    CTRY_JAPAN7	              = 4007,    /* Japan (J7) */
    CTRY_JAPAN8	              = 4008,    /* Japan (J8) */
    CTRY_JAPAN9               = 4009,    /* Japan (J9) */

    CTRY_JAPAN10              = 4010,    /* Japan (J10) */
    CTRY_JAPAN11              = 4011,    /* Japan (J11) */
    CTRY_JAPAN12              = 4012,    /* Japan (J12) */

    CTRY_JAPAN13              = 4013,    /* Japan (J13) */
    CTRY_JAPAN14              = 4014,    /* Japan (J14) */
    CTRY_JAPAN15              = 4015,    /* Japan (J15) */

    CTRY_JAPAN16              = 4016,    /* Japan (J16) */
    CTRY_JAPAN17              = 4017,    /* Japan (J17) */
    CTRY_JAPAN18              = 4018,    /* Japan (J18) */

    CTRY_JAPAN19              = 4019,    /* Japan (J19) */
    CTRY_JAPAN20              = 4020,    /* Japan (J20) */
    CTRY_JAPAN21              = 4021,    /* Japan (J21) */

    CTRY_JAPAN22              = 4022,    /* Japan (J22) */
    CTRY_JAPAN23              = 4023,    /* Japan (J23) */
    CTRY_JAPAN24              = 4024,    /* Japan (J24) */

    CTRY_JAPAN25              = 4025,    /* Japan (J25) */
    CTRY_JAPAN26              = 4026,    /* Japan (J26) */
    CTRY_JAPAN27              = 4027,    /* Japan (J27) */

    CTRY_JAPAN28              = 4028,    /* Japan (J28) */
    CTRY_JAPAN29              = 4029,    /* Japan (J29) */
    CTRY_JAPAN30              = 4030,    /* Japan (J30) */

    CTRY_JAPAN31              = 4031,    /* Japan (J31) */
    CTRY_JAPAN32              = 4032,    /* Japan (J32) */
    CTRY_JAPAN33              = 4033,    /* Japan (J33) */

    CTRY_JAPAN34              = 4034,    /* Japan (J34) */
    CTRY_JAPAN35              = 4035,    /* Japan (J35) */
    CTRY_JAPAN36              = 4036,    /* Japan (J36) */

    CTRY_JAPAN37              = 4037,    /* Japan (J37) */
    CTRY_JAPAN38              = 4038,    /* Japan (J38) */
    CTRY_JAPAN39              = 4039,    /* Japan (J39) */

    CTRY_JAPAN40              = 4040,    /* Japan (J40) */
    CTRY_JAPAN41              = 4041,    /* Japan (J41) */
    CTRY_JAPAN42              = 4042,    /* Japan (J42) */
    CTRY_JAPAN43              = 4043,    /* Japan (J43) */
    CTRY_JAPAN44              = 4044,    /* Japan (J44) */
    CTRY_JAPAN45              = 4045,    /* Japan (J45) */
    CTRY_JAPAN46              = 4046,    /* Japan (J46) */
    CTRY_JAPAN47              = 4047,    /* Japan (J47) */
    CTRY_JAPAN48              = 4048,    /* Japan (J48) */
    CTRY_JAPAN49              = 4049,    /* Japan (J49) */

    CTRY_JAPAN50              = 4050,    /* Japan (J50) */
    CTRY_JAPAN51              = 4051,    /* Japan (J51) */
    CTRY_JAPAN52              = 4052,    /* Japan (J52) */
    CTRY_JAPAN53              = 4053,    /* Japan (J53) */
    CTRY_JAPAN54              = 4054,    /* Japan (J54) */

    CTRY_JORDAN               = 400,     /* Jordan */
    CTRY_KAZAKHSTAN           = 398,     /* Kazakhstan */
    CTRY_KENYA                = 404,     /* Kenya */
    CTRY_KOREA_NORTH          = 408,     /* North Korea */
    CTRY_KOREA_ROC            = 410,     /* South Korea */
    CTRY_KOREA_ROC2           = 411,     /* South Korea */
    CTRY_KOREA_ROC3           = 412,     /* South Korea */
    CTRY_KUWAIT               = 414,     /* Kuwait */
    CTRY_LATVIA               = 428,     /* Latvia */
    CTRY_LEBANON              = 422,     /* Lebanon */
    CTRY_LIBYA                = 434,     /* Libya */
    CTRY_LIECHTENSTEIN        = 438,     /* Liechtenstein */
    CTRY_LITHUANIA            = 440,     /* Lithuania */
    CTRY_LUXEMBOURG           = 442,     /* Luxembourg */
    CTRY_MACAU                = 446,     /* Macau */
    CTRY_MACEDONIA            = 807,     /* the Former Yugoslav Republic of Macedonia */
    CTRY_MALAYSIA             = 458,     /* Malaysia */
    CTRY_MALTA	              = 470,     /* Malta */
    CTRY_MEXICO               = 484,     /* Mexico */
    CTRY_MONACO               = 492,     /* Principality of Monaco */
    CTRY_MOROCCO              = 504,     /* Morocco */
    CTRY_NETHERLANDS          = 528,     /* Netherlands */
    CTRY_NETHERLANDS_ANT      = 530,     /* Netherlands-Antellis */
    CTRY_NEW_ZEALAND          = 554,     /* New Zealand */
    CTRY_NICARAGUA            = 558,     /* Nicaragua */
    CTRY_NORWAY               = 578,     /* Norway */
    CTRY_OMAN                 = 512,     /* Oman */
    CTRY_PAKISTAN             = 586,     /* Islamic Republic of Pakistan */
    CTRY_PANAMA               = 591,     /* Panama */
    CTRY_PARAGUAY             = 600,     /* Paraguay */
    CTRY_PERU                 = 604,     /* Peru */
    CTRY_PHILIPPINES          = 608,     /* Republic of the Philippines */
    CTRY_POLAND               = 616,     /* Poland */
    CTRY_PORTUGAL             = 620,     /* Portugal */
    CTRY_PUERTO_RICO          = 630,     /* Puerto Rico */
    CTRY_QATAR                = 634,     /* Qatar */
    CTRY_ROMANIA              = 642,     /* Romania */
    CTRY_RUSSIA               = 643,     /* Russia */
    CTRY_SAUDI_ARABIA         = 682,     /* Saudi Arabia */
    CTRY_SERBIA_MONT          = 891,     /* Serbia and Montenegro */
    CTRY_SINGAPORE            = 702,     /* Singapore */
    CTRY_SLOVAKIA             = 703,     /* Slovak Republic */
    CTRY_SLOVENIA             = 705,     /* Slovenia */
    CTRY_SOUTH_AFRICA         = 710,     /* South Africa */
    CTRY_SPAIN                = 724,     /* Spain */
    CTRY_SRILANKA             = 144,     /* Srilanka */
    CTRY_SWEDEN               = 752,     /* Sweden */
    CTRY_SWITZERLAND          = 756,     /* Switzerland */
    CTRY_SYRIA                = 760,     /* Syria */
    CTRY_TAIWAN               = 158,     /* Taiwan */
    CTRY_THAILAND             = 764,     /* Thailand */
    CTRY_TRINIDAD_Y_TOBAGO    = 780,     /* Trinidad y Tobago */
    CTRY_TUNISIA              = 788,     /* Tunisia */
    CTRY_TURKEY               = 792,     /* Turkey */
    CTRY_UAE                  = 784,     /* U.A.E. */
    CTRY_UKRAINE              = 804,     /* Ukraine */
    CTRY_UNITED_KINGDOM       = 826,     /* United Kingdom */
    CTRY_UNITED_STATES        = 840,     /* United States */
    CTRY_UNITED_STATES_FCC49  = 842,     /* United States (Public Safety)*/
    CTRY_URUGUAY              = 858,     /* Uruguay */
    CTRY_UZBEKISTAN           = 860,     /* Uzbekistan */
    CTRY_VENEZUELA            = 862,     /* Venezuela */
    CTRY_VIET_NAM             = 704,     /* Viet Nam */
    CTRY_YEMEN                = 887,     /* Yemen */
    CTRY_ZIMBABWE             = 716      /* Zimbabwe */
};

/* Enumerated Regulatory Domain Information 8 bit values indicate that
 * the regdomain is really a pair of unitary regdomains.  12 bit values
 * are the real unitary regdomains and are the only ones which have the
 * frequency bitmasks and flags set.
 */
enum EnumRd {
	/*
	 * The following regulatory domain definitions are
	 * found in the EEPROM. Each regulatory domain
	 * can operate in either a 5GHz or 2.4GHz wireless mode or
	 * both 5GHz and 2.4GHz wireless modes.
	 * In general, the value holds no special
	 * meaning and is used to decode into either specific
	 * 2.4GHz or 5GHz wireless mode for that particular
	 * regulatory domain.
	 */
	NO_ENUMRD	= 0x00,
	NULL1_WORLD	= 0x03,		/* For 11b-only countries (no 11a allowed) */
	NULL1_ETSIB	= 0x07,		/* Israel */
	NULL1_ETSIC	= 0x08,
	FCC1_FCCA	= 0x10,		/* USA */
	FCC1_WORLD	= 0x11,		/* Hong Kong */
	FCC4_FCCA	= 0x12,		/* USA - Public Safety */
	FCC5_FCCA	= 0x13,		/* USA - with no DFS (UNII-1 + UNII-3 only) */
  FCC6_FCCA       = 0x14,         /* Canada */

	FCC2_FCCA	= 0x20,		/* Canada */
	FCC2_WORLD	= 0x21,		/* Australia & HK */
	FCC2_ETSIC	= 0x22,
  FCC6_WORLD      = 0x23,         /* Australia */

	FRANCE_RES	= 0x31,		/* Legacy France for OEM */
	FCC3_FCCA	= 0x3A,		/* USA & Canada w/5470 band, 11h, DFS enabled */
	FCC3_WORLD	= 0x3B,		/* USA & Canada w/5470 band, 11h, DFS enabled */

	ETSI1_WORLD	= 0x37,
	ETSI3_ETSIA	= 0x32,		/* France (optional) */
	ETSI2_WORLD	= 0x35,		/* Hungary & others */
	ETSI3_WORLD	= 0x36,		/* France & others */
	ETSI4_WORLD	= 0x30,
	ETSI4_ETSIC	= 0x38,
	ETSI5_WORLD	= 0x39,
	ETSI6_WORLD	= 0x34,		/* Bulgaria */
	ETSI_RESERVED	= 0x33,		/* Reserved (Do not used) */

	MKK1_MKKA	= 0x40,		/* Japan (JP1) */
	MKK1_MKKB	= 0x41,		/* Japan (JP0) */
	APL4_WORLD	= 0x42,		/* Singapore */
	MKK2_MKKA	= 0x43,		/* Japan with 4.9G channels */
	APL_RESERVED	= 0x44,		/* Reserved (Do not used)  */
	APL2_WORLD	= 0x45,		/* Korea */
	APL2_APLC	= 0x46,
	APL3_WORLD	= 0x47,
	MKK1_FCCA	= 0x48,		/* Japan (JP1-1) */
	APL2_APLD	= 0x49,		/* Korea with 2.3G channels */
	MKK1_MKKA1	= 0x4A,		/* Japan (JE1) */
	MKK1_MKKA2	= 0x4B,		/* Japan (JE2) */
	MKK1_MKKC	= 0x4C,		/* Japan (MKK1_MKKA,except Ch14) */

	APL3_FCCA   = 0x50,
	APL1_WORLD	= 0x52,		/* Latin America */
	APL1_FCCA	= 0x53,
	APL1_APLA	= 0x54,
	APL1_ETSIC	= 0x55,
	APL2_ETSIC	= 0x56,		/* Venezuela */
	APL2_FCCA   = 0x57, 	/* new Latin America */
	APL5_WORLD	= 0x58,		/* Chile */
	APL6_WORLD	= 0x5B,		/* Singapore */
	APL7_FCCA   = 0x5C,     /* Taiwan 5.47 Band */
	APL8_WORLD  = 0x5D,     /* Malaysia 5GHz */
	APL9_WORLD  = 0x5E,     /* Korea 5GHz */

	/*
	 * World mode SKUs
	 */
	WOR0_WORLD	= 0x60,		/* World0 (WO0 SKU) */
	WOR1_WORLD	= 0x61,		/* World1 (WO1 SKU) */
	WOR2_WORLD	= 0x62,		/* World2 (WO2 SKU) */
	WOR3_WORLD	= 0x63,		/* World3 (WO3 SKU) */
	WOR4_WORLD	= 0x64,		/* World4 (WO4 SKU) */
	WOR5_ETSIC	= 0x65,		/* World5 (WO5 SKU) */

	WOR01_WORLD	= 0x66,		/* World0-1 (WW0-1 SKU) */
	WOR02_WORLD	= 0x67,		/* World0-2 (WW0-2 SKU) */
	EU1_WORLD	= 0x68,		/* Same as World0-2 (WW0-2 SKU), except active scan ch1-13. No ch14 */

	WOR9_WORLD	= 0x69,		/* World9 (WO9 SKU) */
	WORA_WORLD	= 0x6A,		/* WorldA (WOA SKU) */

	MKK3_MKKB	= 0x80,		/* Japan UNI-1 even + MKKB */
	MKK3_MKKA2	= 0x81,		/* Japan UNI-1 even + MKKA2 */
	MKK3_MKKC	= 0x82,		/* Japan UNI-1 even + MKKC */

	MKK4_MKKB	= 0x83,		/* Japan UNI-1 even + UNI-2 + MKKB */
	MKK4_MKKA2	= 0x84,		/* Japan UNI-1 even + UNI-2 + MKKA2 */
	MKK4_MKKC	= 0x85,		/* Japan UNI-1 even + UNI-2 + MKKC */

	MKK5_MKKB	= 0x86,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKB */
	MKK5_MKKA2	= 0x87,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKA2 */
	MKK5_MKKC	= 0x88,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKC */

	MKK6_MKKB	= 0x89,		/* Japan UNI-1 even + UNI-1 odd MKKB */
	MKK6_MKKA2	= 0x8A,		/* Japan UNI-1 even + UNI-1 odd + MKKA2 */
	MKK6_MKKC	= 0x8B,		/* Japan UNI-1 even + UNI-1 odd + MKKC */

	MKK7_MKKB	= 0x8C,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKB */
	MKK7_MKKA	= 0x8D,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA2 */
	MKK7_MKKC	= 0x8E,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKC */

	MKK8_MKKB	= 0x8F,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKB */
	MKK8_MKKA2	= 0x90,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKA2 */
	MKK8_MKKC	= 0x91,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKC */

	MKK6_MKKA1      = 0xF8,         /* Japan UNI-1 even + UNI-1 odd + MKKA1 */
	MKK6_FCCA       = 0xF9,         /* Japan UNI-1 even + UNI-1 odd + FCCA */
	MKK7_MKKA1      = 0xFA,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA1 */
	MKK7_FCCA       = 0xFB,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + FCCA */
	MKK9_FCCA       = 0xFC,         /* Japan UNI-1 even + 4.9GHz + FCCA */
	MKK9_MKKA1      = 0xFD,         /* Japan UNI-1 even + 4.9GHz + MKKA1 */
	MKK9_MKKC       = 0xFE,         /* Japan UNI-1 even + 4.9GHz + MKKC */
	MKK9_MKKA2      = 0xFF,         /* Japan UNI-1 even + 4.9GHz + MKKA2 */

	MKK10_FCCA      = 0xD0,         /* Japan UNI-1 even + UNI-2 + 4.9GHz + FCCA */
	MKK10_MKKA1     = 0xD1,         /* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKA1 */
	MKK10_MKKC      = 0xD2,         /* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKC */
	MKK10_MKKA2     = 0xD3,         /* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKA2 */

	MKK11_MKKA      = 0xD4,         /* Japan UNI-1 even + UNI-2 + Midband + 4.9GHz + MKKA */
	MKK11_FCCA      = 0xD5,         /* Japan UNI-1 even + UNI-2 + Midband + 4.9GHz + FCCA */
	MKK11_MKKA1     = 0xD6,         /* Japan UNI-1 even + UNI-2 + Midband + 4.9GHz + MKKA1 */
	MKK11_MKKC      = 0xD7,         /* Japan UNI-1 even + UNI-2 + Midband + 4.9GHz + MKKC */
	MKK11_MKKA2     = 0xD8,         /* Japan UNI-1 even + UNI-2 + Midband + 4.9GHz + MKKA2 */

	MKK12_MKKA      = 0xD9,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + Midband + 4.9GHz + MKKA */
	MKK12_FCCA      = 0xDA,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + Midband + 4.9GHz + FCCA */
	MKK12_MKKA1     = 0xDB,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + Midband + 4.9GHz + MKKA1 */
	MKK12_MKKC      = 0xDC,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + Midband + 4.9GHz + MKKC */
	MKK12_MKKA2     = 0xDD,         /* Japan UNI-1 even + UNI-1 odd + UNI-2 + Midband + 4.9GHz + MKKA2 */

	/* Following definitions are used only by s/w to map old
 	 * Japan SKUs.
	 */
	MKK3_MKKA       = 0xF0,         /* Japan UNI-1 even + MKKA */
	MKK3_MKKA1      = 0xF1,         /* Japan UNI-1 even + MKKA1 */
	MKK3_FCCA       = 0xF2,         /* Japan UNI-1 even + FCCA */
	MKK4_MKKA       = 0xF3,         /* Japan UNI-1 even + UNI-2 + MKKA */
	MKK4_MKKA1      = 0xF4,         /* Japan UNI-1 even + UNI-2 + MKKA1 */
	MKK4_FCCA       = 0xF5,         /* Japan UNI-1 even + UNI-2 + FCCA */
	MKK9_MKKA       = 0xF6,         /* Japan UNI-1 even + 4.9GHz + MKKA*/
	MKK10_MKKA      = 0xF7,         /* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKA */

	/*
	 * Regulator domains ending in a number (e.g. APL1,
	 * MK1, ETSI4, etc) apply to 5GHz channel and power
	 * information.  Regulator domains ending in a letter
	 * (e.g. APLA, FCCA, etc) apply to 2.4GHz channel and
	 * power information.
	 */
	APL1		= 0x0150,	/* LAT & Asia */
	APL2		= 0x0250,	/* LAT & Asia */
	APL3		= 0x0350,	/* Taiwan */
	APL4		= 0x0450,	/* Jordan */
	APL5		= 0x0550,	/* Chile */
	APL6		= 0x0650,	/* Singapore */
	APL7		= 0x0750,	/* Taiwan Middle */
	APL8		= 0x0850,	/* Malaysia */
	APL9		= 0x0950,	/* Korea (South) ROC 3 */

	ETSI1		= 0x0130,	/* Europe & others */
	ETSI2		= 0x0230,	/* Europe & others */
	ETSI3		= 0x0330,	/* Europe & others */
	ETSI4		= 0x0430,	/* Europe & others */
	ETSI5		= 0x0530,	/* Europe & others */
	ETSI6		= 0x0630,	/* Europe & others */
	ETSIA		= 0x0A30,	/* France */
	ETSIB		= 0x0B30,	/* Israel */
	ETSIC		= 0x0C30,	/* Latin America */

	FCC1		= 0x0110,	/* US & others */
	FCC2		= 0x0120,	/* Canada, Australia & New Zealand */
	FCC3		= 0x0160,	/* US w/new middle band & DFS */
	FCC4		= 0x0165,	/* US Public Safety */
	FCC5		= 0x0510,	/* US no DFS */
    FCC6    = 0x0610, /* Canada & Australia */

	FCCA		= 0x0A10,

	APLD		= 0x0D50,	/* South Korea */

	MKK1		= 0x0140,	/* Japan (UNI-1 odd)*/
	MKK2		= 0x0240,	/* Japan (4.9 GHz + UNI-1 odd) */
	MKK3		= 0x0340,	/* Japan (UNI-1 even) */
	MKK4		= 0x0440,	/* Japan (UNI-1 even + UNI-2) */
	MKK5		= 0x0540,	/* Japan (UNI-1 even + UNI-2 + mid-band) */
	MKK6		= 0x0640,	/* Japan (UNI-1 odd + UNI-1 even) */
	MKK7		= 0x0740,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 */
	MKK8		= 0x0840,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 + mid-band) */
	MKK9		= 0x0940,   /* Japan (UNI-1 even + 4.9 GHZ) */
	MKK10		= 0x0B40,   /* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	MKK11		= 0x1140,   /* Japan (UNI-1 even + UNI-2 + mid-band + 4.9 GHZ) */
	MKK12		= 0x1240,   /* Japan (UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9 GHZ) */
	MKKA		= 0x0A40,	/* Japan */
	MKKC		= 0x0A50,

	NULL1		= 0x0198,
	WORLD		= 0x0199,
	DEBUG_REG_DMN	= 0x01ff,
};

/* channelFlags */
#define	ZM_REG_FLAG_CHANNEL_CW_INT	0x0002	/* CW interference detected on channel */
#define	ZM_REG_FLAG_CHANNEL_TURBO	0x0010	/* Turbo Channel */
#define	ZM_REG_FLAG_CHANNEL_CCK	    0x0020	/* CCK channel */
#define	ZM_REG_FLAG_CHANNEL_OFDM	0x0040	/* OFDM channel */
#define	ZM_REG_FLAG_CHANNEL_2GHZ	0x0080	/* 2 GHz spectrum channel. */
#define	ZM_REG_FLAG_CHANNEL_5GHZ	0x0100	/* 5 GHz spectrum channel */
#define	ZM_REG_FLAG_CHANNEL_PASSIVE	0x0200	/* Only passive scan allowed in the channel */
#define	ZM_REG_FLAG_CHANNEL_DYN	    0x0400	/* dynamic CCK-OFDM channel */
#define	ZM_REG_FLAG_CHANNEL_XR	    0x0800	/* XR channel */
#define	ZM_REG_FLAG_CHANNEL_CSA 	0x1000	/* Channel by CSA(Channel Switch Announcement) */
#define	ZM_REG_FLAG_CHANNEL_STURBO	0x2000	/* Static turbo, no 11a-only usage */
#define ZM_REG_FLAG_CHANNEL_HALF    0x4000 	/* Half rate channel */
#define ZM_REG_FLAG_CHANNEL_QUARTER 0x8000 	/* Quarter rate channel */

/* channelFlags */
#define CHANNEL_CW_INT  0x0002  /* CW interference detected on channel */
#define CHANNEL_TURBO   0x0010  /* Turbo Channel */
#define CHANNEL_CCK 0x0020  /* CCK channel */
#define CHANNEL_OFDM    0x0040  /* OFDM channel */
#define CHANNEL_2GHZ    0x0080  /* 2 GHz spectrum channel. */
#define CHANNEL_5GHZ    0x0100  /* 5 GHz spectrum channel */
#define CHANNEL_PASSIVE 0x0200  /* Only passive scan allowed in the channel */
#define CHANNEL_DYN 0x0400  /* dynamic CCK-OFDM channel */
#define CHANNEL_XR  0x0800  /* XR channel */
#define CHANNEL_STURBO  0x2000  /* Static turbo, no 11a-only usage */
#define CHANNEL_HALF    0x4000  /* Half rate channel */
#define CHANNEL_QUARTER 0x8000  /* Quarter rate channel */
#define CHANNEL_HT20    0x10000 /* HT20 channel */
#define CHANNEL_HT40    0x20000 /* HT40 channel */
#define CHANNEL_HT40U 	0x40000 /* control channel can be upper channel */
#define CHANNEL_HT40L 	0x80000 /* control channel can be lower channel */

/* privFlags */
#define ZM_REG_FLAG_CHANNEL_INTERFERENCE   	0x01 /* Software use: channel interference
				        used for as AR as well as RADAR
				        interference detection */
#define ZM_REG_FLAG_CHANNEL_DFS		0x02 /* DFS required on channel */
#define ZM_REG_FLAG_CHANNEL_4MS_LIMIT	0x04 /* 4msec packet limit on this channel */
#define ZM_REG_FLAG_CHANNEL_DFS_CLEAR       0x08 /* if channel has been checked for DFS */

#define CHANNEL_A   (CHANNEL_5GHZ|CHANNEL_OFDM)
#define CHANNEL_B   (CHANNEL_2GHZ|CHANNEL_CCK)
#define CHANNEL_PUREG   (CHANNEL_2GHZ|CHANNEL_OFDM)
#ifdef notdef
#define CHANNEL_G   (CHANNEL_2GHZ|CHANNEL_DYN)
#else
#define CHANNEL_G   (CHANNEL_2GHZ|CHANNEL_OFDM)
#endif
#define CHANNEL_T   (CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define CHANNEL_ST  (CHANNEL_T|CHANNEL_STURBO)
#define CHANNEL_108G    (CHANNEL_2GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define CHANNEL_108A    CHANNEL_T
#define CHANNEL_X   (CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_XR)
#define CHANNEL_G_HT      (CHANNEL_2GHZ | CHANNEL_OFDM | CHANNEL_HT20)
#define CHANNEL_A_HT      (CHANNEL_5GHZ | CHANNEL_OFDM | CHANNEL_HT20)

#define CHANNEL_G_HT20  (CHANNEL_2GHZ|CHANNEL_HT20)
#define CHANNEL_A_HT20  (CHANNEL_5GHZ|CHANNEL_HT20)
#define CHANNEL_G_HT40  (CHANNEL_2GHZ|CHANNEL_HT20|CHANNEL_HT40)
#define CHANNEL_A_HT40  (CHANNEL_5GHZ|CHANNEL_HT20|CHANNEL_HT40)
#define CHANNEL_ALL \
    (CHANNEL_OFDM|CHANNEL_CCK| CHANNEL_2GHZ | CHANNEL_5GHZ | CHANNEL_TURBO | CHANNEL_HT20 | CHANNEL_HT40)
#define CHANNEL_ALL_NOTURBO     (CHANNEL_ALL &~ CHANNEL_TURBO)

enum {
    HAL_MODE_11A    = 0x001,        /* 11a channels */
    HAL_MODE_TURBO  = 0x002,        /* 11a turbo-only channels */
    HAL_MODE_11B    = 0x004,        /* 11b channels */
    HAL_MODE_PUREG  = 0x008,        /* 11g channels (OFDM only) */
#ifdef notdef
    HAL_MODE_11G    = 0x010,        /* 11g channels (OFDM/CCK) */
#else
    HAL_MODE_11G    = 0x008,        /* XXX historical */
#endif
    HAL_MODE_108G   = 0x020,        /* 11a+Turbo channels */
    HAL_MODE_108A   = 0x040,        /* 11g+Turbo channels */
    HAL_MODE_XR     = 0x100,        /* XR channels */
    HAL_MODE_11A_HALF_RATE = 0x200,     /* 11A half rate channels */
    HAL_MODE_11A_QUARTER_RATE = 0x400,  /* 11A quarter rate channels */
    HAL_MODE_11NG   = 0x4000,           /* 11ng channels */
    HAL_MODE_11NA   = 0x8000,           /* 11na channels */
    HAL_MODE_ALL    = 0xffff
};

#endif /* #ifndef _HPREG_H */
