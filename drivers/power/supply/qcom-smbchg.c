// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power supply driver for Qualcomm PMIC switch-mode battery charger
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 * Copyright (c) 2021 Alejandro Tafalla <atafalla@dnyon.com>
 */

#include <linux/unaligned.h>
#include <linux/errno.h>
#include <linux/extcon-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/util_macros.h>
#include <soc/qcom/pmic-sec-write.h>

#include "qcom-smbchg.h"

/**
 * @brief smbchg_usb_is_present() - Check for USB presence
 *
 * @param chip Pointer to smbchg_chip.
 * @return true if USB present, false otherwise
 */
static bool smbchg_usb_is_present(struct smbchg_chip *chip)
{
	u32 value;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_USB_CHGPTH_RT_STS,
			  &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read USB charge path real-time status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	if (!(value & USBIN_SRC_DET_BIT) || (value & USBIN_OV_BIT))
		return false;

	ret = regmap_read(chip->regmap,
			  chip->base + SMBCHG_USB_CHGPTH_INPUT_STS, &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read USB charge path input status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	return !!(value & (USBIN_9V | USBIN_UNREG | USBIN_LV));
}

/**
 * @brief smbchg_usb_enable() - Enable/disable USB charge path
 *
 * @param chip   Pointer to smbchg_chip
 * @param enable true to enable, false to disable
 * @return 0 on success, -errno on failure
 */
static int smbchg_usb_enable(struct smbchg_chip *chip, bool enable)
{
	int ret;

	dev_dbg(chip->dev, "%sabling USB charge path\n", enable ? "En" : "Dis");

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USBIN_SUSPEND_BIT,
				 enable ? 0 : USBIN_SUSPEND_BIT);
	if (ret)
		dev_err(chip->dev, "Failed to %sable USB charge path: %pe\n",
			enable ? "en" : "dis", ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_usb_get_type() - Get USB port type
 *
 * @param chip Pointer to smbchg_chip
 * @return enum power_supply_usb_type, the type of the connected USB port
 */
static enum power_supply_usb_type smbchg_usb_get_type(struct smbchg_chip *chip)
{
	u32 reg;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_MISC_IDEV_STS,
			  &reg);
	if (ret) {
		dev_err(chip->dev, "Failed to read USB type: %pe\n",
			ERR_PTR(ret));
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;
	}

	if (reg & USB_TYPE_SDP_BIT)
		return POWER_SUPPLY_USB_TYPE_SDP;
	else if (reg & USB_TYPE_OTHER_BIT || reg & USB_TYPE_DCP_BIT)
		return POWER_SUPPLY_USB_TYPE_DCP;
	else if (reg & USB_TYPE_CDP_BIT)
		return POWER_SUPPLY_USB_TYPE_CDP;
	else
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;
}

/**
 * @brief smbchg_usb_get_ilim() - Get USB input current limit
 *
 * @param chip Pointer to smbchg_chip
 * @return Current limit in microamperes on success, -errno on failure
 *
 * @details: Get currently configured input current limit on the USB
 * charge path.
 */
static int smbchg_usb_get_ilim(struct smbchg_chip *chip)
{
	bool usb_3, full_current;
	int value, ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
			  &value);
	if (ret)
		return ret;

	/* Low current mode */
	if (!(value & USBIN_MODE_HC_BIT)) {
		ret = regmap_read(chip->regmap,
				  chip->base + SMBCHG_USB_CHGPTH_CFG, &value);
		if (ret)
			return ret;

		usb_3 = value & CFG_USB3P0_SEL_BIT;

		ret = regmap_read(chip->regmap,
				  chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				  &value);
		if (ret)
			return ret;

		full_current = value & USB51_MODE_BIT;

		return smbchg_lc_ilim(usb_3, full_current);
	}

	/* High current mode */
	if (value & ICL_OVERRIDE_BIT) {
		/*
		 * Read the ilim index set in the USB charge path input
		 * limiting configuration register and look up the
		 * corresponding current limit in the ilim table.
		 */
		ret = regmap_read(chip->regmap,
				  chip->base + SMBCHG_USB_CHGPTH_IL_CFG,
				  &value);
		if (ret)
			return ret;

		return chip->data->ilim_table[value & USBIN_INPUT_MASK];
	}

	/* AICL */
	ret = regmap_read(chip->regmap,
			  chip->base + SMBCHG_USB_CHGPTH_ICL_STS_1, &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read ICL status: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	return chip->data->ilim_table[value & ICL_STS_MASK];
}

/**
 * @brief smbchg_usb_set_ilim_lc() - Set USB input current limit using
 * low current mode
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Target current limit in microamperes
 * @return Actual current limit in microamperes on success, -errno on failure
 *
 * @details: Low current mode provides four options for USB input current
 * limiting:
 * - 100mA (USB 2.0 limited SDP current)
 * - 150mA (USB 3.0 limited SDP current)
 * - 500mA (USB 2.0 full SDP current)
 * - 900mA (USB 3.0 full SDP current)
 * This mode is most suitable for use with SDPs.
 */
static int smbchg_usb_set_ilim_lc(struct smbchg_chip *chip, int current_ua)
{
	bool usb_3;
	bool full_current;
	u8 ilim_mask;
	int ret;

	if (current_ua < smbchg_lc_ilim_options[0])
		/* Target current limit too small */
		return -EINVAL;

	ilim_mask = find_closest_smaller(current_ua, smbchg_lc_ilim_options,
					 ARRAY_SIZE(smbchg_lc_ilim_options));

	usb_3 = ilim_mask & LC_ILIM_USB3_BIT;
	full_current = ilim_mask & LC_ILIM_FULL_CURRENT_BIT;

	/* Set USB version */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_USB_CHGPTH_CFG,
					 CFG_USB3P0_SEL_BIT,
					 usb_3 ? USB_3P0_SEL : USB_2P0_SEL);
	if (ret) {
		dev_err(chip->dev, "Failed to set USB version: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Set USB current level */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USB51_MODE_BIT,
				 full_current ? USB51_MODE_BIT : 0);
	if (ret) {
		dev_err(chip->dev, "Failed to set USB current level: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Disable high current mode */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USBIN_MODE_HC_BIT, 0);
	if (ret) {
		dev_err(chip->dev, "Failed to disable high current mode: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	dev_dbg(chip->dev,
		"LC mode current limit set to %duA (%duA requested)\n",
		smbchg_lc_ilim_options[ilim_mask], current_ua);

	return smbchg_lc_ilim_options[ilim_mask];
}

/**
 * @brief smbchg_usb_set_ilim_hc() - Set USB input current limit using
 * high current mode
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Target current limit in microamperes
 * @return Actual current limit in microamperes on success, -errno on failure
 *
 * @details: High current mode provides a large range of input current limits
 * with granular control, but does not reach limits as low as the low
 * current mode does. This mode is most suitable for use with DCPs and CDPs.
 * This will override and disable AICL.
 */
static int smbchg_usb_set_ilim_hc(struct smbchg_chip *chip, int current_ua)
{
	size_t ilim_index;
	int ilim;
	int ret;

	if (current_ua < chip->data->ilim_table[0])
		/* Target current limit too small */
		return -EINVAL;

	/*
	 * Get index of closest current limit supported by the charger.
	 * Always round down to avoid exceeding the target limit.
	 */
	ilim_index =
		find_closest_smaller(current_ua, chip->data->ilim_table,
				     (unsigned int)chip->data->ilim_table_len);
	ilim = chip->data->ilim_table[ilim_index];

	/* Set the current limit index */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_USB_CHGPTH_IL_CFG,
					 USBIN_INPUT_MASK, ilim_index);
	if (ret) {
		dev_err(chip->dev, "Failed to set current limit index: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/*
	 * Enable high current mode and override AICL to bring the current
	 * limit into effect
	 */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USBIN_MODE_HC_BIT | ICL_OVERRIDE_BIT,
				 USBIN_MODE_HC_BIT | ICL_OVERRIDE_BIT);
	if (ret) {
		dev_err(chip->dev, "Failed to set high current mode: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	dev_dbg(chip->dev,
		"HC mode current limit set to %duA (%duA requested)\n", ilim,
		current_ua);

	/* Disable AICL as it is no longer used */
	ret = qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_USB_CHGPTH_AICL_CFG,
		USB_CHGPTH_AICL_EN, 0);
	if (ret)
		/*
		 * Failing to disable AICL is not critical; it will continue
		 * to run but will not affect the input current limit as it
		 * has been overridden previously.
		 */
		dev_warn(chip->dev, "Failed to disable AICL: %pe\n",
			 ERR_PTR(ret));

	return ilim;
}

/**
 * @brief smbchg_usb_set_ilim() - Set USB input current limit
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Target current limit in microamperes
 * @return Actual current limit in microamperes on success, -errno on failure
 *
 * @details: Find a suitable current limiting mode and use it to set the
 * current limit.
 */
static int smbchg_usb_set_ilim(struct smbchg_chip *chip, int current_ua)
{
	size_t i;
	int ret;

	/*
	 * Disable USB charge path if the requested current limit is
	 * lower than the minimum supported limit.
	 */
	if (current_ua < smbchg_lc_ilim_options[0])
		return smbchg_usb_enable(chip, false);

	/*
	 * Use LC mode if the requested current limit matches one of
	 * its options. This would likely mean that the current limit
	 * is meant for a SDP.
	 */
	for (i = 0; i < ARRAY_SIZE(smbchg_lc_ilim_options); ++i) {
		if (current_ua == smbchg_lc_ilim_options[i]) {
			ret = smbchg_usb_set_ilim_lc(chip, current_ua);
			goto out;
		}
	}

	/*
	 * Use LC mode if the requested current limit mode is too low
	 * for HC mode.
	 */
	if (current_ua < chip->data->ilim_table[0]) {
		ret = smbchg_usb_set_ilim_lc(chip, current_ua);
		goto out;
	}

	/* Use HC mode otherwise */
	ret = smbchg_usb_set_ilim_hc(chip, current_ua);
out:
	/* Enable USB charge path if a valid current limit has been set */
	if (ret > 0)
		smbchg_usb_enable(chip, true);

	return ret;
}

/**
 * @brief smbchg_usb_aicl_enable() - Enable AICL on the USB charge path
 *
 * @param chip Pointer to smbchg_chip
 * @return 0 on success, -errno on failure
 *
 * @details: Enable Automatic Input Current Limiting (AICL) on the USB
 * charge path. This will automatically enforce a suitable input current
 * limit. Set a limit with smbchg_usb_set_ilim_hc to disable.
 */
static int smbchg_usb_aicl_enable(struct smbchg_chip *chip)
{
	int ret;

	/* Clear AICL override to make its input current limits effective */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 ICL_OVERRIDE_BIT, 0);
	if (ret) {
		dev_err(chip->dev, "Failed to clear ICL override: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/*
	 * Enable HC mode to use the limit set by AICL. On at least PMI8950
	 * this is also needed to for AICL to run at all.
	 */
	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_USB_CHGPTH_CMD_IL,
				 USBIN_MODE_HC_BIT, USBIN_MODE_HC_BIT);

	if (ret) {
		dev_err(chip->dev, "Failed to set high current mode: %pe\n",
			ERR_PTR(ret));
		return IRQ_NONE;
	}

	/*
	 * AICL limits itself by the HC mode current limit index set in the
	 * USB_CHGPTH_IL_CFG register. Set it to the maximum index to allow
	 * AICL to get the highest possible input current limit.
	 * Since AICL override was cleared, setting this index will not
	 * affect the live input current limit.
	 */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_USB_CHGPTH_IL_CFG,
					 USBIN_INPUT_MASK,
					 chip->data->ilim_table_len - 1);
	if (ret) {
		dev_err(chip->dev, "Failed to set current limit index: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Enable AICL */
	return qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_USB_CHGPTH_AICL_CFG,
		USB_CHGPTH_AICL_EN, USB_CHGPTH_AICL_EN);
}

/**
 * @brief smbchg_charging_enable() - Enable battery charging
 *
 * @param chip   Pointer to smbchg_chip
 * @param enable true to enable, false to disable
 * @return 0 on success, -errno on failure
 */
static int smbchg_charging_enable(struct smbchg_chip *chip, bool enable)
{
	int ret;

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_BAT_IF_CMD_CHG, CHG_EN_BIT,
				 enable ? 0 : CHG_EN_BIT);
	if (ret)
		dev_err(chip->dev, "Failed to enable battery charging: %pe\n",
			ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_charging_set_vfloat() - Set float voltage
 *
 * @param chip       Pointer to smbchg_chip
 * @param voltage_uv Float voltage in microvolts
 * @return Actual float voltage on success, -errno on failure
 */
static int smbchg_charging_set_vfloat(struct smbchg_chip *chip, int voltage_uv)
{
	unsigned int fv_index;
	int fv;
	int ret;

	if (voltage_uv < smbchg_fv_table[0] ||
	    voltage_uv > smbchg_fv_table[ARRAY_SIZE(smbchg_fv_table) - 1])
		/* Float voltage not supported */
		return -EINVAL;

	/*
	 * Float voltage is set by writing the index of the desired voltage
	 * in the float voltage table to the FV_CFG register. Indices start
	 * at 5.
	 */
	fv_index = 5 + find_closest_smaller(voltage_uv, smbchg_fv_table,
					    ARRAY_SIZE(smbchg_fv_table));

	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_CHGR_FV_CFG,
					 FV_MASK, fv_index);
	if (ret) {
		dev_err(chip->dev, "Failed to set float voltage: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	fv = smbchg_fv_table[fv_index - 5];

	dev_dbg(chip->dev, "Float voltage set to %duV (%duV requested)", fv,
		voltage_uv);

	return fv;
}

/**
 * @brief smbchg_charging_get_iterm() - Get charge termination current
 *
 * @param chip Pointer to smbchg_chip
 * @return Charge termination current in microamperes on success,
 * -errno on failure
 */
static int smbchg_charging_get_iterm(struct smbchg_chip *chip)
{
	unsigned int iterm_index;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_TCC_CFG,
			  &iterm_index);
	if (ret)
		return ret;

	iterm_index &= CHG_ITERM_MASK;

	return chip->data->iterm_table[iterm_index];
}

/**
 * @brief smbchg_charging_set_iterm() - Set charge termination current
 *
 * @param chip       Pointer to smbchg_chip
 * @param current_ua Termination current in microamperes
 * @return Actual charge termination current in microamperes on success,
 * -errno on failure
 */
static int smbchg_charging_set_iterm(struct smbchg_chip *chip, int current_ua)
{
	size_t iterm_count = chip->data->iterm_table_len;
	unsigned int iterm_index;
	int iterm;
	int ret;

	iterm_index =
		find_closest(current_ua, chip->data->iterm_table, iterm_count);

	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_CHGR_TCC_CFG,
					 CHG_ITERM_MASK, iterm_index);
	if (ret)
		return ret;

	iterm = chip->data->iterm_table[iterm_index];

	dev_dbg(chip->dev,
		"Termination current limit set to %duA (%duA requested)", iterm,
		current_ua);

	return iterm;
}

/**
 * @brief smbchg_charging_get_ilim() - Get constant charge current limit
 *
 * @param chip       Pointer to smbchg_chip
 * @return Constant charge current limit in microamperes on success, -errno
 * on failure
 */
static int smbchg_charging_get_ilim(struct smbchg_chip *chip)
{
	u32 value;
	int ret;

	/*
	 * Read the ilim index set in the FCC configuration register and
	 * look up the corresponding current limit in the ilim table.
	 */
	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_FCC_CFG,
			  &value);
	if (ret)
		return ret;

	return chip->data->ilim_table[value & FCC_MASK];
}

/**
 * @brief smbchg_charging_set_ilim() - Set constant charge current limit
 *
 * @param chip       Pointer to smbchg_chip
 * @return Constant charge current limit in microamperes on success, -errno
 * on failure
 */
static int smbchg_charging_set_ilim(struct smbchg_chip *chip, int current_ua)
{
	size_t ilim_index;
	int ilim;
	int ret;

	if (current_ua < chip->data->ilim_table[0])
		/* Target current limit too small */
		return -EINVAL;

	/*
	 * Get index of closest current limit supported by the charger.
	 * Always round down to avoid exceeding the target limit.
	 */
	ilim_index =
		find_closest_smaller(current_ua, chip->data->ilim_table,
				     (unsigned int)chip->data->ilim_table_len);
	ilim = chip->data->ilim_table[ilim_index];

	/* Set the current limit index */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_CHGR_FCC_CFG,
					 FCC_MASK, ilim_index);
	if (ret) {
		dev_err(chip->dev,
			"Failed to set constant charge current limit: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	dev_dbg(chip->dev,
		"Constant charge current limit set to %duA (%duA requested)\n",
		ilim, current_ua);

	return ilim;
}

/**
 * @brief smbchg_batt_is_present() - Check for battery presence
 *
 * @param chip Pointer to smbchg_chip
 * @return true if battery present, false otherwise
 */
static bool smbchg_batt_is_present(struct smbchg_chip *chip)
{
	unsigned int value;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_BAT_IF_RT_STS,
			  &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read battery real-time status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	return !(value & BAT_MISSING_BIT);
}

/**
 * @brief smbchg_otg_is_present() - Check for OTG presence
 *
 * @param chip Pointer to smbchg_chip
 * @return true if OTG present, false otherwise
 */
static bool smbchg_otg_is_present(struct smbchg_chip *chip)
{
	u32 value;
	u16 usb_id;
	int ret;

	/* Check ID pin */
	ret = regmap_bulk_read(chip->regmap,
			       chip->base + SMBCHG_USB_CHGPTH_USBID_MSB, &value,
			       2);
	if (ret) {
		dev_err(chip->dev, "Failed to read ID pin: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	put_unaligned_be16(value, &usb_id);
	dev_vdbg(chip->dev, "0x%04x read on ID pin\n", usb_id);
	if (usb_id > USBID_GND_THRESHOLD)
		return false;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_USB_CHGPTH_RID_STS,
			  &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read ID resistance status: %pe\n",
			ERR_PTR(ret));
		return false;
	}

	return (value & RID_MASK) == 0;
}

/**
 * @brief smbchg_otg_enable() - Enable OTG regulator
 *
 * @param rdev Pointer to regulator_dev
 * @return 0 on success, -errno on failure
 */
static int smbchg_otg_enable(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	int ret;

	dev_dbg(chip->dev, "Enabling OTG VBUS regulator");

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_BAT_IF_CMD_CHG, OTG_EN_BIT,
				 OTG_EN_BIT);
	if (ret)
		dev_err(chip->dev, "Failed to enable OTG regulator: %pe\n",
			ERR_PTR(ret));

	return ret;
}

/**
 * @brief smbchg_otg_disable() - Disable OTG regulator
 *
 * @param rdev Pointer to regulator_dev
 * @return 0 on success, -errno on failure
 */
static int smbchg_otg_disable(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	int ret;

	dev_dbg(chip->dev, "Disabling OTG VBUS regulator");

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_BAT_IF_CMD_CHG, OTG_EN_BIT,
				 0);
	if (ret) {
		dev_err(chip->dev, "Failed to disable OTG regulator: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	return 0;
}

/**
 * @brief smbchg_otg_is_enabled() - Check if OTG regulator is enabled
 *
 * @param rdev Pointer to regulator_dev
 * @return int 1 if enabled, 0 if disabled
 */
static int smbchg_otg_is_enabled(struct regulator_dev *rdev)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	u32 value = 0;
	int ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_BAT_IF_CMD_CHG,
			  &value);
	if (ret)
		dev_err(chip->dev, "Failed to read OTG regulator status\n");

	return !!(value & OTG_EN_BIT);
}

static const struct regulator_ops smbchg_otg_ops = {
	.enable = smbchg_otg_enable,
	.disable = smbchg_otg_disable,
	.is_enabled = smbchg_otg_is_enabled,
};

static void smbchg_otg_reset_worker(struct work_struct *work)
{
	struct smbchg_chip *chip =
		container_of(work, struct smbchg_chip, otg_reset_work);
	int ret;

	dev_dbg(chip->dev, "Resetting OTG VBUS regulator\n");

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_BAT_IF_CMD_CHG, OTG_EN_BIT,
				 0);
	if (ret) {
		dev_err(chip->dev,
			"Failed to disable OTG regulator for reset: %pe\n",
			ERR_PTR(ret));
		return;
	}

	msleep(OTG_RESET_DELAY_MS);

	/*
	 * Only re-enable the OTG regulator if OTG is still present
	 * after sleeping
	 */
	if (!smbchg_otg_is_present(chip))
		return;

	ret = regmap_update_bits(chip->regmap,
				 chip->base + SMBCHG_BAT_IF_CMD_CHG, OTG_EN_BIT,
				 OTG_EN_BIT);
	if (ret)
		dev_err(chip->dev,
			"Failed to re-enable OTG regulator after reset: %pe\n",
			ERR_PTR(ret));
}

/**
 * @brief smbchg_extcon_update() - Update extcon properties and sync cables
 *
 * @param chip Pointer to smbchg_chip
 */
static void smbchg_extcon_update(struct smbchg_chip *chip)
{
	enum power_supply_usb_type usb_type = smbchg_usb_get_type(chip);
	bool usb_present = smbchg_usb_is_present(chip);
	bool otg_present = smbchg_otg_is_present(chip);
	int otg_vbus_present = smbchg_otg_is_enabled(chip->otg_reg);

	extcon_set_state(chip->edev, EXTCON_USB, usb_present);
	extcon_set_state(chip->edev, EXTCON_USB_HOST, otg_present);
	extcon_set_property(chip->edev, EXTCON_USB_HOST, EXTCON_PROP_USB_VBUS,
			    (union extcon_property_value)otg_vbus_present);

	if (usb_present) {
		extcon_set_state(chip->edev, EXTCON_CHG_USB_SDP,
				 usb_type == POWER_SUPPLY_USB_TYPE_SDP);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_DCP,
				 usb_type == POWER_SUPPLY_USB_TYPE_DCP);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_CDP,
				 usb_type == POWER_SUPPLY_USB_TYPE_CDP);
		extcon_set_property(
			chip->edev, EXTCON_USB, EXTCON_PROP_USB_VBUS,
			(union extcon_property_value)(
				usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN));
	} else {
		/*
		 * Charging extcon cables and VBUS are unavailable when
		 * USB is not present.
		 */
		extcon_set_state(chip->edev, EXTCON_CHG_USB_SDP, false);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_DCP, false);
		extcon_set_state(chip->edev, EXTCON_CHG_USB_CDP, false);
		extcon_set_property(chip->edev, EXTCON_USB,
				    EXTCON_PROP_USB_VBUS,
				    (union extcon_property_value) false);
	}

	/* Sync all extcon cables */
	extcon_sync(chip->edev, EXTCON_USB);
	extcon_sync(chip->edev, EXTCON_USB_HOST);
	extcon_sync(chip->edev, EXTCON_CHG_USB_SDP);
	extcon_sync(chip->edev, EXTCON_CHG_USB_DCP);
	extcon_sync(chip->edev, EXTCON_CHG_USB_CDP);
}

static const unsigned int smbchg_extcon_cable[] = {
	EXTCON_USB,	    EXTCON_USB_HOST,	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP, EXTCON_CHG_USB_CDP, EXTCON_NONE,
};

static irqreturn_t smbchg_handle_charger_error(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_err(chip->dev, "Charger error");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_p2f(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Fast charging threshold reached");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_rechg(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Recharge threshold reached");

	/* Auto-recharge is enabled, nothing to do here */
	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_taper(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Taper charging threshold reached");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_tcc(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_dbg(chip->dev, "Termination current reached");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_batt_temp(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_batt_presence(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	bool batt_present = smbchg_batt_is_present(chip);

	dev_dbg(chip->dev, "Battery %spresent\n", batt_present ? "" : "not ");

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_usb_source_detect(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	bool usb_present;
	enum power_supply_usb_type usb_type;
	int ret;

	usb_present = smbchg_usb_is_present(chip);
	dev_dbg(chip->dev, "USB %spresent\n", usb_present ? "" : "not ");

	usb_type = smbchg_usb_get_type(chip);

	/*
	 * Prepare for running AICL to find a suitable input current
	 * limit if connected to a DCP or a CDP
	 */
	if (usb_present && (usb_type == POWER_SUPPLY_USB_TYPE_DCP ||
			    usb_type == POWER_SUPPLY_USB_TYPE_CDP)) {
		/*
		 * Enable AICL to find a suitable current limit for the
		 * newly plugged into port.
		 */
		ret = smbchg_usb_aicl_enable(chip);
		if (ret) {
			dev_err(chip->dev, "Failed to enable AICL: %pe\n",
				ERR_PTR(ret));
			return IRQ_NONE;
		}

		/* Enable USB charge path to make AICL run and start charging */
		ret = smbchg_usb_enable(chip, true);
		if (ret) {
			dev_err(chip->dev,
				"Failed to enable USB charge path: %pe\n",
				ERR_PTR(ret));
			return IRQ_NONE;
		}
	}

	smbchg_extcon_update(chip);
	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_usbid_change(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	bool otg_present;

	/*
	 * ADC conversion for USB ID resistance in the fuel gauge can take
	 * up to 15ms to finish after the USB ID change interrupt is fired.
	 * Wait for it to finish before detecting OTG presence. Add an extra
	 * 5ms for good measure.
	 */
	msleep(20);

	otg_present = smbchg_otg_is_present(chip);
	dev_dbg(chip->dev, "OTG %spresent\n", otg_present ? "" : "not ");

	smbchg_extcon_update(chip);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_otg_fail(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	dev_err(chip->dev, "OTG regulator failure");

	/* Report failure */
	regulator_notifier_call_chain(chip->otg_reg, REGULATOR_EVENT_FAIL,
				      NULL);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_otg_oc(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	/*
	 * Inrush current of some devices can trip the OTG over-current
	 * protection on the PMI8994 and PMI8996 chargers due to
	 * a hardware bug.
	 * Try resetting the OTG regulator a few times, and only report
	 * over-current if it persists.
	 */
	if (chip->data->reset_otg_on_oc) {
		if (chip->otg_resets < NUM_OTG_RESET_RETRIES) {
			schedule_work(&chip->otg_reset_work);
			chip->otg_resets++;
			return IRQ_HANDLED;
		}

		chip->otg_resets = 0;
	}

	dev_warn(chip->dev, "OTG over-current");

	/* Report over-current */
	regulator_notifier_call_chain(chip->otg_reg,
				      REGULATOR_EVENT_OVER_CURRENT, NULL);

	/* Regulator is automatically disabled in hardware on over-current */
	regulator_notifier_call_chain(chip->otg_reg, REGULATOR_EVENT_DISABLE,
				      NULL);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_aicl_done(int irq, void *data)
{
	struct smbchg_chip *chip = data;
	int ilim;
	int ret;

	dev_dbg(chip->dev, "AICL done");

	ilim = smbchg_usb_get_ilim(chip);
	if (ilim < 0)
		dev_warn(chip->dev, "Failed to read AICL result: %pe\n",
			 ERR_PTR(ret));
	else
		dev_dbg(chip->dev, "AICL result: %uuA", ilim);

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbchg_handle_temp_shutdown(int irq, void *data)
{
	struct smbchg_chip *chip = data;

	hw_protection_trigger("Charger thermal emergency", 100);

	smbchg_charging_enable(chip, false);
	smbchg_usb_enable(chip, false);

	power_supply_changed(chip->usb_psy);

	return IRQ_HANDLED;
}

const struct smbchg_irq smbchg_irqs[] = {
	{ "chg-error", smbchg_handle_charger_error },
	{ "chg-inhibit", NULL },
	{ "chg-prechg-sft", NULL },
	{ "chg-complete-chg-sft", NULL },
	{ "chg-p2f-thr", smbchg_handle_p2f },
	{ "chg-rechg-thr", smbchg_handle_rechg },
	{ "chg-taper-thr", smbchg_handle_taper },
	{ "chg-tcc-thr", smbchg_handle_tcc },
	{ "batt-hot", smbchg_handle_batt_temp },
	{ "batt-warm", smbchg_handle_batt_temp },
	{ "batt-cold", smbchg_handle_batt_temp },
	{ "batt-cool", smbchg_handle_batt_temp },
	{ "batt-ov", NULL },
	{ "batt-low", NULL },
	{ "batt-missing", smbchg_handle_batt_presence },
	{ "batt-term-missing", NULL },
	{ "usbin-uv", NULL },
	{ "usbin-ov", NULL },
	{ "usbin-src-det", smbchg_handle_usb_source_detect },
	{ "usbid-change", smbchg_handle_usbid_change },
	{ "otg-fail", smbchg_handle_otg_fail },
	{ "otg-oc", smbchg_handle_otg_oc },
	{ "aicl-done", smbchg_handle_aicl_done },
	{ "dcin-uv", NULL },
	{ "dcin-ov", NULL },
	{ "power-ok", NULL },
	{ "temp-shutdown", smbchg_handle_temp_shutdown },
	{ "wdog-timeout", NULL },
	{ "flash-fail", NULL },
	{ "otst2", NULL },
	{ "otst3", NULL },
};

/**
 * @brief smbchg_get_charge_type() - Get charge type
 *
 * @param chip Pointer to smbchg_chip
 * @return Charge type, as defined in <linux/power_supply.h>
 */
static int smbchg_get_charge_type(struct smbchg_chip *chip)
{
	int value, ret;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_STS, &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read charger status: %pe\n",
			ERR_PTR(ret));
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	value = (value & CHG_TYPE_MASK) >> CHG_TYPE_SHIFT;
	dev_vdbg(chip->dev, "Charge type: 0x%x", value);
	switch (value) {
	case BATT_NOT_CHG_VAL:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	case BATT_PRE_CHG_VAL:
		/* Low current precharging */
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case BATT_FAST_CHG_VAL:
		/* Constant current fast charging */
	case BATT_TAPER_CHG_VAL:
		/* Constant voltage fast charging */
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	default:
		dev_err(chip->dev, "Invalid charge type value 0x%x read\n",
			value);
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}
}

/**
 * @brief smbchg_get_health() - Get battery health
 *
 * @param chip Pointer to smbchg_chip
 * @return Battery health, as defined in <linux/power_supply.h>
 */
static int smbchg_get_health(struct smbchg_chip *chip)
{
	int value, ret;
	bool batt_present = smbchg_batt_is_present(chip);

	if (!batt_present)
		return POWER_SUPPLY_HEALTH_NO_BATTERY;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_BAT_IF_RT_STS,
			  &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read battery real-time status: %pe\n",
			ERR_PTR(ret));
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (value & HOT_BAT_HARD_BIT)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (value & HOT_BAT_SOFT_BIT)
		return POWER_SUPPLY_HEALTH_WARM;
	else if (value & COLD_BAT_HARD_BIT)
		return POWER_SUPPLY_HEALTH_COLD;
	else if (value & COLD_BAT_SOFT_BIT)
		return POWER_SUPPLY_HEALTH_COOL;

	return POWER_SUPPLY_HEALTH_GOOD;
}

/**
 * @brief smbchg_get_status() - Get battery status
 *
 * @param chip Pointer to smbchg_chip
 * @return Battery status, as defined in <linux/power_supply.h>
 */
static int smbchg_get_status(struct smbchg_chip *chip)
{
	int value, ret, chg_type;

	/* Check if power input is present */
	/* TODO: Check if DC charge path is present once supported */
	if (!smbchg_usb_is_present(chip))
		return POWER_SUPPLY_STATUS_DISCHARGING;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_RT_STS,
			  &value);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read charger real-time status: %pe\n",
			ERR_PTR(ret));
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
	dev_vdbg(chip->dev, "Charger real-time status: 0x%x", value);

	/* Check if temination current is reached or if charging is inhibited */
	if (value & BAT_TCC_REACHED_BIT || value & CHG_INHIBIT_BIT)
		return POWER_SUPPLY_STATUS_FULL;

	ret = regmap_read(chip->regmap, chip->base + SMBCHG_CHGR_STS, &value);
	if (ret) {
		dev_err(chip->dev, "Failed to read charger status: %pe\n",
			ERR_PTR(ret));
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
	dev_vdbg(chip->dev, "Charger status: 0x%x", value);

	/* Check for charger hold-off */
	if (value & CHG_HOLD_OFF_BIT)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	chg_type = smbchg_get_charge_type(chip);
	switch (chg_type) {
	case POWER_SUPPLY_CHARGE_TYPE_UNKNOWN:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	case POWER_SUPPLY_CHARGE_TYPE_NONE:
		return POWER_SUPPLY_STATUS_DISCHARGING;
	default:
		return POWER_SUPPLY_STATUS_CHARGING;
	}
}

static int smbchg_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = smbchg_get_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = smbchg_get_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = smbchg_get_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = smbchg_usb_is_present(chip);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = smbchg_charging_get_ilim(chip);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chip->batt_info->constant_charge_current_max_ua;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = smbchg_usb_get_ilim(chip);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = smbchg_usb_get_type(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = smbchg_charging_get_iterm(chip);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	default:
		dev_err(chip->dev, "Invalid property: %d\n", psp);
		return -EINVAL;
	}

	return 0;
}

static int smbchg_set_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       const union power_supply_propval *val)
{
	struct smbchg_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = smbchg_usb_enable(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		/*
		 * Prevent exceeding the maximum constant charge current
		 * allowed by the battery
		 */
		if (val->intval >
		    chip->batt_info->constant_charge_current_max_ua)
			return -EINVAL;

		ret = smbchg_charging_set_ilim(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = smbchg_usb_set_ilim(chip, val->intval);
		if (ret > 0)
			ret = 0;
		break;
	default:
		dev_err(chip->dev, "Invalid property: %d\n", psp);
		return -EINVAL;
	}

	return ret;
}

static int smbchg_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return true;
	default:
		return false;
	}

	return 0;
}

static enum power_supply_property smbchg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT
};

static const struct power_supply_desc smbchg_usb_psy_desc = {
	.name = "qcom-smbchg-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP),
	.properties = smbchg_props,
	.num_properties = ARRAY_SIZE(smbchg_props),
	.get_property = smbchg_get_property,
	.set_property = smbchg_set_property,
	.property_is_writeable = smbchg_property_is_writeable
};

/**
 * @brief smbchg_init() - Main initialization routine
 *
 * @param chip Pointer to smbchg_chip
 * @return 0 on success, -errno on failure
 *
 * @details Initialize charger hardware for USB charging.
 */
static int smbchg_init(struct smbchg_chip *chip)
{
	int ret;

	/*
	 * Charger configuration, part 1:
	 * - Set recharge voltage reading source to fuel gauge
	 * - Set charge termination current reading source to fuel gauge
	 */
	ret = qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_CHGR_CHGR_CFG1,
		RECHG_THRESHOLD_SRC_BIT | TERM_I_SRC_BIT,
		RCHG_SRC_FG | TERM_SRC_FG);
	if (ret)
		return ret;

	/*
	 * Charger configuration, part 2:
	 * - Enable charger inhibition
	 * - Enable automatic recharge
	 * - Enable current termination
	 * - Set pre to fast charging transition to automatic
	 * - Set charge enable source to command
	 */
	ret = qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_CHGR_CHGR_CFG2,
		CHARGER_INHIBIT_BIT | AUTO_RECHG_BIT | I_TERM_BIT |
			P2F_CHG_TRAN_BIT | CHG_EN_SRC_BIT,
		CHG_INHIBIT_EN | AUTO_RCHG_EN | CURRENT_TERM_EN |
			PRE_FAST_AUTO | CHG_EN_SRC_CMD);
	if (ret)
		return ret;

	/* Set recharge threshold to 100mV */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_CHGR_CFG,
					 RCHG_LVL_BIT, RCHG_THRESH_100MV);
	if (ret)
		return ret;

	/* Set termination current */
	if (chip->batt_info->charge_term_current_ua != -EINVAL) {
		ret = smbchg_charging_set_iterm(
			chip, chip->batt_info->charge_term_current_ua);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to set termination current: %pe\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	/* Set constant charge current limit */
	ret = smbchg_charging_set_ilim(
		chip, chip->batt_info->constant_charge_current_max_ua);
	if (ret < 0) {
		dev_err(chip->dev,
			"Failed to set constant charge current limit: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Set float voltage */
	ret = smbchg_charging_set_vfloat(
		chip, chip->batt_info->voltage_max_design_uv);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to set float voltage: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Enable charging */
	ret = smbchg_charging_enable(chip, true);
	if (ret) {
		dev_err(chip->dev, "Failed to enable charging: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/*
	 * USB charge path configuration:
	 * - Set USB charge path control to command
	 * - Set command polarity to full current mode (i.e. setting
	 *   USB_CHGPTH_CMD_IL:USB51_MODE_BIT corresponds to full SDP current)
	 */
	ret = qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_USB_CHGPTH_CFG,
		USB51AC_CTRL | USB51_COMMAND_POL,
		USB51_COMMAND_CONTROL | USB51AC_COMMAND1_500);
	if (ret)
		return ret;

	/* Enable APSD */
	ret = qcom_pmic_sec_masked_write(
		chip->regmap, chip->base + SMBCHG_USB_CHGPTH_APSD_CFG,
		USB_CHGPTH_APSD_EN, USB_CHGPTH_APSD_EN);
	if (ret) {
		dev_err(chip->dev, "Failed to enable APSD: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Enable periodic AICL rerun on the USB charge path */
	ret = qcom_pmic_sec_masked_write(chip->regmap,
					 chip->base + SMBCHG_MISC_TRIM_OPT_15_8,
					 AICL_RERUN_MASK, AICL_RERUN_USB_BIT);
	if (ret) {
		dev_err(chip->dev, "Failed to enable AICL rerun: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/*
	 * Call the USB source detect handler once to set USB current limit
	 * and enable the charge path if USB is present.
	 */
	smbchg_handle_usb_source_detect(0, chip);

	return 0;
}

static int smbchg_probe(struct platform_device *pdev)
{
	struct smbchg_chip *chip;
	struct regulator_config config = {};
	struct power_supply_config supply_config = {};
	int i, irq, ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Failed to get regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(chip->dev->of_node, "reg", &chip->base);
	if (ret) {
		dev_err(chip->dev, "Failed to get base address: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	spin_lock_init(&chip->sec_access_lock);
	INIT_WORK(&chip->otg_reset_work, smbchg_otg_reset_worker);

	/* Initialize OTG regulator */
	chip->otg_rdesc.id = -1;
	chip->otg_rdesc.name = "otg-vbus";
	chip->otg_rdesc.ops = &smbchg_otg_ops;
	chip->otg_rdesc.owner = THIS_MODULE;
	chip->otg_rdesc.type = REGULATOR_VOLTAGE;
	chip->otg_rdesc.of_match = "otg-vbus";

	config.dev = chip->dev;
	config.driver_data = chip;

	chip->otg_reg =
		devm_regulator_register(chip->dev, &chip->otg_rdesc, &config);
	if (IS_ERR(chip->otg_reg)) {
		dev_err(chip->dev,
			"Failed to register OTG VBUS regulator: %pe\n",
			chip->otg_reg);
		return PTR_ERR(chip->otg_reg);
	}

	chip->data = of_device_get_match_data(chip->dev);

	supply_config.drv_data = chip;
	supply_config.of_node = pdev->dev.of_node;
	chip->usb_psy = devm_power_supply_register(
		chip->dev, &smbchg_usb_psy_desc, &supply_config);
	if (IS_ERR(chip->usb_psy)) {
		dev_err(chip->dev, "Failed to register USB power supply: %pe\n",
			chip->usb_psy);
		return PTR_ERR(chip->usb_psy);
	}

	ret = power_supply_get_battery_info(chip->usb_psy, &chip->batt_info);
	if (ret) {
		dev_err(chip->dev, "Failed to get battery info: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (chip->batt_info->voltage_max_design_uv == -EINVAL) {
		dev_err(chip->dev,
			"Battery info missing maximum design voltage\n");
		return -EINVAL;
	}

	if (chip->batt_info->constant_charge_current_max_ua == -EINVAL) {
		dev_err(chip->dev,
			"Battery info missing maximum constant charge current\n");
		return -EINVAL;
	}

	/* Initialize extcon */
	chip->edev = devm_extcon_dev_allocate(chip->dev, smbchg_extcon_cable);
	if (IS_ERR(chip->edev)) {
		dev_err(chip->dev, "Failed to allocate extcon device: %pe\n",
			chip->edev);
		return PTR_ERR(chip->edev);
	}

	ret = devm_extcon_dev_register(chip->dev, chip->edev);
	if (ret) {
		dev_err(chip->dev, "Failed to register extcon device: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	extcon_set_property_capability(chip->edev, EXTCON_USB,
				       EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(chip->edev, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_VBUS);

	/* Initialize charger */
	ret = smbchg_init(chip);
	if (ret)
		return ret;

	/* Request IRQs */
	for (i = 0; i < ARRAY_SIZE(smbchg_irqs); ++i) {
		/* IRQ unused */
		if (!smbchg_irqs[i].handler)
			continue;

		irq = of_irq_get_byname(pdev->dev.of_node, smbchg_irqs[i].name);
		if (irq < 0) {
			dev_err(chip->dev, "Failed to get %s IRQ: %pe\n",
				smbchg_irqs[i].name, ERR_PTR(irq));
			return irq;
		}

		ret = devm_request_threaded_irq(chip->dev, irq, NULL,
						smbchg_irqs[i].handler,
						IRQF_ONESHOT,
						smbchg_irqs[i].name, chip);
		if (ret) {
			dev_err(chip->dev, "failed to request %s IRQ: %pe\n",
				smbchg_irqs[i].name, ERR_PTR(irq));
			return ret;
		}
	}

	platform_set_drvdata(pdev, chip);

	return 0;
}

static void smbchg_remove(struct platform_device *pdev)
{
	struct smbchg_chip *chip = platform_get_drvdata(pdev);

	smbchg_usb_enable(chip, false);
	smbchg_charging_enable(chip, false);
}

static const struct of_device_id smbchg_id_table[] = {
	{ .compatible = "qcom,pmi8994-smbchg", .data = &smbchg_pmi8994_data },
	{ .compatible = "qcom,pmi8996-smbchg", .data = &smbchg_pmi8996_data },
	{}
};
MODULE_DEVICE_TABLE(of, smbchg_id_table);

static struct platform_driver smbchg_driver = {
	.probe = smbchg_probe,
	.remove = smbchg_remove,
	.driver = {
		.name = "qcom-smbchg",
		.of_match_table = smbchg_id_table,
	},
};
module_platform_driver(smbchg_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_AUTHOR("Alejandro Tafalla <atafalla@dnyon.com>");
MODULE_DESCRIPTION("Qualcomm PMIC switch-mode battery charger driver");
MODULE_LICENSE("GPL");
