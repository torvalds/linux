/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2023 MediaTek Inc. */

#ifndef __MT7921_ACPI_SAR_H
#define __MT7921_ACPI_SAR_H

#define MT792x_ASAR_MIN_DYN		1
#define MT792x_ASAR_MAX_DYN		8
#define MT792x_ASAR_MIN_GEO		3
#define MT792x_ASAR_MAX_GEO		8
#define MT792x_ASAR_MIN_FG		8

#define MT792x_ACPI_MTCL		"MTCL"
#define MT792x_ACPI_MTDS		"MTDS"
#define MT792x_ACPI_MTGS		"MTGS"
#define MT792x_ACPI_MTFG		"MTFG"

struct mt792x_asar_dyn_limit {
	u8 idx;
	u8 frp[5];
} __packed;

struct mt792x_asar_dyn {
	u8 names[4];
	u8 enable;
	u8 nr_tbl;
	DECLARE_FLEX_ARRAY(struct mt792x_asar_dyn_limit, tbl);
} __packed;

struct mt792x_asar_dyn_limit_v2 {
	u8 idx;
	u8 frp[11];
} __packed;

struct mt792x_asar_dyn_v2 {
	u8 names[4];
	u8 enable;
	u8 rsvd;
	u8 nr_tbl;
	DECLARE_FLEX_ARRAY(struct mt792x_asar_dyn_limit_v2, tbl);
} __packed;

struct mt792x_asar_geo_band {
	u8 pwr;
	u8 offset;
} __packed;

struct mt792x_asar_geo_limit {
	u8 idx;
	/* 0:2G, 1:5G */
	struct mt792x_asar_geo_band band[2];
} __packed;

struct mt792x_asar_geo {
	u8 names[4];
	u8 version;
	u8 nr_tbl;
	DECLARE_FLEX_ARRAY(struct mt792x_asar_geo_limit, tbl);
} __packed;

struct mt792x_asar_geo_limit_v2 {
	u8 idx;
	/* 0:2G, 1:5G, 2:6G */
	struct mt792x_asar_geo_band band[3];
} __packed;

struct mt792x_asar_geo_v2 {
	u8 names[4];
	u8 version;
	u8 rsvd;
	u8 nr_tbl;
	DECLARE_FLEX_ARRAY(struct mt792x_asar_geo_limit_v2, tbl);
} __packed;

struct mt792x_asar_cl {
	u8 names[4];
	u8 version;
	u8 mode_6g;
	u8 cl6g[6];
	u8 mode_5g9;
	u8 cl5g9[6];
} __packed;

struct mt792x_asar_fg {
	u8 names[4];
	u8 version;
	u8 rsvd;
	u8 nr_flag;
	u8 rsvd1;
	u8 flag[];
} __packed;

struct mt792x_acpi_sar {
	u8 ver;
	union {
		struct mt792x_asar_dyn *dyn;
		struct mt792x_asar_dyn_v2 *dyn_v2;
	};
	union {
		struct mt792x_asar_geo *geo;
		struct mt792x_asar_geo_v2 *geo_v2;
	};
	struct mt792x_asar_cl *countrylist;
	struct mt792x_asar_fg *fg;
};

#endif
