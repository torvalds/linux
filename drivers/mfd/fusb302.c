/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Some ideas are from chrome ec and fairchild GPL fusb302 driver.
 */

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>

#include "fusb302.h"

#define FUSB302_MAX_REG		(FUSB_REG_FIFO + 50)
#define FUSB_MS_TO_NS(x)	((s64)x * 1000 * 1000)

#define FUSB_MODE_DRP		0
#define FUSB_MODE_UFP		1
#define FUSB_MODE_DFP		2
#define FUSB_MODE_ASS		3

#define TYPEC_CC_VOLT_OPEN	0
#define TYPEC_CC_VOLT_RA	1
#define TYPEC_CC_VOLT_RD	2
#define TYPEC_CC_VOLT_RP	3

#define EVENT_CC		BIT(0)
#define EVENT_RX		BIT(1)
#define EVENT_TX		BIT(2)
#define EVENT_REC_RESET		BIT(3)
#define EVENT_WORK_CONTINUE	BIT(5)
#define EVENT_TIMER_MUX		BIT(6)
#define EVENT_TIMER_STATE	BIT(7)
#define FLAG_EVENT		(EVENT_RX | EVENT_TIMER_MUX | \
				 EVENT_TIMER_STATE)

#define PIN_MAP_A		BIT(0)
#define PIN_MAP_B		BIT(1)
#define PIN_MAP_C		BIT(2)
#define PIN_MAP_D		BIT(3)
#define PIN_MAP_E		BIT(4)
#define PIN_MAP_F		BIT(5)

#define PACKET_IS_CONTROL_MSG(header, type) \
		(PD_HEADER_CNT(header) == 0 && \
		 PD_HEADER_TYPE(header) == type)

#define PACKET_IS_DATA_MSG(header, type) \
		(PD_HEADER_CNT(header) != 0 && \
		 PD_HEADER_TYPE(header) == type)

static u8 fusb30x_port_used;
static struct fusb30x_chip *fusb30x_port_info[256];

static bool is_write_reg(struct device *dev, unsigned int reg)
{
	if (reg >= FUSB_REG_FIFO)
		return true;
	else
		return ((reg < (FUSB_REG_CONTROL4 + 1)) && (reg > 0x01)) ?
			true : false;
}

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg > FUSB_REG_CONTROL4)
		return true;

	switch (reg) {
	case FUSB_REG_CONTROL0:
	case FUSB_REG_CONTROL1:
	case FUSB_REG_CONTROL3:
	case FUSB_REG_RESET:
		return true;
	}
	return false;
}

struct regmap_config fusb302_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = is_write_reg,
	.volatile_reg = is_volatile_reg,
	.max_register = FUSB302_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
};

static void dump_notify_info(struct fusb30x_chip *chip)
{
	dev_dbg(chip->dev, "port        %d\n", chip->port_num);
	dev_dbg(chip->dev, "orientation %d\n", chip->notify.orientation);
	dev_dbg(chip->dev, "power_role  %d\n", chip->notify.power_role);
	dev_dbg(chip->dev, "data_role   %d\n", chip->notify.data_role);
	dev_dbg(chip->dev, "cc          %d\n", chip->notify.is_cc_connected);
	dev_dbg(chip->dev, "pd          %d\n", chip->notify.is_pd_connected);
	dev_dbg(chip->dev, "enter_mode  %d\n", chip->notify.is_enter_mode);
	dev_dbg(chip->dev, "pin support %d\n",
		chip->notify.pin_assignment_support);
	dev_dbg(chip->dev, "pin def     %d\n", chip->notify.pin_assignment_def);
	dev_dbg(chip->dev, "attention   %d\n", chip->notify.attention);
}

static const unsigned int fusb302_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_USB_VBUS_EN,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_SLOW,
	EXTCON_CHG_USB_FAST,
	EXTCON_DISP_DP,
	EXTCON_NONE,
};

static void fusb_set_pos_power(struct fusb30x_chip *chip, int max_vol,
			       int max_cur)
{
	int i;
	int pos_find;
	int tmp;

	pos_find = 0;
	for (i = PD_HEADER_CNT(chip->rec_head) - 1; i >= 0; i--) {
		switch (CAP_POWER_TYPE(chip->rec_load[i])) {
		case 0:
			/* Fixed Supply */
			if ((CAP_FPDO_VOLTAGE(chip->rec_load[i]) * 50) <=
			    max_vol &&
			    (CAP_FPDO_CURRENT(chip->rec_load[i]) * 10) <=
			    max_cur) {
				chip->pos_power = i + 1;
				tmp = CAP_FPDO_VOLTAGE(chip->rec_load[i]);
				chip->pd_output_vol = tmp * 50;
				tmp = CAP_FPDO_CURRENT(chip->rec_load[i]);
				chip->pd_output_cur = tmp * 10;
				pos_find = 1;
			}
			break;
		case 1:
			/* Battery */
			if ((CAP_VPDO_VOLTAGE(chip->rec_load[i]) * 50) <=
			    max_vol &&
			    (CAP_VPDO_CURRENT(chip->rec_load[i]) * 10) <=
			    max_cur) {
				chip->pos_power = i + 1;
				tmp = CAP_VPDO_VOLTAGE(chip->rec_load[i]);
				chip->pd_output_vol = tmp * 50;
				tmp = CAP_VPDO_CURRENT(chip->rec_load[i]);
				chip->pd_output_cur = tmp * 10;
				pos_find = 1;
			}
			break;
		default:
			/* not meet battery caps */
			break;
		}
		if (pos_find)
			break;
	}
}

static int fusb302_set_pos_power_by_charge_ic(struct fusb30x_chip *chip)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val;
	enum power_supply_property psp;
	int max_vol, max_cur;

	max_vol = 0;
	max_cur = 0;
	psy = power_supply_get_by_phandle(chip->dev->of_node, "charge-dev");
	if (!psy || IS_ERR(psy))
		return -1;

	psp = POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX;
	if (power_supply_get_property(psy, psp, &val) == 0)
		max_vol = val.intval / 1000;

	psp = POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT;
	if (power_supply_get_property(psy, psp, &val) == 0)
		max_cur = val.intval / 1000;

	if (max_vol > 0 && max_cur > 0)
		fusb_set_pos_power(chip, max_vol, max_cur);

	return 0;
}

void fusb_irq_disable(struct fusb30x_chip *chip)
{
	unsigned long irqflags = 0;

	spin_lock_irqsave(&chip->irq_lock, irqflags);
	if (chip->enable_irq) {
		disable_irq_nosync(chip->gpio_int_irq);
		chip->enable_irq = 0;
	} else {
		dev_warn(chip->dev, "irq have already disabled\n");
	}
	spin_unlock_irqrestore(&chip->irq_lock, irqflags);
}

void fusb_irq_enable(struct fusb30x_chip *chip)
{
	unsigned long irqflags = 0;

	spin_lock_irqsave(&chip->irq_lock, irqflags);
	if (!chip->enable_irq) {
		enable_irq(chip->gpio_int_irq);
		chip->enable_irq = 1;
	}
	spin_unlock_irqrestore(&chip->irq_lock, irqflags);
}

static void platform_fusb_notify(struct fusb30x_chip *chip)
{
	bool plugged = 0, flip = 0, dfp = 0, ufp = 0, dp = 0, usb_ss = 0,
	     hpd = 0;
	union extcon_property_value property;

	if (chip->notify.is_cc_connected)
		chip->notify.orientation = chip->cc_polarity + 1;

	/* avoid notify repeated */
	if (memcmp(&chip->notify, &chip->notify_cmp,
		   sizeof(struct notify_info))) {
		dump_notify_info(chip);
		chip->notify.attention = 0;
		memcpy(&chip->notify_cmp, &chip->notify,
		       sizeof(struct notify_info));

		plugged = chip->notify.is_cc_connected ||
			  chip->notify.is_pd_connected;
		flip = chip->notify.orientation ?
		       (chip->notify.orientation - 1) : 0;
		dp = chip->notify.is_enter_mode;

		if (dp) {
			dfp = 1;
			usb_ss = (chip->notify.pin_assignment_def &
				(PIN_MAP_B | PIN_MAP_D | PIN_MAP_F)) ? 1 : 0;
			hpd = GET_DP_STATUS_HPD(chip->notify.dp_status);
		} else if (chip->notify.data_role) {
			dfp = 1;
			usb_ss = 1;
		} else if (plugged) {
			ufp = 1;
			usb_ss = 1;
		}

		property.intval = flip;
		extcon_set_property(chip->extcon, EXTCON_USB,
				    EXTCON_PROP_USB_TYPEC_POLARITY, property);
		extcon_set_property(chip->extcon, EXTCON_USB_HOST,
				    EXTCON_PROP_USB_TYPEC_POLARITY, property);
		extcon_set_property(chip->extcon, EXTCON_DISP_DP,
				    EXTCON_PROP_USB_TYPEC_POLARITY, property);

		property.intval = usb_ss;
		extcon_set_property(chip->extcon, EXTCON_USB,
				    EXTCON_PROP_USB_SS, property);
		extcon_set_property(chip->extcon, EXTCON_USB_HOST,
				    EXTCON_PROP_USB_SS, property);
		extcon_set_property(chip->extcon, EXTCON_DISP_DP,
				    EXTCON_PROP_USB_SS, property);
		extcon_set_state(chip->extcon, EXTCON_USB, ufp);
		extcon_set_state(chip->extcon, EXTCON_USB_HOST, dfp);
		extcon_set_state(chip->extcon, EXTCON_DISP_DP, dp && hpd);
		extcon_sync(chip->extcon, EXTCON_USB);
		extcon_sync(chip->extcon, EXTCON_USB_HOST);
		extcon_sync(chip->extcon, EXTCON_DISP_DP);
		if (chip->notify.power_role == 0 &&
		    chip->notify.is_pd_connected &&
		    chip->pd_output_vol > 0 && chip->pd_output_cur > 0) {
			extcon_set_state(chip->extcon, EXTCON_CHG_USB_FAST, true);
			property.intval =
				(chip->pd_output_cur << 15 |
				 chip->pd_output_vol);
			extcon_set_property(chip->extcon, EXTCON_CHG_USB_FAST,
					    EXTCON_PROP_USB_TYPEC_POLARITY,
					    property);
			extcon_sync(chip->extcon, EXTCON_CHG_USB_FAST);
		}
	}
}

static bool platform_get_device_irq_state(struct fusb30x_chip *chip)
{
	return !gpiod_get_value(chip->gpio_int);
}

static void fusb_timer_start(struct hrtimer *timer, int ms)
{
	ktime_t ktime;

	ktime = ktime_set(0, FUSB_MS_TO_NS(ms));
	hrtimer_start(timer, ktime, HRTIMER_MODE_REL);
}

static void platform_set_vbus_lvl_enable(struct fusb30x_chip *chip, int vbus_5v,
					 int vbus_other)
{
	bool gpio_vbus_value = 0;

	gpio_vbus_value = gpiod_get_value(chip->gpio_vbus_5v);
	if (chip->gpio_vbus_5v) {
		gpiod_set_raw_value(chip->gpio_vbus_5v, vbus_5v);
		/* Only set state here, don't sync notifier to PMIC */
		extcon_set_state(chip->extcon, EXTCON_USB_VBUS_EN, vbus_5v);
	} else {
		extcon_set_state(chip->extcon, EXTCON_USB_VBUS_EN, vbus_5v);
		extcon_sync(chip->extcon, EXTCON_USB_VBUS_EN);
		dev_info(chip->dev, "fusb302 send extcon to %s vbus 5v\n", vbus_5v ? "enable" : "disable");
	}

	if (chip->gpio_vbus_other)
		gpiod_set_raw_value(chip->gpio_vbus_5v, vbus_other);

	if (chip->gpio_discharge && !vbus_5v && gpio_vbus_value) {
		gpiod_set_value(chip->gpio_discharge, 1);
		msleep(20);
		gpiod_set_value(chip->gpio_discharge, 0);
	}
}

static void set_state(struct fusb30x_chip *chip, enum connection_state state)
{
	dev_dbg(chip->dev, "port %d, state %d\n", chip->port_num, state);
	if (!state)
		dev_info(chip->dev, "PD disabled\n");
	chip->conn_state = state;
	chip->sub_state = 0;
	chip->val_tmp = 0;
	chip->work_continue = 1;
}

static int tcpm_get_message(struct fusb30x_chip *chip)
{
	u8 buf[32];
	int len;

	do {
		regmap_raw_read(chip->regmap, FUSB_REG_FIFO, buf, 3);
		chip->rec_head = (buf[1] & 0xff) | ((buf[2] << 8) & 0xff00);

		len = PD_HEADER_CNT(chip->rec_head) << 2;
		regmap_raw_read(chip->regmap, FUSB_REG_FIFO, buf, len + 4);
	/* ignore good_crc message */
	} while (PACKET_IS_CONTROL_MSG(chip->rec_head, CMT_GOODCRC));

	memcpy(chip->rec_load, buf, len);

	return 0;
}

static void fusb302_flush_rx_fifo(struct fusb30x_chip *chip)
{
	regmap_write(chip->regmap, FUSB_REG_CONTROL1, CONTROL1_RX_FLUSH);
}

static int tcpm_get_cc(struct fusb30x_chip *chip, int *CC1, int *CC2)
{
	u32 val;
	int *CC_MEASURE;
	u32 store;

	*CC1 = TYPEC_CC_VOLT_OPEN;
	*CC2 = TYPEC_CC_VOLT_OPEN;

	if (chip->cc_state & 0x01)
		CC_MEASURE = CC1;
	else
		CC_MEASURE = CC2;

	if (chip->cc_state & 0x04) {
		regmap_read(chip->regmap, FUSB_REG_SWITCHES0, &store);
		/* measure cc1 first */
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2 |
				   SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2 |
				   SWITCHES0_PDWN1 | SWITCHES0_PDWN2,
				   SWITCHES0_PDWN1 | SWITCHES0_PDWN2 |
				   SWITCHES0_MEAS_CC1);
		usleep_range(250, 300);

		regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);
		val &= STATUS0_BC_LVL;
		if (val)
			*CC1 = val;

		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2 |
				   SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2 |
				   SWITCHES0_PDWN1 | SWITCHES0_PDWN2,
				   SWITCHES0_PDWN1 | SWITCHES0_PDWN2 |
				   SWITCHES0_MEAS_CC2);
		usleep_range(250, 300);

		regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);
		val &= STATUS0_BC_LVL;
		if (val)
			*CC2 = val;
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2,
				   store);
	} else {
		regmap_read(chip->regmap, FUSB_REG_SWITCHES0, &store);
		val = store;
		val &= ~(SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2 |
				SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2);
		if (chip->cc_state & 0x01) {
			val |= SWITCHES0_MEAS_CC1 | SWITCHES0_PU_EN1;
		} else {
			val |= SWITCHES0_MEAS_CC2 | SWITCHES0_PU_EN2;
		}
		regmap_write(chip->regmap, FUSB_REG_SWITCHES0, val);

		regmap_write(chip->regmap, FUSB_REG_MEASURE, chip->cc_meas_high);
		usleep_range(250, 300);

		regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);
		if (val & STATUS0_COMP) {
			int retry = 3;
			int comp_times = 0;

			while (retry--) {
				regmap_write(chip->regmap, FUSB_REG_MEASURE, chip->cc_meas_high);
				usleep_range(250, 300);
				regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);
				if (val & STATUS0_COMP) {
					comp_times++;
					if (comp_times == 3) {
						*CC_MEASURE = TYPEC_CC_VOLT_OPEN;
						regmap_write(chip->regmap, FUSB_REG_SWITCHES0, store);
					}
				}
			}
		} else {
			regmap_write(chip->regmap, FUSB_REG_MEASURE, chip->cc_meas_low);
			regmap_read(chip->regmap, FUSB_REG_MEASURE, &val);
			usleep_range(250, 300);

			regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);

			if (val & STATUS0_COMP)
				*CC_MEASURE = TYPEC_CC_VOLT_RD;
			else
				*CC_MEASURE = TYPEC_CC_VOLT_RA;
			regmap_write(chip->regmap, FUSB_REG_SWITCHES0, store);
		}
	}

	return 0;
}

static int tcpm_set_cc(struct fusb30x_chip *chip, int mode)
{
	u8 val = 0, mask;

	val &= ~(SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2 |
		 SWITCHES0_PDWN1 | SWITCHES0_PDWN2);

	mask = ~val;

	switch (mode) {
	case FUSB_MODE_DFP:
		if (chip->togdone_pullup)
			val |= SWITCHES0_PU_EN2;
		else
			val |= SWITCHES0_PU_EN1;
		break;
	case FUSB_MODE_UFP:
		val |= SWITCHES0_PDWN1 | SWITCHES0_PDWN2;
		break;
	case FUSB_MODE_DRP:
		val |= SWITCHES0_PDWN1 | SWITCHES0_PDWN2;
		break;
	case FUSB_MODE_ASS:
		break;
	}

	regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0, mask, val);
	return 0;
}

static int tcpm_set_rx_enable(struct fusb30x_chip *chip, int enable)
{
	u8 val = 0;

	if (enable) {
		if (chip->cc_polarity)
			val |= SWITCHES0_MEAS_CC2;
		else
			val |= SWITCHES0_MEAS_CC1;
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2,
				   val);
		fusb302_flush_rx_fifo(chip);
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES1,
				   SWITCHES1_AUTO_CRC, SWITCHES1_AUTO_CRC);
	} else {
		/*
		 * bit of a hack here.
		 * when this function is called to disable rx (enable=0)
		 * using it as an indication of detach (gulp!)
		 * to reset our knowledge of where
		 * the toggle state machine landed.
		 */
		chip->togdone_pullup = 0;

#ifdef	FUSB_HAVE_DRP
		tcpm_set_cc(chip, FUSB_MODE_DRP);
		regmap_update_bits(chip->regmap, FUSB_REG_CONTROL2,
				   CONTROL2_TOG_RD_ONLY,
				   CONTROL2_TOG_RD_ONLY);
#endif
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2,
				   0);
		regmap_update_bits(chip->regmap,
				   FUSB_REG_SWITCHES1, SWITCHES1_AUTO_CRC, 0);
	}

	return 0;
}

static int tcpm_set_msg_header(struct fusb30x_chip *chip)
{
	regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES1,
			   SWITCHES1_POWERROLE | SWITCHES1_DATAROLE,
			   (chip->notify.power_role << 7) |
			   (chip->notify.data_role << 4));
	regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES1,
			   SWITCHES1_SPECREV, 2 << 5);
	return 0;
}

static int tcpm_set_polarity(struct fusb30x_chip *chip, bool polarity)
{
	u8 val = 0;

#ifdef FUSB_VCONN_SUPPORT
	if (chip->vconn_enabled) {
		if (polarity)
			val |= SWITCHES0_VCONN_CC1;
		else
			val |= SWITCHES0_VCONN_CC2;
	}
#endif

	if (polarity)
		val |= SWITCHES0_MEAS_CC2;
	else
		val |= SWITCHES0_MEAS_CC1;

	regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
			   SWITCHES0_VCONN_CC1 | SWITCHES0_VCONN_CC2 |
			   SWITCHES0_MEAS_CC1 | SWITCHES0_MEAS_CC2,
			   val);

	val = 0;
	if (polarity)
		val |= SWITCHES1_TXCC2;
	else
		val |= SWITCHES1_TXCC1;
	regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES1,
			   SWITCHES1_TXCC1 | SWITCHES1_TXCC2,
			   val);

	chip->cc_polarity = polarity;

	return 0;
}

static int tcpm_set_vconn(struct fusb30x_chip *chip, int enable)
{
	u8 val = 0;

	if (enable) {
		tcpm_set_polarity(chip, chip->cc_polarity);
	} else {
		val &= ~(SWITCHES0_VCONN_CC1 | SWITCHES0_VCONN_CC2);
		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_VCONN_CC1 | SWITCHES0_VCONN_CC2,
				   val);
	}
	chip->vconn_enabled = enable;
	return 0;
}

static void fusb302_pd_reset(struct fusb30x_chip *chip)
{
	regmap_write(chip->regmap, FUSB_REG_RESET, RESET_PD_RESET);
	regmap_reinit_cache(chip->regmap, &fusb302_regmap_config);
}

static void tcpm_select_rp_value(struct fusb30x_chip *chip, u32 rp)
{
	u32 control0_reg;

	regmap_read(chip->regmap, FUSB_REG_CONTROL0, &control0_reg);

	control0_reg &= ~CONTROL0_HOST_CUR;
	/*
	 * according to the host current, the compare value is different
	*/
	switch (rp) {
	/* host pull up current is 80ua , high voltage is 1.596v, low is 0.21v */
	case TYPEC_RP_USB:
		chip->cc_meas_high = 0x26;
		chip->cc_meas_low = 0x5;
		control0_reg |= CONTROL0_HOST_CUR_USB;
		break;
	/* host pull up current is 180ua , high voltage is 1.596v, low is 0.42v */
	case TYPEC_RP_1A5:
		chip->cc_meas_high = 0x26;
		chip->cc_meas_low = 0xa;
		control0_reg |= CONTROL0_HOST_CUR_1A5;
		break;
	/* host pull up current is 330ua , high voltage is 2.604v, low is 0.798v*/
	case TYPEC_RP_3A0:
		chip->cc_meas_high = 0x26;
		chip->cc_meas_low = 0x13;
		control0_reg |= CONTROL0_HOST_CUR_3A0;
		break;
	default:
		chip->cc_meas_high = 0x26;
		chip->cc_meas_low = 0xa;
		control0_reg |= CONTROL0_HOST_CUR_1A5;
		break;
	}

	regmap_write(chip->regmap, FUSB_REG_CONTROL0, control0_reg);
}

static int tcpm_check_vbus(struct fusb30x_chip *chip)
{
	u32 val;

	/* Read status register */
	regmap_read(chip->regmap, FUSB_REG_STATUS0, &val);

	return (val & STATUS0_VBUSOK) ? 1 : 0;
}

static void tcpm_init(struct fusb30x_chip *chip)
{
	u8 val;
	u32 tmp;

	regmap_read(chip->regmap, FUSB_REG_DEVICEID, &tmp);
	chip->chip_id = (u8)tmp;
	platform_set_vbus_lvl_enable(chip, 0, 0);
	chip->notify.is_cc_connected = 0;
	chip->cc_state = 0;

	/* restore default settings */
	regmap_update_bits(chip->regmap, FUSB_REG_RESET, RESET_SW_RESET,
			   RESET_SW_RESET);
	fusb302_pd_reset(chip);
	/* set auto_retry and number of retries */
	regmap_update_bits(chip->regmap, FUSB_REG_CONTROL3,
			   CONTROL3_AUTO_RETRY | CONTROL3_N_RETRIES,
			   CONTROL3_AUTO_RETRY | CONTROL3_N_RETRIES),

	/* set interrupts */
	val = 0xff;
	val &= ~(MASK_M_BC_LVL | MASK_M_COLLISION | MASK_M_ALERT |
		 MASK_M_VBUSOK);
	regmap_write(chip->regmap, FUSB_REG_MASK, val);

	val = 0xff;
	val &= ~(MASKA_M_TOGDONE | MASKA_M_RETRYFAIL | MASKA_M_HARDSENT |
		 MASKA_M_TXSENT | MASKA_M_HARDRST);
	regmap_write(chip->regmap, FUSB_REG_MASKA, val);

	val = 0xff;
	val = ~MASKB_M_GCRCSEND;
	regmap_write(chip->regmap, FUSB_REG_MASKB, val);

#ifdef	FUSB_HAVE_DRP
		regmap_update_bits(chip->regmap, FUSB_REG_CONTROL2,
				   CONTROL2_MODE | CONTROL2_TOGGLE,
				   (1 << 1) | CONTROL2_TOGGLE);

		regmap_update_bits(chip->regmap, FUSB_REG_CONTROL2,
				   CONTROL2_TOG_RD_ONLY,
				   CONTROL2_TOG_RD_ONLY);
#endif

	tcpm_select_rp_value(chip, TYPEC_RP_1A5);
	/* Interrupts Enable */
	regmap_update_bits(chip->regmap, FUSB_REG_CONTROL0, CONTROL0_INT_MASK,
			   ~CONTROL0_INT_MASK);

	tcpm_set_polarity(chip, 0);
	tcpm_set_vconn(chip, 0);

	regmap_write(chip->regmap, FUSB_REG_POWER, 0xf);
	chip->vbus_begin = tcpm_check_vbus(chip);
}

static void pd_execute_hard_reset(struct fusb30x_chip *chip)
{
	chip->msg_id = 0;
	chip->vdm_state = 0;
	if (chip->notify.power_role)
		set_state(chip, policy_src_transition_default);
	else
		set_state(chip, policy_snk_transition_default);
}

static void tcpc_alert(struct fusb30x_chip *chip, int *evt)
{
	int interrupt, interrupta, interruptb;
	u32 val;
	static int retry;

	regmap_read(chip->regmap, FUSB_REG_INTERRUPT, &interrupt);
	regmap_read(chip->regmap, FUSB_REG_INTERRUPTA, &interrupta);
	regmap_read(chip->regmap, FUSB_REG_INTERRUPTB, &interruptb);

	if (interrupt & INTERRUPT_BC_LVL) {
		if (chip->notify.is_cc_connected)
			*evt |= EVENT_CC;
	}

	if (interrupt & INTERRUPT_VBUSOK) {
		if (chip->notify.is_cc_connected)
			*evt |= EVENT_CC;
	}

	if (interrupta & INTERRUPTA_TOGDONE) {
		*evt |= EVENT_CC;
		regmap_read(chip->regmap, FUSB_REG_STATUS1A, &val);
		chip->cc_state = ((u8)val >> 3) & 0x07;

		regmap_update_bits(chip->regmap, FUSB_REG_CONTROL2,
				   CONTROL2_TOGGLE,
				   0);

		val &= ~(SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2 |
			 SWITCHES0_PDWN1 | SWITCHES0_PDWN2);

		if (chip->cc_state & 0x01)
			val |= SWITCHES0_PU_EN1;
		else
			val |= SWITCHES0_PU_EN2;

		regmap_update_bits(chip->regmap, FUSB_REG_SWITCHES0,
				   SWITCHES0_PU_EN1 | SWITCHES0_PU_EN2 |
				   SWITCHES0_PDWN1 | SWITCHES0_PDWN2,
				   val);
	}

	if (interrupta & INTERRUPTA_TXSENT) {
		*evt |= EVENT_TX;
		chip->tx_state = tx_success;
	}

	if (interruptb & INTERRUPTB_GCRCSENT)
		*evt |= EVENT_RX;

	if (interrupta & INTERRUPTA_HARDRST) {
		fusb302_pd_reset(chip);
		pd_execute_hard_reset(chip);
		*evt |= EVENT_REC_RESET;
	}

	if (interrupta & INTERRUPTA_RETRYFAIL) {
		*evt |= EVENT_TX;
		chip->tx_state = tx_failed;
	}

	if (interrupta & INTERRUPTA_HARDSENT) {
		/*
		 * The fusb PD should be reset once to sync adapter PD
		 * signal after fusb sent hard reset cmd.This is not PD
		 * device if reset failed.
		 */
		if (!retry) {
			retry = 1;
			fusb302_pd_reset(chip);
			pd_execute_hard_reset(chip);
		} else {
			retry = 0;
			chip->tx_state = tx_success;
			chip->timer_state = T_DISABLED;
			*evt |= EVENT_TX;
		}
	}
}

static void mux_alert(struct fusb30x_chip *chip, int *evt)
{
	if (!chip->timer_mux) {
		*evt |= EVENT_TIMER_MUX;
		chip->timer_mux = T_DISABLED;
	}

	if (!chip->timer_state) {
		*evt |= EVENT_TIMER_STATE;
		chip->timer_state = T_DISABLED;
	}

	if (chip->work_continue) {
		*evt |= EVENT_WORK_CONTINUE;
		chip->work_continue = 0;
	}
}

static void set_state_unattached(struct fusb30x_chip *chip)
{
	dev_info(chip->dev, "connection has disconnected\n");
	tcpm_init(chip);
	tcpm_set_rx_enable(chip, 0);
	chip->conn_state = unattached;
	tcpm_set_cc(chip, FUSB_MODE_DRP);

	/* claer notify_info */
	memset(&chip->notify, 0, sizeof(struct notify_info));
	platform_fusb_notify(chip);

	if (chip->gpio_discharge)
		gpiod_set_value(chip->gpio_discharge, 1);
	msleep(100);
	if (chip->gpio_discharge)
		gpiod_set_value(chip->gpio_discharge, 0);
}

static void set_mesg(struct fusb30x_chip *chip, int cmd, int is_DMT)
{
	int i;
	struct PD_CAP_INFO *pd_cap_info = &chip->pd_cap_info;

	chip->send_head = ((chip->msg_id & 0x7) << 9) |
			 ((chip->notify.power_role & 0x1) << 8) |
			 (1 << 6) |
			 ((chip->notify.data_role & 0x1) << 5);

	if (is_DMT) {
		switch (cmd) {
		case DMT_SOURCECAPABILITIES:
			chip->send_head |= ((chip->n_caps_used & 0x3) << 12) | (cmd & 0xf);

			for (i = 0; i < chip->n_caps_used; i++) {
				chip->send_load[i] = (pd_cap_info->supply_type << 30) |
						    (pd_cap_info->dual_role_power << 29) |
						    (pd_cap_info->usb_suspend_support << 28) |
						    (pd_cap_info->externally_powered << 27) |
						    (pd_cap_info->usb_communications_cap << 26) |
						    (pd_cap_info->data_role_swap << 25) |
						    (pd_cap_info->peak_current << 20) |
						    (chip->source_power_supply[i] << 10) |
						    (chip->source_max_current[i]);
			}
			break;
		case DMT_REQUEST:
			chip->send_head |= ((1 << 12) | (cmd & 0xf));
			/* send request with FVRDO */
			chip->send_load[0] = (chip->pos_power << 28) |
					    (0 << 27) |
					    (1 << 26) |
					    (0 << 25) |
					    (0 << 24);

			switch (CAP_POWER_TYPE(chip->rec_load[chip->pos_power - 1])) {
			case 0:
				/* Fixed Supply */
				chip->send_load[0] |= ((CAP_FPDO_VOLTAGE(chip->rec_load[chip->pos_power - 1]) << 10) & 0x3ff);
				chip->send_load[0] |= (CAP_FPDO_CURRENT(chip->rec_load[chip->pos_power - 1]) & 0x3ff);
				break;
			case 1:
				/* Battery */
				chip->send_load[0] |= ((CAP_VPDO_VOLTAGE(chip->rec_load[chip->pos_power - 1]) << 10) & 0x3ff);
				chip->send_load[0] |= (CAP_VPDO_CURRENT(chip->rec_load[chip->pos_power - 1]) & 0x3ff);
				break;
			default:
				/* not meet battery caps */
				break;
			}
			break;
		case DMT_SINKCAPABILITIES:
			break;
		case DMT_VENDERDEFINED:
			break;
		default:
			break;
		}
	} else {
		chip->send_head |= (cmd & 0xf);
	}
}

static void set_vdm_mesg(struct fusb30x_chip *chip, int cmd, int type, int mode)
{
	chip->send_head = (chip->msg_id & 0x7) << 9;
	chip->send_head |= (chip->notify.power_role & 0x1) << 8;

	chip->send_head = ((chip->msg_id & 0x7) << 9) |
			 ((chip->notify.power_role & 0x1) << 8) |
			 (1 << 6) |
			 ((chip->notify.data_role & 0x1) << 5) |
			 (DMT_VENDERDEFINED & 0xf);

	chip->send_load[0] = (1 << 15) |
			    (0 << 13) |
			    (type << 6) |
			    (cmd);

	switch (cmd) {
	case VDM_DISCOVERY_ID:
	case VDM_DISCOVERY_SVIDS:
	case VDM_ATTENTION:
		chip->send_load[0] |= (0xff00 << 16);
		chip->send_head |= (1 << 12);
		break;
	case VDM_DISCOVERY_MODES:
		chip->send_load[0] |=
			(chip->vdm_svid[chip->val_tmp >> 1] << 16);
		chip->send_head |= (1 << 12);
		break;
	case VDM_ENTER_MODE:
		chip->send_head |= (1 << 12);
		chip->send_load[0] |= (mode << 8) | (0xff01 << 16);
		break;
	case VDM_EXIT_MODE:
		chip->send_head |= (1 << 12);
		chip->send_load[0] |= (0x0f << 8) | (0xff01 << 16);
		break;
	case VDM_DP_STATUS_UPDATE:
		chip->send_head |= (2 << 12);
		chip->send_load[0] |= (1 << 8) | (0xff01 << 16);
		chip->send_load[1] = 5;
		break;
	case VDM_DP_CONFIG:
		chip->send_head |= (2 << 12);
		chip->send_load[0] |= (1 << 8) | (0xff01 << 16);
		chip->send_load[1] = (chip->notify.pin_assignment_def << 8) |
				    (1 << 2) | 2;
		break;
	default:
		break;
	}
}

static enum tx_state policy_send_hardrst(struct fusb30x_chip *chip, int evt)
{
	switch (chip->tx_state) {
	case 0:
		regmap_update_bits(chip->regmap, FUSB_REG_CONTROL3,
				   CONTROL3_SEND_HARDRESET,
				   CONTROL3_SEND_HARDRESET);
		chip->tx_state = tx_busy;
		chip->timer_state = T_BMC_TIMEOUT;
		fusb_timer_start(&chip->timer_state_machine,
				 chip->timer_state);
		break;
	default:
		if (evt & EVENT_TIMER_STATE)
			chip->tx_state = tx_success;
		break;
	}
	return chip->tx_state;
}

static enum tx_state policy_send_data(struct fusb30x_chip *chip)
{
	u8 senddata[40];
	int pos = 0;
	u8 len;

	switch (chip->tx_state) {
	case 0:
		senddata[pos++] = FUSB_TKN_SYNC1;
		senddata[pos++] = FUSB_TKN_SYNC1;
		senddata[pos++] = FUSB_TKN_SYNC1;
		senddata[pos++] = FUSB_TKN_SYNC2;

		len = PD_HEADER_CNT(chip->send_head) << 2;
		senddata[pos++] = FUSB_TKN_PACKSYM | ((len + 2) & 0x1f);

		senddata[pos++] = chip->send_head & 0xff;
		senddata[pos++] = (chip->send_head >> 8) & 0xff;

		memcpy(&senddata[pos], chip->send_load, len);
		pos += len;

		senddata[pos++] = FUSB_TKN_JAMCRC;
		senddata[pos++] = FUSB_TKN_EOP;
		senddata[pos++] = FUSB_TKN_TXOFF;
		senddata[pos++] = FUSB_TKN_TXON;

		regmap_raw_write(chip->regmap, FUSB_REG_FIFO, senddata, pos);
		chip->tx_state = tx_busy;
		break;

	default:
		/* wait Tx result */
		break;
	}

	return chip->tx_state;
}

static void process_vdm_msg(struct fusb30x_chip *chip)
{
	u32 vdm_header = chip->rec_load[0];
	int i;
	u32 tmp;

	/* can't procee unstructed vdm msg */
	if (!GET_VDMHEAD_STRUCT_TYPE(vdm_header))
		return;

	switch (GET_VDMHEAD_CMD_TYPE(vdm_header)) {
	case VDM_TYPE_INIT:
		switch (GET_VDMHEAD_CMD(vdm_header)) {
		case VDM_ATTENTION:
			chip->notify.dp_status = GET_DP_STATUS(chip->rec_load[1]);
			dev_info(chip->dev, "attention, dp_status %x\n",
				 chip->rec_load[1]);
			chip->notify.attention = 1;
			chip->vdm_state = 6;
			platform_fusb_notify(chip);
			break;
		default:
			dev_warn(chip->dev, "rec unknown init vdm msg\n");
			break;
		}
		break;
	case VDM_TYPE_ACK:
		switch (GET_VDMHEAD_CMD(vdm_header)) {
		case VDM_DISCOVERY_ID:
			chip->vdm_id = chip->rec_load[1];
			break;
		case VDM_DISCOVERY_SVIDS:
			for (i = 0; i < 6; i++) {
				tmp = (chip->rec_load[i + 1] >> 16) &
				      0x0000ffff;
				if (tmp) {
					chip->vdm_svid[i * 2] = tmp;
					chip->vdm_svid_num++;
				} else {
					break;
				}

				tmp = (chip->rec_load[i + 1] & 0x0000ffff);
				if (tmp) {
					chip->vdm_svid[i * 2 + 1] = tmp;
					chip->vdm_svid_num++;
				} else {
					break;
				}
			}
			break;
		case VDM_DISCOVERY_MODES:
			/* indicate there are some vdo modes */
			if (PD_HEADER_CNT(chip->rec_head) > 1) {
				/*
				 * store mode config,
				 * enter first mode default
				 */
				if (!((chip->rec_load[1] >> 8) & 0x3f)) {
					chip->val_tmp |= 1;
					break;
				}
				chip->notify.pin_assignment_support = 0;
				chip->notify.pin_assignment_def = 0;
				chip->notify.pin_assignment_support =
					(chip->rec_load[1] >> 8) & 0x3f;
				tmp = chip->notify.pin_assignment_support;
				for (i = 0; i < 6; i++) {
					if (!(tmp & 0x20))
						tmp = tmp << 1;
					else
						break;
				}
				chip->notify.pin_assignment_def = 0x20 >> i;
				chip->val_tmp |= 1;
			}
			break;
		case VDM_ENTER_MODE:
			chip->val_tmp = 1;
			break;
		case VDM_DP_STATUS_UPDATE:
			chip->notify.dp_status = GET_DP_STATUS(chip->rec_load[1]);
			dev_dbg(chip->dev, "dp_status 0x%x\n",
				chip->rec_load[1]);
			chip->val_tmp = 1;
			break;
		case VDM_DP_CONFIG:
			chip->val_tmp = 1;
			dev_info(chip->dev,
				 "DP config successful, pin_assignment 0x%x\n",
				 chip->notify.pin_assignment_def);
			chip->notify.is_enter_mode = 1;
			break;
		default:
			break;
		}
		break;
	case VDM_TYPE_NACK:
			dev_warn(chip->dev, "REC NACK for 0x%x\n",
				 GET_VDMHEAD_CMD(vdm_header));
			/* disable vdm */
			chip->vdm_state = 0xff;
		break;
	}
}

static int vdm_send_discoveryid(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	switch (chip->vdm_send_state) {
	case 0:
		set_vdm_mesg(chip, VDM_DISCOVERY_ID, VDM_TYPE_INIT, 0);
		chip->vdm_id = 0;
		chip->tx_state = 0;
		chip->vdm_send_state++;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->vdm_send_state++;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			dev_warn(chip->dev, "VDM_DISCOVERY_ID send failed\n");
			/* disable auto_vdm_machine */
			chip->vdm_state = 0xff;
		}

		if (chip->vdm_send_state != 2)
			break;
	default:
		if (chip->vdm_id) {
			chip->vdm_send_state = 0;
			return 0;
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "VDM_DISCOVERY_ID time out\n");
			chip->vdm_state = 0xff;
			chip->work_continue = 1;
		}
		break;
	}
	return -EINPROGRESS;
}

static int vdm_send_discoverysvid(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	switch (chip->vdm_send_state) {
	case 0:
		set_vdm_mesg(chip, VDM_DISCOVERY_SVIDS, VDM_TYPE_INIT, 0);
		memset(chip->vdm_svid, 0, sizeof(chip->vdm_svid));
		chip->vdm_svid_num = 0;
		chip->tx_state = 0;
		chip->vdm_send_state++;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->vdm_send_state++;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			dev_warn(chip->dev, "VDM_DISCOVERY_SVIDS send failed\n");
			/* disable auto_vdm_machine */
			chip->vdm_state = 0xff;
		}

		if (chip->vdm_send_state != 2)
			break;
	default:
		if (chip->vdm_svid_num) {
			chip->vdm_send_state = 0;
			return 0;
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "VDM_DISCOVERY_SVIDS time out\n");
			chip->vdm_state = 0xff;
			chip->work_continue = 1;
		}
		break;
	}
	return -EINPROGRESS;
}

static int vdm_send_discoverymodes(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	if ((chip->val_tmp >> 1) != chip->vdm_svid_num) {
		switch (chip->vdm_send_state) {
		case 0:
			set_vdm_mesg(chip, VDM_DISCOVERY_MODES,
				     VDM_TYPE_INIT, 0);
			chip->tx_state = 0;
			chip->vdm_send_state++;
		case 1:
			tmp = policy_send_data(chip);
			if (tmp == tx_success) {
				chip->vdm_send_state++;
				chip->timer_state = T_SENDER_RESPONSE;
				fusb_timer_start(&chip->timer_state_machine,
						 chip->timer_state);
			} else if (tmp == tx_failed) {
				dev_warn(chip->dev,
					 "VDM_DISCOVERY_MODES send failed\n");
				chip->vdm_state = 0xff;
			}

			if (chip->vdm_send_state != 2)
				break;
		default:
			if (chip->val_tmp & 1) {
				chip->val_tmp &= 0xfe;
				chip->val_tmp += 2;
				chip->vdm_send_state = 0;
				chip->work_continue = 1;
			} else if (evt & EVENT_TIMER_STATE) {
				dev_warn(chip->dev,
					 "VDM_DISCOVERY_MODES time out\n");
				chip->vdm_state = 0xff;
				chip->work_continue = 1;
			}
			break;
		}
	} else {
		chip->val_tmp = 0;
		return 0;
	}

	return -EINPROGRESS;
}

static int vdm_send_entermode(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	switch (chip->vdm_send_state) {
	case 0:
		set_vdm_mesg(chip, VDM_ENTER_MODE, VDM_TYPE_INIT, 1);
		chip->tx_state = 0;
		chip->vdm_send_state++;
		chip->notify.is_enter_mode = 0;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->vdm_send_state++;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			dev_warn(chip->dev, "VDM_ENTER_MODE send failed\n");
			/* disable auto_vdm_machine */
			chip->vdm_state = 0xff;
		}

		if (chip->vdm_send_state != 2)
			break;
	default:
		if (chip->val_tmp) {
			chip->val_tmp = 0;
			chip->vdm_send_state = 0;
			return 0;
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "VDM_ENTER_MODE time out\n");
			chip->vdm_state = 0xff;
			chip->work_continue = 1;
		}
		break;
	}
	return -EINPROGRESS;
}

static int vdm_send_getdpstatus(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	switch (chip->vdm_send_state) {
	case 0:
		set_vdm_mesg(chip, VDM_DP_STATUS_UPDATE, VDM_TYPE_INIT, 1);
		chip->tx_state = 0;
		chip->vdm_send_state++;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->vdm_send_state++;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			dev_warn(chip->dev,
				 "VDM_DP_STATUS_UPDATE send failed\n");
			/* disable auto_vdm_machine */
			chip->vdm_state = 0xff;
		}

		if (chip->vdm_send_state != 2)
			break;
	default:
		if (chip->val_tmp) {
			chip->val_tmp = 0;
			chip->vdm_send_state = 0;
			return 0;
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "VDM_DP_STATUS_UPDATE time out\n");
			chip->vdm_state = 0xff;
			chip->work_continue = 1;
		}
		break;
	}
	return -EINPROGRESS;
}

static int vdm_send_dpconfig(struct fusb30x_chip *chip, int evt)
{
	int tmp;

	switch (chip->vdm_send_state) {
	case 0:
		set_vdm_mesg(chip, VDM_DP_CONFIG, VDM_TYPE_INIT, 0);
		chip->tx_state = 0;
		chip->vdm_send_state++;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->vdm_send_state++;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			dev_warn(chip->dev, "vdm_send_dpconfig send failed\n");
			/* disable auto_vdm_machine */
			chip->vdm_state = 0xff;
		}

		if (chip->vdm_send_state != 2)
			break;
	default:
		if (chip->val_tmp) {
			chip->val_tmp = 0;
			chip->vdm_send_state = 0;
			return 0;
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "vdm_send_dpconfig time out\n");
			chip->vdm_state = 0xff;
			chip->work_continue = 1;
		}
		break;
	}
	return -EINPROGRESS;
}

static void auto_vdm_machine(struct fusb30x_chip *chip, int evt)
{
	switch (chip->vdm_state) {
	case 0:
		if (vdm_send_discoveryid(chip, evt))
			break;
		chip->vdm_state++;
		/* without break */
	case 1:
		if (vdm_send_discoverysvid(chip, evt))
			break;
		chip->vdm_state++;
		/* without break */
	case 2:
		if (vdm_send_discoverymodes(chip, evt))
			break;
		chip->vdm_state++;
		/* without break */
	case 3:
		if (vdm_send_entermode(chip, evt))
			break;
		chip->vdm_state++;
		/* without break */
	case 4:
		if (vdm_send_dpconfig(chip, evt))
			break;
		chip->vdm_state = 6;
		/* without break */
	case 5:
		if (vdm_send_getdpstatus(chip, evt))
			break;
		chip->vdm_state++;
		/* without break */
	default:
		platform_fusb_notify(chip);
		break;
	}
}

static void fusb_state_disabled(struct fusb30x_chip *chip, int evt)
{
	/* Do nothing */
}

static void fusb_state_unattached(struct fusb30x_chip *chip, int evt)
{
	chip->notify.is_cc_connected = 0;
	chip->is_pd_support = 0;

	if ((evt & EVENT_CC) && chip->cc_state) {
		if (chip->cc_state & 0x04)
			set_state(chip, attach_wait_sink);
		else
			set_state(chip, attach_wait_source);

		tcpm_get_cc(chip, &chip->cc1, &chip->cc2);
		chip->debounce_cnt = 0;
		chip->timer_mux = 2;
		fusb_timer_start(&chip->timer_mux_machine, chip->timer_mux);
	}
}

static void fusb_state_attach_wait_sink(struct fusb30x_chip *chip, int evt)
{
	int cc1, cc2;

	if (evt & EVENT_TIMER_MUX) {
		tcpm_get_cc(chip, &cc1, &cc2);

		if ((chip->cc1 == cc1) && (chip->cc2 == cc2)) {
			chip->debounce_cnt++;
		} else {
			chip->cc1 = cc1;
			chip->cc2 = cc2;
			chip->debounce_cnt = 0;
		}

		if (chip->debounce_cnt > N_DEBOUNCE_CNT) {
			if ((chip->cc1 != chip->cc2) &&
			    ((!chip->cc1) || (!chip->cc2))) {
				set_state(chip, attached_sink);
			} else {
				set_state_unattached(chip);
			}
			return;
		}

		chip->timer_mux = 2;
		fusb_timer_start(&chip->timer_mux_machine,
				 chip->timer_mux);
	}
}

static void fusb_state_attach_wait_source(struct fusb30x_chip *chip, int evt)
{
	int cc1, cc2;

	if (evt & EVENT_TIMER_MUX) {
		tcpm_get_cc(chip, &cc1, &cc2);

		if ((chip->cc1 == cc1) && (chip->cc2 == cc2)) {
			if (chip->debounce_cnt++ == 0)
				platform_set_vbus_lvl_enable(chip, 1, 0);
		} else {
			chip->cc1 = cc1;
			chip->cc2 = cc2;
			chip->debounce_cnt = 0;
		}

		if (chip->debounce_cnt > N_DEBOUNCE_CNT) {
			if (((!chip->cc1) || (!chip->cc2)) &&
			    ((chip->cc1 == TYPEC_CC_VOLT_RD) ||
			     (chip->cc2 == TYPEC_CC_VOLT_RD))) {
				set_state(chip, attached_source);
			} else {
				set_state_unattached(chip);
			}

			return;
		}

		chip->timer_mux = 2;
		fusb_timer_start(&chip->timer_mux_machine,
				 chip->timer_mux);
	}
}

static void fusb_state_attached_source(struct fusb30x_chip *chip, int evt)
{
	tcpm_set_polarity(chip, !(chip->cc_state & 0x01));
	tcpm_set_vconn(chip, 1);

	chip->notify.is_cc_connected = 1;
	if (chip->cc_state & 0x01)
		chip->cc_polarity = 0;
	else
		chip->cc_polarity = 1;

	chip->notify.power_role = 1;
	chip->notify.data_role = 1;
	chip->hardrst_count = 0;
	set_state(chip, policy_src_startup);
	dev_info(chip->dev, "CC connected in %d as DFP\n", chip->cc_polarity);
}

static void fusb_state_attached_sink(struct fusb30x_chip *chip, int evt)
{
	chip->notify.is_cc_connected = 1;
	if (chip->cc_state & 0x01)
		chip->cc_polarity = 0;
	else
		chip->cc_polarity = 1;

	chip->notify.power_role = 0;
	chip->notify.data_role = 0;
	chip->hardrst_count = 0;
	set_state(chip, policy_snk_startup);
	dev_info(chip->dev, "CC connected in %d as UFP\n", chip->cc_polarity);
}

static void fusb_soft_reset_parameter(struct fusb30x_chip *chip)
{
	chip->caps_counter = 0;
	chip->msg_id = 0;
	chip->vdm_state = 0;
	chip->vdm_substate = 0;
	chip->vdm_send_state = 0;
	chip->val_tmp = 0;
	chip->pos_power = 0;
}

static void fusb_state_src_startup(struct fusb30x_chip *chip, int evt)
{
	chip->notify.is_pd_connected = 0;
	fusb_soft_reset_parameter(chip);

	memset(chip->partner_cap, 0, sizeof(chip->partner_cap));

	tcpm_set_msg_header(chip);
	tcpm_set_polarity(chip, chip->cc_polarity);
	tcpm_set_rx_enable(chip, 1);

	set_state(chip, policy_src_send_caps);
	platform_fusb_notify(chip);
}

static void fusb_state_src_discovery(struct fusb30x_chip *chip, int evt)
{
	switch (chip->sub_state) {
	case 0:
		chip->caps_counter++;

		if (chip->caps_counter < N_CAPS_COUNT) {
			chip->timer_state = T_TYPEC_SEND_SOURCECAP;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
			chip->sub_state = 1;
		} else {
			set_state(chip, disabled);
		}
		break;
	default:
		if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_src_send_caps);
		} else if (evt & EVENT_TIMER_MUX) {
			if (!chip->is_pd_support)
				set_state(chip, disabled);
			else if (chip->hardrst_count > N_HARDRESET_COUNT)
				set_state(chip, error_recovery);
			else
				set_state(chip, policy_src_send_hardrst);
		}
		break;
	}
}

static void fusb_state_src_send_caps(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, DMT_SOURCECAPABILITIES, DATAMESSAGE);
		chip->sub_state = 1;
		chip->tx_state = tx_idle;
		/* without break */
	case 1:
		tmp = policy_send_data(chip);

		if (tmp == tx_success) {
			chip->hardrst_count = 0;
			chip->caps_counter = 0;
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
			chip->timer_mux = T_DISABLED;
			chip->sub_state++;
			chip->is_pd_support = 1;
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_discovery);
			break;
		}

		if (!(evt & FLAG_EVENT))
			break;
	default:
		if (evt & EVENT_RX) {
			if (PACKET_IS_DATA_MSG(chip->rec_head, DMT_REQUEST)) {
				set_state(chip, policy_src_negotiate_cap);
			} else {
				set_state(chip, policy_src_send_softrst);
			}
		} else if (evt & EVENT_TIMER_STATE) {
			if (chip->hardrst_count <= N_HARDRESET_COUNT)
				set_state(chip, policy_src_send_hardrst);
			else
				set_state(chip, disabled);
		} else if (evt & EVENT_TIMER_MUX) {
			if (!chip->is_pd_support)
				set_state(chip, disabled);
			else if (chip->hardrst_count > N_HARDRESET_COUNT)
				set_state(chip, error_recovery);
			else
				set_state(chip, policy_src_send_hardrst);
		}
		break;
	}
}

static void fusb_state_src_negotiate_cap(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	/* base on evb1 */
	tmp = (chip->rec_load[0] >> 28) & 0x07;
	if (tmp > chip->n_caps_used)
		set_state(chip, policy_src_cap_response);
	else
		set_state(chip, policy_src_transition_supply);
}

static void fusb_state_src_transition_supply(struct fusb30x_chip *chip,
					     int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_ACCEPT, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->timer_state = T_SRC_TRANSITION;
			chip->sub_state++;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_softrst);
		}
		break;
	case 2:
		if (evt & EVENT_TIMER_STATE) {
			chip->notify.is_pd_connected = 1;
			platform_set_vbus_lvl_enable(chip, 1, 0);
			set_mesg(chip, CMT_PS_RDY, CONTROLMESSAGE);
			chip->tx_state = tx_idle;
			chip->sub_state++;
			chip->work_continue = 1;
		}
		break;
	default:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			dev_info(chip->dev,
				 "PD connected as DFP, supporting 5V\n");
			set_state(chip, policy_src_ready);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_softrst);
		}
		break;
	}
}

static void fusb_state_src_cap_response(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_REJECT, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	default:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			if (chip->notify.is_pd_connected) {
				dev_info(chip->dev,
					 "PD connected as DFP, supporting 5V\n");
				set_state(chip, policy_src_ready);
			} else {
				set_state(chip, policy_src_send_hardrst);
			}
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_softrst);
		}
		break;
	}
}

static void fusb_state_src_transition_default(struct fusb30x_chip *chip,
					      int evt)
{
	switch (chip->sub_state) {
	case 0:
		chip->notify.is_pd_connected = 0;
		platform_set_vbus_lvl_enable(chip, 0, 0);
		if (chip->notify.data_role)
			regmap_update_bits(chip->regmap,
					   FUSB_REG_SWITCHES1,
					   SWITCHES1_DATAROLE,
					   SWITCHES1_DATAROLE);
		else
			regmap_update_bits(chip->regmap,
					   FUSB_REG_SWITCHES1,
					   SWITCHES1_DATAROLE,
					   0);

		chip->timer_state = T_SRC_RECOVER;
		fusb_timer_start(&chip->timer_state_machine,
				 chip->timer_state);
		chip->sub_state++;
		break;
	default:
		if (evt & EVENT_TIMER_STATE) {
			platform_set_vbus_lvl_enable(chip, 1, 0);
			chip->timer_mux = T_NO_RESPONSE;
			fusb_timer_start(&chip->timer_mux_machine,
					 chip->timer_mux);
			set_state(chip, policy_src_startup);
			dev_dbg(chip->dev, "reset over-> src startup\n");
		}
		break;
	}
}

static void fusb_state_src_ready(struct fusb30x_chip *chip, int evt)
{
	if (evt & EVENT_RX) {
		if (PACKET_IS_DATA_MSG(chip->rec_head, DMT_VENDERDEFINED)) {
			process_vdm_msg(chip);
			chip->work_continue = 1;
			chip->timer_state = T_DISABLED;
		}
	}

	/* TODO: swap function would be added here later on*/

	if (!chip->partner_cap[0])
		set_state(chip, policy_src_get_sink_caps);
	else
		auto_vdm_machine(chip, evt);
}

static void fusb_state_src_get_sink_cap(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_GETSINKCAP, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->timer_state = T_SENDER_RESPONSE;
			chip->sub_state++;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_softrst);
		}

		if (!(evt & FLAG_EVENT))
			break;
	default:
		if (evt & EVENT_RX) {
			if (PACKET_IS_DATA_MSG(chip->rec_head,
					       DMT_SINKCAPABILITIES)) {
				for (tmp = 0;
				     tmp < PD_HEADER_CNT(chip->rec_head);
				     tmp++) {
					chip->partner_cap[tmp] =
						chip->rec_load[tmp];
				}
				set_state(chip, policy_src_ready);
			} else {
				chip->partner_cap[0] = 0xffffffff;
				set_state(chip, policy_src_ready);
			}
		} else if (evt & EVENT_TIMER_STATE) {
			dev_warn(chip->dev, "Get sink cap time out\n");
			chip->partner_cap[0] = 0xffffffff;
			set_state(chip, policy_src_ready);
		}
	}
}

static void fusb_state_src_send_hardreset(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	default:
		tmp = policy_send_hardrst(chip, evt);
		if (tmp == tx_success) {
			chip->hardrst_count++;
			set_state(chip, policy_src_transition_default);
		} else if (tmp == tx_failed) {
			/* can't reach here */
			set_state(chip, error_recovery);
		}
		break;
	}
}

static void fusb_state_src_softreset(struct fusb30x_chip *chip)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_ACCEPT, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	default:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			fusb_soft_reset_parameter(chip);
			set_state(chip, policy_src_send_caps);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_hardrst);
		}
		break;
	}
}

static void fusb_state_src_send_softreset(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_SOFTRESET, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->timer_state = T_SENDER_RESPONSE;
			chip->sub_state++;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_src_send_hardrst);
		}

		if (!(evt & FLAG_EVENT))
			break;
	default:
		if (evt & EVENT_RX) {
			if (PACKET_IS_CONTROL_MSG(chip->rec_head, CMT_ACCEPT)) {
				fusb_soft_reset_parameter(chip);
				set_state(chip, policy_src_send_caps);
			}
		} else if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_src_send_hardrst);
		}
		break;
	}
}

static void fusb_state_snk_startup(struct fusb30x_chip *chip, int evt)
{
	chip->notify.is_pd_connected = 0;
	fusb_soft_reset_parameter(chip);

	memset(chip->partner_cap, 0, sizeof(chip->partner_cap));

	tcpm_set_msg_header(chip);
	tcpm_set_polarity(chip, chip->cc_polarity);
	tcpm_set_rx_enable(chip, 1);
	set_state(chip, policy_snk_discovery);
	platform_fusb_notify(chip);
}

static void fusb_state_snk_discovery(struct fusb30x_chip *chip, int evt)
{
	set_state(chip, policy_snk_wait_caps);
	chip->timer_state = T_TYPEC_SINK_WAIT_CAP;
	fusb_timer_start(&chip->timer_state_machine,
			 chip->timer_state);
}

static void fusb_state_snk_wait_caps(struct fusb30x_chip *chip, int evt)
{
	if (evt & EVENT_RX) {
		if (PACKET_IS_DATA_MSG(chip->rec_head,
				       DMT_SOURCECAPABILITIES)) {
			chip->is_pd_support = 1;
			chip->timer_mux = T_DISABLED;
			set_state(chip, policy_snk_evaluate_caps);
		}
	} else if (evt & EVENT_TIMER_STATE) {
		if (chip->hardrst_count <= N_HARDRESET_COUNT) {
			if (chip->vbus_begin) {
				chip->vbus_begin = false;
				set_state(chip, policy_snk_send_softrst);
			} else {
				set_state(chip, policy_snk_send_hardrst);
			}
		} else {
			if (chip->is_pd_support)
				set_state(chip, error_recovery);
			else
				set_state(chip, disabled);
		}
	} else if ((evt & EVENT_TIMER_MUX) &&
		   (chip->hardrst_count > N_HARDRESET_COUNT)) {
		if (chip->is_pd_support)
			set_state(chip, error_recovery);
		else
			set_state(chip, disabled);
	}
}

static void fusb_state_snk_evaluate_caps(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	chip->hardrst_count = 0;
	chip->pos_power = 0;

	for (tmp = 0; tmp < PD_HEADER_CNT(chip->rec_head); tmp++) {
		switch (CAP_POWER_TYPE(chip->rec_load[tmp])) {
		case 0:
			/* Fixed Supply */
			if (CAP_FPDO_VOLTAGE(chip->rec_load[tmp]) <= 100)
				chip->pos_power = tmp + 1;
			break;
		case 1:
			/* Battery */
			if (CAP_VPDO_VOLTAGE(chip->rec_load[tmp]) <= 100)
				chip->pos_power = tmp + 1;
			break;
		default:
			/* not meet battery caps */
			break;
		}
	}
	fusb302_set_pos_power_by_charge_ic(chip);

	if ((!chip->pos_power) || (chip->pos_power > 7)) {
		chip->pos_power = 0;
		set_state(chip, policy_snk_wait_caps);
	} else {
		set_state(chip, policy_snk_select_cap);
	}
}

static void fusb_state_snk_select_cap(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, DMT_REQUEST, DATAMESSAGE);
		chip->sub_state = 1;
		chip->tx_state = tx_idle;
		/* without break */
	case 1:
		tmp = policy_send_data(chip);

		if (tmp == tx_success) {
			chip->timer_state = T_SENDER_RESPONSE;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
			chip->sub_state++;
		} else if (tmp == tx_failed) {
			set_state(chip, policy_snk_discovery);
			break;
		}

		if (!(evt & FLAG_EVENT))
			break;
	default:
		if (evt & EVENT_RX) {
			if (!PD_HEADER_CNT(chip->rec_head)) {
				switch (PD_HEADER_TYPE(chip->rec_head)) {
				case CMT_ACCEPT:
					set_state(chip,
						  policy_snk_transition_sink);
					chip->timer_state = T_PS_TRANSITION;
					fusb_timer_start(&chip->timer_state_machine,
							 chip->timer_state);
					break;
				case CMT_WAIT:
				case CMT_REJECT:
					if (chip->notify.is_pd_connected) {
						dev_info(chip->dev,
							 "PD connected as UFP, fetching 5V\n");
						set_state(chip,
							  policy_snk_ready);
					} else {
						set_state(chip,
							  policy_snk_wait_caps);
						/*
						 * make sure don't send
						 * hard reset to prevent
						 * infinite loop
						 */
						chip->hardrst_count =
							N_HARDRESET_COUNT + 1;
					}
					break;
				default:
					break;
				}
			}
		} else if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_snk_send_hardrst);
		}
		break;
	}
}

static void fusb_state_snk_transition_sink(struct fusb30x_chip *chip, int evt)
{
	if (evt & EVENT_RX) {
		if (PACKET_IS_CONTROL_MSG(chip->rec_head, CMT_PS_RDY)) {
			chip->notify.is_pd_connected = 1;
			dev_info(chip->dev,
				 "PD connected as UFP, fetching 5V\n");
			set_state(chip, policy_snk_ready);
		} else if (PACKET_IS_DATA_MSG(chip->rec_head,
					      DMT_SOURCECAPABILITIES)) {
			set_state(chip, policy_snk_evaluate_caps);
		}
	} else if (evt & EVENT_TIMER_STATE) {
		set_state(chip, policy_snk_send_hardrst);
	}
}

static void fusb_state_snk_transition_default(struct fusb30x_chip *chip,
					      int evt)
{
	switch (chip->sub_state) {
	case 0:
		chip->notify.is_pd_connected = 0;
		chip->timer_mux = T_NO_RESPONSE;
		fusb_timer_start(&chip->timer_mux_machine,
				 chip->timer_mux);
		chip->timer_state = T_PS_HARD_RESET_MAX + T_SAFE_0V;
		fusb_timer_start(&chip->timer_state_machine,
				 chip->timer_state);
		if (chip->notify.data_role)
			tcpm_set_msg_header(chip);

		chip->sub_state++;
	case 1:
		if (!tcpm_check_vbus(chip)) {
			chip->sub_state++;
			chip->timer_state = T_SRC_RECOVER_MAX + T_SRC_TURN_ON;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_snk_startup);
		}
		break;
	default:
		if (tcpm_check_vbus(chip)) {
			chip->timer_state = T_DISABLED;
			set_state(chip, policy_snk_startup);
		} else if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_snk_startup);
		}
		break;
	}
}

static void fusb_state_snk_ready(struct fusb30x_chip *chip, int evt)
{
	/* TODO: snk_ready_function would be added later on*/
	platform_fusb_notify(chip);
}

static void fusb_state_snk_send_hardreset(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		chip->tx_state = tx_idle;
		chip->sub_state++;
	default:
		tmp = policy_send_hardrst(chip, evt);
		if (tmp == tx_success) {
			chip->hardrst_count++;
			set_state(chip, policy_snk_transition_default);
		} else if (tmp == tx_failed) {
			set_state(chip, error_recovery);
		}
		break;
	}
}

static void fusb_state_snk_softreset(struct fusb30x_chip *chip)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_ACCEPT, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
		/* without break */
	default:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			fusb_soft_reset_parameter(chip);
			chip->timer_state = T_TYPEC_SINK_WAIT_CAP;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
			set_state(chip, policy_snk_wait_caps);
		} else if (tmp == tx_failed) {
			set_state(chip, policy_snk_send_hardrst);
		}
		break;
	}
}

static void fusb_state_snk_send_softreset(struct fusb30x_chip *chip, int evt)
{
	u32 tmp;

	switch (chip->sub_state) {
	case 0:
		set_mesg(chip, CMT_SOFTRESET, CONTROLMESSAGE);
		chip->tx_state = tx_idle;
		chip->sub_state++;
	case 1:
		tmp = policy_send_data(chip);
		if (tmp == tx_success) {
			chip->timer_state = T_SENDER_RESPONSE;
			chip->sub_state++;
			fusb_timer_start(&chip->timer_state_machine,
					 chip->timer_state);
		} else if (tmp == tx_failed) {
			/* can't reach here */
			set_state(chip, policy_snk_send_hardrst);
		}

		if (!(evt & FLAG_EVENT))
			break;
	default:
		if (evt & EVENT_RX) {
			if ((!PD_HEADER_CNT(chip->rec_head)) &&
			    (PD_HEADER_TYPE(chip->rec_head) == CMT_ACCEPT)) {
				fusb_soft_reset_parameter(chip);
				chip->timer_state = T_TYPEC_SINK_WAIT_CAP;
				fusb_timer_start(&chip->timer_state_machine,
						 chip->timer_state);
				set_state(chip, policy_snk_wait_caps);
			}
		} else if (evt & EVENT_TIMER_STATE) {
			set_state(chip, policy_snk_send_hardrst);
		}
		break;
	}
}

static void state_machine_typec(struct fusb30x_chip *chip)
{
	int evt = 0;
	int cc1, cc2;

	tcpc_alert(chip, &evt);
	mux_alert(chip, &evt);
	if (!evt)
		goto BACK;

	if (chip->notify.is_cc_connected) {
		if (evt & EVENT_CC) {
			if ((chip->cc_state & 0x04) &&
			    (chip->conn_state !=
			     policy_snk_transition_default)) {
				if (!tcpm_check_vbus(chip))
					set_state_unattached(chip);
			} else if (chip->conn_state !=
				   policy_src_transition_default) {
				tcpm_get_cc(chip, &cc1, &cc2);
				if (!(chip->cc_state & 0x01))
					cc1 = cc2;
				if (cc1 == TYPEC_CC_VOLT_OPEN)
					set_state_unattached(chip);
			}
		}
	}

	if (evt & EVENT_RX) {
		tcpm_get_message(chip);
		if (PACKET_IS_CONTROL_MSG(chip->rec_head, CMT_SOFTRESET)) {
			if (chip->notify.power_role)
				set_state(chip, policy_src_softrst);
			else
				set_state(chip, policy_snk_softrst);
		}
	}

	if (evt & EVENT_TX) {
		if (chip->tx_state == tx_success)
			chip->msg_id++;
	}
	switch (chip->conn_state) {
	case disabled:
		fusb_state_disabled(chip, evt);
		break;
	case error_recovery:
		set_state_unattached(chip);
		break;
	case unattached:
		fusb_state_unattached(chip, evt);
		break;
	case attach_wait_sink:
		fusb_state_attach_wait_sink(chip, evt);
		break;
	case attach_wait_source:
		fusb_state_attach_wait_source(chip, evt);
		break;
	case attached_source:
		fusb_state_attached_source(chip, evt);
		break;
	case attached_sink:
		fusb_state_attached_sink(chip, evt);
		break;

	/* POWER DELIVERY */
	case policy_src_startup:
		fusb_state_src_startup(chip, evt);
		break;
	case policy_src_discovery:
		fusb_state_src_discovery(chip, evt);
		break;
	case policy_src_send_caps:
		fusb_state_src_send_caps(chip, evt);
		if (chip->conn_state != policy_src_negotiate_cap)
			break;
	case policy_src_negotiate_cap:
		fusb_state_src_negotiate_cap(chip, evt);

	case policy_src_transition_supply:
		fusb_state_src_transition_supply(chip, evt);
		break;
	case policy_src_cap_response:
		fusb_state_src_cap_response(chip, evt);
		break;
	case policy_src_transition_default:
		fusb_state_src_transition_default(chip, evt);
		break;
	case policy_src_ready:
		fusb_state_src_ready(chip, evt);
		break;
	case policy_src_get_sink_caps:
		fusb_state_src_get_sink_cap(chip, evt);
		break;
	case policy_src_send_hardrst:
		fusb_state_src_send_hardreset(chip, evt);
		break;
	case policy_src_send_softrst:
		fusb_state_src_send_softreset(chip, evt);
		break;
	case policy_src_softrst:
		fusb_state_src_softreset(chip);
		break;

	/* UFP */
	case policy_snk_startup:
		fusb_state_snk_startup(chip, evt);
		break;
	case policy_snk_discovery:
		fusb_state_snk_discovery(chip, evt);
		break;
	case policy_snk_wait_caps:
		fusb_state_snk_wait_caps(chip, evt);
		break;
	case policy_snk_evaluate_caps:
		fusb_state_snk_evaluate_caps(chip, evt);
		/* without break */
	case policy_snk_select_cap:
		fusb_state_snk_select_cap(chip, evt);
		break;
	case policy_snk_transition_sink:
		fusb_state_snk_transition_sink(chip, evt);
		break;
	case policy_snk_transition_default:
		fusb_state_snk_transition_default(chip, evt);
		break;
	case policy_snk_ready:
		fusb_state_snk_ready(chip, evt);
		break;
	case policy_snk_send_hardrst:
		fusb_state_snk_send_hardreset(chip, evt);
		break;
	case policy_snk_send_softrst:
		fusb_state_snk_send_softreset(chip, evt);
		break;
	case policy_snk_softrst:
		fusb_state_snk_softreset(chip);
		break;

	default:
		break;
	}

BACK:
	if (chip->work_continue) {
		queue_work(chip->fusb30x_wq, &chip->work);
		return;
	}

	if (!platform_get_device_irq_state(chip))
		fusb_irq_enable(chip);
	else
		queue_work(chip->fusb30x_wq, &chip->work);
}

static irqreturn_t cc_interrupt_handler(int irq, void *dev_id)
{
	struct fusb30x_chip *chip = dev_id;

	queue_work(chip->fusb30x_wq, &chip->work);
	fusb_irq_disable(chip);
	return IRQ_HANDLED;
}

static int fusb_initialize_gpio(struct fusb30x_chip *chip)
{
	chip->gpio_int = devm_gpiod_get_optional(chip->dev, "int-n", GPIOD_IN);
	if (IS_ERR(chip->gpio_int))
		return PTR_ERR(chip->gpio_int);

	/* some board support vbus with other ways */
	chip->gpio_vbus_5v = devm_gpiod_get_optional(chip->dev, "vbus-5v",
						     GPIOD_OUT_LOW);
	if (IS_ERR(chip->gpio_vbus_5v))
		dev_warn(chip->dev,
			 "Could not get named GPIO for VBus5V!\n");
	else
		gpiod_set_raw_value(chip->gpio_vbus_5v, 0);

	chip->gpio_vbus_other = devm_gpiod_get_optional(chip->dev,
							"vbus-other",
							GPIOD_OUT_LOW);
	if (IS_ERR(chip->gpio_vbus_other))
		dev_warn(chip->dev,
			 "Could not get named GPIO for VBusOther!\n");
	else
		gpiod_set_raw_value(chip->gpio_vbus_other, 0);

	chip->gpio_discharge = devm_gpiod_get_optional(chip->dev, "discharge",
						       GPIOD_OUT_LOW);
	if (IS_ERR(chip->gpio_discharge)) {
		dev_warn(chip->dev,
			 "Could not get named GPIO for discharge!\n");
		chip->gpio_discharge = NULL;
	}

	return 0;
}

static enum hrtimer_restart fusb_timer_handler(struct hrtimer *timer)
{
	int i;

	for (i = 0; i < fusb30x_port_used; i++) {
		if (timer == &fusb30x_port_info[i]->timer_state_machine) {
			if (fusb30x_port_info[i]->timer_state != T_DISABLED)
				fusb30x_port_info[i]->timer_state = 0;
			break;
		}

		if (timer == &fusb30x_port_info[i]->timer_mux_machine) {
			if (fusb30x_port_info[i]->timer_mux != T_DISABLED)
				fusb30x_port_info[i]->timer_mux = 0;
			break;
		}
	}

	if (i != fusb30x_port_used)
		queue_work(fusb30x_port_info[i]->fusb30x_wq,
			   &fusb30x_port_info[i]->work);

	return HRTIMER_NORESTART;
}

static void fusb_initialize_timer(struct fusb30x_chip *chip)
{
	hrtimer_init(&chip->timer_state_machine, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	chip->timer_state_machine.function = fusb_timer_handler;

	hrtimer_init(&chip->timer_mux_machine, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	chip->timer_mux_machine.function = fusb_timer_handler;

	chip->timer_state = T_DISABLED;
	chip->timer_mux = T_DISABLED;
}

static void fusb302_work_func(struct work_struct *work)
{
	struct fusb30x_chip *chip;

	chip = container_of(work, struct fusb30x_chip, work);
	state_machine_typec(chip);
}

static int fusb30x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct fusb30x_chip *chip;
	struct PD_CAP_INFO *pd_cap_info;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (fusb30x_port_used == 0xff)
		return -1;

	chip->port_num = fusb30x_port_used++;
	fusb30x_port_info[chip->port_num] = chip;

	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &fusb302_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err(&client->dev, "Failed to allocate regmap!\n");
		return PTR_ERR(chip->regmap);
	}

	ret = fusb_initialize_gpio(chip);
	if (ret)
		return ret;

	fusb_initialize_timer(chip);

	chip->fusb30x_wq = create_workqueue("fusb302_wq");
	INIT_WORK(&chip->work, fusb302_work_func);

	tcpm_init(chip);
	tcpm_set_rx_enable(chip, 0);
	chip->conn_state = unattached;
	tcpm_set_cc(chip, FUSB_MODE_DRP);

	chip->n_caps_used = 1;
	chip->source_power_supply[0] = 0x64;
	chip->source_max_current[0] = 0x96;

	/*
	 * these two variable should be 1 if support DRP,
	 * but now we do not support swap,
	 * it will be blanked in future
	 */
	pd_cap_info = &chip->pd_cap_info;
	pd_cap_info->dual_role_power = 0;
	pd_cap_info->data_role_swap = 0;

	pd_cap_info->externally_powered = 1;
	pd_cap_info->usb_suspend_support = 0;
	pd_cap_info->usb_communications_cap = 0;
	pd_cap_info->supply_type = 0;
	pd_cap_info->peak_current = 0;

	chip->extcon = devm_extcon_dev_allocate(&client->dev, fusb302_cable);
	if (IS_ERR(chip->extcon)) {
		dev_err(&client->dev, "allocat extcon failed\n");
		return PTR_ERR(chip->extcon);
	}

	ret = devm_extcon_dev_register(&client->dev, chip->extcon);
	if (ret) {
		dev_err(&client->dev, "failed to register extcon: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_USB,
					     EXTCON_PROP_USB_TYPEC_POLARITY);
	if (ret) {
		dev_err(&client->dev,
			"failed to set USB property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_USB_HOST,
					     EXTCON_PROP_USB_TYPEC_POLARITY);
	if (ret) {
		dev_err(&client->dev,
			"failed to set USB_HOST property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_DISP_DP,
					     EXTCON_PROP_USB_TYPEC_POLARITY);
	if (ret) {
		dev_err(&client->dev,
			"failed to set DISP_DP property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_USB,
					     EXTCON_PROP_USB_SS);
	if (ret) {
		dev_err(&client->dev,
			"failed to set USB USB_SS property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_USB_HOST,
					     EXTCON_PROP_USB_SS);
	if (ret) {
		dev_err(&client->dev,
			"failed to set USB_HOST USB_SS property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_DISP_DP,
					     EXTCON_PROP_USB_SS);
	if (ret) {
		dev_err(&client->dev,
			"failed to set DISP_DP USB_SS property capability: %d\n",
			ret);
		return ret;
	}

	ret = extcon_set_property_capability(chip->extcon, EXTCON_CHG_USB_FAST,
					     EXTCON_PROP_USB_TYPEC_POLARITY);
	if (ret) {
		dev_err(&client->dev,
			"failed to set USB_PD property capability: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, chip);

	spin_lock_init(&chip->irq_lock);
	chip->enable_irq = 1;

	chip->gpio_int_irq = gpiod_to_irq(chip->gpio_int);
	if (chip->gpio_int_irq < 0) {
		dev_err(&client->dev,
			"Unable to request IRQ for INT_N GPIO! %d\n",
			ret);
		ret = chip->gpio_int_irq;
		goto IRQ_ERR;
	}

	ret = devm_request_threaded_irq(&client->dev,
					chip->gpio_int_irq,
					NULL,
					cc_interrupt_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					client->name,
					chip);
	if (ret) {
		dev_err(&client->dev, "irq request failed\n");
		goto IRQ_ERR;
	}

	dev_info(chip->dev, "port %d probe success\n", chip->port_num);

	return 0;

IRQ_ERR:
	destroy_workqueue(chip->fusb30x_wq);
	return ret;
}

static int fusb30x_remove(struct i2c_client *client)
{
	struct fusb30x_chip *chip = i2c_get_clientdata(client);

	destroy_workqueue(chip->fusb30x_wq);
	return 0;
}

static void fusb30x_shutdown(struct i2c_client *client)
{
	struct fusb30x_chip *chip = i2c_get_clientdata(client);

	if (chip->gpio_vbus_5v)
		gpiod_set_value(chip->gpio_vbus_5v, 0);
	if (chip->gpio_discharge) {
		gpiod_set_value(chip->gpio_discharge, 1);
		msleep(100);
		gpiod_set_value(chip->gpio_discharge, 0);
	}
}

static const struct of_device_id fusb30x_dt_match[] = {
	{ .compatible = FUSB30X_I2C_DEVICETREE_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, fusb30x_dt_match);

static const struct i2c_device_id fusb30x_i2c_device_id[] = {
	{ FUSB30X_I2C_DRIVER_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, fusb30x_i2c_device_id);

static struct i2c_driver fusb30x_driver = {
	.driver = {
		.name = FUSB30X_I2C_DRIVER_NAME,
		.of_match_table = of_match_ptr(fusb30x_dt_match),
	},
	.probe = fusb30x_probe,
	.remove = fusb30x_remove,
	.shutdown = fusb30x_shutdown,
	.id_table = fusb30x_i2c_device_id,
};

module_i2c_driver(fusb30x_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zain wang <zain.wang@rock-chips.com>");
MODULE_DESCRIPTION("fusb302 typec pd driver");
