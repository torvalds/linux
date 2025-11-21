// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/marvell.c
 *
 * Driver for Marvell PHYs
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * Copyright (c) 2013 Michael Stapelberg <michael@stapelberg.de>
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/phy.h>
#include <linux/marvell_phy.h>
#include <linux/bitfield.h>
#include <linux/of.h>
#include <linux/sfp.h>

#include <linux/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

#define MII_MARVELL_PHY_PAGE		22
#define MII_MARVELL_COPPER_PAGE		0x00
#define MII_MARVELL_FIBER_PAGE		0x01
#define MII_MARVELL_MSCR_PAGE		0x02
#define MII_MARVELL_LED_PAGE		0x03
#define MII_MARVELL_VCT5_PAGE		0x05
#define MII_MARVELL_MISC_TEST_PAGE	0x06
#define MII_MARVELL_VCT7_PAGE		0x07
#define MII_MARVELL_WOL_PAGE		0x11
#define MII_MARVELL_MODE_PAGE		0x12

#define MII_M1011_IEVENT		0x13
#define MII_M1011_IEVENT_CLEAR		0x0000

#define MII_M1011_IMASK			0x12
#define MII_M1011_IMASK_INIT		0x6400
#define MII_M1011_IMASK_CLEAR		0x0000

#define MII_M1011_PHY_SCR			0x10
#define MII_M1011_PHY_SCR_DOWNSHIFT_EN		BIT(11)
#define MII_M1011_PHY_SCR_DOWNSHIFT_MASK	GENMASK(14, 12)
#define MII_M1011_PHY_SCR_DOWNSHIFT_MAX		8
#define MII_M1011_PHY_SCR_MDI			(0x0 << 5)
#define MII_M1011_PHY_SCR_MDI_X			(0x1 << 5)
#define MII_M1011_PHY_SCR_AUTO_CROSS		(0x3 << 5)

#define MII_M1011_PHY_SSR			0x11
#define MII_M1011_PHY_SSR_DOWNSHIFT		BIT(5)

#define MII_M1111_PHY_LED_CONTROL	0x18
#define MII_M1111_PHY_LED_DIRECT	0x4100
#define MII_M1111_PHY_LED_COMBINE	0x411c
#define MII_M1111_PHY_EXT_CR		0x14
#define MII_M1111_PHY_EXT_CR_DOWNSHIFT_MASK	GENMASK(11, 9)
#define MII_M1111_PHY_EXT_CR_DOWNSHIFT_MAX	8
#define MII_M1111_PHY_EXT_CR_DOWNSHIFT_EN	BIT(8)
#define MII_M1111_RGMII_RX_DELAY	BIT(7)
#define MII_M1111_RGMII_TX_DELAY	BIT(1)
#define MII_M1111_PHY_EXT_SR		0x1b

#define MII_M1111_HWCFG_MODE_MASK		0xf
#define MII_M1111_HWCFG_MODE_FIBER_RGMII	0x3
#define MII_M1111_HWCFG_MODE_SGMII_NO_CLK	0x4
#define MII_M1111_HWCFG_MODE_RTBI		0x7
#define MII_M1111_HWCFG_MODE_COPPER_1000X_AN	0x8
#define MII_M1111_HWCFG_MODE_COPPER_RTBI	0x9
#define MII_M1111_HWCFG_MODE_COPPER_RGMII	0xb
#define MII_M1111_HWCFG_MODE_COPPER_1000X_NOAN	0xc
#define MII_M1111_HWCFG_SERIAL_AN_BYPASS	BIT(12)
#define MII_M1111_HWCFG_FIBER_COPPER_RES	BIT(13)
#define MII_M1111_HWCFG_FIBER_COPPER_AUTO	BIT(15)

#define MII_88E1121_PHY_MSCR_REG	21
#define MII_88E1121_PHY_MSCR_RX_DELAY	BIT(5)
#define MII_88E1121_PHY_MSCR_TX_DELAY	BIT(4)
#define MII_88E1121_PHY_MSCR_DELAY_MASK	(BIT(5) | BIT(4))

#define MII_88E1121_MISC_TEST				0x1a
#define MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK	0x1f00
#define MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT	8
#define MII_88E1510_MISC_TEST_TEMP_IRQ_EN		BIT(7)
#define MII_88E1510_MISC_TEST_TEMP_IRQ			BIT(6)
#define MII_88E1121_MISC_TEST_TEMP_SENSOR_EN		BIT(5)
#define MII_88E1121_MISC_TEST_TEMP_MASK			0x1f

#define MII_88E1510_TEMP_SENSOR		0x1b
#define MII_88E1510_TEMP_SENSOR_MASK	0xff

#define MII_88E1540_COPPER_CTRL3	0x1a
#define MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_MASK	GENMASK(11, 10)
#define MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_00MS	0
#define MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_10MS	1
#define MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_20MS	2
#define MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_40MS	3
#define MII_88E1540_COPPER_CTRL3_FAST_LINK_DOWN		BIT(9)

#define MII_88E6390_MISC_TEST		0x1b
#define MII_88E6390_MISC_TEST_TEMP_SENSOR_ENABLE_SAMPLE_1S	(0x0 << 14)
#define MII_88E6390_MISC_TEST_TEMP_SENSOR_ENABLE		(0x1 << 14)
#define MII_88E6390_MISC_TEST_TEMP_SENSOR_ENABLE_ONESHOT	(0x2 << 14)
#define MII_88E6390_MISC_TEST_TEMP_SENSOR_DISABLE		(0x3 << 14)
#define MII_88E6390_MISC_TEST_TEMP_SENSOR_MASK			(0x3 << 14)
#define MII_88E6393_MISC_TEST_SAMPLES_2048	(0x0 << 11)
#define MII_88E6393_MISC_TEST_SAMPLES_4096	(0x1 << 11)
#define MII_88E6393_MISC_TEST_SAMPLES_8192	(0x2 << 11)
#define MII_88E6393_MISC_TEST_SAMPLES_16384	(0x3 << 11)
#define MII_88E6393_MISC_TEST_SAMPLES_MASK	(0x3 << 11)
#define MII_88E6393_MISC_TEST_RATE_2_3MS	(0x5 << 8)
#define MII_88E6393_MISC_TEST_RATE_6_4MS	(0x6 << 8)
#define MII_88E6393_MISC_TEST_RATE_11_9MS	(0x7 << 8)
#define MII_88E6393_MISC_TEST_RATE_MASK		(0x7 << 8)

#define MII_88E6390_TEMP_SENSOR		0x1c
#define MII_88E6393_TEMP_SENSOR_THRESHOLD_MASK	0xff00
#define MII_88E6393_TEMP_SENSOR_THRESHOLD_SHIFT	8
#define MII_88E6390_TEMP_SENSOR_MASK		0xff
#define MII_88E6390_TEMP_SENSOR_SAMPLES		10

#define MII_88E1318S_PHY_MSCR1_REG	16
#define MII_88E1318S_PHY_MSCR1_PAD_ODD	BIT(6)

/* Copper Specific Interrupt Enable Register */
#define MII_88E1318S_PHY_CSIER				0x12
/* WOL Event Interrupt Enable */
#define MII_88E1318S_PHY_CSIER_WOL_EIE			BIT(7)

#define MII_88E1318S_PHY_LED_FUNC		0x10
#define MII_88E1318S_PHY_LED_FUNC_OFF		(0x8)
#define MII_88E1318S_PHY_LED_FUNC_ON		(0x9)
#define MII_88E1318S_PHY_LED_FUNC_HI_Z		(0xa)
#define MII_88E1318S_PHY_LED_FUNC_BLINK		(0xb)
#define MII_88E1318S_PHY_LED_TCR		0x12
#define MII_88E1318S_PHY_LED_TCR_FORCE_INT	BIT(15)
#define MII_88E1318S_PHY_LED_TCR_INTn_ENABLE	BIT(7)
#define MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW	BIT(11)

/* Magic Packet MAC address registers */
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD2		0x17
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD1		0x18
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD0		0x19

#define MII_88E1318S_PHY_WOL_CTRL				0x10
#define MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS		BIT(12)
#define MII_88E1318S_PHY_WOL_CTRL_LINK_UP_ENABLE		BIT(13)
#define MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE	BIT(14)

#define MII_PHY_LED_CTRL	        16
#define MII_88E1121_PHY_LED_DEF		0x0030
#define MII_88E1510_PHY_LED_DEF		0x1177
#define MII_88E1510_PHY_LED0_LINK_LED1_ACTIVE	0x1040

#define MII_M1011_PHY_STATUS		0x11
#define MII_M1011_PHY_STATUS_1000	0x8000
#define MII_M1011_PHY_STATUS_100	0x4000
#define MII_M1011_PHY_STATUS_SPD_MASK	0xc000
#define MII_M1011_PHY_STATUS_FULLDUPLEX	0x2000
#define MII_M1011_PHY_STATUS_RESOLVED	0x0800
#define MII_M1011_PHY_STATUS_LINK	0x0400
#define MII_M1011_PHY_STATUS_MDIX	BIT(6)

#define MII_88E3016_PHY_SPEC_CTRL	0x10
#define MII_88E3016_DISABLE_SCRAMBLER	0x0200
#define MII_88E3016_AUTO_MDIX_CROSSOVER	0x0030

#define MII_88E1510_GEN_CTRL_REG_1		0x14
#define MII_88E1510_GEN_CTRL_REG_1_MODE_MASK	0x7
#define MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII	0x0	/* RGMII to copper */
#define MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII	0x1	/* SGMII to copper */
/* RGMII to 1000BASE-X */
#define MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_1000X	0x2
/* RGMII to 100BASE-FX */
#define MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_100FX	0x3
/* RGMII to SGMII */
#define MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_SGMII	0x4
#define MII_88E1510_GEN_CTRL_REG_1_RESET	0x8000	/* Soft reset */

#define MII_88E1510_MSCR_2		0x15

#define MII_VCT5_TX_RX_MDI0_COUPLING	0x10
#define MII_VCT5_TX_RX_MDI1_COUPLING	0x11
#define MII_VCT5_TX_RX_MDI2_COUPLING	0x12
#define MII_VCT5_TX_RX_MDI3_COUPLING	0x13
#define MII_VCT5_TX_RX_AMPLITUDE_MASK	0x7f00
#define MII_VCT5_TX_RX_AMPLITUDE_SHIFT	8
#define MII_VCT5_TX_RX_COUPLING_POSITIVE_REFLECTION	BIT(15)

#define MII_VCT5_CTRL				0x17
#define MII_VCT5_CTRL_ENABLE				BIT(15)
#define MII_VCT5_CTRL_COMPLETE				BIT(14)
#define MII_VCT5_CTRL_TX_SAME_CHANNEL			(0x0 << 11)
#define MII_VCT5_CTRL_TX0_CHANNEL			(0x4 << 11)
#define MII_VCT5_CTRL_TX1_CHANNEL			(0x5 << 11)
#define MII_VCT5_CTRL_TX2_CHANNEL			(0x6 << 11)
#define MII_VCT5_CTRL_TX3_CHANNEL			(0x7 << 11)
#define MII_VCT5_CTRL_SAMPLES_2				(0x0 << 8)
#define MII_VCT5_CTRL_SAMPLES_4				(0x1 << 8)
#define MII_VCT5_CTRL_SAMPLES_8				(0x2 << 8)
#define MII_VCT5_CTRL_SAMPLES_16			(0x3 << 8)
#define MII_VCT5_CTRL_SAMPLES_32			(0x4 << 8)
#define MII_VCT5_CTRL_SAMPLES_64			(0x5 << 8)
#define MII_VCT5_CTRL_SAMPLES_128			(0x6 << 8)
#define MII_VCT5_CTRL_SAMPLES_DEFAULT			(0x6 << 8)
#define MII_VCT5_CTRL_SAMPLES_256			(0x7 << 8)
#define MII_VCT5_CTRL_SAMPLES_SHIFT			8
#define MII_VCT5_CTRL_MODE_MAXIMUM_PEEK			(0x0 << 6)
#define MII_VCT5_CTRL_MODE_FIRST_LAST_PEEK		(0x1 << 6)
#define MII_VCT5_CTRL_MODE_OFFSET			(0x2 << 6)
#define MII_VCT5_CTRL_SAMPLE_POINT			(0x3 << 6)
#define MII_VCT5_CTRL_PEEK_HYST_DEFAULT			3

#define MII_VCT5_SAMPLE_POINT_DISTANCE		0x18
#define MII_VCT5_SAMPLE_POINT_DISTANCE_MAX	511
#define MII_VCT5_TX_PULSE_CTRL			0x1c
#define MII_VCT5_TX_PULSE_CTRL_DONT_WAIT_LINK_DOWN	BIT(12)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_128nS	(0x0 << 10)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_96nS		(0x1 << 10)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_64nS		(0x2 << 10)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_32nS		(0x3 << 10)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_SHIFT	10
#define MII_VCT5_TX_PULSE_CTRL_PULSE_AMPLITUDE_1000mV	(0x0 << 8)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_AMPLITUDE_750mV	(0x1 << 8)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_AMPLITUDE_500mV	(0x2 << 8)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_AMPLITUDE_250mV	(0x3 << 8)
#define MII_VCT5_TX_PULSE_CTRL_PULSE_AMPLITUDE_SHIFT	8
#define MII_VCT5_TX_PULSE_CTRL_MAX_AMP			BIT(7)
#define MII_VCT5_TX_PULSE_CTRL_GT_140m_46_86mV		(0x6 << 0)

/* For TDR measurements less than 11 meters, a short pulse should be
 * used.
 */
#define TDR_SHORT_CABLE_LENGTH	11

#define MII_VCT7_PAIR_0_DISTANCE	0x10
#define MII_VCT7_PAIR_1_DISTANCE	0x11
#define MII_VCT7_PAIR_2_DISTANCE	0x12
#define MII_VCT7_PAIR_3_DISTANCE	0x13

#define MII_VCT7_RESULTS	0x14
#define MII_VCT7_RESULTS_PAIR3_MASK	0xf000
#define MII_VCT7_RESULTS_PAIR2_MASK	0x0f00
#define MII_VCT7_RESULTS_PAIR1_MASK	0x00f0
#define MII_VCT7_RESULTS_PAIR0_MASK	0x000f
#define MII_VCT7_RESULTS_PAIR3_SHIFT	12
#define MII_VCT7_RESULTS_PAIR2_SHIFT	8
#define MII_VCT7_RESULTS_PAIR1_SHIFT	4
#define MII_VCT7_RESULTS_PAIR0_SHIFT	0
#define MII_VCT7_RESULTS_INVALID	0
#define MII_VCT7_RESULTS_OK		1
#define MII_VCT7_RESULTS_OPEN		2
#define MII_VCT7_RESULTS_SAME_SHORT	3
#define MII_VCT7_RESULTS_CROSS_SHORT	4
#define MII_VCT7_RESULTS_BUSY		9

#define MII_VCT7_CTRL		0x15
#define MII_VCT7_CTRL_RUN_NOW			BIT(15)
#define MII_VCT7_CTRL_RUN_ANEG			BIT(14)
#define MII_VCT7_CTRL_DISABLE_CROSS		BIT(13)
#define MII_VCT7_CTRL_RUN_AFTER_BREAK_LINK	BIT(12)
#define MII_VCT7_CTRL_IN_PROGRESS		BIT(11)
#define MII_VCT7_CTRL_METERS			BIT(10)
#define MII_VCT7_CTRL_CENTIMETERS		0

#define MII_VCT_TXPINS			0x1A
#define MII_VCT_RXPINS			0x1B
#define MII_VCT_SR			0x1C
#define MII_VCT_TXPINS_ENVCT		BIT(15)
#define MII_VCT_TXRXPINS_VCTTST		GENMASK(14, 13)
#define MII_VCT_TXRXPINS_VCTTST_SHIFT	13
#define MII_VCT_TXRXPINS_VCTTST_OK	0
#define MII_VCT_TXRXPINS_VCTTST_SHORT	1
#define MII_VCT_TXRXPINS_VCTTST_OPEN	2
#define MII_VCT_TXRXPINS_VCTTST_FAIL	3
#define MII_VCT_TXRXPINS_AMPRFLN	GENMASK(12, 8)
#define MII_VCT_TXRXPINS_AMPRFLN_SHIFT	8
#define MII_VCT_TXRXPINS_DISTRFLN	GENMASK(7, 0)
#define MII_VCT_TXRXPINS_DISTRFLN_MAX	0xff

#define M88E3082_PAIR_A		BIT(0)
#define M88E3082_PAIR_B		BIT(1)

#define LPA_PAUSE_FIBER		0x180
#define LPA_PAUSE_ASYM_FIBER	0x100

#define NB_FIBER_STATS	1
#define NB_STAT_MAX	3

MODULE_DESCRIPTION("Marvell PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");

struct marvell_hw_stat {
	const char *string;
	u8 page;
	u8 reg;
	u8 bits;
};

static const struct marvell_hw_stat marvell_hw_stats[] = {
	{ "phy_receive_errors_copper", 0, 21, 16},
	{ "phy_idle_errors", 0, 10, 8 },
	{ "phy_receive_errors_fiber", 1, 21, 16},
};

static_assert(ARRAY_SIZE(marvell_hw_stats) <= NB_STAT_MAX);

/* "simple" stat list + corresponding marvell_get_*_simple functions are used
 * on PHYs without a page register
 */
struct marvell_hw_stat_simple {
	const char *string;
	u8 reg;
	u8 bits;
};

static const struct marvell_hw_stat_simple marvell_hw_stats_simple[] = {
	{ "phy_receive_errors", 21, 16},
};

static_assert(ARRAY_SIZE(marvell_hw_stats_simple) <= NB_STAT_MAX);

enum {
	M88E3082_VCT_OFF,
	M88E3082_VCT_PHASE1,
	M88E3082_VCT_PHASE2,
};

struct marvell_priv {
	u64 stats[NB_STAT_MAX];
	char *hwmon_name;
	struct device *hwmon_dev;
	bool cable_test_tdr;
	u32 first;
	u32 last;
	u32 step;
	s8 pair;
	u8 vct_phase;
};

static int marvell_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MII_MARVELL_PHY_PAGE);
}

static int marvell_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MII_MARVELL_PHY_PAGE, page);
}

static int marvell_set_page(struct phy_device *phydev, int page)
{
	return phy_write(phydev, MII_MARVELL_PHY_PAGE, page);
}

static int marvell_ack_interrupt(struct phy_device *phydev)
{
	int err;

	/* Clear the interrupts by reading the reg */
	err = phy_read(phydev, MII_M1011_IEVENT);

	if (err < 0)
		return err;

	return 0;
}

static int marvell_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = marvell_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, MII_M1011_IMASK,
				MII_M1011_IMASK_INIT);
	} else {
		err = phy_write(phydev, MII_M1011_IMASK,
				MII_M1011_IMASK_CLEAR);
		if (err)
			return err;

		err = marvell_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t marvell_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_M1011_IEVENT);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & MII_M1011_IMASK_INIT))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int marvell_set_polarity(struct phy_device *phydev, int polarity)
{
	u16 val;

	switch (polarity) {
	case ETH_TP_MDI:
		val = MII_M1011_PHY_SCR_MDI;
		break;
	case ETH_TP_MDI_X:
		val = MII_M1011_PHY_SCR_MDI_X;
		break;
	case ETH_TP_MDI_AUTO:
	case ETH_TP_MDI_INVALID:
	default:
		val = MII_M1011_PHY_SCR_AUTO_CROSS;
		break;
	}

	return phy_modify_changed(phydev, MII_M1011_PHY_SCR,
				  MII_M1011_PHY_SCR_AUTO_CROSS, val);
}

static int marvell_config_aneg(struct phy_device *phydev)
{
	int changed = 0;
	int err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	changed = err;

	err = phy_write(phydev, MII_M1111_PHY_LED_CONTROL,
			MII_M1111_PHY_LED_DIRECT);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE || changed) {
		/* A write to speed/duplex bits (that is performed by
		 * genphy_config_aneg() call above) must be followed by
		 * a software reset. Otherwise, the write has no effect.
		 */
		err = genphy_soft_reset(phydev);
		if (err < 0)
			return err;
	}

	return 0;
}

static int m88e1101_config_aneg(struct phy_device *phydev)
{
	int err;

	/* This Marvell PHY has an errata which requires
	 * that certain registers get written in order
	 * to restart autonegotiation
	 */
	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x1f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x200c);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x5);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x100);
	if (err < 0)
		return err;

	return marvell_config_aneg(phydev);
}

#if IS_ENABLED(CONFIG_OF_MDIO)
/* Set and/or override some configuration registers based on the
 * marvell,reg-init property stored in the of_node for the phydev.
 *
 * marvell,reg-init = <reg-page reg mask value>,...;
 *
 * There may be one or more sets of <reg-page reg mask value>:
 *
 * reg-page: which register bank to use.
 * reg: the register.
 * mask: if non-zero, ANDed with existing register value.
 * value: ORed with the masked value and written to the regiser.
 *
 */
static int marvell_of_reg_init(struct phy_device *phydev)
{
	const __be32 *paddr;
	int len, i, saved_page, current_page, ret = 0;

	if (!phydev->mdio.dev.of_node)
		return 0;

	paddr = of_get_property(phydev->mdio.dev.of_node,
				"marvell,reg-init", &len);
	if (!paddr || len < (4 * sizeof(*paddr)))
		return 0;

	saved_page = phy_save_page(phydev);
	if (saved_page < 0)
		goto err;
	current_page = saved_page;

	len /= sizeof(*paddr);
	for (i = 0; i < len - 3; i += 4) {
		u16 page = be32_to_cpup(paddr + i);
		u16 reg = be32_to_cpup(paddr + i + 1);
		u16 mask = be32_to_cpup(paddr + i + 2);
		u16 val_bits = be32_to_cpup(paddr + i + 3);
		int val;

		if (page != current_page) {
			current_page = page;
			ret = marvell_write_page(phydev, page);
			if (ret < 0)
				goto err;
		}

		val = 0;
		if (mask) {
			val = __phy_read(phydev, reg);
			if (val < 0) {
				ret = val;
				goto err;
			}
			val &= mask;
		}
		val |= val_bits;

		ret = __phy_write(phydev, reg, val);
		if (ret < 0)
			goto err;
	}
err:
	return phy_restore_page(phydev, saved_page, ret);
}
#else
static int marvell_of_reg_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int m88e1121_config_aneg_rgmii_delays(struct phy_device *phydev)
{
	int mscr;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		mscr = MII_88E1121_PHY_MSCR_RX_DELAY |
		       MII_88E1121_PHY_MSCR_TX_DELAY;
	else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
		mscr = MII_88E1121_PHY_MSCR_RX_DELAY;
	else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
		mscr = MII_88E1121_PHY_MSCR_TX_DELAY;
	else
		mscr = 0;

	return phy_modify_paged_changed(phydev, MII_MARVELL_MSCR_PAGE,
					MII_88E1121_PHY_MSCR_REG,
					MII_88E1121_PHY_MSCR_DELAY_MASK, mscr);
}

static int m88e1121_config_aneg(struct phy_device *phydev)
{
	int changed = 0;
	int err = 0;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1121_config_aneg_rgmii_delays(phydev);
		if (err < 0)
			return err;
	}

	changed = err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	changed |= err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE || changed) {
		/* A software reset is used to ensure a "commit" of the
		 * changes is done.
		 */
		err = genphy_soft_reset(phydev);
		if (err < 0)
			return err;
	}

	return 0;
}

static int m88e1318_config_aneg(struct phy_device *phydev)
{
	int err;

	err = phy_modify_paged(phydev, MII_MARVELL_MSCR_PAGE,
			       MII_88E1318S_PHY_MSCR1_REG,
			       0, MII_88E1318S_PHY_MSCR1_PAD_ODD);
	if (err < 0)
		return err;

	return m88e1121_config_aneg(phydev);
}

/**
 * linkmode_adv_to_fiber_adv_t
 * @advertise: the linkmode advertisement settings
 *
 * A small helper function that translates linkmode advertisement
 * settings to phy autonegotiation advertisements for the MII_ADV
 * register for fiber link.
 */
static inline u32 linkmode_adv_to_fiber_adv_t(unsigned long *advertise)
{
	u32 result = 0;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, advertise))
		result |= ADVERTISE_1000XHALF;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, advertise))
		result |= ADVERTISE_1000XFULL;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, advertise) &&
	    linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertise))
		result |= ADVERTISE_1000XPSE_ASYM;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertise))
		result |= ADVERTISE_1000XPAUSE;

	return result;
}

/**
 * marvell_config_aneg_fiber - restart auto-negotiation or write BMCR
 * @phydev: target phy_device struct
 *
 * Description: If auto-negotiation is enabled, we configure the
 *   advertising, and then restart auto-negotiation.  If it is not
 *   enabled, then we write the BMCR. Adapted for fiber link in
 *   some Marvell's devices.
 */
static int marvell_config_aneg_fiber(struct phy_device *phydev)
{
	int changed = 0;
	int err;
	u16 adv;

	if (phydev->autoneg != AUTONEG_ENABLE)
		return genphy_setup_forced(phydev);

	/* Only allow advertising what this PHY supports */
	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	adv = linkmode_adv_to_fiber_adv_t(phydev->advertising);

	/* Setup fiber advertisement */
	err = phy_modify_changed(phydev, MII_ADVERTISE,
				 ADVERTISE_1000XHALF | ADVERTISE_1000XFULL |
				 ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM,
				 adv);
	if (err < 0)
		return err;
	if (err > 0)
		changed = 1;

	return genphy_check_and_restart_aneg(phydev, changed);
}

static unsigned int m88e1111_inband_caps(struct phy_device *phydev,
					 phy_interface_t interface)
{
	/* In 1000base-X and SGMII modes, the inband mode can be changed
	 * through the Fibre page BMCR ANENABLE bit.
	 */
	if (interface == PHY_INTERFACE_MODE_1000BASEX ||
	    interface == PHY_INTERFACE_MODE_SGMII)
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE |
		       LINK_INBAND_BYPASS;

	return 0;
}

static int m88e1111_config_inband(struct phy_device *phydev, unsigned int modes)
{
	u16 extsr, bmcr;
	int err;

	if (phydev->interface != PHY_INTERFACE_MODE_1000BASEX &&
	    phydev->interface != PHY_INTERFACE_MODE_SGMII)
		return -EINVAL;

	if (modes == LINK_INBAND_BYPASS)
		extsr = MII_M1111_HWCFG_SERIAL_AN_BYPASS;
	else
		extsr = 0;

	if (modes == LINK_INBAND_DISABLE)
		bmcr = 0;
	else
		bmcr = BMCR_ANENABLE;

	err = phy_modify(phydev, MII_M1111_PHY_EXT_SR,
			 MII_M1111_HWCFG_SERIAL_AN_BYPASS, extsr);
	if (err < 0)
		return extsr;

	return phy_modify_paged(phydev, MII_MARVELL_FIBER_PAGE, MII_BMCR,
				BMCR_ANENABLE, bmcr);
}

static int m88e1111_config_aneg(struct phy_device *phydev)
{
	int extsr = phy_read(phydev, MII_M1111_PHY_EXT_SR);
	int err;

	if (extsr < 0)
		return extsr;

	/* If not using SGMII or copper 1000BaseX modes, use normal process.
	 * Steps below are only required for these modes.
	 */
	if (phydev->interface != PHY_INTERFACE_MODE_SGMII &&
	    (extsr & MII_M1111_HWCFG_MODE_MASK) !=
	    MII_M1111_HWCFG_MODE_COPPER_1000X_AN)
		return marvell_config_aneg(phydev);

	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		goto error;

	/* Configure the copper link first */
	err = marvell_config_aneg(phydev);
	if (err < 0)
		goto error;

	/* Then the fiber link */
	err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
	if (err < 0)
		goto error;

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII)
		/* Do not touch the fiber advertisement if we're in copper->sgmii mode.
		 * Just ensure that SGMII-side autonegotiation is enabled.
		 * If we switched from some other mode to SGMII it may not be.
		 */
		err = genphy_check_and_restart_aneg(phydev, false);
	else
		err = marvell_config_aneg_fiber(phydev);
	if (err < 0)
		goto error;

	return marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

static int m88e1510_config_aneg(struct phy_device *phydev)
{
	int err;

	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		goto error;

	/* Configure the copper link first */
	err = m88e1318_config_aneg(phydev);
	if (err < 0)
		goto error;

	/* Do not touch the fiber page if we're in copper->sgmii mode */
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII)
		return 0;

	/* Then the fiber link */
	err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
	if (err < 0)
		goto error;

	err = marvell_config_aneg_fiber(phydev);
	if (err < 0)
		goto error;

	return marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

static void marvell_config_led(struct phy_device *phydev)
{
	u16 def_config;
	int err;

	switch (MARVELL_PHY_FAMILY_ID(phydev->phy_id)) {
	/* Default PHY LED config: LED[0] .. Link, LED[1] .. Activity */
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1121R):
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1318S):
		def_config = MII_88E1121_PHY_LED_DEF;
		break;
	/* Default PHY LED config:
	 * LED[0] .. 1000Mbps Link
	 * LED[1] .. 100Mbps Link
	 * LED[2] .. Blink, Activity
	 */
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1510):
		if (phydev->dev_flags & MARVELL_PHY_LED0_LINK_LED1_ACTIVE)
			def_config = MII_88E1510_PHY_LED0_LINK_LED1_ACTIVE;
		else
			def_config = MII_88E1510_PHY_LED_DEF;
		break;
	default:
		return;
	}

	err = phy_write_paged(phydev, MII_MARVELL_LED_PAGE, MII_PHY_LED_CTRL,
			      def_config);
	if (err < 0)
		phydev_warn(phydev, "Fail to config marvell phy LED.\n");
}

static int marvell_config_init(struct phy_device *phydev)
{
	/* Set default LED */
	marvell_config_led(phydev);

	/* Set registers from marvell,reg-init DT property */
	return marvell_of_reg_init(phydev);
}

static int m88e3016_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable Scrambler and Auto-Crossover */
	ret = phy_modify(phydev, MII_88E3016_PHY_SPEC_CTRL,
			 MII_88E3016_DISABLE_SCRAMBLER,
			 MII_88E3016_AUTO_MDIX_CROSSOVER);
	if (ret < 0)
		return ret;

	return marvell_config_init(phydev);
}

static int m88e1111_config_init_hwcfg_mode(struct phy_device *phydev,
					   u16 mode,
					   int fibre_copper_auto)
{
	if (fibre_copper_auto)
		mode |= MII_M1111_HWCFG_FIBER_COPPER_AUTO;

	return phy_modify(phydev, MII_M1111_PHY_EXT_SR,
			  MII_M1111_HWCFG_MODE_MASK |
			  MII_M1111_HWCFG_FIBER_COPPER_AUTO |
			  MII_M1111_HWCFG_FIBER_COPPER_RES,
			  mode);
}

static int m88e1111_config_init_rgmii_delays(struct phy_device *phydev)
{
	int delay;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		delay = MII_M1111_RGMII_RX_DELAY | MII_M1111_RGMII_TX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		delay = MII_M1111_RGMII_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		delay = MII_M1111_RGMII_TX_DELAY;
		break;
	default:
		delay = 0;
		break;
	}

	return phy_modify(phydev, MII_M1111_PHY_EXT_CR,
			  MII_M1111_RGMII_RX_DELAY | MII_M1111_RGMII_TX_DELAY,
			  delay);
}

static int m88e1111_config_init_rgmii(struct phy_device *phydev)
{
	int temp;
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
	if (err < 0)
		return err;

	temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
	if (temp < 0)
		return temp;

	temp &= ~(MII_M1111_HWCFG_MODE_MASK);

	if (temp & MII_M1111_HWCFG_FIBER_COPPER_RES)
		temp |= MII_M1111_HWCFG_MODE_FIBER_RGMII;
	else
		temp |= MII_M1111_HWCFG_MODE_COPPER_RGMII;

	return phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
}

static int m88e1111_config_init_sgmii(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_SGMII_NO_CLK,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
	if (err < 0)
		return err;

	/* make sure copper is selected */
	return marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
}

static int m88e1111_config_init_rtbi(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
	if (err < 0)
		return err;

	err = m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_RTBI,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
	if (err < 0)
		return err;

	/* soft reset */
	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	return m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_RTBI,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
}

static int m88e1111_config_init_1000basex(struct phy_device *phydev)
{
	int extsr = phy_read(phydev, MII_M1111_PHY_EXT_SR);
	int err, mode;

	if (extsr < 0)
		return extsr;

	/* If using copper mode, ensure 1000BaseX auto-negotiation is enabled.
	 * FIXME: this does not actually enable 1000BaseX auto-negotiation if
	 * it was previously disabled in the Fiber BMCR!
	 */
	mode = extsr & MII_M1111_HWCFG_MODE_MASK;
	if (mode == MII_M1111_HWCFG_MODE_COPPER_1000X_NOAN) {
		err = phy_modify(phydev, MII_M1111_PHY_EXT_SR,
				 MII_M1111_HWCFG_MODE_MASK |
				 MII_M1111_HWCFG_SERIAL_AN_BYPASS,
				 MII_M1111_HWCFG_MODE_COPPER_1000X_AN |
				 MII_M1111_HWCFG_SERIAL_AN_BYPASS);
		if (err < 0)
			return err;
	}
	return 0;
}

static int m88e1111_config_init(struct phy_device *phydev)
{
	int err;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1111_config_init_rgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		err = m88e1111_config_init_sgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RTBI) {
		err = m88e1111_config_init_rtbi(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_1000BASEX) {
		err = m88e1111_config_init_1000basex(phydev);
		if (err < 0)
			return err;
	}

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		/* If the HWCFG_MODE was changed from another mode (such as
		 * 1000BaseX) to SGMII, the state of the support bits may have
		 * also changed now that the PHY has been reset.
		 * Update the PHY abilities accordingly.
		 */
		err = genphy_read_abilities(phydev);
		linkmode_or(phydev->advertising, phydev->advertising,
			    phydev->supported);
	}
	return err;
}

static int m88e1111_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, cnt, enable;

	val = phy_read(phydev, MII_M1111_PHY_EXT_CR);
	if (val < 0)
		return val;

	enable = FIELD_GET(MII_M1111_PHY_EXT_CR_DOWNSHIFT_EN, val);
	cnt = FIELD_GET(MII_M1111_PHY_EXT_CR_DOWNSHIFT_MASK, val) + 1;

	*data = enable ? cnt : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int m88e1111_set_downshift(struct phy_device *phydev, u8 cnt)
{
	int val, err;

	if (cnt > MII_M1111_PHY_EXT_CR_DOWNSHIFT_MAX)
		return -E2BIG;

	if (!cnt) {
		err = phy_clear_bits(phydev, MII_M1111_PHY_EXT_CR,
				     MII_M1111_PHY_EXT_CR_DOWNSHIFT_EN);
	} else {
		val = MII_M1111_PHY_EXT_CR_DOWNSHIFT_EN;
		val |= FIELD_PREP(MII_M1111_PHY_EXT_CR_DOWNSHIFT_MASK, cnt - 1);

		err = phy_modify(phydev, MII_M1111_PHY_EXT_CR,
				 MII_M1111_PHY_EXT_CR_DOWNSHIFT_EN |
				 MII_M1111_PHY_EXT_CR_DOWNSHIFT_MASK,
				 val);
	}

	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1111_get_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1111_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int m88e1111_set_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1111_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static int m88e1011_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, cnt, enable;

	val = phy_read(phydev, MII_M1011_PHY_SCR);
	if (val < 0)
		return val;

	enable = FIELD_GET(MII_M1011_PHY_SCR_DOWNSHIFT_EN, val);
	cnt = FIELD_GET(MII_M1011_PHY_SCR_DOWNSHIFT_MASK, val) + 1;

	*data = enable ? cnt : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int m88e1011_set_downshift(struct phy_device *phydev, u8 cnt)
{
	int val, err;

	if (cnt > MII_M1011_PHY_SCR_DOWNSHIFT_MAX)
		return -E2BIG;

	if (!cnt) {
		err = phy_clear_bits(phydev, MII_M1011_PHY_SCR,
				     MII_M1011_PHY_SCR_DOWNSHIFT_EN);
	} else {
		val = MII_M1011_PHY_SCR_DOWNSHIFT_EN;
		val |= FIELD_PREP(MII_M1011_PHY_SCR_DOWNSHIFT_MASK, cnt - 1);

		err = phy_modify(phydev, MII_M1011_PHY_SCR,
				 MII_M1011_PHY_SCR_DOWNSHIFT_EN |
				 MII_M1011_PHY_SCR_DOWNSHIFT_MASK,
				 val);
	}

	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1011_get_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1011_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int m88e1011_set_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1011_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static int m88e1112_config_init(struct phy_device *phydev)
{
	int err;

	err = m88e1011_set_downshift(phydev, 3);
	if (err < 0)
		return err;

	return m88e1111_config_init(phydev);
}

static int m88e1111gbe_config_init(struct phy_device *phydev)
{
	int err;

	err = m88e1111_set_downshift(phydev, 3);
	if (err < 0)
		return err;

	return m88e1111_config_init(phydev);
}

static int marvell_1011gbe_config_init(struct phy_device *phydev)
{
	int err;

	err = m88e1011_set_downshift(phydev, 3);
	if (err < 0)
		return err;

	return marvell_config_init(phydev);
}
static int m88e1116r_config_init(struct phy_device *phydev)
{
	int err;

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	msleep(500);

	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	err = m88e1011_set_downshift(phydev, 8);
	if (err < 0)
		return err;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1121_config_aneg_rgmii_delays(phydev);
		if (err < 0)
			return err;
	}

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	return marvell_config_init(phydev);
}

static int m88e1318_config_init(struct phy_device *phydev)
{
	if (phy_interrupt_is_valid(phydev)) {
		int err = phy_modify_paged(
			phydev, MII_MARVELL_LED_PAGE,
			MII_88E1318S_PHY_LED_TCR,
			MII_88E1318S_PHY_LED_TCR_FORCE_INT,
			MII_88E1318S_PHY_LED_TCR_INTn_ENABLE |
			MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW);
		if (err < 0)
			return err;
	}

	return marvell_config_init(phydev);
}

static int m88e1510_config_init(struct phy_device *phydev)
{
	static const struct {
		u16 reg17, reg16;
	} errata_vals[] = {
		{ 0x214b, 0x2144 },
		{ 0x0c28, 0x2146 },
		{ 0xb233, 0x214d },
		{ 0xcc0c, 0x2159 },
	};
	int err;
	int i;

	/* As per Marvell Release Notes - Alaska 88E1510/88E1518/88E1512/
	 * 88E1514 Rev A0, Errata Section 5.1:
	 * If EEE is intended to be used, the following register writes
	 * must be done once after every hardware reset.
	 */
	err = marvell_set_page(phydev, 0x00FF);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(errata_vals); ++i) {
		err = phy_write(phydev, 17, errata_vals[i].reg17);
		if (err)
			return err;
		err = phy_write(phydev, 16, errata_vals[i].reg16);
		if (err)
			return err;
	}

	err = marvell_set_page(phydev, 0x00FB);
	if (err < 0)
		return err;
	err = phy_write(phydev, 07, 0xC00D);
	if (err < 0)
		return err;
	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	/* SGMII-to-Copper mode initialization */
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		/* Select page 18 */
		err = marvell_set_page(phydev, 18);
		if (err < 0)
			return err;

		/* In reg 20, write MODE[2:0] = 0x1 (SGMII to Copper) */
		err = phy_modify(phydev, MII_88E1510_GEN_CTRL_REG_1,
				 MII_88E1510_GEN_CTRL_REG_1_MODE_MASK,
				 MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII);
		if (err < 0)
			return err;

		/* PHY reset is necessary after changing MODE[2:0] */
		err = phy_set_bits(phydev, MII_88E1510_GEN_CTRL_REG_1,
				   MII_88E1510_GEN_CTRL_REG_1_RESET);
		if (err < 0)
			return err;

		/* Reset page selection */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			return err;
	}
	err = m88e1011_set_downshift(phydev, 3);
	if (err < 0)
		return err;

	return m88e1318_config_init(phydev);
}

static int m88e1118_config_aneg(struct phy_device *phydev)
{
	int err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1118_config_init(struct phy_device *phydev)
{
	u16 leds;
	int err;

	/* Enable 1000 Mbit */
	err = phy_write_paged(phydev, MII_MARVELL_MSCR_PAGE,
			      MII_88E1121_PHY_MSCR_REG, 0x1070);
	if (err < 0)
		return err;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1121_config_aneg_rgmii_delays(phydev);
		if (err < 0)
			return err;
	}

	/* Adjust LED Control */
	if (phydev->dev_flags & MARVELL_PHY_M1118_DNS323_LEDS)
		leds = 0x1100;
	else
		leds = 0x021e;

	err = phy_write_paged(phydev, MII_MARVELL_LED_PAGE, 0x10, leds);
	if (err < 0)
		return err;

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	/* Reset page register */
	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1149_config_init(struct phy_device *phydev)
{
	int err;

	/* Change address */
	err = marvell_set_page(phydev, MII_MARVELL_MSCR_PAGE);
	if (err < 0)
		return err;

	/* Enable 1000 Mbit */
	err = phy_write(phydev, 0x15, 0x1048);
	if (err < 0)
		return err;

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	/* Reset address */
	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1145_config_init_rgmii(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
	if (err < 0)
		return err;

	if (phydev->dev_flags & MARVELL_PHY_M1145_FLAGS_RESISTANCE) {
		err = phy_write(phydev, 0x1d, 0x0012);
		if (err < 0)
			return err;

		err = phy_modify(phydev, 0x1e, 0x0fc0,
				 2 << 9 | /* 36 ohm */
				 2 << 6); /* 39 ohm */
		if (err < 0)
			return err;

		err = phy_write(phydev, 0x1d, 0x3);
		if (err < 0)
			return err;

		err = phy_write(phydev, 0x1e, 0x8000);
	}
	return err;
}

static int m88e1145_config_init_sgmii(struct phy_device *phydev)
{
	return m88e1111_config_init_hwcfg_mode(
		phydev, MII_M1111_HWCFG_MODE_SGMII_NO_CLK,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
}

static int m88e1145_config_init(struct phy_device *phydev)
{
	int err;

	/* Take care of errata E0 & E1 */
	err = phy_write(phydev, 0x1d, 0x001b);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x418f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x0016);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0xa2da);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
		err = m88e1145_config_init_rgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		err = m88e1145_config_init_sgmii(phydev);
		if (err < 0)
			return err;
	}
	err = m88e1111_set_downshift(phydev, 3);
	if (err < 0)
		return err;

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	return 0;
}

static int m88e1540_get_fld(struct phy_device *phydev, u8 *msecs)
{
	int val;

	val = phy_read(phydev, MII_88E1540_COPPER_CTRL3);
	if (val < 0)
		return val;

	if (!(val & MII_88E1540_COPPER_CTRL3_FAST_LINK_DOWN)) {
		*msecs = ETHTOOL_PHY_FAST_LINK_DOWN_OFF;
		return 0;
	}

	val = FIELD_GET(MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_MASK, val);

	switch (val) {
	case MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_00MS:
		*msecs = 0;
		break;
	case MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_10MS:
		*msecs = 10;
		break;
	case MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_20MS:
		*msecs = 20;
		break;
	case MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_40MS:
		*msecs = 40;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int m88e1540_set_fld(struct phy_device *phydev, const u8 *msecs)
{
	int val, ret;

	if (*msecs == ETHTOOL_PHY_FAST_LINK_DOWN_OFF)
		return phy_clear_bits(phydev, MII_88E1540_COPPER_CTRL3,
				      MII_88E1540_COPPER_CTRL3_FAST_LINK_DOWN);

	/* According to the Marvell data sheet EEE must be disabled for
	 * Fast Link Down detection to work properly
	 */
	if (phydev->eee_cfg.eee_enabled) {
		phydev_warn(phydev, "Fast Link Down detection requires EEE to be disabled!\n");
		return -EBUSY;
	}

	if (*msecs <= 5)
		val = MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_00MS;
	else if (*msecs <= 15)
		val = MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_10MS;
	else if (*msecs <= 30)
		val = MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_20MS;
	else
		val = MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_40MS;

	val = FIELD_PREP(MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_MASK, val);

	ret = phy_modify(phydev, MII_88E1540_COPPER_CTRL3,
			 MII_88E1540_COPPER_CTRL3_LINK_DOWN_DELAY_MASK, val);
	if (ret)
		return ret;

	return phy_set_bits(phydev, MII_88E1540_COPPER_CTRL3,
			    MII_88E1540_COPPER_CTRL3_FAST_LINK_DOWN);
}

static int m88e1540_get_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return m88e1540_get_fld(phydev, data);
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1011_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int m88e1540_set_tunable(struct phy_device *phydev,
				struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return m88e1540_set_fld(phydev, data);
	case ETHTOOL_PHY_DOWNSHIFT:
		return m88e1011_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

/* The VOD can be out of specification on link up. Poke an
 * undocumented register, in an undocumented page, with a magic value
 * to fix this.
 */
static int m88e6390_errata(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_BMCR,
			BMCR_ANENABLE | BMCR_SPEED1000 | BMCR_FULLDPLX);
	if (err)
		return err;

	usleep_range(300, 400);

	err = phy_write_paged(phydev, 0xf8, 0x08, 0x36);
	if (err)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e6390_config_aneg(struct phy_device *phydev)
{
	int err;

	err = m88e6390_errata(phydev);
	if (err)
		return err;

	return m88e1510_config_aneg(phydev);
}

/**
 * fiber_lpa_mod_linkmode_lpa_t
 * @advertising: the linkmode advertisement settings
 * @lpa: value of the MII_LPA register for fiber link
 *
 * A small helper function that translates MII_LPA bits to linkmode LP
 * advertisement settings. Other bits in advertising are left
 * unchanged.
 */
static void fiber_lpa_mod_linkmode_lpa_t(unsigned long *advertising, u32 lpa)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
			 advertising, lpa & LPA_1000XHALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 advertising, lpa & LPA_1000XFULL);
}

static int marvell_read_status_page_an(struct phy_device *phydev,
				       int fiber, int status)
{
	int lpa;
	int err;

	if (!(status & MII_M1011_PHY_STATUS_RESOLVED)) {
		phydev->link = 0;
		return 0;
	}

	if (status & MII_M1011_PHY_STATUS_FULLDUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	switch (status & MII_M1011_PHY_STATUS_SPD_MASK) {
	case MII_M1011_PHY_STATUS_1000:
		phydev->speed = SPEED_1000;
		break;

	case MII_M1011_PHY_STATUS_100:
		phydev->speed = SPEED_100;
		break;

	default:
		phydev->speed = SPEED_10;
		break;
	}

	if (!fiber) {
		err = genphy_read_lpa(phydev);
		if (err < 0)
			return err;

		phy_resolve_aneg_pause(phydev);
	} else {
		lpa = phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		/* The fiber link is only 1000M capable */
		fiber_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);

		if (phydev->duplex == DUPLEX_FULL) {
			if (!(lpa & LPA_PAUSE_FIBER)) {
				phydev->pause = 0;
				phydev->asym_pause = 0;
			} else if ((lpa & LPA_PAUSE_ASYM_FIBER)) {
				phydev->pause = 1;
				phydev->asym_pause = 1;
			} else {
				phydev->pause = 1;
				phydev->asym_pause = 0;
			}
		}
	}

	return 0;
}

/* marvell_read_status_page
 *
 * Description:
 *   Check the link, then figure out the current state
 *   by comparing what we advertise with what the link partner
 *   advertises.  Start by checking the gigabit possibilities,
 *   then move on to 10/100.
 */
static int marvell_read_status_page(struct phy_device *phydev, int page)
{
	int status;
	int fiber;
	int err;

	status = phy_read(phydev, MII_M1011_PHY_STATUS);
	if (status < 0)
		return status;

	/* Use the generic register for copper link status,
	 * and the PHY status register for fiber link status.
	 */
	if (page == MII_MARVELL_FIBER_PAGE) {
		phydev->link = !!(status & MII_M1011_PHY_STATUS_LINK);
	} else {
		err = genphy_update_link(phydev);
		if (err)
			return err;
	}

	if (page == MII_MARVELL_FIBER_PAGE)
		fiber = 1;
	else
		fiber = 0;

	linkmode_zero(phydev->lp_advertising);
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->port = fiber ? PORT_FIBRE : PORT_TP;

	if (fiber) {
		phydev->mdix = ETH_TP_MDI_INVALID;
	} else {
		/* The MDI-X state is set regardless of Autoneg being enabled
		 * and reflects forced MDI-X state as well as auto resolution
		 */
		if (status & MII_M1011_PHY_STATUS_RESOLVED)
			phydev->mdix = status & MII_M1011_PHY_STATUS_MDIX ?
				ETH_TP_MDI_X : ETH_TP_MDI;
		else
			phydev->mdix = ETH_TP_MDI_INVALID;
	}

	if (phydev->autoneg == AUTONEG_ENABLE)
		err = marvell_read_status_page_an(phydev, fiber, status);
	else
		err = genphy_read_status_fixed(phydev);

	return err;
}

/* marvell_read_status
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need status checked.
 * Description:
 *   First, check the fiber link and status.
 *   If the fiber link is down, check the copper link and status which
 *   will be the default value if both link are down.
 */
static int marvell_read_status(struct phy_device *phydev)
{
	int err;

	/* Check the fiber mode first */
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported) &&
	    phydev->interface != PHY_INTERFACE_MODE_SGMII) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		err = marvell_read_status_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* If the fiber link is up, it is the selected and
		 * used link. In this case, we need to stay in the
		 * fiber page. Please to be careful about that, avoid
		 * to restore Copper page in other functions which
		 * could break the behaviour for some fiber phy like
		 * 88E1512.
		 */
		if (phydev->link)
			return 0;

		/* If fiber link is down, check and save copper mode state */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	return marvell_read_status_page(phydev, MII_MARVELL_COPPER_PAGE);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

/* marvell_suspend
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need to be suspended
 */
static int marvell_suspend(struct phy_device *phydev)
{
	int err;

	/* Suspend the fiber mode first */
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported)) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* With the page set, use the generic suspend */
		err = genphy_suspend(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic suspend */
	return genphy_suspend(phydev);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

/* marvell_resume
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need to be resumed
 */
static int marvell_resume(struct phy_device *phydev)
{
	int err;

	/* Resume the fiber mode first */
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported)) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* With the page set, use the generic resume */
		err = genphy_resume(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic resume */
	return genphy_resume(phydev);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

/* m88e1510_resume
 *
 * The 88e1510 PHY has an erratum where the phy downshift counter is not cleared
 * after phy being suspended(BMCR_PDOWN set) and then later resumed(BMCR_PDOWN
 * cleared). This can cause the link to intermittently downshift to a lower speed.
 *
 * Disabling and re-enabling the downshift feature clears the counter, allowing
 * the PHY to retry gigabit link negotiation up to the programmed retry count
 * before downshifting. This behavior has been observed on copper links.
 */
static int m88e1510_resume(struct phy_device *phydev)
{
	int err;
	u8 cnt = 0;

	err = marvell_resume(phydev);
	if (err < 0)
		return err;

	/* read downshift counter value */
	err = m88e1011_get_downshift(phydev, &cnt);
	if (err < 0)
		return err;

	if (cnt) {
		/* downshift disabled */
		err = m88e1011_set_downshift(phydev, 0);
		if (err < 0)
			return err;

		/* downshift enabled, with previous counter value */
		err = m88e1011_set_downshift(phydev, cnt);
	}

	return err;
}

static int marvell_aneg_done(struct phy_device *phydev)
{
	int retval = phy_read(phydev, MII_M1011_PHY_STATUS);

	return (retval < 0) ? retval : (retval & MII_M1011_PHY_STATUS_RESOLVED);
}

static void m88e1318_get_wol(struct phy_device *phydev,
			     struct ethtool_wolinfo *wol)
{
	int ret;

	wol->supported = WAKE_MAGIC | WAKE_PHY;
	wol->wolopts = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_WOL_PAGE,
			     MII_88E1318S_PHY_WOL_CTRL);
	if (ret < 0)
		return;

	if (ret & MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE)
		wol->wolopts |= WAKE_MAGIC;

	if (ret & MII_88E1318S_PHY_WOL_CTRL_LINK_UP_ENABLE)
		wol->wolopts |= WAKE_PHY;
}

static int m88e1318_set_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int err = 0, oldpage;

	oldpage = phy_save_page(phydev);
	if (oldpage < 0)
		goto error;

	if (wol->wolopts & (WAKE_MAGIC | WAKE_PHY)) {
		/* Explicitly switch to page 0x00, just to be sure */
		err = marvell_write_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;

		/* If WOL event happened once, the LED[2] interrupt pin
		 * will not be cleared unless we reading the interrupt status
		 * register. If interrupts are in use, the normal interrupt
		 * handling will clear the WOL event. Clear the WOL event
		 * before enabling it if !phy_interrupt_is_valid()
		 */
		if (!phy_interrupt_is_valid(phydev))
			__phy_read(phydev, MII_M1011_IEVENT);

		/* Enable the WOL interrupt */
		err = __phy_set_bits(phydev, MII_88E1318S_PHY_CSIER,
				     MII_88E1318S_PHY_CSIER_WOL_EIE);
		if (err < 0)
			goto error;

		err = marvell_write_page(phydev, MII_MARVELL_LED_PAGE);
		if (err < 0)
			goto error;

		/* Setup LED[2] as interrupt pin (active low) */
		err = __phy_modify(phydev, MII_88E1318S_PHY_LED_TCR,
				   MII_88E1318S_PHY_LED_TCR_FORCE_INT,
				   MII_88E1318S_PHY_LED_TCR_INTn_ENABLE |
				   MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW);
		if (err < 0)
			goto error;
	}

	if (wol->wolopts & WAKE_MAGIC) {
		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Store the device address for the magic packet */
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD2,
				((phydev->attached_dev->dev_addr[5] << 8) |
				 phydev->attached_dev->dev_addr[4]));
		if (err < 0)
			goto error;
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD1,
				((phydev->attached_dev->dev_addr[3] << 8) |
				 phydev->attached_dev->dev_addr[2]));
		if (err < 0)
			goto error;
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD0,
				((phydev->attached_dev->dev_addr[1] << 8) |
				 phydev->attached_dev->dev_addr[0]));
		if (err < 0)
			goto error;

		/* Clear WOL status and enable magic packet matching */
		err = __phy_set_bits(phydev, MII_88E1318S_PHY_WOL_CTRL,
				     MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS |
				     MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE);
		if (err < 0)
			goto error;
	} else {
		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Clear WOL status and disable magic packet matching */
		err = __phy_modify(phydev, MII_88E1318S_PHY_WOL_CTRL,
				   MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE,
				   MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS);
		if (err < 0)
			goto error;
	}

	if (wol->wolopts & WAKE_PHY) {
		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Clear WOL status and enable link up event */
		err = __phy_modify(phydev, MII_88E1318S_PHY_WOL_CTRL, 0,
				   MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS |
				   MII_88E1318S_PHY_WOL_CTRL_LINK_UP_ENABLE);
		if (err < 0)
			goto error;
	} else {
		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Clear WOL status and disable link up event */
		err = __phy_modify(phydev, MII_88E1318S_PHY_WOL_CTRL,
				   MII_88E1318S_PHY_WOL_CTRL_LINK_UP_ENABLE,
				   MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS);
		if (err < 0)
			goto error;
	}

error:
	return phy_restore_page(phydev, oldpage, err);
}

static int marvell_get_sset_count(struct phy_device *phydev)
{
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported))
		return ARRAY_SIZE(marvell_hw_stats);
	else
		return ARRAY_SIZE(marvell_hw_stats) - NB_FIBER_STATS;
}

static int marvell_get_sset_count_simple(struct phy_device *phydev)
{
	return ARRAY_SIZE(marvell_hw_stats_simple);
}

static void marvell_get_strings(struct phy_device *phydev, u8 *data)
{
	int count = marvell_get_sset_count(phydev);
	int i;

	for (i = 0; i < count; i++)
		ethtool_puts(&data, marvell_hw_stats[i].string);
}

static void marvell_get_strings_simple(struct phy_device *phydev, u8 *data)
{
	int count = marvell_get_sset_count_simple(phydev);
	int i;

	for (i = 0; i < count; i++)
		ethtool_puts(&data, marvell_hw_stats_simple[i].string);
}

static u64 marvell_get_stat(struct phy_device *phydev, int i)
{
	struct marvell_hw_stat stat = marvell_hw_stats[i];
	struct marvell_priv *priv = phydev->priv;
	int val;
	u64 ret;

	val = phy_read_paged(phydev, stat.page, stat.reg);
	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static u64 marvell_get_stat_simple(struct phy_device *phydev, int i)
{
	struct marvell_hw_stat_simple stat = marvell_hw_stats_simple[i];
	struct marvell_priv *priv = phydev->priv;
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void marvell_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	int count = marvell_get_sset_count(phydev);
	int i;

	for (i = 0; i < count; i++)
		data[i] = marvell_get_stat(phydev, i);
}

static void marvell_get_stats_simple(struct phy_device *phydev,
				     struct ethtool_stats *stats, u64 *data)
{
	int count = marvell_get_sset_count_simple(phydev);
	int i;

	for (i = 0; i < count; i++)
		data[i] = marvell_get_stat_simple(phydev, i);
}

static int m88e1510_loopback(struct phy_device *phydev, bool enable, int speed)
{
	u16 bmcr_ctl, mscr2_ctl = 0;
	int err;

	if (!enable)
		return genphy_loopback(phydev, enable, 0);

	if (speed == SPEED_10 || speed == SPEED_100 || speed == SPEED_1000)
		phydev->speed = speed;
	else if (speed)
		return -EINVAL;

	bmcr_ctl = mii_bmcr_encode_fixed(phydev->speed, phydev->duplex);

	err = phy_write(phydev, MII_BMCR, bmcr_ctl);
	if (err < 0)
		return err;

	if (phydev->speed == SPEED_1000)
		mscr2_ctl = BMCR_SPEED1000;
	else if (phydev->speed == SPEED_100)
		mscr2_ctl = BMCR_SPEED100;

	err = phy_modify_paged(phydev, MII_MARVELL_MSCR_PAGE,
			       MII_88E1510_MSCR_2, BMCR_SPEED1000 |
			       BMCR_SPEED100, mscr2_ctl);
	if (err < 0)
		return err;

	/* Need soft reset to have speed configuration takes effect */
	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	err = phy_modify(phydev, MII_BMCR, BMCR_LOOPBACK,
			 BMCR_LOOPBACK);

	if (!err) {
		/*
		 * It takes some time for PHY device to switch into loopback
		 * mode.
		 */
		msleep(1000);
	}
	return err;
}

static int marvell_vct5_wait_complete(struct phy_device *phydev)
{
	int i;
	int val;

	for (i = 0; i < 32; i++) {
		val = __phy_read(phydev, MII_VCT5_CTRL);
		if (val < 0)
			return val;

		if (val & MII_VCT5_CTRL_COMPLETE)
			return 0;
	}

	phydev_err(phydev, "Timeout while waiting for cable test to finish\n");
	return -ETIMEDOUT;
}

static int marvell_vct5_amplitude(struct phy_device *phydev, int pair)
{
	int amplitude;
	int val;
	int reg;

	reg = MII_VCT5_TX_RX_MDI0_COUPLING + pair;
	val = __phy_read(phydev, reg);

	if (val < 0)
		return 0;

	amplitude = (val & MII_VCT5_TX_RX_AMPLITUDE_MASK) >>
		MII_VCT5_TX_RX_AMPLITUDE_SHIFT;

	if (!(val & MII_VCT5_TX_RX_COUPLING_POSITIVE_REFLECTION))
		amplitude = -amplitude;

	return 1000 * amplitude / 128;
}

static u32 marvell_vct5_distance2cm(int distance)
{
	return distance * 805 / 10;
}

static u32 marvell_vct5_cm2distance(int cm)
{
	return cm * 10 / 805;
}

static int marvell_vct5_amplitude_distance(struct phy_device *phydev,
					   int distance, int pair)
{
	u16 reg;
	int err;
	int mV;
	int i;

	err = __phy_write(phydev, MII_VCT5_SAMPLE_POINT_DISTANCE,
			  distance);
	if (err)
		return err;

	reg = MII_VCT5_CTRL_ENABLE |
		MII_VCT5_CTRL_TX_SAME_CHANNEL |
		MII_VCT5_CTRL_SAMPLES_DEFAULT |
		MII_VCT5_CTRL_SAMPLE_POINT |
		MII_VCT5_CTRL_PEEK_HYST_DEFAULT;
	err = __phy_write(phydev, MII_VCT5_CTRL, reg);
	if (err)
		return err;

	err = marvell_vct5_wait_complete(phydev);
	if (err)
		return err;

	for (i = 0; i < 4; i++) {
		if (pair != PHY_PAIR_ALL && i != pair)
			continue;

		mV = marvell_vct5_amplitude(phydev, i);
		ethnl_cable_test_amplitude(phydev, i, mV);
	}

	return 0;
}

static int marvell_vct5_amplitude_graph(struct phy_device *phydev)
{
	struct marvell_priv *priv = phydev->priv;
	int distance;
	u16 width;
	int page;
	int err;
	u16 reg;

	if (priv->first <= TDR_SHORT_CABLE_LENGTH)
		width = MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_32nS;
	else
		width = MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_128nS;

	reg = MII_VCT5_TX_PULSE_CTRL_GT_140m_46_86mV |
		MII_VCT5_TX_PULSE_CTRL_DONT_WAIT_LINK_DOWN |
		MII_VCT5_TX_PULSE_CTRL_MAX_AMP | width;

	err = phy_write_paged(phydev, MII_MARVELL_VCT5_PAGE,
			      MII_VCT5_TX_PULSE_CTRL, reg);
	if (err)
		return err;

	/* Reading the TDR data is very MDIO heavy. We need to optimize
	 * access to keep the time to a minimum. So lock the bus once,
	 * and don't release it until complete. We can then avoid having
	 * to change the page for every access, greatly speeding things
	 * up.
	 */
	page = phy_select_page(phydev, MII_MARVELL_VCT5_PAGE);
	if (page < 0)
		goto restore_page;

	for (distance = priv->first;
	     distance <= priv->last;
	     distance += priv->step) {
		err = marvell_vct5_amplitude_distance(phydev, distance,
						      priv->pair);
		if (err)
			goto restore_page;

		if (distance > TDR_SHORT_CABLE_LENGTH &&
		    width == MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_32nS) {
			width = MII_VCT5_TX_PULSE_CTRL_PULSE_WIDTH_128nS;
			reg = MII_VCT5_TX_PULSE_CTRL_GT_140m_46_86mV |
				MII_VCT5_TX_PULSE_CTRL_DONT_WAIT_LINK_DOWN |
				MII_VCT5_TX_PULSE_CTRL_MAX_AMP | width;
			err = __phy_write(phydev, MII_VCT5_TX_PULSE_CTRL, reg);
			if (err)
				goto restore_page;
		}
	}

restore_page:
	return phy_restore_page(phydev, page, err);
}

static int marvell_cable_test_start_common(struct phy_device *phydev)
{
	int bmcr, bmsr, ret;

	/* If auto-negotiation is enabled, but not complete, the cable
	 * test never completes. So disable auto-neg.
	 */
	bmcr = phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	bmsr = phy_read(phydev, MII_BMSR);

	if (bmsr < 0)
		return bmsr;

	if (bmcr & BMCR_ANENABLE) {
		ret =  phy_clear_bits(phydev, MII_BMCR, BMCR_ANENABLE);
		if (ret < 0)
			return ret;
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	/* If the link is up, allow it some time to go down */
	if (bmsr & BMSR_LSTATUS)
		msleep(1500);

	return 0;
}

static int marvell_vct7_cable_test_start(struct phy_device *phydev)
{
	struct marvell_priv *priv = phydev->priv;
	int ret;

	ret = marvell_cable_test_start_common(phydev);
	if (ret)
		return ret;

	priv->cable_test_tdr = false;

	/* Reset the VCT5 API control to defaults, otherwise
	 * VCT7 does not work correctly.
	 */
	ret = phy_write_paged(phydev, MII_MARVELL_VCT5_PAGE,
			      MII_VCT5_CTRL,
			      MII_VCT5_CTRL_TX_SAME_CHANNEL |
			      MII_VCT5_CTRL_SAMPLES_DEFAULT |
			      MII_VCT5_CTRL_MODE_MAXIMUM_PEEK |
			      MII_VCT5_CTRL_PEEK_HYST_DEFAULT);
	if (ret)
		return ret;

	ret = phy_write_paged(phydev, MII_MARVELL_VCT5_PAGE,
			      MII_VCT5_SAMPLE_POINT_DISTANCE, 0);
	if (ret)
		return ret;

	return phy_write_paged(phydev, MII_MARVELL_VCT7_PAGE,
			       MII_VCT7_CTRL,
			       MII_VCT7_CTRL_RUN_NOW |
			       MII_VCT7_CTRL_CENTIMETERS);
}

static int marvell_vct5_cable_test_tdr_start(struct phy_device *phydev,
					     const struct phy_tdr_config *cfg)
{
	struct marvell_priv *priv = phydev->priv;
	int ret;

	priv->cable_test_tdr = true;
	priv->first = marvell_vct5_cm2distance(cfg->first);
	priv->last = marvell_vct5_cm2distance(cfg->last);
	priv->step = marvell_vct5_cm2distance(cfg->step);
	priv->pair = cfg->pair;

	if (priv->first > MII_VCT5_SAMPLE_POINT_DISTANCE_MAX)
		return -EINVAL;

	if (priv->last > MII_VCT5_SAMPLE_POINT_DISTANCE_MAX)
		return -EINVAL;

	/* Disable  VCT7 */
	ret = phy_write_paged(phydev, MII_MARVELL_VCT7_PAGE,
			      MII_VCT7_CTRL, 0);
	if (ret)
		return ret;

	ret = marvell_cable_test_start_common(phydev);
	if (ret)
		return ret;

	ret = ethnl_cable_test_pulse(phydev, 1000);
	if (ret)
		return ret;

	return ethnl_cable_test_step(phydev,
				     marvell_vct5_distance2cm(priv->first),
				     marvell_vct5_distance2cm(priv->last),
				     marvell_vct5_distance2cm(priv->step));
}

static int marvell_vct7_distance_to_length(int distance, bool meter)
{
	if (meter)
		distance *= 100;

	return distance;
}

static bool marvell_vct7_distance_valid(int result)
{
	switch (result) {
	case MII_VCT7_RESULTS_OPEN:
	case MII_VCT7_RESULTS_SAME_SHORT:
	case MII_VCT7_RESULTS_CROSS_SHORT:
		return true;
	}
	return false;
}

static int marvell_vct7_report_length(struct phy_device *phydev,
				      int pair, bool meter)
{
	int length;
	int ret;

	ret = phy_read_paged(phydev, MII_MARVELL_VCT7_PAGE,
			     MII_VCT7_PAIR_0_DISTANCE + pair);
	if (ret < 0)
		return ret;

	length = marvell_vct7_distance_to_length(ret, meter);

	ethnl_cable_test_fault_length(phydev, pair, length);

	return 0;
}

static int marvell_vct7_cable_test_report_trans(int result)
{
	switch (result) {
	case MII_VCT7_RESULTS_OK:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case MII_VCT7_RESULTS_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case MII_VCT7_RESULTS_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case MII_VCT7_RESULTS_CROSS_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT;
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int marvell_vct7_cable_test_report(struct phy_device *phydev)
{
	int pair0, pair1, pair2, pair3;
	bool meter;
	int ret;

	ret = phy_read_paged(phydev, MII_MARVELL_VCT7_PAGE,
			     MII_VCT7_RESULTS);
	if (ret < 0)
		return ret;

	pair3 = (ret & MII_VCT7_RESULTS_PAIR3_MASK) >>
		MII_VCT7_RESULTS_PAIR3_SHIFT;
	pair2 = (ret & MII_VCT7_RESULTS_PAIR2_MASK) >>
		MII_VCT7_RESULTS_PAIR2_SHIFT;
	pair1 = (ret & MII_VCT7_RESULTS_PAIR1_MASK) >>
		MII_VCT7_RESULTS_PAIR1_SHIFT;
	pair0 = (ret & MII_VCT7_RESULTS_PAIR0_MASK) >>
		MII_VCT7_RESULTS_PAIR0_SHIFT;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				marvell_vct7_cable_test_report_trans(pair0));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_B,
				marvell_vct7_cable_test_report_trans(pair1));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_C,
				marvell_vct7_cable_test_report_trans(pair2));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_D,
				marvell_vct7_cable_test_report_trans(pair3));

	ret = phy_read_paged(phydev, MII_MARVELL_VCT7_PAGE, MII_VCT7_CTRL);
	if (ret < 0)
		return ret;

	meter = ret & MII_VCT7_CTRL_METERS;

	if (marvell_vct7_distance_valid(pair0))
		marvell_vct7_report_length(phydev, 0, meter);
	if (marvell_vct7_distance_valid(pair1))
		marvell_vct7_report_length(phydev, 1, meter);
	if (marvell_vct7_distance_valid(pair2))
		marvell_vct7_report_length(phydev, 2, meter);
	if (marvell_vct7_distance_valid(pair3))
		marvell_vct7_report_length(phydev, 3, meter);

	return 0;
}

static int marvell_vct7_cable_test_get_status(struct phy_device *phydev,
					      bool *finished)
{
	struct marvell_priv *priv = phydev->priv;
	int ret;

	if (priv->cable_test_tdr) {
		ret = marvell_vct5_amplitude_graph(phydev);
		*finished = true;
		return ret;
	}

	*finished = false;

	ret = phy_read_paged(phydev, MII_MARVELL_VCT7_PAGE,
			     MII_VCT7_CTRL);

	if (ret < 0)
		return ret;

	if (!(ret & MII_VCT7_CTRL_IN_PROGRESS)) {
		*finished = true;

		return marvell_vct7_cable_test_report(phydev);
	}

	return 0;
}

static int m88e3082_vct_cable_test_start(struct phy_device *phydev)
{
	struct marvell_priv *priv = phydev->priv;
	int ret;

	/* It needs some magic workarounds described in VCT manual for this PHY.
	 */
	ret = phy_write(phydev, 29, 0x0003);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x6440);
	if (ret < 0)
		return ret;

	if (priv->vct_phase == M88E3082_VCT_PHASE1) {
		ret = phy_write(phydev, 29, 0x000a);
		if (ret < 0)
			return ret;

		ret = phy_write(phydev, 30, 0x0002);
		if (ret < 0)
			return ret;
	}

	ret = phy_write(phydev, MII_BMCR,
			BMCR_RESET | BMCR_SPEED100 | BMCR_FULLDPLX);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, MII_VCT_TXPINS, MII_VCT_TXPINS_ENVCT);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 29, 0x0003);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x0);
	if (ret < 0)
		return ret;

	if (priv->vct_phase == M88E3082_VCT_OFF) {
		priv->vct_phase = M88E3082_VCT_PHASE1;
		priv->pair = 0;

		return 0;
	}

	ret = phy_write(phydev, 29, 0x000a);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x0);
	if (ret < 0)
		return ret;

	priv->vct_phase = M88E3082_VCT_PHASE2;

	return 0;
}

static int m88e3082_vct_cable_test_report_trans(int result, u8 distance)
{
	switch (result) {
	case MII_VCT_TXRXPINS_VCTTST_OK:
		if (distance == MII_VCT_TXRXPINS_DISTRFLN_MAX)
			return ETHTOOL_A_CABLE_RESULT_CODE_OK;
		return ETHTOOL_A_CABLE_RESULT_CODE_IMPEDANCE_MISMATCH;
	case MII_VCT_TXRXPINS_VCTTST_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case MII_VCT_TXRXPINS_VCTTST_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static u32 m88e3082_vct_distrfln_2_cm(u8 distrfln)
{
	if (distrfln < 24)
		return 0;

	/* Original function for meters: y = 0.7861x - 18.862 */
	return (7861 * distrfln - 188620) / 100;
}

static int m88e3082_vct_cable_test_get_status(struct phy_device *phydev,
					      bool *finished)
{
	u8 tx_vcttst_res, rx_vcttst_res, tx_distrfln, rx_distrfln;
	struct marvell_priv *priv = phydev->priv;
	int ret, tx_result, rx_result;
	bool done_phase = true;

	*finished = false;

	ret = phy_read(phydev, MII_VCT_TXPINS);
	if (ret < 0)
		return ret;
	else if (ret & MII_VCT_TXPINS_ENVCT)
		return 0;

	tx_distrfln = ret & MII_VCT_TXRXPINS_DISTRFLN;
	tx_vcttst_res = (ret & MII_VCT_TXRXPINS_VCTTST) >>
			MII_VCT_TXRXPINS_VCTTST_SHIFT;

	ret = phy_read(phydev, MII_VCT_RXPINS);
	if (ret < 0)
		return ret;

	rx_distrfln = ret & MII_VCT_TXRXPINS_DISTRFLN;
	rx_vcttst_res = (ret & MII_VCT_TXRXPINS_VCTTST) >>
			MII_VCT_TXRXPINS_VCTTST_SHIFT;

	*finished = true;

	switch (priv->vct_phase) {
	case M88E3082_VCT_PHASE1:
		tx_result = m88e3082_vct_cable_test_report_trans(tx_vcttst_res,
								 tx_distrfln);
		rx_result = m88e3082_vct_cable_test_report_trans(rx_vcttst_res,
								 rx_distrfln);

		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					tx_result);
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_B,
					rx_result);

		if (tx_vcttst_res == MII_VCT_TXRXPINS_VCTTST_OPEN) {
			done_phase = false;
			priv->pair |= M88E3082_PAIR_A;
		} else if (tx_distrfln < MII_VCT_TXRXPINS_DISTRFLN_MAX) {
			u8 pair = ETHTOOL_A_CABLE_PAIR_A;
			u32 cm = m88e3082_vct_distrfln_2_cm(tx_distrfln);

			ethnl_cable_test_fault_length(phydev, pair, cm);
		}

		if (rx_vcttst_res == MII_VCT_TXRXPINS_VCTTST_OPEN) {
			done_phase = false;
			priv->pair |= M88E3082_PAIR_B;
		} else if (rx_distrfln < MII_VCT_TXRXPINS_DISTRFLN_MAX) {
			u8 pair = ETHTOOL_A_CABLE_PAIR_B;
			u32 cm = m88e3082_vct_distrfln_2_cm(rx_distrfln);

			ethnl_cable_test_fault_length(phydev, pair, cm);
		}

		break;
	case M88E3082_VCT_PHASE2:
		if (priv->pair & M88E3082_PAIR_A &&
		    tx_vcttst_res == MII_VCT_TXRXPINS_VCTTST_OPEN &&
		    tx_distrfln < MII_VCT_TXRXPINS_DISTRFLN_MAX) {
			u8 pair = ETHTOOL_A_CABLE_PAIR_A;
			u32 cm = m88e3082_vct_distrfln_2_cm(tx_distrfln);

			ethnl_cable_test_fault_length(phydev, pair, cm);
		}
		if (priv->pair & M88E3082_PAIR_B &&
		    rx_vcttst_res == MII_VCT_TXRXPINS_VCTTST_OPEN &&
		    rx_distrfln < MII_VCT_TXRXPINS_DISTRFLN_MAX) {
			u8 pair = ETHTOOL_A_CABLE_PAIR_B;
			u32 cm = m88e3082_vct_distrfln_2_cm(rx_distrfln);

			ethnl_cable_test_fault_length(phydev, pair, cm);
		}

		break;
	default:
		return -EINVAL;
	}

	if (!done_phase) {
		*finished = false;
		return m88e3082_vct_cable_test_start(phydev);
	}
	if (*finished)
		priv->vct_phase = M88E3082_VCT_OFF;
	return 0;
}

static int m88e1111_vct_cable_test_start(struct phy_device *phydev)
{
	int ret;

	ret = marvell_cable_test_start_common(phydev);
	if (ret)
		return ret;

	/* It needs some magic workarounds described in VCT manual for this PHY.
	 */
	ret = phy_write(phydev, 29, 0x0018);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x00c2);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x00ca);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x00c2);
	if (ret < 0)
		return ret;

	ret = phy_write_paged(phydev, MII_MARVELL_COPPER_PAGE, MII_VCT_SR,
			      MII_VCT_TXPINS_ENVCT);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 29, 0x0018);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, 30, 0x0042);
	if (ret < 0)
		return ret;

	return 0;
}

static u32 m88e1111_vct_distrfln_2_cm(u8 distrfln)
{
	if (distrfln < 36)
		return 0;

	/* Original function for meters: y = 0.8018x - 28.751 */
	return (8018 * distrfln - 287510) / 100;
}

static int m88e1111_vct_cable_test_get_status(struct phy_device *phydev,
					      bool *finished)
{
	u8 vcttst_res, distrfln;
	int ret, result;

	*finished = false;

	/* Each pair use one page: A-0, B-1, C-2, D-3 */
	for (u8 i = 0; i < 4; i++) {
		ret = phy_read_paged(phydev, i, MII_VCT_SR);
		if (ret < 0)
			return ret;
		else if (i == 0 && ret & MII_VCT_TXPINS_ENVCT)
			return 0;

		distrfln = ret & MII_VCT_TXRXPINS_DISTRFLN;
		vcttst_res = (ret & MII_VCT_TXRXPINS_VCTTST) >>
			      MII_VCT_TXRXPINS_VCTTST_SHIFT;

		result = m88e3082_vct_cable_test_report_trans(vcttst_res,
							      distrfln);
		ethnl_cable_test_result(phydev, i, result);

		if (distrfln < MII_VCT_TXRXPINS_DISTRFLN_MAX) {
			u32 cm = m88e1111_vct_distrfln_2_cm(distrfln);

			ethnl_cable_test_fault_length(phydev, i, cm);
		}
	}

	*finished = true;
	return 0;
}

#ifdef CONFIG_HWMON
struct marvell_hwmon_ops {
	int (*config)(struct phy_device *phydev);
	int (*get_temp)(struct phy_device *phydev, long *temp);
	int (*get_temp_critical)(struct phy_device *phydev, long *temp);
	int (*set_temp_critical)(struct phy_device *phydev, long temp);
	int (*get_temp_alarm)(struct phy_device *phydev, long *alarm);
};

static const struct marvell_hwmon_ops *
to_marvell_hwmon_ops(const struct phy_device *phydev)
{
	return phydev->drv->driver_data;
}

static int m88e1121_get_temp(struct phy_device *phydev, long *temp)
{
	int oldpage;
	int ret = 0;
	int val;

	*temp = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_MISC_TEST_PAGE);
	if (oldpage < 0)
		goto error;

	/* Enable temperature sensor */
	ret = __phy_read(phydev, MII_88E1121_MISC_TEST);
	if (ret < 0)
		goto error;

	ret = __phy_write(phydev, MII_88E1121_MISC_TEST,
			  ret | MII_88E1121_MISC_TEST_TEMP_SENSOR_EN);
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	val = __phy_read(phydev, MII_88E1121_MISC_TEST);
	if (val < 0) {
		ret = val;
		goto error;
	}

	/* Disable temperature sensor */
	ret = __phy_write(phydev, MII_88E1121_MISC_TEST,
			  ret & ~MII_88E1121_MISC_TEST_TEMP_SENSOR_EN);
	if (ret < 0)
		goto error;

	*temp = ((val & MII_88E1121_MISC_TEST_TEMP_MASK) - 5) * 5000;

error:
	return phy_restore_page(phydev, oldpage, ret);
}

static int m88e1510_get_temp(struct phy_device *phydev, long *temp)
{
	int ret;

	*temp = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1510_TEMP_SENSOR);
	if (ret < 0)
		return ret;

	*temp = ((ret & MII_88E1510_TEMP_SENSOR_MASK) - 25) * 1000;

	return 0;
}

static int m88e1510_get_temp_critical(struct phy_device *phydev, long *temp)
{
	int ret;

	*temp = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1121_MISC_TEST);
	if (ret < 0)
		return ret;

	*temp = (((ret & MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK) >>
		  MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT) * 5) - 25;
	/* convert to mC */
	*temp *= 1000;

	return 0;
}

static int m88e1510_set_temp_critical(struct phy_device *phydev, long temp)
{
	temp = temp / 1000;
	temp = clamp_val(DIV_ROUND_CLOSEST(temp, 5) + 5, 0, 0x1f);

	return phy_modify_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
				MII_88E1121_MISC_TEST,
				MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK,
				temp << MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT);
}

static int m88e1510_get_temp_alarm(struct phy_device *phydev, long *alarm)
{
	int ret;

	*alarm = false;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1121_MISC_TEST);
	if (ret < 0)
		return ret;

	*alarm = !!(ret & MII_88E1510_MISC_TEST_TEMP_IRQ);

	return 0;
}

static int m88e6390_get_temp(struct phy_device *phydev, long *temp)
{
	int sum = 0;
	int oldpage;
	int ret = 0;
	int i;

	*temp = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_MISC_TEST_PAGE);
	if (oldpage < 0)
		goto error;

	/* Enable temperature sensor */
	ret = __phy_read(phydev, MII_88E6390_MISC_TEST);
	if (ret < 0)
		goto error;

	ret &= ~MII_88E6390_MISC_TEST_TEMP_SENSOR_MASK;
	ret |= MII_88E6390_MISC_TEST_TEMP_SENSOR_ENABLE_SAMPLE_1S;

	ret = __phy_write(phydev, MII_88E6390_MISC_TEST, ret);
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	/* Reading the temperature sense has an errata. You need to read
	 * a number of times and take an average.
	 */
	for (i = 0; i < MII_88E6390_TEMP_SENSOR_SAMPLES; i++) {
		ret = __phy_read(phydev, MII_88E6390_TEMP_SENSOR);
		if (ret < 0)
			goto error;
		sum += ret & MII_88E6390_TEMP_SENSOR_MASK;
	}

	sum /= MII_88E6390_TEMP_SENSOR_SAMPLES;
	*temp = (sum  - 75) * 1000;

	/* Disable temperature sensor */
	ret = __phy_read(phydev, MII_88E6390_MISC_TEST);
	if (ret < 0)
		goto error;

	ret = ret & ~MII_88E6390_MISC_TEST_TEMP_SENSOR_MASK;
	ret |= MII_88E6390_MISC_TEST_TEMP_SENSOR_DISABLE;

	ret = __phy_write(phydev, MII_88E6390_MISC_TEST, ret);

error:
	phy_restore_page(phydev, oldpage, ret);

	return ret;
}

static int m88e6393_get_temp(struct phy_device *phydev, long *temp)
{
	int err;

	err = m88e1510_get_temp(phydev, temp);

	/* 88E1510 measures T + 25, while the PHY on 88E6393X switch
	 * T + 75, so we have to subtract another 50
	 */
	*temp -= 50000;

	return err;
}

static int m88e6393_get_temp_critical(struct phy_device *phydev, long *temp)
{
	int ret;

	*temp = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E6390_TEMP_SENSOR);
	if (ret < 0)
		return ret;

	*temp = (((ret & MII_88E6393_TEMP_SENSOR_THRESHOLD_MASK) >>
		  MII_88E6393_TEMP_SENSOR_THRESHOLD_SHIFT) - 75) * 1000;

	return 0;
}

static int m88e6393_set_temp_critical(struct phy_device *phydev, long temp)
{
	temp = (temp / 1000) + 75;

	return phy_modify_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
				MII_88E6390_TEMP_SENSOR,
				MII_88E6393_TEMP_SENSOR_THRESHOLD_MASK,
				temp << MII_88E6393_TEMP_SENSOR_THRESHOLD_SHIFT);
}

static int m88e6393_hwmon_config(struct phy_device *phydev)
{
	int err;

	err = m88e6393_set_temp_critical(phydev, 100000);
	if (err)
		return err;

	return phy_modify_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
				MII_88E6390_MISC_TEST,
				MII_88E6390_MISC_TEST_TEMP_SENSOR_MASK |
				MII_88E6393_MISC_TEST_SAMPLES_MASK |
				MII_88E6393_MISC_TEST_RATE_MASK,
				MII_88E6390_MISC_TEST_TEMP_SENSOR_ENABLE |
				MII_88E6393_MISC_TEST_SAMPLES_2048 |
				MII_88E6393_MISC_TEST_RATE_2_3MS);
}

static int marvell_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	const struct marvell_hwmon_ops *ops = to_marvell_hwmon_ops(phydev);
	int err = -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		if (ops->get_temp)
			err = ops->get_temp(phydev, temp);
		break;
	case hwmon_temp_crit:
		if (ops->get_temp_critical)
			err = ops->get_temp_critical(phydev, temp);
		break;
	case hwmon_temp_max_alarm:
		if (ops->get_temp_alarm)
			err = ops->get_temp_alarm(phydev, temp);
		break;
	}

	return err;
}

static int marvell_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	const struct marvell_hwmon_ops *ops = to_marvell_hwmon_ops(phydev);
	int err = -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_crit:
		if (ops->set_temp_critical)
			err = ops->set_temp_critical(phydev, temp);
		break;
	}

	return err;
}

static umode_t marvell_hwmon_is_visible(const void *data,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	const struct phy_device *phydev = data;
	const struct marvell_hwmon_ops *ops = to_marvell_hwmon_ops(phydev);

	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return ops->get_temp ? 0444 : 0;
	case hwmon_temp_max_alarm:
		return ops->get_temp_alarm ? 0444 : 0;
	case hwmon_temp_crit:
		return (ops->get_temp_critical ? 0444 : 0) |
		       (ops->set_temp_critical ? 0200 : 0);
	default:
		return 0;
	}
}

/* we can define HWMON_T_CRIT and HWMON_T_MAX_ALARM even though these are not
 * defined for all PHYs, because the hwmon code checks whether the attributes
 * exists via the .is_visible method
 */
static const struct hwmon_channel_info * const marvell_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_MAX_ALARM),
	NULL
};

static const struct hwmon_ops marvell_hwmon_hwmon_ops = {
	.is_visible = marvell_hwmon_is_visible,
	.read = marvell_hwmon_read,
	.write = marvell_hwmon_write,
};

static const struct hwmon_chip_info marvell_hwmon_chip_info = {
	.ops = &marvell_hwmon_hwmon_ops,
	.info = marvell_hwmon_info,
};

static int marvell_hwmon_name(struct phy_device *phydev)
{
	struct marvell_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	const char *devname = dev_name(dev);
	size_t len = strlen(devname);
	int i, j;

	priv->hwmon_name = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!priv->hwmon_name)
		return -ENOMEM;

	for (i = j = 0; i < len && devname[i]; i++) {
		if (isalnum(devname[i]))
			priv->hwmon_name[j++] = devname[i];
	}

	return 0;
}

static int marvell_hwmon_probe(struct phy_device *phydev)
{
	const struct marvell_hwmon_ops *ops = to_marvell_hwmon_ops(phydev);
	struct marvell_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	int err;

	if (!ops)
		return 0;

	err = marvell_hwmon_name(phydev);
	if (err)
		return err;

	priv->hwmon_dev = devm_hwmon_device_register_with_info(
		dev, priv->hwmon_name, phydev, &marvell_hwmon_chip_info, NULL);
	if (IS_ERR(priv->hwmon_dev))
		return PTR_ERR(priv->hwmon_dev);

	if (ops->config)
		err = ops->config(phydev);

	return err;
}

static const struct marvell_hwmon_ops m88e1121_hwmon_ops = {
	.get_temp = m88e1121_get_temp,
};

static const struct marvell_hwmon_ops m88e1510_hwmon_ops = {
	.get_temp = m88e1510_get_temp,
	.get_temp_critical = m88e1510_get_temp_critical,
	.set_temp_critical = m88e1510_set_temp_critical,
	.get_temp_alarm = m88e1510_get_temp_alarm,
};

static const struct marvell_hwmon_ops m88e6390_hwmon_ops = {
	.get_temp = m88e6390_get_temp,
};

static const struct marvell_hwmon_ops m88e6393_hwmon_ops = {
	.config = m88e6393_hwmon_config,
	.get_temp = m88e6393_get_temp,
	.get_temp_critical = m88e6393_get_temp_critical,
	.set_temp_critical = m88e6393_set_temp_critical,
	.get_temp_alarm = m88e1510_get_temp_alarm,
};

#define DEF_MARVELL_HWMON_OPS(s) (&(s))

#else

#define DEF_MARVELL_HWMON_OPS(s) NULL

static int marvell_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int m88e1318_led_brightness_set(struct phy_device *phydev,
				       u8 index, enum led_brightness value)
{
	int reg;

	reg = phy_read_paged(phydev, MII_MARVELL_LED_PAGE,
			     MII_88E1318S_PHY_LED_FUNC);
	if (reg < 0)
		return reg;

	switch (index) {
	case 0:
	case 1:
	case 2:
		reg &= ~(0xf << (4 * index));
		if (value == LED_OFF)
			reg |= MII_88E1318S_PHY_LED_FUNC_OFF << (4 * index);
		else
			reg |= MII_88E1318S_PHY_LED_FUNC_ON << (4 * index);
		break;
	default:
		return -EINVAL;
	}

	return phy_write_paged(phydev, MII_MARVELL_LED_PAGE,
			       MII_88E1318S_PHY_LED_FUNC, reg);
}

static int m88e1318_led_blink_set(struct phy_device *phydev, u8 index,
				  unsigned long *delay_on,
				  unsigned long *delay_off)
{
	int reg;

	reg = phy_read_paged(phydev, MII_MARVELL_LED_PAGE,
			     MII_88E1318S_PHY_LED_FUNC);
	if (reg < 0)
		return reg;

	switch (index) {
	case 0:
	case 1:
	case 2:
		reg &= ~(0xf << (4 * index));
		reg |= MII_88E1318S_PHY_LED_FUNC_BLINK << (4 * index);
		/* Reset default is 84ms */
		*delay_on = 84 / 2;
		*delay_off = 84 / 2;
		break;
	default:
		return -EINVAL;
	}

	return phy_write_paged(phydev, MII_MARVELL_LED_PAGE,
			       MII_88E1318S_PHY_LED_FUNC, reg);
}

struct marvell_led_rules {
	int mode;
	unsigned long rules;
};

static const struct marvell_led_rules marvell_led0[] = {
	{
		.mode = 0,
		.rules = BIT(TRIGGER_NETDEV_LINK),
	},
	{
		.mode = 1,
		.rules = (BIT(TRIGGER_NETDEV_LINK) |
			  BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 3,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 4,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 5,
		.rules = BIT(TRIGGER_NETDEV_TX),
	},
	{
		.mode = 6,
		.rules = BIT(TRIGGER_NETDEV_LINK),
	},
	{
		.mode = 7,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
	},
	{
		.mode = 8,
		.rules = 0,
	},
};

static const struct marvell_led_rules marvell_led1[] = {
	{
		.mode = 1,
		.rules = (BIT(TRIGGER_NETDEV_LINK) |
			  BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 2,
		.rules = (BIT(TRIGGER_NETDEV_LINK) |
			  BIT(TRIGGER_NETDEV_RX)),
	},
	{
		.mode = 3,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 4,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 6,
		.rules = (BIT(TRIGGER_NETDEV_LINK_100) |
			  BIT(TRIGGER_NETDEV_LINK_1000)),
	},
	{
		.mode = 7,
		.rules = BIT(TRIGGER_NETDEV_LINK_100),
	},
	{
		.mode = 8,
		.rules = 0,
	},
};

static const struct marvell_led_rules marvell_led2[] = {
	{
		.mode = 0,
		.rules = BIT(TRIGGER_NETDEV_LINK),
	},
	{
		.mode = 1,
		.rules = (BIT(TRIGGER_NETDEV_LINK) |
			  BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 3,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 4,
		.rules = (BIT(TRIGGER_NETDEV_RX) |
			  BIT(TRIGGER_NETDEV_TX)),
	},
	{
		.mode = 5,
		.rules = BIT(TRIGGER_NETDEV_TX),
	},
	{
		.mode = 6,
		.rules = (BIT(TRIGGER_NETDEV_LINK_10) |
			  BIT(TRIGGER_NETDEV_LINK_1000)),
	},
	{
		.mode = 7,
		.rules = BIT(TRIGGER_NETDEV_LINK_10),
	},
	{
		.mode = 8,
		.rules = 0,
	},
};

static int marvell_find_led_mode(unsigned long rules,
				 const struct marvell_led_rules *marvell_rules,
				 int count,
				 int *mode)
{
	int i;

	for (i = 0; i < count; i++) {
		if (marvell_rules[i].rules == rules) {
			*mode = marvell_rules[i].mode;
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

static int marvell_get_led_mode(u8 index, unsigned long rules, int *mode)
{
	int ret;

	switch (index) {
	case 0:
		ret = marvell_find_led_mode(rules, marvell_led0,
					    ARRAY_SIZE(marvell_led0), mode);
		break;
	case 1:
		ret = marvell_find_led_mode(rules, marvell_led1,
					    ARRAY_SIZE(marvell_led1), mode);
		break;
	case 2:
		ret = marvell_find_led_mode(rules, marvell_led2,
					    ARRAY_SIZE(marvell_led2), mode);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int marvell_find_led_rules(unsigned long *rules,
				  const struct marvell_led_rules *marvell_rules,
				  int count,
				  int mode)
{
	int i;

	for (i = 0; i < count; i++) {
		if (marvell_rules[i].mode == mode) {
			*rules = marvell_rules[i].rules;
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

static int marvell_get_led_rules(u8 index, unsigned long *rules, int mode)
{
	int ret;

	switch (index) {
	case 0:
		ret = marvell_find_led_rules(rules, marvell_led0,
					     ARRAY_SIZE(marvell_led0), mode);
		break;
	case 1:
		ret = marvell_find_led_rules(rules, marvell_led1,
					     ARRAY_SIZE(marvell_led1), mode);
		break;
	case 2:
		ret = marvell_find_led_rules(rules, marvell_led2,
					     ARRAY_SIZE(marvell_led2), mode);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int m88e1318_led_hw_is_supported(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	int mode, ret;

	switch (index) {
	case 0:
	case 1:
	case 2:
		ret = marvell_get_led_mode(index, rules, &mode);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int m88e1318_led_hw_control_set(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	int mode, ret, reg;

	switch (index) {
	case 0:
	case 1:
	case 2:
		ret = marvell_get_led_mode(index, rules, &mode);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	reg = phy_read_paged(phydev, MII_MARVELL_LED_PAGE,
			     MII_88E1318S_PHY_LED_FUNC);
	if (reg < 0)
		return reg;

	reg &= ~(0xf << (4 * index));
	reg |= mode << (4 * index);
	return phy_write_paged(phydev, MII_MARVELL_LED_PAGE,
			       MII_88E1318S_PHY_LED_FUNC, reg);
}

static int m88e1318_led_hw_control_get(struct phy_device *phydev, u8 index,
				       unsigned long *rules)
{
	int mode, reg;

	if (index > 2)
		return -EINVAL;

	reg = phy_read_paged(phydev, MII_MARVELL_LED_PAGE,
			     MII_88E1318S_PHY_LED_FUNC);
	if (reg < 0)
		return reg;

	mode = (reg >> (4 * index)) & 0xf;

	return marvell_get_led_rules(index, rules, mode);
}

static int marvell_probe(struct phy_device *phydev)
{
	struct marvell_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return marvell_hwmon_probe(phydev);
}

static int m88e1510_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
{
	struct phy_device *phydev = upstream;
	const struct sfp_module_caps *caps;
	phy_interface_t interface;
	struct device *dev;
	int oldpage;
	int ret = 0;
	u16 mode;

	dev = &phydev->mdio.dev;

	caps = sfp_get_module_caps(phydev->sfp_bus);
	interface = sfp_select_interface(phydev->sfp_bus, caps->link_modes);

	dev_info(dev, "%s SFP module inserted\n", phy_modes(interface));

	switch (interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
		mode = MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_1000X;

		break;
	case PHY_INTERFACE_MODE_100BASEX:
		mode = MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_100FX;

		break;
	case PHY_INTERFACE_MODE_SGMII:
		mode = MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII_SGMII;

		break;
	default:
		dev_err(dev, "Incompatible SFP module inserted\n");

		return -EINVAL;
	}

	oldpage = phy_select_page(phydev, MII_MARVELL_MODE_PAGE);
	if (oldpage < 0)
		goto error;

	ret = __phy_modify(phydev, MII_88E1510_GEN_CTRL_REG_1,
			   MII_88E1510_GEN_CTRL_REG_1_MODE_MASK, mode);
	if (ret < 0)
		goto error;

	ret = __phy_set_bits(phydev, MII_88E1510_GEN_CTRL_REG_1,
			     MII_88E1510_GEN_CTRL_REG_1_RESET);

error:
	return phy_restore_page(phydev, oldpage, ret);
}

static void m88e1510_sfp_remove(void *upstream)
{
	struct phy_device *phydev = upstream;
	int oldpage;
	int ret = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_MODE_PAGE);
	if (oldpage < 0)
		goto error;

	ret = __phy_modify(phydev, MII_88E1510_GEN_CTRL_REG_1,
			   MII_88E1510_GEN_CTRL_REG_1_MODE_MASK,
			   MII_88E1510_GEN_CTRL_REG_1_MODE_RGMII);
	if (ret < 0)
		goto error;

	ret = __phy_set_bits(phydev, MII_88E1510_GEN_CTRL_REG_1,
			     MII_88E1510_GEN_CTRL_REG_1_RESET);

error:
	phy_restore_page(phydev, oldpage, ret);
}

static const struct sfp_upstream_ops m88e1510_sfp_ops = {
	.module_insert = m88e1510_sfp_insert,
	.module_remove = m88e1510_sfp_remove,
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
	.connect_phy = phy_sfp_connect_phy,
	.disconnect_phy = phy_sfp_disconnect_phy,
};

static int m88e1510_probe(struct phy_device *phydev)
{
	int err;

	err = marvell_probe(phydev);
	if (err)
		return err;

	return phy_sfp_probe(phydev, &m88e1510_sfp_ops);
}

static struct phy_driver marvell_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88E1101,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1101",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = marvell_config_init,
		.config_aneg = m88e1101_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E3082,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E308X/88E609X Family",
		/* PHY_BASIC_FEATURES */
		.probe = marvell_probe,
		.config_init = marvell_config_init,
		.aneg_done = marvell_aneg_done,
		.read_status = marvell_read_status,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.cable_test_start = m88e3082_vct_cable_test_start,
		.cable_test_get_status = m88e3082_vct_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1112,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1112",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.inband_caps = m88e1111_inband_caps,
		.config_inband = m88e1111_config_inband,
		.config_init = m88e1112_config_init,
		.config_aneg = marvell_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1011_get_tunable,
		.set_tunable = m88e1011_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1111,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1111",
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.inband_caps = m88e1111_inband_caps,
		.config_inband = m88e1111_config_inband,
		.config_init = m88e1111gbe_config_init,
		.config_aneg = m88e1111_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1111_get_tunable,
		.set_tunable = m88e1111_set_tunable,
		.cable_test_start = m88e1111_vct_cable_test_start,
		.cable_test_get_status = m88e1111_vct_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1111_FINISAR,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1111 (Finisar)",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.inband_caps = m88e1111_inband_caps,
		.config_inband = m88e1111_config_inband,
		.config_init = m88e1111gbe_config_init,
		.config_aneg = m88e1111_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1111_get_tunable,
		.set_tunable = m88e1111_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1118,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1118",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e1118_config_init,
		.config_aneg = m88e1118_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1121R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1121R",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1121_hwmon_ops),
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1121_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1011_get_tunable,
		.set_tunable = m88e1011_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1318S,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1318S",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e1318_config_init,
		.config_aneg = m88e1318_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.get_wol = m88e1318_get_wol,
		.set_wol = m88e1318_set_wol,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.led_brightness_set = m88e1318_led_brightness_set,
		.led_blink_set = m88e1318_led_blink_set,
		.led_hw_is_supported = m88e1318_led_hw_is_supported,
		.led_hw_control_set = m88e1318_led_hw_control_set,
		.led_hw_control_get = m88e1318_led_hw_control_get,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1145,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1145",
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.config_init = m88e1145_config_init,
		.config_aneg = m88e1101_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1111_get_tunable,
		.set_tunable = m88e1111_set_tunable,
		.cable_test_start = m88e1111_vct_cable_test_start,
		.cable_test_get_status = m88e1111_vct_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1149R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1149R",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e1149_config_init,
		.config_aneg = m88e1118_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1240,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1240",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e1112_config_init,
		.config_aneg = marvell_config_aneg,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1011_get_tunable,
		.set_tunable = m88e1011_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1116R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1116R",
		/* PHY_GBIT_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e1116r_config_init,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1011_get_tunable,
		.set_tunable = m88e1011_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1510,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1510",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		.features = PHY_GBIT_FIBRE_FEATURES,
		.flags = PHY_POLL_CABLE_TEST,
		.probe = m88e1510_probe,
		.config_init = m88e1510_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.get_wol = m88e1318_get_wol,
		.set_wol = m88e1318_set_wol,
		.resume = m88e1510_resume,
		.suspend = marvell_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.set_loopback = m88e1510_loopback,
		.get_tunable = m88e1011_get_tunable,
		.set_tunable = m88e1011_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
		.led_brightness_set = m88e1318_led_brightness_set,
		.led_blink_set = m88e1318_led_blink_set,
		.led_hw_is_supported = m88e1318_led_hw_is_supported,
		.led_hw_control_set = m88e1318_led_hw_control_set,
		.led_hw_control_get = m88e1318_led_hw_control_get,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1540,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1540",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
		.led_brightness_set = m88e1318_led_brightness_set,
		.led_blink_set = m88e1318_led_blink_set,
		.led_hw_is_supported = m88e1318_led_hw_is_supported,
		.led_hw_control_set = m88e1318_led_hw_control_set,
		.led_hw_control_get = m88e1318_led_hw_control_get,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1545,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1545",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		.probe = marvell_probe,
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
		.led_brightness_set = m88e1318_led_brightness_set,
		.led_blink_set = m88e1318_led_blink_set,
		.led_hw_is_supported = m88e1318_led_hw_is_supported,
		.led_hw_control_set = m88e1318_led_hw_control_set,
		.led_hw_control_get = m88e1318_led_hw_control_get,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E3016,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E3016",
		/* PHY_BASIC_FEATURES */
		.probe = marvell_probe,
		.config_init = m88e3016_config_init,
		.aneg_done = marvell_aneg_done,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E6250_FAMILY,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E6250 Family",
		/* PHY_BASIC_FEATURES */
		.probe = marvell_probe,
		.aneg_done = marvell_aneg_done,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.get_sset_count = marvell_get_sset_count_simple,
		.get_strings = marvell_get_strings_simple,
		.get_stats = marvell_get_stats_simple,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E6341_FAMILY,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E6341 Family",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e6390_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E6390_FAMILY,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E6390 Family",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e6390_hwmon_ops),
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e6390_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E6393_FAMILY,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E6393 Family",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e6393_hwmon_ops),
		/* PHY_GBIT_FEATURES */
		.flags = PHY_POLL_CABLE_TEST,
		.probe = marvell_probe,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.cable_test_start = marvell_vct7_cable_test_start,
		.cable_test_tdr_start = marvell_vct5_cable_test_tdr_start,
		.cable_test_get_status = marvell_vct7_cable_test_get_status,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1340S,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1340S",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		.probe = marvell_probe,
		/* PHY_GBIT_FEATURES */
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1548P,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1548P",
		.driver_data = DEF_MARVELL_HWMON_OPS(m88e1510_hwmon_ops),
		.probe = marvell_probe,
		.features = PHY_GBIT_FIBRE_FEATURES,
		.config_init = marvell_1011gbe_config_init,
		.config_aneg = m88e1510_config_aneg,
		.read_status = marvell_read_status,
		.config_intr = marvell_config_intr,
		.handle_interrupt = marvell_handle_interrupt,
		.resume = genphy_resume,
		.suspend = genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.get_tunable = m88e1540_get_tunable,
		.set_tunable = m88e1540_set_tunable,
		.led_brightness_set = m88e1318_led_brightness_set,
		.led_blink_set = m88e1318_led_blink_set,
		.led_hw_is_supported = m88e1318_led_hw_is_supported,
		.led_hw_control_set = m88e1318_led_hw_control_set,
		.led_hw_control_get = m88e1318_led_hw_control_get,
	},
};

module_phy_driver(marvell_drivers);

static const struct mdio_device_id __maybe_unused marvell_tbl[] = {
	{ MARVELL_PHY_ID_88E1101, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E3082, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1112, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1111, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1111_FINISAR, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1118, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1121R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1145, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1149R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1240, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1318S, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1116R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1510, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1540, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1545, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E3016, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E6250_FAMILY, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E6341_FAMILY, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E6390_FAMILY, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E6393_FAMILY, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1340S, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1548P, MARVELL_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, marvell_tbl);
