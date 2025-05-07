/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025, Altera Corporation
 * stmmac VLAN(802.1Q) handling
 */

#ifndef __STMMAC_VLAN_H__
#define __STMMAC_VLAN_H__

#include <linux/bitfield.h>
#include "dwmac4.h"
#include "dwxgmac2.h"

/* DWMAC4 Setting */
#define GMAC_VLAN_TAG			0x00000050
#define GMAC_VLAN_TAG_DATA		0x00000054
#define GMAC_VLAN_HASH_TABLE		0x00000058
#define GMAC_VLAN_INCL			0x00000060

/* MAC VLAN */
#define GMAC_VLAN_EDVLP			BIT(26)
#define GMAC_VLAN_VTHM			BIT(25)
#define GMAC_VLAN_DOVLTC		BIT(20)
#define GMAC_VLAN_ESVL			BIT(18)
#define GMAC_VLAN_ETV			BIT(16)
#define GMAC_VLAN_VID			GENMASK(15, 0)
#define GMAC_VLAN_VLTI			BIT(20)
#define GMAC_VLAN_CSVL			BIT(19)
#define GMAC_VLAN_VLC			GENMASK(17, 16)
#define GMAC_VLAN_VLC_SHIFT		16
#define GMAC_VLAN_VLHT			GENMASK(15, 0)

/* MAC VLAN Tag */
#define GMAC_VLAN_TAG_VID		GENMASK(15, 0)
#define GMAC_VLAN_TAG_ETV		BIT(16)

/* MAC VLAN Tag Control */
#define GMAC_VLAN_TAG_CTRL_OB		BIT(0)
#define GMAC_VLAN_TAG_CTRL_CT		BIT(1)
#define GMAC_VLAN_TAG_CTRL_OFS_MASK	GENMASK(6, 2)
#define GMAC_VLAN_TAG_CTRL_OFS_SHIFT	2
#define GMAC_VLAN_TAG_CTRL_EVLS_MASK	GENMASK(22, 21)
#define GMAC_VLAN_TAG_CTRL_EVLS_SHIFT	21
#define GMAC_VLAN_TAG_CTRL_EVLRXS	BIT(24)

#define GMAC_VLAN_TAG_STRIP_NONE	(0x0 << GMAC_VLAN_TAG_CTRL_EVLS_SHIFT)
#define GMAC_VLAN_TAG_STRIP_PASS	(0x1 << GMAC_VLAN_TAG_CTRL_EVLS_SHIFT)
#define GMAC_VLAN_TAG_STRIP_FAIL	(0x2 << GMAC_VLAN_TAG_CTRL_EVLS_SHIFT)
#define GMAC_VLAN_TAG_STRIP_ALL		(0x3 << GMAC_VLAN_TAG_CTRL_EVLS_SHIFT)

/* MAC VLAN Tag Data/Filter */
#define GMAC_VLAN_TAG_DATA_VID		GENMASK(15, 0)
#define GMAC_VLAN_TAG_DATA_VEN		BIT(16)
#define GMAC_VLAN_TAG_DATA_ETV		BIT(17)

/* DWXGMAC Setting */
#define XGMAC_VLAN_TAG			0x00000050
#define XGMAC_VLAN_EDVLP		BIT(26)
#define XGMAC_VLAN_VTHM			BIT(25)
#define XGMAC_VLAN_DOVLTC		BIT(20)
#define XGMAC_VLAN_ESVL			BIT(18)
#define XGMAC_VLAN_ETV			BIT(16)
#define XGMAC_VLAN_VID			GENMASK(15, 0)
#define XGMAC_VLAN_HASH_TABLE		0x00000058
#define XGMAC_VLAN_INCL			0x00000060
#define XGMAC_VLAN_VLTI			BIT(20)
#define XGMAC_VLAN_CSVL			BIT(19)
#define XGMAC_VLAN_VLC			GENMASK(17, 16)
#define XGMAC_VLAN_VLC_SHIFT		16
#define XGMAC_FILTER_VTFE		BIT(16)

extern const struct stmmac_vlan_ops dwmac4_vlan_ops;
extern const struct stmmac_vlan_ops dwmac410_vlan_ops;
extern const struct stmmac_vlan_ops dwmac510_vlan_ops;
extern const struct stmmac_vlan_ops dwxgmac210_vlan_ops;
extern const struct stmmac_vlan_ops dwxlgmac2_vlan_ops;

u32 dwmac4_get_num_vlan(void __iomem *ioaddr);

#endif /* __STMMAC_VLAN_H__ */
