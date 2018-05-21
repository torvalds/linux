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


void
phydm_init_trx_antenna_setting(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	if (p_dm->support_ic_type & (ODM_RTL8814A)) {
		u8	rx_ant = 0, tx_ant = 0;

		rx_ant = (u8)odm_get_bb_reg(p_dm, ODM_REG(BB_RX_PATH, p_dm), ODM_BIT(BB_RX_PATH, p_dm));
		tx_ant = (u8)odm_get_bb_reg(p_dm, ODM_REG(BB_TX_PATH, p_dm), ODM_BIT(BB_TX_PATH, p_dm));
		p_dm->tx_ant_status = (tx_ant & 0xf);
		p_dm->rx_ant_status = (rx_ant & 0xf);
	} else if (p_dm->support_ic_type & (ODM_RTL8723D | ODM_RTL8821C | ODM_RTL8710B)) {/* JJ ADD 20161014 */
		p_dm->tx_ant_status = 0x1;
		p_dm->rx_ant_status = 0x1;

	}
}

void
phydm_config_ofdm_tx_path(
	void			*p_dm_void,
	u32			path
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if ((RTL8192E_SUPPORT == 1) || (RTL8812A_SUPPORT == 1))
	u8	ofdm_tx_path = 0x33;
#endif

#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type & (ODM_RTL8192E)) {

		if (path == BB_PATH_A) {
			odm_set_bb_reg(p_dm, 0x90c, MASKDWORD, 0x81121111);
			/**/
		} else if (path == BB_PATH_B) {
			odm_set_bb_reg(p_dm, 0x90c, MASKDWORD, 0x82221222);
			/**/
		} else  if (path == BB_PATH_AB) {
			odm_set_bb_reg(p_dm, 0x90c, MASKDWORD, 0x83321333);
			/**/
		}


	}
#endif

#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type & (ODM_RTL8812)) {

		if (path == BB_PATH_A) {
			ofdm_tx_path = 0x11;
			/**/
		} else if (path == BB_PATH_B) {
			ofdm_tx_path = 0x22;
			/**/
		} else  if (path == BB_PATH_AB) {
			ofdm_tx_path = 0x33;
			/**/
		}

		odm_set_bb_reg(p_dm, 0x80c, 0xff00, ofdm_tx_path);
	}
#endif
}

void
phydm_config_ofdm_rx_path(
	void			*p_dm_void,
	u32			path
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	ofdm_rx_path = 0;


	if (p_dm->support_ic_type & (ODM_RTL8192E)) {
#if (RTL8192E_SUPPORT == 1)
		if (path == BB_PATH_A) {
			ofdm_rx_path = 1;
			/**/
		} else if (path == BB_PATH_B) {
			ofdm_rx_path = 2;
			/**/
		} else  if (path == BB_PATH_AB) {
			ofdm_rx_path = 3;
			/**/
		}

		odm_set_bb_reg(p_dm, 0xC04, 0xff, (((ofdm_rx_path) << 4) | ofdm_rx_path));
		odm_set_bb_reg(p_dm, 0xD04, 0xf, ofdm_rx_path);
#endif
	}
#if (RTL8812A_SUPPORT || RTL8822B_SUPPORT)
	else if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B)) {

		if (path == BB_PATH_A) {
			ofdm_rx_path = 1;
			/**/
		} else if (path == BB_PATH_B) {
			ofdm_rx_path = 2;
			/**/
		} else  if (path == BB_PATH_AB) {
			ofdm_rx_path = 3;
			/**/
		}

		odm_set_bb_reg(p_dm, 0x808, MASKBYTE0, ((ofdm_rx_path << 4) | ofdm_rx_path));
	}
#endif
}

void
phydm_config_cck_rx_antenna_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	/*CCK 2R CCA parameters*/
	odm_set_bb_reg(p_dm, 0xa00, BIT(15), 0x0); /*Disable antenna diversity*/
	odm_set_bb_reg(p_dm, 0xa70, BIT(7), 0); /*Concurrent CCA at LSB & USB*/
	odm_set_bb_reg(p_dm, 0xa74, BIT(8), 0); /*RX path diversity enable*/
	odm_set_bb_reg(p_dm, 0xa14, BIT(7), 0); /*r_en_mrc_antsel*/
	odm_set_bb_reg(p_dm, 0xa20, (BIT(5) | BIT(4)), 1); /*MBC weighting*/

	if (p_dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8197F)) {
		odm_set_bb_reg(p_dm, 0xa08, BIT(28), 1); /*r_cck_2nd_sel_eco*/
		/**/
	} else if (p_dm->support_ic_type & ODM_RTL8814A) {
		odm_set_bb_reg(p_dm, 0xa84, BIT(28), 1); /*2R CCA only*/
		/**/
	}
#endif
}

void
phydm_config_cck_rx_path(
	void		*p_dm_void,
	enum bb_path			path
)
{
#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	path_div_select = 0;
	u8	cck_path[2] = {0};
	u8	en_2R_path = 0;
	u8	en_2R_mrc = 0;
	u8	i = 0, j =0; 
	u8	num_enable_path = 0;
	u8	cck_mrc_max_path = 2;
	
	for (i = 0; i < 4; i++) {
		if (path & BIT(i)) { /*ex: PHYDM_ABCD*/
			num_enable_path++;
			cck_path[j] = i;
			j++;
		}
		if (num_enable_path >= cck_mrc_max_path)
			break;
	}

	if (num_enable_path > 1) {
		path_div_select = 1;
		en_2R_path = 1;
		en_2R_mrc = 1;
	} else {
		path_div_select = 0;
		en_2R_path = 0;
		en_2R_mrc = 0;
	}
		
	odm_set_bb_reg(p_dm, 0xa04, (BIT(27) | BIT(26)), cck_path[0]);	/*CCK_1 input signal path*/
	odm_set_bb_reg(p_dm, 0xa04, (BIT(25) | BIT(24)), cck_path[1]);	/*CCK_2 input signal path*/
	odm_set_bb_reg(p_dm, 0xa74, BIT(8), path_div_select);	/*enable Rx path diversity*/
	odm_set_bb_reg(p_dm, 0xa2c, BIT(18), en_2R_path);	/*enable 2R Rx path*/
	odm_set_bb_reg(p_dm, 0xa2c, BIT(22), en_2R_mrc);	/*enable 2R MRC*/
	
#endif
}

void
phydm_config_trx_path(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* CCK */
	if (dm_value[0] == 0) {

		if (dm_value[1] == 1) { /*TX*/
			if (dm_value[2] == 1)
				odm_set_bb_reg(p_dm, 0xa04, 0xf0000000, 0x8);
			else if (dm_value[2] == 2)
				odm_set_bb_reg(p_dm, 0xa04, 0xf0000000, 0x4);
			else if (dm_value[2] == 3)
				odm_set_bb_reg(p_dm, 0xa04, 0xf0000000, 0xc);
		} else if (dm_value[1] == 2) { /*RX*/

			phydm_config_cck_rx_antenna_init(p_dm);

			if (dm_value[2] == 1)
				phydm_config_cck_rx_path(p_dm, BB_PATH_A);
			else  if (dm_value[2] == 2)
				phydm_config_cck_rx_path(p_dm, BB_PATH_B);
			else  if (dm_value[2] == 3) {
				phydm_config_cck_rx_path(p_dm, BB_PATH_AB);
			}
		}
	}
	/* OFDM */
	else if (dm_value[0] == 1) {

		if (dm_value[1] == 1) { /*TX*/
			phydm_config_ofdm_tx_path(p_dm, dm_value[2]);
			/**/
		} else if (dm_value[1] == 2) { /*RX*/
			phydm_config_ofdm_rx_path(p_dm, dm_value[2]);
			/**/
		}
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "PHYDM Set path [%s] [%s] = [%s%s%s%s]\n",
			(dm_value[0] == 1) ? "OFDM" : "CCK",
			(dm_value[1] == 1) ? "TX" : "RX",
			(dm_value[2] & 0x1) ? "A" : "",
			(dm_value[2] & 0x2) ? "B" : "",
			(dm_value[2] & 0x4) ? "C" : "",
			(dm_value[2] & 0x8) ? "D" : ""
		       ));

}

void
phydm_stop_3_wire(
	void		*p_dm_void,
	u8		set_type
)
{
	struct	PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (set_type == PHYDM_SET) {

		/*[Stop 3-wires]*/
		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(p_dm, 0xc00, 0xf, 0x4);/*	hardware 3-wire off */
			odm_set_bb_reg(p_dm, 0xe00, 0xf, 0x4);/*	hardware 3-wire off */
		} else {
			odm_set_bb_reg(p_dm, 0x88c, 0xf00000, 0xf);	/* 3 wire Disable    88c[23:20]=0xf */
		}
		
	} else {  /*if (set_type == PHYDM_REVERT)*/
		
		/*[Start 3-wires]*/
		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(p_dm, 0xc00, 0xf, 0x7);/*	hardware 3-wire on */
			odm_set_bb_reg(p_dm, 0xe00, 0xf, 0x7);/*	hardware 3-wire on */
		} else {
			odm_set_bb_reg(p_dm, 0x88c, 0xf00000, 0x0);	/* 3 wire enable 88c[23:20]=0x0 */
		}
	}
}

u8
phydm_stop_ic_trx(
	void		*p_dm_void,
	u8		set_type
	)
{
	struct	PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct	phydm_api_stuc 	*p_api = &(p_dm->api_table);
	u32		i;
	u8		trx_idle_success = false;
	u32		dbg_port_value = 0;

	if (set_type == PHYDM_SET) {
		/*[Stop TRX]---------------------------------------------------------------------*/
		if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_3, 0x0) == false) /*set debug port to 0x0*/
			return PHYDM_SET_FAIL;
		
		for (i = 0; i<10000; i++) {
			dbg_port_value = phydm_get_bb_dbg_port_value(p_dm);
			if ((dbg_port_value & (BIT(17) | BIT(3))) == 0)	/* PHYTXON && CCA_all */ {
				PHYDM_DBG(p_dm, ODM_COMP_API, ("PSD wait for ((%d)) times\n", i));
				
				trx_idle_success = true;
				break;
			}
		}
		phydm_release_bb_dbg_port(p_dm);
		
		if (trx_idle_success) {

			p_api->tx_queue_bitmap = (u8)odm_get_bb_reg(p_dm, 0x520, 0xff0000);
			
			odm_set_bb_reg(p_dm, 0x520, 0xff0000, 0xff); /*pause all TX queue*/
			
			if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
				odm_set_bb_reg(p_dm, 0x808, BIT(28), 0); /*disable CCK block*/
				odm_set_bb_reg(p_dm, 0x838, BIT(1), 1); /*disable OFDM RX CCA*/
			} else {
				/*TBD*/
				odm_set_bb_reg(p_dm, 0x800, BIT(24), 0); /* disable whole CCK block */


				p_api->rx_iqc_reg_1 = odm_get_bb_reg(p_dm, 0xc14, MASKDWORD);
				p_api->rx_iqc_reg_2 = odm_get_bb_reg(p_dm, 0xc1c, MASKDWORD);
				
				odm_set_bb_reg(p_dm, 0xc14, MASKDWORD, 0x0); /* [ Set IQK Matrix = 0 ] equivalent to [ Turn off CCA] */
				odm_set_bb_reg(p_dm, 0xc1c, MASKDWORD, 0x0);
			}
				
		} else {
			return PHYDM_SET_FAIL;
		}
		
		return PHYDM_SET_SUCCESS;
		
	} else {  /*if (set_type == PHYDM_REVERT)*/

		odm_set_bb_reg(p_dm, 0x520, 0xff0000, (u32)(p_api->tx_queue_bitmap)); /*Release all TX queue*/

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(p_dm, 0x808, BIT(28), 1); /*enable CCK block*/
			odm_set_bb_reg(p_dm, 0x838, BIT(1), 0); /*enable OFDM RX CCA*/
		} else {
			/*TBD*/
			odm_set_bb_reg(p_dm, 0x800, BIT(24), 1); /* enable whole CCK block */
			
			odm_set_bb_reg(p_dm, 0xc14, MASKDWORD, p_api->rx_iqc_reg_1); /* [ Set IQK Matrix = 0 ] equivalent to [ Turn off CCA] */
			odm_set_bb_reg(p_dm, 0xc1c, MASKDWORD, p_api->rx_iqc_reg_2);
		}

		return PHYDM_SET_SUCCESS;
	}
	
}

void
phydm_set_ext_switch(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			ext_ant_switch =  dm_value[0];

#if (RTL8821A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1)
	if (p_dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {

		/*Output Pin Settings*/
		odm_set_mac_reg(p_dm, 0x4C, BIT(23), 0); /*select DPDT_P and DPDT_N as output pin*/
		odm_set_mac_reg(p_dm, 0x4C, BIT(24), 1); /*by WLAN control*/

		odm_set_bb_reg(p_dm, 0xCB4, 0xFF, 77); /*DPDT_N = 1b'0*/  /*DPDT_P = 1b'0*/

		if (ext_ant_switch == MAIN_ANT) {
			odm_set_bb_reg(p_dm, 0xCB4, (BIT(29) | BIT(28)), 1);
			PHYDM_DBG(p_dm, ODM_COMP_API, ("***8821A set ant switch = 2b'01 (Main)\n"));
		} else if (ext_ant_switch == AUX_ANT) {
			odm_set_bb_reg(p_dm, 0xCB4, BIT(29) | BIT(28), 2);
			PHYDM_DBG(p_dm, ODM_COMP_API, ("***8821A set ant switch = 2b'10 (Aux)\n"));
		}
	}
#endif
}

void
phydm_csi_mask_enable(
	void		*p_dm_void,
	u32		enable
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		reg_value = 0;

	reg_value = (enable == FUNC_ENABLE) ? 1 : 0;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		odm_set_bb_reg(p_dm, 0xD2C, BIT(28), reg_value);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Enable CSI Mask:  Reg 0xD2C[28] = ((0x%x))\n", reg_value));

	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		odm_set_bb_reg(p_dm, 0x874, BIT(0), reg_value);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Enable CSI Mask:  Reg 0x874[0] = ((0x%x))\n", reg_value));
	}

}

void
phydm_clean_all_csi_mask(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		odm_set_bb_reg(p_dm, 0xD40, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0xD44, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0xD48, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0xD4c, MASKDWORD, 0);

	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		odm_set_bb_reg(p_dm, 0x880, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x884, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x888, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x88c, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x890, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x894, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x898, MASKDWORD, 0);
		odm_set_bb_reg(p_dm, 0x89c, MASKDWORD, 0);
	}
}

void
phydm_set_csi_mask_reg(
	void		*p_dm_void,
	u32		tone_idx_tmp,
	u8		tone_direction
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		byte_offset, bit_offset;
	u32		target_reg;
	u8		reg_tmp_value;
	u32		tone_num = 64;
	u32		tone_num_shift = 0;
	u32		csi_mask_reg_p = 0, csi_mask_reg_n = 0;

	/* calculate real tone idx*/
	if ((tone_idx_tmp % 10) >= 5)
		tone_idx_tmp += 10;

	tone_idx_tmp = (tone_idx_tmp / 10);

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		tone_num = 64;
		csi_mask_reg_p = 0xD40;
		csi_mask_reg_n = 0xD48;

	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		tone_num = 128;
		csi_mask_reg_p = 0x880;
		csi_mask_reg_n = 0x890;
	}

	if (tone_direction == FREQ_POSITIVE) {

		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = (tone_num - 1);

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_p + byte_offset;

	} else {
		tone_num_shift = tone_num;

		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = tone_num - tone_idx_tmp;

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_n + byte_offset;
	}

	reg_tmp_value = odm_read_1byte(p_dm, target_reg);
	PHYDM_DBG(p_dm, ODM_COMP_API, ("Pre Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n", (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value));
	reg_tmp_value |= BIT(bit_offset);
	odm_write_1byte(p_dm, target_reg, reg_tmp_value);
	PHYDM_DBG(p_dm, ODM_COMP_API, ("New Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n", (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value));
}

void
phydm_set_nbi_reg(
	void		*p_dm_void,
	u32		tone_idx_tmp,
	u32		bw
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	nbi_table_128[NBI_TABLE_SIZE_128] = {25, 55, 85, 115, 135, 155, 185, 205, 225, 245,		/*1~10*/		/*tone_idx X 10*/
		     265, 285, 305, 335, 355, 375, 395, 415, 435, 455,	/*11~20*/
					     485, 505, 525, 555, 585, 615, 635
						};				/*21~27*/

	u32	nbi_table_256[NBI_TABLE_SIZE_256] = { 25,   55,   85, 115, 135, 155, 175, 195, 225, 245,	/*1~10*/
		265, 285, 305, 325, 345, 365, 385, 405, 425, 445,	/*11~20*/
		465, 485, 505, 525, 545, 565, 585, 605, 625, 645,	/*21~30*/
		665, 695, 715, 735, 755, 775, 795, 815, 835, 855,	/*31~40*/
		875, 895, 915, 935, 955, 975, 995, 1015, 1035, 1055,	/*41~50*/
		      1085, 1105, 1125, 1145, 1175, 1195, 1225, 1255, 1275
						};	/*51~59*/

	u32	reg_idx = 0;
	u32	i;
	u8	nbi_table_idx = FFT_128_TYPE;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES)

		nbi_table_idx = FFT_128_TYPE;
	else if (p_dm->support_ic_type & ODM_IC_11AC_1_SERIES)

		nbi_table_idx = FFT_256_TYPE;
	else if (p_dm->support_ic_type & ODM_IC_11AC_2_SERIES) {

		if (bw == 80)
			nbi_table_idx = FFT_256_TYPE;
		else /*20M, 40M*/
			nbi_table_idx = FFT_128_TYPE;
	}

	if (nbi_table_idx == FFT_128_TYPE) {

		for (i = 0; i < NBI_TABLE_SIZE_128; i++) {
			if (tone_idx_tmp < nbi_table_128[i]) {
				reg_idx = i + 1;
				break;
			}
		}

	} else if (nbi_table_idx == FFT_256_TYPE) {

		for (i = 0; i < NBI_TABLE_SIZE_256; i++) {
			if (tone_idx_tmp < nbi_table_256[i]) {
				reg_idx = i + 1;
				break;
			}
		}
	}

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(p_dm, 0xc40, 0x1f000000, reg_idx);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Set tone idx:  Reg0xC40[28:24] = ((0x%x))\n", reg_idx));
		/**/
	} else {
		odm_set_bb_reg(p_dm, 0x87c, 0xfc000, reg_idx);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Set tone idx: Reg0x87C[19:14] = ((0x%x))\n", reg_idx));
		/**/
	}
}


void
phydm_nbi_enable(
	void		*p_dm_void,
	u32		enable
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		reg_value = 0;

	reg_value = (enable == FUNC_ENABLE) ? 1 : 0;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		odm_set_bb_reg(p_dm, 0xc40, BIT(9), reg_value);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Enable NBI Reg0xC40[9] = ((0x%x))\n", reg_value));

	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		if (p_dm->support_ic_type & (ODM_RTL8822B|ODM_RTL8821C)) {
			odm_set_bb_reg(p_dm, 0x87c, BIT(13), reg_value);
			odm_set_bb_reg(p_dm, 0xc20, BIT(28), reg_value);
			if (p_dm->rf_type > RF_1T1R)
				odm_set_bb_reg(p_dm, 0xe20, BIT(28), reg_value);
		} else
			odm_set_bb_reg(p_dm, 0x87c, BIT(13), reg_value);
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Enable NBI Reg0x87C[13] = ((0x%x))\n", reg_value));
	}
}

u8
phydm_calculate_fc(
	void		*p_dm_void,
	u32		channel,
	u32		bw,
	u32		second_ch,
	u32		*fc_in
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		fc = *fc_in;
	u32		start_ch_per_40m[NUM_START_CH_40M] = {36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157, 165, 173};
	u32		start_ch_per_80m[NUM_START_CH_80M] = {36, 52, 100, 116, 132, 149, 165};
	u32		*p_start_ch = &(start_ch_per_40m[0]);
	u32		num_start_channel = NUM_START_CH_40M;
	u32		channel_offset = 0;
	u32		i;

	/*2.4G*/
	if (channel <= 14 && channel > 0) {

		if (bw == 80)
			return	PHYDM_SET_FAIL;

		fc = 2412 + (channel - 1) * 5;

		if (bw == 40 && (second_ch == PHYDM_ABOVE)) {

			if (channel >= 10) {
				PHYDM_DBG(p_dm, ODM_COMP_API, ("CH = ((%d)), Scnd_CH = ((%d)) Error setting\n", channel, second_ch));
				return	PHYDM_SET_FAIL;
			}
			fc += 10;
		} else if (bw == 40 && (second_ch == PHYDM_BELOW)) {

			if (channel <= 2) {
				PHYDM_DBG(p_dm, ODM_COMP_API, ("CH = ((%d)), Scnd_CH = ((%d)) Error setting\n", channel, second_ch));
				return	PHYDM_SET_FAIL;
			}
			fc -= 10;
		}
	}
	/*5G*/
	else if (channel >= 36 && channel <= 177) {

		if (bw != 20) {

			if (bw == 40) {
				num_start_channel = NUM_START_CH_40M;
				p_start_ch = &(start_ch_per_40m[0]);
				channel_offset = CH_OFFSET_40M;
			} else if (bw == 80) {
				num_start_channel = NUM_START_CH_80M;
				p_start_ch = &(start_ch_per_80m[0]);
				channel_offset = CH_OFFSET_80M;
			}

			for (i = 0; i < (num_start_channel - 1); i++) {

				if (channel < p_start_ch[i + 1]) {
					channel = p_start_ch[i] + channel_offset;
					break;
				}
			}
			PHYDM_DBG(p_dm, ODM_COMP_API, ("Mod_CH = ((%d))\n", channel));
		}

		fc = 5180 + (channel - 36) * 5;

	} else {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("CH = ((%d)) Error setting\n", channel));
		return	PHYDM_SET_FAIL;
	}

	*fc_in = fc;

	return PHYDM_SET_SUCCESS;
}


u8
phydm_calculate_intf_distance(
	void		*p_dm_void,
	u32		bw,
	u32		fc,
	u32		f_interference,
	u32		*p_tone_idx_tmp_in
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		bw_up, bw_low;
	u32		int_distance;
	u32		tone_idx_tmp;
	u8		set_result = PHYDM_SET_NO_NEED;

	bw_up = fc + bw / 2;
	bw_low = fc - bw / 2;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("[f_l, fc, fh] = [ %d, %d, %d ], f_int = ((%d))\n", bw_low, fc, bw_up, f_interference));

	if ((f_interference >= bw_low) && (f_interference <= bw_up)) {

		int_distance = (fc >= f_interference) ? (fc - f_interference) : (f_interference - fc);
		tone_idx_tmp = (int_distance << 5); /* =10*(int_distance /0.3125) */
		PHYDM_DBG(p_dm, ODM_COMP_API, ("int_distance = ((%d MHz)) Mhz, tone_idx_tmp = ((%d.%d))\n", int_distance, (tone_idx_tmp / 10), (tone_idx_tmp % 10)));
		*p_tone_idx_tmp_in = tone_idx_tmp;
		set_result = PHYDM_SET_SUCCESS;
	}

	return	set_result;

}


u8
phydm_csi_mask_setting(
	void		*p_dm_void,
	u32		enable,
	u32		channel,
	u32		bw,
	u32		f_interference,
	u32		second_ch
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		fc = 2412;
	u8		tone_direction;
	u32		tone_idx_tmp;
	u8		set_result = PHYDM_SET_SUCCESS;

	if (enable == FUNC_DISABLE) {
		set_result = PHYDM_SET_SUCCESS;
		phydm_clean_all_csi_mask(p_dm);

	} else {

		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Set CSI MASK_] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			channel, bw, f_interference, (((bw == 20) || (channel > 14)) ? "Don't care" : (second_ch == PHYDM_ABOVE) ? "H" : "L")));

		/*calculate fc*/
		if (phydm_calculate_fc(p_dm, channel, bw, second_ch, &fc) == PHYDM_SET_FAIL)
			set_result = PHYDM_SET_FAIL;

		else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(p_dm, bw, fc, f_interference, &tone_idx_tmp) == PHYDM_SET_SUCCESS) {

				tone_direction = (f_interference >= fc) ? FREQ_POSITIVE : FREQ_NEGATIVE;
				phydm_set_csi_mask_reg(p_dm, tone_idx_tmp, tone_direction);
				set_result = PHYDM_SET_SUCCESS;
			} else
				set_result = PHYDM_SET_NO_NEED;
		}
	}

	if (set_result == PHYDM_SET_SUCCESS)
		phydm_csi_mask_enable(p_dm, enable);
	else
		phydm_csi_mask_enable(p_dm, FUNC_DISABLE);

	return	set_result;
}

u8
phydm_nbi_setting(
	void		*p_dm_void,
	u32		enable,
	u32		channel,
	u32		bw,
	u32		f_interference,
	u32		second_ch
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		fc = 2412;
	u32		tone_idx_tmp;
	u8		set_result = PHYDM_SET_SUCCESS;

	if (enable == FUNC_DISABLE)
		set_result = PHYDM_SET_SUCCESS;

	else {

		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Set NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			channel, bw, f_interference, (((second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "Don't care" : (second_ch == PHYDM_ABOVE) ? "H" : "L")));

		/*calculate fc*/
		if (phydm_calculate_fc(p_dm, channel, bw, second_ch, &fc) == PHYDM_SET_FAIL)
			set_result = PHYDM_SET_FAIL;

		else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(p_dm, bw, fc, f_interference, &tone_idx_tmp) == PHYDM_SET_SUCCESS) {

				phydm_set_nbi_reg(p_dm, tone_idx_tmp, bw);
				set_result = PHYDM_SET_SUCCESS;
			} else
				set_result = PHYDM_SET_NO_NEED;
		}
	}

	if (set_result == PHYDM_SET_SUCCESS)
		phydm_nbi_enable(p_dm, enable);
	else
		phydm_nbi_enable(p_dm, FUNC_DISABLE);

	return	set_result;
}

void
phydm_api_debug(
	void		*p_dm_void,
	u32		function_map,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;
	u32			channel =  dm_value[1];
	u32			bw =  dm_value[2];
	u32			f_interference =  dm_value[3];
	u32			second_ch =  dm_value[4];
	u8			set_result = 0;

	/*PHYDM_API_NBI*/
	/*-------------------------------------------------------------------------------------------------------------------------------*/
	if (function_map == PHYDM_API_NBI) {

		if (dm_value[0] == 100) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[HELP-NBI]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n"));
			return;

		} else if (dm_value[0] == FUNC_ENABLE) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[Enable NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
				channel, bw, f_interference, ((second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "Don't care" : ((second_ch == PHYDM_ABOVE) ? "H" : "L")));
			set_result = phydm_nbi_setting(p_dm, FUNC_ENABLE, channel, bw, f_interference, second_ch);

		} else if (dm_value[0] == FUNC_DISABLE) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[Disable NBI]\n"));
			set_result = phydm_nbi_setting(p_dm, FUNC_DISABLE, channel, bw, f_interference, second_ch);

		} else

			set_result = PHYDM_SET_FAIL;
		PHYDM_SNPRINTF((output + used, out_len - used, "[NBI set result: %s]\n", (set_result == PHYDM_SET_SUCCESS) ? "Success" : ((set_result == PHYDM_SET_NO_NEED) ? "No need" : "Error")));

	}

	/*PHYDM_CSI_MASK*/
	/*-------------------------------------------------------------------------------------------------------------------------------*/
	else if (function_map == PHYDM_API_CSI_MASK) {

		if (dm_value[0] == 100) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[HELP-CSI MASK]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n"));
			return;

		} else if (dm_value[0] == FUNC_ENABLE) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[Enable CSI MASK] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
				channel, bw, f_interference, (channel > 14) ? "Don't care" : (((second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "H" : "L")));
			set_result = phydm_csi_mask_setting(p_dm,	FUNC_ENABLE, channel, bw, f_interference, second_ch);

		} else if (dm_value[0] == FUNC_DISABLE) {

			PHYDM_SNPRINTF((output + used, out_len - used, "[Disable CSI MASK]\n"));
			set_result = phydm_csi_mask_setting(p_dm, FUNC_DISABLE, channel, bw, f_interference, second_ch);

		} else

			set_result = PHYDM_SET_FAIL;
		PHYDM_SNPRINTF((output + used, out_len - used, "[CSI MASK set result: %s]\n", (set_result == PHYDM_SET_SUCCESS) ? "Success" : ((set_result == PHYDM_SET_NO_NEED) ? "No need" : "Error")));
	}
	*_used = used;
	*_out_len = out_len;
}

void
phydm_stop_ck320(
	void			*p_dm_void,
	u8			enable
) {
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		reg_value = (enable == true) ? 1 : 0;
	
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm, 0x8b4, BIT(6), reg_value);
		/**/
	} else { 

		if (p_dm->support_ic_type & ODM_IC_N_2SS) {	/*N-2SS*/
			odm_set_bb_reg(p_dm, 0x87c, BIT(29), reg_value);
			/**/
		} else {	/*N-1SS*/
			odm_set_bb_reg(p_dm, 0x87c, BIT(31), reg_value);
			/**/
		}
	}
}

#ifdef PHYDM_COMMON_API_SUPPORT
boolean
phydm_api_set_txagc(
	void				*p_dm_void,
	u32				power_index,
	enum rf_path		path,
	u8				hw_rate,
	boolean			is_single_rate
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	boolean		ret = false;
	u8	i;

#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1))
	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
		if (is_single_rate) {
			
			#if (RTL8822B_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8822B)
				ret = phydm_write_txagc_1byte_8822b(p_dm, power_index, path, hw_rate);
			#endif
			
			#if (RTL8821C_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8821C)
				ret = phydm_write_txagc_1byte_8821c(p_dm, power_index, path, hw_rate);
			#endif
			
			#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			set_current_tx_agc(p_dm->priv, path, hw_rate, (u8)power_index);
			#endif

		} else {

			#if (RTL8822B_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8822B)
				ret = config_phydm_write_txagc_8822b(p_dm, power_index, path, hw_rate);
			#endif
			
			#if (RTL8821C_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8821C)
				ret = config_phydm_write_txagc_8821c(p_dm, power_index, path, hw_rate);
			#endif
			
			#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			for (i = 0; i < 4; i++)
				set_current_tx_agc(p_dm->priv, path, (hw_rate + i), (u8)power_index);
			#endif
		}
	}
#endif


#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_write_txagc_8197f(p_dm, power_index, path, hw_rate);
#endif

	return ret;
}

u8
phydm_api_get_txagc(
	void				*p_dm_void,
	enum rf_path		path,
	u8				hw_rate
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	ret = 0;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_read_txagc_8822b(p_dm, path, hw_rate);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_read_txagc_8197f(p_dm, path, hw_rate);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8821C)
		ret = config_phydm_read_txagc_8821c(p_dm, path, hw_rate);
#endif

	return ret;
}


boolean
phydm_api_switch_bw_channel(
	void					*p_dm_void,
	u8					central_ch,
	u8					primary_ch_idx,
	enum channel_width	bandwidth
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;	
	boolean		ret = false;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_switch_channel_bw_8822b(p_dm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_switch_channel_bw_8197f(p_dm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8821C)
		ret = config_phydm_switch_channel_bw_8821c(p_dm, central_ch, primary_ch_idx, bandwidth);
#endif

	return ret;
}

boolean
phydm_api_trx_mode(
	void				*p_dm_void,
	enum bb_path	tx_path,
	enum bb_path	rx_path,
	boolean			is_tx2_path
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	boolean		ret = false;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_trx_mode_8822b(p_dm, tx_path, rx_path, is_tx2_path);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_trx_mode_8197f(p_dm, tx_path, rx_path, is_tx2_path);
#endif

	return ret;
}
#endif


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
phydm_normal_driver_rx_sniffer(
	struct PHY_DM_STRUCT			*p_dm,
	u8				*p_desc,
	PRT_RFD_STATUS		p_rt_rfd_status,
	u8				*p_drv_info,
	u8				phy_status
)
{
#if (defined(CONFIG_PHYDM_RX_SNIFFER_PARSING))
	u32		*p_msg;
	u16		seq_num;
	struct phydm_fat_struct		*p_dm_fat_table = &p_dm->dm_fat_table;

	if (p_rt_rfd_status->packet_report_type != NORMAL_RX)
		return;

	if (!p_dm->is_linked) {
		if (p_rt_rfd_status->is_hw_error)
			return;
	}

	if (!(p_dm_fat_table->fat_state == FAT_TRAINING_STATE))
		return;

	if (phy_status == true) {

		if ((p_dm->rx_pkt_type == type_block_ack) || (p_dm->rx_pkt_type == type_rts) || (p_dm->rx_pkt_type == type_cts))
			seq_num = 0;
		else
			seq_num = p_rt_rfd_status->seq_num;

		PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, ("%04d , %01s, rate=0x%02x, L=%04d , %s , %s",
				seq_num,
				/*p_rt_rfd_status->mac_id,*/
			((p_rt_rfd_status->is_crc) ? "C" : (p_rt_rfd_status->is_ampdu) ? "A" : "_"),
				p_rt_rfd_status->data_rate,
				p_rt_rfd_status->length,
			((p_rt_rfd_status->band_width == 0) ? "20M" : ((p_rt_rfd_status->band_width == 1) ? "40M" : "80M")),
				((p_rt_rfd_status->is_ldpc) ? "LDP" : "BCC")));

		if (p_dm->rx_pkt_type == type_asoc_req) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "AS_REQ"));
			/**/
		} else if (p_dm->rx_pkt_type == type_asoc_rsp) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "AS_RSP"));
			/**/
		} else if (p_dm->rx_pkt_type == type_probe_req) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "PR_REQ"));
			/**/
		} else if (p_dm->rx_pkt_type == type_probe_rsp) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "PR_RSP"));
			/**/
		} else if (p_dm->rx_pkt_type == type_deauth) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "DEAUTH"));
			/**/
		} else if (p_dm->rx_pkt_type == type_beacon) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "BEACON"));
			/**/
		} else if (p_dm->rx_pkt_type == type_block_ack_req) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "BA_REQ"));
			/**/
		} else if (p_dm->rx_pkt_type == type_rts) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "__RTS_"));
			/**/
		} else if (p_dm->rx_pkt_type == type_cts) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "__CTS_"));
			/**/
		} else if (p_dm->rx_pkt_type == type_ack) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "__ACK_"));
			/**/
		} else if (p_dm->rx_pkt_type == type_block_ack) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "__BA__"));
			/**/
		} else if (p_dm->rx_pkt_type == type_data) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "_DATA_"));
			/**/
		} else if (p_dm->rx_pkt_type == type_data_ack) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "Data_Ack"));
			/**/
		} else if (p_dm->rx_pkt_type == type_qos_data) {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [%s]", "QoS_Data"));
			/**/
		} else {
			PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [0x%x]", p_dm->rx_pkt_type));
			/**/
		}

		PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , [RSSI=%d,%d,%d,%d ]",
				p_dm->RSSI_A,
				p_dm->RSSI_B,
				p_dm->RSSI_C,
				p_dm->RSSI_D));

		p_msg = (u32 *)p_drv_info;

		PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, (" , P-STS[28:0]=%08x-%08x-%08x-%08x-%08x-%08x-%08x\n",
			p_msg[6], p_msg[5], p_msg[4], p_msg[3], p_msg[2], p_msg[1], p_msg[1]));
	} else {

		PHYDM_DBG_F(p_dm, ODM_COMP_SNIFFER, ("%04d , %01s, rate=0x%02x, L=%04d , %s , %s\n",
				p_rt_rfd_status->seq_num,
				/*p_rt_rfd_status->mac_id,*/
			((p_rt_rfd_status->is_crc) ? "C" : (p_rt_rfd_status->is_ampdu) ? "A" : "_"),
				p_rt_rfd_status->data_rate,
				p_rt_rfd_status->length,
			((p_rt_rfd_status->band_width == 0) ? "20M" : ((p_rt_rfd_status->band_width == 1) ? "40M" : "80M")),
				((p_rt_rfd_status->is_ldpc) ? "LDP" : "BCC")));
	}


#endif
}
#endif

