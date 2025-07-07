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

#define QCA808X_CDT_ENABLE_TEST			BIT(15)
#define QCA808X_CDT_INTER_CHECK_DIS		BIT(13)
#define QCA808X_CDT_STATUS			BIT(11)
#define QCA808X_CDT_LENGTH_UNIT			BIT(10)

#define QCA808X_MMD3_CDT_STATUS			0x8064
#define QCA808X_MMD3_CDT_DIAG_PAIR_A		0x8065
#define QCA808X_MMD3_CDT_DIAG_PAIR_B		0x8066
#define QCA808X_MMD3_CDT_DIAG_PAIR_C		0x8067
#define QCA808X_MMD3_CDT_DIAG_PAIR_D		0x8068
#define QCA808X_CDT_DIAG_LENGTH_SAME_SHORT	GENMASK(15, 8)
#define QCA808X_CDT_DIAG_LENGTH_CROSS_SHORT	GENMASK(7, 0)

#define QCA808X_CDT_CODE_PAIR_A			GENMASK(15, 12)
#define QCA808X_CDT_CODE_PAIR_B			GENMASK(11, 8)
#define QCA808X_CDT_CODE_PAIR_C			GENMASK(7, 4)
#define QCA808X_CDT_CODE_PAIR_D			GENMASK(3, 0)

#define QCA808X_CDT_STATUS_STAT_TYPE		GENMASK(1, 0)
#define QCA808X_CDT_STATUS_STAT_FAIL		FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_TYPE, 0)
#define QCA808X_CDT_STATUS_STAT_NORMAL		FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_TYPE, 1)
#define QCA808X_CDT_STATUS_STAT_SAME_OPEN	FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_TYPE, 2)
#define QCA808X_CDT_STATUS_STAT_SAME_SHORT	FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_TYPE, 3)

#define QCA808X_CDT_STATUS_STAT_MDI		GENMASK(3, 2)
#define QCA808X_CDT_STATUS_STAT_MDI1		FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_MDI, 1)
#define QCA808X_CDT_STATUS_STAT_MDI2		FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_MDI, 2)
#define QCA808X_CDT_STATUS_STAT_MDI3		FIELD_PREP_CONST(QCA808X_CDT_STATUS_STAT_MDI, 3)

/* NORMAL are MDI with type set to 0 */
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_NORMAL	QCA808X_CDT_STATUS_STAT_MDI1
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_OPEN		(QCA808X_CDT_STATUS_STAT_SAME_OPEN |\
									 QCA808X_CDT_STATUS_STAT_MDI1)
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_SHORT	(QCA808X_CDT_STATUS_STAT_SAME_SHORT |\
									 QCA808X_CDT_STATUS_STAT_MDI1)
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_NORMAL	QCA808X_CDT_STATUS_STAT_MDI2
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_OPEN		(QCA808X_CDT_STATUS_STAT_SAME_OPEN |\
									 QCA808X_CDT_STATUS_STAT_MDI2)
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_SHORT	(QCA808X_CDT_STATUS_STAT_SAME_SHORT |\
									 QCA808X_CDT_STATUS_STAT_MDI2)
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_NORMAL	QCA808X_CDT_STATUS_STAT_MDI3
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_OPEN		(QCA808X_CDT_STATUS_STAT_SAME_OPEN |\
									 QCA808X_CDT_STATUS_STAT_MDI3)
#define QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_SHORT	(QCA808X_CDT_STATUS_STAT_SAME_SHORT |\
									 QCA808X_CDT_STATUS_STAT_MDI3)

/* Added for reference of existence but should be handled by wait_for_completion already */
#define QCA808X_CDT_STATUS_STAT_BUSY		(BIT(1) | BIT(3))

#define QCA808X_MMD7_LED_GLOBAL			0x8073
#define QCA808X_LED_BLINK_1			GENMASK(11, 6)
#define QCA808X_LED_BLINK_2			GENMASK(5, 0)
/* Values are the same for both BLINK_1 and BLINK_2 */
#define QCA808X_LED_BLINK_FREQ_MASK		GENMASK(5, 3)
#define QCA808X_LED_BLINK_FREQ_2HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x0)
#define QCA808X_LED_BLINK_FREQ_4HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x1)
#define QCA808X_LED_BLINK_FREQ_8HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x2)
#define QCA808X_LED_BLINK_FREQ_16HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x3)
#define QCA808X_LED_BLINK_FREQ_32HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x4)
#define QCA808X_LED_BLINK_FREQ_64HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x5)
#define QCA808X_LED_BLINK_FREQ_128HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x6)
#define QCA808X_LED_BLINK_FREQ_256HZ		FIELD_PREP(QCA808X_LED_BLINK_FREQ_MASK, 0x7)
#define QCA808X_LED_BLINK_DUTY_MASK		GENMASK(2, 0)
#define QCA808X_LED_BLINK_DUTY_50_50		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x0)
#define QCA808X_LED_BLINK_DUTY_75_25		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x1)
#define QCA808X_LED_BLINK_DUTY_25_75		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x2)
#define QCA808X_LED_BLINK_DUTY_33_67		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x3)
#define QCA808X_LED_BLINK_DUTY_67_33		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x4)
#define QCA808X_LED_BLINK_DUTY_17_83		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x5)
#define QCA808X_LED_BLINK_DUTY_83_17		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x6)
#define QCA808X_LED_BLINK_DUTY_8_92		FIELD_PREP(QCA808X_LED_BLINK_DUTY_MASK, 0x7)

/* LED hw control pattern is the same for every LED */
#define QCA808X_LED_PATTERN_MASK		GENMASK(15, 0)
#define QCA808X_LED_SPEED2500_ON		BIT(15)
#define QCA808X_LED_SPEED2500_BLINK		BIT(14)
/* Follow blink trigger even if duplex or speed condition doesn't match */
#define QCA808X_LED_BLINK_CHECK_BYPASS		BIT(13)
#define QCA808X_LED_FULL_DUPLEX_ON		BIT(12)
#define QCA808X_LED_HALF_DUPLEX_ON		BIT(11)
#define QCA808X_LED_TX_BLINK			BIT(10)
#define QCA808X_LED_RX_BLINK			BIT(9)
#define QCA808X_LED_TX_ON_10MS			BIT(8)
#define QCA808X_LED_RX_ON_10MS			BIT(7)
#define QCA808X_LED_SPEED1000_ON		BIT(6)
#define QCA808X_LED_SPEED100_ON			BIT(5)
#define QCA808X_LED_SPEED10_ON			BIT(4)
#define QCA808X_LED_COLLISION_BLINK		BIT(3)
#define QCA808X_LED_SPEED1000_BLINK		BIT(2)
#define QCA808X_LED_SPEED100_BLINK		BIT(1)
#define QCA808X_LED_SPEED10_BLINK		BIT(0)

/* LED force ctrl is the same for every LED
 * No documentation exist for this, not even internal one
 * with NDA as QCOM gives only info about configuring
 * hw control pattern rules and doesn't indicate any way
 * to force the LED to specific mode.
 * These define comes from reverse and testing and maybe
 * lack of some info or some info are not entirely correct.
 * For the basic LED control and hw control these finding
 * are enough to support LED control in all the required APIs.
 *
 * On doing some comparison with implementation with qca807x,
 * it was found that it's 1:1 equal to it and confirms all the
 * reverse done. It was also found further specification with the
 * force mode and the blink modes.
 */
#define QCA808X_LED_FORCE_EN			BIT(15)
#define QCA808X_LED_FORCE_MODE_MASK		GENMASK(14, 13)
#define QCA808X_LED_FORCE_BLINK_1		FIELD_PREP(QCA808X_LED_FORCE_MODE_MASK, 0x3)
#define QCA808X_LED_FORCE_BLINK_2		FIELD_PREP(QCA808X_LED_FORCE_MODE_MASK, 0x2)
#define QCA808X_LED_FORCE_ON			FIELD_PREP(QCA808X_LED_FORCE_MODE_MASK, 0x1)
#define QCA808X_LED_FORCE_OFF			FIELD_PREP(QCA808X_LED_FORCE_MODE_MASK, 0x0)

#define AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A

#define AT803X_PHY_MMD3_WOL_CTRL		0x8012
#define AT803X_WOL_EN				BIT(5)

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
int at8031_set_wol(struct phy_device *phydev,
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
int at803x_read_status(struct phy_device *phydev);
int at803x_get_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, void *data);
int at803x_set_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, const void *data);
int at803x_cdt_fault_length(int dt);
int at803x_cdt_start(struct phy_device *phydev, u32 cdt_start);
int at803x_cdt_wait_for_completion(struct phy_device *phydev,
				   u32 cdt_en);
int qca808x_cable_test_get_status(struct phy_device *phydev, bool *finished);
int qca808x_led_reg_hw_control_enable(struct phy_device *phydev, u16 reg);
bool qca808x_led_reg_hw_control_status(struct phy_device *phydev, u16 reg);
int qca808x_led_reg_brightness_set(struct phy_device *phydev,
				   u16 reg, enum led_brightness value);
int qca808x_led_reg_blink_set(struct phy_device *phydev, u16 reg,
			      unsigned long *delay_on,
			      unsigned long *delay_off);
