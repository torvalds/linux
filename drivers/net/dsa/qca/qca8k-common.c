// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 John Crispin <john@phrozen.org>
 */

#include <linux/netdevice.h>
#include <net/dsa.h>

#include "qca8k.h"

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

const struct qca8k_mib_desc ar8327_mib[] = {
	MIB_DESC(1, 0x00, "RxBroad"),
	MIB_DESC(1, 0x04, "RxPause"),
	MIB_DESC(1, 0x08, "RxMulti"),
	MIB_DESC(1, 0x0c, "RxFcsErr"),
	MIB_DESC(1, 0x10, "RxAlignErr"),
	MIB_DESC(1, 0x14, "RxRunt"),
	MIB_DESC(1, 0x18, "RxFragment"),
	MIB_DESC(1, 0x1c, "Rx64Byte"),
	MIB_DESC(1, 0x20, "Rx128Byte"),
	MIB_DESC(1, 0x24, "Rx256Byte"),
	MIB_DESC(1, 0x28, "Rx512Byte"),
	MIB_DESC(1, 0x2c, "Rx1024Byte"),
	MIB_DESC(1, 0x30, "Rx1518Byte"),
	MIB_DESC(1, 0x34, "RxMaxByte"),
	MIB_DESC(1, 0x38, "RxTooLong"),
	MIB_DESC(2, 0x3c, "RxGoodByte"),
	MIB_DESC(2, 0x44, "RxBadByte"),
	MIB_DESC(1, 0x4c, "RxOverFlow"),
	MIB_DESC(1, 0x50, "Filtered"),
	MIB_DESC(1, 0x54, "TxBroad"),
	MIB_DESC(1, 0x58, "TxPause"),
	MIB_DESC(1, 0x5c, "TxMulti"),
	MIB_DESC(1, 0x60, "TxUnderRun"),
	MIB_DESC(1, 0x64, "Tx64Byte"),
	MIB_DESC(1, 0x68, "Tx128Byte"),
	MIB_DESC(1, 0x6c, "Tx256Byte"),
	MIB_DESC(1, 0x70, "Tx512Byte"),
	MIB_DESC(1, 0x74, "Tx1024Byte"),
	MIB_DESC(1, 0x78, "Tx1518Byte"),
	MIB_DESC(1, 0x7c, "TxMaxByte"),
	MIB_DESC(1, 0x80, "TxOverSize"),
	MIB_DESC(2, 0x84, "TxByte"),
	MIB_DESC(1, 0x8c, "TxCollision"),
	MIB_DESC(1, 0x90, "TxAbortCol"),
	MIB_DESC(1, 0x94, "TxMultiCol"),
	MIB_DESC(1, 0x98, "TxSingleCol"),
	MIB_DESC(1, 0x9c, "TxExcDefer"),
	MIB_DESC(1, 0xa0, "TxDefer"),
	MIB_DESC(1, 0xa4, "TxLateCol"),
	MIB_DESC(1, 0xa8, "RXUnicast"),
	MIB_DESC(1, 0xac, "TXUnicast"),
};

int qca8k_read(struct qca8k_priv *priv, u32 reg, u32 *val)
{
	return regmap_read(priv->regmap, reg, val);
}

int qca8k_write(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_write(priv->regmap, reg, val);
}

int qca8k_rmw(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	return regmap_update_bits(priv->regmap, reg, mask, write_val);
}

static const struct regmap_range qca8k_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x00e4), /* Global control */
	regmap_reg_range(0x0100, 0x0168), /* EEE control */
	regmap_reg_range(0x0200, 0x0270), /* Parser control */
	regmap_reg_range(0x0400, 0x0454), /* ACL */
	regmap_reg_range(0x0600, 0x0718), /* Lookup */
	regmap_reg_range(0x0800, 0x0b70), /* QM */
	regmap_reg_range(0x0c00, 0x0c80), /* PKT */
	regmap_reg_range(0x0e00, 0x0e98), /* L3 */
	regmap_reg_range(0x1000, 0x10ac), /* MIB - Port0 */
	regmap_reg_range(0x1100, 0x11ac), /* MIB - Port1 */
	regmap_reg_range(0x1200, 0x12ac), /* MIB - Port2 */
	regmap_reg_range(0x1300, 0x13ac), /* MIB - Port3 */
	regmap_reg_range(0x1400, 0x14ac), /* MIB - Port4 */
	regmap_reg_range(0x1500, 0x15ac), /* MIB - Port5 */
	regmap_reg_range(0x1600, 0x16ac), /* MIB - Port6 */
};

const struct regmap_access_table qca8k_readable_table = {
	.yes_ranges = qca8k_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8k_readable_ranges),
};
