/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_DYNAMIC_RX_PATH

void
phydm_process_phy_status_for_dynamic_rx_path(
	void			*p_dm_void,
	void			*p_phy_info_void,
	void			*p_pkt_info_void
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_phyinfo_struct		*p_phy_info = (struct phydm_phyinfo_struct *)p_phy_info_void;
	struct phydm_perpkt_info_struct		*p_pktinfo = (struct phydm_perpkt_info_struct *)p_pkt_info_void;
	struct _DYNAMIC_RX_PATH_					*p_dm_drp_table	= &(p_dm->dm_drp_table);
	/*u8					is_cck_rate=0;*/



}

void
phydm_drp_get_statistic(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DYNAMIC_RX_PATH_						*p_dm_drp_table = &(p_dm->dm_drp_table);
	struct phydm_fa_struct		*false_alm_cnt = (struct phydm_fa_struct *)phydm_get_structure(p_dm, PHYDM_FALSEALMCNT);

	odm_false_alarm_counter_statistics(p_dm);

	PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca, false_alm_cnt->cnt_cca_all));

	PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail, false_alm_cnt->cnt_all));
}

void
phydm_dynamic_rx_path(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DYNAMIC_RX_PATH_					*p_dm_drp_table	= &(p_dm->dm_drp_table);
	u8		training_set_timmer_en;
	u8		curr_drp_state;
	u32		rx_ok_cal;
	u32		RSSI = 0;
	struct phydm_fa_struct		*false_alm_cnt = (struct phydm_fa_struct *)phydm_get_structure(p_dm, PHYDM_FALSEALMCNT);

	if (!(p_dm->support_ability & ODM_BB_DYNAMIC_RX_PATH)) {
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Return Init]   Not Support Dynamic RX PAth\n"));
		return;
	}

	PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("Current drp_state = ((%d))\n", p_dm_drp_table->drp_state));

	curr_drp_state = p_dm_drp_table->drp_state;

	if (p_dm_drp_table->drp_state == DRP_INIT_STATE) {

		phydm_drp_get_statistic(p_dm);

		if (false_alm_cnt->cnt_crc32_ok_all > 20) {
			PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Stop DRP Training] cnt_crc32_ok_all = ((%d))\n", false_alm_cnt->cnt_crc32_ok_all));
			p_dm_drp_table->drp_state  = DRP_INIT_STATE;
			training_set_timmer_en = false;
		} else {
			PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Start DRP Training] cnt_crc32_ok_all = ((%d))\n", false_alm_cnt->cnt_crc32_ok_all));
			p_dm_drp_table->drp_state  = DRP_TRAINING_STATE_0;
			p_dm_drp_table->curr_rx_path = BB_PATH_AB;
			training_set_timmer_en = true;
		}

	} else if (p_dm_drp_table->drp_state == DRP_TRAINING_STATE_0) {

		phydm_drp_get_statistic(p_dm);

		p_dm_drp_table->curr_cca_all_cnt_0 = false_alm_cnt->cnt_cca_all;
		p_dm_drp_table->curr_fa_all_cnt_0 = false_alm_cnt->cnt_all;

		p_dm_drp_table->drp_state  = DRP_TRAINING_STATE_1;
		p_dm_drp_table->curr_rx_path = BB_PATH_B;
		training_set_timmer_en = true;

	} else if (p_dm_drp_table->drp_state == DRP_TRAINING_STATE_1) {

		phydm_drp_get_statistic(p_dm);

		p_dm_drp_table->curr_cca_all_cnt_1 = false_alm_cnt->cnt_cca_all;
		p_dm_drp_table->curr_fa_all_cnt_1 = false_alm_cnt->cnt_all;

#if 1
		p_dm_drp_table->drp_state  = DRP_DECISION_STATE;
#else

		if (*(p_dm->p_mp_mode)) {
			rx_ok_cal = p_dm->phy_dbg_info.num_qry_phy_status_cck + p_dm->phy_dbg_info.num_qry_phy_status_ofdm;
			RSSI = (rx_ok_cal != 0) ? p_dm->rx_pwdb_ave / rx_ok_cal : 0;
			PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("MP RSSI = ((%d))\n", RSSI));
		}

		if (RSSI > p_dm_drp_table->rssi_threshold)

			p_dm_drp_table->drp_state  = DRP_DECISION_STATE;

		else  {

			p_dm_drp_table->drp_state  = DRP_TRAINING_STATE_2;
			p_dm_drp_table->curr_rx_path = BB_PATH_A;
			training_set_timmer_en = true;
		}
#endif
	} else if (p_dm_drp_table->drp_state == DRP_TRAINING_STATE_2) {

		phydm_drp_get_statistic(p_dm);

		p_dm_drp_table->curr_cca_all_cnt_2 = false_alm_cnt->cnt_cca_all;
		p_dm_drp_table->curr_fa_all_cnt_2 = false_alm_cnt->cnt_all;
		p_dm_drp_table->drp_state  = DRP_DECISION_STATE;
	}

	if (p_dm_drp_table->drp_state == DRP_DECISION_STATE) {

		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("Current drp_state = ((%d))\n", p_dm_drp_table->drp_state));

		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[0] {CCA, FA} = {%d, %d}\n", p_dm_drp_table->curr_cca_all_cnt_0, p_dm_drp_table->curr_fa_all_cnt_0));
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[1] {CCA, FA} = {%d, %d}\n", p_dm_drp_table->curr_cca_all_cnt_1, p_dm_drp_table->curr_fa_all_cnt_1));
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[2] {CCA, FA} = {%d, %d}\n", p_dm_drp_table->curr_cca_all_cnt_2, p_dm_drp_table->curr_fa_all_cnt_2));

		if (p_dm_drp_table->curr_fa_all_cnt_1 < p_dm_drp_table->curr_fa_all_cnt_0) {

			if ((p_dm_drp_table->curr_fa_all_cnt_0 - p_dm_drp_table->curr_fa_all_cnt_1) > p_dm_drp_table->fa_diff_threshold)
				p_dm_drp_table->curr_rx_path = BB_PATH_B;
			else
				p_dm_drp_table->curr_rx_path = BB_PATH_AB;
		} else
			p_dm_drp_table->curr_rx_path = BB_PATH_AB;

		phydm_config_ofdm_rx_path(p_dm, p_dm_drp_table->curr_rx_path);
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Training Result]  curr_rx_path = ((%s%s)),\n",
			((p_dm_drp_table->curr_rx_path & BB_PATH_A)  ? "A"  : " "), ((p_dm_drp_table->curr_rx_path & BB_PATH_B)  ? "B"  : " ")));

		p_dm_drp_table->drp_state = DRP_INIT_STATE;
		training_set_timmer_en = false;
	}

	PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("DRP_state: ((%d)) -> ((%d))\n", curr_drp_state, p_dm_drp_table->drp_state));

	if (training_set_timmer_en) {

		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Training en]  curr_rx_path = ((%s%s)), training_time = ((%d ms))\n",
			((p_dm_drp_table->curr_rx_path & BB_PATH_A)  ? "A"  : " "), ((p_dm_drp_table->curr_rx_path & BB_PATH_B)  ? "B"  : " "), p_dm_drp_table->training_time));

		phydm_config_ofdm_rx_path(p_dm, p_dm_drp_table->curr_rx_path);
		odm_set_timer(p_dm, &(p_dm_drp_table->phydm_dynamic_rx_path_timer), p_dm_drp_table->training_time); /*ms*/
	} else
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("DRP period end\n\n", curr_drp_state, p_dm_drp_table->drp_state));

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
phydm_dynamic_rx_path_callback(
	struct timer_list		*p_timer
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_timer->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &(p_hal_data->DM_OutSrc);
	struct _DYNAMIC_RX_PATH_			*p_dm_drp_table = &(p_dm->dm_drp_table);

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&(p_dm_drp_table->phydm_dynamic_rx_path_workitem));
#else
	{
		/* dbg_print("phydm_dynamic_rx_path\n"); */
		phydm_dynamic_rx_path(p_dm);
	}
#endif
#else
	odm_schedule_work_item(&(p_dm_drp_table->phydm_dynamic_rx_path_workitem));
#endif
}

void
phydm_dynamic_rx_path_workitem_callback(
	void		*p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm = &(p_hal_data->DM_OutSrc);

	/* dbg_print("phydm_dynamic_rx_path\n"); */
	phydm_dynamic_rx_path(p_dm);
}
#else if (DM_ODM_SUPPORT_TYPE == ODM_CE)

void
phydm_dynamic_rx_path_callback(
	void *function_context
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)function_context;
	struct _ADAPTER	*padapter = p_dm->adapter;

	if (padapter->net_closed == true)
		return;

#if 0 /* Can't do I/O in timer callback*/
	odm_s0s1_sw_ant_div(p_dm, SWAW_STEP_DETERMINE);
#else
	/*rtw_run_in_thread_cmd(padapter, odm_sw_antdiv_workitem_callback, padapter);*/
#endif
}

#endif

void
phydm_dynamic_rx_path_timers(
	void		*p_dm_void,
	u8		state
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DYNAMIC_RX_PATH_			*p_dm_drp_table	= &(p_dm->dm_drp_table);

	if (state == INIT_DRP_TIMMER) {

		odm_initialize_timer(p_dm, &(p_dm_drp_table->phydm_dynamic_rx_path_timer),
			(void *)phydm_dynamic_rx_path_callback, NULL, "phydm_sw_antenna_switch_timer");
	} else if (state == CANCEL_DRP_TIMMER)

		odm_cancel_timer(p_dm, &(p_dm_drp_table->phydm_dynamic_rx_path_timer));

	else if (state == RELEASE_DRP_TIMMER)

		odm_release_timer(p_dm, &(p_dm_drp_table->phydm_dynamic_rx_path_timer));

}

void
phydm_dynamic_rx_path_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DYNAMIC_RX_PATH_					*p_dm_drp_table	= &(p_dm->dm_drp_table);
	boolean			ret_value;

	if (!(p_dm->support_ability & ODM_BB_DYNAMIC_RX_PATH)) {
		PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("[Return]   Not Support Dynamic RX PAth\n"));
		return;
	}
	PHYDM_DBG(p_dm, DBG_DYN_RX_PATH, ("phydm_dynamic_rx_path_init\n"));

	p_dm_drp_table->drp_state = DRP_INIT_STATE;
	p_dm_drp_table->rssi_threshold = DRP_RSSI_TH;
	p_dm_drp_table->fa_count_thresold = 50;
	p_dm_drp_table->fa_diff_threshold = 50;
	p_dm_drp_table->training_time = 100; /*ms*/
	p_dm_drp_table->drp_skip_counter = 0;
	p_dm_drp_table->drp_period  = 0;
	p_dm_drp_table->drp_init_finished = true;

	ret_value = phydm_api_trx_mode(p_dm, (enum bb_path)BB_PATH_AB, (enum bb_path)BB_PATH_AB, true);

}

void
phydm_drp_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;
	struct _DYNAMIC_RX_PATH_			*p_dm_drp_table = &(p_dm->dm_drp_table);

	switch (dm_value[0])	{

	case DRP_TRAINING_TIME:
		p_dm_drp_table->training_time = (u16)dm_value[1];
		break;
	case DRP_TRAINING_PERIOD:
		p_dm_drp_table->drp_period = (u8)dm_value[1];
		break;
	case DRP_RSSI_THRESHOLD:
		p_dm_drp_table->rssi_threshold = (u8)dm_value[1];
		break;
	case DRP_FA_THRESHOLD:
		p_dm_drp_table->fa_count_thresold = dm_value[1];
		break;
	case DRP_FA_DIFF_THRESHOLD:
		p_dm_drp_table->fa_diff_threshold = dm_value[1];
		break;
	default:
		PHYDM_SNPRINTF((output + used, out_len - used, "[DRP] unknown command\n"));
		break;
	}

	*_used = used;
	*_out_len = out_len;
}

void
phydm_dynamic_rx_path_caller(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DYNAMIC_RX_PATH_			*p_dm_drp_table	= &(p_dm->dm_drp_table);

	if (p_dm_drp_table->drp_skip_counter <  p_dm_drp_table->drp_period)
		p_dm_drp_table->drp_skip_counter++;
	else
		p_dm_drp_table->drp_skip_counter = 0;

	if (p_dm_drp_table->drp_skip_counter != 0)
		return;

	if (p_dm_drp_table->drp_init_finished != true)
		return;

	phydm_dynamic_rx_path(p_dm);

}
#endif
