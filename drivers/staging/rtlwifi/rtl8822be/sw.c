// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "hw.h"
#include "sw.h"
#include "fw.h"
#include "trx.h"
#include "led.h"
#include "../btcoexist/rtl_btc.h"
#include "../halmac/rtl_halmac.h"
#include "../phydm/rtl_phydm.h"
#include <linux/vmalloc.h>
#include <linux/module.h>

static void rtl8822be_init_aspm_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/*close ASPM for AMD defaultly */
	rtlpci->const_amdpci_aspm = 0;

	/*
	 * ASPM PS mode.
	 * 0 - Disable ASPM,
	 * 1 - Enable ASPM without Clock Req,
	 * 2 - Enable ASPM with Clock Req,
	 * 3 - Always Enable ASPM with Clock Req,
	 * 4 - Always Enable ASPM without Clock Req.
	 * set default to RTL8822BE:3 RTL8822B:2
	 *
	 */
	rtlpci->const_pci_aspm = 3;

	/*Setting for PCI-E device */
	rtlpci->const_devicepci_aspm_setting = 0x03;

	/*Setting for PCI-E bridge */
	rtlpci->const_hostpci_aspm_setting = 0x02;

	/*
	 * In Hw/Sw Radio Off situation.
	 * 0 - Default,
	 * 1 - From ASPM setting without low Mac Pwr,
	 * 2 - From ASPM setting with low Mac Pwr,
	 * 3 - Bus D3
	 * set default to RTL8822BE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 0;

	/*
	 * This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = rtlpriv->cfg->mod_params->aspm_support;
}

int rtl8822be_init_sw_vars(struct ieee80211_hw *hw)
{
	int err = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	const char *fw_name;
	struct rtl_phydm_params params;

	rtl8822be_bt_reg_init(hw);
	rtlpci->msi_support = rtlpriv->cfg->mod_params->msi_support;
	rtlpriv->btcoexist.btc_ops = rtl_btc_get_ops_pointer();
	rtlpriv->halmac.ops = rtl_halmac_get_ops_pointer();
	rtlpriv->halmac.ops->halmac_init_adapter(rtlpriv);

	/* should after halmac_init_adapter() */
	rtl8822be_read_eeprom_info(hw, &params);

	/* need eeprom info */
	rtlpriv->phydm.ops = rtl_phydm_get_ops_pointer();
	rtlpriv->phydm.ops->phydm_init_priv(rtlpriv, &params);

	rtlpriv->dm.dm_initialgain_enable = 1;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = 0;
	/*rtlpriv->dm.thermalvalue = 0;*/
	rtlpriv->dm.useramask = 1; /* turn on RA */
	rtlpci->transmit_config = CFENDFORM | BIT(15);

	rtlpriv->rtlhal.current_bandtype = BAND_ON_2_4G;
	/*following 2 is for register 5G band, refer to _rtl_init_mac80211()*/
	rtlpriv->rtlhal.bandset = BAND_ON_BOTH;
	rtlpriv->rtlhal.macphymode = SINGLEMAC_SINGLEPHY;

	rtlpci->receive_config = (RCR_APPFCS			|
				  RCR_APP_MIC			|
				  RCR_APP_ICV			|
				  RCR_APP_PHYST_RXFF		|
				  RCR_VHT_DACK			|
				  RCR_HTC_LOC_CTRL		|
				  /*RCR_AMF			|*/
				  RCR_CBSSID_BCN		|
				  RCR_CBSSID_DATA		|
				  /*RCR_ACF			|*/
				  /*RCR_ADF			|*/
				  /*RCR_AICV			|*/
				  /*RCR_ACRC32			|*/
				  RCR_AB			|
				  RCR_AM			|
				  RCR_APM			|
				  0);

	rtlpci->irq_mask[0] = (u32)(IMR_PSTIMEOUT		|
				    /*IMR_TBDER			|*/
				    /*IMR_TBDOK			|*/
				    /*IMR_BCNDMAINT0		|*/
				    IMR_GTINT3			|
				    IMR_HSISR_IND_ON_INT	|
				    IMR_C2HCMD			|
				    IMR_HIGHDOK			|
				    IMR_MGNTDOK			|
				    IMR_BKDOK			|
				    IMR_BEDOK			|
				    IMR_VIDOK			|
				    IMR_VODOK			|
				    IMR_RDU			|
				    IMR_ROK			|
				    0);

	rtlpci->irq_mask[1] = (u32)(IMR_RXFOVW | IMR_TXFOVW | 0);
	rtlpci->irq_mask[3] = (u32)(BIT_SETH2CDOK_MASK | 0);

	/* for LPS & IPS */
	rtlpriv->psc.inactiveps = rtlpriv->cfg->mod_params->inactiveps;
	rtlpriv->psc.swctrl_lps = rtlpriv->cfg->mod_params->swctrl_lps;
	rtlpriv->psc.fwctrl_lps = rtlpriv->cfg->mod_params->fwctrl_lps;
	rtlpci->msi_support = rtlpriv->cfg->mod_params->msi_support;
	if (rtlpriv->cfg->mod_params->disable_watchdog)
		pr_info("watchdog disabled\n");
	rtlpriv->psc.reg_fwctrl_lps = 2;
	rtlpriv->psc.reg_max_lps_awakeintvl = 2;
	/* for ASPM, you can close aspm through
	 * set const_support_pciaspm = 0
	 */
	rtl8822be_init_aspm_vars(hw);

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
	rtlpriv->rtlhal.pfirmware = vzalloc(0x40000);
	if (!rtlpriv->rtlhal.pfirmware) {
		/*pr_err("Can't alloc buffer for fw\n");*/
		return 1;
	}

	/* request fw */
	fw_name = "rtlwifi/rtl8822befw.bin";

	rtlpriv->max_fw_size = 0x40000;
	pr_info("Using firmware %s\n", fw_name);
	err = request_firmware_nowait(THIS_MODULE, 1, fw_name, rtlpriv->io.dev,
				      GFP_KERNEL, hw, rtl_fw_cb);
	if (err) {
		pr_err("Failed to request firmware!\n");
		return 1;
	}

	/* init table of tx power by rate & limit */
	rtl8822be_load_txpower_by_rate(hw);
	rtl8822be_load_txpower_limit(hw);

	return 0;
}

void rtl8822be_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->halmac.ops->halmac_deinit_adapter(rtlpriv);
	rtlpriv->phydm.ops->phydm_deinit_priv(rtlpriv);

	if (rtlpriv->rtlhal.pfirmware) {
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}
}

/* get bt coexist status */
bool rtl8822be_get_btc_status(void)
{
	return true;
}

static void rtl8822be_phydm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 tmp;

	tmp = rtl_read_dword(rtlpriv, 0xc00);
	if (tmp & 0xFF000000) { /* Recover 0xC00: 0xF800000C --> 0x0000000C */
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "found regaddr_c00=%08X\n", tmp);
		tmp &= ~0xFF000000;
		rtl_write_dword(rtlpriv, 0xc00, tmp);
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "apply regaddr_c00=%08X\n", tmp);
	}

	rtlpriv->phydm.ops->phydm_watchdog(rtlpriv);
}

static struct rtl_hal_ops rtl8822be_hal_ops = {
	.init_sw_vars = rtl8822be_init_sw_vars,
	.deinit_sw_vars = rtl8822be_deinit_sw_vars,
	.read_eeprom_info = rtl8822be_read_eeprom_info_dummy,
	.interrupt_recognized = rtl8822be_interrupt_recognized,
	.hw_init = rtl8822be_hw_init,
	.hw_disable = rtl8822be_card_disable,
	.hw_suspend = rtl8822be_suspend,
	.hw_resume = rtl8822be_resume,
	.enable_interrupt = rtl8822be_enable_interrupt,
	.disable_interrupt = rtl8822be_disable_interrupt,
	.set_network_type = rtl8822be_set_network_type,
	.set_chk_bssid = rtl8822be_set_check_bssid,
	.set_qos = rtl8822be_set_qos,
	.set_bcn_reg = rtl8822be_set_beacon_related_registers,
	.set_bcn_intv = rtl8822be_set_beacon_interval,
	.update_interrupt_mask = rtl8822be_update_interrupt_mask,
	.get_hw_reg = rtl8822be_get_hw_reg,
	.set_hw_reg = rtl8822be_set_hw_reg,
	.update_rate_tbl = rtl8822be_update_hal_rate_tbl,
	.pre_fill_tx_bd_desc = rtl8822be_pre_fill_tx_bd_desc,
	.rx_desc_buff_remained_cnt = rtl8822be_rx_desc_buff_remained_cnt,
	.rx_check_dma_ok = rtl8822be_rx_check_dma_ok,
	.fill_tx_desc = rtl8822be_tx_fill_desc,
	.fill_tx_special_desc = rtl8822be_tx_fill_special_desc,
	.query_rx_desc = rtl8822be_rx_query_desc,
	.radio_onoff_checking = rtl8822be_gpio_radio_on_off_checking,
	.switch_channel = rtl8822be_phy_sw_chnl,
	.set_channel_access = rtl8822be_update_channel_access_setting,
	.set_bw_mode = rtl8822be_phy_set_bw_mode,
	.dm_watchdog = rtl8822be_phydm_watchdog,
	.scan_operation_backup = rtl8822be_phy_scan_operation_backup,
	.set_rf_power_state = rtl8822be_phy_set_rf_power_state,
	.led_control = rtl8822be_led_control,
	.set_desc = rtl8822be_set_desc,
	.get_desc = rtl8822be_get_desc,
	.is_tx_desc_closed = rtl8822be_is_tx_desc_closed,
	.get_available_desc = rtl8822be_get_available_desc,
	.tx_polling = rtl8822be_tx_polling,
	.enable_hw_sec = rtl8822be_enable_hw_security_config,
	.set_key = rtl8822be_set_key,
	.init_sw_leds = rtl8822be_init_sw_leds,
	.get_bbreg = rtl8822be_phy_query_bb_reg,
	.set_bbreg = rtl8822be_phy_set_bb_reg,
	.get_rfreg = rtl8822be_phy_query_rf_reg,
	.set_rfreg = rtl8822be_phy_set_rf_reg,
	.fill_h2c_cmd = rtl8822be_fill_h2c_cmd,
	.set_default_port_id_cmd = rtl8822be_set_default_port_id_cmd,
	.get_btc_status = rtl8822be_get_btc_status,
	.rx_command_packet = rtl8822be_rx_command_packet,
	.c2h_content_parsing = rtl8822be_c2h_content_parsing,
	/* ops for halmac cb */
	.halmac_cb_init_mac_register = rtl8822be_halmac_cb_init_mac_register,
	.halmac_cb_init_bb_rf_register =
		rtl8822be_halmac_cb_init_bb_rf_register,
	.halmac_cb_write_data_rsvd_page =
		rtl8822b_halmac_cb_write_data_rsvd_page,
	.halmac_cb_write_data_h2c = rtl8822b_halmac_cb_write_data_h2c,
	/* ops for phydm cb */
	.get_txpower_index = rtl8822be_get_txpower_index,
	.set_tx_power_index_by_rs = rtl8822be_phy_set_tx_power_index_by_rs,
	.store_tx_power_by_rate = rtl8822be_store_tx_power_by_rate,
	.phy_set_txpower_limit = rtl8822be_phy_set_txpower_limit,
};

static struct rtl_mod_params rtl8822be_mod_params = {
	.sw_crypto = false,
	.inactiveps = true,
	.swctrl_lps = false,
	.fwctrl_lps = true,
	.msi_support = true,
	.dma64 = false,
	.aspm_support = 1,
	.disable_watchdog = false,
	.debug_level = 0,
	.debug_mask = 0,
};

static struct rtl_hal_cfg rtl8822be_hal_cfg = {
	.bar_id = 2,
	.write_readback = false,
	.name = "rtl8822be_pci",
	.ops = &rtl8822be_hal_ops,
	.mod_params = &rtl8822be_mod_params,
	.spec_ver = RTL_SPEC_NEW_RATEID | RTL_SPEC_SUPPORT_VHT |
		    RTL_SPEC_NEW_FW_C2H,
	.maps[SYS_ISO_CTRL] = REG_SYS_ISO_CTRL_8822B,
	.maps[SYS_FUNC_EN] = REG_SYS_FUNC_EN_8822B,
	.maps[SYS_CLK] = REG_SYS_CLK_CTRL_8822B,
	.maps[MAC_RCR_AM] = AM,
	.maps[MAC_RCR_AB] = AB,
	.maps[MAC_RCR_ACRC32] = ACRC32,
	.maps[MAC_RCR_ACF] = ACF,
	.maps[MAC_RCR_AAP] = AAP,
	.maps[MAC_HIMR] = REG_HIMR0_8822B,
	.maps[MAC_HIMRE] = REG_HIMR1_8822B,

	.maps[EFUSE_ACCESS] = REG_EFUSE_ACCESS_8822B,

	.maps[EFUSE_TEST] = REG_LDO_EFUSE_CTRL_8822B,
	.maps[EFUSE_CTRL] = REG_EFUSE_CTRL_8822B,
	.maps[EFUSE_CLK] = 0,
	.maps[EFUSE_CLK_CTRL] = REG_EFUSE_CTRL_8822B,
	.maps[EFUSE_PWC_EV12V] = PWC_EV12V,
	.maps[EFUSE_FEN_ELDR] = FEN_ELDR,
	.maps[EFUSE_LOADER_CLK_EN] = LOADER_CLK_EN,
	.maps[EFUSE_ANA8M] = ANA8M,
	.maps[EFUSE_HWSET_MAX_SIZE] = HWSET_MAX_SIZE,
	.maps[EFUSE_MAX_SECTION_MAP] = EFUSE_MAX_SECTION,
	.maps[EFUSE_REAL_CONTENT_SIZE] = EFUSE_REAL_CONTENT_LEN,
	.maps[EFUSE_OOB_PROTECT_BYTES_LEN] = EFUSE_OOB_PROTECT_BYTES,

	.maps[RWCAM] = REG_CAMCMD_8822B,
	.maps[WCAMI] = REG_CAMWRITE_8822B,
	.maps[RCAMO] = REG_CAMREAD_8822B,
	.maps[CAMDBG] = REG_CAMDBG_8822B,
	.maps[SECR] = REG_SECCFG_8822B,
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
	/*	.maps[RTL_IMR_BCNDOK8] = IMR_BCNDOK8,     */ /*need check*/
	.maps[RTL_IMR_BCNDOK7] = IMR_BCNDOK7,
	.maps[RTL_IMR_BCNDOK6] = IMR_BCNDOK6,
	.maps[RTL_IMR_BCNDOK5] = IMR_BCNDOK5,
	.maps[RTL_IMR_BCNDOK4] = IMR_BCNDOK4,
	.maps[RTL_IMR_BCNDOK3] = IMR_BCNDOK3,
	.maps[RTL_IMR_BCNDOK2] = IMR_BCNDOK2,
	.maps[RTL_IMR_BCNDOK1] = IMR_BCNDOK1,
	/*	.maps[RTL_IMR_TIMEOUT2] = IMR_TIMEOUT2,*/
	/*	.maps[RTL_IMR_TIMEOUT1] = IMR_TIMEOUT1,*/

	.maps[RTL_IMR_TXFOVW] = IMR_TXFOVW,
	.maps[RTL_IMR_PSTIMEOUT] = IMR_PSTIMEOUT,
	.maps[RTL_IMR_BCNINT] = IMR_BCNDMAINT0,
	.maps[RTL_IMR_RXFOVW] = IMR_RXFOVW,
	.maps[RTL_IMR_RDU] = IMR_RDU,
	.maps[RTL_IMR_ATIMEND] = IMR_ATIMEND,
	.maps[RTL_IMR_H2CDOK] = IMR_H2CDOK,
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

	/*VHT hightest rate*/
	.maps[RTL_RC_VHT_RATE_1SS_MCS7] = DESC_RATEVHT1SS_MCS7,
	.maps[RTL_RC_VHT_RATE_1SS_MCS8] = DESC_RATEVHT1SS_MCS8,
	.maps[RTL_RC_VHT_RATE_1SS_MCS9] = DESC_RATEVHT1SS_MCS9,
	.maps[RTL_RC_VHT_RATE_2SS_MCS7] = DESC_RATEVHT2SS_MCS7,
	.maps[RTL_RC_VHT_RATE_2SS_MCS8] = DESC_RATEVHT2SS_MCS8,
	.maps[RTL_RC_VHT_RATE_2SS_MCS9] = DESC_RATEVHT2SS_MCS9,
};

static const struct pci_device_id rtl8822be_pci_ids[] = {
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xB822, rtl8822be_hal_cfg)},
	{},
};

MODULE_DEVICE_TABLE(pci, rtl8822be_pci_ids);

MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.Finger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8822BE 802.11n PCI wireless");
MODULE_FIRMWARE("rtlwifi/rtl8822befw.bin");

module_param_named(swenc, rtl8822be_mod_params.sw_crypto, bool, 0444);
module_param_named(debug_level, rtl8822be_mod_params.debug_level, int, 0644);
module_param_named(debug_mask, rtl8822be_mod_params.debug_mask, ullong, 0644);
module_param_named(ips, rtl8822be_mod_params.inactiveps, bool, 0444);
module_param_named(swlps, rtl8822be_mod_params.swctrl_lps, bool, 0444);
module_param_named(fwlps, rtl8822be_mod_params.fwctrl_lps, bool, 0444);
module_param_named(msi, rtl8822be_mod_params.msi_support, bool, 0444);
module_param_named(dma64, rtl8822be_mod_params.dma64, bool, 0444);
module_param_named(aspm, rtl8822be_mod_params.aspm_support, int, 0444);
module_param_named(disable_watchdog, rtl8822be_mod_params.disable_watchdog,
		   bool, 0444);
MODULE_PARM_DESC(swenc, "Set to 1 for software crypto (default 0)\n");
MODULE_PARM_DESC(ips, "Set to 0 to not use link power save (default 1)\n");
MODULE_PARM_DESC(swlps, "Set to 1 to use SW control power save (default 0)\n");
MODULE_PARM_DESC(fwlps, "Set to 1 to use FW control power save (default 1)\n");
MODULE_PARM_DESC(msi, "Set to 1 to use MSI interrupts mode (default 1)\n");
MODULE_PARM_DESC(dma64, "Set to 1 to use DMA 64 (default 0)\n");
MODULE_PARM_DESC(aspm, "Set to 1 to enable ASPM (default 1)\n");
MODULE_PARM_DESC(debug, "Set debug level (0-5) (default 0)");
MODULE_PARM_DESC(debug_mask, "Set debug mask (default 0)");
MODULE_PARM_DESC(disable_watchdog,
		 "Set to 1 to disable the watchdog (default 0)\n");

static SIMPLE_DEV_PM_OPS(rtlwifi_pm_ops, rtl_pci_suspend, rtl_pci_resume);

static struct pci_driver rtl8822be_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl8822be_pci_ids,
	.probe = rtl_pci_probe,
	.remove = rtl_pci_disconnect,
	.driver.pm = &rtlwifi_pm_ops,
};

module_pci_driver(rtl8822be_driver);
