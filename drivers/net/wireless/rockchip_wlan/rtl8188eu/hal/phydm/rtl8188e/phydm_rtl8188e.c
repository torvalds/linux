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

#include "../phydm_precomp.h"

#if (RTL8188E_SUPPORT == 1)

void
odm_dig_lower_bound_88e(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
	struct _dynamic_initial_gain_threshold_		*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV) {
		p_dm_dig_table->rx_gain_range_min = (u8) p_dm_dig_table->ant_div_rssi_max;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_dig_lower_bound_88e(): p_dm_dig_table->ant_div_rssi_max=%d\n", p_dm_dig_table->ant_div_rssi_max));
	}
	/* If only one Entry connected */
}

/*=============================================================
*  AntDiv Before Link
===============================================================*/
void
odm_sw_ant_div_reset_before_link(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{

	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;

	p_dm_swat_table->swas_no_link_state = 0;

}


/* 3============================================================
 * 3 Dynamic Primary CCA
 * 3============================================================ */

void
odm_primary_cca_init(
	struct PHY_DM_STRUCT		*p_dm_odm)
{
	struct _dynamic_primary_cca		*primary_cca = &(p_dm_odm->dm_pri_cca);
	primary_cca->dup_rts_flag = 0;
	primary_cca->intf_flag = 0;
	primary_cca->intf_type = 0;
	primary_cca->monitor_flag = 0;
	primary_cca->pri_cca_flag = 0;
}

bool
odm_dynamic_primary_cca_dup_rts(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
	struct _dynamic_primary_cca		*primary_cca = &(p_dm_odm->dm_pri_cca);

	return	primary_cca->dup_rts_flag;
}

void
odm_dynamic_primary_cca(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{

#if (DM_ODM_SUPPORT_TYPE != ODM_CE)

	struct _ADAPTER	*adapter =  p_dm_odm->adapter;	/* for NIC */

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct sta_info	*p_entry;
#endif

	struct _FALSE_ALARM_STATISTICS		*false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	struct _dynamic_primary_cca		*primary_cca = &(p_dm_odm->dm_pri_cca);

	bool		is_40mhz;
	bool		client_40mhz = false, client_tmp = false;      /* connected client BW */
	bool		is_connected = false;		/* connected or not */
	static u8	client_40mhz_pre = 0;
	static u64	last_tx_ok_cnt = 0;
	static u64	last_rx_ok_cnt = 0;
	static u32	counter = 0;
	static u8	delay = 1;
	u64		cur_tx_ok_cnt;
	u64		cur_rx_ok_cnt;
	u8		sec_ch_offset;
	u8		i;

	if (!(p_dm_odm->support_ability & ODM_BB_PRIMARY_CCA))
		return;

	if (p_dm_odm->support_ic_type != ODM_RTL8188E)
		return;

	is_40mhz = *(p_dm_odm->p_band_width);
	sec_ch_offset = *(p_dm_odm->p_sec_ch_offset);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Second CH Offset = %d\n", sec_ch_offset));

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (is_40mhz == 1)
		sec_ch_offset = sec_ch_offset % 2 + 1; /* NIC's definition is reverse to AP   1:secondary below,  2: secondary above */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Second CH Offset = %d\n", sec_ch_offset));
	/* 3 Check Current WLAN Traffic */
	cur_tx_ok_cnt = adapter->TxStats.NumTxBytesUnicast - last_tx_ok_cnt;
	cur_rx_ok_cnt = adapter->RxStats.NumRxBytesUnicast - last_rx_ok_cnt;
	last_tx_ok_cnt = adapter->TxStats.NumTxBytesUnicast;
	last_rx_ok_cnt = adapter->RxStats.NumRxBytesUnicast;
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	/* 3 Check Current WLAN Traffic */
	cur_tx_ok_cnt = *(p_dm_odm->p_num_tx_bytes_unicast) - last_tx_ok_cnt;
	cur_rx_ok_cnt = *(p_dm_odm->p_num_rx_bytes_unicast) - last_rx_ok_cnt;
	last_tx_ok_cnt = *(p_dm_odm->p_num_tx_bytes_unicast);
	last_rx_ok_cnt = *(p_dm_odm->p_num_rx_bytes_unicast);
#endif

	/* ==================Debug Message==================== */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("TP = %llu\n", cur_tx_ok_cnt + cur_rx_ok_cnt));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("is_40mhz = %d\n", is_40mhz));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("BW_LSC = %d\n", false_alm_cnt->cnt_bw_lsc));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("BW_USC = %d\n", false_alm_cnt->cnt_bw_usc));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCA OFDM = %d\n", false_alm_cnt->cnt_ofdm_cca));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCA CCK = %d\n", false_alm_cnt->cnt_cck_cca));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("OFDM FA = %d\n", false_alm_cnt->cnt_ofdm_fail));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCK FA = %d\n", false_alm_cnt->cnt_cck_fail));
	/* ================================================ */

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (ACTING_AS_AP(adapter))   /* primary cca process only do at AP mode */
#endif
	{

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("ACTING as AP mode=%d\n", ACTING_AS_AP(adapter)));
		/* 3 To get entry's connection and BW infomation status. */
		for (i = 0; i < ASSOCIATE_ENTRY_NUM; i++) {
			if (IsAPModeExist(adapter) && GetFirstExtAdapter(adapter) != NULL)
				p_entry = AsocEntry_EnumStation(GetFirstExtAdapter(adapter), i);
			else
				p_entry = AsocEntry_EnumStation(GetDefaultAdapter(adapter), i);
			if (p_entry != NULL) {
				client_tmp = p_entry->BandWidth;   /* client BW */
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Client_BW=%d\n", client_tmp));
				if (client_tmp > client_40mhz)
					client_40mhz = client_tmp;     /* 40M/20M coexist => 40M priority is High */

				if (p_entry->bAssociated) {
					is_connected = true;  /* client is connected or not */
					break;
				}
			} else
				break;
		}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		/* 3 To get entry's connection and BW infomation status. */

		struct sta_info *pstat;

		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
			pstat = p_dm_odm->p_odm_sta_info[i];
			if (IS_STA_VALID(pstat)) {
				client_tmp = pstat->tx_bw;
				if (client_tmp > client_40mhz)
					client_40mhz = client_tmp;     /* 40M/20M coexist => 40M priority is High */

				is_connected = true;
			}
		}
#endif
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("is_connected=%d\n", is_connected));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Is Client 40MHz=%d\n", client_40mhz));
		/* 1 Monitor whether the interference exists or not */
		if (primary_cca->monitor_flag == 1) {
			if (sec_ch_offset == 1) {    /* secondary channel is below the primary channel */
				if ((false_alm_cnt->cnt_ofdm_cca > 500) && (false_alm_cnt->cnt_bw_lsc > false_alm_cnt->cnt_bw_usc + 500)) {
					if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1) {
						primary_cca->intf_type = 1;
						primary_cca->pri_cca_flag = 1;
						odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT7, 2); /* USC MF */
						if (primary_cca->dup_rts_flag == 1)
							primary_cca->dup_rts_flag = 0;
					} else {
						primary_cca->intf_type = 2;
						if (primary_cca->dup_rts_flag == 0)
							primary_cca->dup_rts_flag = 1;
					}

				} else { /* interferecne disappear */
					primary_cca->dup_rts_flag = 0;
					primary_cca->intf_flag = 0;
					primary_cca->intf_type = 0;
				}
			} else if (sec_ch_offset == 2) { /* secondary channel is above the primary channel */
				if ((false_alm_cnt->cnt_ofdm_cca > 500) && (false_alm_cnt->cnt_bw_usc > false_alm_cnt->cnt_bw_lsc + 500)) {
					if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1) {
						primary_cca->intf_type = 1;
						primary_cca->pri_cca_flag = 1;
						odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT7, 1); /* LSC MF */
						if (primary_cca->dup_rts_flag == 1)
							primary_cca->dup_rts_flag = 0;
					} else {
						primary_cca->intf_type = 2;
						if (primary_cca->dup_rts_flag == 0)
							primary_cca->dup_rts_flag = 1;
					}

				} else { /* interferecne disappear */
					primary_cca->dup_rts_flag = 0;
					primary_cca->intf_flag = 0;
					primary_cca->intf_type = 0;
				}


			}
			primary_cca->monitor_flag = 0;
		}

		/* 1 Dynamic Primary CCA Main Function */
		if (primary_cca->monitor_flag == 0) {
			if (is_40mhz) {  		/* if RFBW==40M mode which require to process primary cca */
				/* 2 STA is NOT Connected */
				if (!is_connected) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("STA NOT Connected!!!!\n"));

					if (primary_cca->pri_cca_flag == 1) {	/* reset primary cca when STA is disconnected */
						primary_cca->pri_cca_flag = 0;
						odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 0);
					}
					if (primary_cca->dup_rts_flag == 1)		/* reset Duplicate RTS when STA is disconnected */
						primary_cca->dup_rts_flag = 0;

					if (sec_ch_offset == 1) { /* secondary channel is below the primary channel */
						if ((false_alm_cnt->cnt_ofdm_cca > 800) && (false_alm_cnt->cnt_bw_lsc * 5 > false_alm_cnt->cnt_bw_usc * 9)) {
							primary_cca->intf_flag = 1;    /* secondary channel interference is detected!!! */
							if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
								primary_cca->intf_type = 1;   	/* interference is shift */
							else
								primary_cca->intf_type = 2;   	/* interference is in-band */
						} else {
							primary_cca->intf_flag = 0;
							primary_cca->intf_type = 0;
						}
					} else if (sec_ch_offset == 2) { /* secondary channel is above the primary channel */
						if ((false_alm_cnt->cnt_ofdm_cca > 800) && (false_alm_cnt->cnt_bw_usc * 5 > false_alm_cnt->cnt_bw_lsc * 9)) {
							primary_cca->intf_flag = 1;    /* secondary channel interference is detected!!! */
							if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
								primary_cca->intf_type = 1;   	/* interference is shift */
							else
								primary_cca->intf_type = 2;   	/* interference is in-band */
						} else {
							primary_cca->intf_flag = 0;
							primary_cca->intf_type = 0;
						}
					}
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("primary_cca=%d\n", primary_cca->pri_cca_flag));
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Intf_Type=%d\n", primary_cca->intf_type));
				}
				/* 2 STA is Connected */
				else {
					if (client_40mhz == 0)		/* 3 */ { /* client BW = 20MHz */
						if (primary_cca->pri_cca_flag == 0) {
							primary_cca->pri_cca_flag = 1;
							if (sec_ch_offset == 1)
								odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 2);
							else if (sec_ch_offset == 2)
								odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 1);
						}
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("STA Connected 20M!!! primary_cca=%d\n", primary_cca->pri_cca_flag));
					} else		/* 3 */ { /* client BW = 40MHz */
						if (primary_cca->intf_flag == 1) { /* interference is detected!! */
							if (primary_cca->intf_type == 1) {
								if (primary_cca->pri_cca_flag != 1) {
									primary_cca->pri_cca_flag = 1;
									if (sec_ch_offset == 1)
										odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 2);
									else if (sec_ch_offset == 2)
										odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 1);
								}
							} else if (primary_cca->intf_type == 2) {
								if (primary_cca->dup_rts_flag != 1)
									primary_cca->dup_rts_flag = 1;
							}
						} else { /* if intf_flag==0 */
							if ((cur_tx_ok_cnt + cur_rx_ok_cnt) < 10000) { /* idle mode or TP traffic is very low */
								if (sec_ch_offset == 1) {
									if ((false_alm_cnt->cnt_ofdm_cca > 800) && (false_alm_cnt->cnt_bw_lsc * 5 > false_alm_cnt->cnt_bw_usc * 9)) {
										primary_cca->intf_flag = 1;
										if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
											primary_cca->intf_type = 1;   	/* interference is shift */
										else
											primary_cca->intf_type = 2;   	/* interference is in-band */
									}
								} else if (sec_ch_offset == 2) {
									if ((false_alm_cnt->cnt_ofdm_cca > 800) && (false_alm_cnt->cnt_bw_usc * 5 > false_alm_cnt->cnt_bw_lsc * 9)) {
										primary_cca->intf_flag = 1;
										if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
											primary_cca->intf_type = 1;   	/* interference is shift */
										else
											primary_cca->intf_type = 2;   	/* interference is in-band */
									}

								}
							} else { /* TP Traffic is High */
								if (sec_ch_offset == 1) {
									if (false_alm_cnt->cnt_bw_lsc > (false_alm_cnt->cnt_bw_usc + 500)) {
										if (delay == 0) { /* add delay to avoid interference occurring abruptly, jump one time */
											primary_cca->intf_flag = 1;
											if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
												primary_cca->intf_type = 1;   	/* interference is shift */
											else
												primary_cca->intf_type = 2;   	/* interference is in-band */
											delay = 1;
										} else
											delay = 0;
									}
								} else if (sec_ch_offset == 2) {
									if (false_alm_cnt->cnt_bw_usc > (false_alm_cnt->cnt_bw_lsc + 500)) {
										if (delay == 0) { /* add delay to avoid interference occurring abruptly */
											primary_cca->intf_flag = 1;
											if (false_alm_cnt->cnt_ofdm_fail > false_alm_cnt->cnt_ofdm_cca >> 1)
												primary_cca->intf_type = 1;   	/* interference is shift */
											else
												primary_cca->intf_type = 2;   	/* interference is in-band */
											delay = 1;
										} else
											delay = 0;
									}
								}
							}
						}
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Primary CCA=%d\n", primary_cca->pri_cca_flag));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Duplicate RTS=%d\n", primary_cca->dup_rts_flag));
					}

				} /* end of connected */
			}
		}
		/* 1 Dynamic Primary CCA Monitor counter */
		if ((primary_cca->pri_cca_flag == 1) || (primary_cca->dup_rts_flag == 1)) {
			if (client_40mhz == 0) {  /* client=20M no need to monitor primary cca flag */
				client_40mhz_pre = client_40mhz;
				return;
			}
			counter++;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("counter=%d\n", counter));
			if ((counter == 30) || ((client_40mhz - client_40mhz_pre) == 1)) { /* Every 60 sec to monitor one time */
				primary_cca->monitor_flag = 1;     /* monitor flag is triggered!!!!! */
				if (primary_cca->pri_cca_flag == 1) {
					primary_cca->pri_cca_flag = 0;
					odm_set_bb_reg(p_dm_odm, 0xc6c, BIT(8) | BIT(7), 0);
				}
				counter = 0;
			}
		}
	}

	client_40mhz_pre = client_40mhz;
#endif
}

#endif /* #if (RTL8188E_SUPPORT == 1) */
