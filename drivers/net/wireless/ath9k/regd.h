/*
 * Copyright (c) 2008 Atheros Communications Inc.
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

#ifndef REGD_H
#define REGD_H

#include "ath9k.h"

#define BMLEN 2
#define BMZERO {(u64) 0, (u64) 0}

#define BM(_fa, _fb, _fc, _fd, _fe, _ff, _fg, _fh, _fi, _fj, _fk, _fl) \
	{((((_fa >= 0) && (_fa < 64)) ? \
		(((u64) 1) << _fa) : (u64) 0) | \
	(((_fb >= 0) && (_fb < 64)) ? \
		(((u64) 1) << _fb) : (u64) 0) | \
	(((_fc >= 0) && (_fc < 64)) ? \
		(((u64) 1) << _fc) : (u64) 0) | \
	(((_fd >= 0) && (_fd < 64)) ? \
		(((u64) 1) << _fd) : (u64) 0) | \
	(((_fe >= 0) && (_fe < 64)) ? \
		(((u64) 1) << _fe) : (u64) 0) | \
	(((_ff >= 0) && (_ff < 64)) ? \
		(((u64) 1) << _ff) : (u64) 0) | \
	(((_fg >= 0) && (_fg < 64)) ? \
		(((u64) 1) << _fg) : (u64) 0) | \
	(((_fh >= 0) && (_fh < 64)) ? \
		(((u64) 1) << _fh) : (u64) 0) | \
	(((_fi >= 0) && (_fi < 64)) ? \
		(((u64) 1) << _fi) : (u64) 0) | \
	(((_fj >= 0) && (_fj < 64)) ? \
		(((u64) 1) << _fj) : (u64) 0) | \
	(((_fk >= 0) && (_fk < 64)) ? \
		(((u64) 1) << _fk) : (u64) 0) | \
	(((_fl >= 0) && (_fl < 64)) ? \
		(((u64) 1) << _fl) : (u64) 0) | \
			((((_fa > 63) && (_fa < 128)) ? \
			(((u64) 1) << (_fa - 64)) : (u64) 0) | \
	(((_fb > 63) && (_fb < 128)) ? \
		(((u64) 1) << (_fb - 64)) : (u64) 0) | \
	(((_fc > 63) && (_fc < 128)) ? \
		(((u64) 1) << (_fc - 64)) : (u64) 0) | \
	(((_fd > 63) && (_fd < 128)) ? \
		(((u64) 1) << (_fd - 64)) : (u64) 0) | \
	(((_fe > 63) && (_fe < 128)) ? \
		(((u64) 1) << (_fe - 64)) : (u64) 0) | \
	(((_ff > 63) && (_ff < 128)) ? \
		(((u64) 1) << (_ff - 64)) : (u64) 0) | \
	(((_fg > 63) && (_fg < 128)) ? \
		(((u64) 1) << (_fg - 64)) : (u64) 0) | \
	(((_fh > 63) && (_fh < 128)) ? \
		(((u64) 1) << (_fh - 64)) : (u64) 0) | \
	(((_fi > 63) && (_fi < 128)) ? \
		(((u64) 1) << (_fi - 64)) : (u64) 0) | \
	(((_fj > 63) && (_fj < 128)) ? \
		(((u64) 1) << (_fj - 64)) : (u64) 0) | \
	(((_fk > 63) && (_fk < 128)) ? \
		(((u64) 1) << (_fk - 64)) : (u64) 0) | \
	(((_fl > 63) && (_fl < 128)) ? \
		(((u64) 1) << (_fl - 64)) : (u64) 0)))}

#define DEF_REGDMN      FCC1_FCCA
#define DEF_DMN_5       FCC1
#define DEF_DMN_2       FCCA
#define COUNTRY_ERD_FLAG        0x8000
#define WORLDWIDE_ROAMING_FLAG  0x4000
#define SUPER_DOMAIN_MASK   0x0fff
#define COUNTRY_CODE_MASK   0x3fff
#define CF_INTERFERENCE     (CHANNEL_CW_INT | CHANNEL_RADAR_INT)
#define CHANNEL_14      (2484)
#define IS_11G_CH14(_ch,_cf) \
    (((_ch) == CHANNEL_14) && ((_cf) == CHANNEL_G))

#define NO_PSCAN    0x0ULL
#define PSCAN_FCC   0x0000000000000001ULL
#define PSCAN_FCC_T 0x0000000000000002ULL
#define PSCAN_ETSI  0x0000000000000004ULL
#define PSCAN_MKK1  0x0000000000000008ULL
#define PSCAN_MKK2  0x0000000000000010ULL
#define PSCAN_MKKA  0x0000000000000020ULL
#define PSCAN_MKKA_G    0x0000000000000040ULL
#define PSCAN_ETSIA 0x0000000000000080ULL
#define PSCAN_ETSIB 0x0000000000000100ULL
#define PSCAN_ETSIC 0x0000000000000200ULL
#define PSCAN_WWR   0x0000000000000400ULL
#define PSCAN_MKKA1 0x0000000000000800ULL
#define PSCAN_MKKA1_G   0x0000000000001000ULL
#define PSCAN_MKKA2 0x0000000000002000ULL
#define PSCAN_MKKA2_G   0x0000000000004000ULL
#define PSCAN_MKK3  0x0000000000008000ULL
#define PSCAN_DEFER 0x7FFFFFFFFFFFFFFFULL
#define IS_ECM_CHAN 0x8000000000000000ULL

#define isWwrSKU(_ah) \
	(((ath9k_regd_get_eepromRD((_ah)) & WORLD_SKU_MASK) == \
		WORLD_SKU_PREFIX) || \
		(ath9k_regd_get_eepromRD(_ah) == WORLD))

#define isWwrSKU_NoMidband(_ah) \
	((ath9k_regd_get_eepromRD((_ah)) == WOR3_WORLD) || \
	(ath9k_regd_get_eepromRD(_ah) == WOR4_WORLD) || \
	(ath9k_regd_get_eepromRD(_ah) == WOR5_ETSIC))

#define isUNII1OddChan(ch) \
	((ch == 5170) || (ch == 5190) || (ch == 5210) || (ch == 5230))

#define IS_HT40_MODE(_mode)					\
	(((_mode == ATH9K_MODE_11NA_HT40PLUS  ||		\
	   _mode == ATH9K_MODE_11NG_HT40PLUS    ||		\
	   _mode == ATH9K_MODE_11NA_HT40MINUS   ||		\
	   _mode == ATH9K_MODE_11NG_HT40MINUS) ? true : false))

#define CHAN_FLAGS      (CHANNEL_ALL|CHANNEL_HALF|CHANNEL_QUARTER)

#define swap_array(_a, _b, _size) {                   \
	u8 *s = _b;                       \
	int i = _size;                          \
	do {                                    \
		u8 tmp = *_a;             \
		*_a++ = *s;                     \
		*s++ = tmp;                     \
	} while (--i);                          \
	_a -= _size;                            \
}


#define HALF_MAXCHANBW          10

#define MULTI_DOMAIN_MASK 0xFF00

#define WORLD_SKU_MASK          0x00F0
#define WORLD_SKU_PREFIX        0x0060

#define CHANNEL_HALF_BW         10
#define CHANNEL_QUARTER_BW      5

typedef int ath_hal_cmp_t(const void *, const void *);

struct reg_dmn_pair_mapping {
	u16 regDmnEnum;
	u16 regDmn5GHz;
	u16 regDmn2GHz;
	u32 flags5GHz;
	u32 flags2GHz;
	u64 pscanMask;
	u16 singleCC;
};

struct ccmap {
	char isoName[3];
	u16 countryCode;
};

struct country_code_to_enum_rd {
	u16 countryCode;
	u16 regDmnEnum;
	const char *isoName;
	const char *name;
	bool allow11g;
	bool allow11aTurbo;
	bool allow11gTurbo;
	bool allow11ng20;
	bool allow11ng40;
	bool allow11na20;
	bool allow11na40;
	u16 outdoorChanStart;
};

struct RegDmnFreqBand {
	u16 lowChannel;
	u16 highChannel;
	u8 powerDfs;
	u8 antennaMax;
	u8 channelBW;
	u8 channelSep;
	u64 useDfs;
	u64 usePassScan;
	u8 regClassId;
};

struct regDomain {
	u16 regDmnEnum;
	u8 conformanceTestLimit;
	u64 dfsMask;
	u64 pscan;
	u32 flags;
	u64 chan11a[BMLEN];
	u64 chan11a_turbo[BMLEN];
	u64 chan11a_dyn_turbo[BMLEN];
	u64 chan11b[BMLEN];
	u64 chan11g[BMLEN];
	u64 chan11g_turbo[BMLEN];
};

struct cmode {
	u32 mode;
	u32 flags;
};

#define YES true
#define NO  false

struct japan_bandcheck {
	u16 freqbandbit;
	u32 eepromflagtocheck;
};

struct common_mode_power {
	u16 lchan;
	u16 hchan;
	u8 pwrlvl;
};

enum CountryCode {
	CTRY_ALBANIA = 8,
	CTRY_ALGERIA = 12,
	CTRY_ARGENTINA = 32,
	CTRY_ARMENIA = 51,
	CTRY_AUSTRALIA = 36,
	CTRY_AUSTRIA = 40,
	CTRY_AZERBAIJAN = 31,
	CTRY_BAHRAIN = 48,
	CTRY_BELARUS = 112,
	CTRY_BELGIUM = 56,
	CTRY_BELIZE = 84,
	CTRY_BOLIVIA = 68,
	CTRY_BOSNIA_HERZ = 70,
	CTRY_BRAZIL = 76,
	CTRY_BRUNEI_DARUSSALAM = 96,
	CTRY_BULGARIA = 100,
	CTRY_CANADA = 124,
	CTRY_CHILE = 152,
	CTRY_CHINA = 156,
	CTRY_COLOMBIA = 170,
	CTRY_COSTA_RICA = 188,
	CTRY_CROATIA = 191,
	CTRY_CYPRUS = 196,
	CTRY_CZECH = 203,
	CTRY_DENMARK = 208,
	CTRY_DOMINICAN_REPUBLIC = 214,
	CTRY_ECUADOR = 218,
	CTRY_EGYPT = 818,
	CTRY_EL_SALVADOR = 222,
	CTRY_ESTONIA = 233,
	CTRY_FAEROE_ISLANDS = 234,
	CTRY_FINLAND = 246,
	CTRY_FRANCE = 250,
	CTRY_GEORGIA = 268,
	CTRY_GERMANY = 276,
	CTRY_GREECE = 300,
	CTRY_GUATEMALA = 320,
	CTRY_HONDURAS = 340,
	CTRY_HONG_KONG = 344,
	CTRY_HUNGARY = 348,
	CTRY_ICELAND = 352,
	CTRY_INDIA = 356,
	CTRY_INDONESIA = 360,
	CTRY_IRAN = 364,
	CTRY_IRAQ = 368,
	CTRY_IRELAND = 372,
	CTRY_ISRAEL = 376,
	CTRY_ITALY = 380,
	CTRY_JAMAICA = 388,
	CTRY_JAPAN = 392,
	CTRY_JORDAN = 400,
	CTRY_KAZAKHSTAN = 398,
	CTRY_KENYA = 404,
	CTRY_KOREA_NORTH = 408,
	CTRY_KOREA_ROC = 410,
	CTRY_KOREA_ROC2 = 411,
	CTRY_KOREA_ROC3 = 412,
	CTRY_KUWAIT = 414,
	CTRY_LATVIA = 428,
	CTRY_LEBANON = 422,
	CTRY_LIBYA = 434,
	CTRY_LIECHTENSTEIN = 438,
	CTRY_LITHUANIA = 440,
	CTRY_LUXEMBOURG = 442,
	CTRY_MACAU = 446,
	CTRY_MACEDONIA = 807,
	CTRY_MALAYSIA = 458,
	CTRY_MALTA = 470,
	CTRY_MEXICO = 484,
	CTRY_MONACO = 492,
	CTRY_MOROCCO = 504,
	CTRY_NEPAL = 524,
	CTRY_NETHERLANDS = 528,
	CTRY_NETHERLANDS_ANTILLES = 530,
	CTRY_NEW_ZEALAND = 554,
	CTRY_NICARAGUA = 558,
	CTRY_NORWAY = 578,
	CTRY_OMAN = 512,
	CTRY_PAKISTAN = 586,
	CTRY_PANAMA = 591,
	CTRY_PAPUA_NEW_GUINEA = 598,
	CTRY_PARAGUAY = 600,
	CTRY_PERU = 604,
	CTRY_PHILIPPINES = 608,
	CTRY_POLAND = 616,
	CTRY_PORTUGAL = 620,
	CTRY_PUERTO_RICO = 630,
	CTRY_QATAR = 634,
	CTRY_ROMANIA = 642,
	CTRY_RUSSIA = 643,
	CTRY_SAUDI_ARABIA = 682,
	CTRY_SERBIA_MONTENEGRO = 891,
	CTRY_SINGAPORE = 702,
	CTRY_SLOVAKIA = 703,
	CTRY_SLOVENIA = 705,
	CTRY_SOUTH_AFRICA = 710,
	CTRY_SPAIN = 724,
	CTRY_SRI_LANKA = 144,
	CTRY_SWEDEN = 752,
	CTRY_SWITZERLAND = 756,
	CTRY_SYRIA = 760,
	CTRY_TAIWAN = 158,
	CTRY_THAILAND = 764,
	CTRY_TRINIDAD_Y_TOBAGO = 780,
	CTRY_TUNISIA = 788,
	CTRY_TURKEY = 792,
	CTRY_UAE = 784,
	CTRY_UKRAINE = 804,
	CTRY_UNITED_KINGDOM = 826,
	CTRY_UNITED_STATES = 840,
	CTRY_UNITED_STATES_FCC49 = 842,
	CTRY_URUGUAY = 858,
	CTRY_UZBEKISTAN = 860,
	CTRY_VENEZUELA = 862,
	CTRY_VIET_NAM = 704,
	CTRY_YEMEN = 887,
	CTRY_ZIMBABWE = 716,
	CTRY_JAPAN1 = 393,
	CTRY_JAPAN2 = 394,
	CTRY_JAPAN3 = 395,
	CTRY_JAPAN4 = 396,
	CTRY_JAPAN5 = 397,
	CTRY_JAPAN6 = 4006,
	CTRY_JAPAN7 = 4007,
	CTRY_JAPAN8 = 4008,
	CTRY_JAPAN9 = 4009,
	CTRY_JAPAN10 = 4010,
	CTRY_JAPAN11 = 4011,
	CTRY_JAPAN12 = 4012,
	CTRY_JAPAN13 = 4013,
	CTRY_JAPAN14 = 4014,
	CTRY_JAPAN15 = 4015,
	CTRY_JAPAN16 = 4016,
	CTRY_JAPAN17 = 4017,
	CTRY_JAPAN18 = 4018,
	CTRY_JAPAN19 = 4019,
	CTRY_JAPAN20 = 4020,
	CTRY_JAPAN21 = 4021,
	CTRY_JAPAN22 = 4022,
	CTRY_JAPAN23 = 4023,
	CTRY_JAPAN24 = 4024,
	CTRY_JAPAN25 = 4025,
	CTRY_JAPAN26 = 4026,
	CTRY_JAPAN27 = 4027,
	CTRY_JAPAN28 = 4028,
	CTRY_JAPAN29 = 4029,
	CTRY_JAPAN30 = 4030,
	CTRY_JAPAN31 = 4031,
	CTRY_JAPAN32 = 4032,
	CTRY_JAPAN33 = 4033,
	CTRY_JAPAN34 = 4034,
	CTRY_JAPAN35 = 4035,
	CTRY_JAPAN36 = 4036,
	CTRY_JAPAN37 = 4037,
	CTRY_JAPAN38 = 4038,
	CTRY_JAPAN39 = 4039,
	CTRY_JAPAN40 = 4040,
	CTRY_JAPAN41 = 4041,
	CTRY_JAPAN42 = 4042,
	CTRY_JAPAN43 = 4043,
	CTRY_JAPAN44 = 4044,
	CTRY_JAPAN45 = 4045,
	CTRY_JAPAN46 = 4046,
	CTRY_JAPAN47 = 4047,
	CTRY_JAPAN48 = 4048,
	CTRY_JAPAN49 = 4049,
	CTRY_JAPAN50 = 4050,
	CTRY_JAPAN51 = 4051,
	CTRY_JAPAN52 = 4052,
	CTRY_JAPAN53 = 4053,
	CTRY_JAPAN54 = 4054,
	CTRY_JAPAN55 = 4055,
	CTRY_JAPAN56 = 4056,
	CTRY_JAPAN57 = 4057,
	CTRY_JAPAN58 = 4058,
	CTRY_JAPAN59 = 4059,
	CTRY_AUSTRALIA2 = 5000,
	CTRY_CANADA2 = 5001,
	CTRY_BELGIUM2 = 5002
};

void ath9k_regd_get_current_country(struct ath_hal *ah,
				    struct ath9k_country_entry *ctry);

#endif
