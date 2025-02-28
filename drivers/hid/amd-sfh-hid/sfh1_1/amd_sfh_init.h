/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 1.1 initialization structures
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef AMD_SFH_INIT_H
#define AMD_SFH_INIT_H

#include "../amd_sfh_common.h"

struct amd_sfh1_1_ops {
	int (*init)(struct amd_mp2_dev *mp2);
	void (*toggle_hpd)(struct amd_mp2_dev *mp2, bool enable);
};

int amd_sfh1_1_init(struct amd_mp2_dev *mp2);
void amd_sfh_toggle_hpd(struct amd_mp2_dev *mp2, bool enabled);

static const struct amd_sfh1_1_ops __maybe_unused sfh1_1_ops = {
	.init = amd_sfh1_1_init,
	.toggle_hpd = amd_sfh_toggle_hpd,
};

#endif
