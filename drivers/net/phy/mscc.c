// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for Microsemi VSC85xx PHYs
 *
 * Author: Nagaraju Lakkaraju
 * License: Dual MIT/GPL
 * Copyright (c) 2016 Microsemi Corporation
 */

#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/netdevice.h>
#include <dt-bindings/net/mscc-phy-vsc8531.h>

#include <linux/scatterlist.h>
#include <crypto/skcipher.h>

#if IS_ENABLED(CONFIG_MACSEC)
#include <net/macsec.h>
#endif

#include "mscc_macsec.h"
#include "mscc_mac.h"
#include "mscc_fc_buffer.h"

enum rgmii_rx_clock_delay {
	RGMII_RX_CLK_DELAY_0_2_NS = 0,
	RGMII_RX_CLK_DELAY_0_8_NS = 1,
	RGMII_RX_CLK_DELAY_1_1_NS = 2,
	RGMII_RX_CLK_DELAY_1_7_NS = 3,
	RGMII_RX_CLK_DELAY_2_0_NS = 4,
	RGMII_RX_CLK_DELAY_2_3_NS = 5,
	RGMII_RX_CLK_DELAY_2_6_NS = 6,
	RGMII_RX_CLK_DELAY_3_4_NS = 7
};

/* Microsemi VSC85xx PHY registers */
/* IEEE 802. Std Registers */
#define MSCC_PHY_BYPASS_CONTROL		  18
#define DISABLE_HP_AUTO_MDIX_MASK	  0x0080
#define DISABLE_PAIR_SWAP_CORR_MASK	  0x0020
#define DISABLE_POLARITY_CORR_MASK	  0x0010
#define PARALLEL_DET_IGNORE_ADVERTISED    0x0008

#define MSCC_PHY_EXT_CNTL_STATUS          22
#define SMI_BROADCAST_WR_EN		  0x0001

#define MSCC_PHY_ERR_RX_CNT		  19
#define MSCC_PHY_ERR_FALSE_CARRIER_CNT	  20
#define MSCC_PHY_ERR_LINK_DISCONNECT_CNT  21
#define ERR_CNT_MASK			  GENMASK(7, 0)

#define MSCC_PHY_EXT_PHY_CNTL_1           23
#define MAC_IF_SELECTION_MASK             0x1800
#define MAC_IF_SELECTION_GMII             0
#define MAC_IF_SELECTION_RMII             1
#define MAC_IF_SELECTION_RGMII            2
#define MAC_IF_SELECTION_POS              11
#define VSC8584_MAC_IF_SELECTION_MASK     0x1000
#define VSC8584_MAC_IF_SELECTION_SGMII    0
#define VSC8584_MAC_IF_SELECTION_1000BASEX 1
#define VSC8584_MAC_IF_SELECTION_POS      12
#define FAR_END_LOOPBACK_MODE_MASK        0x0008
#define MEDIA_OP_MODE_MASK		  0x0700
#define MEDIA_OP_MODE_COPPER		  0
#define MEDIA_OP_MODE_SERDES		  1
#define MEDIA_OP_MODE_1000BASEX		  2
#define MEDIA_OP_MODE_100BASEFX		  3
#define MEDIA_OP_MODE_AMS_COPPER_SERDES	  5
#define MEDIA_OP_MODE_AMS_COPPER_1000BASEX	6
#define MEDIA_OP_MODE_AMS_COPPER_100BASEFX	7
#define MEDIA_OP_MODE_POS		  8

#define MSCC_PHY_EXT_PHY_CNTL_2		  24

#define MII_VSC85XX_INT_MASK		  25
#define MII_VSC85XX_INT_MASK_MASK	  0xa020
#define MII_VSC85XX_INT_MASK_WOL	  0x0040
#define MII_VSC85XX_INT_STATUS		  26

#define MSCC_PHY_WOL_MAC_CONTROL          27
#define EDGE_RATE_CNTL_POS                5
#define EDGE_RATE_CNTL_MASK               0x00E0

#define MSCC_PHY_DEV_AUX_CNTL		  28
#define HP_AUTO_MDIX_X_OVER_IND_MASK	  0x2000

#define MSCC_PHY_LED_MODE_SEL		  29
#define LED_MODE_SEL_POS(x)		  ((x) * 4)
#define LED_MODE_SEL_MASK(x)		  (GENMASK(3, 0) << LED_MODE_SEL_POS(x))
#define LED_MODE_SEL(x, mode)		  (((mode) << LED_MODE_SEL_POS(x)) & LED_MODE_SEL_MASK(x))

#define MSCC_EXT_PAGE_CSR_CNTL_17	  17
#define MSCC_EXT_PAGE_CSR_CNTL_18	  18

#define MSCC_EXT_PAGE_CSR_CNTL_19	  19
#define MSCC_PHY_CSR_CNTL_19_REG_ADDR(x)  (x)
#define MSCC_PHY_CSR_CNTL_19_TARGET(x)	  ((x) << 12)
#define MSCC_PHY_CSR_CNTL_19_READ	  BIT(14)
#define MSCC_PHY_CSR_CNTL_19_CMD	  BIT(15)

#define MSCC_EXT_PAGE_CSR_CNTL_20	  20
#define MSCC_PHY_CSR_CNTL_20_TARGET(x)	  (x)

#define PHY_MCB_TARGET			  0x07
#define PHY_MCB_S6G_WRITE		  BIT(31)
#define PHY_MCB_S6G_READ		  BIT(30)

#define PHY_S6G_PLL5G_CFG0		  0x06
#define PHY_S6G_LCPLL_CFG		  0x11
#define PHY_S6G_PLL_CFG			  0x2b
#define PHY_S6G_COMMON_CFG		  0x2c
#define PHY_S6G_GPC_CFG			  0x2e
#define PHY_S6G_MISC_CFG		  0x3b
#define PHY_MCB_S6G_CFG			  0x3f
#define PHY_S6G_DFT_CFG2		  0x3e
#define PHY_S6G_PLL_STATUS		  0x31
#define PHY_S6G_IB_STATUS0		  0x2f

#define PHY_S6G_SYS_RST_POS		  31
#define PHY_S6G_ENA_LANE_POS		  18
#define PHY_S6G_ENA_LOOP_POS		  8
#define PHY_S6G_QRATE_POS		  6
#define PHY_S6G_IF_MODE_POS		  4
#define PHY_S6G_PLL_ENA_OFFS_POS	  21
#define PHY_S6G_PLL_FSM_CTRL_DATA_POS	  8
#define PHY_S6G_PLL_FSM_ENA_POS		  7

#define MSCC_EXT_PAGE_MACSEC_17		  17
#define MSCC_EXT_PAGE_MACSEC_18		  18

#define MSCC_EXT_PAGE_MACSEC_19		  19
#define MSCC_PHY_MACSEC_19_REG_ADDR(x)	  (x)
#define MSCC_PHY_MACSEC_19_TARGET(x)	  ((x) << 12)
#define MSCC_PHY_MACSEC_19_READ		  BIT(14)
#define MSCC_PHY_MACSEC_19_CMD		  BIT(15)

#define MSCC_EXT_PAGE_MACSEC_20		  20
#define MSCC_PHY_MACSEC_20_TARGET(x)	  (x)
enum macsec_bank {
	FC_BUFFER   = 0x04,
	HOST_MAC    = 0x05,
	LINE_MAC    = 0x06,
	IP_1588     = 0x0e,
	MACSEC_INGR = 0x38,
	MACSEC_EGR  = 0x3c,
};

#define MSCC_EXT_PAGE_ACCESS		  31
#define MSCC_PHY_PAGE_STANDARD		  0x0000 /* Standard registers */
#define MSCC_PHY_PAGE_EXTENDED		  0x0001 /* Extended registers */
#define MSCC_PHY_PAGE_EXTENDED_2	  0x0002 /* Extended reg - page 2 */
#define MSCC_PHY_PAGE_EXTENDED_3	  0x0003 /* Extended reg - page 3 */
#define MSCC_PHY_PAGE_EXTENDED_4	  0x0004 /* Extended reg - page 4 */
#define MSCC_PHY_PAGE_CSR_CNTL		  MSCC_PHY_PAGE_EXTENDED_4
#define MSCC_PHY_PAGE_MACSEC		  MSCC_PHY_PAGE_EXTENDED_4
/* Extended reg - GPIO; this is a bank of registers that are shared for all PHYs
 * in the same package.
 */
#define MSCC_PHY_PAGE_EXTENDED_GPIO	  0x0010 /* Extended reg - GPIO */
#define MSCC_PHY_PAGE_TEST		  0x2a30 /* Test reg */
#define MSCC_PHY_PAGE_TR		  0x52b5 /* Token ring registers */

/* Extended Page 1 Registers */
#define MSCC_PHY_CU_MEDIA_CRC_VALID_CNT	  18
#define VALID_CRC_CNT_CRC_MASK		  GENMASK(13, 0)

#define MSCC_PHY_EXT_MODE_CNTL		  19
#define FORCE_MDI_CROSSOVER_MASK	  0x000C
#define FORCE_MDI_CROSSOVER_MDIX	  0x000C
#define FORCE_MDI_CROSSOVER_MDI		  0x0008

#define MSCC_PHY_ACTIPHY_CNTL		  20
#define PHY_ADDR_REVERSED		  0x0200
#define DOWNSHIFT_CNTL_MASK		  0x001C
#define DOWNSHIFT_EN			  0x0010
#define DOWNSHIFT_CNTL_POS		  2

#define MSCC_PHY_EXT_PHY_CNTL_4		  23
#define PHY_CNTL_4_ADDR_POS		  11

#define MSCC_PHY_VERIPHY_CNTL_2		  25

#define MSCC_PHY_VERIPHY_CNTL_3		  26

/* Extended Page 2 Registers */
#define MSCC_PHY_CU_PMD_TX_CNTL		  16

#define MSCC_PHY_RGMII_CNTL		  20
#define RGMII_RX_CLK_DELAY_MASK		  0x0070
#define RGMII_RX_CLK_DELAY_POS		  4

#define MSCC_PHY_WOL_LOWER_MAC_ADDR	  21
#define MSCC_PHY_WOL_MID_MAC_ADDR	  22
#define MSCC_PHY_WOL_UPPER_MAC_ADDR	  23
#define MSCC_PHY_WOL_LOWER_PASSWD	  24
#define MSCC_PHY_WOL_MID_PASSWD		  25
#define MSCC_PHY_WOL_UPPER_PASSWD	  26

#define MSCC_PHY_WOL_MAC_CONTROL	  27
#define SECURE_ON_ENABLE		  0x8000
#define SECURE_ON_PASSWD_LEN_4		  0x4000

#define MSCC_PHY_EXTENDED_INT		  28
#define MSCC_PHY_EXTENDED_INT_MS_EGR	  BIT(9)

/* Extended Page 3 Registers */
#define MSCC_PHY_SERDES_TX_VALID_CNT	  21
#define MSCC_PHY_SERDES_TX_CRC_ERR_CNT	  22
#define MSCC_PHY_SERDES_RX_VALID_CNT	  28
#define MSCC_PHY_SERDES_RX_CRC_ERR_CNT	  29

/* Extended page GPIO Registers */
#define MSCC_DW8051_CNTL_STATUS		  0
#define MICRO_NSOFT_RESET		  0x8000
#define RUN_FROM_INT_ROM		  0x4000
#define AUTOINC_ADDR			  0x2000
#define PATCH_RAM_CLK			  0x1000
#define MICRO_PATCH_EN			  0x0080
#define DW8051_CLK_EN			  0x0010
#define MICRO_CLK_EN			  0x0008
#define MICRO_CLK_DIVIDE(x)		  ((x) >> 1)
#define MSCC_DW8051_VLD_MASK		  0xf1ff

/* x Address in range 1-4 */
#define MSCC_TRAP_ROM_ADDR(x)		  ((x) * 2 + 1)
#define MSCC_PATCH_RAM_ADDR(x)		  (((x) + 1) * 2)
#define MSCC_INT_MEM_ADDR		  11

#define MSCC_INT_MEM_CNTL		  12
#define READ_SFR			  0x6000
#define READ_PRAM			  0x4000
#define READ_ROM			  0x2000
#define READ_RAM			  0x0000
#define INT_MEM_WRITE_EN		  0x1000
#define EN_PATCH_RAM_TRAP_ADDR(x)	  (0x0100 << ((x) - 1))
#define INT_MEM_DATA_M			  0x00ff
#define INT_MEM_DATA(x)			  (INT_MEM_DATA_M & (x))

#define MSCC_PHY_PROC_CMD		  18
#define PROC_CMD_NCOMPLETED		  0x8000
#define PROC_CMD_FAILED			  0x4000
#define PROC_CMD_SGMII_PORT(x)		  ((x) << 8)
#define PROC_CMD_FIBER_PORT(x)		  (0x0100 << (x) % 4)
#define PROC_CMD_QSGMII_PORT		  0x0c00
#define PROC_CMD_RST_CONF_PORT		  0x0080
#define PROC_CMD_RECONF_PORT		  0x0000
#define PROC_CMD_READ_MOD_WRITE_PORT	  0x0040
#define PROC_CMD_WRITE			  0x0040
#define PROC_CMD_READ			  0x0000
#define PROC_CMD_FIBER_DISABLE		  0x0020
#define PROC_CMD_FIBER_100BASE_FX	  0x0010
#define PROC_CMD_FIBER_1000BASE_X	  0x0000
#define PROC_CMD_SGMII_MAC		  0x0030
#define PROC_CMD_QSGMII_MAC		  0x0020
#define PROC_CMD_NO_MAC_CONF		  0x0000
#define PROC_CMD_1588_DEFAULT_INIT	  0x0010
#define PROC_CMD_NOP			  0x000f
#define PROC_CMD_PHY_INIT		  0x000a
#define PROC_CMD_CRC16			  0x0008
#define PROC_CMD_FIBER_MEDIA_CONF	  0x0001
#define PROC_CMD_MCB_ACCESS_MAC_CONF	  0x0000
#define PROC_CMD_NCOMPLETED_TIMEOUT_MS    500

#define MSCC_PHY_MAC_CFG_FASTLINK	  19
#define MAC_CFG_MASK			  0xc000
#define MAC_CFG_SGMII			  0x0000
#define MAC_CFG_QSGMII			  0x4000

/* Test page Registers */
#define MSCC_PHY_TEST_PAGE_5		  5
#define MSCC_PHY_TEST_PAGE_8		  8
#define MSCC_PHY_TEST_PAGE_9		  9
#define MSCC_PHY_TEST_PAGE_20		  20
#define MSCC_PHY_TEST_PAGE_24		  24

/* Token ring page Registers */
#define MSCC_PHY_TR_CNTL		  16
#define TR_WRITE			  0x8000
#define TR_ADDR(x)			  (0x7fff & (x))
#define MSCC_PHY_TR_LSB			  17
#define MSCC_PHY_TR_MSB			  18

/* Microsemi PHY ID's
 *   Code assumes lowest nibble is 0
 */
#define PHY_ID_VSC8504			  0x000704c0
#define PHY_ID_VSC8514			  0x00070670
#define PHY_ID_VSC8530			  0x00070560
#define PHY_ID_VSC8531			  0x00070570
#define PHY_ID_VSC8540			  0x00070760
#define PHY_ID_VSC8541			  0x00070770
#define PHY_ID_VSC8552			  0x000704e0
#define PHY_ID_VSC856X			  0x000707e0
#define PHY_ID_VSC8572			  0x000704d0
#define PHY_ID_VSC8574			  0x000704a0
#define PHY_ID_VSC8575			  0x000707d0
#define PHY_ID_VSC8582			  0x000707b0
#define PHY_ID_VSC8584			  0x000707c0

#define MSCC_VDDMAC_1500		  1500
#define MSCC_VDDMAC_1800		  1800
#define MSCC_VDDMAC_2500		  2500
#define MSCC_VDDMAC_3300		  3300

#define DOWNSHIFT_COUNT_MAX		  5

#define MAX_LEDS			  4

#define VSC8584_SUPP_LED_MODES (BIT(VSC8531_LINK_ACTIVITY) | \
				BIT(VSC8531_LINK_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_100_ACTIVITY) | \
				BIT(VSC8531_LINK_10_ACTIVITY) | \
				BIT(VSC8531_LINK_100_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_10_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_10_100_ACTIVITY) | \
				BIT(VSC8584_LINK_100FX_1000X_ACTIVITY) | \
				BIT(VSC8531_DUPLEX_COLLISION) | \
				BIT(VSC8531_COLLISION) | \
				BIT(VSC8531_ACTIVITY) | \
				BIT(VSC8584_100FX_1000X_ACTIVITY) | \
				BIT(VSC8531_AUTONEG_FAULT) | \
				BIT(VSC8531_SERIAL_MODE) | \
				BIT(VSC8531_FORCE_LED_OFF) | \
				BIT(VSC8531_FORCE_LED_ON))

#define VSC85XX_SUPP_LED_MODES (BIT(VSC8531_LINK_ACTIVITY) | \
				BIT(VSC8531_LINK_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_100_ACTIVITY) | \
				BIT(VSC8531_LINK_10_ACTIVITY) | \
				BIT(VSC8531_LINK_100_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_10_1000_ACTIVITY) | \
				BIT(VSC8531_LINK_10_100_ACTIVITY) | \
				BIT(VSC8531_DUPLEX_COLLISION) | \
				BIT(VSC8531_COLLISION) | \
				BIT(VSC8531_ACTIVITY) | \
				BIT(VSC8531_AUTONEG_FAULT) | \
				BIT(VSC8531_SERIAL_MODE) | \
				BIT(VSC8531_FORCE_LED_OFF) | \
				BIT(VSC8531_FORCE_LED_ON))

#define MSCC_VSC8584_REVB_INT8051_FW		"microchip/mscc_vsc8584_revb_int8051_fb48.bin"
#define MSCC_VSC8584_REVB_INT8051_FW_START_ADDR	0xe800
#define MSCC_VSC8584_REVB_INT8051_FW_CRC	0xfb48

#define MSCC_VSC8574_REVB_INT8051_FW		"microchip/mscc_vsc8574_revb_int8051_29e8.bin"
#define MSCC_VSC8574_REVB_INT8051_FW_START_ADDR	0x4000
#define MSCC_VSC8574_REVB_INT8051_FW_CRC	0x29e8

#define VSC8584_REVB				0x0001
#define MSCC_DEV_REV_MASK			GENMASK(3, 0)

struct reg_val {
	u16	reg;
	u32	val;
};

struct vsc85xx_hw_stat {
	const char *string;
	u8 reg;
	u16 page;
	u16 mask;
};

static const struct vsc85xx_hw_stat vsc85xx_hw_stats[] = {
	{
		.string	= "phy_receive_errors",
		.reg	= MSCC_PHY_ERR_RX_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_false_carrier",
		.reg	= MSCC_PHY_ERR_FALSE_CARRIER_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_cu_media_link_disconnect",
		.reg	= MSCC_PHY_ERR_LINK_DISCONNECT_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_cu_media_crc_good_count",
		.reg	= MSCC_PHY_CU_MEDIA_CRC_VALID_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED,
		.mask	= VALID_CRC_CNT_CRC_MASK,
	}, {
		.string	= "phy_cu_media_crc_error_count",
		.reg	= MSCC_PHY_EXT_PHY_CNTL_4,
		.page	= MSCC_PHY_PAGE_EXTENDED,
		.mask	= ERR_CNT_MASK,
	},
};

static const struct vsc85xx_hw_stat vsc8584_hw_stats[] = {
	{
		.string	= "phy_receive_errors",
		.reg	= MSCC_PHY_ERR_RX_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_false_carrier",
		.reg	= MSCC_PHY_ERR_FALSE_CARRIER_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_cu_media_link_disconnect",
		.reg	= MSCC_PHY_ERR_LINK_DISCONNECT_CNT,
		.page	= MSCC_PHY_PAGE_STANDARD,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_cu_media_crc_good_count",
		.reg	= MSCC_PHY_CU_MEDIA_CRC_VALID_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED,
		.mask	= VALID_CRC_CNT_CRC_MASK,
	}, {
		.string	= "phy_cu_media_crc_error_count",
		.reg	= MSCC_PHY_EXT_PHY_CNTL_4,
		.page	= MSCC_PHY_PAGE_EXTENDED,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_serdes_tx_good_pkt_count",
		.reg	= MSCC_PHY_SERDES_TX_VALID_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED_3,
		.mask	= VALID_CRC_CNT_CRC_MASK,
	}, {
		.string	= "phy_serdes_tx_bad_crc_count",
		.reg	= MSCC_PHY_SERDES_TX_CRC_ERR_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED_3,
		.mask	= ERR_CNT_MASK,
	}, {
		.string	= "phy_serdes_rx_good_pkt_count",
		.reg	= MSCC_PHY_SERDES_RX_VALID_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED_3,
		.mask	= VALID_CRC_CNT_CRC_MASK,
	}, {
		.string	= "phy_serdes_rx_bad_crc_count",
		.reg	= MSCC_PHY_SERDES_RX_CRC_ERR_CNT,
		.page	= MSCC_PHY_PAGE_EXTENDED_3,
		.mask	= ERR_CNT_MASK,
	},
};

#if IS_ENABLED(CONFIG_MACSEC)
struct macsec_flow {
	struct list_head list;
	enum mscc_macsec_destination_ports port;
	enum macsec_bank bank;
	u32 index;
	int assoc_num;
	bool has_transformation;

	/* Highest takes precedence [0..15] */
	u8 priority;

	u8 key[MACSEC_KEYID_LEN];

	union {
		struct macsec_rx_sa *rx_sa;
		struct macsec_tx_sa *tx_sa;
	};

	/* Matching */
	struct {
		u8 sci:1;
		u8 tagged:1;
		u8 untagged:1;
		u8 etype:1;
	} match;

	u16 etype;

	/* Action */
	struct {
		u8 bypass:1;
		u8 drop:1;
	} action;

};
#endif

struct vsc8531_private {
	int rate_magic;
	u16 supp_led_modes;
	u32 leds_mode[MAX_LEDS];
	u8 nleds;
	const struct vsc85xx_hw_stat *hw_stats;
	u64 *stats;
	int nstats;
	bool pkg_init;
	/* For multiple port PHYs; the MDIO address of the base PHY in the
	 * package.
	 */
	unsigned int base_addr;

#if IS_ENABLED(CONFIG_MACSEC)
	/* MACsec fields:
	 * - One SecY per device (enforced at the s/w implementation level)
	 * - macsec_flows: list of h/w flows
	 * - ingr_flows: bitmap of ingress flows
	 * - egr_flows: bitmap of egress flows
	 */
	struct macsec_secy *secy;
	struct list_head macsec_flows;
	unsigned long ingr_flows;
	unsigned long egr_flows;
#endif
};

#ifdef CONFIG_OF_MDIO
struct vsc8531_edge_rate_table {
	u32 vddmac;
	u32 slowdown[8];
};

static const struct vsc8531_edge_rate_table edge_table[] = {
	{MSCC_VDDMAC_3300, { 0, 2,  4,  7, 10, 17, 29, 53} },
	{MSCC_VDDMAC_2500, { 0, 3,  6, 10, 14, 23, 37, 63} },
	{MSCC_VDDMAC_1800, { 0, 5,  9, 16, 23, 35, 52, 76} },
	{MSCC_VDDMAC_1500, { 0, 6, 14, 21, 29, 42, 58, 77} },
};
#endif /* CONFIG_OF_MDIO */

static int vsc85xx_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MSCC_EXT_PAGE_ACCESS);
}

static int vsc85xx_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MSCC_EXT_PAGE_ACCESS, page);
}

static int vsc85xx_get_sset_count(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;

	if (!priv)
		return 0;

	return priv->nstats;
}

static void vsc85xx_get_strings(struct phy_device *phydev, u8 *data)
{
	struct vsc8531_private *priv = phydev->priv;
	int i;

	if (!priv)
		return;

	for (i = 0; i < priv->nstats; i++)
		strlcpy(data + i * ETH_GSTRING_LEN, priv->hw_stats[i].string,
			ETH_GSTRING_LEN);
}

static u64 vsc85xx_get_stat(struct phy_device *phydev, int i)
{
	struct vsc8531_private *priv = phydev->priv;
	int val;

	val = phy_read_paged(phydev, priv->hw_stats[i].page,
			     priv->hw_stats[i].reg);
	if (val < 0)
		return U64_MAX;

	val = val & priv->hw_stats[i].mask;
	priv->stats[i] += val;

	return priv->stats[i];
}

static void vsc85xx_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	struct vsc8531_private *priv = phydev->priv;
	int i;

	if (!priv)
		return;

	for (i = 0; i < priv->nstats; i++)
		data[i] = vsc85xx_get_stat(phydev, i);
}

static int vsc85xx_led_cntl_set(struct phy_device *phydev,
				u8 led_num,
				u8 mode)
{
	int rc;
	u16 reg_val;

	mutex_lock(&phydev->lock);
	reg_val = phy_read(phydev, MSCC_PHY_LED_MODE_SEL);
	reg_val &= ~LED_MODE_SEL_MASK(led_num);
	reg_val |= LED_MODE_SEL(led_num, (u16)mode);
	rc = phy_write(phydev, MSCC_PHY_LED_MODE_SEL, reg_val);
	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_mdix_get(struct phy_device *phydev, u8 *mdix)
{
	u16 reg_val;

	reg_val = phy_read(phydev, MSCC_PHY_DEV_AUX_CNTL);
	if (reg_val & HP_AUTO_MDIX_X_OVER_IND_MASK)
		*mdix = ETH_TP_MDI_X;
	else
		*mdix = ETH_TP_MDI;

	return 0;
}

static int vsc85xx_mdix_set(struct phy_device *phydev, u8 mdix)
{
	int rc;
	u16 reg_val;

	reg_val = phy_read(phydev, MSCC_PHY_BYPASS_CONTROL);
	if (mdix == ETH_TP_MDI || mdix == ETH_TP_MDI_X) {
		reg_val |= (DISABLE_PAIR_SWAP_CORR_MASK |
			    DISABLE_POLARITY_CORR_MASK  |
			    DISABLE_HP_AUTO_MDIX_MASK);
	} else {
		reg_val &= ~(DISABLE_PAIR_SWAP_CORR_MASK |
			     DISABLE_POLARITY_CORR_MASK  |
			     DISABLE_HP_AUTO_MDIX_MASK);
	}
	rc = phy_write(phydev, MSCC_PHY_BYPASS_CONTROL, reg_val);
	if (rc)
		return rc;

	reg_val = 0;

	if (mdix == ETH_TP_MDI)
		reg_val = FORCE_MDI_CROSSOVER_MDI;
	else if (mdix == ETH_TP_MDI_X)
		reg_val = FORCE_MDI_CROSSOVER_MDIX;

	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_EXTENDED,
			      MSCC_PHY_EXT_MODE_CNTL, FORCE_MDI_CROSSOVER_MASK,
			      reg_val);
	if (rc < 0)
		return rc;

	return genphy_restart_aneg(phydev);
}

static int vsc85xx_downshift_get(struct phy_device *phydev, u8 *count)
{
	int reg_val;

	reg_val = phy_read_paged(phydev, MSCC_PHY_PAGE_EXTENDED,
				 MSCC_PHY_ACTIPHY_CNTL);
	if (reg_val < 0)
		return reg_val;

	reg_val &= DOWNSHIFT_CNTL_MASK;
	if (!(reg_val & DOWNSHIFT_EN))
		*count = DOWNSHIFT_DEV_DISABLE;
	else
		*count = ((reg_val & ~DOWNSHIFT_EN) >> DOWNSHIFT_CNTL_POS) + 2;

	return 0;
}

static int vsc85xx_downshift_set(struct phy_device *phydev, u8 count)
{
	if (count == DOWNSHIFT_DEV_DEFAULT_COUNT) {
		/* Default downshift count 3 (i.e. Bit3:2 = 0b01) */
		count = ((1 << DOWNSHIFT_CNTL_POS) | DOWNSHIFT_EN);
	} else if (count > DOWNSHIFT_COUNT_MAX || count == 1) {
		phydev_err(phydev, "Downshift count should be 2,3,4 or 5\n");
		return -ERANGE;
	} else if (count) {
		/* Downshift count is either 2,3,4 or 5 */
		count = (((count - 2) << DOWNSHIFT_CNTL_POS) | DOWNSHIFT_EN);
	}

	return phy_modify_paged(phydev, MSCC_PHY_PAGE_EXTENDED,
				MSCC_PHY_ACTIPHY_CNTL, DOWNSHIFT_CNTL_MASK,
				count);
}

static int vsc85xx_wol_set(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	int rc;
	u16 reg_val;
	u8  i;
	u16 pwd[3] = {0, 0, 0};
	struct ethtool_wolinfo *wol_conf = wol;
	u8 *mac_addr = phydev->attached_dev->dev_addr;

	mutex_lock(&phydev->lock);
	rc = phy_select_page(phydev, MSCC_PHY_PAGE_EXTENDED_2);
	if (rc < 0) {
		rc = phy_restore_page(phydev, rc, rc);
		goto out_unlock;
	}

	if (wol->wolopts & WAKE_MAGIC) {
		/* Store the device address for the magic packet */
		for (i = 0; i < ARRAY_SIZE(pwd); i++)
			pwd[i] = mac_addr[5 - (i * 2 + 1)] << 8 |
				 mac_addr[5 - i * 2];
		__phy_write(phydev, MSCC_PHY_WOL_LOWER_MAC_ADDR, pwd[0]);
		__phy_write(phydev, MSCC_PHY_WOL_MID_MAC_ADDR, pwd[1]);
		__phy_write(phydev, MSCC_PHY_WOL_UPPER_MAC_ADDR, pwd[2]);
	} else {
		__phy_write(phydev, MSCC_PHY_WOL_LOWER_MAC_ADDR, 0);
		__phy_write(phydev, MSCC_PHY_WOL_MID_MAC_ADDR, 0);
		__phy_write(phydev, MSCC_PHY_WOL_UPPER_MAC_ADDR, 0);
	}

	if (wol_conf->wolopts & WAKE_MAGICSECURE) {
		for (i = 0; i < ARRAY_SIZE(pwd); i++)
			pwd[i] = wol_conf->sopass[5 - (i * 2 + 1)] << 8 |
				 wol_conf->sopass[5 - i * 2];
		__phy_write(phydev, MSCC_PHY_WOL_LOWER_PASSWD, pwd[0]);
		__phy_write(phydev, MSCC_PHY_WOL_MID_PASSWD, pwd[1]);
		__phy_write(phydev, MSCC_PHY_WOL_UPPER_PASSWD, pwd[2]);
	} else {
		__phy_write(phydev, MSCC_PHY_WOL_LOWER_PASSWD, 0);
		__phy_write(phydev, MSCC_PHY_WOL_MID_PASSWD, 0);
		__phy_write(phydev, MSCC_PHY_WOL_UPPER_PASSWD, 0);
	}

	reg_val = __phy_read(phydev, MSCC_PHY_WOL_MAC_CONTROL);
	if (wol_conf->wolopts & WAKE_MAGICSECURE)
		reg_val |= SECURE_ON_ENABLE;
	else
		reg_val &= ~SECURE_ON_ENABLE;
	__phy_write(phydev, MSCC_PHY_WOL_MAC_CONTROL, reg_val);

	rc = phy_restore_page(phydev, rc, rc > 0 ? 0 : rc);
	if (rc < 0)
		goto out_unlock;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Enable the WOL interrupt */
		reg_val = phy_read(phydev, MII_VSC85XX_INT_MASK);
		reg_val |= MII_VSC85XX_INT_MASK_WOL;
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK, reg_val);
		if (rc)
			goto out_unlock;
	} else {
		/* Disable the WOL interrupt */
		reg_val = phy_read(phydev, MII_VSC85XX_INT_MASK);
		reg_val &= (~MII_VSC85XX_INT_MASK_WOL);
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK, reg_val);
		if (rc)
			goto out_unlock;
	}
	/* Clear WOL iterrupt status */
	reg_val = phy_read(phydev, MII_VSC85XX_INT_STATUS);

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

static void vsc85xx_wol_get(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int rc;
	u16 reg_val;
	u8  i;
	u16 pwd[3] = {0, 0, 0};
	struct ethtool_wolinfo *wol_conf = wol;

	mutex_lock(&phydev->lock);
	rc = phy_select_page(phydev, MSCC_PHY_PAGE_EXTENDED_2);
	if (rc < 0)
		goto out_unlock;

	reg_val = __phy_read(phydev, MSCC_PHY_WOL_MAC_CONTROL);
	if (reg_val & SECURE_ON_ENABLE)
		wol_conf->wolopts |= WAKE_MAGICSECURE;
	if (wol_conf->wolopts & WAKE_MAGICSECURE) {
		pwd[0] = __phy_read(phydev, MSCC_PHY_WOL_LOWER_PASSWD);
		pwd[1] = __phy_read(phydev, MSCC_PHY_WOL_MID_PASSWD);
		pwd[2] = __phy_read(phydev, MSCC_PHY_WOL_UPPER_PASSWD);
		for (i = 0; i < ARRAY_SIZE(pwd); i++) {
			wol_conf->sopass[5 - i * 2] = pwd[i] & 0x00ff;
			wol_conf->sopass[5 - (i * 2 + 1)] = (pwd[i] & 0xff00)
							    >> 8;
		}
	}

out_unlock:
	phy_restore_page(phydev, rc, rc > 0 ? 0 : rc);
	mutex_unlock(&phydev->lock);
}

#ifdef CONFIG_OF_MDIO
static int vsc85xx_edge_rate_magic_get(struct phy_device *phydev)
{
	u32 vdd, sd;
	int i, j;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	u8 sd_array_size = ARRAY_SIZE(edge_table[0].slowdown);

	if (!of_node)
		return -ENODEV;

	if (of_property_read_u32(of_node, "vsc8531,vddmac", &vdd))
		vdd = MSCC_VDDMAC_3300;

	if (of_property_read_u32(of_node, "vsc8531,edge-slowdown", &sd))
		sd = 0;

	for (i = 0; i < ARRAY_SIZE(edge_table); i++)
		if (edge_table[i].vddmac == vdd)
			for (j = 0; j < sd_array_size; j++)
				if (edge_table[i].slowdown[j] == sd)
					return (sd_array_size - j - 1);

	return -EINVAL;
}

static int vsc85xx_dt_led_mode_get(struct phy_device *phydev,
				   char *led,
				   u32 default_mode)
{
	struct vsc8531_private *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	u32 led_mode;
	int err;

	if (!of_node)
		return -ENODEV;

	led_mode = default_mode;
	err = of_property_read_u32(of_node, led, &led_mode);
	if (!err && !(BIT(led_mode) & priv->supp_led_modes)) {
		phydev_err(phydev, "DT %s invalid\n", led);
		return -EINVAL;
	}

	return led_mode;
}

#else
static int vsc85xx_edge_rate_magic_get(struct phy_device *phydev)
{
	return 0;
}

static int vsc85xx_dt_led_mode_get(struct phy_device *phydev,
				   char *led,
				   u8 default_mode)
{
	return default_mode;
}
#endif /* CONFIG_OF_MDIO */

static int vsc85xx_dt_led_modes_get(struct phy_device *phydev,
				    u32 *default_mode)
{
	struct vsc8531_private *priv = phydev->priv;
	char led_dt_prop[28];
	int i, ret;

	for (i = 0; i < priv->nleds; i++) {
		ret = sprintf(led_dt_prop, "vsc8531,led-%d-mode", i);
		if (ret < 0)
			return ret;

		ret = vsc85xx_dt_led_mode_get(phydev, led_dt_prop,
					      default_mode[i]);
		if (ret < 0)
			return ret;
		priv->leds_mode[i] = ret;
	}

	return 0;
}

static int vsc85xx_edge_rate_cntl_set(struct phy_device *phydev, u8 edge_rate)
{
	int rc;

	mutex_lock(&phydev->lock);
	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_EXTENDED_2,
			      MSCC_PHY_WOL_MAC_CONTROL, EDGE_RATE_CNTL_MASK,
			      edge_rate << EDGE_RATE_CNTL_POS);
	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_mac_if_set(struct phy_device *phydev,
			      phy_interface_t interface)
{
	int rc;
	u16 reg_val;

	mutex_lock(&phydev->lock);
	reg_val = phy_read(phydev, MSCC_PHY_EXT_PHY_CNTL_1);
	reg_val &= ~(MAC_IF_SELECTION_MASK);
	switch (interface) {
	case PHY_INTERFACE_MODE_RGMII:
		reg_val |= (MAC_IF_SELECTION_RGMII << MAC_IF_SELECTION_POS);
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg_val |= (MAC_IF_SELECTION_RMII << MAC_IF_SELECTION_POS);
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		reg_val |= (MAC_IF_SELECTION_GMII << MAC_IF_SELECTION_POS);
		break;
	default:
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = phy_write(phydev, MSCC_PHY_EXT_PHY_CNTL_1, reg_val);
	if (rc)
		goto out_unlock;

	rc = genphy_soft_reset(phydev);

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_default_config(struct phy_device *phydev)
{
	int rc;
	u16 reg_val;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	mutex_lock(&phydev->lock);

	reg_val = RGMII_RX_CLK_DELAY_1_1_NS << RGMII_RX_CLK_DELAY_POS;

	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_EXTENDED_2,
			      MSCC_PHY_RGMII_CNTL, RGMII_RX_CLK_DELAY_MASK,
			      reg_val);

	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_get_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return vsc85xx_downshift_get(phydev, (u8 *)data);
	default:
		return -EINVAL;
	}
}

static int vsc85xx_set_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna,
			       const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return vsc85xx_downshift_set(phydev, *(u8 *)data);
	default:
		return -EINVAL;
	}
}

/* mdiobus lock should be locked when using this function */
static void vsc85xx_tr_write(struct phy_device *phydev, u16 addr, u32 val)
{
	__phy_write(phydev, MSCC_PHY_TR_MSB, val >> 16);
	__phy_write(phydev, MSCC_PHY_TR_LSB, val & GENMASK(15, 0));
	__phy_write(phydev, MSCC_PHY_TR_CNTL, TR_WRITE | TR_ADDR(addr));
}

static int vsc8531_pre_init_seq_set(struct phy_device *phydev)
{
	int rc;
	static const struct reg_val init_seq[] = {
		{0x0f90, 0x00688980},
		{0x0696, 0x00000003},
		{0x07fa, 0x0050100f},
		{0x1686, 0x00000004},
	};
	unsigned int i;
	int oldpage;

	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_STANDARD,
			      MSCC_PHY_EXT_CNTL_STATUS, SMI_BROADCAST_WR_EN,
			      SMI_BROADCAST_WR_EN);
	if (rc < 0)
		return rc;
	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_TEST,
			      MSCC_PHY_TEST_PAGE_24, 0, 0x0400);
	if (rc < 0)
		return rc;
	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_TEST,
			      MSCC_PHY_TEST_PAGE_5, 0x0a00, 0x0e00);
	if (rc < 0)
		return rc;
	rc = phy_modify_paged(phydev, MSCC_PHY_PAGE_TEST,
			      MSCC_PHY_TEST_PAGE_8, 0x8000, 0x8000);
	if (rc < 0)
		return rc;

	mutex_lock(&phydev->lock);
	oldpage = phy_select_page(phydev, MSCC_PHY_PAGE_TR);
	if (oldpage < 0)
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(init_seq); i++)
		vsc85xx_tr_write(phydev, init_seq[i].reg, init_seq[i].val);

out_unlock:
	oldpage = phy_restore_page(phydev, oldpage, oldpage);
	mutex_unlock(&phydev->lock);

	return oldpage;
}

static int vsc85xx_eee_init_seq_set(struct phy_device *phydev)
{
	static const struct reg_val init_eee[] = {
		{0x0f82, 0x0012b00a},
		{0x1686, 0x00000004},
		{0x168c, 0x00d2c46f},
		{0x17a2, 0x00000620},
		{0x16a0, 0x00eeffdd},
		{0x16a6, 0x00071448},
		{0x16a4, 0x0013132f},
		{0x16a8, 0x00000000},
		{0x0ffc, 0x00c0a028},
		{0x0fe8, 0x0091b06c},
		{0x0fea, 0x00041600},
		{0x0f80, 0x00000af4},
		{0x0fec, 0x00901809},
		{0x0fee, 0x0000a6a1},
		{0x0ffe, 0x00b01007},
		{0x16b0, 0x00eeff00},
		{0x16b2, 0x00007000},
		{0x16b4, 0x00000814},
	};
	unsigned int i;
	int oldpage;

	mutex_lock(&phydev->lock);
	oldpage = phy_select_page(phydev, MSCC_PHY_PAGE_TR);
	if (oldpage < 0)
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(init_eee); i++)
		vsc85xx_tr_write(phydev, init_eee[i].reg, init_eee[i].val);

out_unlock:
	oldpage = phy_restore_page(phydev, oldpage, oldpage);
	mutex_unlock(&phydev->lock);

	return oldpage;
}

/* phydev->bus->mdio_lock should be locked when using this function */
static int phy_base_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	struct vsc8531_private *priv = phydev->priv;

	if (unlikely(!mutex_is_locked(&phydev->mdio.bus->mdio_lock))) {
		dev_err(&phydev->mdio.dev, "MDIO bus lock not held!\n");
		dump_stack();
	}

	return __mdiobus_write(phydev->mdio.bus, priv->base_addr, regnum, val);
}

/* phydev->bus->mdio_lock should be locked when using this function */
static int phy_base_read(struct phy_device *phydev, u32 regnum)
{
	struct vsc8531_private *priv = phydev->priv;

	if (unlikely(!mutex_is_locked(&phydev->mdio.bus->mdio_lock))) {
		dev_err(&phydev->mdio.dev, "MDIO bus lock not held!\n");
		dump_stack();
	}

	return __mdiobus_read(phydev->mdio.bus, priv->base_addr, regnum);
}

/* bus->mdio_lock should be locked when using this function */
static void vsc8584_csr_write(struct phy_device *phydev, u16 addr, u32 val)
{
	phy_base_write(phydev, MSCC_PHY_TR_MSB, val >> 16);
	phy_base_write(phydev, MSCC_PHY_TR_LSB, val & GENMASK(15, 0));
	phy_base_write(phydev, MSCC_PHY_TR_CNTL, TR_WRITE | TR_ADDR(addr));
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_cmd(struct phy_device *phydev, u16 val)
{
	unsigned long deadline;
	u16 reg_val;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	phy_base_write(phydev, MSCC_PHY_PROC_CMD, PROC_CMD_NCOMPLETED | val);

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		reg_val = phy_base_read(phydev, MSCC_PHY_PROC_CMD);
	} while (time_before(jiffies, deadline) &&
		 (reg_val & PROC_CMD_NCOMPLETED) &&
		 !(reg_val & PROC_CMD_FAILED));

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	if (reg_val & PROC_CMD_FAILED)
		return -EIO;

	if (reg_val & PROC_CMD_NCOMPLETED)
		return -ETIMEDOUT;

	return 0;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_micro_deassert_reset(struct phy_device *phydev,
					bool patch_en)
{
	u32 enable, release;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	enable = RUN_FROM_INT_ROM | MICRO_CLK_EN | DW8051_CLK_EN;
	release = MICRO_NSOFT_RESET | RUN_FROM_INT_ROM | DW8051_CLK_EN |
		MICRO_CLK_EN;

	if (patch_en) {
		enable |= MICRO_PATCH_EN;
		release |= MICRO_PATCH_EN;

		/* Clear all patches */
		phy_base_write(phydev, MSCC_INT_MEM_CNTL, READ_RAM);
	}

	/* Enable 8051 Micro clock; CLEAR/SET patch present; disable PRAM clock
	 * override and addr. auto-incr; operate at 125 MHz
	 */
	phy_base_write(phydev, MSCC_DW8051_CNTL_STATUS, enable);
	/* Release 8051 Micro SW reset */
	phy_base_write(phydev, MSCC_DW8051_CNTL_STATUS, release);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	return 0;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_micro_assert_reset(struct phy_device *phydev)
{
	int ret;
	u16 reg;

	ret = vsc8584_cmd(phydev, PROC_CMD_NOP);
	if (ret)
		return ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	reg = phy_base_read(phydev, MSCC_INT_MEM_CNTL);
	reg &= ~EN_PATCH_RAM_TRAP_ADDR(4);
	phy_base_write(phydev, MSCC_INT_MEM_CNTL, reg);

	phy_base_write(phydev, MSCC_TRAP_ROM_ADDR(4), 0x005b);
	phy_base_write(phydev, MSCC_PATCH_RAM_ADDR(4), 0x005b);

	reg = phy_base_read(phydev, MSCC_INT_MEM_CNTL);
	reg |= EN_PATCH_RAM_TRAP_ADDR(4);
	phy_base_write(phydev, MSCC_INT_MEM_CNTL, reg);

	phy_base_write(phydev, MSCC_PHY_PROC_CMD, PROC_CMD_NOP);

	reg = phy_base_read(phydev, MSCC_DW8051_CNTL_STATUS);
	reg &= ~MICRO_NSOFT_RESET;
	phy_base_write(phydev, MSCC_DW8051_CNTL_STATUS, reg);

	phy_base_write(phydev, MSCC_PHY_PROC_CMD, PROC_CMD_MCB_ACCESS_MAC_CONF |
		       PROC_CMD_SGMII_PORT(0) | PROC_CMD_NO_MAC_CONF |
		       PROC_CMD_READ);

	reg = phy_base_read(phydev, MSCC_INT_MEM_CNTL);
	reg &= ~EN_PATCH_RAM_TRAP_ADDR(4);
	phy_base_write(phydev, MSCC_INT_MEM_CNTL, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	return 0;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_get_fw_crc(struct phy_device *phydev, u16 start, u16 size,
			      u16 *crc)
{
	int ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED);

	phy_base_write(phydev, MSCC_PHY_VERIPHY_CNTL_2, start);
	phy_base_write(phydev, MSCC_PHY_VERIPHY_CNTL_3, size);

	/* Start Micro command */
	ret = vsc8584_cmd(phydev, PROC_CMD_CRC16);
	if (ret)
		goto out;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED);

	*crc = phy_base_read(phydev, MSCC_PHY_VERIPHY_CNTL_2);

out:
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	return ret;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_patch_fw(struct phy_device *phydev,
			    const struct firmware *fw)
{
	int i, ret;

	ret = vsc8584_micro_assert_reset(phydev);
	if (ret) {
		dev_err(&phydev->mdio.dev,
			"%s: failed to assert reset of micro\n", __func__);
		return ret;
	}

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	/* Hold 8051 Micro in SW Reset, Enable auto incr address and patch clock
	 * Disable the 8051 Micro clock
	 */
	phy_base_write(phydev, MSCC_DW8051_CNTL_STATUS, RUN_FROM_INT_ROM |
		       AUTOINC_ADDR | PATCH_RAM_CLK | MICRO_CLK_EN |
		       MICRO_CLK_DIVIDE(2));
	phy_base_write(phydev, MSCC_INT_MEM_CNTL, READ_PRAM | INT_MEM_WRITE_EN |
		       INT_MEM_DATA(2));
	phy_base_write(phydev, MSCC_INT_MEM_ADDR, 0x0000);

	for (i = 0; i < fw->size; i++)
		phy_base_write(phydev, MSCC_INT_MEM_CNTL, READ_PRAM |
			       INT_MEM_WRITE_EN | fw->data[i]);

	/* Clear internal memory access */
	phy_base_write(phydev, MSCC_INT_MEM_CNTL, READ_RAM);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	return 0;
}

/* bus->mdio_lock should be locked when using this function */
static bool vsc8574_is_serdes_init(struct phy_device *phydev)
{
	u16 reg;
	bool ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	reg = phy_base_read(phydev, MSCC_TRAP_ROM_ADDR(1));
	if (reg != 0x3eb7) {
		ret = false;
		goto out;
	}

	reg = phy_base_read(phydev, MSCC_PATCH_RAM_ADDR(1));
	if (reg != 0x4012) {
		ret = false;
		goto out;
	}

	reg = phy_base_read(phydev, MSCC_INT_MEM_CNTL);
	if (reg != EN_PATCH_RAM_TRAP_ADDR(1)) {
		ret = false;
		goto out;
	}

	reg = phy_base_read(phydev, MSCC_DW8051_CNTL_STATUS);
	if ((MICRO_NSOFT_RESET | RUN_FROM_INT_ROM |  DW8051_CLK_EN |
	     MICRO_CLK_EN) != (reg & MSCC_DW8051_VLD_MASK)) {
		ret = false;
		goto out;
	}

	ret = true;
out:
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	return ret;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8574_config_pre_init(struct phy_device *phydev)
{
	static const struct reg_val pre_init1[] = {
		{0x0fae, 0x000401bd},
		{0x0fac, 0x000f000f},
		{0x17a0, 0x00a0f147},
		{0x0fe4, 0x00052f54},
		{0x1792, 0x0027303d},
		{0x07fe, 0x00000704},
		{0x0fe0, 0x00060150},
		{0x0f82, 0x0012b00a},
		{0x0f80, 0x00000d74},
		{0x02e0, 0x00000012},
		{0x03a2, 0x00050208},
		{0x03b2, 0x00009186},
		{0x0fb0, 0x000e3700},
		{0x1688, 0x00049f81},
		{0x0fd2, 0x0000ffff},
		{0x168a, 0x00039fa2},
		{0x1690, 0x0020640b},
		{0x0258, 0x00002220},
		{0x025a, 0x00002a20},
		{0x025c, 0x00003060},
		{0x025e, 0x00003fa0},
		{0x03a6, 0x0000e0f0},
		{0x0f92, 0x00001489},
		{0x16a2, 0x00007000},
		{0x16a6, 0x00071448},
		{0x16a0, 0x00eeffdd},
		{0x0fe8, 0x0091b06c},
		{0x0fea, 0x00041600},
		{0x16b0, 0x00eeff00},
		{0x16b2, 0x00007000},
		{0x16b4, 0x00000814},
		{0x0f90, 0x00688980},
		{0x03a4, 0x0000d8f0},
		{0x0fc0, 0x00000400},
		{0x07fa, 0x0050100f},
		{0x0796, 0x00000003},
		{0x07f8, 0x00c3ff98},
		{0x0fa4, 0x0018292a},
		{0x168c, 0x00d2c46f},
		{0x17a2, 0x00000620},
		{0x16a4, 0x0013132f},
		{0x16a8, 0x00000000},
		{0x0ffc, 0x00c0a028},
		{0x0fec, 0x00901c09},
		{0x0fee, 0x0004a6a1},
		{0x0ffe, 0x00b01807},
	};
	static const struct reg_val pre_init2[] = {
		{0x0486, 0x0008a518},
		{0x0488, 0x006dc696},
		{0x048a, 0x00000912},
		{0x048e, 0x00000db6},
		{0x049c, 0x00596596},
		{0x049e, 0x00000514},
		{0x04a2, 0x00410280},
		{0x04a4, 0x00000000},
		{0x04a6, 0x00000000},
		{0x04a8, 0x00000000},
		{0x04aa, 0x00000000},
		{0x04ae, 0x007df7dd},
		{0x04b0, 0x006d95d4},
		{0x04b2, 0x00492410},
	};
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw;
	unsigned int i;
	u16 crc, reg;
	bool serdes_init;
	int ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* all writes below are broadcasted to all PHYs in the same package */
	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg |= SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	phy_base_write(phydev, MII_VSC85XX_INT_MASK, 0);

	/* The below register writes are tweaking analog and electrical
	 * configuration that were determined through characterization by PHY
	 * engineers. These don't mean anything more than "these are the best
	 * values".
	 */
	phy_base_write(phydev, MSCC_PHY_EXT_PHY_CNTL_2, 0x0040);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_20, 0x4320);
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_24, 0x0c00);
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_9, 0x18ca);
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_5, 0x1b20);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg |= 0x8000;
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TR);

	for (i = 0; i < ARRAY_SIZE(pre_init1); i++)
		vsc8584_csr_write(phydev, pre_init1[i].reg, pre_init1[i].val);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED_2);

	phy_base_write(phydev, MSCC_PHY_CU_PMD_TX_CNTL, 0x028e);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TR);

	for (i = 0; i < ARRAY_SIZE(pre_init2); i++)
		vsc8584_csr_write(phydev, pre_init2[i].reg, pre_init2[i].val);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg &= ~0x8000;
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* end of write broadcasting */
	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg &= ~SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	ret = request_firmware(&fw, MSCC_VSC8574_REVB_INT8051_FW, dev);
	if (ret) {
		dev_err(dev, "failed to load firmware %s, ret: %d\n",
			MSCC_VSC8574_REVB_INT8051_FW, ret);
		return ret;
	}

	/* Add one byte to size for the one added by the patch_fw function */
	ret = vsc8584_get_fw_crc(phydev,
				 MSCC_VSC8574_REVB_INT8051_FW_START_ADDR,
				 fw->size + 1, &crc);
	if (ret)
		goto out;

	if (crc == MSCC_VSC8574_REVB_INT8051_FW_CRC) {
		serdes_init = vsc8574_is_serdes_init(phydev);

		if (!serdes_init) {
			ret = vsc8584_micro_assert_reset(phydev);
			if (ret) {
				dev_err(dev,
					"%s: failed to assert reset of micro\n",
					__func__);
				goto out;
			}
		}
	} else {
		dev_dbg(dev, "FW CRC is not the expected one, patching FW\n");

		serdes_init = false;

		if (vsc8584_patch_fw(phydev, fw))
			dev_warn(dev,
				 "failed to patch FW, expect non-optimal device\n");
	}

	if (!serdes_init) {
		phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
			       MSCC_PHY_PAGE_EXTENDED_GPIO);

		phy_base_write(phydev, MSCC_TRAP_ROM_ADDR(1), 0x3eb7);
		phy_base_write(phydev, MSCC_PATCH_RAM_ADDR(1), 0x4012);
		phy_base_write(phydev, MSCC_INT_MEM_CNTL,
			       EN_PATCH_RAM_TRAP_ADDR(1));

		vsc8584_micro_deassert_reset(phydev, false);

		/* Add one byte to size for the one added by the patch_fw
		 * function
		 */
		ret = vsc8584_get_fw_crc(phydev,
					 MSCC_VSC8574_REVB_INT8051_FW_START_ADDR,
					 fw->size + 1, &crc);
		if (ret)
			goto out;

		if (crc != MSCC_VSC8574_REVB_INT8051_FW_CRC)
			dev_warn(dev,
				 "FW CRC after patching is not the expected one, expect non-optimal device\n");
	}

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	ret = vsc8584_cmd(phydev, PROC_CMD_1588_DEFAULT_INIT |
			  PROC_CMD_PHY_INIT);

out:
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	release_firmware(fw);

	return ret;
}

/* bus->mdio_lock should be locked when using this function */
static int vsc8584_config_pre_init(struct phy_device *phydev)
{
	static const struct reg_val pre_init1[] = {
		{0x07fa, 0x0050100f},
		{0x1688, 0x00049f81},
		{0x0f90, 0x00688980},
		{0x03a4, 0x0000d8f0},
		{0x0fc0, 0x00000400},
		{0x0f82, 0x0012b002},
		{0x1686, 0x00000004},
		{0x168c, 0x00d2c46f},
		{0x17a2, 0x00000620},
		{0x16a0, 0x00eeffdd},
		{0x16a6, 0x00071448},
		{0x16a4, 0x0013132f},
		{0x16a8, 0x00000000},
		{0x0ffc, 0x00c0a028},
		{0x0fe8, 0x0091b06c},
		{0x0fea, 0x00041600},
		{0x0f80, 0x00fffaff},
		{0x0fec, 0x00901809},
		{0x0ffe, 0x00b01007},
		{0x16b0, 0x00eeff00},
		{0x16b2, 0x00007000},
		{0x16b4, 0x00000814},
	};
	static const struct reg_val pre_init2[] = {
		{0x0486, 0x0008a518},
		{0x0488, 0x006dc696},
		{0x048a, 0x00000912},
	};
	const struct firmware *fw;
	struct device *dev = &phydev->mdio.dev;
	unsigned int i;
	u16 crc, reg;
	int ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* all writes below are broadcasted to all PHYs in the same package */
	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg |= SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	phy_base_write(phydev, MII_VSC85XX_INT_MASK, 0);

	reg = phy_base_read(phydev,  MSCC_PHY_BYPASS_CONTROL);
	reg |= PARALLEL_DET_IGNORE_ADVERTISED;
	phy_base_write(phydev, MSCC_PHY_BYPASS_CONTROL, reg);

	/* The below register writes are tweaking analog and electrical
	 * configuration that were determined through characterization by PHY
	 * engineers. These don't mean anything more than "these are the best
	 * values".
	 */
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED_3);

	phy_base_write(phydev, MSCC_PHY_SERDES_TX_CRC_ERR_CNT, 0x2000);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_5, 0x1f20);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg |= 0x8000;
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TR);

	phy_base_write(phydev, MSCC_PHY_TR_CNTL, TR_WRITE | TR_ADDR(0x2fa4));

	reg = phy_base_read(phydev, MSCC_PHY_TR_MSB);
	reg &= ~0x007f;
	reg |= 0x0019;
	phy_base_write(phydev, MSCC_PHY_TR_MSB, reg);

	phy_base_write(phydev, MSCC_PHY_TR_CNTL, TR_WRITE | TR_ADDR(0x0fa4));

	for (i = 0; i < ARRAY_SIZE(pre_init1); i++)
		vsc8584_csr_write(phydev, pre_init1[i].reg, pre_init1[i].val);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED_2);

	phy_base_write(phydev, MSCC_PHY_CU_PMD_TX_CNTL, 0x028e);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TR);

	for (i = 0; i < ARRAY_SIZE(pre_init2); i++)
		vsc8584_csr_write(phydev, pre_init2[i].reg, pre_init2[i].val);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg &= ~0x8000;
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* end of write broadcasting */
	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg &= ~SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	ret = request_firmware(&fw, MSCC_VSC8584_REVB_INT8051_FW, dev);
	if (ret) {
		dev_err(dev, "failed to load firmware %s, ret: %d\n",
			MSCC_VSC8584_REVB_INT8051_FW, ret);
		return ret;
	}

	/* Add one byte to size for the one added by the patch_fw function */
	ret = vsc8584_get_fw_crc(phydev,
				 MSCC_VSC8584_REVB_INT8051_FW_START_ADDR,
				 fw->size + 1, &crc);
	if (ret)
		goto out;

	if (crc != MSCC_VSC8584_REVB_INT8051_FW_CRC) {
		dev_dbg(dev, "FW CRC is not the expected one, patching FW\n");
		if (vsc8584_patch_fw(phydev, fw))
			dev_warn(dev,
				 "failed to patch FW, expect non-optimal device\n");
	}

	vsc8584_micro_deassert_reset(phydev, false);

	/* Add one byte to size for the one added by the patch_fw function */
	ret = vsc8584_get_fw_crc(phydev,
				 MSCC_VSC8584_REVB_INT8051_FW_START_ADDR,
				 fw->size + 1, &crc);
	if (ret)
		goto out;

	if (crc != MSCC_VSC8584_REVB_INT8051_FW_CRC)
		dev_warn(dev,
			 "FW CRC after patching is not the expected one, expect non-optimal device\n");

	ret = vsc8584_micro_assert_reset(phydev);
	if (ret)
		goto out;

	vsc8584_micro_deassert_reset(phydev, true);

out:
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	release_firmware(fw);

	return ret;
}

#if IS_ENABLED(CONFIG_MACSEC)
static u32 vsc8584_macsec_phy_read(struct phy_device *phydev,
				   enum macsec_bank bank, u32 reg)
{
	u32 val, val_l = 0, val_h = 0;
	unsigned long deadline;
	int rc;

	rc = phy_select_page(phydev, MSCC_PHY_PAGE_MACSEC);
	if (rc < 0)
		goto failed;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_20,
		    MSCC_PHY_MACSEC_20_TARGET(bank >> 2));

	if (bank >> 2 == 0x1)
		/* non-MACsec access */
		bank &= 0x3;
	else
		bank = 0;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_19,
		    MSCC_PHY_MACSEC_19_CMD | MSCC_PHY_MACSEC_19_READ |
		    MSCC_PHY_MACSEC_19_REG_ADDR(reg) |
		    MSCC_PHY_MACSEC_19_TARGET(bank));

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		val = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_19);
	} while (time_before(jiffies, deadline) && !(val & MSCC_PHY_MACSEC_19_CMD));

	val_l = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_17);
	val_h = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_18);

failed:
	phy_restore_page(phydev, rc, rc);

	return (val_h << 16) | val_l;
}

static void vsc8584_macsec_phy_write(struct phy_device *phydev,
				     enum macsec_bank bank, u32 reg, u32 val)
{
	unsigned long deadline;
	int rc;

	rc = phy_select_page(phydev, MSCC_PHY_PAGE_MACSEC);
	if (rc < 0)
		goto failed;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_20,
		    MSCC_PHY_MACSEC_20_TARGET(bank >> 2));

	if ((bank >> 2 == 0x1) || (bank >> 2 == 0x3))
		bank &= 0x3;
	else
		/* MACsec access */
		bank = 0;

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_17, (u16)val);
	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_18, (u16)(val >> 16));

	__phy_write(phydev, MSCC_EXT_PAGE_MACSEC_19,
		    MSCC_PHY_MACSEC_19_CMD | MSCC_PHY_MACSEC_19_REG_ADDR(reg) |
		    MSCC_PHY_MACSEC_19_TARGET(bank));

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		val = __phy_read(phydev, MSCC_EXT_PAGE_MACSEC_19);
	} while (time_before(jiffies, deadline) && !(val & MSCC_PHY_MACSEC_19_CMD));

failed:
	phy_restore_page(phydev, rc, rc);
}

static void vsc8584_macsec_classification(struct phy_device *phydev,
					  enum macsec_bank bank)
{
	/* enable VLAN tag parsing */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_CP_TAG,
				 MSCC_MS_SAM_CP_TAG_PARSE_STAG |
				 MSCC_MS_SAM_CP_TAG_PARSE_QTAG |
				 MSCC_MS_SAM_CP_TAG_PARSE_QINQ);
}

static void vsc8584_macsec_flow_default_action(struct phy_device *phydev,
					       enum macsec_bank bank,
					       bool block)
{
	u32 port = (bank == MACSEC_INGR) ?
		    MSCC_MS_PORT_UNCONTROLLED : MSCC_MS_PORT_COMMON;
	u32 action = MSCC_MS_FLOW_BYPASS;

	if (block)
		action = MSCC_MS_FLOW_DROP;

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_NM_FLOW_NCP,
				 /* MACsec untagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_DEST_PORT(port) |
				 /* MACsec tagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_DEST_PORT(port) |
				 /* Bad tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_DEST_PORT(port) |
				 /* Kay tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_DEST_PORT(port));
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_NM_FLOW_CP,
				 /* MACsec untagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_UNTAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_UNTAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_UNTAGGED_DEST_PORT(port) |
				 /* MACsec tagged */
				 MSCC_MS_SAM_NM_FLOW_NCP_TAGGED_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_TAGGED_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_TAGGED_DEST_PORT(port) |
				 /* Bad tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_BADTAG_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_BADTAG_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_BADTAG_DEST_PORT(port) |
				 /* Kay tag */
				 MSCC_MS_SAM_NM_FLOW_NCP_KAY_FLOW_TYPE(action) |
				 MSCC_MS_SAM_NM_FLOW_CP_KAY_DROP_ACTION(MSCC_MS_ACTION_DROP) |
				 MSCC_MS_SAM_NM_FLOW_CP_KAY_DEST_PORT(port));
}

static void vsc8584_macsec_integrity_checks(struct phy_device *phydev,
					    enum macsec_bank bank)
{
	u32 val;

	if (bank != MACSEC_INGR)
		return;

	/* Set default rules to pass unmatched frames */
	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MS_PARAMS2_IG_CC_CONTROL);
	val |= MSCC_MS_PARAMS2_IG_CC_CONTROL_NON_MATCH_CTRL_ACT |
	       MSCC_MS_PARAMS2_IG_CC_CONTROL_NON_MATCH_ACT;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PARAMS2_IG_CC_CONTROL,
				 val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PARAMS2_IG_CP_TAG,
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_STAG |
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_QTAG |
				 MSCC_MS_PARAMS2_IG_CP_TAG_PARSE_QINQ);
}

static void vsc8584_macsec_block_init(struct phy_device *phydev,
				      enum macsec_bank bank)
{
	u32 val;
	int i;

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_SW_RST |
				 MSCC_MS_ENA_CFG_MACSEC_BYPASS_ENA);

	/* Set the MACsec block out of s/w reset and enable clocks */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_CLK_ENA);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_STATUS_CONTEXT_CTRL,
				 bank == MACSEC_INGR ? 0xe5880214 : 0xe5880218);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_MISC_CONTROL,
				 MSCC_MS_MISC_CONTROL_MC_LATENCY_FIX(bank == MACSEC_INGR ? 57 : 40) |
				 MSCC_MS_MISC_CONTROL_XFORM_REC_SIZE(bank == MACSEC_INGR ? 1 : 2));

	/* Clear the counters */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_COUNT_CONTROL);
	val |= MSCC_MS_COUNT_CONTROL_AUTO_CNTR_RESET;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_COUNT_CONTROL, val);

	/* Enable octet increment mode */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_PP_CTRL,
				 MSCC_MS_PP_CTRL_MACSEC_OCTET_INCR_MODE);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_BLOCK_CTX_UPDATE, 0x3);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_COUNT_CONTROL);
	val |= MSCC_MS_COUNT_CONTROL_RESET_ALL;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_COUNT_CONTROL, val);

	/* Set the MTU */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_NON_VLAN_MTU_CHECK,
				 MSCC_MS_NON_VLAN_MTU_CHECK_NV_MTU_COMPARE(32761) |
				 MSCC_MS_NON_VLAN_MTU_CHECK_NV_MTU_COMP_DROP);

	for (i = 0; i < 8; i++)
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_VLAN_MTU_CHECK(i),
					 MSCC_MS_VLAN_MTU_CHECK_MTU_COMPARE(32761) |
					 MSCC_MS_VLAN_MTU_CHECK_MTU_COMP_DROP);

	if (bank == MACSEC_EGR) {
		val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_INTR_CTRL_STATUS);
		val &= ~MSCC_MS_INTR_CTRL_STATUS_INTR_ENABLE_M;
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_INTR_CTRL_STATUS, val);

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_FC_CFG,
					 MSCC_MS_FC_CFG_FCBUF_ENA |
					 MSCC_MS_FC_CFG_LOW_THRESH(0x1) |
					 MSCC_MS_FC_CFG_HIGH_THRESH(0x4) |
					 MSCC_MS_FC_CFG_LOW_BYTES_VAL(0x4) |
					 MSCC_MS_FC_CFG_HIGH_BYTES_VAL(0x6));
	}

	vsc8584_macsec_classification(phydev, bank);
	vsc8584_macsec_flow_default_action(phydev, bank, false);
	vsc8584_macsec_integrity_checks(phydev, bank);

	/* Enable the MACsec block */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_ENA_CFG,
				 MSCC_MS_ENA_CFG_CLK_ENA |
				 MSCC_MS_ENA_CFG_MACSEC_ENA |
				 MSCC_MS_ENA_CFG_MACSEC_SPEED_MODE(0x5));
}

static void vsc8584_macsec_mac_init(struct phy_device *phydev,
				    enum macsec_bank bank)
{
	u32 val;
	int i;

	/* Clear host & line stats */
	for (i = 0; i < 36; i++)
		vsc8584_macsec_phy_write(phydev, bank, 0x1c + i, 0);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL);
	val &= ~MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_MODE_M;
	val |= MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_MODE(2) |
	       MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_PAUSE_VALUE(0xffff);
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL, val);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_2);
	val |= 0xffff;
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_TX_FRAME_CTRL_2, val);

	val = vsc8584_macsec_phy_read(phydev, bank,
				      MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL);
	if (bank == HOST_MAC)
		val |= MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_TIMER_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_FRAME_DROP_ENA;
	else
		val |= MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_REACT_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_FRAME_DROP_ENA |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_PAUSE_MODE |
		       MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL_EARLY_PAUSE_DETECT_ENA;
	vsc8584_macsec_phy_write(phydev, bank,
				 MSCC_MAC_PAUSE_CFG_RX_FRAME_CTRL, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_PKTINF_CFG,
				 MSCC_MAC_CFG_PKTINF_CFG_STRIP_FCS_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_INSERT_FCS_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_LPI_RELAY_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_STRIP_PREAMBLE_ENA |
				 MSCC_MAC_CFG_PKTINF_CFG_INSERT_PREAMBLE_ENA |
				 (bank == HOST_MAC ?
				  MSCC_MAC_CFG_PKTINF_CFG_ENABLE_TX_PADDING : 0));

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_MODE_CFG);
	val &= ~MSCC_MAC_CFG_MODE_CFG_DISABLE_DIC;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_MODE_CFG, val);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_MAXLEN_CFG);
	val &= ~MSCC_MAC_CFG_MAXLEN_CFG_MAX_LEN_M;
	val |= MSCC_MAC_CFG_MAXLEN_CFG_MAX_LEN(10240);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_MAXLEN_CFG, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_ADV_CHK_CFG,
				 MSCC_MAC_CFG_ADV_CHK_CFG_SFD_CHK_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_PRM_CHK_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_OOR_ERR_ENA |
				 MSCC_MAC_CFG_ADV_CHK_CFG_INR_ERR_ENA);

	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MAC_CFG_LFS_CFG);
	val &= ~MSCC_MAC_CFG_LFS_CFG_LFS_MODE_ENA;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_LFS_CFG, val);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MAC_CFG_ENA_CFG,
				 MSCC_MAC_CFG_ENA_CFG_RX_CLK_ENA |
				 MSCC_MAC_CFG_ENA_CFG_TX_CLK_ENA |
				 MSCC_MAC_CFG_ENA_CFG_RX_ENA |
				 MSCC_MAC_CFG_ENA_CFG_TX_ENA);
}

/* Must be called with mdio_lock taken */
static int vsc8584_macsec_init(struct phy_device *phydev)
{
	u32 val;

	vsc8584_macsec_block_init(phydev, MACSEC_INGR);
	vsc8584_macsec_block_init(phydev, MACSEC_EGR);
	vsc8584_macsec_mac_init(phydev, HOST_MAC);
	vsc8584_macsec_mac_init(phydev, LINE_MAC);

	vsc8584_macsec_phy_write(phydev, FC_BUFFER,
				 MSCC_FCBUF_FC_READ_THRESH_CFG,
				 MSCC_FCBUF_FC_READ_THRESH_CFG_TX_THRESH(4) |
				 MSCC_FCBUF_FC_READ_THRESH_CFG_RX_THRESH(5));

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER, MSCC_FCBUF_MODE_CFG);
	val |= MSCC_FCBUF_MODE_CFG_PAUSE_GEN_ENA |
	       MSCC_FCBUF_MODE_CFG_RX_PPM_RATE_ADAPT_ENA |
	       MSCC_FCBUF_MODE_CFG_TX_PPM_RATE_ADAPT_ENA;
	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_MODE_CFG, val);

	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG,
				 MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG_TX_THRESH(8) |
				 MSCC_FCBUF_PPM_RATE_ADAPT_THRESH_CFG_TX_OFFSET(9));

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER,
				      MSCC_FCBUF_TX_DATA_QUEUE_CFG);
	val &= ~(MSCC_FCBUF_TX_DATA_QUEUE_CFG_START_M |
		 MSCC_FCBUF_TX_DATA_QUEUE_CFG_END_M);
	val |= MSCC_FCBUF_TX_DATA_QUEUE_CFG_START(0) |
		MSCC_FCBUF_TX_DATA_QUEUE_CFG_END(5119);
	vsc8584_macsec_phy_write(phydev, FC_BUFFER,
				 MSCC_FCBUF_TX_DATA_QUEUE_CFG, val);

	val = vsc8584_macsec_phy_read(phydev, FC_BUFFER, MSCC_FCBUF_ENA_CFG);
	val |= MSCC_FCBUF_ENA_CFG_TX_ENA | MSCC_FCBUF_ENA_CFG_RX_ENA;
	vsc8584_macsec_phy_write(phydev, FC_BUFFER, MSCC_FCBUF_ENA_CFG, val);

	val = vsc8584_macsec_phy_read(phydev, IP_1588,
				      MSCC_PROC_0_IP_1588_TOP_CFG_STAT_MODE_CTL);
	val &= ~MSCC_PROC_0_IP_1588_TOP_CFG_STAT_MODE_CTL_PROTOCOL_MODE_M;
	val |= MSCC_PROC_0_IP_1588_TOP_CFG_STAT_MODE_CTL_PROTOCOL_MODE(4);
	vsc8584_macsec_phy_write(phydev, IP_1588,
				 MSCC_PROC_0_IP_1588_TOP_CFG_STAT_MODE_CTL, val);

	return 0;
}

static void vsc8584_macsec_flow(struct phy_device *phydev,
				struct macsec_flow *flow)
{
	struct vsc8531_private *priv = phydev->priv;
	enum macsec_bank bank = flow->bank;
	u32 val, match = 0, mask = 0, action = 0, idx = flow->index;

	if (flow->match.tagged)
		match |= MSCC_MS_SAM_MISC_MATCH_TAGGED;
	if (flow->match.untagged)
		match |= MSCC_MS_SAM_MISC_MATCH_UNTAGGED;

	if (bank == MACSEC_INGR && flow->assoc_num >= 0) {
		match |= MSCC_MS_SAM_MISC_MATCH_AN(flow->assoc_num);
		mask |= MSCC_MS_SAM_MASK_AN_MASK(0x3);
	}

	if (bank == MACSEC_INGR && flow->match.sci && flow->rx_sa->sc->sci) {
		match |= MSCC_MS_SAM_MISC_MATCH_TCI(BIT(3));
		mask |= MSCC_MS_SAM_MASK_TCI_MASK(BIT(3)) |
			MSCC_MS_SAM_MASK_SCI_MASK;

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MATCH_SCI_LO(idx),
					 lower_32_bits(flow->rx_sa->sc->sci));
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MATCH_SCI_HI(idx),
					 upper_32_bits(flow->rx_sa->sc->sci));
	}

	if (flow->match.etype) {
		mask |= MSCC_MS_SAM_MASK_MAC_ETYPE_MASK;

		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MAC_SA_MATCH_HI(idx),
					 MSCC_MS_SAM_MAC_SA_MATCH_HI_ETYPE(htons(flow->etype)));
	}

	match |= MSCC_MS_SAM_MISC_MATCH_PRIORITY(flow->priority);

	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MISC_MATCH(idx), match);
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_MASK(idx), mask);

	/* Action for matching packets */
	if (flow->action.drop)
		action = MSCC_MS_FLOW_DROP;
	else if (flow->action.bypass || flow->port == MSCC_MS_PORT_UNCONTROLLED)
		action = MSCC_MS_FLOW_BYPASS;
	else
		action = (bank == MACSEC_INGR) ?
			 MSCC_MS_FLOW_INGRESS : MSCC_MS_FLOW_EGRESS;

	val = MSCC_MS_SAM_FLOW_CTRL_FLOW_TYPE(action) |
	      MSCC_MS_SAM_FLOW_CTRL_DROP_ACTION(MSCC_MS_ACTION_DROP) |
	      MSCC_MS_SAM_FLOW_CTRL_DEST_PORT(flow->port);

	if (action == MSCC_MS_FLOW_BYPASS)
		goto write_ctrl;

	if (bank == MACSEC_INGR) {
		if (priv->secy->replay_protect)
			val |= MSCC_MS_SAM_FLOW_CTRL_REPLAY_PROTECT;
		if (priv->secy->validate_frames == MACSEC_VALIDATE_STRICT)
			val |= MSCC_MS_SAM_FLOW_CTRL_VALIDATE_FRAMES(MSCC_MS_VALIDATE_STRICT);
		else if (priv->secy->validate_frames == MACSEC_VALIDATE_CHECK)
			val |= MSCC_MS_SAM_FLOW_CTRL_VALIDATE_FRAMES(MSCC_MS_VALIDATE_CHECK);
	} else if (bank == MACSEC_EGR) {
		if (priv->secy->protect_frames)
			val |= MSCC_MS_SAM_FLOW_CTRL_PROTECT_FRAME;
		if (priv->secy->tx_sc.encrypt)
			val |= MSCC_MS_SAM_FLOW_CTRL_CONF_PROTECT;
		if (priv->secy->tx_sc.send_sci)
			val |= MSCC_MS_SAM_FLOW_CTRL_INCLUDE_SCI;
	}

write_ctrl:
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static struct macsec_flow *vsc8584_macsec_find_flow(struct macsec_context *ctx,
						    enum macsec_bank bank)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &priv->macsec_flows, list)
		if (pos->assoc_num == ctx->sa.assoc_num && pos->bank == bank)
			return pos;

	return ERR_PTR(-ENOENT);
}

static void vsc8584_macsec_flow_enable(struct phy_device *phydev,
				       struct macsec_flow *flow)
{
	enum macsec_bank bank = flow->bank;
	u32 val, idx = flow->index;

	if ((flow->bank == MACSEC_INGR && flow->rx_sa && !flow->rx_sa->active) ||
	    (flow->bank == MACSEC_EGR && flow->tx_sa && !flow->tx_sa->active))
		return;

	/* Enable */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_ENTRY_SET1, BIT(idx));

	/* Set in-use */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx));
	val |= MSCC_MS_SAM_FLOW_CTRL_SA_IN_USE;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static void vsc8584_macsec_flow_disable(struct phy_device *phydev,
					struct macsec_flow *flow)
{
	enum macsec_bank bank = flow->bank;
	u32 val, idx = flow->index;

	/* Disable */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_ENTRY_CLEAR1, BIT(idx));

	/* Clear in-use */
	val = vsc8584_macsec_phy_read(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx));
	val &= ~MSCC_MS_SAM_FLOW_CTRL_SA_IN_USE;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_SAM_FLOW_CTRL(idx), val);
}

static u32 vsc8584_macsec_flow_context_id(struct macsec_flow *flow)
{
	if (flow->bank == MACSEC_INGR)
		return flow->index + MSCC_MS_MAX_FLOWS;

	return flow->index;
}

/* Derive the AES key to get a key for the hash autentication */
static int vsc8584_macsec_derive_key(const u8 key[MACSEC_KEYID_LEN],
				     u16 key_len, u8 hkey[16])
{
	struct crypto_skcipher *tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	struct skcipher_request *req = NULL;
	struct scatterlist src, dst;
	DECLARE_CRYPTO_WAIT(wait);
	u32 input[4] = {0};
	int ret;

	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out;
	}

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP, crypto_req_done,
				      &wait);
	ret = crypto_skcipher_setkey(tfm, key, key_len);
	if (ret < 0)
		goto out;

	sg_init_one(&src, input, 16);
	sg_init_one(&dst, hkey, 16);
	skcipher_request_set_crypt(req, &src, &dst, 16, NULL);

	ret = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);

out:
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return ret;
}

static int vsc8584_macsec_transformation(struct phy_device *phydev,
					 struct macsec_flow *flow)
{
	struct vsc8531_private *priv = phydev->priv;
	enum macsec_bank bank = flow->bank;
	int i, ret, index = flow->index;
	u32 rec = 0, control = 0;
	u8 hkey[16];
	sci_t sci;

	ret = vsc8584_macsec_derive_key(flow->key, priv->secy->key_len, hkey);
	if (ret)
		return ret;

	switch (priv->secy->key_len) {
	case 16:
		control |= CONTROL_CRYPTO_ALG(CTRYPTO_ALG_AES_CTR_128);
		break;
	case 32:
		control |= CONTROL_CRYPTO_ALG(CTRYPTO_ALG_AES_CTR_256);
		break;
	default:
		return -EINVAL;
	}

	control |= (bank == MACSEC_EGR) ?
		   (CONTROL_TYPE_EGRESS | CONTROL_AN(priv->secy->tx_sc.encoding_sa)) :
		   (CONTROL_TYPE_INGRESS | CONTROL_SEQ_MASK);

	control |= CONTROL_UPDATE_SEQ | CONTROL_ENCRYPT_AUTH | CONTROL_KEY_IN_CTX |
		   CONTROL_IV0 | CONTROL_IV1 | CONTROL_IV_IN_SEQ |
		   CONTROL_DIGEST_TYPE(0x2) | CONTROL_SEQ_TYPE(0x1) |
		   CONTROL_AUTH_ALG(AUTH_ALG_AES_GHAS) | CONTROL_CONTEXT_ID;

	/* Set the control word */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 control);

	/* Set the context ID. Must be unique. */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 vsc8584_macsec_flow_context_id(flow));

	/* Set the encryption/decryption key */
	for (i = 0; i < priv->secy->key_len / sizeof(u32); i++)
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 ((u32 *)flow->key)[i]);

	/* Set the authentication key */
	for (i = 0; i < 4; i++)
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 ((u32 *)hkey)[i]);

	/* Initial sequence number */
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 bank == MACSEC_INGR ?
				 flow->rx_sa->next_pn : flow->tx_sa->next_pn);

	if (bank == MACSEC_INGR)
		/* Set the mask (replay window size) */
		vsc8584_macsec_phy_write(phydev, bank,
					 MSCC_MS_XFORM_REC(index, rec++),
					 priv->secy->replay_window);

	/* Set the input vectors */
	sci = bank == MACSEC_INGR ? flow->rx_sa->sc->sci : priv->secy->sci;
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 lower_32_bits(sci));
	vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
				 upper_32_bits(sci));

	while (rec < 20)
		vsc8584_macsec_phy_write(phydev, bank, MSCC_MS_XFORM_REC(index, rec++),
					 0);

	flow->has_transformation = true;
	return 0;
}

static struct macsec_flow *vsc8584_macsec_alloc_flow(struct vsc8531_private *priv,
						     enum macsec_bank bank)
{
	unsigned long *bitmap = bank == MACSEC_INGR ?
				&priv->ingr_flows : &priv->egr_flows;
	struct macsec_flow *flow;
	int index;

	index = find_first_zero_bit(bitmap, MSCC_MS_MAX_FLOWS);

	if (index == MSCC_MS_MAX_FLOWS)
		return ERR_PTR(-ENOMEM);

	flow = kzalloc(sizeof(*flow), GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	set_bit(index, bitmap);
	flow->index = index;
	flow->bank = bank;
	flow->priority = 8;
	flow->assoc_num = -1;

	list_add_tail(&flow->list, &priv->macsec_flows);
	return flow;
}

static void vsc8584_macsec_free_flow(struct vsc8531_private *priv,
				     struct macsec_flow *flow)
{
	unsigned long *bitmap = flow->bank == MACSEC_INGR ?
				&priv->ingr_flows : &priv->egr_flows;

	list_del(&flow->list);
	clear_bit(flow->index, bitmap);
	kfree(flow);
}

static int vsc8584_macsec_add_flow(struct phy_device *phydev,
				   struct macsec_flow *flow, bool update)
{
	int ret;

	flow->port = MSCC_MS_PORT_CONTROLLED;
	vsc8584_macsec_flow(phydev, flow);

	if (update)
		return 0;

	ret = vsc8584_macsec_transformation(phydev, flow);
	if (ret) {
		vsc8584_macsec_free_flow(phydev->priv, flow);
		return ret;
	}

	return 0;
}

static int vsc8584_macsec_default_flows(struct phy_device *phydev)
{
	struct macsec_flow *flow;

	/* Add a rule to let the MKA traffic go through, ingress */
	flow = vsc8584_macsec_alloc_flow(phydev->priv, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	flow->priority = 15;
	flow->port = MSCC_MS_PORT_UNCONTROLLED;
	flow->match.tagged = 1;
	flow->match.untagged = 1;
	flow->match.etype = 1;
	flow->etype = ETH_P_PAE;
	flow->action.bypass = 1;

	vsc8584_macsec_flow(phydev, flow);
	vsc8584_macsec_flow_enable(phydev, flow);

	/* Add a rule to let the MKA traffic go through, egress */
	flow = vsc8584_macsec_alloc_flow(phydev->priv, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	flow->priority = 15;
	flow->port = MSCC_MS_PORT_COMMON;
	flow->match.untagged = 1;
	flow->match.etype = 1;
	flow->etype = ETH_P_PAE;
	flow->action.bypass = 1;

	vsc8584_macsec_flow(phydev, flow);
	vsc8584_macsec_flow_enable(phydev, flow);

	return 0;
}

static void vsc8584_macsec_del_flow(struct phy_device *phydev,
				    struct macsec_flow *flow)
{
	vsc8584_macsec_flow_disable(phydev, flow);
	vsc8584_macsec_free_flow(phydev->priv, flow);
}

static int __vsc8584_macsec_add_rxsa(struct macsec_context *ctx,
				     struct macsec_flow *flow, bool update)
{
	struct phy_device *phydev = ctx->phydev;
	struct vsc8531_private *priv = phydev->priv;

	if (!flow) {
		flow = vsc8584_macsec_alloc_flow(priv, MACSEC_INGR);
		if (IS_ERR(flow))
			return PTR_ERR(flow);

		memcpy(flow->key, ctx->sa.key, priv->secy->key_len);
	}

	flow->assoc_num = ctx->sa.assoc_num;
	flow->rx_sa = ctx->sa.rx_sa;

	/* Always match tagged packets on ingress */
	flow->match.tagged = 1;
	flow->match.sci = 1;

	if (priv->secy->validate_frames != MACSEC_VALIDATE_DISABLED)
		flow->match.untagged = 1;

	return vsc8584_macsec_add_flow(phydev, flow, update);
}

static int __vsc8584_macsec_add_txsa(struct macsec_context *ctx,
				     struct macsec_flow *flow, bool update)
{
	struct phy_device *phydev = ctx->phydev;
	struct vsc8531_private *priv = phydev->priv;

	if (!flow) {
		flow = vsc8584_macsec_alloc_flow(priv, MACSEC_EGR);
		if (IS_ERR(flow))
			return PTR_ERR(flow);

		memcpy(flow->key, ctx->sa.key, priv->secy->key_len);
	}

	flow->assoc_num = ctx->sa.assoc_num;
	flow->tx_sa = ctx->sa.tx_sa;

	/* Always match untagged packets on egress */
	flow->match.untagged = 1;

	return vsc8584_macsec_add_flow(phydev, flow, update);
}

static int vsc8584_macsec_dev_open(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_flow_enable(ctx->phydev, flow);

	return 0;
}

static int vsc8584_macsec_dev_stop(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

	return 0;
}

static int vsc8584_macsec_add_secy(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_secy *secy = ctx->secy;

	if (ctx->prepare) {
		if (priv->secy)
			return -EEXIST;

		return 0;
	}

	priv->secy = secy;

	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_EGR,
					   secy->validate_frames != MACSEC_VALIDATE_DISABLED);
	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_INGR,
					   secy->validate_frames != MACSEC_VALIDATE_DISABLED);

	return vsc8584_macsec_default_flows(ctx->phydev);
}

static int vsc8584_macsec_del_secy(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list)
		vsc8584_macsec_del_flow(ctx->phydev, flow);

	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_EGR, false);
	vsc8584_macsec_flow_default_action(ctx->phydev, MACSEC_INGR, false);

	priv->secy = NULL;
	return 0;
}

static int vsc8584_macsec_upd_secy(struct macsec_context *ctx)
{
	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_secy(ctx);
	return vsc8584_macsec_add_secy(ctx);
}

static int vsc8584_macsec_add_rxsc(struct macsec_context *ctx)
{
	/* Nothing to do */
	return 0;
}

static int vsc8584_macsec_upd_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int vsc8584_macsec_del_rxsc(struct macsec_context *ctx)
{
	struct vsc8531_private *priv = ctx->phydev->priv;
	struct macsec_flow *flow, *tmp;

	/* No operation to perform before the commit step */
	if (ctx->prepare)
		return 0;

	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list) {
		if (flow->bank == MACSEC_INGR && flow->rx_sa &&
		    flow->rx_sa->sc->sci == ctx->rx_sc->sci)
			vsc8584_macsec_del_flow(ctx->phydev, flow);
	}

	return 0;
}

static int vsc8584_macsec_add_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow = NULL;

	if (ctx->prepare)
		return __vsc8584_macsec_add_rxsa(ctx, flow, false);

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_upd_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (ctx->prepare) {
		/* Make sure the flow is disabled before updating it */
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

		return __vsc8584_macsec_add_rxsa(ctx, flow, true);
	}

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_del_rxsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_INGR);

	if (IS_ERR(flow))
		return PTR_ERR(flow);
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_flow(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_add_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow = NULL;

	if (ctx->prepare)
		return __vsc8584_macsec_add_txsa(ctx, flow, false);

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_upd_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (ctx->prepare) {
		/* Make sure the flow is disabled before updating it */
		vsc8584_macsec_flow_disable(ctx->phydev, flow);

		return __vsc8584_macsec_add_txsa(ctx, flow, true);
	}

	vsc8584_macsec_flow_enable(ctx->phydev, flow);
	return 0;
}

static int vsc8584_macsec_del_txsa(struct macsec_context *ctx)
{
	struct macsec_flow *flow;

	flow = vsc8584_macsec_find_flow(ctx, MACSEC_EGR);

	if (IS_ERR(flow))
		return PTR_ERR(flow);
	if (ctx->prepare)
		return 0;

	vsc8584_macsec_del_flow(ctx->phydev, flow);
	return 0;
}

static struct macsec_ops vsc8584_macsec_ops = {
	.mdo_dev_open = vsc8584_macsec_dev_open,
	.mdo_dev_stop = vsc8584_macsec_dev_stop,
	.mdo_add_secy = vsc8584_macsec_add_secy,
	.mdo_upd_secy = vsc8584_macsec_upd_secy,
	.mdo_del_secy = vsc8584_macsec_del_secy,
	.mdo_add_rxsc = vsc8584_macsec_add_rxsc,
	.mdo_upd_rxsc = vsc8584_macsec_upd_rxsc,
	.mdo_del_rxsc = vsc8584_macsec_del_rxsc,
	.mdo_add_rxsa = vsc8584_macsec_add_rxsa,
	.mdo_upd_rxsa = vsc8584_macsec_upd_rxsa,
	.mdo_del_rxsa = vsc8584_macsec_del_rxsa,
	.mdo_add_txsa = vsc8584_macsec_add_txsa,
	.mdo_upd_txsa = vsc8584_macsec_upd_txsa,
	.mdo_del_txsa = vsc8584_macsec_del_txsa,
};
#endif /* CONFIG_MACSEC */

/* Check if one PHY has already done the init of the parts common to all PHYs
 * in the Quad PHY package.
 */
static bool vsc8584_is_pkg_init(struct phy_device *phydev, bool reversed)
{
	struct mdio_device **map = phydev->mdio.bus->mdio_map;
	struct vsc8531_private *vsc8531;
	struct phy_device *phy;
	int i, addr;

	/* VSC8584 is a Quad PHY */
	for (i = 0; i < 4; i++) {
		vsc8531 = phydev->priv;

		if (reversed)
			addr = vsc8531->base_addr - i;
		else
			addr = vsc8531->base_addr + i;

		if (!map[addr])
			continue;

		phy = container_of(map[addr], struct phy_device, mdio);

		if ((phy->phy_id & phydev->drv->phy_id_mask) !=
		    (phydev->drv->phy_id & phydev->drv->phy_id_mask))
			continue;

		vsc8531 = phy->priv;

		if (vsc8531 && vsc8531->pkg_init)
			return true;
	}

	return false;
}

static int vsc8584_config_init(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	u16 addr, val;
	int ret, i;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	mutex_lock(&phydev->mdio.bus->mdio_lock);

	__mdiobus_write(phydev->mdio.bus, phydev->mdio.addr,
			MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED);
	addr = __mdiobus_read(phydev->mdio.bus, phydev->mdio.addr,
			      MSCC_PHY_EXT_PHY_CNTL_4);
	addr >>= PHY_CNTL_4_ADDR_POS;

	val = __mdiobus_read(phydev->mdio.bus, phydev->mdio.addr,
			     MSCC_PHY_ACTIPHY_CNTL);
	if (val & PHY_ADDR_REVERSED)
		vsc8531->base_addr = phydev->mdio.addr + addr;
	else
		vsc8531->base_addr = phydev->mdio.addr - addr;

	/* Some parts of the init sequence are identical for every PHY in the
	 * package. Some parts are modifying the GPIO register bank which is a
	 * set of registers that are affecting all PHYs, a few resetting the
	 * microprocessor common to all PHYs. The CRC check responsible of the
	 * checking the firmware within the 8051 microprocessor can only be
	 * accessed via the PHY whose internal address in the package is 0.
	 * All PHYs' interrupts mask register has to be zeroed before enabling
	 * any PHY's interrupt in this register.
	 * For all these reasons, we need to do the init sequence once and only
	 * once whatever is the first PHY in the package that is initialized and
	 * do the correct init sequence for all PHYs that are package-critical
	 * in this pre-init function.
	 */
	if (!vsc8584_is_pkg_init(phydev, val & PHY_ADDR_REVERSED ? 1 : 0)) {
		/* The following switch statement assumes that the lowest
		 * nibble of the phy_id_mask is always 0. This works because
		 * the lowest nibble of the PHY_ID's below are also 0.
		 */
		WARN_ON(phydev->drv->phy_id_mask & 0xf);

		switch (phydev->phy_id & phydev->drv->phy_id_mask) {
		case PHY_ID_VSC8504:
		case PHY_ID_VSC8552:
		case PHY_ID_VSC8572:
		case PHY_ID_VSC8574:
			ret = vsc8574_config_pre_init(phydev);
			break;
		case PHY_ID_VSC856X:
		case PHY_ID_VSC8575:
		case PHY_ID_VSC8582:
		case PHY_ID_VSC8584:
			ret = vsc8584_config_pre_init(phydev);
			break;
		default:
			ret = -EINVAL;
			break;
		}

		if (ret)
			goto err;
	}

	vsc8531->pkg_init = true;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	val = phy_base_read(phydev, MSCC_PHY_MAC_CFG_FASTLINK);
	val &= ~MAC_CFG_MASK;
	if (phydev->interface == PHY_INTERFACE_MODE_QSGMII)
		val |= MAC_CFG_QSGMII;
	else
		val |= MAC_CFG_SGMII;

	ret = phy_base_write(phydev, MSCC_PHY_MAC_CFG_FASTLINK, val);
	if (ret)
		goto err;

	val = PROC_CMD_MCB_ACCESS_MAC_CONF | PROC_CMD_RST_CONF_PORT |
		PROC_CMD_READ_MOD_WRITE_PORT;
	if (phydev->interface == PHY_INTERFACE_MODE_QSGMII)
		val |= PROC_CMD_QSGMII_MAC;
	else
		val |= PROC_CMD_SGMII_MAC;

	ret = vsc8584_cmd(phydev, val);
	if (ret)
		goto err;

	usleep_range(10000, 20000);

	/* Disable SerDes for 100Base-FX */
	ret = vsc8584_cmd(phydev, PROC_CMD_FIBER_MEDIA_CONF |
			  PROC_CMD_FIBER_PORT(addr) | PROC_CMD_FIBER_DISABLE |
			  PROC_CMD_READ_MOD_WRITE_PORT |
			  PROC_CMD_RST_CONF_PORT | PROC_CMD_FIBER_100BASE_FX);
	if (ret)
		goto err;

	/* Disable SerDes for 1000Base-X */
	ret = vsc8584_cmd(phydev, PROC_CMD_FIBER_MEDIA_CONF |
			  PROC_CMD_FIBER_PORT(addr) | PROC_CMD_FIBER_DISABLE |
			  PROC_CMD_READ_MOD_WRITE_PORT |
			  PROC_CMD_RST_CONF_PORT | PROC_CMD_FIBER_1000BASE_X);
	if (ret)
		goto err;

	mutex_unlock(&phydev->mdio.bus->mdio_lock);

#if IS_ENABLED(CONFIG_MACSEC)
	/* MACsec */
	switch (phydev->phy_id & phydev->drv->phy_id_mask) {
	case PHY_ID_VSC856X:
	case PHY_ID_VSC8575:
	case PHY_ID_VSC8582:
	case PHY_ID_VSC8584:
		INIT_LIST_HEAD(&vsc8531->macsec_flows);
		vsc8531->secy = NULL;

		phydev->macsec_ops = &vsc8584_macsec_ops;

		ret = vsc8584_macsec_init(phydev);
		if (ret)
			goto err;
	}
#endif

	phy_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	val = phy_read(phydev, MSCC_PHY_EXT_PHY_CNTL_1);
	val &= ~(MEDIA_OP_MODE_MASK | VSC8584_MAC_IF_SELECTION_MASK);
	val |= MEDIA_OP_MODE_COPPER | (VSC8584_MAC_IF_SELECTION_SGMII <<
				       VSC8584_MAC_IF_SELECTION_POS);
	ret = phy_write(phydev, MSCC_PHY_EXT_PHY_CNTL_1, val);

	ret = genphy_soft_reset(phydev);
	if (ret)
		return ret;

	for (i = 0; i < vsc8531->nleds; i++) {
		ret = vsc85xx_led_cntl_set(phydev, i, vsc8531->leds_mode[i]);
		if (ret)
			return ret;
	}

	return 0;

err:
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
	return ret;
}

static int vsc8584_handle_interrupt(struct phy_device *phydev)
{
#if IS_ENABLED(CONFIG_MACSEC)
	struct vsc8531_private *priv = phydev->priv;
	struct macsec_flow *flow, *tmp;
	u32 cause, rec;

	/* Check MACsec PN rollover */
	cause = vsc8584_macsec_phy_read(phydev, MACSEC_EGR,
					MSCC_MS_INTR_CTRL_STATUS);
	cause &= MSCC_MS_INTR_CTRL_STATUS_INTR_CLR_STATUS_M;
	if (!(cause & MACSEC_INTR_CTRL_STATUS_ROLLOVER))
		goto skip_rollover;

	rec = 6 + priv->secy->key_len / sizeof(u32);
	list_for_each_entry_safe(flow, tmp, &priv->macsec_flows, list) {
		u32 val;

		if (flow->bank != MACSEC_EGR || !flow->has_transformation)
			continue;

		val = vsc8584_macsec_phy_read(phydev, MACSEC_EGR,
					      MSCC_MS_XFORM_REC(flow->index, rec));
		if (val == 0xffffffff) {
			vsc8584_macsec_flow_disable(phydev, flow);
			macsec_pn_wrapped(priv->secy, flow->tx_sa);
			break;
		}
	}

skip_rollover:
#endif

	phy_mac_interrupt(phydev);
	return 0;
}

static int vsc85xx_config_init(struct phy_device *phydev)
{
	int rc, i, phy_id;
	struct vsc8531_private *vsc8531 = phydev->priv;

	rc = vsc85xx_default_config(phydev);
	if (rc)
		return rc;

	rc = vsc85xx_mac_if_set(phydev, phydev->interface);
	if (rc)
		return rc;

	rc = vsc85xx_edge_rate_cntl_set(phydev, vsc8531->rate_magic);
	if (rc)
		return rc;

	phy_id = phydev->drv->phy_id & phydev->drv->phy_id_mask;
	if (PHY_ID_VSC8531 == phy_id || PHY_ID_VSC8541 == phy_id ||
	    PHY_ID_VSC8530 == phy_id || PHY_ID_VSC8540 == phy_id) {
		rc = vsc8531_pre_init_seq_set(phydev);
		if (rc)
			return rc;
	}

	rc = vsc85xx_eee_init_seq_set(phydev);
	if (rc)
		return rc;

	for (i = 0; i < vsc8531->nleds; i++) {
		rc = vsc85xx_led_cntl_set(phydev, i, vsc8531->leds_mode[i]);
		if (rc)
			return rc;
	}

	return 0;
}

static int vsc8584_did_interrupt(struct phy_device *phydev)
{
	int rc = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);

	return (rc < 0) ? 0 : rc & MII_VSC85XX_INT_MASK_MASK;
}

static int vsc8514_config_pre_init(struct phy_device *phydev)
{
	/* These are the settings to override the silicon default
	 * values to handle hardware performance of PHY. They
	 * are set at Power-On state and remain until PHY Reset.
	 */
	static const struct reg_val pre_init1[] = {
		{0x0f90, 0x00688980},
		{0x0786, 0x00000003},
		{0x07fa, 0x0050100f},
		{0x0f82, 0x0012b002},
		{0x1686, 0x00000004},
		{0x168c, 0x00d2c46f},
		{0x17a2, 0x00000620},
		{0x16a0, 0x00eeffdd},
		{0x16a6, 0x00071448},
		{0x16a4, 0x0013132f},
		{0x16a8, 0x00000000},
		{0x0ffc, 0x00c0a028},
		{0x0fe8, 0x0091b06c},
		{0x0fea, 0x00041600},
		{0x0f80, 0x00fffaff},
		{0x0fec, 0x00901809},
		{0x0ffe, 0x00b01007},
		{0x16b0, 0x00eeff00},
		{0x16b2, 0x00007000},
		{0x16b4, 0x00000814},
	};
	unsigned int i;
	u16 reg;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* all writes below are broadcasted to all PHYs in the same package */
	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg |= SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg |= BIT(15);
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TR);

	for (i = 0; i < ARRAY_SIZE(pre_init1); i++)
		vsc8584_csr_write(phydev, pre_init1[i].reg, pre_init1[i].val);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_TEST);

	reg = phy_base_read(phydev, MSCC_PHY_TEST_PAGE_8);
	reg &= ~BIT(15);
	phy_base_write(phydev, MSCC_PHY_TEST_PAGE_8, reg);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	reg = phy_base_read(phydev, MSCC_PHY_EXT_CNTL_STATUS);
	reg &= ~SMI_BROADCAST_WR_EN;
	phy_base_write(phydev, MSCC_PHY_EXT_CNTL_STATUS, reg);

	return 0;
}

static u32 vsc85xx_csr_ctrl_phy_read(struct phy_device *phydev,
				     u32 target, u32 reg)
{
	unsigned long deadline;
	u32 val, val_l, val_h;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_CSR_CNTL);

	/* CSR registers are grouped under different Target IDs.
	 * 6-bit Target_ID is split between MSCC_EXT_PAGE_CSR_CNTL_20 and
	 * MSCC_EXT_PAGE_CSR_CNTL_19 registers.
	 * Target_ID[5:2] maps to bits[3:0] of MSCC_EXT_PAGE_CSR_CNTL_20
	 * and Target_ID[1:0] maps to bits[13:12] of MSCC_EXT_PAGE_CSR_CNTL_19.
	 */

	/* Setup the Target ID */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_20,
		       MSCC_PHY_CSR_CNTL_20_TARGET(target >> 2));

	/* Trigger CSR Action - Read into the CSR's */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_19,
		       MSCC_PHY_CSR_CNTL_19_CMD | MSCC_PHY_CSR_CNTL_19_READ |
		       MSCC_PHY_CSR_CNTL_19_REG_ADDR(reg) |
		       MSCC_PHY_CSR_CNTL_19_TARGET(target & 0x3));

	/* Wait for register access*/
	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		val = phy_base_read(phydev, MSCC_EXT_PAGE_CSR_CNTL_19);
	} while (time_before(jiffies, deadline) &&
		!(val & MSCC_PHY_CSR_CNTL_19_CMD));

	if (!(val & MSCC_PHY_CSR_CNTL_19_CMD))
		return 0xffffffff;

	/* Read the Least Significant Word (LSW) (17) */
	val_l = phy_base_read(phydev, MSCC_EXT_PAGE_CSR_CNTL_17);

	/* Read the Most Significant Word (MSW) (18) */
	val_h = phy_base_read(phydev, MSCC_EXT_PAGE_CSR_CNTL_18);

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_STANDARD);

	return (val_h << 16) | val_l;
}

static int vsc85xx_csr_ctrl_phy_write(struct phy_device *phydev,
				      u32 target, u32 reg, u32 val)
{
	unsigned long deadline;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_CSR_CNTL);

	/* CSR registers are grouped under different Target IDs.
	 * 6-bit Target_ID is split between MSCC_EXT_PAGE_CSR_CNTL_20 and
	 * MSCC_EXT_PAGE_CSR_CNTL_19 registers.
	 * Target_ID[5:2] maps to bits[3:0] of MSCC_EXT_PAGE_CSR_CNTL_20
	 * and Target_ID[1:0] maps to bits[13:12] of MSCC_EXT_PAGE_CSR_CNTL_19.
	 */

	/* Setup the Target ID */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_20,
		       MSCC_PHY_CSR_CNTL_20_TARGET(target >> 2));

	/* Write the Least Significant Word (LSW) (17) */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_17, (u16)val);

	/* Write the Most Significant Word (MSW) (18) */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_18, (u16)(val >> 16));

	/* Trigger CSR Action - Write into the CSR's */
	phy_base_write(phydev, MSCC_EXT_PAGE_CSR_CNTL_19,
		       MSCC_PHY_CSR_CNTL_19_CMD |
		       MSCC_PHY_CSR_CNTL_19_REG_ADDR(reg) |
		       MSCC_PHY_CSR_CNTL_19_TARGET(target & 0x3));

	/* Wait for register access */
	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		val = phy_base_read(phydev, MSCC_EXT_PAGE_CSR_CNTL_19);
	} while (time_before(jiffies, deadline) &&
		 !(val & MSCC_PHY_CSR_CNTL_19_CMD));

	if (!(val & MSCC_PHY_CSR_CNTL_19_CMD))
		return -ETIMEDOUT;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_STANDARD);

	return 0;
}

static int __phy_write_mcb_s6g(struct phy_device *phydev, u32 reg, u8 mcb,
			       u32 op)
{
	unsigned long deadline;
	u32 val;
	int ret;

	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET, reg,
					 op | (1 << mcb));
	if (ret)
		return -EINVAL;

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		val = vsc85xx_csr_ctrl_phy_read(phydev, PHY_MCB_TARGET, reg);

		if (val == 0xffffffff)
			return -EIO;

	} while (time_before(jiffies, deadline) && (val & op));

	if (val & op)
		return -ETIMEDOUT;

	return 0;
}

/* Trigger a read to the spcified MCB */
static int phy_update_mcb_s6g(struct phy_device *phydev, u32 reg, u8 mcb)
{
	return __phy_write_mcb_s6g(phydev, reg, mcb, PHY_MCB_S6G_READ);
}

/* Trigger a write to the spcified MCB */
static int phy_commit_mcb_s6g(struct phy_device *phydev, u32 reg, u8 mcb)
{
	return __phy_write_mcb_s6g(phydev, reg, mcb, PHY_MCB_S6G_WRITE);
}

static int vsc8514_config_init(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	unsigned long deadline;
	u16 val, addr;
	int ret, i;
	u32 reg;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	mutex_lock(&phydev->mdio.bus->mdio_lock);

	__phy_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED);

	addr = __phy_read(phydev, MSCC_PHY_EXT_PHY_CNTL_4);
	addr >>= PHY_CNTL_4_ADDR_POS;

	val = __phy_read(phydev, MSCC_PHY_ACTIPHY_CNTL);

	if (val & PHY_ADDR_REVERSED)
		vsc8531->base_addr = phydev->mdio.addr + addr;
	else
		vsc8531->base_addr = phydev->mdio.addr - addr;

	/* Some parts of the init sequence are identical for every PHY in the
	 * package. Some parts are modifying the GPIO register bank which is a
	 * set of registers that are affecting all PHYs, a few resetting the
	 * microprocessor common to all PHYs.
	 * All PHYs' interrupts mask register has to be zeroed before enabling
	 * any PHY's interrupt in this register.
	 * For all these reasons, we need to do the init sequence once and only
	 * once whatever is the first PHY in the package that is initialized and
	 * do the correct init sequence for all PHYs that are package-critical
	 * in this pre-init function.
	 */
	if (!vsc8584_is_pkg_init(phydev, val & PHY_ADDR_REVERSED ? 1 : 0))
		vsc8514_config_pre_init(phydev);

	vsc8531->pkg_init = true;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
		       MSCC_PHY_PAGE_EXTENDED_GPIO);

	val = phy_base_read(phydev, MSCC_PHY_MAC_CFG_FASTLINK);

	val &= ~MAC_CFG_MASK;
	val |= MAC_CFG_QSGMII;
	ret = phy_base_write(phydev, MSCC_PHY_MAC_CFG_FASTLINK, val);

	if (ret)
		goto err;

	ret = vsc8584_cmd(phydev,
			  PROC_CMD_MCB_ACCESS_MAC_CONF |
			  PROC_CMD_RST_CONF_PORT |
			  PROC_CMD_READ_MOD_WRITE_PORT | PROC_CMD_QSGMII_MAC);
	if (ret)
		goto err;

	/* 6g mcb */
	phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	/* lcpll mcb */
	phy_update_mcb_s6g(phydev, PHY_S6G_LCPLL_CFG, 0);
	/* pll5gcfg0 */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_PLL5G_CFG0, 0x7036f145);
	if (ret)
		goto err;

	phy_commit_mcb_s6g(phydev, PHY_S6G_LCPLL_CFG, 0);
	/* pllcfg */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_PLL_CFG,
					 (3 << PHY_S6G_PLL_ENA_OFFS_POS) |
					 (120 << PHY_S6G_PLL_FSM_CTRL_DATA_POS)
					 | (0 << PHY_S6G_PLL_FSM_ENA_POS));
	if (ret)
		goto err;

	/* commoncfg */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_COMMON_CFG,
					 (0 << PHY_S6G_SYS_RST_POS) |
					 (0 << PHY_S6G_ENA_LANE_POS) |
					 (0 << PHY_S6G_ENA_LOOP_POS) |
					 (0 << PHY_S6G_QRATE_POS) |
					 (3 << PHY_S6G_IF_MODE_POS));
	if (ret)
		goto err;

	/* misccfg */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_MISC_CFG, 1);
	if (ret)
		goto err;

	/* gpcfg */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_GPC_CFG, 768);
	if (ret)
		goto err;

	phy_commit_mcb_s6g(phydev, PHY_S6G_DFT_CFG2, 0);

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG,
				   0); /* read 6G MCB into CSRs */
		reg = vsc85xx_csr_ctrl_phy_read(phydev, PHY_MCB_TARGET,
						PHY_S6G_PLL_STATUS);
		if (reg == 0xffffffff) {
			mutex_unlock(&phydev->mdio.bus->mdio_lock);
			return -EIO;
		}

	} while (time_before(jiffies, deadline) && (reg & BIT(12)));

	if (reg & BIT(12)) {
		mutex_unlock(&phydev->mdio.bus->mdio_lock);
		return -ETIMEDOUT;
	}

	/* misccfg */
	ret = vsc85xx_csr_ctrl_phy_write(phydev, PHY_MCB_TARGET,
					 PHY_S6G_MISC_CFG, 0);
	if (ret)
		goto err;

	phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG,
				   0); /* read 6G MCB into CSRs */
		reg = vsc85xx_csr_ctrl_phy_read(phydev, PHY_MCB_TARGET,
						PHY_S6G_IB_STATUS0);
		if (reg == 0xffffffff) {
			mutex_unlock(&phydev->mdio.bus->mdio_lock);
			return -EIO;
		}

	} while (time_before(jiffies, deadline) && !(reg & BIT(8)));

	if (!(reg & BIT(8))) {
		mutex_unlock(&phydev->mdio.bus->mdio_lock);
		return -ETIMEDOUT;
	}

	mutex_unlock(&phydev->mdio.bus->mdio_lock);

	ret = phy_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	if (ret)
		return ret;

	ret = phy_modify(phydev, MSCC_PHY_EXT_PHY_CNTL_1, MEDIA_OP_MODE_MASK,
			 MEDIA_OP_MODE_COPPER);

	if (ret)
		return ret;

	ret = genphy_soft_reset(phydev);

	if (ret)
		return ret;

	for (i = 0; i < vsc8531->nleds; i++) {
		ret = vsc85xx_led_cntl_set(phydev, i, vsc8531->leds_mode[i]);
		if (ret)
			return ret;
	}

	return ret;

err:
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
	return ret;
}

static int vsc85xx_ack_interrupt(struct phy_device *phydev)
{
	int rc = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);

	return (rc < 0) ? rc : 0;
}

static int vsc85xx_config_intr(struct phy_device *phydev)
{
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
#if IS_ENABLED(CONFIG_MACSEC)
		phy_write(phydev, MSCC_EXT_PAGE_ACCESS,
			  MSCC_PHY_PAGE_EXTENDED_2);
		phy_write(phydev, MSCC_PHY_EXTENDED_INT,
			  MSCC_PHY_EXTENDED_INT_MS_EGR);
		phy_write(phydev, MSCC_EXT_PAGE_ACCESS,
			  MSCC_PHY_PAGE_STANDARD);

		vsc8584_macsec_phy_write(phydev, MACSEC_EGR,
					 MSCC_MS_AIC_CTRL, 0xf);
		vsc8584_macsec_phy_write(phydev, MACSEC_EGR,
			MSCC_MS_INTR_CTRL_STATUS,
			MSCC_MS_INTR_CTRL_STATUS_INTR_ENABLE(MACSEC_INTR_CTRL_STATUS_ROLLOVER));
#endif
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK,
			       MII_VSC85XX_INT_MASK_MASK);
	} else {
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK, 0);
		if (rc < 0)
			return rc;
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);
	}

	return rc;
}

static int vsc85xx_config_aneg(struct phy_device *phydev)
{
	int rc;

	rc = vsc85xx_mdix_set(phydev, phydev->mdix_ctrl);
	if (rc < 0)
		return rc;

	return genphy_config_aneg(phydev);
}

static int vsc85xx_read_status(struct phy_device *phydev)
{
	int rc;

	rc = vsc85xx_mdix_get(phydev, &phydev->mdix);
	if (rc < 0)
		return rc;

	return genphy_read_status(phydev);
}

static int vsc8514_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531;
	u32 default_mode[4] = {VSC8531_LINK_1000_ACTIVITY,
	   VSC8531_LINK_100_ACTIVITY, VSC8531_LINK_ACTIVITY,
	   VSC8531_DUPLEX_COLLISION};

	vsc8531 = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531), GFP_KERNEL);
	if (!vsc8531)
		return -ENOMEM;

	phydev->priv = vsc8531;

	vsc8531->nleds = 4;
	vsc8531->supp_led_modes = VSC85XX_SUPP_LED_MODES;
	vsc8531->hw_stats = vsc85xx_hw_stats;
	vsc8531->nstats = ARRAY_SIZE(vsc85xx_hw_stats);
	vsc8531->stats = devm_kcalloc(&phydev->mdio.dev, vsc8531->nstats,
				      sizeof(u64), GFP_KERNEL);
	if (!vsc8531->stats)
		return -ENOMEM;

	return vsc85xx_dt_led_modes_get(phydev, default_mode);
}

static int vsc8574_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531;
	u32 default_mode[4] = {VSC8531_LINK_1000_ACTIVITY,
	   VSC8531_LINK_100_ACTIVITY, VSC8531_LINK_ACTIVITY,
	   VSC8531_DUPLEX_COLLISION};

	vsc8531 = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531), GFP_KERNEL);
	if (!vsc8531)
		return -ENOMEM;

	phydev->priv = vsc8531;

	vsc8531->nleds = 4;
	vsc8531->supp_led_modes = VSC8584_SUPP_LED_MODES;
	vsc8531->hw_stats = vsc8584_hw_stats;
	vsc8531->nstats = ARRAY_SIZE(vsc8584_hw_stats);
	vsc8531->stats = devm_kcalloc(&phydev->mdio.dev, vsc8531->nstats,
				      sizeof(u64), GFP_KERNEL);
	if (!vsc8531->stats)
		return -ENOMEM;

	return vsc85xx_dt_led_modes_get(phydev, default_mode);
}

static int vsc8584_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531;
	u32 default_mode[4] = {VSC8531_LINK_1000_ACTIVITY,
	   VSC8531_LINK_100_ACTIVITY, VSC8531_LINK_ACTIVITY,
	   VSC8531_DUPLEX_COLLISION};

	if ((phydev->phy_id & MSCC_DEV_REV_MASK) != VSC8584_REVB) {
		dev_err(&phydev->mdio.dev, "Only VSC8584 revB is supported.\n");
		return -ENOTSUPP;
	}

	vsc8531 = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531), GFP_KERNEL);
	if (!vsc8531)
		return -ENOMEM;

	phydev->priv = vsc8531;

	vsc8531->nleds = 4;
	vsc8531->supp_led_modes = VSC8584_SUPP_LED_MODES;
	vsc8531->hw_stats = vsc8584_hw_stats;
	vsc8531->nstats = ARRAY_SIZE(vsc8584_hw_stats);
	vsc8531->stats = devm_kcalloc(&phydev->mdio.dev, vsc8531->nstats,
				      sizeof(u64), GFP_KERNEL);
	if (!vsc8531->stats)
		return -ENOMEM;

	return vsc85xx_dt_led_modes_get(phydev, default_mode);
}

static int vsc85xx_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531;
	int rate_magic;
	u32 default_mode[2] = {VSC8531_LINK_1000_ACTIVITY,
	   VSC8531_LINK_100_ACTIVITY};

	rate_magic = vsc85xx_edge_rate_magic_get(phydev);
	if (rate_magic < 0)
		return rate_magic;

	vsc8531 = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531), GFP_KERNEL);
	if (!vsc8531)
		return -ENOMEM;

	phydev->priv = vsc8531;

	vsc8531->rate_magic = rate_magic;
	vsc8531->nleds = 2;
	vsc8531->supp_led_modes = VSC85XX_SUPP_LED_MODES;
	vsc8531->hw_stats = vsc85xx_hw_stats;
	vsc8531->nstats = ARRAY_SIZE(vsc85xx_hw_stats);
	vsc8531->stats = devm_kcalloc(&phydev->mdio.dev, vsc8531->nstats,
				      sizeof(u64), GFP_KERNEL);
	if (!vsc8531->stats)
		return -ENOMEM;

	return vsc85xx_dt_led_modes_get(phydev, default_mode);
}

/* Microsemi VSC85xx PHYs */
static struct phy_driver vsc85xx_driver[] = {
{
	.phy_id		= PHY_ID_VSC8504,
	.name		= "Microsemi GE VSC8504 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8574_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8514,
	.name		= "Microsemi GE VSC8514 SyncE",
	.phy_id_mask	= 0xfffffff0,
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8514_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8514_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page      = &vsc85xx_phy_read_page,
	.write_page     = &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8530,
	.name		= "Microsemi FE VSC8530",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_BASIC_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init	= &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt	= &vsc85xx_ack_interrupt,
	.config_intr	= &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc85xx_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8531,
	.name		= "Microsemi VSC8531",
	.phy_id_mask    = 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc85xx_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8540,
	.name		= "Microsemi FE VSC8540 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_BASIC_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init	= &vsc85xx_config_init,
	.config_aneg	= &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt	= &vsc85xx_ack_interrupt,
	.config_intr	= &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc85xx_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8541,
	.name		= "Microsemi VSC8541 SyncE",
	.phy_id_mask    = 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc85xx_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8552,
	.name		= "Microsemi GE VSC8552 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8574_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC856X,
	.name		= "Microsemi GE VSC856X SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8584_probe,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8572,
	.name		= "Microsemi GE VSC8572 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.handle_interrupt = &vsc8584_handle_interrupt,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8574_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8574,
	.name		= "Microsemi GE VSC8574 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8574_probe,
	.set_wol	= &vsc85xx_wol_set,
	.get_wol	= &vsc85xx_wol_get,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8575,
	.name		= "Microsemi GE VSC8575 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.handle_interrupt = &vsc8584_handle_interrupt,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8584_probe,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8582,
	.name		= "Microsemi GE VSC8582 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.handle_interrupt = &vsc8584_handle_interrupt,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8584_probe,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
},
{
	.phy_id		= PHY_ID_VSC8584,
	.name		= "Microsemi GE VSC8584 SyncE",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_GBIT_FEATURES */
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc8584_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status	= &vsc85xx_read_status,
	.handle_interrupt = &vsc8584_handle_interrupt,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.did_interrupt  = &vsc8584_did_interrupt,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe		= &vsc8584_probe,
	.get_tunable	= &vsc85xx_get_tunable,
	.set_tunable	= &vsc85xx_set_tunable,
	.read_page	= &vsc85xx_phy_read_page,
	.write_page	= &vsc85xx_phy_write_page,
	.get_sset_count = &vsc85xx_get_sset_count,
	.get_strings    = &vsc85xx_get_strings,
	.get_stats      = &vsc85xx_get_stats,
}

};

module_phy_driver(vsc85xx_driver);

static struct mdio_device_id __maybe_unused vsc85xx_tbl[] = {
	{ PHY_ID_VSC8504, 0xfffffff0, },
	{ PHY_ID_VSC8514, 0xfffffff0, },
	{ PHY_ID_VSC8530, 0xfffffff0, },
	{ PHY_ID_VSC8531, 0xfffffff0, },
	{ PHY_ID_VSC8540, 0xfffffff0, },
	{ PHY_ID_VSC8541, 0xfffffff0, },
	{ PHY_ID_VSC8552, 0xfffffff0, },
	{ PHY_ID_VSC856X, 0xfffffff0, },
	{ PHY_ID_VSC8572, 0xfffffff0, },
	{ PHY_ID_VSC8574, 0xfffffff0, },
	{ PHY_ID_VSC8575, 0xfffffff0, },
	{ PHY_ID_VSC8582, 0xfffffff0, },
	{ PHY_ID_VSC8584, 0xfffffff0, },
	{ }
};

MODULE_DEVICE_TABLE(mdio, vsc85xx_tbl);

MODULE_DESCRIPTION("Microsemi VSC85xx PHY driver");
MODULE_AUTHOR("Nagaraju Lakkaraju");
MODULE_LICENSE("Dual MIT/GPL");
