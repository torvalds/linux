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

void halrf_basic_profile(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* HAL RF version List */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% HAL RF version %"));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Power Tracking", HALRF_POWRTRACKING_VER));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "IQK", HALRF_IQK_VER));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "LCK", HALRF_LCK_VER));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "DPK", HALRF_DPK_VER));

	*_used = used;
	*_out_len = out_len;
#endif
}

#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
void
_iqk_page_switch(
		void			*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (p_dm->support_ic_type == ODM_RTL8821C)	
		odm_write_4byte(p_dm, 0x1b00, 0xf8000008);
	else	
		odm_write_4byte(p_dm, 0x1b00, 0xf800000a);
}

u32 halrf_psd_log2base(IN u32 val)
{
	u8	j;
	u32	tmp, tmp2, val_integerd_b = 0, tindex, shiftcount = 0;
	u32	result, val_fractiond_b = 0, table_fraction[21] = {0, 432, 332, 274, 232, 200,
				   174, 151, 132, 115, 100, 86, 74, 62, 51, 42,
							   32, 23, 15, 7, 0
							      };

	if (val == 0)
		return 0;

	tmp = val;

	while (1) {
		if (tmp == 1)
			break;

		tmp = (tmp >> 1);
		shiftcount++;
	}


	val_integerd_b = shiftcount + 1;

	tmp2 = 1;
	for (j = 1; j <= val_integerd_b; j++)
		tmp2 = tmp2 * 2;

	tmp = (val * 100) / tmp2;
	tindex = tmp / 5;

	if (tindex > 20)
		tindex = 20;

	val_fractiond_b = table_fraction[tindex];

	result = val_integerd_b * 100 - val_fractiond_b;
	
	return result;


}

void phydm_get_iqk_cfir(
	void *p_dm_void,
	u8 idx,
	u8 path,
	boolean debug
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	
	u8 i, ch;
	u32 tmp;

	if (debug)
		ch = 2;
	else
		ch = 0;

		odm_set_bb_reg(p_dm, 0x1b00, MASKDWORD, 0xf8000008 | path << 1);
		if (idx == 0)
			odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x3);
		else
			odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x1);
		odm_set_bb_reg(p_dm, 0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x10);
		for (i = 0; i < 8; i++) {
			odm_set_bb_reg(p_dm, 0x1bd8, MASKDWORD, 0xe0000001 + (i * 4));
			tmp = odm_get_bb_reg(p_dm, 0x1bfc, MASKDWORD);
			p_iqk_info->IQK_CFIR_real[ch][path][idx][i] = (tmp & 0x0fff0000) >> 16;
			p_iqk_info->IQK_CFIR_imag[ch][path][idx][i] = tmp & 0xfff;
		}
	odm_set_bb_reg(p_dm, 0x1bd8, MASKDWORD, 0x0);
	odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x0);
}

void
halrf_iqk_xym_enable(
	struct PHY_DM_STRUCT *p_dm,
	u8 xym_enable
	)
{
	struct _IQK_INFORMATION *p_iqk_info = &p_dm->IQK_info;

	if (xym_enable == 0)
		p_iqk_info->xym_read = false;
	else
		p_iqk_info->xym_read = true;

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %s\n", "xym_read = ", (p_iqk_info->xym_read ? "true": "false")));	
}

void
halrf_iqk_xym_read(
	void *p_dm_void,
	u8 path,
	u8 xym_type /*0: rx_sym; 1: tx_xym; 2:gs1_xym; 3:gs2_sym; 4: rxk1_xym*/
 )
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION *p_iqk_info = &p_dm->IQK_info;	
	u8 i, start, num;
	u32 tmp1, tmp2;

	if (!p_iqk_info->xym_read)
		return;

	if (*p_dm->p_band_width == 0) {
		start = 3;
		num = 4;
	}else if (*p_dm->p_band_width == 1) { 
		start = 2;
		num = 6;
	}else {
		start = 0;
  		num = 10;
 	}
	
	odm_write_4byte(p_dm, 0x1b00, 0xf8000008);
 	tmp1 =  odm_read_4byte(p_dm, 0x1b1c);
	odm_write_4byte(p_dm, 0x1b1c, 0xa2193c32);

 	odm_write_4byte(p_dm, 0x1b00, 0xf800000a);
 	tmp2 =  odm_read_4byte(p_dm, 0x1b1c);
	odm_write_4byte(p_dm, 0x1b1c, 0xa2193c32);

	for (path = 0; path < 2; path ++) {
		odm_write_4byte(p_dm, 0x1b00, 0xf8000008 | path << 1);
		switch(xym_type){
 			case 0:
				for (i = 0; i < num ;i++) {
	   				odm_write_4byte(p_dm, 0x1b14, 0xe6+start+i);
	   				odm_write_4byte(p_dm, 0x1b14, 0x0);
	   				p_iqk_info->rx_xym[path][i] = odm_read_4byte(p_dm, 0x1b38);
				}
			break;
			case 1:		
				for (i = 0; i < num ;i++) {
	   				odm_write_4byte(p_dm, 0x1b14, 0xe6+start+i);
	   				odm_write_4byte(p_dm, 0x1b14, 0x0);
	   				p_iqk_info->tx_xym[path][i] = odm_read_4byte(p_dm, 0x1b38);
				}
			break;
			case 2:		
				for (i = 0; i < 6 ;i++) {
	   				odm_write_4byte(p_dm, 0x1b14, 0xe0+i);
	   				odm_write_4byte(p_dm, 0x1b14, 0x0);
	   				p_iqk_info->gs1_xym[path][i] = odm_read_4byte(p_dm, 0x1b38);
				}
			break;
			case 3:		
				for (i = 0; i < 6 ;i++) {
	   				odm_write_4byte(p_dm, 0x1b14, 0xe0+i);
	   				odm_write_4byte(p_dm, 0x1b14, 0x0);
	   				p_iqk_info->gs2_xym[path][i] = odm_read_4byte(p_dm, 0x1b38);
	  		}
			break;			
			case 4:		
				for (i = 0; i < 6 ;i++) {
	   				odm_write_4byte(p_dm, 0x1b14, 0xe0+i);
	   				odm_write_4byte(p_dm, 0x1b14, 0x0);
	   				p_iqk_info->rxk1_xym[path][i] = odm_read_4byte(p_dm, 0x1b38);
	  		}
			break;

		}
		odm_write_4byte(p_dm, 0x1b38, 0x20000000);
		odm_write_4byte(p_dm, 0x1b00, 0xf8000008);
		odm_write_4byte(p_dm, 0x1b1c, tmp1);
		odm_write_4byte(p_dm, 0x1b00, 0xf800000a);
		odm_write_4byte(p_dm, 0x1b1c, tmp2);
		_iqk_page_switch(p_dm);
	}
}

void halrf_iqk_xym_show(
	struct PHY_DM_STRUCT *p_dm,
	u8 xym_type /*0: rx_sym; 1: tx_xym; 2:gs1_xym; 3:gs2_sym; 4: rxk1_xym*/
 )
{
	u8 num, path, path_num, i;		
	struct _IQK_INFORMATION *p_iqk_info = &p_dm->IQK_info;	

	if (p_dm->rf_type ==RF_1T1R)
		path_num = 0x1;
	else if (p_dm->rf_type ==RF_2T2R)
		path_num = 0x2;
	else
		path_num = 0x4;

	if (*p_dm->p_band_width == CHANNEL_WIDTH_20)
		num = 4;
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_40)
		num = 6;
	else
		num = 10;
		
	for (path = 0; path < path_num; path ++) {
		switch (xym_type){
		case 0:
			for (i = 0 ; i < num; i ++)
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %-2d: 0x%x\n",
					(path == 0) ? "PATH A RX-XYM ": "PATH B RX-XYM", i, p_iqk_info->rx_xym[path][i]));
			break;
		case 1:
			for (i = 0 ; i < num; i ++)
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %-2d: 0x%x\n",
					(path == 0) ? "PATH A TX-XYM ": "PATH B TX-XYM", i, p_iqk_info->tx_xym[path][i]));
			break;
		case 2:
			for (i = 0 ; i < 6; i ++)
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %-2d: 0x%x\n",
					(path == 0) ? "PATH A GS1-XYM ": "PATH B GS1-XYM", i, p_iqk_info->gs1_xym[path][i]));
			break;
		case 3:
			for (i = 0 ; i < 6; i ++)
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %-2d: 0x%x\n",
					(path == 0) ? "PATH A GS2-XYM ": "PATH B GS2-XYM", i, p_iqk_info->gs2_xym[path][i]));
			break;
		case 4:			
			for (i = 0 ; i < 6; i ++)
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s %-2d: 0x%x\n",
					(path == 0) ? "PATH A RXK1-XYM ": "PATH B RXK1-XYM", i, p_iqk_info->rxk1_xym[path][i]));
			break;
		}
	}
}


void
halrf_iqk_xym_dump(
	void *p_dm_void
 )
{
	u32 tmp1, tmp2;
 	struct PHY_DM_STRUCT	 *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_write_4byte(p_dm, 0x1b00, 0xf8000008);
 	tmp1 =  odm_read_4byte(p_dm, 0x1b1c);
 	odm_write_4byte(p_dm, 0x1b00, 0xf800000a);
 	tmp2 =  odm_read_4byte(p_dm, 0x1b1c);
 	/*halrf_iqk_xym_read(p_dm, xym_type);*/
 	odm_write_4byte(p_dm, 0x1b00, 0xf8000008);
 	odm_write_4byte(p_dm, 0x1b1c, tmp1);
 	odm_write_4byte(p_dm, 0x1b00, 0xf800000a);
 	odm_write_4byte(p_dm, 0x1b1c, tmp2);
 	_iqk_page_switch(p_dm);
}

void halrf_iqk_info_dump(
	void *p_dm_void,
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;	
	u8 path, num, i;

	u8 rf_path, j, reload_iqk = 0;
	u32 tmp;
	boolean iqk_result[2][NUM][2];	/*two channel, PATH, TX/RX, 0:pass 1 :fail*/
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;

	/* IQK INFO */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s\n", "% IQK Info %"));
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s\n",
		(p_dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ? "FW-IQK" : "Driver-IQK"));	

	reload_iqk = (u8)odm_get_bb_reg(p_dm, 0x1bf0, BIT(16));
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
		"reload", (reload_iqk) ? "True" : "False"));

	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
			"rfk_forbidden", (p_iqk_info->rfk_forbidden) ? "True" : "False"));
#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
			"segment_iqk", (p_iqk_info->segment_iqk) ? "True" : "False"));
#endif

	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s:%d %d\n",
			"iqk count / fail count", p_dm->n_iqk_cnt, p_dm->n_iqk_fail_cnt));

	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %d\n",
			"channel", *p_dm->p_channel));

	if (*p_dm->p_band_width == CHANNEL_WIDTH_20)
		PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"bandwidth", "BW_20"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_40)
		PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"bandwidth", "BW_40"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_80)
		PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"bandwidth", "BW_80"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_160)
		PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"bandwidth", "BW_160"));
	else
		PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"bandwidth", "BW_UNKNOW"));

	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %llu %s\n",
				"progressing_time", p_dm->rf_calibrate_info.iqk_total_progressing_time, "(ms)"));
		
	tmp = odm_read_4byte(p_dm, 0x1bf0);
	for(rf_path = RF_PATH_A; rf_path <= RF_PATH_B; rf_path++)
		for(j = 0; j < 2; j++)
			iqk_result[0][rf_path][j] = (boolean)(tmp & BIT(rf_path + (j * 4)) >> (rf_path + (j * 4)));

	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: 0x%08x\n","Reg0x1bf0", tmp));
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"PATH_A-Tx result", (iqk_result[0][RF_PATH_A][0]) ?  "Fail" : "Pass"));
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"PATH_A-Rx result", (iqk_result[0][RF_PATH_A][1]) ?  "Fail" : "Pass"));
#if (RTL8822B_SUPPORT == 1) 
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"PATH_B-Tx result", (iqk_result[0][RF_PATH_B][0]) ?  "Fail" : "Pass"));
	PHYDM_SNPRINTF((output + used, out_len - used, "%-20s: %s\n",
				"PATH_B-Rx result", (iqk_result[0][RF_PATH_B][1]) ?  "Fail" : "Pass"));
#endif
	*_used = used;
	*_out_len = out_len;

}

void halrf_get_fw_version(void	*p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{
		struct _ADAPTER		*adapter = p_dm->adapter;

		p_rf->fw_ver = (adapter->MgntInfo.FirmwareVersion << 16) | adapter->MgntInfo.FirmwareSubVersion;
	}
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	{
		struct rtl8192cd_priv *priv = p_dm->priv;

		p_rf->fw_ver = (priv->pshare->fw_version << 16) | priv->pshare->fw_sub_version;
	}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
	{
		struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;
		struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

		p_rf->fw_ver = (rtlhal->fw_version << 16) | rtlhal->fw_subversion;
	}
#else
	{
		struct _ADAPTER		*adapter = p_dm->adapter;
		HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);

		p_rf->fw_ver = (p_hal_data->firmware_version << 16) | p_hal_data->firmware_sub_version;
	}
#endif
}


void halrf_iqk_dbg(void	*p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8 rf_path, j, reload_iqk = 0;
	u8 path, num, i;
	u32 tmp;
	boolean iqk_result[2][NUM][2];	/*two channel, PATH, TX/RX, 0:pass 1 :fail*/
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

	/* IQK INFO */
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s\n", "====== IQK Info ======"));

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s\n",
		(p_dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ? "FW-IQK" : "Driver-IQK"));

	if (p_dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) {
		halrf_get_fw_version(p_dm);
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: 0x%x\n",
			"FW_VER", p_rf->fw_ver));
	} else
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
			"IQK_VER", HALRF_IQK_VER));

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION,( "%-20s: %s\n",
		"reload", (p_iqk_info->is_reload) ? "True" : "False"));

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %d %d\n",
			"iqk count / fail count", p_dm->n_iqk_cnt, p_dm->n_iqk_fail_cnt));

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %d\n",
			"channel", *p_dm->p_channel));

	if (*p_dm->p_band_width == CHANNEL_WIDTH_20)
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"bandwidth", "BW_20"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_40)
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"bandwidth", "BW_40"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_80)
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"bandwidth", "BW_80"));
	else if (*p_dm->p_band_width == CHANNEL_WIDTH_160)
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"bandwidth", "BW_160"));
	else
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"bandwidth", "BW_UNKNOW"));
/*
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %llu %s\n",
				"progressing_time", p_dm->rf_calibrate_info.iqk_total_progressing_time, "(ms)"));
*/
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"rfk_forbidden", (p_iqk_info->rfk_forbidden) ? "True" : "False"));
#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"segment_iqk", (p_iqk_info->segment_iqk) ? "True" : "False"));
#endif

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %llu %s\n",
				"progressing_time", p_dm->rf_calibrate_info.iqk_progressing_time, "(ms)"));

	


	tmp = odm_read_4byte(p_dm, 0x1bf0);
	for(rf_path = RF_PATH_A; rf_path <= RF_PATH_B; rf_path++)
		for(j = 0; j < 2; j++)
			iqk_result[0][rf_path][j] = (boolean)(tmp & BIT(rf_path + (j * 4)) >> (rf_path + (j * 4)));

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: 0x%08x\n", "Reg0x1bf0", tmp));
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: 0x%08x\n", "Reg0x1be8", odm_read_4byte(p_dm, 0x1be8)));
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"PATH_A-Tx result", (iqk_result[0][RF_PATH_A][0]) ?  "Fail" : "Pass"));
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"PATH_A-Rx result", (iqk_result[0][RF_PATH_A][1]) ?  "Fail" : "Pass"));
#if (RTL8822B_SUPPORT == 1) 
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",		
				"PATH_B-Tx result", (iqk_result[0][RF_PATH_B][0]) ?  "Fail" : "Pass"));
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %s\n",
				"PATH_B-Rx result", (iqk_result[0][RF_PATH_B][1]) ?  "Fail" : "Pass"));
#endif


}
void halrf_lck_dbg(struct PHY_DM_STRUCT *p_dm)
{
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s\n", "====== LCK Info ======"));
	/*PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, "%-20s\n",
		(p_dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ? "LCK" : "RTK"));*/
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("%-20s: %llu %s\n",
				"progressing_time", p_dm->rf_calibrate_info.lck_progressing_time, "(ms)"));
}

void
halrf_iqk_dbg_cfir_backup(struct PHY_DM_STRUCT *p_dm)
{
	struct _IQK_INFORMATION *p_iqk_info = &p_dm->IQK_info;
	u8	path, idx, i;

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "backup TX/RX CFIR"));	

	for (path = 0; path < 2; path ++) {
		for (idx = 0; idx < 2; idx++) {
			phydm_get_iqk_cfir(p_dm, idx, path, true);
		}
	}

	for (path = 0; path < 2; path ++) {
		for (idx = 0; idx < 2; idx++) {
			for(i = 0; i < 8; i++) {
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-7s %-3s CFIR_real: %-2d: 0x%x\n",
					(path == 0) ? "PATH A": "PATH B", (idx == 0) ? "TX": "RX", i, p_iqk_info->IQK_CFIR_real[2][path][idx][i]));
			}
			for(i = 0; i < 8; i++) {
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-7s %-3s CFIR_img:%-2d: 0x%x\n",
					(path == 0) ? "PATH A": "PATH B", (idx == 0) ? "TX": "RX", i, p_iqk_info->IQK_CFIR_imag[2][path][idx][i]));
			}
		}
	}
}


void
halrf_iqk_dbg_cfir_backup_update(
	struct PHY_DM_STRUCT			*p_dm
)
{
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	u8 i, path, idx;

	if(p_iqk_info->IQK_CFIR_real[2][0][0][0] == 0) {
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "CFIR is invalid"));
		return;
	}
	for (path = 0; path < 2; path++) {
		for (idx = 0; idx < 2; idx++) {
			odm_set_bb_reg(p_dm, 0x1b00, MASKDWORD, 0xf8000008 | path << 1);
			odm_set_bb_reg(p_dm, 0x1b2c, MASKDWORD, 0x7);
			odm_set_bb_reg(p_dm, 0x1b38, MASKDWORD, 0x20000000);
			odm_set_bb_reg(p_dm, 0x1b3c, MASKDWORD, 0x20000000);
			odm_set_bb_reg(p_dm, 0x1bcc, MASKDWORD, 0x00000000);
			if (idx == 0)
				odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x3);
			else
				odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x1);
			odm_set_bb_reg(p_dm, 0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x10);
			for (i = 0; i < 8; i++) {
				odm_write_4byte(p_dm, 0x1bd8,	((0xc0000000 >> idx) + 0x3) + (i * 4) + (p_iqk_info->IQK_CFIR_real[2][path][idx][i] << 9));
				odm_write_4byte(p_dm, 0x1bd8, ((0xc0000000 >> idx) + 0x1) + (i * 4) + (p_iqk_info->IQK_CFIR_imag[2][path][idx][i] << 9));
				/*odm_write_4byte(p_dm, 0x1bd8, p_iqk_info->IQK_CFIR_real[2][path][idx][i]);*/
				/*odm_write_4byte(p_dm, 0x1bd8, p_iqk_info->IQK_CFIR_imag[2][path][idx][i]);*/
			}
		}
		odm_set_bb_reg(p_dm, 0x1bd8, MASKDWORD, 0x0);
		odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x0);
	}
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "update new CFIR"));
}


void
halrf_iqk_dbg_cfir_reload(
	struct PHY_DM_STRUCT			*p_dm
)
{
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	u8 i, path, idx;

	if(p_iqk_info->IQK_CFIR_real[0][0][0][0] == 0) {
		PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "CFIR is invalid"));
		return;
	}
	for (path = 0; path < 2; path++) {
		for (idx = 0; idx < 2; idx++) {
			odm_set_bb_reg(p_dm, 0x1b00, MASKDWORD, 0xf8000008 | path << 1);
			odm_set_bb_reg(p_dm, 0x1b2c, MASKDWORD, 0x7);
			odm_set_bb_reg(p_dm, 0x1b38, MASKDWORD, 0x20000000);
			odm_set_bb_reg(p_dm, 0x1b3c, MASKDWORD, 0x20000000);
			odm_set_bb_reg(p_dm, 0x1bcc, MASKDWORD, 0x00000000);
			if (idx == 0)
				odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x3);
			else
				odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x1);
			odm_set_bb_reg(p_dm, 0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x10);
			for (i = 0; i < 8; i++) {
				/*odm_write_4byte(p_dm, 0x1bd8, p_iqk_info->IQK_CFIR_real[0][path][idx][i]);*/
				/*odm_write_4byte(p_dm, 0x1bd8, p_iqk_info->IQK_CFIR_imag[0][path][idx][i]);*/
				odm_write_4byte(p_dm, 0x1bd8,	((0xc0000000 >> idx) + 0x3) + (i * 4) + (p_iqk_info->IQK_CFIR_real[0][path][idx][i] << 9));
				odm_write_4byte(p_dm, 0x1bd8, ((0xc0000000 >> idx) + 0x1) + (i * 4) + (p_iqk_info->IQK_CFIR_imag[0][path][idx][i] << 9));
			}
		}
		odm_set_bb_reg(p_dm, 0x1bd8, MASKDWORD, 0x0);
		odm_set_bb_reg(p_dm, 0x1b0c, BIT(13) | BIT(12), 0x0);
	}
	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "write CFIR with default value"));
}

void
halrf_iqk_dbg_cfir_write(
	struct PHY_DM_STRUCT			*p_dm,
	u8	type,
	u32 path,
	u32 idx,
	u32 i,
	u32 data
)
{
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	if (type == 0)
		p_iqk_info->IQK_CFIR_real[2][path][idx][i] = data;
	else
		p_iqk_info->IQK_CFIR_imag[2][path][idx][i] = data;
}

void
halrf_iqk_dbg_cfir_backup_show(struct PHY_DM_STRUCT *p_dm)
{
	struct _IQK_INFORMATION *p_iqk_info = &p_dm->IQK_info;
	u8	path, idx, i;

	PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-20s\n", "backup TX/RX CFIR"));	

	for (path = 0; path < 2; path ++) {
		for (idx = 0; idx < 2; idx++) {
			for(i = 0; i < 8; i++) {
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-10s %-3s CFIR_real: %-2d: 0x%x\n",
					(path == 0) ? "PATH A": "PATH B", (idx == 0) ? "TX": "RX", i, p_iqk_info->IQK_CFIR_real[2][path][idx][i]));
			}
			for(i = 0; i < 8; i++) {
				PHYDM_DBG(p_dm, ODM_COMP_CALIBRATION, ("[IQK]%-10s %-3s CFIR_img:%-2d: 0x%x\n",
					(path == 0) ? "PATH A": "PATH B", (idx == 0) ? "TX": "RX", i, p_iqk_info->IQK_CFIR_imag[2][path][idx][i]));
			}
		}
	}
}

void
halrf_do_imr_test(
	void	*p_dm_void,
	u8  flag_imr_test
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (flag_imr_test != 0x0)
		switch (p_dm->support_ic_type) {
#if (RTL8822B_SUPPORT == 1)
		case ODM_RTL8822B:
			break;
#endif
#if (RTL8821C_SUPPORT == 1)
		case ODM_RTL8821C:
			do_imr_test_8821c(p_dm);
			break;
#endif
		default:
		break;
		}
}

void halrf_iqk_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	
	/*dm_value[0]=0x0: backup from SRAM & show*/
	/*dm_value[0]=0x1: write backup CFIR to SRAM*/
	/*dm_value[0]=0x2: reload default CFIR to SRAM*/
	/*dm_value[0]=0x3: show backup*/
	/*dm_value[0]=0x10: write backup CFIR real part*/
	/*--> dm_value[1]:path, dm_value[2]:tx/rx, dm_value[3]:index, dm_value[4]:data*/
	/*dm_value[0]=0x11: write backup CFIR imag*/
	/*--> dm_value[1]:path, dm_value[2]:tx/rx, dm_value[3]:index, dm_value[4]:data*/	
	/*dm_value[0]=0x20 :xym_read enable*/
	/*--> dm_value[1]:0:disable, 1:enable*/ 
	/*if dm_value[0]=0x20 = enable, */
	/*0x1:show rx_sym; 0x2: tx_xym; 0x3:gs1_xym; 0x4:gs2_sym; 0x5:rxk1_xym*/

	if (dm_value[0] == 0x0)
		halrf_iqk_dbg_cfir_backup(p_dm);
	else if (dm_value[0] == 0x1)
		halrf_iqk_dbg_cfir_backup_update(p_dm);
	else if (dm_value[0] == 0x2)
		halrf_iqk_dbg_cfir_reload(p_dm);
	else if (dm_value[0] == 0x3)
		halrf_iqk_dbg_cfir_backup_show(p_dm);
	else if (dm_value[0] == 0x10)
		halrf_iqk_dbg_cfir_write(p_dm, 0, dm_value[1], dm_value[2], dm_value[3], dm_value[4]);
	else if (dm_value[0] == 0x11)
		halrf_iqk_dbg_cfir_write(p_dm, 1, dm_value[1], dm_value[2], dm_value[3], dm_value[4]);
	else if (dm_value[0] == 0x20)
		halrf_iqk_xym_enable(p_dm, (u8)dm_value[1]);
	else if (dm_value[0] == 0x21)
		halrf_iqk_xym_show(p_dm,(u8)dm_value[1]);
	else if (dm_value[0] == 0x30)
		halrf_do_imr_test(p_dm, (u8)dm_value[1]);
}

void
halrf_iqk_hwtx_check(
	void *p_dm_void,
	boolean		is_check
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	u32 tmp_b04;

	if (is_check)
		p_iqk_info->is_hwtx = (boolean)odm_get_bb_reg(p_dm, 0xb00, BIT(8));
	else {
		if (p_iqk_info->is_hwtx) {
			tmp_b04 = odm_read_4byte(p_dm, 0xb04);
			odm_set_bb_reg(p_dm, 0xb04, BIT(3) | BIT (2), 0x0);
			odm_write_4byte(p_dm, 0xb04, tmp_b04);
		}
	}
}

void
halrf_segment_iqk_trigger(
	void			*p_dm_void,
	boolean		clear,
	boolean		segment_iqk
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION		*p_iqk_info = &p_dm->IQK_info;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	u64 start_time;
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(p_dm) == false)
		return;
#endif

	if ((p_dm->p_mp_mode != NULL) && (p_rf->p_is_con_tx != NULL) && (p_rf->p_is_single_tone != NULL) && (p_rf->p_is_carrier_suppresion != NULL))
		if (*(p_dm->p_mp_mode) && ((*(p_rf->p_is_con_tx) || *(p_rf->p_is_single_tone) || *(p_rf->p_is_carrier_suppresion))))
			return;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_rf->rf_supportability & HAL_RF_IQK))
		return;
#endif

#if DISABLE_BB_RF
	return;
#endif
	if (p_iqk_info->rfk_forbidden)
		return;

	if (!p_dm->rf_calibrate_info.is_iqk_in_progress) {
		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_iqk_in_progress = true;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
		start_time = odm_get_current_time(p_dm);
		p_dm->IQK_info.segment_iqk = segment_iqk;

		switch (p_dm->support_ic_type) {
#if (RTL8822B_SUPPORT == 1)
		case ODM_RTL8822B:
			phy_iq_calibrate_8822b(p_dm, clear, segment_iqk);
			break;
#endif
#if (RTL8821C_SUPPORT == 1)
		case ODM_RTL8821C:
			phy_iq_calibrate_8821c(p_dm, clear, segment_iqk);
			break;
#endif
#if (RTL8814B_SUPPORT == 1)
		case ODM_RTL8814B:
			break;
#endif
		default:
			break;
		}
		p_dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(p_dm, start_time);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK progressing_time = %lld ms\n", p_dm->rf_calibrate_info.iqk_progressing_time));

		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_iqk_in_progress = false;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
	} else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("== Return the IQK CMD, because RFKs in Progress ==\n"));
}



#endif


void
halrf_rf_lna_setting(
	void	*p_dm_void,
	enum phydm_lna_set type
)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_	 *p_rf = &(p_dm->rf_table);

		switch (p_dm->support_ic_type) {
#if (RTL8188E_SUPPORT == 1)
		case ODM_RTL8188E:
			halrf_rf_lna_setting_8188e(p_dm, type);
			break;
#endif
#if (RTL8192E_SUPPORT == 1)
		case ODM_RTL8192E:
			halrf_rf_lna_setting_8192e(p_dm, type);
			break;
#endif
#if (RTL8723B_SUPPORT == 1)
		case ODM_RTL8723B:
			halrf_rf_lna_setting_8723b(p_dm, type);
			break;
#endif
#if (RTL8812A_SUPPORT == 1)
		case ODM_RTL8812:
			halrf_rf_lna_setting_8812a(p_dm, type);
			break;
#endif
#if ((RTL8821A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1))
		case ODM_RTL8881A:
		case ODM_RTL8821:
			halrf_rf_lna_setting_8821a(p_dm, type);
			break;
#endif
#if (RTL8822B_SUPPORT == 1)
		case ODM_RTL8822B:
			halrf_rf_lna_setting_8822b(p_dm_void, type);
			break;
#endif
#if (RTL8821C_SUPPORT == 1)
		case ODM_RTL8821C:
			halrf_rf_lna_setting_8821c(p_dm_void, type);
			break;
#endif
		default:
			break;
		}

	}


void
halrf_support_ability_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	u32	dm_value[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8	i;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &dm_value[i]);
		}
	}
	
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "[RF Supportability]\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))Power Tracking\n", ((p_rf->rf_supportability & HAL_RF_TX_PWR_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))IQK\n", ((p_rf->rf_supportability & HAL_RF_IQK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))LCK\n", ((p_rf->rf_supportability & HAL_RF_LCK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))DPK\n", ((p_rf->rf_supportability & HAL_RF_DPK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "04. (( %s ))HAL_RF_TXGAPK\n", ((p_rf->rf_supportability & HAL_RF_TXGAPK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));		
	}
	else {

		if (dm_value[1] == 1) { /* enable */
			p_rf->rf_supportability |= BIT(dm_value[0]) ;
		} else if (dm_value[1] == 2) /* disable */
			p_rf->rf_supportability &= ~(BIT(dm_value[0])) ;
		else {
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
		}
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-RF_supportability =  0x%x\n", p_rf->rf_supportability));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	*_used = used;
	*_out_len = out_len;
}

void
halrf_cmn_info_init(
	void		*p_dm_void,
enum halrf_cmninfo_init_e	cmn_info,
	u32			value
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

	switch	(cmn_info) {
	case	HALRF_CMNINFO_EEPROM_THERMAL_VALUE:
		p_rf->eeprom_thermal = (u8)value;
		break;
	case	HALRF_CMNINFO_FW_VER:
		p_rf->fw_ver = (u32)value;
		break;
	default:
		break;
	}
}


void
halrf_cmn_info_hook(
	void		*p_dm_void,
enum halrf_cmninfo_hook_e cmn_info,
	void		*p_value
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	
	switch	(cmn_info) {
	case	HALRF_CMNINFO_CON_TX:
		p_rf->p_is_con_tx = (boolean *)p_value;
		break;
	case	HALRF_CMNINFO_SINGLE_TONE:
		p_rf->p_is_single_tone = (boolean *)p_value;		
		break;
	case	HALRF_CMNINFO_CARRIER_SUPPRESSION:
		p_rf->p_is_carrier_suppresion = (boolean *)p_value;		
		break;
	case	HALRF_CMNINFO_MP_RATE_INDEX:
		p_rf->p_mp_rate_index = (u8 *)p_value;
		break;
	default:
		/*do nothing*/
		break;
	}
}

void
halrf_cmn_info_set(
	void		*p_dm_void,
	u32			cmn_info,
	u64			value
)
{
	/*  */
	/* This init variable may be changed in run time. */
	/*  */
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	
	switch	(cmn_info) {

		case	HALRF_CMNINFO_ABILITY:
			p_rf->rf_supportability = (u32)value;
			break;

		case	HALRF_CMNINFO_DPK_EN:
			p_rf->dpk_en = (u8)value;
			break;
		case HALRF_CMNINFO_RFK_FORBIDDEN :
			p_dm->IQK_info.rfk_forbidden = (boolean)value;
			break;
		#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		case HALRF_CMNINFO_IQK_SEGMENT:
			p_dm->IQK_info.segment_iqk = (boolean)value;
			break;
		#endif
		default:
			/* do nothing */
			break;
	}
}

u64
halrf_cmn_info_get(
	void		*p_dm_void,
	u32			cmn_info
)
{
	/*  */
	/* This init variable may be changed in run time. */
	/*  */
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	u64	return_value = 0;
	
	switch	(cmn_info) {

		case	HALRF_CMNINFO_ABILITY:
			return_value = (u32)p_rf->rf_supportability;
			break;
		case HALRF_CMNINFO_RFK_FORBIDDEN :
			return_value = p_dm->IQK_info.rfk_forbidden;
			break;
		#if (RTL8814A_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		case HALRF_CMNINFO_IQK_SEGMENT:
			return_value = p_dm->IQK_info.segment_iqk;
			break;
		#endif
		default:
			/* do nothing */
			break;
	}

	return	return_value;
}

void
halrf_supportability_init_mp(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

	switch (p_dm->support_ic_type) {

	case ODM_RTL8814B:
		#if (RTL8814B_SUPPORT == 1) 
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			0;
		#endif
		break;
	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			0;
		break;
	#endif

	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			/*HAL_RF_TXGAPK			|*/
			0;
		break;
	#endif

	default:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			/*HAL_RF_TXGAPK			|*/
			0;
		break;

	}

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IC = ((0x%x)), RF_Supportability Init MP = ((0x%x))\n", p_dm->support_ic_type, p_rf->rf_supportability));
}

void
halrf_supportability_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

	switch (p_dm->support_ic_type) {

	case ODM_RTL8814B:
		#if (RTL8814B_SUPPORT == 1) 
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			0;
		#endif
		break;
	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			0;
		break;
	#endif

	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/		
			/*HAL_RF_TXGAPK				|*/
			0;
		break;
	#endif

	default:
		p_rf->rf_supportability = 
			HAL_RF_TX_PWR_TRACK	|
			HAL_RF_IQK				|
			HAL_RF_LCK				|
			/*HAL_RF_DPK				|*/
			0;
		break;

	}

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IC = ((0x%x)), RF_Supportability Init = ((0x%x))\n", p_dm->support_ic_type, p_rf->rf_supportability));
}

void
halrf_watchdog(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	phydm_rf_watchdog(p_dm);
}
#if 0
void
halrf_iqk_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);

	switch (p_dm->support_ic_type) {

	#if (RTL8814B_SUPPORT == 1) 
	case ODM_RTL8814B:
		break;
	#endif
	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		_iq_calibrate_8822b_init(p_dm);
		break;
	#endif
	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		break;
	#endif

	default:
		break;
	}
}
#endif


void
halrf_iqk_trigger(
	void			*p_dm_void,
	boolean		is_recovery
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION		*p_iqk_info = &p_dm->IQK_info;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	u64 start_time;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(p_dm) == false)
		return;
#endif

	if ((p_dm->p_mp_mode != NULL) && (p_rf->p_is_con_tx != NULL) && (p_rf->p_is_single_tone != NULL) && (p_rf->p_is_carrier_suppresion != NULL))
		if (*(p_dm->p_mp_mode) && ((*(p_rf->p_is_con_tx) || *(p_rf->p_is_single_tone) || *(p_rf->p_is_carrier_suppresion))))
			return;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_rf->rf_supportability & HAL_RF_IQK))
		return;
#endif

#if DISABLE_BB_RF
	return;
#endif

	if (p_iqk_info->rfk_forbidden)
		return;

	if (!p_dm->rf_calibrate_info.is_iqk_in_progress) {
		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_iqk_in_progress = true;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
		start_time = odm_get_current_time(p_dm);
		switch (p_dm->support_ic_type) {
#if (RTL8188E_SUPPORT == 1) 
		case ODM_RTL8188E:
			phy_iq_calibrate_8188e(p_dm, is_recovery);
			break;
#endif
#if (RTL8188F_SUPPORT == 1) 
		case ODM_RTL8188F:
			phy_iq_calibrate_8188f(p_dm, is_recovery);
			break;
#endif
#if (RTL8192E_SUPPORT == 1) 
		case ODM_RTL8192E:
			phy_iq_calibrate_8192e(p_dm, is_recovery);
			break;
#endif
#if (RTL8197F_SUPPORT == 1) 
		case ODM_RTL8197F:
			phy_iq_calibrate_8197f(p_dm, is_recovery);
			break;
#endif
#if (RTL8703B_SUPPORT == 1) 
		case ODM_RTL8703B:
			phy_iq_calibrate_8703b(p_dm, is_recovery);
			break;
#endif
#if (RTL8710B_SUPPORT == 1) 
		case ODM_RTL8710B:
			phy_iq_calibrate_8710b(p_dm, is_recovery);
			break;
#endif
#if (RTL8723B_SUPPORT == 1) 
		case ODM_RTL8723B:
			phy_iq_calibrate_8723b(p_dm, is_recovery);
			break;
#endif
#if (RTL8723D_SUPPORT == 1) 
		case ODM_RTL8723D:
			phy_iq_calibrate_8723d(p_dm, is_recovery);
			break;
#endif
#if (RTL8812A_SUPPORT == 1) 
		case ODM_RTL8812:
			phy_iq_calibrate_8812a(p_dm, is_recovery);
			break;
#endif
#if (RTL8821A_SUPPORT == 1) 
		case ODM_RTL8821:
			phy_iq_calibrate_8821a(p_dm, is_recovery);
			break;
#endif
#if (RTL8814A_SUPPORT == 1) 
		case ODM_RTL8814A:
			phy_iq_calibrate_8814a(p_dm, is_recovery);
			break;
#endif
#if (RTL8822B_SUPPORT == 1) 
		case ODM_RTL8822B:
			phy_iq_calibrate_8822b(p_dm, false, false);
			break;
#endif
#if (RTL8821C_SUPPORT == 1) 
		case ODM_RTL8821C:
			phy_iq_calibrate_8821c(p_dm, false, false);
			break;
#endif
#if (RTL8814B_SUPPORT == 1) 
		case ODM_RTL8814B:
			break;
#endif
		default:
			break;
		}
		p_dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(p_dm, start_time);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK progressing_time = %lld ms\n", p_dm->rf_calibrate_info.iqk_progressing_time));

		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_iqk_in_progress = false;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
	} else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("== Return the IQK CMD, because RFKs in Progress ==\n"));
}



void
halrf_lck_trigger(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION		*p_iqk_info = &p_dm->IQK_info;
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	u64 start_time;
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(p_dm) == false)
		return;
#endif

	if ((p_dm->p_mp_mode != NULL) && (p_rf->p_is_con_tx != NULL) && (p_rf->p_is_single_tone != NULL) && (p_rf->p_is_carrier_suppresion != NULL))
		if (*(p_dm->p_mp_mode) && ((*(p_rf->p_is_con_tx) || *(p_rf->p_is_single_tone) || *(p_rf->p_is_carrier_suppresion))))
			return;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_rf->rf_supportability & HAL_RF_LCK))
		return;
#endif

#if DISABLE_BB_RF
		return;
#endif
	if (p_iqk_info->rfk_forbidden)
		return;
	while (*(p_dm->p_is_scan_in_process)) {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[LCK]scan is in process, bypass LCK\n"));
		return;
	}

	if (!p_dm->rf_calibrate_info.is_lck_in_progress) {
		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_lck_in_progress = true;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
		start_time = odm_get_current_time(p_dm);
		switch (p_dm->support_ic_type) {
#if (RTL8188E_SUPPORT == 1)
		case ODM_RTL8188E:
			phy_lc_calibrate_8188e(p_dm);
			break;
#endif
#if (RTL8188F_SUPPORT == 1)
		case ODM_RTL8188F:
			phy_lc_calibrate_8188f(p_dm);
			break;
#endif
#if (RTL8192E_SUPPORT == 1)
		case ODM_RTL8192E:
			phy_lc_calibrate_8192e(p_dm);
			break;
#endif
#if (RTL8197F_SUPPORT == 1)
		case ODM_RTL8197F:
			phy_lc_calibrate_8197f(p_dm);
			break;
#endif
#if (RTL8703B_SUPPORT == 1)
		case ODM_RTL8703B:
			phy_lc_calibrate_8703b(p_dm);
			break;
#endif
#if (RTL8710B_SUPPORT == 1)
		case ODM_RTL8710B:
			phy_lc_calibrate_8710b(p_dm);
			break;
#endif
#if (RTL8723B_SUPPORT == 1) 
		case ODM_RTL8723B:
			phy_lc_calibrate_8723b(p_dm);
			break;
#endif
#if (RTL8723D_SUPPORT == 1)
		case ODM_RTL8723D:
			phy_lc_calibrate_8723d(p_dm);
			break;
#endif
#if (RTL8812A_SUPPORT == 1)
		case ODM_RTL8812:
			phy_lc_calibrate_8812a(p_dm);
			break;
#endif
#if (RTL8821A_SUPPORT == 1) 
		case ODM_RTL8821:
			phy_lc_calibrate_8821a(p_dm);
			break;
#endif
#if (RTL8814A_SUPPORT == 1) 
		case ODM_RTL8814A:
			phy_lc_calibrate_8814a(p_dm);
			break;
#endif
#if (RTL8822B_SUPPORT == 1) 
		case ODM_RTL8822B:
			phy_lc_calibrate_8822b(p_dm);
			break;
#endif
#if (RTL8821C_SUPPORT == 1) 
		case ODM_RTL8821C:
			phy_lc_calibrate_8821c(p_dm);
			break;
#endif
#if (RTL8814B_SUPPORT == 1) 
		case ODM_RTL8814B:
			break;
#endif
		default:
			break;
		}
		p_dm->rf_calibrate_info.lck_progressing_time = odm_get_progressing_time(p_dm, start_time);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]LCK progressing_time = %lld ms\n", p_dm->rf_calibrate_info.lck_progressing_time));
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		halrf_lck_dbg(p_dm);
#endif
		odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
		p_dm->rf_calibrate_info.is_lck_in_progress = false;
		odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);		
	}else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("== Return the LCK CMD, because RFK is in Progress ==\n"));
}

void
halrf_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("HALRF_Init\n"));

	if (*(p_dm->p_mp_mode) == true)
		halrf_supportability_init_mp(p_dm);
	else
		halrf_supportability_init(p_dm);

	/*Init all RF funciton*/
	/*iqk_init();*/
	/*dpk_init();*/
}




