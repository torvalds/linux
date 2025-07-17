/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025, Altera Corporation
 * stmmac VLAN(802.1Q) handling
 */

#ifndef __STMMAC_VLAN_H__
#define __STMMAC_VLAN_H__

#include <linux/bitfield.h>
#include "dwxgmac2.h"

#define VLAN_TAG			0x00000050
#define VLAN_TAG_DATA			0x00000054
#define VLAN_HASH_TABLE			0x00000058
#define VLAN_INCL			0x00000060

/* MAC VLAN */
#define VLAN_EDVLP			BIT(26)
#define VLAN_VTHM			BIT(25)
#define VLAN_DOVLTC			BIT(20)
#define VLAN_ESVL			BIT(18)
#define VLAN_ETV			BIT(16)
#define VLAN_VID			GENMASK(15, 0)
#define VLAN_VLTI			BIT(20)
#define VLAN_CSVL			BIT(19)
#define VLAN_VLC			GENMASK(17, 16)
#define VLAN_VLC_SHIFT			16
#define VLAN_VLHT			GENMASK(15, 0)

/* MAC VLAN Tag */
#define VLAN_TAG_VID			GENMASK(15, 0)
#define VLAN_TAG_ETV			BIT(16)

/* MAC VLAN Tag Control */
#define VLAN_TAG_CTRL_OB		BIT(0)
#define VLAN_TAG_CTRL_CT		BIT(1)
#define VLAN_TAG_CTRL_OFS_MASK		GENMASK(6, 2)
#define VLAN_TAG_CTRL_OFS_SHIFT		2
#define VLAN_TAG_CTRL_EVLS_MASK		GENMASK(22, 21)
#define VLAN_TAG_CTRL_EVLS_SHIFT	21
#define VLAN_TAG_CTRL_EVLRXS		BIT(24)

#define VLAN_TAG_STRIP_NONE		FIELD_PREP(VLAN_TAG_CTRL_EVLS_MASK, 0x0)
#define VLAN_TAG_STRIP_PASS		FIELD_PREP(VLAN_TAG_CTRL_EVLS_MASK, 0x1)
#define VLAN_TAG_STRIP_FAIL		FIELD_PREP(VLAN_TAG_CTRL_EVLS_MASK, 0x2)
#define VLAN_TAG_STRIP_ALL		FIELD_PREP(VLAN_TAG_CTRL_EVLS_MASK, 0x3)

/* MAC VLAN Tag Data/Filter */
#define VLAN_TAG_DATA_VID		GENMASK(15, 0)
#define VLAN_TAG_DATA_VEN		BIT(16)
#define VLAN_TAG_DATA_ETV		BIT(17)

/* MAC VLAN HW FEAT */
#define HW_FEATURE3			0x00000128
#define VLAN_HW_FEAT_NRVF		GENMASK(2, 0)

extern const struct stmmac_vlan_ops dwmac_vlan_ops;
extern const struct stmmac_vlan_ops dwxgmac210_vlan_ops;
extern const struct stmmac_vlan_ops dwxlgmac2_vlan_ops;

u32 stmmac_get_num_vlan(void __iomem *ioaddr);

#endif /* __STMMAC_VLAN_H__ */
