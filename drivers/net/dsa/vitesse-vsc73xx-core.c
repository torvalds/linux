// SPDX-License-Identifier: GPL-2.0
/* DSA driver for:
 * Vitesse VSC7385 SparX-G5 5+1-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7388 SparX-G8 8-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7395 SparX-G5e 5+1-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7398 SparX-G8e 8-port Integrated Gigabit Ethernet Switch
 *
 * These switches have a built-in 8051 CPU and can download and execute a
 * firmware in this CPU. They can also be configured to use an external CPU
 * handling the switch in a memory-mapped manner by connecting to that external
 * CPU's memory bus.
 *
 * Copyright (C) 2018 Linus Wallej <linus.walleij@linaro.org>
 * Includes portions of code from the firmware uploader by:
 * Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/bitops.h>
#include <linux/if_bridge.h>
#include <linux/etherdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/random.h>
#include <net/dsa.h>

#include "vitesse-vsc73xx.h"

#define VSC73XX_BLOCK_MAC	0x1 /* Subblocks 0-4, 6 (CPU port) */
#define VSC73XX_BLOCK_ANALYZER	0x2 /* Only subblock 0 */
#define VSC73XX_BLOCK_MII	0x3 /* Subblocks 0 and 1 */
#define VSC73XX_BLOCK_MEMINIT	0x3 /* Only subblock 2 */
#define VSC73XX_BLOCK_CAPTURE	0x4 /* Only subblock 2 */
#define VSC73XX_BLOCK_ARBITER	0x5 /* Only subblock 0 */
#define VSC73XX_BLOCK_SYSTEM	0x7 /* Only subblock 0 */

#define CPU_PORT	6 /* CPU port */

/* MAC Block registers */
#define VSC73XX_MAC_CFG		0x00
#define VSC73XX_MACHDXGAP	0x02
#define VSC73XX_FCCONF		0x04
#define VSC73XX_FCMACHI		0x08
#define VSC73XX_FCMACLO		0x0c
#define VSC73XX_MAXLEN		0x10
#define VSC73XX_ADVPORTM	0x19
#define VSC73XX_TXUPDCFG	0x24
#define VSC73XX_TXQ_SELECT_CFG	0x28
#define VSC73XX_RXOCT		0x50
#define VSC73XX_TXOCT		0x51
#define VSC73XX_C_RX0		0x52
#define VSC73XX_C_RX1		0x53
#define VSC73XX_C_RX2		0x54
#define VSC73XX_C_TX0		0x55
#define VSC73XX_C_TX1		0x56
#define VSC73XX_C_TX2		0x57
#define VSC73XX_C_CFG		0x58
#define VSC73XX_CAT_DROP	0x6e
#define VSC73XX_CAT_PR_MISC_L2	0x6f
#define VSC73XX_CAT_PR_USR_PRIO	0x75
#define VSC73XX_Q_MISC_CONF	0xdf

/* MAC_CFG register bits */
#define VSC73XX_MAC_CFG_WEXC_DIS	BIT(31)
#define VSC73XX_MAC_CFG_PORT_RST	BIT(29)
#define VSC73XX_MAC_CFG_TX_EN		BIT(28)
#define VSC73XX_MAC_CFG_SEED_LOAD	BIT(27)
#define VSC73XX_MAC_CFG_SEED_MASK	GENMASK(26, 19)
#define VSC73XX_MAC_CFG_SEED_OFFSET	19
#define VSC73XX_MAC_CFG_FDX		BIT(18)
#define VSC73XX_MAC_CFG_GIGA_MODE	BIT(17)
#define VSC73XX_MAC_CFG_RX_EN		BIT(16)
#define VSC73XX_MAC_CFG_VLAN_DBLAWR	BIT(15)
#define VSC73XX_MAC_CFG_VLAN_AWR	BIT(14)
#define VSC73XX_MAC_CFG_100_BASE_T	BIT(13) /* Not in manual */
#define VSC73XX_MAC_CFG_TX_IPG_MASK	GENMASK(10, 6)
#define VSC73XX_MAC_CFG_TX_IPG_OFFSET	6
#define VSC73XX_MAC_CFG_TX_IPG_1000M	(6 << VSC73XX_MAC_CFG_TX_IPG_OFFSET)
#define VSC73XX_MAC_CFG_TX_IPG_100_10M	(17 << VSC73XX_MAC_CFG_TX_IPG_OFFSET)
#define VSC73XX_MAC_CFG_MAC_RX_RST	BIT(5)
#define VSC73XX_MAC_CFG_MAC_TX_RST	BIT(4)
#define VSC73XX_MAC_CFG_CLK_SEL_MASK	GENMASK(2, 0)
#define VSC73XX_MAC_CFG_CLK_SEL_OFFSET	0
#define VSC73XX_MAC_CFG_CLK_SEL_1000M	1
#define VSC73XX_MAC_CFG_CLK_SEL_100M	2
#define VSC73XX_MAC_CFG_CLK_SEL_10M	3
#define VSC73XX_MAC_CFG_CLK_SEL_EXT	4

#define VSC73XX_MAC_CFG_1000M_F_PHY	(VSC73XX_MAC_CFG_FDX | \
					 VSC73XX_MAC_CFG_GIGA_MODE | \
					 VSC73XX_MAC_CFG_TX_IPG_1000M | \
					 VSC73XX_MAC_CFG_CLK_SEL_EXT)
#define VSC73XX_MAC_CFG_100_10M_F_PHY	(VSC73XX_MAC_CFG_FDX | \
					 VSC73XX_MAC_CFG_TX_IPG_100_10M | \
					 VSC73XX_MAC_CFG_CLK_SEL_EXT)
#define VSC73XX_MAC_CFG_100_10M_H_PHY	(VSC73XX_MAC_CFG_TX_IPG_100_10M | \
					 VSC73XX_MAC_CFG_CLK_SEL_EXT)
#define VSC73XX_MAC_CFG_1000M_F_RGMII	(VSC73XX_MAC_CFG_FDX | \
					 VSC73XX_MAC_CFG_GIGA_MODE | \
					 VSC73XX_MAC_CFG_TX_IPG_1000M | \
					 VSC73XX_MAC_CFG_CLK_SEL_1000M)
#define VSC73XX_MAC_CFG_RESET		(VSC73XX_MAC_CFG_PORT_RST | \
					 VSC73XX_MAC_CFG_MAC_RX_RST | \
					 VSC73XX_MAC_CFG_MAC_TX_RST)

/* Flow control register bits */
#define VSC73XX_FCCONF_ZERO_PAUSE_EN	BIT(17)
#define VSC73XX_FCCONF_FLOW_CTRL_OBEY	BIT(16)
#define VSC73XX_FCCONF_PAUSE_VAL_MASK	GENMASK(15, 0)

/* ADVPORTM advanced port setup register bits */
#define VSC73XX_ADVPORTM_IFG_PPM	BIT(7)
#define VSC73XX_ADVPORTM_EXC_COL_CONT	BIT(6)
#define VSC73XX_ADVPORTM_EXT_PORT	BIT(5)
#define VSC73XX_ADVPORTM_INV_GTX	BIT(4)
#define VSC73XX_ADVPORTM_ENA_GTX	BIT(3)
#define VSC73XX_ADVPORTM_DDR_MODE	BIT(2)
#define VSC73XX_ADVPORTM_IO_LOOPBACK	BIT(1)
#define VSC73XX_ADVPORTM_HOST_LOOPBACK	BIT(0)

/* CAT_DROP categorizer frame dropping register bits */
#define VSC73XX_CAT_DROP_DROP_MC_SMAC_ENA	BIT(6)
#define VSC73XX_CAT_DROP_FWD_CTRL_ENA		BIT(4)
#define VSC73XX_CAT_DROP_FWD_PAUSE_ENA		BIT(3)
#define VSC73XX_CAT_DROP_UNTAGGED_ENA		BIT(2)
#define VSC73XX_CAT_DROP_TAGGED_ENA		BIT(1)
#define VSC73XX_CAT_DROP_NULL_MAC_ENA		BIT(0)

#define VSC73XX_Q_MISC_CONF_EXTENT_MEM		BIT(31)
#define VSC73XX_Q_MISC_CONF_EARLY_TX_MASK	GENMASK(4, 1)
#define VSC73XX_Q_MISC_CONF_EARLY_TX_512	(1 << 1)
#define VSC73XX_Q_MISC_CONF_MAC_PAUSE_MODE	BIT(0)

/* Frame analyzer block 2 registers */
#define VSC73XX_STORMLIMIT	0x02
#define VSC73XX_ADVLEARN	0x03
#define VSC73XX_IFLODMSK	0x04
#define VSC73XX_VLANMASK	0x05
#define VSC73XX_MACHDATA	0x06
#define VSC73XX_MACLDATA	0x07
#define VSC73XX_ANMOVED		0x08
#define VSC73XX_ANAGEFIL	0x09
#define VSC73XX_ANEVENTS	0x0a
#define VSC73XX_ANCNTMASK	0x0b
#define VSC73XX_ANCNTVAL	0x0c
#define VSC73XX_LEARNMASK	0x0d
#define VSC73XX_UFLODMASK	0x0e
#define VSC73XX_MFLODMASK	0x0f
#define VSC73XX_RECVMASK	0x10
#define VSC73XX_AGGRCTRL	0x20
#define VSC73XX_AGGRMSKS	0x30 /* Until 0x3f */
#define VSC73XX_DSTMASKS	0x40 /* Until 0x7f */
#define VSC73XX_SRCMASKS	0x80 /* Until 0x87 */
#define VSC73XX_CAPENAB		0xa0
#define VSC73XX_MACACCESS	0xb0
#define VSC73XX_IPMCACCESS	0xb1
#define VSC73XX_MACTINDX	0xc0
#define VSC73XX_VLANACCESS	0xd0
#define VSC73XX_VLANTIDX	0xe0
#define VSC73XX_AGENCTRL	0xf0
#define VSC73XX_CAPRST		0xff

#define VSC73XX_MACACCESS_CPU_COPY		BIT(14)
#define VSC73XX_MACACCESS_FWD_KILL		BIT(13)
#define VSC73XX_MACACCESS_IGNORE_VLAN		BIT(12)
#define VSC73XX_MACACCESS_AGED_FLAG		BIT(11)
#define VSC73XX_MACACCESS_VALID			BIT(10)
#define VSC73XX_MACACCESS_LOCKED		BIT(9)
#define VSC73XX_MACACCESS_DEST_IDX_MASK		GENMASK(8, 3)
#define VSC73XX_MACACCESS_CMD_MASK		GENMASK(2, 0)
#define VSC73XX_MACACCESS_CMD_IDLE		0
#define VSC73XX_MACACCESS_CMD_LEARN		1
#define VSC73XX_MACACCESS_CMD_FORGET		2
#define VSC73XX_MACACCESS_CMD_AGE_TABLE		3
#define VSC73XX_MACACCESS_CMD_FLUSH_TABLE	4
#define VSC73XX_MACACCESS_CMD_CLEAR_TABLE	5
#define VSC73XX_MACACCESS_CMD_READ_ENTRY	6
#define VSC73XX_MACACCESS_CMD_WRITE_ENTRY	7

#define VSC73XX_VLANACCESS_LEARN_DISABLED	BIT(30)
#define VSC73XX_VLANACCESS_VLAN_MIRROR		BIT(29)
#define VSC73XX_VLANACCESS_VLAN_SRC_CHECK	BIT(28)
#define VSC73XX_VLANACCESS_VLAN_PORT_MASK	GENMASK(9, 2)
#define VSC73XX_VLANACCESS_VLAN_TBL_CMD_MASK	GENMASK(2, 0)
#define VSC73XX_VLANACCESS_VLAN_TBL_CMD_IDLE	0
#define VSC73XX_VLANACCESS_VLAN_TBL_CMD_READ_ENTRY	1
#define VSC73XX_VLANACCESS_VLAN_TBL_CMD_WRITE_ENTRY	2
#define VSC73XX_VLANACCESS_VLAN_TBL_CMD_CLEAR_TABLE	3

/* MII block 3 registers */
#define VSC73XX_MII_STAT	0x0
#define VSC73XX_MII_CMD		0x1
#define VSC73XX_MII_DATA	0x2

/* Arbiter block 5 registers */
#define VSC73XX_ARBEMPTY		0x0c
#define VSC73XX_ARBDISC			0x0e
#define VSC73XX_SBACKWDROP		0x12
#define VSC73XX_DBACKWDROP		0x13
#define VSC73XX_ARBBURSTPROB		0x15

/* System block 7 registers */
#define VSC73XX_ICPU_SIPAD		0x01
#define VSC73XX_GMIIDELAY		0x05
#define VSC73XX_ICPU_CTRL		0x10
#define VSC73XX_ICPU_ADDR		0x11
#define VSC73XX_ICPU_SRAM		0x12
#define VSC73XX_HWSEM			0x13
#define VSC73XX_GLORESET		0x14
#define VSC73XX_ICPU_MBOX_VAL		0x15
#define VSC73XX_ICPU_MBOX_SET		0x16
#define VSC73XX_ICPU_MBOX_CLR		0x17
#define VSC73XX_CHIPID			0x18
#define VSC73XX_GPIO			0x34

#define VSC73XX_GMIIDELAY_GMII0_GTXDELAY_NONE	0
#define VSC73XX_GMIIDELAY_GMII0_GTXDELAY_1_4_NS	1
#define VSC73XX_GMIIDELAY_GMII0_GTXDELAY_1_7_NS	2
#define VSC73XX_GMIIDELAY_GMII0_GTXDELAY_2_0_NS	3

#define VSC73XX_GMIIDELAY_GMII0_RXDELAY_NONE	(0 << 4)
#define VSC73XX_GMIIDELAY_GMII0_RXDELAY_1_4_NS	(1 << 4)
#define VSC73XX_GMIIDELAY_GMII0_RXDELAY_1_7_NS	(2 << 4)
#define VSC73XX_GMIIDELAY_GMII0_RXDELAY_2_0_NS	(3 << 4)

#define VSC73XX_ICPU_CTRL_WATCHDOG_RST	BIT(31)
#define VSC73XX_ICPU_CTRL_CLK_DIV_MASK	GENMASK(12, 8)
#define VSC73XX_ICPU_CTRL_SRST_HOLD	BIT(7)
#define VSC73XX_ICPU_CTRL_ICPU_PI_EN	BIT(6)
#define VSC73XX_ICPU_CTRL_BOOT_EN	BIT(3)
#define VSC73XX_ICPU_CTRL_EXT_ACC_EN	BIT(2)
#define VSC73XX_ICPU_CTRL_CLK_EN	BIT(1)
#define VSC73XX_ICPU_CTRL_SRST		BIT(0)

#define VSC73XX_CHIPID_ID_SHIFT		12
#define VSC73XX_CHIPID_ID_MASK		0xffff
#define VSC73XX_CHIPID_REV_SHIFT	28
#define VSC73XX_CHIPID_REV_MASK		0xf
#define VSC73XX_CHIPID_ID_7385		0x7385
#define VSC73XX_CHIPID_ID_7388		0x7388
#define VSC73XX_CHIPID_ID_7395		0x7395
#define VSC73XX_CHIPID_ID_7398		0x7398

#define VSC73XX_GLORESET_STROBE		BIT(4)
#define VSC73XX_GLORESET_ICPU_LOCK	BIT(3)
#define VSC73XX_GLORESET_MEM_LOCK	BIT(2)
#define VSC73XX_GLORESET_PHY_RESET	BIT(1)
#define VSC73XX_GLORESET_MASTER_RESET	BIT(0)

#define VSC7385_CLOCK_DELAY		((3 << 4) | 3)
#define VSC7385_CLOCK_DELAY_MASK	((3 << 4) | 3)

#define VSC73XX_ICPU_CTRL_STOP	(VSC73XX_ICPU_CTRL_SRST_HOLD | \
				 VSC73XX_ICPU_CTRL_BOOT_EN | \
				 VSC73XX_ICPU_CTRL_EXT_ACC_EN)

#define VSC73XX_ICPU_CTRL_START	(VSC73XX_ICPU_CTRL_CLK_DIV | \
				 VSC73XX_ICPU_CTRL_BOOT_EN | \
				 VSC73XX_ICPU_CTRL_CLK_EN | \
				 VSC73XX_ICPU_CTRL_SRST)

#define IS_7385(a) ((a)->chipid == VSC73XX_CHIPID_ID_7385)
#define IS_7388(a) ((a)->chipid == VSC73XX_CHIPID_ID_7388)
#define IS_7395(a) ((a)->chipid == VSC73XX_CHIPID_ID_7395)
#define IS_7398(a) ((a)->chipid == VSC73XX_CHIPID_ID_7398)
#define IS_739X(a) (IS_7395(a) || IS_7398(a))

struct vsc73xx_counter {
	u8 counter;
	const char *name;
};

/* Counters are named according to the MIB standards where applicable.
 * Some counters are custom, non-standard. The standard counters are
 * named in accordance with RFC2819, RFC2021 and IEEE Std 802.3-2002 Annex
 * 30A Counters.
 */
static const struct vsc73xx_counter vsc73xx_rx_counters[] = {
	{ 0, "RxEtherStatsPkts" },
	{ 1, "RxBroadcast+MulticastPkts" }, /* non-standard counter */
	{ 2, "RxTotalErrorPackets" }, /* non-standard counter */
	{ 3, "RxEtherStatsBroadcastPkts" },
	{ 4, "RxEtherStatsMulticastPkts" },
	{ 5, "RxEtherStatsPkts64Octets" },
	{ 6, "RxEtherStatsPkts65to127Octets" },
	{ 7, "RxEtherStatsPkts128to255Octets" },
	{ 8, "RxEtherStatsPkts256to511Octets" },
	{ 9, "RxEtherStatsPkts512to1023Octets" },
	{ 10, "RxEtherStatsPkts1024to1518Octets" },
	{ 11, "RxJumboFrames" }, /* non-standard counter */
	{ 12, "RxaPauseMACControlFramesTransmitted" },
	{ 13, "RxFIFODrops" }, /* non-standard counter */
	{ 14, "RxBackwardDrops" }, /* non-standard counter */
	{ 15, "RxClassifierDrops" }, /* non-standard counter */
	{ 16, "RxEtherStatsCRCAlignErrors" },
	{ 17, "RxEtherStatsUndersizePkts" },
	{ 18, "RxEtherStatsOversizePkts" },
	{ 19, "RxEtherStatsFragments" },
	{ 20, "RxEtherStatsJabbers" },
	{ 21, "RxaMACControlFramesReceived" },
	/* 22-24 are undefined */
	{ 25, "RxaFramesReceivedOK" },
	{ 26, "RxQoSClass0" }, /* non-standard counter */
	{ 27, "RxQoSClass1" }, /* non-standard counter */
	{ 28, "RxQoSClass2" }, /* non-standard counter */
	{ 29, "RxQoSClass3" }, /* non-standard counter */
};

static const struct vsc73xx_counter vsc73xx_tx_counters[] = {
	{ 0, "TxEtherStatsPkts" },
	{ 1, "TxBroadcast+MulticastPkts" }, /* non-standard counter */
	{ 2, "TxTotalErrorPackets" }, /* non-standard counter */
	{ 3, "TxEtherStatsBroadcastPkts" },
	{ 4, "TxEtherStatsMulticastPkts" },
	{ 5, "TxEtherStatsPkts64Octets" },
	{ 6, "TxEtherStatsPkts65to127Octets" },
	{ 7, "TxEtherStatsPkts128to255Octets" },
	{ 8, "TxEtherStatsPkts256to511Octets" },
	{ 9, "TxEtherStatsPkts512to1023Octets" },
	{ 10, "TxEtherStatsPkts1024to1518Octets" },
	{ 11, "TxJumboFrames" }, /* non-standard counter */
	{ 12, "TxaPauseMACControlFramesTransmitted" },
	{ 13, "TxFIFODrops" }, /* non-standard counter */
	{ 14, "TxDrops" }, /* non-standard counter */
	{ 15, "TxEtherStatsCollisions" },
	{ 16, "TxEtherStatsCRCAlignErrors" },
	{ 17, "TxEtherStatsUndersizePkts" },
	{ 18, "TxEtherStatsOversizePkts" },
	{ 19, "TxEtherStatsFragments" },
	{ 20, "TxEtherStatsJabbers" },
	/* 21-24 are undefined */
	{ 25, "TxaFramesReceivedOK" },
	{ 26, "TxQoSClass0" }, /* non-standard counter */
	{ 27, "TxQoSClass1" }, /* non-standard counter */
	{ 28, "TxQoSClass2" }, /* non-standard counter */
	{ 29, "TxQoSClass3" }, /* non-standard counter */
};

int vsc73xx_is_addr_valid(u8 block, u8 subblock)
{
	switch (block) {
	case VSC73XX_BLOCK_MAC:
		switch (subblock) {
		case 0 ... 4:
		case 6:
			return 1;
		}
		break;

	case VSC73XX_BLOCK_ANALYZER:
	case VSC73XX_BLOCK_SYSTEM:
		switch (subblock) {
		case 0:
			return 1;
		}
		break;

	case VSC73XX_BLOCK_MII:
	case VSC73XX_BLOCK_CAPTURE:
	case VSC73XX_BLOCK_ARBITER:
		switch (subblock) {
		case 0 ... 1:
			return 1;
		}
		break;
	}

	return 0;
}
EXPORT_SYMBOL(vsc73xx_is_addr_valid);

static int vsc73xx_read(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
			u32 *val)
{
	return vsc->ops->read(vsc, block, subblock, reg, val);
}

static int vsc73xx_write(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
			 u32 val)
{
	return vsc->ops->write(vsc, block, subblock, reg, val);
}

static int vsc73xx_update_bits(struct vsc73xx *vsc, u8 block, u8 subblock,
			       u8 reg, u32 mask, u32 val)
{
	u32 tmp, orig;
	int ret;

	/* Same read-modify-write algorithm as e.g. regmap */
	ret = vsc73xx_read(vsc, block, subblock, reg, &orig);
	if (ret)
		return ret;
	tmp = orig & ~mask;
	tmp |= val & mask;
	return vsc73xx_write(vsc, block, subblock, reg, tmp);
}

static int vsc73xx_detect(struct vsc73xx *vsc)
{
	bool icpu_si_boot_en;
	bool icpu_pi_en;
	u32 val;
	u32 rev;
	int ret;
	u32 id;

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			   VSC73XX_ICPU_MBOX_VAL, &val);
	if (ret) {
		dev_err(vsc->dev, "unable to read mailbox (%d)\n", ret);
		return ret;
	}

	if (val == 0xffffffff) {
		dev_info(vsc->dev, "chip seems dead.\n");
		return -EAGAIN;
	}

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			   VSC73XX_CHIPID, &val);
	if (ret) {
		dev_err(vsc->dev, "unable to read chip id (%d)\n", ret);
		return ret;
	}

	id = (val >> VSC73XX_CHIPID_ID_SHIFT) &
		VSC73XX_CHIPID_ID_MASK;
	switch (id) {
	case VSC73XX_CHIPID_ID_7385:
	case VSC73XX_CHIPID_ID_7388:
	case VSC73XX_CHIPID_ID_7395:
	case VSC73XX_CHIPID_ID_7398:
		break;
	default:
		dev_err(vsc->dev, "unsupported chip, id=%04x\n", id);
		return -ENODEV;
	}

	vsc->chipid = id;
	rev = (val >> VSC73XX_CHIPID_REV_SHIFT) &
		VSC73XX_CHIPID_REV_MASK;
	dev_info(vsc->dev, "VSC%04X (rev: %d) switch found\n", id, rev);

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			   VSC73XX_ICPU_CTRL, &val);
	if (ret) {
		dev_err(vsc->dev, "unable to read iCPU control\n");
		return ret;
	}

	/* The iCPU can always be used but can boot in different ways.
	 * If it is initially disabled and has no external memory,
	 * we are in control and can do whatever we like, else we
	 * are probably in trouble (we need some way to communicate
	 * with the running firmware) so we bail out for now.
	 */
	icpu_pi_en = !!(val & VSC73XX_ICPU_CTRL_ICPU_PI_EN);
	icpu_si_boot_en = !!(val & VSC73XX_ICPU_CTRL_BOOT_EN);
	if (icpu_si_boot_en && icpu_pi_en) {
		dev_err(vsc->dev,
			"iCPU enabled boots from SI, has external memory\n");
		dev_err(vsc->dev, "no idea how to deal with this\n");
		return -ENODEV;
	}
	if (icpu_si_boot_en && !icpu_pi_en) {
		dev_err(vsc->dev,
			"iCPU enabled boots from PI/SI, no external memory\n");
		return -EAGAIN;
	}
	if (!icpu_si_boot_en && icpu_pi_en) {
		dev_err(vsc->dev,
			"iCPU enabled, boots from PI external memory\n");
		dev_err(vsc->dev, "no idea how to deal with this\n");
		return -ENODEV;
	}
	/* !icpu_si_boot_en && !cpu_pi_en */
	dev_info(vsc->dev, "iCPU disabled, no external memory\n");

	return 0;
}

static int vsc73xx_phy_read(struct dsa_switch *ds, int phy, int regnum)
{
	struct vsc73xx *vsc = ds->priv;
	u32 cmd;
	u32 val;
	int ret;

	/* Setting bit 26 means "read" */
	cmd = BIT(26) | (phy << 21) | (regnum << 16);
	ret = vsc73xx_write(vsc, VSC73XX_BLOCK_MII, 0, 1, cmd);
	if (ret)
		return ret;
	msleep(2);
	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_MII, 0, 2, &val);
	if (ret)
		return ret;
	if (val & BIT(16)) {
		dev_err(vsc->dev, "reading reg %02x from phy%d failed\n",
			regnum, phy);
		return -EIO;
	}
	val &= 0xFFFFU;

	dev_dbg(vsc->dev, "read reg %02x from phy%d = %04x\n",
		regnum, phy, val);

	return val;
}

static int vsc73xx_phy_write(struct dsa_switch *ds, int phy, int regnum,
			     u16 val)
{
	struct vsc73xx *vsc = ds->priv;
	u32 cmd;
	int ret;

	/* It was found through tedious experiments that this router
	 * chip really hates to have it's PHYs reset. They
	 * never recover if that happens: autonegotiation stops
	 * working after a reset. Just filter out this command.
	 * (Resetting the whole chip is OK.)
	 */
	if (regnum == 0 && (val & BIT(15))) {
		dev_info(vsc->dev, "reset PHY - disallowed\n");
		return 0;
	}

	cmd = (phy << 21) | (regnum << 16);
	ret = vsc73xx_write(vsc, VSC73XX_BLOCK_MII, 0, 1, cmd);
	if (ret)
		return ret;

	dev_dbg(vsc->dev, "write %04x to reg %02x in phy%d\n",
		val, regnum, phy);
	return 0;
}

static enum dsa_tag_protocol vsc73xx_get_tag_protocol(struct dsa_switch *ds,
						      int port,
						      enum dsa_tag_protocol mp)
{
	/* The switch internally uses a 8 byte header with length,
	 * source port, tag, LPA and priority. This is supposedly
	 * only accessible when operating the switch using the internal
	 * CPU or with an external CPU mapping the device in, but not
	 * when operating the switch over SPI and putting frames in/out
	 * on port 6 (the CPU port). So far we must assume that we
	 * cannot access the tag. (See "Internal frame header" section
	 * 3.9.1 in the manual.)
	 */
	return DSA_TAG_PROTO_NONE;
}

static int vsc73xx_setup(struct dsa_switch *ds)
{
	struct vsc73xx *vsc = ds->priv;
	int i;

	dev_info(vsc->dev, "set up the switch\n");

	/* Issue RESET */
	vsc73xx_write(vsc, VSC73XX_BLOCK_SYSTEM, 0, VSC73XX_GLORESET,
		      VSC73XX_GLORESET_MASTER_RESET);
	usleep_range(125, 200);

	/* Initialize memory, initialize RAM bank 0..15 except 6 and 7
	 * This sequence appears in the
	 * VSC7385 SparX-G5 datasheet section 6.6.1
	 * VSC7395 SparX-G5e datasheet section 6.6.1
	 * "initialization sequence".
	 * No explanation is given to the 0x1010400 magic number.
	 */
	for (i = 0; i <= 15; i++) {
		if (i != 6 && i != 7) {
			vsc73xx_write(vsc, VSC73XX_BLOCK_MEMINIT,
				      2,
				      0, 0x1010400 + i);
			mdelay(1);
		}
	}
	mdelay(30);

	/* Clear MAC table */
	vsc73xx_write(vsc, VSC73XX_BLOCK_ANALYZER, 0,
		      VSC73XX_MACACCESS,
		      VSC73XX_MACACCESS_CMD_CLEAR_TABLE);

	/* Clear VLAN table */
	vsc73xx_write(vsc, VSC73XX_BLOCK_ANALYZER, 0,
		      VSC73XX_VLANACCESS,
		      VSC73XX_VLANACCESS_VLAN_TBL_CMD_CLEAR_TABLE);

	msleep(40);

	/* Use 20KiB buffers on all ports on VSC7395
	 * The VSC7385 has 16KiB buffers and that is the
	 * default if we don't set this up explicitly.
	 * Port "31" is "all ports".
	 */
	if (IS_739X(vsc))
		vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, 0x1f,
			      VSC73XX_Q_MISC_CONF,
			      VSC73XX_Q_MISC_CONF_EXTENT_MEM);

	/* Put all ports into reset until enabled */
	for (i = 0; i < 7; i++) {
		if (i == 5)
			continue;
		vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, 4,
			      VSC73XX_MAC_CFG, VSC73XX_MAC_CFG_RESET);
	}

	/* MII delay, set both GTX and RX delay to 2 ns */
	vsc73xx_write(vsc, VSC73XX_BLOCK_SYSTEM, 0, VSC73XX_GMIIDELAY,
		      VSC73XX_GMIIDELAY_GMII0_GTXDELAY_2_0_NS |
		      VSC73XX_GMIIDELAY_GMII0_RXDELAY_2_0_NS);
	/* Enable reception of frames on all ports */
	vsc73xx_write(vsc, VSC73XX_BLOCK_ANALYZER, 0, VSC73XX_RECVMASK,
		      0x5f);
	/* IP multicast flood mask (table 144) */
	vsc73xx_write(vsc, VSC73XX_BLOCK_ANALYZER, 0, VSC73XX_IFLODMSK,
		      0xff);

	mdelay(50);

	/* Release reset from the internal PHYs */
	vsc73xx_write(vsc, VSC73XX_BLOCK_SYSTEM, 0, VSC73XX_GLORESET,
		      VSC73XX_GLORESET_PHY_RESET);

	udelay(4);

	return 0;
}

static void vsc73xx_init_port(struct vsc73xx *vsc, int port)
{
	u32 val;

	/* MAC configure, first reset the port and then write defaults */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_MAC_CFG,
		      VSC73XX_MAC_CFG_RESET);

	/* Take up the port in 1Gbit mode by default, this will be
	 * augmented after auto-negotiation on the PHY-facing
	 * ports.
	 */
	if (port == CPU_PORT)
		val = VSC73XX_MAC_CFG_1000M_F_RGMII;
	else
		val = VSC73XX_MAC_CFG_1000M_F_PHY;

	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_MAC_CFG,
		      val |
		      VSC73XX_MAC_CFG_TX_EN |
		      VSC73XX_MAC_CFG_RX_EN);

	/* Flow control for the CPU port:
	 * Use a zero delay pause frame when pause condition is left
	 * Obey pause control frames
	 */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_FCCONF,
		      VSC73XX_FCCONF_ZERO_PAUSE_EN |
		      VSC73XX_FCCONF_FLOW_CTRL_OBEY);

	/* Issue pause control frames on PHY facing ports.
	 * Allow early initiation of MAC transmission if the amount
	 * of egress data is below 512 bytes on CPU port.
	 * FIXME: enable 20KiB buffers?
	 */
	if (port == CPU_PORT)
		val = VSC73XX_Q_MISC_CONF_EARLY_TX_512;
	else
		val = VSC73XX_Q_MISC_CONF_MAC_PAUSE_MODE;
	val |= VSC73XX_Q_MISC_CONF_EXTENT_MEM;
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_Q_MISC_CONF,
		      val);

	/* Flow control MAC: a MAC address used in flow control frames */
	val = (vsc->addr[5] << 16) | (vsc->addr[4] << 8) | (vsc->addr[3]);
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_FCMACHI,
		      val);
	val = (vsc->addr[2] << 16) | (vsc->addr[1] << 8) | (vsc->addr[0]);
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_FCMACLO,
		      val);

	/* Tell the categorizer to forward pause frames, not control
	 * frame. Do not drop anything.
	 */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port,
		      VSC73XX_CAT_DROP,
		      VSC73XX_CAT_DROP_FWD_PAUSE_ENA);

	/* Clear all counters */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
		      port, VSC73XX_C_RX0, 0);
}

static void vsc73xx_adjust_enable_port(struct vsc73xx *vsc,
				       int port, struct phy_device *phydev,
				       u32 initval)
{
	u32 val = initval;
	u8 seed;

	/* Reset this port FIXME: break out subroutine */
	val |= VSC73XX_MAC_CFG_RESET;
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port, VSC73XX_MAC_CFG, val);

	/* Seed the port randomness with randomness */
	get_random_bytes(&seed, 1);
	val |= seed << VSC73XX_MAC_CFG_SEED_OFFSET;
	val |= VSC73XX_MAC_CFG_SEED_LOAD;
	val |= VSC73XX_MAC_CFG_WEXC_DIS;
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port, VSC73XX_MAC_CFG, val);

	/* Flow control for the PHY facing ports:
	 * Use a zero delay pause frame when pause condition is left
	 * Obey pause control frames
	 * When generating pause frames, use 0xff as pause value
	 */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port, VSC73XX_FCCONF,
		      VSC73XX_FCCONF_ZERO_PAUSE_EN |
		      VSC73XX_FCCONF_FLOW_CTRL_OBEY |
		      0xff);

	/* Disallow backward dropping of frames from this port */
	vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ARBITER, 0,
			    VSC73XX_SBACKWDROP, BIT(port), 0);

	/* Enable TX, RX, deassert reset, stop loading seed */
	vsc73xx_update_bits(vsc, VSC73XX_BLOCK_MAC, port,
			    VSC73XX_MAC_CFG,
			    VSC73XX_MAC_CFG_RESET | VSC73XX_MAC_CFG_SEED_LOAD |
			    VSC73XX_MAC_CFG_TX_EN | VSC73XX_MAC_CFG_RX_EN,
			    VSC73XX_MAC_CFG_TX_EN | VSC73XX_MAC_CFG_RX_EN);
}

static void vsc73xx_adjust_link(struct dsa_switch *ds, int port,
				struct phy_device *phydev)
{
	struct vsc73xx *vsc = ds->priv;
	u32 val;

	/* Special handling of the CPU-facing port */
	if (port == CPU_PORT) {
		/* Other ports are already initialized but not this one */
		vsc73xx_init_port(vsc, CPU_PORT);
		/* Select the external port for this interface (EXT_PORT)
		 * Enable the GMII GTX external clock
		 * Use double data rate (DDR mode)
		 */
		vsc73xx_write(vsc, VSC73XX_BLOCK_MAC,
			      CPU_PORT,
			      VSC73XX_ADVPORTM,
			      VSC73XX_ADVPORTM_EXT_PORT |
			      VSC73XX_ADVPORTM_ENA_GTX |
			      VSC73XX_ADVPORTM_DDR_MODE);
	}

	/* This is the MAC confiuration that always need to happen
	 * after a PHY or the CPU port comes up or down.
	 */
	if (!phydev->link) {
		int maxloop = 10;

		dev_dbg(vsc->dev, "port %d: went down\n",
			port);

		/* Disable RX on this port */
		vsc73xx_update_bits(vsc, VSC73XX_BLOCK_MAC, port,
				    VSC73XX_MAC_CFG,
				    VSC73XX_MAC_CFG_RX_EN, 0);

		/* Discard packets */
		vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ARBITER, 0,
				    VSC73XX_ARBDISC, BIT(port), BIT(port));

		/* Wait until queue is empty */
		vsc73xx_read(vsc, VSC73XX_BLOCK_ARBITER, 0,
			     VSC73XX_ARBEMPTY, &val);
		while (!(val & BIT(port))) {
			msleep(1);
			vsc73xx_read(vsc, VSC73XX_BLOCK_ARBITER, 0,
				     VSC73XX_ARBEMPTY, &val);
			if (--maxloop == 0) {
				dev_err(vsc->dev,
					"timeout waiting for block arbiter\n");
				/* Continue anyway */
				break;
			}
		}

		/* Put this port into reset */
		vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port, VSC73XX_MAC_CFG,
			      VSC73XX_MAC_CFG_RESET);

		/* Accept packets again */
		vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ARBITER, 0,
				    VSC73XX_ARBDISC, BIT(port), 0);

		/* Allow backward dropping of frames from this port */
		vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ARBITER, 0,
				    VSC73XX_SBACKWDROP, BIT(port), BIT(port));

		/* Receive mask (disable forwarding) */
		vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ANALYZER, 0,
				    VSC73XX_RECVMASK, BIT(port), 0);

		return;
	}

	/* Figure out what speed was negotiated */
	if (phydev->speed == SPEED_1000) {
		dev_dbg(vsc->dev, "port %d: 1000 Mbit mode full duplex\n",
			port);

		/* Set up default for internal port or external RGMII */
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII)
			val = VSC73XX_MAC_CFG_1000M_F_RGMII;
		else
			val = VSC73XX_MAC_CFG_1000M_F_PHY;
		vsc73xx_adjust_enable_port(vsc, port, phydev, val);
	} else if (phydev->speed == SPEED_100) {
		if (phydev->duplex == DUPLEX_FULL) {
			val = VSC73XX_MAC_CFG_100_10M_F_PHY;
			dev_dbg(vsc->dev,
				"port %d: 100 Mbit full duplex mode\n",
				port);
		} else {
			val = VSC73XX_MAC_CFG_100_10M_H_PHY;
			dev_dbg(vsc->dev,
				"port %d: 100 Mbit half duplex mode\n",
				port);
		}
		vsc73xx_adjust_enable_port(vsc, port, phydev, val);
	} else if (phydev->speed == SPEED_10) {
		if (phydev->duplex == DUPLEX_FULL) {
			val = VSC73XX_MAC_CFG_100_10M_F_PHY;
			dev_dbg(vsc->dev,
				"port %d: 10 Mbit full duplex mode\n",
				port);
		} else {
			val = VSC73XX_MAC_CFG_100_10M_H_PHY;
			dev_dbg(vsc->dev,
				"port %d: 10 Mbit half duplex mode\n",
				port);
		}
		vsc73xx_adjust_enable_port(vsc, port, phydev, val);
	} else {
		dev_err(vsc->dev,
			"could not adjust link: unknown speed\n");
	}

	/* Enable port (forwarding) in the receieve mask */
	vsc73xx_update_bits(vsc, VSC73XX_BLOCK_ANALYZER, 0,
			    VSC73XX_RECVMASK, BIT(port), BIT(port));
}

static int vsc73xx_port_enable(struct dsa_switch *ds, int port,
			       struct phy_device *phy)
{
	struct vsc73xx *vsc = ds->priv;

	dev_info(vsc->dev, "enable port %d\n", port);
	vsc73xx_init_port(vsc, port);

	return 0;
}

static void vsc73xx_port_disable(struct dsa_switch *ds, int port)
{
	struct vsc73xx *vsc = ds->priv;

	/* Just put the port into reset */
	vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port,
		      VSC73XX_MAC_CFG, VSC73XX_MAC_CFG_RESET);
}

static const struct vsc73xx_counter *
vsc73xx_find_counter(struct vsc73xx *vsc,
		     u8 counter,
		     bool tx)
{
	const struct vsc73xx_counter *cnts;
	int num_cnts;
	int i;

	if (tx) {
		cnts = vsc73xx_tx_counters;
		num_cnts = ARRAY_SIZE(vsc73xx_tx_counters);
	} else {
		cnts = vsc73xx_rx_counters;
		num_cnts = ARRAY_SIZE(vsc73xx_rx_counters);
	}

	for (i = 0; i < num_cnts; i++) {
		const struct vsc73xx_counter *cnt;

		cnt = &cnts[i];
		if (cnt->counter == counter)
			return cnt;
	}

	return NULL;
}

static void vsc73xx_get_strings(struct dsa_switch *ds, int port, u32 stringset,
				uint8_t *data)
{
	const struct vsc73xx_counter *cnt;
	struct vsc73xx *vsc = ds->priv;
	u8 indices[6];
	u8 *buf = data;
	int i;
	u32 val;
	int ret;

	if (stringset != ETH_SS_STATS)
		return;

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_MAC, port,
			   VSC73XX_C_CFG, &val);
	if (ret)
		return;

	indices[0] = (val & 0x1f); /* RX counter 0 */
	indices[1] = ((val >> 5) & 0x1f); /* RX counter 1 */
	indices[2] = ((val >> 10) & 0x1f); /* RX counter 2 */
	indices[3] = ((val >> 16) & 0x1f); /* TX counter 0 */
	indices[4] = ((val >> 21) & 0x1f); /* TX counter 1 */
	indices[5] = ((val >> 26) & 0x1f); /* TX counter 2 */

	/* The first counters is the RX octets */
	ethtool_puts(&buf, "RxEtherStatsOctets");

	/* Each port supports recording 3 RX counters and 3 TX counters,
	 * figure out what counters we use in this set-up and return the
	 * names of them. The hardware default counters will be number of
	 * packets on RX/TX, combined broadcast+multicast packets RX/TX and
	 * total error packets RX/TX.
	 */
	for (i = 0; i < 3; i++) {
		cnt = vsc73xx_find_counter(vsc, indices[i], false);
		ethtool_puts(&buf, cnt ? cnt->name : "");
	}

	/* TX stats begins with the number of TX octets */
	ethtool_puts(&buf, "TxEtherStatsOctets");

	for (i = 3; i < 6; i++) {
		cnt = vsc73xx_find_counter(vsc, indices[i], true);
		ethtool_puts(&buf, cnt ? cnt->name : "");

	}
}

static int vsc73xx_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	/* We only support SS_STATS */
	if (sset != ETH_SS_STATS)
		return 0;
	/* RX and TX packets, then 3 RX counters, 3 TX counters */
	return 8;
}

static void vsc73xx_get_ethtool_stats(struct dsa_switch *ds, int port,
				      uint64_t *data)
{
	struct vsc73xx *vsc = ds->priv;
	u8 regs[] = {
		VSC73XX_RXOCT,
		VSC73XX_C_RX0,
		VSC73XX_C_RX1,
		VSC73XX_C_RX2,
		VSC73XX_TXOCT,
		VSC73XX_C_TX0,
		VSC73XX_C_TX1,
		VSC73XX_C_TX2,
	};
	u32 val;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = vsc73xx_read(vsc, VSC73XX_BLOCK_MAC, port,
				   regs[i], &val);
		if (ret) {
			dev_err(vsc->dev, "error reading counter %d\n", i);
			return;
		}
		data[i] = val;
	}
}

static int vsc73xx_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct vsc73xx *vsc = ds->priv;

	return vsc73xx_write(vsc, VSC73XX_BLOCK_MAC, port,
			     VSC73XX_MAXLEN, new_mtu + ETH_HLEN + ETH_FCS_LEN);
}

/* According to application not "VSC7398 Jumbo Frames" setting
 * up the frame size to 9.6 KB does not affect the performance on standard
 * frames. It is clear from the application note that
 * "9.6 kilobytes" == 9600 bytes.
 */
static int vsc73xx_get_max_mtu(struct dsa_switch *ds, int port)
{
	return 9600 - ETH_HLEN - ETH_FCS_LEN;
}

static void vsc73xx_phylink_get_caps(struct dsa_switch *dsa, int port,
				     struct phylink_config *config)
{
	unsigned long *interfaces = config->supported_interfaces;

	if (port == 5)
		return;

	if (port == CPU_PORT) {
		__set_bit(PHY_INTERFACE_MODE_MII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_REVMII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_GMII, interfaces);
		__set_bit(PHY_INTERFACE_MODE_RGMII, interfaces);
	}

	if (port <= 4) {
		/* Internal PHYs */
		__set_bit(PHY_INTERFACE_MODE_INTERNAL, interfaces);
		/* phylib default */
		__set_bit(PHY_INTERFACE_MODE_GMII, interfaces);
	}

	config->mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 | MAC_1000;
}

static const struct dsa_switch_ops vsc73xx_ds_ops = {
	.get_tag_protocol = vsc73xx_get_tag_protocol,
	.setup = vsc73xx_setup,
	.phy_read = vsc73xx_phy_read,
	.phy_write = vsc73xx_phy_write,
	.adjust_link = vsc73xx_adjust_link,
	.get_strings = vsc73xx_get_strings,
	.get_ethtool_stats = vsc73xx_get_ethtool_stats,
	.get_sset_count = vsc73xx_get_sset_count,
	.port_enable = vsc73xx_port_enable,
	.port_disable = vsc73xx_port_disable,
	.port_change_mtu = vsc73xx_change_mtu,
	.port_max_mtu = vsc73xx_get_max_mtu,
	.phylink_get_caps = vsc73xx_phylink_get_caps,
};

static int vsc73xx_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct vsc73xx *vsc = gpiochip_get_data(chip);
	u32 val;
	int ret;

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			   VSC73XX_GPIO, &val);
	if (ret)
		return ret;

	return !!(val & BIT(offset));
}

static void vsc73xx_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int val)
{
	struct vsc73xx *vsc = gpiochip_get_data(chip);
	u32 tmp = val ? BIT(offset) : 0;

	vsc73xx_update_bits(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			    VSC73XX_GPIO, BIT(offset), tmp);
}

static int vsc73xx_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int offset, int val)
{
	struct vsc73xx *vsc = gpiochip_get_data(chip);
	u32 tmp = val ? BIT(offset) : 0;

	return vsc73xx_update_bits(vsc, VSC73XX_BLOCK_SYSTEM, 0,
				   VSC73XX_GPIO, BIT(offset + 4) | BIT(offset),
				   BIT(offset + 4) | tmp);
}

static int vsc73xx_gpio_direction_input(struct gpio_chip *chip,
					unsigned int offset)
{
	struct vsc73xx *vsc = gpiochip_get_data(chip);

	return  vsc73xx_update_bits(vsc, VSC73XX_BLOCK_SYSTEM, 0,
				    VSC73XX_GPIO, BIT(offset + 4),
				    0);
}

static int vsc73xx_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct vsc73xx *vsc = gpiochip_get_data(chip);
	u32 val;
	int ret;

	ret = vsc73xx_read(vsc, VSC73XX_BLOCK_SYSTEM, 0,
			   VSC73XX_GPIO, &val);
	if (ret)
		return ret;

	return !(val & BIT(offset + 4));
}

static int vsc73xx_gpio_probe(struct vsc73xx *vsc)
{
	int ret;

	vsc->gc.label = devm_kasprintf(vsc->dev, GFP_KERNEL, "VSC%04x",
				       vsc->chipid);
	if (!vsc->gc.label)
		return -ENOMEM;
	vsc->gc.ngpio = 4;
	vsc->gc.owner = THIS_MODULE;
	vsc->gc.parent = vsc->dev;
	vsc->gc.base = -1;
	vsc->gc.get = vsc73xx_gpio_get;
	vsc->gc.set = vsc73xx_gpio_set;
	vsc->gc.direction_input = vsc73xx_gpio_direction_input;
	vsc->gc.direction_output = vsc73xx_gpio_direction_output;
	vsc->gc.get_direction = vsc73xx_gpio_get_direction;
	vsc->gc.can_sleep = true;
	ret = devm_gpiochip_add_data(vsc->dev, &vsc->gc, vsc);
	if (ret) {
		dev_err(vsc->dev, "unable to register GPIO chip\n");
		return ret;
	}
	return 0;
}

int vsc73xx_probe(struct vsc73xx *vsc)
{
	struct device *dev = vsc->dev;
	int ret;

	/* Release reset, if any */
	vsc->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(vsc->reset)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(vsc->reset);
	}
	if (vsc->reset)
		/* Wait 20ms according to datasheet table 245 */
		msleep(20);

	ret = vsc73xx_detect(vsc);
	if (ret == -EAGAIN) {
		dev_err(vsc->dev,
			"Chip seems to be out of control. Assert reset and try again.\n");
		gpiod_set_value_cansleep(vsc->reset, 1);
		/* Reset pulse should be 20ns minimum, according to datasheet
		 * table 245, so 10us should be fine
		 */
		usleep_range(10, 100);
		gpiod_set_value_cansleep(vsc->reset, 0);
		/* Wait 20ms according to datasheet table 245 */
		msleep(20);
		ret = vsc73xx_detect(vsc);
	}
	if (ret) {
		dev_err(dev, "no chip found (%d)\n", ret);
		return -ENODEV;
	}

	eth_random_addr(vsc->addr);
	dev_info(vsc->dev,
		 "MAC for control frames: %02X:%02X:%02X:%02X:%02X:%02X\n",
		 vsc->addr[0], vsc->addr[1], vsc->addr[2],
		 vsc->addr[3], vsc->addr[4], vsc->addr[5]);

	/* The VSC7395 switch chips have 5+1 ports which means 5
	 * ordinary ports and a sixth CPU port facing the processor
	 * with an RGMII interface. These ports are numbered 0..4
	 * and 6, so they leave a "hole" in the port map for port 5,
	 * which is invalid.
	 *
	 * The VSC7398 has 8 ports, port 7 is again the CPU port.
	 *
	 * We allocate 8 ports and avoid access to the nonexistant
	 * ports.
	 */
	vsc->ds = devm_kzalloc(dev, sizeof(*vsc->ds), GFP_KERNEL);
	if (!vsc->ds)
		return -ENOMEM;

	vsc->ds->dev = dev;
	vsc->ds->num_ports = 8;
	vsc->ds->priv = vsc;

	vsc->ds->ops = &vsc73xx_ds_ops;
	ret = dsa_register_switch(vsc->ds);
	if (ret) {
		dev_err(dev, "unable to register switch (%d)\n", ret);
		return ret;
	}

	ret = vsc73xx_gpio_probe(vsc);
	if (ret) {
		dsa_unregister_switch(vsc->ds);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(vsc73xx_probe);

void vsc73xx_remove(struct vsc73xx *vsc)
{
	dsa_unregister_switch(vsc->ds);
	gpiod_set_value(vsc->reset, 1);
}
EXPORT_SYMBOL(vsc73xx_remove);

void vsc73xx_shutdown(struct vsc73xx *vsc)
{
	dsa_switch_shutdown(vsc->ds);
}
EXPORT_SYMBOL(vsc73xx_shutdown);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Vitesse VSC7385/7388/7395/7398 driver");
MODULE_LICENSE("GPL v2");
