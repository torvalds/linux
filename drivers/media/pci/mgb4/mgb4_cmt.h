/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_CMT_H__
#define __MGB4_CMT_H__

#include "mgb4_vout.h"
#include "mgb4_vin.h"

u32 mgb4_cmt_set_vout_freq(struct mgb4_vout_dev *voutdev, unsigned int freq);
void mgb4_cmt_set_vin_freq_range(struct mgb4_vin_dev *vindev,
				 unsigned int freq_range);

#endif
