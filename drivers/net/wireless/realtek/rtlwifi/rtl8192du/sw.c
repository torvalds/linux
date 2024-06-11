// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../core.h"
#include "../usb.h"
#include "../base.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/fw_common.h"
#include "../rtl8192d/hw_common.h"
#include "../rtl8192d/phy_common.h"
#include "../rtl8192d/trx_common.h"
#include "phy.h"
#include "dm.h"
#include "hw.h"
#include "trx.h"
#include "led.h"

#include <linux/module.h>

static struct usb_interface *rtl92du_get_other_intf(struct ieee80211_hw *hw)
{
	struct usb_interface *intf;
	struct usb_device *udev;
	u8 other_interfaceindex;

	/* See SET_IEEE80211_DEV(hw, &intf->dev); in usb.c */
	intf = container_of_const(wiphy_dev(hw->wiphy), struct usb_interface, dev);

	if (intf->altsetting[0].desc.bInterfaceNumber == 0)
		other_interfaceindex = 1;
	else
		other_interfaceindex = 0;

	udev = interface_to_usbdev(intf);

	return usb_ifnum_to_if(udev, other_interfaceindex);
}

static int rtl92du_init_shared_data(struct ieee80211_hw *hw)
{
	struct usb_interface *other_intf = rtl92du_get_other_intf(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_priv *other_rtlpriv = NULL;
	struct ieee80211_hw *other_hw = NULL;

	if (other_intf)
		other_hw = usb_get_intfdata(other_intf);

	if (other_hw) {
		/* The other interface was already probed. */
		other_rtlpriv = rtl_priv(other_hw);
		rtlpriv->curveindex_2g = other_rtlpriv->curveindex_2g;
		rtlpriv->curveindex_5g = other_rtlpriv->curveindex_5g;
		rtlpriv->mutex_for_power_on_off = other_rtlpriv->mutex_for_power_on_off;
		rtlpriv->mutex_for_hw_init = other_rtlpriv->mutex_for_hw_init;

		if (!rtlpriv->curveindex_2g || !rtlpriv->curveindex_5g ||
		    !rtlpriv->mutex_for_power_on_off || !rtlpriv->mutex_for_hw_init)
			return -ENOMEM;

		return 0;
	}

	/* The other interface doesn't exist or was not probed yet. */
	rtlpriv->curveindex_2g = kcalloc(TARGET_CHNL_NUM_2G,
					 sizeof(*rtlpriv->curveindex_2g),
					 GFP_KERNEL);
	rtlpriv->curveindex_5g = kcalloc(TARGET_CHNL_NUM_5G,
					 sizeof(*rtlpriv->curveindex_5g),
					 GFP_KERNEL);
	rtlpriv->mutex_for_power_on_off =
		kzalloc(sizeof(*rtlpriv->mutex_for_power_on_off), GFP_KERNEL);
	rtlpriv->mutex_for_hw_init =
		kzalloc(sizeof(*rtlpriv->mutex_for_hw_init), GFP_KERNEL);

	if (!rtlpriv->curveindex_2g || !rtlpriv->curveindex_5g ||
	    !rtlpriv->mutex_for_power_on_off || !rtlpriv->mutex_for_hw_init) {
		kfree(rtlpriv->curveindex_2g);
		kfree(rtlpriv->curveindex_5g);
		kfree(rtlpriv->mutex_for_power_on_off);
		kfree(rtlpriv->mutex_for_hw_init);
		rtlpriv->curveindex_2g = NULL;
		rtlpriv->curveindex_5g = NULL;
		rtlpriv->mutex_for_power_on_off = NULL;
		rtlpriv->mutex_for_hw_init = NULL;
		return -ENOMEM;
	}

	mutex_init(rtlpriv->mutex_for_power_on_off);
	mutex_init(rtlpriv->mutex_for_hw_init);

	return 0;
}

static void rtl92du_deinit_shared_data(struct ieee80211_hw *hw)
{
	struct usb_interface *other_intf = rtl92du_get_other_intf(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (!other_intf || !usb_get_intfdata(other_intf)) {
		/* The other interface doesn't exist or was already disconnected. */
		kfree(rtlpriv->curveindex_2g);
		kfree(rtlpriv->curveindex_5g);
		if (rtlpriv->mutex_for_power_on_off)
			mutex_destroy(rtlpriv->mutex_for_power_on_off);
		if (rtlpriv->mutex_for_hw_init)
			mutex_destroy(rtlpriv->mutex_for_hw_init);
		kfree(rtlpriv->mutex_for_power_on_off);
		kfree(rtlpriv->mutex_for_hw_init);
	}
}

static int rtl92du_init_sw_vars(struct ieee80211_hw *hw)
{
	const char *fw_name = "rtlwifi/rtl8192dufw.bin";
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err;

	err = rtl92du_init_shared_data(hw);
	if (err)
		return err;

	rtlpriv->dm.dm_initialgain_enable = true;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = false;
	rtlpriv->dm.thermalvalue = 0;
	rtlpriv->dm.useramask = true;

	/* dual mac */
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G)
		rtlpriv->phy.current_channel = 36;
	else
		rtlpriv->phy.current_channel = 1;

	if (rtlpriv->rtlhal.macphymode != SINGLEMAC_SINGLEPHY)
		rtlpriv->rtlhal.disable_amsdu_8k = true;

	/* for LPS & IPS */
	rtlpriv->psc.inactiveps = rtlpriv->cfg->mod_params->inactiveps;
	rtlpriv->psc.swctrl_lps = rtlpriv->cfg->mod_params->swctrl_lps;
	rtlpriv->psc.fwctrl_lps = rtlpriv->cfg->mod_params->fwctrl_lps;

	/* for early mode */
	rtlpriv->rtlhal.earlymode_enable = false;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = kmalloc(0x8000, GFP_KERNEL);
	if (!rtlpriv->rtlhal.pfirmware)
		return -ENOMEM;

	rtlpriv->max_fw_size = 0x8000;
	pr_info("Driver for Realtek RTL8192DU WLAN interface\n");
	pr_info("Loading firmware file %s\n", fw_name);

	/* request fw */
	err = request_firmware_nowait(THIS_MODULE, 1, fw_name,
				      rtlpriv->io.dev, GFP_KERNEL, hw,
				      rtl_fw_cb);
	if (err) {
		pr_err("Failed to request firmware!\n");
		kfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
		return err;
	}

	return 0;
}

static void rtl92du_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	kfree(rtlpriv->rtlhal.pfirmware);
	rtlpriv->rtlhal.pfirmware = NULL;

	rtl92du_deinit_shared_data(hw);
}

static const struct rtl_hal_ops rtl8192du_hal_ops = {
	.init_sw_vars = rtl92du_init_sw_vars,
	.deinit_sw_vars = rtl92du_deinit_sw_vars,
	.read_chip_version = rtl92du_read_chip_version,
	.read_eeprom_info = rtl92d_read_eeprom_info,
	.hw_init = rtl92du_hw_init,
	.hw_disable = rtl92du_card_disable,
	.enable_interrupt = rtl92du_enable_interrupt,
	.disable_interrupt = rtl92du_disable_interrupt,
	.set_network_type = rtl92du_set_network_type,
	.set_chk_bssid = rtl92du_set_check_bssid,
	.set_qos = rtl92d_set_qos,
	.set_bcn_reg = rtl92du_set_beacon_related_registers,
	.set_bcn_intv = rtl92du_set_beacon_interval,
	.update_interrupt_mask = rtl92du_update_interrupt_mask,
	.get_hw_reg = rtl92du_get_hw_reg,
	.set_hw_reg = rtl92du_set_hw_reg,
	.update_rate_tbl = rtl92d_update_hal_rate_tbl,
	.fill_tx_desc = rtl92du_tx_fill_desc,
	.query_rx_desc = rtl92d_rx_query_desc,
	.set_channel_access = rtl92d_update_channel_access_setting,
	.radio_onoff_checking = rtl92d_gpio_radio_on_off_checking,
	.set_bw_mode = rtl92du_phy_set_bw_mode,
	.switch_channel = rtl92du_phy_sw_chnl,
	.dm_watchdog = rtl92du_dm_watchdog,
	.scan_operation_backup = rtl_phy_scan_operation_backup,
	.set_rf_power_state = rtl92du_phy_set_rf_power_state,
	.led_control = rtl92du_led_control,
	.set_desc = rtl92d_set_desc,
	.get_desc = rtl92d_get_desc,
	.enable_hw_sec = rtl92d_enable_hw_security_config,
	.set_key = rtl92d_set_key,
	.get_bbreg = rtl92du_phy_query_bb_reg,
	.set_bbreg = rtl92du_phy_set_bb_reg,
	.get_rfreg = rtl92d_phy_query_rf_reg,
	.set_rfreg = rtl92d_phy_set_rf_reg,
	.linked_set_reg = rtl92du_linked_set_reg,
	.fill_h2c_cmd = rtl92d_fill_h2c_cmd,
	.get_btc_status = rtl_btc_status_false,
	.phy_iq_calibrate = rtl92du_phy_iq_calibrate,
	.phy_lc_calibrate = rtl92du_phy_lc_calibrate,
};

static struct rtl_mod_params rtl92du_mod_params = {
	.sw_crypto = false,
	.inactiveps = false,
	.swctrl_lps = false,
	.debug_level = 0,
	.debug_mask = 0,
};

static const struct rtl_hal_usbint_cfg rtl92du_interface_cfg = {
	/* rx */
	.rx_urb_num = 8,
	.rx_max_size = 15360,
	.usb_rx_hdl = NULL,
	.usb_rx_segregate_hdl = NULL,
	/* tx */
	.usb_tx_cleanup = rtl92du_tx_cleanup,
	.usb_tx_post_hdl = rtl92du_tx_post_hdl,
	.usb_tx_aggregate_hdl = rtl92du_tx_aggregate_hdl,
	.usb_endpoint_mapping = rtl92du_endpoint_mapping,
	.usb_mq_to_hwq = rtl92du_mq_to_hwq,
};

static const struct rtl_hal_cfg rtl92du_hal_cfg = {
	.name = "rtl8192du",
	.ops = &rtl8192du_hal_ops,
	.mod_params = &rtl92du_mod_params,
	.usb_interface_cfg = &rtl92du_interface_cfg,

	.maps[SYS_ISO_CTRL] = REG_SYS_ISO_CTRL,
	.maps[SYS_FUNC_EN] = REG_SYS_FUNC_EN,
	.maps[SYS_CLK] = REG_SYS_CLKR,
	.maps[MAC_RCR_AM] = RCR_AM,
	.maps[MAC_RCR_AB] = RCR_AB,
	.maps[MAC_RCR_ACRC32] = RCR_ACRC32,
	.maps[MAC_RCR_ACF] = RCR_ACF,
	.maps[MAC_RCR_AAP] = RCR_AAP,

	.maps[EFUSE_TEST] = REG_EFUSE_TEST,
	.maps[EFUSE_ACCESS] = REG_EFUSE_ACCESS,
	.maps[EFUSE_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_CLK] = 0,	/* just for 92se */
	.maps[EFUSE_CLK_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_PWC_EV12V] = PWC_EV12V,
	.maps[EFUSE_FEN_ELDR] = FEN_ELDR,
	.maps[EFUSE_LOADER_CLK_EN] = 0,
	.maps[EFUSE_ANA8M] = 0,	/* just for 92se */
	.maps[EFUSE_HWSET_MAX_SIZE] = HWSET_MAX_SIZE,
	.maps[EFUSE_MAX_SECTION_MAP] = EFUSE_MAX_SECTION,
	.maps[EFUSE_REAL_CONTENT_SIZE] = EFUSE_REAL_CONTENT_LEN,

	.maps[RWCAM] = REG_CAMCMD,
	.maps[WCAMI] = REG_CAMWRITE,
	.maps[RCAMO] = REG_CAMREAD,
	.maps[CAMDBG] = REG_CAMDBG,
	.maps[SECR] = REG_SECCFG,
	.maps[SEC_CAM_NONE] = CAM_NONE,
	.maps[SEC_CAM_WEP40] = CAM_WEP40,
	.maps[SEC_CAM_TKIP] = CAM_TKIP,
	.maps[SEC_CAM_AES] = CAM_AES,
	.maps[SEC_CAM_WEP104] = CAM_WEP104,

	.maps[RTL_IMR_BCNDMAINT6] = IMR_BCNDMAINT6,
	.maps[RTL_IMR_BCNDMAINT5] = IMR_BCNDMAINT5,
	.maps[RTL_IMR_BCNDMAINT4] = IMR_BCNDMAINT4,
	.maps[RTL_IMR_BCNDMAINT3] = IMR_BCNDMAINT3,
	.maps[RTL_IMR_BCNDMAINT2] = IMR_BCNDMAINT2,
	.maps[RTL_IMR_BCNDMAINT1] = IMR_BCNDMAINT1,
	.maps[RTL_IMR_BCNDOK8] = IMR_BCNDOK8,
	.maps[RTL_IMR_BCNDOK7] = IMR_BCNDOK7,
	.maps[RTL_IMR_BCNDOK6] = IMR_BCNDOK6,
	.maps[RTL_IMR_BCNDOK5] = IMR_BCNDOK5,
	.maps[RTL_IMR_BCNDOK4] = IMR_BCNDOK4,
	.maps[RTL_IMR_BCNDOK3] = IMR_BCNDOK3,
	.maps[RTL_IMR_BCNDOK2] = IMR_BCNDOK2,
	.maps[RTL_IMR_BCNDOK1] = IMR_BCNDOK1,
	.maps[RTL_IMR_TIMEOUT2] = IMR_TIMEOUT2,
	.maps[RTL_IMR_TIMEOUT1] = IMR_TIMEOUT1,

	.maps[RTL_IMR_TXFOVW] = IMR_TXFOVW,
	.maps[RTL_IMR_PSTIMEOUT] = IMR_PSTIMEOUT,
	.maps[RTL_IMR_BCNINT] = IMR_BCNINT,
	.maps[RTL_IMR_RXFOVW] = IMR_RXFOVW,
	.maps[RTL_IMR_RDU] = IMR_RDU,
	.maps[RTL_IMR_ATIMEND] = IMR_ATIMEND,
	.maps[RTL_IMR_BDOK] = IMR_BDOK,
	.maps[RTL_IMR_MGNTDOK] = IMR_MGNTDOK,
	.maps[RTL_IMR_TBDER] = IMR_TBDER,
	.maps[RTL_IMR_HIGHDOK] = IMR_HIGHDOK,
	.maps[RTL_IMR_TBDOK] = IMR_TBDOK,
	.maps[RTL_IMR_BKDOK] = IMR_BKDOK,
	.maps[RTL_IMR_BEDOK] = IMR_BEDOK,
	.maps[RTL_IMR_VIDOK] = IMR_VIDOK,
	.maps[RTL_IMR_VODOK] = IMR_VODOK,
	.maps[RTL_IMR_ROK] = IMR_ROK,
	.maps[RTL_IBSS_INT_MASKS] = (IMR_BCNINT | IMR_TBDOK | IMR_TBDER),

	.maps[RTL_RC_CCK_RATE1M] = DESC_RATE1M,
	.maps[RTL_RC_CCK_RATE2M] = DESC_RATE2M,
	.maps[RTL_RC_CCK_RATE5_5M] = DESC_RATE5_5M,
	.maps[RTL_RC_CCK_RATE11M] = DESC_RATE11M,
	.maps[RTL_RC_OFDM_RATE6M] = DESC_RATE6M,
	.maps[RTL_RC_OFDM_RATE9M] = DESC_RATE9M,
	.maps[RTL_RC_OFDM_RATE12M] = DESC_RATE12M,
	.maps[RTL_RC_OFDM_RATE18M] = DESC_RATE18M,
	.maps[RTL_RC_OFDM_RATE24M] = DESC_RATE24M,
	.maps[RTL_RC_OFDM_RATE36M] = DESC_RATE36M,
	.maps[RTL_RC_OFDM_RATE48M] = DESC_RATE48M,
	.maps[RTL_RC_OFDM_RATE54M] = DESC_RATE54M,

	.maps[RTL_RC_HT_RATEMCS7] = DESC_RATEMCS7,
	.maps[RTL_RC_HT_RATEMCS15] = DESC_RATEMCS15,
};

module_param_named(swenc, rtl92du_mod_params.sw_crypto, bool, 0444);
module_param_named(debug_level, rtl92du_mod_params.debug_level, int, 0644);
module_param_named(ips, rtl92du_mod_params.inactiveps, bool, 0444);
module_param_named(swlps, rtl92du_mod_params.swctrl_lps, bool, 0444);
module_param_named(debug_mask, rtl92du_mod_params.debug_mask, ullong, 0644);
MODULE_PARM_DESC(swenc, "Set to 1 for software crypto (default 0)\n");
MODULE_PARM_DESC(ips, "Set to 0 to not use link power save (default 0)\n");
MODULE_PARM_DESC(swlps, "Set to 1 to use SW control power save (default 0)\n");
MODULE_PARM_DESC(debug_level, "Set debug level (0-5) (default 0)");
MODULE_PARM_DESC(debug_mask, "Set debug mask (default 0)");

#define USB_VENDOR_ID_REALTEK		0x0bda

static const struct usb_device_id rtl8192d_usb_ids[] = {
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0x8193, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0x8194, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0x8111, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0x0193, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0x8171, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDOR_ID_REALTEK, 0xe194, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x2019, 0xab2c, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x2019, 0xab2d, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x2019, 0x4903, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x2019, 0x4904, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x07b8, 0x8193, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x20f4, 0x664b, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x04dd, 0x954f, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x04dd, 0x96a6, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x050d, 0x110a, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x050d, 0x1105, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x050d, 0x120a, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x1668, 0x8102, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x0930, 0x0a0a, rtl92du_hal_cfg)},
	{RTL_USB_DEVICE(0x2001, 0x330c, rtl92du_hal_cfg)},
	{}
};

MODULE_DEVICE_TABLE(usb, rtl8192d_usb_ids);

static int rtl8192du_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	return rtl_usb_probe(intf, id, &rtl92du_hal_cfg);
}

static struct usb_driver rtl8192du_driver = {
	.name = "rtl8192du",
	.probe = rtl8192du_probe,
	.disconnect = rtl_usb_disconnect,
	.id_table = rtl8192d_usb_ids,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(rtl8192du_driver);

MODULE_AUTHOR("Bitterblue Smith	<rtl8821cerfe2@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8192DU 802.11n Dual Mac USB wireless");
MODULE_FIRMWARE("rtlwifi/rtl8192dufw.bin");
