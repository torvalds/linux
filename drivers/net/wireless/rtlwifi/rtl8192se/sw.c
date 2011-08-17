/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/vmalloc.h>

#include "../wifi.h"
#include "../core.h"
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "hw.h"
#include "sw.h"
#include "trx.h"
#include "led.h"

static void rtl92s_init_aspm_vars(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/*close ASPM for AMD defaultly */
	rtlpci->const_amdpci_aspm = 0;

	/*
	 * ASPM PS mode.
	 * 0 - Disable ASPM,
	 * 1 - Enable ASPM without Clock Req,
	 * 2 - Enable ASPM with Clock Req,
	 * 3 - Alwyas Enable ASPM with Clock Req,
	 * 4 - Always Enable ASPM without Clock Req.
	 * set defult to RTL8192CE:3 RTL8192E:2
	 * */
	rtlpci->const_pci_aspm = 2;

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
	 * set default to RTL8192CE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 2;

	/*
	 * This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = 2;
}

static int rtl92s_init_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	const struct firmware *firmware;
	struct rt_firmware *pfirmware = NULL;
	int err = 0;
	u16 earlyrxthreshold = 7;

	rtlpriv->dm.dm_initialgain_enable = 1;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.disable_framebursting = 0;
	rtlpriv->dm.thermalvalue = 0;
	rtlpriv->dm.useramask = true;

	/* compatible 5G band 91se just 2.4G band & smsp */
	rtlpriv->rtlhal.current_bandtype = BAND_ON_2_4G;
	rtlpriv->rtlhal.bandset = BAND_ON_2_4G;
	rtlpriv->rtlhal.macphymode = SINGLEMAC_SINGLEPHY;

	rtlpci->transmit_config = 0;

	rtlpci->receive_config =
			RCR_APPFCS |
			RCR_APWRMGT |
			/*RCR_ADD3 |*/
			RCR_AMF	|
			RCR_ADF |
			RCR_APP_MIC |
			RCR_APP_ICV |
			RCR_AICV |
			/* Accept ICV error, CRC32 Error */
			RCR_ACRC32 |
			RCR_AB |
			/* Accept Broadcast, Multicast */
			RCR_AM	|
			/* Accept Physical match */
			RCR_APM |
			/* Accept Destination Address packets */
			/*RCR_AAP |*/
			RCR_APP_PHYST_STAFF |
			/* Accept PHY status */
			RCR_APP_PHYST_RXFF |
			(earlyrxthreshold << RCR_FIFO_OFFSET);

	rtlpci->irq_mask[0] = (u32)
			(IMR_ROK |
			IMR_VODOK |
			IMR_VIDOK |
			IMR_BEDOK |
			IMR_BKDOK |
			IMR_HCCADOK |
			IMR_MGNTDOK |
			IMR_COMDOK |
			IMR_HIGHDOK |
			IMR_BDOK |
			IMR_RXCMDOK |
			/*IMR_TIMEOUT0 |*/
			IMR_RDU |
			IMR_RXFOVW	|
			IMR_BCNINT
			/*| IMR_TXFOVW*/
			/*| IMR_TBDOK |
			IMR_TBDER*/);

	rtlpci->irq_mask[1] = (u32) 0;

	rtlpci->shortretry_limit = 0x30;
	rtlpci->longretry_limit = 0x30;

	rtlpci->first_init = true;

	/* for LPS & IPS */
	rtlpriv->psc.inactiveps = rtlpriv->cfg->mod_params->inactiveps;
	rtlpriv->psc.swctrl_lps = rtlpriv->cfg->mod_params->swctrl_lps;
	rtlpriv->psc.fwctrl_lps = rtlpriv->cfg->mod_params->fwctrl_lps;
	rtlpriv->psc.reg_fwctrl_lps = 3;
	rtlpriv->psc.reg_max_lps_awakeintvl = 5;
	/* for ASPM, you can close aspm through
	 * set const_support_pciaspm = 0 */
	rtl92s_init_aspm_vars(hw);

	if (rtlpriv->psc.reg_fwctrl_lps == 1)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MIN_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 2)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MAX_MODE;
	else if (rtlpriv->psc.reg_fwctrl_lps == 3)
		rtlpriv->psc.fwctrl_psmode = FW_PS_DTIM_MODE;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = vzalloc(sizeof(struct rt_firmware));
	if (!rtlpriv->rtlhal.pfirmware) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't alloc buffer for fw.\n"));
		return 1;
	}

	pr_info("Driver for Realtek RTL8192SE/RTL8191SE\n"
		"Loading firmware %s\n", rtlpriv->cfg->fw_name);
	/* request fw */
	err = request_firmware(&firmware, rtlpriv->cfg->fw_name,
			rtlpriv->io.dev);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Failed to request firmware!\n"));
		return 1;
	}
	if (firmware->size > sizeof(struct rt_firmware)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Firmware is too big!\n"));
		release_firmware(firmware);
		return 1;
	}

	pfirmware = (struct rt_firmware *)rtlpriv->rtlhal.pfirmware;
	memcpy(pfirmware->sz_fw_tmpbuffer, firmware->data, firmware->size);
	pfirmware->sz_fw_tmpbufferlen = firmware->size;
	release_firmware(firmware);

	return err;
}

static void rtl92s_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.pfirmware) {
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}
}

static struct rtl_hal_ops rtl8192se_hal_ops = {
	.init_sw_vars = rtl92s_init_sw_vars,
	.deinit_sw_vars = rtl92s_deinit_sw_vars,
	.read_eeprom_info = rtl92se_read_eeprom_info,
	.interrupt_recognized = rtl92se_interrupt_recognized,
	.hw_init = rtl92se_hw_init,
	.hw_disable = rtl92se_card_disable,
	.hw_suspend = rtl92se_suspend,
	.hw_resume = rtl92se_resume,
	.enable_interrupt = rtl92se_enable_interrupt,
	.disable_interrupt = rtl92se_disable_interrupt,
	.set_network_type = rtl92se_set_network_type,
	.set_chk_bssid = rtl92se_set_check_bssid,
	.set_qos = rtl92se_set_qos,
	.set_bcn_reg = rtl92se_set_beacon_related_registers,
	.set_bcn_intv = rtl92se_set_beacon_interval,
	.update_interrupt_mask = rtl92se_update_interrupt_mask,
	.get_hw_reg = rtl92se_get_hw_reg,
	.set_hw_reg = rtl92se_set_hw_reg,
	.update_rate_tbl = rtl92se_update_hal_rate_tbl,
	.fill_tx_desc = rtl92se_tx_fill_desc,
	.fill_tx_cmddesc = rtl92se_tx_fill_cmddesc,
	.query_rx_desc = rtl92se_rx_query_desc,
	.set_channel_access = rtl92se_update_channel_access_setting,
	.radio_onoff_checking = rtl92se_gpio_radio_on_off_checking,
	.set_bw_mode = rtl92s_phy_set_bw_mode,
	.switch_channel = rtl92s_phy_sw_chnl,
	.dm_watchdog = rtl92s_dm_watchdog,
	.scan_operation_backup = rtl92s_phy_scan_operation_backup,
	.set_rf_power_state = rtl92s_phy_set_rf_power_state,
	.led_control = rtl92se_led_control,
	.set_desc = rtl92se_set_desc,
	.get_desc = rtl92se_get_desc,
	.tx_polling = rtl92se_tx_polling,
	.enable_hw_sec = rtl92se_enable_hw_security_config,
	.set_key = rtl92se_set_key,
	.init_sw_leds = rtl92se_init_sw_leds,
	.get_bbreg = rtl92s_phy_query_bb_reg,
	.set_bbreg = rtl92s_phy_set_bb_reg,
	.get_rfreg = rtl92s_phy_query_rf_reg,
	.set_rfreg = rtl92s_phy_set_rf_reg,
};

static struct rtl_mod_params rtl92se_mod_params = {
	.sw_crypto = false,
	.inactiveps = true,
	.swctrl_lps = true,
	.fwctrl_lps = false,
};

/* Because memory R/W bursting will cause system hang/crash
 * for 92se, so we don't read back after every write action */
static struct rtl_hal_cfg rtl92se_hal_cfg = {
	.bar_id = 1,
	.write_readback = false,
	.name = "rtl92s_pci",
	.fw_name = "rtlwifi/rtl8192sefw.bin",
	.ops = &rtl8192se_hal_ops,
	.mod_params = &rtl92se_mod_params,

	.maps[SYS_ISO_CTRL] = REG_SYS_ISO_CTRL,
	.maps[SYS_FUNC_EN] = REG_SYS_FUNC_EN,
	.maps[SYS_CLK] = SYS_CLKR,
	.maps[MAC_RCR_AM] = RCR_AM,
	.maps[MAC_RCR_AB] = RCR_AB,
	.maps[MAC_RCR_ACRC32] = RCR_ACRC32,
	.maps[MAC_RCR_ACF] = RCR_ACF,
	.maps[MAC_RCR_AAP] = RCR_AAP,

	.maps[EFUSE_TEST] = REG_EFUSE_TEST,
	.maps[EFUSE_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_CLK] = REG_EFUSE_CLK,
	.maps[EFUSE_CLK_CTRL] = REG_EFUSE_CTRL,
	.maps[EFUSE_PWC_EV12V] = 0, /* nouse for 8192se */
	.maps[EFUSE_FEN_ELDR] = 0, /* nouse for 8192se */
	.maps[EFUSE_LOADER_CLK_EN] = 0,/* nouse for 8192se */
	.maps[EFUSE_ANA8M] = EFUSE_ANA8M,
	.maps[EFUSE_HWSET_MAX_SIZE] = HWSET_MAX_SIZE_92S,
	.maps[EFUSE_MAX_SECTION_MAP] = EFUSE_MAX_SECTION,
	.maps[EFUSE_REAL_CONTENT_SIZE] = EFUSE_REAL_CONTENT_LEN,

	.maps[RWCAM] = REG_RWCAM,
	.maps[WCAMI] = REG_WCAMI,
	.maps[RCAMO] = REG_RCAMO,
	.maps[CAMDBG] = REG_CAMDBG,
	.maps[SECR] = REG_SECR,
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
	.maps[RTL_IMR_COMDOK] = IMR_COMDOK,
	.maps[RTL_IMR_TBDOK] = IMR_TBDOK,
	.maps[RTL_IMR_BKDOK] = IMR_BKDOK,
	.maps[RTL_IMR_BEDOK] = IMR_BEDOK,
	.maps[RTL_IMR_VIDOK] = IMR_VIDOK,
	.maps[RTL_IMR_VODOK] = IMR_VODOK,
	.maps[RTL_IMR_ROK] = IMR_ROK,
	.maps[RTL_IBSS_INT_MASKS] = (IMR_BCNINT | IMR_TBDOK | IMR_TBDER),

	.maps[RTL_RC_CCK_RATE1M] = DESC92S_RATE1M,
	.maps[RTL_RC_CCK_RATE2M] = DESC92S_RATE2M,
	.maps[RTL_RC_CCK_RATE5_5M] = DESC92S_RATE5_5M,
	.maps[RTL_RC_CCK_RATE11M] = DESC92S_RATE11M,
	.maps[RTL_RC_OFDM_RATE6M] = DESC92S_RATE6M,
	.maps[RTL_RC_OFDM_RATE9M] = DESC92S_RATE9M,
	.maps[RTL_RC_OFDM_RATE12M] = DESC92S_RATE12M,
	.maps[RTL_RC_OFDM_RATE18M] = DESC92S_RATE18M,
	.maps[RTL_RC_OFDM_RATE24M] = DESC92S_RATE24M,
	.maps[RTL_RC_OFDM_RATE36M] = DESC92S_RATE36M,
	.maps[RTL_RC_OFDM_RATE48M] = DESC92S_RATE48M,
	.maps[RTL_RC_OFDM_RATE54M] = DESC92S_RATE54M,

	.maps[RTL_RC_HT_RATEMCS7] = DESC92S_RATEMCS7,
	.maps[RTL_RC_HT_RATEMCS15] = DESC92S_RATEMCS15,
};

static struct pci_device_id rtl92se_pci_ids[] __devinitdata = {
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8192, rtl92se_hal_cfg)},
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8171, rtl92se_hal_cfg)},
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8172, rtl92se_hal_cfg)},
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8173, rtl92se_hal_cfg)},
	{RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8174, rtl92se_hal_cfg)},
	{},
};

MODULE_DEVICE_TABLE(pci, rtl92se_pci_ids);

MODULE_AUTHOR("lizhaoming	<chaoming_li@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8192S/8191S 802.11n PCI wireless");
MODULE_FIRMWARE("rtlwifi/rtl8192sefw.bin");

module_param_named(swenc, rtl92se_mod_params.sw_crypto, bool, 0444);
module_param_named(ips, rtl92se_mod_params.inactiveps, bool, 0444);
module_param_named(swlps, rtl92se_mod_params.swctrl_lps, bool, 0444);
module_param_named(fwlps, rtl92se_mod_params.fwctrl_lps, bool, 0444);
MODULE_PARM_DESC(swenc, "using hardware crypto (default 0 [hardware])\n");
MODULE_PARM_DESC(ips, "using no link power save (default 1 is open)\n");
MODULE_PARM_DESC(swlps, "using linked sw control power save (default 1 is "
		 "open)\n");


static struct pci_driver rtl92se_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl92se_pci_ids,
	.probe = rtl_pci_probe,
	.remove = rtl_pci_disconnect,

#ifdef CONFIG_PM
	.suspend = rtl_pci_suspend,
	.resume = rtl_pci_resume,
#endif

};

static int __init rtl92se_module_init(void)
{
	int ret = 0;

	ret = pci_register_driver(&rtl92se_driver);
	if (ret)
		RT_ASSERT(false, (": No device found\n"));

	return ret;
}

static void __exit rtl92se_module_exit(void)
{
	pci_unregister_driver(&rtl92se_driver);
}

module_init(rtl92se_module_init);
module_exit(rtl92se_module_exit);
