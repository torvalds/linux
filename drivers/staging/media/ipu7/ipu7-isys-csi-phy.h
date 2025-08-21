/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_ISYS_CSI_PHY_H
#define IPU7_ISYS_CSI_PHY_H

struct ipu7_isys;

#define PHY_MODE_DPHY		0U
#define PHY_MODE_CPHY		1U

int ipu7_isys_csi_phy_powerup(struct ipu7_isys_csi2 *csi2);
void ipu7_isys_csi_phy_powerdown(struct ipu7_isys_csi2 *csi2);
#endif
