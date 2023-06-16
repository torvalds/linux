/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __STEP_CHG_H__
#define __STEP_CHG_H__

#include <linux/iio/consumer.h>
#include "smb5-iio.h"

#define MAX_STEP_CHG_ENTRIES	8

struct step_chg_jeita_param {
	u32			psy_prop;
	u32			iio_prop;
	char			*prop_name;
	int			rise_hys;
	int			fall_hys;
	bool			use_bms;
};

struct range_data {
	int low_threshold;
	int high_threshold;
	u32 value;
};

int qcom_step_chg_init(struct device *dev, bool step_chg_enable,
	bool sw_jeita_enable, bool jeita_arb_en, struct iio_channel *iio_chans);
void qcom_step_chg_deinit(void);
#endif /* __STEP_CHG_H__ */
