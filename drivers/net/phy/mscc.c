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
#define MII_VSC85XX_INT_MASK_MASK	  0xa000
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

#define MSCC_EXT_PAGE_ACCESS		  31
#define MSCC_PHY_PAGE_STANDARD		  0x0000 /* Standard registers */
#define MSCC_PHY_PAGE_EXTENDED		  0x0001 /* Extended registers */
#define MSCC_PHY_PAGE_EXTENDED_2	  0x0002 /* Extended reg - page 2 */
#define MSCC_PHY_PAGE_EXTENDED_3	  0x0003 /* Extended reg - page 3 */
#define MSCC_PHY_PAGE_EXTENDED_4	  0x0004 /* Extended reg - page 4 */
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

/* Microsemi PHY ID's */
#define PHY_ID_VSC8530			  0x00070560
#define PHY_ID_VSC8531			  0x00070570
#define PHY_ID_VSC8540			  0x00070760
#define PHY_ID_VSC8541			  0x00070770
#define PHY_ID_VSC8574			  0x000704a0
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

#define MSCC_VSC8584_REVB_INT8051_FW		"mscc_vsc8584_revb_int8051_fb48.bin"
#define MSCC_VSC8584_REVB_INT8051_FW_START_ADDR	0xe800
#define MSCC_VSC8584_REVB_INT8051_FW_CRC	0xfb48

#define MSCC_VSC8574_REVB_INT8051_FW		"mscc_vsc8574_revb_int8051_29e8.bin"
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
	rc = phy_select_page(phydev, MSCC_PHY_PAGE_EXTENDED_2);
	if (rc < 0)
		goto out_unlock;

	reg_val = phy_read(phydev, MSCC_PHY_RGMII_CNTL);
	reg_val &= ~(RGMII_RX_CLK_DELAY_MASK);
	reg_val |= (RGMII_RX_CLK_DELAY_1_1_NS << RGMII_RX_CLK_DELAY_POS);
	phy_write(phydev, MSCC_PHY_RGMII_CNTL, reg_val);

out_unlock:
	rc = phy_restore_page(phydev, rc, rc > 0 ? 0 : rc);
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

static int vsc85xx_eee_init_seq_set(struct phy_device *phydev)
{
	const struct reg_val init_eee[] = {
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
	const struct reg_val pre_init1[] = {
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
	const struct reg_val pre_init2[] = {
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
	const struct reg_val pre_init1[] = {
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
	const struct reg_val pre_init2[] = {
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
		if ((phydev->phy_id & phydev->drv->phy_id_mask) ==
		    (PHY_ID_VSC8574 & phydev->drv->phy_id_mask))
			ret = vsc8574_config_pre_init(phydev);
		else if ((phydev->phy_id & phydev->drv->phy_id_mask) ==
			 (PHY_ID_VSC8584 & phydev->drv->phy_id_mask))
			ret = vsc8584_config_pre_init(phydev);
		else
			ret = -EINVAL;

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

	return genphy_config_init(phydev);

err:
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
	return ret;
}

static int vsc85xx_config_init(struct phy_device *phydev)
{
	int rc, i;
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

	rc = vsc85xx_eee_init_seq_set(phydev);
	if (rc)
		return rc;

	for (i = 0; i < vsc8531->nleds; i++) {
		rc = vsc85xx_led_cntl_set(phydev, i, vsc8531->leds_mode[i]);
		if (rc)
			return rc;
	}

	return genphy_config_init(phydev);
}

static int vsc8584_did_interrupt(struct phy_device *phydev)
{
	int rc = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);

	return (rc < 0) ? 0 : rc & MII_VSC85XX_INT_MASK_MASK;
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
	vsc8531->stats = devm_kmalloc_array(&phydev->mdio.dev, vsc8531->nstats,
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
	vsc8531->stats = devm_kmalloc_array(&phydev->mdio.dev, vsc8531->nstats,
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
	vsc8531->stats = devm_kmalloc_array(&phydev->mdio.dev, vsc8531->nstats,
					    sizeof(u64), GFP_KERNEL);
	if (!vsc8531->stats)
		return -ENOMEM;

	return vsc85xx_dt_led_modes_get(phydev, default_mode);
}

/* Microsemi VSC85xx PHYs */
static struct phy_driver vsc85xx_driver[] = {
{
	.phy_id		= PHY_ID_VSC8530,
	.name		= "Microsemi FE VSC8530",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.soft_reset	= &genphy_soft_reset,
	.config_init	= &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
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
	.features	= PHY_GBIT_FEATURES,
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
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
	.features	= PHY_BASIC_FEATURES,
	.soft_reset	= &genphy_soft_reset,
	.config_init	= &vsc85xx_config_init,
	.config_aneg	= &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
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
	.features	= PHY_GBIT_FEATURES,
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &vsc85xx_config_aneg,
	.aneg_done	= &genphy_aneg_done,
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
	.phy_id		= PHY_ID_VSC8574,
	.name		= "Microsemi GE VSC8574 SyncE",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_GBIT_FEATURES,
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
	.phy_id		= PHY_ID_VSC8584,
	.name		= "Microsemi GE VSC8584 SyncE",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_GBIT_FEATURES,
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
	{ PHY_ID_VSC8530, 0xfffffff0, },
	{ PHY_ID_VSC8531, 0xfffffff0, },
	{ PHY_ID_VSC8540, 0xfffffff0, },
	{ PHY_ID_VSC8541, 0xfffffff0, },
	{ PHY_ID_VSC8574, 0xfffffff0, },
	{ PHY_ID_VSC8584, 0xfffffff0, },
	{ }
};

MODULE_DEVICE_TABLE(mdio, vsc85xx_tbl);

MODULE_DESCRIPTION("Microsemi VSC85xx PHY driver");
MODULE_AUTHOR("Nagaraju Lakkaraju");
MODULE_LICENSE("Dual MIT/GPL");
