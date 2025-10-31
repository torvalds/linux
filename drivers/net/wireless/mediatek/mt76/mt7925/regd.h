/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2025 MediaTek Inc. */

#ifndef __MT7925_REGD_H
#define __MT7925_REGD_H

#include "mt7925.h"

void mt7925_regd_be_ctrl(struct mt792x_dev *dev, u8 *alpha2);
void mt7925_regd_update(struct mt792x_dev *dev);
void mt7925_regd_notifier(struct wiphy *wiphy, struct regulatory_request *req);
bool mt7925_regd_clc_supported(struct mt792x_dev *dev);

#endif

