// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/usb/typec.h>
#include <linux/usb/ucsi_glink.h>
#include <linux/soc/qcom/wcd939x-i2c.h>
#include <linux/qti-regmap-debugfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/pm_runtime.h>
#include "wcd-usbss-priv.h"
#include "wcd-usbss-registers.h"
#include "wcd-usbss-reg-masks.h"
#include "wcd-usbss-reg-shifts.h"

#define WCD_USBSS_I2C_NAME	"wcd-usbss-i2c-driver"

#define DEFAULT_SURGE_TIMER_PERIOD_MS 15000
#define SEC_TO_MS 1000
#define NUM_RCO_MISC2_READ 10
#define MIN_SURGE_TIMER_PERIOD_SEC 3
#define MAX_SURGE_TIMER_PERIOD_SEC 20

enum {
	WCD_USBSS_AUDIO_MANUAL,
	WCD_USBSS_AUDIO_FSM,
};

enum {
	WCD_USBSS_1_X,
	WCD_USBSS_2_0,
};

struct wcd_usbss_reg_mask_val {
	u16 reg;
	u8 mask;
	u8 val;
};

/* regulator power supply names */
static const char * const supply_names[] = {
	"vdd-usb-cp",
};

static const u8 wcd_usbss_reg_access[WCD_USBSS_NUM_REGISTERS];
static bool config_standby;
static int audio_fsm_mode = WCD_USBSS_AUDIO_MANUAL;

/* Linearlizer coefficients for 32ohm load */
static const struct wcd_usbss_reg_mask_val coeff_init[] = {
	{WCD_USBSS_AUD_COEF_L_K5_0,       0xFF, 0x39},
	{WCD_USBSS_AUD_COEF_R_K5_0,       0xFF, 0x39},
	{WCD_USBSS_GND_COEF_L_K2_0,       0xFF, 0xE8},
	{WCD_USBSS_GND_COEF_L_K4_0,       0xFF, 0x73},
	{WCD_USBSS_GND_COEF_R_K2_0,       0xFF, 0xE8},
	{WCD_USBSS_GND_COEF_R_K4_0,       0xFF, 0x73},
	{WCD_USBSS_RATIO_SPKR_REXT_L_LSB, 0xFF, 0x00},
	{WCD_USBSS_RATIO_SPKR_REXT_L_MSB, 0x7F, 0x04},
	{WCD_USBSS_RATIO_SPKR_REXT_R_LSB, 0xFF, 0x00},
	{WCD_USBSS_RATIO_SPKR_REXT_R_MSB, 0x7F, 0x04},
};

static struct wcd_usbss_ctxt *wcd_usbss_ctxt_;

/* Required for kobj_attributes */
static ssize_t wcd_usbss_surge_enable_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count);

static ssize_t wcd_usbss_surge_period_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count);

static ssize_t wcd_usbss_standby_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count);

static struct kobj_attribute wcd_usbss_surge_enable_attribute =
	__ATTR(surge_enable, 0220, NULL, wcd_usbss_surge_enable_store);

static struct kobj_attribute wcd_usbss_surge_period_attribute =
	__ATTR(surge_period, 0220, NULL, wcd_usbss_surge_period_store);

static struct kobj_attribute wcd_usbss_standby_enable_attribute =
	__ATTR(standby_mode, 0220, NULL, wcd_usbss_standby_store);

/**
 * wcd_usbss_sbu_switch_orientation() - Determine SBU switch orientation based on switch settings.
 *
 * This function is used to determine SBU switch orientation of the WCD USBSS. INVALID_ORIENTATION
 * in enum wcd_usbss_sbu_switch_orientation represents an error state where none of the defined
 * orientations can be inferred by the switch settings.
 *
 * Return: Returns an enum wcd_usbss_sbu_switch_orientation to client. INVALID_ORIENTATION is
 *	   returned if the driver is not probed or if undefined switch settings are discovered.
 */
enum wcd_usbss_sbu_switch_orientation wcd_usbss_get_sbu_switch_orientation(void)
{
	unsigned int read_val = 0;

	/* check if driver is probed and private context is init'ed */
	if (wcd_usbss_ctxt_ == NULL)
		return INVALID_ORIENTATION;

	if (!wcd_usbss_ctxt_->regmap)
		return INVALID_ORIENTATION;

	regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_SWITCH_SELECT0, &read_val);
	if ((read_val & 0x3) == 0x1)
		return GND_SBU1_ORIENTATION_B;
	if ((read_val & 0x3) == 0x2)
		return GND_SBU2_ORIENTATION_A;
	return INVALID_ORIENTATION;
}
EXPORT_SYMBOL(wcd_usbss_get_sbu_switch_orientation);

/*
 * wcd_usbss_set_switch_settings_enable() - Configure a specified WCD USBSS switch.
 * @switch_type: Switch to be enabled/disabled.
 * @switch_setting: Enable or disable.
 *
 * This function will set or reset a specific bit in the WCD_USBSS_SWITCH_SETTINGS_ENABLE register.
 * There is a check that switch_type represents a bit in this register. Update the definition of
 * enum wcd_usbss_switch_type switch_type if the bits in WCD_USBSS_SWITCH_SETTINGS_ENABLE change.
 *
 * Return : Returns int on whether the switch configuration happened or not. -ENODEV is returned if
 *	    the driver is not probed.
 */
int wcd_usbss_set_switch_settings_enable(enum wcd_usbss_switch_type switch_type,
					 enum wcd_usbss_switch_state switch_state)
{
	/* check if driver is probed and private context is initialized */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if ((!wcd_usbss_ctxt_->regmap) || (switch_type < MIN_SWITCH_TYPE_NUM) ||
	    (switch_type > MAX_SWITCH_TYPE_NUM) ||
	    (switch_state != USBSS_SWITCH_DISABLE && switch_state != USBSS_SWITCH_ENABLE))
		return -EINVAL;

	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE,
			   1 << switch_type, switch_state << switch_type);
	return 0;
}
EXPORT_SYMBOL(wcd_usbss_set_switch_settings_enable);

/*
 * wcd_usbss_linearizer_rdac_cal_code_select() - Configure the linearizer calibration codes source.
 *
 * @source: HW (hardware) or SW (software).
 *
 * This function configures the linearizer to use SW or HW as the sources for the calibration codes.
 *
 * Return: Returns int on whether the switch configuration happened or not. -ENODEV is returned if
 *	   the driver is not probed.
 */
int wcd_usbss_linearizer_rdac_cal_code_select(enum linearizer_rdac_cal_code_select source)
{
	/* check if driver is probed and private context is initialized */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if ((!wcd_usbss_ctxt_->regmap) || (source != LINEARIZER_SOURCE_HW &&
					   source != LINEARIZER_SOURCE_SW))
		return -EINVAL;

	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_FUNCTION_ENABLE, 0x4, source << 2);
	return 0;
}
EXPORT_SYMBOL(wcd_usbss_linearizer_rdac_cal_code_select);

/*
 * wcd_usbss_set_linearizer_sw_tap() - Configure linearizer audio and ground software tap values.
 *
 * @aud_tap: 10-bit tap code for the L and R audio software tap registers.
 * @gnd_tap: 10-bit tap code for the L and R ground software tap registers.
 *
 * This function writes tap values to the left and right tap registers for the audio and ground
 * FETs. Note that the tap values are 10 bits and cannot exceed 0x3FF, but they can be 0.
 *
 * Return: Returns int on whether the switch configuration happened or not. -ENODEV is returned if
 *	   the driver is not probed.
 */
int wcd_usbss_set_linearizer_sw_tap(uint32_t aud_tap, uint32_t gnd_tap)
{
	uint32_t lsb_mask = 0xFF, msb_shift = 8;

	/* check if driver is probed and private context is initialized */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if ((!wcd_usbss_ctxt_->regmap) || aud_tap > 0x3FF || gnd_tap > 0x3FF)
		return -EINVAL;

	/* Audio left */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_AUD_L_LSB, 0xFF,
			   aud_tap & lsb_mask);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_AUD_L_MSB, 0x3,
			   aud_tap >> msb_shift);
	/* Audio right */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_AUD_R_LSB, 0xFF,
			   aud_tap & lsb_mask);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_AUD_R_MSB, 0x3,
			   aud_tap >> msb_shift);
	/* Ground left */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_GND_L_LSB, 0xFF,
			   gnd_tap & lsb_mask);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_GND_L_MSB, 0x3,
			   gnd_tap >> msb_shift);
	/* Ground right */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_GND_R_LSB, 0xFF,
			   gnd_tap & lsb_mask);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_TAP_GND_R_MSB, 0x3,
			   gnd_tap >> msb_shift);
	return 0;
}
EXPORT_SYMBOL(wcd_usbss_set_linearizer_sw_tap);

static bool wcd_usbss_readable_register(struct device *dev, unsigned int reg)
{
	if (reg <= (WCD_USBSS_BASE + 1))
		return false;

	if ((wcd_usbss_ctxt_ && wcd_usbss_ctxt_->version == WCD_USBSS_1_X) &&
			(reg >= WCD_USBSS_EFUSE_CTL &&
			reg <= WCD_USBSS_ANA_CSR_DBG_CTL))
		return false;

	return wcd_usbss_reg_access[WCD_USBSS_REG(reg)] & RD_REG;
}

/*
 * wcd_usbss_is_in_reset_state() - Check whether a negative surge ESD event has occurred.
 *
 * This function has a series of three checks to determine whether a negative surge ESD event has
 * occurred. If any of the three check conditions is met, it is concluded that a negative surge
 * ESD event has occurred. The checks include the following:
 * 1. Register WCD_USBSS_CPLDO_CTL2 reads 0xFF
 * 2. Register WCD_USBSS_RCO_MISC2 Bit<1> reads 0 at least once in NUM_RCO_MISC2_READ reads
 * 3. Register 0x06 Bit<0> reads 1 after toggling register WCD_USBSS_PMP_MISC1 Bit<0> from
 *    0 --> 1 --> 0
 *
 * Return: Returns true if any check(s) fail, false otherwise.
 */
static bool wcd_usbss_is_in_reset_state(void)
{
	bool ret = false;
	int i = 0;
	int rc = 0;
	unsigned int read_val = 0;
	struct device *i2c_bus_dev = wcd_usbss_ctxt_->client->adapter->dev.parent;
	bool disable_rpm = false;

	if (!pm_runtime_enabled(i2c_bus_dev)) {
		pm_runtime_enable(i2c_bus_dev);
		disable_rpm = true;
	}

	/* Check 1: Read WCD_USBSS_CPLDO_CTL2 */
	rc = regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_CPLDO_CTL2, &read_val);
	if (rc != 0)
		goto done;

	if (read_val != 0xFF) {
		dev_err(wcd_usbss_ctxt_->dev, "%s: Surge check #1 failed\n", __func__);
		ret = true;
		goto done;
	}

	/* Check 2: Read WCD_USBSS_RCO_MISC2 */
	for (i = 0; i < NUM_RCO_MISC2_READ; i++) {
		rc = regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_RCO_MISC2, &read_val);
		if (rc != 0)
			goto done;

		if ((read_val & 0x2) == 0)
			break;
		if (i == (NUM_RCO_MISC2_READ - 1)) {
			dev_err(wcd_usbss_ctxt_->dev, "%s: Surge check #2 failed\n", __func__);
			ret = true;
			goto done;
		}
	}

	/* Toggle WCD_USBSS_PMP_MISC1 bit<0>: 0 --> 1 --> 0 */
	rc = rc | regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x0);
	rc = rc | regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x1);
	rc = rc | regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x0);

	/* Check 3: Read WCD_USBSS_PMP_MISC2 */
	rc = rc | regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC2, &read_val);

	if (rc != 0)
		goto done;

	if ((read_val & 0x1) == 0) {
		dev_err(wcd_usbss_ctxt_->dev, "%s: Surge check #3 failed\n", __func__);
		ret = true;
	}

done:
	if (disable_rpm)
		pm_runtime_disable(i2c_bus_dev);

	/* All checks passed, so a negative surge ESD event has not occurred */
	return ret;
}

/*
 * wcd_usbss_reset_routine - Uses cached state to restore USB-SS registers after a negative surge.
 *
 * Return: Returns int return value from wcd_usbss_switch_update()
 */
static int wcd_usbss_reset_routine(void)
{
	struct device *i2c_bus_dev = wcd_usbss_ctxt_->client->adapter->dev.parent;
	bool disable_rpm = false;

	if (!pm_runtime_enabled(i2c_bus_dev)) {
		pm_runtime_enable(i2c_bus_dev);
		disable_rpm = true;
	}

	/* Mark the cache as dirty to force a flush */
	regcache_mark_dirty(wcd_usbss_ctxt_->regmap);
	regcache_sync(wcd_usbss_ctxt_->regmap);
	/* Write 0xFF to WCD_USBSS_CPLDO_CTL2 */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_CPLDO_CTL2, 0xFF, 0xFF);

	/* If in none audio mode, reset RCO */
	if (!(wcd_usbss_ctxt_->cable_status & (BIT(WCD_USBSS_AATC) |
					       BIT(WCD_USBSS_GND_MIC_SWAP_AATC) |
					       BIT(WCD_USBSS_HSJ_CONNECT) |
					       BIT(WCD_USBSS_GND_MIC_SWAP_HSJ)))) {
		/* Set RCO_EN: WCD_USBSS_USB_SS_CNTL Bit<3> --> 0x0 --> 0x1 */
		regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_USB_SS_CNTL, 0x8, 0x0);
		regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_USB_SS_CNTL, 0x8, 0x8);
	}

	if (disable_rpm)
		pm_runtime_disable(i2c_bus_dev);

	return 0;
}

static int wcd_usbss_standby_control(bool enter_standby)
{
	struct device *i2c_bus_dev = wcd_usbss_ctxt_->client->adapter->dev.parent;
	bool disable_rpm = false;

	if (!wcd_usbss_ctxt_->standby_enable)
		return 0;

	if (wcd_usbss_ctxt_->is_in_standby == enter_standby)
		return 0;

	mutex_lock(&wcd_usbss_ctxt_->standby_lock);

	if (!pm_runtime_enabled(i2c_bus_dev)) {
		pm_runtime_enable(i2c_bus_dev);
		disable_rpm = true;
	}

	if (enter_standby) {
		dev_dbg(wcd_usbss_ctxt_->dev, "%s: Enabling standby mode\n",
			__func__);
		regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_USB_SS_CNTL, 0x10, 0x10);
		wcd_usbss_ctxt_->is_in_standby = true;
	} else {
		dev_dbg(wcd_usbss_ctxt_->dev, "%s: Disabling standby mode\n",
			__func__);
		regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_USB_SS_CNTL, 0x10, 0x00);
		wcd_usbss_ctxt_->is_in_standby = false;
	}

	if (disable_rpm)
		pm_runtime_disable(i2c_bus_dev);

	mutex_unlock(&wcd_usbss_ctxt_->standby_lock);

	return 0;
}

static ssize_t wcd_usbss_surge_enable_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int enable = 0;

	if (kstrtouint(buf, 10, &enable) < 0)
		return -EINVAL;

	/* Return if period is 0ms */
	if (!wcd_usbss_ctxt_->surge_timer_period_ms)
		wcd_usbss_ctxt_->surge_timer_period_ms = DEFAULT_SURGE_TIMER_PERIOD_MS;

	wcd_usbss_ctxt_->surge_enable = enable;

	return count;
}

static ssize_t wcd_usbss_surge_period_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int period_sec = 0;

	if (kstrtouint(buf, 10, &period_sec) < 0)
		return -EINVAL;

	/* Constrain period */
	if (period_sec >= MIN_SURGE_TIMER_PERIOD_SEC && period_sec <= MAX_SURGE_TIMER_PERIOD_SEC)
		wcd_usbss_ctxt_->surge_timer_period_ms = SEC_TO_MS * period_sec;

	if (!wcd_usbss_ctxt_->surge_thread)
		return count;

	/* Wake up thread if usb is connected and surge is enabled */
	if (wcd_usbss_ctxt_->cable_status && wcd_usbss_ctxt_->surge_enable)
		wake_up_process(wcd_usbss_ctxt_->surge_thread);

	return count;
}

static ssize_t wcd_usbss_standby_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int enable = 0;

	if (kstrtouint(buf, 10, &enable) < 0)
		return -EINVAL;

	/* temporarily enabling standby to force proper state update */
	wcd_usbss_ctxt_->standby_enable = true;

	if (enable) {
		if (!wcd_usbss_ctxt_->cable_status)
			wcd_usbss_standby_control(true);
		else
			wcd_usbss_standby_control(false);
	} else {
		wcd_usbss_standby_control(false);
	}

	wcd_usbss_ctxt_->standby_enable = enable;

	return count;
}
/*
 * wcd_usbss_surge_kthread_fn - checks for a negative surge reset at a given period interval
 *
 * Returns 0
 */
static int wcd_usbss_surge_kthread_fn(void *p)
{
	while (!kthread_should_stop()) {
		if (wcd_usbss_ctxt_->cable_status &&
			wcd_usbss_ctxt_->surge_enable &&
			wcd_usbss_is_in_reset_state())
			wcd_usbss_reset_routine();

		msleep_interruptible(wcd_usbss_ctxt_->surge_timer_period_ms);
	}

	return 0;
}

/*
 * wcd_usbss_enable_surge_kthread - routine for creating and deploying a kthread to handle surge
 *								   protection.
 */
static void wcd_usbss_enable_surge_kthread(void)
{

	if (!wcd_usbss_ctxt_->surge_enable)
		return;

	if (!wcd_usbss_ctxt_->surge_thread)
		wcd_usbss_ctxt_->surge_thread = kthread_run(wcd_usbss_surge_kthread_fn,
						NULL, "Surge kthread");

	if (!wcd_usbss_ctxt_->surge_thread)
		pr_err("%s, Unable to create WCD USBSS surge kthread.\n", __func__);
}

/*
 * wcd_usbss_disable_surge_kthread - routine for stopping a kthread that handles surge
 *								    protection.
 */
static void wcd_usbss_disable_surge_kthread(void)
{
	if (!wcd_usbss_ctxt_->surge_enable)
		return;

	if (!wcd_usbss_ctxt_->surge_thread)
		return;

	kthread_stop(wcd_usbss_ctxt_->surge_thread);
	wcd_usbss_ctxt_->surge_thread = NULL;
}

static int wcd_usbss_sysfs_init(struct wcd_usbss_ctxt *priv)
{
	int rc = 0;

	priv->surge_kobject = kobject_create_and_add("wcd_usbss", kernel_kobj);

	if (!(priv->surge_kobject)) {
		dev_err(priv->dev, "%s: sysfs failed, surge kobj not created\n", __func__);
		return -ENOMEM;
	}

	rc = sysfs_create_file(priv->surge_kobject, &wcd_usbss_surge_enable_attribute.attr);
	if (rc < 0) {
		dev_err(priv->dev,
			"%s: sysfs failed, unable to register surge enable attribute. rc: %d\n",
			__func__, rc);
		return rc;
	}

	rc = sysfs_create_file(priv->surge_kobject, &wcd_usbss_surge_period_attribute.attr);
	if (rc < 0) {
		dev_err(priv->dev,
			"%s: sysfs failed, unable to register surge period attribute. rc: %d\n",
			__func__, rc);
		return rc;
	}

	rc = sysfs_create_file(priv->surge_kobject, &wcd_usbss_standby_enable_attribute.attr);
	if (rc < 0) {
		dev_err(priv->dev,
			"%s: sysfs failed, unable to register standby enable attribute. rc: %d\n",
			__func__, rc);
		return rc;
	}

	return 0;
}

static int wcd_usbss_usbc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct wcd_usbss_ctxt *priv =
			container_of(nb, struct wcd_usbss_ctxt, ucsi_nb);
	struct device *dev;
	enum typec_accessory acc = ((struct ucsi_glink_constat_info *)ptr)->acc;

	if (!priv)
		return -EINVAL;

	dev = priv->dev;
	if (!dev)
		return -EINVAL;

	dev_dbg(dev, "%s: USB change event received, supply mode %d, usbc mode %ld, expected %d\n",
			__func__, acc, priv->usbc_mode.counter,
			TYPEC_ACCESSORY_AUDIO);

	switch (acc) {
	case TYPEC_ACCESSORY_AUDIO:
	case TYPEC_ACCESSORY_NONE:
		if (atomic_read(&(priv->usbc_mode)) == acc)
			break; /* filter notifications received before */
		atomic_set(&(priv->usbc_mode), acc);

		dev_dbg(dev, "%s: queueing usbc_analog_work\n",
			__func__);
		pm_stay_awake(priv->dev);
		queue_work(system_freezable_wq, &priv->usbc_analog_work);
		break;
	default:
		break;
	}

	return 0;
}

static int wcd_usbss_usbc_analog_setup_switches(struct wcd_usbss_ctxt *priv)
{
	int rc = 0;
	int mode;
	struct device *dev;
	bool cable_status_cache = false;

	if (!priv)
		return -EINVAL;

	dev = priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&priv->notification_lock);
	/* get latest mode again within locked context */
	mode = atomic_read(&(priv->usbc_mode));

	dev_dbg(dev, "%s: setting GPIOs active = %d cable_status = %d mode = %d\n",
		__func__, mode != TYPEC_ACCESSORY_NONE, priv->cable_status, mode);

	switch (mode) {
	/* add all modes WCD USBSS should notify for in here */
	case TYPEC_ACCESSORY_AUDIO:
		/*
		 * If cable_type is already decided, update the cable_status to
		 * avoid reconfiguration of AATC switch settings again
		 */
		if (priv->cable_status & (BIT(WCD_USBSS_AATC) |
					  BIT(WCD_USBSS_GND_MIC_SWAP_AATC) |
					  BIT(WCD_USBSS_HSJ_CONNECT) |
					  BIT(WCD_USBSS_GND_MIC_SWAP_HSJ)))
			cable_status_cache = true;
		/* notify call chain on event */
		blocking_notifier_call_chain(&priv->wcd_usbss_notifier,
					     mode, &cable_status_cache);
		break;
	case TYPEC_ACCESSORY_NONE:
		/* notify call chain on event */
		blocking_notifier_call_chain(&priv->wcd_usbss_notifier,
				TYPEC_ACCESSORY_NONE, NULL);
		break;
	default:
		/* ignore other usb connection modes */
		break;
	}

	mutex_unlock(&priv->notification_lock);
	return rc;
}

static int wcd_usbss_validate_display_port_settings(struct wcd_usbss_ctxt *priv,
						enum wcd_usbss_cable_types ctype)
{
	unsigned int sts;
	int rc;

	rc = regmap_read(priv->regmap, WCD_USBSS_SWITCH_STATUS1, &sts);
	if (rc)
		return rc;

	sts &= 0xCC;
	pr_info("DPAUX switch status (MG1/2): %08x\n", sts);

	if (ctype == WCD_USBSS_DP_AUX_CC1 && sts == 0x48)
		return 0;

	if (ctype == WCD_USBSS_DP_AUX_CC2 && sts == 0x84)
		return 0;

	pr_err("Failed to update switch for display port\n");
	rc = -EINVAL;

	return rc;
}

static int wcd_usbss_switch_update_defaults(struct wcd_usbss_ctxt *priv)
{
	/* Disable all switches */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x7F, 0x00);
	/* Select MG1 for AGND_SWITCHES */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SELECT1, 0x01, 0x00);
	/* Select GSBU1 and MG1 for MIC_SWITCHES */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SELECT0, 0x03, 0x00);
	/* Enable OVP_MG1_BIAS PCOMP_DYN_BST_EN */
	regmap_update_bits(priv->regmap, WCD_USBSS_MG1_BIAS, 0x08, 0x08);
	/* Enable OVP_MG2_BIAS PCOMP_DYN_BST_EN */
	regmap_update_bits(priv->regmap, WCD_USBSS_MG2_BIAS, 0x08, 0x08);
	regmap_update_bits_base(priv->regmap, WCD_USBSS_AUDIO_FSM_START, 0x01,
			0x01, NULL, false, true);
	/* Select DN for DNL_SWITHCES and DP for DPR_SWITCHES */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SELECT0, 0x3C, 0x14);
	/* Enable DNL_SWITCHES and DPR_SWITCHES */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x18, 0x18);
	/* Disable Equalizer */
	regmap_update_bits(priv->regmap, WCD_USBSS_EQUALIZER1,
			WCD_USBSS_EQUALIZER1_EQ_EN_MASK, 0x00);
	regmap_update_bits(priv->regmap, WCD_USBSS_USB_SS_CNTL, 0x07, 0x05); /* Mode5: USB*/
	regmap_write(priv->regmap, WCD_USBSS_PMP_EN, 0x0);
	if (wcd_usbss_ctxt_->version == WCD_USBSS_2_0)
		regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_OUT1,
				0x40, 0x00);
	/* Once plug-out done, restore to MANUAL mode */
	audio_fsm_mode = WCD_USBSS_AUDIO_MANUAL;
	return 0;
}

static void wcd_usbss_update_reg_init(struct regmap *regmap)
{
	if (audio_fsm_mode == WCD_USBSS_AUDIO_FSM)
		regmap_update_bits(regmap, WCD_USBSS_FUNCTION_ENABLE, 0x03,
				0x02); /* AUDIO_FSM mode */
	else
		regmap_update_bits(regmap, WCD_USBSS_FUNCTION_ENABLE, 0x03,
				0x01); /* AUDIO_MANUAL mode */

	/* Enable dynamic boosting for DP and DN */
	regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_DP_DN_MISC1, 0x09, 0x09);
	/* Enable dynamic boosting for MG1 OVP */
	regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_MG1_MISC, 0x20, 0x20);
	/* Enable dynamic boosting for MG2 OVP */
	regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_MG2_MISC, 0x20, 0x20);

	/* Disable Equalizer */
	regmap_update_bits(regmap, WCD_USBSS_EQUALIZER1,
			WCD_USBSS_EQUALIZER1_EQ_EN_MASK, 0x00);
	/* For surge reset routine: Write WCD_USBSS_CPLDO_CTL2 --> 0xFF */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_CPLDO_CTL2, 0xFF, 0xFF);
}

#define AUXP_M_EN_MASK	(WCD_USBSS_SWITCH_SETTINGS_ENABLE_DP_AUXM_TO_MGX_SWITCHES_MASK |\
			WCD_USBSS_SWITCH_SETTINGS_ENABLE_DP_AUXP_TO_MGX_SWITCHES_MASK)

static int wcd_usbss_display_port_switch_update(struct wcd_usbss_ctxt *priv,
					enum wcd_usbss_cable_types ctype)
{
	pr_info("Configuring display port for ctype %d\n", ctype);

	/* Disable AUX switches */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE, AUXP_M_EN_MASK, 0x00);

	/* Select MG1 for AUXP and MG2 for AUXM */
	if (ctype == WCD_USBSS_DP_AUX_CC1)
		regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SELECT0, 0xC0, 0x40);
	/* Select MG2 for AUXP and MG1 for AUXM */
	else
		regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SELECT0, 0xC0, 0x80);

	/* Enable DP_AUXP_TO_MGX and DP_AUXM_TO_MGX switches */
	regmap_update_bits(priv->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE, AUXP_M_EN_MASK, 0x60);
	return wcd_usbss_validate_display_port_settings(priv, ctype);
}

/* to use with DPDM switch selection */
#define DPDM_SEL_MASK       (WCD_USBSS_SWITCH_SELECT0_DPR_SWITCHES_MASK |\
					WCD_USBSS_SWITCH_SELECT0_DNL_SWITCHES_MASK)
#define DPDM_SEL_ENABLE     ((0x1 << WCD_USBSS_SWITCH_SELECT0_DPR_SWITCHES_SHIFT) |\
					(0x1 << WCD_USBSS_SWITCH_SELECT0_DNL_SWITCHES_SHIFT))
#define DPDM_SEL_DISABLE    0x0

/* to use with DPDM switch enable/disable*/
#define DPDM_SW_EN_MASK     (WCD_USBSS_SWITCH_SETTINGS_ENABLE_DPR_SWITCHES_MASK |\
					WCD_USBSS_SWITCH_SETTINGS_ENABLE_DNL_SWITCHES_MASK)
#define DPDM_SW_ENABLE      ((0x1 << WCD_USBSS_SWITCH_SETTINGS_ENABLE_DNL_SWITCHES_SHIFT) |\
				(0x1 << WCD_USBSS_SWITCH_SETTINGS_ENABLE_DPR_SWITCHES_SHIFT))
#define DPDM_SW_DISABLE     0x0

/*
 * wcd_usbss_dpdm_switch_update - configure WCD USBSS DP/DM switch position
 *
 * @sw_en: enable or disable DP/DM switches.
 * @eq_en: enable or disable equalizer. Usually true in case of USB high-speed.
 *
 * Returns zero for success, a negative number on error.
 */
int wcd_usbss_dpdm_switch_update(bool sw_en, bool eq_en)
{
	int ret = 0;

	/* check if driver is probed and private context is initialized */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	ret = regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SWITCH_SELECT0,
				DPDM_SEL_MASK, (sw_en ? DPDM_SEL_ENABLE : DPDM_SEL_DISABLE));

	if (ret)
		pr_err("%s(): Failed to write dpdm_sel_value ret:%d\n", __func__, ret);

	ret = regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_SWITCH_SETTINGS_ENABLE,
				DPDM_SW_EN_MASK, (sw_en ? DPDM_SW_ENABLE : DPDM_SW_DISABLE));
	if (ret)
		pr_err("%s(): Failed to write dpdm_en_value ret:%d\n", __func__, ret);

	ret = regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_EQUALIZER1,
				WCD_USBSS_EQUALIZER1_EQ_EN_MASK,
				(eq_en ? WCD_USBSS_EQUALIZER1_EQ_EN_MASK : 0x0));
	if (ret)
		pr_err("%s(): Failed to write equalizer1_en ret:%d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL(wcd_usbss_dpdm_switch_update);

/* wcd_usbss_audio_config - configure audio for power mode and Impedance calculations
 *
 * @enable: enable/disable switch settings for MIC and SENSE for impedance readings
 * @config_type: Config type to configure audio
 * @power_mode: power mode type to config
 *
 * Returns int on whether the config happened or not. -ENODEV is returned
 * in case if the driver is not probed.
 */

int wcd_usbss_audio_config(bool enable, enum wcd_usbss_config_type config_type,
			unsigned int power_mode)
{

	int rc = 0;
	struct device *i2c_bus_dev = wcd_usbss_ctxt_->client->adapter->dev.parent;
	bool disable_rpm = false;

	/* check if driver is probed and private context is init'ed */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	pr_info("%s: connect_status = 0x%x, power mode = %d\n",
		__func__, wcd_usbss_ctxt_->cable_status, power_mode);

	if (!(wcd_usbss_ctxt_->cable_status & (BIT(WCD_USBSS_AATC) |
					       BIT(WCD_USBSS_GND_MIC_SWAP_AATC) |
					       BIT(WCD_USBSS_HSJ_CONNECT) |
					       BIT(WCD_USBSS_GND_MIC_SWAP_HSJ))))
		return 0;

	if (!pm_runtime_enabled(i2c_bus_dev)) {
		pm_runtime_enable(i2c_bus_dev);
		disable_rpm = true;
	}

	switch (config_type) {
	case WCD_USBSS_CONFIG_TYPE_POWER_MODE:
		regmap_update_bits(wcd_usbss_ctxt_->regmap,
			WCD_USBSS_USB_SS_CNTL, 0x07, power_mode);
		if (power_mode == 0x1) /* MBHC Mode */
			regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_EN, 0xF);
		else
			regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_EN, 0x0);
		break;
	default:
		pr_err("%s Invalid config type %d\n", __func__, config_type);
		rc = -EINVAL;
	}

	if (disable_rpm)
		pm_runtime_disable(i2c_bus_dev);

	return rc;
}
EXPORT_SYMBOL(wcd_usbss_audio_config);

/*
 * wcd_usbss_switch_update - configure WCD USBSS switch position based on
 *  cable type and status
 *
 * @ctype - cable type
 * @connect_status - cable connected/disconnected status
 *
 * Returns int on whether the switch happened or not. -ENODEV is returned
 *  in case if the driver is not probed
 */
int wcd_usbss_switch_update(enum wcd_usbss_cable_types ctype,
							enum wcd_usbss_cable_status connect_status)
{
	int i = 0, ret = 0;

	/* check if driver is probed and private context is init'ed */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	mutex_lock(&wcd_usbss_ctxt_->switch_update_lock);

	pr_info("%s: ctype = %d, connect_status = %d\n",
		__func__, ctype, connect_status);

	if (connect_status == WCD_USBSS_CABLE_DISCONNECT) {
		wcd_usbss_ctxt_->cable_status &= ~BIT(ctype);

		switch (ctype) {
		case WCD_USBSS_USB:
			/* Keep DP/DM switch on but disable EQ */
			wcd_usbss_dpdm_switch_update(true, false);
			break;
		case WCD_USBSS_DP_AUX_CC1:
			fallthrough;
		case WCD_USBSS_DP_AUX_CC2:
			/* Disable AUX switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0xC0, 0x00);
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE,
					AUXP_M_EN_MASK, 0x00);
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_DP_AUX_CC1);
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_DP_AUX_CC2);
			break;
		case WCD_USBSS_AATC:
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_GND_MIC_SWAP_AATC);
			break;
		case WCD_USBSS_GND_MIC_SWAP_AATC:
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_AATC);
			break;
		case WCD_USBSS_HSJ_CONNECT:
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_GND_MIC_SWAP_HSJ);
			break;
		case WCD_USBSS_GND_MIC_SWAP_HSJ:
			wcd_usbss_ctxt_->cable_status &= ~BIT(WCD_USBSS_HSJ_CONNECT);
			break;
		default:
			break;
		}

		/* reset to defaults when all cable types are disconnected */
		if (!wcd_usbss_ctxt_->cable_status) {
			wcd_usbss_switch_update_defaults(wcd_usbss_ctxt_);
			if (config_standby) {
				wcd_usbss_dpdm_switch_update(false, false);
				wcd_usbss_standby_control(true);
			}
		}
	} else if (connect_status == WCD_USBSS_CABLE_CONNECT) {
		wcd_usbss_ctxt_->cable_status |= BIT(ctype);

		wcd_usbss_standby_control(false);

		switch (ctype) {
		case WCD_USBSS_USB:
			wcd_usbss_dpdm_switch_update(true, true);
			break;
		case WCD_USBSS_AATC:
			/* Update power mode to mode 1 for AATC */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
				WCD_USBSS_USB_SS_CNTL, 0x07, 0x01);
			regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_EN, 0xF);
			if (wcd_usbss_ctxt_->version == WCD_USBSS_2_0)
				regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_PMP_OUT1, 0x40, 0x40);
			/* for AATC plug-in, change mode to FSM */
			audio_fsm_mode = WCD_USBSS_AUDIO_FSM;
			/* Disable all switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
				WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x7F, 0x00);
			if (audio_fsm_mode == WCD_USBSS_AUDIO_FSM) {
				regmap_update_bits_base(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_AUDIO_FSM_START, 0x01, 0x01, NULL, false, true);
			}
			/* Select L, R, GSBU2, MG1 */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0x3F, 0x02);
			/* Disable OVP_MG2_BIAS PCOMP_DYN_BST_EN */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_MG2_BIAS, 0x08, 0x00);
			/* Enable SENSE, MIC switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x06, 0x06);
			/* Select MG2 for AGND_SWITCHES */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT1, 0x01, 0x01);
			/* Enable AGND switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x01, 0x01);
			/* Enable DPR, DNL */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x18, 0x18);
			/* Set DELAY_L_SW to CYL_1K */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DELAY_L_SW, 0xFF, 0x02);
			/* Set DELAY_R_SW to CYL_1K */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DELAY_R_SW, 0xFF, 0x02);
			/* Set DELAY_MIC_SW to CYL_1K */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DELAY_MIC_SW, 0xFF, 0x01);
			if (audio_fsm_mode == WCD_USBSS_AUDIO_FSM) {
				regmap_update_bits_base(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_AUDIO_FSM_START, 0x01, 0x01, NULL, false, true);
			}
			for (i = 0; i < ARRAY_SIZE(coeff_init); ++i)
				regmap_update_bits(wcd_usbss_ctxt_->regmap, coeff_init[i].reg,
						coeff_init[i].mask, coeff_init[i].val);
			break;
		case WCD_USBSS_GND_MIC_SWAP_AATC:
			dev_info(wcd_usbss_ctxt_->dev,
					"%s: GND MIC Swap register updates..\n", __func__);
			/* Update power mode to mode 1 for AATC */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
				WCD_USBSS_USB_SS_CNTL, 0x07, 0x01);
			regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_EN, 0xF);
			if (wcd_usbss_ctxt_->version == WCD_USBSS_2_0)
				regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_PMP_OUT1, 0x40, 0x40);
			/* for GND MIC Swap, change mode to FSM */
			audio_fsm_mode = WCD_USBSS_AUDIO_FSM;
			/* Disable all switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x7F, 0x00);
			/* Select L, R, GSBU1, MG2 */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0x3F, 0x01);
			/* Disable OVP_MG1_BIAS PCOMP_DYN_BST_EN */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_MG1_BIAS, 0x08, 0x00);
			/* Enable SENSE, MIC switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x06, 0x06);
			/* Select MG1 for AGND_SWITCHES */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT1, 0x01, 0x00);
			/* Enable AGND switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x01, 0x01);
			/* Enable DPR, DNL */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x18, 0x18);
			regmap_update_bits_base(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_AUDIO_FSM_START, 0x01, 0x01, NULL, false, true);
			break;
		case WCD_USBSS_HSJ_CONNECT:
			/* Update power mode to mode 1 for AATC */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
				WCD_USBSS_USB_SS_CNTL, 0x07, 0x01);
			regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_EN, 0xF);
			if (wcd_usbss_ctxt_->version == WCD_USBSS_2_0)
				regmap_update_bits(wcd_usbss_ctxt_->regmap,
						WCD_USBSS_PMP_OUT1, 0x40, 0x40);
			/* Select MG2, GSBU1 */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0x03, 0x1);
			/* Disable OVP_MG1_BIAS PCOMP_DYN_BST_EN */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_MG1_BIAS, 0x08, 0x00);
			/* Enable SENSE, MIC, AGND switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x07, 0x07);
			break;
		case WCD_USBSS_GND_MIC_SWAP_HSJ:
			/* Disable SENSE, MIC, AGND switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x07, 0x00);
			/* Select MG1, GSBU2 */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0x03, 0x2);
			/* Enable SENSE, MIC, AGND switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x07, 0x07);
			break;
		case WCD_USBSS_CHARGER:
			/* Disable DN DP Switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x18, 0x00);
			/* Select DN2 DP2 */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SELECT0, 0x3C, 0x28);
			/* Enable DN DP Switches */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_SWITCH_SETTINGS_ENABLE, 0x18, 0x18);
			break;
		case WCD_USBSS_DP_AUX_CC1:
			fallthrough;
		case WCD_USBSS_DP_AUX_CC2:
			/* Update Leakage Canceller Coefficient for AUXP pins */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DISP_AUXP_CTL, 0x07, 0x01);
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DISP_AUXP_THRESH, 0xE0, 0xE0);
			ret = wcd_usbss_display_port_switch_update(wcd_usbss_ctxt_, ctype);
			if (ret) /* clear DP AUX bit if DP switch update fails */
				wcd_usbss_ctxt_->cable_status &= ~BIT(ctype);
			break;
		default:
			break;
		}
	}

	mutex_unlock(&wcd_usbss_ctxt_->switch_update_lock);
	return ret;
}
EXPORT_SYMBOL(wcd_usbss_switch_update);

/*
 * wcd_usbss_reg_notifier - register notifier block with wcd usbss driver
 *
 * @nb - notifier block of wcd_usbss
 * @node - phandle node to wcd_usbss device
 *
 * Returns 0 on success, or error code
 */
int wcd_usbss_reg_notifier(struct notifier_block *nb,
			 struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct wcd_usbss_ctxt *priv;

	if (!client)
		return -EINVAL;

	priv = (struct wcd_usbss_ctxt *)i2c_get_clientdata(client);
	if (!priv)
		return -EINVAL;

	rc = blocking_notifier_chain_register
				(&priv->wcd_usbss_notifier, nb);

	dev_dbg(priv->dev, "%s: registered notifier for %s\n",
		__func__, node->name);
	if (rc)
		return rc;

	/*
	 * as part of the init sequence check if there is a connected
	 * USB C analog adapter
	 */
	if (atomic_read(&(priv->usbc_mode)) == TYPEC_ACCESSORY_AUDIO) {
		dev_dbg(priv->dev, "%s: analog adapter already inserted\n",
			__func__);
		rc = wcd_usbss_usbc_analog_setup_switches(priv);
	}

	return rc;
}
EXPORT_SYMBOL(wcd_usbss_reg_notifier);

/*
 * wcd_usbss_unreg_notifier - unregister notifier block with wcd usbss driver
 *
 * @nb - notifier block of wcd_usbss
 * @node - phandle node to wcd_usbss device
 *
 * Returns 0 on pass, or error code
 */
int wcd_usbss_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct wcd_usbss_ctxt *priv;

	if (!client)
		return -EINVAL;

	priv = (struct wcd_usbss_ctxt *)i2c_get_clientdata(client);
	if (!priv)
		return -EINVAL;

	return blocking_notifier_chain_unregister
					(&priv->wcd_usbss_notifier, nb);
}
EXPORT_SYMBOL(wcd_usbss_unreg_notifier);


/*
 * wcd_usbss_update_default_trim - update default trim for TP < 3
 *
 * Returns 0 on pass, or error code
 */
int wcd_usbss_update_default_trim(void)
{
	if (!wcd_usbss_ctxt_)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_SW_LIN_CTRL_1, 0x01);
	regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_DC_TRIMCODE_1, 0x00);
	regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_DC_TRIMCODE_2, 0x00);
	regmap_write(wcd_usbss_ctxt_->regmap, WCD_USBSS_DC_TRIMCODE_3, 0x00);

	return 0;
}
EXPORT_SYMBOL(wcd_usbss_update_default_trim);

static void wcd_usbss_usbc_analog_work_fn(struct work_struct *work)
{
	struct wcd_usbss_ctxt *priv =
		container_of(work, struct wcd_usbss_ctxt, usbc_analog_work);

	if (!priv) {
		pr_err("%s: wcd usbss container invalid\n", __func__);
		return;
	}
	wcd_usbss_usbc_analog_setup_switches(priv);
	pm_relax(priv->dev);
}

static int wcd_usbss_init_optional_reset_pins(struct wcd_usbss_ctxt *priv)
{
	priv->rst_pins = devm_pinctrl_get(priv->dev);
	if (IS_ERR_OR_NULL(priv->rst_pins)) {
		dev_dbg(priv->dev, "Cannot get wcd usbss reset pinctrl:%ld\n",
				PTR_ERR(priv->rst_pins));
		return PTR_ERR(priv->rst_pins);
	}

	priv->rst_pins_active = pinctrl_lookup_state(
			priv->rst_pins, "active");
	if (IS_ERR_OR_NULL(priv->rst_pins_active)) {
		dev_dbg(priv->dev, "Cannot get active pinctrl state:%ld\n",
				PTR_ERR(priv->rst_pins_active));
		return PTR_ERR(priv->rst_pins_active);
	}

	if (priv->rst_pins_active)
		return pinctrl_select_state(priv->rst_pins,
				priv->rst_pins_active);

	return 0;
}

static int wcd_usbss_probe(struct i2c_client *i2c)
{
	struct wcd_usbss_ctxt *priv;
	int rc = 0, i;
	unsigned int ver = 0;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &i2c->dev;
	priv->client = i2c;
	mutex_init(&priv->io_lock);
	mutex_init(&priv->switch_update_lock);
	i2c_set_clientdata(i2c, priv);

	if (ARRAY_SIZE(supply_names) >= WCD_USBSS_SUPPLY_MAX) {
		dev_err(priv->dev, "Unsupported number of supplies: %d\n",
				ARRAY_SIZE(supply_names));
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(supply_names); ++i)
		priv->supplies[i].supply = supply_names[i];

	rc = devm_regulator_bulk_get(priv->dev, ARRAY_SIZE(supply_names),
			priv->supplies);
	if (rc < 0) {
		dev_err(priv->dev, "Failed to get supplies: %d\n", rc);
		return rc;
	}

	rc = regulator_bulk_enable(ARRAY_SIZE(supply_names), priv->supplies);
	if (rc) {
		dev_err(priv->dev, "Failed to enable supplies: %d\n", rc);
		return rc;
	}

	rc = wcd_usbss_init_optional_reset_pins(priv);
	if (rc) {
		dev_dbg(priv->dev, "%s: Optional reset pin reset failed\n",
				__func__);
		rc = 0;
	}
	wcd_usbss_regmap_config.readable_reg = wcd_usbss_readable_register;
	priv->regmap = wcd_usbss_regmap_init(priv->dev, &wcd_usbss_regmap_config);
	if (IS_ERR_OR_NULL(priv->regmap)) {
		rc = PTR_ERR(priv->regmap);
		if (!priv->regmap)
			rc = -EINVAL;

		dev_err(priv->dev, "Failed to initialize regmap: %d\n", rc);
		goto err_data;
	}

	/* OVP-Fuse settings recommended from HW */
	regmap_update_bits(priv->regmap, WCD_USBSS_FSM_OVERRIDE, 0x77, 0x77);
	regmap_update_bits(priv->regmap, WCD_USBSS_DP_EN, 0x0E, 0x08);
	regmap_update_bits(priv->regmap, WCD_USBSS_DN_EN, 0x0E, 0x08);

	regmap_read(priv->regmap, WCD_USBSS_CHIP_ID1, &ver);
	if (ver == 0x1) { /* Harmonium 2.0 */
		regmap_update_bits(priv->regmap, WCD_USBSS_MG1_EN, 0x2, 0x0);
		regmap_update_bits(priv->regmap, WCD_USBSS_MG2_EN, 0x2, 0x0);
	}
	priv->version = ver;

	devm_regmap_qti_debugfs_register(priv->dev, priv->regmap);

	wcd_usbss_ctxt_ = priv;

	mutex_init(&priv->standby_lock);
	i2c_set_clientdata(i2c, priv);
	if (config_standby) {
		priv->standby_enable = true;
		wcd_usbss_dpdm_switch_update(false, false);
		wcd_usbss_standby_control(true);
	}

	priv->ucsi_nb.notifier_call = wcd_usbss_usbc_event_changed;
	priv->ucsi_nb.priority = 0;
	rc = register_ucsi_glink_notifier(&priv->ucsi_nb);
	if (rc) {
		dev_err(priv->dev, "%s: ucsi glink notifier registration failed: %d\n",
			__func__, rc);
		goto err_data;
	}

	mutex_init(&priv->notification_lock);
	i2c_set_clientdata(i2c, priv);

	wcd_usbss_update_reg_init(priv->regmap);
	INIT_WORK(&priv->usbc_analog_work,
		  wcd_usbss_usbc_analog_work_fn);
	BLOCKING_INIT_NOTIFIER_HEAD(&priv->wcd_usbss_notifier);

	rc = wcd_usbss_sysfs_init(priv);
	if (rc == 0) {
		priv->surge_timer_period_ms = DEFAULT_SURGE_TIMER_PERIOD_MS;
		priv->surge_enable = true;
		wcd_usbss_enable_surge_kthread();
	}

	dev_info(priv->dev, "Probe completed!\n");
	return 0;

err_data:
	return rc;
}

static void wcd_usbss_remove(struct i2c_client *i2c)
{
	struct wcd_usbss_ctxt *priv =
			(struct wcd_usbss_ctxt *)i2c_get_clientdata(i2c);

	if (!priv)
		return;

	wcd_usbss_disable_surge_kthread();
	unregister_ucsi_glink_notifier(&priv->ucsi_nb);
	cancel_work_sync(&priv->usbc_analog_work);
	pm_relax(priv->dev);
	mutex_destroy(&priv->notification_lock);
	mutex_destroy(&priv->io_lock);
	mutex_destroy(&priv->standby_lock);
	mutex_destroy(&priv->switch_update_lock);
	dev_set_drvdata(&i2c->dev, NULL);
	wcd_usbss_ctxt_ = NULL;
}

static const struct of_device_id wcd_usbss_i2c_dt_match[] = {
	{
		.compatible = "qcom,wcd939x-i2c",
	},
	{}
};
MODULE_DEVICE_TABLE(of, wcd_usbss_i2c_dt_match);

static const struct i2c_device_id wcd_usbss_id_i2c[] = {
	{ "wcd939x", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, wcd_usbss_id_i2c);

static struct i2c_driver wcd_usbss_i2c_driver = {
	.driver = {
		.name = WCD_USBSS_I2C_NAME,
		.of_match_table = wcd_usbss_i2c_dt_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = wcd_usbss_id_i2c,
	.probe_new = wcd_usbss_probe,
	.remove = wcd_usbss_remove,
};
module_i2c_driver(wcd_usbss_i2c_driver);

MODULE_DESCRIPTION("WCD USBSS I2C driver");
MODULE_LICENSE("GPL");
