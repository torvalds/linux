/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __VENUS_HFI_VENUS_H__
#define __VENUS_HFI_VENUS_H__

struct venus_core;

void venus_hfi_destroy(struct venus_core *core);
int venus_hfi_create(struct venus_core *core);

#endif
