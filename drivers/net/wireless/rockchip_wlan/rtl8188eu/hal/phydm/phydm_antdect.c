/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

/* ************************************************************
 * include files
 * ************************************************************ */

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_ANT_DETECTION

/* @IS_ANT_DETECT_SUPPORT_SINGLE_TONE(adapter)
 * IS_ANT_DETECT_SUPPORT_RSSI(adapter)
 * IS_ANT_DETECT_SUPPORT_PSD(adapter) */

/* @1 [1. Single Tone method] =================================================== */

/*@
 * Description:
 *	Set Single/Dual Antenna default setting for products that do not do detection in advance.
 *
 * Added by Joseph, 2012.03.22
 *   */
void odm_sw_ant_div_construct_scan_chnl(
	void *adapter,
	u8 scan_chnl)
{
}

u8 odm_sw_ant_div_select_scan_chnl(
	void *adapter)
{
	return 0;
}

void odm_single_dual_antenna_default_setting(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;
	void *adapter = dm->adapter;

	u8 bt_ant_num = BT_GetPgAntNum(adapter);
	/* Set default antenna A and B status */
	if (bt_ant_num == 2) {
		dm_swat_table->ANTA_ON = true;
		dm_swat_table->ANTB_ON = true;

	} else if (bt_ant_num == 1) {
		/* Set antenna A as default */
		dm_swat_table->ANTA_ON = true;
		dm_swat_table->ANTB_ON = false;

	} else
		RT_ASSERT(false, ("Incorrect antenna number!!\n"));
}

/* @2 8723A ANT DETECT
 *
 * Description:
 *	Implement IQK single tone for RF DPK loopback and BB PSD scanning.
 *	This function is cooperated with BB team Neil.
 *
 * Added by Roger, 2011.12.15
 *   */
boolean
odm_single_dual_antenna_detection(
	void *dm_void,
	u8 mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *adapter = dm->adapter;
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;
	u32 current_channel, rf_loop_reg;
	u8 n;
	u32 reg88c, regc08, reg874, regc50, reg948, regb2c, reg92c, reg930, reg064, afe_rrx_wait_cca;
	u8 initial_gain = 0x5a;
	u32 PSD_report_tmp;
	u32 ant_a_report = 0x0, ant_b_report = 0x0, ant_0_report = 0x0;
	boolean is_result = true;

	PHYDM_DBG(dm, DBG_ANT_DIV, "%s============>\n", __func__);

	if (!(dm->support_ic_type & ODM_RTL8723B))
		return is_result;

	/* Retrieve antenna detection registry info, added by Roger, 2012.11.27. */
	if (!IS_ANT_DETECT_SUPPORT_SINGLE_TONE(((PADAPTER)adapter)))
		return is_result;

	/* @1 Backup Current RF/BB Settings */

	current_channel = odm_get_rf_reg(dm, RF_PATH_A, ODM_CHANNEL, RFREGOFFSETMASK);
	rf_loop_reg = odm_get_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK);
	if (dm->support_ic_type & ODM_RTL8723B) {
		reg92c = odm_get_bb_reg(dm, REG_DPDT_CONTROL, MASKDWORD);
		reg930 = odm_get_bb_reg(dm, rfe_ctrl_anta_src, MASKDWORD);
		reg948 = odm_get_bb_reg(dm, REG_S0_S1_PATH_SWITCH, MASKDWORD);
		regb2c = odm_get_bb_reg(dm, REG_AGC_TABLE_SELECT, MASKDWORD);
		reg064 = odm_get_mac_reg(dm, REG_SYM_WLBT_PAPE_SEL, BIT(29));
		odm_set_bb_reg(dm, REG_DPDT_CONTROL, 0x3, 0x1);
		odm_set_bb_reg(dm, rfe_ctrl_anta_src, 0xff, 0x77);
		odm_set_mac_reg(dm, REG_SYM_WLBT_PAPE_SEL, BIT(29), 0x1); /* @dbg 7 */
		odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, 0x3c0, 0x0); /* @dbg 8 */
		odm_set_bb_reg(dm, REG_AGC_TABLE_SELECT, BIT(31), 0x0);
	}

	ODM_delay_us(10);

	/* Store A path Register 88c, c08, 874, c50 */
	reg88c = odm_get_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, MASKDWORD);
	regc08 = odm_get_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD);
	reg874 = odm_get_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD);
	regc50 = odm_get_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, MASKDWORD);

	/* Store AFE Registers */
	if (dm->support_ic_type & ODM_RTL8723B)
		afe_rrx_wait_cca = odm_get_bb_reg(dm, REG_RX_WAIT_CCA, MASKDWORD);

	/* Set PSD 128 pts */
	odm_set_bb_reg(dm, REG_FPGA0_PSD_FUNCTION, BIT(14) | BIT15, 0x0); /* @128 pts */

	/* To SET CH1 to do */
	odm_set_rf_reg(dm, RF_PATH_A, ODM_CHANNEL, RFREGOFFSETMASK, 0x7401); /* @channel 1 */

	/* @AFE all on step */
	if (dm->support_ic_type & ODM_RTL8723B)
		odm_set_bb_reg(dm, REG_RX_WAIT_CCA, MASKDWORD, 0x01c00016);

	/* @3 wire Disable */
	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, MASKDWORD, 0xCCF000C0);

	/* @BB IQK setting */
	odm_set_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800E4);
	odm_set_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x22208000);

	/* @IQK setting tone@ 4.34Mhz */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x10008C1C);
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);

	/* Page B init */
	odm_set_bb_reg(dm, REG_CONFIG_ANT_A, MASKDWORD, 0x00080000);
	odm_set_bb_reg(dm, REG_CONFIG_ANT_A, MASKDWORD, 0x0f600000);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x10008c1f);
	if (dm->support_ic_type & ODM_RTL8723B) {
		odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82150016);
		odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28150016);
	}
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x001028d0);
	odm_set_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, 0x7f, initial_gain);

	/* @IQK Single tone start */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	ODM_delay_us(10000);

	/* PSD report of antenna A */
	PSD_report_tmp = 0x0;
	for (n = 0; n < 2; n++) {
		PSD_report_tmp = phydm_get_psd_data(dm, 14, initial_gain);
		if (PSD_report_tmp > ant_a_report)
			ant_a_report = PSD_report_tmp;
	}

	/* @change to Antenna B */
	if (dm->support_ic_type & ODM_RTL8723B) {
#if 0
		/* odm_set_bb_reg(dm, REG_DPDT_CONTROL, 0x3, 0x2); */
#endif
		odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, 0xfff, 0x280);
		odm_set_bb_reg(dm, REG_AGC_TABLE_SELECT, BIT(31), 0x1);
	}

	ODM_delay_us(10);

	/* PSD report of antenna B */
	PSD_report_tmp = 0x0;
	for (n = 0; n < 2; n++) {
		PSD_report_tmp = phydm_get_psd_data(dm, 14, initial_gain);
		if (PSD_report_tmp > ant_b_report)
			ant_b_report = PSD_report_tmp;
	}

	/* @Close IQK Single Tone function */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* @1 Return to antanna A */
	if (dm->support_ic_type & ODM_RTL8723B) {
		/* @external DPDT */
		odm_set_bb_reg(dm, REG_DPDT_CONTROL, MASKDWORD, reg92c);

		/* @internal S0/S1 */
		odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, MASKDWORD, reg948);
		odm_set_bb_reg(dm, REG_AGC_TABLE_SELECT, MASKDWORD, regb2c);
		odm_set_bb_reg(dm, rfe_ctrl_anta_src, MASKDWORD, reg930);
		odm_set_mac_reg(dm, REG_SYM_WLBT_PAPE_SEL, BIT(29), reg064);
	}

	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, MASKDWORD, reg88c);
	odm_set_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, regc08);
	odm_set_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, reg874);
	odm_set_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, 0x7F, 0x40);
	odm_set_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, MASKDWORD, regc50);
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, current_channel);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, rf_loop_reg);

	/* Reload AFE Registers */
	if (dm->support_ic_type & ODM_RTL8723B)
		odm_set_bb_reg(dm, REG_RX_WAIT_CCA, MASKDWORD, afe_rrx_wait_cca);

	if (dm->support_ic_type & ODM_RTL8723B) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "psd_report_A[%d]= %d\n", 2416,
			  ant_a_report);
		PHYDM_DBG(dm, DBG_ANT_DIV, "psd_report_B[%d]= %d\n", 2416,
			  ant_b_report);

		/* @2 Test ant B based on ant A is ON */
		if (ant_a_report >= 100 && ant_b_report >= 100 && ant_a_report <= 135 && ant_b_report <= 135) {
			u8 TH1 = 2, TH2 = 6;

			if ((ant_a_report - ant_b_report < TH1) || (ant_b_report - ant_a_report < TH1)) {
				dm_swat_table->ANTA_ON = true;
				dm_swat_table->ANTB_ON = true;
				PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Dual Antenna\n",
					  __func__);
			} else if (((ant_a_report - ant_b_report >= TH1) && (ant_a_report - ant_b_report <= TH2)) ||
				   ((ant_b_report - ant_a_report >= TH1) && (ant_b_report - ant_a_report <= TH2))) {
				dm_swat_table->ANTA_ON = false;
				dm_swat_table->ANTB_ON = false;
				is_result = false;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: Need to check again\n",
					  __func__);
			} else {
				dm_swat_table->ANTA_ON = true;
				dm_swat_table->ANTB_ON = false;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: Single Antenna\n", __func__);
			}
			dm->ant_detected_info.is_ant_detected = true;
			dm->ant_detected_info.db_for_ant_a = ant_a_report;
			dm->ant_detected_info.db_for_ant_b = ant_b_report;
			dm->ant_detected_info.db_for_ant_o = ant_0_report;

		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV, "return false!!\n");
			is_result = false;
		}
	}
	return is_result;
}

/* @1 [2. Scan AP RSSI method] ================================================== */

boolean
odm_sw_ant_div_check_before_link(
	void *dm_void)
{
#if (RT_MEM_SIZE_LEVEL != RT_MEM_SIZE_MINIMUM)

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *adapter = dm->adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	//PMGNT_INFO		mgnt_info = &adapter->MgntInfo;
	PMGNT_INFO mgnt_info = &(((PADAPTER)(adapter))->MgntInfo);
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	s8 score = 0;
	PRT_WLAN_BSS p_tmp_bss_desc, p_test_bss_desc;
	u8 power_target_L = 9, power_target_H = 16;
	u8 tmp_power_diff = 0, power_diff = 0, avg_power_diff = 0, max_power_diff = 0, min_power_diff = 0xff;
	u16 index, counter = 0;
	static u8 scan_channel;
	u32 tmp_swas_no_link_bk_reg948;

	PHYDM_DBG(dm, DBG_ANT_DIV, "ANTA_ON = (( %d )) , ANTB_ON = (( %d ))\n",
		  dm->dm_swat_table.ANTA_ON, dm->dm_swat_table.ANTB_ON);

	/* @if(HP id) */
	{
		if (dm->dm_swat_table.rssi_ant_dect_result == true && dm->support_ic_type == ODM_RTL8723B) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "8723B RSSI-based Antenna Detection is done\n");
			return false;
		}

		if (dm->support_ic_type == ODM_RTL8723B) {
			if (dm_swat_table->swas_no_link_bk_reg948 == 0xff)
				dm_swat_table->swas_no_link_bk_reg948 = odm_read_4byte(dm, REG_S0_S1_PATH_SWITCH);
		}
	}

	if (dm->adapter == NULL) { /* @For BSOD when plug/unplug fast.  //By YJ,120413 */
		/* The ODM structure is not initialized. */
		return false;
	}

	/* Retrieve antenna detection registry info, added by Roger, 2012.11.27. */
	if (!IS_ANT_DETECT_SUPPORT_RSSI(((PADAPTER)adapter)))
		return false;
	else
		PHYDM_DBG(dm, DBG_ANT_DIV, "Antenna Detection: RSSI method\n");

	/* Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF. */
	odm_acquire_spin_lock(dm, RT_RF_STATE_SPINLOCK);
	if (hal_data->eRFPowerState != eRfOn || mgnt_info->RFChangeInProgress || mgnt_info->bMediaConnect) {
		odm_release_spin_lock(dm, RT_RF_STATE_SPINLOCK);

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "%s: rf_change_in_progress(%x), e_rf_power_state(%x)\n",
			  __func__, mgnt_info->RFChangeInProgress,
			  hal_data->eRFPowerState);

		dm_swat_table->swas_no_link_state = 0;

		return false;
	} else
		odm_release_spin_lock(dm, RT_RF_STATE_SPINLOCK);
	PHYDM_DBG(dm, DBG_ANT_DIV, "dm_swat_table->swas_no_link_state = %d\n",
		  dm_swat_table->swas_no_link_state);
	/* @1 Run AntDiv mechanism "Before Link" part. */
	if (dm_swat_table->swas_no_link_state == 0) {
		/* @1 Prepare to do Scan again to check current antenna state. */

		/* Set check state to next step. */
		dm_swat_table->swas_no_link_state = 1;

		/* @Copy Current Scan list. */
		mgnt_info->tmpNumBssDesc = mgnt_info->NumBssDesc;
		PlatformMoveMemory((void *)mgnt_info->tmpbssDesc, (void *)mgnt_info->bssDesc, sizeof(RT_WLAN_BSS) * MAX_BSS_DESC);

		/* @Go back to scan function again. */
		PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Scan one more time\n",
			  __func__);
		mgnt_info->ScanStep = 0;
		mgnt_info->bScanAntDetect = true;
		scan_channel = odm_sw_ant_div_select_scan_chnl(adapter);

		if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8821)) {
			if (fat_tab->rx_idle_ant == MAIN_ANT)
				odm_update_rx_idle_ant(dm, AUX_ANT);
			else
				odm_update_rx_idle_ant(dm, MAIN_ANT);
			if (scan_channel == 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: No AP List Avaiable, Using ant(%s)\n",
					  __func__,
					  (fat_tab->rx_idle_ant == MAIN_ANT) ?
					  "AUX_ANT" : "MAIN_ANT");

				if (IS_5G_WIRELESS_MODE(mgnt_info->dot11CurrentWirelessMode)) {
					dm_swat_table->ant_5g = fat_tab->rx_idle_ant;
					PHYDM_DBG(dm, DBG_ANT_DIV, "dm_swat_table->ant_5g=%s\n", (fat_tab->rx_idle_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
				} else {
					dm_swat_table->ant_2g = fat_tab->rx_idle_ant;
					PHYDM_DBG(dm, DBG_ANT_DIV, "dm_swat_table->ant_2g=%s\n", (fat_tab->rx_idle_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
				}
				return false;
			}

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "%s: Change to %s for testing.\n", __func__,
				  ((fat_tab->rx_idle_ant == MAIN_ANT) ?
				  "MAIN_ANT" : "AUX_ANT"));
		} else if (dm->support_ic_type & (ODM_RTL8723B)) {
			/*Switch Antenna to another one.*/

			tmp_swas_no_link_bk_reg948 = odm_read_4byte(dm, REG_S0_S1_PATH_SWITCH);

			if (dm_swat_table->cur_antenna == MAIN_ANT && tmp_swas_no_link_bk_reg948 == 0x200) {
				odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, 0xfff, 0x280);
				odm_set_bb_reg(dm, REG_AGC_TABLE_SELECT, BIT(31), 0x1);
				dm_swat_table->cur_antenna = AUX_ANT;
			} else {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Reg[948]= (( %x )) was in wrong state\n",
					  tmp_swas_no_link_bk_reg948);
				return false;
			}
			ODM_delay_us(10);

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "%s: Change to (( %s-ant))  for testing.\n",
				  __func__,
				  (dm_swat_table->cur_antenna == MAIN_ANT) ?
				  "MAIN" : "AUX");
		}

		odm_sw_ant_div_construct_scan_chnl(adapter, scan_channel);
		PlatformSetTimer(adapter, &mgnt_info->ScanTimer, 5);

		return true;
	} else { /* @dm_swat_table->swas_no_link_state == 1 */
		/* @1 ScanComple() is called after antenna swiched. */
		/* @1 Check scan result and determine which antenna is going */
		/* @1 to be used. */

		PHYDM_DBG(dm, DBG_ANT_DIV, " tmp_num_bss_desc= (( %d ))\n",
			  mgnt_info->tmpNumBssDesc); /* @debug for Dino */

		for (index = 0; index < mgnt_info->tmpNumBssDesc; index++) {
			p_tmp_bss_desc = &mgnt_info->tmpbssDesc[index]; /* @Antenna 1 */
			p_test_bss_desc = &mgnt_info->bssDesc[index]; /* @Antenna 2 */

			if (PlatformCompareMemory(p_test_bss_desc->bdBssIdBuf, p_tmp_bss_desc->bdBssIdBuf, 6) != 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: ERROR!! This shall not happen.\n",
					  __func__);
				continue;
			}

			if (dm->support_ic_type != ODM_RTL8723B) {
				if (p_tmp_bss_desc->ChannelNumber == scan_channel) {
					if (p_tmp_bss_desc->RecvSignalPower > p_test_bss_desc->RecvSignalPower) {
						PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Compare scan entry: score++\n", __func__);
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", p_tmp_bss_desc->bdSsIdBuf, p_tmp_bss_desc->bdSsIdLen);
						PHYDM_DBG(dm, DBG_ANT_DIV, "at ch %d, Original: %d, Test: %d\n\n", p_tmp_bss_desc->ChannelNumber, p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);

						score++;
						PlatformMoveMemory(p_test_bss_desc, p_tmp_bss_desc, sizeof(RT_WLAN_BSS));
					} else if (p_tmp_bss_desc->RecvSignalPower < p_test_bss_desc->RecvSignalPower) {
						PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Compare scan entry: score--\n", __func__);
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", p_tmp_bss_desc->bdSsIdBuf, p_tmp_bss_desc->bdSsIdLen);
						PHYDM_DBG(dm, DBG_ANT_DIV, "at ch %d, Original: %d, Test: %d\n\n", p_tmp_bss_desc->ChannelNumber, p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);
						score--;
					} else {
						if (p_test_bss_desc->bdTstamp - p_tmp_bss_desc->bdTstamp < 5000) {
							RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", p_tmp_bss_desc->bdSsIdBuf, p_tmp_bss_desc->bdSsIdLen);
							PHYDM_DBG(dm, DBG_ANT_DIV, "at ch %d, Original: %d, Test: %d\n", p_tmp_bss_desc->ChannelNumber, p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);
							PHYDM_DBG(dm, DBG_ANT_DIV, "The 2nd Antenna didn't get this AP\n\n");
						}
					}
				}
			} else { /* @8723B */
				if (p_tmp_bss_desc->ChannelNumber == scan_channel) {
					PHYDM_DBG(dm, DBG_ANT_DIV, "channel_number == scan_channel->(( %d ))\n", p_tmp_bss_desc->ChannelNumber);

					if (p_tmp_bss_desc->RecvSignalPower > p_test_bss_desc->RecvSignalPower) { /* Pow(Ant1) > Pow(Ant2) */
						counter++;
						tmp_power_diff = (u8)(p_tmp_bss_desc->RecvSignalPower - p_test_bss_desc->RecvSignalPower);
						power_diff = power_diff + tmp_power_diff;

						PHYDM_DBG(dm, DBG_ANT_DIV, "Original: %d, Test: %d\n", p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);
						PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "SSID:", p_tmp_bss_desc->bdSsIdBuf);
						PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "BSSID:", p_tmp_bss_desc->bdSsIdBuf);

#if 0
						/* PHYDM_DBG(dm,DBG_ANT_DIV, "tmp_power_diff: (( %d)),max_power_diff: (( %d)),min_power_diff: (( %d))\n", tmp_power_diff,max_power_diff,min_power_diff); */
#endif
						if (tmp_power_diff > max_power_diff)
							max_power_diff = tmp_power_diff;
						if (tmp_power_diff < min_power_diff)
							min_power_diff = tmp_power_diff;
#if 0
						/* PHYDM_DBG(dm,DBG_ANT_DIV, "max_power_diff: (( %d)),min_power_diff: (( %d))\n",max_power_diff,min_power_diff); */
#endif

						PlatformMoveMemory(p_test_bss_desc, p_tmp_bss_desc, sizeof(RT_WLAN_BSS));
					} else if (p_test_bss_desc->RecvSignalPower > p_tmp_bss_desc->RecvSignalPower) { /* Pow(Ant1) < Pow(Ant2) */
						counter++;
						tmp_power_diff = (u8)(p_test_bss_desc->RecvSignalPower - p_tmp_bss_desc->RecvSignalPower);
						power_diff = power_diff + tmp_power_diff;
						PHYDM_DBG(dm, DBG_ANT_DIV, "Original: %d, Test: %d\n", p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);
						PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "SSID:", p_tmp_bss_desc->bdSsIdBuf);
						PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "BSSID:", p_tmp_bss_desc->bdSsIdBuf);
						if (tmp_power_diff > max_power_diff)
							max_power_diff = tmp_power_diff;
						if (tmp_power_diff < min_power_diff)
							min_power_diff = tmp_power_diff;
					} else { /* Pow(Ant1) = Pow(Ant2) */
						if (p_test_bss_desc->bdTstamp > p_tmp_bss_desc->bdTstamp) { /* Stamp(Ant1) < Stamp(Ant2) */
							PHYDM_DBG(dm, DBG_ANT_DIV, "time_diff: %lld\n", (p_test_bss_desc->bdTstamp - p_tmp_bss_desc->bdTstamp) / 1000);
							if (p_test_bss_desc->bdTstamp - p_tmp_bss_desc->bdTstamp > 5000) {
								counter++;
								PHYDM_DBG(dm, DBG_ANT_DIV, "Original: %d, Test: %d\n", p_tmp_bss_desc->RecvSignalPower, p_test_bss_desc->RecvSignalPower);
								PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "SSID:", p_tmp_bss_desc->bdSsIdBuf);
								PHYDM_PRINT_ADDR(dm, DBG_ANT_DIV, "BSSID:", p_tmp_bss_desc->bdSsIdBuf);
								min_power_diff = 0;
							}
						} else
							PHYDM_DBG(dm, DBG_ANT_DIV, "[Error !!!]: Time_diff: %lld\n", (p_test_bss_desc->bdTstamp - p_tmp_bss_desc->bdTstamp) / 1000);
					}
				}
			}
		}

		if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8821)) {
			if (mgnt_info->NumBssDesc != 0 && score < 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: Using ant(%s)\n", __func__,
					  (fat_tab->rx_idle_ant == MAIN_ANT) ?
					  "MAIN_ANT" : "AUX_ANT");
			} else {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "%s: Remain ant(%s)\n", __func__,
					  (fat_tab->rx_idle_ant == MAIN_ANT) ?
					  "AUX_ANT" : "MAIN_ANT");

				if (fat_tab->rx_idle_ant == MAIN_ANT)
					odm_update_rx_idle_ant(dm, AUX_ANT);
				else
					odm_update_rx_idle_ant(dm, MAIN_ANT);
			}

			if (IS_5G_WIRELESS_MODE(mgnt_info->dot11CurrentWirelessMode)) {
				dm_swat_table->ant_5g = fat_tab->rx_idle_ant;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "dm_swat_table->ant_5g=%s\n",
					  (fat_tab->rx_idle_ant == MAIN_ANT) ?
					  "MAIN_ANT" : "AUX_ANT");
			} else {
				dm_swat_table->ant_2g = fat_tab->rx_idle_ant;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "dm_swat_table->ant_2g=%s\n",
					  (fat_tab->rx_idle_ant == MAIN_ANT) ?
					  "MAIN_ANT" : "AUX_ANT");
			}
		} else if (dm->support_ic_type == ODM_RTL8723B) {
			if (counter == 0) {
				if (dm->dm_swat_table.pre_aux_fail_detec == false) {
					dm->dm_swat_table.pre_aux_fail_detec = true;
					dm->dm_swat_table.rssi_ant_dect_result = false;
					PHYDM_DBG(dm, DBG_ANT_DIV, "counter=(( 0 )) , [[ Cannot find any AP with Aux-ant ]] ->  Scan Target-channel again\n");

					/* @3 [ Scan again ] */
					odm_sw_ant_div_construct_scan_chnl(adapter, scan_channel);
					PlatformSetTimer(adapter, &mgnt_info->ScanTimer, 5);
					return true;
				} else { /* pre_aux_fail_detec == true */
					/* @2 [ Single Antenna ] */
					dm->dm_swat_table.pre_aux_fail_detec = false;
					dm->dm_swat_table.rssi_ant_dect_result = true;
					PHYDM_DBG(dm, DBG_ANT_DIV, "counter=(( 0 )) , [[  Still cannot find any AP ]]\n");
					PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Single antenna\n", __func__);
				}
				dm->dm_swat_table.aux_fail_detec_counter++;
			} else {
				dm->dm_swat_table.pre_aux_fail_detec = false;

				if (counter == 3) {
					avg_power_diff = ((power_diff - max_power_diff - min_power_diff) >> 1) + ((max_power_diff + min_power_diff) >> 2);
					PHYDM_DBG(dm, DBG_ANT_DIV, "counter: (( %d )) ,  power_diff: (( %d ))\n", counter, power_diff);
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ counter==3 ] Modified avg_power_diff: (( %d )) , max_power_diff: (( %d )) ,  min_power_diff: (( %d ))\n", avg_power_diff, max_power_diff, min_power_diff);
				} else if (counter >= 4) {
					avg_power_diff = (power_diff - max_power_diff - min_power_diff) / (counter - 2);
					PHYDM_DBG(dm, DBG_ANT_DIV, "counter: (( %d )) ,  power_diff: (( %d ))\n", counter, power_diff);
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ counter>=4 ] Modified avg_power_diff: (( %d )) , max_power_diff: (( %d )) ,  min_power_diff: (( %d ))\n", avg_power_diff, max_power_diff, min_power_diff);

				} else { /* @counter==1,2 */
					avg_power_diff = power_diff / counter;
					PHYDM_DBG(dm, DBG_ANT_DIV, "avg_power_diff: (( %d )) , counter: (( %d )) ,  power_diff: (( %d ))\n", avg_power_diff, counter, power_diff);
				}

				/* @2 [ Retry ] */
				if (avg_power_diff >= power_target_L && avg_power_diff <= power_target_H) {
					dm->dm_swat_table.retry_counter++;

					if (dm->dm_swat_table.retry_counter <= 3) {
						dm->dm_swat_table.rssi_ant_dect_result = false;
						PHYDM_DBG(dm, DBG_ANT_DIV, "[[ Low confidence result ]] avg_power_diff= (( %d ))  ->  Scan Target-channel again ]]\n", avg_power_diff);

						/* @3 [ Scan again ] */
						odm_sw_ant_div_construct_scan_chnl(adapter, scan_channel);
						PlatformSetTimer(adapter, &mgnt_info->ScanTimer, 5);
						return true;
					} else {
						dm->dm_swat_table.rssi_ant_dect_result = true;
						PHYDM_DBG(dm, DBG_ANT_DIV, "[[ Still Low confidence result ]]  (( retry_counter > 3 ))\n");
						PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Single antenna\n", __func__);
					}
				}
				/* @2 [ Dual Antenna ] */
				else if ((mgnt_info->NumBssDesc != 0) && (avg_power_diff < power_target_L)) {
					dm->dm_swat_table.rssi_ant_dect_result = true;
					if (dm->dm_swat_table.ANTB_ON == false) {
						dm->dm_swat_table.ANTA_ON = true;
						dm->dm_swat_table.ANTB_ON = true;
					}
					PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Dual antenna\n", __func__);
					dm->dm_swat_table.dual_ant_counter++;

					/* set bt coexDM from 1ant coexDM to 2ant coexDM */
					BT_SetBtCoexAntNum(adapter, BT_COEX_ANT_TYPE_DETECTED, 2);

					/* @3 [ Init antenna diversity ] */
					dm->support_ability |= ODM_BB_ANT_DIV;
					odm_ant_div_init(dm);
				}
				/* @2 [ Single Antenna ] */
				else if (avg_power_diff > power_target_H) {
					dm->dm_swat_table.rssi_ant_dect_result = true;
					if (dm->dm_swat_table.ANTB_ON == true) {
						dm->dm_swat_table.ANTA_ON = true;
						dm->dm_swat_table.ANTB_ON = false;
#if 0
						/* @bt_set_bt_coex_ant_num(adapter, BT_COEX_ANT_TYPE_DETECTED, 1); */
#endif
					}
					PHYDM_DBG(dm, DBG_ANT_DIV, "%s: Single antenna\n", __func__);
					dm->dm_swat_table.single_ant_counter++;
				}
			}
#if 0
			/* PHYDM_DBG(dm,DBG_ANT_DIV, "is_result=(( %d ))\n",dm->dm_swat_table.rssi_ant_dect_result); */
#endif
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "dual_ant_counter = (( %d )), single_ant_counter = (( %d )) , retry_counter = (( %d )) , aux_fail_detec_counter = (( %d ))\n\n\n",
				  dm->dm_swat_table.dual_ant_counter,
				  dm->dm_swat_table.single_ant_counter,
				  dm->dm_swat_table.retry_counter,
				  dm->dm_swat_table.aux_fail_detec_counter);

			/* @2 recover the antenna setting */

			if (dm->dm_swat_table.ANTB_ON == false)
				odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, 0xfff, (dm_swat_table->swas_no_link_bk_reg948));

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "is_result=(( %d )), Recover  Reg[948]= (( %x ))\n\n",
				  dm->dm_swat_table.rssi_ant_dect_result,
				  dm_swat_table->swas_no_link_bk_reg948);
		}

		/* @Check state reset to default and wait for next time. */
		dm_swat_table->swas_no_link_state = 0;
		mgnt_info->bScanAntDetect = false;

		return false;
	}

#else
	return false;
#endif

	return false;
}

/* @1 [3. PSD method] ========================================================== */
void odm_single_dual_antenna_detection_psd(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 channel_ori;
	u8 initial_gain = 0x36;
	u8 tone_idx;
	u8 tone_lenth_1 = 7, tone_lenth_2 = 4;
	u16 tone_idx_1[7] = {88, 104, 120, 8, 24, 40, 56};
	u16 tone_idx_2[4] = {8, 24, 40, 56};
	u32 psd_report_main[11] = {0}, psd_report_aux[11] = {0};
	/* u8	tone_lenth_1=4, tone_lenth_2=2; */
	/* u16	tone_idx_1[4]={88, 120, 24, 56}; */
	/* u16	tone_idx_2[2]={ 24,  56}; */
	/* u32	psd_report_main[6]={0}, psd_report_aux[6]={0}; */

	u32 PSD_report_temp, max_psd_report_main = 0, max_psd_report_aux = 0;
	u32 PSD_power_threshold;
	u32 main_psd_result = 0, aux_psd_result = 0;
	u32 regc50, reg948, regb2c, regc14, reg908;
	u32 i = 0, test_num = 8;

	if (dm->support_ic_type != ODM_RTL8723B)
		return;

	PHYDM_DBG(dm, DBG_ANT_DIV, "%s============>\n", __func__);

	/* @2 [ Backup Current RF/BB Settings ] */

	channel_ori = odm_get_rf_reg(dm, RF_PATH_A, ODM_CHANNEL, RFREGOFFSETMASK);
	reg948 = odm_get_bb_reg(dm, REG_S0_S1_PATH_SWITCH, MASKDWORD);
	regb2c = odm_get_bb_reg(dm, REG_AGC_TABLE_SELECT, MASKDWORD);
	regc50 = odm_get_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, MASKDWORD);
	regc14 = odm_get_bb_reg(dm, R_0xc14, MASKDWORD);
	reg908 = odm_get_bb_reg(dm, R_0x908, MASKDWORD);

	/* @2 [ setting for doing PSD function (CH4)] */
	odm_set_bb_reg(dm, REG_FPGA0_RFMOD, BIT(24), 0); /* @disable whole CCK block */
	odm_write_1byte(dm, REG_TXPAUSE, 0xFF); /* Turn off TX  ->  Pause TX Queue */
	odm_set_bb_reg(dm, R_0xc14, MASKDWORD, 0x0); /* @[ Set IQK Matrix = 0 ] equivalent to [ Turn off CCA] */

	/* PHYTXON while loop */
	odm_set_bb_reg(dm, R_0x908, MASKDWORD, 0x803);
	while (odm_get_bb_reg(dm, R_0xdf4, BIT(6))) {
		i++;
		if (i > 1000000) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Wait in %s() more than %d times!\n",
				  __FUNCTION__, i);
			break;
		}
	}

	odm_set_bb_reg(dm, R_0xc50, 0x7f, initial_gain);
	odm_set_rf_reg(dm, RF_PATH_A, ODM_CHANNEL, 0x7ff, 0x04); /* Set RF to CH4 & 40M */
	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, 0xf00000, 0xf); /* @3 wire Disable    88c[23:20]=0xf */
	odm_set_bb_reg(dm, REG_FPGA0_PSD_FUNCTION, BIT(14) | BIT15, 0x0); /* 128 pt	 */ /* Set PSD 128 ptss */
	ODM_delay_us(3000);

	/* @2 [ Doing PSD Function in (CH4)] */

	/* @Antenna A */
	PHYDM_DBG(dm, DBG_ANT_DIV, "Switch to Main-ant   (CH4)\n");
	odm_set_bb_reg(dm, R_0x948, 0xfff, 0x200);
	ODM_delay_us(10);
	PHYDM_DBG(dm, DBG_ANT_DIV, "dbg\n");
	for (i = 0; i < test_num; i++) {
		for (tone_idx = 0; tone_idx < tone_lenth_1; tone_idx++) {
			PSD_report_temp = phydm_get_psd_data(dm, tone_idx_1[tone_idx], initial_gain);
			/* @if(  PSD_report_temp>psd_report_main[tone_idx]  ) */
			psd_report_main[tone_idx] += PSD_report_temp;
		}
	}
	/* @Antenna B */
	PHYDM_DBG(dm, DBG_ANT_DIV, "Switch to Aux-ant   (CH4)\n");
	odm_set_bb_reg(dm, R_0x948, 0xfff, 0x280);
	ODM_delay_us(10);
	for (i = 0; i < test_num; i++) {
		for (tone_idx = 0; tone_idx < tone_lenth_1; tone_idx++) {
			PSD_report_temp = phydm_get_psd_data(dm, tone_idx_1[tone_idx], initial_gain);
			/* @if(  PSD_report_temp>psd_report_aux[tone_idx]  ) */
			psd_report_aux[tone_idx] += PSD_report_temp;
		}
	}
	/* @2 [ Doing PSD Function in (CH8)] */

	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, 0xf00000, 0x0); /* @3 wire enable    88c[23:20]=0x0 */
	ODM_delay_us(3000);

	odm_set_bb_reg(dm, R_0xc50, 0x7f, initial_gain);
	odm_set_rf_reg(dm, RF_PATH_A, ODM_CHANNEL, 0x7ff, 0x04); /* Set RF to CH8 & 40M */

	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, 0xf00000, 0xf); /* @3 wire Disable    88c[23:20]=0xf */
	ODM_delay_us(3000);

	/* @Antenna A */
	PHYDM_DBG(dm, DBG_ANT_DIV, "Switch to Main-ant   (CH8)\n");
	odm_set_bb_reg(dm, R_0x948, 0xfff, 0x200);
	ODM_delay_us(10);

	for (i = 0; i < test_num; i++) {
		for (tone_idx = 0; tone_idx < tone_lenth_2; tone_idx++) {
			PSD_report_temp = phydm_get_psd_data(dm, tone_idx_2[tone_idx], initial_gain);
			/* @if(  PSD_report_temp>psd_report_main[tone_idx]  ) */
			psd_report_main[tone_lenth_1 + tone_idx] += PSD_report_temp;
		}
	}

	/* @Antenna B */
	PHYDM_DBG(dm, DBG_ANT_DIV, "Switch to Aux-ant   (CH8)\n");
	odm_set_bb_reg(dm, R_0x948, 0xfff, 0x280);
	ODM_delay_us(10);

	for (i = 0; i < test_num; i++) {
		for (tone_idx = 0; tone_idx < tone_lenth_2; tone_idx++) {
			PSD_report_temp = phydm_get_psd_data(dm, tone_idx_2[tone_idx], initial_gain);
			/* @if(  PSD_report_temp>psd_report_aux[tone_idx]  ) */
			psd_report_aux[tone_lenth_1 + tone_idx] += PSD_report_temp;
		}
	}

	/* @2 [ Calculate Result ] */

	PHYDM_DBG(dm, DBG_ANT_DIV, "\nMain PSD Result: (ALL)\n");
	for (tone_idx = 0; tone_idx < (tone_lenth_1 + tone_lenth_2); tone_idx++) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[Tone-%d]: %d,\n", (tone_idx + 1),
			  psd_report_main[tone_idx]);
		main_psd_result += psd_report_main[tone_idx];
		if (psd_report_main[tone_idx] > max_psd_report_main)
			max_psd_report_main = psd_report_main[tone_idx];
	}
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "--------------------------- \nTotal_Main= (( %d ))\n",
		  main_psd_result);
	PHYDM_DBG(dm, DBG_ANT_DIV, "MAX_Main = (( %d ))\n",
		  max_psd_report_main);

	PHYDM_DBG(dm, DBG_ANT_DIV, "\nAux PSD Result: (ALL)\n");
	for (tone_idx = 0; tone_idx < (tone_lenth_1 + tone_lenth_2); tone_idx++) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[Tone-%d]: %d,\n", (tone_idx + 1),
			  psd_report_aux[tone_idx]);
		aux_psd_result += psd_report_aux[tone_idx];
		if (psd_report_aux[tone_idx] > max_psd_report_aux)
			max_psd_report_aux = psd_report_aux[tone_idx];
	}
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "--------------------------- \nTotal_Aux= (( %d ))\n",
		  aux_psd_result);
	PHYDM_DBG(dm, DBG_ANT_DIV, "MAX_Aux = (( %d ))\n\n",
		  max_psd_report_aux);

	/* @main_psd_result=main_psd_result-max_psd_report_main; */
	/* @aux_psd_result=aux_psd_result-max_psd_report_aux; */
	PSD_power_threshold = (main_psd_result * 7) >> 3;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Main_result, Aux_result ] = [ %d , %d ], PSD_power_threshold=(( %d ))\n",
		  main_psd_result, aux_psd_result, PSD_power_threshold);

	/* @3 [ Dual Antenna ] */
	if (aux_psd_result >= PSD_power_threshold) {
		if (dm->dm_swat_table.ANTB_ON == false) {
			dm->dm_swat_table.ANTA_ON = true;
			dm->dm_swat_table.ANTB_ON = true;
		}
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "odm_sw_ant_div_check_before_link(): Dual antenna\n");

#if 0
		/* set bt coexDM from 1ant coexDM to 2ant coexDM */
		/* @bt_set_bt_coex_ant_num(adapter, BT_COEX_ANT_TYPE_DETECTED, 2); */
#endif

		/* @Init antenna diversity */
		dm->support_ability |= ODM_BB_ANT_DIV;
		odm_ant_div_init(dm);
	}
	/* @3 [ Single Antenna ] */
	else {
		if (dm->dm_swat_table.ANTB_ON == true) {
			dm->dm_swat_table.ANTA_ON = true;
			dm->dm_swat_table.ANTB_ON = false;
		}
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "odm_sw_ant_div_check_before_link(): Single antenna\n");
	}

	/* @2 [ Recover all parameters ] */

	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, channel_ori);
	odm_set_bb_reg(dm, REG_FPGA0_ANALOG_PARAMETER4, 0xf00000, 0x0); /* @3 wire enable    88c[23:20]=0x0 */
	odm_set_bb_reg(dm, R_0xc50, 0x7f, regc50);

	odm_set_bb_reg(dm, REG_S0_S1_PATH_SWITCH, MASKDWORD, reg948);
	odm_set_bb_reg(dm, REG_AGC_TABLE_SELECT, MASKDWORD, regb2c);

	odm_set_bb_reg(dm, REG_FPGA0_RFMOD, BIT(24), 1); /* @enable whole CCK block */
	odm_write_1byte(dm, REG_TXPAUSE, 0x0); /* Turn on TX	 */ /* Resume TX Queue */
	odm_set_bb_reg(dm, R_0xc14, MASKDWORD, regc14); /* @[ Set IQK Matrix = 0 ] equivalent to [ Turn on CCA] */
	odm_set_bb_reg(dm, R_0x908, MASKDWORD, reg908);

	return;
}

void odm_sw_ant_detect_init(void *dm_void)
{
#if (RTL8723B_SUPPORT == 1)

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *dm_swat_table = &dm->dm_swat_table;

	if (dm->support_ic_type != ODM_RTL8723B)
		return;

	/* @dm_swat_table->pre_antenna = MAIN_ANT; */
	/* @dm_swat_table->cur_antenna = MAIN_ANT; */
	dm_swat_table->swas_no_link_state = 0;
	dm_swat_table->pre_aux_fail_detec = false;
	dm_swat_table->swas_no_link_bk_reg948 = 0xff;

#ifdef CONFIG_PSD_TOOL
	phydm_psd_init(dm);
#endif
#endif
}
#endif

