/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef IPU7_ISYS_CSI_PHY_H
#define IPU7_ISYS_CSI_PHY_H

struct ipu6_isys;
struct ipu6_isys_csi2_config;
struct ipu6_isys_csi2_timing;

int ipu7_isys_csi_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on);

#endif /* IPU7_ISYS_CSI_PHY_H */
