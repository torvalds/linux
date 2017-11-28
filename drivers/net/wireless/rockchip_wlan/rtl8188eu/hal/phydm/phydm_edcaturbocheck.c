/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if PHYDM_SUPPORT_EDCA

void
odm_edca_turbo_init(
	void		*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = NULL;
	HAL_DATA_TYPE	*p_hal_data = NULL;

	if (p_dm_odm->adapter == NULL)	{
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("EdcaTurboInit fail!!!\n"));
		return;
	}

	adapter = p_dm_odm->adapter;
	p_hal_data = GET_HAL_DATA(adapter);

	p_dm_odm->dm_edca_table.is_current_turbo_edca = false;
	p_dm_odm->dm_edca_table.is_cur_rdl_state = false;
	p_hal_data->is_any_non_be_pkts = false;

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
	p_dm_odm->dm_edca_table.is_current_turbo_edca = false;
	p_dm_odm->dm_edca_table.is_cur_rdl_state = false;
	adapter->recvpriv.is_any_non_be_pkts = false;

#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Orginial VO PARAM: 0x%x\n", odm_read_4byte(p_dm_odm, ODM_EDCA_VO_PARAM)));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Orginial VI PARAM: 0x%x\n", odm_read_4byte(p_dm_odm, ODM_EDCA_VI_PARAM)));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Orginial BE PARAM: 0x%x\n", odm_read_4byte(p_dm_odm, ODM_EDCA_BE_PARAM)));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Orginial BK PARAM: 0x%x\n", odm_read_4byte(p_dm_odm, ODM_EDCA_BK_PARAM)));


}	/* ODM_InitEdcaTurbo */

void
odm_edca_turbo_check(
	void		*p_dm_void
)
{
	/*  */
	/* For AP/ADSL use struct rtl8192cd_priv* */
	/* For CE/NIC use struct _ADAPTER* */
	/*  */

	/*  */
	/* 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate */
	/* at the same time. In the stage2/3, we need to prive universal interface and merge all */
	/* HW dynamic mechanism. */
	/*  */
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("odm_edca_turbo_check========================>\n"));

	if (!(p_dm_odm->support_ability & ODM_MAC_EDCA_TURBO))
		return;

	switch	(p_dm_odm->support_platform) {
	case	ODM_WIN:

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		odm_edca_turbo_check_mp(p_dm_odm);
#endif
		break;

	case	ODM_CE:
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		odm_edca_turbo_check_ce(p_dm_odm);
#endif
		break;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("<========================odm_edca_turbo_check\n"));

}	/* odm_CheckEdcaTurbo */

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)


void
odm_edca_turbo_check_ce(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	u32	EDCA_BE_UL = 0x5ea42b;/* Parameter suggested by Scott  */ /* edca_setting_UL[p_mgnt_info->iot_peer]; */
	u32	EDCA_BE_DL = 0x00a42b;/* Parameter suggested by Scott  */ /* edca_setting_DL[p_mgnt_info->iot_peer]; */
	u32	ic_type = p_dm_odm->support_ic_type;
	u32	iot_peer = 0;
	u8	wireless_mode = 0xFF;                 /* invalid value */
	u32	traffic_index;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u8	bbtchange = _TRUE;
	u8	is_bias_on_rx = _FALSE;
	HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);
	struct dvobj_priv		*pdvobjpriv = adapter_to_dvobj(adapter);
	struct xmit_priv		*pxmitpriv = &(adapter->xmitpriv);
	struct recv_priv		*precvpriv = &(adapter->recvpriv);
	struct registry_priv	*pregpriv = &adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (p_dm_odm->is_linked != _TRUE) {
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	if ((pregpriv->wifi_spec == 1)) { /* || (pmlmeinfo->HT_enable == 0)) */
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	if (p_dm_odm->p_wireless_mode != NULL)
		wireless_mode = *(p_dm_odm->p_wireless_mode);

	iot_peer = pmlmeinfo->assoc_AP_vendor;

	if (iot_peer >=  HT_IOT_PEER_MAX) {
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	if (p_dm_odm->support_ic_type & ODM_RTL8188E) {
		if ((iot_peer == HT_IOT_PEER_RALINK) || (iot_peer == HT_IOT_PEER_ATHEROS))
			is_bias_on_rx = _TRUE;
	}

	/* Check if the status needs to be changed. */
	if ((bbtchange) || (!precvpriv->is_any_non_be_pkts)) {
		cur_tx_bytes = pdvobjpriv->traffic_stat.cur_tx_bytes;
		cur_rx_bytes = pdvobjpriv->traffic_stat.cur_rx_bytes;

		/* traffic, TX or RX */
		if (is_bias_on_rx) {
			if (cur_tx_bytes > (cur_rx_bytes << 2)) {
				/* Uplink TP is present. */
				traffic_index = UP_LINK;
			} else {
				/* Balance TP is present. */
				traffic_index = DOWN_LINK;
			}
		} else {
			if (cur_rx_bytes > (cur_tx_bytes << 2)) {
				/* Downlink TP is present. */
				traffic_index = DOWN_LINK;
			} else {
				/* Balance TP is present. */
				traffic_index = UP_LINK;
			}
		}

		/* if ((p_dm_odm->dm_edca_table.prv_traffic_idx != traffic_index) || (!p_dm_odm->dm_edca_table.is_current_turbo_edca)) */
		{
			if (p_dm_odm->support_interface == ODM_ITRF_PCIE) {
				EDCA_BE_UL = 0x6ea42b;
				EDCA_BE_DL = 0x6ea42b;
			}

			/* 92D txop can't be set to 0x3e for cisco1250 */
			if ((iot_peer == HT_IOT_PEER_CISCO) && (wireless_mode == ODM_WM_N24G)) {
				EDCA_BE_DL = edca_setting_DL[iot_peer];
				EDCA_BE_UL = edca_setting_UL[iot_peer];
			}
			/* merge from 92s_92c_merge temp brunch v2445    20120215 */
			else if ((iot_peer == HT_IOT_PEER_CISCO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == (ODM_WM_B | ODM_WM_G)) || (wireless_mode == ODM_WM_A) || (wireless_mode == ODM_WM_B)))
				EDCA_BE_DL = edca_setting_dl_g_mode[iot_peer];
			else if ((iot_peer == HT_IOT_PEER_AIRGO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == ODM_WM_A)))
				EDCA_BE_DL = 0xa630;
			else if (iot_peer == HT_IOT_PEER_MARVELL) {
				EDCA_BE_DL = edca_setting_DL[iot_peer];
				EDCA_BE_UL = edca_setting_UL[iot_peer];
			} else if (iot_peer == HT_IOT_PEER_ATHEROS) {
				/* Set DL EDCA for Atheros peer to 0x3ea42b. Suggested by SD3 Wilson for ASUS TP issue. */
				EDCA_BE_DL = edca_setting_DL[iot_peer];
			}

			if ((ic_type == ODM_RTL8812) || (ic_type == ODM_RTL8821) || (ic_type == ODM_RTL8192E)) { /* add 8812AU/8812AE */
				EDCA_BE_UL = 0x5ea42b;
				EDCA_BE_DL = 0x5ea42b;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("8812A: EDCA_BE_UL=0x%x EDCA_BE_DL =0x%x", EDCA_BE_UL, EDCA_BE_DL));
			}

			if (traffic_index == DOWN_LINK)
				edca_param = EDCA_BE_DL;
			else
				edca_param = EDCA_BE_UL;

			rtw_write32(adapter, REG_EDCA_BE_PARAM, edca_param);

			p_dm_odm->dm_edca_table.prv_traffic_idx = traffic_index;
		}

		p_dm_odm->dm_edca_table.is_current_turbo_edca = _TRUE;
	} else {
		/*  */
		/* Turn Off EDCA turbo here. */
		/* Restore original EDCA according to the declaration of AP. */
		/*  */
		if (p_dm_odm->dm_edca_table.is_current_turbo_edca) {
			rtw_write32(adapter, REG_EDCA_BE_PARAM, p_hal_data->ac_param_be);
			p_dm_odm->dm_edca_table.is_current_turbo_edca = _FALSE;
		}
	}

}


#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_edca_turbo_check_mp(
	void		*p_dm_void
)
{

	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);

	struct _ADAPTER			*p_default_adapter = get_default_adapter(adapter);
	struct _ADAPTER			*p_ext_adapter = get_first_ext_adapter(adapter); /* NULL; */
	PMGNT_INFO			p_mgnt_info = &adapter->MgntInfo;
	PSTA_QOS			p_sta_qos = adapter->MgntInfo.p_sta_qos;
	/* [Win7 count Tx/Rx statistic for Extension Port] odm_CheckEdcaTurbo's adapter is always Default. 2009.08.20, by Bohn */
	u64				ext_cur_tx_ok_cnt = 0;
	u64				ext_cur_rx_ok_cnt = 0;
	/* For future Win7  Enable Default Port to modify AMPDU size dynamically, 2009.08.20, Bohn. */
	u8 two_port_status = (u8)TWO_PORT_STATUS__WITHOUT_ANY_ASSOCIATE;

	/* Keep past Tx/Rx packet count for RT-to-RT EDCA turbo. */
	u64				cur_tx_ok_cnt = 0;
	u64				cur_rx_ok_cnt = 0;
	u32				EDCA_BE_UL = 0x5ea42b;/* Parameter suggested by Scott  */ /* edca_setting_UL[p_mgnt_info->iot_peer]; */
	u32				EDCA_BE_DL = 0x5ea42b;/* Parameter suggested by Scott  */ /* edca_setting_DL[p_mgnt_info->iot_peer]; */
	u32                         EDCA_BE = 0x5ea42b;
	u8                         iot_peer = 0;
	bool                      *p_is_cur_rdl_state = NULL;
	bool                      is_last_is_cur_rdl_state = false;
	bool				 is_bias_on_rx = false;
	bool				is_edca_turbo_on = false;
	u8				tx_rate = 0xFF;
	u64				value64;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("odm_edca_turbo_check_mp========================>"));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Orginial BE PARAM: 0x%x\n", odm_read_4byte(p_dm_odm, ODM_EDCA_BE_PARAM)));

	/* *******************************
	 * list paramter for different platform
	 * ******************************* */
	is_last_is_cur_rdl_state = p_dm_odm->dm_edca_table.is_cur_rdl_state;
	p_is_cur_rdl_state = &(p_dm_odm->dm_edca_table.is_cur_rdl_state);

	/* 2012/09/14 MH Add */
	if (p_mgnt_info->num_non_be_pkt > p_mgnt_info->reg_edca_thresh && !(adapter->MgntInfo.wifi_confg & RT_WIFI_LOGO))
		p_hal_data->is_any_non_be_pkts = true;

	p_mgnt_info->num_non_be_pkt = 0;

	/* Caculate TX/RX TP: */
	cur_tx_ok_cnt = p_dm_odm->cur_tx_ok_cnt;
	cur_rx_ok_cnt = p_dm_odm->cur_rx_ok_cnt;


	if (p_ext_adapter == NULL)
		p_ext_adapter = p_default_adapter;

	ext_cur_tx_ok_cnt = p_ext_adapter->tx_stats.num_tx_bytes_unicast - p_mgnt_info->ext_last_tx_ok_cnt;
	ext_cur_rx_ok_cnt = p_ext_adapter->rx_stats.num_rx_bytes_unicast - p_mgnt_info->ext_last_rx_ok_cnt;
	get_two_port_shared_resource(adapter, TWO_PORT_SHARED_OBJECT__STATUS, NULL, &two_port_status);
	/* For future Win7  Enable Default Port to modify AMPDU size dynamically, 2009.08.20, Bohn. */
	if (two_port_status == TWO_PORT_STATUS__EXTENSION_ONLY) {
		cur_tx_ok_cnt = ext_cur_tx_ok_cnt ;
		cur_rx_ok_cnt = ext_cur_rx_ok_cnt ;
	}
	/*  */
	iot_peer = p_mgnt_info->iot_peer;
	is_bias_on_rx = (p_mgnt_info->iot_action & HT_IOT_ACT_EDCA_BIAS_ON_RX) ? true : false;
	is_edca_turbo_on = ((!p_hal_data->is_any_non_be_pkts)) ? true : false;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("is_any_non_be_pkts : 0x%lx\n", p_hal_data->is_any_non_be_pkts));


	/* *******************************
	 * check if edca turbo is disabled
	 * ******************************* */
	if (odm_is_edca_turbo_disable(p_dm_odm)) {
		p_hal_data->is_any_non_be_pkts = false;
		p_mgnt_info->last_tx_ok_cnt = adapter->tx_stats.num_tx_bytes_unicast;
		p_mgnt_info->last_rx_ok_cnt = adapter->rx_stats.num_rx_bytes_unicast;
		p_mgnt_info->ext_last_tx_ok_cnt = p_ext_adapter->tx_stats.num_tx_bytes_unicast;
		p_mgnt_info->ext_last_rx_ok_cnt = p_ext_adapter->rx_stats.num_rx_bytes_unicast;

	}

	/* *******************************
	 * remove iot case out
	 * ******************************* */
	odm_edca_para_sel_by_iot(p_dm_odm, &EDCA_BE_UL, &EDCA_BE_DL);


	/* *******************************
	 * Check if the status needs to be changed.
	 * ******************************* */
	if (is_edca_turbo_on) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("is_edca_turbo_on : 0x%x is_bias_on_rx : 0x%x\n", is_edca_turbo_on, is_bias_on_rx));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("cur_tx_ok_cnt : 0x%lx\n", cur_tx_ok_cnt));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("cur_rx_ok_cnt : 0x%lx\n", cur_rx_ok_cnt));
		if (is_bias_on_rx)
			odm_edca_choose_traffic_idx(p_dm_odm, cur_tx_ok_cnt, cur_rx_ok_cnt,   true,  p_is_cur_rdl_state);
		else
			odm_edca_choose_traffic_idx(p_dm_odm, cur_tx_ok_cnt, cur_rx_ok_cnt,   false,  p_is_cur_rdl_state);

		/* modify by Guo.Mingzhi 2011-12-29 */
		if (adapter->AP_EDCA_PARAM[0] != EDCA_BE)
			EDCA_BE = adapter->AP_EDCA_PARAM[0];
		else
			EDCA_BE = ((*p_is_cur_rdl_state) == true) ? EDCA_BE_DL : EDCA_BE_UL;

		/*For TPLINK 8188EU test*/
		if ((IS_HARDWARE_TYPE_8188EU(adapter)) && (p_hal_data->UndecoratedSmoothedPWDB < 28)) { /* Set to origimal EDCA 0x5EA42B now need to update.*/

		} else { /*Use TPLINK preferred EDCA parameters.*/
			EDCA_BE = p_mgnt_info->EDCABEPara;
		}

		if (IS_HARDWARE_TYPE_8821U(adapter)) {
			if (p_mgnt_info->reg_tx_duty_enable) {
				/* 2013.01.23 LukeLee: debug for 8811AU thermal issue (reduce Tx duty cycle) */
				if (!p_mgnt_info->forced_data_rate) { /* auto rate */
					if (p_dm_odm->tx_rate != 0xFF)
						tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm_odm->tx_rate);
				} else /* force rate */
					tx_rate = (u8) p_mgnt_info->forced_data_rate;

				value64 = (cur_rx_ok_cnt << 2);
				if (cur_tx_ok_cnt < value64) /* Downlink */
					odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
				else { /* Uplink */
					/*dbg_print("p_rf_calibrate_info->thermal_value = 0x%X\n", p_rf_calibrate_info->thermal_value);*/
					/*if(p_rf_calibrate_info->thermal_value < p_hal_data->eeprom_thermal_meter)*/
					if ((p_dm_odm->rf_calibrate_info.thermal_value < 0x2c) || (*p_dm_odm->p_band_type == BAND_ON_2_4G))
						odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
					else {
						switch (tx_rate) {
						case MGN_VHT1SS_MCS6:
						case MGN_VHT1SS_MCS5:
						case MGN_MCS6:
						case MGN_MCS5:
						case MGN_48M:
						case MGN_54M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0x1ea42b);
							break;
						case MGN_VHT1SS_MCS4:
						case MGN_MCS4:
						case MGN_36M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa42b);
							break;
						case MGN_VHT1SS_MCS3:
						case MGN_MCS3:
						case MGN_24M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa47f);
							break;
						case MGN_VHT1SS_MCS2:
						case MGN_MCS2:
						case MGN_18M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa57f);
							break;
						case MGN_VHT1SS_MCS1:
						case MGN_MCS1:
						case MGN_9M:
						case MGN_12M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa77f);
							break;
						case MGN_VHT1SS_MCS0:
						case MGN_MCS0:
						case MGN_6M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa87f);
							break;
						default:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
							break;
						}
					}
				}
			} else
				odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);

		} else if (IS_HARDWARE_TYPE_8812AU(adapter)) {
			if (p_mgnt_info->reg_tx_duty_enable) {
				/* 2013.07.26 Wilson: debug for 8812AU thermal issue (reduce Tx duty cycle) */
				/* it;s the same issue as 8811AU */
				if (!p_mgnt_info->forced_data_rate) { /* auto rate */
					if (p_dm_odm->tx_rate != 0xFF)
						tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm_odm->tx_rate);
				} else /* force rate */
					tx_rate = (u8) p_mgnt_info->forced_data_rate;

				value64 = (cur_rx_ok_cnt << 2);
				if (cur_tx_ok_cnt < value64) /* Downlink */
					odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
				else { /* Uplink */
					/*dbg_print("p_rf_calibrate_info->thermal_value = 0x%X\n", p_rf_calibrate_info->thermal_value);*/
					/*if(p_rf_calibrate_info->thermal_value < p_hal_data->eeprom_thermal_meter)*/
					if ((p_dm_odm->rf_calibrate_info.thermal_value < 0x2c) || (*p_dm_odm->p_band_type == BAND_ON_2_4G))
						odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
					else {
						switch (tx_rate) {
						case MGN_VHT2SS_MCS9:
						case MGN_VHT1SS_MCS9:
						case MGN_VHT1SS_MCS8:
						case MGN_MCS15:
						case MGN_MCS7:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0x1ea44f);
						case MGN_VHT2SS_MCS8:
						case MGN_VHT1SS_MCS7:
						case MGN_MCS14:
						case MGN_MCS6:
						case MGN_54M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa44f);
						case MGN_VHT2SS_MCS7:
						case MGN_VHT2SS_MCS6:
						case MGN_VHT1SS_MCS6:
						case MGN_VHT1SS_MCS5:
						case MGN_MCS13:
						case MGN_MCS5:
						case MGN_48M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa630);
							break;
						case MGN_VHT2SS_MCS5:
						case MGN_VHT2SS_MCS4:
						case MGN_VHT1SS_MCS4:
						case MGN_VHT1SS_MCS3:
						case MGN_MCS12:
						case MGN_MCS4:
						case MGN_MCS3:
						case MGN_36M:
						case MGN_24M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa730);
							break;
						case MGN_VHT2SS_MCS3:
						case MGN_VHT2SS_MCS2:
						case MGN_VHT2SS_MCS1:
						case MGN_VHT1SS_MCS2:
						case MGN_VHT1SS_MCS1:
						case MGN_MCS11:
						case MGN_MCS10:
						case MGN_MCS9:
						case MGN_MCS2:
						case MGN_MCS1:
						case MGN_18M:
						case MGN_12M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa830);
							break;
						case MGN_VHT2SS_MCS0:
						case MGN_VHT1SS_MCS0:
						case MGN_MCS0:
						case MGN_MCS8:
						case MGN_9M:
						case MGN_6M:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, 0xa87f);
							break;
						default:
							odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
							break;
						}
					}
				}
			} else
				odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);
		} else
			odm_write_4byte(p_dm_odm, ODM_EDCA_BE_PARAM, EDCA_BE);

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("EDCA Turbo on: EDCA_BE:0x%lx\n", EDCA_BE));

		p_dm_odm->dm_edca_table.is_current_turbo_edca = true;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("EDCA_BE_DL : 0x%lx  EDCA_BE_UL : 0x%lx  EDCA_BE : 0x%lx\n", EDCA_BE_DL, EDCA_BE_UL, EDCA_BE));

	} else {
		/* Turn Off EDCA turbo here. */
		/* Restore original EDCA according to the declaration of AP. */
		if (p_dm_odm->dm_edca_table.is_current_turbo_edca) {
			phydm_set_hw_reg_handler_interface(p_dm_odm, HW_VAR_AC_PARAM, GET_WMM_PARAM_ELE_SINGLE_AC_PARAM(p_sta_qos->wmm_param_ele, AC0_BE));

			p_dm_odm->dm_edca_table.is_current_turbo_edca = false;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Restore EDCA BE: 0x%lx\n", p_dm_odm->WMMEDCA_BE));

		}
	}

}


/* check if edca turbo is disabled */
bool
odm_is_edca_turbo_disable(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PMGNT_INFO			p_mgnt_info = &adapter->MgntInfo;
	u32				iot_peer = p_mgnt_info->iot_peer;

	if (p_dm_odm->is_bt_disable_edca_turbo) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("EdcaTurboDisable for BT!!\n"));
		return true;
	}

	if ((!(p_dm_odm->support_ability & ODM_MAC_EDCA_TURBO)) ||
	    (p_dm_odm->wifi_test & RT_WIFI_LOGO) ||
	    (iot_peer >= HT_IOT_PEER_MAX)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("EdcaTurboDisable\n"));
		return true;
	}


	/* 1. We do not turn on EDCA turbo mode for some AP that has IOT issue */
	/* 2. User may disable EDCA Turbo mode with OID settings. */
	if (p_mgnt_info->iot_action & HT_IOT_ACT_DISABLE_EDCA_TURBO) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("iot_action:EdcaTurboDisable\n"));
		return	true;
	}

	return	false;


}

/* add iot case here: for MP/CE */
void
odm_edca_para_sel_by_iot(
	void		*p_dm_void,
	u32		*EDCA_BE_UL,
	u32		*EDCA_BE_DL
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	u32                         iot_peer = 0;
	u32                         ic_type = p_dm_odm->support_ic_type;
	u8                         wireless_mode = 0xFF;                 /* invalid value */
	u32                         iot_peer_sub_type = 0;

	PMGNT_INFO			p_mgnt_info = &adapter->MgntInfo;
	u8				two_port_status = (u8)TWO_PORT_STATUS__WITHOUT_ANY_ASSOCIATE;

	if (p_dm_odm->p_wireless_mode != NULL)
		wireless_mode = *(p_dm_odm->p_wireless_mode);

	/* ========================================================= */
	/* list paramter for different platform */

	iot_peer = p_mgnt_info->iot_peer;
	iot_peer_sub_type = p_mgnt_info->iot_peer_subtype;
	get_two_port_shared_resource(adapter, TWO_PORT_SHARED_OBJECT__STATUS, NULL, &two_port_status);

	/* ****************************
	 * / IOT case for MP
	 * **************************** */
	if (p_dm_odm->support_interface == ODM_ITRF_PCIE) {
		(*EDCA_BE_UL) = 0x6ea42b;
		(*EDCA_BE_DL) = 0x6ea42b;
	}

	if (two_port_status == TWO_PORT_STATUS__EXTENSION_ONLY) {
		(*EDCA_BE_UL) = 0x5ea42b;/* Parameter suggested by Scott  */ /* edca_setting_UL[ExtAdapter->mgnt_info.iot_peer]; */
		(*EDCA_BE_DL) = 0x5ea42b;/* Parameter suggested by Scott  */ /* edca_setting_DL[ExtAdapter->mgnt_info.iot_peer]; */
	}

#if (INTEL_PROXIMITY_SUPPORT == 1)
	if (p_mgnt_info->intel_class_mode_info.is_enable_ca == true)
		(*EDCA_BE_UL) = (*EDCA_BE_DL) = 0xa44f;
	else
#endif
	{
		if ((p_mgnt_info->iot_action & (HT_IOT_ACT_FORCED_ENABLE_BE_TXOP | HT_IOT_ACT_AMSDU_ENABLE))) {
			/* To check whether we shall force turn on TXOP configuration. */
			if (!((*EDCA_BE_UL) & 0xffff0000))
				(*EDCA_BE_UL) |= 0x005e0000; /* Force TxOP limit to 0x005e for UL. */
			if (!((*EDCA_BE_DL) & 0xffff0000))
				(*EDCA_BE_DL) |= 0x005e0000; /* Force TxOP limit to 0x005e for DL. */
		}

		/* 92D txop can't be set to 0x3e for cisco1250 */
		if ((iot_peer == HT_IOT_PEER_CISCO) && (wireless_mode == ODM_WM_N24G)) {
			(*EDCA_BE_DL) = edca_setting_DL[iot_peer];
			(*EDCA_BE_UL) = edca_setting_UL[iot_peer];
		}
		/* merge from 92s_92c_merge temp brunch v2445    20120215 */
		else if ((iot_peer == HT_IOT_PEER_CISCO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == (ODM_WM_B | ODM_WM_G)) || (wireless_mode == ODM_WM_A) || (wireless_mode == ODM_WM_B)))
			(*EDCA_BE_DL) = edca_setting_dl_g_mode[iot_peer];
		else if ((iot_peer == HT_IOT_PEER_AIRGO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == ODM_WM_A)))
			(*EDCA_BE_DL) = 0xa630;

		else if (iot_peer == HT_IOT_PEER_MARVELL) {
			(*EDCA_BE_DL) = edca_setting_DL[iot_peer];
			(*EDCA_BE_UL) = edca_setting_UL[iot_peer];
		} else if (iot_peer == HT_IOT_PEER_ATHEROS && iot_peer_sub_type != HT_IOT_PEER_TPLINK_AC1750) {
			/* Set DL EDCA for Atheros peer to 0x3ea42b. Suggested by SD3 Wilson for ASUS TP issue. */
			if (wireless_mode == ODM_WM_G)
				(*EDCA_BE_DL) = edca_setting_dl_g_mode[iot_peer];
			else
				(*EDCA_BE_DL) = edca_setting_DL[iot_peer];

			if (ic_type == ODM_RTL8821)
				(*EDCA_BE_DL) = 0x5ea630;

		}
	}

	if ((ic_type == ODM_RTL8812) || (ic_type == ODM_RTL8192E)) {  /* add 8812AU/8812AE */
		(*EDCA_BE_UL) = 0x5ea42b;
		(*EDCA_BE_DL) = 0x5ea42b;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("8812A: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx\n", (*EDCA_BE_UL), (*EDCA_BE_DL)));
	}

	if ((ic_type == ODM_RTL8814A) && (iot_peer == HT_IOT_PEER_REALTEK)) {      /*8814AU and 8814AR*/
		(*EDCA_BE_UL) = 0x5ea42b;
		(*EDCA_BE_DL) = 0xa42b;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("8814A: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx\n", (*EDCA_BE_UL), (*EDCA_BE_DL)));
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Special: EDCA_BE_UL=0x%lx EDCA_BE_DL =0x%lx, iot_peer = %d\n", (*EDCA_BE_UL), (*EDCA_BE_DL), iot_peer));

}


void
odm_edca_choose_traffic_idx(
	void		*p_dm_void,
	u64			cur_tx_bytes,
	u64			cur_rx_bytes,
	bool		is_bias_on_rx,
	bool		*p_is_cur_rdl_state
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (is_bias_on_rx) {

		if (cur_tx_bytes > (cur_rx_bytes * 4)) {
			*p_is_cur_rdl_state = false;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Uplink Traffic\n "));

		} else {
			*p_is_cur_rdl_state = true;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Balance Traffic\n"));

		}
	} else {
		if (cur_rx_bytes > (cur_tx_bytes * 4)) {
			*p_is_cur_rdl_state = true;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Downlink	Traffic\n"));

		} else {
			*p_is_cur_rdl_state = false;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD, ("Balance Traffic\n"));
		}
	}

	return ;
}

#endif
#endif /*PHYDM_SUPPORT_EDCA*/
