/* SPDX-License-Identifier: GPL-2.0 */

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

int at803x_debug_reg_read(struct phy_device *phydev, u16 reg);
int at803x_debug_reg_mask(struct phy_device *phydev, u16 reg,
			  u16 clear, u16 set);
int at803x_debug_reg_write(struct phy_device *phydev, u16 reg, u16 data);
