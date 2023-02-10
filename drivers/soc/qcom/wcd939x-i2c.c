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

struct wcd_usbss_reg_mask_val {
	u16 reg;
	u8 mask;
	u8 val;
};

/* regulator power supply names */
static const char * const supply_names[] = {
	"vdd-usb-cp",
};

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

static struct kobj_attribute wcd_usbss_surge_enable_attribute =
	__ATTR(surge_enable, 0220, NULL, wcd_usbss_surge_enable_store);

static struct kobj_attribute wcd_usbss_surge_period_attribute =
	__ATTR(surge_period, 0220, NULL, wcd_usbss_surge_period_store);

/*
 * wcd_usbss_is_in_reset_state - routine for using captured state to reset WCD USB-SS after surge
 *
 * Checks:
 * 1. Register WCD_USBSS_CPLDO_CTL2 reads 0xFF
 * 2. Register WCD_USBSS_RCO_MISC2 Bit<1> reads 0 at least once in NUM_RCO_MISC2_READ reads
 * 3. Register 0x06 Bit<0> reads 1 after toggling
 *    register WCD_USBSS_PMP_MISC1 Bit<0> from 0 --> 1 --> 0
 *
 * Returns true if all checks fails (indicates OVP and reset needed), false otherwise
 */
static bool wcd_usbss_is_in_reset_state(void)
{
	int i = 0;
	unsigned int read_val = 0;

	/* Check 1: Read WCD_USBSS_CPLDO_CTL2 */
	regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_CPLDO_CTL2, &read_val);
	if (read_val == 0xFF)
		return false;

	/* Check 2: Read WCD_USBSS_RCO_MISC2 */
	for (i = 0; i < NUM_RCO_MISC2_READ; i++) {
		regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_RCO_MISC2, &read_val);
		if ((read_val & 0x2) == 0)
			return false;
	}

	/* Toggle WCD_USBSS_PMP_MISC1 bit<0>: 0 --> 1 --> 0 */
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x0);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x1);
	regmap_update_bits(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC1, 0x1, 0x0);

	/* Check 3: Read WCD_USBSS_PMP_MISC2 */
	regmap_read(wcd_usbss_ctxt_->regmap, WCD_USBSS_PMP_MISC2, &read_val);
	if (read_val & 0x1)
		return false;

	/* All checks failed, so a reset has occurred, and return true */
	return true;
}

/*
 * wcd_usbss_reset_routine - routine for using captured state to reset WCD after surge
 *
 * Returns return value from wcd_usbss_switch_update
 */
static int wcd_usbss_reset_routine(void)
{
	/* Mark the cache as dirty to force a flush */
	regcache_mark_dirty(wcd_usbss_ctxt_->regmap);
	regcache_sync(wcd_usbss_ctxt_->regmap);

	return wcd_usbss_switch_update(wcd_usbss_ctxt_->cable_type, wcd_usbss_ctxt_->cable_status);
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
	if (wcd_usbss_ctxt_->cable_status == WCD_USBSS_CABLE_CONNECT &&
		wcd_usbss_ctxt_->surge_enable)
		wake_up_process(wcd_usbss_ctxt_->surge_thread);

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
		if (wcd_usbss_ctxt_->cable_status == WCD_USBSS_CABLE_CONNECT &&
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

	if (!priv)
		return -EINVAL;

	dev = priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&priv->notification_lock);
	/* get latest mode again within locked context */
	mode = atomic_read(&(priv->usbc_mode));

	dev_dbg(dev, "%s: setting GPIOs active = %d\n",
		__func__, mode != TYPEC_ACCESSORY_NONE);

	switch (mode) {
	/* add all modes WCD USBSS should notify for in here */
	case TYPEC_ACCESSORY_AUDIO:
		/* notify call chain on event */
		blocking_notifier_call_chain(&priv->wcd_usbss_notifier,
					     mode, NULL);
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

	pr_info("Switch Status (MG1/2): %08x\n", sts);

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
	/* Once plug-out done, restore to MANUAL mode */
	audio_fsm_mode = WCD_USBSS_AUDIO_MANUAL;
	return 0;
}

static void wcd_usbss_update_reg_init(struct regmap *regmap)
{
	/* update Register Power On Reset values (if any) */
	regmap_update_bits(regmap, WCD_USBSS_USB_SS_CNTL, 0x07, 0x02); /*Mode2*/
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

	/* check if driver is probed and private context is init'ed */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	switch (config_type) {
	case WCD_USBSS_CONFIG_TYPE_POWER_MODE:
		/* Configure power mode from RX HPH mixer ctl */
		regmap_update_bits(wcd_usbss_ctxt_->regmap,
				WCD_USBSS_USB_SS_CNTL, 0x07, power_mode);
		break;
	default:
		pr_err("%s Invalid config type %d\n", __func__, config_type);
		return -EINVAL;
	}
	return 0;
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
	int i = 0;

	/* check if driver is probed and private context is init'ed */
	if (wcd_usbss_ctxt_ == NULL)
		return -ENODEV;

	if (!wcd_usbss_ctxt_->regmap)
		return -EINVAL;

	wcd_usbss_ctxt_->cable_type = ctype;
	wcd_usbss_ctxt_->cable_status = connect_status;

	if (connect_status == WCD_USBSS_CABLE_DISCONNECT) {
		wcd_usbss_switch_update_defaults(wcd_usbss_ctxt_);
	} else if (connect_status == WCD_USBSS_CABLE_CONNECT) {
		switch (ctype) {
		case WCD_USBSS_USB:
			wcd_usbss_dpdm_switch_update(true, true);
			break;
		case WCD_USBSS_AATC:
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
		case WCD_USBSS_DP_AUX_CC1:
			fallthrough;
		case WCD_USBSS_DP_AUX_CC2:
			/* Update Leakage Canceller Coefficient for AUXP pins */
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DISP_AUXP_CTL, 0x07, 0x05);
			regmap_update_bits(wcd_usbss_ctxt_->regmap,
					WCD_USBSS_DISP_AUXP_THRESH, 0xE0, 0xE0);
			return wcd_usbss_display_port_switch_update(wcd_usbss_ctxt_, ctype);
		}
	}

	return 0;
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

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &i2c->dev;
	priv->client = i2c;
	mutex_init(&priv->io_lock);
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
	priv->regmap = wcd_usbss_regmap_init(priv->dev, &wcd_usbss_regmap_config);
	if (IS_ERR_OR_NULL(priv->regmap)) {
		rc = PTR_ERR(priv->regmap);
		if (!priv->regmap)
			rc = -EINVAL;

		dev_err(priv->dev, "Failed to initialize regmap: %d\n", rc);
		goto err_data;
	}

	devm_regmap_qti_debugfs_register(priv->dev, priv->regmap);

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
	wcd_usbss_ctxt_ = priv;

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
