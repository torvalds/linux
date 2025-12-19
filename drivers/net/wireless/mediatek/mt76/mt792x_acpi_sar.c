// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/acpi.h>
#include "mt792x.h"

static const char * const cc_list_all[] = {
	"00", "EU", "AR", "AU", "AZ", "BY", "BO", "BR",
	"CA", "CL", "CN", "ID", "JP", "MY", "MX", "ME",
	"MA", "NZ", "NG", "PH", "RU", "RS", "SG", "KR",
	"TW", "TH", "UA", "GB", "US", "VN", "KH", "PY",
};

static const char * const cc_list_eu[] = {
	"AD", "AT", "BE", "BG", "CY", "CZ", "HR", "DK",
	"EE", "FI", "FR", "DE", "GR", "HU", "IS", "IE",
	"IT", "LV", "LI", "LT", "LU", "MC", "MT", "NL",
	"NO", "PL", "PT", "RO", "SK", "SI", "ES", "SE",
	"CH",
};

static const char * const cc_list_be[] = {
	"AR", "BR", "BY", "CL", "IQ", "MX", "OM", "RU",
	"RW", "VN", "KR", "UA", "", "", "", "",
	"EU", "AT", "CN", "CA", "TW", "NZ", "PH", "UK",
	"US",
};

static int
mt792x_acpi_read(struct mt792x_dev *dev, u8 *method, u8 **tbl, u32 *len)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct mt76_dev *mdev = &dev->mt76;
	union acpi_object *sar_root;
	acpi_handle root, handle;
	acpi_status status;
	u32 i = 0;
	int ret;

	root = ACPI_HANDLE(mdev->dev);
	if (!root)
		return -EOPNOTSUPP;

	status = acpi_get_handle(root, method, &handle);
	if (ACPI_FAILURE(status))
		return -EIO;

	status = acpi_evaluate_object(handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status))
		return -EIO;

	sar_root = buf.pointer;
	if (sar_root->type != ACPI_TYPE_PACKAGE ||
	    sar_root->package.count < 4 ||
	    sar_root->package.elements[0].type != ACPI_TYPE_INTEGER) {
		dev_err(mdev->dev, "sar cnt = %d\n",
			sar_root->package.count);
		ret = -EINVAL;
		goto free;
	}

	if (!*tbl) {
		*tbl = devm_kzalloc(mdev->dev, sar_root->package.count,
				    GFP_KERNEL);
		if (!*tbl) {
			ret = -ENOMEM;
			goto free;
		}
	}

	if (len)
		*len = sar_root->package.count;

	for (i = 0; i < sar_root->package.count; i++) {
		union acpi_object *sar_unit = &sar_root->package.elements[i];

		if (sar_unit->type != ACPI_TYPE_INTEGER)
			break;

		*(*tbl + i) = (u8)sar_unit->integer.value;
	}

	ret = i == sar_root->package.count ? 0 : -EINVAL;
free:
	kfree(sar_root);

	return ret;
}

/* MTCL : Country List Table for 6G band */
/* MTCL : Country List Table for 6G band and 11BE */
static int
mt792x_asar_acpi_read_mtcl(struct mt792x_dev *dev, u8 **table, u8 *version)
{
	int len, ret;

	ret = mt792x_acpi_read(dev, MT792x_ACPI_MTCL, table, &len);
	if (ret)
		return ret;

	if (len == sizeof(struct mt792x_asar_cl))
		*version = ((struct mt792x_asar_cl *)*table)->version;
	else if (len == sizeof(struct mt792x_asar_cl_v3))
		*version = ((struct mt792x_asar_cl_v3 *)*table)->version;
	else
		return -EINVAL;

	return ret;
}

/* MTDS : Dynamic SAR Power Table */
static int
mt792x_asar_acpi_read_mtds(struct mt792x_dev *dev, u8 **table, u8 version)
{
	int len, ret, sarlen, prelen, tblcnt;
	bool enable;

	ret = mt792x_acpi_read(dev, MT792x_ACPI_MTDS, table, &len);
	if (ret)
		return ret;

	/* Table content validation */
	switch (version) {
	case 1:
		enable = ((struct mt792x_asar_dyn *)*table)->enable;
		sarlen = sizeof(struct mt792x_asar_dyn_limit);
		prelen = sizeof(struct mt792x_asar_dyn);
		break;
	case 2:
		enable = ((struct mt792x_asar_dyn_v2 *)*table)->enable;
		sarlen = sizeof(struct mt792x_asar_dyn_limit_v2);
		prelen = sizeof(struct mt792x_asar_dyn_v2);
		break;
	default:
		return -EINVAL;
	}

	tblcnt = (len - prelen) / sarlen;
	if (!enable ||
	    tblcnt > MT792x_ASAR_MAX_DYN || tblcnt < MT792x_ASAR_MIN_DYN)
		return -EINVAL;

	return 0;
}

/* MTGS : Geo SAR Power Table */
static int
mt792x_asar_acpi_read_mtgs(struct mt792x_dev *dev, u8 **table, u8 version)
{
	int len, ret, sarlen, prelen, tblcnt;

	ret = mt792x_acpi_read(dev, MT792x_ACPI_MTGS, table, &len);
	if (ret)
		return ret;

	/* Table content validation */
	switch (version) {
	case 1:
		sarlen = sizeof(struct mt792x_asar_geo_limit);
		prelen = sizeof(struct mt792x_asar_geo);
		break;
	case 2:
		sarlen = sizeof(struct mt792x_asar_geo_limit_v2);
		prelen = sizeof(struct mt792x_asar_geo_v2);
		break;
	default:
		return -EINVAL;
	}

	tblcnt = (len - prelen) / sarlen;
	if (tblcnt > MT792x_ASAR_MAX_GEO || tblcnt < MT792x_ASAR_MIN_GEO)
		return -EINVAL;

	return 0;
}

/* MTFG : Flag Table */
static int
mt792x_asar_acpi_read_mtfg(struct mt792x_dev *dev, u8 **table)
{
	int len, ret;

	ret = mt792x_acpi_read(dev, MT792x_ACPI_MTFG, table, &len);
	if (ret)
		return ret;

	if (len < MT792x_ASAR_MIN_FG)
		return -EINVAL;

	return 0;
}

int mt792x_init_acpi_sar(struct mt792x_dev *dev)
{
	struct mt792x_acpi_sar *asar;
	int ret;

	asar = devm_kzalloc(dev->mt76.dev, sizeof(*asar), GFP_KERNEL);
	if (!asar)
		return -ENOMEM;

	ret = mt792x_asar_acpi_read_mtcl(dev, (u8 **)&asar->countrylist, &asar->ver);
	if (ret) {
		devm_kfree(dev->mt76.dev, asar->countrylist);
		asar->countrylist = NULL;
	}

	ret = mt792x_asar_acpi_read_mtds(dev, (u8 **)&asar->dyn, asar->ver);
	if (ret) {
		devm_kfree(dev->mt76.dev, asar->dyn);
		asar->dyn = NULL;
	}

	/* MTGS is optional */
	ret = mt792x_asar_acpi_read_mtgs(dev, (u8 **)&asar->geo, asar->ver);
	if (ret) {
		devm_kfree(dev->mt76.dev, asar->geo);
		asar->geo = NULL;
	}

	/* MTFG is optional */
	ret = mt792x_asar_acpi_read_mtfg(dev, (u8 **)&asar->fg);
	if (ret) {
		devm_kfree(dev->mt76.dev, asar->fg);
		asar->fg = NULL;
	}
	dev->phy.acpisar = asar;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_init_acpi_sar);

static s8
mt792x_asar_get_geo_pwr(struct mt792x_phy *phy,
			enum nl80211_band band, s8 dyn_power)
{
	struct mt792x_acpi_sar *asar = phy->acpisar;
	struct mt792x_asar_geo_band *band_pwr;
	s8 geo_power;
	u8 idx, max;

	if (!asar->geo)
		return dyn_power;

	switch (phy->mt76->dev->region) {
	case NL80211_DFS_FCC:
		idx = 0;
		break;
	case NL80211_DFS_ETSI:
		idx = 1;
		break;
	default: /* WW */
		idx = 2;
		break;
	}

	if (asar->ver == 1) {
		band_pwr = &asar->geo->tbl[idx].band[0];
		max = ARRAY_SIZE(asar->geo->tbl[idx].band);
	} else {
		band_pwr = &asar->geo_v2->tbl[idx].band[0];
		max = ARRAY_SIZE(asar->geo_v2->tbl[idx].band);
	}

	switch (band) {
	case NL80211_BAND_2GHZ:
		idx = 0;
		break;
	case NL80211_BAND_5GHZ:
		idx = 1;
		break;
	case NL80211_BAND_6GHZ:
		idx = 2;
		break;
	default:
		return dyn_power;
	}

	if (idx >= max)
		return dyn_power;

	geo_power = (band_pwr + idx)->pwr;
	dyn_power += (band_pwr + idx)->offset;

	return min(geo_power, dyn_power);
}

static s8
mt792x_asar_range_pwr(struct mt792x_phy *phy,
		      const struct cfg80211_sar_freq_ranges *range,
		      u8 idx)
{
	const struct cfg80211_sar_capa *capa = phy->mt76->hw->wiphy->sar_capa;
	struct mt792x_acpi_sar *asar = phy->acpisar;
	u8 *limit, band, max;

	if (!capa)
		return 127;

	if (asar->ver == 1) {
		limit = &asar->dyn->tbl[0].frp[0];
		max = ARRAY_SIZE(asar->dyn->tbl[0].frp);
	} else {
		limit = &asar->dyn_v2->tbl[0].frp[0];
		max = ARRAY_SIZE(asar->dyn_v2->tbl[0].frp);
	}

	if (idx >= max)
		return 127;

	if (range->start_freq >= 5945)
		band = NL80211_BAND_6GHZ;
	else if (range->start_freq >= 5150)
		band = NL80211_BAND_5GHZ;
	else
		band = NL80211_BAND_2GHZ;

	return mt792x_asar_get_geo_pwr(phy, band, limit[idx]);
}

int mt792x_init_acpi_sar_power(struct mt792x_phy *phy, bool set_default)
{
	const struct cfg80211_sar_capa *capa = phy->mt76->hw->wiphy->sar_capa;
	int i;

	if (!phy->acpisar || !((struct mt792x_acpi_sar *)phy->acpisar)->dyn)
		return 0;

	/* When ACPI SAR enabled in HW, we should apply rules for .frp
	 * 1. w/o .sar_specs : set ACPI SAR power as the defatul value
	 * 2. w/  .sar_specs : set power with min(.sar_specs, ACPI_SAR)
	 */
	for (i = 0; i < capa->num_freq_ranges; i++) {
		struct mt76_freq_range_power *frp = &phy->mt76->frp[i];

		frp->range = set_default ? &capa->freq_ranges[i] : frp->range;
		if (!frp->range)
			continue;

		frp->power = min_t(s8, set_default ? 127 : frp->power,
				   mt792x_asar_range_pwr(phy, frp->range, i));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_init_acpi_sar_power);

u8 mt792x_acpi_get_flags(struct mt792x_phy *phy)
{
	struct mt792x_acpi_sar *acpisar = phy->acpisar;
	struct mt792x_asar_fg *fg;
	struct {
		u8 acpi_idx;
		u8 chip_idx;
	} map[] = {
		{ 1, 1 },
		{ 4, 2 },
	};
	u8 flags = BIT(0);
	int i, j;

	if (!acpisar)
		return 0;

	fg = acpisar->fg;
	if (!fg)
		return flags;

	/* pickup necessary settings per device and
	 * translate the index of bitmap for chip command.
	 */
	for (i = 0; i < fg->nr_flag; i++) {
		for (j = 0; j < ARRAY_SIZE(map); j++) {
			if (fg->flag[i] == map[j].acpi_idx) {
				flags |= BIT(map[j].chip_idx);
				break;
			}
		}
	}

	return flags;
}
EXPORT_SYMBOL_GPL(mt792x_acpi_get_flags);

static u32
mt792x_acpi_get_mtcl_map_v3(int row, int column, struct mt792x_asar_cl_v3 *cl)
{
	u32 config = 0;
	u8 mode_be = 0;

	mode_be = (cl->mode_be > 0x02) ? 0 : cl->mode_be;

	if (cl->version > 2 && cl->clbe[row] & BIT(column))
		config |= (mode_be & 0x3) << 4;

	return config;
}

static u32
mt792x_acpi_get_mtcl_map(int row, int column, struct mt792x_asar_cl *cl)
{
	u32 config = 0;
	u8 mode_6g, mode_5g9;

	mode_6g = (cl->mode_6g > 0x02) ? 0 : cl->mode_6g;
	mode_5g9 = (cl->mode_5g9 > 0x01) ? 0 : cl->mode_5g9;

	if ((cl->cl6g[row] & BIT(column)) || cl->mode_6g == 0x02)
		config |= (mode_6g & 0x3) << 2;
	if (cl->version > 1 && cl->cl5g9[row] & BIT(column))
		config |= (mode_5g9 & 0x3);

	return config;
}

static u32
mt792x_acpi_parse_mtcl_tbl_v3(struct mt792x_phy *phy, char *alpha2)
{
	struct mt792x_acpi_sar *sar = phy->acpisar;
	struct mt792x_asar_cl_v3 *cl = sar->countrylist_v3;
	int col, row, i;

	if (sar->ver != 3)
		goto out;

	if (!cl)
		return MT792X_ACPI_MTCL_INVALID;

	for (i = 0; i < ARRAY_SIZE(cc_list_be); i++) {
		col = 7 - i % 8;
		row = i / 8;
		if (!memcmp(cc_list_be[i], alpha2, 2))
			return mt792x_acpi_get_mtcl_map_v3(row, col, cl);
	}
	for (i = 0; i < ARRAY_SIZE(cc_list_eu); i++) {
		if (!memcmp(cc_list_eu[i], alpha2, 2))
			return mt792x_acpi_get_mtcl_map_v3(3, 7, cl);
	}

out:
	/* Depends on driver */
	return 0x20;
}

static u32
mt792x_acpi_parse_mtcl_tbl(struct mt792x_phy *phy, char *alpha2)
{
	struct mt792x_acpi_sar *sar = phy->acpisar;
	struct mt792x_asar_cl *cl = sar->countrylist;
	int col, row, i;

	if (!cl)
		return MT792X_ACPI_MTCL_INVALID;

	for (i = 0; i < ARRAY_SIZE(cc_list_all); i++) {
		col = 7 - i % 8;
		row = i / 8;
		if (!memcmp(cc_list_all[i], alpha2, 2))
			return mt792x_acpi_get_mtcl_map(row, col, cl);
	}

	for (i = 0; i < ARRAY_SIZE(cc_list_eu); i++)
		if (!memcmp(cc_list_eu[i], alpha2, 2))
			return mt792x_acpi_get_mtcl_map(0, 6, cl);

	return mt792x_acpi_get_mtcl_map(0, 7, cl);
}

u32 mt792x_acpi_get_mtcl_conf(struct mt792x_phy *phy, char *alpha2)
{
	struct mt792x_acpi_sar *sar = phy->acpisar;
	u32 config = 0;

	if (!sar)
		return MT792X_ACPI_MTCL_INVALID;

	config = mt792x_acpi_parse_mtcl_tbl_v3(phy, alpha2);

	if (config == MT792X_ACPI_MTCL_INVALID)
		return MT792X_ACPI_MTCL_INVALID;

	config |= mt792x_acpi_parse_mtcl_tbl(phy, alpha2);

	return config;
}
EXPORT_SYMBOL_GPL(mt792x_acpi_get_mtcl_conf);
