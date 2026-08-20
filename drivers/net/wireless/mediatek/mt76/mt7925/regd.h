/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2025 MediaTek Inc. */

#ifndef __MT7925_REGD_H
#define __MT7925_REGD_H

#include "mt7925.h"

struct mt7925_regd_rule_header {
	u8 alpha2[2];
	u8 dfs_region;
	u8 rsv[13];
};

struct mt7925_regd_rule {
	__le32 start_freq;
	__le32 end_freq;
	__le32 max_bw;
	__le32 eirp;
	__le32 flags;
	u8 rsv[12];
};

struct mt7925_regd_cc {
	u8 alpha2[2];
	u8 ver;
	u8 rsv;
	__le32 n_reg_rules;
	u8 sign_type;
	u8 rsv1[7];
	u8 data[];
};

struct mt7925_regd_rule_ev {
	__le16 tag;
	__le16 len;
	__le32 n_reg_rules;
	u8 dfs_region;
	u8 rsv[15];
	struct mt7925_regd_rule reg_rule[];
};

struct mt7925_regd_query_req {
	u8 rsv[4];
	__le16 tag;
	__le16 len;
	u8 ver;
	u8 sign_type;
	u8 rsv1[2];
	__le32 size;
	u8 alpha2[2];
	u8 rsv2[2];
	__le32 n_reg_rules;
	u8 rsv3[64];
	u8 data[];
};

#define MT7925_REGD_SUPPORTED(phy) \
	(((phy)->chip_cap & MT792x_CHIP_CAP_REGD_EN) && \
	(phy)->clc[MT792x_CLC_REGD])

int mt7925_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env);

void mt7925_regd_be_ctrl(struct mt792x_dev *dev, u8 *alpha2);
void mt7925_regd_notifier(struct wiphy *wiphy, struct regulatory_request *req);
bool mt7925_regd_clc_supported(struct mt792x_dev *dev);
int mt7925_regd_update(struct mt792x_phy *phy, char *alpha2);
int mt7925_regd_change(struct mt792x_phy *phy, char *alpha2);
bool mt7925_regd_is_valid_channel(struct mt792x_dev *dev,
				  enum nl80211_band band,
				  struct ieee80211_channel *chan);
int mt7925_regd_init(struct mt792x_phy *phy);

#endif

