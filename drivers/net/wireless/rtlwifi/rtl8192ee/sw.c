/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "hw.h"
#include "sw.h"
#include "fw.h"
#include "trx.h"
#include "led.h"
#include "table.h"

#include "../btcoexist/rtl_btc.h"

#include <linux/vmalloc.h>
#include <linux/module.h>

static void rtl92ee_init_aspm_vars(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/*close ASPM for AMD defaultly */
	rtlpci->const_amdpci_aspm = 0;

	/**
	 * ASPM PS mode.
	 * 0 - Disable ASPM,
	 * 1 - Enable ASPM without Clock Req,
	 * 2 - Enable ASPM with Clock Req,
	 * 3 - Alwyas Enable ASPM with Clock Req,
	 * 4 - Always Enable ASPM without Clock Req.
	 * set defult to RTL8192CE:3 RTL8192E:2
	 */
	rtlpci->const_pci_aspm = 3;

	/*Setting for PCI-E device */
	rtlpci->const_devicepci_aspm_setting = 0x03;

	/*Setting for PCI-E bridge */
	rtlpci->const_hostpci_aspm_setting = 0x02;

	/**
	 * In Hw/Sw Radio Off situation.
	 * 0 - Default,
	 * 1 - From ASPM setting without low Mac Pwr,
	 * 2 - From ASPM setting with low Mac Pwr,
	 * 3 - Bus D3
	 * set default to RTL8192CE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 0;

	/**
	 * This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = 1;
}

int rtl92ee_init_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int err = 0;

	rtl92ee_bt_reg_init(hw);
	rtlpci->msi_support = rtlpriv->cfg->mod_params->msi_support;
	rtlpriv->btcoexist.btc_ops = rtl_btc_get_ops_pointer();

	rtlpriv->dm.dm_initialgain_enable = 1;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = 0;
	rtlpci->transmit_config = CFENDFORM | BIT(15);

	/*just 2.4G band*/
	rtlpriv->rtlhal.current_bandtype = BAND_ON_2_4G;
	rtlpriv->rtlhal.bandset = BAND_ON_2_4G;
	rtlpriv->rtlhal.macphymode = SINGLEMAC_SINGLEPHY;

	rtlpci->receive_config = (RCR_APPFCS			|
				  RCR_APP_MIC			|
				  RCR_APP_ICV			|
				  RCR_APP_PHYST_RXFF		|
				  RCR_HTC_LOC_CTRL		|
				  RCR_AMF			|
				  RCR_ACF			|
				  RCR_ACRC32			|
				  RCR_AB			|
				  RCR_AM			|
				  RCR_APM			|
				  0);

	rtlpci->irq_mask[0] = (u32)(IMR_PSTIMEOUT		|
				     IMR_C2HCMD			|
				     IMR_HIGHDOK		|
				     IMR_MGNTDOK		|
				     IMR_BKDOK			|
				     IMR_BEDOK			|
				     IMR_VIDOK			|
				     IMR_VODOK			|
				     IMR_RDU			|
				     IMR_ROK			|
				     0);
	rtlpci->irq_mask[1] = (u32)(IMR_RXFOVW | 0);

	/* for debug level */
	rtlpriv->dbg.global_debuglevel = rtlpriv->cfg->mod_params->debug;
	/* for LPS & IPS */
	rtlpriv->psc.inactiveps = rtlpriv->cfg->mod_params->inactiveps;
	rtlpriv->psc.swctrl_lps = rtlpriv->cfg->mod_params->swctrl_lps;
	rtlpriv->psc.fwctrl_lps = rtlpriv->cfg->mod_params->fwctrl_lps;
	rtlpci->msi_support = rtlpriv->cfg->mod_params->msi_support;
	if (rtlpriv->cfg->mod_params->disable_watchdog)
		pr_info("watchdog disabled\n");
	rtlpriv->psc.reg_fwctrl_lps = 3;
	rtlpriv->psc.reg_max_lps_awakeintvl = 5;
	/* for ASPM, you can close aspm through
	 * set const_support_pciaspm = 0
	 */
	rtl92ee_init_aspm_vars(hw);

	if (rtlpriv->psc.reg_fwctrl_lps == 1)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MIN_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 2)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MAX_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 3)
		rtlpriv->psc.fwctrl_psmode = FW_PS_DTIM_MODE;

	/* for early mode */
	rtlpriv->rtlhal.earlymode_enable = false;

	/*low power */
	rtlpriv->psc.low_power_enable = false;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = vzalloc(0x8000);
	if (!rtlpriv->rtlhal.pfirmware) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Can't alloc buffer for fw\n");
		return 1;
	}

	/* request fw */
	rtlpriv->cfg->fw_name = "rtlwifi/rtl8192eefw.bin";

	rtlpriv->max_fw_size = 0x8000;
	pr_info("Using firmware %s\n", rtlpriv->cfg->fw_name);
	err = request_firmware_nowait(THIS_MODULE, 1, rtlpriv->cfg->fw_name,
				      rtlpriv->io.dev, GFP_KERNEL, hw,
				      rtl_fw_cb);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Failed to request firmware!\n");
		return 1;
	}

	return 0;
}

void rtl92ee_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.pfirmware) {
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}
}

/* get bt coexist status */
bool rtl92ee_get_btc_status(void)
{
	return true;
}

static struct rtl_hal_ops rtl8192ee_hal_ops = {
	.init_sw_vars = rtl92ee_init_sw_vars,
	.deinit_sw_vars = rtl92ee_deinit_sw_vars,
	.read_eeprom_info = rtl92ee_read_eeprom_info,
	.interrupt_recognized = rtl92ee_interrupt_recognized,/*need check*/
	.hw_init = rtl92ee_hw_init,
	.hw_disable = rtl92ee_card_disable,
	.hw_suspend = rtl92ee_suspend,
	.hw_resume = rtl92ee_resume,
	.enable_interrupt = rtl92ee_enable_interrupt,
	.disable_interrupt = rtl92ee_disable_interrupt,
	.set_network_type = rtl92ee_set_network_type,
	.set_chk_bssid = rtl92ee_set_check_bssid,
	.set_qos = rtl92ee_set_qos,
	.set_bcn_reg = rtl92ee_set_beacon_related_registers,
	.set_bcn_intv = rtl92ee_set_beacon_interval,
	.update_interrupt_mask = rtl92ee_update_interrupt_mask,
	.get_hw_reg = rtl92ee_get_hw_reg,
	.set_hw_reg = rtl92ee_set_hw_reg,
	.update_rate_tbl = rtl92ee_update_hal_rate_tbl,
	.pre_fill_tx_bd_desc = rtl92ee_pre_fill_tx_bd_desc,
	.rx_desc_buff_remained_cnt = rtl92ee_rx_desc_buff_remained_cnt,
	.rx_check_dma_ok = rtl92ee_rx_check_dma_ok,
	.fill_tx_desc = rtl92ee_tx_fill_desc,
	.fill_tx_cmddesc = rtl92ee_tx_fill_cmddesc,
	.query_rx_desc = rtl92ee_rx_query_desc,
	.set_channel_access = rtl92ee_update_channel_access_setting,
	.radio_onoff_checking = rtl92ee_gpio_radio_on_off_checking,
	.set_bw_mode = rtl92ee_phy_set_bw_mode,
	.switch_channel = rtl92ee_phy_sw_chnl,
	.dm_watchdog = rtl92ee_dm_watchdog,
	.scan_operation_backup = rtl92ee_phy_scan_operation_backup,
	.set_rf_power_state = rtl92ee_phy_set_rf_power_state,
	.led_control = rtl92ee_led_control,
	.set_desc = rtl92ee_set_desc,
	.get_desc = rtl92ee_get_desc,
	.is_tx_desc_closed = rtl92ee_is_tx_desc_closed,
	.get_available_desc = rtl92ee_get_available_desc,
	.tx_polling = rtl92ee_tx_polling,
	.enable_hw_sec = rtl92ee_enable_hw_security_config,
	.set_key = rtl92ee_set_key,
	.init_sw_leds = rtl92ee_init_sw_leds,
	.get_bbreg = rtl92ee_phy_query_bb_reg,
	.set_bbreg = rtl92ee_phy_set_bb_reg,
	.get_rfreg = rtl92ee_phy_query_rf_reg,
	.set_rfreg = rtl92ee_phy_set_rf_reg,
	.fill_h2c_cmd = rtl92ee_fill_h2c_cmd,
	.get_btc_status = rtl92ee_get_btc_status,
	.rx_command_packet = rtl92ee_rx_command_packet,
};

static struct rtl_mod_params rtl92ee_mod_params = {
	.sw_crypto = false,
	.inactiveps = false,
	.swctrl_lps = false,
	.fwctrl_lps = true,
	.msi_support = true,
	.debug = DBG_EMERG,
};

static struct rtl_hal_cfg rtl92ee_hal_cfg = {
	.bar_id = 2,
	.write_readback = true,
	.name = "rtl92ee_pci",
	.fw_name = "rtlwifi/rtl8192eefw.bin",
	.ops = &rtl8192ee_hal_ops,
	.mod_params = &rtl92ee_mod_params,

	.maps[SYS_ISO_CTRL] = REG_SYS_ISO_CTRL,
	.maps[SYS_FUNC_EN] = REG_SYS_FUNC_EN,
	.maps[SYS_CLK] = REG_SYS_CLKR,
	.maps[MAC_RCR_AM] = AM,
	.maps[MAC_RCR_AB] = AB,
	.maps[MAC_RCR_ACRC32] = ACRC32,
	.maps[MAC_RCR_ACF] = ACF,
	.maps[MAC_RCR_AAP] = AAP,
	.maps[MAC_HIMR] = REG_HIMR,
	.maps[MAC_HIMRE] = REG_HIMRE,

	.maps[EFUSE_ACCESS] = REG_EFUSE_ACCESS,

	.maps[EFUSE_TEST] = REG_EFUSE_TEST,
	.maps[EFUSE_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_CLK] = 0,
	.maps[EFUSE_CLK_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_PWC_EV12V] = PWC_EV12V,
	.maps[EFUSE_FEN_ELDR] = FEN_ELDR,
	.maps[EFUSE_LOADER_CLK_EN] = LOADER_CLK_EN,
	.maps[EFUSE_ANA8M] = ANA8M,
	.maps[EFUSE_HWSET_MAX_SIZE] = HWSET_MAX_SIZE,
	.maps[EFUSE_MAX_SECTION_MAP] = EFUSE_MAX_SECTION,
	.maps[EFUSE_REAL_CONTENT_SIZE] = EFUSE_REAL_CONTENT_LEN,
	.maps[EFUSE_OOB_PROTECT_BYTES_LEN] = EFUSE_OOB_PROTECT_BYTES,

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
	.maps[RTL_IMR_BCNDOK7] = IMR_BCNDOK7,
	.maps[RTL_IMR_BCNDOK6] = IMR_BCNDOK6,
	.maps[RTL_IMR_BCNDOK5] = IMR_BCNDOK5,
	.maps[RTL_IMR_BCNDOK4] = IMR_BCNDOK4,
	.maps[RTL_IMR_BCNDOK3] = IMR_BCNDOK3,
	.maps[RTL_IMR_BCNDOK2] = IMR_BCNDOK2,
	.maps[RTL_IMR_BCNDOK1] = IMR_BCNDOK1,

	.maps[RTL_IMR_TXFOVW] = IMR_TXFOVW,
	.maps[RTL_IMR_PSTIMEOUT] = IMR_PSTIMEOUT,
	.maps[RTL_IMR_BCNINT] = IMR_BCNDMAINT0,
	.maps[RTL_IMR_RXFOVW] = IMR_RXFOVW,
	.maps[RTL_IMR_RDU] = IMR_RDU,
	.maps[RTL_IMR_ATIMEND] = IMR_ATIMEND,
	.maps[RTL_IMR_BDOK] = IMR_BCNDOK0,
	.maps[RTL_IMR_MGNTDOK] = IMR_MGNTDOK,
	.maps[RTL_IMR_TBDER] = IMR_TBDER,
	.maps[RTL_IMR_HIGHDOK] = IMR_HIGHDOK,
	.maps[RTL_IMR_TBDOK] = IMR_TBDOK,
	.maps[RTL_IMR_BKDOK] = IMR_BKDOK,
	.maps[RTL_IMR_BEDOK] = IMR_BEDOK,
	.maps[RTL_IMR_VIDOK] = IMR_VIDOK,
	.maps[RTL_IMR_VODOK] = IMR_VODOK,
	.maps[RTL_IMR_ROK] = IMR_ROK,
	.maps[RTL_IBSS_INT_MASKS] = (IMR_BCNDMAINT0 | IMR_TBDOK | IMR_TBDER),

	.maps[RTL_RC_CCK_RATE1M] = DESC92C_RATE1M,
	.maps[RTL_RC_CCK_RATE2M] = DESC92C_RATE2M,
	.maps[RTL_RC_CCK_RATE5_5M] = DESC92C_RATE5_5M,
	.maps[RTL_RC_CCK_RATE11M] = DESC92C_RATE11M,
	.maps[RTL_RC_OFDM_RATE6M] = DESC92C_RATE6M,
	.maps[RTL_RC_OFDM_RATE9M] = DESC92C_RATE9M,
	.maps[RTL_RC_OFDM_RATE12M] = DESC92C_RATE12M,
	.maps[RTL_RC_OFDM_RATE18M] = DESC92C_RATE18M,
	.maps[RTL_RC_OFDM_RATE24M] = DESC92C_RATE24M,
	.maps[RTL_RC_OFDM_RATE36M] = DESC92C_RATE36M,
	.maps[RTL_RC_OFDM_RATE48M] = DESC92C_RATE48M,
	.maps[RTL_RC_OFDM_RATE54M] = DESC92C_RATE54M,

	.maps[RTL_RC_HT_RATEMCS7] = DESC92C_RATEMCS7,
	.maps[RTL_RC_HT_RATEMCS15] = DESC92C_RATEMCS15,
};

static struct pci_device_id rtl92ee_pci_ids[] = {
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x818B, rtl92ee_hal_cfg)},
	{},
};

MODULE_DEVICE_TABLE(pci, rtl92ee_pci_ids);

MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.Finger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8192EE 802.11n PCI wireless");
MODULE_FIRMWARE("rtlwifi/rtl8192eefw.bin");

module_param_named(swenc, rtl92ee_mod_params.sw_crypto, bool, 0444);
module_param_named(debug, rtl92ee_mod_params.debug, int, 0444);
module_param_named(ips, rtl92ee_mod_params.inactiveps, bool, 0444);
module_param_named(swlps, rtl92ee_mod_params.swctrl_lps, bool, 0444);
module_param_named(fwlps, rtl92ee_mod_params.fwctrl_lps, bool, 0444);
module_param_named(msi, rtl92ee_mod_params.msi_support, bool, 0444);
module_param_named(disable_watchdog, rtl92ee_mod_params.disable_watchdog,
		   bool, 0444);
MODULE_PARM_DESC(swenc, "Set to 1 for software crypto (default 0)\n");
MODULE_PARM_DESC(ips, "Set to 0 to not use link power save (default 1)\n");
MODULE_PARM_DESC(swlps, "Set to 1 to use SW control power save (default 0)\n");
MODULE_PARM_DESC(fwlps, "Set to 1 to use FW control power save (default 1)\n");
MODULE_PARM_DESC(msi, "Set to 1 to use MSI interrupts mode (default 1)\n");
MODULE_PARM_DESC(debug, "Set debug level (0-5) (default 0)");
MODULE_PARM_DESC(disable_watchdog, "Set to 1 to disable the watchdog (default 0)\n");

static SIMPLE_DEV_PM_OPS(rtlwifi_pm_ops, rtl_pci_suspend, rtl_pci_resume);

static struct pci_driver rtl92ee_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl92ee_pci_ids,
	.probe = rtl_pci_probe,
	.remove = rtl_pci_disconnect,
	.driver.pm = &rtlwifi_pm_ops,
};

module_pci_driver(rtl92ee_driver);
