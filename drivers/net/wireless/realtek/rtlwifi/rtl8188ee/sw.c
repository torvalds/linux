// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#include "../wifi.h"
#include "../core.h"
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "hw.h"
#include "trx.h"
#include "led.h"
#include "table.h"

#include <linux/vmalloc.h>
#include <linux/module.h>

static void rtl88e_init_aspm_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/* ASPM PS mode.
	 * 0 - Disable ASPM,
	 * 1 - Enable ASPM without Clock Req,
	 * 2 - Enable ASPM with Clock Req,
	 * 3 - Alwyas Enable ASPM with Clock Req,
	 * 4 - Always Enable ASPM without Clock Req.
	 * set default to RTL8192CE:3 RTL8192E:2
	 */
	rtlpci->const_pci_aspm = 3;

	/*Setting for PCI-E device */
	rtlpci->const_devicepci_aspm_setting = 0x03;

	/*Setting for PCI-E bridge */
	rtlpci->const_hostpci_aspm_setting = 0x02;

	/* In Hw/Sw Radio Off situation.
	 * 0 - Default,
	 * 1 - From ASPM setting without low Mac Pwr,
	 * 2 - From ASPM setting with low Mac Pwr,
	 * 3 - Bus D3
	 * set default to RTL8192CE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 0;

	/* This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = rtlpriv->cfg->mod_params->aspm_support;
}

static int rtl88e_init_sw_vars(struct ieee80211_hw *hw)
{
	int err = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 tid;
	char *fw_name;

	rtl8188ee_bt_reg_init(hw);
	rtlpriv->dm.dm_initialgain_enable = true;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = false;
	rtlpriv->dm.thermalvalue = 0;
	rtlpci->transmit_config = CFENDFORM | BIT(15);

	/* compatible 5G band 88ce just 2.4G band & smsp */
	rtlpriv->rtlhal.current_bandtype = BAND_ON_2_4G;
	rtlpriv->rtlhal.bandset = BAND_ON_2_4G;
	rtlpriv->rtlhal.macphymode = SINGLEMAC_SINGLEPHY;

	rtlpci->receive_config = (RCR_APPFCS |
				  RCR_APP_MIC |
				  RCR_APP_ICV |
				  RCR_APP_PHYST_RXFF |
				  RCR_HTC_LOC_CTRL |
				  RCR_AMF |
				  RCR_ACF |
				  RCR_ADF |
				  RCR_AICV |
				  RCR_ACRC32 |
				  RCR_AB |
				  RCR_AM |
				  RCR_APM |
				  0);

	rtlpci->irq_mask[0] =
				(u32)(IMR_PSTIMEOUT	|
				IMR_HSISR_IND_ON_INT	|
				IMR_C2HCMD		|
				IMR_HIGHDOK		|
				IMR_MGNTDOK		|
				IMR_BKDOK		|
				IMR_BEDOK		|
				IMR_VIDOK		|
				IMR_VODOK		|
				IMR_RDU			|
				IMR_ROK			|
				0);
	rtlpci->irq_mask[1] = (u32) (IMR_RXFOVW | 0);
	rtlpci->sys_irq_mask = (u32) (HSIMR_PDN_INT_EN | HSIMR_RON_INT_EN);

	/* for LPS & IPS */
	rtlpriv->psc.inactiveps = rtlpriv->cfg->mod_params->inactiveps;
	rtlpriv->psc.swctrl_lps = rtlpriv->cfg->mod_params->swctrl_lps;
	rtlpriv->psc.fwctrl_lps = rtlpriv->cfg->mod_params->fwctrl_lps;
	rtlpci->msi_support = rtlpriv->cfg->mod_params->msi_support;
	if (rtlpriv->cfg->mod_params->disable_watchdog)
		pr_info("watchdog disabled\n");
	if (!rtlpriv->psc.inactiveps)
		pr_info("rtl8188ee: Power Save off (module option)\n");
	if (!rtlpriv->psc.fwctrl_lps)
		pr_info("rtl8188ee: FW Power Save off (module option)\n");
	rtlpriv->psc.reg_fwctrl_lps = 3;
	rtlpriv->psc.reg_max_lps_awakeintvl = 5;
	/* for ASPM, you can close aspm through
	 * set const_support_pciaspm = 0
	 */
	rtl88e_init_aspm_vars(hw);

	if (rtlpriv->psc.reg_fwctrl_lps == 1)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MIN_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 2)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MAX_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 3)
		rtlpriv->psc.fwctrl_psmode = FW_PS_DTIM_MODE;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = vzalloc(0x8000);
	if (!rtlpriv->rtlhal.pfirmware) {
		pr_info("Can't alloc buffer for fw.\n");
		return 1;
	}

	fw_name = "rtlwifi/rtl8188efw.bin";
	rtlpriv->max_fw_size = 0x8000;
	pr_info("Using firmware %s\n", fw_name);
	err = request_firmware_nowait(THIS_MODULE, 1, fw_name,
				      rtlpriv->io.dev, GFP_KERNEL, hw,
				      rtl_fw_cb);
	if (err) {
		pr_info("Failed to request firmware!\n");
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
		return 1;
	}

	/* for early mode */
	rtlpriv->rtlhal.earlymode_enable = false;
	rtlpriv->rtlhal.max_earlymode_num = 10;
	for (tid = 0; tid < 8; tid++)
		skb_queue_head_init(&rtlpriv->mac80211.skb_waitq[tid]);

	/*low power */
	rtlpriv->psc.low_power_enable = false;
	if (rtlpriv->psc.low_power_enable) {
		timer_setup(&rtlpriv->works.fw_clockoff_timer,
			    rtl88ee_fw_clk_off_timer_callback, 0);
	}

	timer_setup(&rtlpriv->works.fast_antenna_training_timer,
		    rtl88e_dm_fast_antenna_training_callback, 0);
	return err;
}

static void rtl88e_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.pfirmware) {
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}

	if (rtlpriv->psc.low_power_enable)
		timer_delete_sync(&rtlpriv->works.fw_clockoff_timer);

	timer_delete_sync(&rtlpriv->works.fast_antenna_training_timer);
}

/* get bt coexist status */
static bool rtl88e_get_btc_status(void)
{
	return false;
}

static struct rtl_hal_ops rtl8188ee_hal_ops = {
	.init_sw_vars = rtl88e_init_sw_vars,
	.deinit_sw_vars = rtl88e_deinit_sw_vars,
	.read_eeprom_info = rtl88ee_read_eeprom_info,
	.interrupt_recognized = rtl88ee_interrupt_recognized,/*need check*/
	.hw_init = rtl88ee_hw_init,
	.hw_disable = rtl88ee_card_disable,
	.hw_suspend = rtl88ee_suspend,
	.hw_resume = rtl88ee_resume,
	.enable_interrupt = rtl88ee_enable_interrupt,
	.disable_interrupt = rtl88ee_disable_interrupt,
	.set_network_type = rtl88ee_set_network_type,
	.set_chk_bssid = rtl88ee_set_check_bssid,
	.set_qos = rtl88ee_set_qos,
	.set_bcn_reg = rtl88ee_set_beacon_related_registers,
	.set_bcn_intv = rtl88ee_set_beacon_interval,
	.update_interrupt_mask = rtl88ee_update_interrupt_mask,
	.get_hw_reg = rtl88ee_get_hw_reg,
	.set_hw_reg = rtl88ee_set_hw_reg,
	.update_rate_tbl = rtl88ee_update_hal_rate_tbl,
	.fill_tx_desc = rtl88ee_tx_fill_desc,
	.fill_tx_cmddesc = rtl88ee_tx_fill_cmddesc,
	.query_rx_desc = rtl88ee_rx_query_desc,
	.set_channel_access = rtl88ee_update_channel_access_setting,
	.radio_onoff_checking = rtl88ee_gpio_radio_on_off_checking,
	.set_bw_mode = rtl88e_phy_set_bw_mode,
	.switch_channel = rtl88e_phy_sw_chnl,
	.dm_watchdog = rtl88e_dm_watchdog,
	.scan_operation_backup = rtl88e_phy_scan_operation_backup,
	.set_rf_power_state = rtl88e_phy_set_rf_power_state,
	.led_control = rtl88ee_led_control,
	.set_desc = rtl88ee_set_desc,
	.get_desc = rtl88ee_get_desc,
	.is_tx_desc_closed = rtl88ee_is_tx_desc_closed,
	.tx_polling = rtl88ee_tx_polling,
	.enable_hw_sec = rtl88ee_enable_hw_security_config,
	.set_key = rtl88ee_set_key,
	.get_bbreg = rtl88e_phy_query_bb_reg,
	.set_bbreg = rtl88e_phy_set_bb_reg,
	.get_rfreg = rtl88e_phy_query_rf_reg,
	.set_rfreg = rtl88e_phy_set_rf_reg,
	.get_btc_status = rtl88e_get_btc_status,
};

static struct rtl_mod_params rtl88ee_mod_params = {
	.sw_crypto = false,
	.inactiveps = true,
	.swctrl_lps = false,
	.fwctrl_lps = false,
	.msi_support = true,
	.aspm_support = 1,
	.debug_level = 0,
	.debug_mask = 0,
};

static const struct rtl_hal_cfg rtl88ee_hal_cfg = {
	.bar_id = 2,
	.write_readback = true,
	.name = "rtl88e_pci",
	.ops = &rtl8188ee_hal_ops,
	.mod_params = &rtl88ee_mod_params,

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
	.maps[MAC_HSISR] = REG_HSISR,

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
/*	.maps[RTL_IMR_BCNDOK8] = IMR_BCNDOK8,     */   /*need check*/
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
	.maps[RTL_IMR_HSISR_IND] = IMR_HSISR_IND_ON_INT,
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

static const struct pci_device_id rtl88ee_pci_ids[] = {
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8179, rtl88ee_hal_cfg)},
	{},
};

MODULE_DEVICE_TABLE(pci, rtl88ee_pci_ids);

MODULE_AUTHOR("zhiyuan_yang	<zhiyuan_yang@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.Finger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8188E 802.11n PCI wireless");
MODULE_FIRMWARE("rtlwifi/rtl8188efw.bin");

module_param_named(swenc, rtl88ee_mod_params.sw_crypto, bool, 0444);
module_param_named(debug_level, rtl88ee_mod_params.debug_level, int, 0644);
module_param_named(debug_mask, rtl88ee_mod_params.debug_mask, ullong, 0644);
module_param_named(ips, rtl88ee_mod_params.inactiveps, bool, 0444);
module_param_named(swlps, rtl88ee_mod_params.swctrl_lps, bool, 0444);
module_param_named(fwlps, rtl88ee_mod_params.fwctrl_lps, bool, 0444);
module_param_named(msi, rtl88ee_mod_params.msi_support, bool, 0444);
module_param_named(aspm, rtl88ee_mod_params.aspm_support, int, 0444);
module_param_named(disable_watchdog, rtl88ee_mod_params.disable_watchdog,
		   bool, 0444);
MODULE_PARM_DESC(swenc, "Set to 1 for software crypto (default 0)\n");
MODULE_PARM_DESC(ips, "Set to 0 to not use link power save (default 1)\n");
MODULE_PARM_DESC(swlps, "Set to 1 to use SW control power save (default 0)\n");
MODULE_PARM_DESC(fwlps, "Set to 1 to use FW control power save (default 1)\n");
MODULE_PARM_DESC(msi, "Set to 1 to use MSI interrupts mode (default 1)\n");
MODULE_PARM_DESC(aspm, "Set to 1 to enable ASPM (default 1)\n");
MODULE_PARM_DESC(debug_level, "Set debug level (0-5) (default 0)");
MODULE_PARM_DESC(debug_mask, "Set debug mask (default 0)");
MODULE_PARM_DESC(disable_watchdog, "Set to 1 to disable the watchdog (default 0)\n");

static SIMPLE_DEV_PM_OPS(rtlwifi_pm_ops, rtl_pci_suspend, rtl_pci_resume);

static struct pci_driver rtl88ee_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl88ee_pci_ids,
	.probe = rtl_pci_probe,
	.remove = rtl_pci_disconnect,
	.driver.pm = &rtlwifi_pm_ops,
};

module_pci_driver(rtl88ee_driver);
