/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_ICC_DEBUG_H__
#define __QCOM_ICC_DEBUG_H__

#include <linux/interconnect-provider.h>

int qcom_icc_debug_register(struct icc_provider *provider);
int qcom_icc_debug_unregister(struct icc_provider *provider);

#endif
