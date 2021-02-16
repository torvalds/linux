/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Driver for Microsemi VSC85xx PHYs
 *
 * Copyright (c) 2016 Microsemi Corporation
 */

#ifndef _MSCC_PHY_H_
#define _MSCC_PHY_H_

#if IS_ENABLED(CONFIG_MACSEC)
#include "mscc_macsec.h"
#endif

enum rgmii_clock_delay {
	RGMII_CLK_DELAY_0_2_NS = 0,
	RGMII_CLK_DELAY_0_8_NS = 1,
	RGMII_CLK_DELAY_1_1_NS = 2,
	RGMII_CLK_DELAY_1_7_NS = 3,
	RGMII_CLK_DELAY_2_0_NS = 4,
	RGMII_CLK_DELAY_2_3_NS = 5,
	RGMII_CLK_DELAY_2_6_NS = 6,
	RGMII_CLK_DELAY_3_4_NS = 7
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
#define MII_VSC85XX_INT_MASK_MDINT	  BIT(15)
#define MII_VSC85XX_INT_MASK_LINK_CHG	  BIT(13)
#define MII_VSC85XX_INT_MASK_WOL	  BIT(6)
#define MII_VSC85XX_INT_MASK_EXT	  BIT(5)
#define MII_VSC85XX_INT_STATUS		  26

#define MII_VSC85XX_INT_MASK_MASK	  (MII_VSC85XX_INT_MASK_MDINT    | \
					   MII_VSC85XX_INT_MASK_LINK_CHG | \
					   MII_VSC85XX_INT_MASK_EXT)

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
#define PHY_S6G_PLL5G_CFG2		  0x08
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

#define PHY_S6G_CFG2_FSM_DIS              1
#define PHY_S6G_CFG2_FSM_CLK_BP          23

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
#define MSCC_PHY_PAGE_1588		  0x1588 /* PTP (1588) */
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

/* RGMII setting controls at address 18E2, for VSC8572 and similar */
#define VSC8572_RGMII_CNTL		  18
#define VSC8572_RGMII_RX_DELAY_MASK	  0x000E
#define VSC8572_RGMII_TX_DELAY_MASK	  0x0070

/* RGMII controls at address 20E2, for VSC8502 and similar */
#define VSC8502_RGMII_CNTL		  20
#define VSC8502_RGMII_RX_DELAY_MASK	  0x0070
#define VSC8502_RGMII_TX_DELAY_MASK	  0x0007

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
#define MAC_CFG_RGMII			  0x8000

/* Test page Registers */
#define MSCC_PHY_TEST_PAGE_5		  5
#define MSCC_PHY_TEST_PAGE_8		  8
#define TR_CLK_DISABLE			  0x8000
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
#define PHY_ID_VSC8502			  0x00070630
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

struct vsc8531_private {
	int rate_magic;
	u16 supp_led_modes;
	u32 leds_mode[MAX_LEDS];
	u8 nleds;
	const struct vsc85xx_hw_stat *hw_stats;
	u64 *stats;
	int nstats;
	/* PHY address within the package. */
	u8 addr;
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

	struct mii_timestamper mii_ts;

	bool input_clk_init;
	struct vsc85xx_ptp *ptp;
	/* LOAD/SAVE GPIO pin, used for retrieving or setting time to the PHC. */
	struct gpio_desc *load_save;

	/* For multiple port PHYs; the MDIO address of the base PHY in the
	 * pair of two PHYs that share a 1588 engine. PHY0 and PHY2 are coupled.
	 * PHY1 and PHY3 as well. PHY0 and PHY1 are base PHYs for their
	 * respective pair.
	 */
	unsigned int ts_base_addr;
	u8 ts_base_phy;

	/* ts_lock: used for per-PHY timestamping operations.
	 * phc_lock: used for per-PHY PHC opertations.
	 */
	struct mutex ts_lock;
	struct mutex phc_lock;
};

/* Shared structure between the PHYs of the same package.
 * gpio_lock: used for PHC operations. Common for all PHYs as the load/save GPIO
 * is shared.
 */
struct vsc85xx_shared_private {
	struct mutex gpio_lock;
};

#if IS_ENABLED(CONFIG_OF_MDIO)
struct vsc8531_edge_rate_table {
	u32 vddmac;
	u32 slowdown[8];
};
#endif /* CONFIG_OF_MDIO */

enum csr_target {
	MACRO_CTRL  = 0x07,
};

#if IS_ENABLED(CONFIG_MACSEC)
int vsc8584_macsec_init(struct phy_device *phydev);
void vsc8584_handle_macsec_interrupt(struct phy_device *phydev);
void vsc8584_config_macsec_intr(struct phy_device *phydev);
#else
static inline int vsc8584_macsec_init(struct phy_device *phydev)
{
	return 0;
}
static inline void vsc8584_handle_macsec_interrupt(struct phy_device *phydev)
{
}
static inline void vsc8584_config_macsec_intr(struct phy_device *phydev)
{
}
#endif

#if IS_ENABLED(CONFIG_NETWORK_PHY_TIMESTAMPING)
void vsc85xx_link_change_notify(struct phy_device *phydev);
void vsc8584_config_ts_intr(struct phy_device *phydev);
int vsc8584_ptp_init(struct phy_device *phydev);
int vsc8584_ptp_probe_once(struct phy_device *phydev);
int vsc8584_ptp_probe(struct phy_device *phydev);
irqreturn_t vsc8584_handle_ts_interrupt(struct phy_device *phydev);
#else
static inline void vsc85xx_link_change_notify(struct phy_device *phydev)
{
}
static inline void vsc8584_config_ts_intr(struct phy_device *phydev)
{
}
static inline int vsc8584_ptp_init(struct phy_device *phydev)
{
	return 0;
}
static inline int vsc8584_ptp_probe_once(struct phy_device *phydev)
{
	return 0;
}
static inline int vsc8584_ptp_probe(struct phy_device *phydev)
{
	return 0;
}
static inline irqreturn_t vsc8584_handle_ts_interrupt(struct phy_device *phydev)
{
	return IRQ_NONE;
}
#endif

#endif /* _MSCC_PHY_H_ */
