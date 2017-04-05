/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/


#ifndef	__RT_CHANNELPLAN_H__
#define __RT_CHANNELPLAN_H__

enum rt_channel_domain_new {

	/* ===== Add new channel plan above this line =============== */

	/* For new architecture we define different 2G/5G CH area for all country. */
	/* 2.4 G only */
	RT_CHANNEL_DOMAIN_2G_WORLD_5G_NULL				= 0x20,
	RT_CHANNEL_DOMAIN_2G_ETSI1_5G_NULL				= 0x21,
	RT_CHANNEL_DOMAIN_2G_FCC1_5G_NULL				= 0x22,
	RT_CHANNEL_DOMAIN_2G_MKK1_5G_NULL				= 0x23,
	RT_CHANNEL_DOMAIN_2G_ETSI2_5G_NULL				= 0x24,
	/* 2.4 G + 5G type 1 */
	RT_CHANNEL_DOMAIN_2G_FCC1_5G_FCC1				= 0x25,
	RT_CHANNEL_DOMAIN_2G_WORLD_5G_ETSI1				= 0x26,
	/* RT_CHANNEL_DOMAIN_2G_WORLD_5G_ETSI1				= 0x27, */
	/* ..... */

	RT_CHANNEL_DOMAIN_MAX_NEW,

};


#if 0
#define DOMAIN_CODE_2G_WORLD \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13
#define DOMAIN_CODE_2G_ETSI1 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13
#define DOMAIN_CODE_2G_ETSI2 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11
#define DOMAIN_CODE_2G_FCC1 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14
#define DOMAIN_CODE_2G_MKK1 \
	{10, 11, 12, 13}, 4

#define DOMAIN_CODE_5G_ETSI1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19
#define DOMAIN_CODE_5G_ETSI2 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24
#define DOMAIN_CODE_5G_ETSI3 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165}, 22
#define DOMAIN_CODE_5G_FCC1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24
#define DOMAIN_CODE_5G_FCC2 \
	{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9
#define DOMAIN_CODE_5G_FCC3 \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13
#define DOMAIN_CODE_5G_FCC4 \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161}, 12
#define DOMAIN_CODE_5G_FCC5 \
	{149, 153, 157, 161, 165}, 5
#define DOMAIN_CODE_5G_FCC6 \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define DOMAIN_CODE_5G_FCC7 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define DOMAIN_CODE_5G_IC1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define DOMAIN_CODE_5G_KCC1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 20
#define DOMAIN_CODE_5G_MKK1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19
#define DOMAIN_CODE_5G_MKK2 \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define DOMAIN_CODE_5G_MKK3 \
	{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11
#define DOMAIN_CODE_5G_NCC1 \
	{56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 24
#define DOMAIN_CODE_5G_NCC2 \
	{56, 60, 64, 149, 153, 157, 161, 165}, 8
#define UNDEFINED \
	{0}, 0
#endif

/*
 *
 *
 *

Countries							"Country Abbreviation"	Domain Code					SKU's	Ch# of 20MHz
															2G			5G						Ch# of 40MHz
"Albania阿爾巴尼亞"					AL													Local Test

"Algeria阿爾及利亞"					DZ									CE TCF

"Antigua & Barbuda安提瓜島&巴布達"	AG						2G_WORLD					FCC TCF

"Argentina阿根廷"					AR						2G_WORLD					Local Test

"Armenia亞美尼亞"					AM						2G_WORLD					ETSI

"Aruba阿魯巴島"						AW						2G_WORLD					FCC TCF

"Australia澳洲"						AU						2G_WORLD		5G_ETSI2

"Austria奧地利"						AT						2G_WORLD		5G_ETSI1	CE

"Azerbaijan阿塞拜彊"				AZ						2G_WORLD					CE TCF

"Bahamas巴哈馬"						BS						2G_WORLD

"Barbados巴巴多斯"					BB						2G_WORLD					FCC TCF

"Belgium比利時"						BE						2G_WORLD		5G_ETSI1	CE

"Bermuda百慕達"						BM						2G_WORLD					FCC TCF

"Brazil巴西"						BR						2G_WORLD					Local Test

"Bulgaria保加利亞"					BG						2G_WORLD		5G_ETSI1	CE

"Canada加拿大"						CA						2G_FCC1			5G_FCC7		IC / FCC	IC / FCC

"Cayman Islands開曼群島"			KY						2G_WORLD		5G_ETSI1	CE

"Chile智利"							CL						2G_WORLD					FCC TCF

"China中國"							CN						2G_WORLD		5G_FCC5		信部?【2002】353?

"Columbia哥倫比亞"					CO						2G_WORLD					Voluntary

"Costa Rica哥斯達黎加"				CR						2G_WORLD					FCC TCF

"Cyprus塞浦路斯"					CY						2G_WORLD		5G_ETSI1	CE

"Czech 捷克"						CZ						2G_WORLD		5G_ETSI1	CE

"Denmark丹麥"						DK						2G_WORLD		5G_ETSI1	CE

"Dominican Republic多明尼加共和國"	DO						2G_WORLD					FCC TCF

"Egypt埃及"	EG	2G_WORLD			CE T												CF

"El Salvador薩爾瓦多"				SV						2G_WORLD					Voluntary

"Estonia愛沙尼亞"					EE						2G_WORLD		5G_ETSI1	CE

"Finland芬蘭"						FI						2G_WORLD		5G_ETSI1	CE

"France法國"						FR										5G_E		TSI1	CE

"Germany德國"						DE						2G_WORLD		5G_ETSI1	CE

"Greece 希臘"						GR						2G_WORLD		5G_ETSI1	CE

"Guam關島"							GU						2G_WORLD

"Guatemala瓜地馬拉"					GT						2G_WORLD

"Haiti海地"							HT						2G_WORLD					FCC TCF

"Honduras宏都拉斯"					HN						2G_WORLD					FCC TCF

"Hungary匈牙利"						HU						2G_WORLD		5G_ETSI1	CE

"Iceland冰島"						IS						2G_WORLD		5G_ETSI1	CE

"India印度"												2G_WORLD		5G_FCC3		FCC/CE TCF

"Ireland愛爾蘭"						IE						2G_WORLD		5G_ETSI1	CE

"Israel以色列"						IL										5G_F		CC6	CE TCF

"Italy義大利"						IT						2G_WORLD		5G_ETSI1	CE

"Japan日本"							JP						2G_MKK1			5G_MKK1		MKK	MKK

"Korea韓國"							KR						2G_WORLD		5G_KCC1		KCC	KCC

"Latvia拉脫維亞"					LV						2G_WORLD		5G_ETSI1	CE

"Lithuania立陶宛"					LT						2G_WORLD		5G_ETSI1	CE

"Luxembourg盧森堡"					LU						2G_WORLD		5G_ETSI1	CE

"Malaysia馬來西亞"					MY						2G_WORLD					Local Test

"Malta馬爾他"						MT						2G_WORLD		5G_ETSI1	CE

"Mexico墨西哥"						MX						2G_WORLD		5G_FCC3		Local Test

"Morocco摩洛哥"						MA													CE TCF

"Netherlands荷蘭"					NL						2G_WORLD		5G_ETSI1	CE

"New Zealand紐西蘭"					NZ						2G_WORLD		5G_ETSI2

"Norway挪威"						NO						2G_WORLD		5G_ETSI1	CE

"Panama巴拿馬 "						PA						2G_FCC1						Voluntary

"Philippines菲律賓"					PH						2G_WORLD					FCC TCF

"Poland波蘭"						PL						2G_WORLD		5G_ETSI1	CE

"Portugal葡萄牙"					PT						2G_WORLD		5G_ETSI1	CE

"Romania羅馬尼亞"					RO						2G_WORLD		5G_ETSI1	CE

"Russia俄羅斯"						RU						2G_WORLD		5G_ETSI3	CE TCF

"Saudi Arabia沙地阿拉伯"			SA						2G_WORLD					CE TCF

"Singapore新加坡"					SG						2G_WORLD

"Slovakia斯洛伐克"					SK						2G_WORLD		5G_ETSI1	CE

"Slovenia斯洛維尼亞"				SI						2G_WORLD		5G_ETSI1	CE

"South Africa南非"					ZA						2G_WORLD					CE TCF

"Spain西班牙"						ES										5G_ETSI1	CE

"Sweden瑞典"						SE						2G_WORLD		5G_ETSI1	CE

"Switzerland瑞士"					CH						2G_WORLD		5G_ETSI1	CE

"Taiwan臺灣"						TW						2G_FCC1			5G_NCC1	NCC

"Thailand泰國"						TH						2G_WORLD					FCC/CE TCF

"Turkey土耳其"						TR						2G_WORLD

"Ukraine烏克蘭"						UA						2G_WORLD					Local Test

"United Kingdom英國"				GB						2G_WORLD		5G_ETSI1	CE	ETSI

"United States美國"					US						2G_FCC1			5G_FCC7		FCC	FCC

"Venezuela委內瑞拉"					VE						2G_WORLD		5G_FCC4		FCC TCF

"Vietnam越南"						VN						2G_WORLD					FCC/CE TCF



*/

/* counter abbervation. */
enum rt_country_name {
	RT_CTRY_AL,				/*	"Albania阿爾巴尼亞" */
	RT_CTRY_DZ,             /* "Algeria阿爾及利亞" */
	RT_CTRY_AG,             /* "Antigua & Barbuda安提瓜島&巴布達" */
	RT_CTRY_AR,             /* "Argentina阿根廷" */
	RT_CTRY_AM,             /* "Armenia亞美尼亞" */
	RT_CTRY_AW,             /* "Aruba阿魯巴島" */
	RT_CTRY_AU,             /* "Australia澳洲" */
	RT_CTRY_AT,             /* "Austria奧地利" */
	RT_CTRY_AZ,             /* "Azerbaijan阿塞拜彊" */
	RT_CTRY_BS,             /* "Bahamas巴哈馬" */
	RT_CTRY_BB,             /* "Barbados巴巴多斯" */
	RT_CTRY_BE,             /* "Belgium比利時" */
	RT_CTRY_BM,             /* "Bermuda百慕達" */
	RT_CTRY_BR,             /* "Brazil巴西" */
	RT_CTRY_BG,             /* "Bulgaria保加利亞" */
	RT_CTRY_CA,             /* "Canada加拿大" */
	RT_CTRY_KY,             /* "Cayman Islands開曼群島" */
	RT_CTRY_CL,             /* "Chile智利" */
	RT_CTRY_CN,             /* "China中國" */
	RT_CTRY_CO,             /* "Columbia哥倫比亞" */
	RT_CTRY_CR,             /* "Costa Rica哥斯達黎加" */
	RT_CTRY_CY,             /* "Cyprus塞浦路斯" */
	RT_CTRY_CZ,             /* "Czech 捷克" */
	RT_CTRY_DK,             /* "Denmark丹麥" */
	RT_CTRY_DO,             /* "Dominican Republic多明尼加共和國" */
	RT_CTRY_CE,             /* "Egypt埃及"	EG	2G_WORLD */
	RT_CTRY_SV,             /* "El Salvador薩爾瓦多" */
	RT_CTRY_EE,             /* "Estonia愛沙尼亞" */
	RT_CTRY_FI,             /* "Finland芬蘭" */
	RT_CTRY_FR,             /* "France法國" */
	RT_CTRY_DE,             /* "Germany德國" */
	RT_CTRY_GR,             /* "Greece 希臘" */
	RT_CTRY_GU,             /* "Guam關島" */
	RT_CTRY_GT,             /* "Guatemala瓜地馬拉" */
	RT_CTRY_HT,             /* "Haiti海地" */
	RT_CTRY_HN,             /* "Honduras宏都拉斯" */
	RT_CTRY_HU,             /* "Hungary匈牙利" */
	RT_CTRY_IS,             /* "Iceland冰島" */
	RT_CTRY_IN,             /* "India印度" */
	RT_CTRY_IE,             /* "Ireland愛爾蘭" */
	RT_CTRY_IL,             /* "Israel以色列" */
	RT_CTRY_IT,             /* "Italy義大利" */
	RT_CTRY_JP,             /* "Japan日本" */
	RT_CTRY_KR,             /* "Korea韓國" */
	RT_CTRY_LV,             /* "Latvia拉脫維亞" */
	RT_CTRY_LT,             /* "Lithuania立陶宛" */
	RT_CTRY_LU,             /* "Luxembourg盧森堡" */
	RT_CTRY_MY,             /* "Malaysia馬來西亞" */
	RT_CTRY_MT,             /* "Malta馬爾他" */
	RT_CTRY_MX,             /* "Mexico墨西哥" */
	RT_CTRY_MA,             /* "Morocco摩洛哥" */
	RT_CTRY_NL,             /* "Netherlands荷蘭" */
	RT_CTRY_NZ,             /* "New Zealand紐西蘭" */
	RT_CTRY_NO,             /* "Norway挪威" */
	RT_CTRY_PA,             /* "Panama巴拿馬 " */
	RT_CTRY_PH,             /* "Philippines菲律賓" */
	RT_CTRY_PL,             /* "Poland波蘭" */
	RT_CTRY_PT,             /* "Portugal葡萄牙" */
	RT_CTRY_RO,             /* "Romania羅馬尼亞" */
	RT_CTRY_RU,             /* "Russia俄羅斯" */
	RT_CTRY_SA,             /* "Saudi Arabia沙地阿拉伯" */
	RT_CTRY_SG,             /* "Singapore新加坡" */
	RT_CTRY_SK,             /* "Slovakia斯洛伐克" */
	RT_CTRY_SI,             /* "Slovenia斯洛維尼亞" */
	RT_CTRY_ZA,             /* "South Africa南非" */
	RT_CTRY_ES,             /* "Spain西班牙" */
	RT_CTRY_SE,             /* "Sweden瑞典" */
	RT_CTRY_CH,             /* "Switzerland瑞士" */
	RT_CTRY_TW,             /* "Taiwan臺灣" */
	RT_CTRY_TH,             /* "Thailand泰國" */
	RT_CTRY_TR,             /* "Turkey土耳其" */
	RT_CTRY_UA,             /* "Ukraine烏克蘭" */
	RT_CTRY_GB,             /* "United Kingdom英國" */
	RT_CTRY_US,             /* "United States美國" */
	RT_CTRY_VE,             /* "Venezuela委內瑞拉" */
	RT_CTRY_VN,             /* "Vietnam越南" */
	RT_CTRY_MAX,

};

/* Scan type including active and passive scan. */
enum rt_scan_type_new {
	SCAN_NULL,
	SCAN_ACT,
	SCAN_PAS,
	SCAN_BOTH,
};


/* Power table sample. */

struct _RT_CHNL_PLAN_LIMIT {
	u16	chnl_start;
	u16	chnl_end;

	u16	freq_start;
	u16	freq_end;
};


/*
 * 2.4G Regulatory Domains
 *   */
enum rt_regulation_2g {
	RT_2G_NULL,
	RT_2G_WORLD,
	RT_2G_ETSI1,
	RT_2G_FCC1,
	RT_2G_MKK1,
	RT_2G_ETSI2

};


/* typedef struct _RT_CHANNEL_BEHAVIOR
 * {
 *	u8	chnl;
 *	enum rt_scan_type_new
 *
 * }RT_CHANNEL_BEHAVIOR, *PRT_CHANNEL_BEHAVIOR; */

/* typedef struct _RT_CHANNEL_PLAN_TYPE
 * {
 *	RT_CHANNEL_BEHAVIOR
 *	u8					Chnl_num;
 * }RT_CHNL_PLAN_TYPE, *PRT_CHNL_PLAN_TYPE; */

/*
 * 2.4G channel number
 * channel definition & number
 *   */
#define CHNL_RT_2G_NULL \
	{0}, 0
#define CHNL_RT_2G_WORLD \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13
#define CHNL_RT_2G_WORLD_TEST \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13

#define CHNL_RT_2G_EFSI1 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13
#define CHNL_RT_2G_FCC1 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11
#define CHNL_RT_2G_MKK1 \
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14
#define CHNL_RT_2G_ETSI2 \
	{10, 11, 12, 13}, 4

/*
 * 2.4G channel active or passive scan.
 *   */
#define CHNL_RT_2G_NULL_SCAN_TYPE \
	{SCAN_NULL}
#define CHNL_RT_2G_WORLD_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}
#define CHNL_RT_2G_EFSI1_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define CHNL_RT_2G_FCC1_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define CHNL_RT_2G_MKK1_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define CHNL_RT_2G_ETSI2_SCAN_TYPE \
	{1, 1, 1, 1}


/*
 * 2.4G band & Frequency Section
 * Freqency start & end / band number
 *   */
#define FREQ_RT_2G_NULL \
	{0}, 0
/* Passive scan CH 12, 13 */
#define FREQ_RT_2G_WORLD \
	{2412, 2472}, 1
#define FREQ_RT_2G_EFSI1 \
	{2412, 2472}, 1
#define FREQ_RT_2G_FCC1 \
	{2412, 2462}, 1
#define FREQ_RT_2G_MKK1 \
	{2412, 2484}, 1
#define FREQ_RT_2G_ETSI2 \
	{2457, 2472}, 1


/*
 * 5G Regulatory Domains
 *   */
enum rt_regulation_5g {
	RT_5G_NULL,
	RT_5G_WORLD,
	RT_5G_ETSI1,
	RT_5G_ETSI2,
	RT_5G_ETSI3,
	RT_5G_FCC1,
	RT_5G_FCC2,
	RT_5G_FCC3,
	RT_5G_FCC4,
	RT_5G_FCC5,
	RT_5G_FCC6,
	RT_5G_FCC7,
	RT_5G_IC1,
	RT_5G_KCC1,
	RT_5G_MKK1,
	RT_5G_MKK2,
	RT_5G_MKK3,
	RT_5G_NCC1,

};

/*
 * 5G channel number
 *   */
#define CHNL_RT_5G_NULL \
	{0}, 0
#define CHNL_RT_5G_WORLD \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19
#define CHNL_RT_5G_ETSI1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24
#define CHNL_RT_5G_ETSI2 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165}, 22
#define CHNL_RT_5G_ETSI3 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24
#define CHNL_RT_5G_FCC1 \
	{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9
#define CHNL_RT_5G_FCC2 \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13
#define CHNL_RT_5G_FCC3 \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161}, 12
#define CHNL_RT_5G_FCC4 \
	{149, 153, 157, 161, 165}, 5
#define CHNL_RT_5G_FCC5 \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define CHNL_RT_5G_FCC6 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_FCC7 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_IC1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_KCC1 \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19
#define CHNL_RT_5G_MKK1 \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define CHNL_RT_5G_MKK2 \
	{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11
#define CHNL_RT_5G_MKK3 \
	{56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 24
#define CHNL_RT_5G_NCC1 \
	{56, 60, 64, 149, 153, 157, 161, 165}, 8

/*
 * 5G channel active or passive scan.
 *   */
#define CHNL_RT_5G_NULL_SCAN_TYPE \
	{SCAN_NULL}
#define CHNL_RT_5G_WORLD_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define CHNL_RT_5G_ETSI1_SCAN_TYPE \
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
#define CHNL_RT_5G_ETSI2_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165}, 22
#define CHNL_RT_5G_ETSI3_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 24
#define CHNL_RT_5G_FCC1_SCAN_TYPE \
	{36, 40, 44, 48, 149, 153, 157, 161, 165}, 9
#define CHNL_RT_5G_FCC2_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 13
#define CHNL_RT_5G_FCC3_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161}, 12
#define CHNL_RT_5G_FCC4_SCAN_TYPE \
	{149, 153, 157, 161, 165}, 5
#define CHNL_RT_5G_FCC5_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define CHNL_RT_5G_FCC6_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_FCC7_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_IC1_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 20
#define CHNL_RT_5G_KCC1_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 19
#define CHNL_RT_5G_MKK1_SCAN_TYPE \
	{36, 40, 44, 48, 52, 56, 60, 64}, 8
#define CHNL_RT_5G_MKK2_SCAN_TYPE \
	{100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 11
#define CHNL_RT_5G_MKK3_SCAN_TYPE \
	{56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 24
#define CHNL_RT_5G_NCC1_SCAN_TYPE \
	{56, 60, 64, 149, 153, 157, 161, 165}, 8

/*
 * Global regulation
 *   */
enum rt_regulation_cmn {
	RT_WORLD,
	RT_FCC,
	RT_MKK,
	RT_ETSI,
	RT_IC,
	RT_CE,
	RT_NCC,

};



/*
 * Special requirement for different regulation domain.
 * For internal test or customerize special request.
 *   */
enum rt_chnlplan_sreq {
	RT_SREQ_NA						= 0x0,
	RT_SREQ_2G_ADHOC_11N			= 0x00000001,
	RT_SREQ_2G_ADHOC_11B			= 0x00000002,
	RT_SREQ_2G_ALL_PASS				= 0x00000004,
	RT_SREQ_2G_ALL_ACT				= 0x00000008,
	RT_SREQ_5G_ADHOC_11N			= 0x00000010,
	RT_SREQ_5G_ADHOC_11AC			= 0x00000020,
	RT_SREQ_5G_ALL_PASS				= 0x00000040,
	RT_SREQ_5G_ALL_ACT				= 0x00000080,
	RT_SREQ_C1_PLAN					= 0x00000100,
	RT_SREQ_C2_PLAN					= 0x00000200,
	RT_SREQ_C3_PLAN					= 0x00000400,
	RT_SREQ_C4_PLAN					= 0x00000800,
	RT_SREQ_NFC_ON					= 0x00001000,
	RT_SREQ_MASK					= 0x0000FFFF,   /* Requirements bit mask */

};


/*
 * enum rt_country_name & enum rt_regulation_2g & enum rt_regulation_5g transfer table
 *
 *   */
struct _RT_CHANNEL_PLAN_COUNTRY_TRANSFER_TABLE {
	/*  */
	/* Define countery domain and corresponding */
	/*  */
	enum rt_country_name		country_enum;
	char				country_name[3];

	/* char		Domain_Name[12]; */
	enum rt_regulation_2g	domain_2g;

	enum rt_regulation_5g	domain_5g;

	RT_CHANNEL_DOMAIN	rt_ch_domain;
	/* u8		Country_Area; */

};


#define		RT_MAX_CHNL_NUM_2G		13
#define		RT_MAX_CHNL_NUM_5G		44

/* Power table sample. */

struct _RT_CHNL_PLAN_PWR_LIMIT {
	u16	chnl_start;
	u16	chnl_end;
	u8	db_max;
	u16	m_w_max;
};


#define		RT_MAX_BAND_NUM			5

struct _RT_CHANNEL_PLAN_MAXPWR {
	/*	STRING_T */
	struct _RT_CHNL_PLAN_PWR_LIMIT	chnl[RT_MAX_BAND_NUM];
	u8				band_useful_num;


};


/*
 * Power By rate Table.
 *   */



struct _RT_CHANNEL_PLAN_NEW {
	/*  */
	/* Define countery domain and corresponding */
	/*  */
	/* char		country_name[36]; */
	/* u8		country_enum; */

	/* char		Domain_Name[12]; */


	struct _RT_CHANNEL_PLAN_COUNTRY_TRANSFER_TABLE		*p_ctry_transfer;

	RT_CHANNEL_DOMAIN		rt_ch_domain;

	enum rt_regulation_2g		domain_2g;

	enum rt_regulation_5g		domain_5g;

	enum rt_regulation_cmn		regulator;

	enum rt_chnlplan_sreq		chnl_sreq;

	/* struct _RT_CHNL_PLAN_LIMIT		RtChnl; */

	u8	chnl_2g[MAX_CHANNEL_NUM];				/* CHNL_RT_2G_WORLD */
	u8	len_2g;
	u8	chnl_2g_scan_tp[MAX_CHANNEL_NUM];			/* CHNL_RT_2G_WORLD_SCAN_TYPE */
	/* u8	Freq2G[2];								 */ /* FREQ_RT_2G_WORLD */

	u8	chnl_5g[MAX_CHANNEL_NUM];
	u8	len_5g;
	u8	chnl_5g_scan_tp[MAX_CHANNEL_NUM];
	/* u8	Freq2G[2];								 */ /* FREQ_RT_2G_WORLD */

	struct _RT_CHANNEL_PLAN_MAXPWR	chnl_max_pwr;


};


#endif /* __RT_CHANNELPLAN_H__ */
