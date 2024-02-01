// SPDX-License-Identifier: GPL-2.0

#include <linux/phy.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

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
