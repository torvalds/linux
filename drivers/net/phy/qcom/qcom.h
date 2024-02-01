/* SPDX-License-Identifier: GPL-2.0 */

#define AT803X_SPECIFIC_FUNCTION_CONTROL	0x10
#define AT803X_SFC_ASSERT_CRS			BIT(11)
#define AT803X_SFC_FORCE_LINK			BIT(10)
#define AT803X_SFC_MDI_CROSSOVER_MODE_M		GENMASK(6, 5)
#define AT803X_SFC_AUTOMATIC_CROSSOVER		0x3
#define AT803X_SFC_MANUAL_MDIX			0x1
#define AT803X_SFC_MANUAL_MDI			0x0
#define AT803X_SFC_SQE_TEST			BIT(2)
#define AT803X_SFC_POLARITY_REVERSAL		BIT(1)
#define AT803X_SFC_DISABLE_JABBER		BIT(0)

#define AT803X_SPECIFIC_STATUS			0x11
#define AT803X_SS_SPEED_MASK			GENMASK(15, 14)
#define AT803X_SS_SPEED_1000			2
#define AT803X_SS_SPEED_100			1
#define AT803X_SS_SPEED_10			0
#define AT803X_SS_DUPLEX			BIT(13)
#define AT803X_SS_SPEED_DUPLEX_RESOLVED		BIT(11)
#define AT803X_SS_MDIX				BIT(6)

#define QCA808X_SS_SPEED_MASK			GENMASK(9, 7)
#define QCA808X_SS_SPEED_2500			4

#define AT803X_INTR_ENABLE			0x12
#define AT803X_INTR_ENABLE_AUTONEG_ERR		BIT(15)
#define AT803X_INTR_ENABLE_SPEED_CHANGED	BIT(14)
#define AT803X_INTR_ENABLE_DUPLEX_CHANGED	BIT(13)
#define AT803X_INTR_ENABLE_PAGE_RECEIVED	BIT(12)
#define AT803X_INTR_ENABLE_LINK_FAIL		BIT(11)
#define AT803X_INTR_ENABLE_LINK_SUCCESS		BIT(10)
#define AT803X_INTR_ENABLE_LINK_FAIL_BX		BIT(8)
#define AT803X_INTR_ENABLE_LINK_SUCCESS_BX	BIT(7)
#define AT803X_INTR_ENABLE_WIRESPEED_DOWNGRADE	BIT(5)
#define AT803X_INTR_ENABLE_POLARITY_CHANGED	BIT(1)
#define AT803X_INTR_ENABLE_WOL			BIT(0)

#define AT803X_INTR_STATUS			0x13

#define AT803X_SMART_SPEED			0x14
#define AT803X_SMART_SPEED_ENABLE		BIT(5)
#define AT803X_SMART_SPEED_RETRY_LIMIT_MASK	GENMASK(4, 2)
#define AT803X_SMART_SPEED_BYPASS_TIMER		BIT(1)

#define AT803X_CDT				0x16
#define AT803X_CDT_MDI_PAIR_MASK		GENMASK(9, 8)
#define AT803X_CDT_ENABLE_TEST			BIT(0)
#define AT803X_CDT_STATUS			0x1c
#define AT803X_CDT_STATUS_STAT_NORMAL		0
#define AT803X_CDT_STATUS_STAT_SHORT		1
#define AT803X_CDT_STATUS_STAT_OPEN		2
#define AT803X_CDT_STATUS_STAT_FAIL		3
#define AT803X_CDT_STATUS_STAT_MASK		GENMASK(9, 8)
#define AT803X_CDT_STATUS_DELTA_TIME_MASK	GENMASK(7, 0)

#define AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A

#define AT803X_DEBUG_ADDR			0x1D
#define AT803X_DEBUG_DATA			0x1E

#define AT803X_DEBUG_ANALOG_TEST_CTRL		0x00
#define QCA8327_DEBUG_MANU_CTRL_EN		BIT(2)
#define QCA8337_DEBUG_MANU_CTRL_EN		GENMASK(3, 2)
#define AT803X_DEBUG_RX_CLK_DLY_EN		BIT(15)

#define AT803X_DEBUG_SYSTEM_CTRL_MODE		0x05
#define AT803X_DEBUG_TX_CLK_DLY_EN		BIT(8)

#define AT803X_DEBUG_REG_HIB_CTRL		0x0b
#define   AT803X_DEBUG_HIB_CTRL_SEL_RST_80U	BIT(10)
#define   AT803X_DEBUG_HIB_CTRL_EN_ANY_CHANGE	BIT(13)
#define   AT803X_DEBUG_HIB_CTRL_PS_HIB_EN	BIT(15)

#define AT803X_DEFAULT_DOWNSHIFT		5
#define AT803X_MIN_DOWNSHIFT			2
#define AT803X_MAX_DOWNSHIFT			9

enum stat_access_type {
	PHY,
	MMD
};

struct at803x_hw_stat {
	const char *string;
	u8 reg;
	u32 mask;
	enum stat_access_type access_type;
};

struct at803x_ss_mask {
	u16 speed_mask;
	u8 speed_shift;
};

int at803x_debug_reg_read(struct phy_device *phydev, u16 reg);
int at803x_debug_reg_mask(struct phy_device *phydev, u16 reg,
			  u16 clear, u16 set);
int at803x_debug_reg_write(struct phy_device *phydev, u16 reg, u16 data);
int at803x_set_wol(struct phy_device *phydev,
		   struct ethtool_wolinfo *wol);
void at803x_get_wol(struct phy_device *phydev,
		    struct ethtool_wolinfo *wol);
int at803x_ack_interrupt(struct phy_device *phydev);
int at803x_config_intr(struct phy_device *phydev);
irqreturn_t at803x_handle_interrupt(struct phy_device *phydev);
int at803x_read_specific_status(struct phy_device *phydev,
				struct at803x_ss_mask ss_mask);
int at803x_config_mdix(struct phy_device *phydev, u8 ctrl);
int at803x_prepare_config_aneg(struct phy_device *phydev);
int at803x_get_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, void *data);
int at803x_set_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, const void *data);
int at803x_cdt_fault_length(int dt);
int at803x_cdt_start(struct phy_device *phydev, u32 cdt_start);
int at803x_cdt_wait_for_completion(struct phy_device *phydev,
				   u32 cdt_en);
