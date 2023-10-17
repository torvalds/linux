// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "regd.h"
#include "debug.h"
#include "phy.h"

#define COUNTRY_REGD_ENT(_alpha2, _regd_2g, _regd_5g) \
	{.alpha2 = (_alpha2), \
	 .txpwr_regd_2g = (_regd_2g), \
	 .txpwr_regd_5g = (_regd_5g), \
	}

#define rtw_dbg_regd_dump(_dev, _msg, _args...)			\
do {								\
	struct rtw_dev *__d = (_dev);				\
	const struct rtw_regd *__r =  &__d->regd;		\
	rtw_dbg(__d, RTW_DBG_REGD, _msg				\
		"apply alpha2 %c%c, regd {%d, %d}, dfs_region %d\n",\
		##_args,					\
		__r->regulatory->alpha2[0],			\
		__r->regulatory->alpha2[1],			\
		__r->regulatory->txpwr_regd_2g,			\
		__r->regulatory->txpwr_regd_5g,			\
		__r->dfs_region);				\
} while (0)

/* If country code is not correctly defined in efuse,
 * use worldwide country code and txpwr regd.
 */
static const struct rtw_regulatory rtw_reg_ww =
	COUNTRY_REGD_ENT("00", RTW_REGD_WW, RTW_REGD_WW);

static const struct rtw_regulatory rtw_reg_map[] = {
	COUNTRY_REGD_ENT("AD", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AG", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("AI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AN", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("AO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AQ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AR", RTW_REGD_MEXICO, RTW_REGD_MEXICO),
	COUNTRY_REGD_ENT("AS", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("AT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("AU", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("AW", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("AZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BB", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("BD", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BJ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BM", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("BN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BO", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("BR", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("BS", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("BT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BV", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BW", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BY", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("BZ", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("CA", RTW_REGD_IC, RTW_REGD_IC),
	COUNTRY_REGD_ENT("CC", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("CD", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CL", RTW_REGD_CHILE, RTW_REGD_CHILE),
	COUNTRY_REGD_ENT("CM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CN", RTW_REGD_CN, RTW_REGD_CN),
	COUNTRY_REGD_ENT("CO", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("CR", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("CV", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CX", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("CY", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("CZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("DE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("DJ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("DK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("DM", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("DO", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("DZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("EC", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("EE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("EG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("EH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ER", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ES", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ET", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("FI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("FJ", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("FK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("FM", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("FO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("FR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GB", RTW_REGD_UK, RTW_REGD_UK),
	COUNTRY_REGD_ENT("GD", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("GE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GP", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GQ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GT", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("GU", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("GW", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("GY", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("HK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("HM", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("HN", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("HR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("HT", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("HU", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ID", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IQ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("IT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("JE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("JM", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("JO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("JP", RTW_REGD_MKK, RTW_REGD_MKK),
	COUNTRY_REGD_ENT("KE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KN", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("KR", RTW_REGD_KCC, RTW_REGD_KCC),
	COUNTRY_REGD_ENT("KW", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("KY", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("KZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LB", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LC", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("LI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LU", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LV", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("LY", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MC", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MD", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ME", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MF", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("MG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MH", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("MK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ML", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MP", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("MQ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MU", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MV", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MW", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MX", RTW_REGD_MEXICO, RTW_REGD_MEXICO),
	COUNTRY_REGD_ENT("MY", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("MZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NC", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NF", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("NG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NI", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("NL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NP", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("NU", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("NZ", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("OM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PA", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("PE", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("PF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PR", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("PS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("PW", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("PY", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("QA", RTW_REGD_QATAR, RTW_REGD_QATAR),
	COUNTRY_REGD_ENT("RE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("RO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("RS", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("RU", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("RW", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SB", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SC", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("SE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SI", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SJ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SL", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SR", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("ST", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("SV", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("SX", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("SZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TC", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TD", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TH", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TJ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TK", RTW_REGD_ACMA, RTW_REGD_ACMA),
	COUNTRY_REGD_ENT("TM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TO", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TR", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("TT", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("TV", RTW_REGD_ETSI, RTW_REGD_WW),
	COUNTRY_REGD_ENT("TW", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("TZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("UA", RTW_REGD_UKRAINE, RTW_REGD_UKRAINE),
	COUNTRY_REGD_ENT("UG", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("US", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("UY", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("UZ", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("VA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("VC", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("VE", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("VG", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("VI", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("VN", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("VU", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("WF", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("WS", RTW_REGD_FCC, RTW_REGD_FCC),
	COUNTRY_REGD_ENT("XK", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("YE", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("YT", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ZA", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ZM", RTW_REGD_ETSI, RTW_REGD_ETSI),
	COUNTRY_REGD_ENT("ZW", RTW_REGD_ETSI, RTW_REGD_ETSI),
};

static void rtw_regd_apply_hw_cap_flags(struct wiphy *wiphy)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int i;

	if (efuse->hw_cap.bw & BIT(RTW_CHANNEL_WIDTH_80))
		return;

	sband = wiphy->bands[NL80211_BAND_2GHZ];
	if (!sband)
		goto out_5g;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		ch->flags |= IEEE80211_CHAN_NO_80MHZ;
	}

out_5g:
	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		ch->flags |= IEEE80211_CHAN_NO_80MHZ;
	}
}

static bool rtw_reg_is_ww(const struct rtw_regulatory *reg)
{
	return reg == &rtw_reg_ww;
}

static bool rtw_reg_match(const struct rtw_regulatory *reg, const char *alpha2)
{
	return memcmp(reg->alpha2, alpha2, 2) == 0;
}

static const struct rtw_regulatory *rtw_reg_find_by_name(const char *alpha2)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rtw_reg_map); i++) {
		if (rtw_reg_match(&rtw_reg_map[i], alpha2))
			return &rtw_reg_map[i];
	}

	return &rtw_reg_ww;
}

static
void rtw_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request);

/* call this before ieee80211_register_hw() */
int rtw_regd_init(struct rtw_dev *rtwdev)
{
	struct wiphy *wiphy = rtwdev->hw->wiphy;
	const struct rtw_regulatory *chip_reg;

	if (!wiphy)
		return -EINVAL;

	wiphy->reg_notifier = rtw_regd_notifier;

	chip_reg = rtw_reg_find_by_name(rtwdev->efuse.country_code);
	if (!rtw_reg_is_ww(chip_reg)) {
		rtwdev->regd.state = RTW_REGD_STATE_PROGRAMMED;

		/* Set REGULATORY_STRICT_REG before ieee80211_register_hw(),
		 * stack will wait for regulatory_hint() and consider it
		 * as the superset for our regulatory rule.
		 */
		wiphy->regulatory_flags |= REGULATORY_STRICT_REG;
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	} else {
		rtwdev->regd.state = RTW_REGD_STATE_WORLDWIDE;
	}

	rtwdev->regd.regulatory = &rtw_reg_ww;
	rtwdev->regd.dfs_region = NL80211_DFS_UNSET;
	rtw_dbg_regd_dump(rtwdev, "regd init state %d: ", rtwdev->regd.state);

	rtw_regd_apply_hw_cap_flags(wiphy);
	return 0;
}

/* call this after ieee80211_register_hw() */
int rtw_regd_hint(struct rtw_dev *rtwdev)
{
	struct wiphy *wiphy = rtwdev->hw->wiphy;
	int ret;

	if (!wiphy)
		return -EINVAL;

	if (rtwdev->regd.state == RTW_REGD_STATE_PROGRAMMED) {
		rtw_dbg(rtwdev, RTW_DBG_REGD,
			"country domain %c%c is PGed on efuse",
			rtwdev->efuse.country_code[0],
			rtwdev->efuse.country_code[1]);

		ret = regulatory_hint(wiphy, rtwdev->efuse.country_code);
		if (ret) {
			rtw_warn(rtwdev,
				 "failed to hint regulatory: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static bool rtw_regd_mgmt_worldwide(struct rtw_dev *rtwdev,
				    struct rtw_regd *next_regd,
				    struct regulatory_request *request)
{
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	next_regd->state = RTW_REGD_STATE_WORLDWIDE;

	if (request->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !rtw_reg_is_ww(next_regd->regulatory)) {
		next_regd->state = RTW_REGD_STATE_SETTING;
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	}

	return true;
}

static bool rtw_regd_mgmt_programmed(struct rtw_dev *rtwdev,
				     struct rtw_regd *next_regd,
				     struct regulatory_request *request)
{
	if (request->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    rtw_reg_match(next_regd->regulatory, rtwdev->efuse.country_code)) {
		next_regd->state = RTW_REGD_STATE_PROGRAMMED;
		return true;
	}

	return false;
}

static bool rtw_regd_mgmt_setting(struct rtw_dev *rtwdev,
				  struct rtw_regd *next_regd,
				  struct regulatory_request *request)
{
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	if (request->initiator != NL80211_REGDOM_SET_BY_USER)
		return false;

	next_regd->state = RTW_REGD_STATE_SETTING;

	if (rtw_reg_is_ww(next_regd->regulatory)) {
		next_regd->state = RTW_REGD_STATE_WORLDWIDE;
		wiphy->regulatory_flags &= ~REGULATORY_COUNTRY_IE_IGNORE;
	}

	return true;
}

static bool (*const rtw_regd_handler[RTW_REGD_STATE_NR])
	(struct rtw_dev *, struct rtw_regd *, struct regulatory_request *) = {
	[RTW_REGD_STATE_WORLDWIDE] = rtw_regd_mgmt_worldwide,
	[RTW_REGD_STATE_PROGRAMMED] = rtw_regd_mgmt_programmed,
	[RTW_REGD_STATE_SETTING] = rtw_regd_mgmt_setting,
};

static bool rtw_regd_state_hdl(struct rtw_dev *rtwdev,
			       struct rtw_regd *next_regd,
			       struct regulatory_request *request)
{
	next_regd->regulatory = rtw_reg_find_by_name(request->alpha2);
	next_regd->dfs_region = request->dfs_region;
	return rtw_regd_handler[rtwdev->regd.state](rtwdev, next_regd, request);
}

static
void rtw_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_regd next_regd = {0};
	bool hdl;

	hdl = rtw_regd_state_hdl(rtwdev, &next_regd, request);
	if (!hdl) {
		rtw_dbg(rtwdev, RTW_DBG_REGD,
			"regd state %d: ignore request %c%c of initiator %d\n",
			rtwdev->regd.state,
			request->alpha2[0],
			request->alpha2[1],
			request->initiator);
		return;
	}

	rtw_dbg(rtwdev, RTW_DBG_REGD, "regd state: %d -> %d\n",
		rtwdev->regd.state, next_regd.state);

	mutex_lock(&rtwdev->mutex);
	rtwdev->regd = next_regd;
	rtw_dbg_regd_dump(rtwdev, "get alpha2 %c%c from initiator %d: ",
			  request->alpha2[0],
			  request->alpha2[1],
			  request->initiator);

	rtw_phy_adaptivity_set_mode(rtwdev);
	rtw_phy_set_tx_power_level(rtwdev, hal->current_channel);
	mutex_unlock(&rtwdev->mutex);
}

u8 rtw_regd_get(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 band = hal->current_band_type;

	return band == RTW_BAND_2G ?
	       rtwdev->regd.regulatory->txpwr_regd_2g :
	       rtwdev->regd.regulatory->txpwr_regd_5g;
}
EXPORT_SYMBOL(rtw_regd_get);

bool rtw_regd_srrc(struct rtw_dev *rtwdev)
{
	struct rtw_regd *regd = &rtwdev->regd;

	return rtw_reg_match(regd->regulatory, "CN");
}
EXPORT_SYMBOL(rtw_regd_srrc);

struct rtw_regd_alternative_t {
	bool set;
	u8 alt;
};

#define DECL_REGD_ALT(_regd, _regd_alt) \
	[(_regd)] = {.set = true, .alt = (_regd_alt)}

static const struct rtw_regd_alternative_t
rtw_regd_alt[RTW_REGD_MAX] = {
	DECL_REGD_ALT(RTW_REGD_IC, RTW_REGD_FCC),
	DECL_REGD_ALT(RTW_REGD_KCC, RTW_REGD_ETSI),
	DECL_REGD_ALT(RTW_REGD_ACMA, RTW_REGD_ETSI),
	DECL_REGD_ALT(RTW_REGD_CHILE, RTW_REGD_FCC),
	DECL_REGD_ALT(RTW_REGD_UKRAINE, RTW_REGD_ETSI),
	DECL_REGD_ALT(RTW_REGD_MEXICO, RTW_REGD_FCC),
	DECL_REGD_ALT(RTW_REGD_CN, RTW_REGD_ETSI),
	DECL_REGD_ALT(RTW_REGD_QATAR, RTW_REGD_ETSI),
	DECL_REGD_ALT(RTW_REGD_UK, RTW_REGD_ETSI),
};

bool rtw_regd_has_alt(u8 regd, u8 *regd_alt)
{
	if (!rtw_regd_alt[regd].set)
		return false;

	*regd_alt = rtw_regd_alt[regd].alt;
	return true;
}
