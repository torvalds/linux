// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 Google, Inc
 *
 * USB-C module to reduce wakeups due to contaminants.
 */

#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec.h>

#include "tcpci_maxim.h"

enum fladc_select {
	CC1_SCALE1 = 1,
	CC1_SCALE2,
	CC2_SCALE1,
	CC2_SCALE2,
	SBU1,
	SBU2,
};

#define FLADC_1uA_LSB_MV		25
/* High range CC */
#define FLADC_CC_HIGH_RANGE_LSB_MV	208
/* Low range CC */
#define FLADC_CC_LOW_RANGE_LSB_MV      126

/* 1uA current source */
#define FLADC_CC_SCALE1			1
/* 5 uA current source */
#define FLADC_CC_SCALE2			5

#define FLADC_1uA_CC_OFFSET_MV		300
#define FLADC_CC_HIGH_RANGE_OFFSET_MV	624
#define FLADC_CC_LOW_RANGE_OFFSET_MV	378

#define CONTAMINANT_THRESHOLD_SBU_K	1000
#define	CONTAMINANT_THRESHOLD_CC_K	1000

#define READ1_SLEEP_MS			10
#define READ2_SLEEP_MS			5

#define STATUS_CHECK(reg, mask, val)	(((reg) & (mask)) == (val))

#define IS_CC_OPEN(cc_status) \
	(STATUS_CHECK((cc_status), TCPC_CC_STATUS_CC1_MASK << TCPC_CC_STATUS_CC1_SHIFT,  \
		      TCPC_CC_STATE_SRC_OPEN) && STATUS_CHECK((cc_status),               \
							      TCPC_CC_STATUS_CC2_MASK << \
							      TCPC_CC_STATUS_CC2_SHIFT,  \
							      TCPC_CC_STATE_SRC_OPEN))

static int max_contaminant_adc_to_mv(struct max_tcpci_chip *chip, enum fladc_select channel,
				     bool ua_src, u8 fladc)
{
	/* SBU channels only have 1 scale with 1uA. */
	if ((ua_src && (channel == CC1_SCALE2 || channel == CC2_SCALE2 || channel == SBU1 ||
			channel == SBU2)))
		/* Mean of range */
		return FLADC_1uA_CC_OFFSET_MV + (fladc * FLADC_1uA_LSB_MV);
	else if (!ua_src && (channel == CC1_SCALE1 || channel == CC2_SCALE1))
		return FLADC_CC_HIGH_RANGE_OFFSET_MV + (fladc * FLADC_CC_HIGH_RANGE_LSB_MV);
	else if (!ua_src && (channel == CC1_SCALE2 || channel == CC2_SCALE2))
		return FLADC_CC_LOW_RANGE_OFFSET_MV + (fladc * FLADC_CC_LOW_RANGE_LSB_MV);

	dev_err_once(chip->dev, "ADC ERROR: SCALE UNKNOWN");

	return -EINVAL;
}

static int max_contaminant_read_adc_mv(struct max_tcpci_chip *chip, enum fladc_select channel,
				       int sleep_msec, bool raw, bool ua_src)
{
	struct regmap *regmap = chip->data.regmap;
	u8 fladc;
	int ret;

	/* Channel & scale select */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_ADC_CTRL1, ADCINSEL_MASK,
				 channel << ADC_CHANNEL_OFFSET);
	if (ret < 0)
		return ret;

	/* Enable ADC */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_ADC_CTRL1, ADCEN, ADCEN);
	if (ret < 0)
		return ret;

	usleep_range(sleep_msec * 1000, (sleep_msec + 1) * 1000);
	ret = max_tcpci_read8(chip, TCPC_VENDOR_FLADC_STATUS, &fladc);
	if (ret < 0)
		return ret;

	/* Disable ADC */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_ADC_CTRL1, ADCEN, 0);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_ADC_CTRL1, ADCINSEL_MASK, 0);
	if (ret < 0)
		return ret;

	if (!raw)
		return max_contaminant_adc_to_mv(chip, channel, ua_src, fladc);
	else
		return fladc;
}

static int max_contaminant_read_resistance_kohm(struct max_tcpci_chip *chip,
						enum fladc_select channel, int sleep_msec, bool raw)
{
	struct regmap *regmap = chip->data.regmap;
	int mv;
	int ret;

	if (channel == CC1_SCALE1 || channel == CC2_SCALE1 || channel == CC1_SCALE2 ||
	    channel == CC2_SCALE2) {
		/* Enable 1uA current source */
		ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCLPMODESEL_MASK,
					 ULTRA_LOW_POWER_MODE);
		if (ret < 0)
			return ret;

		/* Enable 1uA current source */
		ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCRPCTRL_MASK, UA_1_SRC);
		if (ret < 0)
			return ret;

		/* OVP disable */
		ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCOVPDIS, CCOVPDIS);
		if (ret < 0)
			return ret;

		mv = max_contaminant_read_adc_mv(chip, channel, sleep_msec, raw, true);
		if (mv < 0)
			return ret;

		/* OVP enable */
		ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCOVPDIS, 0);
		if (ret < 0)
			return ret;
		/* returns KOhm as 1uA source is used. */
		return mv;
	}

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, SBUOVPDIS, SBUOVPDIS);
	if (ret < 0)
		return ret;

	/* SBU switches auto configure when channel is selected. */
	/* Enable 1ua current source */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, SBURPCTRL, SBURPCTRL);
	if (ret < 0)
		return ret;

	mv = max_contaminant_read_adc_mv(chip, channel, sleep_msec, raw, true);
	if (mv < 0)
		return ret;
	/* Disable current source */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, SBURPCTRL, 0);
	if (ret < 0)
		return ret;

	/* OVP disable */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, SBUOVPDIS, 0);
	if (ret < 0)
		return ret;

	return mv;
}

static int max_contaminant_read_comparators(struct max_tcpci_chip *chip, u8 *vendor_cc_status2_cc1,
					    u8 *vendor_cc_status2_cc2)
{
	struct regmap *regmap = chip->data.regmap;
	int ret;

	/* Enable 80uA source */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCRPCTRL_MASK, UA_80_SRC);
	if (ret < 0)
		return ret;

	/* Enable comparators */
	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL1, CCCOMPEN, CCCOMPEN);
	if (ret < 0)
		return ret;

	/* Sleep to allow comparators settle */
	usleep_range(5000, 6000);
	ret = regmap_update_bits(regmap, TCPC_TCPC_CTRL, TCPC_TCPC_CTRL_ORIENTATION, PLUG_ORNT_CC1);
	if (ret < 0)
		return ret;

	usleep_range(5000, 6000);
	ret = max_tcpci_read8(chip, VENDOR_CC_STATUS2, vendor_cc_status2_cc1);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_TCPC_CTRL, TCPC_TCPC_CTRL_ORIENTATION, PLUG_ORNT_CC2);
	if (ret < 0)
		return ret;

	usleep_range(5000, 6000);
	ret = max_tcpci_read8(chip, VENDOR_CC_STATUS2, vendor_cc_status2_cc2);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL1, CCCOMPEN, 0);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCRPCTRL_MASK, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int max_contaminant_detect_contaminant(struct max_tcpci_chip *chip)
{
	int cc1_k, cc2_k, sbu1_k, sbu2_k, ret;
	u8 vendor_cc_status2_cc1 = 0xff, vendor_cc_status2_cc2 = 0xff;
	u8 role_ctrl = 0, role_ctrl_backup = 0;
	int inferred_state = NOT_DETECTED;

	ret = max_tcpci_read8(chip, TCPC_ROLE_CTRL, &role_ctrl);
	if (ret < 0)
		return NOT_DETECTED;

	role_ctrl_backup = role_ctrl;
	role_ctrl = 0x0F;
	ret = max_tcpci_write8(chip, TCPC_ROLE_CTRL, role_ctrl);
	if (ret < 0)
		return NOT_DETECTED;

	cc1_k = max_contaminant_read_resistance_kohm(chip, CC1_SCALE2, READ1_SLEEP_MS, false);
	if (cc1_k < 0)
		goto exit;

	cc2_k = max_contaminant_read_resistance_kohm(chip, CC2_SCALE2, READ2_SLEEP_MS, false);
	if (cc2_k < 0)
		goto exit;

	sbu1_k = max_contaminant_read_resistance_kohm(chip, SBU1, READ1_SLEEP_MS, false);
	if (sbu1_k < 0)
		goto exit;

	sbu2_k = max_contaminant_read_resistance_kohm(chip, SBU2, READ2_SLEEP_MS, false);
	if (sbu2_k < 0)
		goto exit;

	ret = max_contaminant_read_comparators(chip, &vendor_cc_status2_cc1,
					       &vendor_cc_status2_cc2);

	if (ret < 0)
		goto exit;

	if ((!(CC1_VUFP_RD0P5 & vendor_cc_status2_cc1) ||
	     !(CC2_VUFP_RD0P5 & vendor_cc_status2_cc2)) &&
	    !(CC1_VUFP_RD0P5 & vendor_cc_status2_cc1 && CC2_VUFP_RD0P5 & vendor_cc_status2_cc2))
		inferred_state = SINK;
	else if ((cc1_k < CONTAMINANT_THRESHOLD_CC_K || cc2_k < CONTAMINANT_THRESHOLD_CC_K) &&
		 (sbu1_k < CONTAMINANT_THRESHOLD_SBU_K || sbu2_k < CONTAMINANT_THRESHOLD_SBU_K))
		inferred_state = DETECTED;

	if (inferred_state == NOT_DETECTED)
		max_tcpci_write8(chip, TCPC_ROLE_CTRL, role_ctrl_backup);
	else
		max_tcpci_write8(chip, TCPC_ROLE_CTRL, (TCPC_ROLE_CTRL_DRP | 0xA));

	return inferred_state;
exit:
	max_tcpci_write8(chip, TCPC_ROLE_CTRL, role_ctrl_backup);
	return NOT_DETECTED;
}

static int max_contaminant_enable_dry_detection(struct max_tcpci_chip *chip)
{
	struct regmap *regmap = chip->data.regmap;
	u8 temp;
	int ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL3, CCWTRDEB_MASK | CCWTRSEL_MASK
				    | WTRCYCLE_MASK, CCWTRDEB_1MS << CCWTRDEB_SHIFT |
				    CCWTRSEL_1V << CCWTRSEL_SHIFT | WTRCYCLE_4_8_S <<
				    WTRCYCLE_SHIFT);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_ROLE_CTRL, TCPC_ROLE_CTRL_DRP, TCPC_ROLE_CTRL_DRP);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL1, CCCONNDRY, CCCONNDRY);
	if (ret < 0)
		return ret;
	ret = max_tcpci_read8(chip, TCPC_VENDOR_CC_CTRL1, &temp);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, TCPC_VENDOR_CC_CTRL2, CCLPMODESEL_MASK,
				 ULTRA_LOW_POWER_MODE);
	if (ret < 0)
		return ret;
	ret = max_tcpci_read8(chip, TCPC_VENDOR_CC_CTRL2, &temp);
	if (ret < 0)
		return ret;

	/* Enable Look4Connection before sending the command */
	ret = regmap_update_bits(regmap, TCPC_TCPC_CTRL, TCPC_TCPC_CTRL_EN_LK4CONN_ALRT,
				 TCPC_TCPC_CTRL_EN_LK4CONN_ALRT);
	if (ret < 0)
		return ret;

	ret = max_tcpci_write8(chip, TCPC_COMMAND, TCPC_CMD_LOOK4CONNECTION);
	if (ret < 0)
		return ret;
	return 0;
}

bool max_contaminant_is_contaminant(struct max_tcpci_chip *chip, bool disconnect_while_debounce)
{
	u8 cc_status, pwr_cntl;
	int ret;

	ret = max_tcpci_read8(chip, TCPC_CC_STATUS, &cc_status);
	if (ret < 0)
		return false;

	ret = max_tcpci_read8(chip, TCPC_POWER_CTRL, &pwr_cntl);
	if (ret < 0)
		return false;

	if (chip->contaminant_state == NOT_DETECTED || chip->contaminant_state == SINK) {
		if (!disconnect_while_debounce)
			msleep(100);

		ret = max_tcpci_read8(chip, TCPC_CC_STATUS, &cc_status);
		if (ret < 0)
			return false;

		if (IS_CC_OPEN(cc_status)) {
			u8 role_ctrl, role_ctrl_backup;

			ret = max_tcpci_read8(chip, TCPC_ROLE_CTRL, &role_ctrl);
			if (ret < 0)
				return false;

			role_ctrl_backup = role_ctrl;
			role_ctrl |= 0x0F;
			role_ctrl &= ~(TCPC_ROLE_CTRL_DRP);
			ret = max_tcpci_write8(chip, TCPC_ROLE_CTRL, role_ctrl);
			if (ret < 0)
				return false;

			chip->contaminant_state = max_contaminant_detect_contaminant(chip);

			ret = max_tcpci_write8(chip, TCPC_ROLE_CTRL, role_ctrl_backup);
			if (ret < 0)
				return false;

			if (chip->contaminant_state == DETECTED) {
				max_contaminant_enable_dry_detection(chip);
				return true;
			}
		}
		return false;
	} else if (chip->contaminant_state == DETECTED) {
		if (STATUS_CHECK(cc_status, TCPC_CC_STATUS_TOGGLING, 0)) {
			chip->contaminant_state = max_contaminant_detect_contaminant(chip);
			if (chip->contaminant_state == DETECTED) {
				max_contaminant_enable_dry_detection(chip);
				return true;
			}
		}
	}

	return false;
}

MODULE_DESCRIPTION("MAXIM TCPC CONTAMINANT Module");
MODULE_AUTHOR("Badhri Jagan Sridharan <badhri@google.com>");
MODULE_LICENSE("GPL");
