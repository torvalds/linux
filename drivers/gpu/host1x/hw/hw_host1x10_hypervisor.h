/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#define HOST1X_HV_SYNCPT_PROT_EN			0x172c
#define HOST1X_HV_SYNCPT_PROT_EN_CH_EN			BIT(1)
#define HOST1X_HV_CH_MLOCK_EN(x)			(0x1708 + (x * 4))
#define HOST1X_HV_CH_KERNEL_FILTER_GBUFFER(x)		(0x1718 + (x * 4))
#define HOST1X_HV_SYNCPT_VM(x)				(0x0 + 4 * (x))
