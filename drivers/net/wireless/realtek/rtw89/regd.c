// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "acpi.h"
#include "debug.h"
#include "ps.h"
#include "util.h"

#define COUNTRY_REGD(_alpha2, _txpwr_regd...) \
	{.alpha2 = (_alpha2), \
	 .txpwr_regd = {_txpwr_regd}, \
	}

static const struct rtw89_regd rtw89_ww_regd =
	COUNTRY_REGD("00", RTW89_WW, RTW89_WW, RTW89_WW);

static const struct rtw89_regd rtw89_regd_map[] = {
	COUNTRY_REGD("AR", RTW89_MEXICO, RTW89_MEXICO, RTW89_FCC),
	COUNTRY_REGD("BO", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BR", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("CL", RTW89_CHILE, RTW89_CHILE, RTW89_CHILE),
	COUNTRY_REGD("CO", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("CR", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("EC", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("SV", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("GT", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("HN", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("MX", RTW89_MEXICO, RTW89_MEXICO, RTW89_FCC),
	COUNTRY_REGD("NI", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PA", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PY", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PE", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("US", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("UY", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("VE", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PR", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("DO", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("AT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("BE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CY", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("DK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("EE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("FI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("FR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("DE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("GR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("HU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("IS", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("IE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("IT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LV", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MC", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("NL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("NO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("PL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("PT", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("SK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("SI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("ES", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("SE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("GB", RTW89_UK, RTW89_UK, RTW89_UK),
	COUNTRY_REGD("AL", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("BH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("BA", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("HR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("EG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GH", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("IQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("IL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("JO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("KZ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("KW", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("KG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LB", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LS", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MK", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MZ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NA", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("OM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("QA", RTW89_QATAR, RTW89_QATAR, RTW89_QATAR),
	COUNTRY_REGD("RO", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("RU", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("SN", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("RS", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("ME", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ZA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("TR", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("UA", RTW89_UKRAINE, RTW89_UKRAINE, RTW89_UKRAINE),
	COUNTRY_REGD("AE", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("YE", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ZW", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BD", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KH", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("CN", RTW89_CN, RTW89_CN, RTW89_CN),
	COUNTRY_REGD("HK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("IN", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ID", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KR", RTW89_KCC, RTW89_KCC, RTW89_KCC),
	COUNTRY_REGD("MY", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("PK", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("PH", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("LK", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TW", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("TH", RTW89_ETSI, RTW89_ETSI, RTW89_THAILAND),
	COUNTRY_REGD("VN", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AU", RTW89_ACMA, RTW89_ACMA, RTW89_ACMA),
	COUNTRY_REGD("NZ", RTW89_ACMA, RTW89_ACMA, RTW89_ACMA),
	COUNTRY_REGD("PG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CA", RTW89_IC, RTW89_IC, RTW89_IC),
	COUNTRY_REGD("JP", RTW89_MKK, RTW89_MKK, RTW89_MKK),
	COUNTRY_REGD("JM", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("AN", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("TT", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("TN", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("DZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("AS", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("AD", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("AQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("AG", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("AM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("AW", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BS", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BB", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BY", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BZ", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("BJ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BM", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BT", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BW", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("BV", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("IO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("VG", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("BN", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BF", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("BI", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("CV", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KY", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("CF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TD", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CX", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("CC", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("KM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("CG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("CD", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("CK", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("CI", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("DJ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("DM", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("GQ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("ER", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ET", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("FK", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("FO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("FJ", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("GF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("PF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GA", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GM", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("GE", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GI", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GL", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GD", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("GP", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GU", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("GG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GN", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("GW", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GY", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("HT", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("HM", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("VA", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("IM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("JE", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KI", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("XK", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LA", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("LR", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("LY", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MW", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MV", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ML", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MH", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("MQ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MR", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("MU", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("YT", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("FM", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("MD", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MN", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("MS", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NR", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NP", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NC", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NE", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("NU", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("NF", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("MP", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PW", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("RE", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("RW", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SH", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("KN", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("LC", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("MF", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("SX", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("PM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("VC", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("WS", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("SM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ST", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SC", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("SL", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("SB", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("GS", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SR", RTW89_FCC, RTW89_FCC, RTW89_FCC),
	COUNTRY_REGD("SJ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("SZ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TJ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("TZ", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TG", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("TK", RTW89_ACMA, RTW89_ACMA, RTW89_NA),
	COUNTRY_REGD("TO", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TC", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("TV", RTW89_ETSI, RTW89_NA, RTW89_NA),
	COUNTRY_REGD("UG", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("VI", RTW89_FCC, RTW89_FCC, RTW89_NA),
	COUNTRY_REGD("UZ", RTW89_ETSI, RTW89_ETSI, RTW89_ETSI),
	COUNTRY_REGD("VU", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("WF", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("EH", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("ZM", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("IR", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
	COUNTRY_REGD("PS", RTW89_ETSI, RTW89_ETSI, RTW89_NA),
};

static const struct rtw89_regd *rtw89_regd_find_reg_by_name(char *alpha2)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(rtw89_regd_map); i++) {
		if (!memcmp(rtw89_regd_map[i].alpha2, alpha2, 2))
			return &rtw89_regd_map[i];
	}

	return &rtw89_ww_regd;
}

static bool rtw89_regd_is_ww(const struct rtw89_regd *regd)
{
	return regd == &rtw89_ww_regd;
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
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool regd_allow_unii_4 = chip->support_unii4;
	struct ieee80211_supported_band *sband;
	int ret;
	u8 val;

	if (!chip->support_unii4)
		goto bottom;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_59G_EN, &val);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval unii 4: %d\n", ret);
		goto bottom;
	}

	rtw89_debug(rtwdev, RTW89_DBG_REGD,
		    "acpi: eval if allow unii 4: %d\n", val);

	switch (val) {
	case 0:
		regd_allow_unii_4 = false;
		break;
	case 1:
		regd_allow_unii_4 = true;
		break;
	default:
		break;
	}

bottom:
	rtw89_debug(rtwdev, RTW89_DBG_REGD, "regd: allow unii 4: %d\n",
		    regd_allow_unii_4);

	if (regd_allow_unii_4)
		return;

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	sband->n_channels -= 3;
}

static void rtw89_regd_setup_6ghz(struct rtw89_dev *rtwdev, struct wiphy *wiphy)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool chip_support_6ghz = chip->support_bands & BIT(NL80211_BAND_6GHZ);
	bool regd_allow_6ghz = chip_support_6ghz;
	struct ieee80211_supported_band *sband;
	int ret;
	u8 val;

	if (!chip_support_6ghz)
		goto bottom;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_6G_DIS, &val);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_REGD,
			    "acpi: cannot eval 6ghz: %d\n", ret);
		goto bottom;
	}

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

	if (regd_allow_6ghz)
		return;

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	wiphy->bands[NL80211_BAND_6GHZ] = NULL;
	kfree((__force void *)sband->iftype_data);
	kfree(sband);
}

int rtw89_regd_setup(struct rtw89_dev *rtwdev)
{
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	if (!wiphy)
		return -EINVAL;

	rtw89_regd_setup_unii4(rtwdev, wiphy);
	rtw89_regd_setup_6ghz(rtwdev, wiphy);

	wiphy->reg_notifier = rtw89_regd_notifier;
	return 0;
}

int rtw89_regd_init(struct rtw89_dev *rtwdev,
		    void (*reg_notifier)(struct wiphy *wiphy,
					 struct regulatory_request *request))
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *chip_regd;
	struct wiphy *wiphy = rtwdev->hw->wiphy;
	int ret;

	regulatory->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;

	if (!wiphy)
		return -EINVAL;

	chip_regd = rtw89_regd_find_reg_by_name(rtwdev->efuse.country_code);
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

static void rtw89_regd_notifier_apply(struct rtw89_dev *rtwdev,
				      struct wiphy *wiphy,
				      struct regulatory_request *request)
{
	rtwdev->regulatory.regd = rtw89_regd_find_reg_by_name(request->alpha2);
	/* This notification might be set from the system of distros,
	 * and it does not expect the regulatory will be modified by
	 * connecting to an AP (i.e. country ie).
	 */
	if (request->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !rtw89_regd_is_ww(rtwdev->regulatory.regd))
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	else
		wiphy->regulatory_flags &= ~REGULATORY_COUNTRY_IE_IGNORE;
}

void rtw89_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
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
	mutex_unlock(&rtwdev->mutex);
}

static void __rtw89_reg_6ghz_power_recalc(struct rtw89_dev *rtwdev)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	enum rtw89_reg_6ghz_power sel;
	const struct rtw89_chan *chan;
	struct rtw89_vif *rtwvif;
	int count = 0;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		chan = rtw89_chan_get(rtwdev, rtwvif->sub_entity_idx);
		if (chan->band_type != RTW89_BAND_6G)
			continue;

		if (count != 0 && rtwvif->reg_6ghz_power == sel)
			continue;

		sel = rtwvif->reg_6ghz_power;
		count++;
	}

	if (count != 1)
		sel = RTW89_REG_6GHZ_POWER_DFLT;

	if (regulatory->reg_6ghz_power == sel)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_REGD,
		    "recalc 6 GHz reg power type to %d\n", sel);

	regulatory->reg_6ghz_power = sel;

	rtw89_core_set_chip_txpwr(rtwdev);
}

void rtw89_reg_6ghz_power_recalc(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool active)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);

	lockdep_assert_held(&rtwdev->mutex);

	if (active) {
		switch (vif->bss_conf.power_type) {
		case IEEE80211_REG_VLP_AP:
			rtwvif->reg_6ghz_power = RTW89_REG_6GHZ_POWER_VLP;
			break;
		case IEEE80211_REG_LPI_AP:
			rtwvif->reg_6ghz_power = RTW89_REG_6GHZ_POWER_LPI;
			break;
		case IEEE80211_REG_SP_AP:
			rtwvif->reg_6ghz_power = RTW89_REG_6GHZ_POWER_STD;
			break;
		default:
			rtwvif->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;
			break;
		}
	} else {
		rtwvif->reg_6ghz_power = RTW89_REG_6GHZ_POWER_DFLT;
	}

	__rtw89_reg_6ghz_power_recalc(rtwdev);
}
