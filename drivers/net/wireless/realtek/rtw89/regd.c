// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "acpi.h"
#include "debug.h"
#include "ps.h"
#include "util.h"

static
void rtw89_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request);

#define COUNTRY_REGD(_alpha2, _rule_2ghz, _rule_5ghz, _rule_6ghz, _fmap) \
	{							\
		.alpha2 = _alpha2,				\
		.txpwr_regd[RTW89_BAND_2G] = _rule_2ghz,	\
		.txpwr_regd[RTW89_BAND_5G] = _rule_5ghz,	\
		.txpwr_regd[RTW89_BAND_6G] = _rule_6ghz,	\
		.func_bitmap = { _fmap, },	\
	}

static_assert(BITS_PER_TYPE(unsigned long) >= NUM_OF_RTW89_REGD_FUNC);

static const struct rtw89_regd rtw89_ww_regd =
	COUNTRY_REGD("00", RTW89_WW, RTW89_WW, RTW89_WW, 0x0);

static const struct rtw89_regd rtw89_regd_map[] = {
	COUNTRY_REGD("AR", RTW89_MEXICO, RTW89_MEXICO, RTW89_FCC, 0x0),
	COUNTRY_REGD("BO", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("BR", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("CL", RTW89_CHILE, RTW89_CHILE, RTW89_CHILE, 0x0),
	COUNTRY_REGD("CO", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("CR", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("EC", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("SV", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("GT", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("HN", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("MX", RTW89_MEXICO, RTW89_MEXICO, RTW89_FCC, 0x0),
	COUNTRY_REGD("NI", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("PA", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("PY", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("PE", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("US", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x1),
	COUNTRY_REGD("UY", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("VE", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("PR", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("DO", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("AT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("BE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("CY", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("CZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("DK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("EE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("FI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("FR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("DE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("GR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("HU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("IS", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("IE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("IT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("LV", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("LI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("LT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("LU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("MT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("MC", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("NL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("NO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("PL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("PT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("SK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("SI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("ES", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("SE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("CH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("GB", RTW89_UK, RTW89_UK, RTW89_UK, 0x0),
	COUNTRY_REGD("AL", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("BH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("BA", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("HR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("EG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("GH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("IQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("IL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("JO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("KZ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("KW", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("KG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LB", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LS", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MK", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("NA", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("OM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("QA", RTW89_QATAR, RTW89_QATAR, RTW89_QATAR, 0x0),
	COUNTRY_REGD("RO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x2),
	COUNTRY_REGD("RU", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("SN", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("RS", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("ME", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ZA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("UA", RTW89_UKRAINE, RTW89_UKRAINE, RTW89_UKRAINE, 0x0),
	COUNTRY_REGD("AE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("YE", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ZW", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("BD", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KH", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("CN", RTW89_CN, RTW89_CN, RTW89_CN, 0x0),
	COUNTRY_REGD("HK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("IN", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ID", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KR", RTW89_KCC, RTW89_KCC, RTW89_KCC, 0x1),
	COUNTRY_REGD("MY", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("PK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("PH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("SG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LK", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TW", RTW89_FCC, RTW89_FCC, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TH", RTW89_THAILAND, RTW89_THAILAND, RTW89_THAILAND, 0x0),
	COUNTRY_REGD("VN", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AU", RTW89_ACMA, RTW89_ACMA, RTW89_ACMA, 0x0),
	COUNTRY_REGD("NZ", RTW89_ACMA, RTW89_ACMA, RTW89_ACMA, 0x0),
	COUNTRY_REGD("PG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("CA", RTW89_IC, RTW89_IC, RTW89_IC, 0x1),
	COUNTRY_REGD("JP", RTW89_MKK, RTW89_MKK, RTW89_MKK, 0x0),
	COUNTRY_REGD("JM", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("AN", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("TT", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("TN", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("DZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("AS", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("AD", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("AQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("AG", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("AM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("AW", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("BS", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("BB", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("BY", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BZ", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("BJ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BM", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("BT", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BW", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("BV", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("IO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("VG", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("BN", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BF", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("BI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("CM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("CV", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KY", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("CF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TD", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("CX", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("CC", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("KM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("CG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("CD", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("CK", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("CI", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("DJ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("DM", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("GQ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("ER", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ET", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("FK", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("FO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("FJ", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("GF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("PF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GA", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("GE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("GI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("GL", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GD", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("GP", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GU", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("GG", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GN", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("GW", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GY", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("HT", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("HM", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("VA", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("IM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("JE", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KI", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("XK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("LY", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MW", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MV", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ML", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MH", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("MQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MR", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("MU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("YT", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("FM", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("MD", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MN", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("MS", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NR", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NP", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NC", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NE", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("NU", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("NF", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("MP", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("PW", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("RE", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("RW", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SH", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("KN", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("LC", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("MF", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("SX", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("PM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("VC", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("WS", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("SM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ST", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SC", RTW89_FCC, RTW89_FCC, RTW89_NA, 0x0),
	COUNTRY_REGD("SL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("SB", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("GS", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SR", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("SJ", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TJ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("TK", RTW89_ACMA, RTW89_ACMA, RTW89_NA, 0x0),
	COUNTRY_REGD("TO", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TC", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("TV", RTW89_ETSI, RTW89_NA, RTW89_NA, 0x0),
	COUNTRY_REGD("UG", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("VI", RTW89_FCC, RTW89_FCC, RTW89_FCC, 0x0),
	COUNTRY_REGD("UZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI, 0x0),
	COUNTRY_REGD("VU", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("WF", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("EH", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("ZM", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("CU", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("IR", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("SY", RTW89_ETSI, RTW89_NA, RTW89_NA, 0x0),
	COUNTRY_REGD("SD", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
	COUNTRY_REGD("PS", RTW89_ETSI, RTW89_ETSI, RTW89_NA, 0x0),
};

static const char rtw89_alpha2_list_eu[][3] = {
	"AT",
	"BE",
	"CY",
	"CZ",
	"DK",
	"EE",
	"FI",
	"FR",
	"DE",
	"GR",
	"HU",
	"IS",
	"IE",
	"IT",
	"LV",
	"LI",
	"LT",
	"LU",
	"MT",
	"MC",
	"NL",
	"NO",
	"PL",
	"PT",
	"SK",
	"SI",
	"ES",
	"SE",
	"CH",
	"BG",
	"HR",
	"RO",
};

static const struct rtw89_regd *rtw89_regd_find_reg_by_name(struct rtw89_dev *rtwdev,
							    const char *alpha2)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd_ctrl *regd_ctrl = &regulatory->ctrl;
	u32 i;

	for (i = 0; i < regd_ctrl->nr; i++) {
		if (!memcmp(regd_ctrl->map[i].alpha2, alpha2, 2))
			return &regd_ctrl->map[i];
	}

	return &rtw89_ww_regd;
}

static bool rtw89_regd_is_ww(const struct rtw89_regd *regd)
{
	return regd == &rtw89_ww_regd;
}

static u8 rtw89_regd_get_index(struct rtw89_dev *rtwdev, const struct rtw89_regd *regd)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd_ctrl *regd_ctrl = &regulatory->ctrl;

	BUILD_BUG_ON(ARRAY_SIZE(rtw89_regd_map) > RTW89_REGD_MAX_COUNTRY_NUM);

	if (rtw89_regd_is_ww(regd))
		return RTW89_REGD_MAX_COUNTRY_NUM;

	return regd - regd_ctrl->map;
}

static u8 rtw89_regd_get_index_by_name(struct rtw89_dev *rtwdev, const char *alpha2)
{
	const struct rtw89_regd *regd;

	regd = rtw89_regd_find_reg_by_name(rtwdev, alpha2);
	return rtw89_regd_get_index(rtwdev, regd);
}

#define rtw89_debug_regd(_dev, _regd, _desc, _argv...) \
do { \
	typeof(_regd) __r = _regd; \
	rtw89_debug(_dev, RTW89_DBG_REGD, _desc \
		    ": %c%c: mapping txregd to {2g: %d, 5g: %d, 6g: %d}\n", \
		    ##_argv, __r->alpha2[0], __r->alpha2[1], \
		    __r->txpwr_regd[RTW89_BAND_2G], \
		    __r->txpwr_regd[RTW89_BAND_5G], \
		    __r->txpwr_regd[RTW89_BAND_6G]); \
} while (0)

static void rtw89_regd_setup_unii4(struct rtw89_dev *rtwdev,
				   struct wiphy *wiphy)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd_ctrl *regd_ctrl = &regulatory->ctrl;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct ieee80211_supported_band *sband;
	struct rtw89_acpi_dsm_result res = {};
	bool enable_by_fcc;
	bool enable_by_ic;
	int ret;
	u8 val;
	int i;

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	if (!chip->support_unii4) {
		sband->n_channels -= RTW89_5GHZ_UNII4_CHANNEL_NUM;
		return;
	}

	bitmap_fill(regulatory->block_unii4, RTW89_REGD_MAX_COUNTRY_NUM);

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_UNII4_SUP, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval unii 4: %d\n", ret);
		enable_by_fcc = true;
		enable_by_ic = false;
		goto bottom;
	}

	val = res.u.value;
	enable_by_fcc = u8_get_bits(val, RTW89_ACPI_CONF_UNII4_FCC);
	enable_by_ic = u8_get_bits(val, RTW89_ACPI_CONF_UNII4_IC);

	rtw89_debug(rtwdev, RTW89_DBG_REGD,
		    "acpi: eval if allow unii-4: 0x%x\n", val);

bottom:
	for (i = 0; i < regd_ctrl->nr; i++) {
		const struct rtw89_regd *regd = &regd_ctrl->map[i];

		switch (regd->txpwr_regd[RTW89_BAND_5G]) {
		case RTW89_FCC:
			if (enable_by_fcc)
				clear_bit(i, regulatory->block_unii4);
			break;
		case RTW89_IC:
			if (enable_by_ic)
				clear_bit(i, regulatory->block_unii4);
			break;
		default:
			break;
		}
	}
}

static void __rtw89_regd_setup_policy_6ghz(struct rtw89_dev *rtwdev, bool block,
					   const char *alpha2)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	u8 index;

	index = rtw89_regd_get_index_by_name(rtwdev, alpha2);
	if (index == RTW89_REGD_MAX_COUNTRY_NUM) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD, "%s: unknown alpha2 %c%c\n",
			    __func__, alpha2[0], alpha2[1]);
		return;
	}

	if (block)
		set_bit(index, regulatory->block_6ghz);
	else
		clear_bit(index, regulatory->block_6ghz);
}

static void rtw89_regd_setup_policy_6ghz(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_acpi_country_code *country;
	const struct rtw89_acpi_policy_6ghz *ptr;
	struct rtw89_acpi_dsm_result res = {};
	bool to_block;
	int i, j;
	int ret;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_6G_BP, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval policy 6ghz: %d\n", ret);
		return;
	}

	ptr = res.u.policy_6ghz;

	switch (ptr->policy_mode) {
	case RTW89_ACPI_POLICY_BLOCK:
		to_block = true;
		break;
	case RTW89_ACPI_POLICY_ALLOW:
		to_block = false;
		/* only below list is allowed; block all first */
		bitmap_fill(regulatory->block_6ghz, RTW89_REGD_MAX_COUNTRY_NUM);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "%s: unknown policy mode: %d\n", __func__,
			    ptr->policy_mode);
		goto out;
	}

	for (i = 0; i < ptr->country_count; i++) {
		country = &ptr->country_list[i];
		if (memcmp("EU", country->alpha2, 2) != 0) {
			__rtw89_regd_setup_policy_6ghz(rtwdev, to_block,
						       country->alpha2);
			continue;
		}

		for (j = 0; j < ARRAY_SIZE(rtw89_alpha2_list_eu); j++)
			__rtw89_regd_setup_policy_6ghz(rtwdev, to_block,
						       rtw89_alpha2_list_eu[j]);
	}

out:
	kfree(ptr);
}

static void rtw89_regd_setup_policy_6ghz_sp(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd_ctrl *regd_ctrl = &regulatory->ctrl;
	const struct rtw89_acpi_policy_6ghz_sp *ptr;
	struct rtw89_acpi_dsm_result res = {};
	bool enable_by_us;
	int ret;
	int i;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_6GHZ_SP_SUP, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval policy 6ghz-sp: %d\n", ret);
		return;
	}

	ptr = res.u.policy_6ghz_sp;

	switch (ptr->override) {
	default:
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "%s: unknown override case: %d\n", __func__,
			    ptr->override);
		fallthrough;
	case 0:
		goto out;
	case 1:
		break;
	}

	bitmap_fill(regulatory->block_6ghz_sp, RTW89_REGD_MAX_COUNTRY_NUM);

	enable_by_us = u8_get_bits(ptr->conf, RTW89_ACPI_CONF_6GHZ_SP_US);

	for (i = 0; i < regd_ctrl->nr; i++) {
		const struct rtw89_regd *tmp = &regd_ctrl->map[i];

		if (enable_by_us && memcmp(tmp->alpha2, "US", 2) == 0)
			clear_bit(i, regulatory->block_6ghz_sp);
	}

out:
	kfree(ptr);
}

static void rtw89_regd_setup_6ghz(struct rtw89_dev *rtwdev, struct wiphy *wiphy)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool chip_support_6ghz = chip->support_bands & BIT(NL80211_BAND_6GHZ);
	bool regd_allow_6ghz = chip_support_6ghz;
	struct ieee80211_supported_band *sband;
	struct rtw89_acpi_dsm_result res = {};
	int ret;
	u8 val;

	if (!chip_support_6ghz)
		goto bottom;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_6G_DIS, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval 6ghz: %d\n", ret);
		goto bottom;
	}

	val = res.u.value;

	rtw89_debug(rtwdev, RTW89_DBG_REGD,
		    "acpi: eval if disallow 6ghz: %d\n", val);

	switch (val) {
	case 0:
		regd_allow_6ghz = true;
		break;
	case 1:
		regd_allow_6ghz = false;
		break;
	default:
		break;
	}

bottom:
	rtw89_debug(rtwdev, RTW89_DBG_REGD, "regd: allow 6ghz: %d\n",
		    regd_allow_6ghz);

	if (regd_allow_6ghz) {
		rtw89_regd_setup_policy_6ghz(rtwdev);
		rtw89_regd_setup_policy_6ghz_sp(rtwdev);
		return;
	}

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	wiphy->bands[NL80211_BAND_6GHZ] = NULL;
	kfree((__force void *)sband->iftype_data);
	kfree(sband);
}

#define RTW89_DEF_REGD_STR(regd) \
	[RTW89_ ## regd] = #regd

static const char * const rtw89_regd_string[] = {
	RTW89_DEF_REGD_STR(WW),
	RTW89_DEF_REGD_STR(ETSI),
	RTW89_DEF_REGD_STR(FCC),
	RTW89_DEF_REGD_STR(MKK),
	RTW89_DEF_REGD_STR(NA),
	RTW89_DEF_REGD_STR(IC),
	RTW89_DEF_REGD_STR(KCC),
	RTW89_DEF_REGD_STR(ACMA),
	RTW89_DEF_REGD_STR(NCC),
	RTW89_DEF_REGD_STR(MEXICO),
	RTW89_DEF_REGD_STR(CHILE),
	RTW89_DEF_REGD_STR(UKRAINE),
	RTW89_DEF_REGD_STR(CN),
	RTW89_DEF_REGD_STR(QATAR),
	RTW89_DEF_REGD_STR(UK),
	RTW89_DEF_REGD_STR(THAILAND),
};

static_assert(ARRAY_SIZE(rtw89_regd_string) == RTW89_REGD_NUM);

const char *rtw89_regd_get_string(enum rtw89_regulation_type regd)
{
	if (regd < 0 || regd >= RTW89_REGD_NUM)
		return "(unknown)";

	return rtw89_regd_string[regd];
}

int rtw89_regd_setup(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_regd_data *regd_data = elm_info->regd;
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	if (regd_data) {
		regulatory->ctrl.nr = regd_data->nr;
		regulatory->ctrl.map = regd_data->map;
	} else {
		regulatory->ctrl.nr = ARRAY_SIZE(rtw89_regd_map);
		regulatory->ctrl.map = rtw89_regd_map;
	}

	regulatory->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;

	if (!wiphy)
		return -EINVAL;

	rtw89_regd_setup_unii4(rtwdev, wiphy);
	rtw89_regd_setup_6ghz(rtwdev, wiphy);

	wiphy->reg_notifier = rtw89_regd_notifier;
	return 0;
}

int rtw89_regd_init_hint(struct rtw89_dev *rtwdev)
{
	const struct rtw89_regd *chip_regd;
	struct wiphy *wiphy = rtwdev->hw->wiphy;
	int ret;

	if (!wiphy)
		return -EINVAL;

	chip_regd = rtw89_regd_find_reg_by_name(rtwdev, rtwdev->efuse.country_code);
	if (!rtw89_regd_is_ww(chip_regd)) {
		rtwdev->regulatory.regd = chip_regd;
		/* Ignore country ie if there is a country domain programmed in chip */
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
		wiphy->regulatory_flags |= REGULATORY_STRICT_REG;

		ret = regulatory_hint(rtwdev->hw->wiphy,
				      rtwdev->regulatory.regd->alpha2);
		if (ret)
			rtw89_warn(rtwdev, "failed to hint regulatory:%d\n", ret);

		rtw89_debug_regd(rtwdev, chip_regd, "efuse country code");
		return 0;
	}

	rtw89_debug_regd(rtwdev, rtwdev->regulatory.regd,
			 "worldwide roaming chip, follow the setting of stack");
	return 0;
}

static void rtw89_regd_apply_policy_unii4(struct rtw89_dev *rtwdev,
					  struct wiphy *wiphy)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_regd *regd = regulatory->regd;
	struct ieee80211_supported_band *sband;
	u8 index;
	int i;

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	if (!chip->support_unii4)
		return;

	index = rtw89_regd_get_index(rtwdev, regd);
	if (index != RTW89_REGD_MAX_COUNTRY_NUM &&
	    !test_bit(index, regulatory->block_unii4))
		return;

	rtw89_debug(rtwdev, RTW89_DBG_REGD, "%c%c unii-4 is blocked by policy\n",
		    regd->alpha2[0], regd->alpha2[1]);

	for (i = RTW89_5GHZ_UNII4_START_INDEX; i < sband->n_channels; i++)
		sband->channels[i].flags |= IEEE80211_CHAN_DISABLED;
}

static bool regd_is_6ghz_blocked(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *regd = regulatory->regd;
	u8 index;

	index = rtw89_regd_get_index(rtwdev, regd);
	if (index != RTW89_REGD_MAX_COUNTRY_NUM &&
	    !test_bit(index, regulatory->block_6ghz))
		return false;

	rtw89_debug(rtwdev, RTW89_DBG_REGD, "%c%c 6 GHz is blocked by policy\n",
		    regd->alpha2[0], regd->alpha2[1]);
	return true;
}

static bool regd_is_6ghz_not_applicable(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *regd = regulatory->regd;

	if (regd->txpwr_regd[RTW89_BAND_6G] != RTW89_NA)
		return false;

	rtw89_debug(rtwdev, RTW89_DBG_REGD, "%c%c 6 GHz is N/A in regd map\n",
		    regd->alpha2[0], regd->alpha2[1]);
	return true;
}

static void rtw89_regd_apply_policy_6ghz(struct rtw89_dev *rtwdev,
					 struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	int i;

	if (!regd_is_6ghz_blocked(rtwdev) &&
	    !regd_is_6ghz_not_applicable(rtwdev))
		return;

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++)
		sband->channels[i].flags |= IEEE80211_CHAN_DISABLED;
}

static void rtw89_regd_apply_policy_tas(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *regd = regulatory->regd;
	struct rtw89_tas_info *tas = &rtwdev->tas;

	if (!tas->enable)
		return;

	tas->block_regd = !test_bit(RTW89_REGD_FUNC_TAS, regd->func_bitmap);
}

static void rtw89_regd_apply_policy_ant_gain(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_regd *regd = regulatory->regd;

	if (!chip->support_ant_gain)
		return;

	ant_gain->block_country = !test_bit(RTW89_REGD_FUNC_DAG, regd->func_bitmap);
}

static void rtw89_regd_notifier_apply(struct rtw89_dev *rtwdev,
				      struct wiphy *wiphy,
				      struct regulatory_request *request)
{
	rtwdev->regulatory.regd = rtw89_regd_find_reg_by_name(rtwdev, request->alpha2);
	/* This notification might be set from the system of distros,
	 * and it does not expect the regulatory will be modified by
	 * connecting to an AP (i.e. country ie).
	 */
	if (request->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !rtw89_regd_is_ww(rtwdev->regulatory.regd))
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	else
		wiphy->regulatory_flags &= ~REGULATORY_COUNTRY_IE_IGNORE;

	rtw89_regd_apply_policy_unii4(rtwdev, wiphy);
	rtw89_regd_apply_policy_6ghz(rtwdev, wiphy);
	rtw89_regd_apply_policy_tas(rtwdev);
	rtw89_regd_apply_policy_ant_gain(rtwdev);
}

static
void rtw89_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct rtw89_dev *rtwdev = hw->priv;

	wiphy_lock(wiphy);
	rtw89_leave_ps_mode(rtwdev);

	if (wiphy->regd) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "There is a country domain programmed in chip, ignore notifications\n");
		goto exit;
	}
	rtw89_regd_notifier_apply(rtwdev, wiphy, request);
	rtw89_debug_regd(rtwdev, rtwdev->regulatory.regd,
			 "get from initiator %d, alpha2",
			 request->initiator);

	rtw89_core_set_chip_txpwr(rtwdev);

exit:
	wiphy_unlock(wiphy);
}

/* Maximum Transmit Power field (@raw) can be EIRP or PSD.
 * Both units are 0.5 dB-based. Return a constraint in dB.
 */
static s8 tpe_get_constraint(s8 raw)
{
	const u8 hw_deviation = 3; /* unit: 0.5 dB */
	const u8 antenna_gain = 10; /* unit: 0.5 dB */
	const u8 array_gain = 6; /* unit: 0.5 dB */
	const u8 offset = hw_deviation + antenna_gain + array_gain;

	return (raw - offset) / 2;
}

static void tpe_intersect_constraint(struct rtw89_reg_6ghz_tpe *tpe, s8 cstr)
{
	if (tpe->valid) {
		tpe->constraint = min(tpe->constraint, cstr);
		return;
	}

	tpe->constraint = cstr;
	tpe->valid = true;
}

static void tpe_deal_with_eirp(struct rtw89_reg_6ghz_tpe *tpe,
			       const struct ieee80211_parsed_tpe_eirp *eirp)
{
	unsigned int i;
	s8 cstr;

	if (!eirp->valid)
		return;

	for (i = 0; i < eirp->count; i++) {
		cstr = tpe_get_constraint(eirp->power[i]);
		tpe_intersect_constraint(tpe, cstr);
	}
}

static s8 tpe_convert_psd_to_eirp(s8 psd)
{
	static const unsigned int mlog20 = 1301;

	return psd + 10 * mlog20 / 1000;
}

static void tpe_deal_with_psd(struct rtw89_reg_6ghz_tpe *tpe,
			      const struct ieee80211_parsed_tpe_psd *psd)
{
	unsigned int i;
	s8 cstr_psd;
	s8 cstr;

	if (!psd->valid)
		return;

	for (i = 0; i < psd->count; i++) {
		cstr_psd = tpe_get_constraint(psd->power[i]);
		cstr = tpe_convert_psd_to_eirp(cstr_psd);
		tpe_intersect_constraint(tpe, cstr);
	}
}

static void rtw89_calculate_tpe(struct rtw89_dev *rtwdev,
				struct rtw89_reg_6ghz_tpe *result_tpe,
				const struct ieee80211_parsed_tpe *parsed_tpe)
{
	static const u8 category = IEEE80211_TPE_CAT_6GHZ_DEFAULT;

	tpe_deal_with_eirp(result_tpe, &parsed_tpe->max_local[category]);
	tpe_deal_with_eirp(result_tpe, &parsed_tpe->max_reg_client[category]);
	tpe_deal_with_psd(result_tpe, &parsed_tpe->psd_local[category]);
	tpe_deal_with_psd(result_tpe, &parsed_tpe->psd_reg_client[category]);
}

static bool __rtw89_reg_6ghz_tpe_recalc(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	struct rtw89_reg_6ghz_tpe new = {};
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	bool changed = false;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		const struct rtw89_reg_6ghz_tpe *tmp;
		const struct rtw89_chan *chan;

		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
			chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
			if (chan->band_type != RTW89_BAND_6G)
				continue;

			tmp = &rtwvif_link->reg_6ghz_tpe;
			if (!tmp->valid)
				continue;

			tpe_intersect_constraint(&new, tmp->constraint);
		}
	}

	if (memcmp(&regulatory->reg_6ghz_tpe, &new,
		   sizeof(regulatory->reg_6ghz_tpe)) != 0)
		changed = true;

	if (changed) {
		if (new.valid)
			rtw89_debug(rtwdev, RTW89_DBG_REGD,
				    "recalc 6 GHz reg TPE to %d dBm\n",
				    new.constraint);
		else
			rtw89_debug(rtwdev, RTW89_DBG_REGD,
				    "recalc 6 GHz reg TPE to none\n");

		regulatory->reg_6ghz_tpe = new;
	}

	return changed;
}

static int rtw89_reg_6ghz_tpe_recalc(struct rtw89_dev *rtwdev,
				     struct rtw89_vif_link *rtwvif_link, bool active,
				     unsigned int *changed)
{
	struct rtw89_reg_6ghz_tpe *tpe = &rtwvif_link->reg_6ghz_tpe;
	struct ieee80211_bss_conf *bss_conf;

	memset(tpe, 0, sizeof(*tpe));

	if (!active || rtwvif_link->reg_6ghz_power != RTW89_REG_6GHZ_POWER_STD)
		goto bottom;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
	rtw89_calculate_tpe(rtwdev, tpe, &bss_conf->tpe);

	rcu_read_unlock();

	if (!tpe->valid)
		goto bottom;

	if (tpe->constraint < RTW89_MIN_VALID_POWER_CONSTRAINT) {
		rtw89_err(rtwdev,
			  "%s: constraint %d dBm is less than min valid val\n",
			  __func__, tpe->constraint);

		tpe->valid = false;
		return -EINVAL;
	}

bottom:
	*changed += __rtw89_reg_6ghz_tpe_recalc(rtwdev);
	return 0;
}

static bool __rtw89_reg_6ghz_power_recalc(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *regd = regulatory->regd;
	enum rtw89_reg_6ghz_power sel;
	const struct rtw89_chan *chan;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	int count = 0;
	u8 index;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
			chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
			if (chan->band_type != RTW89_BAND_6G)
				continue;

			if (count != 0 && rtwvif_link->reg_6ghz_power == sel)
				continue;

			sel = rtwvif_link->reg_6ghz_power;
			count++;
		}
	}

	if (count != 1)
		sel = RTW89_REG_6GHZ_POWER_DFLT;

	if (sel == RTW89_REG_6GHZ_POWER_STD) {
		index = rtw89_regd_get_index(rtwdev, regd);
		if (index == RTW89_REGD_MAX_COUNTRY_NUM ||
		    test_bit(index, regulatory->block_6ghz_sp)) {
			rtw89_debug(rtwdev, RTW89_DBG_REGD,
				    "%c%c 6 GHz SP is blocked by policy\n",
				    regd->alpha2[0], regd->alpha2[1]);
			sel = RTW89_REG_6GHZ_POWER_DFLT;
		}
	}

	if (regulatory->reg_6ghz_power == sel)
		return false;

	rtw89_debug(rtwdev, RTW89_DBG_REGD,
		    "recalc 6 GHz reg power type to %d\n", sel);

	regulatory->reg_6ghz_power = sel;
	return true;
}

static int rtw89_reg_6ghz_power_recalc(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link, bool active,
				       unsigned int *changed)
{
	struct ieee80211_bss_conf *bss_conf;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	if (active) {
		switch (bss_conf->power_type) {
		case IEEE80211_REG_VLP_AP:
			rtwvif_link->reg_6ghz_power = RTW89_REG_6GHZ_POWER_VLP;
			break;
		case IEEE80211_REG_LPI_AP:
			rtwvif_link->reg_6ghz_power = RTW89_REG_6GHZ_POWER_LPI;
			break;
		case IEEE80211_REG_SP_AP:
			rtwvif_link->reg_6ghz_power = RTW89_REG_6GHZ_POWER_STD;
			break;
		default:
			rtwvif_link->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;
			break;
		}
	} else {
		rtwvif_link->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;
	}

	rcu_read_unlock();

	*changed += __rtw89_reg_6ghz_power_recalc(rtwdev);
	return 0;
}

int rtw89_reg_6ghz_recalc(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			  bool active)
{
	unsigned int changed = 0;
	int ret;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	/* The result of reg_6ghz_tpe may depend on reg_6ghz_power type,
	 * so must do reg_6ghz_tpe_recalc() after reg_6ghz_power_recalc().
	 */

	ret = rtw89_reg_6ghz_power_recalc(rtwdev, rtwvif_link, active, &changed);
	if (ret)
		return ret;

	ret = rtw89_reg_6ghz_tpe_recalc(rtwdev, rtwvif_link, active, &changed);
	if (ret)
		return ret;

	if (changed)
		rtw89_core_set_chip_txpwr(rtwdev);

	return 0;
}
