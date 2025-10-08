// SPDX-License-Identifier: GPL-2.0

#include <linux/phy.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool_netlink.h>

#include "qcom.h"

MODULE_DESCRIPTION("Qualcomm PHY driver Common Functions");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");

int at803x_debug_reg_read(struct phy_device *phydev, u16 reg)
{
	int ret;

	ret = phy_write(phydev, AT803X_DEBUG_ADDR, reg);
	if (ret < 0)
		return ret;

	return phy_read(phydev, AT803X_DEBUG_DATA);
}
EXPORT_SYMBOL_GPL(at803x_debug_reg_read);

int at803x_debug_reg_mask(struct phy_device *phydev, u16 reg,
			  u16 clear, u16 set)
{
	u16 val;
	int ret;

	ret = at803x_debug_reg_read(phydev, reg);
	if (ret < 0)
		return ret;

	val = ret & 0xffff;
	val &= ~clear;
	val |= set;

	return phy_write(phydev, AT803X_DEBUG_DATA, val);
}
EXPORT_SYMBOL_GPL(at803x_debug_reg_mask);

int at803x_debug_reg_write(struct phy_device *phydev, u16 reg, u16 data)
{
	int ret;

	ret = phy_write(phydev, AT803X_DEBUG_ADDR, reg);
	if (ret < 0)
		return ret;

	return phy_write(phydev, AT803X_DEBUG_DATA, data);
}
EXPORT_SYMBOL_GPL(at803x_debug_reg_write);

int at803x_set_wol(struct phy_device *phydev,
		   struct ethtool_wolinfo *wol)
{
	int ret, irq_enabled;

	if (wol->wolopts & WAKE_MAGIC) {
		struct net_device *ndev = phydev->attached_dev;
		const u8 *mac;
		unsigned int i;
		static const unsigned int offsets[] = {
			AT803X_LOC_MAC_ADDR_32_47_OFFSET,
			AT803X_LOC_MAC_ADDR_16_31_OFFSET,
			AT803X_LOC_MAC_ADDR_0_15_OFFSET,
		};

		if (!ndev)
			return -ENODEV;

		mac = (const u8 *)ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		for (i = 0; i < 3; i++)
			phy_write_mmd(phydev, MDIO_MMD_PCS, offsets[i],
				      mac[(i * 2) + 1] | (mac[(i * 2)] << 8));

		/* Enable WOL interrupt */
		ret = phy_modify(phydev, AT803X_INTR_ENABLE, 0, AT803X_INTR_ENABLE_WOL);
		if (ret)
			return ret;
	} else {
		/* Disable WOL interrupt */
		ret = phy_modify(phydev, AT803X_INTR_ENABLE, AT803X_INTR_ENABLE_WOL, 0);
		if (ret)
			return ret;
	}

	/* Clear WOL status */
	ret = phy_read(phydev, AT803X_INTR_STATUS);
	if (ret < 0)
		return ret;

	/* Check if there are other interrupts except for WOL triggered when PHY is
	 * in interrupt mode, only the interrupts enabled by AT803X_INTR_ENABLE can
	 * be passed up to the interrupt PIN.
	 */
	irq_enabled = phy_read(phydev, AT803X_INTR_ENABLE);
	if (irq_enabled < 0)
		return irq_enabled;

	irq_enabled &= ~AT803X_INTR_ENABLE_WOL;
	if (ret & irq_enabled && !phy_polling_mode(phydev))
		phy_trigger_machine(phydev);

	return 0;
}
EXPORT_SYMBOL_GPL(at803x_set_wol);

int at8031_set_wol(struct phy_device *phydev,
		   struct ethtool_wolinfo *wol)
{
	int ret;

	/* First setup MAC address and enable WOL interrupt */
	ret = at803x_set_wol(phydev, wol);
	if (ret)
		return ret;

	if (wol->wolopts & WAKE_MAGIC)
		/* Enable WOL function for 1588 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
				     AT803X_PHY_MMD3_WOL_CTRL,
				     0, AT803X_WOL_EN);
	else
		/* Disable WoL function for 1588 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
				     AT803X_PHY_MMD3_WOL_CTRL,
				     AT803X_WOL_EN, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(at8031_set_wol);

void at803x_get_wol(struct phy_device *phydev,
		    struct ethtool_wolinfo *wol)
{
	int value;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	value = phy_read(phydev, AT803X_INTR_ENABLE);
	if (value < 0)
		return;

	if (value & AT803X_INTR_ENABLE_WOL)
		wol->wolopts |= WAKE_MAGIC;
}
EXPORT_SYMBOL_GPL(at803x_get_wol);

int at803x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, AT803X_INTR_STATUS);

	return (err < 0) ? err : 0;
}
EXPORT_SYMBOL_GPL(at803x_ack_interrupt);

int at803x_config_intr(struct phy_device *phydev)
{
	int err;
	int value;

	value = phy_read(phydev, AT803X_INTR_ENABLE);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* Clear any pending interrupts */
		err = at803x_ack_interrupt(phydev);
		if (err)
			return err;

		value |= AT803X_INTR_ENABLE_AUTONEG_ERR;
		value |= AT803X_INTR_ENABLE_SPEED_CHANGED;
		value |= AT803X_INTR_ENABLE_DUPLEX_CHANGED;
		value |= AT803X_INTR_ENABLE_LINK_FAIL;
		value |= AT803X_INTR_ENABLE_LINK_SUCCESS;

		err = phy_write(phydev, AT803X_INTR_ENABLE, value);
	} else {
		err = phy_write(phydev, AT803X_INTR_ENABLE, 0);
		if (err)
			return err;

		/* Clear any pending interrupts */
		err = at803x_ack_interrupt(phydev);
	}

	return err;
}
EXPORT_SYMBOL_GPL(at803x_config_intr);

irqreturn_t at803x_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, int_enabled;

	irq_status = phy_read(phydev, AT803X_INTR_STATUS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* Read the current enabled interrupts */
	int_enabled = phy_read(phydev, AT803X_INTR_ENABLE);
	if (int_enabled < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* See if this was one of our enabled interrupts */
	if (!(irq_status & int_enabled))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(at803x_handle_interrupt);

int at803x_read_specific_status(struct phy_device *phydev,
				struct at803x_ss_mask ss_mask)
{
	int ss;

	/* Read the AT8035 PHY-Specific Status register, which indicates the
	 * speed and duplex that the PHY is actually using, irrespective of
	 * whether we are in autoneg mode or not.
	 */
	ss = phy_read(phydev, AT803X_SPECIFIC_STATUS);
	if (ss < 0)
		return ss;

	if (ss & AT803X_SS_SPEED_DUPLEX_RESOLVED) {
		int sfc, speed;

		sfc = phy_read(phydev, AT803X_SPECIFIC_FUNCTION_CONTROL);
		if (sfc < 0)
			return sfc;

		speed = ss & ss_mask.speed_mask;
		speed >>= ss_mask.speed_shift;

		switch (speed) {
		case AT803X_SS_SPEED_10:
			phydev->speed = SPEED_10;
			break;
		case AT803X_SS_SPEED_100:
			phydev->speed = SPEED_100;
			break;
		case AT803X_SS_SPEED_1000:
			phydev->speed = SPEED_1000;
			break;
		case QCA808X_SS_SPEED_2500:
			phydev->speed = SPEED_2500;
			break;
		}
		if (ss & AT803X_SS_DUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		if (ss & AT803X_SS_MDIX)
			phydev->mdix = ETH_TP_MDI_X;
		else
			phydev->mdix = ETH_TP_MDI;

		switch (FIELD_GET(AT803X_SFC_MDI_CROSSOVER_MODE_M, sfc)) {
		case AT803X_SFC_MANUAL_MDI:
			phydev->mdix_ctrl = ETH_TP_MDI;
			break;
		case AT803X_SFC_MANUAL_MDIX:
			phydev->mdix_ctrl = ETH_TP_MDI_X;
			break;
		case AT803X_SFC_AUTOMATIC_CROSSOVER:
			phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(at803x_read_specific_status);

int at803x_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI:
		val = AT803X_SFC_MANUAL_MDI;
		break;
	case ETH_TP_MDI_X:
		val = AT803X_SFC_MANUAL_MDIX;
		break;
	case ETH_TP_MDI_AUTO:
		val = AT803X_SFC_AUTOMATIC_CROSSOVER;
		break;
	default:
		return 0;
	}

	return phy_modify_changed(phydev, AT803X_SPECIFIC_FUNCTION_CONTROL,
			  AT803X_SFC_MDI_CROSSOVER_MODE_M,
			  FIELD_PREP(AT803X_SFC_MDI_CROSSOVER_MODE_M, val));
}
EXPORT_SYMBOL_GPL(at803x_config_mdix);

int at803x_prepare_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = at803x_config_mdix(phydev, phydev->mdix_ctrl);
	if (ret < 0)
		return ret;

	/* Changes of the midx bits are disruptive to the normal operation;
	 * therefore any changes to these registers must be followed by a
	 * software reset to take effect.
	 */
	if (ret == 1) {
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(at803x_prepare_config_aneg);

int at803x_read_status(struct phy_device *phydev)
{
	struct at803x_ss_mask ss_mask = { 0 };
	int err, old_link = phydev->link;

	/* Update the link, but return if there was an error */
	err = genphy_update_link(phydev);
	if (err)
		return err;

	/* why bother the PHY if nothing can have changed */
	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	err = genphy_read_lpa(phydev);
	if (err < 0)
		return err;

	ss_mask.speed_mask = AT803X_SS_SPEED_MASK;
	ss_mask.speed_shift = __bf_shf(AT803X_SS_SPEED_MASK);
	err = at803x_read_specific_status(phydev, ss_mask);
	if (err < 0)
		return err;

	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete)
		phy_resolve_aneg_pause(phydev);

	return 0;
}
EXPORT_SYMBOL_GPL(at803x_read_status);

static int at803x_get_downshift(struct phy_device *phydev, u8 *d)
{
	int val;

	val = phy_read(phydev, AT803X_SMART_SPEED);
	if (val < 0)
		return val;

	if (val & AT803X_SMART_SPEED_ENABLE)
		*d = FIELD_GET(AT803X_SMART_SPEED_RETRY_LIMIT_MASK, val) + 2;
	else
		*d = DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int at803x_set_downshift(struct phy_device *phydev, u8 cnt)
{
	u16 mask, set;
	int ret;

	switch (cnt) {
	case DOWNSHIFT_DEV_DEFAULT_COUNT:
		cnt = AT803X_DEFAULT_DOWNSHIFT;
		fallthrough;
	case AT803X_MIN_DOWNSHIFT ... AT803X_MAX_DOWNSHIFT:
		set = AT803X_SMART_SPEED_ENABLE |
		      AT803X_SMART_SPEED_BYPASS_TIMER |
		      FIELD_PREP(AT803X_SMART_SPEED_RETRY_LIMIT_MASK, cnt - 2);
		mask = AT803X_SMART_SPEED_RETRY_LIMIT_MASK;
		break;
	case DOWNSHIFT_DEV_DISABLE:
		set = 0;
		mask = AT803X_SMART_SPEED_ENABLE |
		       AT803X_SMART_SPEED_BYPASS_TIMER;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_modify_changed(phydev, AT803X_SMART_SPEED, mask, set);

	/* After changing the smart speed settings, we need to perform a
	 * software reset, use phy_init_hw() to make sure we set the
	 * reapply any values which might got lost during software reset.
	 */
	if (ret == 1)
		ret = phy_init_hw(phydev);

	return ret;
}

int at803x_get_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return at803x_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(at803x_get_tunable);

int at803x_set_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return at803x_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(at803x_set_tunable);

int at803x_cdt_fault_length(int dt)
{
	/* According to the datasheet the distance to the fault is
	 * DELTA_TIME * 0.824 meters.
	 *
	 * The author suspect the correct formula is:
	 *
	 *   fault_distance = DELTA_TIME * (c * VF) / 125MHz / 2
	 *
	 * where c is the speed of light, VF is the velocity factor of
	 * the twisted pair cable, 125MHz the counter frequency and
	 * we need to divide by 2 because the hardware will measure the
	 * round trip time to the fault and back to the PHY.
	 *
	 * With a VF of 0.69 we get the factor 0.824 mentioned in the
	 * datasheet.
	 */
	return (dt * 824) / 10;
}
EXPORT_SYMBOL_GPL(at803x_cdt_fault_length);

int at803x_cdt_start(struct phy_device *phydev, u32 cdt_start)
{
	return phy_write(phydev, AT803X_CDT, cdt_start);
}
EXPORT_SYMBOL_GPL(at803x_cdt_start);

int at803x_cdt_wait_for_completion(struct phy_device *phydev,
				   u32 cdt_en)
{
	int val, ret;

	/* One test run takes about 25ms */
	ret = phy_read_poll_timeout(phydev, AT803X_CDT, val,
				    !(val & cdt_en),
				    30000, 100000, true);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(at803x_cdt_wait_for_completion);

static bool qca808x_cdt_fault_length_valid(int cdt_code)
{
	switch (cdt_code) {
	case QCA808X_CDT_STATUS_STAT_SAME_SHORT:
	case QCA808X_CDT_STATUS_STAT_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_SHORT:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_SHORT:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_SHORT:
		return true;
	default:
		return false;
	}
}

static int qca808x_cable_test_result_trans(int cdt_code)
{
	switch (cdt_code) {
	case QCA808X_CDT_STATUS_STAT_NORMAL:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case QCA808X_CDT_STATUS_STAT_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case QCA808X_CDT_STATUS_STAT_SAME_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI1_SAME_SHORT:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI2_SAME_SHORT:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_NORMAL:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_OPEN:
	case QCA808X_CDT_STATUS_STAT_CROSS_SHORT_WITH_MDI3_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT;
	case QCA808X_CDT_STATUS_STAT_FAIL:
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int qca808x_cdt_fault_length(struct phy_device *phydev, int pair,
				    int result)
{
	int val;
	u32 cdt_length_reg = 0;

	switch (pair) {
	case ETHTOOL_A_CABLE_PAIR_A:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_A;
		break;
	case ETHTOOL_A_CABLE_PAIR_B:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_B;
		break;
	case ETHTOOL_A_CABLE_PAIR_C:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_C;
		break;
	case ETHTOOL_A_CABLE_PAIR_D:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_D;
		break;
	default:
		return -EINVAL;
	}

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, cdt_length_reg);
	if (val < 0)
		return val;

	if (result == ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT)
		val = FIELD_GET(QCA808X_CDT_DIAG_LENGTH_SAME_SHORT, val);
	else
		val = FIELD_GET(QCA808X_CDT_DIAG_LENGTH_CROSS_SHORT, val);

	return at803x_cdt_fault_length(val);
}

static int qca808x_cable_test_get_pair_status(struct phy_device *phydev, u8 pair,
					      u16 status)
{
	int length, result;
	u16 pair_code;

	switch (pair) {
	case ETHTOOL_A_CABLE_PAIR_A:
		pair_code = FIELD_GET(QCA808X_CDT_CODE_PAIR_A, status);
		break;
	case ETHTOOL_A_CABLE_PAIR_B:
		pair_code = FIELD_GET(QCA808X_CDT_CODE_PAIR_B, status);
		break;
	case ETHTOOL_A_CABLE_PAIR_C:
		pair_code = FIELD_GET(QCA808X_CDT_CODE_PAIR_C, status);
		break;
	case ETHTOOL_A_CABLE_PAIR_D:
		pair_code = FIELD_GET(QCA808X_CDT_CODE_PAIR_D, status);
		break;
	default:
		return -EINVAL;
	}

	result = qca808x_cable_test_result_trans(pair_code);
	ethnl_cable_test_result(phydev, pair, result);

	if (qca808x_cdt_fault_length_valid(pair_code)) {
		length = qca808x_cdt_fault_length(phydev, pair, result);
		ethnl_cable_test_fault_length(phydev, pair, length);
	}

	return 0;
}

int qca808x_cable_test_get_status(struct phy_device *phydev, bool *finished)
{
	int ret, val;

	*finished = false;

	val = QCA808X_CDT_ENABLE_TEST |
	      QCA808X_CDT_LENGTH_UNIT;
	ret = at803x_cdt_start(phydev, val);
	if (ret)
		return ret;

	ret = at803x_cdt_wait_for_completion(phydev, QCA808X_CDT_ENABLE_TEST);
	if (ret)
		return ret;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, QCA808X_MMD3_CDT_STATUS);
	if (val < 0)
		return val;

	ret = qca808x_cable_test_get_pair_status(phydev, ETHTOOL_A_CABLE_PAIR_A, val);
	if (ret)
		return ret;

	ret = qca808x_cable_test_get_pair_status(phydev, ETHTOOL_A_CABLE_PAIR_B, val);
	if (ret)
		return ret;

	ret = qca808x_cable_test_get_pair_status(phydev, ETHTOOL_A_CABLE_PAIR_C, val);
	if (ret)
		return ret;

	ret = qca808x_cable_test_get_pair_status(phydev, ETHTOOL_A_CABLE_PAIR_D, val);
	if (ret)
		return ret;

	*finished = true;

	return 0;
}
EXPORT_SYMBOL_GPL(qca808x_cable_test_get_status);

int qca808x_led_reg_hw_control_enable(struct phy_device *phydev, u16 reg)
{
	return phy_clear_bits_mmd(phydev, MDIO_MMD_AN, reg,
				  QCA808X_LED_FORCE_EN);
}
EXPORT_SYMBOL_GPL(qca808x_led_reg_hw_control_enable);

bool qca808x_led_reg_hw_control_status(struct phy_device *phydev, u16 reg)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, reg);
	return !(val & QCA808X_LED_FORCE_EN);
}
EXPORT_SYMBOL_GPL(qca808x_led_reg_hw_control_status);

int qca808x_led_reg_brightness_set(struct phy_device *phydev,
				   u16 reg, enum led_brightness value)
{
	return phy_modify_mmd(phydev, MDIO_MMD_AN, reg,
			      QCA808X_LED_FORCE_EN | QCA808X_LED_FORCE_MODE_MASK,
			      QCA808X_LED_FORCE_EN | (value ? QCA808X_LED_FORCE_ON :
							      QCA808X_LED_FORCE_OFF));
}
EXPORT_SYMBOL_GPL(qca808x_led_reg_brightness_set);

int qca808x_led_reg_blink_set(struct phy_device *phydev, u16 reg,
			      unsigned long *delay_on,
			      unsigned long *delay_off)
{
	int ret;

	/* Set blink to 50% off, 50% on at 4Hz by default */
	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_LED_GLOBAL,
			     QCA808X_LED_BLINK_FREQ_MASK | QCA808X_LED_BLINK_DUTY_MASK,
			     QCA808X_LED_BLINK_FREQ_4HZ | QCA808X_LED_BLINK_DUTY_50_50);
	if (ret)
		return ret;

	/* We use BLINK_1 for normal blinking */
	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, reg,
			     QCA808X_LED_FORCE_EN | QCA808X_LED_FORCE_MODE_MASK,
			     QCA808X_LED_FORCE_EN | QCA808X_LED_FORCE_BLINK_1);
	if (ret)
		return ret;

	/* We set blink to 4Hz, aka 250ms */
	*delay_on = 250 / 2;
	*delay_off = 250 / 2;

	return 0;
}
EXPORT_SYMBOL_GPL(qca808x_led_reg_blink_set);

/* Enable CRC checking for both received and transmitted frames to ensure
 * accurate counter recording. The hardware supports a 32-bit counter,
 * configure the counter to clear after it is read to facilitate the
 * implementation of a 64-bit software counter
 */
int qcom_phy_counter_config(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_CTRL,
				QCA808X_MMD7_CNT_CTRL_CRC_CHECK_EN |
				QCA808X_MMD7_CNT_CTRL_READ_CLEAR_EN);
}
EXPORT_SYMBOL_GPL(qcom_phy_counter_config);

int qcom_phy_update_stats(struct phy_device *phydev,
			  struct qcom_phy_hw_stats *hw_stats)
{
	int ret;
	u32 cnt;

	/* PHY 32-bit counter for RX packets. */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_RX_PKT_15_0);
	if (ret < 0)
		return ret;

	cnt = ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_RX_PKT_31_16);
	if (ret < 0)
		return ret;

	cnt |= ret << 16;
	hw_stats->rx_pkts += cnt;

	/* PHY 16-bit counter for RX CRC error packets. */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_RX_ERR_PKT);
	if (ret < 0)
		return ret;

	hw_stats->rx_err_pkts += ret;

	/* PHY 32-bit counter for TX packets. */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_TX_PKT_15_0);
	if (ret < 0)
		return ret;

	cnt = ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_TX_PKT_31_16);
	if (ret < 0)
		return ret;

	cnt |= ret << 16;
	hw_stats->tx_pkts += cnt;

	/* PHY 16-bit counter for TX CRC error packets. */
	ret = phy_read_mmd(phydev, MDIO_MMD_AN, QCA808X_MMD7_CNT_TX_ERR_PKT);
	if (ret < 0)
		return ret;

	hw_stats->tx_err_pkts += ret;

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_phy_update_stats);

void qcom_phy_get_stats(struct ethtool_phy_stats *stats,
			struct qcom_phy_hw_stats hw_stats)
{
	stats->tx_packets = hw_stats.tx_pkts;
	stats->tx_errors = hw_stats.tx_err_pkts;
	stats->rx_packets = hw_stats.rx_pkts;
	stats->rx_errors = hw_stats.rx_err_pkts;
}
EXPORT_SYMBOL_GPL(qcom_phy_get_stats);
