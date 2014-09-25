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

#include <linux/vmalloc.h>
#include <linux/module.h>

#include "../wifi.h"
#include "../core.h"
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "hw.h"
#include "sw.h"
#include "trx.h"
#include "led.h"
#include "table.h"
#include "hal_btc.h"
#include "../btcoexist/rtl_btc.h"

static void rtl8821ae_init_aspm_vars(struct ieee80211_hw *hw)
{
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
	 * set default to RTL8192CE:3 RTL8192E:2
	 * */
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
	 * set default to RTL8192CE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 0;

	/*
	 * This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = 1;
}

/*InitializeVariables8812E*/
int rtl8821ae_init_sw_vars(struct ieee80211_hw *hw)
{
	int err = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	const struct firmware *firmware;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	char *fw_name = NULL;

	rtl8821ae_bt_reg_init(hw);
	rtlpriv->btcoexist.btc_ops = rtl_btc_get_ops_pointer();

	rtlpriv->dm.b_dm_initialgain_enable = 1;
	rtlpriv->dm.dm_flag = 0;
	rtlpriv->dm.b_disable_framebursting = 0;;
	rtlpriv->dm.thermalvalue = 0;
	rtlpci->transmit_config = CFENDFORM | BIT(15) | BIT(24) | BIT(25);

	mac->ht_enable = true;

	rtlpriv->rtlhal.current_bandtype = BAND_ON_2_4G;
	/*following 2 is for register 5G band, refer to _rtl_init_mac80211()*/
	rtlpriv->rtlhal.bandset = BAND_ON_BOTH;
	rtlpriv->rtlhal.macphymode = SINGLEMAC_SINGLEPHY;

	rtlpci->receive_config = (RCR_APPFCS			|
							  RCR_APP_MIC 			|
							  RCR_APP_ICV 			|
							  RCR_APP_PHYST_RXFF	|
							  RCR_NONQOS_VHT		|
							  RCR_HTC_LOC_CTRL		|
							  RCR_AMF 				|
							  RCR_ACF 				|
							  RCR_ADF 				|	/*This bit controls the PS-Poll packet filter.*/
							  RCR_AICV				|
							  RCR_ACRC32			|
							  RCR_AB				|
							  RCR_AM				|
							  RCR_APM 				|
							  0);


	rtlpci->irq_mask[0] =
	     (u32) (IMR_PSTIMEOUT			|
				IMR_GTINT3 				|
				/*IMR_TBDER				|
				IMR_TBDOK				|
				IMR_BCNDMAINT0 			|*/
				IMR_HSISR_IND_ON_INT	|
				IMR_C2HCMD 				|
				IMR_HIGHDOK				|
				IMR_MGNTDOK				|
				IMR_BKDOK				|
				IMR_BEDOK				|
				IMR_VIDOK				|
				IMR_VODOK				|
				IMR_RDU					|
				IMR_ROK					|
				0);

	rtlpci->irq_mask[1]	=
		 (u32)(	IMR_RXFOVW |
		 		IMR_TXFOVW |
				0);

	/* for LPS & IPS */
	rtlpriv->psc.b_inactiveps = rtlpriv->cfg->mod_params->b_inactiveps;
	rtlpriv->psc.b_swctrl_lps = rtlpriv->cfg->mod_params->b_swctrl_lps;
	rtlpriv->psc.b_fwctrl_lps = rtlpriv->cfg->mod_params->b_fwctrl_lps;
	rtlpriv->psc.b_reg_fwctrl_lps = 3;
	rtlpriv->psc.reg_max_lps_awakeintvl = 5;
	/* for ASPM, you can close aspm through
	 * set const_support_pciaspm = 0 */
	rtl8821ae_init_aspm_vars(hw);

	if (rtlpriv->psc.b_reg_fwctrl_lps == 1)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MIN_MODE;
	else if (rtlpriv->psc.b_reg_fwctrl_lps == 2)
		rtlpriv->psc.fwctrl_psmode = FW_PS_MAX_MODE;
	else if (rtlpriv->psc.b_reg_fwctrl_lps == 3)
		rtlpriv->psc.fwctrl_psmode = FW_PS_DTIM_MODE;

	/* for firmware buf */
	rtlpriv->rtlhal.pfirmware = (u8 *) vmalloc(0x8000);
	if (!rtlpriv->rtlhal.pfirmware) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Can't alloc buffer for fw.\n"));
		return 1;
	}

	fw_name = "rtlwifi/rtl8821aefw.bin";
	err = request_firmware(&firmware, fw_name, rtlpriv->io.dev);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Failed to request firmware!\n"));
		return 1;
	}

	if (firmware->size > 0x8000) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Firmware is too big!\n"));
		release_firmware(firmware);
		return 1;
	}

	memcpy(rtlpriv->rtlhal.pfirmware, firmware->data, firmware->size);
	rtlpriv->rtlhal.fwsize = firmware->size;
	release_firmware(firmware);

	if (rtlpriv->cfg->ops->get_btc_status()){
		rtlpriv->btcoexist.btc_ops->btc_init_variables(rtlpriv);
		rtlpriv->btcoexist.btc_ops->btc_init_hal_vars(rtlpriv);
	}

	RT_TRACE(COMP_INIT, DBG_LOUD, (" FirmwareDownload OK\n"));
	return err;
}

void rtl8821ae_deinit_sw_vars(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	//printk("=========>rtl8821ae_deinit_sw_vars().\n");
	if (rtlpriv->cfg->ops->get_btc_status()){
		//printk("=========>rtl8821ae_deinit_sw_vars().get_btc_status\n");
		rtlpriv->btcoexist.btc_ops->btc_halt_notify();
	}
	if (rtlpriv->rtlhal.pfirmware) {
		//printk("=========>rtl8821ae_deinit_sw_vars().rtlpriv->rtlhal.pfirmware\n");
		vfree(rtlpriv->rtlhal.pfirmware);
		rtlpriv->rtlhal.pfirmware = NULL;
	}
	//printk("<=========rtl8821ae_deinit_sw_vars().\n");
}

static u32 rtl8812ae_rx_command_packet_handler(
	struct ieee80211_hw *hw,
	const struct rtl_stats *status,
	struct sk_buff *skb
	)
{
	u32 result = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (status->packet_report_type) {
		case NORMAL_RX:
			result = 0;
			break;
		case C2H_PACKET:
			rtl8812ae_c2h_packet_handler(hw, skb->data, (u8) skb->len);
			result = 1;
			RT_TRACE(COMP_RECV, DBG_LOUD,
				("===>rtl8821ae_rx_command_packet_handler(): (u8) skb->len=%d\n\n", skb->len));
			break;
		default:
			RT_TRACE(COMP_RECV, DBG_LOUD,
				("===>rtl8821ae_rx_command_packet_handler(): No this packet type!!\n"));
			break;
	}

	return result;
}


/* get bt coexist status */
bool rtl8821ae_get_btc_status(void)
{
	return true;
}

static struct rtl_hal_ops rtl8821ae_hal_ops = {
	.init_sw_vars = rtl8821ae_init_sw_vars,
	.deinit_sw_vars = rtl8821ae_deinit_sw_vars,
	.read_eeprom_info = rtl8821ae_read_eeprom_info,
	.interrupt_recognized = rtl8821ae_interrupt_recognized,
	.hw_init = rtl8821ae_hw_init,
	.hw_disable = rtl8821ae_card_disable,
	.hw_suspend = rtl8821ae_suspend,
	.hw_resume = rtl8821ae_resume,
	.enable_interrupt = rtl8821ae_enable_interrupt,
	.disable_interrupt = rtl8821ae_disable_interrupt,
	.set_network_type = rtl8821ae_set_network_type,
	.set_chk_bssid = rtl8821ae_set_check_bssid,
	.set_qos = rtl8821ae_set_qos,
	.set_bcn_reg = rtl8821ae_set_beacon_related_registers,
	.set_bcn_intv = rtl8821ae_set_beacon_interval,
	.update_interrupt_mask = rtl8821ae_update_interrupt_mask,
	.get_hw_reg = rtl8821ae_get_hw_reg,
	.set_hw_reg = rtl8821ae_set_hw_reg,
	.update_rate_tbl = rtl8821ae_update_hal_rate_tbl,
	.fill_tx_desc = rtl8821ae_tx_fill_desc,
	.fill_tx_cmddesc = rtl8821ae_tx_fill_cmddesc,
	.query_rx_desc = rtl8821ae_rx_query_desc,
	.set_channel_access = rtl8821ae_update_channel_access_setting,
	.radio_onoff_checking = rtl8821ae_gpio_radio_on_off_checking,
	.set_bw_mode = rtl8821ae_phy_set_bw_mode,
	.switch_channel = rtl8821ae_phy_sw_chnl,
	.dm_watchdog = rtl8821ae_dm_watchdog,
	.scan_operation_backup = rtl8821ae_phy_scan_operation_backup,
	.set_rf_power_state = rtl8821ae_phy_set_rf_power_state,
	.led_control = rtl8821ae_led_control,
	.set_desc = rtl8821ae_set_desc,
	.get_desc = rtl8821ae_get_desc,
	.is_tx_desc_closed = rtl8821ae_is_tx_desc_closed,
	.tx_polling = rtl8821ae_tx_polling,
	.enable_hw_sec = rtl8821ae_enable_hw_security_config,
	.set_key = rtl8821ae_set_key,
	.init_sw_leds = rtl8821ae_init_sw_leds,
	.allow_all_destaddr = rtl8821ae_allow_all_destaddr,
	.get_bbreg = rtl8821ae_phy_query_bb_reg,
	.set_bbreg = rtl8821ae_phy_set_bb_reg,
	.get_rfreg = rtl8821ae_phy_query_rf_reg,
	.set_rfreg = rtl8821ae_phy_set_rf_reg,
	.c2h_command_handle = rtl_8821ae_c2h_command_handle,
	.bt_wifi_media_status_notify = rtl_8821ae_bt_wifi_media_status_notify,
	.bt_turn_off_bt_coexist_before_enter_lps = rtl8821ae_dm_bt_turn_off_bt_coexist_before_enter_lps,
	.fill_h2c_cmd = rtl8821ae_fill_h2c_cmd,
	.get_btc_status = rtl8821ae_get_btc_status,
	.rx_command_packet_handler = rtl8812ae_rx_command_packet_handler,
};

static struct rtl_mod_params rtl8821ae_mod_params = {
	.sw_crypto = false,
	.b_inactiveps = false,//true,
	.b_swctrl_lps = false,
	.b_fwctrl_lps = false, //true,
};

static struct rtl_hal_cfg rtl8821ae_hal_cfg = {
	.bar_id = 2,
	.write_readback = true,
	.name = "rtl8821ae_pci",
	.fw_name = "rtlwifi/rtl8821aefw.bin",
	.ops = &rtl8821ae_hal_ops,
	.mod_params = &rtl8821ae_mod_params,
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
	.maps[RTL_IMR_BcnInt] = IMR_BCNDMAINT0,
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

	.maps[RTL_RC_CCK_RATE1M] = DESC_RATE1M,
	.maps[RTL_RC_CCK_RATE2M] =  DESC_RATE2M,
	.maps[RTL_RC_CCK_RATE5_5M] =  DESC_RATE5_5M,
	.maps[RTL_RC_CCK_RATE11M] =  DESC_RATE11M,
	.maps[RTL_RC_OFDM_RATE6M] =  DESC_RATE6M,
	.maps[RTL_RC_OFDM_RATE9M] =  DESC_RATE9M,
	.maps[RTL_RC_OFDM_RATE12M] =  DESC_RATE12M,
	.maps[RTL_RC_OFDM_RATE18M] =  DESC_RATE18M,
	.maps[RTL_RC_OFDM_RATE24M] =  DESC_RATE24M,
	.maps[RTL_RC_OFDM_RATE36M] =  DESC_RATE36M,
	.maps[RTL_RC_OFDM_RATE48M] =  DESC_RATE48M,
	.maps[RTL_RC_OFDM_RATE54M] =  DESC_RATE54M,

	.maps[RTL_RC_HT_RATEMCS7] =  DESC_RATEMCS7,
	.maps[RTL_RC_HT_RATEMCS15] =  DESC_RATEMCS15,
};

static struct pci_device_id rtl8821ae_pci_ids[] = {
        {RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8812, rtl8821ae_hal_cfg)},
        {RTL_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8821, rtl8821ae_hal_cfg)},
        {},
};

MODULE_DEVICE_TABLE(pci, rtl8821ae_pci_ids);

MODULE_AUTHOR("Ping Yan<ping_yan@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 8821ae 802.11ac PCI wireless");
MODULE_FIRMWARE("rtlwifi/rtl8821aefw.bin");

module_param_named(swenc, rtl8821ae_mod_params.sw_crypto, bool, 0444);
module_param_named(ips, rtl8821ae_mod_params.b_inactiveps, bool, 0444);
module_param_named(swlps, rtl8821ae_mod_params.b_swctrl_lps, bool, 0444);
module_param_named(fwlps, rtl8821ae_mod_params.b_fwctrl_lps, bool, 0444);
MODULE_PARM_DESC(swenc, "using hardware crypto (default 0 [hardware])\n");
MODULE_PARM_DESC(ips, "using no link power save (default 1 is open)\n");
MODULE_PARM_DESC(fwlps, "using linked fw control power save (default 1 is open)\n");

static SIMPLE_DEV_PM_OPS(rtlwifi_pm_ops, rtl_pci_suspend, rtl_pci_resume);

static struct pci_driver rtl8821ae_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl8821ae_pci_ids,
	.probe = rtl_pci_probe,
	.remove = rtl_pci_disconnect,

	.driver.pm = &rtlwifi_pm_ops,
};


extern int rtl_core_module_init(void);
extern void rtl_core_module_exit(void);

static int __init rtl8821ae_module_init(void)
{
	int ret;

	ret = rtl_core_module_init();
	if (ret)
		return ret;

	//printk("==========>rtl8821ae_module_init().\n");
	ret = pci_register_driver(&rtl8821ae_driver);
	if (ret)
		RT_ASSERT(false, (": No device found\n"));

	return ret;
}

static void __exit rtl8821ae_module_exit(void)
{
	pci_unregister_driver(&rtl8821ae_driver);
	rtl_core_module_exit();
}

module_init(rtl8821ae_module_init);
module_exit(rtl8821ae_module_exit);
