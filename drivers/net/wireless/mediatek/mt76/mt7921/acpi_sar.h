/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2022 MediaTek Inc. */

#ifndef __MT7921_ACPI_SAR_H
#define __MT7921_ACPI_SAR_H

#define MT7921_ASAR_MIN_DYN		1
#define MT7921_ASAR_MAX_DYN		8
#define MT7921_ASAR_MIN_GEO		3
#define MT7921_ASAR_MAX_GEO		8

#define MT7921_ACPI_MTCL		"MTCL"
#define MT7921_ACPI_MTDS		"MTDS"
#define MT7921_ACPI_MTGS		"MTGS"

struct mt7921_asar_dyn_limit {
	u8 idx;
	u8 frp[5];
} __packed;

struct mt7921_asar_dyn {
	u8 names[4];
	u8 enable;
	u8 nr_tbl;
	struct mt7921_asar_dyn_limit tbl[0];
} __packed;

struct mt7921_asar_dyn_limit_v2 {
	u8 idx;
	u8 frp[11];
} __packed;

struct mt7921_asar_dyn_v2 {
	u8 names[4];
	u8 enable;
	u8 rsvd;
	u8 nr_tbl;
	struct mt7921_asar_dyn_limit_v2 tbl[0];
} __packed;

struct mt7921_asar_geo_band {
	u8 pwr;
	u8 offset;
} __packed;

struct mt7921_asar_geo_limit {
	u8 idx;
	/* 0:2G, 1:5G */
	struct mt7921_asar_geo_band band[2];
} __packed;

struct mt7921_asar_geo {
	u8 names[4];
	u8 version;
	u8 nr_tbl;
	struct mt7921_asar_geo_limit tbl[0];
} __packed;

struct mt7921_asar_geo_limit_v2 {
	u8 idx;
	/* 0:2G, 1:5G, 2:6G */
	struct mt7921_asar_geo_band band[3];
} __packed;

struct mt7921_asar_geo_v2 {
	u8 names[4];
	u8 version;
	u8 rsvd;
	u8 nr_tbl;
	struct mt7921_asar_geo_limit_v2 tbl[0];
} __packed;

struct mt7921_asar_cl {
	u8 names[4];
	u8 version;
	u8 mode_6g;
	u8 cl6g[6];
} __packed;

struct mt7921_acpi_sar {
	u8 ver;
	union {
		struct mt7921_asar_dyn *dyn;
		struct mt7921_asar_dyn_v2 *dyn_v2;
	};
	union {
		struct mt7921_asar_geo *geo;
		struct mt7921_asar_geo_v2 *geo_v2;
	};
	struct mt7921_asar_cl *countrylist;
};

#endif
