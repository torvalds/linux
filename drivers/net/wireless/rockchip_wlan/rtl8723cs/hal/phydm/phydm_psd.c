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

//============================================================
// include files
//============================================================
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (CONFIG_PSD_TOOL == 1)

u32
phydm_get_psd_data(
	void			*p_dm_void,
	u32			psd_tone_idx,
	u32			igi
	)
{
	struct	PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);
	u32		psd_report = 0;
	
	odm_set_bb_reg(p_dm_odm, p_dm_psd_table->psd_reg, 0x3ff, psd_tone_idx);
	
	odm_set_bb_reg(p_dm_odm, p_dm_psd_table->psd_reg, BIT(22), 1); /*PSD trigger start*/
	ODM_delay_us(10);
	odm_set_bb_reg(p_dm_odm, p_dm_psd_table->psd_reg, BIT(22), 0); /*PSD trigger stop*/

	psd_report = odm_get_bb_reg(p_dm_odm, p_dm_psd_table->psd_report_reg, 0xffff);
	psd_report = odm_convert_to_db(psd_report) + igi;

	return psd_report;
}

u8
phydm_psd_stop_trx(
	void		*p_dm_void
	)
{
	struct	PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);
	u32		i;
	u8		trx_idle_success = FALSE;
	u32		dbg_port_value = 0;

	/*[Stop TRX]---------------------------------------------------------------------*/
	if (phydm_set_bb_dbg_port(p_dm_odm, BB_DBGPORT_PRIORITY_3, 0x0) == FALSE) /*set debug port to 0x0*/
		return STOP_TRX_FAIL;
	
	for (i = 0; i<10000; i++) {
		dbg_port_value = phydm_get_bb_dbg_port_value(p_dm_odm);
		if ((dbg_port_value & (BIT(17) | BIT(3))) == 0)	/* PHYTXON && CCA_all */ {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("PSD wait for ((%d)) times\n", i));
			
			trx_idle_success = TRUE;
			break;
		}
	}
	
	if (trx_idle_success) {
		
		odm_set_bb_reg(p_dm_odm, 0x520, 0xff0000, 0xff); /*pause all TX queue*/
		
		if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(p_dm_odm, 0x808, BIT(28), 0); /*disable CCK block*/
			odm_set_bb_reg(p_dm_odm, 0x838, BIT(1), 1); /*disable OFDM RX CCA*/
		} else {
			/*TBD*/
			odm_set_bb_reg(p_dm_odm, 0x800, BIT(24), 0); /* disable whole CCK block */
			odm_set_bb_reg(p_dm_odm, 0xC14, MASKDWORD, 0x0); /* [ Set IQK Matrix = 0 ] equivalent to [ Turn off CCA] */
		}
			
	} else {
		return STOP_TRX_FAIL;
	}

	phydm_release_bb_dbg_port(p_dm_odm);
	
	return STOP_TRX_SUCCESS;
	
}

u8		psd_result_cali_tone_8821[7]= {21, 28, 33, 93, 98, 105, 127};
u8		psd_result_cali_val_8821[7] = {67,69,71,72,71,69,67};	

void
phydm_psd(
	void		*p_dm_void,
	u32		igi,
	u16		start_point,
	u16		stop_point
	)
{
	struct	PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);
	u32		i = 0, mod_tone_idx;
	u32		t = 0;
	u16		fft_max_half_bw;
	u32		psd_igi_a_reg;
	u32		psd_igi_b_reg;
	u16		psd_fc_channel = p_dm_psd_table->psd_fc_channel;
	u8		ag_rf_mode_reg = 0;
	u8		rf_reg18_9_8 = 0;
	u32		psd_result_tmp = 0;
	u8		psd_result = 0;
	u8		psd_result_cali_tone[7] = {0};
	u8		psd_result_cali_val[7] = {0};
	u8		noise_table_idx = 0;
	u8		psd_result_cali_tmp = 0;

	if (p_dm_odm->support_ic_type == ODM_RTL8821) {
		odm_move_memory(p_dm_odm, psd_result_cali_tone, psd_result_cali_tone_8821, 7);
		odm_move_memory(p_dm_odm, psd_result_cali_val, psd_result_cali_val_8821, 7);
	}
	
	p_dm_psd_table->psd_in_progress = 1;

	/*[Stop DIG]*/
	p_dm_odm->support_ability &= ~(ODM_BB_DIG);
	p_dm_odm->support_ability &= ~(ODM_BB_FA_CNT);



	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("PSD Start =>\n"));

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		psd_igi_a_reg = 0xc50;
		psd_igi_b_reg = 0xe50;
	} else {
		psd_igi_a_reg = 0xc50;
		psd_igi_b_reg = 0xc58;
	}
	
	/*[back up IGI]*/
	p_dm_psd_table->initial_gain_backup = odm_get_bb_reg(p_dm_odm, psd_igi_a_reg, 0xff); 
	odm_set_bb_reg(p_dm_odm, psd_igi_a_reg, 0xff, 0x6e); /*IGI target at 0dBm & make it can't CCA*/
	odm_set_bb_reg(p_dm_odm, psd_igi_b_reg, 0xff, 0x6e); /*IGI target at 0dBm & make it can't CCA*/
	ODM_delay_us(10);
	
	if (phydm_psd_stop_trx(p_dm_odm) == STOP_TRX_FAIL) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("STOP_TRX_FAIL\n"));
		return;
	}

	/*[Set IGI]*/
	odm_set_bb_reg(p_dm_odm, psd_igi_a_reg, 0xff, igi);
	odm_set_bb_reg(p_dm_odm, psd_igi_b_reg, 0xff, igi);
	
	/*[Backup RF Reg]*/
	p_dm_psd_table->rf_0x18_bkp = odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, RFREGOFFSETMASK);

	if (psd_fc_channel > 14) {
		
		rf_reg18_9_8 = 1;
		
		if (36 <= psd_fc_channel && psd_fc_channel <= 64) 
			ag_rf_mode_reg = 0x1;
		else if (100 <= psd_fc_channel && psd_fc_channel <= 140) 
			ag_rf_mode_reg = 0x3; 
		else if (140 < psd_fc_channel) 
			ag_rf_mode_reg = 0x5; 
	}

	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, 0xff, psd_fc_channel);     /* Set RF fc*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, 0x300, rf_reg18_9_8);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, 0xc00, p_dm_psd_table->psd_bw_rf_reg);     /*2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, 0xf0000, ag_rf_mode_reg);     /* Set RF ag fc mode*/

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("0xc50=((0x%x))\n", odm_get_bb_reg(p_dm_odm, 0xc50, MASKDWORD)));
	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("RF0x0=((0x%x))\n", odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x0, RFREGOFFSETMASK)));*/
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("RF0x18=((0x%x))\n", odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, RFREGOFFSETMASK)));
	
	/*[Stop 3-wires]*/
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm_odm, 0xc00, 0xf, 0x4);/*	hardware 3-wire off */
		odm_set_bb_reg(p_dm_odm, 0xe00, 0xf, 0x4);/*	hardware 3-wire off */
	} else {
		odm_set_bb_reg(p_dm_odm, 0x88c, 0xf00000, 0xf);	/* 3 wire Disable    88c[23:20]=0xf */
	}
	ODM_delay_us(10);

	if (stop_point > (p_dm_psd_table->fft_smp_point-1))
		stop_point = (p_dm_psd_table->fft_smp_point-1);	

	if (start_point > (p_dm_psd_table->fft_smp_point-1))
		start_point = (p_dm_psd_table->fft_smp_point-1);

	if (start_point > stop_point)
		stop_point = start_point;


	for (i = start_point; i <= stop_point; i++ ) {

		fft_max_half_bw = (p_dm_psd_table->fft_smp_point)>>1;

		if (i < fft_max_half_bw) {
			mod_tone_idx = i + fft_max_half_bw;
		} else {
			mod_tone_idx = i - fft_max_half_bw;
		}
		
		psd_result_tmp = 0;
		for (t = 0; t < p_dm_psd_table->sw_avg_time; t++) {
			psd_result_tmp += phydm_get_psd_data(p_dm_odm, mod_tone_idx, igi);
			/**/
		}
		psd_result = (u8)((psd_result_tmp/p_dm_psd_table->sw_avg_time)) - p_dm_psd_table->psd_pwr_common_offset;

		if( p_dm_psd_table->fft_smp_point == 128) {

			if (p_dm_psd_table->noise_k_en) {
				if (i > psd_result_cali_tone[noise_table_idx]) {
					noise_table_idx ++;
				}

				if (noise_table_idx > 6)
					noise_table_idx = 6;

				if (psd_result >= psd_result_cali_val[noise_table_idx])
					psd_result = psd_result - psd_result_cali_val[noise_table_idx];
				else
					psd_result = 0;
			}

			p_dm_psd_table->psd_result[i] = psd_result;
		}
		
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("[%d] N_cali = %d, PSD = %d\n", mod_tone_idx, psd_result_cali_val[noise_table_idx],  psd_result));

	}

	/*[Start 3-wires]*/
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm_odm, 0xc00, 0xf, 0x7);/*	hardware 3-wire on */
		odm_set_bb_reg(p_dm_odm, 0xe00, 0xf, 0x7);/*	hardware 3-wire on */
	} else {
		odm_set_bb_reg(p_dm_odm, 0x88c, 0xf00000, 0x0);	/* 3 wire enable    88c[23:20]=0x0 */
	}
	ODM_delay_us(10);

	/*[Revert Reg]*/
	odm_set_bb_reg(p_dm_odm, 0x520, 0xff0000, 0x0); /*start all TX queue*/
	odm_set_bb_reg(p_dm_odm, 0x808, BIT(28), 1); /*enable CCK block*/
	odm_set_bb_reg(p_dm_odm, 0x838, BIT(1), 0); /*enable OFDM RX CCA*/
	
	odm_set_bb_reg(p_dm_odm, psd_igi_a_reg, 0xff, p_dm_psd_table->initial_gain_backup);
	odm_set_bb_reg(p_dm_odm, psd_igi_b_reg, 0xff, p_dm_psd_table->initial_gain_backup);
	
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x18, RFREGOFFSETMASK, p_dm_psd_table->rf_0x18_bkp);
	
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("PSD finished\n\n"));
	
	p_dm_odm->support_ability |= ODM_BB_DIG;
	p_dm_odm->support_ability |= ODM_BB_FA_CNT;
	p_dm_psd_table->psd_in_progress = 0;
	

}

void
phydm_psd_para_setting(
	void		*p_dm_void,
	u8		sw_avg_time,
	u8		hw_avg_time,	
	u8		i_q_setting,
	u16		fft_smp_point,
	u8		ant_sel,
	u8		psd_input,
	u8		channel,
	u8		noise_k_en
	)
{
	struct	PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);
	u32		avg_temp;
	u8		fft_smp_point_idx = 0;

	p_dm_psd_table->fft_smp_point = fft_smp_point;

	if (sw_avg_time == 0)
		sw_avg_time = 1;
	
	p_dm_psd_table->sw_avg_time = sw_avg_time;
	p_dm_psd_table->psd_fc_channel = channel;
	p_dm_psd_table->noise_k_en = noise_k_en;
		
	if (fft_smp_point == 128)
		fft_smp_point_idx = 0;
	else if (fft_smp_point == 256)
		fft_smp_point_idx = 1;
	else if (fft_smp_point == 512)
		fft_smp_point_idx = 2;
	else if (fft_smp_point == 1024)
		fft_smp_point_idx = 3;
		
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		
		odm_set_bb_reg(p_dm_odm, 0x910, BIT(11) | BIT(10), i_q_setting);
		odm_set_bb_reg(p_dm_odm, 0x910, BIT(13) | BIT(12), hw_avg_time);
		odm_set_bb_reg(p_dm_odm, 0x910, BIT(15) | BIT(14), fft_smp_point_idx);
		odm_set_bb_reg(p_dm_odm, 0x910, BIT(17) | BIT(16), ant_sel);
		odm_set_bb_reg(p_dm_odm, 0x910, BIT(23), psd_input);

	} else {

	}

	/*bw = (*p_dm_odm->p_band_width); //ODM_BW20M */
	/*channel = *(p_dm_odm->p_channel);*/
	
	


}

void
phydm_psd_init(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("PSD para init\n"));

	p_dm_psd_table->psd_in_progress = FALSE;
	
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		
		p_dm_psd_table->psd_reg = 0x910;
		p_dm_psd_table->psd_report_reg = 0xF44;

		if (ODM_IC_11AC_2_SERIES)
			p_dm_psd_table->psd_bw_rf_reg = 1;	/*2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		else
			p_dm_psd_table->psd_bw_rf_reg = 2;	/*2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		
	} else {
	
		p_dm_psd_table->psd_reg = 0x808;
		p_dm_psd_table->psd_report_reg = 0x8B4;
		p_dm_psd_table->psd_bw_rf_reg = 2; /*2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
	}

	if (p_dm_odm->support_ic_type == ODM_RTL8812)
		p_dm_psd_table->psd_pwr_common_offset = 0;
	else if (p_dm_odm->support_ic_type == ODM_RTL8821)
		p_dm_psd_table->psd_pwr_common_offset = 0;
	else
		p_dm_psd_table->psd_pwr_common_offset = 0;
	
	phydm_psd_para_setting(p_dm_odm, 1, 2, 3, 128, 0, 0, 7, 0);
	/*phydm_psd(p_dm_odm, 0x3c, 0, 127);*/			/* target at -50dBm */


}

void
phydm_psd_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u8		i;

	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "{0} {sw_avg} {hw_avg 0:3} {1:I,2:Q,3:IQ} {fft_point: 128*(1:4)} {path_sel 0~3} {0:ADC, 1:RXIQC} {CH} {noise_k}\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "{1} {IGI(hex)} {start_point} {stop_point}\n"));

	} else {
	

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 0) {

			for (i = 1; i < 10; i++) {
				if (input[i + 1]) {
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				}
			}
			
			PHYDM_SNPRINTF((output + used, out_len - used, "sw_avg_time=((%d)), hw_avg_time=((%d)), IQ=((%d)), fft=((%d)), path=((%d)), input =((%d)) ch=((%d)), noise_k=((%d))\n", 
				var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], (u8)var1[7], (u8)var1[8]));
			phydm_psd_para_setting(p_dm_odm, (u8)var1[1], (u8)var1[2], (u8)var1[3], (u16)var1[4], (u8)var1[5], (u8)var1[6], (u8)var1[7], (u8)var1[8]);
			
		} else if (var1[0] == 1) {

			PHYDM_SSCANF(input[2], DCMD_HEX, &var1[1]);
			PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
			PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var1[3]);
			PHYDM_SNPRINTF((output + used, out_len - used, "IGI=((0x%x)), start_point=((%d)), stop_point=((%d))\n", var1[1], var1[2], var1[3]));
			p_dm_odm->debug_components |= ODM_COMP_API;
			phydm_psd(p_dm_odm, var1[1], (u16)var1[2], (u16)var1[3]);
			p_dm_odm->debug_components &= (~ODM_COMP_API);
		}

	}


	
}

u8
phydm_get_psd_result_table(
	void		*p_dm_void,
	int 		index
	)
{
	struct	PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	_PHYDM_PSD_	*p_dm_psd_table = &(p_dm_odm->dm_psd_table);
	u8 		temp_result = 0;

	if(index<128)
		temp_result = p_dm_psd_table->psd_result[index];

	return temp_result;
	
}

#endif

