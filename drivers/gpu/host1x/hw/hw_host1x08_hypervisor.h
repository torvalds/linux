/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 NVIDIA Corporation.
 */

#define HOST1X_HV_SYNCPT_PROT_EN			0x1724
#define HOST1X_HV_SYNCPT_PROT_EN_CH_EN			BIT(1)
#define HOST1X_HV_CH_MLOCK_EN(x)			(0x1700 + (x * 4))
#define HOST1X_HV_CH_KERNEL_FILTER_GBUFFER(x)		(0x1710 + (x * 4))
