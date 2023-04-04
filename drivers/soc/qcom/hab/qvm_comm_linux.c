// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab_qvm.h"

inline void habhyp_notify(void *commdev)
{
	struct qvm_channel *dev = (struct qvm_channel *)commdev;

	if (dev && dev->guest_ctrl)
		dev->guest_ctrl->notify = ~0;
}
