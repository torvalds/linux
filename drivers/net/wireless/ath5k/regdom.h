/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#ifndef _IEEE80211_REGDOMAIN_H_
#define _IEEE80211_REGDOMAIN_H_

#include <linux/types.h>

/* Default regulation domain if stored value EEPROM value is invalid */
#define AR5K_TUNE_REGDOMAIN	DMN_FCC2_FCCA	/* Canada */
#define AR5K_TUNE_CTRY		CTRY_DEFAULT


enum ath5k_regdom {
	DMN_DEFAULT		= 0x00,
	DMN_NULL_WORLD		= 0x03,
	DMN_NULL_ETSIB		= 0x07,
	DMN_NULL_ETSIC		= 0x08,
	DMN_FCC1_FCCA		= 0x10,
	DMN_FCC1_WORLD		= 0x11,
	DMN_FCC2_FCCA		= 0x20,
	DMN_FCC2_WORLD		= 0x21,
	DMN_FCC2_ETSIC		= 0x22,
	DMN_FRANCE_NULL		= 0x31,
	DMN_FCC3_FCCA		= 0x3A,
	DMN_ETSI1_WORLD		= 0x37,
	DMN_ETSI3_ETSIA		= 0x32,
	DMN_ETSI2_WORLD		= 0x35,
	DMN_ETSI3_WORLD		= 0x36,
	DMN_ETSI4_WORLD		= 0x30,
	DMN_ETSI4_ETSIC		= 0x38,
	DMN_ETSI5_WORLD		= 0x39,
	DMN_ETSI6_WORLD		= 0x34,
	DMN_ETSI_NULL		= 0x33,
	DMN_MKK1_MKKA		= 0x40,
	DMN_MKK1_MKKB		= 0x41,
	DMN_APL4_WORLD		= 0x42,
	DMN_MKK2_MKKA		= 0x43,
	DMN_APL_NULL		= 0x44,
	DMN_APL2_WORLD		= 0x45,
	DMN_APL2_APLC		= 0x46,
	DMN_APL3_WORLD		= 0x47,
	DMN_MKK1_FCCA		= 0x48,
	DMN_APL2_APLD		= 0x49,
	DMN_MKK1_MKKA1		= 0x4A,
	DMN_MKK1_MKKA2		= 0x4B,
	DMN_APL1_WORLD		= 0x52,
	DMN_APL1_FCCA		= 0x53,
	DMN_APL1_APLA		= 0x54,
	DMN_APL1_ETSIC		= 0x55,
	DMN_APL2_ETSIC		= 0x56,
	DMN_APL5_WORLD		= 0x58,
	DMN_WOR0_WORLD		= 0x60,
	DMN_WOR1_WORLD		= 0x61,
	DMN_WOR2_WORLD		= 0x62,
	DMN_WOR3_WORLD		= 0x63,
	DMN_WOR4_WORLD		= 0x64,
	DMN_WOR5_ETSIC		= 0x65,
	DMN_WOR01_WORLD		= 0x66,
	DMN_WOR02_WORLD		= 0x67,
	DMN_EU1_WORLD		= 0x68,
	DMN_WOR9_WORLD		= 0x69,
	DMN_WORA_WORLD		= 0x6A,

	DMN_APL1		= 0xf0000001,
	DMN_APL2		= 0xf0000002,
	DMN_APL3		= 0xf0000004,
	DMN_APL4		= 0xf0000008,
	DMN_APL5		= 0xf0000010,
	DMN_ETSI1		= 0xf0000020,
	DMN_ETSI2		= 0xf0000040,
	DMN_ETSI3		= 0xf0000080,
	DMN_ETSI4		= 0xf0000100,
	DMN_ETSI5		= 0xf0000200,
	DMN_ETSI6		= 0xf0000400,
	DMN_ETSIA		= 0xf0000800,
	DMN_ETSIB		= 0xf0001000,
	DMN_ETSIC		= 0xf0002000,
	DMN_FCC1		= 0xf0004000,
	DMN_FCC2		= 0xf0008000,
	DMN_FCC3		= 0xf0010000,
	DMN_FCCA		= 0xf0020000,
	DMN_APLD		= 0xf0040000,
	DMN_MKK1		= 0xf0080000,
	DMN_MKK2		= 0xf0100000,
	DMN_MKKA		= 0xf0200000,
	DMN_NULL		= 0xf0400000,
	DMN_WORLD		= 0xf0800000,
	DMN_DEBUG               = 0xf1000000	/* used for debugging */
};

#define IEEE80211_DMN(_d)	((_d) & ~0xf0000000)

enum ath5k_countrycode {
	CTRY_DEFAULT            = 0,   /* Default domain (NA) */
	CTRY_ALBANIA            = 8,   /* Albania */
	CTRY_ALGERIA            = 12,  /* Algeria */
	CTRY_ARGENTINA          = 32,  /* Argentina */
	CTRY_ARMENIA            = 51,  /* Armenia */
	CTRY_AUSTRALIA          = 36,  /* Australia */
	CTRY_AUSTRIA            = 40,  /* Austria */
	CTRY_AZERBAIJAN         = 31,  /* Azerbaijan */
	CTRY_BAHRAIN            = 48,  /* Bahrain */
	CTRY_BELARUS            = 112, /* Belarus */
	CTRY_BELGIUM            = 56,  /* Belgium */
	CTRY_BELIZE             = 84,  /* Belize */
	CTRY_BOLIVIA            = 68,  /* Bolivia */
	CTRY_BRAZIL             = 76,  /* Brazil */
	CTRY_BRUNEI_DARUSSALAM  = 96,  /* Brunei Darussalam */
	CTRY_BULGARIA           = 100, /* Bulgaria */
	CTRY_CANADA             = 124, /* Canada */
	CTRY_CHILE              = 152, /* Chile */
	CTRY_CHINA              = 156, /* People's Republic of China */
	CTRY_COLOMBIA           = 170, /* Colombia */
	CTRY_COSTA_RICA         = 188, /* Costa Rica */
	CTRY_CROATIA            = 191, /* Croatia */
	CTRY_CYPRUS             = 196, /* Cyprus */
	CTRY_CZECH              = 203, /* Czech Republic */
	CTRY_DENMARK            = 208, /* Denmark */
	CTRY_DOMINICAN_REPUBLIC = 214, /* Dominican Republic */
	CTRY_ECUADOR            = 218, /* Ecuador */
	CTRY_EGYPT              = 818, /* Egypt */
	CTRY_EL_SALVADOR        = 222, /* El Salvador */
	CTRY_ESTONIA            = 233, /* Estonia */
	CTRY_FAEROE_ISLANDS     = 234, /* Faeroe Islands */
	CTRY_FINLAND            = 246, /* Finland */
	CTRY_FRANCE             = 250, /* France */
	CTRY_FRANCE2            = 255, /* France2 */
	CTRY_GEORGIA            = 268, /* Georgia */
	CTRY_GERMANY            = 276, /* Germany */
	CTRY_GREECE             = 300, /* Greece */
	CTRY_GUATEMALA          = 320, /* Guatemala */
	CTRY_HONDURAS           = 340, /* Honduras */
	CTRY_HONG_KONG          = 344, /* Hong Kong S.A.R., P.R.C. */
	CTRY_HUNGARY            = 348, /* Hungary */
	CTRY_ICELAND            = 352, /* Iceland */
	CTRY_INDIA              = 356, /* India */
	CTRY_INDONESIA          = 360, /* Indonesia */
	CTRY_IRAN               = 364, /* Iran */
	CTRY_IRAQ               = 368, /* Iraq */
	CTRY_IRELAND            = 372, /* Ireland */
	CTRY_ISRAEL             = 376, /* Israel */
	CTRY_ITALY              = 380, /* Italy */
	CTRY_JAMAICA            = 388, /* Jamaica */
	CTRY_JAPAN              = 392, /* Japan */
	CTRY_JAPAN1             = 393, /* Japan (JP1) */
	CTRY_JAPAN2             = 394, /* Japan (JP0) */
	CTRY_JAPAN3             = 395, /* Japan (JP1-1) */
	CTRY_JAPAN4             = 396, /* Japan (JE1) */
	CTRY_JAPAN5             = 397, /* Japan (JE2) */
	CTRY_JORDAN             = 400, /* Jordan */
	CTRY_KAZAKHSTAN         = 398, /* Kazakhstan */
	CTRY_KENYA              = 404, /* Kenya */
	CTRY_KOREA_NORTH        = 408, /* North Korea */
	CTRY_KOREA_ROC          = 410, /* South Korea */
	CTRY_KOREA_ROC2         = 411, /* South Korea */
	CTRY_KUWAIT             = 414, /* Kuwait */
	CTRY_LATVIA             = 428, /* Latvia */
	CTRY_LEBANON            = 422, /* Lebanon */
	CTRY_LIBYA              = 434, /* Libya */
	CTRY_LIECHTENSTEIN      = 438, /* Liechtenstein */
	CTRY_LITHUANIA          = 440, /* Lithuania */
	CTRY_LUXEMBOURG         = 442, /* Luxembourg */
	CTRY_MACAU              = 446, /* Macau */
	CTRY_MACEDONIA          = 807, /* Republic of Macedonia */
	CTRY_MALAYSIA           = 458, /* Malaysia */
	CTRY_MEXICO             = 484, /* Mexico */
	CTRY_MONACO             = 492, /* Principality of Monaco */
	CTRY_MOROCCO            = 504, /* Morocco */
	CTRY_NETHERLANDS        = 528, /* Netherlands */
	CTRY_NEW_ZEALAND        = 554, /* New Zealand */
	CTRY_NICARAGUA          = 558, /* Nicaragua */
	CTRY_NORWAY             = 578, /* Norway */
	CTRY_OMAN               = 512, /* Oman */
	CTRY_PAKISTAN           = 586, /* Islamic Republic of Pakistan */
	CTRY_PANAMA             = 591, /* Panama */
	CTRY_PARAGUAY           = 600, /* Paraguay */
	CTRY_PERU               = 604, /* Peru */
	CTRY_PHILIPPINES        = 608, /* Republic of the Philippines */
	CTRY_POLAND             = 616, /* Poland */
	CTRY_PORTUGAL           = 620, /* Portugal */
	CTRY_PUERTO_RICO        = 630, /* Puerto Rico */
	CTRY_QATAR              = 634, /* Qatar */
	CTRY_ROMANIA            = 642, /* Romania */
	CTRY_RUSSIA             = 643, /* Russia */
	CTRY_SAUDI_ARABIA       = 682, /* Saudi Arabia */
	CTRY_SINGAPORE          = 702, /* Singapore */
	CTRY_SLOVAKIA           = 703, /* Slovak Republic */
	CTRY_SLOVENIA           = 705, /* Slovenia */
	CTRY_SOUTH_AFRICA       = 710, /* South Africa */
	CTRY_SPAIN              = 724, /* Spain */
	CTRY_SRI_LANKA          = 728, /* Sri Lanka */
	CTRY_SWEDEN             = 752, /* Sweden */
	CTRY_SWITZERLAND        = 756, /* Switzerland */
	CTRY_SYRIA              = 760, /* Syria */
	CTRY_TAIWAN             = 158, /* Taiwan */
	CTRY_THAILAND           = 764, /* Thailand */
	CTRY_TRINIDAD_Y_TOBAGO  = 780, /* Trinidad y Tobago */
	CTRY_TUNISIA            = 788, /* Tunisia */
	CTRY_TURKEY             = 792, /* Turkey */
	CTRY_UAE                = 784, /* U.A.E. */
	CTRY_UKRAINE            = 804, /* Ukraine */
	CTRY_UNITED_KINGDOM     = 826, /* United Kingdom */
	CTRY_UNITED_STATES      = 840, /* United States */
	CTRY_URUGUAY            = 858, /* Uruguay */
	CTRY_UZBEKISTAN         = 860, /* Uzbekistan */
	CTRY_VENEZUELA          = 862, /* Venezuela */
	CTRY_VIET_NAM           = 704, /* Viet Nam */
	CTRY_YEMEN              = 887, /* Yemen */
	CTRY_ZIMBABWE           = 716, /* Zimbabwe */
};

#define IEEE80211_CHANNELS_2GHZ_MIN	2412	/* 2GHz channel 1 */
#define IEEE80211_CHANNELS_2GHZ_MAX	2732	/* 2GHz channel 26 */
#define IEEE80211_CHANNELS_5GHZ_MIN	5005	/* 5GHz channel 1 */
#define IEEE80211_CHANNELS_5GHZ_MAX	6100	/* 5GHz channel 220 */

struct ath5k_regchannel {
	u16 chan;
	enum ath5k_regdom domain;
	u32 mode;
};

#define IEEE80211_CHANNELS_2GHZ {					\
/*2412*/ {   1, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2417*/ {   2, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2422*/ {   3, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2427*/ {   4, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2432*/ {   5, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2442*/ {   7, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2447*/ {   8, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2452*/ {   9, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2457*/ {  10, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2462*/ {  11, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2467*/ {  12, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2472*/ {  13, DMN_APLD, CHANNEL_CCK|CHANNEL_OFDM },			\
									\
/*2432*/ {   5, DMN_ETSIB, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_ETSIB, CHANNEL_CCK|CHANNEL_OFDM|CHANNEL_TURBO },	\
/*2442*/ {   7, DMN_ETSIB, CHANNEL_CCK|CHANNEL_OFDM },			\
									\
/*2412*/ {   1, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2417*/ {   2, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2422*/ {   3, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2427*/ {   4, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2432*/ {   5, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM|CHANNEL_TURBO },	\
/*2442*/ {   7, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2447*/ {   8, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2452*/ {   9, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2457*/ {  10, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2462*/ {  11, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2467*/ {  12, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2472*/ {  13, DMN_ETSIC, CHANNEL_CCK|CHANNEL_OFDM },			\
									\
/*2412*/ {   1, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2417*/ {   2, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2422*/ {   3, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2427*/ {   4, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2432*/ {   5, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM|CHANNEL_TURBO },	\
/*2442*/ {   7, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2447*/ {   8, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2452*/ {   9, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2457*/ {  10, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2462*/ {  11, DMN_FCCA, CHANNEL_CCK|CHANNEL_OFDM },			\
									\
/*2412*/ {   1, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2417*/ {   2, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2422*/ {   3, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2427*/ {   4, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2432*/ {   5, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2442*/ {   7, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2447*/ {   8, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2452*/ {   9, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2457*/ {  10, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2462*/ {  11, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2467*/ {  12, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2472*/ {  13, DMN_MKKA, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2484*/ {  14, DMN_MKKA, CHANNEL_CCK },				\
									\
/*2412*/ {   1, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2417*/ {   2, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2422*/ {   3, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2427*/ {   4, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2432*/ {   5, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2437*/ {   6, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM|CHANNEL_TURBO },	\
/*2442*/ {   7, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2447*/ {   8, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2452*/ {   9, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2457*/ {  10, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2462*/ {  11, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2467*/ {  12, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
/*2472*/ {  13, DMN_WORLD, CHANNEL_CCK|CHANNEL_OFDM },			\
}

#define IEEE80211_CHANNELS_5GHZ {			\
/*5745*/ { 149, DMN_APL1, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_APL1, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_APL1, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_APL1, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_APL1, CHANNEL_OFDM },		\
							\
/*5745*/ { 149, DMN_APL2, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_APL2, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_APL2, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_APL2, CHANNEL_OFDM },		\
							\
/*5280*/ {  56, DMN_APL3, CHANNEL_OFDM },		\
/*5300*/ {  60, DMN_APL3, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_APL3, CHANNEL_OFDM },		\
/*5745*/ { 149, DMN_APL3, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_APL3, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_APL3, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_APL3, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_APL4, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_APL4, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_APL4, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_APL4, CHANNEL_OFDM },		\
/*5745*/ { 149, DMN_APL4, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_APL4, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_APL4, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_APL4, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_APL4, CHANNEL_OFDM },		\
							\
/*5745*/ { 149, DMN_APL5, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_APL5, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_APL5, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_APL5, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_APL5, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI1, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI1, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI1, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI1, CHANNEL_OFDM },		\
/*5260*/ {  52, DMN_ETSI1, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_ETSI1, CHANNEL_OFDM },		\
/*5300*/ {  60, DMN_ETSI1, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_ETSI1, CHANNEL_OFDM },		\
/*5500*/ { 100, DMN_ETSI1, CHANNEL_OFDM },		\
/*5520*/ { 104, DMN_ETSI1, CHANNEL_OFDM },		\
/*5540*/ { 108, DMN_ETSI1, CHANNEL_OFDM },		\
/*5560*/ { 112, DMN_ETSI1, CHANNEL_OFDM },		\
/*5580*/ { 116, DMN_ETSI1, CHANNEL_OFDM },		\
/*5600*/ { 120, DMN_ETSI1, CHANNEL_OFDM },		\
/*5620*/ { 124, DMN_ETSI1, CHANNEL_OFDM },		\
/*5640*/ { 128, DMN_ETSI1, CHANNEL_OFDM },		\
/*5660*/ { 132, DMN_ETSI1, CHANNEL_OFDM },		\
/*5680*/ { 136, DMN_ETSI1, CHANNEL_OFDM },		\
/*5700*/ { 140, DMN_ETSI1, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI2, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI2, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI2, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI2, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI3, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI3, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI3, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI3, CHANNEL_OFDM },		\
/*5260*/ {  52, DMN_ETSI3, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_ETSI3, CHANNEL_OFDM },		\
/*5300*/ {  60, DMN_ETSI3, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_ETSI3, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI4, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI4, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI4, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI4, CHANNEL_OFDM },		\
/*5260*/ {  52, DMN_ETSI4, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_ETSI4, CHANNEL_OFDM },		\
/*5300*/ {  60, DMN_ETSI4, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_ETSI4, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI5, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI5, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI5, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI5, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_ETSI6, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_ETSI6, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_ETSI6, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_ETSI6, CHANNEL_OFDM },		\
/*5260*/ {  52, DMN_ETSI6, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_ETSI6, CHANNEL_OFDM },		\
/*5500*/ { 100, DMN_ETSI6, CHANNEL_OFDM },		\
/*5520*/ { 104, DMN_ETSI6, CHANNEL_OFDM },		\
/*5540*/ { 108, DMN_ETSI6, CHANNEL_OFDM },		\
/*5560*/ { 112, DMN_ETSI6, CHANNEL_OFDM },		\
/*5580*/ { 116, DMN_ETSI6, CHANNEL_OFDM },		\
/*5600*/ { 120, DMN_ETSI6, CHANNEL_OFDM },		\
/*5620*/ { 124, DMN_ETSI6, CHANNEL_OFDM },		\
/*5640*/ { 128, DMN_ETSI6, CHANNEL_OFDM },		\
/*5660*/ { 132, DMN_ETSI6, CHANNEL_OFDM },		\
/*5680*/ { 136, DMN_ETSI6, CHANNEL_OFDM },		\
/*5700*/ { 140, DMN_ETSI6, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_FCC1, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_FCC1, CHANNEL_OFDM },		\
/*5210*/ {  42, DMN_FCC1, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5220*/ {  44, DMN_FCC1, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_FCC1, CHANNEL_OFDM },		\
/*5250*/ {  50, DMN_FCC1, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5260*/ {  52, DMN_FCC1, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_FCC1, CHANNEL_OFDM },		\
/*5290*/ {  58, DMN_FCC1, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5300*/ {  60, DMN_FCC1, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_FCC1, CHANNEL_OFDM },		\
/*5745*/ { 149, DMN_FCC1, CHANNEL_OFDM },		\
/*5760*/ { 152, DMN_FCC1, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5765*/ { 153, DMN_FCC1, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_FCC1, CHANNEL_OFDM },		\
/*5800*/ { 160, DMN_FCC1, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5805*/ { 161, DMN_FCC1, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_FCC1, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_FCC2, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_FCC2, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_FCC2, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_FCC2, CHANNEL_OFDM },		\
/*5260*/ {  52, DMN_FCC2, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_FCC2, CHANNEL_OFDM },		\
/*5300*/ {  60, DMN_FCC2, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_FCC2, CHANNEL_OFDM },		\
/*5745*/ { 149, DMN_FCC2, CHANNEL_OFDM },		\
/*5765*/ { 153, DMN_FCC2, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_FCC2, CHANNEL_OFDM },		\
/*5805*/ { 161, DMN_FCC2, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_FCC2, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_FCC3, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_FCC3, CHANNEL_OFDM },		\
/*5210*/ {  42, DMN_FCC3, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5220*/ {  44, DMN_FCC3, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_FCC3, CHANNEL_OFDM },		\
/*5250*/ {  50, DMN_FCC3, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5260*/ {  52, DMN_FCC3, CHANNEL_OFDM },		\
/*5280*/ {  56, DMN_FCC3, CHANNEL_OFDM },		\
/*5290*/ {  58, DMN_FCC3, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5300*/ {  60, DMN_FCC3, CHANNEL_OFDM },		\
/*5320*/ {  64, DMN_FCC3, CHANNEL_OFDM },		\
/*5500*/ { 100, DMN_FCC3, CHANNEL_OFDM },		\
/*5520*/ { 104, DMN_FCC3, CHANNEL_OFDM },		\
/*5540*/ { 108, DMN_FCC3, CHANNEL_OFDM },		\
/*5560*/ { 112, DMN_FCC3, CHANNEL_OFDM },		\
/*5580*/ { 116, DMN_FCC3, CHANNEL_OFDM },		\
/*5600*/ { 120, DMN_FCC3, CHANNEL_OFDM },		\
/*5620*/ { 124, DMN_FCC3, CHANNEL_OFDM },		\
/*5640*/ { 128, DMN_FCC3, CHANNEL_OFDM },		\
/*5660*/ { 132, DMN_FCC3, CHANNEL_OFDM },		\
/*5680*/ { 136, DMN_FCC3, CHANNEL_OFDM },		\
/*5700*/ { 140, DMN_FCC3, CHANNEL_OFDM },		\
/*5745*/ { 149, DMN_FCC3, CHANNEL_OFDM },		\
/*5760*/ { 152, DMN_FCC3, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5765*/ { 153, DMN_FCC3, CHANNEL_OFDM },		\
/*5785*/ { 157, DMN_FCC3, CHANNEL_OFDM },		\
/*5800*/ { 160, DMN_FCC3, CHANNEL_OFDM|CHANNEL_TURBO },	\
/*5805*/ { 161, DMN_FCC3, CHANNEL_OFDM },		\
/*5825*/ { 165, DMN_FCC3, CHANNEL_OFDM },		\
							\
/*5170*/ {  34, DMN_MKK1, CHANNEL_OFDM },		\
/*5190*/ {  38, DMN_MKK1, CHANNEL_OFDM },		\
/*5210*/ {  42, DMN_MKK1, CHANNEL_OFDM },		\
/*5230*/ {  46, DMN_MKK1, CHANNEL_OFDM },		\
							\
/*5040*/ {   8, DMN_MKK2, CHANNEL_OFDM },		\
/*5060*/ {  12, DMN_MKK2, CHANNEL_OFDM },		\
/*5080*/ {  16, DMN_MKK2, CHANNEL_OFDM },		\
/*5170*/ {  34, DMN_MKK2, CHANNEL_OFDM },		\
/*5190*/ {  38, DMN_MKK2, CHANNEL_OFDM },		\
/*5210*/ {  42, DMN_MKK2, CHANNEL_OFDM },		\
/*5230*/ {  46, DMN_MKK2, CHANNEL_OFDM },		\
							\
/*5180*/ {  36, DMN_WORLD, CHANNEL_OFDM },		\
/*5200*/ {  40, DMN_WORLD, CHANNEL_OFDM },		\
/*5220*/ {  44, DMN_WORLD, CHANNEL_OFDM },		\
/*5240*/ {  48, DMN_WORLD, CHANNEL_OFDM },		\
}

enum ath5k_regdom ath5k_regdom2flag(enum ath5k_regdom, u16);
u16 ath5k_regdom_from_ieee(enum ath5k_regdom ieee);
enum ath5k_regdom ath5k_regdom_to_ieee(u16 regdomain);

#endif
