/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2025 MediaTek Inc. */

#ifndef __MT7921_REGD_H
#define __MT7921_REGD_H

struct mt792x_dev;
struct wiphy;
struct regulatory_request;

int mt7921_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env);
int __mt7921_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			     enum environment_cap country_ie_env);
void mt7921_regd_notifier(struct wiphy *wiphy,
			  struct regulatory_request *request);
bool mt7921_regd_clc_supported(struct mt792x_dev *dev);
int mt7921_regd_change(struct mt792x_phy *phy, char *alpha2);
int mt7921_regd_init(struct mt792x_phy *phy);

#endif
