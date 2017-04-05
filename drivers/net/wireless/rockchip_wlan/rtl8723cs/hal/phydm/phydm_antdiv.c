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

/* ******************************************************
 * when antenna test utility is on or some testing need to disable antenna diversity
 * call this function to disable all ODM related mechanisms which will switch antenna.
 * ****************************************************** */
void
odm_stop_antenna_switch_dm(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	/* disable ODM antenna diversity */
	p_dm_odm->support_ability &= ~ODM_BB_ANT_DIV;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("STOP Antenna Diversity\n"));
}

void
phydm_enable_antenna_diversity(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	p_dm_odm->support_ability |= ODM_BB_ANT_DIV;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("AntDiv is enabled & Re-Init AntDiv\n"));
	odm_antenna_diversity_init(p_dm_odm);
}

void
odm_set_ant_config(
	void	*p_dm_void,
	u8		ant_setting	/* 0=A, 1=B, 2=C, .... */
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
		if (ant_setting == 0)		/* ant A*/
			odm_set_bb_reg(p_dm_odm, 0x948, MASKDWORD, 0x00000000);
		else if (ant_setting == 1)
			odm_set_bb_reg(p_dm_odm, 0x948, MASKDWORD, 0x00000280);
	} else if (p_dm_odm->support_ic_type == ODM_RTL8723D) {
		if (ant_setting == 0)		/* ant A*/
			odm_set_bb_reg(p_dm_odm, 0x948, MASKLWORD, 0x0000);
		else if (ant_setting == 1)
			odm_set_bb_reg(p_dm_odm, 0x948, MASKLWORD, 0x0280);
	}
}

/* ****************************************************** */


void
odm_sw_ant_div_rest_after_link(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u32             i;

	if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {

		p_dm_swat_table->try_flag = SWAW_STEP_INIT;
		p_dm_swat_table->rssi_trying = 0;
		p_dm_swat_table->double_chk_flag = 0;

		p_dm_fat_table->rx_idle_ant = MAIN_ANT;

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
			phydm_antdiv_reset_statistic(p_dm_odm, i);
#endif


	}
}


#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
void
phydm_antdiv_reset_statistic(
	void	*p_dm_void,
	u32	macid
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	p_dm_fat_table->main_ant_sum[macid] = 0;
	p_dm_fat_table->aux_ant_sum[macid] = 0;
	p_dm_fat_table->main_ant_cnt[macid] = 0;
	p_dm_fat_table->aux_ant_cnt[macid] = 0;
	p_dm_fat_table->main_ant_sum_cck[macid] = 0;
	p_dm_fat_table->aux_ant_sum_cck[macid] = 0;
	p_dm_fat_table->main_ant_cnt_cck[macid] = 0;
	p_dm_fat_table->aux_ant_cnt_cck[macid] = 0;
}

void
odm_ant_div_on_off(
	void		*p_dm_void,
	u8		swch
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	if (p_dm_fat_table->ant_div_on_off != swch) {
		if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV)
			return;

		if (p_dm_odm->support_ic_type & ODM_N_ANTDIV_SUPPORT) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) N-Series HW-AntDiv block\n", (swch == ANTDIV_ON) ? "ON" : "OFF"));
			odm_set_bb_reg(p_dm_odm, 0xc50, BIT(7), swch);
			odm_set_bb_reg(p_dm_odm, 0xa00, BIT(15), swch);

		} else if (p_dm_odm->support_ic_type & ODM_AC_ANTDIV_SUPPORT) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) AC-Series HW-AntDiv block\n", (swch == ANTDIV_ON) ? "ON" : "OFF"));
			if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B)) {
				odm_set_bb_reg(p_dm_odm, 0xc50, BIT(7), swch); /* OFDM AntDiv function block enable */
				odm_set_bb_reg(p_dm_odm, 0xa00, BIT(15), swch); /* CCK AntDiv function block enable */
			} else {
				odm_set_bb_reg(p_dm_odm, 0x8D4, BIT(24), swch); /* OFDM AntDiv function block enable */

				if ((p_dm_odm->cut_version >= ODM_CUT_C) && (p_dm_odm->support_ic_type == ODM_RTL8821) && (p_dm_odm->ant_div_type != S0S1_SW_ANTDIV)) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) CCK HW-AntDiv block\n", (swch == ANTDIV_ON) ? "ON" : "OFF"));
					odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), swch);
					odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), swch); /* CCK AntDiv function block enable */
				} else if (p_dm_odm->support_ic_type == ODM_RTL8821C) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) CCK HW-AntDiv block\n", (swch == ANTDIV_ON) ? "ON" : "OFF"));
					odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), swch);
					odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), swch); /* CCK AntDiv function block enable */
				}
			}
		}
	}
	p_dm_fat_table->ant_div_on_off = swch;

}

void
phydm_fast_training_enable(
	void		*p_dm_void,
	u8			swch
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			enable;

	if (swch == FAT_ON)
		enable = 1;
	else
		enable = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Fast ant Training_en = ((%d))\n", enable));

	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
		odm_set_bb_reg(p_dm_odm, 0xe08, BIT(16), enable);	/*enable fast training*/
		/**/
	} else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(p_dm_odm, 0xB34, BIT(28), enable);	/*enable fast training (path-A)*/
		/*odm_set_bb_reg(p_dm_odm, 0xB34, BIT(29), enable);*/	/*enable fast training (path-B)*/
	} else if (p_dm_odm->support_ic_type & (ODM_RTL8821 | ODM_RTL8822B)) {
		odm_set_bb_reg(p_dm_odm, 0x900, BIT(19), enable);	/*enable fast training */
		/**/
	}
}

void
phydm_keep_rx_ack_ant_by_tx_ant_time(
	void		*p_dm_void,
	u32		time
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	/* Timming issue: keep Rx ant after tx for ACK ( time x 3.2 mu sec)*/
	if (p_dm_odm->support_ic_type & ODM_N_ANTDIV_SUPPORT) {

		odm_set_bb_reg(p_dm_odm, 0xE20, BIT(23) | BIT(22) | BIT(21) | BIT(20), time);
		/**/
	} else if (p_dm_odm->support_ic_type & ODM_AC_ANTDIV_SUPPORT) {

		odm_set_bb_reg(p_dm_odm, 0x818, BIT(23) | BIT(22) | BIT(21) | BIT(20), time);
		/**/
	}
}

void
odm_tx_by_tx_desc_or_reg(
	void		*p_dm_void,
	u8			swch
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u8 enable;

	if (p_dm_fat_table->b_fix_tx_ant == NO_FIX_TX_ANT)
		enable = (swch == TX_BY_DESC) ? 1 : 0;
	else
		enable = 0;/*Force TX by Reg*/

	if (p_dm_odm->ant_div_type != CGCS_RX_HW_ANTDIV) {
		if (p_dm_odm->support_ic_type & ODM_N_ANTDIV_SUPPORT)
			odm_set_bb_reg(p_dm_odm, 0x80c, BIT(21), enable);
		else if (p_dm_odm->support_ic_type & ODM_AC_ANTDIV_SUPPORT)
			odm_set_bb_reg(p_dm_odm, 0x900, BIT(18), enable);

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[AntDiv] TX_Ant_BY (( %s ))\n", (enable == TX_BY_DESC) ? "DESC" : "REG"));
	}
}

void
odm_update_rx_idle_ant(
	void		*p_dm_void,
	u8		ant
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u32			default_ant, optional_ant, value32, default_tx_ant;

	if (p_dm_fat_table->rx_idle_ant != ant) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] rx_idle_ant =%s\n", (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));

		if (!(p_dm_odm->support_ic_type & ODM_RTL8723B))
			p_dm_fat_table->rx_idle_ant = ant;

		if (ant == MAIN_ANT) {
			default_ant   =  ANT1_2G;
			optional_ant =  ANT2_2G;
		} else {
			default_ant  =   ANT2_2G;
			optional_ant =  ANT1_2G;
		}

		if (p_dm_fat_table->b_fix_tx_ant != NO_FIX_TX_ANT)
			default_tx_ant = (p_dm_fat_table->b_fix_tx_ant == FIX_TX_AT_MAIN) ? 0 : 1;
		else
			default_tx_ant = default_ant;

		if (p_dm_odm->support_ic_type & ODM_N_ANTDIV_SUPPORT) {
			if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
				odm_set_bb_reg(p_dm_odm, 0xB38, BIT(5) | BIT4 | BIT3, default_ant); /* Default RX */
				odm_set_bb_reg(p_dm_odm, 0xB38, BIT(8) | BIT7 | BIT6, optional_ant); /* Optional RX */
				odm_set_bb_reg(p_dm_odm, 0x860, BIT(14) | BIT13 | BIT12, default_ant); /* Default TX */
			}
#if (RTL8723B_SUPPORT == 1)
			else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {

				value32 = odm_get_bb_reg(p_dm_odm, 0x948, 0xFFF);

				if (value32 != 0x280)
					odm_update_rx_idle_ant_8723b(p_dm_odm, ant, default_ant, optional_ant);
				else
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to 0x948 = 0x280\n"));
			}
#endif
			else { /*8188E & 8188F*/

				if (p_dm_odm->support_ic_type == ODM_RTL8723D) {
#if (RTL8723D_SUPPORT == 1)
					phydm_set_tx_ant_pwr_8723d(p_dm_odm, ant);
#endif
				}
#if (RTL8188F_SUPPORT == 1)
				else if (p_dm_odm->support_ic_type == ODM_RTL8188F) {
					phydm_update_rx_idle_antenna_8188F(p_dm_odm, default_ant);
					/**/
				}
#endif

				odm_set_bb_reg(p_dm_odm, 0x864, BIT(5) | BIT4 | BIT3, default_ant);		/*Default RX*/
				odm_set_bb_reg(p_dm_odm, 0x864, BIT(8) | BIT7 | BIT6, optional_ant);	/*Optional RX*/
				odm_set_bb_reg(p_dm_odm, 0x860, BIT(14) | BIT13 | BIT12, default_tx_ant);	/*Default TX*/
			}
		} else if (p_dm_odm->support_ic_type & ODM_AC_ANTDIV_SUPPORT) {
			u16	value16 = odm_read_2byte(p_dm_odm, ODM_REG_TRMUX_11AC + 2);
			/*  */
			/* 2014/01/14 MH/Luke.Lee Add direct write for register 0xc0a to prevnt */
			/* incorrect 0xc08 bit0-15 .We still not know why it is changed. */
			/*  */
			value16 &= ~(BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3));
			value16 |= ((u16)default_ant << 3);
			value16 |= ((u16)optional_ant << 6);
			value16 |= ((u16)default_ant << 9);
			odm_write_2byte(p_dm_odm, ODM_REG_TRMUX_11AC + 2, value16);
#if 0
			odm_set_bb_reg(p_dm_odm, ODM_REG_TRMUX_11AC, BIT(21) | BIT20 | BIT19, default_ant);	 /* Default RX */
			odm_set_bb_reg(p_dm_odm, ODM_REG_TRMUX_11AC, BIT(24) | BIT23 | BIT22, optional_ant); /* Optional RX */
			odm_set_bb_reg(p_dm_odm, ODM_REG_TRMUX_11AC, BIT(27) | BIT26 | BIT25, default_ant);	 /* Default TX */
#endif
		}

		if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
			odm_set_mac_reg(p_dm_odm, 0x6D8, BIT(7) | BIT6, default_tx_ant);		/*PathA Resp Tx*/
			/**/
		} else {
			odm_set_mac_reg(p_dm_odm, 0x6D8, BIT(10) | BIT9 | BIT8, default_tx_ant);	/*PathA Resp Tx*/
			/**/
		}

	} else { /* p_dm_fat_table->rx_idle_ant == ant */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Stay in Ori-ant ]  rx_idle_ant =%s\n", (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));
		p_dm_fat_table->rx_idle_ant = ant;
	}
}

void
odm_update_tx_ant(
	void		*p_dm_void,
	u8		ant,
	u32		mac_id
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u8	tx_ant;

	if (p_dm_fat_table->b_fix_tx_ant != NO_FIX_TX_ANT)
		ant = (p_dm_fat_table->b_fix_tx_ant == FIX_TX_AT_MAIN) ? MAIN_ANT : AUX_ANT;

	if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV)
		tx_ant = ant;
	else {
		if (ant == MAIN_ANT)
			tx_ant = ANT1_2G;
		else
			tx_ant = ANT2_2G;
	}

	p_dm_fat_table->antsel_a[mac_id] = tx_ant & BIT(0);
	p_dm_fat_table->antsel_b[mac_id] = (tx_ant & BIT(1)) >> 1;
	p_dm_fat_table->antsel_c[mac_id] = (tx_ant & BIT(2)) >> 2;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Set TX-DESC value]: mac_id:(( %d )),  tx_ant = (( %s ))\n", mac_id, (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));
	/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=(( 3'b%d%d%d ))\n",p_dm_fat_table->antsel_c[mac_id] , p_dm_fat_table->antsel_b[mac_id] , p_dm_fat_table->antsel_a[mac_id] )); */

}

#ifdef BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

void
odm_bdc_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _BF_DIV_COEX_	*p_dm_bdc_table = &p_dm_odm->dm_bdc_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n[ BDC Initialization......]\n"));
	p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
	p_dm_bdc_table->bdc_mode = BDC_MODE_NULL;
	p_dm_bdc_table->bdc_try_flag = 0;
	p_dm_bdc_table->bd_ccoex_type_wbfer = 0;
	p_dm_odm->bdc_holdstate = 0xff;

	if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(p_dm_odm, 0xd7c, 0x0FFFFFFF, 0x1081008);
		odm_set_bb_reg(p_dm_odm, 0xd80, 0x0FFFFFFF, 0);
	} else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		odm_set_bb_reg(p_dm_odm, 0x9b0, 0x0FFFFFFF, 0x1081008);     /* 0x9b0[30:0] = 01081008 */
		odm_set_bb_reg(p_dm_odm, 0x9b4, 0x0FFFFFFF, 0);                 /* 0x9b4[31:0] = 00000000 */
	}

}


void
odm_CSI_on_off(
	void		*p_dm_void,
	u8			CSI_en
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (CSI_en == CSI_ON) {
		if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
			odm_set_mac_reg(p_dm_odm, 0xd84, BIT(11), 1);  /* 0xd84[11]=1 */
		} else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
			odm_set_mac_reg(p_dm_odm, 0x9b0, BIT(31), 1);  /* 0x9b0[31]=1 */
		}

	} else if (CSI_en == CSI_OFF) {
		if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
			odm_set_mac_reg(p_dm_odm, 0xd84, BIT(11), 0);  /* 0xd84[11]=0 */
		} else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
			odm_set_mac_reg(p_dm_odm, 0x9b0, BIT(31), 0);  /* 0x9b0[31]=0 */
		}
	}
}

void
odm_bd_ccoex_type_with_bfer_client(
	void		*p_dm_void,
	u8			swch
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _BF_DIV_COEX_	*p_dm_bdc_table = &p_dm_odm->dm_bdc_table;
	u8     bd_ccoex_type_wbfer;

	if (swch == DIVON_CSIOFF) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[BDCcoexType: 1] {DIV,CSI} ={1,0}\n"));
		bd_ccoex_type_wbfer = 1;

		if (bd_ccoex_type_wbfer != p_dm_bdc_table->bd_ccoex_type_wbfer) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
			odm_CSI_on_off(p_dm_odm, CSI_OFF);
			p_dm_bdc_table->bd_ccoex_type_wbfer = 1;
		}
	} else if (swch == DIVOFF_CSION) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[BDCcoexType: 2] {DIV,CSI} ={0,1}\n"));
		bd_ccoex_type_wbfer = 2;

		if (bd_ccoex_type_wbfer != p_dm_bdc_table->bd_ccoex_type_wbfer) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
			odm_CSI_on_off(p_dm_odm, CSI_ON);
			p_dm_bdc_table->bd_ccoex_type_wbfer = 2;
		}
	}
}

void
odm_bf_ant_div_mode_arbitration(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _BF_DIV_COEX_			*p_dm_bdc_table = &p_dm_odm->dm_bdc_table;
	u8			current_bdc_mode;

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n"));

	/* 2 mode 1 */
	if ((p_dm_bdc_table->num_txbfee_client != 0) && (p_dm_bdc_table->num_txbfer_client == 0)) {
		current_bdc_mode = BDC_MODE_1;

		if (current_bdc_mode != p_dm_bdc_table->bdc_mode) {
			p_dm_bdc_table->bdc_mode = BDC_MODE_1;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
			p_dm_bdc_table->bdc_rx_idle_update_counter = 1;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Change to (( Mode1 ))\n"));
		}

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Antdiv + BF coextance mode] : (( Mode1 ))\n"));
	}
	/* 2 mode 2 */
	else if ((p_dm_bdc_table->num_txbfee_client == 0) && (p_dm_bdc_table->num_txbfer_client != 0)) {
		current_bdc_mode = BDC_MODE_2;

		if (current_bdc_mode != p_dm_bdc_table->bdc_mode) {
			p_dm_bdc_table->bdc_mode = BDC_MODE_2;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			p_dm_bdc_table->bdc_try_flag = 0;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Change to (( Mode2 ))\n"));

		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Antdiv + BF coextance mode] : (( Mode2 ))\n"));
	}
	/* 2 mode 3 */
	else if ((p_dm_bdc_table->num_txbfee_client != 0) && (p_dm_bdc_table->num_txbfer_client != 0)) {
		current_bdc_mode = BDC_MODE_3;

		if (current_bdc_mode != p_dm_bdc_table->bdc_mode) {
			p_dm_bdc_table->bdc_mode = BDC_MODE_3;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->bdc_rx_idle_update_counter = 1;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Change to (( Mode3 ))\n"));
		}

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Antdiv + BF coextance mode] : (( Mode3 ))\n"));
	}
	/* 2 mode 4 */
	else if ((p_dm_bdc_table->num_txbfee_client == 0) && (p_dm_bdc_table->num_txbfer_client == 0)) {
		current_bdc_mode = BDC_MODE_4;

		if (current_bdc_mode != p_dm_bdc_table->bdc_mode) {
			p_dm_bdc_table->bdc_mode = BDC_MODE_4;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Change to (( Mode4 ))\n"));
		}

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Antdiv + BF coextance mode] : (( Mode4 ))\n"));
	}
#endif

}

void
odm_div_train_state_setting(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _BF_DIV_COEX_	*p_dm_bdc_table = &p_dm_odm->dm_bdc_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n*****[S T A R T ]*****  [2-0. DIV_TRAIN_STATE]\n"));
	p_dm_bdc_table->bdc_try_counter = 2;
	p_dm_bdc_table->bdc_try_flag = 1;
	p_dm_bdc_table->BDC_state = bdc_bfer_train_state;
	odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
}

void
odm_bd_ccoex_bfee_rx_div_arbitration(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _BF_DIV_COEX_    *p_dm_bdc_table = &p_dm_odm->dm_bdc_table;
	boolean stop_bf_flag;
	u8	bdc_active_mode;


#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***{ num_BFee,  num_BFer, num_client}  = (( %d  ,  %d  ,  %d))\n", p_dm_bdc_table->num_txbfee_client, p_dm_bdc_table->num_txbfer_client, p_dm_bdc_table->num_client));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***{ num_BF_tars,  num_DIV_tars }  = ((  %d  ,  %d ))\n", p_dm_bdc_table->num_bf_tar, p_dm_bdc_table->num_div_tar));

	/* 2 [ MIB control ] */
	if (p_dm_odm->bdc_holdstate == 2) {
		odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
		p_dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Force in [ BF STATE]\n"));
		return;
	} else if (p_dm_odm->bdc_holdstate == 1) {
		p_dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
		odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Force in [ DIV STATE]\n"));
		return;
	}

	/* ------------------------------------------------------------ */



	/* 2 mode 2 & 3 */
	if (p_dm_bdc_table->bdc_mode == BDC_MODE_2 || p_dm_bdc_table->bdc_mode == BDC_MODE_3) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n{ Try_flag,  Try_counter } = {  %d , %d  }\n", p_dm_bdc_table->bdc_try_flag, p_dm_bdc_table->bdc_try_counter));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BDCcoexType = (( %d ))  \n\n", p_dm_bdc_table->bd_ccoex_type_wbfer));

		/* All Client have Bfer-Cap------------------------------- */
		if (p_dm_bdc_table->num_txbfer_client == p_dm_bdc_table->num_client) { /* BFer STA Only?: yes */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BFer STA only?  (( Yes ))\n"));
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
			return;
		} else
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BFer STA only?  (( No ))\n"));
		/*  */
		if (p_dm_bdc_table->is_all_bf_sta_idle == false && p_dm_bdc_table->is_all_div_sta_idle == true) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("All DIV-STA are idle, but BF-STA not\n"));
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->BDC_state = bdc_bfer_train_state;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
			return;
		} else if (p_dm_bdc_table->is_all_bf_sta_idle == true && p_dm_bdc_table->is_all_div_sta_idle == false) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("All BF-STA are idle, but DIV-STA not\n"));
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
			return;
		}

		/* Select active mode-------------------------------------- */
		if (p_dm_bdc_table->num_bf_tar == 0) { /* Selsect_1,  Selsect_2 */
			if (p_dm_bdc_table->num_div_tar == 0) { /* Selsect_3 */
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Select active mode (( 1 ))\n"));
				p_dm_bdc_table->bdc_active_mode = 1;
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Select active mode  (( 2 ))\n"));
				p_dm_bdc_table->bdc_active_mode = 2;
			}
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
			return;
		} else { /* num_bf_tar > 0 */
			if (p_dm_bdc_table->num_div_tar == 0) { /* Selsect_3 */
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Select active mode (( 3 ))\n"));
				p_dm_bdc_table->bdc_active_mode = 3;
				p_dm_bdc_table->bdc_try_flag = 0;
				p_dm_bdc_table->BDC_state = bdc_bfer_train_state;
				odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
				return;
			} else { /* Selsect_4 */
				bdc_active_mode = 4;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Select active mode (( 4 ))\n"));

				if (bdc_active_mode != p_dm_bdc_table->bdc_active_mode) {
					p_dm_bdc_table->bdc_active_mode = 4;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Change to active mode (( 4 ))  &  return!!!\n"));
					return;
				}
			}
		}

#if 1
		if (p_dm_odm->bdc_holdstate == 0xff) {
			p_dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
			odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Force in [ DIV STATE]\n"));
			return;
		}
#endif

		/* Does Client number changed ? ------------------------------- */
		if (p_dm_bdc_table->num_client != p_dm_bdc_table->pre_num_client) {
			p_dm_bdc_table->bdc_try_flag = 0;
			p_dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[  The number of client has been changed !!!]   return to (( BDC_DIV_TRAIN_STATE ))\n"));
		}
		p_dm_bdc_table->pre_num_client = p_dm_bdc_table->num_client;

		if (p_dm_bdc_table->bdc_try_flag == 0) {
			/* 2 DIV_TRAIN_STATE (mode 2-0) */
			if (p_dm_bdc_table->BDC_state == BDC_DIV_TRAIN_STATE)
				odm_div_train_state_setting(p_dm_odm);
			/* 2 BFer_TRAIN_STATE (mode 2-1) */
			else if (p_dm_bdc_table->BDC_state == bdc_bfer_train_state) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*****[2-1. BFer_TRAIN_STATE ]*****\n"));

				/* if(p_dm_bdc_table->num_bf_tar==0) */
				/* { */
				/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BF_tars exist?  : (( No )),   [ bdc_bfer_train_state ] >> [BDC_DIV_TRAIN_STATE]\n")); */
				/*	odm_div_train_state_setting( p_dm_odm); */
				/* } */
				/* else */ /* num_bf_tar != 0 */
				/* { */
				p_dm_bdc_table->bdc_try_counter = 2;
				p_dm_bdc_table->bdc_try_flag = 1;
				p_dm_bdc_table->BDC_state = BDC_DECISION_STATE;
				odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BF_tars exist?  : (( Yes )),   [ bdc_bfer_train_state ] >> [BDC_DECISION_STATE]\n"));
				/* } */
			}
			/* 2 DECISION_STATE (mode 2-2) */
			else if (p_dm_bdc_table->BDC_state == BDC_DECISION_STATE) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*****[2-2. DECISION_STATE]*****\n"));
				/* if(p_dm_bdc_table->num_bf_tar==0) */
				/* { */
				/*	ODM_AntDiv_Printk(("BF_tars exist?  : (( No )),   [ DECISION_STATE ] >> [BDC_DIV_TRAIN_STATE]\n")); */
				/*	odm_div_train_state_setting( p_dm_odm); */
				/* } */
				/* else */ /* num_bf_tar != 0 */
				/* { */
				if (p_dm_bdc_table->BF_pass == false || p_dm_bdc_table->DIV_pass == false)
					stop_bf_flag = true;
				else
					stop_bf_flag = false;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BF_tars exist?  : (( Yes )),  {BF_pass, DIV_pass, stop_bf_flag }  = { %d, %d, %d }\n", p_dm_bdc_table->BF_pass, p_dm_bdc_table->DIV_pass, stop_bf_flag));

				if (stop_bf_flag == true) { /* DIV_en */
					p_dm_bdc_table->bdc_hold_counter = 10; /* 20 */
					odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
					p_dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ stop_bf_flag= ((true)),   BDC_DECISION_STATE ] >> [BDC_DIV_HOLD_STATE]\n"));
				} else { /* BF_en */
					p_dm_bdc_table->bdc_hold_counter = 10; /* 20 */
					odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
					p_dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[stop_bf_flag= ((false)),   BDC_DECISION_STATE ] >> [BDC_BF_HOLD_STATE]\n"));
				}
				/* } */
			}
			/* 2 BF-HOLD_STATE (mode 2-3) */
			else if (p_dm_bdc_table->BDC_state == BDC_BF_HOLD_STATE) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*****[2-3. BF_HOLD_STATE ]*****\n"));

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("bdc_hold_counter = (( %d ))\n", p_dm_bdc_table->bdc_hold_counter));

				if (p_dm_bdc_table->bdc_hold_counter == 1) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ BDC_BF_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n"));
					odm_div_train_state_setting(p_dm_odm);
				} else {
					p_dm_bdc_table->bdc_hold_counter--;

					/* if(p_dm_bdc_table->num_bf_tar==0) */
					/* { */
					/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BF_tars exist?  : (( No )),   [ BDC_BF_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n")); */
					/*	odm_div_train_state_setting( p_dm_odm); */
					/* } */
					/* else */ /* num_bf_tar != 0 */
					/* { */
					/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("BF_tars exist?  : (( Yes ))\n")); */
					p_dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
					odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVOFF_CSION);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ BDC_BF_HOLD_STATE ] >> [BDC_BF_HOLD_STATE]\n"));
					/* } */
				}

			}
			/* 2 DIV-HOLD_STATE (mode 2-4) */
			else if (p_dm_bdc_table->BDC_state == BDC_DIV_HOLD_STATE) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*****[2-4. DIV_HOLD_STATE ]*****\n"));

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("bdc_hold_counter = (( %d ))\n", p_dm_bdc_table->bdc_hold_counter));

				if (p_dm_bdc_table->bdc_hold_counter == 1) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ BDC_DIV_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n"));
					odm_div_train_state_setting(p_dm_odm);
				} else {
					p_dm_bdc_table->bdc_hold_counter--;
					p_dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
					odm_bd_ccoex_type_with_bfer_client(p_dm_odm, DIVON_CSIOFF);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ BDC_DIV_HOLD_STATE ] >> [BDC_DIV_HOLD_STATE]\n"));
				}

			}

		} else if (p_dm_bdc_table->bdc_try_flag == 1) {
			/* 2 Set Training counter */
			if (p_dm_bdc_table->bdc_try_counter > 1) {
				p_dm_bdc_table->bdc_try_counter--;
				if (p_dm_bdc_table->bdc_try_counter == 1)
					p_dm_bdc_table->bdc_try_flag = 0;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training !!\n"));
				/* return ; */
			}

		}

	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n[end]\n"));

#endif /* #if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */






}

#endif
#endif /* #ifdef BEAMFORMING_SUPPORT */


#if (RTL8188E_SUPPORT == 1)


void
odm_rx_hw_ant_div_init_88e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	value32;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

#if 0
	if (p_dm_odm->mp_mode == true) {
		odm_set_bb_reg(p_dm_odm, ODM_REG_IGI_A_11N, BIT(7), 0); /* disable HW AntDiv */
		odm_set_bb_reg(p_dm_odm, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);  /* 1:CG, 0:CS */
		return;
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  ant_div_type=[CGCS_RX_HW_ANTDIV]\n"));

	/* MAC setting */
	value32 = odm_get_mac_reg(p_dm_odm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD);
	odm_set_mac_reg(p_dm_odm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD, value32 | (BIT(23) | BIT25)); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT8, 0);/* reg870[8]=1'b0, reg870[9]=1'b0		 */ /* antsel antselb by HW */
	odm_set_bb_reg(p_dm_odm, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);	/* reg864[10]=1'b0	 */ /* antsel2 by HW */
	odm_set_bb_reg(p_dm_odm, ODM_REG_LNA_SWITCH_11N, BIT(22), 1);	/* regb2c[22]=1'b0	 */ /* disable CS/CG switch */
	odm_set_bb_reg(p_dm_odm, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);	/* regb2c[31]=1'b1	 */ /* output at CG only */
	/* OFDM Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/* CCK Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1); /* Fix CCK PHY status report issue */
	odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */

	odm_set_bb_reg(p_dm_odm, ODM_REG_ANT_MAPPING1_11N, 0xFFFF, 0x0001);	/* antenna mapping table */

	p_dm_fat_table->enable_ctrl_frame_antdiv = 1;
}

void
odm_trx_hw_ant_div_init_88e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	value32;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

#if 0
	if (p_dm_odm->mp_mode == true) {
		odm_set_bb_reg(p_dm_odm, ODM_REG_IGI_A_11N, BIT(7), 0); /* disable HW AntDiv */
		odm_set_bb_reg(p_dm_odm, ODM_REG_RX_ANT_CTRL_11N, BIT(5) | BIT4 | BIT3, 0); /* Default RX   (0/1) */
		return;
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  ant_div_type=[CG_TRX_HW_ANTDIV (SPDT)]\n"));

	/* MAC setting */
	value32 = odm_get_mac_reg(p_dm_odm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD);
	odm_set_mac_reg(p_dm_odm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD, value32 | (BIT(23) | BIT25)); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT8, 0);/* reg870[8]=1'b0, reg870[9]=1'b0		 */ /* antsel antselb by HW */
	odm_set_bb_reg(p_dm_odm, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);	/* reg864[10]=1'b0	 */ /* antsel2 by HW */
	odm_set_bb_reg(p_dm_odm, ODM_REG_LNA_SWITCH_11N, BIT(22), 0);	/* regb2c[22]=1'b0	 */ /* disable CS/CG switch */
	odm_set_bb_reg(p_dm_odm, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);	/* regb2c[31]=1'b1	 */ /* output at CG only */
	/* OFDM Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/* CCK Settings */
	odm_set_bb_reg(p_dm_odm, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1); /* Fix CCK PHY status report issue */
	odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */

	/* antenna mapping table */
	if (!p_dm_odm->is_mp_chip) { /* testchip */
		odm_set_bb_reg(p_dm_odm, ODM_REG_RX_DEFUALT_A_11N, BIT(10) | BIT9 | BIT8, 1);	/* Reg858[10:8]=3'b001 */
		odm_set_bb_reg(p_dm_odm, ODM_REG_RX_DEFUALT_A_11N, BIT(13) | BIT12 | BIT11, 2);	/* Reg858[13:11]=3'b010 */
	} else /* MPchip */
		odm_set_bb_reg(p_dm_odm, ODM_REG_ANT_MAPPING1_11N, MASKDWORD, 0x0201);	/*Reg914=3'b010, Reg915=3'b001*/

	p_dm_fat_table->enable_ctrl_frame_antdiv = 1;
}


#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
void
odm_smart_hw_ant_div_init_88e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	value32, i;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  ant_div_type=[CG_TRX_SMART_ANTDIV]\n"));

#if 0
	if (p_dm_odm->mp_mode == true) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("p_dm_odm->ant_div_type: %d\n", p_dm_odm->ant_div_type));
		return;
	}
#endif

	p_dm_fat_table->train_idx = 0;
	p_dm_fat_table->fat_state = FAT_PREPARE_STATE;

	p_dm_odm->fat_comb_a = 5;
	p_dm_odm->antdiv_intvl = 0x64; /* 100ms */

	for (i = 0; i < 6; i++)
		p_dm_fat_table->bssid[i] = 0;
	for (i = 0; i < (p_dm_odm->fat_comb_a) ; i++) {
		p_dm_fat_table->ant_sum_rssi[i] = 0;
		p_dm_fat_table->ant_rssi_cnt[i] = 0;
		p_dm_fat_table->ant_ave_rssi[i] = 0;
	}

	/* MAC setting */
	value32 = odm_get_mac_reg(p_dm_odm, 0x4c, MASKDWORD);
	odm_set_mac_reg(p_dm_odm, 0x4c, MASKDWORD, value32 | (BIT(23) | BIT25)); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	value32 = odm_get_mac_reg(p_dm_odm,  0x7B4, MASKDWORD);
	odm_set_mac_reg(p_dm_odm, 0x7b4, MASKDWORD, value32 | (BIT(16) | BIT17)); /* Reg7B4[16]=1 enable antenna training, Reg7B4[17]=1 enable A2 match */
	/* value32 = platform_efio_read_4byte(adapter, 0x7B4); */
	/* platform_efio_write_4byte(adapter, 0x7b4, value32|BIT(18));	 */ /* append MACID in reponse packet */

	/* Match MAC ADDR */
	odm_set_mac_reg(p_dm_odm, 0x7b4, 0xFFFF, 0);
	odm_set_mac_reg(p_dm_odm, 0x7b0, MASKDWORD, 0);

	odm_set_bb_reg(p_dm_odm, 0x870, BIT(9) | BIT8, 0);/* reg870[8]=1'b0, reg870[9]=1'b0		 */ /* antsel antselb by HW */
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(10), 0);	/* reg864[10]=1'b0	 */ /* antsel2 by HW */
	odm_set_bb_reg(p_dm_odm, 0xb2c, BIT(22), 0);	/* regb2c[22]=1'b0	 */ /* disable CS/CG switch */
	odm_set_bb_reg(p_dm_odm, 0xb2c, BIT(31), 0);	/* regb2c[31]=1'b1	 */ /* output at CS only */
	odm_set_bb_reg(p_dm_odm, 0xca4, MASKDWORD, 0x000000a0);

	/* antenna mapping table */
	if (p_dm_odm->fat_comb_a == 2) {
		if (!p_dm_odm->is_mp_chip) { /* testchip */
			odm_set_bb_reg(p_dm_odm, 0x858, BIT(10) | BIT9 | BIT8, 1);	/* Reg858[10:8]=3'b001 */
			odm_set_bb_reg(p_dm_odm, 0x858, BIT(13) | BIT12 | BIT11, 2);	/* Reg858[13:11]=3'b010 */
		} else { /* MPchip */
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE0, 1);
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE1, 2);
		}
	} else {
		if (!p_dm_odm->is_mp_chip) { /* testchip */
			odm_set_bb_reg(p_dm_odm, 0x858, BIT(10) | BIT9 | BIT8, 0);	/* Reg858[10:8]=3'b000 */
			odm_set_bb_reg(p_dm_odm, 0x858, BIT(13) | BIT12 | BIT11, 1);	/* Reg858[13:11]=3'b001 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(16), 0);
			odm_set_bb_reg(p_dm_odm, 0x858, BIT(15) | BIT14, 2);	/* (Reg878[0],Reg858[14:15])=3'b010 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(19) | BIT18 | BIT17, 3); /* Reg878[3:1]=3b'011 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(22) | BIT21 | BIT20, 4); /* Reg878[6:4]=3b'100 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(25) | BIT24 | BIT23, 5); /* Reg878[9:7]=3b'101 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(28) | BIT27 | BIT26, 6); /* Reg878[12:10]=3b'110 */
			odm_set_bb_reg(p_dm_odm, 0x878, BIT(31) | BIT30 | BIT29, 7); /* Reg878[15:13]=3b'111 */
		} else { /* MPchip */
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE0, 4);     /* 0: 3b'000 */
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE1, 2);     /* 1: 3b'001 */
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE2, 0);     /* 2: 3b'010 */
			odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE3, 1);     /* 3: 3b'011 */
			odm_set_bb_reg(p_dm_odm, 0x918, MASKBYTE0, 3);     /* 4: 3b'100 */
			odm_set_bb_reg(p_dm_odm, 0x918, MASKBYTE1, 5);     /* 5: 3b'101 */
			odm_set_bb_reg(p_dm_odm, 0x918, MASKBYTE2, 6);     /* 6: 3b'110 */
			odm_set_bb_reg(p_dm_odm, 0x918, MASKBYTE3, 255); /* 7: 3b'111 */
		}
	}

	/* Default ant setting when no fast training */
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(5) | BIT4 | BIT3, 0);	/* Default RX */
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(8) | BIT7 | BIT6, 1);	/* Optional RX */
	odm_set_bb_reg(p_dm_odm, 0x860, BIT(14) | BIT13 | BIT12, 0); /* Default TX */

	/* Enter Traing state */
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(2) | BIT1 | BIT0, (p_dm_odm->fat_comb_a - 1));	/* reg864[2:0]=3'd6	 */ /* ant combination=reg864[2:0]+1 */

	/* SW Control */
	/* phy_set_bb_reg(adapter, 0x864, BIT10, 1); */
	/* phy_set_bb_reg(adapter, 0x870, BIT9, 1); */
	/* phy_set_bb_reg(adapter, 0x870, BIT8, 1); */
	/* phy_set_bb_reg(adapter, 0x864, BIT11, 1); */
	/* phy_set_bb_reg(adapter, 0x860, BIT9, 0); */
	/* phy_set_bb_reg(adapter, 0x860, BIT8, 0); */
}
#endif

#endif /* #if (RTL8188E_SUPPORT == 1) */


#if (RTL8192E_SUPPORT == 1)
void
odm_rx_hw_ant_div_init_92e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

#if 0
	if (p_dm_odm->mp_mode == true) {
		odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
		odm_set_bb_reg(p_dm_odm, 0xc50, BIT(8), 0); /* r_rxdiv_enable_anta  regc50[8]=1'b0  0: control by c50[9] */
		odm_set_bb_reg(p_dm_odm, 0xc50, BIT(9), 1);  /* 1:CG, 0:CS */
		return;
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8192E AntDiv_Init =>  ant_div_type=[CGCS_RX_HW_ANTDIV]\n"));

	/* Pin Settings */
	odm_set_bb_reg(p_dm_odm, 0x870, BIT(8), 0);/* reg870[8]=1'b0,     */ /* "antsel" is controled by HWs */
	odm_set_bb_reg(p_dm_odm, 0xc50, BIT(8), 1); /* regc50[8]=1'b1   */ /* " CS/CG switching" is controled by HWs */

	/* Mapping table */
	odm_set_bb_reg(p_dm_odm, 0x914, 0xFFFF, 0x0100); /* antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(p_dm_odm, 0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0xca4, 0x7FF000, 0x0); /* bias */

	/* CCK Settings */
	odm_set_bb_reg(p_dm_odm, 0xa04, 0xF000000, 0); /* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(p_dm_odm, 0xb34, BIT(30), 0); /* (92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(p_dm_odm, 0xa74, BIT(7), 1); /* Fix CCK PHY status report issue */
	odm_set_bb_reg(p_dm_odm, 0xa0c, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	/* EVM enhance AntDiv method init---------------------------------------------------------------------- */
	p_dm_fat_table->EVM_method_enable = 0;
	p_dm_fat_table->fat_state = NORMAL_STATE_MIAN;
	p_dm_odm->antdiv_intvl = 0x64;
	odm_set_bb_reg(p_dm_odm, 0x910, 0x3f, 0xf);
	p_dm_odm->antdiv_evm_en = 1;
	/* p_dm_odm->antdiv_period=1; */
	p_dm_odm->evm_antdiv_period = 3;

#endif

}

void
odm_trx_hw_ant_div_init_92e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

#if 0
	if (p_dm_odm->mp_mode == true) {
		odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
		odm_set_bb_reg(p_dm_odm, 0xc50, BIT(8), 0); /* r_rxdiv_enable_anta  regc50[8]=1'b0  0: control by c50[9] */
		odm_set_bb_reg(p_dm_odm, 0xc50, BIT(9), 1);  /* 1:CG, 0:CS */
		return;
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8192E AntDiv_Init =>  ant_div_type=[ Only for DIR605, CG_TRX_HW_ANTDIV]\n"));

	/* 3 --RFE pin setting--------- */
	/* [MAC] */
	odm_set_mac_reg(p_dm_odm, 0x38, BIT(11), 1);            /* DBG PAD Driving control (GPIO 8) */
	odm_set_mac_reg(p_dm_odm, 0x4c, BIT(23), 0);            /* path-A, RFE_CTRL_3 */
	odm_set_mac_reg(p_dm_odm, 0x4c, BIT(29), 1);            /* path-A, RFE_CTRL_8 */
	/* [BB] */
	odm_set_bb_reg(p_dm_odm, 0x944, BIT(3), 1);              /* RFE_buffer */
	odm_set_bb_reg(p_dm_odm, 0x944, BIT(8), 1);
	odm_set_bb_reg(p_dm_odm, 0x940, BIT(7) | BIT6, 0x0); /* r_rfe_path_sel_   (RFE_CTRL_3) */
	odm_set_bb_reg(p_dm_odm, 0x940, BIT(17) | BIT16, 0x0); /* r_rfe_path_sel_   (RFE_CTRL_8) */
	odm_set_bb_reg(p_dm_odm, 0x944, BIT(31), 0);     /* RFE_buffer */
	odm_set_bb_reg(p_dm_odm, 0x92C, BIT(3), 0);     /* rfe_inv  (RFE_CTRL_3) */
	odm_set_bb_reg(p_dm_odm, 0x92C, BIT(8), 1);     /* rfe_inv  (RFE_CTRL_8) */
	odm_set_bb_reg(p_dm_odm, 0x930, 0xF000, 0x8);           /* path-A, RFE_CTRL_3 */
	odm_set_bb_reg(p_dm_odm, 0x934, 0xF, 0x8);           /* path-A, RFE_CTRL_8 */
	/* 3 ------------------------- */

	/* Pin Settings */
	odm_set_bb_reg(p_dm_odm, 0xC50, BIT(8), 0);	   /* path-A  	 */ /* disable CS/CG switch */

#if 0
	/* Let it follows PHY_REG for bit9 setting */
	if (p_dm_odm->priv->pshare->rf_ft_var.use_ext_pa || p_dm_odm->priv->pshare->rf_ft_var.use_ext_lna)
		odm_set_bb_reg(p_dm_odm, 0xC50, BIT(9), 1);/* path-A 	//output at CS */
	else
		odm_set_bb_reg(p_dm_odm, 0xC50, BIT(9), 0);    /* path-A 	//output at CG ->normal power */
#endif

	odm_set_bb_reg(p_dm_odm, 0x870, BIT(9) | BIT8, 0);  /* path-A 	 */ /* antsel antselb by HW */
	odm_set_bb_reg(p_dm_odm, 0xB38, BIT(10), 0);	   /* path-A   	 */ /* antsel2 by HW */

	/* Mapping table */
	odm_set_bb_reg(p_dm_odm, 0x914, 0xFFFF, 0x0100); /* antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(p_dm_odm, 0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0xca4, 0x7FF000, 0x0); /* bias */

	/* CCK Settings */
	odm_set_bb_reg(p_dm_odm, 0xa04, 0xF000000, 0); /* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(p_dm_odm, 0xb34, BIT(30), 0); /* (92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(p_dm_odm, 0xa74, BIT(7), 1); /* Fix CCK PHY status report issue */
	odm_set_bb_reg(p_dm_odm, 0xa0c, BIT(4), 1); /* CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	/* EVM enhance AntDiv method init---------------------------------------------------------------------- */
	p_dm_fat_table->EVM_method_enable = 0;
	p_dm_fat_table->fat_state = NORMAL_STATE_MIAN;
	p_dm_odm->antdiv_intvl = 0x64;
	odm_set_bb_reg(p_dm_odm, 0x910, 0x3f, 0xf);
	p_dm_odm->antdiv_evm_en = 1;
	/* p_dm_odm->antdiv_period=1; */
	p_dm_odm->evm_antdiv_period = 3;
#endif
}

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
void
odm_smart_hw_ant_div_init_92e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8192E AntDiv_Init =>  ant_div_type=[CG_TRX_SMART_ANTDIV]\n"));
}
#endif

#endif /* #if (RTL8192E_SUPPORT == 1) */

#if (RTL8723D_SUPPORT == 1)
void
odm_trx_hw_ant_div_init_8723d(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8723D] AntDiv_Init =>  ant_div_type=[S0S1_HW_TRX_AntDiv]\n"));

	/*BT Coexistence*/
	/*keep antsel_map when GNT_BT = 1*/
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(12), 1);
	/* Disable hw antsw & fast_train.antsw when GNT_BT=1 */
	odm_set_bb_reg(p_dm_odm, 0x874, BIT(23), 0);
	/* Disable hw antsw & fast_train.antsw when BT TX/RX */
	odm_set_bb_reg(p_dm_odm, 0xE64, 0xFFFF0000, 0x000c);


	odm_set_bb_reg(p_dm_odm, 0x870, BIT(9) | BIT(8), 0);
	/*PTA setting: WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL)*/
	/*odm_set_bb_reg(p_dm_odm, 0x948, BIT6, 0);*/
	/*odm_set_bb_reg(p_dm_odm, 0x948, BIT8, 0);*/
	/*GNT_WL tx*/
	odm_set_bb_reg(p_dm_odm, 0x950, BIT(29), 0);


	/*Mapping Table*/
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE1, 3);
	/* odm_set_bb_reg(p_dm_odm, 0x864, BIT5|BIT4|BIT3, 0); */
	/* odm_set_bb_reg(p_dm_odm, 0x864, BIT8|BIT7|BIT6, 1); */

	/* Set WLBB_SEL_RF_ON 1 if RXFIR_PWDB > 0xCcc[3:0] */
	odm_set_bb_reg(p_dm_odm, 0xCcc, BIT(12), 0);
	/* Low-to-High threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(p_dm_odm, 0xCcc, 0x0F, 0x01);
	/* High-to-Low threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(p_dm_odm, 0xCcc, 0xF0, 0x0);
	/* b Low-to-High threshold for WLBB_SEL_RF_ON when OFDM disable ( only CCK ) */
	odm_set_bb_reg(p_dm_odm, 0xAbc, 0xFF, 0x06);
	/* High-to-Low threshold for WLBB_SEL_RF_ON when OFDM disable ( only CCK ) */
	odm_set_bb_reg(p_dm_odm, 0xAbc, 0xFF00, 0x00);


	/*OFDM HW AntDiv Parameters*/
	odm_set_bb_reg(p_dm_odm, 0xCA4, 0x7FF, 0xa0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, 0x7FF000, 0x00);
	odm_set_bb_reg(p_dm_odm, 0xC5C, BIT(20) | BIT(19) | BIT(18), 0x04);

	/*CCK HW AntDiv Parameters*/
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1);
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1);
	odm_set_bb_reg(p_dm_odm, 0xAA8, BIT(8), 0);

	odm_set_bb_reg(p_dm_odm, 0xA0C, 0x0F, 0xf);
	odm_set_bb_reg(p_dm_odm, 0xA14, 0x1F, 0x8);
	odm_set_bb_reg(p_dm_odm, 0xA10, BIT(13), 0x1);
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(8), 0x0);
	odm_set_bb_reg(p_dm_odm, 0xB34, BIT(30), 0x1);

	/*disable antenna training	*/
	odm_set_bb_reg(p_dm_odm, 0xE08, BIT(16), 0);
	odm_set_bb_reg(p_dm_odm, 0xc50, BIT(8), 0);

}

void
phydm_set_tx_ant_pwr_8723d(
	void			*p_dm_void,
	u8			ant
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _ADAPTER		*p_adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	p_dm_fat_table->rx_idle_ant = ant;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	p_adapter->HalFunc.SetTxPowerLevelHandler(p_adapter, *p_dm_odm->p_channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	rtw_hal_set_tx_power_level(p_adapter, *p_dm_odm->p_channel);
#endif

}
#endif

#if (RTL8723B_SUPPORT == 1)
void
odm_trx_hw_ant_div_init_8723b(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8723B AntDiv_Init =>  ant_div_type=[CG_TRX_HW_ANTDIV(DPDT)]\n"));

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xCA4, 0x7FF, 0xa0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0xCA4, 0x7FF000, 0x00); /* bias */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	/* BT Coexistence */
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(12), 0); /* keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(p_dm_odm, 0x874, BIT(23), 0); /* Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	/* Output Pin Settings */
	odm_set_bb_reg(p_dm_odm, 0x870, BIT(8), 0);

	odm_set_bb_reg(p_dm_odm, 0x948, BIT(6), 0); /* WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL) */
	odm_set_bb_reg(p_dm_odm, 0x948, BIT(7), 0);

	odm_set_mac_reg(p_dm_odm, 0x40, BIT(3), 1);
	odm_set_mac_reg(p_dm_odm, 0x38, BIT(11), 1);
	odm_set_mac_reg(p_dm_odm, 0x4C,  BIT(24) | BIT23, 2); /* select DPDT_P and DPDT_N as output pin */

	odm_set_bb_reg(p_dm_odm, 0x944, BIT(0) | BIT1, 3); /* in/out */
	odm_set_bb_reg(p_dm_odm, 0x944, BIT(31), 0);

	odm_set_bb_reg(p_dm_odm, 0x92C, BIT(1), 0); /* DPDT_P non-inverse */
	odm_set_bb_reg(p_dm_odm, 0x92C, BIT(0), 1); /* DPDT_N inverse */

	odm_set_bb_reg(p_dm_odm, 0x930, 0xF0, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0x930, 0xF, 8); /* DPDT_N = ANTSEL[0] */

	/* 2 [--For HW Bug setting] */
	if (p_dm_odm->ant_type == ODM_AUTO_ANT)
		odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), 0); /* CCK AntDiv function block enable */

}



void
odm_s0s1_sw_ant_div_init_8723b(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8723B AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n"));

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0x914, MASKBYTE1, 1);

	/* Output Pin Settings */
	/* odm_set_bb_reg(p_dm_odm, 0x948, BIT6, 0x1); */
	odm_set_bb_reg(p_dm_odm, 0x870, BIT(9) | BIT(8), 0);

	p_dm_fat_table->is_become_linked  = false;
	p_dm_swat_table->try_flag = SWAW_STEP_INIT;
	p_dm_swat_table->double_chk_flag = 0;

	/* 2 [--For HW Bug setting] */
	odm_set_bb_reg(p_dm_odm, 0x80C, BIT(21), 0); /* TX ant  by Reg */

}

void
odm_update_rx_idle_ant_8723b(
	void			*p_dm_void,
	u8			ant,
	u32			default_ant,
	u32			optional_ant
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _ADAPTER		*p_adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	u8			count = 0;
	u8			u1_temp;
	u8			h2c_parameter;

	if ((!p_dm_odm->is_linked) && (p_dm_odm->ant_type == ODM_AUTO_ANT)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to no link\n"));
		return;
	}

#if 0
	/* Send H2C command to FW */
	/* Enable wifi calibration */
	h2c_parameter = true;
	odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);

	/* Check if H2C command sucess or not (0x1e6) */
	u1_temp = odm_read_1byte(p_dm_odm, 0x1e6);
	while ((u1_temp != 0x1) && (count < 100)) {
		ODM_delay_us(10);
		u1_temp = odm_read_1byte(p_dm_odm, 0x1e6);
		count++;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: H2C command status = %d, count = %d\n", u1_temp, count));

	if (u1_temp == 0x1) {
		/* Check if BT is doing IQK (0x1e7) */
		count = 0;
		u1_temp = odm_read_1byte(p_dm_odm, 0x1e7);
		while ((!(u1_temp & BIT(0)))  && (count < 100)) {
			ODM_delay_us(50);
			u1_temp = odm_read_1byte(p_dm_odm, 0x1e7);
			count++;
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: BT IQK status = %d, count = %d\n", u1_temp, count));

		if (u1_temp & BIT(0)) {
			odm_set_bb_reg(p_dm_odm, 0x948, BIT(6), 0x1);
			odm_set_bb_reg(p_dm_odm, 0x948, BIT(9), default_ant);
			odm_set_bb_reg(p_dm_odm, 0x864, BIT(5) | BIT4 | BIT3, default_ant);	/* Default RX */
			odm_set_bb_reg(p_dm_odm, 0x864, BIT(8) | BIT7 | BIT6, optional_ant);	/* Optional RX */
			odm_set_bb_reg(p_dm_odm, 0x860, BIT(14) | BIT13 | BIT12, default_ant); /* Default TX */
			p_dm_fat_table->rx_idle_ant = ant;

			/* Set TX AGC by S0/S1 */
			/* Need to consider Linux driver */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			p_adapter->hal_func.set_tx_power_level_handler(p_adapter, *p_dm_odm->p_channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
			rtw_hal_set_tx_power_level(p_adapter, *p_dm_odm->p_channel);
#endif

			/* Set IQC by S0/S1 */
			odm_set_iqc_by_rfpath(p_dm_odm, default_ant);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Sucess to set RX antenna\n"));
		} else
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to BT IQK\n"));
	} else
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to H2C command fail\n"));

	/* Send H2C command to FW */
	/* Disable wifi calibration */
	h2c_parameter = false;
	odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
#else

	odm_set_bb_reg(p_dm_odm, 0x948, BIT(6), 0x1);
	odm_set_bb_reg(p_dm_odm, 0x948, BIT(9), default_ant);
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(5) | BIT4 | BIT3, default_ant);      /*Default RX*/
	odm_set_bb_reg(p_dm_odm, 0x864, BIT(8) | BIT7 | BIT6, optional_ant);     /*Optional RX*/
	odm_set_bb_reg(p_dm_odm, 0x860, BIT(14) | BIT13 | BIT12, default_ant);    /*Default TX*/
	p_dm_fat_table->rx_idle_ant = ant;

	/* Set TX AGC by S0/S1 */
	/* Need to consider Linux driver */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	p_adapter->HalFunc.SetTxPowerLevelHandler(p_adapter, *p_dm_odm->p_channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	rtw_hal_set_tx_power_level(p_adapter, *p_dm_odm->p_channel);
#endif

	/* Set IQC by S0/S1 */
	odm_set_iqc_by_rfpath(p_dm_odm, default_ant);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-ant ] 8723B: Success to set RX antenna\n"));

#endif
}

boolean
phydm_is_bt_enable_8723b(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			bt_state;
	/*u32			reg75;*/

	/*reg75 = odm_get_bb_reg(p_dm_odm, 0x74, BIT8);*/
	/*odm_set_bb_reg(p_dm_odm, 0x74, BIT8, 0x0);*/
	odm_set_bb_reg(p_dm_odm, 0xa0, BIT(24) | BIT(25) | BIT(26), 0x5);
	bt_state = odm_get_bb_reg(p_dm_odm, 0xa0, (BIT(3) | BIT(2) | BIT(1) | BIT(0)));
	/*odm_set_bb_reg(p_dm_odm, 0x74, BIT8, reg75);*/

	if ((bt_state == 4) || (bt_state == 7) || (bt_state == 9) || (bt_state == 13))
		return true;
	else
		return false;
}
#endif /* #if (RTL8723B_SUPPORT == 1) */

#if (RTL8821A_SUPPORT == 1)
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
void
phydm_hl_smart_ant_type1_init_8821a(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u32			value32;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821A SmartAnt_Init => ant_div_type=[Hong-Lin Smart ant Type1]\n"));

#if 0
	/* ---------------------------------------- */
	/* GPIO 2-3 for Beam control */
	/* reg0x66[2]=0 */
	/* reg0x44[27:26] = 0 */
	/* reg0x44[23:16]  enable_output for P_GPIO[7:0] */
	/* reg0x44[15:8]  output_value for P_GPIO[7:0] */
	/* reg0x40[1:0] = 0  GPIO function */
	/* ------------------------------------------ */
#endif

	/*GPIO setting*/
	odm_set_mac_reg(p_dm_odm, 0x64, BIT(18), 0);
	odm_set_mac_reg(p_dm_odm, 0x44, BIT(27) | BIT(26), 0);
	odm_set_mac_reg(p_dm_odm, 0x44, BIT(19) | BIT18, 0x3);	/*enable_output for P_GPIO[3:2]*/
	/*odm_set_mac_reg(p_dm_odm, 0x44, BIT(11)|BIT10, 0);*/ /*output value*/
	odm_set_mac_reg(p_dm_odm, 0x40, BIT(1) | BIT0, 0);		/*GPIO function*/

	/*Hong_lin smart antenna HW setting*/
	pdm_sat_table->rfu_codeword_total_bit_num  = 24;/*max=32*/
	pdm_sat_table->rfu_each_ant_bit_num = 4;
	pdm_sat_table->beam_patten_num_each_ant = 4;

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	pdm_sat_table->latch_time = 100; /*mu sec*/
#elif DEV_BUS_TYPE == RT_USB_INTERFACE
	pdm_sat_table->latch_time = 100; /*mu sec*/
#endif
	pdm_sat_table->pkt_skip_statistic_en = 0;

	pdm_sat_table->ant_num = 1;/*max=8*/
	pdm_sat_table->ant_num_total = NUM_ANTENNA_8821A;
	pdm_sat_table->first_train_ant = MAIN_ANT;

	pdm_sat_table->rfu_codeword_table[0] = 0x0;
	pdm_sat_table->rfu_codeword_table[1] = 0x4;
	pdm_sat_table->rfu_codeword_table[2] = 0x8;
	pdm_sat_table->rfu_codeword_table[3] = 0xc;

	pdm_sat_table->rfu_codeword_table_5g[0] = 0x1;
	pdm_sat_table->rfu_codeword_table_5g[1] = 0x2;
	pdm_sat_table->rfu_codeword_table_5g[2] = 0x4;
	pdm_sat_table->rfu_codeword_table_5g[3] = 0x8;

	pdm_sat_table->fix_beam_pattern_en  = 0;
	pdm_sat_table->decision_holding_period = 0;

	/*beam training setting*/
	pdm_sat_table->pkt_counter = 0;
	pdm_sat_table->per_beam_training_pkt_num = 10;

	/*set default beam*/
	pdm_sat_table->fast_training_beam_num = 0;
	pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;
	phydm_set_all_ant_same_beam_num(p_dm_odm);

	p_dm_fat_table->fat_state = FAT_BEFORE_LINK_STATE;

	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKDWORD, 0x01000100);
	odm_set_bb_reg(p_dm_odm, 0xCA8, MASKDWORD, 0x01000100);

	/*[BB] FAT setting*/
	odm_set_bb_reg(p_dm_odm, 0xc08, BIT(18) | BIT(17) | BIT(16), pdm_sat_table->ant_num);
	odm_set_bb_reg(p_dm_odm, 0xc08, BIT(31), 0); /*increase ant num every FAT period 0:+1, 1+2*/
	odm_set_bb_reg(p_dm_odm, 0x8c4, BIT(2) | BIT1, 1); /*change cca antenna timming threshold if no CCA occurred: 0:200ms / 1:100ms / 2:no use / 3: 300*/
	odm_set_bb_reg(p_dm_odm, 0x8c4, BIT(0), 1); /*FAT_watchdog_en*/

	value32 = odm_get_mac_reg(p_dm_odm,  0x7B4, MASKDWORD);
	odm_set_mac_reg(p_dm_odm, 0x7b4, MASKDWORD, value32 | (BIT(16) | BIT17));	/*Reg7B4[16]=1 enable antenna training */
	/*Reg7B4[17]=1 enable  match MAC addr*/
	odm_set_mac_reg(p_dm_odm, 0x7b4, 0xFFFF, 0);/*Match MAC ADDR*/
	odm_set_mac_reg(p_dm_odm, 0x7b0, MASKDWORD, 0);

}
#endif

void
odm_trx_hw_ant_div_init_8821a(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821A AntDiv_Init => ant_div_type=[ CG_TRX_HW_ANTDIV (DPDT)]\n"));

	/* Output Pin Settings */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(25), 0);

	odm_set_mac_reg(p_dm_odm, 0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(p_dm_odm, 0x64, BIT(28), 1); /* LNAON by WLAN control */

	odm_set_bb_reg(p_dm_odm, 0xCB0, MASKDWORD, 0x77775745);
	odm_set_bb_reg(p_dm_odm, 0xCB8, BIT(16), 0);

	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(23), 0); /* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(24), 1); /* by WLAN control */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF0, 8); /* DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(29), 0); /* DPDT_P non-inverse */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(28), 1); /* DPDT_N inverse */

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF000, 0x10); /* bias */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), 0); /* ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), 0); /* CCK AntDiv function block enable */

	/* BT Coexistence */
	odm_set_bb_reg(p_dm_odm, 0xCAC, BIT(9), 1); /* keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(p_dm_odm, 0x804, BIT(4), 1); /* Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	odm_set_bb_reg(p_dm_odm, 0x8CC, BIT(20) | BIT19 | BIT18, 3); /* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(p_dm_odm, 0x668, BIT(3), 1);

}

void
odm_s0s1_sw_ant_div_init_8821a(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821A AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n"));

	/* Output Pin Settings */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(25), 0);

	odm_set_mac_reg(p_dm_odm, 0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(p_dm_odm, 0x64, BIT(28), 1); /* LNAON by WLAN control */

	odm_set_bb_reg(p_dm_odm, 0xCB0, MASKDWORD, 0x77775745);
	odm_set_bb_reg(p_dm_odm, 0xCB8, BIT(16), 0);

	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(23), 0); /* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(24), 1); /* by WLAN control */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF0, 8); /* DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(29), 0); /* DPDT_P non-inverse */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(28), 1); /* DPDT_N inverse */

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF000, 0x10); /* bias */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), 0); /* ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), 0); /* CCK AntDiv function block enable */

	/* BT Coexistence */
	odm_set_bb_reg(p_dm_odm, 0xCAC, BIT(9), 1); /* keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(p_dm_odm, 0x804, BIT(4), 1); /* Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	odm_set_bb_reg(p_dm_odm, 0x8CC, BIT(20) | BIT19 | BIT18, 3); /* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(p_dm_odm, 0x668, BIT(3), 1);


	odm_set_bb_reg(p_dm_odm, 0x900, BIT(18), 0);

	p_dm_swat_table->try_flag = SWAW_STEP_INIT;
	p_dm_swat_table->double_chk_flag = 0;
	p_dm_swat_table->cur_antenna = MAIN_ANT;
	p_dm_swat_table->pre_antenna = MAIN_ANT;
	p_dm_swat_table->swas_no_link_state = 0;

}
#endif /* #if (RTL8821A_SUPPORT == 1) */

#if (RTL8822B_SUPPORT == 1)
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
void
phydm_hl_smart_ant_type2_init_8822b(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u8	j;
	u8	rfu_codeword_table_init_2g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B] = {
			{0, 1},
			{1, 2},
			{2, 3},
			{3, 4},
			{4, 5},
			{5, 6},
			{6, 7},
			{7, 8}
		}; 
	u8	rfu_codeword_table_init_5g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B] ={
			{0, 1},
			{1, 2},
			{2, 3},
			{3, 4},
			{4, 5},
			{5, 6},
			{6, 7},
			{7, 8}
		}; 		

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***RTK 8822B SmartAnt_Init: Hong-Bo SmrtAnt Type2]\n"));

	/* ---------------------------------------- */
	/* GPIO 0-1 for Beam control */
	/* reg0x66[2:0]=0 */
	/* reg0x44[25:24] = 0 */
	/* reg0x44[23:16]  enable_output for P_GPIO[7:0] */
	/* reg0x44[15:8]  output_value for P_GPIO[7:0] */
	/* reg0x40[1:0] = 0  GPIO function */
	/* ------------------------------------------ */

	odm_move_memory(p_dm_odm, pdm_sat_table->rfu_codeword_table_2g, rfu_codeword_table_init_2g, (SUPPORT_BEAM_SET_PATTERN_NUM * MAX_PATH_NUM_8822B));
	odm_move_memory(p_dm_odm, pdm_sat_table->rfu_codeword_table_5g, rfu_codeword_table_init_5g, (SUPPORT_BEAM_SET_PATTERN_NUM * MAX_PATH_NUM_8822B));

	/*GPIO setting*/
	odm_set_mac_reg(p_dm_odm, 0x64, (BIT(18) | BIT(17) | BIT(16)), 0);
	odm_set_mac_reg(p_dm_odm, 0x44, BIT(25) | BIT24, 0);	/*config P_GPIO[3:2] to data port*/
	odm_set_mac_reg(p_dm_odm, 0x44, BIT(17) | BIT16, 0x3);	/*enable_output for P_GPIO[3:2]*/
	/*odm_set_mac_reg(p_dm_odm, 0x44, BIT(9)|BIT8, 0);*/ /*P_GPIO[3:2] output value*/
	odm_set_mac_reg(p_dm_odm, 0x40, BIT(1) | BIT0, 0);		/*GPIO function*/

	/*Hong_lin smart antenna HW setting*/
	pdm_sat_table->rfu_protocol_type = 2;
	pdm_sat_table->rfu_codeword_total_bit_num  = 16;/*max=32bit*/
	pdm_sat_table->rfu_each_ant_bit_num = 4;
	
	pdm_sat_table->total_beam_set_num = 4;
	pdm_sat_table->total_beam_set_num_2g = 4;
	pdm_sat_table->total_beam_set_num_5g = 6;

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	pdm_sat_table->latch_time = 100; /*mu sec*/
#elif DEV_BUS_TYPE == RT_USB_INTERFACE
	pdm_sat_table->latch_time = 100; /*mu sec*/
#endif
	pdm_sat_table->pkt_skip_statistic_en = 0;

	pdm_sat_table->ant_num = 2;
	pdm_sat_table->ant_num_total = MAX_PATH_NUM_8822B;
	pdm_sat_table->first_train_ant = MAIN_ANT;



	pdm_sat_table->fix_beam_pattern_en  = 0;
	pdm_sat_table->decision_holding_period = 0;

	/*beam training setting*/
	pdm_sat_table->pkt_counter = 0;
	pdm_sat_table->per_beam_training_pkt_num = 10;

	/*set default beam*/
	pdm_sat_table->fast_training_beam_num = 0;
	pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;

	for (j = 0; j < SUPPORT_BEAM_SET_PATTERN_NUM; j++) {
		
		pdm_sat_table->beam_set_avg_rssi_pre[j] = 0;
		pdm_sat_table->beam_set_train_rssi_diff[j] = 0;
		pdm_sat_table->beam_set_train_cnt[j] = 0;
	}
	phydm_set_rfu_beam_pattern_type2(p_dm_odm);
	p_dm_fat_table->fat_state = FAT_BEFORE_LINK_STATE;
	
}
#endif
#endif


#if (RTL8821C_SUPPORT == 1)
void
odm_trx_hw_ant_div_init_8821c(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821C AntDiv_Init => ant_div_type=[ CG_TRX_HW_ANTDIV (DPDT)]\n"));
	/* Output Pin Settings */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(25), 0);

	odm_set_mac_reg(p_dm_odm, 0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(p_dm_odm, 0x64, BIT(28), 1); /* LNAON by WLAN control */

	odm_set_bb_reg(p_dm_odm, 0xCB0, MASKDWORD, 0x77775745);
	odm_set_bb_reg(p_dm_odm, 0xCB8, BIT(16), 0);

	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(23), 0); /* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(24), 1); /* by WLAN control */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, 0xF0, 8); /* DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(29), 0); /* DPDT_P non-inverse */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(28), 1); /* DPDT_N inverse */

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF000, 0x10); /* bias */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), 0); /* ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), 0); /* CCK AntDiv function block enable */

	/* BT Coexistence */
	odm_set_bb_reg(p_dm_odm, 0xCAC, BIT(9), 1); /* keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(p_dm_odm, 0x804, BIT(4), 1); /* Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	/* Timming issue */
	odm_set_bb_reg(p_dm_odm, 0x818, BIT(23) | BIT22 | BIT21 | BIT20, 0); /*keep antidx after tx for ACK ( unit x 3.2 mu sec)*/
	odm_set_bb_reg(p_dm_odm, 0x8CC, BIT(20) | BIT19 | BIT18, 3); /* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(p_dm_odm, 0x668, BIT(3), 1);

}
#endif /* #if (RTL8821C_SUPPORT == 1) */


#if (RTL8881A_SUPPORT == 1)
void
odm_trx_hw_ant_div_init_8881a(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8881A AntDiv_Init => ant_div_type=[ CG_TRX_HW_ANTDIV (SPDT)]\n"));

	/* Output Pin Settings */
	/* [SPDT related] */
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(25), 0);
	odm_set_mac_reg(p_dm_odm, 0x4C, BIT(26), 0);
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(31), 0); /* delay buffer */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(22), 0);
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(24), 1);
	odm_set_bb_reg(p_dm_odm, 0xCB0, 0xF00, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB0, 0xF0000, 8); /* DPDT_N = ANTSEL[0] */

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF000, 0x0); /* bias */
	odm_set_bb_reg(p_dm_odm, 0x8CC, BIT(20) | BIT19 | BIT18, 3); /* settling time of antdiv by RF LNA = 100ns */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	/* 2 [--For HW Bug setting] */

	odm_set_bb_reg(p_dm_odm, 0x900, BIT(18), 0); /* TX ant  by Reg */ /* A-cut bug */
}

#endif /* #if (RTL8881A_SUPPORT == 1) */


#if (RTL8812A_SUPPORT == 1)
void
odm_trx_hw_ant_div_init_8812a(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8812A AntDiv_Init => ant_div_type=[ CG_TRX_HW_ANTDIV (SPDT)]\n"));

	/* 3 */ /* 3 --RFE pin setting--------- */
	/* [BB] */
	odm_set_bb_reg(p_dm_odm, 0x900, BIT(10) | BIT9 | BIT8, 0x0);	 /* disable SW switch */
	odm_set_bb_reg(p_dm_odm, 0x900, BIT(17) | BIT(16), 0x0);
	odm_set_bb_reg(p_dm_odm, 0x974, BIT(7) | BIT6, 0x3);   /* in/out */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(31), 0); /* delay buffer */
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(26), 0);
	odm_set_bb_reg(p_dm_odm, 0xCB4, BIT(27), 1);
	odm_set_bb_reg(p_dm_odm, 0xCB0, 0xF000000, 8); /* DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(p_dm_odm, 0xCB0, 0xF0000000, 8); /* DPDT_N = ANTSEL[0] */
	/* 3 ------------------------- */

	/* Mapping Table */
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE0, 0);
	odm_set_bb_reg(p_dm_odm, 0xCA4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(p_dm_odm, 0x8D4, 0x7FF000, 0x0); /* bias */
	odm_set_bb_reg(p_dm_odm, 0x8CC, BIT(20) | BIT19 | BIT18, 3); /* settling time of antdiv by RF LNA = 100ns */

	/* CCK HW AntDiv Parameters */
	odm_set_bb_reg(p_dm_odm, 0xA74, BIT(7), 1); /* patch for clk from 88M to 80M */
	odm_set_bb_reg(p_dm_odm, 0xA0C, BIT(4), 1); /* do 64 samples */

	/* 2 [--For HW Bug setting] */

	odm_set_bb_reg(p_dm_odm, 0x900, BIT(18), 0); /* TX ant  by Reg */ /* A-cut bug */

}

#endif /* #if (RTL8812A_SUPPORT == 1) */

#if (RTL8188F_SUPPORT == 1)
void
odm_s0s1_sw_ant_div_init_8188f(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188F AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n"));


	/*GPIO setting*/
	/*odm_set_mac_reg(p_dm_odm, 0x64, BIT18, 0); */
	/*odm_set_mac_reg(p_dm_odm, 0x44, BIT28|BIT27, 0);*/
	odm_set_mac_reg(p_dm_odm, 0x44, BIT(20) | BIT19, 0x3);	/*enable_output for P_GPIO[4:3]*/
	/*odm_set_mac_reg(p_dm_odm, 0x44, BIT(12)|BIT11, 0);*/ /*output value*/
	/*odm_set_mac_reg(p_dm_odm, 0x40, BIT(1)|BIT0, 0);*/		/*GPIO function*/

	p_dm_fat_table->is_become_linked  = false;
	p_dm_swat_table->try_flag = SWAW_STEP_INIT;
	p_dm_swat_table->double_chk_flag = 0;
}

void
phydm_update_rx_idle_antenna_8188F(
	void	*p_dm_void,
	u32	default_ant
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		codeword;

	if (default_ant == ANT1_2G)
		codeword = 1; /*2'b01*/
	else
		codeword = 2;/*2'b10*/

	odm_set_mac_reg(p_dm_odm, 0x44, (BIT(12) | BIT11), codeword); /*GPIO[4:3] output value*/
}

#endif



#ifdef ODM_EVM_ENHANCE_ANTDIV

void
odm_evm_fast_ant_reset(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	p_dm_fat_table->EVM_method_enable = 0;
	odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
	p_dm_fat_table->fat_state = NORMAL_STATE_MIAN;
	p_dm_odm->antdiv_period = 0;
	odm_set_mac_reg(p_dm_odm, 0x608, BIT(8), 0);
}


void
odm_evm_enhance_ant_div(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	main_rssi, aux_rssi ;
	u32	main_crc_utility = 0, aux_crc_utility = 0, utility_ratio = 1;
	u32	main_evm, aux_evm, diff_rssi = 0, diff_EVM = 0;
	u8	score_EVM = 0, score_CRC = 0;
	u8	rssi_larger_ant = 0;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u32	value32, i;
	boolean main_above1 = false, aux_above1 = false;
	boolean force_antenna = false;
	struct sta_info	*p_entry;
	p_dm_fat_table->target_ant_enhance = 0xFF;


	if ((p_dm_odm->support_ic_type & ODM_EVM_ENHANCE_ANTDIV_SUPPORT_IC)) {
		if (p_dm_odm->is_one_entry_only) {
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[One Client only]\n")); */
			i = p_dm_odm->one_entry_macid;

			main_rssi = (p_dm_fat_table->main_ant_cnt[i] != 0) ? (p_dm_fat_table->main_ant_sum[i] / p_dm_fat_table->main_ant_cnt[i]) : 0;
			aux_rssi = (p_dm_fat_table->aux_ant_cnt[i] != 0) ? (p_dm_fat_table->aux_ant_sum[i] / p_dm_fat_table->aux_ant_cnt[i]) : 0;

			if ((main_rssi == 0 && aux_rssi != 0 && aux_rssi >= FORCE_RSSI_DIFF) || (main_rssi != 0 && aux_rssi == 0 && main_rssi >= FORCE_RSSI_DIFF))
				diff_rssi = FORCE_RSSI_DIFF;
			else if (main_rssi != 0 && aux_rssi != 0)
				diff_rssi = (main_rssi >= aux_rssi) ? (main_rssi - aux_rssi) : (aux_rssi - main_rssi);

			if (main_rssi >= aux_rssi)
				rssi_larger_ant = MAIN_ANT;
			else
				rssi_larger_ant = AUX_ANT;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" Main_Cnt = (( %d ))  , main_rssi= ((  %d ))\n", p_dm_fat_table->main_ant_cnt[i], main_rssi));
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" Aux_Cnt   = (( %d ))  , aux_rssi = ((  %d ))\n", p_dm_fat_table->aux_ant_cnt[i], aux_rssi));

			if (((main_rssi >= evm_rssi_th_high || aux_rssi >= evm_rssi_th_high) || (p_dm_fat_table->EVM_method_enable == 1))
			    /* && (diff_rssi <= FORCE_RSSI_DIFF + 1) */
			   ) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[> TH_H || EVM_method_enable==1]  && "));

				if (((main_rssi >= evm_rssi_th_low) || (aux_rssi >= evm_rssi_th_low))) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[> TH_L ]\n"));

					/* 2 [ Normal state Main] */
					if (p_dm_fat_table->fat_state == NORMAL_STATE_MIAN) {

						p_dm_fat_table->EVM_method_enable = 1;
						odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
						p_dm_odm->antdiv_period = p_dm_odm->evm_antdiv_period;

						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ start training: MIAN]\n"));
						p_dm_fat_table->main_ant_evm_sum[i] = 0;
						p_dm_fat_table->aux_ant_evm_sum[i] = 0;
						p_dm_fat_table->main_ant_evm_cnt[i] = 0;
						p_dm_fat_table->aux_ant_evm_cnt[i] = 0;

						p_dm_fat_table->fat_state = NORMAL_STATE_AUX;
						odm_set_mac_reg(p_dm_odm, 0x608, BIT(8), 1); /* Accept CRC32 Error packets. */
						odm_update_rx_idle_ant(p_dm_odm, MAIN_ANT);

						p_dm_fat_table->crc32_ok_cnt = 0;
						p_dm_fat_table->crc32_fail_cnt = 0;
						odm_set_timer(p_dm_odm, &p_dm_odm->evm_fast_ant_training_timer, p_dm_odm->antdiv_intvl); /* m */
					}
					/* 2 [ Normal state Aux ] */
					else if (p_dm_fat_table->fat_state == NORMAL_STATE_AUX) {
						p_dm_fat_table->main_crc32_ok_cnt = p_dm_fat_table->crc32_ok_cnt;
						p_dm_fat_table->main_crc32_fail_cnt = p_dm_fat_table->crc32_fail_cnt;

						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ start training: AUX]\n"));
						p_dm_fat_table->fat_state = TRAINING_STATE;
						odm_update_rx_idle_ant(p_dm_odm, AUX_ANT);

						p_dm_fat_table->crc32_ok_cnt = 0;
						p_dm_fat_table->crc32_fail_cnt = 0;
						odm_set_timer(p_dm_odm, &p_dm_odm->evm_fast_ant_training_timer, p_dm_odm->antdiv_intvl); /* ms */
					} else if (p_dm_fat_table->fat_state == TRAINING_STATE) {
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Training state ]\n"));
						p_dm_fat_table->fat_state = NORMAL_STATE_MIAN;

						/* 3 [CRC32 statistic] */
						p_dm_fat_table->aux_crc32_ok_cnt = p_dm_fat_table->crc32_ok_cnt;
						p_dm_fat_table->aux_crc32_fail_cnt = p_dm_fat_table->crc32_fail_cnt;

						if ((p_dm_fat_table->main_crc32_ok_cnt > ((p_dm_fat_table->aux_crc32_ok_cnt) << 1)) || ((diff_rssi >= 20) && (rssi_larger_ant == MAIN_ANT))) {
							p_dm_fat_table->target_ant_crc32 = MAIN_ANT;
							force_antenna = true;
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("CRC32 Force Main\n"));
						} else if ((p_dm_fat_table->aux_crc32_ok_cnt > ((p_dm_fat_table->main_crc32_ok_cnt) << 1)) || ((diff_rssi >= 20) && (rssi_larger_ant == AUX_ANT))) {
							p_dm_fat_table->target_ant_crc32 = AUX_ANT;
							force_antenna = true;
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("CRC32 Force Aux\n"));
						} else {
							if (p_dm_fat_table->main_crc32_fail_cnt <= 5)
								p_dm_fat_table->main_crc32_fail_cnt = 5;

							if (p_dm_fat_table->aux_crc32_fail_cnt <= 5)
								p_dm_fat_table->aux_crc32_fail_cnt = 5;

							if (p_dm_fat_table->main_crc32_ok_cnt > p_dm_fat_table->main_crc32_fail_cnt)
								main_above1 = true;

							if (p_dm_fat_table->aux_crc32_ok_cnt > p_dm_fat_table->aux_crc32_fail_cnt)
								aux_above1 = true;

							if (main_above1 == true && aux_above1 == false) {
								force_antenna = true;
								p_dm_fat_table->target_ant_crc32 = MAIN_ANT;
							} else if (main_above1 == false && aux_above1 == true) {
								force_antenna = true;
								p_dm_fat_table->target_ant_crc32 = AUX_ANT;
							} else if (main_above1 == true && aux_above1 == true) {
								main_crc_utility = ((p_dm_fat_table->main_crc32_ok_cnt) << 7) / p_dm_fat_table->main_crc32_fail_cnt;
								aux_crc_utility = ((p_dm_fat_table->aux_crc32_ok_cnt) << 7) / p_dm_fat_table->aux_crc32_fail_cnt;
								p_dm_fat_table->target_ant_crc32 = (main_crc_utility == aux_crc_utility) ? (p_dm_fat_table->pre_target_ant_enhance) : ((main_crc_utility >= aux_crc_utility) ? MAIN_ANT : AUX_ANT);

								if (main_crc_utility != 0 && aux_crc_utility != 0) {
									if (main_crc_utility >= aux_crc_utility)
										utility_ratio = (main_crc_utility << 1) / aux_crc_utility;
									else
										utility_ratio = (aux_crc_utility << 1) / main_crc_utility;
								}
							} else if (main_above1 == false && aux_above1 == false) {
								if (p_dm_fat_table->main_crc32_ok_cnt == 0)
									p_dm_fat_table->main_crc32_ok_cnt = 1;
								if (p_dm_fat_table->aux_crc32_ok_cnt == 0)
									p_dm_fat_table->aux_crc32_ok_cnt = 1;

								main_crc_utility = ((p_dm_fat_table->main_crc32_fail_cnt) << 7) / p_dm_fat_table->main_crc32_ok_cnt;
								aux_crc_utility = ((p_dm_fat_table->aux_crc32_fail_cnt) << 7) / p_dm_fat_table->aux_crc32_ok_cnt;
								p_dm_fat_table->target_ant_crc32 = (main_crc_utility == aux_crc_utility) ? (p_dm_fat_table->pre_target_ant_enhance) : ((main_crc_utility <= aux_crc_utility) ? MAIN_ANT : AUX_ANT);

								if (main_crc_utility != 0 && aux_crc_utility != 0) {
									if (main_crc_utility >= aux_crc_utility)
										utility_ratio = (main_crc_utility << 1) / (aux_crc_utility);
									else
										utility_ratio = (aux_crc_utility << 1) / (main_crc_utility);
								}
							}
						}
						odm_set_mac_reg(p_dm_odm, 0x608, BIT(8), 0);/* NOT Accept CRC32 Error packets. */

						/* 3 [EVM statistic] */
						main_evm = (p_dm_fat_table->main_ant_evm_cnt[i] != 0) ? (p_dm_fat_table->main_ant_evm_sum[i] / p_dm_fat_table->main_ant_evm_cnt[i]) : 0;
						aux_evm = (p_dm_fat_table->aux_ant_evm_cnt[i] != 0) ? (p_dm_fat_table->aux_ant_evm_sum[i] / p_dm_fat_table->aux_ant_evm_cnt[i]) : 0;
						p_dm_fat_table->target_ant_evm = (main_evm == aux_evm) ? (p_dm_fat_table->pre_target_ant_enhance) : ((main_evm >= aux_evm) ? MAIN_ANT : AUX_ANT);

						if ((main_evm == 0 || aux_evm == 0))
							diff_EVM = 0;
						else if (main_evm >= aux_evm)
							diff_EVM = main_evm - aux_evm;
						else
							diff_EVM = aux_evm - main_evm;

						/* 2 [ Decision state ] */
						if (p_dm_fat_table->target_ant_evm == p_dm_fat_table->target_ant_crc32) {
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Decision type 1, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM));

							if ((utility_ratio < 2 && force_antenna == false) && diff_EVM <= 30)
								p_dm_fat_table->target_ant_enhance = p_dm_fat_table->pre_target_ant_enhance;
							else
								p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_evm;
						} else if ((diff_EVM <= 50 && (utility_ratio > 4 && force_antenna == false)) || (force_antenna == true)) {
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Decision type 2, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM));
							p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_crc32;
						} else if (diff_EVM >= 100) {
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Decision type 3, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM));
							p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_evm;
						} else if (utility_ratio >= 6 && force_antenna == false) {
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Decision type 4, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM));
							p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_crc32;
						} else {

							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Decision type 5, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM));

							if (force_antenna == true)
								score_CRC = 3;
							else if (utility_ratio >= 3) /*>0.5*/
								score_CRC = 2;
							else if (utility_ratio >= 2) /*>1*/
								score_CRC = 1;
							else
								score_CRC = 0;

							if (diff_EVM >= 100)
								score_EVM = 2;
							else if (diff_EVM  >= 50)
								score_EVM = 1;
							else
								score_EVM = 0;

							if (score_CRC > score_EVM)
								p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_crc32;
							else if (score_CRC < score_EVM)
								p_dm_fat_table->target_ant_enhance = p_dm_fat_table->target_ant_evm;
							else
								p_dm_fat_table->target_ant_enhance = p_dm_fat_table->pre_target_ant_enhance;
						}
						p_dm_fat_table->pre_target_ant_enhance = p_dm_fat_table->target_ant_enhance;

						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] : MainEVM_Cnt = (( %d ))  , main_evm= ((  %d ))\n", i, p_dm_fat_table->main_ant_evm_cnt[i], main_evm));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] : AuxEVM_Cnt   = (( %d ))  , aux_evm = ((  %d ))\n", i, p_dm_fat_table->aux_ant_evm_cnt[i], aux_evm));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** target_ant_evm = (( %s ))\n", (p_dm_fat_table->target_ant_evm  == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("M_CRC_Ok = (( %d ))  , M_CRC_Fail = ((  %d )), main_crc_utility = (( %d ))\n", p_dm_fat_table->main_crc32_ok_cnt, p_dm_fat_table->main_crc32_fail_cnt, main_crc_utility));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("A_CRC_Ok  = (( %d ))  , A_CRC_Fail = ((  %d )), aux_crc_utility   = ((  %d ))\n", p_dm_fat_table->aux_crc32_ok_cnt, p_dm_fat_table->aux_crc32_fail_cnt, aux_crc_utility));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** target_ant_crc32 = (( %s ))\n", (p_dm_fat_table->target_ant_crc32 == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("****** target_ant_enhance = (( %s ))******\n", (p_dm_fat_table->target_ant_enhance == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));


					}
				} else { /* RSSI< = evm_rssi_th_low */
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ <TH_L: escape from > TH_L ]\n"));
					odm_evm_fast_ant_reset(p_dm_odm);
				}
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[escape from> TH_H || EVM_method_enable==1]\n"));
				odm_evm_fast_ant_reset(p_dm_odm);
			}
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[multi-Client]\n"));
			odm_evm_fast_ant_reset(p_dm_odm);
		}
	}
}

void
odm_evm_fast_ant_training_callback(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("******odm_evm_fast_ant_training_callback******\n"));
	odm_hw_ant_div(p_dm_odm);
}
#endif

void
odm_hw_ant_div(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	i, min_max_rssi = 0xFF,  ant_div_max_rssi = 0, max_rssi = 0, local_max_rssi;
	u32	main_rssi, aux_rssi, mian_cnt, aux_cnt;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u8	rx_idle_ant = p_dm_fat_table->rx_idle_ant, target_ant = 7;
	struct _dynamic_initial_gain_threshold_	*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct sta_info	*p_entry;

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	struct _BF_DIV_COEX_    *p_dm_bdc_table = &p_dm_odm->dm_bdc_table;
	u32	TH1 = 500000;
	u32	TH2 = 10000000;
	u32	ma_rx_temp, degrade_TP_temp, improve_TP_temp;
	u8	monitor_rssi_threshold = 30;

	p_dm_bdc_table->BF_pass = true;
	p_dm_bdc_table->DIV_pass = true;
	p_dm_bdc_table->is_all_div_sta_idle = true;
	p_dm_bdc_table->is_all_bf_sta_idle = true;
	p_dm_bdc_table->num_bf_tar = 0 ;
	p_dm_bdc_table->num_div_tar = 0;
	p_dm_bdc_table->num_client = 0;
#endif
#endif

	if (!p_dm_odm->is_linked) { /* is_linked==False */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));

		if (p_dm_fat_table->is_become_linked == true) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
			odm_update_rx_idle_ant(p_dm_odm, MAIN_ANT);
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
			p_dm_odm->antdiv_period = 0;

			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
		return;
	} else {
		if (p_dm_fat_table->is_become_linked == false) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));
			odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
			/*odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);*/

			/* if(p_dm_odm->support_ic_type == ODM_RTL8821 ) */
			/* odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), 0); */ /* CCK AntDiv function disable */

			/* #if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */
			/* else if(p_dm_odm->support_ic_type == ODM_RTL8881A) */
			/* odm_set_bb_reg(p_dm_odm, 0x800, BIT(25), 0); */ /* CCK AntDiv function disable */
			/* #endif */

			/* else if(p_dm_odm->support_ic_type == ODM_RTL8723B ||p_dm_odm->support_ic_type == ODM_RTL8812) */
			/* odm_set_bb_reg(p_dm_odm, 0xA00, BIT(15), 0); */ /* CCK AntDiv function disable */

			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;

			if (p_dm_odm->support_ic_type == ODM_RTL8723B && p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV) {
				odm_set_bb_reg(p_dm_odm, 0x930, 0xF0, 8); /* DPDT_P = ANTSEL[0]   */ /* for 8723B AntDiv function patch.  BB  Dino  130412 */
				odm_set_bb_reg(p_dm_odm, 0x930, 0xF, 8); /* DPDT_N = ANTSEL[0] */
			}

			/* 2 BDC Init */
#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
			odm_bdc_init(p_dm_odm);
#endif
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
			odm_evm_fast_ant_reset(p_dm_odm);
#endif
		}
	}

	if (*(p_dm_fat_table->p_force_tx_ant_by_desc) == false) {
		if (p_dm_odm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
	}

#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (p_dm_odm->antdiv_evm_en == 1) {
		odm_evm_enhance_ant_div(p_dm_odm);
		if (p_dm_fat_table->fat_state != NORMAL_STATE_MIAN)
			return;
	} else
		odm_evm_fast_ant_reset(p_dm_odm);
#endif

	/* 2 BDC mode Arbitration */
#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (p_dm_odm->antdiv_evm_en == 0 || p_dm_fat_table->EVM_method_enable == 0)
		odm_bf_ant_div_mode_arbitration(p_dm_odm);
#endif
#endif

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_entry = p_dm_odm->p_odm_sta_info[i];
		if (IS_STA_VALID(p_entry)) {
			/* 2 Caculate RSSI per Antenna */
			if ((p_dm_fat_table->main_ant_cnt[i] != 0) || (p_dm_fat_table->aux_ant_cnt[i] != 0)) {
				mian_cnt = p_dm_fat_table->main_ant_cnt[i];
				aux_cnt = p_dm_fat_table->aux_ant_cnt[i];
				main_rssi = (mian_cnt != 0) ? (p_dm_fat_table->main_ant_sum[i] / mian_cnt) : 0;
				aux_rssi = (aux_cnt != 0) ? (p_dm_fat_table->aux_ant_sum[i] / aux_cnt) : 0;
				target_ant = (mian_cnt == aux_cnt) ? p_dm_fat_table->rx_idle_ant : ((mian_cnt >= aux_cnt) ? MAIN_ANT : AUX_ANT); /*Use counter number for OFDM*/

			} else {	/*CCK only case*/
				mian_cnt = p_dm_fat_table->main_ant_cnt_cck[i];
				aux_cnt = p_dm_fat_table->aux_ant_cnt_cck[i];
				main_rssi = (mian_cnt != 0) ? (p_dm_fat_table->main_ant_sum_cck[i] / mian_cnt) : 0;
				aux_rssi = (aux_cnt != 0) ? (p_dm_fat_table->aux_ant_sum_cck[i] / aux_cnt) : 0;
				target_ant = (main_rssi == aux_rssi) ? p_dm_fat_table->rx_idle_ant : ((main_rssi >= aux_rssi) ? MAIN_ANT : AUX_ANT); /*Use RSSI for CCK only case*/
			}

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] : Main_Cnt = (( %d ))  ,  CCK_Main_Cnt = (( %d )) ,  main_rssi= ((  %d ))\n", i, p_dm_fat_table->main_ant_cnt[i], p_dm_fat_table->main_ant_cnt_cck[i], main_rssi));
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] : Aux_Cnt   = (( %d ))  , CCK_Aux_Cnt   = (( %d )) ,  aux_rssi = ((  %d ))\n", i, p_dm_fat_table->aux_ant_cnt[i], p_dm_fat_table->aux_ant_cnt_cck[i], aux_rssi));
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** MAC ID:[ %d ] , target_ant = (( %s ))\n", i ,( target_ant ==MAIN_ANT)?"MAIN_ANT":"AUX_ANT")); */

			local_max_rssi = (main_rssi > aux_rssi) ? main_rssi : aux_rssi;
			/* 2 Select max_rssi for DIG */
			if ((local_max_rssi > ant_div_max_rssi) && (local_max_rssi < 40))
				ant_div_max_rssi = local_max_rssi;
			if (local_max_rssi > max_rssi)
				max_rssi = local_max_rssi;

			/* 2 Select RX Idle Antenna */
			if ((local_max_rssi != 0) && (local_max_rssi < min_max_rssi)) {
				rx_idle_ant = target_ant;
				min_max_rssi = local_max_rssi;
			}

#ifdef ODM_EVM_ENHANCE_ANTDIV
			if (p_dm_odm->antdiv_evm_en == 1) {
				if (p_dm_fat_table->target_ant_enhance != 0xFF) {
					target_ant = p_dm_fat_table->target_ant_enhance;
					rx_idle_ant = p_dm_fat_table->target_ant_enhance;
				}
			}
#endif

			/* 2 Select TX Antenna */
			if (p_dm_odm->ant_div_type != CGCS_RX_HW_ANTDIV) {
#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
				if (p_dm_bdc_table->w_bfee_client[i] == 0)
#endif
#endif
				{
					odm_update_tx_ant(p_dm_odm, target_ant, i);
				}
			}

			/* ------------------------------------------------------------ */

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

			p_dm_bdc_table->num_client++;

			if (p_dm_bdc_table->bdc_mode == BDC_MODE_2 || p_dm_bdc_table->bdc_mode == BDC_MODE_3) {
				/* 2 Byte counter */

				ma_rx_temp = (p_entry->rx_byte_cnt_low_maw) << 3 ; /* RX  TP   ( bit /sec) */

				if (p_dm_bdc_table->BDC_state == bdc_bfer_train_state)
					p_dm_bdc_table->MA_rx_TP_DIV[i] =  ma_rx_temp ;
				else
					p_dm_bdc_table->MA_rx_TP[i] = ma_rx_temp ;

				if ((ma_rx_temp < TH2)   && (ma_rx_temp > TH1) && (local_max_rssi <= monitor_rssi_threshold)) {
					if (p_dm_bdc_table->w_bfer_client[i] == 1) { /* Bfer_Target */
						p_dm_bdc_table->num_bf_tar++;

						if (p_dm_bdc_table->BDC_state == BDC_DECISION_STATE && p_dm_bdc_table->bdc_try_flag == 0) {
							improve_TP_temp = (p_dm_bdc_table->MA_rx_TP_DIV[i] * 9) >> 3 ; /* * 1.125 */
							p_dm_bdc_table->BF_pass = (p_dm_bdc_table->MA_rx_TP[i] > improve_TP_temp) ? true : false;
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] :  { MA_rx_TP,improve_TP_temp, MA_rx_TP_DIV,  BF_pass}={ %d,  %d, %d , %d }\n", i, p_dm_bdc_table->MA_rx_TP[i], improve_TP_temp, p_dm_bdc_table->MA_rx_TP_DIV[i], p_dm_bdc_table->BF_pass));
						}
					} else { /* DIV_Target */
						p_dm_bdc_table->num_div_tar++;

						if (p_dm_bdc_table->BDC_state == BDC_DECISION_STATE && p_dm_bdc_table->bdc_try_flag == 0) {
							degrade_TP_temp = (p_dm_bdc_table->MA_rx_TP_DIV[i] * 5) >> 3; /* * 0.625 */
							p_dm_bdc_table->DIV_pass = (p_dm_bdc_table->MA_rx_TP[i] > degrade_TP_temp) ? true : false;
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] :  { MA_rx_TP, degrade_TP_temp, MA_rx_TP_DIV,  DIV_pass}=\n{ %d,  %d, %d , %d }\n", i, p_dm_bdc_table->MA_rx_TP[i], degrade_TP_temp, p_dm_bdc_table->MA_rx_TP_DIV[i], p_dm_bdc_table->DIV_pass));
						}
					}
				}

				if (ma_rx_temp > TH1) {
					if (p_dm_bdc_table->w_bfer_client[i] == 1) /* Bfer_Target */
						p_dm_bdc_table->is_all_bf_sta_idle = false;
					else/* DIV_Target */
						p_dm_bdc_table->is_all_div_sta_idle = false;
				}

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] :  { BFmeeCap, BFmerCap}  = { %d , %d }\n", i, p_dm_bdc_table->w_bfee_client[i], p_dm_bdc_table->w_bfer_client[i]));

				if (p_dm_bdc_table->BDC_state == bdc_bfer_train_state)
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] :    MA_rx_TP_DIV = (( %d ))\n", i, p_dm_bdc_table->MA_rx_TP_DIV[i]));

				else
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Client[ %d ] :    MA_rx_TP = (( %d ))\n", i, p_dm_bdc_table->MA_rx_TP[i]));

			}
#endif
#endif

		}

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		if (p_dm_bdc_table->bdc_try_flag == 0)
#endif
#endif
		{
			phydm_antdiv_reset_statistic(p_dm_odm, i);
		}
	}



	/* 2 Set RX Idle Antenna & TX Antenna(Because of HW Bug ) */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** rx_idle_ant = (( %s ))\n\n", (rx_idle_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (p_dm_bdc_table->bdc_mode == BDC_MODE_1 || p_dm_bdc_table->bdc_mode == BDC_MODE_3) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** bdc_rx_idle_update_counter = (( %d ))\n", p_dm_bdc_table->bdc_rx_idle_update_counter));

		if (p_dm_bdc_table->bdc_rx_idle_update_counter == 1) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***Update RxIdle Antenna!!!\n"));
			p_dm_bdc_table->bdc_rx_idle_update_counter = 30;
			odm_update_rx_idle_ant(p_dm_odm, rx_idle_ant);
		} else {
			p_dm_bdc_table->bdc_rx_idle_update_counter--;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***NOT update RxIdle Antenna because of BF  ( need to fix TX-ant)\n"));
		}
	} else
#endif
#endif
		odm_update_rx_idle_ant(p_dm_odm, rx_idle_ant);
#else

	odm_update_rx_idle_ant(p_dm_odm, rx_idle_ant);

#endif/* #if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */



	/* 2 BDC Main Algorithm */
#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (p_dm_odm->antdiv_evm_en == 0 || p_dm_fat_table->EVM_method_enable == 0)
		odm_bd_ccoex_bfee_rx_div_arbitration(p_dm_odm);
#endif
#endif

	if (ant_div_max_rssi == 0)
		p_dm_dig_table->ant_div_rssi_max = p_dm_odm->rssi_min;
	else
		p_dm_dig_table->ant_div_rssi_max = ant_div_max_rssi;

	p_dm_dig_table->RSSI_max = max_rssi;
}



#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY

void
odm_s0s1_sw_ant_div_reset(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_		*p_dm_swat_table	= &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table		= &p_dm_odm->dm_fat_table;

	p_dm_fat_table->is_become_linked  = false;
	p_dm_swat_table->try_flag = SWAW_STEP_INIT;
	p_dm_swat_table->double_chk_flag = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_s0s1_sw_ant_div_reset(): p_dm_fat_table->is_become_linked = %d\n", p_dm_fat_table->is_become_linked));
}

void
odm_s0s1_sw_ant_div(
	void			*p_dm_void,
	u8			step
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_			*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	u32			i, min_max_rssi = 0xFF, local_max_rssi, local_min_rssi;
	u32			main_rssi, aux_rssi;
	u8			high_traffic_train_time_u = 0x32, high_traffic_train_time_l = 0, train_time_temp;
	u8			low_traffic_train_time_u = 200, low_traffic_train_time_l = 0;
	u8			rx_idle_ant = p_dm_swat_table->pre_antenna, target_ant, next_ant = 0;
	struct sta_info		*p_entry = NULL;
	u32			value32;
	u32			main_ant_sum;
	u32			aux_ant_sum;
	u32			main_ant_cnt;
	u32			aux_ant_cnt;


	if (!p_dm_odm->is_linked) { /* is_linked==False */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));
		if (p_dm_fat_table->is_become_linked == true) {
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
			if (p_dm_odm->support_ic_type == ODM_RTL8723B) {

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set REG 948[9:6]=0x0\n"));
				odm_set_bb_reg(p_dm_odm, 0x948, (BIT(9) | BIT(8) | BIT(7) | BIT(6)), 0x0);
			}
			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
		return;
	} else {
		if (p_dm_fat_table->is_become_linked == false) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));

			if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
				value32 = odm_get_bb_reg(p_dm_odm, 0x864, BIT(5) | BIT(4) | BIT(3));

#if (RTL8723B_SUPPORT == 1)
				if (value32 == 0x0)
					odm_update_rx_idle_ant_8723b(p_dm_odm, MAIN_ANT, ANT1_2G, ANT2_2G);
				else if (value32 == 0x1)
					odm_update_rx_idle_ant_8723b(p_dm_odm, AUX_ANT, ANT2_2G, ANT1_2G);
#endif

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("8723B: First link! Force antenna to  %s\n", (value32 == 0x0 ? "MAIN" : "AUX")));
			}
			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
	}

	if (*(p_dm_fat_table->p_force_tx_ant_by_desc) == false) {
		if (p_dm_odm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[%d] { try_flag=(( %d )), step=(( %d )), double_chk_flag = (( %d )) }\n",
		__LINE__, p_dm_swat_table->try_flag, step, p_dm_swat_table->double_chk_flag));

	/* Handling step mismatch condition. */
	/* Peak step is not finished at last time. Recover the variable and check again. */
	if (step != p_dm_swat_table->try_flag) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[step != try_flag]    Need to Reset After Link\n"));
		odm_sw_ant_div_rest_after_link(p_dm_odm);
	}

	if (p_dm_swat_table->try_flag == SWAW_STEP_INIT) {

		p_dm_swat_table->try_flag = SWAW_STEP_PEEK;
		p_dm_swat_table->train_time_flag = 0;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[set try_flag = 0]  Prepare for peek!\n\n"));
		return;

	} else {

		/* 1 Normal state (Begin Trying) */
		if (p_dm_swat_table->try_flag == SWAW_STEP_PEEK) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TxOkCnt=(( %llu )), RxOkCnt=(( %llu )), traffic_load = (%d))\n", p_dm_odm->cur_tx_ok_cnt, p_dm_odm->cur_rx_ok_cnt, p_dm_odm->traffic_load));

			if (p_dm_odm->traffic_load == TRAFFIC_HIGH) {
				train_time_temp = p_dm_swat_table->train_time ;

				if (p_dm_swat_table->train_time_flag == 3) {
					high_traffic_train_time_l = 0xa;

					if (train_time_temp <= 16)
						train_time_temp = high_traffic_train_time_l;
					else
						train_time_temp -= 16;

				} else if (p_dm_swat_table->train_time_flag == 2) {
					train_time_temp -= 8;
					high_traffic_train_time_l = 0xf;
				} else if (p_dm_swat_table->train_time_flag == 1) {
					train_time_temp -= 4;
					high_traffic_train_time_l = 0x1e;
				} else if (p_dm_swat_table->train_time_flag == 0) {
					train_time_temp += 8;
					high_traffic_train_time_l = 0x28;
				}


				/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** train_time_temp = ((%d))\n",train_time_temp)); */

				/* -- */
				if (train_time_temp > high_traffic_train_time_u)
					train_time_temp = high_traffic_train_time_u;

				else if (train_time_temp < high_traffic_train_time_l)
					train_time_temp = high_traffic_train_time_l;

				p_dm_swat_table->train_time = train_time_temp; /*10ms~200ms*/

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("train_time_flag=((%d)), train_time=((%d))\n", p_dm_swat_table->train_time_flag, p_dm_swat_table->train_time));

			} else if ((p_dm_odm->traffic_load == TRAFFIC_MID) || (p_dm_odm->traffic_load == TRAFFIC_LOW)) {

				train_time_temp = p_dm_swat_table->train_time ;

				if (p_dm_swat_table->train_time_flag == 3) {
					low_traffic_train_time_l = 10;
					if (train_time_temp < 50)
						train_time_temp = low_traffic_train_time_l;
					else
						train_time_temp -= 50;
				} else if (p_dm_swat_table->train_time_flag == 2) {
					train_time_temp -= 30;
					low_traffic_train_time_l = 36;
				} else if (p_dm_swat_table->train_time_flag == 1) {
					train_time_temp -= 10;
					low_traffic_train_time_l = 40;
				} else {

					train_time_temp += 10;
					low_traffic_train_time_l = 50;
				}

				/* -- */
				if (train_time_temp >= low_traffic_train_time_u)
					train_time_temp = low_traffic_train_time_u;

				else if (train_time_temp <= low_traffic_train_time_l)
					train_time_temp = low_traffic_train_time_l;

				p_dm_swat_table->train_time = train_time_temp; /*10ms~200ms*/

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("train_time_flag=((%d)) , train_time=((%d))\n", p_dm_swat_table->train_time_flag, p_dm_swat_table->train_time));

			} else {
				p_dm_swat_table->train_time = 0xc8; /*200ms*/

			}

			/* ----------------- */

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Current min_max_rssi is ((%d))\n", p_dm_fat_table->min_max_rssi));

			/* ---reset index--- */
			if (p_dm_swat_table->reset_idx >= RSSI_CHECK_RESET_PERIOD) {

				p_dm_fat_table->min_max_rssi = 0;
				p_dm_swat_table->reset_idx = 0;
			}
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reset_idx = (( %d ))\n", p_dm_swat_table->reset_idx));

			p_dm_swat_table->reset_idx++;

			/* ---double check flag--- */
			if ((p_dm_fat_table->min_max_rssi > RSSI_CHECK_THRESHOLD) && (p_dm_swat_table->double_chk_flag == 0)) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" min_max_rssi is ((%d)), and > %d\n",
					p_dm_fat_table->min_max_rssi, RSSI_CHECK_THRESHOLD));

				p_dm_swat_table->double_chk_flag = 1;
				p_dm_swat_table->try_flag = SWAW_STEP_DETERMINE;
				p_dm_swat_table->rssi_trying = 0;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Test the current ant for (( %d )) ms again\n", p_dm_swat_table->train_time));
				odm_update_rx_idle_ant(p_dm_odm, p_dm_fat_table->rx_idle_ant);
				odm_set_timer(p_dm_odm, &(p_dm_swat_table->phydm_sw_antenna_switch_timer), p_dm_swat_table->train_time); /*ms*/
				return;
			}

			next_ant = (p_dm_fat_table->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;

			p_dm_swat_table->try_flag = SWAW_STEP_DETERMINE;

			if (p_dm_swat_table->reset_idx <= 1)
				p_dm_swat_table->rssi_trying = 2;
			else
				p_dm_swat_table->rssi_trying = 1;

			odm_s0s1_sw_ant_div_by_ctrl_frame(p_dm_odm, SWAW_STEP_PEEK);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[set try_flag=1]  Normal state:  Begin Trying!!\n"));

		} else if ((p_dm_swat_table->try_flag == SWAW_STEP_DETERMINE) && (p_dm_swat_table->double_chk_flag == 0)) {

			next_ant = (p_dm_fat_table->rx_idle_ant  == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
			p_dm_swat_table->rssi_trying--;
		}

		/* 1 Decision state */
		if ((p_dm_swat_table->try_flag == SWAW_STEP_DETERMINE) && (p_dm_swat_table->rssi_trying == 0)) {

			boolean is_by_ctrl_frame = false;
			u64	pkt_cnt_total = 0;

			for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
				p_entry = p_dm_odm->p_odm_sta_info[i];
				if (IS_STA_VALID(p_entry)) {
					/* 2 Caculate RSSI per Antenna */

					main_ant_sum = (u32)p_dm_fat_table->main_ant_sum[i] + (u32)p_dm_fat_table->main_ant_sum_cck[i];
					aux_ant_sum = (u32)p_dm_fat_table->aux_ant_sum[i] + (u32)p_dm_fat_table->aux_ant_sum_cck[i];
					main_ant_cnt = (u32)p_dm_fat_table->main_ant_cnt[i] + (u32)p_dm_fat_table->main_ant_cnt_cck[i];
					aux_ant_cnt = (u32)p_dm_fat_table->aux_ant_cnt[i] + (u32)p_dm_fat_table->aux_ant_cnt_cck[i];

					main_rssi = (main_ant_cnt != 0) ? (main_ant_sum / main_ant_cnt) : 0;
					aux_rssi = (aux_ant_cnt != 0) ? (aux_ant_sum / aux_ant_cnt) : 0;

					if (p_dm_fat_table->main_ant_cnt[i] <= 1 && p_dm_fat_table->main_ant_cnt_cck[i] >= 1)
						main_rssi = 0;

					if (p_dm_fat_table->aux_ant_cnt[i] <= 1 && p_dm_fat_table->aux_ant_cnt_cck[i] >= 1)
						aux_rssi = 0;

					target_ant = (main_rssi == aux_rssi) ? p_dm_swat_table->pre_antenna : ((main_rssi >= aux_rssi) ? MAIN_ANT : AUX_ANT);
					local_max_rssi = (main_rssi >= aux_rssi) ? main_rssi : aux_rssi;
					local_min_rssi = (main_rssi >= aux_rssi) ? aux_rssi : main_rssi;

					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  CCK_counter_main = (( %d ))  , CCK_counter_aux= ((  %d ))\n", p_dm_fat_table->main_ant_cnt_cck[i], p_dm_fat_table->aux_ant_cnt_cck[i]));
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  OFDM_counter_main = (( %d ))  , OFDM_counter_aux= ((  %d ))\n", p_dm_fat_table->main_ant_cnt[i], p_dm_fat_table->aux_ant_cnt[i]));
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Main_Cnt = (( %d ))  , main_rssi= ((  %d ))\n", main_ant_cnt, main_rssi));
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Aux_Cnt   = (( %d ))  , aux_rssi = ((  %d ))\n", aux_ant_cnt, aux_rssi));
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** MAC ID:[ %d ] , target_ant = (( %s ))\n", i, (target_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT"));

					/* 2 Select RX Idle Antenna */

					if (local_max_rssi != 0 && local_max_rssi < min_max_rssi) {
						rx_idle_ant = target_ant;
						min_max_rssi = local_max_rssi;
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** local_max_rssi-local_min_rssi = ((%d))\n", (local_max_rssi - local_min_rssi)));

						if ((local_max_rssi - local_min_rssi) > 8) {
							if (local_min_rssi != 0)
								p_dm_swat_table->train_time_flag = 3;
							else {
								if (min_max_rssi > RSSI_CHECK_THRESHOLD)
									p_dm_swat_table->train_time_flag = 0;
								else
									p_dm_swat_table->train_time_flag = 3;
							}
						} else if ((local_max_rssi - local_min_rssi) > 5)
							p_dm_swat_table->train_time_flag = 2;
						else if ((local_max_rssi - local_min_rssi) > 2)
							p_dm_swat_table->train_time_flag = 1;
						else
							p_dm_swat_table->train_time_flag = 0;

					}

					/* 2 Select TX Antenna */
					if (target_ant == MAIN_ANT)
						p_dm_fat_table->antsel_a[i] = ANT1_2G;
					else
						p_dm_fat_table->antsel_a[i] = ANT2_2G;

				}
				phydm_antdiv_reset_statistic(p_dm_odm, i);
				pkt_cnt_total += (main_ant_cnt + aux_ant_cnt);
			}

			if (p_dm_swat_table->is_sw_ant_div_by_ctrl_frame) {
				odm_s0s1_sw_ant_div_by_ctrl_frame(p_dm_odm, SWAW_STEP_DETERMINE);
				is_by_ctrl_frame = true;
			}

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Control frame packet counter = %d, data frame packet counter = %llu\n",
				p_dm_swat_table->pkt_cnt_sw_ant_div_by_ctrl_frame, pkt_cnt_total));

			if (min_max_rssi == 0xff || ((pkt_cnt_total < (p_dm_swat_table->pkt_cnt_sw_ant_div_by_ctrl_frame >> 1)) && p_dm_odm->phy_dbg_info.num_qry_beacon_pkt < 2)) {
				min_max_rssi = 0;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Check RSSI of control frame because min_max_rssi == 0xff\n"));
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("is_by_ctrl_frame = %d\n", is_by_ctrl_frame));

				if (is_by_ctrl_frame) {
					main_rssi = (p_dm_fat_table->main_ant_ctrl_frame_cnt != 0) ? (p_dm_fat_table->main_ant_ctrl_frame_sum / p_dm_fat_table->main_ant_ctrl_frame_cnt) : 0;
					aux_rssi = (p_dm_fat_table->aux_ant_ctrl_frame_cnt != 0) ? (p_dm_fat_table->aux_ant_ctrl_frame_sum / p_dm_fat_table->aux_ant_ctrl_frame_cnt) : 0;

					if (p_dm_fat_table->main_ant_ctrl_frame_cnt <= 1 && p_dm_fat_table->cck_ctrl_frame_cnt_main >= 1)
						main_rssi = 0;

					if (p_dm_fat_table->aux_ant_ctrl_frame_cnt <= 1 && p_dm_fat_table->cck_ctrl_frame_cnt_aux >= 1)
						aux_rssi = 0;

					if (main_rssi != 0 || aux_rssi != 0) {
						rx_idle_ant = (main_rssi == aux_rssi) ? p_dm_swat_table->pre_antenna : ((main_rssi >= aux_rssi) ? MAIN_ANT : AUX_ANT);
						local_max_rssi = (main_rssi >= aux_rssi) ? main_rssi : aux_rssi;
						local_min_rssi = (main_rssi >= aux_rssi) ? aux_rssi : main_rssi;

						if ((local_max_rssi - local_min_rssi) > 8)
							p_dm_swat_table->train_time_flag = 3;
						else if ((local_max_rssi - local_min_rssi) > 5)
							p_dm_swat_table->train_time_flag = 2;
						else if ((local_max_rssi - local_min_rssi) > 2)
							p_dm_swat_table->train_time_flag = 1;
						else
							p_dm_swat_table->train_time_flag = 0;

						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Control frame: main_rssi = %d, aux_rssi = %d\n", main_rssi, aux_rssi));
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("rx_idle_ant decided by control frame = %s\n", (rx_idle_ant == MAIN_ANT ? "MAIN" : "AUX")));
					}
				}
			}

			p_dm_fat_table->min_max_rssi = min_max_rssi;
			p_dm_swat_table->try_flag = SWAW_STEP_PEEK;

			if (p_dm_swat_table->double_chk_flag == 1) {
				p_dm_swat_table->double_chk_flag = 0;

				if (p_dm_fat_table->min_max_rssi > RSSI_CHECK_THRESHOLD) {

					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [Double check] min_max_rssi ((%d)) > %d again!!\n",
						p_dm_fat_table->min_max_rssi, RSSI_CHECK_THRESHOLD));

					odm_update_rx_idle_ant(p_dm_odm, rx_idle_ant);

					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[reset try_flag = 0] Training accomplished !!!]\n\n\n"));
					return;
				} else {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [Double check] min_max_rssi ((%d)) <= %d !!\n",
						p_dm_fat_table->min_max_rssi, RSSI_CHECK_THRESHOLD));

					next_ant = (p_dm_fat_table->rx_idle_ant  == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
					p_dm_swat_table->try_flag = SWAW_STEP_PEEK;
					p_dm_swat_table->reset_idx = RSSI_CHECK_RESET_PERIOD;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[set try_flag=0]  Normal state:  Need to tryg again!!\n\n\n"));
					return;
				}
			} else {
				if (p_dm_fat_table->min_max_rssi < RSSI_CHECK_THRESHOLD)
					p_dm_swat_table->reset_idx = RSSI_CHECK_RESET_PERIOD;

				p_dm_swat_table->pre_antenna = rx_idle_ant;
				odm_update_rx_idle_ant(p_dm_odm, rx_idle_ant);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[reset try_flag = 0] Training accomplished !!!] \n\n\n"));
				return;
			}

		}

	}

	/* 1 4.Change TRX antenna */

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("rssi_trying = (( %d )),    ant: (( %s )) >>> (( %s ))\n",
		p_dm_swat_table->rssi_trying, (p_dm_fat_table->rx_idle_ant  == MAIN_ANT ? "MAIN" : "AUX"), (next_ant == MAIN_ANT ? "MAIN" : "AUX")));

	odm_update_rx_idle_ant(p_dm_odm, next_ant);

	/* 1 5.Reset Statistics */

	p_dm_fat_table->rx_idle_ant  = next_ant;

	/* 1 6.Set next timer   (Trying state) */

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" Test ((%s)) ant for (( %d )) ms\n", (next_ant == MAIN_ANT ? "MAIN" : "AUX"), p_dm_swat_table->train_time));
	odm_set_timer(p_dm_odm, &(p_dm_swat_table->phydm_sw_antenna_switch_timer), p_dm_swat_table->train_time); /*ms*/
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_sw_antdiv_callback(
	struct timer_list		*p_timer
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_timer->Adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct _sw_antenna_switch_			*p_dm_swat_table = &p_hal_data->DM_OutSrc.dm_swat_table;

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_swat_table->phydm_sw_antenna_switch_workitem);
#else
	{
		/* dbg_print("SW_antdiv_Callback"); */
		odm_s0s1_sw_ant_div(&p_hal_data->DM_OutSrc, SWAW_STEP_DETERMINE);
	}
#endif
#else
	odm_schedule_work_item(&p_dm_swat_table->phydm_sw_antenna_switch_workitem);
#endif
}
void
odm_sw_antdiv_workitem_callback(
	void            *p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	/* dbg_print("SW_antdiv_Workitem_Callback"); */
	odm_s0s1_sw_ant_div(&p_hal_data->DM_OutSrc, SWAW_STEP_DETERMINE);
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

void
odm_sw_antdiv_workitem_callback(
	void	*p_context
)
{
	struct _ADAPTER *
	p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE
	*p_hal_data = GET_HAL_DATA(p_adapter);

	/*dbg_print("SW_antdiv_Workitem_Callback");*/
	odm_s0s1_sw_ant_div(&p_hal_data->odmpriv, SWAW_STEP_DETERMINE);
}

void
odm_sw_antdiv_callback(void *function_context)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)function_context;
	struct _ADAPTER	*padapter = p_dm_odm->adapter;
	if (padapter->net_closed == _TRUE)
		return;

#if 0 /* Can't do I/O in timer callback*/
	odm_s0s1_sw_ant_div(p_dm_odm, SWAW_STEP_DETERMINE);
#else
	rtw_run_in_thread_cmd(padapter, odm_sw_antdiv_workitem_callback, padapter);
#endif
}


#endif

void
odm_s0s1_sw_ant_div_by_ctrl_frame(
	void			*p_dm_void,
	u8			step
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _sw_antenna_switch_	*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	switch (step) {
	case SWAW_STEP_PEEK:
		p_dm_swat_table->pkt_cnt_sw_ant_div_by_ctrl_frame = 0;
		p_dm_swat_table->is_sw_ant_div_by_ctrl_frame = true;
		p_dm_fat_table->main_ant_ctrl_frame_cnt = 0;
		p_dm_fat_table->aux_ant_ctrl_frame_cnt = 0;
		p_dm_fat_table->main_ant_ctrl_frame_sum = 0;
		p_dm_fat_table->aux_ant_ctrl_frame_sum = 0;
		p_dm_fat_table->cck_ctrl_frame_cnt_main = 0;
		p_dm_fat_table->cck_ctrl_frame_cnt_aux = 0;
		p_dm_fat_table->ofdm_ctrl_frame_cnt_main = 0;
		p_dm_fat_table->ofdm_ctrl_frame_cnt_aux = 0;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_S0S1_SwAntDivForAPMode(): Start peek and reset counter\n"));
		break;
	case SWAW_STEP_DETERMINE:
		p_dm_swat_table->is_sw_ant_div_by_ctrl_frame = false;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_S0S1_SwAntDivForAPMode(): Stop peek\n"));
		break;
	default:
		p_dm_swat_table->is_sw_ant_div_by_ctrl_frame = false;
		break;
	}
}

void
odm_antsel_statistics_of_ctrl_frame(
	void			*p_dm_void,
	u8			antsel_tr_mux,
	u32			rx_pwdb_all

)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	if (antsel_tr_mux == ANT1_2G) {
		p_dm_fat_table->main_ant_ctrl_frame_sum += rx_pwdb_all;
		p_dm_fat_table->main_ant_ctrl_frame_cnt++;
	} else {
		p_dm_fat_table->aux_ant_ctrl_frame_sum += rx_pwdb_all;
		p_dm_fat_table->aux_ant_ctrl_frame_cnt++;
	}
}

void
odm_s0s1_sw_ant_div_by_ctrl_frame_process_rssi(
	void			*p_dm_void,
	void			*p_phy_info_void,
	void			*p_pkt_info_void
	/*	struct _odm_phy_status_info_*		p_phy_info, */
	/*	struct _odm_per_pkt_info_*		p_pktinfo */
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _odm_phy_status_info_	*p_phy_info = (struct _odm_phy_status_info_ *)p_phy_info_void;
	struct _odm_per_pkt_info_	*p_pktinfo = (struct _odm_per_pkt_info_ *)p_pkt_info_void;
	struct _sw_antenna_switch_	*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	boolean		is_cck_rate;

	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (p_dm_odm->ant_div_type != S0S1_SW_ANTDIV)
		return;

	/* In try state */
	if (!p_dm_swat_table->is_sw_ant_div_by_ctrl_frame)
		return;

	/* No HW error and match receiver address */
	if (!p_pktinfo->is_to_self)
		return;

	p_dm_swat_table->pkt_cnt_sw_ant_div_by_ctrl_frame++;
	is_cck_rate = ((p_pktinfo->data_rate >= DESC_RATE1M) && (p_pktinfo->data_rate <= DESC_RATE11M)) ? true : false;

	if (is_cck_rate) {
		p_dm_fat_table->antsel_rx_keep_0 = (p_dm_fat_table->rx_idle_ant == MAIN_ANT) ? ANT1_2G : ANT2_2G;

		if (p_dm_fat_table->antsel_rx_keep_0 == ANT1_2G)
			p_dm_fat_table->cck_ctrl_frame_cnt_main++;
		else
			p_dm_fat_table->cck_ctrl_frame_cnt_aux++;

		odm_antsel_statistics_of_ctrl_frame(p_dm_odm, p_dm_fat_table->antsel_rx_keep_0, p_phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A]);
	} else {
		if (p_dm_fat_table->antsel_rx_keep_0 == ANT1_2G)
			p_dm_fat_table->ofdm_ctrl_frame_cnt_main++;
		else
			p_dm_fat_table->ofdm_ctrl_frame_cnt_aux++;

		odm_antsel_statistics_of_ctrl_frame(p_dm_odm, p_dm_fat_table->antsel_rx_keep_0, p_phy_info->rx_pwdb_all);
	}
}

#endif /* #if (RTL8723B_SUPPORT == 1) || (RTL8821A_SUPPORT == 1) */




void
odm_set_next_mac_addr_target(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct sta_info	*p_entry;
	u32			value32, i;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_set_next_mac_addr_target() ==>\n"));

	if (p_dm_odm->is_linked) {
		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {

			if ((p_dm_fat_table->train_idx + 1) == ODM_ASSOCIATE_ENTRY_NUM)
				p_dm_fat_table->train_idx = 0;
			else
				p_dm_fat_table->train_idx++;

			p_entry = p_dm_odm->p_odm_sta_info[p_dm_fat_table->train_idx];

			if (IS_STA_VALID(p_entry)) {

				/*Match MAC ADDR*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
				value32 = (p_entry->hwaddr[5] << 8) | p_entry->hwaddr[4];
#else
				value32 = (p_entry->MacAddr[5] << 8) | p_entry->MacAddr[4];
#endif

				odm_set_mac_reg(p_dm_odm, 0x7b4, 0xFFFF, value32);/*0x7b4~0x7b5*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
				value32 = (p_entry->hwaddr[3] << 24) | (p_entry->hwaddr[2] << 16) | (p_entry->hwaddr[1] << 8) | p_entry->hwaddr[0];
#else
				value32 = (p_entry->MacAddr[3] << 24) | (p_entry->MacAddr[2] << 16) | (p_entry->MacAddr[1] << 8) | p_entry->MacAddr[0];
#endif
				odm_set_mac_reg(p_dm_odm, 0x7b0, MASKDWORD, value32);/*0x7b0~0x7b3*/

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("p_dm_fat_table->train_idx=%d\n", p_dm_fat_table->train_idx));

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training MAC addr = %x:%x:%x:%x:%x:%x\n",
					p_entry->hwaddr[5], p_entry->hwaddr[4], p_entry->hwaddr[3], p_entry->hwaddr[2], p_entry->hwaddr[1], p_entry->hwaddr[0]));
#else
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training MAC addr = %x:%x:%x:%x:%x:%x\n",
					p_entry->MacAddr[5], p_entry->MacAddr[4], p_entry->MacAddr[3], p_entry->MacAddr[2], p_entry->MacAddr[1], p_entry->MacAddr[0]));
#endif

				break;
			}
		}
	}

#if 0
	/*  */
	/* 2012.03.26 LukeLee: This should be removed later, the MAC address is changed according to MACID in turn */
	/*  */
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{
		struct _ADAPTER	*adapter =  p_dm_odm->adapter;
		PMGNT_INFO	p_mgnt_info = &adapter->MgntInfo;

		for (i = 0; i < 6; i++) {
			bssid[i] = p_mgnt_info->bssid[i];
			/* dbg_print("bssid[%d]=%x\n", i, bssid[i]); */
		}
	}
#endif

	/* odm_set_next_mac_addr_target(p_dm_odm); */

	/* 1 Select MAC Address Filter */
	for (i = 0; i < 6; i++) {
		if (bssid[i] != p_dm_fat_table->bssid[i]) {
			is_match_bssid = false;
			break;
		}
	}
	if (is_match_bssid == false) {
		/* Match MAC ADDR */
		value32 = (bssid[5] << 8) | bssid[4];
		odm_set_mac_reg(p_dm_odm, 0x7b4, 0xFFFF, value32);
		value32 = (bssid[3] << 24) | (bssid[2] << 16) | (bssid[1] << 8) | bssid[0];
		odm_set_mac_reg(p_dm_odm, 0x7b0, MASKDWORD, value32);
	}

	return is_match_bssid;
#endif

}

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))

void
odm_fast_ant_training(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	u32	max_rssi_path_a = 0, pckcnt_path_a = 0;
	u8	i, target_ant_path_a = 0;
	boolean	is_pkt_filter_macth_path_a = false;
#if (RTL8192E_SUPPORT == 1)
	u32	max_rssi_path_b = 0, pckcnt_path_b = 0;
	u8	target_ant_path_b = 0;
	boolean	is_pkt_filter_macth_path_b = false;
#endif


	if (!p_dm_odm->is_linked) { /* is_linked==False */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));

		if (p_dm_fat_table->is_become_linked == true) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
			phydm_fast_training_enable(p_dm_odm, FAT_OFF);
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
		return;
	} else {
		if (p_dm_fat_table->is_become_linked == false) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked!!!]\n"));
			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
	}

	if (*(p_dm_fat_table->p_force_tx_ant_by_desc) == false) {
		if (p_dm_odm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
	}


	if (p_dm_odm->support_ic_type == ODM_RTL8188E)
		odm_set_bb_reg(p_dm_odm, 0x864, BIT(2) | BIT(1) | BIT(0), ((p_dm_odm->fat_comb_a) - 1));
#if (RTL8192E_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(p_dm_odm, 0xB38, BIT(2) | BIT1 | BIT0, ((p_dm_odm->fat_comb_a) - 1));	   /* path-A  */ /* ant combination=regB38[2:0]+1 */
		odm_set_bb_reg(p_dm_odm, 0xB38, BIT(18) | BIT17 | BIT16, ((p_dm_odm->fat_comb_b) - 1));  /* path-B  */ /* ant combination=regB38[18:16]+1 */
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("==>odm_fast_ant_training()\n"));

	/* 1 TRAINING STATE */
	if (p_dm_fat_table->fat_state == FAT_TRAINING_STATE) {
		/* 2 Caculate RSSI per Antenna */

		/* 3 [path-A]--------------------------- */
		for (i = 0; i < (p_dm_odm->fat_comb_a); i++) { /* i : antenna index */
			if (p_dm_fat_table->ant_rssi_cnt[i] == 0)
				p_dm_fat_table->ant_ave_rssi[i] = 0;
			else {
				p_dm_fat_table->ant_ave_rssi[i] = p_dm_fat_table->ant_sum_rssi[i] / p_dm_fat_table->ant_rssi_cnt[i];
				is_pkt_filter_macth_path_a = true;
			}

			if (p_dm_fat_table->ant_ave_rssi[i] > max_rssi_path_a) {
				max_rssi_path_a = p_dm_fat_table->ant_ave_rssi[i];
				pckcnt_path_a = p_dm_fat_table->ant_rssi_cnt[i];
				target_ant_path_a =  i ;
			} else if (p_dm_fat_table->ant_ave_rssi[i] == max_rssi_path_a) {
				if ((p_dm_fat_table->ant_rssi_cnt[i])   >   pckcnt_path_a) {
					max_rssi_path_a = p_dm_fat_table->ant_ave_rssi[i];
					pckcnt_path_a = p_dm_fat_table->ant_rssi_cnt[i];
					target_ant_path_a = i ;
				}
			}

			ODM_RT_TRACE("*** ant-index : [ %d ],      counter = (( %d )),     Avg RSSI = (( %d ))\n", i, p_dm_fat_table->ant_rssi_cnt[i],  p_dm_fat_table->ant_ave_rssi[i]);
		}


#if 0
#if (RTL8192E_SUPPORT == 1)
		/* 3 [path-B]--------------------------- */
		for (i = 0; i < (p_dm_odm->fat_comb_b); i++) {
			if (p_dm_fat_table->antRSSIcnt_pathB[i] == 0)
				p_dm_fat_table->antAveRSSI_pathB[i] = 0;
			else { /*  (ant_rssi_cnt[i] != 0) */
				p_dm_fat_table->antAveRSSI_pathB[i] = p_dm_fat_table->antSumRSSI_pathB[i] / p_dm_fat_table->antRSSIcnt_pathB[i];
				is_pkt_filter_macth_path_b = true;
			}
			if (p_dm_fat_table->antAveRSSI_pathB[i] > max_rssi_path_b) {
				max_rssi_path_b = p_dm_fat_table->antAveRSSI_pathB[i];
				pckcnt_path_b = p_dm_fat_table->antRSSIcnt_pathB[i];
				target_ant_path_b = (u8) i;
			}
			if (p_dm_fat_table->antAveRSSI_pathB[i] == max_rssi_path_b) {
				if (p_dm_fat_table->antRSSIcnt_pathB > pckcnt_path_b) {
					max_rssi_path_b = p_dm_fat_table->antAveRSSI_pathB[i];
					target_ant_path_b = (u8) i;
				}
			}
			if (p_dm_odm->fat_print_rssi == 1) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***{path-B}: Sum RSSI[%d] = (( %d )),      cnt RSSI [%d] = (( %d )),     Avg RSSI[%d] = (( %d ))\n",
					i, p_dm_fat_table->antSumRSSI_pathB[i], i, p_dm_fat_table->antRSSIcnt_pathB[i], i, p_dm_fat_table->antAveRSSI_pathB[i]));
			}
		}
#endif
#endif

		/* 1 DECISION STATE */

		/* 2 Select TRX Antenna */

		phydm_fast_training_enable(p_dm_odm, FAT_OFF);

		/* 3 [path-A]--------------------------- */
		if (is_pkt_filter_macth_path_a  == false) {
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("{path-A}: None Packet is matched\n")); */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("{path-A}: None Packet is matched\n"));
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
		} else {
			ODM_RT_TRACE("target_ant_path_a = (( %d )) , max_rssi_path_a = (( %d ))\n", target_ant_path_a, max_rssi_path_a);

			/* 3 [ update RX-optional ant ]        Default RX is Omni, Optional RX is the best decision by FAT */
			if (p_dm_odm->support_ic_type == ODM_RTL8188E)
				odm_set_bb_reg(p_dm_odm, 0x864, BIT(8) | BIT(7) | BIT(6), target_ant_path_a);
			else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
				odm_set_bb_reg(p_dm_odm, 0xB38, BIT(8) | BIT7 | BIT6, target_ant_path_a); /* Optional RX [pth-A] */
			}
			/* 3 [ update TX ant ] */
			odm_update_tx_ant(p_dm_odm, target_ant_path_a, (p_dm_fat_table->train_idx));

			if (target_ant_path_a == 0)
				odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
		}
#if 0
#if (RTL8192E_SUPPORT == 1)
		/* 3 [path-B]--------------------------- */
		if (is_pkt_filter_macth_path_b == false) {
			if (p_dm_odm->fat_print_rssi == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***[%d]{path-B}: None Packet is matched\n\n\n", __LINE__));
		} else {
			if (p_dm_odm->fat_print_rssi == 1) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
					(" ***target_ant_path_b = (( %d )) *** max_rssi = (( %d ))***\n\n\n", target_ant_path_b, max_rssi_path_b));
			}
			odm_set_bb_reg(p_dm_odm, 0xB38, BIT(21) | BIT20 | BIT19, target_ant_path_b);	/* Default RX is Omni, Optional RX is the best decision by FAT */
			odm_set_bb_reg(p_dm_odm, 0x80c, BIT(21), 1); /* Reg80c[21]=1'b1		//from TX Info */

			p_dm_fat_table->antsel_pathB[p_dm_fat_table->train_idx] = target_ant_path_b;
		}
#endif
#endif

		/* 2 Reset counter */
		for (i = 0; i < (p_dm_odm->fat_comb_a); i++) {
			p_dm_fat_table->ant_sum_rssi[i] = 0;
			p_dm_fat_table->ant_rssi_cnt[i] = 0;
		}
		/*
		#if (RTL8192E_SUPPORT == 1)
		for(i=0; i<=(p_dm_odm->fat_comb_b); i++)
		{
			p_dm_fat_table->antSumRSSI_pathB[i] = 0;
			p_dm_fat_table->antRSSIcnt_pathB[i] = 0;
		}
		#endif
		*/

		p_dm_fat_table->fat_state = FAT_PREPARE_STATE;
		return;
	}

	/* 1 NORMAL STATE */
	if (p_dm_fat_table->fat_state == FAT_PREPARE_STATE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Start Prepare state ]\n"));

		odm_set_next_mac_addr_target(p_dm_odm);

		/* 2 Prepare Training */
		p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
		phydm_fast_training_enable(p_dm_odm, FAT_ON);
		odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);		/* enable HW AntDiv */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Start Training state]\n"));

		odm_set_timer(p_dm_odm, &p_dm_odm->fast_ant_training_timer, p_dm_odm->antdiv_intvl); /* ms */
	}

}

void
odm_fast_ant_training_callback(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct _ADAPTER	*padapter = p_dm_odm->adapter;
	if (padapter->net_closed == _TRUE)
		return;
	/* if(*p_dm_odm->p_is_net_closed == true) */
	/* return; */
#endif

#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_odm->fast_ant_training_workitem);
#else
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("******odm_fast_ant_training_callback******\n"));
	odm_fast_ant_training(p_dm_odm);
#endif
}

void
odm_fast_ant_training_work_item_callback(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("******odm_fast_ant_training_work_item_callback******\n"));
	odm_fast_ant_training(p_dm_odm);
}

#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2

u32
phydm_construct_hb_rfu_codeword_type2(
	void		*p_dm_void,
	u32		beam_set_idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_		*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32		sync_codeword = 0xff;
	u32		codeword = 0;
	u32		data_tmp = 0;
	u32		i;

	for (i = 0; i < pdm_sat_table->ant_num_total; i++) {

		if (*p_dm_odm->p_band_type == ODM_BAND_5G)
			data_tmp = pdm_sat_table->rfu_codeword_table_5g[beam_set_idx][i];
		else
			data_tmp = pdm_sat_table->rfu_codeword_table_2g[beam_set_idx][i];
			
		codeword |= (data_tmp << (i * pdm_sat_table->rfu_each_ant_bit_num));
	}

	codeword = (codeword<<8) | sync_codeword;
	
	return codeword;
}

void
phydm_update_beam_pattern_type2(
	void		*p_dm_void,
	u32		codeword,
	u32		codeword_length
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u8			i;
	boolean			beam_ctrl_signal;
	u32			one = 0x1;
	u32			reg44_tmp_p, reg44_tmp_n, reg44_ori;
	u8			devide_num = 4;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set codeword = ((0x%x))\n", codeword));

	reg44_ori = odm_get_mac_reg(p_dm_odm, 0x44, MASKDWORD);
	reg44_tmp_p = reg44_ori;
	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_ori =0x%x\n", reg44_ori));*/

	/*devide_num = (pdm_sat_table->rfu_protocol_type == 2) ? 8 : 4;*/

	for (i = 0; i <= (codeword_length - 1); i++) {
		beam_ctrl_signal = (boolean)((codeword & BIT(i)) >> i);
		
		#if 1
		if (p_dm_odm->debug_components & ODM_COMP_ANT_DIV) {

			if (i == (codeword_length - 1)) {
				dbg_print("%d ]\n", beam_ctrl_signal);
				/**/
			} else if (i == 0) {
				dbg_print("Start sending codeword[1:%d] ---> [ %d ", codeword_length, beam_ctrl_signal);
				/**/
			} else if ((i % devide_num) == (devide_num-1)) {
				dbg_print("%d  |  ", beam_ctrl_signal);
				/**/
			} else {
				dbg_print("%d ", beam_ctrl_signal);
				/**/
			}
		}
		#endif
		
		if (p_dm_odm->support_ic_type == ODM_RTL8821) {
			#if (RTL8821A_SUPPORT == 1)
			reg44_tmp_p = reg44_ori & (~(BIT(11) | BIT10)); /*clean bit 10 & 11*/
			reg44_tmp_p |= ((1 << 11) | (beam_ctrl_signal << 10));
			reg44_tmp_n = reg44_ori & (~(BIT(11) | BIT(10)));

			/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n));*/
			odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
			odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_n);
			#endif
		}
		#if (RTL8822B_SUPPORT == 1)
		else if (p_dm_odm->support_ic_type == ODM_RTL8822B) {

			if (pdm_sat_table->rfu_protocol_type == 2) {

				reg44_tmp_p = reg44_tmp_p & ~(BIT(8)); /*clean bit 8*/
				reg44_tmp_p = reg44_tmp_p ^ BIT(9); /*get new clk high/low, exclusive-or*/

	
				reg44_tmp_p |= (beam_ctrl_signal << 8);
				
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44 =(( 0x%x )), reg44[9:8] = ((%x)), beam_ctrl_signal =((%x))\n", reg44_tmp_p, ((reg44_tmp_p & 0x300)>>8), beam_ctrl_signal));*/
				
			} else {
				reg44_tmp_p = reg44_ori & (~(BIT(9) | BIT8)); /*clean bit 9 & 8*/
				reg44_tmp_p |= ((1 << 9) | (beam_ctrl_signal << 8));
				reg44_tmp_n = reg44_ori & (~(BIT(9) | BIT(8)));

				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n)); */
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_n);
				ODM_delay_us(10);
			}
		}
		#endif
	}
}

void
phydm_update_rx_idle_beam_type2(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32			i;

	pdm_sat_table->update_beam_codeword = phydm_construct_hb_rfu_codeword_type2(p_dm_odm, pdm_sat_table->rx_idle_beam_set_idx);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set target beam_pattern codeword = (( 0x%x ))\n", pdm_sat_table->update_beam_codeword));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-Beam ] rx_idle_beam_set_idx = %d\n", pdm_sat_table->rx_idle_beam_set_idx));

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	phydm_update_beam_pattern_type2(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
#else
	odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
	/*odm_stall_execution(1);*/
#endif

	pdm_sat_table->pre_codeword = pdm_sat_table->update_beam_codeword;
}


void
phydm_hl_smart_ant_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32			used = *_used;
	u32			out_len = *_out_len;
	u32			one = 0x1;
	u32			codeword_length = pdm_sat_table->rfu_codeword_total_bit_num;
	u32			beam_ctrl_signal, i;
	u8			devide_num = 4;

	if (dm_value[0] == 1) { /*fix beam pattern*/

		pdm_sat_table->fix_beam_pattern_en = dm_value[1];

		if (pdm_sat_table->fix_beam_pattern_en == 1) {

			pdm_sat_table->fix_beam_pattern_codeword = dm_value[2];

			if (pdm_sat_table->fix_beam_pattern_codeword  > (one << codeword_length)) {

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Codeword overflow, Current codeword is ((0x%x)), and should be less than ((%d))bit\n",
					pdm_sat_table->fix_beam_pattern_codeword, codeword_length));
				
				(pdm_sat_table->fix_beam_pattern_codeword) &= 0xffffff;
				
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Auto modify to (0x%x)\n", pdm_sat_table->fix_beam_pattern_codeword));
			}

			pdm_sat_table->update_beam_codeword = pdm_sat_table->fix_beam_pattern_codeword;

			/*---------------------------------------------------------*/
			PHYDM_SNPRINTF((output + used, out_len - used, "Fix Beam Pattern\n"));
			
			devide_num = (pdm_sat_table->rfu_protocol_type == 2) ? 8 : 4;
			
			for (i = 0; i <= (codeword_length - 1); i++) {
				beam_ctrl_signal = (boolean)((pdm_sat_table->update_beam_codeword & BIT(i)) >> i);

				if (i == (codeword_length - 1)) {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d]\n", beam_ctrl_signal));
					/**/
				} else if (i == 0) {
					PHYDM_SNPRINTF((output + used, out_len - used, "Send Codeword[1:%d] to RFU -> [%d", pdm_sat_table->rfu_codeword_total_bit_num, beam_ctrl_signal));
					/**/
				} else if ((i % devide_num) == (devide_num-1)) {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d|", beam_ctrl_signal));
					/**/
				} else {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d", beam_ctrl_signal));
					/**/
				}
			}
			/*---------------------------------------------------------*/


#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			phydm_update_beam_pattern_type2(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
#else
			odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
			/*odm_stall_execution(1);*/
#endif
		} else if (pdm_sat_table->fix_beam_pattern_en == 0)
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Smart Antenna: Enable\n"));

	} else if (dm_value[0] == 2) { /*set latch time*/

		pdm_sat_table->latch_time = dm_value[1];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ]  latch_time =0x%x\n", pdm_sat_table->latch_time));
	} else if (dm_value[0] == 3) {

		pdm_sat_table->fix_training_num_en = dm_value[1];

		if (pdm_sat_table->fix_training_num_en == 1) {
			pdm_sat_table->per_beam_training_pkt_num = (u8)dm_value[2];
			pdm_sat_table->decision_holding_period = (u8)dm_value[3];

			PHYDM_SNPRINTF((output + used, out_len - used, "[SmartAnt][Dbg] Fix_train_en = (( %d )), train_pkt_num = (( %d )), holding_period = (( %d )),\n",
				pdm_sat_table->fix_training_num_en, pdm_sat_table->per_beam_training_pkt_num, pdm_sat_table->decision_holding_period));

		} else if (pdm_sat_table->fix_training_num_en == 0) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ]  AUTO per_beam_training_pkt_num\n"));
			/**/
		}
	} else if (dm_value[0] == 4) {

		if (dm_value[1] == 1) {
			pdm_sat_table->ant_num = 1;
			pdm_sat_table->first_train_ant = MAIN_ANT;

		} else if (dm_value[1] == 2) {
			pdm_sat_table->ant_num = 1;
			pdm_sat_table->first_train_ant = AUX_ANT;

		} else if (dm_value[1] == 3) {
			pdm_sat_table->ant_num = 2;
			pdm_sat_table->first_train_ant = MAIN_ANT;
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ]  Set ant Num = (( %d )), first_train_ant = (( %d ))\n",
			pdm_sat_table->ant_num, (pdm_sat_table->first_train_ant - 1)));
	} else if (dm_value[0] == 5) {
		#if 0
		if (dm_value[1] <= 3) {
			pdm_sat_table->rfu_codeword_table[dm_value[1]] = dm_value[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Set Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					dm_value[1], dm_value[2]));
		} else {
			for (i = 0; i < 4; i++) {
				PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Show Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					i, pdm_sat_table->rfu_codeword_table[i]));
			}
		}
		#endif
	} else if (dm_value[0] == 6) {
		#if 0
		if (dm_value[1] <= 3) {
			pdm_sat_table->rfu_codeword_table_5g[dm_value[1]] = dm_value[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Set Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					dm_value[1], dm_value[2]));
		} else {
			for (i = 0; i < 4; i++) {
				PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Show Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					i, pdm_sat_table->rfu_codeword_table_5g[i]));
			}
		}
		#endif
	}

}

void
phydm_set_rfu_beam_pattern_type2(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);

	if (p_dm_odm->ant_div_type != HL_SW_SMART_ANT_TYPE2)
		return;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training beam_set index = (( 0x%x ))\n", pdm_sat_table->fast_training_beam_num));
	pdm_sat_table->update_beam_codeword = phydm_construct_hb_rfu_codeword_type2(p_dm_odm, pdm_sat_table->fast_training_beam_num);

	#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	phydm_update_beam_pattern_type2(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
	#else
	odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
	/*odm_stall_execution(1);*/
	#endif
}

void
phydm_fast_ant_training_hl_smart_antenna_type2(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table	 = &(p_dm_odm->dm_fat_table);
	struct _sw_antenna_switch_				*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	u32		codeword = 0;
	u8		i = 0, j=0;
	u8		avg_rssi_tmp;
	u8		avg_rssi_tmp_ma;
	u8		target_ant_beam_max_rssi = 0;
	u8		max_beam_ant_rssi = 0;
	u8		target_ant_beam = 0;
	u32		beam_tmp;
	u8		per_beam_rssi_diff_tmp = 0, training_pkt_num_offset;

	if (!p_dm_odm->is_linked) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));

		if (p_dm_fat_table->is_become_linked == true) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Link->no Link\n"));
			p_dm_fat_table->fat_state = FAT_BEFORE_LINK_STATE;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("change to (( %d )) FAT_state\n", p_dm_fat_table->fat_state));
			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
		return;

	} else {
		if (p_dm_fat_table->is_become_linked == false) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));

			p_dm_fat_table->fat_state = FAT_PREPARE_STATE;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("change to (( %d )) FAT_state\n", p_dm_fat_table->fat_state));

			/*pdm_sat_table->fast_training_beam_num = 0;*/
			/*phydm_set_rfu_beam_pattern_type2(p_dm_odm);*/

			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
	}


	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("HL Smart ant Training: state (( %d ))\n", p_dm_fat_table->fat_state));*/

	/* [DECISION STATE] */
	/*=======================================================================================*/
	if (p_dm_fat_table->fat_state == FAT_DECISION_STATE) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 3. In Decision state]\n"));

		/*compute target beam in each antenna*/
		i = 0;
		for (j = 0; j < (pdm_sat_table->total_beam_set_num); j++) {

			if (pdm_sat_table->pkt_rssi_cnt[i][j] == 0) {	/*if new RSSI = 0 -> MA_RSSI-=2*/
				avg_rssi_tmp = pdm_sat_table->beam_set_avg_rssi_pre[j];
				avg_rssi_tmp = (avg_rssi_tmp >= 2) ? (avg_rssi_tmp - 2) : avg_rssi_tmp;
				avg_rssi_tmp_ma = avg_rssi_tmp;
			} else {
				avg_rssi_tmp = (u8)((pdm_sat_table->pkt_rssi_sum[i][j]) / (pdm_sat_table->pkt_rssi_cnt[i][j]));
				avg_rssi_tmp_ma = (avg_rssi_tmp + pdm_sat_table->beam_set_avg_rssi_pre[j]) >> 1;
			}

			pdm_sat_table->beam_set_avg_rssi_pre[j] = avg_rssi_tmp;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Beam_Set[%d]: pkt_cnt=(( %d )), avg_rssi_MA=(( %d )), avg_rssi=(( %d ))\n",
				j, pdm_sat_table->pkt_rssi_cnt[i][j], avg_rssi_tmp_ma, avg_rssi_tmp));

			if (avg_rssi_tmp > target_ant_beam_max_rssi) {
				target_ant_beam = j;
				target_ant_beam_max_rssi = avg_rssi_tmp;
			}

			/*reset counter value*/
			pdm_sat_table->pkt_rssi_sum[i][j] = 0;
			pdm_sat_table->pkt_rssi_cnt[i][j] = 0;

		}
		
		pdm_sat_table->rx_idle_beam_set_idx = target_ant_beam;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("---------> Target Beam_Set(( %d )) RSSI_max = ((%d))\n",
			target_ant_beam, target_ant_beam_max_rssi));

		for (j = 0; j < (pdm_sat_table->total_beam_set_num); j++) {

			per_beam_rssi_diff_tmp = target_ant_beam_max_rssi - pdm_sat_table->beam_set_avg_rssi_pre[j];
			pdm_sat_table->beam_set_train_rssi_diff[j] = per_beam_rssi_diff_tmp;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Beam_Set[%d]: RSSI_diff= ((%d))\n",
					j, per_beam_rssi_diff_tmp));
		}

		/*set beam in each antenna*/
		phydm_update_rx_idle_beam_type2(p_dm_odm);
		p_dm_fat_table->fat_state = FAT_PREPARE_STATE;
		return;

	}
	/* [TRAINING STATE] */
	else if (p_dm_fat_table->fat_state == FAT_TRAINING_STATE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2. In Training state]\n"));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("fat_beam_n = (( %d )), pre_fat_beam_n = (( %d ))\n",
			pdm_sat_table->fast_training_beam_num, pdm_sat_table->pre_fast_training_beam_num));

		if (pdm_sat_table->fast_training_beam_num > pdm_sat_table->pre_fast_training_beam_num)

			pdm_sat_table->force_update_beam_en = 0;

		else {

			pdm_sat_table->force_update_beam_en = 1;

			pdm_sat_table->pkt_counter = 0;
			beam_tmp = pdm_sat_table->fast_training_beam_num;
			if (pdm_sat_table->fast_training_beam_num >= ((u32)pdm_sat_table->total_beam_set_num - 1)) {

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Timeout Update]  Beam_num (( %d )) -> (( decision ))\n", pdm_sat_table->fast_training_beam_num));
				p_dm_fat_table->fat_state = FAT_DECISION_STATE;
				phydm_fast_ant_training_hl_smart_antenna_type2(p_dm_odm);

			} else {
				pdm_sat_table->fast_training_beam_num++;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Timeout Update]  Beam_num (( %d )) -> (( %d ))\n", beam_tmp, pdm_sat_table->fast_training_beam_num));
				phydm_set_rfu_beam_pattern_type2(p_dm_odm);
				p_dm_fat_table->fat_state = FAT_TRAINING_STATE;

			}
		}
		pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[prepare state] Update Pre_Beam =(( %d ))\n", pdm_sat_table->pre_fast_training_beam_num));
	}
	/*  [Prepare state] */
	/*=======================================================================================*/
	else if (p_dm_fat_table->fat_state == FAT_PREPARE_STATE) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n\n[ 1. In Prepare state]\n"));

		if (p_dm_odm->pre_traffic_load == (p_dm_odm->traffic_load)) {
			if (pdm_sat_table->decision_holding_period != 0) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Holding_period = (( %d )), return!!!\n", pdm_sat_table->decision_holding_period));
				pdm_sat_table->decision_holding_period--;
				return;
			}
		}

		/* Set training packet number*/
		if (pdm_sat_table->fix_training_num_en == 0) {

			switch (p_dm_odm->traffic_load) {

			case TRAFFIC_HIGH:
				pdm_sat_table->per_beam_training_pkt_num = 8;
				pdm_sat_table->decision_holding_period = 2;
				break;
			case TRAFFIC_MID:
				pdm_sat_table->per_beam_training_pkt_num = 6;
				pdm_sat_table->decision_holding_period = 3;
				break;
			case TRAFFIC_LOW:
				pdm_sat_table->per_beam_training_pkt_num = 3; /*ping 60000*/
				pdm_sat_table->decision_holding_period = 4;
				break;
			case TRAFFIC_ULTRA_LOW:
				pdm_sat_table->per_beam_training_pkt_num = 1;
				pdm_sat_table->decision_holding_period = 6;
				break;
			default:
				break;
			}
		}
		
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TrafficLoad = (( %d )), Fix_beam = (( %d )), per_beam_training_pkt_num = (( %d )), decision_holding_period = ((%d))\n",
			p_dm_odm->traffic_load, pdm_sat_table->fix_training_num_en, pdm_sat_table->per_beam_training_pkt_num, pdm_sat_table->decision_holding_period));

		/*Beam_set number*/
		if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
			pdm_sat_table->total_beam_set_num = pdm_sat_table->total_beam_set_num_5g;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("5G beam_set num = ((%d))\n", pdm_sat_table->total_beam_set_num));
		} else {
			pdm_sat_table->total_beam_set_num = pdm_sat_table->total_beam_set_num_2g;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("2G beam_set num = ((%d))\n", pdm_sat_table->total_beam_set_num));
		}

		for (j = 0; j < (pdm_sat_table->total_beam_set_num); j++) {

			training_pkt_num_offset = pdm_sat_table->beam_set_train_rssi_diff[j];

			if ((pdm_sat_table->per_beam_training_pkt_num) > training_pkt_num_offset)
				pdm_sat_table->beam_set_train_cnt[j] = pdm_sat_table->per_beam_training_pkt_num - training_pkt_num_offset;
			else
				pdm_sat_table->beam_set_train_cnt[j] = 1;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Beam_Set[ %d ] training_pkt_offset = ((%d)), training_pkt_num = ((%d))\n",
				j, pdm_sat_table->beam_set_train_rssi_diff[j], pdm_sat_table->beam_set_train_cnt[j]));
		}
		
		pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
		pdm_sat_table->update_beam_idx = 0;
		pdm_sat_table->pkt_counter = 0;
		
		pdm_sat_table->fast_training_beam_num = 0;
		phydm_set_rfu_beam_pattern_type2(p_dm_odm);
		pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;
		p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
	}

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void
phydm_beam_switch_workitem_callback(
	void	*p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	pdm_sat_table->pkt_skip_statistic_en = 1;
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Beam Switch Workitem Callback, pkt_skip_statistic_en = (( %d ))\n", pdm_sat_table->pkt_skip_statistic_en));

	phydm_update_beam_pattern_type2(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	/*odm_stall_execution(pdm_sat_table->latch_time);*/
	pdm_sat_table->pkt_skip_statistic_en = 0;
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pkt_skip_statistic_en = (( %d )), latch_time = (( %d ))\n", pdm_sat_table->pkt_skip_statistic_en, pdm_sat_table->latch_time));
}

void
phydm_beam_decision_workitem_callback(
	void	*p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Beam decision Workitem Callback\n"));
	phydm_fast_ant_training_hl_smart_antenna_type2(p_dm_odm);
}
#endif
#endif


#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1

u32
phydm_construct_hl_beam_codeword(
	void		*p_dm_void,
	u32		*beam_pattern_idx,
	u32		ant_num
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_		*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32		codeword = 0;
	u32		data_tmp;
	u32		i;
	u32		break_counter = 0;

	if (ant_num < 8) {
		for (i = 0; i < (pdm_sat_table->ant_num_total); i++) {
			/*ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("beam_pattern_num[%x] = %x\n",i,beam_pattern_num[i] ));*/
			if ((i < (pdm_sat_table->first_train_ant - 1)) || (break_counter >= (pdm_sat_table->ant_num))) {
				data_tmp = 0;
				/**/
			} else {

				break_counter++;

				if (beam_pattern_idx[i] == 0) {

					if (*p_dm_odm->p_band_type == ODM_BAND_5G)
						data_tmp = pdm_sat_table->rfu_codeword_table_5g[0];
					else
						data_tmp = pdm_sat_table->rfu_codeword_table[0];

				} else if (beam_pattern_idx[i] == 1) {


					if (*p_dm_odm->p_band_type == ODM_BAND_5G)
						data_tmp = pdm_sat_table->rfu_codeword_table_5g[1];
					else
						data_tmp = pdm_sat_table->rfu_codeword_table[1];

				} else if (beam_pattern_idx[i] == 2) {

					if (*p_dm_odm->p_band_type == ODM_BAND_5G)
						data_tmp = pdm_sat_table->rfu_codeword_table_5g[2];
					else
						data_tmp = pdm_sat_table->rfu_codeword_table[2];

				} else if (beam_pattern_idx[i] == 3) {

					if (*p_dm_odm->p_band_type == ODM_BAND_5G)
						data_tmp = pdm_sat_table->rfu_codeword_table_5g[3];
					else
						data_tmp = pdm_sat_table->rfu_codeword_table[3];
				}
			}


			codeword |= (data_tmp << (i * 4));

		}
	}

	return codeword;
}

void
phydm_update_beam_pattern(
	void		*p_dm_void,
	u32		codeword,
	u32		codeword_length
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u8			i;
	boolean			beam_ctrl_signal;
	u32			one = 0x1;
	u32			reg44_tmp_p, reg44_tmp_n, reg44_ori;
	u8			devide_num = 4;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Set Beam Pattern =0x%x\n", codeword));

	reg44_ori = odm_get_mac_reg(p_dm_odm, 0x44, MASKDWORD);
	reg44_tmp_p = reg44_ori;
	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_ori =0x%x\n", reg44_ori));*/

	devide_num = (pdm_sat_table->rfu_protocol_type == 2) ? 6 : 4;

	for (i = 0; i <= (codeword_length - 1); i++) {
		beam_ctrl_signal = (boolean)((codeword & BIT(i)) >> i);

		if (p_dm_odm->debug_components & ODM_COMP_ANT_DIV) {

			if (i == (codeword_length - 1)) {
				dbg_print("%d ]\n", beam_ctrl_signal);
				/**/
			} else if (i == 0) {
				dbg_print("Send codeword[1:%d] ---> [ %d ", codeword_length, beam_ctrl_signal);
				/**/
			} else if ((i % devide_num) == (devide_num-1)) {
				dbg_print("%d  |  ", beam_ctrl_signal);
				/**/
			} else {
				dbg_print("%d ", beam_ctrl_signal);
				/**/
			}
		}

		if (p_dm_odm->support_ic_type == ODM_RTL8821) {
			#if (RTL8821A_SUPPORT == 1)
			reg44_tmp_p = reg44_ori & (~(BIT(11) | BIT10)); /*clean bit 10 & 11*/
			reg44_tmp_p |= ((1 << 11) | (beam_ctrl_signal << 10));
			reg44_tmp_n = reg44_ori & (~(BIT(11) | BIT(10)));

			/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n));*/
			odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
			odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_n);
			#endif
		}
		#if (RTL8822B_SUPPORT == 1)
		else if (p_dm_odm->support_ic_type == ODM_RTL8822B) {

			if (pdm_sat_table->rfu_protocol_type == 2) {

				reg44_tmp_p = reg44_tmp_p & ~(BIT(8)); /*clean bit 8*/
				reg44_tmp_p = reg44_tmp_p ^ BIT(9); /*get new clk high/low, exclusive-or*/

	
				reg44_tmp_p |= (beam_ctrl_signal << 8);
				
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44 =(( 0x%x )), reg44[9:8] = ((%x)), beam_ctrl_signal =((%x))\n", reg44_tmp_p, ((reg44_tmp_p & 0x300)>>8), beam_ctrl_signal));*/
				
			} else {
				reg44_tmp_p = reg44_ori & (~(BIT(9) | BIT8)); /*clean bit 9 & 8*/
				reg44_tmp_p |= ((1 << 9) | (beam_ctrl_signal << 8));
				reg44_tmp_n = reg44_ori & (~(BIT(9) | BIT(8)));

				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reg44_tmp_p =(( 0x%x )), reg44_tmp_n = (( 0x%x ))\n", reg44_tmp_p, reg44_tmp_n)); */
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_p);
				ODM_delay_us(10);
				odm_set_mac_reg(p_dm_odm, 0x44, MASKDWORD, reg44_tmp_n);
				ODM_delay_us(10);
			}
		}
		#endif
	}
}

void
phydm_update_rx_idle_beam(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32			i;

	pdm_sat_table->update_beam_codeword = phydm_construct_hl_beam_codeword(p_dm_odm, &(pdm_sat_table->rx_idle_beam[0]), pdm_sat_table->ant_num);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set target beam_pattern codeword = (( 0x%x ))\n", pdm_sat_table->update_beam_codeword));

	for (i = 0; i < (pdm_sat_table->ant_num); i++) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-Beam ] RxIdleBeam[%d] =%d\n", i, pdm_sat_table->rx_idle_beam[i]));
		/**/
	}

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	phydm_update_beam_pattern(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
#else
	odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
	/*odm_stall_execution(1);*/
#endif

	pdm_sat_table->pre_codeword = pdm_sat_table->update_beam_codeword;
}

void
phydm_hl_smart_ant_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32			used = *_used;
	u32			out_len = *_out_len;
	u32			one = 0x1;
	u32			codeword_length = pdm_sat_table->rfu_codeword_total_bit_num;
	u32			beam_ctrl_signal, i;
	u8			devide_num = 4;

	if (dm_value[0] == 1) { /*fix beam pattern*/

		pdm_sat_table->fix_beam_pattern_en = dm_value[1];

		if (pdm_sat_table->fix_beam_pattern_en == 1) {

			pdm_sat_table->fix_beam_pattern_codeword = dm_value[2];

			if (pdm_sat_table->fix_beam_pattern_codeword  > (one << codeword_length)) {

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Codeword overflow, Current codeword is ((0x%x)), and should be less than ((%d))bit\n",
					pdm_sat_table->fix_beam_pattern_codeword, codeword_length));
				
				(pdm_sat_table->fix_beam_pattern_codeword) &= 0xffffff;
				
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Auto modify to (0x%x)\n", pdm_sat_table->fix_beam_pattern_codeword));
			}

			pdm_sat_table->update_beam_codeword = pdm_sat_table->fix_beam_pattern_codeword;

			/*---------------------------------------------------------*/
			PHYDM_SNPRINTF((output + used, out_len - used, "Fix Beam Pattern\n"));
			
			devide_num = (pdm_sat_table->rfu_protocol_type == 2) ? 6 : 4;
			
			for (i = 0; i <= (codeword_length - 1); i++) {
				beam_ctrl_signal = (boolean)((pdm_sat_table->update_beam_codeword & BIT(i)) >> i);

				if (i == (codeword_length - 1)) {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d]\n", beam_ctrl_signal));
					/**/
				} else if (i == 0) {
					PHYDM_SNPRINTF((output + used, out_len - used, "Send Codeword[1:24] to RFU -> [%d", beam_ctrl_signal));
					/**/
				} else if ((i % devide_num) == (devide_num-1)) {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d|", beam_ctrl_signal));
					/**/
				} else {
					PHYDM_SNPRINTF((output + used, out_len - used, "%d", beam_ctrl_signal));
					/**/
				}
			}
			/*---------------------------------------------------------*/


#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			phydm_update_beam_pattern(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
#else
			odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
			/*odm_stall_execution(1);*/
#endif
		} else if (pdm_sat_table->fix_beam_pattern_en == 0)
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Smart Antenna: Enable\n"));

	} else if (dm_value[0] == 2) { /*set latch time*/

		pdm_sat_table->latch_time = dm_value[1];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ]  latch_time =0x%x\n", pdm_sat_table->latch_time));
	} else if (dm_value[0] == 3) {

		pdm_sat_table->fix_training_num_en = dm_value[1];

		if (pdm_sat_table->fix_training_num_en == 1) {
			pdm_sat_table->per_beam_training_pkt_num = (u8)dm_value[2];
			pdm_sat_table->decision_holding_period = (u8)dm_value[3];

			PHYDM_SNPRINTF((output + used, out_len - used, "[SmartAnt][Dbg] Fix_train_en = (( %d )), train_pkt_num = (( %d )), holding_period = (( %d )),\n",
				pdm_sat_table->fix_training_num_en, pdm_sat_table->per_beam_training_pkt_num, pdm_sat_table->decision_holding_period));

		} else if (pdm_sat_table->fix_training_num_en == 0) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ]  AUTO per_beam_training_pkt_num\n"));
			/**/
		}
	} else if (dm_value[0] == 4) {

		if (dm_value[1] == 1) {
			pdm_sat_table->ant_num = 1;
			pdm_sat_table->first_train_ant = MAIN_ANT;

		} else if (dm_value[1] == 2) {
			pdm_sat_table->ant_num = 1;
			pdm_sat_table->first_train_ant = AUX_ANT;

		} else if (dm_value[1] == 3) {
			pdm_sat_table->ant_num = 2;
			pdm_sat_table->first_train_ant = MAIN_ANT;
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ]  Set ant Num = (( %d )), first_train_ant = (( %d ))\n",
			pdm_sat_table->ant_num, (pdm_sat_table->first_train_ant - 1)));
	} else if (dm_value[0] == 5) {

		if (dm_value[1] <= 3) {
			pdm_sat_table->rfu_codeword_table[dm_value[1]] = dm_value[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Set Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					dm_value[1], dm_value[2]));
		} else {
			for (i = 0; i < 4; i++) {
				PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Show Beam_2G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					i, pdm_sat_table->rfu_codeword_table[i]));
			}
		}
	} else if (dm_value[0] == 6) {

		if (dm_value[1] <= 3) {
			pdm_sat_table->rfu_codeword_table_5g[dm_value[1]] = dm_value[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Set Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					dm_value[1], dm_value[2]));
		} else {
			for (i = 0; i < 4; i++) {
				PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Show Beam_5G: (( %d )), RFU codeword table = (( 0x%x ))\n",
					i, pdm_sat_table->rfu_codeword_table_5g[i]));
			}
		}
	} else if (dm_value[0] == 7) {

		if (dm_value[1] <= 4) {

			pdm_sat_table->beam_patten_num_each_ant = dm_value[1];
			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Set Beam number = (( %d ))\n",
				pdm_sat_table->beam_patten_num_each_ant));
		} else {

			PHYDM_SNPRINTF((output + used, out_len - used, "[ SmartAnt ] Show Beam number = (( %d ))\n",
				pdm_sat_table->beam_patten_num_each_ant));
		}
	}

}


void
phydm_set_all_ant_same_beam_num(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);

	if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE1) { /*2ant for 8821A*/

		pdm_sat_table->rx_idle_beam[0] = pdm_sat_table->fast_training_beam_num;
		pdm_sat_table->rx_idle_beam[1] = pdm_sat_table->fast_training_beam_num;
	}

	pdm_sat_table->update_beam_codeword = phydm_construct_hl_beam_codeword(p_dm_odm, &(pdm_sat_table->rx_idle_beam[0]), pdm_sat_table->ant_num);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Set all ant beam_pattern: codeword = (( 0x%x ))\n", pdm_sat_table->update_beam_codeword));

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	phydm_update_beam_pattern(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);
#else
	odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_workitem);
	/*odm_stall_execution(1);*/
#endif
}

void
odm_fast_ant_training_hl_smart_antenna_type1(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _SMART_ANTENNA_TRAINNING_		*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table	 = &(p_dm_odm->dm_fat_table);
	struct _sw_antenna_switch_		*p_dm_swat_table = &p_dm_odm->dm_swat_table;
	u32		codeword = 0, i, j;
	u32		target_ant;
	u32		avg_rssi_tmp, avg_rssi_tmp_ma;
	u32		target_ant_beam_max_rssi[SUPPORT_RF_PATH_NUM] = {0};
	u32		max_beam_ant_rssi = 0;
	u32		target_ant_beam[SUPPORT_RF_PATH_NUM] = {0};
	u32		beam_tmp;
	u8		next_ant;
	u32		rssi_sorting_seq[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u32		rank_idx_seq[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u32		rank_idx_out[SUPPORT_BEAM_PATTERN_NUM] = {0};
	u8		per_beam_rssi_diff_tmp = 0, training_pkt_num_offset;
	u32		break_counter = 0;
	u32		used_ant;


	if (!p_dm_odm->is_linked) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));

		if (p_dm_fat_table->is_become_linked == true) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Link->no Link\n"));
			p_dm_fat_table->fat_state = FAT_BEFORE_LINK_STATE;
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("change to (( %d )) FAT_state\n", p_dm_fat_table->fat_state));

			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
		return;

	} else {
		if (p_dm_fat_table->is_become_linked == false) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));

			p_dm_fat_table->fat_state = FAT_PREPARE_STATE;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("change to (( %d )) FAT_state\n", p_dm_fat_table->fat_state));

			/*pdm_sat_table->fast_training_beam_num = 0;*/
			/*phydm_set_all_ant_same_beam_num(p_dm_odm);*/

			p_dm_fat_table->is_become_linked = p_dm_odm->is_linked;
		}
	}

	if (*(p_dm_fat_table->p_force_tx_ant_by_desc) == false) {
		if (p_dm_odm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
	}

	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("HL Smart ant Training: state (( %d ))\n", p_dm_fat_table->fat_state));*/

	/* [DECISION STATE] */
	/*=======================================================================================*/
	if (p_dm_fat_table->fat_state == FAT_DECISION_STATE) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 3. In Decision state]\n"));
		phydm_fast_training_enable(p_dm_odm, FAT_OFF);

		break_counter = 0;
		/*compute target beam in each antenna*/
		for (i = (pdm_sat_table->first_train_ant - 1); i < pdm_sat_table->ant_num_total; i++) {
			for (j = 0; j < (pdm_sat_table->beam_patten_num_each_ant); j++) {

				if (pdm_sat_table->pkt_rssi_cnt[i][j] == 0) {
					avg_rssi_tmp = pdm_sat_table->pkt_rssi_pre[i][j];
					avg_rssi_tmp = (avg_rssi_tmp >= 2) ? (avg_rssi_tmp - 2) : avg_rssi_tmp;
					avg_rssi_tmp_ma = avg_rssi_tmp;
				} else {
					avg_rssi_tmp = (pdm_sat_table->pkt_rssi_sum[i][j]) / (pdm_sat_table->pkt_rssi_cnt[i][j]);
					avg_rssi_tmp_ma = (avg_rssi_tmp + pdm_sat_table->pkt_rssi_pre[i][j]) >> 1;
				}

				rssi_sorting_seq[j] = avg_rssi_tmp;
				pdm_sat_table->pkt_rssi_pre[i][j] = avg_rssi_tmp;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ant[%d], Beam[%d]: pkt_cnt=(( %d )), avg_rssi_MA=(( %d )), avg_rssi=(( %d ))\n",
					i, j, pdm_sat_table->pkt_rssi_cnt[i][j], avg_rssi_tmp_ma, avg_rssi_tmp));

				if (avg_rssi_tmp > target_ant_beam_max_rssi[i]) {
					target_ant_beam[i] = j;
					target_ant_beam_max_rssi[i] = avg_rssi_tmp;
				}

				/*reset counter value*/
				pdm_sat_table->pkt_rssi_sum[i][j] = 0;
				pdm_sat_table->pkt_rssi_cnt[i][j] = 0;

			}
			pdm_sat_table->rx_idle_beam[i] = target_ant_beam[i];
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("---------> Target of ant[%d]: Beam_num-(( %d )) RSSI= ((%d))\n",
				i,  target_ant_beam[i], target_ant_beam_max_rssi[i]));

			/*sorting*/
			/*
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Pre]rssi_sorting_seq = [%d, %d, %d, %d]\n", rssi_sorting_seq[0], rssi_sorting_seq[1], rssi_sorting_seq[2], rssi_sorting_seq[3]));
			*/

			/*phydm_seq_sorting(p_dm_odm, &rssi_sorting_seq[0], &rank_idx_seq[0], &rank_idx_out[0], SUPPORT_BEAM_PATTERN_NUM);*/

			/*
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Post]rssi_sorting_seq = [%d, %d, %d, %d]\n", rssi_sorting_seq[0], rssi_sorting_seq[1], rssi_sorting_seq[2], rssi_sorting_seq[3]));
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Post]rank_idx_seq = [%d, %d, %d, %d]\n", rank_idx_seq[0], rank_idx_seq[1], rank_idx_seq[2], rank_idx_seq[3]));
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Post]rank_idx_out = [%d, %d, %d, %d]\n", rank_idx_out[0], rank_idx_out[1], rank_idx_out[2], rank_idx_out[3]));
			*/

			if (target_ant_beam_max_rssi[i] > max_beam_ant_rssi) {
				target_ant = i;
				max_beam_ant_rssi = target_ant_beam_max_rssi[i];
				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Target of ant = (( %d )) max_beam_ant_rssi = (( %d ))\n",
					target_ant,  max_beam_ant_rssi));*/
			}
			break_counter++;
			if (break_counter >= (pdm_sat_table->ant_num))
				break;
		}

#ifdef CONFIG_FAT_PATCH
		break_counter = 0;
		for (i = (pdm_sat_table->first_train_ant - 1); i < pdm_sat_table->ant_num_total; i++) {
			for (j = 0; j < (pdm_sat_table->beam_patten_num_each_ant); j++) {

				per_beam_rssi_diff_tmp = (u8)(max_beam_ant_rssi - pdm_sat_table->pkt_rssi_pre[i][j]);
				pdm_sat_table->beam_train_rssi_diff[i][j] = per_beam_rssi_diff_tmp;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ant[%d], Beam[%d]: RSSI_diff= ((%d))\n",
						i,  j, per_beam_rssi_diff_tmp));
			}
			break_counter++;
			if (break_counter >= (pdm_sat_table->ant_num))
				break;
		}
#endif

		if (target_ant == 0)
			target_ant = MAIN_ANT;
		else if (target_ant == 1)
			target_ant = AUX_ANT;

		if (pdm_sat_table->ant_num > 1) {
			/* [ update RX ant ]*/
			odm_update_rx_idle_ant(p_dm_odm, (u8)target_ant);

			/* [ update TX ant ]*/
			odm_update_tx_ant(p_dm_odm, (u8)target_ant, (p_dm_fat_table->train_idx));
		}

		/*set beam in each antenna*/
		phydm_update_rx_idle_beam(p_dm_odm);

		odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
		p_dm_fat_table->fat_state = FAT_PREPARE_STATE;
		return;

	}
	/* [TRAINING STATE] */
	else if (p_dm_fat_table->fat_state == FAT_TRAINING_STATE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2. In Training state]\n"));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("fat_beam_n = (( %d )), pre_fat_beam_n = (( %d ))\n",
			pdm_sat_table->fast_training_beam_num, pdm_sat_table->pre_fast_training_beam_num));

		if (pdm_sat_table->fast_training_beam_num > pdm_sat_table->pre_fast_training_beam_num)

			pdm_sat_table->force_update_beam_en = 0;

		else {

			pdm_sat_table->force_update_beam_en = 1;

			pdm_sat_table->pkt_counter = 0;
			beam_tmp = pdm_sat_table->fast_training_beam_num;
			if (pdm_sat_table->fast_training_beam_num >= (pdm_sat_table->beam_patten_num_each_ant - 1)) {

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Timeout Update]  Beam_num (( %d )) -> (( decision ))\n", pdm_sat_table->fast_training_beam_num));
				p_dm_fat_table->fat_state = FAT_DECISION_STATE;
				odm_fast_ant_training_hl_smart_antenna_type1(p_dm_odm);

			} else {
				pdm_sat_table->fast_training_beam_num++;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Timeout Update]  Beam_num (( %d )) -> (( %d ))\n", beam_tmp, pdm_sat_table->fast_training_beam_num));
				phydm_set_all_ant_same_beam_num(p_dm_odm);
				p_dm_fat_table->fat_state = FAT_TRAINING_STATE;

			}
		}
		pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[prepare state] Update Pre_Beam =(( %d ))\n", pdm_sat_table->pre_fast_training_beam_num));
	}
	/*  [Prepare state] */
	/*=======================================================================================*/
	else if (p_dm_fat_table->fat_state == FAT_PREPARE_STATE) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n\n[ 1. In Prepare state]\n"));

		if (p_dm_odm->pre_traffic_load == (p_dm_odm->traffic_load)) {
			if (pdm_sat_table->decision_holding_period != 0) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Holding_period = (( %d )), return!!!\n", pdm_sat_table->decision_holding_period));
				pdm_sat_table->decision_holding_period--;
				return;
			}
		}


		/* Set training packet number*/
		if (pdm_sat_table->fix_training_num_en == 0) {

			switch (p_dm_odm->traffic_load) {

			case TRAFFIC_HIGH:
				pdm_sat_table->per_beam_training_pkt_num = 8;
				pdm_sat_table->decision_holding_period = 2;
				break;
			case TRAFFIC_MID:
				pdm_sat_table->per_beam_training_pkt_num = 6;
				pdm_sat_table->decision_holding_period = 3;
				break;
			case TRAFFIC_LOW:
				pdm_sat_table->per_beam_training_pkt_num = 3; /*ping 60000*/
				pdm_sat_table->decision_holding_period = 4;
				break;
			case TRAFFIC_ULTRA_LOW:
				pdm_sat_table->per_beam_training_pkt_num = 1;
				pdm_sat_table->decision_holding_period = 6;
				break;
			default:
				break;
			}
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Fix_training_en = (( %d )), training_pkt_num_base = (( %d )), holding_period = ((%d))\n",
			pdm_sat_table->fix_training_num_en, pdm_sat_table->per_beam_training_pkt_num, pdm_sat_table->decision_holding_period));


#ifdef CONFIG_FAT_PATCH
		break_counter = 0;
		for (i = (pdm_sat_table->first_train_ant - 1); i < pdm_sat_table->ant_num_total; i++) {
			for (j = 0; j < (pdm_sat_table->beam_patten_num_each_ant); j++) {

				per_beam_rssi_diff_tmp = pdm_sat_table->beam_train_rssi_diff[i][j];
				training_pkt_num_offset = per_beam_rssi_diff_tmp;

				if ((pdm_sat_table->per_beam_training_pkt_num) > training_pkt_num_offset)
					pdm_sat_table->beam_train_cnt[i][j] = pdm_sat_table->per_beam_training_pkt_num - training_pkt_num_offset;
				else
					pdm_sat_table->beam_train_cnt[i][j] = 1;


				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ant[%d]: Beam_num-(( %d ))  training_pkt_num = ((%d))\n",
					i,  j, pdm_sat_table->beam_train_cnt[i][j]));
			}
			break_counter++;
			if (break_counter >= (pdm_sat_table->ant_num))
				break;
		}


		phydm_fast_training_enable(p_dm_odm, FAT_OFF);
		pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
		pdm_sat_table->update_beam_idx = 0;

		if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set 5G ant\n"));
			/*used_ant = (pdm_sat_table->first_train_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;*/
			used_ant = pdm_sat_table->first_train_ant;
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set 2.4G ant\n"));
			used_ant = pdm_sat_table->first_train_ant;
		}

		odm_update_rx_idle_ant(p_dm_odm, (u8)used_ant);

#else
		/* Set training MAC addr. of target */
		odm_set_next_mac_addr_target(p_dm_odm);
		phydm_fast_training_enable(p_dm_odm, FAT_ON);
#endif

		odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
		pdm_sat_table->pkt_counter = 0;
		pdm_sat_table->fast_training_beam_num = 0;
		phydm_set_all_ant_same_beam_num(p_dm_odm);
		pdm_sat_table->pre_fast_training_beam_num = pdm_sat_table->fast_training_beam_num;
		p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
	}

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void
phydm_beam_switch_workitem_callback(
	void	*p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	pdm_sat_table->pkt_skip_statistic_en = 1;
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Beam Switch Workitem Callback, pkt_skip_statistic_en = (( %d ))\n", pdm_sat_table->pkt_skip_statistic_en));

	phydm_update_beam_pattern(p_dm_odm, pdm_sat_table->update_beam_codeword, pdm_sat_table->rfu_codeword_total_bit_num);

#if DEV_BUS_TYPE != RT_PCI_INTERFACE
	/*odm_stall_execution(pdm_sat_table->latch_time);*/
	pdm_sat_table->pkt_skip_statistic_en = 0;
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pkt_skip_statistic_en = (( %d )), latch_time = (( %d ))\n", pdm_sat_table->pkt_skip_statistic_en, pdm_sat_table->latch_time));
}

void
phydm_beam_decision_workitem_callback(
	void	*p_context
)
{
	struct _ADAPTER		*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] Beam decision Workitem Callback\n"));
	odm_fast_ant_training_hl_smart_antenna_type1(p_dm_odm);
}
#endif

#endif /*#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1*/

void
odm_ant_div_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _sw_antenna_switch_			*p_dm_swat_table = &p_dm_odm->dm_swat_table;


	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!]   Not Support Antenna Diversity Function\n"));
		return;
	}
	/* --- */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (p_dm_fat_table->ant_div_2g_5g == ODM_ANTDIV_2G) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[2G AntDiv Init]: Only Support 2G Antenna Diversity Function\n"));
		if (!(p_dm_odm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	} else	if (p_dm_fat_table->ant_div_2g_5g == ODM_ANTDIV_5G) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[5G AntDiv Init]: Only Support 5G Antenna Diversity Function\n"));
		if (!(p_dm_odm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC))
			return;
	} else	if (p_dm_fat_table->ant_div_2g_5g == (ODM_ANTDIV_2G | ODM_ANTDIV_5G))
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[2G & 5G AntDiv Init]:Support Both 2G & 5G Antenna Diversity Function\n"));

#endif
	/* --- */

	/* 2 [--General---] */
	p_dm_odm->antdiv_period = 0;

	p_dm_fat_table->is_become_linked = false;
	p_dm_fat_table->ant_div_on_off = 0xff;

	/* 3       -   AP   - */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	odm_bdc_init(p_dm_odm);
#endif
#endif

	/* 3     -   WIN   - */
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	p_dm_swat_table->ant_5g = MAIN_ANT;
	p_dm_swat_table->ant_2g = MAIN_ANT;
#endif

	/* 2 [---Set MAIN_ANT as default antenna if Auto-ant enable---] */
	odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);

	p_dm_odm->ant_type = ODM_AUTO_ANT;

	p_dm_fat_table->rx_idle_ant = 0xff; /*to make RX-idle-antenna will be updated absolutly*/
	odm_update_rx_idle_ant(p_dm_odm, MAIN_ANT);
	phydm_keep_rx_ack_ant_by_tx_ant_time(p_dm_odm, 0);  /* Timming issue: keep Rx ant after tx for ACK ( 5 x 3.2 mu = 16mu sec)*/

	/* 2 [---Set TX Antenna---] */
	if (p_dm_fat_table->p_force_tx_ant_by_desc == NULL) {
	p_dm_fat_table->force_tx_ant_by_desc = 0;
	p_dm_fat_table->p_force_tx_ant_by_desc = &(p_dm_fat_table->force_tx_ant_by_desc);
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("p_force_tx_ant_by_desc = %d\n", *p_dm_fat_table->p_force_tx_ant_by_desc));

	if (*(p_dm_fat_table->p_force_tx_ant_by_desc) == true)
		odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
	else
	odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);


	/* 2 [--88E---] */
	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		/* p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV; */
		/* p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV; */

		if ((p_dm_odm->ant_div_type != CGCS_RX_HW_ANTDIV)  && (p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV) && (p_dm_odm->ant_div_type != CG_TRX_SMART_ANTDIV)) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!]  88E Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_rx_hw_ant_div_init_88e(p_dm_odm);
		else if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_88e(p_dm_odm);
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_smart_hw_ant_div_init_88e(p_dm_odm);
#endif
#endif
	}

	/* 2 [--92E---] */
#if (RTL8192E_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		/* p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV; */
		/* p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV; */

		if ((p_dm_odm->ant_div_type != CGCS_RX_HW_ANTDIV) && (p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV)   && (p_dm_odm->ant_div_type != CG_TRX_SMART_ANTDIV)) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!]  8192E Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_rx_hw_ant_div_init_92e(p_dm_odm);
		else if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_92e(p_dm_odm);
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_smart_hw_ant_div_init_92e(p_dm_odm);
#endif

	}
#endif

	/* 2 [--8723B---] */
#if (RTL8723B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		/* p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (p_dm_odm->ant_div_type != S0S1_SW_ANTDIV && p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8723B  Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV)
			odm_s0s1_sw_ant_div_init_8723b(p_dm_odm);
		else if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8723b(p_dm_odm);
	}
#endif
	/*2 [--8723D---]*/
#if (RTL8723D_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8723D) {
		if (p_dm_fat_table->p_default_s0_s1 == NULL) {
			p_dm_fat_table->default_s0_s1 = 1;
			p_dm_fat_table->p_default_s0_s1 = &(p_dm_fat_table->default_s0_s1);
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("default_s0_s1 = %d\n", *p_dm_fat_table->p_default_s0_s1));

		if (*(p_dm_fat_table->p_default_s0_s1) == true)
			odm_update_rx_idle_ant(p_dm_odm, MAIN_ANT);
		else
			odm_update_rx_idle_ant(p_dm_odm, AUX_ANT);

		if (p_dm_odm->ant_div_type == S0S1_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8723d(p_dm_odm);
		else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8723D  Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

	}
#endif
	/* 2 [--8811A 8821A---] */
#if (RTL8821A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821) {
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
		p_dm_odm->ant_div_type = HL_SW_SMART_ANT_TYPE1;

		if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {

			odm_trx_hw_ant_div_init_8821a(p_dm_odm);
			phydm_hl_smart_ant_type1_init_8821a(p_dm_odm);
		} else
		#endif
		{
			/*p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;*/
			p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;

			if (p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV && p_dm_odm->ant_div_type != S0S1_SW_ANTDIV) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8821A & 8811A  Not Supprrt This AntDiv type\n"));
				p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
				return;
			}
			if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
				odm_trx_hw_ant_div_init_8821a(p_dm_odm);
			else if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV)
				odm_s0s1_sw_ant_div_init_8821a(p_dm_odm);
		}
	}
#endif

	/* 2 [--8821C---] */
#if (RTL8821C_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821C) {
		p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
		if (p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8821C  Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		odm_trx_hw_ant_div_init_8821c(p_dm_odm);
	}
#endif

	/* 2 [--8881A---] */
#if (RTL8881A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
		/* p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV) {

			odm_trx_hw_ant_div_init_8881a(p_dm_odm);
			/**/
		} else {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8881A  Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		odm_trx_hw_ant_div_init_8881a(p_dm_odm);
	}
#endif

	/* 2 [--8812---] */
#if (RTL8812A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		/* p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (p_dm_odm->ant_div_type != CG_TRX_HW_ANTDIV) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!] 8812A  Not Supprrt This AntDiv type\n"));
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		odm_trx_hw_ant_div_init_8812a(p_dm_odm);
	}
#endif

	/*[--8188F---]*/
#if (RTL8188F_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8188F) {

		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		odm_s0s1_sw_ant_div_init_8188f(p_dm_odm);
	}
#endif

	/*[--8822B---]*/
#if (RTL8822B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8822B) {
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		p_dm_odm->ant_div_type = HL_SW_SMART_ANT_TYPE2;

		if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE2)
			phydm_hl_smart_ant_type2_init_8822b(p_dm_odm);
		#endif
	}
#endif

	/*
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** support_ic_type=[%lu]\n",p_dm_odm->support_ic_type));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** AntDiv support_ability=[%lu]\n",(p_dm_odm->support_ability & ODM_BB_ANT_DIV)>>6));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** AntDiv type=[%d]\n",p_dm_odm->ant_div_type));
	*/
}

void
odm_ant_div(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*p_adapter	= p_dm_odm->adapter;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))
	struct _SMART_ANTENNA_TRAINNING_			*pdm_sat_table = &(p_dm_odm->dm_sat_table);
#endif

	if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
		if (p_dm_fat_table->idx_ant_div_counter_5g <  p_dm_odm->antdiv_period) {
			p_dm_fat_table->idx_ant_div_counter_5g++;
			return;
		} else
			p_dm_fat_table->idx_ant_div_counter_5g = 0;
	} else	if (*p_dm_odm->p_band_type == ODM_BAND_2_4G) {
		if (p_dm_fat_table->idx_ant_div_counter_2g <  p_dm_odm->antdiv_period) {
			p_dm_fat_table->idx_ant_div_counter_2g++;
			return;
		} else
			p_dm_fat_table->idx_ant_div_counter_2g = 0;
	}

	/* ---------- */
	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Return!!!]   Not Support Antenna Diversity Function\n"));
		return;
	}

	/* ---------- */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	if (p_dm_fat_table->enable_ctrl_frame_antdiv) {

		if ((p_dm_odm->data_frame_num <= 10) && (p_dm_odm->is_linked))
			p_dm_fat_table->use_ctrl_frame_antdiv = 1;
		else
			p_dm_fat_table->use_ctrl_frame_antdiv = 0;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("use_ctrl_frame_antdiv = (( %d )), data_frame_num = (( %d ))\n", p_dm_fat_table->use_ctrl_frame_antdiv, p_dm_odm->data_frame_num));
		p_dm_odm->data_frame_num = 0;
	}

	if (p_adapter->MgntInfo.AntennaTest)
		return;

	{
#if (BEAMFORMING_SUPPORT == 1)
		enum beamforming_cap		beamform_cap = (p_dm_odm->beamforming_info.beamform_cap);

		if (beamform_cap & BEAMFORMEE_CAP) { /* BFmee On  &&   Div On->Div Off */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ AntDiv : OFF ]   BFmee ==1\n"));
			if (p_dm_fat_table->fix_ant_bfee == 0) {
				odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
				p_dm_fat_table->fix_ant_bfee = 1;
			}
			return;
		} else { /* BFmee Off   &&   Div Off->Div On */
			if ((p_dm_fat_table->fix_ant_bfee == 1)  &&  p_dm_odm->is_linked) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ AntDiv : ON ]   BFmee ==0\n"));
				if ((p_dm_odm->ant_div_type != S0S1_SW_ANTDIV))
					odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);

				p_dm_fat_table->fix_ant_bfee = 0;
			}
		}
#endif
	}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	/* ----------just for fool proof */

	if (p_dm_odm->antdiv_rssi)
		p_dm_odm->debug_components |= ODM_COMP_ANT_DIV;
	else
		p_dm_odm->debug_components &= ~ODM_COMP_ANT_DIV;

	if (p_dm_fat_table->ant_div_2g_5g == ODM_ANTDIV_2G) {
		/* ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 2G AntDiv Running ]\n")); */
		if (!(p_dm_odm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	} else if (p_dm_fat_table->ant_div_2g_5g == ODM_ANTDIV_5G) {
		/* ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 5G AntDiv Running ]\n")); */
		if (!(p_dm_odm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC))
			return;
	}
	/* else 	if(p_dm_fat_table->ant_div_2g_5g == (ODM_ANTDIV_2G|ODM_ANTDIV_5G)) */
	/* { */
	/* ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 2G & 5G AntDiv Running ]\n")); */
	/* } */
#endif

	/* ---------- */

	if (p_dm_odm->antdiv_select == 1)
		p_dm_odm->ant_type = ODM_FIX_MAIN_ANT;
	else if (p_dm_odm->antdiv_select == 2)
		p_dm_odm->ant_type = ODM_FIX_AUX_ANT;
	else  /* if (p_dm_odm->antdiv_select==0) */
		p_dm_odm->ant_type = ODM_AUTO_ANT;

	/* ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("ant_type= (( %d )) , pre_ant_type= (( %d ))\n",p_dm_odm->ant_type,p_dm_odm->pre_ant_type)); */

	if (p_dm_odm->ant_type != ODM_AUTO_ANT) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Fix Antenna at (( %s ))\n", (p_dm_odm->ant_type == ODM_FIX_MAIN_ANT) ? "MAIN" : "AUX"));

		if (p_dm_odm->ant_type != p_dm_odm->pre_ant_type) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_REG);

			if (p_dm_odm->ant_type == ODM_FIX_MAIN_ANT)
				odm_update_rx_idle_ant(p_dm_odm, MAIN_ANT);
			else if (p_dm_odm->ant_type == ODM_FIX_AUX_ANT)
				odm_update_rx_idle_ant(p_dm_odm, AUX_ANT);
		}
		p_dm_odm->pre_ant_type = p_dm_odm->ant_type;
		return;
	} else {
		if (p_dm_odm->ant_type != p_dm_odm->pre_ant_type) {
			odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
			odm_tx_by_tx_desc_or_reg(p_dm_odm, TX_BY_DESC);
		}
		p_dm_odm->pre_ant_type = p_dm_odm->ant_type;
	}


	/* 3 ----------------------------------------------------------------------------------------------------------- */
	/* 2 [--88E---] */
	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV || p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_hw_ant_div(p_dm_odm);

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_fast_ant_training(p_dm_odm);
#endif

#endif

	}
	/* 2 [--92E---] */
#if (RTL8192E_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV || p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_hw_ant_div(p_dm_odm);

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_fast_ant_training(p_dm_odm);
#endif

	}
#endif

#if (RTL8723B_SUPPORT == 1)
	/* 2 [--8723B---] */
	else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
		if (phydm_is_bt_enable_8723b(p_dm_odm)) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[BT is enable!!!]\n"));
			if (p_dm_fat_table->is_become_linked == true) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set REG 948[9:6]=0x0\n"));
				if (p_dm_odm->support_ic_type == ODM_RTL8723B)
					odm_set_bb_reg(p_dm_odm, 0x948, BIT(9) | BIT(8) | BIT(7) | BIT(6), 0x0);

				p_dm_fat_table->is_become_linked = false;
			}
		} else {
			if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
				odm_s0s1_sw_ant_div(p_dm_odm, SWAW_STEP_PEEK);
#endif
			} else if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
				odm_hw_ant_div(p_dm_odm);
		}
	}
#endif
	/*8723D*/
#if (RTL8723D_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8723D) {

		odm_hw_ant_div(p_dm_odm);
		/**/
	}
#endif

	/* 2 [--8821A---] */
#if (RTL8821A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821) {
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
		if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {

			if (pdm_sat_table->fix_beam_pattern_en != 0) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [ SmartAnt ] Fix SmartAnt Pattern = 0x%x\n", pdm_sat_table->fix_beam_pattern_codeword));
				/*return;*/
			} else {
				/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ SmartAnt ] ant_div_type = HL_SW_SMART_ANT_TYPE1\n"));*/
				odm_fast_ant_training_hl_smart_antenna_type1(p_dm_odm);
			}

		} else
		#endif
		{

			if (!p_dm_odm->is_bt_enabled) { /*BT disabled*/
				if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {
					p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [S0S1_SW_ANTDIV]  ->  [CG_TRX_HW_ANTDIV]\n"));
					/*odm_set_bb_reg(p_dm_odm, 0x8D4, BIT24, 1); */
					if (p_dm_fat_table->is_become_linked == true)
						odm_ant_div_on_off(p_dm_odm, ANTDIV_ON);
				}

			} else { /*BT enabled*/

				if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV) {
					p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [CG_TRX_HW_ANTDIV]  ->  [S0S1_SW_ANTDIV]\n"));
					/*odm_set_bb_reg(p_dm_odm, 0x8D4, BIT24, 0);*/
					odm_ant_div_on_off(p_dm_odm, ANTDIV_OFF);
				}
			}

			if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
				odm_s0s1_sw_ant_div(p_dm_odm, SWAW_STEP_PEEK);
#endif
			} else if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV)
				odm_hw_ant_div(p_dm_odm);
		}
	}
#endif

	/* 2 [--8821C---] */
#if (RTL8821C_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821C)
		odm_hw_ant_div(p_dm_odm);
#endif

	/* 2 [--8881A---] */
#if (RTL8881A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8881A)
		odm_hw_ant_div(p_dm_odm);
#endif

	/* 2 [--8812A---] */
#if (RTL8812A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8812)
		odm_hw_ant_div(p_dm_odm);
#endif

#if (RTL8188F_SUPPORT == 1)
	/* [--8188F---]*/
	else if (p_dm_odm->support_ic_type == ODM_RTL8188F)	{

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_s0s1_sw_ant_div(p_dm_odm, SWAW_STEP_PEEK);
#endif
	}
#endif

	/* [--8822B---]*/
#if (RTL8822B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8822B) {
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE2) {

			if (pdm_sat_table->fix_beam_pattern_en != 0)
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" [ SmartAnt ] Fix SmartAnt Pattern = 0x%x\n", pdm_sat_table->fix_beam_pattern_codeword));
			else
				phydm_fast_ant_training_hl_smart_antenna_type2(p_dm_odm);
		}
		#endif
	}
#endif


}


void
odm_antsel_statistics(
	void			*p_dm_void,
	u8			antsel_tr_mux,
	u32			mac_id,
	u32			utility,
	u8			method,
	u8			is_cck_rate

)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	if (method == RSSI_METHOD) {

		if (is_cck_rate) {
			if (antsel_tr_mux == ANT1_2G) {
				if (p_dm_fat_table->main_ant_sum_cck[mac_id] > 65435) /*to prevent u16 overflow, max(RSSI)=100, 65435+100 = 65535 (u16)*/
					return;

				p_dm_fat_table->main_ant_sum_cck[mac_id] += (u16)utility;
				p_dm_fat_table->main_ant_cnt_cck[mac_id]++;
			} else {
				if (p_dm_fat_table->aux_ant_sum_cck[mac_id] > 65435)
					return;

				p_dm_fat_table->aux_ant_sum_cck[mac_id] += (u16)utility;
				p_dm_fat_table->aux_ant_cnt_cck[mac_id]++;
			}

		} else { /*ofdm rate*/

			if (antsel_tr_mux == ANT1_2G) {
				if (p_dm_fat_table->main_ant_sum[mac_id] > 65435)
					return;

				p_dm_fat_table->main_ant_sum[mac_id] += (u16)utility;
				p_dm_fat_table->main_ant_cnt[mac_id]++;
			} else {
				if (p_dm_fat_table->aux_ant_sum[mac_id] > 65435)
					return;

				p_dm_fat_table->aux_ant_sum[mac_id] += (u16)utility;
				p_dm_fat_table->aux_ant_cnt[mac_id]++;
			}
		}
	}
#ifdef ODM_EVM_ENHANCE_ANTDIV
	else if (method == EVM_METHOD) {
		if (antsel_tr_mux == ANT1_2G) {
			p_dm_fat_table->main_ant_evm_sum[mac_id] += (utility << 5);
			p_dm_fat_table->main_ant_evm_cnt[mac_id]++;
		} else {
			p_dm_fat_table->aux_ant_evm_sum[mac_id] += (utility << 5);
			p_dm_fat_table->aux_ant_evm_cnt[mac_id]++;
		}
	} else if (method == CRC32_METHOD) {
		if (utility == 0)
			p_dm_fat_table->crc32_fail_cnt++;
		else
			p_dm_fat_table->crc32_ok_cnt += utility;
	}
#endif
}

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
void
phydm_process_rssi_for_hb_smtant_type2(
	void		*p_dm_void,
	void		*p_phy_info_void,
	void		*p_pkt_info_void,
	u8		rx_power_ant0,
	u8		rx_power_ant1,
	u8		rssi_avg	
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _odm_phy_status_info_			*p_phy_info = (struct _odm_phy_status_info_ *)p_phy_info_void;
	struct _odm_per_pkt_info_				*p_pktinfo = (struct _odm_per_pkt_info_ *)p_pkt_info_void;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u8		train_pkt_number;
	u32		beam_tmp;

	/*[Beacon]*/
	if (p_pktinfo->is_packet_beacon) {

		pdm_sat_table->beacon_counter++;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MatchBSSID_beacon_counter = ((%d))\n", pdm_sat_table->beacon_counter));

		if (pdm_sat_table->beacon_counter >= pdm_sat_table->pre_beacon_counter + 2) {

			pdm_sat_table->update_beam_idx++;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pre_beacon_counter = ((%d)), pkt_counter = ((%d)), update_beam_idx = ((%d))\n",
				pdm_sat_table->pre_beacon_counter, pdm_sat_table->pkt_counter, pdm_sat_table->update_beam_idx));
			
			pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
			pdm_sat_table->pkt_counter = 0;
		}
	}
	/*[data]*/
	else if (p_pktinfo->is_packet_to_self) {

		if (pdm_sat_table->pkt_skip_statistic_en == 0) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ID[%d][pkt_cnt = %d]: Beam_pair = {%d}, RSSI{A,B,avg} = {%d, %d, %d}\n",
				p_pktinfo->station_id, pdm_sat_table->pkt_counter,  pdm_sat_table->fast_training_beam_num, rx_power_ant0, rx_power_ant1, rssi_avg));

			pdm_sat_table->pkt_rssi_sum[0][pdm_sat_table->fast_training_beam_num] += rssi_avg;
			pdm_sat_table->pkt_rssi_cnt[0][pdm_sat_table->fast_training_beam_num]++;
			
			pdm_sat_table->pkt_counter++;

			train_pkt_number = pdm_sat_table->beam_set_train_cnt[pdm_sat_table->fast_training_beam_num];

			if (pdm_sat_table->pkt_counter >= train_pkt_number) {

				pdm_sat_table->update_beam_idx++;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pre_beacon_counter = ((%d)), Update_new_beam = ((%d))\n",
					pdm_sat_table->pre_beacon_counter, pdm_sat_table->update_beam_idx));
				
				pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
				pdm_sat_table->pkt_counter = 0;
			}
		}
	}

	if (pdm_sat_table->update_beam_idx > 0) {
		
		pdm_sat_table->update_beam_idx = 0;

		if (pdm_sat_table->fast_training_beam_num >= ((u32)pdm_sat_table->total_beam_set_num - 1)) {

			p_dm_fat_table->fat_state = FAT_DECISION_STATE;

			#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			phydm_fast_ant_training_hl_smart_antenna_type2(p_dm_odm); /*go to make decision*/
			#else
			odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_decision_workitem);
			#endif


		} else {
			beam_tmp = pdm_sat_table->fast_training_beam_num;
			pdm_sat_table->fast_training_beam_num++;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Update Beam_num (( %d )) -> (( %d ))\n", beam_tmp, pdm_sat_table->fast_training_beam_num));
			phydm_set_rfu_beam_pattern_type2(p_dm_odm);

			p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
		}
	}
	
}
#endif

void
odm_process_rssi_for_ant_div(
	void			*p_dm_void,
	void			*p_phy_info_void,
	void			*p_pkt_info_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _odm_phy_status_info_			*p_phy_info = (struct _odm_phy_status_info_ *)p_phy_info_void;
	struct _odm_per_pkt_info_				*p_pktinfo = (struct _odm_per_pkt_info_ *)p_pkt_info_void;
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &p_dm_odm->dm_fat_table;
#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))
	struct _SMART_ANTENNA_TRAINNING_	*pdm_sat_table = &(p_dm_odm->dm_sat_table);
	u32			beam_tmp;
	u8			next_ant;
	u8			train_pkt_number;
#endif
	u8			is_cck_rate = FALSE;
	u8			rx_power_ant0 = p_phy_info->rx_mimo_signal_strength[0];
	u8			rx_power_ant1 = p_phy_info->rx_mimo_signal_strength[1];
	u8			rx_evm_ant0 = p_phy_info->rx_mimo_signal_quality[0];
	u8			rx_evm_ant1 = p_phy_info->rx_mimo_signal_quality[1];
	u8			rssi_avg;

	is_cck_rate = (p_pktinfo->data_rate <= ODM_RATE11M) ? TRUE : FALSE;

	if ((p_dm_odm->support_ic_type & ODM_IC_2SS) && (!is_cck_rate)) {

		if (rx_power_ant1 < 100)
			rssi_avg = (u8)odm_convert_to_db((odm_convert_to_linear(rx_power_ant0) + odm_convert_to_linear(rx_power_ant1))>>1); /*averaged PWDB*/
		
	} else {
		rx_power_ant0 = (u8)p_phy_info->rx_pwdb_all;
		rssi_avg = rx_power_ant0;
	}
	
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
	if ((p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE2) && (p_dm_fat_table->fat_state == FAT_TRAINING_STATE)) {
			/*for 8822B*/
			phydm_process_rssi_for_hb_smtant_type2(p_dm_odm, p_phy_info, p_pktinfo, rx_power_ant0, rx_power_ant1, rssi_avg);
	} else
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
#ifdef CONFIG_FAT_PATCH
	if ((p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE1) && (p_dm_fat_table->fat_state == FAT_TRAINING_STATE)) {

		/*[Beacon]*/
		if (p_pktinfo->is_packet_beacon) {

			pdm_sat_table->beacon_counter++;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MatchBSSID_beacon_counter = ((%d))\n", pdm_sat_table->beacon_counter));

			if (pdm_sat_table->beacon_counter >= pdm_sat_table->pre_beacon_counter + 2) {

				if (pdm_sat_table->ant_num > 1) {
					next_ant = (p_dm_fat_table->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
					odm_update_rx_idle_ant(p_dm_odm, next_ant);
				}

				pdm_sat_table->update_beam_idx++;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pre_beacon_counter = ((%d)), pkt_counter = ((%d)), update_beam_idx = ((%d))\n",
					pdm_sat_table->pre_beacon_counter, pdm_sat_table->pkt_counter, pdm_sat_table->update_beam_idx));

				pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
				pdm_sat_table->pkt_counter = 0;
			}
		}
		/*[data]*/
		else if (p_pktinfo->is_packet_to_self) {

			if (pdm_sat_table->pkt_skip_statistic_en == 0) {
				/*
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("StaID[%d]:  antsel_pathA = ((%d)), hw_antsw_occur = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					p_pktinfo->station_id, p_dm_fat_table->antsel_rx_keep_0, p_dm_fat_table->hw_antsw_occur, pdm_sat_table->fast_training_beam_num, rx_power_ant0));
				*/
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ID[%d][pkt_cnt = %d]: {ANT, Beam} = {%d, %d}, RSSI = ((%d))\n",
					p_pktinfo->station_id, pdm_sat_table->pkt_counter, p_dm_fat_table->antsel_rx_keep_0, pdm_sat_table->fast_training_beam_num, rx_power_ant0));

				pdm_sat_table->pkt_rssi_sum[p_dm_fat_table->antsel_rx_keep_0][pdm_sat_table->fast_training_beam_num] += rx_power_ant0;
				pdm_sat_table->pkt_rssi_cnt[p_dm_fat_table->antsel_rx_keep_0][pdm_sat_table->fast_training_beam_num]++;
				pdm_sat_table->pkt_counter++;

				#if 1
				train_pkt_number = pdm_sat_table->beam_train_cnt[p_dm_fat_table->rx_idle_ant - 1][pdm_sat_table->fast_training_beam_num];
				#else
				train_pkt_number =  pdm_sat_table->per_beam_training_pkt_num;
				#endif

				/*Swich Antenna erery N pkts*/
				if (pdm_sat_table->pkt_counter == train_pkt_number) {

					if (pdm_sat_table->ant_num > 1) {

						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("packet enugh ((%d ))pkts ---> Switch antenna\n", train_pkt_number));
						next_ant = (p_dm_fat_table->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
						odm_update_rx_idle_ant(p_dm_odm, next_ant);
					}

					pdm_sat_table->update_beam_idx++;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pre_beacon_counter = ((%d)), update_beam_idx_counter = ((%d))\n",
						pdm_sat_table->pre_beacon_counter, pdm_sat_table->update_beam_idx));

					pdm_sat_table->pre_beacon_counter = pdm_sat_table->beacon_counter;
					pdm_sat_table->pkt_counter = 0;
				}
			}
		}

		/*Swich Beam after switch "pdm_sat_table->ant_num" antennas*/
		if (pdm_sat_table->update_beam_idx == pdm_sat_table->ant_num) {

			pdm_sat_table->update_beam_idx = 0;
			pdm_sat_table->pkt_counter = 0;
			beam_tmp = pdm_sat_table->fast_training_beam_num;

			if (pdm_sat_table->fast_training_beam_num >= (pdm_sat_table->beam_patten_num_each_ant - 1)) {

				p_dm_fat_table->fat_state = FAT_DECISION_STATE;

				#if DEV_BUS_TYPE == RT_PCI_INTERFACE
				odm_fast_ant_training_hl_smart_antenna_type1(p_dm_odm);
				#else
				odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_decision_workitem);
				#endif


			} else {
				pdm_sat_table->fast_training_beam_num++;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Update Beam_num (( %d )) -> (( %d ))\n", beam_tmp, pdm_sat_table->fast_training_beam_num));
				phydm_set_all_ant_same_beam_num(p_dm_odm);

				p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
			}
		}

	}
#else

	if (p_dm_odm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {
		if ((p_dm_odm->support_ic_type & ODM_HL_SMART_ANT_TYPE1_SUPPORT) &&
		    (p_pktinfo->is_packet_to_self)   &&
		    (p_dm_fat_table->fat_state == FAT_TRAINING_STATE)
		   ) {

			if (pdm_sat_table->pkt_skip_statistic_en == 0) {
				/*
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("StaID[%d]:  antsel_pathA = ((%d)), hw_antsw_occur = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					p_pktinfo->station_id, p_dm_fat_table->antsel_rx_keep_0, p_dm_fat_table->hw_antsw_occur, pdm_sat_table->fast_training_beam_num, rx_power_ant0));
				*/
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("StaID[%d]:  antsel_pathA = ((%d)), is_packet_to_self = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					p_pktinfo->station_id, p_dm_fat_table->antsel_rx_keep_0, p_pktinfo->is_packet_to_self, pdm_sat_table->fast_training_beam_num, rx_power_ant0));


				pdm_sat_table->pkt_rssi_sum[p_dm_fat_table->antsel_rx_keep_0][pdm_sat_table->fast_training_beam_num] += rx_power_ant0;
				pdm_sat_table->pkt_rssi_cnt[p_dm_fat_table->antsel_rx_keep_0][pdm_sat_table->fast_training_beam_num]++;
				pdm_sat_table->pkt_counter++;

				/*swich beam every N pkt*/
				if ((pdm_sat_table->pkt_counter) >= (pdm_sat_table->per_beam_training_pkt_num)) {

					pdm_sat_table->pkt_counter = 0;
					beam_tmp = pdm_sat_table->fast_training_beam_num;

					if (pdm_sat_table->fast_training_beam_num >= (pdm_sat_table->beam_patten_num_each_ant - 1)) {

						p_dm_fat_table->fat_state = FAT_DECISION_STATE;

						#if DEV_BUS_TYPE == RT_PCI_INTERFACE
						odm_fast_ant_training_hl_smart_antenna_type1(p_dm_odm);
						#else
						odm_schedule_work_item(&pdm_sat_table->hl_smart_antenna_decision_workitem);
						#endif


					} else {
						pdm_sat_table->fast_training_beam_num++;
						phydm_set_all_ant_same_beam_num(p_dm_odm);

						p_dm_fat_table->fat_state = FAT_TRAINING_STATE;
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Update  Beam_num (( %d )) -> (( %d ))\n", beam_tmp, pdm_sat_table->fast_training_beam_num));
					}
				}
			}
		}
	}
#endif
	else
#endif
		if (p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV) {
			if ((p_dm_odm->support_ic_type & ODM_SMART_ANT_SUPPORT) && (p_pktinfo->is_packet_to_self)   && (p_dm_fat_table->fat_state == FAT_TRAINING_STATE)) { /* (p_pktinfo->is_packet_match_bssid && (!p_pktinfo->is_packet_beacon)) */
				u8	antsel_tr_mux;
				antsel_tr_mux = (p_dm_fat_table->antsel_rx_keep_2 << 2) | (p_dm_fat_table->antsel_rx_keep_1 << 1) | p_dm_fat_table->antsel_rx_keep_0;
				p_dm_fat_table->ant_sum_rssi[antsel_tr_mux] += rx_power_ant0;
				p_dm_fat_table->ant_rssi_cnt[antsel_tr_mux]++;
			}
		} else { /* ant_div_type != CG_TRX_SMART_ANTDIV */
			if ((p_dm_odm->support_ic_type & ODM_ANTDIV_SUPPORT) && (p_pktinfo->is_packet_to_self || p_dm_fat_table->use_ctrl_frame_antdiv)) {

				if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {

					if (is_cck_rate)
						p_dm_fat_table->antsel_rx_keep_0 = (p_dm_fat_table->rx_idle_ant == MAIN_ANT) ? ANT1_2G : ANT2_2G;

						odm_antsel_statistics(p_dm_odm, p_dm_fat_table->antsel_rx_keep_0, p_pktinfo->station_id, rx_power_ant0, RSSI_METHOD, is_cck_rate);

					} else {

					odm_antsel_statistics(p_dm_odm, p_dm_fat_table->antsel_rx_keep_0, p_pktinfo->station_id, rx_power_ant0, RSSI_METHOD, is_cck_rate);

					#ifdef ODM_EVM_ENHANCE_ANTDIV
					if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
						if (!is_cck_rate)
							odm_antsel_statistics(p_dm_odm, p_dm_fat_table->antsel_rx_keep_0, p_pktinfo->station_id, rx_evm_ant0, EVM_METHOD, is_cck_rate);

					}
					#endif
				}
			}
		}
	/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("is_cck_rate=%d, PWDB_ALL=%d\n",is_cck_rate, p_phy_info->rx_pwdb_all)); */
	/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=3'b%d%d%d\n",p_dm_fat_table->antsel_rx_keep_2, p_dm_fat_table->antsel_rx_keep_1, p_dm_fat_table->antsel_rx_keep_0)); */
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void
odm_set_tx_ant_by_tx_info(
	void			*p_dm_void,
	u8			*p_desc,
	u8			mac_id

)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_	*p_dm_fat_table = &p_dm_odm->dm_fat_table;

	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;


	if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
#if (RTL8723B_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8723B(p_desc, p_dm_fat_table->antsel_a[mac_id]);
		/*ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8723B] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
			mac_id, p_dm_fat_table->antsel_c[mac_id], p_dm_fat_table->antsel_b[mac_id], p_dm_fat_table->antsel_a[mac_id]));*/
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8821) {
#if (RTL8821A_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8812(p_desc, p_dm_fat_table->antsel_a[mac_id]);
		/*ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8821A] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
			mac_id, p_dm_fat_table->antsel_c[mac_id], p_dm_fat_table->antsel_b[mac_id], p_dm_fat_table->antsel_a[mac_id]));*/
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_88E(p_desc, p_dm_fat_table->antsel_a[mac_id]);
		SET_TX_DESC_ANTSEL_B_88E(p_desc, p_dm_fat_table->antsel_b[mac_id]);
		SET_TX_DESC_ANTSEL_C_88E(p_desc, p_dm_fat_table->antsel_c[mac_id]);
		/*ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8188E] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
			mac_id, p_dm_fat_table->antsel_c[mac_id], p_dm_fat_table->antsel_b[mac_id], p_dm_fat_table->antsel_a[mac_id]));*/
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8821C) {
#if (RTL8821C_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8821C(p_desc, p_dm_fat_table->antsel_a[mac_id]);
		/*ODM_RT_TRACE(p_dm_odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8821C] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
			mac_id, p_dm_fat_table->antsel_c[mac_id], p_dm_fat_table->antsel_b[mac_id], p_dm_fat_table->antsel_a[mac_id]));*/
#endif
	}
}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

void
odm_set_tx_ant_by_tx_info(
	struct	rtl8192cd_priv		*priv,
	struct	tx_desc	*pdesc,
	unsigned short			aid
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = &(priv->pshare->_dmodm);
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &priv->pshare->_dmodm.dm_fat_table;
	u32		support_ic_type = priv->pshare->_dmodm.support_ic_type;

	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;

	if (support_ic_type == ODM_RTL8881A) {
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******\n",__FUNCTION__,__LINE__);	*/
		pdesc->dword6 &= set_desc(~(BIT(18) | BIT(17) | BIT(16)));
		pdesc->dword6 |= set_desc(p_dm_fat_table->antsel_a[aid] << 16);
	} else if (support_ic_type == ODM_RTL8192E) {
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8192E******\n",__FUNCTION__,__LINE__);	*/
		pdesc->dword6 &= set_desc(~(BIT(18) | BIT(17) | BIT(16)));
		pdesc->dword6 |= set_desc(p_dm_fat_table->antsel_a[aid] << 16);
	} else if (support_ic_type == ODM_RTL8188E) {
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8188E******\n",__FUNCTION__,__LINE__);*/
		pdesc->dword2 &= set_desc(~BIT(24));
		pdesc->dword2 &= set_desc(~BIT(25));
		pdesc->dword7 &= set_desc(~BIT(29));

		pdesc->dword2 |= set_desc(p_dm_fat_table->antsel_a[aid] << 24);
		pdesc->dword2 |= set_desc(p_dm_fat_table->antsel_b[aid] << 25);
		pdesc->dword7 |= set_desc(p_dm_fat_table->antsel_c[aid] << 29);


	} else if (support_ic_type == ODM_RTL8812) {
		/*[path-A]*/
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******\n",__FUNCTION__,__LINE__);*/

		pdesc->dword6 &= set_desc(~BIT(16));
		pdesc->dword6 &= set_desc(~BIT(17));
		pdesc->dword6 &= set_desc(~BIT(18));

		pdesc->dword6 |= set_desc(p_dm_fat_table->antsel_a[aid] << 16);
		pdesc->dword6 |= set_desc(p_dm_fat_table->antsel_b[aid] << 17);
		pdesc->dword6 |= set_desc(p_dm_fat_table->antsel_c[aid] << 18);

	}
}


#if 1 /*def CONFIG_WLAN_HAL*/
void
odm_set_tx_ant_by_tx_info_hal(
	struct	rtl8192cd_priv		*priv,
	void	*pdesc_data,
	u16					aid
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = &(priv->pshare->_dmodm);
	struct _FAST_ANTENNA_TRAINNING_		*p_dm_fat_table = &priv->pshare->_dmodm.dm_fat_table;
	u32		support_ic_type = priv->pshare->_dmodm.support_ic_type;
	PTX_DESC_DATA_88XX	pdescdata = (PTX_DESC_DATA_88XX)pdesc_data;

	if (!(p_dm_odm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (p_dm_odm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;

	if (support_ic_type == ODM_RTL8881A || support_ic_type == ODM_RTL8192E || support_ic_type == ODM_RTL8814A) {
		/*panic_printk("[%s] [%d] ******odm_set_tx_ant_by_tx_info_hal******\n",__FUNCTION__,__LINE__);*/
		pdescdata->ant_sel = 1;
		pdescdata->ant_sel_a = p_dm_fat_table->antsel_a[aid];
	}
}
#endif	/*#ifdef CONFIG_WLAN_HAL*/

#endif


void
odm_ant_div_config(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FAST_ANTENNA_TRAINNING_			*p_dm_fat_table = &p_dm_odm->dm_fat_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("WIN Config Antenna Diversity\n"));
	/*
	if(p_dm_odm->support_ic_type==ODM_RTL8723B)
	{
		if((!p_dm_odm->dm_swat_table.ANTA_ON || !p_dm_odm->dm_swat_table.ANTB_ON))
			p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
	}
	*/
	if (p_dm_odm->support_ic_type == ODM_RTL8723D) {

		p_dm_odm->ant_div_type = S0S1_TRX_HW_ANTDIV;
		/**/
	}
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("CE Config Antenna Diversity\n"));

	if (p_dm_odm->support_ic_type == ODM_RTL8723B)
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;



#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("AP Config Antenna Diversity\n"));

	/* 2 [ NOT_SUPPORT_ANTDIV ] */
#if (defined(CONFIG_NOT_SUPPORT_ANTDIV))
	p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Disable AntDiv function] : Not Support 2.4G & 5G Antenna Diversity\n"));

	/* 2 [ 2G&5G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_2G5G_SUPPORT_ANTDIV))
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : 2.4G & 5G Support Antenna Diversity Simultaneously\n"));
	p_dm_fat_table->ant_div_2g_5g = (ODM_ANTDIV_2G | ODM_ANTDIV_5G);

	if (p_dm_odm->support_ic_type & ODM_ANTDIV_SUPPORT)
		p_dm_odm->support_ability |= ODM_BB_ANT_DIV;
	if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
#if (defined(CONFIG_5G_CGCS_RX_DIVERSITY))
		p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n"));
		panic_printk("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
#elif (defined(CONFIG_5G_CG_TRX_DIVERSITY) || defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A))
		p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n"));
		panic_printk("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CG_SMART_ANTDIV\n"));
#elif (defined(CONFIG_5G_S0S1_SW_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = S0S1_SW_ANTDIV\n"));
#endif
	} else if (*p_dm_odm->p_band_type == ODM_BAND_2_4G) {
#if (defined(CONFIG_2G_CGCS_RX_DIVERSITY))
		p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CGCS_RX_HW_ANTDIV\n"));
#elif (defined(CONFIG_2G_CG_TRX_DIVERSITY) || defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A))
		p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CG_TRX_HW_ANTDIV\n"));
#elif (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CG_SMART_ANTDIV\n"));
#elif (defined(CONFIG_2G_S0S1_SW_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = S0S1_SW_ANTDIV\n"));
#endif
	}

	/* 2 [ 5G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_5G_SUPPORT_ANTDIV))
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : Only 5G Support Antenna Diversity\n"));
	panic_printk("[ Enable AntDiv function] : Only 5G Support Antenna Diversity\n");
	p_dm_fat_table->ant_div_2g_5g = (ODM_ANTDIV_5G);
	if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
		if (p_dm_odm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC)
			p_dm_odm->support_ability |= ODM_BB_ANT_DIV;
#if (defined(CONFIG_5G_CGCS_RX_DIVERSITY))
		p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n"));
		panic_printk("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
#elif (defined(CONFIG_5G_CG_TRX_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
		panic_printk("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n"));
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = CG_SMART_ANTDIV\n"));
#elif (defined(CONFIG_5G_S0S1_SW_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv type = S0S1_SW_ANTDIV\n"));
#endif
	} else if (*p_dm_odm->p_band_type == ODM_BAND_2_4G) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Not Support 2G ant_div_type\n"));
		p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
	}

	/* 2 [ 2G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_2G_SUPPORT_ANTDIV))
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : Only 2.4G Support Antenna Diversity\n"));
	p_dm_fat_table->ant_div_2g_5g = (ODM_ANTDIV_2G);
	if (*p_dm_odm->p_band_type == ODM_BAND_2_4G) {
		if (p_dm_odm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC)
			p_dm_odm->support_ability |= ODM_BB_ANT_DIV;
#if (defined(CONFIG_2G_CGCS_RX_DIVERSITY))
		p_dm_odm->ant_div_type = CGCS_RX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CGCS_RX_HW_ANTDIV\n"));
#elif (defined(CONFIG_2G_CG_TRX_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_HW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CG_TRX_HW_ANTDIV\n"));
#elif (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = CG_TRX_SMART_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = CG_SMART_ANTDIV\n"));
#elif (defined(CONFIG_2G_S0S1_SW_ANT_DIVERSITY))
		p_dm_odm->ant_div_type = S0S1_SW_ANTDIV;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv type = S0S1_SW_ANTDIV\n"));
#endif
	} else if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Not Support 5G ant_div_type\n"));
		p_dm_odm->support_ability &= ~(ODM_BB_ANT_DIV);
	}
#endif
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[AntDiv Config Info] AntDiv_SupportAbility = (( %x ))\n", ((p_dm_odm->support_ability & ODM_BB_ANT_DIV) ? 1 : 0)));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[AntDiv Config Info] be_fix_tx_ant = ((%d))\n", p_dm_odm->dm_fat_table.b_fix_tx_ant));

}


void
odm_ant_div_timers(
	void		*p_dm_void,
	u8		state
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (state == INIT_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_initialize_timer(p_dm_odm, &(p_dm_odm->dm_swat_table.phydm_sw_antenna_switch_timer),
			(void *)odm_sw_antdiv_callback, NULL, "phydm_sw_antenna_switch_timer");
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_initialize_timer(p_dm_odm, &p_dm_odm->fast_ant_training_timer,
			(void *)odm_fast_ant_training_callback, NULL, "fast_ant_training_timer");
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_initialize_timer(p_dm_odm, &p_dm_odm->evm_fast_ant_training_timer,
			(void *)odm_evm_fast_ant_training_callback, NULL, "evm_fast_ant_training_timer");
#endif
	} else if (state == CANCEL_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_cancel_timer(p_dm_odm, &(p_dm_odm->dm_swat_table.phydm_sw_antenna_switch_timer));
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_cancel_timer(p_dm_odm, &p_dm_odm->fast_ant_training_timer);
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_cancel_timer(p_dm_odm, &p_dm_odm->evm_fast_ant_training_timer);
#endif
	} else if (state == RELEASE_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_release_timer(p_dm_odm, &(p_dm_odm->dm_swat_table.phydm_sw_antenna_switch_timer));
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_release_timer(p_dm_odm, &p_dm_odm->fast_ant_training_timer);
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_release_timer(p_dm_odm, &p_dm_odm->evm_fast_ant_training_timer);
#endif
	}

}

void
phydm_antdiv_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	/*struct _FAST_ANTENNA_TRAINNING_*			p_dm_fat_table = &p_dm_odm->dm_fat_table;*/
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm_value[0] == 1) { /*fixed or auto antenna*/

		if (dm_value[1] == 0) {
			p_dm_odm->antdiv_select = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "AntDiv: Auto\n"));
		} else if (dm_value[1] == 1) {
			p_dm_odm->antdiv_select = 1;
			PHYDM_SNPRINTF((output + used, out_len - used, "AntDiv: Fix MAin\n"));
		} else if (dm_value[1] == 2) {
			p_dm_odm->antdiv_select = 2;
			PHYDM_SNPRINTF((output + used, out_len - used, "AntDiv: Fix Aux\n"));
		}
	} else if (dm_value[0] == 2) { /*dynamic period for AntDiv*/

		p_dm_odm->antdiv_period = (u8)dm_value[1];
		PHYDM_SNPRINTF((output + used, out_len - used, "AntDiv_period = ((%d))\n", p_dm_odm->antdiv_period));
	}
}

#endif /*#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))*/

void
odm_ant_div_reset(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->ant_div_type == S0S1_SW_ANTDIV) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_s0s1_sw_ant_div_reset(p_dm_odm);
#endif
	}

}

void
odm_antenna_diversity_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

#if 0
	if (p_dm_odm->mp_mode == true)
		return;
#endif

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_config(p_dm_odm);
	odm_ant_div_init(p_dm_odm);
#endif
}

void
odm_antenna_diversity(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (p_dm_odm->mp_mode == true)
		return;

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div(p_dm_odm);
#endif
}
