// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments ICSSG Ethernet Driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/types.h>
#include <linux/regmap.h>

#include "icssg_prueth.h"

#define ICSSG_NUM_CLASSIFIERS	16
#define ICSSG_NUM_FT1_SLOTS	8
#define ICSSG_NUM_FT3_SLOTS	16

#define ICSSG_NUM_CLASSIFIERS_IN_USE	5

/* Filter 1 - FT1 */
#define FT1_NUM_SLOTS	8
#define FT1_SLOT_SIZE	0x10	/* bytes */

/* offsets from FT1 slot base i.e. slot 1 start */
#define FT1_DA0		0x0
#define FT1_DA1		0x4
#define FT1_DA0_MASK	0x8
#define FT1_DA1_MASK	0xc

#define FT1_N_REG(slize, n, reg)	\
	(offs[slice].ft1_slot_base + FT1_SLOT_SIZE * (n) + (reg))

#define FT1_LEN_MASK		GENMASK(19, 16)
#define FT1_LEN_SHIFT		16
#define FT1_LEN(len)		(((len) << FT1_LEN_SHIFT) & FT1_LEN_MASK)
#define FT1_START_MASK		GENMASK(14, 0)
#define FT1_START(start)	((start) & FT1_START_MASK)
#define FT1_MATCH_SLOT(n)	(GENMASK(23, 16) & (BIT(n) << 16))

/* FT1 config type */
enum ft1_cfg_type {
	FT1_CFG_TYPE_DISABLED = 0,
	FT1_CFG_TYPE_EQ,
	FT1_CFG_TYPE_GT,
	FT1_CFG_TYPE_LT,
};

#define FT1_CFG_SHIFT(n)	(2 * (n))
#define FT1_CFG_MASK(n)		(0x3 << FT1_CFG_SHIFT((n)))

/* Filter 3 -  FT3 */
#define FT3_NUM_SLOTS	16
#define FT3_SLOT_SIZE	0x20	/* bytes */

/* offsets from FT3 slot n's base */
#define FT3_START		0
#define FT3_START_AUTO		0x4
#define FT3_START_OFFSET	0x8
#define FT3_JUMP_OFFSET		0xc
#define FT3_LEN			0x10
#define FT3_CFG			0x14
#define FT3_T			0x18
#define FT3_T_MASK		0x1c

#define FT3_N_REG(slize, n, reg)	\
	(offs[slice].ft3_slot_base + FT3_SLOT_SIZE * (n) + (reg))

/* offsets from rx_class n's base */
#define RX_CLASS_AND_EN		0
#define RX_CLASS_OR_EN		0x4
#define RX_CLASS_NUM_SLOTS	16
#define RX_CLASS_EN_SIZE	0x8	/* bytes */

#define RX_CLASS_N_REG(slice, n, reg)	\
	(offs[slice].rx_class_base + RX_CLASS_EN_SIZE * (n) + (reg))

/* RX Class Gates */
#define RX_CLASS_GATES_SIZE	0x4	/* bytes */

#define RX_CLASS_GATES_N_REG(slice, n)	\
	(offs[slice].rx_class_gates_base + RX_CLASS_GATES_SIZE * (n))

#define RX_CLASS_GATES_ALLOW_MASK	BIT(6)
#define RX_CLASS_GATES_RAW_MASK		BIT(5)
#define RX_CLASS_GATES_PHASE_MASK	BIT(4)

/* RX Class traffic data matching bits */
#define RX_CLASS_FT_UC				BIT(31)
#define RX_CLASS_FT_MC			BIT(30)
#define RX_CLASS_FT_BC			BIT(29)
#define RX_CLASS_FT_FW			BIT(28)
#define RX_CLASS_FT_RCV			BIT(27)
#define RX_CLASS_FT_VLAN		BIT(26)
#define RX_CLASS_FT_DA_P		BIT(25)
#define RX_CLASS_FT_DA_I		BIT(24)
#define RX_CLASS_FT_FT1_MATCH_MASK	GENMASK(23, 16)
#define RX_CLASS_FT_FT1_MATCH_SHIFT	16
#define RX_CLASS_FT_FT3_MATCH_MASK	GENMASK(15, 0)
#define RX_CLASS_FT_FT3_MATCH_SHIFT	0

#define RX_CLASS_FT_FT1_MATCH(slot)	\
	((BIT(slot) << RX_CLASS_FT_FT1_MATCH_SHIFT) & \
	RX_CLASS_FT_FT1_MATCH_MASK)

/* RX class type */
enum rx_class_sel_type {
	RX_CLASS_SEL_TYPE_OR = 0,
	RX_CLASS_SEL_TYPE_AND = 1,
	RX_CLASS_SEL_TYPE_OR_AND_AND = 2,
	RX_CLASS_SEL_TYPE_OR_OR_AND = 3,
};

#define FT1_CFG_SHIFT(n)	(2 * (n))
#define FT1_CFG_MASK(n)		(0x3 << FT1_CFG_SHIFT((n)))

#define RX_CLASS_SEL_SHIFT(n)	(2 * (n))
#define RX_CLASS_SEL_MASK(n)	(0x3 << RX_CLASS_SEL_SHIFT((n)))

#define ICSSG_CFG_OFFSET	0
#define MAC_INTERFACE_0		0x18
#define MAC_INTERFACE_1		0x1c

#define ICSSG_CFG_RX_L2_G_EN	BIT(2)

/* These are register offsets per PRU */
struct miig_rt_offsets {
	u32 mac0;
	u32 mac1;
	u32 ft1_start_len;
	u32 ft1_cfg;
	u32 ft1_slot_base;
	u32 ft3_slot_base;
	u32 ft3_p_base;
	u32 ft_rx_ptr;
	u32 rx_class_base;
	u32 rx_class_cfg1;
	u32 rx_class_cfg2;
	u32 rx_class_gates_base;
	u32 rx_green;
	u32 rx_rate_cfg_base;
	u32 rx_rate_src_sel0;
	u32 rx_rate_src_sel1;
	u32 tx_rate_cfg_base;
	u32 stat_base;
	u32 tx_hsr_tag;
	u32 tx_hsr_seq;
	u32 tx_vlan_type;
	u32 tx_vlan_ins;
};

/* These are the offset values for miig_rt_offsets registers */
static const struct miig_rt_offsets offs[] = {
	/* PRU0 */
	{
		0x8,
		0xc,
		0x80,
		0x84,
		0x88,
		0x108,
		0x308,
		0x408,
		0x40c,
		0x48c,
		0x490,
		0x494,
		0x4d4,
		0x4e4,
		0x504,
		0x508,
		0x50c,
		0x54c,
		0x63c,
		0x640,
		0x644,
		0x648,
	},
	/* PRU1 */
	{
		0x10,
		0x14,
		0x64c,
		0x650,
		0x654,
		0x6d4,
		0x8d4,
		0x9d4,
		0x9d8,
		0xa58,
		0xa5c,
		0xa60,
		0xaa0,
		0xab0,
		0xad0,
		0xad4,
		0xad8,
		0xb18,
		0xc08,
		0xc0c,
		0xc10,
		0xc14,
	},
};

static void rx_class_ft1_set_start_len(struct regmap *miig_rt, int slice,
				       u16 start, u8 len)
{
	u32 offset, val;

	offset = offs[slice].ft1_start_len;
	val = FT1_LEN(len) | FT1_START(start);
	regmap_write(miig_rt, offset, val);
}

static void rx_class_ft1_set_da(struct regmap *miig_rt, int slice,
				int n, const u8 *addr)
{
	u32 offset;

	offset = FT1_N_REG(slice, n, FT1_DA0);
	regmap_write(miig_rt, offset, (u32)(addr[0] | addr[1] << 8 |
		     addr[2] << 16 | addr[3] << 24));
	offset = FT1_N_REG(slice, n, FT1_DA1);
	regmap_write(miig_rt, offset, (u32)(addr[4] | addr[5] << 8));
}

static void rx_class_ft1_set_da_mask(struct regmap *miig_rt, int slice,
				     int n, const u8 *addr)
{
	u32 offset;

	offset = FT1_N_REG(slice, n, FT1_DA0_MASK);
	regmap_write(miig_rt, offset, (u32)(addr[0] | addr[1] << 8 |
		     addr[2] << 16 | addr[3] << 24));
	offset = FT1_N_REG(slice, n, FT1_DA1_MASK);
	regmap_write(miig_rt, offset, (u32)(addr[4] | addr[5] << 8));
}

static void rx_class_ft1_cfg_set_type(struct regmap *miig_rt, int slice, int n,
				      enum ft1_cfg_type type)
{
	u32 offset;

	offset = offs[slice].ft1_cfg;
	regmap_update_bits(miig_rt, offset, FT1_CFG_MASK(n),
			   type << FT1_CFG_SHIFT(n));
}

static void rx_class_sel_set_type(struct regmap *miig_rt, int slice, int n,
				  enum rx_class_sel_type type)
{
	u32 offset;

	offset = offs[slice].rx_class_cfg1;
	regmap_update_bits(miig_rt, offset, RX_CLASS_SEL_MASK(n),
			   type << RX_CLASS_SEL_SHIFT(n));
}

static void rx_class_set_and(struct regmap *miig_rt, int slice, int n,
			     u32 data)
{
	u32 offset;

	offset = RX_CLASS_N_REG(slice, n, RX_CLASS_AND_EN);
	regmap_write(miig_rt, offset, data);
}

static void rx_class_set_or(struct regmap *miig_rt, int slice, int n,
			    u32 data)
{
	u32 offset;

	offset = RX_CLASS_N_REG(slice, n, RX_CLASS_OR_EN);
	regmap_write(miig_rt, offset, data);
}

void icssg_class_set_host_mac_addr(struct regmap *miig_rt, const u8 *mac)
{
	regmap_write(miig_rt, MAC_INTERFACE_0, (u32)(mac[0] | mac[1] << 8 |
		     mac[2] << 16 | mac[3] << 24));
	regmap_write(miig_rt, MAC_INTERFACE_1, (u32)(mac[4] | mac[5] << 8));
}

void icssg_class_set_mac_addr(struct regmap *miig_rt, int slice, u8 *mac)
{
	regmap_write(miig_rt, offs[slice].mac0, (u32)(mac[0] | mac[1] << 8 |
		     mac[2] << 16 | mac[3] << 24));
	regmap_write(miig_rt, offs[slice].mac1, (u32)(mac[4] | mac[5] << 8));
}

/* disable all RX traffic */
void icssg_class_disable(struct regmap *miig_rt, int slice)
{
	u32 data, offset;
	int n;

	/* Enable RX_L2_G */
	regmap_update_bits(miig_rt, ICSSG_CFG_OFFSET, ICSSG_CFG_RX_L2_G_EN,
			   ICSSG_CFG_RX_L2_G_EN);

	for (n = 0; n < ICSSG_NUM_CLASSIFIERS; n++) {
		/* AND_EN = 0 */
		rx_class_set_and(miig_rt, slice, n, 0);
		/* OR_EN = 0 */
		rx_class_set_or(miig_rt, slice, n, 0);

		/* set CFG1 to OR */
		rx_class_sel_set_type(miig_rt, slice, n, RX_CLASS_SEL_TYPE_OR);

		/* configure gate */
		offset = RX_CLASS_GATES_N_REG(slice, n);
		regmap_read(miig_rt, offset, &data);
		/* clear class_raw so we go through filters */
		data &= ~RX_CLASS_GATES_RAW_MASK;
		/* set allow and phase mask */
		data |= RX_CLASS_GATES_ALLOW_MASK | RX_CLASS_GATES_PHASE_MASK;
		regmap_write(miig_rt, offset, data);
	}

	/* FT1 Disabled */
	for (n = 0; n < ICSSG_NUM_FT1_SLOTS; n++) {
		const u8 addr[] = { 0, 0, 0, 0, 0, 0, };

		rx_class_ft1_cfg_set_type(miig_rt, slice, n,
					  FT1_CFG_TYPE_DISABLED);
		rx_class_ft1_set_da(miig_rt, slice, n, addr);
		rx_class_ft1_set_da_mask(miig_rt, slice, n, addr);
	}

	/* clear CFG2 */
	regmap_write(miig_rt, offs[slice].rx_class_cfg2, 0);
}

void icssg_class_default(struct regmap *miig_rt, int slice, bool allmulti)
{
	u32 data;

	/* defaults */
	icssg_class_disable(miig_rt, slice);

	/* Setup Classifier */
	/* match on Broadcast or MAC_PRU address */
	data = RX_CLASS_FT_BC | RX_CLASS_FT_DA_P;

	/* multicast */
	if (allmulti)
		data |= RX_CLASS_FT_MC;

	rx_class_set_or(miig_rt, slice, 0, data);

	/* set CFG1 for OR_OR_AND for classifier */
	rx_class_sel_set_type(miig_rt, slice, 0, RX_CLASS_SEL_TYPE_OR_OR_AND);

	/* clear CFG2 */
	regmap_write(miig_rt, offs[slice].rx_class_cfg2, 0);
}

/* required for SAV check */
void icssg_ft1_set_mac_addr(struct regmap *miig_rt, int slice, u8 *mac_addr)
{
	const u8 mask_addr[] = { 0, 0, 0, 0, 0, 0, };

	rx_class_ft1_set_start_len(miig_rt, slice, 0, 6);
	rx_class_ft1_set_da(miig_rt, slice, 0, mac_addr);
	rx_class_ft1_set_da_mask(miig_rt, slice, 0, mask_addr);
	rx_class_ft1_cfg_set_type(miig_rt, slice, 0, FT1_CFG_TYPE_EQ);
}
