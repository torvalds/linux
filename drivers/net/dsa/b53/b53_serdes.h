/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Northstar Plus switch SerDes/SGMII PHY definitions
 *
 * Copyright (C) 2018 Florian Fainelli <f.fainelli@gmail.com>
 */

#include <linux/phy.h>
#include <linux/types.h>

/* Non-standard page used to access SerDes PHY registers on NorthStar Plus */
#define B53_SERDES_PAGE			0x16
#define B53_SERDES_BLKADDR		0x3e
#define B53_SERDES_LANE			0x3c

#define B53_SERDES_ID0			0x20
#define  SERDES_ID0_MODEL_MASK		0x3f
#define  SERDES_ID0_REV_NUM_SHIFT	11
#define  SERDES_ID0_REV_NUM_MASK	0x7
#define  SERDES_ID0_REV_LETTER_SHIFT	14

#define B53_SERDES_MII_REG(x)		(0x20 + (x) * 2)
#define B53_SERDES_DIGITAL_CONTROL(x)	(0x1e + (x) * 2)
#define B53_SERDES_DIGITAL_STATUS	0x28

/* SERDES_DIGITAL_CONTROL1 */
#define  FIBER_MODE_1000X		BIT(0)
#define  TBI_INTERFACE			BIT(1)
#define  SIGNAL_DETECT_EN		BIT(2)
#define  INVERT_SIGNAL_DETECT		BIT(3)
#define  AUTODET_EN			BIT(4)
#define  SGMII_MASTER_MODE		BIT(5)
#define  DISABLE_DLL_PWRDOWN		BIT(6)
#define  CRC_CHECKER_DIS		BIT(7)
#define  COMMA_DET_EN			BIT(8)
#define  ZERO_COMMA_DET_EN		BIT(9)
#define  REMOTE_LOOPBACK		BIT(10)
#define  SEL_RX_PKTS_FOR_CNTR		BIT(11)
#define  MASTER_MDIO_PHY_SEL		BIT(13)
#define  DISABLE_SIGNAL_DETECT_FLT	BIT(14)

/* SERDES_DIGITAL_CONTROL2 */
#define  EN_PARALLEL_DET		BIT(0)
#define  DIS_FALSE_LINK			BIT(1)
#define  FLT_FORCE_LINK			BIT(2)
#define  EN_AUTONEG_ERR_TIMER		BIT(3)
#define  DIS_REMOTE_FAULT_SENSING	BIT(4)
#define  FORCE_XMIT_DATA		BIT(5)
#define  AUTONEG_FAST_TIMERS		BIT(6)
#define  DIS_CARRIER_EXTEND		BIT(7)
#define  DIS_TRRR_GENERATION		BIT(8)
#define  BYPASS_PCS_RX			BIT(9)
#define  BYPASS_PCS_TX			BIT(10)
#define  TEST_CNTR_EN			BIT(11)
#define  TX_PACKET_SEQ_TEST		BIT(12)
#define  TX_IDLE_JAM_SEQ_TEST		BIT(13)
#define  CLR_BER_CNTR			BIT(14)

/* SERDES_DIGITAL_CONTROL3 */
#define  TX_FIFO_RST			BIT(0)
#define  FIFO_ELAST_TX_RX_SHIFT		1
#define  FIFO_ELAST_TX_RX_5K		0
#define  FIFO_ELAST_TX_RX_10K		1
#define  FIFO_ELAST_TX_RX_13_5K		2
#define  FIFO_ELAST_TX_RX_18_5K		3
#define  BLOCK_TXEN_MODE		BIT(9)
#define  JAM_FALSE_CARRIER_MODE		BIT(10)
#define  EXT_PHY_CRS_MODE		BIT(11)
#define  INVERT_EXT_PHY_CRS		BIT(12)
#define  DISABLE_TX_CRS			BIT(13)

/* SERDES_DIGITAL_STATUS */
#define  SGMII_MODE			BIT(0)
#define  LINK_STATUS			BIT(1)
#define  DUPLEX_STATUS			BIT(2)
#define  SPEED_STATUS_SHIFT		3
#define  SPEED_STATUS_10		0
#define  SPEED_STATUS_100		1
#define  SPEED_STATUS_1000		2
#define  SPEED_STATUS_2500		3
#define  SPEED_STATUS_MASK		SPEED_STATUS_2500
#define  PAUSE_RESOLUTION_TX_SIDE	BIT(5)
#define  PAUSE_RESOLUTION_RX_SIDE	BIT(6)
#define  LINK_STATUS_CHANGE		BIT(7)
#define  EARLY_END_EXT_DET		BIT(8)
#define  CARRIER_EXT_ERR_DET		BIT(9)
#define  RX_ERR_DET			BIT(10)
#define  TX_ERR_DET			BIT(11)
#define  CRC_ERR_DET			BIT(12)
#define  FALSE_CARRIER_ERR_DET		BIT(13)
#define  RXFIFO_ERR_DET			BIT(14)
#define  TXFIFO_ERR_DET			BIT(15)

/* Block offsets */
#define SERDES_DIGITAL_BLK		0x8300
#define SERDES_ID0			0x8310
#define SERDES_MII_BLK			0xffe0
#define SERDES_XGXSBLK0_BLOCKADDRESS	0xffd0

struct phylink_link_state;

static inline u8 b53_serdes_map_lane(struct b53_device *dev, int port)
{
	if (!dev->ops->serdes_map_lane)
		return B53_INVALID_LANE;

	return dev->ops->serdes_map_lane(dev, port);
}

void b53_serdes_link_set(struct b53_device *dev, int port, unsigned int mode,
			 phy_interface_t interface, bool link_up);
struct phylink_pcs *b53_serdes_phylink_mac_select_pcs(struct b53_device *dev,
						      int port,
						      phy_interface_t interface);
void b53_serdes_phylink_get_caps(struct b53_device *dev, int port,
				 struct phylink_config *config);
#if IS_ENABLED(CONFIG_B53_SERDES)
int b53_serdes_init(struct b53_device *dev, int port);
#else
static inline int b53_serdes_init(struct b53_device *dev, int port)
{
	return -ENODEV;
}
#endif
