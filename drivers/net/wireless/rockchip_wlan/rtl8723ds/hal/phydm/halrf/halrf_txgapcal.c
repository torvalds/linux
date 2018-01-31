/* SPDX-License-Identifier: GPL-2.0 */
#include "mp_precomp.h"
#include "phydm_precomp.h"



void odm_bub_sort(pu4Byte data, u4Byte n)
{
	int i, j, temp, sp;
	
	for (i = n - 1;i >= 0;i--) {
		sp = 1;
		for (j = 0;j < i;j++) {
			if (data[j] < data[j + 1]) {
				temp = data[j];
				data[j] = data[j + 1];
				data[j + 1] = temp;
				sp = 0;
			}
		}
		if (sp == 1)
			break;          
	}
}


#if (RTL8197F_SUPPORT == 1)

u4Byte
odm_tx_gain_gap_psd_8197f(
	void	*p_dm_void,
	u1Byte	rf_path,
	u4Byte	rf56
)
{
	PDM_ODM_T	p_dm_odm = (PDM_ODM_T)p_dm_void;
	
	u1Byte i, j;
	u4Byte psd_vaule[5], psd_avg_time = 5, psd_vaule_temp;
	
	u4Byte iqk_ctl_addr[2][6] = {{0xe30, 0xe34, 0xe50, 0xe54, 0xe38, 0xe3c},
								{0xe50, 0xe54, 0xe30, 0xe34, 0xe58, 0xe5c}};
	
	u4Byte psd_finish_bit[2] = {0x04000000, 0x20000000};
	u4Byte psd_fail_bit[2] = {0x08000000, 0x40000000};
	
	u4Byte psd_cntl_value[2][2] = {{0x38008c1c, 0x10008c1c},
								   {0x38008c2c, 0x10008c2c}};
	
	u4Byte psd_report_addr[2] = {0xea0, 0xec0};
	
	odm_set_rf_reg(p_dm_odm, rf_path, 0xdf, bRFRegOffsetMask, 0x00e02);

	ODM_delay_us(100);

	odm_set_bb_reg(p_dm_odm, 0xe28, 0xffffffff, 0x0);
	
	odm_set_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff, rf56);
	while(rf56 != (odm_get_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff)))
		odm_set_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff, rf56);

	odm_set_bb_reg(p_dm_odm, 0xd94, 0xffffffff, 0x44FFBB44);
	odm_set_bb_reg(p_dm_odm, 0xe70, 0xffffffff, 0x00400040);
	odm_set_bb_reg(p_dm_odm, 0xc04, 0xffffffff, 0x6f005403);
	odm_set_bb_reg(p_dm_odm, 0xc08, 0xffffffff, 0x000804e4);
	odm_set_bb_reg(p_dm_odm, 0x874, 0xffffffff, 0x04203400);
	odm_set_bb_reg(p_dm_odm, 0xe28, 0xffffffff, 0x80800000);

	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][0], 0xffffffff, psd_cntl_value[rf_path][0]);
	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][1], 0xffffffff, psd_cntl_value[rf_path][1]);
	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][2], 0xffffffff, psd_cntl_value[rf_path][0]);
	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][3], 0xffffffff, psd_cntl_value[rf_path][0]);
	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][4], 0xffffffff, 0x8215001F);
	odm_set_bb_reg(p_dm_odm, iqk_ctl_addr[rf_path][5], 0xffffffff, 0x2805001F);
	
	odm_set_bb_reg(p_dm_odm, 0xe40, 0xffffffff, 0x81007C00);
	odm_set_bb_reg(p_dm_odm, 0xe44, 0xffffffff, 0x81004800);
	odm_set_bb_reg(p_dm_odm, 0xe4c, 0xffffffff, 0x0046a8d0);
	

	for (i = 0; i < psd_avg_time; i++) {
			
		for(j = 0; j < 1000 ; j++) {
			odm_set_bb_reg(p_dm_odm, 0xe48, 0xffffffff, 0xfa005800);
			odm_set_bb_reg(p_dm_odm, 0xe48, 0xffffffff, 0xf8005800);

			while(!odm_get_bb_reg(p_dm_odm, 0xeac, psd_finish_bit[rf_path]));	/*wait finish bit*/

			if (!odm_get_bb_reg(p_dm_odm, 0xeac, psd_fail_bit[rf_path])) {	/*check fail bit*/
				
				psd_vaule[i] = odm_get_bb_reg(p_dm_odm, psd_report_addr[rf_path], 0xffffffff);
				
				if (psd_vaule[i] > 0xffff)
					break;
			}
		}
			
		

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
			("[TGGC] rf0=0x%x rf56=0x%x rf56_reg=0x%x time=%d psd_vaule=0x%x\n",
			odm_get_rf_reg(p_dm_odm, rf_path, 0x0, 0xff),
			rf56, odm_get_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff), j, psd_vaule[i]));
	}

	odm_bub_sort(psd_vaule, psd_avg_time);

	psd_vaule_temp = psd_vaule[(UINT)(psd_avg_time / 2)];

	odm_set_bb_reg(p_dm_odm, 0xd94, 0xffffffff, 0x44BBBB44);
	odm_set_bb_reg(p_dm_odm, 0xe70, 0xffffffff, 0x80408040);
	odm_set_bb_reg(p_dm_odm, 0xc04, 0xffffffff, 0x6f005433);
	odm_set_bb_reg(p_dm_odm, 0xc08, 0xffffffff, 0x000004e4);
	odm_set_bb_reg(p_dm_odm, 0x874, 0xffffffff, 0x04003400);
	odm_set_bb_reg(p_dm_odm, 0xe28, 0xffffffff, 0x00000000);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
		("[TGGC] rf0=0x%x rf56=0x%x rf56_reg=0x%x psd_vaule_temp=0x%x\n",
		odm_get_rf_reg(p_dm_odm, rf_path, 0x0, 0xff),
		rf56, odm_get_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff), psd_vaule_temp));
	
	odm_set_rf_reg(p_dm_odm, rf_path, 0xdf, bRFRegOffsetMask, 0x00602);

	return psd_vaule_temp;

}



void
odm_tx_gain_gap_calibration_8197f(
	void	*p_dm_void
)
{
	PDM_ODM_T	p_dm_odm = (PDM_ODM_T)p_dm_void;

	u1Byte rf_path, rf0_idx, rf0_idx_current, rf0_idx_next, i, delta_gain_retry = 3;
	
	s1Byte delta_gain_gap_pre, delta_gain_gap[2][11];
	u4Byte rf56_current, rf56_next, psd_value_current, psd_value_next;
	u4Byte psd_gap, rf56_current_temp[2][11];
	s4Byte rf33[2][11];

	memset(rf33, 0x0, sizeof(rf33));

	for (rf_path = RF_PATH_A; rf_path <= RF_PATH_B; rf_path++) {

		if (rf_path == RF_PATH_A)
			odm_set_bb_reg(p_dm_odm, 0x88c, (BIT(21) | BIT(20)), 0x3);	/*disable 3-wire*/
		else if (rf_path == RF_PATH_B)
			odm_set_bb_reg(p_dm_odm, 0x88c, (BIT(23) | BIT(22)), 0x3);	/*disable 3-wire*/
		
		ODM_delay_us(100);

		for (rf0_idx = 1; rf0_idx <= 10; rf0_idx++) {
			
			rf0_idx_current = 3 * (rf0_idx - 1) + 1;
			odm_set_rf_reg(p_dm_odm, rf_path, 0x0, 0xff, rf0_idx_current);
			ODM_delay_us(100);
			rf56_current_temp[rf_path][rf0_idx] = odm_get_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff);
			rf56_current = rf56_current_temp[rf_path][rf0_idx];
			
			rf0_idx_next = 3 * rf0_idx + 1;
			odm_set_rf_reg(p_dm_odm, rf_path, 0x0, 0xff, rf0_idx_next);
			ODM_delay_us(100);
			rf56_next= odm_get_rf_reg(p_dm_odm, rf_path, 0x56, 0xfff);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
				("[TGGC] rf56_current[%d][%d]=0x%x rf56_next[%d][%d]=0x%x\n",
				rf_path, rf0_idx, rf56_current,  rf_path, rf0_idx, rf56_next));

			if ((rf56_current >> 5) == (rf56_next >> 5)) {
				delta_gain_gap[rf_path][rf0_idx] = 0;
				
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
					("[TGGC] rf56_current[11:5] == rf56_next[%d][%d][11:5]=0x%x delta_gain_gap[%d][%d]=%d\n",
					rf_path, rf0_idx, (rf56_next >> 5), rf_path, rf0_idx, delta_gain_gap[rf_path][rf0_idx]));
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
					("[TGGC] rf56_current[%d][%d][11:5]=0x%x != rf56_next[%d][%d][11:5]=0x%x\n",
					rf_path, rf0_idx, (rf56_current >> 5), rf_path, rf0_idx, (rf56_next >> 5)));

				
				for (i = 0; i < delta_gain_retry; i++) {
					psd_value_current = odm_tx_gain_gap_psd_8197f(p_dm_odm, rf_path, rf56_current);

					psd_value_next = odm_tx_gain_gap_psd_8197f(p_dm_odm, rf_path, rf56_next - 2);

					psd_gap = psd_value_next / (psd_value_current / 1000);

#if 0
					if (psd_gap > 1413)
						delta_gain_gap[rf_path][rf0_idx] = 1;
					else if (psd_gap > 1122)
						delta_gain_gap[rf_path][rf0_idx] = 0;
					else
						delta_gain_gap[rf_path][rf0_idx] = -1;
#endif

					if (psd_gap > 1445)
						delta_gain_gap[rf_path][rf0_idx] = 1;
					else if (psd_gap > 1096)
						delta_gain_gap[rf_path][rf0_idx] = 0;
					else
						delta_gain_gap[rf_path][rf0_idx] = -1;

					if (i == 0)
						delta_gain_gap_pre = delta_gain_gap[rf_path][rf0_idx];

					ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
						("[TGGC] psd_value_current=0x%x psd_value_next=0x%x psd_value_next/psd_value_current=%d delta_gain_gap[%d][%d]=%d\n",
						psd_value_current, psd_value_next, psd_gap, rf_path, rf0_idx, delta_gain_gap[rf_path][rf0_idx]));

					if ((i == 0) && (delta_gain_gap[rf_path][rf0_idx] == 0)) {
						break;
					}
					else {
						if (delta_gain_gap_pre != delta_gain_gap[rf_path][rf0_idx]) {
							delta_gain_gap[rf_path][rf0_idx] = 0;

							ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
								("[TGGC] delta_gain_gap_pre(%d) != delta_gain_gap[%d][%d](%d) time=%d\n",
								delta_gain_gap_pre, rf_path, rf0_idx, delta_gain_gap[rf_path][rf0_idx], i));

							break;
						} else {
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
								("[TGGC] delta_gain_gap_pre(%d) == delta_gain_gap[%d][%d](%d) time=%d\n",
								delta_gain_gap_pre, rf_path, rf0_idx, delta_gain_gap[rf_path][rf0_idx], i));
						}
					}
				}
				
			}

		}

		if (rf_path == RF_PATH_A)
			odm_set_bb_reg(p_dm_odm, 0x88c, (BIT(21) | BIT(20)), 0x0);	/*enable 3-wire*/
		else if (rf_path == RF_PATH_B)
			odm_set_bb_reg(p_dm_odm, 0x88c, (BIT(23) | BIT(22)), 0x0);	/*enable 3-wire*/
		
		ODM_delay_us(100);

	}
	
	/*odm_set_bb_reg(p_dm_odm, 0x88c, (BIT(23) | BIT(22) | BIT(21) | BIT(20)), 0x0);*/	/*enable 3-wire*/

	for (rf_path = RF_PATH_A; rf_path <= RF_PATH_B; rf_path++) {

		odm_set_rf_reg(p_dm_odm, rf_path, 0xef, bRFRegOffsetMask, 0x00100);
		
		for (rf0_idx = 1; rf0_idx <= 10; rf0_idx++) {
			
			rf33[rf_path][rf0_idx] = rf33[rf_path][rf0_idx] + (rf56_current_temp[rf_path][rf0_idx] & 0x1f); 
			
			for (i = rf0_idx; i <= 10; i++)
				rf33[rf_path][rf0_idx] = rf33[rf_path][rf0_idx] + delta_gain_gap[rf_path][i];

			if (rf33[rf_path][rf0_idx] >= 0x1d)
				rf33[rf_path][rf0_idx] = 0x1d;
			else if (rf33[rf_path][rf0_idx] <= 0x2)
				rf33[rf_path][rf0_idx] = 0x2;

			rf33[rf_path][rf0_idx] = rf33[rf_path][rf0_idx] + ((rf0_idx - 1) * 0x4000) + (rf56_current_temp[rf_path][rf0_idx] & 0xfffe0);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
				("[TGGC] rf56[%d][%d]=0x%05x rf33[%d][%d]=0x%05x\n", rf_path, rf0_idx, rf56_current_temp[rf_path][rf0_idx], rf_path, rf0_idx, rf33[rf_path][rf0_idx]));

			odm_set_rf_reg(p_dm_odm, rf_path, 0x33, bRFRegOffsetMask, rf33[rf_path][rf0_idx]);
		}
		
		odm_set_rf_reg(p_dm_odm, rf_path, 0xef, bRFRegOffsetMask, 0x00000);
	}

}
#endif


void
odm_tx_gain_gap_calibration(
	void	*p_dm_void
)
{
	PDM_ODM_T	p_dm_odm = (PDM_ODM_T)p_dm_void;

	#if (RTL8197F_SUPPORT == 1)
		if (p_dm_odm->SupportICType & ODM_RTL8197F)
			odm_tx_gain_gap_calibration_8197f(p_dm_void);
	#endif

}
