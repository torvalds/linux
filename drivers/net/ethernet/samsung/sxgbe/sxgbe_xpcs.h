/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Byungho An <bh74.an@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SXGBE_XPCS_H__
#define __SXGBE_XPCS_H__

/* XPCS Registers */
#define XPCS_OFFSET			0x1A060000
#define SR_PCS_MMD_CONTROL1		0x030000
#define SR_PCS_CONTROL2			0x030007
#define VR_PCS_MMD_XAUI_MODE_CONTROL	0x038004
#define VR_PCS_MMD_DIGITAL_STATUS	0x038010
#define SR_MII_MMD_CONTROL		0x1F0000
#define SR_MII_MMD_AN_ADV		0x1F0004
#define SR_MII_MMD_AN_LINK_PARTNER_BA	0x1F0005
#define VR_MII_MMD_AN_CONTROL		0x1F8001
#define VR_MII_MMD_AN_INT_STATUS	0x1F8002

#define XPCS_QSEQ_STATE_STABLE		0x10
#define XPCS_QSEQ_STATE_MPLLOFF		0x1c
#define XPCS_TYPE_SEL_R			0x00
#define XPCS_TYPE_SEL_X			0x01
#define XPCS_TYPE_SEL_W			0x02
#define XPCS_XAUI_MODE			0x00
#define XPCS_RXAUI_MODE			0x01

int sxgbe_xpcs_init(struct net_device *ndev);
int sxgbe_xpcs_init_1G(struct net_device *ndev);

#endif /* __SXGBE_XPCS_H__ */
