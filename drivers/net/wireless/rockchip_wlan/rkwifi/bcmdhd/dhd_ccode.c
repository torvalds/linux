// SPDX-License-Identifier: GPL-2.0
#ifdef CCODE_LIST
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif /* COMFIG_COMPAT */
#include <typedefs.h>
#include <dhd_config.h>

#ifdef BCMSDIO
#define CCODE_43438
#define CCODE_43436
#define CCODE_43455C0
#define CCODE_43456C5
#endif
#if defined(BCMSDIO) || defined(BCMPCIE)
#define CCODE_4356A2
#define CCODE_4359C0
#endif
#if defined(BCMPCIE)
#define CCODE_4375B4
#endif
#ifdef BCMDBUS
#define CCODE_4358U
#endif

#ifdef BCMSDIO
#ifdef CCODE_43438
const char ccode_43438[] = "RU/13";
#else
const char ccode_43438[] = "";
#endif

#ifdef CCODE_43436
const char ccode_43436[] = \
"AE/1 AR/1 AT/1 AU/2 "\
"BE/1 BG/1 BN/1 "\
"CA/2 CH/1 CN/38 CY/1 CZ/1 "\
"DE/3 DK/1 "\
"EE/1 ES/1 "\
"FI/1 FR/1 "\
"GB/1 GR/1 "\
"HR/1 HU/1 "\
"ID/5 IE/1 IS/1 IT/1 "\
"JP/3 "\
"KR/4 KW/1 "\
"LI/1 LT/1 LU/1 LV/1 "\
"MA/1 MT/1 MX/1 "\
"NL/1 NO/1 "\
"PL/1 PT/1 PY/1 "\
"RO/1 RU/5 "\
"SE/1 SI/1 SK/1 "\
"TR/7 TW/2 "\
"US/26 "\
"XZ/11";
#else
const char ccode_43436[] = "";
#endif

#ifdef CCODE_43455C0
const char ccode_43455c0[] = \
"AE/6 AG/2 AI/1 AL/2 AS/12 AT/4 AU/6 AW/2 AZ/2 "\
"BA/2 BD/1 BE/4 BG/4 BH/4 BM/12 BN/4 BR/2 BS/2 BY/3 "\
"CA/2 CH/4 CN/38 CO/17 CR/17 CY/4 CZ/4 "\
"DE/7 DK/4 "\
"EC/21 EE/4 EG/13 ES/4 ET/2 "\
"FI/4 FR/5 "\
"GB/6 GD/2 GF/2 GP/2 GR/4 GT/1 GU/30 "\
"HK/2 HR/4 HU/4 "\
"ID/1 IE/5 IL/14 IN/3 IS/4 IT/4 "\
"JO/3 JP/58 "\
"KH/2 KR/96 KW/5 KY/3 "\
"LA/2 LB/5 LI/4 LK/1 LS/2 LT/4 LU/3 LV/4 "\
"MA/2 MC/1 MD/2 ME/2 MK/2 MN/1 MQ/2 MR/2 MT/4 MU/2 MV/3 MW/1 MX/44 MY/3 "\
"NI/2 NL/4 NO/4 NZ/4 "\
"OM/4 "\
"PA/17 PE/20 PH/5 PL/4 PR/38 PT/4 PY/2 "\
"Q2/993 "\
"RE/2 RO/4 RS/2 RU/13 "\
"SE/4 SI/4 SK/4 SV/25 "\
"TH/5 TN/1 TR/7 TT/3 TW/65 "\
"UA/8 US/988 "\
"VA/2 VE/3 VG/2 VN/4 "\
"XZ/11 "\
"YT/2 "\
"ZA/6";
#else
const char ccode_43455c0[] = "";
#endif

#ifdef CCODE_43456C5
const char ccode_43456c5[] = \
"AE/6 AG/2 AI/1 AL/2 AS/12 AT/4 AU/6 AW/2 AZ/2 "\
"BA/2 BD/1 BE/4 BG/4 BH/4 BM/12 BN/4 BR/4 BS/2 BY/3 "\
"CA/2 CH/4 CN/38 CO/17 CR/17 CY/4 CZ/4 "\
"DE/7 DK/4 "\
"EC/21 EE/4 EG/13 ES/4 ET/2 "\
"FI/4 FR/5 "\
"GB/6 GD/2 GF/2 GP/2 GR/4 GT/1 GU/30 "\
"HK/2 HR/4 HU/4 "\
"ID/1 IE/5 IL/14 IN/3 IS/4 IT/4 "\
"JO/3 JP/58 "\
"KH/2 KR/96 KW/5 KY/3 "\
"LA/2 LB/5 LI/4 LK/1 LS/2 LT/4 LU/3 LV/4 "\
"MA/2 MC/1 MD/2 ME/2 MK/2 MN/1 MQ/2 MR/2 MT/4 MU/2 MV/3 MW/1 MX/44 MY/3 "\
"NI/2 NL/4 NO/4 NZ/4 "\
"OM/4 "\
"PA/17 PE/20 PH/5 PL/4 PR/38 PT/4 PY/2 "\
"Q2/993 "\
"RE/2 RO/4 RS/2 RU/13 "\
"SE/4 SI/4 SK/4 SV/25 "\
"TH/5 TN/1 TR/7 TT/3 TW/65 "\
"UA/8 US/988 "\
"VA/2 VE/3 VG/2 VN/4 "\
"XZ/11 "\
"YT/2 "\
"ZA/6";
#else
const char ccode_43456c5[] = "";
#endif
#endif

#ifdef CCODE_4356A2
const char ccode_4356a2[] = \
"AE/6 AG/2 AI/1 AL/2 AN/2 AR/21 AS/12 AT/4 AU/6 AW/2 AZ/2 "\
"BA/2 BD/2 BE/4 BG/4 BH/4 BM/12 BN/4 BR/4 BS/2 BY/3 "\
"CA/31 CH/4 CN/38 CO/17 CR/17 CY/4 CZ/4 "\
"DE/7 DK/4 DZ/1 "\
"EC/21 EE/4 ES/4 ET/2 "\
"FI/4 FR/5 "\
"GB/6 GD/2 GF/2 GP/2 GR/4 GT/1 GU/12 "\
"HK/2 HR/4 HU/4 "\
"ID/13 IE/5 IL/7 IN/28 IS/4 IT/4 "\
"JO/3 JP/58 "\
"KH/2 KR/57 KW/5 KY/3 "\
"LA/2 LB/5 LI/4 LK/1 LS/2 LT/4 LU/3 LV/4 "\
"MA/2 MC/1 MD/2 ME/2 MK/2 MN/1 MO/2 MR/2 MT/4 MQ/2 MU/2 MV/3 MW/1 MX/20 MY/16 "\
"NI/2 NL/4 NO/4 NP/3 NZ/4 "\
"OM/4 "\
"PA/17 PE/20 PG/2 PH/5 PL/4 PR/20 PT/4 PY/2 "\
"RE/2 RO/4 RS/2 RU/986 "\
"SE/4 SG/19 SI/4 SK/4 SN/2 SV/19 "\
"TH/9 TN/1 TR/7 TT/3 TW/1 "\
"UA/8 UG/2 US/1 UY/1 "\
"VA/2 VE/3 VG/2 VI/13 VN/4 "\
"XZ/11 "\
"YT/2 "\
"ZM/2 "\
"E0/32";
#else
const char ccode_4356a2[] = "";
#endif

#ifdef CCODE_4359C0
const char ccode_4359c0[] = \
"AD/1 AE/6 AG/2 AI/1 AL/3 AS/12 AT/21 AU/6 AW/2 AZ/8 "\
"BA/4 BD/1 BE/19 BG/18 BH/4 BM/12 BN/4 BR/2 BS/2 BY/3 "\
"CA/2 CN/38 CO/17 CR/17 CY/18 CZ/18 "\
"DE/30 DK/19 "\
"E0/32 EC/21 EE/18 EG/13 ES/21 ET/2 "\
"FI/19 FR/21 "\
"GB/996 GD/2 GE/1 GF/2 GP/2 GR/18 GT/1 GU/30 "\
"HK/2 HR/18 HU/18 "\
"ID/1 IE/21 IL/276 IN/3 IS/17 IT/20 "\
"JO/3 JP/967 "\
"KH/2 KR/70 KW/5 KY/3 "\
"LA/2 LB/5 LI/17 LK/1 LS/2 LT/18 LU/18 LV/18 "\
"MA/2 MC/2 MD/3 ME/5 MK/4 MN/1 MQ/2 MR/2 MT/18 MU/2 MV/3 MW/1 MX/44 MY/3 "\
"NI/2 NL/19 NO/18 NZ/4 "\
"OM/4 "\
"PA/17 PE/20 PH/5 PL/18 PR/38 PT/20 PY/2 "\
"Q1/947 Q2/993 "\
"RE/2 RO/18 RS/4 RU/986 "\
"SE/19 SI/18 SK/18 SM/1 SV/25 "\
"TH/5 TN/1 TR/18 TT/3 TW/980 "\
"UA/16 US/988 "\
"VA/3 VE/3 VG/2 VN/4 "\
"XZ/11 "\
"YT/2 "\
"ZA/6";
#else
const char ccode_4359c0[] = "";
#endif

#ifdef CCODE_4375B4
const char ccode_4375b4[] = \
"AE/6 AL/2 AM/1 AN/5 AR/21 AT/4 AU/6 AZ/2"\
"BA/2 BE/4 BG/4 BH/4 BN/4 BO/5 BR/17 BY/3"\
"CA/2 CH/4 CL/7 CN/38 CO/17 CR/17 CY/4 CZ/4"\
"DE/7 DK/4 DZ/2 EC/18 EE/4 EG/13 ES/4"\
"FI/4 FR/5"\
"GB/6 GR/4"\
"HK/999 HN/8 HR/4 HU/4"\
"ID/5 IE/5 IL/7 IN/3 IS/4 IT/4"\
"JO/3 JP/72"\
"KE/1 KR/96 KW/5 KZ/5"\
"LA/2 LB/5 LI/4 LK/2 LT/4 LU/4 LV/4"\
"MA/7 MC/1 ME/2 MK/2 MO/4 MT/4 MX/20 MY/19"\
"NL/4 NO/4 NZ/4"\
"OM/4"\
"PA/17 PE/20 PH/5 PK/2 PL/4 PR/20 PT/4"\
"RO/4 RU/62"\
"SA/5 SE/4 SG/12 SI/4 SK/4 SV/17"\
"TH/5 TN/1 TR/7 TT/3 TW/65"\
"UA/16 US/140 UY/10"\
"VE/3 VN/4"\
"ZA/19";
#else
const char ccode_4375b4[] = "";
#endif

#ifdef CCODE_4358U
const char ccode_4358u[] = \
"BE/4 BR/4 "\
"CA/2 CH/4 CN/38 CY/4 "\
"DE/7 DK/4 "\
"ES/4 "\
"FI/4 FR/5 "\
"GB/6 GR/4 "\
"HK/2 HU/4 "\
"IE/5 IL/7 IS/4 IT/4 "\
"JP/72 "\
"KE/0 KR/4 "\
"MY/3 "\
"NL/4 "\
"PT/4 "\
"SA/5 SE/4 SG/0 SZ/0 "\
"TH/5 TR/7 TW/230 "\
"US/0 "\
"VN/4";
#else
const char ccode_4358u[] = "";
#endif

typedef struct ccode_list_map_t {
	uint chip;
	uint chiprev;
	const char *ccode_list;
	const char *ccode_ww;
} ccode_list_map_t;

extern const char ccode_43438[];
extern const char ccode_43455c0[];
extern const char ccode_43456c5[];
extern const char ccode_4356a2[];
extern const char ccode_4359c0[];
extern const char ccode_4358u[];

const ccode_list_map_t ccode_list_map[] = {
	/* ChipID		Chiprev		ccode  */
#ifdef BCMSDIO
	{BCM43430_CHIP_ID,	0,	ccode_43438, ""},
	{BCM43430_CHIP_ID,	1,	ccode_43438, ""},
	{BCM43430_CHIP_ID,	2,	ccode_43436, ""},
	{BCM4345_CHIP_ID,	6,	ccode_43455c0, "XZ/11"},
	{BCM43454_CHIP_ID,	6,	ccode_43455c0, "XZ/11"},
	{BCM4345_CHIP_ID,	9,	ccode_43456c5, "XZ/11"},
	{BCM43454_CHIP_ID,	9,	ccode_43456c5, "XZ/11"},
	{BCM4354_CHIP_ID,	2,	ccode_4356a2, "XZ/11"},
	{BCM4356_CHIP_ID,	2,	ccode_4356a2, "XZ/11"},
	{BCM4371_CHIP_ID,	2,	ccode_4356a2, "XZ/11"},
	{BCM4359_CHIP_ID,	9,	ccode_4359c0, "XZ/11"},
#endif
#ifdef BCMPCIE
	{BCM4354_CHIP_ID,	2,	ccode_4356a2, "XZ/11"},
	{BCM4356_CHIP_ID,	2,	ccode_4356a2, "XZ/11"},
	{BCM4359_CHIP_ID,	9,	ccode_4359c0, "XZ/11"},
	{BCM4375_CHIP_ID,	5,	ccode_4375b4, "XZ/11"},
#endif
#ifdef BCMDBUS
	{BCM43569_CHIP_ID,	2,	ccode_4358u, "XW/0"},
#endif
};

int
dhd_ccode_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec)
{
	int bcmerror = -1, i;
	uint chip = dhd->conf->chip, chiprev = dhd->conf->chiprev; 
	const char *ccode_list = NULL, *ccode_ww = NULL;
	char *pch;

	for (i=0;  i<sizeof(ccode_list_map)/sizeof(ccode_list_map[0]);  i++) {
		const ccode_list_map_t* row = &ccode_list_map[i];
		if (row->chip == chip && row->chiprev == chiprev) {
			ccode_list = row->ccode_list;
			ccode_ww = row->ccode_ww;
			break;
		}
	}

	if (ccode_list) {
		pch = strstr(ccode_list, cspec->ccode);
		if (pch) {
			cspec->rev = (int)simple_strtol(pch+strlen(cspec->ccode)+1, NULL, 0);
			bcmerror = 0;
		}
	}

	if (bcmerror && ccode_ww && strlen(ccode_ww)>=4) {
		memcpy(cspec->ccode, ccode_ww, 2);
		cspec->rev = (int)simple_strtol(ccode_ww+3, NULL, 0);
	}

	return bcmerror;
}
#endif
