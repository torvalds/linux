/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../core.h"
#include "../usb.h"
#include "../efuse.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "mac.h"
#include "dm.h"
#include "rf.h"
#include "sw.h"
#include "trx.h"
#include "led.h"
#include "hw.h"
#include <linux/module.h>

MODULE_AUTHOR("Georgia		<georgia@realtek.com>");
MODULE_AUTHOR("Ziv Huang	<ziv_huang@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.Finger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8192C/8188C 802.11n USB wireless");
MODULE_FIRMWARE("rtlwifi/rtl8192cufw.bin");

static int rtl92cu_init_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err;

	rtlpriv->dm.dm_initialgain_enable = true;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = false;
	rtlpriv->dm.thermalvalue = 0;
	rtlpriv->dbg.global_debuglevel = rtlpriv->cfg->mod_params->debug;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = vzalloc(0x4000);
	if (!rtlpriv->rtlhal.pfirmware) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Can't alloc buffer for fw\n");
		return 1;
	}

	pr_info("Loading firmware %s\n", rtlpriv->cfg->fw_name);
	rtlpriv->max_fw_size = 0x4000;
	err = request_firmware_nowait(THIS_MODULE, 1,
				      rtlpriv->cfg->fw_name, rtlpriv->io.dev,
				      GFP_KERNEL, hw, rtl_fw_cb);


	return 0;
}

static void rtl92cu_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.pfirmware) {
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}
}

static struct rtl_hal_ops rtl8192cu_hal_ops = {
	.init_sw_vars = rtl92cu_init_sw_vars,
	.deinit_sw_vars = rtl92cu_deinit_sw_vars,
	.read_chip_version = rtl92c_read_chip_version,
	.read_eeprom_info = rtl92cu_read_eeprom_info,
	.enable_interrupt = rtl92c_enable_interrupt,
	.disable_interrupt = rtl92c_disable_interrupt,
	.hw_init = rtl92cu_hw_init,
	.hw_disable = rtl92cu_card_disable,
	.set_network_type = rtl92cu_set_network_type,
	.set_chk_bssid = rtl92cu_set_check_bssid,
	.set_qos = rtl92c_set_qos,
	.set_bcn_reg = rtl92cu_set_beacon_related_registers,
	.set_bcn_intv = rtl92cu_set_beacon_interval,
	.update_interrupt_mask = rtl92cu_update_interrupt_mask,
	.get_hw_reg = rtl92cu_get_hw_reg,
	.set_hw_reg = rtl92cu_set_hw_reg,
	.update_rate_tbl = rtl92cu_update_hal_rate_table,
	.update_rate_mask = rtl92cu_update_hal_rate_mask,
	.fill_tx_desc = rtl92cu_tx_fill_desc,
	.fill_fake_txdesc = rtl92cu_fill_fake_txdesc,
	.fill_tx_cmddesc = rtl92cu_tx_fill_cmddesc,
	.cmd_send_packet = rtl92cu_cmd_send_packet,
	.query_rx_desc = rtl92cu_rx_query_desc,
	.set_channel_access = rtl92cu_update_channel_access_setting,
	.radio_onoff_checking = rtl92cu_gpio_radio_on_off_checking,
	.set_bw_mode = rtl92c_phy_set_bw_mode,
	.switch_channel = rtl92c_phy_sw_chnl,
	.dm_watchdog = rtl92c_dm_watchdog,
	.scan_operation_backup = rtl92c_phy_scan_operation_backup,
	.set_rf_power_state = rtl92cu_phy_set_rf_power_state,
	.led_control = rtl92cu_led_control,
	.enable_hw_sec = rtl92cu_enable_hw_security_config,
	.set_key = rtl92c_set_key,
	.init_sw_leds = rtl92cu_init_sw_leds,
	.deinit_sw_leds = rtl92cu_deinit_sw_leds,
	.get_bbreg = rtl92c_phy_query_bb_reg,
	.set_bbreg = rtl92c_phy_set_bb_reg,
	.get_rfreg = rtl92cu_phy_query_rf_reg,
	.set_rfreg = rtl92cu_phy_set_rf_reg,
	.phy_rf6052_config = rtl92cu_phy_rf6052_config,
	.phy_rf6052_set_cck_txpower = rtl92cu_phy_rf6052_set_cck_txpower,
	.phy_rf6052_set_ofdm_txpower = rtl92cu_phy_rf6052_set_ofdm_txpower,
	.config_bb_with_headerfile = _rtl92cu_phy_config_bb_with_headerfile,
	.config_bb_with_pgheaderfile = _rtl92cu_phy_config_bb_with_pgheaderfile,
	.phy_lc_calibrate = _rtl92cu_phy_lc_calibrate,
	.phy_set_bw_mode_callback = rtl92cu_phy_set_bw_mode_callback,
	.dm_dynamic_txpower = rtl92cu_dm_dynamic_txpower,
};

static struct rtl_mod_params rtl92cu_mod_params = {
	.sw_crypto = 0,
	.debug = DBG_EMERG,
};

module_param_named(swenc, rtl92cu_mod_params.sw_crypto, bool, 0444);
module_param_named(debug, rtl92cu_mod_params.debug, int, 0444);
MODULE_PARM_DESC(swenc, "Set to 1 for software crypto (default 0)\n");
MODULE_PARM_DESC(debug, "Set debug level (0-5) (default 0)");

static struct rtl_hal_usbint_cfg rtl92cu_interface_cfg = {
	/* rx */
	.in_ep_num = RTL92C_USB_BULK_IN_NUM,
	.rx_urb_num = RTL92C_NUM_RX_URBS,
	.rx_max_size = RTL92C_SIZE_MAX_RX_BUFFER,
	.usb_rx_hdl = rtl8192cu_rx_hdl,
	.usb_rx_segregate_hdl = NULL, /* rtl8192c_rx_segregate_hdl; */
	/* tx */
	.usb_tx_cleanup = rtl8192c_tx_cleanup,
	.usb_tx_post_hdl = rtl8192c_tx_post_hdl,
	.usb_tx_aggregate_hdl = rtl8192c_tx_aggregate_hdl,
	/* endpoint mapping */
	.usb_endpoint_mapping = rtl8192cu_endpoint_mapping,
	.usb_mq_to_hwq = rtl8192cu_mq_to_hwq,
};

static struct rtl_hal_cfg rtl92cu_hal_cfg = {
	.name = "rtl92c_usb",
	.fw_name = "rtlwifi/rtl8192cufw.bin",
	.ops = &rtl8192cu_hal_ops,
	.mod_params = &rtl92cu_mod_params,
	.usb_interface_cfg = &rtl92cu_interface_cfg,

	.maps[SYS_ISO_CTRL] = REG_SYS_ISO_CTRL,
	.maps[SYS_FUNC_EN] = REG_SYS_FUNC_EN,
	.maps[SYS_CLK] = REG_SYS_CLKR,
	.maps[MAC_RCR_AM] = AM,
	.maps[MAC_RCR_AB] = AB,
	.maps[MAC_RCR_ACRC32] = ACRC32,
	.maps[MAC_RCR_ACF] = ACF,
	.maps[MAC_RCR_AAP] = AAP,

	.maps[EFUSE_TEST] = REG_EFUSE_TEST,
	.maps[EFUSE_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_CLK] = 0,
	.maps[EFUSE_CLK_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_PWC_EV12V] = PWC_EV12V,
	.maps[EFUSE_FEN_ELDR] = FEN_ELDR,
	.maps[EFUSE_LOADER_CLK_EN] = LOADER_CLK_EN,
	.maps[EFUSE_ANA8M] = EFUSE_ANA8M,
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
	.maps[RTL_IMR_BcnInt] = IMR_BCNINT,
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

	.maps[RTL_RC_CCK_RATE1M] = DESC92_RATE1M,
	.maps[RTL_RC_CCK_RATE2M] = DESC92_RATE2M,
	.maps[RTL_RC_CCK_RATE5_5M] = DESC92_RATE5_5M,
	.maps[RTL_RC_CCK_RATE11M] = DESC92_RATE11M,
	.maps[RTL_RC_OFDM_RATE6M] = DESC92_RATE6M,
	.maps[RTL_RC_OFDM_RATE9M] = DESC92_RATE9M,
	.maps[RTL_RC_OFDM_RATE12M] = DESC92_RATE12M,
	.maps[RTL_RC_OFDM_RATE18M] = DESC92_RATE18M,
	.maps[RTL_RC_OFDM_RATE24M] = DESC92_RATE24M,
	.maps[RTL_RC_OFDM_RATE36M] = DESC92_RATE36M,
	.maps[RTL_RC_OFDM_RATE48M] = DESC92_RATE48M,
	.maps[RTL_RC_OFDM_RATE54M] = DESC92_RATE54M,
	.maps[RTL_RC_HT_RATEMCS7] = DESC92_RATEMCS7,
	.maps[RTL_RC_HT_RATEMCS15] = DESC92_RATEMCS15,
};

#define USB_VENDER_ID_REALTEK		0x0bda

/* 2010-10-19 DID_USB_V3.4 */
static struct usb_device_id rtl8192c_usb_ids[] = {

	/*=== Realtek demoboard ===*/
	/* Default ID */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8191, rtl92cu_hal_cfg)},

	/****** 8188CU ********/
	/* RTL8188CTV */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x018a, rtl92cu_hal_cfg)},
	/* 8188CE-VAU USB minCard */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8170, rtl92cu_hal_cfg)},
	/* 8188cu 1*1 dongle */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8176, rtl92cu_hal_cfg)},
	/* 8188cu 1*1 dongle, (b/g mode only) */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8177, rtl92cu_hal_cfg)},
	/* 8188cu Slim Solo */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817a, rtl92cu_hal_cfg)},
	/* 8188cu Slim Combo */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817b, rtl92cu_hal_cfg)},
	/* 8188RU High-power USB Dongle */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817d, rtl92cu_hal_cfg)},
	/* 8188CE-VAU USB minCard (b/g mode only) */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817e, rtl92cu_hal_cfg)},
	/* 8188RU in Alfa AWUS036NHR */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817f, rtl92cu_hal_cfg)},
	/* RTL8188CUS-VL */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x818a, rtl92cu_hal_cfg)},
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x819a, rtl92cu_hal_cfg)},
	/* 8188 Combo for BC4 */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8754, rtl92cu_hal_cfg)},

	/****** 8192CU ********/
	/* 8192cu 2*2 */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8178, rtl92cu_hal_cfg)},
	/* 8192CE-VAU USB minCard */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817c, rtl92cu_hal_cfg)},

	/*=== Customer ID ===*/
	/****** 8188CU ********/
	{RTL_USB_DEVICE(0x050d, 0x1102, rtl92cu_hal_cfg)}, /*Belkin - Edimax*/
	{RTL_USB_DEVICE(0x050d, 0x11f2, rtl92cu_hal_cfg)}, /*Belkin - ISY*/
	{RTL_USB_DEVICE(0x06f8, 0xe033, rtl92cu_hal_cfg)}, /*Hercules - Edimax*/
	{RTL_USB_DEVICE(0x07b8, 0x8188, rtl92cu_hal_cfg)}, /*Abocom - Abocom*/
	{RTL_USB_DEVICE(0x07b8, 0x8189, rtl92cu_hal_cfg)}, /*Funai - Abocom*/
	{RTL_USB_DEVICE(0x0846, 0x9041, rtl92cu_hal_cfg)}, /*NetGear WNA1000M*/
	{RTL_USB_DEVICE(0x0bda, 0x5088, rtl92cu_hal_cfg)}, /*Thinkware-CC&C*/
	{RTL_USB_DEVICE(0x0df6, 0x0052, rtl92cu_hal_cfg)}, /*Sitecom - Edimax*/
	{RTL_USB_DEVICE(0x0df6, 0x005c, rtl92cu_hal_cfg)}, /*Sitecom - Edimax*/
	{RTL_USB_DEVICE(0x0eb0, 0x9071, rtl92cu_hal_cfg)}, /*NO Brand - Etop*/
	{RTL_USB_DEVICE(0x4856, 0x0091, rtl92cu_hal_cfg)}, /*NetweeN - Feixun*/
	/* HP - Lite-On ,8188CUS Slim Combo */
	{RTL_USB_DEVICE(0x103c, 0x1629, rtl92cu_hal_cfg)},
	{RTL_USB_DEVICE(0x13d3, 0x3357, rtl92cu_hal_cfg)}, /* AzureWave */
	{RTL_USB_DEVICE(0x2001, 0x3308, rtl92cu_hal_cfg)}, /*D-Link - Alpha*/
	{RTL_USB_DEVICE(0x2019, 0x4902, rtl92cu_hal_cfg)}, /*Planex - Etop*/
	{RTL_USB_DEVICE(0x2019, 0xab2a, rtl92cu_hal_cfg)}, /*Planex - Abocom*/
	/*SW-WF02-AD15 -Abocom*/
	{RTL_USB_DEVICE(0x2019, 0xab2e, rtl92cu_hal_cfg)},
	{RTL_USB_DEVICE(0x2019, 0xed17, rtl92cu_hal_cfg)}, /*PCI - Edimax*/
	{RTL_USB_DEVICE(0x20f4, 0x648b, rtl92cu_hal_cfg)}, /*TRENDnet - Cameo*/
	{RTL_USB_DEVICE(0x7392, 0x7811, rtl92cu_hal_cfg)}, /*Edimax - Edimax*/
	{RTL_USB_DEVICE(0x13d3, 0x3358, rtl92cu_hal_cfg)}, /*Azwave 8188CE-VAU*/
	/* Russian customer -Azwave (8188CE-VAU  b/g mode only) */
	{RTL_USB_DEVICE(0x13d3, 0x3359, rtl92cu_hal_cfg)},
	{RTL_USB_DEVICE(0x4855, 0x0090, rtl92cu_hal_cfg)}, /* Feixun */
	{RTL_USB_DEVICE(0x4855, 0x0091, rtl92cu_hal_cfg)}, /* NetweeN-Feixun */
	{RTL_USB_DEVICE(0x9846, 0x9041, rtl92cu_hal_cfg)}, /* Netgear Cameo */

	/****** 8188 RU ********/
	/* Netcore */
	{RTL_USB_DEVICE(USB_VENDER_ID_REALTEK, 0x317f, rtl92cu_hal_cfg)},

	/****** 8188CUS Slim Solo********/
	{RTL_USB_DEVICE(0x04f2, 0xaff7, rtl92cu_hal_cfg)}, /*Xavi*/
	{RTL_USB_DEVICE(0x04f2, 0xaff9, rtl92cu_hal_cfg)}, /*Xavi*/
	{RTL_USB_DEVICE(0x04f2, 0xaffa, rtl92cu_hal_cfg)}, /*Xavi*/

	/****** 8188CUS Slim Combo ********/
	{RTL_USB_DEVICE(0x04f2, 0xaff8, rtl92cu_hal_cfg)}, /*Xavi*/
	{RTL_USB_DEVICE(0x04f2, 0xaffb, rtl92cu_hal_cfg)}, /*Xavi*/
	{RTL_USB_DEVICE(0x04f2, 0xaffc, rtl92cu_hal_cfg)}, /*Xavi*/
	{RTL_USB_DEVICE(0x2019, 0x1201, rtl92cu_hal_cfg)}, /*Planex-Vencer*/

	/****** 8192CU ********/
	{RTL_USB_DEVICE(0x050d, 0x2102, rtl92cu_hal_cfg)}, /*Belcom-Sercomm*/
	{RTL_USB_DEVICE(0x050d, 0x2103, rtl92cu_hal_cfg)}, /*Belcom-Edimax*/
	{RTL_USB_DEVICE(0x0586, 0x341f, rtl92cu_hal_cfg)}, /*Zyxel -Abocom*/
	{RTL_USB_DEVICE(0x07aa, 0x0056, rtl92cu_hal_cfg)}, /*ATKK-Gemtek*/
	{RTL_USB_DEVICE(0x07b8, 0x8178, rtl92cu_hal_cfg)}, /*Funai -Abocom*/
	{RTL_USB_DEVICE(0x0846, 0x9021, rtl92cu_hal_cfg)}, /*Netgear-Sercomm*/
	{RTL_USB_DEVICE(0x0b05, 0x17ab, rtl92cu_hal_cfg)}, /*ASUS-Edimax*/
	{RTL_USB_DEVICE(0x0bda, 0x8186, rtl92cu_hal_cfg)}, /*Realtek 92CE-VAU*/
	{RTL_USB_DEVICE(0x0df6, 0x0061, rtl92cu_hal_cfg)}, /*Sitecom-Edimax*/
	{RTL_USB_DEVICE(0x0e66, 0x0019, rtl92cu_hal_cfg)}, /*Hawking-Edimax*/
	{RTL_USB_DEVICE(0x2001, 0x3307, rtl92cu_hal_cfg)}, /*D-Link-Cameo*/
	{RTL_USB_DEVICE(0x2001, 0x3309, rtl92cu_hal_cfg)}, /*D-Link-Alpha*/
	{RTL_USB_DEVICE(0x2001, 0x330a, rtl92cu_hal_cfg)}, /*D-Link-Alpha*/
	{RTL_USB_DEVICE(0x2019, 0xab2b, rtl92cu_hal_cfg)}, /*Planex -Abocom*/
	{RTL_USB_DEVICE(0x20f4, 0x624d, rtl92cu_hal_cfg)}, /*TRENDNet*/
	{RTL_USB_DEVICE(0x7392, 0x7822, rtl92cu_hal_cfg)}, /*Edimax -Edimax*/
	{}
};

MODULE_DEVICE_TABLE(usb, rtl8192c_usb_ids);

static struct usb_driver rtl8192cu_driver = {
	.name = "rtl8192cu",
	.probe = rtl_usb_probe,
	.disconnect = rtl_usb_disconnect,
	.id_table = rtl8192c_usb_ids,

#ifdef CONFIG_PM
	/* .suspend = rtl_usb_suspend, */
	/* .resume = rtl_usb_resume, */
	/* .reset_resume = rtl8192c_resume, */
#endif /* CONFIG_PM */
#ifdef CONFIG_AUTOSUSPEND
	.supports_autosuspend = 1,
#endif
};

module_usb_driver(rtl8192cu_driver);
