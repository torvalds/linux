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

/*************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

/*******************************************************
 * when antenna test utility is on or some testing need to disable antenna
 * diversity call this function to disable all ODM related mechanisms which
 * will switch antenna.
 *****************************************************
 */
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
#if (RTL8710C_SUPPORT == 1)
void odm_s0s1_sw_ant_div_init_8710c(void *dm_void)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch	*swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct		*fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8710C AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n");
	/*MAC setting*/
	HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xdc, HAL_READ32(SYSTEM_CTRL_BASE, R_0xdc) | BIT18 | BIT17 | BIT16);
	HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xac, HAL_READ32(SYSTEM_CTRL_BASE, R_0xac) | BIT24 | BIT6);
	HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x10, 0x307);
	HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x08, 0x80000111);
	HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x1208, 0x800000);

	/* Status init */
	fat_tab->is_become_linked  = false;
	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;
	swat_tab->cur_antenna = MAIN_ANT;
	swat_tab->pre_ant = MAIN_ANT;
	dm->antdiv_counter = CONFIG_ANTDIV_PERIOD;
}

void odm_trx_hw_ant_div_init_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[8710C] AntDiv_Init =>  ant_div_type=[CG_TRX_HW_ANTDIV]\n");
	odm_set_mac_reg(dm, R_0x74, BIT(13) | BIT(12), 1);
	odm_set_mac_reg(dm, R_0x74, BIT(4), 1);

	/*@BT Coexistence*/
	/*@keep antsel_map when GNT_BT = 1*/
	odm_set_bb_reg(dm, R_0x864, BIT(12), 1);
	
	/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */
	odm_set_bb_reg(dm, R_0x874, BIT(23), 1);
	odm_set_bb_reg(dm, R_0x930, 0xF00, 8); /* RFE CTRL_2 ANTSEL0 */

	odm_set_bb_reg(dm, R_0x870, BIT(8), 0);
	odm_set_bb_reg(dm, R_0x804, BIT(8), 0); /* r_keep_rfpin */

	/*@Mapping Table*/
	//odm_set_bb_reg(dm, R_0x864, BIT2|BIT1|BIT0, 2); 
	odm_set_bb_reg(dm, R_0x944, 0xFFFF, 0xffff); 
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 1);
	/*@antenna training	*/
	odm_set_bb_reg(dm, R_0xe08, BIT(16), 0);
	
	//need to check!!!!!!!!!!
	/* Set WLBB_SEL_RF_ON 1 if RXFIR_PWDB > 0xCcc[3:0] */
	odm_set_bb_reg(dm, R_0xccc, BIT(12), 0);
	/* @Low-to-High threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0x0F, 0x01);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0xF0, 0x0);
	/* @b Low-to-High threshold for WLBB_SEL_RF_ON when OFDM disable (CCK)*/
	odm_set_bb_reg(dm, R_0xabc, 0xFF, 0x06);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON when OFDM disable (CCK) */
	odm_set_bb_reg(dm, R_0xabc, 0xFF00, 0x00);

	/*OFDM HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0x80);
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x00);
	odm_set_bb_reg(dm, R_0xc5c, BIT(20) | BIT(19) | BIT(18), 0x04);

	/*@CCK HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
	odm_set_bb_reg(dm, R_0xaa8, BIT(8), 0);

	odm_set_bb_reg(dm, R_0xa0c, 0x0F, 0xf);
	odm_set_bb_reg(dm, R_0xa14, 0x1F, 0xf);
	odm_set_bb_reg(dm, R_0xa10, BIT(13), 0x1);
	odm_set_bb_reg(dm, R_0xa74, BIT(8), 0x0);
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0x1);
}
void odm_update_rx_idle_ant_8710c(void *dm_void, u8 ant, u32 default_ant,
				  u32 optional_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	void *adapter = dm->adapter;
	
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***odm_update_rx_idle_ant_8710c!!!\n");
	if (dm->ant_div_type == S0S1_SW_ANTDIV) {
		if (default_ant == 0x0)
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x1210,0x800000);
		else
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x1214,0x800000);
		
		fat_tab->rx_idle_ant = ant;
	}else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
		odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), default_ant);
		/*@Default RX*/
		odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), optional_ant);
		/*@Optional RX*/
		odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), default_ant);
		/*@Default TX*/
		fat_tab->rx_idle_ant = ant;
	}
}
#endif

#if (RTL8721D_SUPPORT == 1)

void odm_update_rx_idle_ant_8721d(void *dm_void, u8 ant, u32 default_ant,
				  u32 optional_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), default_ant);
	/*@Default RX*/
	odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), optional_ant);
	/*@Optional RX*/
	odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), default_ant);
	/*@Default TX*/
	fat_tab->rx_idle_ant = ant;
}

void odm_trx_hw_ant_div_init_8721d(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[8721D] AntDiv_Init =>  ant_div_type=[CG_TRX_HW_ANTDIV]\n");

	/*@BT Coexistence*/
	/*@keep antsel_map when GNT_BT = 1*/
	odm_set_bb_reg(dm, R_0x864, BIT(12), 1);
	/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */
	odm_set_bb_reg(dm, R_0x874, BIT(23), 0);
	/* @Disable hw antsw & fast_train.antsw when BT TX/RX */
	odm_set_bb_reg(dm, R_0xe64, 0xFFFF0000, 0x000c);

	switch (dm->antdiv_gpio) {
	case ANTDIV_GPIO_PA2PA4: {
		PAD_CMD(_PA_2, ENABLE);
		Pinmux_Config(_PA_2, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PA_4, ENABLE);
		Pinmux_Config(_PA_4, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PA5PA6: {
		PAD_CMD(_PA_5, ENABLE);
		Pinmux_Config(_PA_5, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PA_6, ENABLE);
		Pinmux_Config(_PA_6, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PA12PA13: {
		PAD_CMD(_PA_12, ENABLE);
		Pinmux_Config(_PA_12, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PA_13, ENABLE);
		Pinmux_Config(_PA_13, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PA14PA15: {
		PAD_CMD(_PA_14, ENABLE);
		Pinmux_Config(_PA_14, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PA_15, ENABLE);
		Pinmux_Config(_PA_15, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PA16PA17: {
		PAD_CMD(_PA_16, ENABLE);
		Pinmux_Config(_PA_16, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PA_17, ENABLE);
		Pinmux_Config(_PA_17, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PB1PB2: {
		PAD_CMD(_PB_1, ENABLE);
		Pinmux_Config(_PB_1, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PB_2, ENABLE);
		Pinmux_Config(_PB_2, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PB26PB29: {
		PAD_CMD(_PB_26, ENABLE);
		Pinmux_Config(_PB_26, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PB_29, ENABLE);
		Pinmux_Config(_PB_29, PINMUX_FUNCTION_RFE);
		break;
	}
	case ANTDIV_GPIO_PB1PB2PB26:{
		PAD_CMD(_PB_1, ENABLE);
		Pinmux_Config(_PB_1, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PB_2, ENABLE);
		Pinmux_Config(_PB_2, PINMUX_FUNCTION_RFE);
		PAD_CMD(_PB_26, ENABLE);
		Pinmux_Config(_PB_26, PINMUX_FUNCTION_RFE);
		break;
	}
	default: {
	}
	}

	if (dm->antdiv_gpio == ANTDIV_GPIO_PA12PA13 ||
	    dm->antdiv_gpio == ANTDIV_GPIO_PA14PA15 ||
	    dm->antdiv_gpio == ANTDIV_GPIO_PA16PA17 ||
	    dm->antdiv_gpio == ANTDIV_GPIO_PB1PB2) {
		/* ANT_SEL_P, ANT_SEL_N */
		odm_set_bb_reg(dm, R_0x930, 0xF, 8);
		odm_set_bb_reg(dm, R_0x930, 0xF0, 8);
		odm_set_bb_reg(dm, R_0x92c, BIT(1) | BIT(0), 2);
		odm_set_bb_reg(dm, R_0x944, 0x00000003, 0x3);
	} else if (dm->antdiv_gpio == ANTDIV_GPIO_PA2PA4 ||
		   dm->antdiv_gpio == ANTDIV_GPIO_PA5PA6 ||
		   dm->antdiv_gpio == ANTDIV_GPIO_PB26PB29) {
		/* TRSW_P, TRSW_N */
		odm_set_bb_reg(dm, R_0x930, 0xF00, 8);
		odm_set_bb_reg(dm, R_0x930, 0xF000, 8);
		odm_set_bb_reg(dm, R_0x92c, BIT(3) | BIT(2), 2);
		odm_set_bb_reg(dm, R_0x944, 0x0000000C, 0x3); 	
	}
	else if(dm->antdiv_gpio == ANTDIV_GPIO_PB1PB2PB26){
              /* 3 antenna diversity for AmebaD only */
		odm_set_bb_reg(dm, R_0x930, 0xF, 8);
		odm_set_bb_reg(dm, R_0x930, 0xF0, 9);
		odm_set_bb_reg(dm, R_0x930, 0xF00,0xa); /* set the RFE control table to select antenna*/
		odm_set_bb_reg(dm, R_0x944, 0x00000007, 0x7);
	}

	u32 sysreg208 = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LP_FUNC_EN0);

	sysreg208 |= BIT(28);
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_LP_FUNC_EN0, sysreg208);

	u32 sysreg344 =
		      HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_AUDIO_SHARE_PAD_CTRL);

	sysreg344 |= BIT(9);
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_AUDIO_SHARE_PAD_CTRL, sysreg344);

	u32 sysreg280 = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LP_SYSPLL_CTRL0);

	sysreg280 |= 0x7;
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_LP_SYSPLL_CTRL0, sysreg280);

	sysreg344 |= BIT(8);
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_AUDIO_SHARE_PAD_CTRL, sysreg344);

	sysreg344 |= BIT(0);
	HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_AUDIO_SHARE_PAD_CTRL, sysreg344);

	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0);
	odm_set_bb_reg(dm, R_0x804, 0xF00, 1); /* r_keep_rfpin */

	/*PTA setting: WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL)*/
	/*odm_set_bb_reg(dm, R_0x948, BIT6, 0);*/
	/*odm_set_bb_reg(dm, R_0x948, BIT8, 0);*/
	/*@GNT_WL tx*/
	odm_set_bb_reg(dm, R_0x950, BIT(29), 0);

	/*@Mapping Table*/
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 1);
	if (dm->antdiv_gpio == ANTDIV_GPIO_PB1PB2PB26) {
		odm_set_bb_reg(dm, R_0x914, 0x00000F, 0x1);
		odm_set_bb_reg(dm, R_0x914, 0x000F00, 0x2);
		odm_set_bb_reg(dm, R_0x914, 0x0F0000, 0x4);
	}
	/* odm_set_bb_reg(dm, R_0x864, BIT5|BIT4|BIT3, 0); */
	/* odm_set_bb_reg(dm, R_0x864, BIT8|BIT7|BIT6, 1); */

	/* Set WLBB_SEL_RF_ON 1 if RXFIR_PWDB > 0xCcc[3:0] */
	odm_set_bb_reg(dm, R_0xccc, BIT(12), 0);
	/* @Low-to-High threshold for WLBB_SEL_RF_ON */
	/*when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0x0F, 0x01);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON */
	/* when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0xF0, 0x0);
	/* @b Low-to-High threshold for WLBB_SEL_RF_ON*/
	/*when OFDM disable ( only CCK ) */
	odm_set_bb_reg(dm, R_0xabc, 0xFF, 0x06);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON*/
	/* when OFDM disable ( only CCK ) */
	odm_set_bb_reg(dm, R_0xabc, 0xFF00, 0x00);

	/*OFDM HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xa0);
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x00);
	odm_set_bb_reg(dm, R_0xc5c, BIT(20) | BIT(19) | BIT(18), 0x04);

	/*@CCK HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 0);
	odm_set_bb_reg(dm, R_0xaa8, BIT(8), 0);

	odm_set_bb_reg(dm, R_0xa0c, 0x0F, 0xf);
	odm_set_bb_reg(dm, R_0xa14, 0x1F, 0x8);
	odm_set_bb_reg(dm, R_0xa10, BIT(13), 0x1);
	odm_set_bb_reg(dm, R_0xa74, BIT(8), 0x0);
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0x1);

	/*@disable antenna training	*/
	odm_set_bb_reg(dm, R_0xe08, BIT(16), 0);
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
}
#endif

void odm_stop_antenna_switch_dm(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	/* @disable ODM antenna diversity */
	dm->support_ability &= ~ODM_BB_ANT_DIV;
#if (RTL8710C_SUPPORT == 1)
	dm->support_ability |= ODM_BB_ANT_DIV;
#endif
	if (fat_tab->div_path_type == ANT_PATH_A)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
	else if (fat_tab->div_path_type == ANT_PATH_B)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_B);
	else if (fat_tab->div_path_type == ANT_PATH_AB)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_AB);
	odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
	PHYDM_DBG(dm, DBG_ANT_DIV, "STOP Antenna Diversity\n");
}

void phydm_enable_antenna_diversity(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	dm->support_ability |= ODM_BB_ANT_DIV;
	dm->antdiv_select = 0;
	PHYDM_DBG(dm, DBG_ANT_DIV, "AntDiv is enabled & Re-Init AntDiv\n");
	odm_antenna_diversity_init(dm);
}

void odm_set_ant_config(void *dm_void, u8 ant_setting /* @0=A, 1=B, 2=C,...*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type == ODM_RTL8723B) {
		if (ant_setting == 0) /* @ant A*/
			odm_set_bb_reg(dm, R_0x948, MASKDWORD, 0x00000000);
		else if (ant_setting == 1)
			odm_set_bb_reg(dm, R_0x948, MASKDWORD, 0x00000280);
	} else if (dm->support_ic_type == ODM_RTL8723D) {
		if (ant_setting == 0) /* @ant A*/
			odm_set_bb_reg(dm, R_0x948, MASKLWORD, 0x0000);
		else if (ant_setting == 1)
			odm_set_bb_reg(dm, R_0x948, MASKLWORD, 0x0280);
	}
}

/* ****************************************************** */

void odm_sw_ant_div_rest_after_link(void *dm_void)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 i;

	if (dm->ant_div_type == S0S1_SW_ANTDIV) {
		swat_tab->try_flag = SWAW_STEP_INIT;
		swat_tab->rssi_trying = 0;
		swat_tab->double_chk_flag = 0;
		fat_tab->rx_idle_ant = MAIN_ANT;

		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
			phydm_antdiv_reset_statistic(dm, i);
	}

#endif
}

void phydm_n_on_off(void *dm_void, u8 swch, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (path == ANT_PATH_A) {
		odm_set_bb_reg(dm, R_0xc50, BIT(7), swch);
	} else if (path == ANT_PATH_B) {
		odm_set_bb_reg(dm, R_0xc58, BIT(7), swch);
	} else if (path == ANT_PATH_AB) {
		odm_set_bb_reg(dm, R_0xc50, BIT(7), swch);
		odm_set_bb_reg(dm, R_0xc58, BIT(7), swch);
	}
	odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
#if (RTL8723D_SUPPORT == 1)
	/*@Mingzhi 2017-05-08*/
	if (dm->support_ic_type == ODM_RTL8723D) {
		if (swch == ANTDIV_ON) {
			odm_set_bb_reg(dm, R_0xce0, BIT(1), 1);
			odm_set_bb_reg(dm, R_0x948, BIT(6), 1);
			/*@1:HW ctrl  0:SW ctrl*/
		} else {
			odm_set_bb_reg(dm, R_0xce0, BIT(1), 0);
			odm_set_bb_reg(dm, R_0x948, BIT(6), 0);
			/*@1:HW ctrl  0:SW ctrl*/
		}
	}
#endif
}

void phydm_ac_on_off(void *dm_void, u8 swch, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (dm->support_ic_type & ODM_RTL8812) {
		odm_set_bb_reg(dm, R_0xc50, BIT(7), swch);
		/* OFDM AntDiv function block enable */
		odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
		/* @CCK AntDiv function block enable */
	} else if (dm->support_ic_type & ODM_RTL8822B) {
		odm_set_bb_reg(dm, R_0x800, BIT(25), swch);
		odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
		if (path == ANT_PATH_A) {
			odm_set_bb_reg(dm, R_0xc50, BIT(7), swch);
		} else if (path == ANT_PATH_B) {
			odm_set_bb_reg(dm, R_0xe50, BIT(7), swch);
		} else if (path == ANT_PATH_AB) {
			odm_set_bb_reg(dm, R_0xc50, BIT(7), swch);
			odm_set_bb_reg(dm, R_0xe50, BIT(7), swch);
		}
	} else {
		odm_set_bb_reg(dm, R_0x8d4, BIT(24), swch);
		/* OFDM AntDiv function block enable */

		if (dm->cut_version >= ODM_CUT_C &&
		    dm->support_ic_type == ODM_RTL8821 &&
		    dm->ant_div_type != S0S1_SW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "(Turn %s) CCK HW-AntDiv\n",
				  (swch == ANTDIV_ON) ? "ON" : "OFF");
			odm_set_bb_reg(dm, R_0x800, BIT(25), swch);
			odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
			/* @CCK AntDiv function block enable */
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV, "(Turn %s) CCK HW-AntDiv\n",
				  (swch == ANTDIV_ON) ? "ON" : "OFF");
			odm_set_bb_reg(dm, R_0x800, BIT(25), swch);
			odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
			/* @CCK AntDiv function block enable */
		}
	}
}

void phydm_jgr3_on_off(void *dm_void, u8 swch, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	odm_set_bb_reg(dm, R_0x8a0, BIT(17), swch);
	/* OFDM AntDiv function block enable */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), swch);
	/* @CCK AntDiv function block enable */
}

void odm_ant_div_on_off(void *dm_void, u8 swch, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (fat_tab->ant_div_on_off != swch) {
		if (dm->ant_div_type == S0S1_SW_ANTDIV)
			return;

		if (dm->support_ic_type & ODM_N_ANTDIV_SUPPORT) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "(( Turn %s )) N-Series HW-AntDiv block\n",
				  (swch == ANTDIV_ON) ? "ON" : "OFF");
			phydm_n_on_off(dm, swch, path);

		} else if (dm->support_ic_type & ODM_AC_ANTDIV_SUPPORT) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "(( Turn %s )) AC-Series HW-AntDiv block\n",
				  (swch == ANTDIV_ON) ? "ON" : "OFF");
			phydm_ac_on_off(dm, swch, path);
		} else if (dm->support_ic_type & ODM_JGR3_ANTDIV_SUPPORT) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "(( Turn %s )) JGR3 HW-AntDiv block\n",
				  (swch == ANTDIV_ON) ? "ON" : "OFF");
			phydm_jgr3_on_off(dm, swch, path);
		}
	}
	fat_tab->ant_div_on_off = swch;
}

void odm_tx_by_tx_desc_or_reg(void *dm_void, u8 swch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 enable;

	if (fat_tab->b_fix_tx_ant == NO_FIX_TX_ANT)
		enable = (swch == TX_BY_DESC) ? 1 : 0;
	else
		enable = 0; /*@Force TX by Reg*/

	if (dm->ant_div_type != CGCS_RX_HW_ANTDIV) {
		if (dm->support_ic_type & ODM_N_ANTDIV_SUPPORT)
			odm_set_bb_reg(dm, R_0x80c, BIT(21), enable);
		else if (dm->support_ic_type & ODM_AC_ANTDIV_SUPPORT)
			odm_set_bb_reg(dm, R_0x900, BIT(18), enable);
		else if (dm->support_ic_type & ODM_JGR3_ANTDIV_SUPPORT)
			odm_set_bb_reg(dm, R_0x186c, BIT(1), enable);

		PHYDM_DBG(dm, DBG_ANT_DIV, "[AntDiv] TX_Ant_BY (( %s ))\n",
			  (enable == TX_BY_DESC) ? "DESC" : "REG");
	}
}

void phydm_antdiv_reset_statistic(void *dm_void, u32 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	fat_tab->main_sum[macid] = 0;
	fat_tab->aux_sum[macid] = 0;
	fat_tab->main_cnt[macid] = 0;
	fat_tab->aux_cnt[macid] = 0;
	fat_tab->main_sum_cck[macid] = 0;
	fat_tab->aux_sum_cck[macid] = 0;
	fat_tab->main_cnt_cck[macid] = 0;
	fat_tab->aux_cnt_cck[macid] = 0;
}

void phydm_fast_training_enable(void *dm_void, u8 swch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 enable;

	if (swch == FAT_ON)
		enable = 1;
	else
		enable = 0;

	PHYDM_DBG(dm, DBG_ANT_DIV, "Fast ant Training_en = ((%d))\n", enable);

	if (dm->support_ic_type == ODM_RTL8188E) {
		odm_set_bb_reg(dm, R_0xe08, BIT(16), enable);
			/*@enable fast training*/
	} else if (dm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(dm, R_0xb34, BIT(28), enable);
			/*@enable fast training (path-A)*/
#if 0
		odm_set_bb_reg(dm, R_0xb34, BIT(29), enable);
			/*enable fast training (path-B)*/
#endif
	} else if (dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8822B)) {
		odm_set_bb_reg(dm, R_0x900, BIT(19), enable);
			/*@enable fast training */
	}
}

void phydm_keep_rx_ack_ant_by_tx_ant_time(void *dm_void, u32 time)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/* Timming issue: keep Rx ant after tx for ACK ( time x 3.2 mu sec)*/
	if (dm->support_ic_type & ODM_N_ANTDIV_SUPPORT)
		odm_set_bb_reg(dm, R_0xe20, 0xf00000, time);
	else if (dm->support_ic_type & ODM_AC_ANTDIV_SUPPORT)
		odm_set_bb_reg(dm, R_0x818, 0xf00000, time);
}

void phydm_update_rx_idle_ac(void *dm_void, u8 ant, u32 default_ant,
			     u32 optional_ant, u32 default_tx_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	u16 value16 = odm_read_2byte(dm, ODM_REG_TRMUX_11AC + 2);
	/* @2014/01/14 MH/Luke.Lee Add direct write for register 0xc0a to  */
	/* @prevnt incorrect 0xc08 bit0-15.We still not know why it is changed*/
	value16 &= ~(BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6) |
		   BIT(5) | BIT(4) | BIT(3));
	value16 |= ((u16)default_ant << 3);
	value16 |= ((u16)optional_ant << 6);
	value16 |= ((u16)default_tx_ant << 9);
	odm_write_2byte(dm, ODM_REG_TRMUX_11AC + 2, value16);
#if 0
	odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC, 0x380000, default_ant);
		/* @Default RX */
	odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC, 0x1c00000, optional_ant);
		/* Optional RX */
	odm_set_bb_reg(dm, ODM_REG_TRMUX_11AC, 0xe000000, default_ant);
		/* @Default TX */
#endif
}

void phydm_update_rx_idle_n(void *dm_void, u8 ant, u32 default_ant,
			    u32 optional_ant, u32 default_tx_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32;

	if (dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8197F)) {
		odm_set_bb_reg(dm, R_0xb38, 0x38, default_ant);
			/* @Default RX */
		odm_set_bb_reg(dm, R_0xb38, 0x1c0, optional_ant);
			/* Optional RX */
		odm_set_bb_reg(dm, R_0x860, 0x7000, default_ant);
			/* @Default TX */
#if (RTL8723B_SUPPORT == 1)
	} else if (dm->support_ic_type == ODM_RTL8723B) {
		value32 = odm_get_bb_reg(dm, R_0x948, 0xFFF);

		if (value32 != 0x280)
			odm_update_rx_idle_ant_8723b(dm, ant, default_ant,
						     optional_ant);
		else
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to 0x948 = 0x280\n");
#endif

#if (RTL8723D_SUPPORT == 1) /*@Mingzhi 2017-05-08*/
	} else if (dm->support_ic_type == ODM_RTL8723D) {
		phydm_set_tx_ant_pwr_8723d(dm, ant);
		odm_update_rx_idle_ant_8723d(dm, ant, default_ant,
					     optional_ant);
#endif

#if (RTL8721D_SUPPORT == 1)
	} else if (dm->support_ic_type == ODM_RTL8721D) {
		odm_update_rx_idle_ant_8721d(dm, ant, default_ant,
					     optional_ant);
#endif

#if (RTL8710C_SUPPORT == 1)
	} else if (dm->support_ic_type == ODM_RTL8710C) {
		odm_update_rx_idle_ant_8710c(dm, ant, default_ant,
					     optional_ant);
#endif

	} else {
/*@8188E & 8188F*/
/*@		if (dm->support_ic_type == ODM_RTL8723D) {*/
/*#if (RTL8723D_SUPPORT == 1)*/
/*			phydm_set_tx_ant_pwr_8723d(dm, ant);*/
/*#endif*/
/*		}*/
#if (RTL8188F_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8188F)
			phydm_update_rx_idle_antenna_8188F(dm, default_ant);
#endif

		odm_set_bb_reg(dm, R_0x864, 0x38, default_ant);/*@Default RX*/
		odm_set_bb_reg(dm, R_0x864, 0x1c0, optional_ant);
			/*Optional RX*/
		odm_set_bb_reg(dm, R_0x860, 0x7000, default_tx_ant);
			/*@Default TX*/
	}
}

void phydm_update_rx_idle_jgr3(void *dm_void, u8 ant, u32 default_ant,
			       u32 optional_ant, u32 default_tx_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32;

	odm_set_bb_reg(dm, R_0x1884, 0xf0, default_ant);/*@Default RX*/
	odm_set_bb_reg(dm, R_0x1884, 0xf00, optional_ant);
		/*Optional RX*/
	odm_set_bb_reg(dm, R_0x1884, 0xf000, default_tx_ant);
		/*@Default TX*/
}
void odm_update_rx_idle_ant(void *dm_void, u8 ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 default_ant, optional_ant, value32, default_tx_ant;

	if (fat_tab->rx_idle_ant != ant) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-ant ] rx_idle_ant =%s\n",
			  (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

		if (!(dm->support_ic_type & ODM_RTL8723B))
			fat_tab->rx_idle_ant = ant;

		if (ant == MAIN_ANT) {
			default_ant = ANT1_2G;
			optional_ant = ANT2_2G;
		} else {
			default_ant = ANT2_2G;
			optional_ant = ANT1_2G;
		}

		if (fat_tab->b_fix_tx_ant != NO_FIX_TX_ANT)
			default_tx_ant = (fat_tab->b_fix_tx_ant ==
					 FIX_TX_AT_MAIN) ? 0 : 1;
		else
			default_tx_ant = default_ant;

		if (dm->support_ic_type & ODM_N_ANTDIV_SUPPORT) {
			phydm_update_rx_idle_n(dm, ant, default_ant,
					       optional_ant, default_tx_ant);
		} else if (dm->support_ic_type & ODM_AC_ANTDIV_SUPPORT) {
			phydm_update_rx_idle_ac(dm, ant, default_ant,
						optional_ant, default_tx_ant);
		} else if (dm->support_ic_type & ODM_JGR3_ANTDIV_SUPPORT) {
			phydm_update_rx_idle_jgr3(dm, ant, default_ant,
						  optional_ant, default_tx_ant);
		}
		/*PathA Resp Tx*/
		if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B |
		    ODM_RTL8814A | ODM_RTL8195B))
			odm_set_mac_reg(dm, R_0x6d8, 0x7, default_tx_ant);
		else if (dm->support_ic_type == ODM_RTL8188E)
			odm_set_mac_reg(dm, R_0x6d8, 0xc0, default_tx_ant);
		else if (dm->support_ic_type & ODM_JGR3_ANTDIV_SUPPORT)
			odm_set_mac_reg(dm, R_0x6f8, 0xf, default_tx_ant);
		else
			odm_set_mac_reg(dm, R_0x6d8, 0x700, default_tx_ant);

	} else { /* @fat_tab->rx_idle_ant == ant */
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Stay in Ori-ant ]  rx_idle_ant =%s\n",
			  (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
		fat_tab->rx_idle_ant = ant;
	}
}

#if (RTL8721D_SUPPORT)
void odm_update_rx_idle_ant_sp3t(void *dm_void, u8 ant) /* added by Jiao Qi on May.25,2020, for AmebaD SP3T only */
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 default_ant, optional_ant, value32, default_tx_ant;

	if (!(dm->support_ic_type & ODM_RTL8723B))
		fat_tab->rx_idle_ant = ant;

		default_ant  = fat_tab->ant_idx_vec[0]-1;
		optional_ant = fat_tab->ant_idx_vec[1]-1;

		if(fat_tab->b_fix_tx_ant != NO_FIX_TX_ANT)
			default_tx_ant = (fat_tab->b_fix_tx_ant ==
					 FIX_TX_AT_MAIN) ? 0 : 1;
		else
			default_tx_ant = default_ant;

		odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), default_ant);
		/*@Default RX*/
		odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), optional_ant);
		/*@Optional RX*/
		odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), default_ant);
		/*@Default TX*/
		
		/*PathA Resp Tx*/
		if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B |
		    ODM_RTL8814A))
			odm_set_mac_reg(dm, R_0x6d8, 0x7, default_tx_ant);
		else if (dm->support_ic_type == ODM_RTL8188E)
			odm_set_mac_reg(dm, R_0x6d8, 0xc0, default_tx_ant);
		else
			odm_set_mac_reg(dm, R_0x6d8, 0x700, default_tx_ant);

}
#endif
void phydm_update_rx_idle_ant_pathb(void *dm_void, u8 ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 default_ant, optional_ant, value32, default_tx_ant;

	if (fat_tab->rx_idle_ant2 != ant) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-ant2 ] rx_idle_ant2 =%s\n",
			  (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
		if (ant == MAIN_ANT) {
			default_ant = ANT1_2G;
			optional_ant = ANT2_2G;
		} else {
			default_ant = ANT2_2G;
			optional_ant = ANT1_2G;
		}

		if (fat_tab->b_fix_tx_ant != NO_FIX_TX_ANT)
			default_tx_ant = (fat_tab->b_fix_tx_ant ==
					  FIX_TX_AT_MAIN) ? 0 : 1;
		else
			default_tx_ant = default_ant;
		if (dm->support_ic_type & ODM_RTL8822B) {
			u16 v16 = odm_read_2byte(dm, ODM_REG_ANT_11AC_B + 2);

			v16 &= ~(0xff8);/*0xE08[11:3]*/
			v16 |= ((u16)default_ant << 3);
			v16 |= ((u16)optional_ant << 6);
			v16 |= ((u16)default_tx_ant << 9);
			odm_write_2byte(dm, ODM_REG_ANT_11AC_B + 2, v16);
			odm_set_mac_reg(dm, R_0x6d8, 0x38, default_tx_ant);
			/*PathB Resp Tx*/
		}
	} else {
		/* fat_tab->rx_idle_ant2 == ant */
		PHYDM_DBG(dm, DBG_ANT_DIV, "[Stay Ori Ant] rx_idle_ant2 = %s\n",
			  (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
		fat_tab->rx_idle_ant2 = ant;
	}
}

void phydm_set_antdiv_val(void *dm_void, u32 *val_buf,	u8 val_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (val_len != 1) {
		PHYDM_DBG(dm, ODM_COMP_API, "[Error][antdiv]Need val_len=1\n");
		return;
	}

	odm_update_rx_idle_ant(dm, (u8)(*val_buf));
}

void odm_update_tx_ant(void *dm_void, u8 ant, u32 mac_id)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 tx_ant;

	if (fat_tab->b_fix_tx_ant != NO_FIX_TX_ANT)
		ant = (fat_tab->b_fix_tx_ant == FIX_TX_AT_MAIN) ?
		       MAIN_ANT : AUX_ANT;

	if (dm->ant_div_type == CG_TRX_SMART_ANTDIV)
		tx_ant = ant;
	else {
		if (ant == MAIN_ANT)
			tx_ant = ANT1_2G;
		else
			tx_ant = ANT2_2G;
	}
#if (RTL8721D_SUPPORT)
	if (dm->antdiv_gpio != ANTDIV_GPIO_PB1PB2PB26) {
		if (ant == MAIN_ANT)
			tx_ant = ANT1_2G;
		else
			tx_ant = ANT2_2G;
		}
	else		
		tx_ant = fat_tab->ant_idx_vec[0]-1;
#endif
	fat_tab->antsel_a[mac_id] = tx_ant & BIT(0);
	fat_tab->antsel_b[mac_id] = (tx_ant & BIT(1)) >> 1;
	fat_tab->antsel_c[mac_id] = (tx_ant & BIT(2)) >> 2;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[Set TX-DESC value]: mac_id:(( %d )),  tx_ant = (( %s ))\n",
		  mac_id, (ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
#if 0
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "antsel_tr_mux=(( 3'b%d%d%d ))\n",
		  fat_tab->antsel_c[mac_id], fat_tab->antsel_b[mac_id],
		  fat_tab->antsel_a[mac_id]);
#endif
}

#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

void odm_bdc_init(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "\n[ BDC Initialization......]\n");
	dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
	dm_bdc_table->bdc_mode = BDC_MODE_NULL;
	dm_bdc_table->bdc_try_flag = 0;
	dm_bdc_table->bd_ccoex_type_wbfer = 0;
	dm->bdc_holdstate = 0xff;

	if (dm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(dm, R_0xd7c, 0x0FFFFFFF, 0x1081008);
		odm_set_bb_reg(dm, R_0xd80, 0x0FFFFFFF, 0);
	} else if (dm->support_ic_type == ODM_RTL8812) {
		odm_set_bb_reg(dm, R_0x9b0, 0x0FFFFFFF, 0x1081008);
			/* @0x9b0[30:0] = 01081008 */
		odm_set_bb_reg(dm, R_0x9b4, 0x0FFFFFFF, 0);
			/* @0x9b4[31:0] = 00000000 */
	}
}

void odm_CSI_on_off(
	void *dm_void,
	u8 CSI_en)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	if (CSI_en == CSI_ON) {
		if (dm->support_ic_type == ODM_RTL8192E)
			odm_set_mac_reg(dm, R_0xd84, BIT(11), 1);
				/* @0xd84[11]=1 */
		else if (dm->support_ic_type == ODM_RTL8812)
			odm_set_mac_reg(dm, R_0x9b0, BIT(31), 1);
				/* @0x9b0[31]=1 */

	} else if (CSI_en == CSI_OFF) {
		if (dm->support_ic_type == ODM_RTL8192E)
			odm_set_mac_reg(dm, R_0xd84, BIT(11), 0);
				/* @0xd84[11]=0 */
		else if (dm->support_ic_type == ODM_RTL8812)
			odm_set_mac_reg(dm, R_0x9b0, BIT(31), 0);
				/* @0x9b0[31]=0 */
	}
}

void odm_bd_ccoex_type_with_bfer_client(
	void *dm_void,
	u8 swch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;
	u8 bd_ccoex_type_wbfer;

	if (swch == DIVON_CSIOFF) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[BDCcoexType: 1] {DIV,CSI} ={1,0}\n");
		bd_ccoex_type_wbfer = 1;

		if (bd_ccoex_type_wbfer != dm_bdc_table->bd_ccoex_type_wbfer) {
			odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
			odm_CSI_on_off(dm, CSI_OFF);
			dm_bdc_table->bd_ccoex_type_wbfer = 1;
		}
	} else if (swch == DIVOFF_CSION) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[BDCcoexType: 2] {DIV,CSI} ={0,1}\n");
		bd_ccoex_type_wbfer = 2;

		if (bd_ccoex_type_wbfer != dm_bdc_table->bd_ccoex_type_wbfer) {
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
			odm_CSI_on_off(dm, CSI_ON);
			dm_bdc_table->bd_ccoex_type_wbfer = 2;
		}
	}
}

void odm_bf_ant_div_mode_arbitration(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;
	u8 current_bdc_mode;

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	PHYDM_DBG(dm, DBG_ANT_DIV, "\n");

	/* @2 mode 1 */
	if (dm_bdc_table->num_txbfee_client != 0 &&
	    dm_bdc_table->num_txbfer_client == 0) {
		current_bdc_mode = BDC_MODE_1;

		if (current_bdc_mode != dm_bdc_table->bdc_mode) {
			dm_bdc_table->bdc_mode = BDC_MODE_1;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
			dm_bdc_table->bdc_rx_idle_update_counter = 1;
			PHYDM_DBG(dm, DBG_ANT_DIV, "Change to (( Mode1 ))\n");
		}

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Antdiv + BF coextance mode] : (( Mode1 ))\n");
	}
	/* @2 mode 2 */
	else if ((dm_bdc_table->num_txbfee_client == 0) &&
		 (dm_bdc_table->num_txbfer_client != 0)) {
		current_bdc_mode = BDC_MODE_2;

		if (current_bdc_mode != dm_bdc_table->bdc_mode) {
			dm_bdc_table->bdc_mode = BDC_MODE_2;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			dm_bdc_table->bdc_try_flag = 0;
			PHYDM_DBG(dm, DBG_ANT_DIV, "Change to (( Mode2 ))\n");
		}
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Antdiv + BF coextance mode] : (( Mode2 ))\n");
	}
	/* @2 mode 3 */
	else if ((dm_bdc_table->num_txbfee_client != 0) &&
		 (dm_bdc_table->num_txbfer_client != 0)) {
		current_bdc_mode = BDC_MODE_3;

		if (current_bdc_mode != dm_bdc_table->bdc_mode) {
			dm_bdc_table->bdc_mode = BDC_MODE_3;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->bdc_rx_idle_update_counter = 1;
			PHYDM_DBG(dm, DBG_ANT_DIV, "Change to (( Mode3 ))\n");
		}

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Antdiv + BF coextance mode] : (( Mode3 ))\n");
	}
	/* @2 mode 4 */
	else if ((dm_bdc_table->num_txbfee_client == 0) &&
		 (dm_bdc_table->num_txbfer_client == 0)) {
		current_bdc_mode = BDC_MODE_4;

		if (current_bdc_mode != dm_bdc_table->bdc_mode) {
			dm_bdc_table->bdc_mode = BDC_MODE_4;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
			PHYDM_DBG(dm, DBG_ANT_DIV, "Change to (( Mode4 ))\n");
		}

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Antdiv + BF coextance mode] : (( Mode4 ))\n");
	}
#endif
}

void odm_div_train_state_setting(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "\n*****[S T A R T ]*****  [2-0. DIV_TRAIN_STATE]\n");
	dm_bdc_table->bdc_try_counter = 2;
	dm_bdc_table->bdc_try_flag = 1;
	dm_bdc_table->BDC_state = bdc_bfer_train_state;
	odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
}

void odm_bd_ccoex_bfee_rx_div_arbitration(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;
	boolean stop_bf_flag;
	u8 bdc_active_mode;

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***{ num_BFee,  num_BFer, num_client}  = (( %d  ,  %d  ,  %d))\n",
		  dm_bdc_table->num_txbfee_client,
		  dm_bdc_table->num_txbfer_client, dm_bdc_table->num_client);
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***{ num_BF_tars,  num_DIV_tars }  = ((  %d  ,  %d ))\n",
		  dm_bdc_table->num_bf_tar, dm_bdc_table->num_div_tar);

	/* @2 [ MIB control ] */
	if (dm->bdc_holdstate == 2) {
		odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
		dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
		PHYDM_DBG(dm, DBG_ANT_DIV, "Force in [ BF STATE]\n");
		return;
	} else if (dm->bdc_holdstate == 1) {
		dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
		odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
		PHYDM_DBG(dm, DBG_ANT_DIV, "Force in [ DIV STATE]\n");
		return;
	}

	/* @------------------------------------------------------------ */

	/* @2 mode 2 & 3 */
	if (dm_bdc_table->bdc_mode == BDC_MODE_2 ||
	    dm_bdc_table->bdc_mode == BDC_MODE_3) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "\n{ Try_flag,  Try_counter } = {  %d , %d  }\n",
			  dm_bdc_table->bdc_try_flag,
			  dm_bdc_table->bdc_try_counter);
		PHYDM_DBG(dm, DBG_ANT_DIV, "BDCcoexType = (( %d ))\n\n",
			  dm_bdc_table->bd_ccoex_type_wbfer);

		/* @All Client have Bfer-Cap------------------------------- */
		if (dm_bdc_table->num_txbfer_client == dm_bdc_table->num_client) {
			/* @BFer STA Only?: yes */
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "BFer STA only?  (( Yes ))\n");
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
			return;
		} else
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "BFer STA only?  (( No ))\n");
		if (dm_bdc_table->is_all_bf_sta_idle == false && dm_bdc_table->is_all_div_sta_idle == true) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "All DIV-STA are idle, but BF-STA not\n");
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->BDC_state = bdc_bfer_train_state;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
			return;
		} else if (dm_bdc_table->is_all_bf_sta_idle == true && dm_bdc_table->is_all_div_sta_idle == false) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "All BF-STA are idle, but DIV-STA not\n");
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
			return;
		}

		/* Select active mode-------------------------------------- */
		if (dm_bdc_table->num_bf_tar == 0) { /* Selsect_1,  Selsect_2 */
			if (dm_bdc_table->num_div_tar == 0) { /* Selsect_3 */
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Select active mode (( 1 ))\n");
				dm_bdc_table->bdc_active_mode = 1;
			} else {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Select active mode  (( 2 ))\n");
				dm_bdc_table->bdc_active_mode = 2;
			}
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
			return;
		} else { /* num_bf_tar > 0 */
			if (dm_bdc_table->num_div_tar == 0) { /* Selsect_3 */
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Select active mode (( 3 ))\n");
				dm_bdc_table->bdc_active_mode = 3;
				dm_bdc_table->bdc_try_flag = 0;
				dm_bdc_table->BDC_state = bdc_bfer_train_state;
				odm_bd_ccoex_type_with_bfer_client(dm,
								   DIVOFF_CSION)
								   ;
				return;
			} else { /* Selsect_4 */
				bdc_active_mode = 4;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Select active mode (( 4 ))\n");

				if (bdc_active_mode != dm_bdc_table->bdc_active_mode) {
					dm_bdc_table->bdc_active_mode = 4;
					PHYDM_DBG(dm, DBG_ANT_DIV, "Change to active mode (( 4 ))  &  return!!!\n");
					return;
				}
			}
		}

#if 1
		if (dm->bdc_holdstate == 0xff) {
			dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
			odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
			PHYDM_DBG(dm, DBG_ANT_DIV, "Force in [ DIV STATE]\n");
			return;
		}
#endif

		/* @Does Client number changed ? ------------------------------- */
		if (dm_bdc_table->num_client != dm_bdc_table->pre_num_client) {
			dm_bdc_table->bdc_try_flag = 0;
			dm_bdc_table->BDC_state = BDC_DIV_TRAIN_STATE;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[  The number of client has been changed !!!]   return to (( BDC_DIV_TRAIN_STATE ))\n");
		}
		dm_bdc_table->pre_num_client = dm_bdc_table->num_client;

		if (dm_bdc_table->bdc_try_flag == 0) {
			/* @2 DIV_TRAIN_STATE (mode 2-0) */
			if (dm_bdc_table->BDC_state == BDC_DIV_TRAIN_STATE)
				odm_div_train_state_setting(dm);
			/* @2 BFer_TRAIN_STATE (mode 2-1) */
			else if (dm_bdc_table->BDC_state == bdc_bfer_train_state) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "*****[2-1. BFer_TRAIN_STATE ]*****\n");

#if 0
				/* @if(dm_bdc_table->num_bf_tar==0) */
				/* @{ */
				/*	PHYDM_DBG(dm,DBG_ANT_DIV, "BF_tars exist?  : (( No )),   [ bdc_bfer_train_state ] >> [BDC_DIV_TRAIN_STATE]\n"); */
				/*	odm_div_train_state_setting( dm); */
				/* @} */
				/* else */ /* num_bf_tar != 0 */
				/* @{ */
#endif
				dm_bdc_table->bdc_try_counter = 2;
				dm_bdc_table->bdc_try_flag = 1;
				dm_bdc_table->BDC_state = BDC_DECISION_STATE;
				odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "BF_tars exist?  : (( Yes )),   [ bdc_bfer_train_state ] >> [BDC_DECISION_STATE]\n");
				/* @} */
			}
			/* @2 DECISION_STATE (mode 2-2) */
			else if (dm_bdc_table->BDC_state == BDC_DECISION_STATE) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "*****[2-2. DECISION_STATE]*****\n");
#if 0
				/* @if(dm_bdc_table->num_bf_tar==0) */
				/* @{ */
				/*	ODM_AntDiv_Printk(("BF_tars exist?  : (( No )),   [ DECISION_STATE ] >> [BDC_DIV_TRAIN_STATE]\n")); */
				/*	odm_div_train_state_setting( dm); */
				/* @} */
				/* else */ /* num_bf_tar != 0 */
				/* @{ */
#endif
				if (dm_bdc_table->BF_pass == false || dm_bdc_table->DIV_pass == false)
					stop_bf_flag = true;
				else
					stop_bf_flag = false;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "BF_tars exist?  : (( Yes )),  {BF_pass, DIV_pass, stop_bf_flag }  = { %d, %d, %d }\n",
					  dm_bdc_table->BF_pass,
					  dm_bdc_table->DIV_pass, stop_bf_flag);

				if (stop_bf_flag == true) { /* @DIV_en */
					dm_bdc_table->bdc_hold_counter = 10; /* @20 */
					odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
					dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ stop_bf_flag= ((true)),   BDC_DECISION_STATE ] >> [BDC_DIV_HOLD_STATE]\n");
				} else { /* @BF_en */
					dm_bdc_table->bdc_hold_counter = 10; /* @20 */
					odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
					dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
					PHYDM_DBG(dm, DBG_ANT_DIV, "[stop_bf_flag= ((false)),   BDC_DECISION_STATE ] >> [BDC_BF_HOLD_STATE]\n");
				}
				/* @} */
			}
			/* @2 BF-HOLD_STATE (mode 2-3) */
			else if (dm_bdc_table->BDC_state == BDC_BF_HOLD_STATE) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "*****[2-3. BF_HOLD_STATE ]*****\n");

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "bdc_hold_counter = (( %d ))\n",
					  dm_bdc_table->bdc_hold_counter);

				if (dm_bdc_table->bdc_hold_counter == 1) {
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ BDC_BF_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n");
					odm_div_train_state_setting(dm);
				} else {
					dm_bdc_table->bdc_hold_counter--;

#if 0
					/* @if(dm_bdc_table->num_bf_tar==0) */
					/* @{ */
					/*	PHYDM_DBG(dm,DBG_ANT_DIV, "BF_tars exist?  : (( No )),   [ BDC_BF_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n"); */
					/*	odm_div_train_state_setting( dm); */
					/* @} */
					/* else */ /* num_bf_tar != 0 */
					/* @{ */
					/* PHYDM_DBG(dm,DBG_ANT_DIV, "BF_tars exist?  : (( Yes ))\n"); */
#endif
					dm_bdc_table->BDC_state = BDC_BF_HOLD_STATE;
					odm_bd_ccoex_type_with_bfer_client(dm, DIVOFF_CSION);
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ BDC_BF_HOLD_STATE ] >> [BDC_BF_HOLD_STATE]\n");
					/* @} */
				}
			}
			/* @2 DIV-HOLD_STATE (mode 2-4) */
			else if (dm_bdc_table->BDC_state == BDC_DIV_HOLD_STATE) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "*****[2-4. DIV_HOLD_STATE ]*****\n");

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "bdc_hold_counter = (( %d ))\n",
					  dm_bdc_table->bdc_hold_counter);

				if (dm_bdc_table->bdc_hold_counter == 1) {
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ BDC_DIV_HOLD_STATE ] >> [BDC_DIV_TRAIN_STATE]\n");
					odm_div_train_state_setting(dm);
				} else {
					dm_bdc_table->bdc_hold_counter--;
					dm_bdc_table->BDC_state = BDC_DIV_HOLD_STATE;
					odm_bd_ccoex_type_with_bfer_client(dm, DIVON_CSIOFF);
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ BDC_DIV_HOLD_STATE ] >> [BDC_DIV_HOLD_STATE]\n");
				}
			}

		} else if (dm_bdc_table->bdc_try_flag == 1) {
			/* @2 Set Training counter */
			if (dm_bdc_table->bdc_try_counter > 1) {
				dm_bdc_table->bdc_try_counter--;
				if (dm_bdc_table->bdc_try_counter == 1)
					dm_bdc_table->bdc_try_flag = 0;

				PHYDM_DBG(dm, DBG_ANT_DIV, "Training !!\n");
				/* return ; */
			}
		}
	}

	PHYDM_DBG(dm, DBG_ANT_DIV, "\n[end]\n");

#endif /* @#if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */
}

#endif
#endif /* @#ifdef PHYDM_BEAMFORMING_SUPPORT*/

#if (RTL8188E_SUPPORT == 1)

void odm_rx_hw_ant_div_init_88e(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* @MAC setting */
	value32 = odm_get_mac_reg(dm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD);
	odm_set_mac_reg(dm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD,
			value32 | (BIT(23) | BIT(25)));
			/* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	odm_set_bb_reg(dm, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
			/* reg870[8]=1'b0, reg870[9]=1'b0 */
			/* antsel antselb by HW */
	odm_set_bb_reg(dm, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
			/* reg864[10]=1'b0 */ /* antsel2 by HW */
	odm_set_bb_reg(dm, ODM_REG_LNA_SWITCH_11N, BIT(22), 1);
			/* regb2c[22]=1'b0 */ /* disable CS/CG switch */
	odm_set_bb_reg(dm, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);
			/* regb2c[31]=1'b1 */ /* output at CG only */
	/* OFDM Settings */
	odm_set_bb_reg(dm, ODM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/* @CCK Settings */
	odm_set_bb_reg(dm, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
			/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
			/* @CCK complete HW AntDiv within 64 samples */

	odm_set_bb_reg(dm, ODM_REG_ANT_MAPPING1_11N, 0xFFFF, 0x0001);
			/* @antenna mapping table */

	fat_tab->enable_ctrl_frame_antdiv = 1;
}

void odm_trx_hw_ant_div_init_88e(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;


	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* @MAC setting */
	value32 = odm_get_mac_reg(dm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD);
	odm_set_mac_reg(dm, ODM_REG_ANTSEL_PIN_11N, MASKDWORD,
			value32 | (BIT(23) | BIT(25)));
			/* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	/* Pin Settings */
	odm_set_bb_reg(dm, ODM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
			/* reg870[8]=1'b0, reg870[9]=1'b0 */
			/* antsel antselb by HW */
	odm_set_bb_reg(dm, ODM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
			/* reg864[10]=1'b0 */ /* antsel2 by HW */
	odm_set_bb_reg(dm, ODM_REG_LNA_SWITCH_11N, BIT(22), 0);
			/* regb2c[22]=1'b0 */ /* disable CS/CG switch */
	odm_set_bb_reg(dm, ODM_REG_LNA_SWITCH_11N, BIT(31), 1);
			/* regb2c[31]=1'b1 */ /* output at CG only */
	/* OFDM Settings */
	odm_set_bb_reg(dm, ODM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/* @CCK Settings */
	odm_set_bb_reg(dm, ODM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
			/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, ODM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
			/* @CCK complete HW AntDiv within 64 samples */

	/* @antenna mapping table */
	if (!dm->is_mp_chip) { /* testchip */
		odm_set_bb_reg(dm, ODM_REG_RX_DEFAULT_A_11N, 0x700, 1);
				/* Reg858[10:8]=3'b001 */
		odm_set_bb_reg(dm, ODM_REG_RX_DEFAULT_A_11N, 0x3800, 2);
				/* Reg858[13:11]=3'b010 */
	} else /* @MPchip */
		odm_set_bb_reg(dm, ODM_REG_ANT_MAPPING1_11N, MASKDWORD, 0x0201);
				/*Reg914=3'b010, Reg915=3'b001*/

	fat_tab->enable_ctrl_frame_antdiv = 1;
}

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
void odm_smart_hw_ant_div_init_88e(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 value32, i;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8188E AntDiv_Init =>  ant_div_type=[CG_TRX_SMART_ANTDIV]\n");

#if 0
	if (*dm->mp_mode == true) {
		PHYDM_DBG(dm, ODM_COMP_INIT, "dm->ant_div_type: %d\n",
			  dm->ant_div_type);
		return;
	}
#endif

	fat_tab->train_idx = 0;
	fat_tab->fat_state = FAT_PREPARE_STATE;

	dm->fat_comb_a = 5;
	dm->antdiv_intvl = 0x64; /* @100ms */

	for (i = 0; i < 6; i++)
		fat_tab->bssid[i] = 0;
	for (i = 0; i < (dm->fat_comb_a); i++) {
		fat_tab->ant_sum_rssi[i] = 0;
		fat_tab->ant_rssi_cnt[i] = 0;
		fat_tab->ant_ave_rssi[i] = 0;
	}

	/* @MAC setting */
	value32 = odm_get_mac_reg(dm, R_0x4c, MASKDWORD);
	odm_set_mac_reg(dm, R_0x4c, MASKDWORD, value32 | (BIT(23) | BIT(25))); /* Reg4C[25]=1, Reg4C[23]=1 for pin output */
	value32 = odm_get_mac_reg(dm, R_0x7b4, MASKDWORD);
	odm_set_mac_reg(dm, R_0x7b4, MASKDWORD, value32 | (BIT(16) | BIT(17))); /* Reg7B4[16]=1 enable antenna training, Reg7B4[17]=1 enable A2 match */
	/* value32 = platform_efio_read_4byte(adapter, 0x7B4); */
	/* platform_efio_write_4byte(adapter, 0x7b4, value32|BIT(18));	 */ /* append MACID in reponse packet */

	/* @Match MAC ADDR */
	odm_set_mac_reg(dm, R_0x7b4, 0xFFFF, 0);
	odm_set_mac_reg(dm, R_0x7b0, MASKDWORD, 0);

	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0); /* reg870[8]=1'b0, reg870[9]=1'b0		 */ /* antsel antselb by HW */
	odm_set_bb_reg(dm, R_0x864, BIT(10), 0); /* reg864[10]=1'b0	 */ /* antsel2 by HW */
	odm_set_bb_reg(dm, R_0xb2c, BIT(22), 0); /* regb2c[22]=1'b0	 */ /* disable CS/CG switch */
	odm_set_bb_reg(dm, R_0xb2c, BIT(31), 0); /* regb2c[31]=1'b1	 */ /* output at CS only */
	odm_set_bb_reg(dm, R_0xca4, MASKDWORD, 0x000000a0);

	/* @antenna mapping table */
	if (dm->fat_comb_a == 2) {
		if (!dm->is_mp_chip) { /* testchip */
			odm_set_bb_reg(dm, R_0x858, BIT(10) | BIT(9) | BIT(8), 1); /* Reg858[10:8]=3'b001 */
			odm_set_bb_reg(dm, R_0x858, BIT(13) | BIT(12) | BIT(11), 2); /* Reg858[13:11]=3'b010 */
		} else { /* @MPchip */
			odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 1);
			odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 2);
		}
	} else {
		if (!dm->is_mp_chip) { /* testchip */
			odm_set_bb_reg(dm, R_0x858, BIT(10) | BIT(9) | BIT(8), 0); /* Reg858[10:8]=3'b000 */
			odm_set_bb_reg(dm, R_0x858, BIT(13) | BIT(12) | BIT(11), 1); /* Reg858[13:11]=3'b001 */
			odm_set_bb_reg(dm, R_0x878, BIT(16), 0);
			odm_set_bb_reg(dm, R_0x858, BIT(15) | BIT(14), 2); /* @(Reg878[0],Reg858[14:15])=3'b010 */
			odm_set_bb_reg(dm, R_0x878, BIT(19) | BIT(18) | BIT(17), 3); /* Reg878[3:1]=3b'011 */
			odm_set_bb_reg(dm, R_0x878, BIT(22) | BIT(21) | BIT(20), 4); /* Reg878[6:4]=3b'100 */
			odm_set_bb_reg(dm, R_0x878, BIT(25) | BIT(24) | BIT(23), 5); /* Reg878[9:7]=3b'101 */
			odm_set_bb_reg(dm, R_0x878, BIT(28) | BIT(27) | BIT(26), 6); /* Reg878[12:10]=3b'110 */
			odm_set_bb_reg(dm, R_0x878, BIT(31) | BIT(30) | BIT(29), 7); /* Reg878[15:13]=3b'111 */
		} else { /* @MPchip */
			odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 4); /* @0: 3b'000 */
			odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 2); /* @1: 3b'001 */
			odm_set_bb_reg(dm, R_0x914, MASKBYTE2, 0); /* @2: 3b'010 */
			odm_set_bb_reg(dm, R_0x914, MASKBYTE3, 1); /* @3: 3b'011 */
			odm_set_bb_reg(dm, R_0x918, MASKBYTE0, 3); /* @4: 3b'100 */
			odm_set_bb_reg(dm, R_0x918, MASKBYTE1, 5); /* @5: 3b'101 */
			odm_set_bb_reg(dm, R_0x918, MASKBYTE2, 6); /* @6: 3b'110 */
			odm_set_bb_reg(dm, R_0x918, MASKBYTE3, 255); /* @7: 3b'111 */
		}
	}

	/* @Default ant setting when no fast training */
	odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), 0); /* @Default RX */
	odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), 1); /* Optional RX */
	odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), 0); /* @Default TX */

	/* @Enter Traing state */
	odm_set_bb_reg(dm, R_0x864, BIT(2) | BIT(1) | BIT(0), (dm->fat_comb_a - 1)); /* reg864[2:0]=3'd6	 */ /* ant combination=reg864[2:0]+1 */

#if 0
	/* SW Control */
	/* phy_set_bb_reg(adapter, 0x864, BIT10, 1); */
	/* phy_set_bb_reg(adapter, 0x870, BIT9, 1); */
	/* phy_set_bb_reg(adapter, 0x870, BIT8, 1); */
	/* phy_set_bb_reg(adapter, 0x864, BIT11, 1); */
	/* phy_set_bb_reg(adapter, 0x860, BIT9, 0); */
	/* phy_set_bb_reg(adapter, 0x860, BIT8, 0); */
#endif
}
#endif

#endif /* @#if (RTL8188E_SUPPORT == 1) */

#if (RTL8192E_SUPPORT == 1)
void odm_rx_hw_ant_div_init_92e(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

#if 0
	if (*dm->mp_mode == true) {
		odm_ant_div_on_off(dm, ANTDIV_OFF);
		odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
		/* r_rxdiv_enable_anta  regc50[8]=1'b0  0: control by c50[9] */
		odm_set_bb_reg(dm, R_0xc50, BIT(9), 1);
		/* @1:CG, 0:CS */
		return;
	}
#endif

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0x870, BIT(8), 0);
		/* reg870[8]=1'b0,   antsel is controled by HWs */
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 1);
		/* regc50[8]=1'b1    CS/CG switching is controled by HWs*/

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x914, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x0); /* @bias */

	/* @CCK Settings */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
		/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0);
		/* @(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}

void odm_trx_hw_ant_div_init_92e(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if 0
	if (*dm->mp_mode == true) {
		odm_ant_div_on_off(dm, ANTDIV_OFF);
		odm_set_bb_reg(dm, R_0xc50, BIT(8), 0); /* r_rxdiv_enable_anta  regc50[8]=1'b0  0: control by c50[9] */
		odm_set_bb_reg(dm, R_0xc50, BIT(9), 1);  /* @1:CG, 0:CS */
		return;
	}
#endif

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* @3 --RFE pin setting--------- */
	/* @[MAC] */
	odm_set_mac_reg(dm, R_0x38, BIT(11), 1);
		/* @DBG PAD Driving control (GPIO 8) */
	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0); /* path-A, RFE_CTRL_3 */
	odm_set_mac_reg(dm, R_0x4c, BIT(29), 1); /* path-A, RFE_CTRL_8 */
	/* @[BB] */
	odm_set_bb_reg(dm, R_0x944, BIT(3), 1); /* RFE_buffer */
	odm_set_bb_reg(dm, R_0x944, BIT(8), 1);
	odm_set_bb_reg(dm, R_0x940, BIT(7) | BIT(6), 0x0);
		/* r_rfe_path_sel_   (RFE_CTRL_3) */
	odm_set_bb_reg(dm, R_0x940, BIT(17) | BIT(16), 0x0);
		/* r_rfe_path_sel_   (RFE_CTRL_8) */
	odm_set_bb_reg(dm, R_0x944, BIT(31), 0); /* RFE_buffer */
	odm_set_bb_reg(dm, R_0x92c, BIT(3), 0); /* rfe_inv  (RFE_CTRL_3) */
	odm_set_bb_reg(dm, R_0x92c, BIT(8), 1); /* rfe_inv  (RFE_CTRL_8) */
	odm_set_bb_reg(dm, R_0x930, 0xF000, 0x8); /* path-A, RFE_CTRL_3 */
	odm_set_bb_reg(dm, R_0x934, 0xF, 0x8); /* path-A, RFE_CTRL_8 */
	/* @3 ------------------------- */

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
		/* path-A  */ /* disable CS/CG switch */

#if 0
	/* @Let it follows PHY_REG for bit9 setting */
	if (dm->priv->pshare->rf_ft_var.use_ext_pa ||
	    dm->priv->pshare->rf_ft_var.use_ext_lna)
		odm_set_bb_reg(dm, R_0xc50, BIT(9), 1);/* path-A output at CS */
	else
		odm_set_bb_reg(dm, R_0xc50, BIT(9), 0);
			/* path-A output at CG ->normal power */
#endif

	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0);
		/* path-A*/ /* antsel antselb by HW */
	odm_set_bb_reg(dm, R_0xb38, BIT(10), 0);/* path-A*/ /* antsel2 by HW */

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x914, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x0); /* @bias */

	/* @CCK Settings */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
		/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0);
		/* @(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
void odm_smart_hw_ant_div_init_92e(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8192E AntDiv_Init =>  ant_div_type=[CG_TRX_SMART_ANTDIV]\n");
}
#endif

#endif /* @#if (RTL8192E_SUPPORT == 1) */

#if (RTL8192F_SUPPORT == 1)
void odm_rx_hw_ant_div_init_92f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0x870, BIT(8), 0);
		/* reg870[8]=1'b0, "antsel" is controlled by HWs */
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 1);
		/* regc50[8]=1'b1, " CS/CG switching" is controlled by HWs */

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x914, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x0); /* @bias */

	/* @CCK Settings */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
		/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0);
		/* @(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}

void odm_trx_hw_ant_div_init_92f(void *dm_void)

{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);
	/* @3 --RFE pin setting--------- */
	/* @[MAC] */
	odm_set_mac_reg(dm, R_0x1048, BIT(0), 1);
		/* @DBG PAD Driving control (gpioA_0) */
	odm_set_mac_reg(dm, R_0x1048, BIT(1), 1);
		/* @DBG PAD Driving control (gpioA_1) */
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1);
	odm_set_mac_reg(dm, R_0x1038, BIT(25) | BIT(24) | BIT(23), 0);
		/* @gpioA_0,gpioA_1*/
	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);
	/* @[BB] */
	odm_set_bb_reg(dm, R_0x944, BIT(8), 1); /* output enable */
	odm_set_bb_reg(dm, R_0x944, BIT(9), 1);
	odm_set_bb_reg(dm, R_0x940, BIT(16) | BIT(17), 0x0);
		/* r_rfe_path_sel_   (RFE_CTRL_8) */
	odm_set_bb_reg(dm, R_0x940, BIT(18) | BIT(19), 0x0);
		/* r_rfe_path_sel_   (RFE_CTRL_9) */
	odm_set_bb_reg(dm, R_0x944, BIT(31), 0); /* RFE_buffer_en */
	odm_set_bb_reg(dm, R_0x92c, BIT(8), 0); /* rfe_inv  (RFE_CTRL_8) */
	odm_set_bb_reg(dm, R_0x92c, BIT(9), 1); /* rfe_inv  (RFE_CTRL_9) */
	odm_set_bb_reg(dm, R_0x934, 0xF, 0x8); /* path-A, RFE_CTRL_8 */
	odm_set_bb_reg(dm, R_0x934, 0xF0, 0x8); /* path-A, RFE_CTRL_9 */
	/* @3 ------------------------- */

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
		/* path-A,disable CS/CG switch */
	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0);
		/* path-A*, antsel antselb by HW */
	odm_set_bb_reg(dm, R_0xb38, BIT(10), 0); /* path-A ,antsel2 by HW */

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x914, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x0); /* @bias */

	/* @CCK Settings */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
		/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0);
		/* @(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}

#endif /* @#if (RTL8192F_SUPPORT == 1) */

#if (RTL8822B_SUPPORT == 1)
void phydm_trx_hw_ant_div_init_22b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0xcb8, BIT(21) | BIT(20), 0x1);
	odm_set_bb_reg(dm, R_0xcb8, BIT(23) | BIT(22), 0x1);
	odm_set_bb_reg(dm, R_0xc1c, BIT(7) | BIT(6), 0x0);
	/* @------------------------- */

	/* @Mapping table */
	/* @antenna mapping table */
	odm_set_bb_reg(dm, R_0xca4, 0xFFFF, 0x0100);

	/* OFDM Settings */
	/* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0);
	/* @bias */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x0);
	odm_set_bb_reg(dm, R_0x668, BIT(3), 0x1);

	/* @CCK Settings */
	/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
	/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
	/* @CCK complete HW AntDiv within 64 samples */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
	/* @BT Coexistence */
	/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
	/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);
#if (defined(CONFIG_2T4R_ANTENNA))
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8822B AntDiv_Init =>  2T4R case\n");
	/* Pin Settings */
	odm_set_bb_reg(dm, R_0xeb8, BIT(21) | BIT(20), 0x1);
	odm_set_bb_reg(dm, R_0xeb8, BIT(23) | BIT(22), 0x1);
	odm_set_bb_reg(dm, R_0xe1c, BIT(7) | BIT(6), 0x0);
	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xeac, BIT(9), 1);
	/* @keep antsel_map when GNT_BT = 1 */
	/* Mapping table */
	/* antenna mapping table */
	odm_set_bb_reg(dm, R_0xea4, 0xFFFF, 0x0100);
	/*odm_set_bb_reg(dm, R_0x900, 0x30000, 0x3);*/
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}
#endif /* @#if (RTL8822B_SUPPORT == 1) */

#if (RTL8197F_SUPPORT == 1)
void phydm_rx_hw_ant_div_init_97f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

#if 0
	if (*dm->mp_mode == true) {
		odm_ant_div_on_off(dm, ANTDIV_OFF);
		odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
		/* r_rxdiv_enable_anta  regc50[8]=1'b0  0: control by c50[9] */
		odm_set_bb_reg(dm, R_0xc50, BIT(9), 1);  /* @1:CG, 0:CS */
		return;
	}
#endif
	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0x870, BIT(8), 0);
		/* reg870[8]=1'b0, */ /* "antsel" is controlled by HWs */
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 1);
		/* regc50[8]=1'b1 *//*"CS/CG switching" is controlled by HWs */

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x914, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x0); /* @bias */

	/* @CCK Settings */
	odm_set_bb_reg(dm, R_0xa04, 0xF000000, 0);
		/* Select which path to receive for CCK_1 & CCK_2 */
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0);
		/* @(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0 */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* @Fix CCK PHY status report issue */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @CCK complete HW AntDiv within 64 samples */

#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}
#endif //#if (RTL8197F_SUPPORT == 1)

#if (RTL8197G_SUPPORT == 1)
void phydm_rx_hw_ant_div_init_97g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Pin Settings */
	odm_set_bb_reg(dm, R_0x1884, BIT(23), 0);
		/* reg1844[23]=1'b0 *//*"CS/CG switching" is controlled by HWs*/
	odm_set_bb_reg(dm, R_0x1884, BIT(16), 1);
		/* reg1844[16]=1'b1 *//*"antsel" is controlled by HWs*/

	/* @Mapping table */
	odm_set_bb_reg(dm, R_0x1870, 0xFFFF, 0x0100);
		/* @antenna mapping table */

	/* OFDM Settings */
	odm_set_bb_reg(dm, R_0x1938, 0xFFE0, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x1938, 0x7FF0000, 0x0); /* @bias */


#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_evm_sw_antdiv_init(dm);
#endif
}
#endif //#if (RTL8197F_SUPPORT == 1)

#if (RTL8723D_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8723d(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/*@BT Coexistence*/
	/*@keep antsel_map when GNT_BT = 1*/
	odm_set_bb_reg(dm, R_0x864, BIT(12), 1);
	/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */
	odm_set_bb_reg(dm, R_0x874, BIT(23), 0);
	/* @Disable hw antsw & fast_train.antsw when BT TX/RX */
	odm_set_bb_reg(dm, R_0xe64, 0xFFFF0000, 0x000c);

	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0);
#if 0
	/*PTA setting: WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL)*/
	/*odm_set_bb_reg(dm, R_0x948, BIT6, 0);*/
	/*odm_set_bb_reg(dm, R_0x948, BIT8, 0);*/
#endif
	/*@GNT_WL tx*/
	odm_set_bb_reg(dm, R_0x950, BIT(29), 0);

	/*@Mapping Table*/
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 3);
#if 0
	/* odm_set_bb_reg(dm, R_0x864, BIT5|BIT4|BIT3, 0); */
	/* odm_set_bb_reg(dm, R_0x864, BIT8|BIT7|BIT6, 1); */
#endif

	/* Set WLBB_SEL_RF_ON 1 if RXFIR_PWDB > 0xCcc[3:0] */
	odm_set_bb_reg(dm, R_0xccc, BIT(12), 0);
	/* @Low-to-High threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0x0F, 0x01);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON when OFDM enable */
	odm_set_bb_reg(dm, R_0xccc, 0xF0, 0x0);
	/* @b Low-to-High threshold for WLBB_SEL_RF_ON when OFDM disable (CCK)*/
	odm_set_bb_reg(dm, R_0xabc, 0xFF, 0x06);
	/* @High-to-Low threshold for WLBB_SEL_RF_ON when OFDM disable (CCK) */
	odm_set_bb_reg(dm, R_0xabc, 0xFF00, 0x00);

	/*OFDM HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xa0);
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x00);
	odm_set_bb_reg(dm, R_0xc5c, BIT(20) | BIT(19) | BIT(18), 0x04);

	/*@CCK HW AntDiv Parameters*/
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
	odm_set_bb_reg(dm, R_0xaa8, BIT(8), 0);

	odm_set_bb_reg(dm, R_0xa0c, 0x0F, 0xf);
	odm_set_bb_reg(dm, R_0xa14, 0x1F, 0x8);
	odm_set_bb_reg(dm, R_0xa10, BIT(13), 0x1);
	odm_set_bb_reg(dm, R_0xa74, BIT(8), 0x0);
	odm_set_bb_reg(dm, R_0xb34, BIT(30), 0x1);

	/*@disable antenna training	*/
	odm_set_bb_reg(dm, R_0xe08, BIT(16), 0);
	odm_set_bb_reg(dm, R_0xc50, BIT(8), 0);
}
/*@Mingzhi 2017-05-08*/

void odm_s0s1_sw_ant_div_init_8723d(void *dm_void)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch	*swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct		*fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8723D AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n");

	/*@keep antsel_map when GNT_BT = 1*/
	odm_set_bb_reg(dm, R_0x864, BIT(12), 1);

	/* @Disable antsw when GNT_BT=1 */
	odm_set_bb_reg(dm, R_0x874, BIT(23), 0);

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 1);

	/* Output Pin Settings */
#if 0
	/* odm_set_bb_reg(dm, R_0x948, BIT6, 0x1); */
#endif
	odm_set_bb_reg(dm, R_0x870, BIT(8), 1);
	odm_set_bb_reg(dm, R_0x870, BIT(9), 1);

	/* Status init */
	fat_tab->is_become_linked  = false;
	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;
	swat_tab->cur_antenna = MAIN_ANT;
	swat_tab->pre_ant = MAIN_ANT;
	dm->antdiv_counter = CONFIG_ANTDIV_PERIOD;

	/* @2 [--For HW Bug setting] */
	odm_set_bb_reg(dm, R_0x80c, BIT(21), 0); /* TX ant  by Reg */
}

void odm_update_rx_idle_ant_8723d(void *dm_void, u8 ant, u32 default_ant,
				  u32 optional_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	void *adapter = dm->adapter;
	u8 count = 0;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	/*score board to BT ,a002:WL to do ant-div*/
	odm_set_mac_reg(dm, R_0xa8, MASKHWORD, 0xa002);
	ODM_delay_us(50);
#endif
#if 0
	/*	odm_set_bb_reg(dm, R_0x948, BIT(6), 0x1);	*/
#endif
	if (dm->ant_div_type == S0S1_SW_ANTDIV) {
	odm_set_bb_reg(dm, R_0x860, BIT(8), default_ant);
	odm_set_bb_reg(dm, R_0x860, BIT(9), default_ant);
	}
	odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), default_ant);
		/*@Default RX*/
	odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), optional_ant);
		/*Optional RX*/
	odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), default_ant);
		/*@Default TX*/
	fat_tab->rx_idle_ant = ant;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	/*score board to BT ,a000:WL@S1 a001:WL@S0*/
	if (default_ant == ANT1_2G)
		odm_set_mac_reg(dm, R_0xa8, MASKHWORD, 0xa000);
	else
		odm_set_mac_reg(dm, R_0xa8, MASKHWORD, 0xa001);
#endif
}

void phydm_set_tx_ant_pwr_8723d(void *dm_void, u8 ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	void *adapter = dm->adapter;

	fat_tab->rx_idle_ant = ant;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	((PADAPTER)adapter)->HalFunc.SetTxPowerLevelHandler(adapter, *dm->channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	rtw_hal_set_tx_power_level(adapter, *dm->channel);
#endif
}
#endif

#if (RTL8723B_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8723b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8723B AntDiv_Init =>  ant_div_type=[CG_TRX_HW_ANTDIV(DPDT)]\n");

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF, 0xa0); /* thershold */
	odm_set_bb_reg(dm, R_0xca4, 0x7FF000, 0x00); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1);
		/* @do 64 samples */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0x864, BIT(12), 0);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x874, BIT(23), 0);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	/* Output Pin Settings */
	odm_set_bb_reg(dm, R_0x870, BIT(8), 0);

	odm_set_bb_reg(dm, R_0x948, BIT(6), 0);
		/* WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL) */
	odm_set_bb_reg(dm, R_0x948, BIT(7), 0);

	odm_set_mac_reg(dm, R_0x40, BIT(3), 1);
	odm_set_mac_reg(dm, R_0x38, BIT(11), 1);
	odm_set_mac_reg(dm, R_0x4c, BIT(24) | BIT(23), 2);
		/* select DPDT_P and DPDT_N as output pin */

	odm_set_bb_reg(dm, R_0x944, BIT(0) | BIT(1), 3); /* @in/out */
	odm_set_bb_reg(dm, R_0x944, BIT(31), 0);

	odm_set_bb_reg(dm, R_0x92c, BIT(1), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0x92c, BIT(0), 1); /* @DPDT_N inverse */

	odm_set_bb_reg(dm, R_0x930, 0xF0, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0x930, 0xF, 8); /* @DPDT_N = ANTSEL[0] */

	/* @2 [--For HW Bug setting] */
	if (dm->ant_type == ODM_AUTO_ANT)
		odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
			/* @CCK AntDiv function block enable */
}

void odm_s0s1_sw_ant_div_init_8723b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "***8723B AntDiv_Init => ant_div_type=[ S0S1_SW_AntDiv]\n");

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0x914, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0x914, MASKBYTE1, 1);

#if 0
	/* Output Pin Settings */
	/* odm_set_bb_reg(dm, R_0x948, BIT6, 0x1); */
#endif
	odm_set_bb_reg(dm, R_0x870, BIT(9) | BIT(8), 0);

	fat_tab->is_become_linked = false;
	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;

	/* @2 [--For HW Bug setting] */
	odm_set_bb_reg(dm, R_0x80c, BIT(21), 0); /* TX ant  by Reg */
}

void odm_update_rx_idle_ant_8723b(
	void *dm_void,
	u8 ant,
	u32 default_ant,
	u32 optional_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	void *adapter = dm->adapter;
	u8 count = 0;
	/*u8			u1_temp;*/
	/*u8			h2c_parameter;*/

	if (!dm->is_linked && dm->ant_type == ODM_AUTO_ANT) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to no link\n");
		return;
	}

#if 0
	/* Send H2C command to FW */
	/* @Enable wifi calibration */
	h2c_parameter = true;
	odm_fill_h2c_cmd(dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);

	/* @Check if H2C command sucess or not (0x1e6) */
	u1_temp = odm_read_1byte(dm, 0x1e6);
	while ((u1_temp != 0x1) && (count < 100)) {
		ODM_delay_us(10);
		u1_temp = odm_read_1byte(dm, 0x1e6);
		count++;
	}
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Update Rx-Idle-ant ] 8723B: H2C command status = %d, count = %d\n",
		  u1_temp, count);

	if (u1_temp == 0x1) {
		/* @Check if BT is doing IQK (0x1e7) */
		count = 0;
		u1_temp = odm_read_1byte(dm, 0x1e7);
		while ((!(u1_temp & BIT(0)))  && (count < 100)) {
			ODM_delay_us(50);
			u1_temp = odm_read_1byte(dm, 0x1e7);
			count++;
		}
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-ant ] 8723B: BT IQK status = %d, count = %d\n",
			  u1_temp, count);

		if (u1_temp & BIT(0)) {
			odm_set_bb_reg(dm, R_0x948, BIT(6), 0x1);
			odm_set_bb_reg(dm, R_0x948, BIT(9), default_ant);
			odm_set_bb_reg(dm, R_0x864, 0x38, default_ant);
					/* @Default RX */
			odm_set_bb_reg(dm, R_0x864, 0x1c0, optional_ant);
					/* @Optional RX */
			odm_set_bb_reg(dm, R_0x860, 0x7000, default_ant);
					/* @Default TX */
			fat_tab->rx_idle_ant = ant;

			/* Set TX AGC by S0/S1 */
			/* Need to consider Linux driver */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			adapter->hal_func.set_tx_power_level_handler(adapter, *dm->channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
			rtw_hal_set_tx_power_level(adapter, *dm->channel);
#endif

			/* Set IQC by S0/S1 */
			odm_set_iqc_by_rfpath(dm, default_ant);
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[ Update Rx-Idle-ant ] 8723B: Success to set RX antenna\n");
		} else
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to BT IQK\n");
	} else
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ Update Rx-Idle-ant ] 8723B: Fail to set RX antenna due to H2C command fail\n");

	/* Send H2C command to FW */
	/* @Disable wifi calibration */
	h2c_parameter = false;
	odm_fill_h2c_cmd(dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
#else

	odm_set_bb_reg(dm, R_0x948, BIT(6), 0x1);
	odm_set_bb_reg(dm, R_0x948, BIT(9), default_ant);
	odm_set_bb_reg(dm, R_0x864, BIT(5) | BIT(4) | BIT(3), default_ant);
			/*@Default RX*/
	odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), optional_ant);
			/*Optional RX*/
	odm_set_bb_reg(dm, R_0x860, BIT(14) | BIT(13) | BIT(12), default_ant);
			/*@Default TX*/
	fat_tab->rx_idle_ant = ant;

/* Set TX AGC by S0/S1 */
/* Need to consider Linux driver */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	((PADAPTER)adapter)->HalFunc.SetTxPowerLevelHandler(adapter, *dm->channel);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	rtw_hal_set_tx_power_level(adapter, *dm->channel);
#endif

	/* Set IQC by S0/S1 */
	odm_set_iqc_by_rfpath(dm, default_ant);
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Update Rx-Idle-ant ] 8723B: Success to set RX antenna\n");

#endif
}

boolean
phydm_is_bt_enable_8723b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 bt_state;
#if 0
	/*u32			reg75;*/

	/*reg75 = odm_get_bb_reg(dm, R_0x74, BIT8);*/
	/*odm_set_bb_reg(dm, R_0x74, BIT8, 0x0);*/
#endif
	odm_set_bb_reg(dm, R_0xa0, BIT(24) | BIT(25) | BIT(26), 0x5);
	bt_state = odm_get_bb_reg(dm, R_0xa0, 0xf);
#if 0
	/*odm_set_bb_reg(dm, R_0x74, BIT8, reg75);*/
#endif

	if (bt_state == 4 || bt_state == 7 || bt_state == 9 || bt_state == 13)
		return true;
	else
		return false;
}
#endif /* @#if (RTL8723B_SUPPORT == 1) */

#if (RTL8821A_SUPPORT == 1)

void odm_trx_hw_ant_div_init_8821a(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Output Pin Settings */
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);

	odm_set_mac_reg(dm, R_0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(dm, R_0x64, BIT(28), 1); /* @LNAON by WLAN control */

	odm_set_bb_reg(dm, R_0xcb8, BIT(16), 0);

	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);
			/* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1); /* @by WLAN control */
	odm_set_bb_reg(dm, R_0xcb4, 0xF, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, 0xF0, 8); /* @DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, BIT(29), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0xcb4, BIT(28), 1); /* @DPDT_N inverse */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x10); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	odm_set_bb_reg(dm, R_0x800, BIT(25), 0);
		/* @ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
		/* @CCK AntDiv function block enable */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);
}

void odm_s0s1_sw_ant_div_init_8821a(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Output Pin Settings */
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);

	odm_set_mac_reg(dm, R_0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(dm, R_0x64, BIT(28), 1); /* @LNAON by WLAN control */

	odm_set_bb_reg(dm, R_0xcb8, BIT(16), 0);

	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);
		/* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1); /* @by WLAN control */
	odm_set_bb_reg(dm, R_0xcb4, 0xF, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, 0xF0, 8); /* @DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, BIT(29), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0xcb4, BIT(28), 1); /* @DPDT_N inverse */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x10); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	odm_set_bb_reg(dm, R_0x800, BIT(25), 0);
		/* @ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
		/* @CCK AntDiv function block enable */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);

	odm_set_bb_reg(dm, R_0x900, BIT(18), 0);

	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;
	swat_tab->cur_antenna = MAIN_ANT;
	swat_tab->pre_ant = MAIN_ANT;
	swat_tab->swas_no_link_state = 0;
}
#endif /* @#if (RTL8821A_SUPPORT == 1) */

#if (RTL8821C_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);
	/* Output Pin Settings */
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);

	odm_set_mac_reg(dm, R_0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(dm, R_0x64, BIT(28), 1); /* @LNAON by WLAN control */

	odm_set_bb_reg(dm, R_0xcb8, BIT(16), 0);

	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);
		/* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1); /* @by WLAN control */
	odm_set_bb_reg(dm, R_0xcb4, 0xF, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, 0xF0, 8); /* @DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, BIT(29), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0xcb4, BIT(28), 1); /* @DPDT_N inverse */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x10); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	odm_set_bb_reg(dm, R_0x800, BIT(25), 0);
		/* @ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
		/* @CCK AntDiv function block enable */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	/* Timming issue */
	odm_set_bb_reg(dm, R_0x818, BIT(23) | BIT(22) | BIT(21) | BIT(20), 0);
		/*@keep antidx after tx for ACK ( unit x 3.2 mu sec)*/
	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);
}

void phydm_s0s1_sw_ant_div_init_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Output Pin Settings */
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);

	odm_set_mac_reg(dm, R_0x64, BIT(29), 1); /* PAPE by WLAN control */
	odm_set_mac_reg(dm, R_0x64, BIT(28), 1); /* @LNAON by WLAN control */

	odm_set_bb_reg(dm, R_0xcb8, BIT(16), 0);

	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);
		/* select DPDT_P and DPDT_N as output pin */
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1); /* @by WLAN control */
	odm_set_bb_reg(dm, R_0xcb4, 0xF, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, 0xF0, 8); /* @DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, BIT(29), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0xcb4, BIT(28), 1); /* @DPDT_N inverse */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x00); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	odm_set_bb_reg(dm, R_0x800, BIT(25), 0);
		/* @ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
		/* @CCK AntDiv function block enable */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);

	odm_set_bb_reg(dm, R_0x900, BIT(18), 0);

	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;
	swat_tab->cur_antenna = MAIN_ANT;
	swat_tab->pre_ant = MAIN_ANT;
	swat_tab->swas_no_link_state = 0;
}
#endif /* @#if (RTL8821C_SUPPORT == 1) */

#if (RTL8195B_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8195b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);
	
	odm_set_bb_reg(dm, R_0xcb8, BIT(16), 0);
	/*RFE control pin 0,1*/
	odm_set_bb_reg(dm, R_0xcb0, 0xF, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb0, 0xF0, 8); /* @DPDT_N = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb4, BIT(20), 0); /* @DPDT_P non-inverse */
	odm_set_bb_reg(dm, R_0xcb4, BIT(21), 1); /* @DPDT_N inverse */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x10); /* @bias */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	odm_set_bb_reg(dm, R_0x800, BIT(25), 0);
		/* @ANTSEL_CCK sent to the smart_antenna circuit */
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0);
		/* @CCK AntDiv function block enable */

	/* @BT Coexistence */
	odm_set_bb_reg(dm, R_0xcac, BIT(9), 1);
		/* @keep antsel_map when GNT_BT = 1 */
	odm_set_bb_reg(dm, R_0x804, BIT(4), 1);
		/* @Disable hw antsw & fast_train.antsw when GNT_BT=1 */

	/* Timming issue */
	odm_set_bb_reg(dm, R_0x818, BIT(23) | BIT(22) | BIT(21) | BIT(20), 0);
		/*@keep antidx after tx for ACK ( unit x 3.2 mu sec)*/
	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* response TX ant by RX ant */
	odm_set_mac_reg(dm, R_0x668, BIT(3), 1);
}
#endif /* @#if (RTL8195B_SUPPORT == 1) */

#if (RTL8881A_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8881a(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* Output Pin Settings */
	/* @[SPDT related] */
	odm_set_mac_reg(dm, R_0x4c, BIT(25), 0);
	odm_set_mac_reg(dm, R_0x4c, BIT(26), 0);
	odm_set_bb_reg(dm, R_0xcb4, BIT(31), 0); /* @delay buffer */
	odm_set_bb_reg(dm, R_0xcb4, BIT(22), 0);
	odm_set_bb_reg(dm, R_0xcb4, BIT(24), 1);
	odm_set_bb_reg(dm, R_0xcb0, 0xF00, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb0, 0xF0000, 8); /* @DPDT_N = ANTSEL[0] */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x0); /* @bias */
	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	/* @2 [--For HW Bug setting] */

	odm_set_bb_reg(dm, R_0x900, BIT(18), 0);
		/* TX ant  by Reg *//* A-cut bug */
}

#endif /* @#if (RTL8881A_SUPPORT == 1) */

#if (RTL8812A_SUPPORT == 1)
void odm_trx_hw_ant_div_init_8812a(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

	/* @3 */ /* @3 --RFE pin setting--------- */
	/* @[BB] */
	odm_set_bb_reg(dm, R_0x900, BIT(10) | BIT(9) | BIT(8), 0x0);
		/* @disable SW switch */
	odm_set_bb_reg(dm, R_0x900, BIT(17) | BIT(16), 0x0);
	odm_set_bb_reg(dm, R_0x974, BIT(7) | BIT(6), 0x3); /* @in/out */
	odm_set_bb_reg(dm, R_0xcb4, BIT(31), 0); /* @delay buffer */
	odm_set_bb_reg(dm, R_0xcb4, BIT(26), 0);
	odm_set_bb_reg(dm, R_0xcb4, BIT(27), 1);
	odm_set_bb_reg(dm, R_0xcb0, 0xF000000, 8); /* @DPDT_P = ANTSEL[0] */
	odm_set_bb_reg(dm, R_0xcb0, 0xF0000000, 8); /* @DPDT_N = ANTSEL[0] */
	/* @3 ------------------------- */

	/* @Mapping Table */
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE0, 0);
	odm_set_bb_reg(dm, R_0xca4, MASKBYTE1, 1);

	/* OFDM HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF, 0xA0); /* thershold */
	odm_set_bb_reg(dm, R_0x8d4, 0x7FF000, 0x0); /* @bias */
	odm_set_bb_reg(dm, R_0x8cc, BIT(20) | BIT(19) | BIT(18), 3);
		/* settling time of antdiv by RF LNA = 100ns */

	/* @CCK HW AntDiv Parameters */
	odm_set_bb_reg(dm, R_0xa74, BIT(7), 1);
		/* patch for clk from 88M to 80M */
	odm_set_bb_reg(dm, R_0xa0c, BIT(4), 1); /* @do 64 samples */

	/* @2 [--For HW Bug setting] */

	odm_set_bb_reg(dm, R_0x900, BIT(18), 0);
		/* TX ant  by Reg */ /* A-cut bug */
}

#endif /* @#if (RTL8812A_SUPPORT == 1) */

#if (RTL8188F_SUPPORT == 1)
void odm_s0s1_sw_ant_div_init_8188f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	PHYDM_DBG(dm, DBG_ANT_DIV, "[%s]=====>\n", __func__);

#if 0
	/*@GPIO setting*/
	/*odm_set_mac_reg(dm, R_0x64, BIT(18), 0); */
	/*odm_set_mac_reg(dm, R_0x44, BIT(28)|BIT(27), 0);*/
	/*odm_set_mac_reg(dm, R_0x44, BIT(20) | BIT(19), 0x3);*/
		/*enable_output for P_GPIO[4:3]*/
	/*odm_set_mac_reg(dm, R_0x44, BIT(12)|BIT(11), 0);*/ /*output value*/
	/*odm_set_mac_reg(dm, R_0x40, BIT(1)|BIT(0), 0);*/ /*GPIO function*/
#endif

	if (dm->support_ic_type == ODM_RTL8188F) {
		if (dm->support_interface == ODM_ITRF_USB)
			odm_set_mac_reg(dm, R_0x44, BIT(20) | BIT(19), 0x3);
				/*@enable_output for P_GPIO[4:3]*/
		else if (dm->support_interface == ODM_ITRF_SDIO)
			odm_set_mac_reg(dm, R_0x44, BIT(18), 0x1);
				/*@enable_output for P_GPIO[2]*/
	}

	fat_tab->is_become_linked = false;
	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;
}

void phydm_update_rx_idle_antenna_8188F(void *dm_void, u32 default_ant)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 codeword;

	if (dm->support_ic_type == ODM_RTL8188F) {
		if (dm->support_interface == ODM_ITRF_USB) {
			if (default_ant == ANT1_2G)
				codeword = 1; /*@2'b01*/
			else
				codeword = 2; /*@2'b10*/
			odm_set_mac_reg(dm, R_0x44, 0x1800, codeword);
				/*@GPIO[4:3] output value*/
		} else if (dm->support_interface == ODM_ITRF_SDIO) {
			if (default_ant == ANT1_2G) {
				codeword = 0; /*@1'b0*/
				odm_set_bb_reg(dm, R_0x870, 0x300, 0x3);
				odm_set_bb_reg(dm, R_0x860, 0x300, 0x1);
			} else {
				codeword = 1; /*@1'b1*/
				odm_set_bb_reg(dm, R_0x870, 0x300, 0x3);
				odm_set_bb_reg(dm, R_0x860, 0x300, 0x2);
			}
			odm_set_mac_reg(dm, R_0x44, BIT(10), codeword);
				/*@GPIO[2] output value*/
		}
	}
}
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
void phydm_rx_rate_for_antdiv(void *dm_void, void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	u8 data_rate = 0;

	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	data_rate = pktinfo->data_rate & 0x7f;

	if (!fat_tab->get_stats)
		return;

	if (fat_tab->antsel_rx_keep_0 == ANT1_2G) {
		if (data_rate >= ODM_RATEMCS0 &&
		    data_rate <= ODM_RATEMCS15)
			fat_tab->main_ht_cnt[data_rate - ODM_RATEMCS0]++;
		else if (data_rate >= ODM_RATEVHTSS1MCS0 &&
			 data_rate <= ODM_RATEVHTSS2MCS9)
			fat_tab->main_vht_cnt[data_rate - ODM_RATEVHTSS1MCS0]++;
	} else { /*ANT2_2G*/
		if (data_rate >= ODM_RATEMCS0 &&
		    data_rate <= ODM_RATEMCS15)
			fat_tab->aux_ht_cnt[data_rate - ODM_RATEMCS0]++;
		else if (data_rate >= ODM_RATEVHTSS1MCS0 &&
			 data_rate <= ODM_RATEVHTSS2MCS9)
			fat_tab->aux_vht_cnt[data_rate - ODM_RATEVHTSS1MCS0]++;
	}
}

void phydm_antdiv_reset_rx_rate(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	odm_memory_set(dm, &fat_tab->main_ht_cnt[0], 0, HT_IDX * 2);
	odm_memory_set(dm, &fat_tab->aux_ht_cnt[0], 0, HT_IDX * 2);
	odm_memory_set(dm, &fat_tab->main_vht_cnt[0], 0, VHT_IDX * 2);
	odm_memory_set(dm, &fat_tab->aux_vht_cnt[0], 0, VHT_IDX * 2);
}

void phydm_statistics_evm_1ss(void *dm_void,	void *phy_info_void,
			      u8 antsel_tr_mux, u32 id, u32 utility)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct phydm_phyinfo_struct *phy_info = NULL;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	if (antsel_tr_mux == ANT1_2G) {
		fat_tab->main_evm_sum[id] += ((phy_info->rx_mimo_evm_dbm[0])
					     << 5);
		fat_tab->main_evm_cnt[id]++;
	} else {
		fat_tab->aux_evm_sum[id] += ((phy_info->rx_mimo_evm_dbm[0])
					    << 5);
		fat_tab->aux_evm_cnt[id]++;
	}
}

void phydm_statistics_evm_2ss(void *dm_void,	void *phy_info_void,
			      u8 antsel_tr_mux, u32 id, u32 utility)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct phydm_phyinfo_struct *phy_info = NULL;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	if (antsel_tr_mux == ANT1_2G) {
		fat_tab->main_evm_2ss_sum[id][0] += phy_info->rx_mimo_evm_dbm[0]
						    << 5;
		fat_tab->main_evm_2ss_sum[id][1] += phy_info->rx_mimo_evm_dbm[1]
						    << 5;
		fat_tab->main_evm_2ss_cnt[id]++;

	} else {
		fat_tab->aux_evm_2ss_sum[id][0] += (phy_info->rx_mimo_evm_dbm[0]
						   << 5);
		fat_tab->aux_evm_2ss_sum[id][1] += (phy_info->rx_mimo_evm_dbm[1]
						   << 5);
		fat_tab->aux_evm_2ss_cnt[id]++;
	}
}

void phydm_evm_sw_antdiv_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	/*@EVM enhance AntDiv method init----------------*/
	fat_tab->evm_method_enable = 0;
	fat_tab->fat_state = NORMAL_STATE_MIAN;
	fat_tab->fat_state_cnt = 0;
	fat_tab->pre_antdiv_rssi = 0;

	dm->antdiv_intvl = 30;
	dm->antdiv_delay = 20;
	dm->antdiv_train_num = 4;
	if (dm->support_ic_type & ODM_RTL8192E)
		odm_set_bb_reg(dm, R_0x910, 0x3f, 0xf);
	dm->antdiv_evm_en = 1;
	/*@dm->antdiv_period=1;*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	dm->evm_antdiv_period = 1;
#else
	dm->evm_antdiv_period = 3;
#endif
	dm->stop_antdiv_rssi_th = 3;
	dm->stop_antdiv_tp_th = 80;
	dm->antdiv_tp_period = 3;
	dm->stop_antdiv_tp_diff_th = 5;
}

void odm_evm_fast_ant_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	fat_tab->evm_method_enable = 0;
	if (fat_tab->div_path_type == ANT_PATH_A)
		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
	else if (fat_tab->div_path_type == ANT_PATH_B)
		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_B);
	else if (fat_tab->div_path_type == ANT_PATH_AB)
		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_AB);
	fat_tab->fat_state = NORMAL_STATE_MIAN;
	fat_tab->fat_state_cnt = 0;
	dm->antdiv_period = 0;
	odm_set_mac_reg(dm, R_0x608, BIT(8), 0);
}

void odm_evm_enhance_ant_div(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 main_rssi, aux_rssi;
	u32 main_crc_utility = 0, aux_crc_utility = 0, utility_ratio = 1;
	u32 main_evm, aux_evm, diff_rssi = 0, diff_EVM = 0;
	u32 main_2ss_evm[2], aux_2ss_evm[2];
	u32 main_1ss_evm, aux_1ss_evm;
	u32 main_2ss_evm_sum, aux_2ss_evm_sum;
	u8 score_EVM = 0, score_CRC = 0;
	u8 rssi_larger_ant = 0;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 value32, i, mac_id;
	boolean main_above1 = false, aux_above1 = false;
	boolean force_antenna = false;
	struct cmn_sta_info *sta;
	u32 main_tp_avg, aux_tp_avg;
	u8 curr_rssi, rssi_diff;
	u32 tp_diff, tp_diff_avg;
	u16 main_max_cnt = 0, aux_max_cnt = 0;
	u16 main_max_idx = 0, aux_max_idx = 0;
	u16 main_cnt_all = 0, aux_cnt_all = 0;
	u8 rate_num = dm->num_rf_path;
	u8 rate_ss_shift = 0;
	u8 tp_diff_return = 0, tp_return = 0, rssi_return = 0;
	u8 target_ant_evm_1ss, target_ant_evm_2ss;
	u8 decision_evm_ss;
	u8 next_ant;

	fat_tab->target_ant_enhance = 0xFF;

	if ((dm->support_ic_type & ODM_EVM_ANTDIV_IC)) {
		if (dm->is_one_entry_only) {
#if 0
			/* PHYDM_DBG(dm,DBG_ANT_DIV, "[One Client only]\n"); */
#endif
			mac_id = dm->one_entry_macid;
			sta = dm->phydm_sta_info[mac_id];

			main_rssi = (fat_tab->main_cnt[mac_id] != 0) ? (fat_tab->main_sum[mac_id] / fat_tab->main_cnt[mac_id]) : 0;
			aux_rssi = (fat_tab->aux_cnt[mac_id] != 0) ? (fat_tab->aux_sum[mac_id] / fat_tab->aux_cnt[mac_id]) : 0;

			if ((main_rssi == 0 && aux_rssi != 0 && aux_rssi >= FORCE_RSSI_DIFF) || (main_rssi != 0 && aux_rssi == 0 && main_rssi >= FORCE_RSSI_DIFF))
				diff_rssi = FORCE_RSSI_DIFF;
			else if (main_rssi != 0 && aux_rssi != 0)
				diff_rssi = (main_rssi >= aux_rssi) ? (main_rssi - aux_rssi) : (aux_rssi - main_rssi);

			if (main_rssi >= aux_rssi)
				rssi_larger_ant = MAIN_ANT;
			else
				rssi_larger_ant = AUX_ANT;

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Main_Cnt=(( %d )), main_rssi=(( %d ))\n",
				  fat_tab->main_cnt[mac_id], main_rssi);
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Aux_Cnt=(( %d )), aux_rssi=(( %d ))\n",
				  fat_tab->aux_cnt[mac_id], aux_rssi);

			if (((main_rssi >= evm_rssi_th_high || aux_rssi >= evm_rssi_th_high) || fat_tab->evm_method_enable == 1)
			    /* @&& (diff_rssi <= FORCE_RSSI_DIFF + 1) */
			    ) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "> TH_H || evm_method_enable==1\n");

				if ((main_rssi >= evm_rssi_th_low || aux_rssi >= evm_rssi_th_low)) {
					PHYDM_DBG(dm, DBG_ANT_DIV, "> TH_L, fat_state_cnt =((%d))\n", fat_tab->fat_state_cnt);

					/*Traning state: 0(alt) 1(ori) 2(alt) 3(ori)============================================================*/
					if (fat_tab->fat_state_cnt < (dm->antdiv_train_num << 1)) {
						if (fat_tab->fat_state_cnt == 0) {
							/*Reset EVM 1SS Method */
							fat_tab->main_evm_sum[mac_id] = 0;
							fat_tab->aux_evm_sum[mac_id] = 0;
							fat_tab->main_evm_cnt[mac_id] = 0;
							fat_tab->aux_evm_cnt[mac_id] = 0;
							/*Reset EVM 2SS Method */
							fat_tab->main_evm_2ss_sum[mac_id][0] = 0;
							fat_tab->main_evm_2ss_sum[mac_id][1] = 0;
							fat_tab->aux_evm_2ss_sum[mac_id][0] = 0;
							fat_tab->aux_evm_2ss_sum[mac_id][1] = 0;
							fat_tab->main_evm_2ss_cnt[mac_id] = 0;
							fat_tab->aux_evm_2ss_cnt[mac_id] = 0;

							/*Reset TP Method */
							fat_tab->main_tp = 0;
							fat_tab->aux_tp = 0;
							fat_tab->main_tp_cnt = 0;
							fat_tab->aux_tp_cnt = 0;
							phydm_antdiv_reset_rx_rate(dm);

							/*Reset CRC Method */
							fat_tab->main_crc32_ok_cnt = 0;
							fat_tab->main_crc32_fail_cnt = 0;
							fat_tab->aux_crc32_ok_cnt = 0;
							fat_tab->aux_crc32_fail_cnt = 0;

#ifdef SKIP_EVM_ANTDIV_TRAINING_PATCH
							if ((*dm->band_width == CHANNEL_WIDTH_20) && sta->mimo_type == RF_2T2R) {
								/*@1. Skip training: RSSI*/
#if 0
								/*PHYDM_DBG(pDM_Odm,DBG_ANT_DIV, "TargetAnt_enhance=((%d)), RxIdleAnt=((%d))\n", pDM_FatTable->TargetAnt_enhance, pDM_FatTable->RxIdleAnt);*/
#endif
								curr_rssi = (u8)((fat_tab->rx_idle_ant == MAIN_ANT) ? main_rssi : aux_rssi);
								rssi_diff = (curr_rssi > fat_tab->pre_antdiv_rssi) ? (curr_rssi - fat_tab->pre_antdiv_rssi) : (fat_tab->pre_antdiv_rssi - curr_rssi);

								PHYDM_DBG(dm, DBG_ANT_DIV, "[1] rssi_return, curr_rssi=((%d)), pre_rssi=((%d))\n", curr_rssi, fat_tab->pre_antdiv_rssi);

								fat_tab->pre_antdiv_rssi = curr_rssi;
								if (rssi_diff < dm->stop_antdiv_rssi_th && curr_rssi != 0)
									rssi_return = 1;

								/*@2. Skip training: TP Diff*/
								tp_diff = (dm->rx_tp > fat_tab->pre_antdiv_tp) ? (dm->rx_tp - fat_tab->pre_antdiv_tp) : (fat_tab->pre_antdiv_tp - dm->rx_tp);

								PHYDM_DBG(dm, DBG_ANT_DIV, "[2] tp_diff_return, curr_tp=((%d)), pre_tp=((%d))\n", dm->rx_tp, fat_tab->pre_antdiv_tp);
								fat_tab->pre_antdiv_tp = dm->rx_tp;
								if ((tp_diff < (u32)(dm->stop_antdiv_tp_diff_th) && dm->rx_tp != 0))
									tp_diff_return = 1;

								PHYDM_DBG(dm, DBG_ANT_DIV, "[3] tp_return, curr_rx_tp=((%d))\n", dm->rx_tp);
								/*@3. Skip training: TP*/
								if (dm->rx_tp >= (u32)(dm->stop_antdiv_tp_th))
									tp_return = 1;

								PHYDM_DBG(dm, DBG_ANT_DIV, "[4] Return {rssi, tp_diff, tp} = {%d, %d, %d}\n", rssi_return, tp_diff_return, tp_return);
								/*@4. Joint Return Decision*/
								if (tp_return) {
									if (tp_diff_return || rssi_diff) {
										PHYDM_DBG(dm, DBG_ANT_DIV, "***Return EVM SW AntDiv\n");
										return;
									}
								}
							}
#endif

							fat_tab->evm_method_enable = 1;
							if (fat_tab->div_path_type == ANT_PATH_A)
								odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
							else if (fat_tab->div_path_type == ANT_PATH_B)
								odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_B);
							else if (fat_tab->div_path_type == ANT_PATH_AB)
								odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_AB);
							dm->antdiv_period = dm->evm_antdiv_period;
							odm_set_mac_reg(dm, R_0x608, BIT(8), 1); /*RCR accepts CRC32-Error packets*/
							fat_tab->fat_state_cnt++;
							fat_tab->get_stats = false;
							next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ? MAIN_ANT : AUX_ANT;
							odm_update_rx_idle_ant(dm, next_ant);
							PHYDM_DBG(dm, DBG_ANT_DIV, "[Antdiv Delay ]\n");
							odm_set_timer(dm, &dm->evm_fast_ant_training_timer, dm->antdiv_delay); //ms
						} else if ((fat_tab->fat_state_cnt % 2) != 0) {
							fat_tab->fat_state_cnt++;
							fat_tab->get_stats = true;
							odm_set_timer(dm, &dm->evm_fast_ant_training_timer, dm->antdiv_intvl); //ms
						} else if ((fat_tab->fat_state_cnt % 2) == 0) {
							fat_tab->fat_state_cnt++;
							fat_tab->get_stats = false;
							next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
							odm_update_rx_idle_ant(dm, next_ant);
							PHYDM_DBG(dm, DBG_ANT_DIV, "[Antdiv Delay ]\n");
							odm_set_timer(dm, &dm->evm_fast_ant_training_timer, dm->antdiv_delay); //ms
						}
					}
					/*@Decision state: 4==============================================================*/
					else {
						fat_tab->get_stats = false;
						fat_tab->fat_state_cnt = 0;
						PHYDM_DBG(dm, DBG_ANT_DIV, "[Decisoin state ]\n");

/* @3 [CRC32 statistic] */
#if 0
						if ((fat_tab->main_crc32_ok_cnt > (fat_tab->aux_crc32_ok_cnt << 1)) || (diff_rssi >= 40 && rssi_larger_ant == MAIN_ANT)) {
							fat_tab->target_ant_crc32 = MAIN_ANT;
							force_antenna = true;
							PHYDM_DBG(dm, DBG_ANT_DIV, "CRC32 Force Main\n");
						} else if ((fat_tab->aux_crc32_ok_cnt > ((fat_tab->main_crc32_ok_cnt) << 1)) || ((diff_rssi >= 40) && (rssi_larger_ant == AUX_ANT))) {
							fat_tab->target_ant_crc32 = AUX_ANT;
							force_antenna = true;
							PHYDM_DBG(dm, DBG_ANT_DIV, "CRC32 Force Aux\n");
						} else
#endif
						{
							if (fat_tab->main_crc32_fail_cnt <= 5)
								fat_tab->main_crc32_fail_cnt = 5;

							if (fat_tab->aux_crc32_fail_cnt <= 5)
								fat_tab->aux_crc32_fail_cnt = 5;

							if (fat_tab->main_crc32_ok_cnt > fat_tab->main_crc32_fail_cnt)
								main_above1 = true;

							if (fat_tab->aux_crc32_ok_cnt > fat_tab->aux_crc32_fail_cnt)
								aux_above1 = true;

							if (main_above1 == true && aux_above1 == false) {
								force_antenna = true;
								fat_tab->target_ant_crc32 = MAIN_ANT;
							} else if (main_above1 == false && aux_above1 == true) {
								force_antenna = true;
								fat_tab->target_ant_crc32 = AUX_ANT;
							} else if (main_above1 == true && aux_above1 == true) {
								main_crc_utility = ((fat_tab->main_crc32_ok_cnt) << 7) / fat_tab->main_crc32_fail_cnt;
								aux_crc_utility = ((fat_tab->aux_crc32_ok_cnt) << 7) / fat_tab->aux_crc32_fail_cnt;
								fat_tab->target_ant_crc32 = (main_crc_utility == aux_crc_utility) ? (fat_tab->pre_target_ant_enhance) : ((main_crc_utility >= aux_crc_utility) ? MAIN_ANT : AUX_ANT);

								if (main_crc_utility != 0 && aux_crc_utility != 0) {
									if (main_crc_utility >= aux_crc_utility)
										utility_ratio = (main_crc_utility << 1) / aux_crc_utility;
									else
										utility_ratio = (aux_crc_utility << 1) / main_crc_utility;
								}
							} else if (main_above1 == false && aux_above1 == false) {
								if (fat_tab->main_crc32_ok_cnt == 0)
									fat_tab->main_crc32_ok_cnt = 1;
								if (fat_tab->aux_crc32_ok_cnt == 0)
									fat_tab->aux_crc32_ok_cnt = 1;

								main_crc_utility = ((fat_tab->main_crc32_fail_cnt) << 7) / fat_tab->main_crc32_ok_cnt;
								aux_crc_utility = ((fat_tab->aux_crc32_fail_cnt) << 7) / fat_tab->aux_crc32_ok_cnt;
								fat_tab->target_ant_crc32 = (main_crc_utility == aux_crc_utility) ? (fat_tab->pre_target_ant_enhance) : ((main_crc_utility <= aux_crc_utility) ? MAIN_ANT : AUX_ANT);

								if (main_crc_utility != 0 && aux_crc_utility != 0) {
									if (main_crc_utility >= aux_crc_utility)
										utility_ratio = (main_crc_utility << 1) / (aux_crc_utility);
									else
										utility_ratio = (aux_crc_utility << 1) / (main_crc_utility);
								}
							}
						}
						odm_set_mac_reg(dm, R_0x608, BIT(8), 0); /* NOT Accept CRC32 Error packets. */
						PHYDM_DBG(dm, DBG_ANT_DIV, "MAIN_CRC: Ok=((%d)), Fail = ((%d)), Utility = ((%d))\n", fat_tab->main_crc32_ok_cnt, fat_tab->main_crc32_fail_cnt, main_crc_utility);
						PHYDM_DBG(dm, DBG_ANT_DIV, "AUX__CRC: Ok=((%d)), Fail = ((%d)), Utility = ((%d))\n", fat_tab->aux_crc32_ok_cnt, fat_tab->aux_crc32_fail_cnt, aux_crc_utility);
						PHYDM_DBG(dm, DBG_ANT_DIV, "***1.TargetAnt_CRC32 = ((%s))\n", (fat_tab->target_ant_crc32 == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

						for (i = 0; i < HT_IDX; i++) {
							main_cnt_all += fat_tab->main_ht_cnt[i];
							aux_cnt_all += fat_tab->aux_ht_cnt[i];

							if (fat_tab->main_ht_cnt[i] > main_max_cnt) {
								main_max_cnt = fat_tab->main_ht_cnt[i];
								main_max_idx = i;
							}

							if (fat_tab->aux_ht_cnt[i] > aux_max_cnt) {
								aux_max_cnt = fat_tab->aux_ht_cnt[i];
								aux_max_idx = i;
							}
						}

						for (i = 0; i < rate_num; i++) {
							rate_ss_shift = (i << 3);
							PHYDM_DBG(dm, DBG_ANT_DIV, "*main_ht_cnt  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
							(rate_ss_shift), (rate_ss_shift + 7),
							fat_tab->main_ht_cnt[rate_ss_shift + 0], fat_tab->main_ht_cnt[rate_ss_shift + 1],
							fat_tab->main_ht_cnt[rate_ss_shift + 2], fat_tab->main_ht_cnt[rate_ss_shift + 3],
							fat_tab->main_ht_cnt[rate_ss_shift + 4], fat_tab->main_ht_cnt[rate_ss_shift + 5],
							fat_tab->main_ht_cnt[rate_ss_shift + 6], fat_tab->main_ht_cnt[rate_ss_shift + 7]);
						}

						for (i = 0; i < rate_num; i++) {
							rate_ss_shift = (i << 3);
							PHYDM_DBG(dm, DBG_ANT_DIV, "*aux_ht_cnt  HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
							(rate_ss_shift), (rate_ss_shift + 7),
							fat_tab->aux_ht_cnt[rate_ss_shift + 0], fat_tab->aux_ht_cnt[rate_ss_shift + 1],
							fat_tab->aux_ht_cnt[rate_ss_shift + 2], fat_tab->aux_ht_cnt[rate_ss_shift + 3],
							fat_tab->aux_ht_cnt[rate_ss_shift + 4], fat_tab->aux_ht_cnt[rate_ss_shift + 5],
							fat_tab->aux_ht_cnt[rate_ss_shift + 6], fat_tab->aux_ht_cnt[rate_ss_shift + 7]);
						}

						/* @3 [EVM statistic] */
						/*@1SS EVM*/
						main_1ss_evm = (fat_tab->main_evm_cnt[mac_id] != 0) ? (fat_tab->main_evm_sum[mac_id] / fat_tab->main_evm_cnt[mac_id]) : 0;
						aux_1ss_evm = (fat_tab->aux_evm_cnt[mac_id] != 0) ? (fat_tab->aux_evm_sum[mac_id] / fat_tab->aux_evm_cnt[mac_id]) : 0;
						target_ant_evm_1ss = (main_1ss_evm == aux_1ss_evm) ? (fat_tab->pre_target_ant_enhance) : ((main_1ss_evm >= aux_1ss_evm) ? MAIN_ANT : AUX_ANT);

						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Main1ss_EVM= ((  %d ))\n", fat_tab->main_evm_cnt[mac_id], main_1ss_evm);
						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Aux_1ss_EVM = ((  %d ))\n", fat_tab->aux_evm_cnt[mac_id], aux_1ss_evm);

						/*@2SS EVM*/
						main_2ss_evm[0] = (fat_tab->main_evm_2ss_cnt[mac_id] != 0) ? (fat_tab->main_evm_2ss_sum[mac_id][0] / fat_tab->main_evm_2ss_cnt[mac_id]) : 0;
						main_2ss_evm[1] = (fat_tab->main_evm_2ss_cnt[mac_id] != 0) ? (fat_tab->main_evm_2ss_sum[mac_id][1] / fat_tab->main_evm_2ss_cnt[mac_id]) : 0;
						main_2ss_evm_sum = main_2ss_evm[0] + main_2ss_evm[1];

						aux_2ss_evm[0] = (fat_tab->aux_evm_2ss_cnt[mac_id] != 0) ? (fat_tab->aux_evm_2ss_sum[mac_id][0] / fat_tab->aux_evm_2ss_cnt[mac_id]) : 0;
						aux_2ss_evm[1] = (fat_tab->aux_evm_2ss_cnt[mac_id] != 0) ? (fat_tab->aux_evm_2ss_sum[mac_id][1] / fat_tab->aux_evm_2ss_cnt[mac_id]) : 0;
						aux_2ss_evm_sum = aux_2ss_evm[0] + aux_2ss_evm[1];

						target_ant_evm_2ss = (main_2ss_evm_sum == aux_2ss_evm_sum) ? (fat_tab->pre_target_ant_enhance) : ((main_2ss_evm_sum >= aux_2ss_evm_sum) ? MAIN_ANT : AUX_ANT);

						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Main2ss_EVM{A,B,Sum} = {%d, %d, %d}\n",
							  fat_tab->main_evm_2ss_cnt[mac_id], main_2ss_evm[0], main_2ss_evm[1], main_2ss_evm_sum);
						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Aux_2ss_EVM{A,B,Sum} = {%d, %d, %d}\n",
							  fat_tab->aux_evm_2ss_cnt[mac_id], aux_2ss_evm[0], aux_2ss_evm[1], aux_2ss_evm_sum);

						if ((main_2ss_evm_sum + aux_2ss_evm_sum) != 0) {
							decision_evm_ss = 2;
							main_evm = main_2ss_evm_sum;
							aux_evm = aux_2ss_evm_sum;
							fat_tab->target_ant_evm = target_ant_evm_2ss;
						} else {
							decision_evm_ss = 1;
							main_evm = main_1ss_evm;
							aux_evm = aux_1ss_evm;
							fat_tab->target_ant_evm = target_ant_evm_1ss;
						}

						if ((main_evm == 0 || aux_evm == 0))
							diff_EVM = 100;
						else if (main_evm >= aux_evm)
							diff_EVM = main_evm - aux_evm;
						else
							diff_EVM = aux_evm - main_evm;

						PHYDM_DBG(dm, DBG_ANT_DIV, "***2.TargetAnt_EVM((%d-ss)) = ((%s))\n", decision_evm_ss, (fat_tab->target_ant_evm == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

						//3 [TP statistic]
						main_tp_avg = (fat_tab->main_tp_cnt != 0) ? (fat_tab->main_tp / fat_tab->main_tp_cnt) : 0;
						aux_tp_avg = (fat_tab->aux_tp_cnt != 0) ? (fat_tab->aux_tp / fat_tab->aux_tp_cnt) : 0;
						tp_diff_avg = DIFF_2(main_tp_avg, aux_tp_avg);
						fat_tab->target_ant_tp = (tp_diff_avg < 100) ? (fat_tab->pre_target_ant_enhance) : ((main_tp_avg >= aux_tp_avg) ? MAIN_ANT : AUX_ANT);

						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Main_TP = ((%d))\n", fat_tab->main_tp_cnt, main_tp_avg);
						PHYDM_DBG(dm, DBG_ANT_DIV, "Cnt = ((%d)), Aux_TP = ((%d))\n", fat_tab->aux_tp_cnt, aux_tp_avg);
						PHYDM_DBG(dm, DBG_ANT_DIV, "***3.TargetAnt_TP = ((%s))\n", (fat_tab->target_ant_tp == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

						/*Reset TP Method */
						fat_tab->main_tp = 0;
						fat_tab->aux_tp = 0;
						fat_tab->main_tp_cnt = 0;
						fat_tab->aux_tp_cnt = 0;

						/* @2 [ Decision state ] */
						#if 1
						if (main_max_idx == aux_max_idx && ((main_cnt_all + aux_cnt_all) != 0)) {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision EVM, main_max_idx = ((MCS%d)), aux_max_idx = ((MCS%d))\n", main_max_idx, aux_max_idx);
							fat_tab->target_ant_enhance = fat_tab->target_ant_evm;
						} else {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision TP, main_max_idx = ((MCS%d)), aux_max_idx = ((MCS%d))\n", main_max_idx, aux_max_idx);
							fat_tab->target_ant_enhance = fat_tab->target_ant_tp;
						}
						#else
						if (fat_tab->target_ant_evm == fat_tab->target_ant_crc32) {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision type 1, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM);

							if ((utility_ratio < 2 && force_antenna == false) && diff_EVM <= 30)
								fat_tab->target_ant_enhance = fat_tab->pre_target_ant_enhance;
							else
								fat_tab->target_ant_enhance = fat_tab->target_ant_evm;
						}
						#if 0
						else if ((diff_EVM <= 50 && (utility_ratio > 4 && force_antenna == false)) || (force_antenna == true)) {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision type 2, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM);
							fat_tab->target_ant_enhance = fat_tab->target_ant_crc32;
						}
						#endif
						else if (diff_EVM >= 20) {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision type 3, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM);
							fat_tab->target_ant_enhance = fat_tab->target_ant_evm;
						} else if (utility_ratio >= 6 && force_antenna == false) {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision type 4, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM);
							fat_tab->target_ant_enhance = fat_tab->target_ant_crc32;
						} else {
							PHYDM_DBG(dm, DBG_ANT_DIV, "Decision type 5, CRC_utility = ((%d)), EVM_diff = ((%d))\n", utility_ratio, diff_EVM);

							if (force_antenna == true)
								score_CRC = 2;
							else if (utility_ratio >= 5) /*@>2.5*/
								score_CRC = 2;
							else if (utility_ratio >= 4) /*@>2*/
								score_CRC = 1;
							else
								score_CRC = 0;

							if (diff_EVM >= 15)
								score_EVM = 3;
							else if (diff_EVM >= 10)
								score_EVM = 2;
							else if (diff_EVM >= 5)
								score_EVM = 1;
							else
								score_EVM = 0;

							if (score_CRC > score_EVM)
								fat_tab->target_ant_enhance = fat_tab->target_ant_crc32;
							else if (score_CRC < score_EVM)
								fat_tab->target_ant_enhance = fat_tab->target_ant_evm;
							else
								fat_tab->target_ant_enhance = fat_tab->pre_target_ant_enhance;
						}
						#endif
						fat_tab->pre_target_ant_enhance = fat_tab->target_ant_enhance;

						PHYDM_DBG(dm, DBG_ANT_DIV, "*** 4.TargetAnt_enhance = (( %s ))******\n", (fat_tab->target_ant_enhance == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");
					}
				} else { /* RSSI< = evm_rssi_th_low */
					PHYDM_DBG(dm, DBG_ANT_DIV, "[ <TH_L: escape from > TH_L ]\n");
					odm_evm_fast_ant_reset(dm);
				}
			} else {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[escape from> TH_H || evm_method_enable==1]\n");
				odm_evm_fast_ant_reset(dm);
			}
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[multi-Client]\n");
			odm_evm_fast_ant_reset(dm);
		}
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_evm_antdiv_callback(
	struct phydm_timer_list *timer)
{
	void *adapter = (void *)timer->Adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	#if USE_WORKITEM
	odm_schedule_work_item(&dm->phydm_evm_antdiv_workitem);
	#else
	{
		odm_hw_ant_div(dm);
	}
	#endif
	#else
	odm_schedule_work_item(&dm->phydm_evm_antdiv_workitem);
	#endif
}

void phydm_evm_antdiv_workitem_callback(
	void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	odm_hw_ant_div(dm);
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
void phydm_evm_antdiv_callback(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *padapter = dm->adapter;

	if (*dm->is_net_closed)
		return;
	if (dm->support_interface == ODM_ITRF_PCIE) {
		odm_hw_ant_div(dm);
	} else {
		/* @Can't do I/O in timer callback*/
		phydm_run_in_thread_cmd(dm,
					phydm_evm_antdiv_workitem_callback,
					padapter);
	}
}

void phydm_evm_antdiv_workitem_callback(void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->odmpriv;

	odm_hw_ant_div(dm);
}

#else
void phydm_evm_antdiv_callback(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "******AntDiv_Callback******\n");
	odm_hw_ant_div(dm);
}
#endif

#endif

void odm_hw_ant_div(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 i, min_max_rssi = 0xFF, ant_div_max_rssi = 0, max_rssi = 0;
	u32 main_rssi, aux_rssi, mian_cnt, aux_cnt, local_max_rssi;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 rx_idle_ant = fat_tab->rx_idle_ant, target_ant = 7;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct cmn_sta_info *sta;

#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	struct _BF_DIV_COEX_ *dm_bdc_table = &dm->dm_bdc_table;
	u32 TH1 = 500000;
	u32 TH2 = 10000000;
	u32 ma_rx_temp, degrade_TP_temp, improve_TP_temp;
	u8 monitor_rssi_threshold = 30;

	dm_bdc_table->BF_pass = true;
	dm_bdc_table->DIV_pass = true;
	dm_bdc_table->is_all_div_sta_idle = true;
	dm_bdc_table->is_all_bf_sta_idle = true;
	dm_bdc_table->num_bf_tar = 0;
	dm_bdc_table->num_div_tar = 0;
	dm_bdc_table->num_client = 0;
#endif
#endif

	if (!dm->is_linked) { /* @is_linked==False */
		PHYDM_DBG(dm, DBG_ANT_DIV, "[No Link!!!]\n");

		if (fat_tab->is_become_linked) {
			if (fat_tab->div_path_type == ANT_PATH_A)
				odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
			else if (fat_tab->div_path_type == ANT_PATH_B)
				odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_B);
			else if (fat_tab->div_path_type == ANT_PATH_AB)
				odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_AB);
			odm_update_rx_idle_ant(dm, MAIN_ANT);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
			dm->antdiv_period = 0;

			fat_tab->is_become_linked = dm->is_linked;
		}
		return;
	} else {
		if (!fat_tab->is_become_linked) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Linked !!!]\n");
			if (fat_tab->div_path_type == ANT_PATH_A)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
			else if (fat_tab->div_path_type == ANT_PATH_B)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_B);
			else if (fat_tab->div_path_type == ANT_PATH_AB)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_AB);
			#if 0
			/*odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);*/

			/* @if(dm->support_ic_type == ODM_RTL8821 ) */
			/* odm_set_bb_reg(dm, R_0x800, BIT(25), 0); */
			/* CCK AntDiv function disable */

			/* @#if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */
			/* @else if(dm->support_ic_type == ODM_RTL8881A) */
			/* odm_set_bb_reg(dm, R_0x800, BIT(25), 0); */
			/* CCK AntDiv function disable */
			/* @#endif */

			/* @else if(dm->support_ic_type == ODM_RTL8723B ||*/
			/* @dm->support_ic_type == ODM_RTL8812) */
			/* odm_set_bb_reg(dm, R_0xa00, BIT(15), 0); */
			/* CCK AntDiv function disable */
			#endif

			fat_tab->is_become_linked = dm->is_linked;

			if (dm->support_ic_type == ODM_RTL8723B &&
			    dm->ant_div_type == CG_TRX_HW_ANTDIV) {
				odm_set_bb_reg(dm, R_0x930, 0xF0, 8);
				/* @DPDT_P = ANTSEL[0] for 8723B AntDiv */
				odm_set_bb_reg(dm, R_0x930, 0xF, 8);
				/* @DPDT_N = ANTSEL[0] */
			}

			/* @ BDC Init */
			#ifdef PHYDM_BEAMFORMING_SUPPORT
			#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
			odm_bdc_init(dm);
			#endif
			#endif

			#ifdef ODM_EVM_ENHANCE_ANTDIV
			odm_evm_fast_ant_reset(dm);
			#endif
		}
	}

	if (!(*fat_tab->p_force_tx_by_desc)) {
		if (dm->is_one_entry_only)
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	}

#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (dm->antdiv_evm_en == 1) {
		odm_evm_enhance_ant_div(dm);
		if (fat_tab->fat_state_cnt != 0)
			return;
	} else
		odm_evm_fast_ant_reset(dm);
#endif

/* @2 BDC mode Arbitration */
#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (dm->antdiv_evm_en == 0 || fat_tab->evm_method_enable == 0)
		odm_bf_ant_div_mode_arbitration(dm);
#endif
#endif

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		sta = dm->phydm_sta_info[i];
		if (!is_sta_active(sta)) {
			phydm_antdiv_reset_statistic(dm, i);
			continue;
		}

		/* @2 Caculate RSSI per Antenna */
		if (fat_tab->main_cnt[i] != 0 || fat_tab->aux_cnt[i] != 0) {
			mian_cnt = fat_tab->main_cnt[i];
			aux_cnt = fat_tab->aux_cnt[i];
			main_rssi = (mian_cnt != 0) ?
				    (fat_tab->main_sum[i] / mian_cnt) : 0;
			aux_rssi = (aux_cnt != 0) ?
				   (fat_tab->aux_sum[i] / aux_cnt) : 0;
			target_ant = (mian_cnt == aux_cnt) ?
				     fat_tab->rx_idle_ant :
				     ((mian_cnt >= aux_cnt) ?
				     fat_tab->ant_idx_vec[0]:fat_tab->ant_idx_vec[1]);
				     /*Use counter number for OFDM*/

		} else { /*@CCK only case*/
			mian_cnt = fat_tab->main_cnt_cck[i];
			aux_cnt = fat_tab->aux_cnt_cck[i];
			main_rssi = (mian_cnt != 0) ?
				    (fat_tab->main_sum_cck[i] / mian_cnt) : 0;
			aux_rssi = (aux_cnt != 0) ?
				   (fat_tab->aux_sum_cck[i] / aux_cnt) : 0;
			target_ant = (main_rssi == aux_rssi) ?
				     fat_tab->rx_idle_ant :
				     ((main_rssi >= aux_rssi) ?
				     fat_tab->ant_idx_vec[0]:fat_tab->ant_idx_vec[1]);
				     /*Use RSSI for CCK only case*/
		}
#if (RTL8721D_SUPPORT)
	if(dm->antdiv_gpio == ANTDIV_GPIO_PB1PB2PB26) { /* added by Jiao Qi on May.25,2020, only for 3 antenna diversity */
		u8 tmp;
		if(target_ant == fat_tab->ant_idx_vec[0]){/* switch the second & third ant index */
			tmp = fat_tab->ant_idx_vec[1];
			fat_tab->ant_idx_vec[1] = fat_tab->ant_idx_vec[2];
			fat_tab->ant_idx_vec[2] = tmp;
		}else{
			/* switch the first & second ant index */
			tmp = fat_tab->ant_idx_vec[0];
			fat_tab->ant_idx_vec[0] = fat_tab->ant_idx_vec[1];
			fat_tab->ant_idx_vec[1] = tmp;
			/* switch the second & third ant index */
			tmp = fat_tab->ant_idx_vec[1];
			fat_tab->ant_idx_vec[1] = fat_tab->ant_idx_vec[2];
			fat_tab->ant_idx_vec[2] = tmp;
		}
	}
#endif

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "*** Client[ %d ] : Main_Cnt = (( %d ))  ,  CCK_Main_Cnt = (( %d )) ,  main_rssi= ((  %d ))\n",
			  i, fat_tab->main_cnt[i],
			  fat_tab->main_cnt_cck[i], main_rssi);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "*** Client[ %d ] : Aux_Cnt   = (( %d ))  , CCK_Aux_Cnt   = (( %d )) ,  aux_rssi = ((  %d ))\n",
			  i, fat_tab->aux_cnt[i],
			  fat_tab->aux_cnt_cck[i], aux_rssi);

		local_max_rssi = (main_rssi > aux_rssi) ? main_rssi : aux_rssi;
		/* @ Select max_rssi for DIG */
		if (local_max_rssi > ant_div_max_rssi && local_max_rssi < 40)
			ant_div_max_rssi = local_max_rssi;
		if (local_max_rssi > max_rssi)
			max_rssi = local_max_rssi;

		/* @ Select RX Idle Antenna */
		if (local_max_rssi != 0 && local_max_rssi < min_max_rssi) {
			rx_idle_ant = target_ant;
			min_max_rssi = local_max_rssi;
		}

#ifdef ODM_EVM_ENHANCE_ANTDIV
		if (dm->antdiv_evm_en == 1) {
			if (fat_tab->target_ant_enhance != 0xFF) {
				target_ant = fat_tab->target_ant_enhance;
				rx_idle_ant = fat_tab->target_ant_enhance;
			}
		}
#endif

		/* @2 Select TX Antenna */
		if (dm->ant_div_type != CGCS_RX_HW_ANTDIV) {
			#ifdef PHYDM_BEAMFORMING_SUPPORT
			#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
			if (dm_bdc_table->w_bfee_client[i] == 0)
			#endif
			#endif
			{
				odm_update_tx_ant(dm, target_ant, i);
			}
		}

/* @------------------------------------------------------------ */

		#ifdef PHYDM_BEAMFORMING_SUPPORT
		#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

		dm_bdc_table->num_client++;

		if (dm_bdc_table->bdc_mode == BDC_MODE_2 || dm_bdc_table->bdc_mode == BDC_MODE_3) {
			/* @2 Byte counter */

			ma_rx_temp = sta->rx_moving_average_tp; /* RX  TP   ( bit /sec) */

			if (dm_bdc_table->BDC_state == bdc_bfer_train_state)
				dm_bdc_table->MA_rx_TP_DIV[i] = ma_rx_temp;
			else
				dm_bdc_table->MA_rx_TP[i] = ma_rx_temp;

			if (ma_rx_temp < TH2 && ma_rx_temp > TH1 && local_max_rssi <= monitor_rssi_threshold) {
				if (dm_bdc_table->w_bfer_client[i] == 1) { /* @Bfer_Target */
					dm_bdc_table->num_bf_tar++;

					if (dm_bdc_table->BDC_state == BDC_DECISION_STATE && dm_bdc_table->bdc_try_flag == 0) {
						improve_TP_temp = (dm_bdc_table->MA_rx_TP_DIV[i] * 9) >> 3; /* @* 1.125 */
						dm_bdc_table->BF_pass = (dm_bdc_table->MA_rx_TP[i] > improve_TP_temp) ? true : false;
						PHYDM_DBG(dm, DBG_ANT_DIV, "*** Client[ %d ] :  { MA_rx_TP,improve_TP_temp, MA_rx_TP_DIV,  BF_pass}={ %d,  %d, %d , %d }\n", i, dm_bdc_table->MA_rx_TP[i], improve_TP_temp, dm_bdc_table->MA_rx_TP_DIV[i], dm_bdc_table->BF_pass);
					}
				} else { /* @DIV_Target */
					dm_bdc_table->num_div_tar++;

					if (dm_bdc_table->BDC_state == BDC_DECISION_STATE && dm_bdc_table->bdc_try_flag == 0) {
						degrade_TP_temp = (dm_bdc_table->MA_rx_TP_DIV[i] * 5) >> 3; /* @* 0.625 */
						dm_bdc_table->DIV_pass = (dm_bdc_table->MA_rx_TP[i] > degrade_TP_temp) ? true : false;
						PHYDM_DBG(dm, DBG_ANT_DIV, "*** Client[ %d ] :  { MA_rx_TP, degrade_TP_temp, MA_rx_TP_DIV,  DIV_pass}=\n{ %d,  %d, %d , %d }\n", i, dm_bdc_table->MA_rx_TP[i], degrade_TP_temp, dm_bdc_table->MA_rx_TP_DIV[i], dm_bdc_table->DIV_pass);
					}
				}
			}

			if (ma_rx_temp > TH1) {
				if (dm_bdc_table->w_bfer_client[i] == 1) /* @Bfer_Target */
					dm_bdc_table->is_all_bf_sta_idle = false;
				else /* @DIV_Target */
					dm_bdc_table->is_all_div_sta_idle = false;
			}

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "*** Client[ %d ] :  { BFmeeCap, BFmerCap}  = { %d , %d }\n",
				  i, dm_bdc_table->w_bfee_client[i],
				  dm_bdc_table->w_bfer_client[i]);

			if (dm_bdc_table->BDC_state == bdc_bfer_train_state)
				PHYDM_DBG(dm, DBG_ANT_DIV, "*** Client[ %d ] :    MA_rx_TP_DIV = (( %d ))\n", i, dm_bdc_table->MA_rx_TP_DIV[i]);

			else
				PHYDM_DBG(dm, DBG_ANT_DIV, "*** Client[ %d ] :    MA_rx_TP = (( %d ))\n", i, dm_bdc_table->MA_rx_TP[i]);
		}
		#endif
		#endif

		#ifdef PHYDM_BEAMFORMING_SUPPORT
		#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		if (dm_bdc_table->bdc_try_flag == 0)
		#endif
		#endif
		{
			phydm_antdiv_reset_statistic(dm, i);
		}
	}

/* @2 Set RX Idle Antenna & TX Antenna(Because of HW Bug ) */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	PHYDM_DBG(dm, DBG_ANT_DIV, "*** rx_idle_ant = (( %s ))\n",
		  (rx_idle_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (dm_bdc_table->bdc_mode == BDC_MODE_1 || dm_bdc_table->bdc_mode == BDC_MODE_3) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "*** bdc_rx_idle_update_counter = (( %d ))\n",
			  dm_bdc_table->bdc_rx_idle_update_counter);

		if (dm_bdc_table->bdc_rx_idle_update_counter == 1) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "***Update RxIdle Antenna!!!\n");
			dm_bdc_table->bdc_rx_idle_update_counter = 30;
			odm_update_rx_idle_ant(dm, rx_idle_ant);
		} else {
			dm_bdc_table->bdc_rx_idle_update_counter--;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "***NOT update RxIdle Antenna because of BF  ( need to fix TX-ant)\n");
		}
	} else
#endif
#endif
		odm_update_rx_idle_ant(dm, rx_idle_ant);
#else
#if (RTL8721D_SUPPORT)
if (dm->antdiv_gpio == ANTDIV_GPIO_PB1PB2PB26) {
	if(odm_get_bb_reg(dm,R_0xc50,0x80) || odm_get_bb_reg(dm, R_0xa00, 0x8000))
		odm_update_rx_idle_ant_sp3t(dm, rx_idle_ant);
}
else
#endif
	odm_update_rx_idle_ant(dm, rx_idle_ant);

#endif /* @#if(DM_ODM_SUPPORT_TYPE  == ODM_AP) */

/* @2 BDC Main Algorithm */
#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (dm->antdiv_evm_en == 0 || fat_tab->evm_method_enable == 0)
		odm_bd_ccoex_bfee_rx_div_arbitration(dm);

	dm_bdc_table->num_txbfee_client = 0;
	dm_bdc_table->num_txbfer_client = 0;
#endif
#endif

	if (ant_div_max_rssi == 0)
		dig_t->ant_div_rssi_max = dm->rssi_min;
	else
		dig_t->ant_div_rssi_max = ant_div_max_rssi;

	PHYDM_DBG(dm, DBG_ANT_DIV, "***AntDiv End***\n\n");
}

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY

void odm_s0s1_sw_ant_div_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	fat_tab->is_become_linked = false;
	swat_tab->try_flag = SWAW_STEP_INIT;
	swat_tab->double_chk_flag = 0;

	PHYDM_DBG(dm, DBG_ANT_DIV, "%s: fat_tab->is_become_linked = %d\n",
		  __func__, fat_tab->is_become_linked);
}

void phydm_sw_antdiv_train_time(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	u8 high_traffic_train_time_u = 0x32, high_traffic_train_time_l = 0;
	u8 low_traffic_train_time_u = 200, low_traffic_train_time_l = 0;
	u8 train_time_temp;

	if (dm->traffic_load == TRAFFIC_HIGH) {
		train_time_temp = swat_tab->train_time;

		if (swat_tab->train_time_flag == 3) {
			high_traffic_train_time_l = 0xa;

			if (train_time_temp <= 16)
				train_time_temp = high_traffic_train_time_l;
			else
				train_time_temp -= 16;

		} else if (swat_tab->train_time_flag == 2) {
			train_time_temp -= 8;
			high_traffic_train_time_l = 0xf;
		} else if (swat_tab->train_time_flag == 1) {
			train_time_temp -= 4;
			high_traffic_train_time_l = 0x1e;
		} else if (swat_tab->train_time_flag == 0) {
			train_time_temp += 8;
			high_traffic_train_time_l = 0x28;
		}

		if (dm->support_ic_type == ODM_RTL8188F) {
			if (dm->support_interface == ODM_ITRF_SDIO)
				high_traffic_train_time_l += 0xa;
		}

		/* @-- */
		if (train_time_temp > high_traffic_train_time_u)
			train_time_temp = high_traffic_train_time_u;

		else if (train_time_temp < high_traffic_train_time_l)
			train_time_temp = high_traffic_train_time_l;

		swat_tab->train_time = train_time_temp; /*@10ms~200ms*/

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "train_time_flag=((%d)), train_time=((%d))\n",
			  swat_tab->train_time_flag,
			  swat_tab->train_time);

	} else if ((dm->traffic_load == TRAFFIC_MID) ||
		   (dm->traffic_load == TRAFFIC_LOW)) {
		train_time_temp = swat_tab->train_time;

		if (swat_tab->train_time_flag == 3) {
			low_traffic_train_time_l = 10;
			if (train_time_temp < 50)
				train_time_temp = low_traffic_train_time_l;
			else
				train_time_temp -= 50;
		} else if (swat_tab->train_time_flag == 2) {
			train_time_temp -= 30;
			low_traffic_train_time_l = 36;
		} else if (swat_tab->train_time_flag == 1) {
			train_time_temp -= 10;
			low_traffic_train_time_l = 40;
		} else {
			train_time_temp += 10;
			low_traffic_train_time_l = 50;
		}

		if (dm->support_ic_type == ODM_RTL8188F) {
			if (dm->support_interface == ODM_ITRF_SDIO)
				low_traffic_train_time_l += 10;
		}

		/* @-- */
		if (train_time_temp >= low_traffic_train_time_u)
			train_time_temp = low_traffic_train_time_u;

		else if (train_time_temp <= low_traffic_train_time_l)
			train_time_temp = low_traffic_train_time_l;

		swat_tab->train_time = train_time_temp; /*@10ms~200ms*/

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "train_time_flag=((%d)) , train_time=((%d))\n",
			  swat_tab->train_time_flag, swat_tab->train_time);

	} else {
		swat_tab->train_time = 0xc8; /*@200ms*/
	}
}

void phydm_sw_antdiv_decision(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 i, min_max_rssi = 0xFF, local_max_rssi, local_min_rssi;
	u32 main_rssi, aux_rssi;
	u8 rx_idle_ant = swat_tab->pre_ant;
	u8 target_ant = swat_tab->pre_ant, next_ant = 0;
	struct cmn_sta_info *entry = NULL;
	u32 main_cnt = 0, aux_cnt = 0, main_sum = 0, aux_sum = 0;
	u32 main_ctrl_cnt = 0, aux_ctrl_cnt = 0;
	boolean is_by_ctrl_frame = false;
	boolean cond_23d_main, cond_23d_aux;
	u64 pkt_cnt_total = 0;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		entry = dm->phydm_sta_info[i];
		if (!is_sta_active(entry)) {
			phydm_antdiv_reset_statistic(dm, i);
			continue;
		}

		/* @2 Caculate RSSI per Antenna */
		if (fat_tab->main_cnt[i] != 0 || fat_tab->aux_cnt[i] != 0) {
			main_cnt = (u32)fat_tab->main_cnt[i];
			aux_cnt = (u32)fat_tab->aux_cnt[i];
			main_rssi = (main_cnt != 0) ?
				    (fat_tab->main_sum[i] / main_cnt) : 0;
			aux_rssi = (aux_cnt != 0) ?
				   (fat_tab->aux_sum[i] / aux_cnt) : 0;
			if (dm->support_ic_type == ODM_RTL8723D || dm->support_ic_type == ODM_RTL8710C) {
				cond_23d_main = (aux_cnt > main_cnt) &&
						((main_rssi - aux_rssi < 5) ||
						(aux_rssi > main_rssi));
				cond_23d_aux = (main_cnt > aux_cnt) &&
					       ((aux_rssi - main_rssi < 5) ||
					       (main_rssi > aux_rssi));
				if (swat_tab->pre_ant == MAIN_ANT) {
					if (main_cnt == 0)
						target_ant = (aux_cnt != 0) ?
							     AUX_ANT :
							     swat_tab->pre_ant;
					else
						target_ant = cond_23d_main ?
							     AUX_ANT :
							     swat_tab->pre_ant;
				} else {
					if (aux_cnt == 0)
						target_ant = (main_cnt != 0) ?
							     MAIN_ANT :
							     swat_tab->pre_ant;
					else
						target_ant = cond_23d_aux ?
							     MAIN_ANT :
							     swat_tab->pre_ant;
				}
			} else {
				if (swat_tab->pre_ant == MAIN_ANT) {
					target_ant = (aux_rssi > main_rssi) ?
						     AUX_ANT :
						     swat_tab->pre_ant;
				} else if (swat_tab->pre_ant == AUX_ANT) {
					target_ant = (main_rssi > aux_rssi) ?
						     MAIN_ANT :
						     swat_tab->pre_ant;
				}
			}
		} else { /*@CCK only case*/
			main_cnt = fat_tab->main_cnt_cck[i];
			aux_cnt = fat_tab->aux_cnt_cck[i];
			main_rssi = (main_cnt != 0) ?
				    (fat_tab->main_sum_cck[i] / main_cnt) : 0;
			aux_rssi = (aux_cnt != 0) ?
				   (fat_tab->aux_sum_cck[i] / aux_cnt) : 0;
			target_ant = (main_rssi == aux_rssi) ?
				     swat_tab->pre_ant :
				     ((main_rssi >= aux_rssi) ?
				     MAIN_ANT : AUX_ANT);
				     /*Use RSSI for CCK only case*/
		}
		local_max_rssi = (main_rssi >= aux_rssi) ? main_rssi : aux_rssi;
		local_min_rssi = (main_rssi >= aux_rssi) ? aux_rssi : main_rssi;

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "***  CCK_counter_main = (( %d ))  , CCK_counter_aux= ((  %d ))\n",
			  fat_tab->main_cnt_cck[i], fat_tab->aux_cnt_cck[i]);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "***  OFDM_counter_main = (( %d ))  , OFDM_counter_aux= ((  %d ))\n",
			  fat_tab->main_cnt[i], fat_tab->aux_cnt[i]);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "***  main_Cnt = (( %d ))  , aux_Cnt   = (( %d ))\n",
			  main_cnt, aux_cnt);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "***  main_rssi= ((  %d )) , aux_rssi = ((  %d ))\n",
			  main_rssi, aux_rssi);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "*** MAC ID:[ %d ] , target_ant = (( %s ))\n", i,
			  (target_ant == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");

		/* @2 Select RX Idle Antenna */

		if (local_max_rssi != 0 && local_max_rssi < min_max_rssi) {
			rx_idle_ant = target_ant;
			min_max_rssi = local_max_rssi;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "*** local_max_rssi-local_min_rssi = ((%d))\n",
				  (local_max_rssi - local_min_rssi));

			if ((local_max_rssi - local_min_rssi) > 8) {
				if (local_min_rssi != 0) {
					swat_tab->train_time_flag = 3;
				} else {
					if (min_max_rssi > RSSI_CHECK_THRESHOLD)
						swat_tab->train_time_flag = 0;
					else
						swat_tab->train_time_flag = 3;
				}
			} else if ((local_max_rssi - local_min_rssi) > 5) {
				swat_tab->train_time_flag = 2;
			} else if ((local_max_rssi - local_min_rssi) > 2) {
				swat_tab->train_time_flag = 1;
			} else {
				swat_tab->train_time_flag = 0;
			}
		}

		/* @2 Select TX Antenna */
		if (target_ant == MAIN_ANT)
			fat_tab->antsel_a[i] = ANT1_2G;
		else
			fat_tab->antsel_a[i] = ANT2_2G;

		phydm_antdiv_reset_statistic(dm, i);
		pkt_cnt_total += (main_cnt + aux_cnt);
	}

	if (swat_tab->is_sw_ant_div_by_ctrl_frame) {
		odm_s0s1_sw_ant_div_by_ctrl_frame(dm, SWAW_STEP_DETERMINE);
		is_by_ctrl_frame = true;
	}

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "Control frame packet counter = %d, data frame packet counter = %llu\n",
		  swat_tab->pkt_cnt_sw_ant_div_by_ctrl_frame, pkt_cnt_total);

	if (min_max_rssi == 0xff || ((pkt_cnt_total <
	    (swat_tab->pkt_cnt_sw_ant_div_by_ctrl_frame >> 1)) &&
	    dm->phy_dbg_info.num_qry_beacon_pkt < 2)) {
		min_max_rssi = 0;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "Check RSSI of control frame because min_max_rssi == 0xff\n");
		PHYDM_DBG(dm, DBG_ANT_DIV, "is_by_ctrl_frame = %d\n",
			  is_by_ctrl_frame);

		if (is_by_ctrl_frame) {
			main_ctrl_cnt = fat_tab->main_ctrl_cnt;
			aux_ctrl_cnt = fat_tab->aux_ctrl_cnt;
			main_rssi = (main_ctrl_cnt != 0) ?
				    (fat_tab->main_ctrl_sum / main_ctrl_cnt) :
				    0;
			aux_rssi = (aux_ctrl_cnt != 0) ?
				   (fat_tab->aux_ctrl_sum / aux_ctrl_cnt) : 0;

			if (main_ctrl_cnt <= 1 &&
			    fat_tab->cck_ctrl_frame_cnt_main >= 1)
				main_rssi = 0;

			if (aux_ctrl_cnt <= 1 &&
			    fat_tab->cck_ctrl_frame_cnt_aux >= 1)
				aux_rssi = 0;

			if (main_rssi != 0 || aux_rssi != 0) {
				rx_idle_ant = (main_rssi == aux_rssi) ?
					      swat_tab->pre_ant :
					      ((main_rssi >= aux_rssi) ?
					      MAIN_ANT : AUX_ANT);
				local_max_rssi = (main_rssi >= aux_rssi) ?
						 main_rssi : aux_rssi;
				local_min_rssi = (main_rssi >= aux_rssi) ?
						 aux_rssi : main_rssi;

				if ((local_max_rssi - local_min_rssi) > 8)
					swat_tab->train_time_flag = 3;
				else if ((local_max_rssi - local_min_rssi) > 5)
					swat_tab->train_time_flag = 2;
				else if ((local_max_rssi - local_min_rssi) > 2)
					swat_tab->train_time_flag = 1;
				else
					swat_tab->train_time_flag = 0;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Control frame: main_rssi = %d, aux_rssi = %d\n",
					  main_rssi, aux_rssi);
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "rx_idle_ant decided by control frame = %s\n",
					  (rx_idle_ant == MAIN_ANT ?
					  "MAIN" : "AUX"));
			}
		}
	}

	fat_tab->min_max_rssi = min_max_rssi;
	swat_tab->try_flag = SWAW_STEP_PEEK;

	if (swat_tab->double_chk_flag == 1) {
		swat_tab->double_chk_flag = 0;

		if (fat_tab->min_max_rssi > RSSI_CHECK_THRESHOLD) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  " [Double check] min_max_rssi ((%d)) > %d again!!\n",
				  fat_tab->min_max_rssi, RSSI_CHECK_THRESHOLD);

			odm_update_rx_idle_ant(dm, rx_idle_ant);

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[reset try_flag = 0] Training accomplished !!!]\n\n\n");
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  " [Double check] min_max_rssi ((%d)) <= %d !!\n",
				  fat_tab->min_max_rssi, RSSI_CHECK_THRESHOLD);

			next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ?
				   AUX_ANT : MAIN_ANT;
			swat_tab->try_flag = SWAW_STEP_PEEK;
			swat_tab->reset_idx = RSSI_CHECK_RESET_PERIOD;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[set try_flag=0]  Normal state:  Need to tryg again!!\n\n\n");
		}
	} else {
		if (fat_tab->min_max_rssi < RSSI_CHECK_THRESHOLD)
			swat_tab->reset_idx = RSSI_CHECK_RESET_PERIOD;

		swat_tab->pre_ant = rx_idle_ant;
		odm_update_rx_idle_ant(dm, rx_idle_ant);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[reset try_flag = 0] Training accomplished !!!]\n\n\n");
	}
}

void odm_s0s1_sw_ant_div(void *dm_void, u8 step)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u32 value32;
	u8 next_ant = 0;

	if (!dm->is_linked) { /* @is_linked==False */
		PHYDM_DBG(dm, DBG_ANT_DIV, "[No Link!!!]\n");
		if (fat_tab->is_become_linked == true) {
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
			if (dm->support_ic_type == ODM_RTL8723B) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Set REG 948[9:6]=0x0\n");
				odm_set_bb_reg(dm, R_0x948, 0x3c0, 0x0);
			}
			fat_tab->is_become_linked = dm->is_linked;
		}
		return;
	} else {
		if (fat_tab->is_become_linked == false) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Linked !!!]\n");

			if (dm->support_ic_type == ODM_RTL8723B) {
				value32 = odm_get_bb_reg(dm, R_0x864, 0x38);

#if (RTL8723B_SUPPORT == 1)
				if (value32 == 0x0)
					odm_update_rx_idle_ant_8723b(dm,
								     MAIN_ANT,
								     ANT1_2G,
								     ANT2_2G);
				else if (value32 == 0x1)
					odm_update_rx_idle_ant_8723b(dm,
								     AUX_ANT,
								     ANT2_2G,
								     ANT1_2G);
#endif

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "8723B: First link! Force antenna to  %s\n",
					  (value32 == 0x0 ? "MAIN" : "AUX"));
			}

			if (dm->support_ic_type == ODM_RTL8723D) {
				value32 = odm_get_bb_reg(dm, R_0x864, 0x38);
#if (RTL8723D_SUPPORT == 1)
				if (value32 == 0x0)
					odm_update_rx_idle_ant_8723d(dm,
								     MAIN_ANT,
								     ANT1_2G,
								     ANT2_2G);
				else if (value32 == 0x1)
					odm_update_rx_idle_ant_8723d(dm,
								     AUX_ANT,
								     ANT2_2G,
								     ANT1_2G);
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "8723D: First link! Force antenna to  %s\n",
					  (value32 == 0x0 ? "MAIN" : "AUX"));
#endif
			}
			if (dm->support_ic_type == ODM_RTL8710C) {
#if (RTL8710C_SUPPORT == 1)
				value32 = (HAL_READ32(SYSTEM_CTRL_BASE, R_0x121c) & 0x800000);
				if (value32 == 0x0)
					odm_update_rx_idle_ant_8710c(dm,
								     MAIN_ANT,
								     ANT1_2G,
								     ANT2_2G);
				else if (value32 == 0x1)
					odm_update_rx_idle_ant_8710c(dm,
								     AUX_ANT,
								     ANT2_2G,
								     ANT1_2G);
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "8710C: First link! Force antenna to  %s\n",
					  (value32 == 0x0 ? "MAIN" : "AUX"));
#endif
			}
			fat_tab->is_become_linked = dm->is_linked;
		}
	}

	if (!(*fat_tab->p_force_tx_by_desc)) {
		if (dm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	}

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[%d] { try_flag=(( %d )), step=(( %d )), double_chk_flag = (( %d )) }\n",
		  __LINE__, swat_tab->try_flag, step,
		  swat_tab->double_chk_flag);

	/* @ Handling step mismatch condition. */
	/* @ Peak step is not finished at last time. */
	/* @ Recover the variable and check again. */
	if (step != swat_tab->try_flag) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[step != try_flag]    Need to Reset After Link\n");
		odm_sw_ant_div_rest_after_link(dm);
	}

	if (swat_tab->try_flag == SWAW_STEP_INIT) {
		swat_tab->try_flag = SWAW_STEP_PEEK;
		swat_tab->train_time_flag = 0;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[set try_flag = 0]  Prepare for peek!\n\n");
		return;

	} else {
		/* @1 Normal state (Begin Trying) */
		if (swat_tab->try_flag == SWAW_STEP_PEEK) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "TxOkCnt=(( %llu )), RxOkCnt=(( %llu )), traffic_load = (%d))\n",
				  dm->cur_tx_ok_cnt, dm->cur_rx_ok_cnt,
				  dm->traffic_load);
			phydm_sw_antdiv_train_time(dm);

			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "Current min_max_rssi is ((%d))\n",
				  fat_tab->min_max_rssi);

			/* @---reset index--- */
			if (swat_tab->reset_idx >= RSSI_CHECK_RESET_PERIOD) {
				fat_tab->min_max_rssi = 0;
				swat_tab->reset_idx = 0;
			}
			PHYDM_DBG(dm, DBG_ANT_DIV, "reset_idx = (( %d ))\n",
				  swat_tab->reset_idx);

			swat_tab->reset_idx++;

			/* @---double check flag--- */
			if (fat_tab->min_max_rssi > RSSI_CHECK_THRESHOLD &&
			    swat_tab->double_chk_flag == 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  " min_max_rssi is ((%d)), and > %d\n",
					  fat_tab->min_max_rssi,
					  RSSI_CHECK_THRESHOLD);

				swat_tab->double_chk_flag = 1;
				swat_tab->try_flag = SWAW_STEP_DETERMINE;
				swat_tab->rssi_trying = 0;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Test the current ant for (( %d )) ms again\n",
					  swat_tab->train_time);
				odm_update_rx_idle_ant(dm,
						       fat_tab->rx_idle_ant);
				odm_set_timer(dm, &swat_tab->sw_antdiv_timer,
					      swat_tab->train_time); /*@ms*/
				return;
			}

			next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ?
				   AUX_ANT : MAIN_ANT;

			swat_tab->try_flag = SWAW_STEP_DETERMINE;

			if (swat_tab->reset_idx <= 1)
				swat_tab->rssi_trying = 2;
			else
				swat_tab->rssi_trying = 1;

			odm_s0s1_sw_ant_div_by_ctrl_frame(dm, SWAW_STEP_PEEK);
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[set try_flag=1]  Normal state:  Begin Trying!!\n");

		} else if ((swat_tab->try_flag == SWAW_STEP_DETERMINE) &&
			   (swat_tab->double_chk_flag == 0)) {
			next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ?
				   AUX_ANT : MAIN_ANT;
			swat_tab->rssi_trying--;
		}

		/* @1 Decision state */
		if (swat_tab->try_flag == SWAW_STEP_DETERMINE &&
		    swat_tab->rssi_trying == 0) {
			phydm_sw_antdiv_decision(dm);
			return;
		}
	}

	/* @1 4.Change TRX antenna */

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "rssi_trying = (( %d )),    ant: (( %s )) >>> (( %s ))\n",
		  swat_tab->rssi_trying,
		  (fat_tab->rx_idle_ant == MAIN_ANT ? "MAIN" : "AUX"),
		  (next_ant == MAIN_ANT ? "MAIN" : "AUX"));

	odm_update_rx_idle_ant(dm, next_ant);

	/* @1 5.Reset Statistics */

	fat_tab->rx_idle_ant = next_ant;

	if (dm->support_ic_type == ODM_RTL8723D || dm->support_ic_type == ODM_RTL8710C) {

		if (fat_tab->rx_idle_ant == MAIN_ANT) {
			fat_tab->main_sum[0] = 0;
			fat_tab->main_cnt[0] = 0;
			fat_tab->main_sum_cck[0] = 0;
			fat_tab->main_cnt_cck[0] = 0;
		} else {
			fat_tab->aux_sum[0] = 0;
			fat_tab->aux_cnt[0] = 0;
			fat_tab->aux_sum_cck[0] = 0;
			fat_tab->aux_cnt_cck[0] = 0;
		}
	}

	if (dm->support_ic_type == ODM_RTL8188F) {
		if (dm->support_interface == ODM_ITRF_SDIO) {
			ODM_delay_us(200);

			if (fat_tab->rx_idle_ant == MAIN_ANT) {
				fat_tab->main_sum[0] = 0;
				fat_tab->main_cnt[0] = 0;
				fat_tab->main_sum_cck[0] = 0;
				fat_tab->main_cnt_cck[0] = 0;
			} else {
				fat_tab->aux_sum[0] = 0;
				fat_tab->aux_cnt[0] = 0;
				fat_tab->aux_sum_cck[0] = 0;
				fat_tab->aux_cnt_cck[0] = 0;
			}
		}
	}
	/* @1 6.Set next timer   (Trying state) */
	PHYDM_DBG(dm, DBG_ANT_DIV, " Test ((%s)) ant for (( %d )) ms\n",
		  (next_ant == MAIN_ANT ? "MAIN" : "AUX"),
		  swat_tab->train_time);
	odm_set_timer(dm, &swat_tab->sw_antdiv_timer, swat_tab->train_time);
								/*@ms*/
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void odm_sw_antdiv_callback(struct phydm_timer_list *timer)
{
	void *adapter = (void *)timer->Adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct sw_antenna_switch *swat_tab = &hal_data->DM_OutSrc.dm_swat_table;

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&swat_tab->phydm_sw_antenna_switch_workitem);
#else
	{
#if 0
		/* @dbg_print("SW_antdiv_Callback"); */
#endif
		odm_s0s1_sw_ant_div(&hal_data->DM_OutSrc, SWAW_STEP_DETERMINE);
	}
#endif
#else
	odm_schedule_work_item(&swat_tab->phydm_sw_antenna_switch_workitem);
#endif
}

void odm_sw_antdiv_workitem_callback(void *context)
{
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));

#if 0
	/* @dbg_print("SW_antdiv_Workitem_Callback"); */
#endif
	odm_s0s1_sw_ant_div(&hal_data->DM_OutSrc, SWAW_STEP_DETERMINE);
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

void odm_sw_antdiv_workitem_callback(void *context)
{
	void *
		adapter = (void *)context;
	HAL_DATA_TYPE
	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));

#if 0
	/*@dbg_print("SW_antdiv_Workitem_Callback");*/
#endif
	odm_s0s1_sw_ant_div(&hal_data->odmpriv, SWAW_STEP_DETERMINE);
}

void odm_sw_antdiv_callback(void *function_context)
{
	struct dm_struct *dm = (struct dm_struct *)function_context;
	void *padapter = dm->adapter;
	if (*dm->is_net_closed == true)
		return;

#if 0 /* @Can't do I/O in timer callback*/
	odm_s0s1_sw_ant_div(dm, SWAW_STEP_DETERMINE);
#else
	rtw_run_in_thread_cmd(padapter, odm_sw_antdiv_workitem_callback,
			      padapter);
#endif
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)
void odm_sw_antdiv_callback(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "******AntDiv_Callback******\n");
	odm_s0s1_sw_ant_div(dm, SWAW_STEP_DETERMINE);
}
#endif

void odm_s0s1_sw_ant_div_by_ctrl_frame(void *dm_void, u8 step)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	switch (step) {
	case SWAW_STEP_PEEK:
		swat_tab->pkt_cnt_sw_ant_div_by_ctrl_frame = 0;
		swat_tab->is_sw_ant_div_by_ctrl_frame = true;
		fat_tab->main_ctrl_cnt = 0;
		fat_tab->aux_ctrl_cnt = 0;
		fat_tab->main_ctrl_sum = 0;
		fat_tab->aux_ctrl_sum = 0;
		fat_tab->cck_ctrl_frame_cnt_main = 0;
		fat_tab->cck_ctrl_frame_cnt_aux = 0;
		fat_tab->ofdm_ctrl_frame_cnt_main = 0;
		fat_tab->ofdm_ctrl_frame_cnt_aux = 0;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "odm_S0S1_SwAntDivForAPMode(): Start peek and reset counter\n");
		break;
	case SWAW_STEP_DETERMINE:
		swat_tab->is_sw_ant_div_by_ctrl_frame = false;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "odm_S0S1_SwAntDivForAPMode(): Stop peek\n");
		break;
	default:
		swat_tab->is_sw_ant_div_by_ctrl_frame = false;
		break;
	}
}

void odm_antsel_statistics_ctrl(void *dm_void, u8 antsel_tr_mux,
				u32 rx_pwdb_all)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (antsel_tr_mux == ANT1_2G) {
		fat_tab->main_ctrl_sum += rx_pwdb_all;
		fat_tab->main_ctrl_cnt++;
	} else {
		fat_tab->aux_ctrl_sum += rx_pwdb_all;
		fat_tab->aux_ctrl_cnt++;
	}
}

void odm_s0s1_sw_ant_div_by_ctrl_frame_process_rssi(void *dm_void,
						    void *phy_info_void,
						    void *pkt_info_void
	/*	struct phydm_phyinfo_struct*		phy_info, */
	/*	struct phydm_perpkt_info_struct*		pktinfo */
	)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 rssi_cck;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (dm->ant_div_type != S0S1_SW_ANTDIV)
		return;

	/* @In try state */
	if (!swat_tab->is_sw_ant_div_by_ctrl_frame)
		return;

	/* No HW error and match receiver address */
	if (!pktinfo->is_to_self)
		return;

	swat_tab->pkt_cnt_sw_ant_div_by_ctrl_frame++;

	if (pktinfo->is_cck_rate) {
		rssi_cck = phy_info->rx_mimo_signal_strength[RF_PATH_A];
		fat_tab->antsel_rx_keep_0 = (fat_tab->rx_idle_ant == MAIN_ANT) ?
					    ANT1_2G : ANT2_2G;

		if (fat_tab->antsel_rx_keep_0 == ANT1_2G)
			fat_tab->cck_ctrl_frame_cnt_main++;
		else
			fat_tab->cck_ctrl_frame_cnt_aux++;

		odm_antsel_statistics_ctrl(dm, fat_tab->antsel_rx_keep_0,
					   rssi_cck);
	} else {
		fat_tab->antsel_rx_keep_0 = (fat_tab->rx_idle_ant == MAIN_ANT) ?
					    ANT1_2G : ANT2_2G;

		if (fat_tab->antsel_rx_keep_0 == ANT1_2G)
			fat_tab->ofdm_ctrl_frame_cnt_main++;
		else
			fat_tab->ofdm_ctrl_frame_cnt_aux++;

		odm_antsel_statistics_ctrl(dm, fat_tab->antsel_rx_keep_0,
					   phy_info->rx_pwdb_all);
	}
}

#endif /* @#if (RTL8723B_SUPPORT == 1) || (RTL8821A_SUPPORT == 1) */

void odm_set_next_mac_addr_target(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct cmn_sta_info *entry;
	u32 value32, i;

	PHYDM_DBG(dm, DBG_ANT_DIV, "%s ==>\n", __func__);

	if (dm->is_linked) {
		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
			if ((fat_tab->train_idx + 1) == ODM_ASSOCIATE_ENTRY_NUM)
				fat_tab->train_idx = 0;
			else
				fat_tab->train_idx++;

			entry = dm->phydm_sta_info[fat_tab->train_idx];

			if (is_sta_active(entry)) {
				/*@Match MAC ADDR*/
				value32 = (entry->mac_addr[5] << 8) | entry->mac_addr[4];

				odm_set_mac_reg(dm, R_0x7b4, 0xFFFF, value32); /*@0x7b4~0x7b5*/

				value32 = (entry->mac_addr[3] << 24) | (entry->mac_addr[2] << 16) | (entry->mac_addr[1] << 8) | entry->mac_addr[0];

				odm_set_mac_reg(dm, R_0x7b0, MASKDWORD, value32); /*@0x7b0~0x7b3*/

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "fat_tab->train_idx=%d\n",
					  fat_tab->train_idx);

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Training MAC addr = %x:%x:%x:%x:%x:%x\n",
					  entry->mac_addr[5],
					  entry->mac_addr[4],
					  entry->mac_addr[3],
					  entry->mac_addr[2],
					  entry->mac_addr[1],
					  entry->mac_addr[0]);

				break;
			}
		}
	}
}

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))

void odm_fast_ant_training(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	u32 max_rssi_path_a = 0, pckcnt_path_a = 0;
	u8 i, target_ant_path_a = 0;
	boolean is_pkt_filter_macth_path_a = false;
#if (RTL8192E_SUPPORT == 1)
	u32 max_rssi_path_b = 0, pckcnt_path_b = 0;
	u8 target_ant_path_b = 0;
	boolean is_pkt_filter_macth_path_b = false;
#endif

	if (!dm->is_linked) { /* @is_linked==False */
		PHYDM_DBG(dm, DBG_ANT_DIV, "[No Link!!!]\n");

		if (fat_tab->is_become_linked == true) {
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
			phydm_fast_training_enable(dm, FAT_OFF);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
			fat_tab->is_become_linked = dm->is_linked;
		}
		return;
	} else {
		if (fat_tab->is_become_linked == false) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[Linked!!!]\n");
			fat_tab->is_become_linked = dm->is_linked;
		}
	}

	if (!(*fat_tab->p_force_tx_by_desc)) {
		if (dm->is_one_entry_only == true)
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);
		else
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	}

	if (dm->support_ic_type == ODM_RTL8188E)
		odm_set_bb_reg(dm, R_0x864, BIT(2) | BIT(1) | BIT(0), ((dm->fat_comb_a) - 1));
#if (RTL8192E_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8192E) {
		odm_set_bb_reg(dm, R_0xb38, BIT(2) | BIT(1) | BIT(0), ((dm->fat_comb_a) - 1)); /* path-A  */ /* ant combination=regB38[2:0]+1 */
		odm_set_bb_reg(dm, R_0xb38, BIT(18) | BIT(17) | BIT(16), ((dm->fat_comb_b) - 1)); /* path-B  */ /* ant combination=regB38[18:16]+1 */
	}
#endif

	PHYDM_DBG(dm, DBG_ANT_DIV, "==>%s\n", __func__);

	/* @1 TRAINING STATE */
	if (fat_tab->fat_state == FAT_TRAINING_STATE) {
		/* @2 Caculate RSSI per Antenna */

		/* @3 [path-A]--------------------------- */
		for (i = 0; i < (dm->fat_comb_a); i++) { /* @i : antenna index */
			if (fat_tab->ant_rssi_cnt[i] == 0)
				fat_tab->ant_ave_rssi[i] = 0;
			else {
				fat_tab->ant_ave_rssi[i] = fat_tab->ant_sum_rssi[i] / fat_tab->ant_rssi_cnt[i];
				is_pkt_filter_macth_path_a = true;
			}

			if (fat_tab->ant_ave_rssi[i] > max_rssi_path_a) {
				max_rssi_path_a = fat_tab->ant_ave_rssi[i];
				pckcnt_path_a = fat_tab->ant_rssi_cnt[i];
				target_ant_path_a = i;
			} else if (fat_tab->ant_ave_rssi[i] == max_rssi_path_a) {
				if (fat_tab->ant_rssi_cnt[i] > pckcnt_path_a) {
					max_rssi_path_a = fat_tab->ant_ave_rssi[i];
					pckcnt_path_a = fat_tab->ant_rssi_cnt[i];
					target_ant_path_a = i;
				}
			}

			PHYDM_DBG(
				  "*** ant-index : [ %d ],      counter = (( %d )),     Avg RSSI = (( %d ))\n",
				  i, fat_tab->ant_rssi_cnt[i],
				  fat_tab->ant_ave_rssi[i]);
		}

#if 0
#if (RTL8192E_SUPPORT == 1)
		/* @3 [path-B]--------------------------- */
		for (i = 0; i < (dm->fat_comb_b); i++) {
			if (fat_tab->antRSSIcnt_pathB[i] == 0)
				fat_tab->antAveRSSI_pathB[i] = 0;
			else { /*  @(ant_rssi_cnt[i] != 0) */
				fat_tab->antAveRSSI_pathB[i] = fat_tab->antSumRSSI_pathB[i] / fat_tab->antRSSIcnt_pathB[i];
				is_pkt_filter_macth_path_b = true;
			}
			if (fat_tab->antAveRSSI_pathB[i] > max_rssi_path_b) {
				max_rssi_path_b = fat_tab->antAveRSSI_pathB[i];
				pckcnt_path_b = fat_tab->antRSSIcnt_pathB[i];
				target_ant_path_b = (u8)i;
			}
			if (fat_tab->antAveRSSI_pathB[i] == max_rssi_path_b) {
				if (fat_tab->antRSSIcnt_pathB > pckcnt_path_b) {
					max_rssi_path_b = fat_tab->antAveRSSI_pathB[i];
					target_ant_path_b = (u8)i;
				}
			}
			if (dm->fat_print_rssi == 1) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "***{path-B}: Sum RSSI[%d] = (( %d )),      cnt RSSI [%d] = (( %d )),     Avg RSSI[%d] = (( %d ))\n",
					  i, fat_tab->antSumRSSI_pathB[i], i,
					  fat_tab->antRSSIcnt_pathB[i], i,
					  fat_tab->antAveRSSI_pathB[i]);
			}
		}
#endif
#endif

		/* @1 DECISION STATE */

		/* @2 Select TRX Antenna */

		phydm_fast_training_enable(dm, FAT_OFF);

		/* @3 [path-A]--------------------------- */
		if (is_pkt_filter_macth_path_a == false) {
#if 0
			/* PHYDM_DBG(dm,DBG_ANT_DIV, "{path-A}: None Packet is matched\n"); */
#endif
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "{path-A}: None Packet is matched\n");
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
		} else {
			PHYDM_DBG(
				  "target_ant_path_a = (( %d )) , max_rssi_path_a = (( %d ))\n",
				  target_ant_path_a, max_rssi_path_a);

			/* @3 [ update RX-optional ant ]        Default RX is Omni, Optional RX is the best decision by FAT */
			if (dm->support_ic_type == ODM_RTL8188E)
				odm_set_bb_reg(dm, R_0x864, BIT(8) | BIT(7) | BIT(6), target_ant_path_a);
			else if (dm->support_ic_type == ODM_RTL8192E)
				odm_set_bb_reg(dm, R_0xb38, BIT(8) | BIT(7) | BIT(6), target_ant_path_a); /* Optional RX [pth-A] */

			/* @3 [ update TX ant ] */
			odm_update_tx_ant(dm, target_ant_path_a, (fat_tab->train_idx));

			if (target_ant_path_a == 0)
				odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
		}
#if 0
#if (RTL8192E_SUPPORT == 1)
		/* @3 [path-B]--------------------------- */
		if (is_pkt_filter_macth_path_b == false) {
			if (dm->fat_print_rssi == 1)
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "***[%d]{path-B}: None Packet is matched\n\n\n",
					  __LINE__);
		} else {
			if (dm->fat_print_rssi == 1) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  " ***target_ant_path_b = (( %d )) *** max_rssi = (( %d ))***\n\n\n",
					  target_ant_path_b, max_rssi_path_b);
			}
			odm_set_bb_reg(dm, R_0xb38, BIT(21) | BIT20 | BIT19, target_ant_path_b);	/* @Default RX is Omni, Optional RX is the best decision by FAT */
			odm_set_bb_reg(dm, R_0x80c, BIT(21), 1); /* Reg80c[21]=1'b1		//from TX Info */

			fat_tab->antsel_pathB[fat_tab->train_idx] = target_ant_path_b;
		}
#endif
#endif

		/* @2 Reset counter */
		for (i = 0; i < (dm->fat_comb_a); i++) {
			fat_tab->ant_sum_rssi[i] = 0;
			fat_tab->ant_rssi_cnt[i] = 0;
		}
		/*@
		#if (RTL8192E_SUPPORT == 1)
		for(i=0; i<=(dm->fat_comb_b); i++)
		{
			fat_tab->antSumRSSI_pathB[i] = 0;
			fat_tab->antRSSIcnt_pathB[i] = 0;
		}
		#endif
		*/

		fat_tab->fat_state = FAT_PREPARE_STATE;
		return;
	}

	/* @1 NORMAL STATE */
	if (fat_tab->fat_state == FAT_PREPARE_STATE) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "[ Start Prepare state ]\n");

		odm_set_next_mac_addr_target(dm);

		/* @2 Prepare Training */
		fat_tab->fat_state = FAT_TRAINING_STATE;
		phydm_fast_training_enable(dm, FAT_ON);
		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
		/* @enable HW AntDiv */
		PHYDM_DBG(dm, DBG_ANT_DIV, "[Start Training state]\n");

		odm_set_timer(dm, &dm->fast_ant_training_timer, dm->antdiv_intvl); /* @ms */
	}
}

void odm_fast_ant_training_callback(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (*(dm->is_net_closed) == true)
		return;
#endif

#if USE_WORKITEM
	odm_schedule_work_item(&dm->fast_ant_training_workitem);
#else
	PHYDM_DBG(dm, DBG_ANT_DIV, "******%s******\n", __func__);
	odm_fast_ant_training(dm);
#endif
}

void odm_fast_ant_training_work_item_callback(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, DBG_ANT_DIV, "******%s******\n", __func__);
	odm_fast_ant_training(dm);
}

#endif

void odm_ant_div_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct sw_antenna_switch *swat_tab = &dm->dm_swat_table;
       u8 i;
	if (!(dm->support_ability & ODM_BB_ANT_DIV)) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Return!!!]   Not Support Antenna Diversity Function\n");
		return;
	}
/* @--- */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (fat_tab->ant_div_2g_5g == ODM_ANTDIV_2G) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[2G AntDiv Init]: Only Support 2G Antenna Diversity Function\n");
		if (!(dm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	} else if (fat_tab->ant_div_2g_5g == ODM_ANTDIV_5G) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[5G AntDiv Init]: Only Support 5G Antenna Diversity Function\n");
		if (!(dm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC))
			return;
	} else if (fat_tab->ant_div_2g_5g == (ODM_ANTDIV_2G | ODM_ANTDIV_5G))
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[2G & 5G AntDiv Init]:Support Both 2G & 5G Antenna Diversity Function\n");

#endif
	/* @--- */

	/* @2 [--General---] */
	dm->antdiv_period = 0;

	fat_tab->is_become_linked = false;
	fat_tab->ant_div_on_off = 0xff;
	
	for(i=0;i<3;i++) 
		fat_tab->ant_idx_vec[i]=i+1; /* initialize ant_idx_vec for SP3T */
	

/* @3       -   AP   - */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)

#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	odm_bdc_init(dm);
#endif
#endif

/* @3     -   WIN   - */
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	swat_tab->ant_5g = MAIN_ANT;
	swat_tab->ant_2g = MAIN_ANT;
//#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)
//	swat_tab->ant_2g = MAIN_ANT;
#endif

	/* @2 [---Set MAIN_ANT as default antenna if Auto-ant enable---] */
	if (fat_tab->div_path_type == ANT_PATH_A)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
	else if (fat_tab->div_path_type == ANT_PATH_B)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_B);
	else if (fat_tab->div_path_type == ANT_PATH_AB)
		odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_AB);

	dm->ant_type = ODM_AUTO_ANT;

	fat_tab->rx_idle_ant = 0xff;

	if (dm->support_ic_type == ODM_RTL8710C) {
		/* Soft ware*/
#if (RTL8710C_SUPPORT == 1)
		if (dm->ant_div_type == S0S1_SW_ANTDIV) {
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xdc, HAL_READ32(SYSTEM_CTRL_BASE, R_0xdc) | BIT18 | BIT17 | BIT16);
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xac, HAL_READ32(SYSTEM_CTRL_BASE, R_0xac) | BIT24 | BIT6);
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x10, 0x307);// 1: enable gpio db32 clock , 1: enable gpio pclock
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x08, 0x80000111);
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0x1208, 0x800000);
		} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xdc, HAL_READ32(SYSTEM_CTRL_BASE, R_0xdc) | BIT18 | BIT17);
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xdc, HAL_READ32(SYSTEM_CTRL_BASE, R_0xdc) & (~BIT16));
			HAL_WRITE32(SYSTEM_CTRL_BASE, R_0xac, HAL_READ32(SYSTEM_CTRL_BASE, R_0xac) | BIT24 | BIT6);
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				"[Return!!!] 8710C  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
#endif 
	}
	
	/*to make RX-idle-antenna will be updated absolutly*/
	odm_update_rx_idle_ant(dm, MAIN_ANT);
	phydm_keep_rx_ack_ant_by_tx_ant_time(dm, 0);
	/* Timming issue: keep Rx ant after tx for ACK(5 x 3.2 mu = 16mu sec)*/

	/* @2 [---Set TX Antenna---] */
	if (!fat_tab->p_force_tx_by_desc) {
		fat_tab->force_tx_by_desc = 0;
		fat_tab->p_force_tx_by_desc = &fat_tab->force_tx_by_desc;
	}
	PHYDM_DBG(dm, DBG_ANT_DIV, "p_force_tx_by_desc = %d\n",
		  *fat_tab->p_force_tx_by_desc);

	if (*fat_tab->p_force_tx_by_desc)
		odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	else
		odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);

	/* @2 [--88E---] */
	if (dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		/* @dm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */
		/* @dm->ant_div_type = CG_TRX_SMART_ANTDIV; */

		if (dm->ant_div_type != CGCS_RX_HW_ANTDIV &&
		    dm->ant_div_type != CG_TRX_HW_ANTDIV &&
		    dm->ant_div_type != CG_TRX_SMART_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  88E Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_rx_hw_ant_div_init_88e(dm);
		else if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_88e(dm);
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (dm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_smart_hw_ant_div_init_88e(dm);
#endif
#endif
	}

/* @2 [--92E---] */
#if (RTL8192E_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8192E) {
		/* @dm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */
		/* @dm->ant_div_type = CG_TRX_SMART_ANTDIV; */

		if (dm->ant_div_type != CGCS_RX_HW_ANTDIV &&
		    dm->ant_div_type != CG_TRX_HW_ANTDIV &&
		    dm->ant_div_type != CG_TRX_SMART_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8192E Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_rx_hw_ant_div_init_92e(dm);
		else if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_92e(dm);
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (dm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_smart_hw_ant_div_init_92e(dm);
#endif
	}
#endif

	/* @2 [--92F---] */
#if (RTL8192F_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8192F) {
	/* @dm->ant_div_type = CGCS_RX_HW_ANTDIV; */
	/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */
	/* @dm->ant_div_type = CG_TRX_SMART_ANTDIV; */

	if (dm->ant_div_type != CGCS_RX_HW_ANTDIV) {
		if (dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8192F Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
	}
	if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
		odm_rx_hw_ant_div_init_92f(dm);
	else if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
	odm_trx_hw_ant_div_init_92f(dm);
	}
#endif

#if (RTL8197F_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8197F) {
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;

		if (dm->ant_div_type != CGCS_RX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8197F Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		phydm_rx_hw_ant_div_init_97f(dm);
	}
#endif

#if (RTL8197G_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8197G) {
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;

		if (dm->ant_div_type != CGCS_RX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8197F Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		phydm_rx_hw_ant_div_init_97g(dm);
	}
#endif
/* @2 [--8723B---] */
#if (RTL8723B_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8723B) {
		dm->ant_div_type = S0S1_SW_ANTDIV;
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (dm->ant_div_type != S0S1_SW_ANTDIV &&
		    dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!] 8723B  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		if (dm->ant_div_type == S0S1_SW_ANTDIV)
			odm_s0s1_sw_ant_div_init_8723b(dm);
		else if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8723b(dm);
	}
#endif
/*@2 [--8723D---]*/
#if (RTL8723D_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8723D) {
		if (fat_tab->p_default_s0_s1 == NULL) {
			fat_tab->default_s0_s1 = 1;
			fat_tab->p_default_s0_s1 = &fat_tab->default_s0_s1;
		}
		PHYDM_DBG(dm, DBG_ANT_DIV, "default_s0_s1 = %d\n",
			  *fat_tab->p_default_s0_s1);

		if (*fat_tab->p_default_s0_s1 == true)
			odm_update_rx_idle_ant(dm, MAIN_ANT);
		else
			odm_update_rx_idle_ant(dm, AUX_ANT);

		if (dm->ant_div_type == S0S1_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8723d(dm);
		else if (dm->ant_div_type == S0S1_SW_ANTDIV)
			odm_s0s1_sw_ant_div_init_8723d(dm);
		else {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				"[Return!!!] 8723D  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
	}
#endif
#if (RTL8710C_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8710C) {
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8710c(dm);
		else if(dm->ant_div_type == S0S1_SW_ANTDIV){
			if (fat_tab->p_default_s0_s1 == NULL){
				fat_tab->default_s0_s1 = 1;
				fat_tab->p_default_s0_s1 = &fat_tab->default_s0_s1;
				}
			PHYDM_DBG(dm, DBG_ANT_DIV, "default_s0_s1 = %d\n",
				*fat_tab->p_default_s0_s1);
			if (*fat_tab->p_default_s0_s1 == true)
				odm_update_rx_idle_ant(dm, MAIN_ANT);
			else
				odm_update_rx_idle_ant(dm, AUX_ANT);
			odm_s0s1_sw_ant_div_init_8710c(dm);
			}
	}
#endif
#if (RTL8721D_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8721D) {
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8721D Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_trx_hw_ant_div_init_8721d(dm);
	}
#endif
/* @2 [--8811A 8821A---] */
#if (RTL8821A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8821) {
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
		dm->ant_div_type = HL_SW_SMART_ANT_TYPE1;

		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {
			odm_trx_hw_ant_div_init_8821a(dm);
			phydm_hl_smart_ant_type1_init_8821a(dm);
		} else
#endif
		{
#ifdef ODM_CONFIG_BT_COEXIST
			dm->ant_div_type = S0S1_SW_ANTDIV;
#else
			dm->ant_div_type = CG_TRX_HW_ANTDIV;
#endif

			if (dm->ant_div_type != CG_TRX_HW_ANTDIV &&
			    dm->ant_div_type != S0S1_SW_ANTDIV) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[Return!!!] 8821A & 8811A  Not Supprrt This AntDiv type\n");
				dm->support_ability &= ~(ODM_BB_ANT_DIV);
				return;
			}
			if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
				odm_trx_hw_ant_div_init_8821a(dm);
			else if (dm->ant_div_type == S0S1_SW_ANTDIV)
				odm_s0s1_sw_ant_div_init_8821a(dm);
		}
	}
#endif

/* @2 [--8821C---] */
#if (RTL8821C_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8821C) {
		dm->ant_div_type = S0S1_SW_ANTDIV;
		if (dm->ant_div_type != S0S1_SW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!] 8821C  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		phydm_s0s1_sw_ant_div_init_8821c(dm);
		odm_trx_hw_ant_div_init_8821c(dm);
	}
#endif

/* @2 [--8195B---] */
#if (RTL8195B_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8195B) {
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
		if (dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!] 8821C  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		odm_trx_hw_ant_div_init_8195b(dm);
	}
#endif

/* @2 [--8881A---] */
#if (RTL8881A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8881A) {
		/* @dm->ant_div_type = CGCS_RX_HW_ANTDIV; */
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_trx_hw_ant_div_init_8881a(dm);
		} else {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!] 8881A  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}

		odm_trx_hw_ant_div_init_8881a(dm);
	}
#endif

/* @2 [--8812---] */
#if (RTL8812A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8812) {
		/* @dm->ant_div_type = CG_TRX_HW_ANTDIV; */

		if (dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!] 8812A  Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		odm_trx_hw_ant_div_init_8812a(dm);
	}
#endif

/*@[--8188F---]*/
#if (RTL8188F_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8188F) {
		dm->ant_div_type = S0S1_SW_ANTDIV;
		odm_s0s1_sw_ant_div_init_8188f(dm);
	}
#endif

/*@[--8822B---]*/
#if (RTL8822B_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8822B) {
		dm->ant_div_type = CG_TRX_HW_ANTDIV;

		if (dm->ant_div_type != CG_TRX_HW_ANTDIV) {
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[Return!!!]  8822B Not Supprrt This AntDiv type\n");
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
			return;
		}
		phydm_trx_hw_ant_div_init_22b(dm);
#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		dm->ant_div_type = HL_SW_SMART_ANT_TYPE2;

		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE2)
			phydm_hl_smart_ant_type2_init_8822b(dm);
#endif
	}
#endif

/*@PHYDM_DBG(dm, DBG_ANT_DIV, "*** support_ic_type=[%lu]\n",*/
/*dm->support_ic_type);*/
/*PHYDM_DBG(dm, DBG_ANT_DIV, "*** AntDiv support_ability=[%lu]\n",*/
/*	  (dm->support_ability & ODM_BB_ANT_DIV)>>6);*/
/*PHYDM_DBG(dm, DBG_ANT_DIV, "*** AntDiv type=[%d]\n",dm->ant_div_type);*/
}

void odm_ant_div(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *adapter = dm->adapter;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
#if (defined(CONFIG_HL_SMART_ANTENNA))
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
#endif

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (dm->is_linked) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "tp_active_occur=((%d)), evm_method_enable=((%d))\n",
			  dm->tp_active_occur, fat_tab->evm_method_enable);

		if (dm->tp_active_occur == 1 &&
		    fat_tab->evm_method_enable == 1) {
			fat_tab->idx_ant_div_counter_5g = dm->antdiv_period;
			fat_tab->idx_ant_div_counter_2g = dm->antdiv_period;
		}
	}
#endif

	if (*dm->band_type == ODM_BAND_5G) {
		if (fat_tab->idx_ant_div_counter_5g < dm->antdiv_period) {
			fat_tab->idx_ant_div_counter_5g++;
			return;
		} else
			fat_tab->idx_ant_div_counter_5g = 0;
	} else if (*dm->band_type == ODM_BAND_2_4G) {
		if (fat_tab->idx_ant_div_counter_2g < dm->antdiv_period) {
			fat_tab->idx_ant_div_counter_2g++;
			return;
		} else
			fat_tab->idx_ant_div_counter_2g = 0;
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN || DM_ODM_SUPPORT_TYPE == ODM_CE)

	if (fat_tab->enable_ctrl_frame_antdiv) {
		if (dm->data_frame_num <= 10 && dm->is_linked)
			fat_tab->use_ctrl_frame_antdiv = 1;
		else
			fat_tab->use_ctrl_frame_antdiv = 0;

		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "use_ctrl_frame_antdiv = (( %d )), data_frame_num = (( %d ))\n",
			  fat_tab->use_ctrl_frame_antdiv, dm->data_frame_num);
		dm->data_frame_num = 0;
	}

	{
#ifdef PHYDM_BEAMFORMING_SUPPORT

		enum beamforming_cap beamform_cap = phydm_get_beamform_cap(dm);
		PHYDM_DBG(dm, DBG_ANT_DIV, "is_bt_continuous_turn = ((%d))\n",
			  dm->is_bt_continuous_turn);
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ AntDiv Beam Cap ]   cap= ((%d))\n", beamform_cap);
		if (!dm->is_bt_continuous_turn) {
			if ((beamform_cap & BEAMFORMEE_CAP) &&
			    (!(*fat_tab->is_no_csi_feedback))) {
			    /* @BFmee On  &&   Div On->Div Off */
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ AntDiv : OFF ]   BFmee ==1; cap= ((%d))\n",
					  beamform_cap);
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "[ AntDiv BF]   is_no_csi_feedback= ((%d))\n",
					  *(fat_tab->is_no_csi_feedback));
				if (fat_tab->fix_ant_bfee == 0) {
					odm_ant_div_on_off(dm, ANTDIV_OFF,
							   ANT_PATH_A);
					fat_tab->fix_ant_bfee = 1;
				}
				return;
			} else { /* @BFmee Off   &&   Div Off->Div On */
				if (fat_tab->fix_ant_bfee == 1 &&
				    dm->is_linked) {
					PHYDM_DBG(dm, DBG_ANT_DIV,
						  "[ AntDiv : ON ]   BFmee ==0; cap=((%d))\n",
						  beamform_cap);
					PHYDM_DBG(dm, DBG_ANT_DIV,
						  "[ AntDiv BF]   is_no_csi_feedback= ((%d))\n",
						  *fat_tab->is_no_csi_feedback);
					if (dm->ant_div_type != S0S1_SW_ANTDIV)
						odm_ant_div_on_off(dm, ANTDIV_ON
								   , ANT_PATH_A)
								   ;
					fat_tab->fix_ant_bfee = 0;
				}
			}
		} else {
			if (fat_tab->div_path_type == ANT_PATH_A)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
			else if (fat_tab->div_path_type == ANT_PATH_B)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_B);
			else if (fat_tab->div_path_type == ANT_PATH_AB)
				odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_AB);
		}
#endif
	}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	/* @----------just for fool proof */

	if (dm->antdiv_rssi)
		dm->debug_components |= DBG_ANT_DIV;
	else
		dm->debug_components &= ~DBG_ANT_DIV;

	if (fat_tab->ant_div_2g_5g == ODM_ANTDIV_2G) {
		if (!(dm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	} else if (fat_tab->ant_div_2g_5g == ODM_ANTDIV_5G) {
		if (!(dm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC))
			return;
	}
#endif

	/* @---------- */

	if (dm->antdiv_select == 1)
		dm->ant_type = ODM_FIX_MAIN_ANT;
	else if (dm->antdiv_select == 2)
		dm->ant_type = ODM_FIX_AUX_ANT;
	else { /* @if (dm->antdiv_select==0) */
		dm->ant_type = ODM_AUTO_ANT;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		/*Stop Antenna diversity for CMW500 testing case*/
		if (dm->consecutive_idlel_time >= 10) {
			dm->ant_type = ODM_FIX_MAIN_ANT;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "[AntDiv: OFF] No TP case, consecutive_idlel_time=((%d))\n",
				  dm->consecutive_idlel_time);
		}
#endif
	}

	/*PHYDM_DBG(dm, DBG_ANT_DIV,"ant_type= (%d), pre_ant_type= (%d)\n",*/
	/*dm->ant_type,dm->pre_ant_type); */

	if (dm->ant_type != ODM_AUTO_ANT) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "Fix Antenna at (( %s ))\n",
			  (dm->ant_type == ODM_FIX_MAIN_ANT) ? "MAIN" : "AUX");

		if (dm->ant_type != dm->pre_ant_type) {
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_A);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);

			if (dm->ant_type == ODM_FIX_MAIN_ANT)
				odm_update_rx_idle_ant(dm, MAIN_ANT);
			else if (dm->ant_type == ODM_FIX_AUX_ANT)
				odm_update_rx_idle_ant(dm, AUX_ANT);
		}
		dm->pre_ant_type = dm->ant_type;
		return;
	} else {
		if (dm->ant_type != dm->pre_ant_type) {
			odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
		}
		dm->pre_ant_type = dm->ant_type;
	}
#if (defined(CONFIG_2T4R_ANTENNA))
	if (dm->ant_type2 != ODM_AUTO_ANT) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "PathB Fix Ant at (( %s ))\n",
			  (dm->ant_type2 == ODM_FIX_MAIN_ANT) ? "MAIN" : "AUX");

		if (dm->ant_type2 != dm->pre_ant_type2) {
			odm_ant_div_on_off(dm, ANTDIV_OFF, ANT_PATH_B);
			odm_tx_by_tx_desc_or_reg(dm, TX_BY_REG);

			if (dm->ant_type2 == ODM_FIX_MAIN_ANT)
				phydm_update_rx_idle_ant_pathb(dm, MAIN_ANT);
			else if (dm->ant_type2 == ODM_FIX_AUX_ANT)
				phydm_update_rx_idle_ant_pathb(dm, AUX_ANT);
		}
		dm->pre_ant_type2 = dm->ant_type2;
		return;
	}
	if (dm->ant_type2 != dm->pre_ant_type2) {
		odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_B);
		odm_tx_by_tx_desc_or_reg(dm, TX_BY_DESC);
	}
	dm->pre_ant_type2 = dm->ant_type2;

#endif

/*@ ----------------------------------------------- */
/*@ [--8188E--] */
	if (dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV ||
		    dm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_hw_ant_div(dm);

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) ||\
	(defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (dm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_fast_ant_training(dm);
#endif

#endif
	}
/*@ [--8192E--] */
#if (RTL8192E_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8192E) {
		if (dm->ant_div_type == CGCS_RX_HW_ANTDIV ||
		    dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_hw_ant_div(dm);

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) ||\
	(defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		else if (dm->ant_div_type == CG_TRX_SMART_ANTDIV)
			odm_fast_ant_training(dm);
#endif
	}
#endif
/*@ [--8197F--] */
#if (RTL8197F_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8197F) {
		if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_hw_ant_div(dm);
	}
#endif

/*@ [--8197G--] */
#if (RTL8197G_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8197G) {
		if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
			odm_hw_ant_div(dm);
	}
#endif

#if (RTL8723B_SUPPORT == 1)
/*@ [--8723B---] */
	else if (dm->support_ic_type == ODM_RTL8723B) {
		if (phydm_is_bt_enable_8723b(dm)) {
			PHYDM_DBG(dm, DBG_ANT_DIV, "[BT is enable!!!]\n");
			if (fat_tab->is_become_linked == true) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Set REG 948[9:6]=0x0\n");
				if (dm->support_ic_type == ODM_RTL8723B)
					odm_set_bb_reg(dm, R_0x948, 0x3c0, 0x0)
						       ;

				fat_tab->is_become_linked = false;
			}
		} else {
			if (dm->ant_div_type == S0S1_SW_ANTDIV) {
				#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
				odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
				#endif
			} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
				odm_hw_ant_div(dm);
		}
	}
#endif
/*@ [--8723D--]*/
#if (RTL8723D_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8723D) {
		if (dm->ant_div_type == S0S1_SW_ANTDIV) {
			#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
			if (dm->antdiv_counter == CONFIG_ANTDIV_PERIOD) {
				odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
				dm->antdiv_counter--;
			} else {
				dm->antdiv_counter--;
			}
			if (dm->antdiv_counter == 0)
				dm->antdiv_counter = CONFIG_ANTDIV_PERIOD;
			#endif
		} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_hw_ant_div(dm);
		}
	}
#endif
#if (RTL8721D_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8721D) {
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_hw_ant_div(dm);
		}
	}
#endif
#if (RTL8710C_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8710C) {
		if (dm->ant_div_type == S0S1_SW_ANTDIV) {
			#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
			if (dm->antdiv_counter == CONFIG_ANTDIV_PERIOD) {
				odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
				dm->antdiv_counter--;
			} else {
				dm->antdiv_counter--;
			}
			if (dm->antdiv_counter == 0)
				dm->antdiv_counter = CONFIG_ANTDIV_PERIOD;
			#endif
		} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_hw_ant_div(dm);
		}
	}
#endif
/*@ [--8821A--] */
#if (RTL8821A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8821) {
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {
			if (sat_tab->fix_beam_pattern_en != 0) {
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  " [ SmartAnt ] Fix SmartAnt Pattern = 0x%x\n",
					  sat_tab->fix_beam_pattern_codeword);
				/*return;*/
			} else {
				odm_fast_ant_training_hl_smart_antenna_type1(dm);
			}

		} else
		#endif
		{
		#ifdef ODM_CONFIG_BT_COEXIST
			if (!dm->bt_info_table.is_bt_enabled) { /*@BT disabled*/
				if (dm->ant_div_type == S0S1_SW_ANTDIV) {
					dm->ant_div_type = CG_TRX_HW_ANTDIV;
					PHYDM_DBG(dm, DBG_ANT_DIV,
						  " [S0S1_SW_ANTDIV]  ->  [CG_TRX_HW_ANTDIV]\n");
					/*odm_set_bb_reg(dm, 0x8d4, BIT24, 1);*/
					if (fat_tab->is_become_linked == true)
						odm_ant_div_on_off(dm,
								   ANTDIV_ON,
								   ANT_PATH_A);
				}

			} else { /*@BT enabled*/

				if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
					dm->ant_div_type = S0S1_SW_ANTDIV;
					PHYDM_DBG(dm, DBG_ANT_DIV,
						  " [CG_TRX_HW_ANTDIV]  ->  [S0S1_SW_ANTDIV]\n");
					/*odm_set_bb_reg(dm, 0x8d4, BIT24, 0);*/
					odm_ant_div_on_off(dm, ANTDIV_OFF,
							   ANT_PATH_A);
				}
			}
		#endif

			if (dm->ant_div_type == S0S1_SW_ANTDIV) {
				#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
				odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
				#endif
			} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
				odm_hw_ant_div(dm);
			}
		}
	}
#endif

/*@ [--8821C--] */
#if (RTL8821C_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8821C) {
		if (!dm->is_bt_continuous_turn) {
			dm->ant_div_type = S0S1_SW_ANTDIV;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "is_bt_continuous_turn = ((%d))   ==> SW AntDiv\n",
				  dm->is_bt_continuous_turn);

		} else {
			dm->ant_div_type = CG_TRX_HW_ANTDIV;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "is_bt_continuous_turn = ((%d))   ==> HW AntDiv\n",
				  dm->is_bt_continuous_turn);
			odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
		}

		if (fat_tab->force_antdiv_type)
			dm->ant_div_type = fat_tab->antdiv_type_dbg;

		if (dm->ant_div_type == S0S1_SW_ANTDIV) {
			#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
			odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
			#endif
		} else if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_ant_div_on_off(dm, ANTDIV_ON, ANT_PATH_A);
			odm_hw_ant_div(dm);
		}
	}
#endif

/* @ [--8195B--] */
#if (RTL8195B_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8195B) {
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV) {
			odm_hw_ant_div(dm);
		}
	}
#endif

/* @ [--8881A--] */
#if (RTL8881A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8881A)
		odm_hw_ant_div(dm);
#endif

/*@ [--8812A--] */
#if (RTL8812A_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8812)
		odm_hw_ant_div(dm);
#endif

#if (RTL8188F_SUPPORT == 1)
/*@ [--8188F--]*/
	else if (dm->support_ic_type == ODM_RTL8188F) {
		#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_s0s1_sw_ant_div(dm, SWAW_STEP_PEEK);
		#endif
	}
#endif

/*@ [--8822B--]*/
#if (RTL8822B_SUPPORT == 1)
	else if (dm->support_ic_type == ODM_RTL8822B) {
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV)
			odm_hw_ant_div(dm);
		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE2) {
			if (sat_tab->fix_beam_pattern_en != 0)
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  " [ SmartAnt ] Fix SmartAnt Pattern = 0x%x\n",
					  sat_tab->fix_beam_pattern_codeword);
			else
				phydm_fast_ant_training_hl_smart_antenna_type2(dm);
		}
		#endif
	}
#endif
}

void odm_antsel_statistics(void *dm_void, void *phy_info_void,
			   u8 antsel_tr_mux, u32 mac_id, u32 utility, u8 method,
			   u8 is_cck_rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	struct phydm_phyinfo_struct *phy_info = NULL;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;

	if (method == RSSI_METHOD) {
		if (is_cck_rate) {
			if (antsel_tr_mux == fat_tab->ant_idx_vec[0]-1) {
	/*to prevent u16 overflow, max(RSSI)=100, 65435+100 = 65535 (u16)*/
				if (fat_tab->main_sum_cck[mac_id] > 65435)
					return;

				fat_tab->main_sum_cck[mac_id] += (u16)utility;
				fat_tab->main_cnt_cck[mac_id]++;
			} else {
				if (fat_tab->aux_sum_cck[mac_id] > 65435)
					return;

				fat_tab->aux_sum_cck[mac_id] += (u16)utility;
				fat_tab->aux_cnt_cck[mac_id]++;
			}

		} else { /*ofdm rate*/

			if (antsel_tr_mux == fat_tab->ant_idx_vec[0]-1) {
				if (fat_tab->main_sum[mac_id] > 65435)
					return;

				fat_tab->main_sum[mac_id] += (u16)utility;
				fat_tab->main_cnt[mac_id]++;
			} else {
				if (fat_tab->aux_sum[mac_id] > 65435)
					return;

				fat_tab->aux_sum[mac_id] += (u16)utility;
				fat_tab->aux_cnt[mac_id]++;
			}
		}
	}
#ifdef ODM_EVM_ENHANCE_ANTDIV
	else if (method == EVM_METHOD) {
		if (!fat_tab->get_stats)
			return;

		if (dm->rate_ss == 1) {
			phydm_statistics_evm_1ss(dm, phy_info, antsel_tr_mux,
						 mac_id, utility);
		} else { /*@>= 2SS*/
			phydm_statistics_evm_2ss(dm, phy_info, antsel_tr_mux,
						 mac_id, utility);
		}

	} else if (method == CRC32_METHOD) {
		if (antsel_tr_mux == ANT1_2G) {
			fat_tab->main_crc32_ok_cnt += utility;
			fat_tab->main_crc32_fail_cnt++;
		} else {
			fat_tab->aux_crc32_ok_cnt += utility;
			fat_tab->aux_crc32_fail_cnt++;
		}

	} else if (method == TP_METHOD) {
		if (!fat_tab->get_stats)
			return;
		if (utility <= ODM_RATEMCS15 && utility >= ODM_RATEMCS0) {
			if (antsel_tr_mux == ANT1_2G) {
				fat_tab->main_tp += (phy_rate_table[utility])
						    << 5;
				fat_tab->main_tp_cnt++;
			} else {
				fat_tab->aux_tp += (phy_rate_table[utility])
						   << 5;
				fat_tab->aux_tp_cnt++;
			}
		}
	}
#endif
}

void odm_process_rssi_smart(void *dm_void, void *phy_info_void,
			    void *pkt_info_void, u8 rx_power_ant0)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;

	if ((dm->support_ic_type & ODM_SMART_ANT_SUPPORT) &&
	    pktinfo->is_packet_to_self &&
	    fat_tab->fat_state == FAT_TRAINING_STATE) {
	/* @(pktinfo->is_packet_match_bssid && (!pktinfo->is_packet_beacon)) */
		u8 antsel_tr_mux;

		antsel_tr_mux = (fat_tab->antsel_rx_keep_2 << 2) |
				(fat_tab->antsel_rx_keep_1 << 1) |
				fat_tab->antsel_rx_keep_0;
		fat_tab->ant_sum_rssi[antsel_tr_mux] += rx_power_ant0;
		fat_tab->ant_rssi_cnt[antsel_tr_mux]++;
	}
}

void odm_process_rssi_normal(void *dm_void, void *phy_info_void,
			     void *pkt_info_void, u8 rx_pwr0)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	u8 rx_evm0, rx_evm1;
	boolean b_main;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	rx_evm0 = phy_info->rx_mimo_signal_quality[0];
	rx_evm1 = phy_info->rx_mimo_signal_quality[1];

	if (!(pktinfo->is_packet_to_self || fat_tab->use_ctrl_frame_antdiv))
		return;

	if (dm->ant_div_type == S0S1_SW_ANTDIV) {
		if (pktinfo->is_cck_rate ||
		    dm->support_ic_type == ODM_RTL8188F || dm->support_ic_type == ODM_RTL8710C) {

			b_main = (fat_tab->rx_idle_ant == MAIN_ANT);
			fat_tab->antsel_rx_keep_0 = b_main ? ANT1_2G : ANT2_2G;
		}

		odm_antsel_statistics(dm, phy_info, fat_tab->antsel_rx_keep_0,
				      pktinfo->station_id, rx_pwr0, RSSI_METHOD,
				      pktinfo->is_cck_rate);
	} else {
		odm_antsel_statistics(dm, phy_info, fat_tab->antsel_rx_keep_0,
				      pktinfo->station_id, rx_pwr0, RSSI_METHOD,
				      pktinfo->is_cck_rate);

		#ifdef ODM_EVM_ENHANCE_ANTDIV
		if (!(dm->support_ic_type & ODM_EVM_ANTDIV_IC))
			return;
		if (pktinfo->is_cck_rate)
			return;

		odm_antsel_statistics(dm, phy_info, fat_tab->antsel_rx_keep_0,
				      pktinfo->station_id, rx_evm0, EVM_METHOD,
				      pktinfo->is_cck_rate);
		odm_antsel_statistics(dm, phy_info, fat_tab->antsel_rx_keep_0,
				      pktinfo->station_id, pktinfo->data_rate,
				      TP_METHOD, pktinfo->is_cck_rate);
		#endif
	}
}

void odm_process_rssi_for_ant_div(void *dm_void, void *phy_info_void,
				  void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
#if (defined(CONFIG_HL_SMART_ANTENNA))
	struct smt_ant_honbo *sat_tab = &dm->dm_sat_table;
	u32 beam_tmp;
	u8 next_ant;
	u8 train_pkt_number;
#endif
	boolean b_main;
	u8 rx_power_ant0, rx_power_ant1;
	u8 rx_evm_ant0, rx_evm_ant1;
	u8 rssi_avg;
	u64 rssi_linear = 0;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;
	rx_power_ant0 = phy_info->rx_mimo_signal_strength[0];
	rx_power_ant1 = phy_info->rx_mimo_signal_strength[1];
	rx_evm_ant0 = phy_info->rx_mimo_signal_quality[0];
	rx_evm_ant1 = phy_info->rx_mimo_signal_quality[1];

	if ((dm->support_ic_type & ODM_IC_2SS) && !pktinfo->is_cck_rate) {
		if (rx_power_ant1 < 100) {
			rssi_linear = phydm_db_2_linear(rx_power_ant0) +
				      phydm_db_2_linear(rx_power_ant1);
			/* @Rounding and removing fractional bits */
			rssi_linear = (rssi_linear +
				       (1 << (FRAC_BITS - 1))) >> FRAC_BITS;
			/* @Calculate average RSSI */
			rssi_linear = DIVIDED_2(rssi_linear);
			/* @averaged PWDB */
			rssi_avg = (u8)odm_convert_to_db(rssi_linear);
		}

	} else {
		rx_power_ant0 = (u8)phy_info->rx_pwdb_all;
		rssi_avg = rx_power_ant0;
	}

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
	if ((dm->ant_div_type == HL_SW_SMART_ANT_TYPE2) && (fat_tab->fat_state == FAT_TRAINING_STATE))
		phydm_process_rssi_for_hb_smtant_type2(dm, phy_info, pktinfo, rssi_avg); /*@for 8822B*/
	else
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
#ifdef CONFIG_FAT_PATCH
		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE1 && fat_tab->fat_state == FAT_TRAINING_STATE) {
		/*@[Beacon]*/
		if (pktinfo->is_packet_beacon) {
			sat_tab->beacon_counter++;
			PHYDM_DBG(dm, DBG_ANT_DIV,
				  "MatchBSSID_beacon_counter = ((%d))\n",
				  sat_tab->beacon_counter);

			if (sat_tab->beacon_counter >= sat_tab->pre_beacon_counter + 2) {
				if (sat_tab->ant_num > 1) {
					next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
					odm_update_rx_idle_ant(dm, next_ant);
				}

				sat_tab->update_beam_idx++;

				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "pre_beacon_counter = ((%d)), pkt_counter = ((%d)), update_beam_idx = ((%d))\n",
					  sat_tab->pre_beacon_counter,
					  sat_tab->pkt_counter,
					  sat_tab->update_beam_idx);

				sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
				sat_tab->pkt_counter = 0;
			}
		}
		/*@[data]*/
		else if (pktinfo->is_packet_to_self) {
			if (sat_tab->pkt_skip_statistic_en == 0) {
				/*@
				PHYDM_DBG(dm, DBG_ANT_DIV, "StaID[%d]:  antsel_pathA = ((%d)), hw_antsw_occur = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					pktinfo->station_id, fat_tab->antsel_rx_keep_0, fat_tab->hw_antsw_occur, sat_tab->fast_training_beam_num, rx_power_ant0);
				*/
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "ID[%d][pkt_cnt = %d]: {ANT, Beam} = {%d, %d}, RSSI = ((%d))\n",
					  pktinfo->station_id,
					  sat_tab->pkt_counter,
					  fat_tab->antsel_rx_keep_0,
					  sat_tab->fast_training_beam_num,
					  rx_power_ant0);

				sat_tab->pkt_rssi_sum[fat_tab->antsel_rx_keep_0][sat_tab->fast_training_beam_num] += rx_power_ant0;
				sat_tab->pkt_rssi_cnt[fat_tab->antsel_rx_keep_0][sat_tab->fast_training_beam_num]++;
				sat_tab->pkt_counter++;

#if 1
				train_pkt_number = sat_tab->beam_train_cnt[fat_tab->rx_idle_ant - 1][sat_tab->fast_training_beam_num];
#else
				train_pkt_number = sat_tab->per_beam_training_pkt_num;
#endif

				/*Swich Antenna erery N pkts*/
				if (sat_tab->pkt_counter == train_pkt_number) {
					if (sat_tab->ant_num > 1) {
						PHYDM_DBG(dm, DBG_ANT_DIV, "packet enugh ((%d ))pkts ---> Switch antenna\n", train_pkt_number);
						next_ant = (fat_tab->rx_idle_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;
						odm_update_rx_idle_ant(dm, next_ant);
					}

					sat_tab->update_beam_idx++;
					PHYDM_DBG(dm, DBG_ANT_DIV, "pre_beacon_counter = ((%d)), update_beam_idx_counter = ((%d))\n",
						  sat_tab->pre_beacon_counter, sat_tab->update_beam_idx);

					sat_tab->pre_beacon_counter = sat_tab->beacon_counter;
					sat_tab->pkt_counter = 0;
				}
			}
		}

		/*Swich Beam after switch "sat_tab->ant_num" antennas*/
		if (sat_tab->update_beam_idx == sat_tab->ant_num) {
			sat_tab->update_beam_idx = 0;
			sat_tab->pkt_counter = 0;
			beam_tmp = sat_tab->fast_training_beam_num;

			if (sat_tab->fast_training_beam_num >= (sat_tab->beam_patten_num_each_ant - 1)) {
				fat_tab->fat_state = FAT_DECISION_STATE;

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
				if (dm->support_interface == ODM_ITRF_PCIE)
					odm_fast_ant_training_hl_smart_antenna_type1(dm);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
				if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
					odm_schedule_work_item(&sat_tab->hl_smart_antenna_decision_workitem);
#endif

			} else {
				sat_tab->fast_training_beam_num++;
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "Update Beam_num (( %d )) -> (( %d ))\n",
					  beam_tmp,
					  sat_tab->fast_training_beam_num);
				phydm_set_all_ant_same_beam_num(dm);

				fat_tab->fat_state = FAT_TRAINING_STATE;
			}
		}
	}
#else

		if (dm->ant_div_type == HL_SW_SMART_ANT_TYPE1) {
		if ((dm->support_ic_type & ODM_HL_SMART_ANT_TYPE1_SUPPORT) &&
		    pktinfo->is_packet_to_self &&
		    fat_tab->fat_state == FAT_TRAINING_STATE) {
			if (sat_tab->pkt_skip_statistic_en == 0) {
				/*@
				PHYDM_DBG(dm, DBG_ANT_DIV, "StaID[%d]:  antsel_pathA = ((%d)), hw_antsw_occur = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					pktinfo->station_id, fat_tab->antsel_rx_keep_0, fat_tab->hw_antsw_occur, sat_tab->fast_training_beam_num, rx_power_ant0);
				*/
				PHYDM_DBG(dm, DBG_ANT_DIV,
					  "StaID[%d]:  antsel_pathA = ((%d)), is_packet_to_self = ((%d)), Beam_num = ((%d)), RSSI = ((%d))\n",
					  pktinfo->station_id,
					  fat_tab->antsel_rx_keep_0,
					  pktinfo->is_packet_to_self,
					  sat_tab->fast_training_beam_num,
					  rx_power_ant0);

				sat_tab->pkt_rssi_sum[fat_tab->antsel_rx_keep_0][sat_tab->fast_training_beam_num] += rx_power_ant0;
				sat_tab->pkt_rssi_cnt[fat_tab->antsel_rx_keep_0][sat_tab->fast_training_beam_num]++;
				sat_tab->pkt_counter++;

				/*swich beam every N pkt*/
				if (sat_tab->pkt_counter >= sat_tab->per_beam_training_pkt_num) {
					sat_tab->pkt_counter = 0;
					beam_tmp = sat_tab->fast_training_beam_num;

					if (sat_tab->fast_training_beam_num >= (sat_tab->beam_patten_num_each_ant - 1)) {
						fat_tab->fat_state = FAT_DECISION_STATE;

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
						if (dm->support_interface == ODM_ITRF_PCIE)
							odm_fast_ant_training_hl_smart_antenna_type1(dm);
#endif
#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
						if (dm->support_interface == ODM_ITRF_USB || dm->support_interface == ODM_ITRF_SDIO)
							odm_schedule_work_item(&sat_tab->hl_smart_antenna_decision_workitem);
#endif

					} else {
						sat_tab->fast_training_beam_num++;
						phydm_set_all_ant_same_beam_num(dm);

						fat_tab->fat_state = FAT_TRAINING_STATE;
						PHYDM_DBG(dm, DBG_ANT_DIV, "Update  Beam_num (( %d )) -> (( %d ))\n", beam_tmp, sat_tab->fast_training_beam_num);
					}
				}
			}
		}
	}
#endif
	else
#endif
		if (dm->ant_div_type == CG_TRX_SMART_ANTDIV) {
			odm_process_rssi_smart(dm, phy_info, pktinfo,
					       rx_power_ant0);
		} else { /* @ant_div_type != CG_TRX_SMART_ANTDIV */
			odm_process_rssi_normal(dm, phy_info, pktinfo,
						rx_power_ant0);
		}
#if 0
/* PHYDM_DBG(dm,DBG_ANT_DIV,"is_cck_rate=%d, pwdb_all=%d\n",
 *	     pktinfo->is_cck_rate, phy_info->rx_pwdb_all);
 * PHYDM_DBG(dm,DBG_ANT_DIV,"antsel_tr_mux=3'b%d%d%d\n",
 *	     fat_tab->antsel_rx_keep_2, fat_tab->antsel_rx_keep_1,
 *	     fat_tab->antsel_rx_keep_0);
 */
#endif
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE | ODM_IOT))
void odm_set_tx_ant_by_tx_info(void *dm_void, u8 *desc, u8 mac_id)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;

	if (dm->support_ic_type == (ODM_RTL8723B | ODM_RTL8721D)) {
#if (RTL8723B_SUPPORT == 1 || RTL8721D_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8723B(desc, fat_tab->antsel_a[mac_id]);
/*PHYDM_DBG(dm,DBG_ANT_DIV,
 *	   "[8723B] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
 *	    mac_id, fat_tab->antsel_c[mac_id], fat_tab->antsel_b[mac_id],
 *	    fat_tab->antsel_a[mac_id]);
 */
#endif
	} else if (dm->support_ic_type == ODM_RTL8821) {
#if (RTL8821A_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8812(desc, fat_tab->antsel_a[mac_id]);
/*PHYDM_DBG(dm,DBG_ANT_DIV,
 *	   "[8821A] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
 *	    mac_id, fat_tab->antsel_c[mac_id], fat_tab->antsel_b[mac_id],
 *	    fat_tab->antsel_a[mac_id]);
 */
#endif
	} else if (dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_88E(desc, fat_tab->antsel_a[mac_id]);
		SET_TX_DESC_ANTSEL_B_88E(desc, fat_tab->antsel_b[mac_id]);
		SET_TX_DESC_ANTSEL_C_88E(desc, fat_tab->antsel_c[mac_id]);
/*PHYDM_DBG(dm,DBG_ANT_DIV,
 *	   "[8188E] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
 *	    mac_id, fat_tab->antsel_c[mac_id], fat_tab->antsel_b[mac_id],
 *	    fat_tab->antsel_a[mac_id]);
 */
#endif
	} else if (dm->support_ic_type == ODM_RTL8821C) {
#if (RTL8821C_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8821C(desc, fat_tab->antsel_a[mac_id]);
/*PHYDM_DBG(dm,DBG_ANT_DIV,
 *	   "[8821C] SetTxAntByTxInfo_WIN: mac_id=%d, antsel_tr_mux=3'b%d%d%d\n",
 *	    mac_id, fat_tab->antsel_c[mac_id], fat_tab->antsel_b[mac_id],
 *	    fat_tab->antsel_a[mac_id]);
 */
#endif
	} else if (dm->support_ic_type == ODM_RTL8195B) {
#if (RTL8195B_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8195B(desc, fat_tab->antsel_a[mac_id]);
#endif
	} else if (dm->support_ic_type == ODM_RTL8822B) {
#if (RTL8822B_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8822B(desc, fat_tab->antsel_a[mac_id]);
#endif

	}
}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

void odm_set_tx_ant_by_tx_info(
	struct rtl8192cd_priv *priv,
	struct tx_desc *pdesc,
	unsigned short aid)
{
	struct dm_struct *dm = GET_PDM_ODM(priv); /*@&(priv->pshare->_dmODM);*/
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;

	if (dm->support_ic_type == ODM_RTL8881A) {
#if 0
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******\n",__FUNCTION__,__LINE__);	*/
#endif
		pdesc->Dword6 &= set_desc(~(BIT(18) | BIT(17) | BIT(16)));
		pdesc->Dword6 |= set_desc(fat_tab->antsel_a[aid] << 16);
	} else if (dm->support_ic_type == ODM_RTL8192E) {
#if 0
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8192E******\n",__FUNCTION__,__LINE__);	*/
#endif
		pdesc->Dword6 &= set_desc(~(BIT(18) | BIT(17) | BIT(16)));
		pdesc->Dword6 |= set_desc(fat_tab->antsel_a[aid] << 16);
	} else if (dm->support_ic_type == ODM_RTL8197F) {
#if 0
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8192E******\n",__FUNCTION__,__LINE__);	*/
#endif
		pdesc->Dword6 &= set_desc(~(BIT(17) | BIT(16)));
		pdesc->Dword6 |= set_desc(fat_tab->antsel_a[aid] << 16);
	} else if (dm->support_ic_type == ODM_RTL8822B) {
		pdesc->Dword6 &= set_desc(~(BIT(17) | BIT(16)));
		pdesc->Dword6 |= set_desc(fat_tab->antsel_a[aid] << 16);
	} else if (dm->support_ic_type == ODM_RTL8188E) {
#if 0
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8188E******\n",__FUNCTION__,__LINE__);*/
#endif
		pdesc->Dword2 &= set_desc(~BIT(24));
		pdesc->Dword2 &= set_desc(~BIT(25));
		pdesc->Dword7 &= set_desc(~BIT(29));

		pdesc->Dword2 |= set_desc(fat_tab->antsel_a[aid] << 24);
		pdesc->Dword2 |= set_desc(fat_tab->antsel_b[aid] << 25);
		pdesc->Dword7 |= set_desc(fat_tab->antsel_c[aid] << 29);

	} else if (dm->support_ic_type == ODM_RTL8812) {
		/*@[path-A]*/
#if 0
		/*panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******\n",__FUNCTION__,__LINE__);*/
#endif

		pdesc->Dword6 &= set_desc(~BIT(16));
		pdesc->Dword6 &= set_desc(~BIT(17));
		pdesc->Dword6 &= set_desc(~BIT(18));

		pdesc->Dword6 |= set_desc(fat_tab->antsel_a[aid] << 16);
		pdesc->Dword6 |= set_desc(fat_tab->antsel_b[aid] << 17);
		pdesc->Dword6 |= set_desc(fat_tab->antsel_c[aid] << 18);
	}
}

#if 1 /*@def CONFIG_WLAN_HAL*/
void odm_set_tx_ant_by_tx_info_hal(
	struct rtl8192cd_priv *priv,
	void *pdesc_data,
	u16 aid)
{
	struct dm_struct *dm = GET_PDM_ODM(priv); /*@&(priv->pshare->_dmODM);*/
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	PTX_DESC_DATA_88XX pdescdata = (PTX_DESC_DATA_88XX)pdesc_data;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	if (dm->ant_div_type == CGCS_RX_HW_ANTDIV)
		return;

	if (dm->support_ic_type & (ODM_RTL8881A | ODM_RTL8192E | ODM_RTL8814A |
	    ODM_RTL8197F | ODM_RTL8822B)) {
#if 0
		/*panic_printk("[%s] [%d] **odm_set_tx_ant_by_tx_info_hal**\n",
		 *	       __FUNCTION__,__LINE__);
		 */
#endif
		pdescdata->ant_sel = 1;
		pdescdata->ant_sel_a = fat_tab->antsel_a[aid];
	}
}
#endif /*@#ifdef CONFIG_WLAN_HAL*/

#endif

void odm_ant_div_config(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	PHYDM_DBG(dm, DBG_ANT_DIV, "WIN Config Antenna Diversity\n");
	/*@
	if(dm->support_ic_type==ODM_RTL8723B)
	{
		if((!dm->swat_tab.ANTA_ON || !dm->swat_tab.ANTB_ON))
			dm->support_ability &= ~(ODM_BB_ANT_DIV);
	}
	*/
	#if (defined(CONFIG_2T3R_ANTENNA))
	#if (RTL8822B_SUPPORT == 1)
		dm->rfe_type = ANT_2T3R_RFE_TYPE;
	#endif
	#endif

	#if (defined(CONFIG_2T4R_ANTENNA))
	#if (RTL8822B_SUPPORT == 1)
		dm->rfe_type = ANT_2T4R_RFE_TYPE;
	#endif
	#endif

	if (dm->support_ic_type == ODM_RTL8723D)
		dm->ant_div_type = S0S1_SW_ANTDIV;
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))

	PHYDM_DBG(dm, DBG_ANT_DIV, "CE Config Antenna Diversity\n");

	if (dm->support_ic_type == ODM_RTL8723B)
		dm->ant_div_type = S0S1_SW_ANTDIV;

	if (dm->support_ic_type == ODM_RTL8723D)
		dm->ant_div_type = S0S1_SW_ANTDIV;
#elif (DM_ODM_SUPPORT_TYPE & (ODM_IOT))

	PHYDM_DBG(dm, DBG_ANT_DIV, "IOT Config Antenna Diversity\n");

	if (dm->support_ic_type == ODM_RTL8721D)
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
	if (dm->support_ic_type == ODM_RTL8710C){
		if(dm->cut_version >  ODM_CUT_C)
			dm->ant_div_type = CG_TRX_HW_ANTDIV;
		else
			dm->ant_div_type = S0S1_SW_ANTDIV;
	}

#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))

	PHYDM_DBG(dm, DBG_ANT_DIV, "AP Config Antenna Diversity\n");

	/* @2 [ NOT_SUPPORT_ANTDIV ] */
#if (defined(CONFIG_NOT_SUPPORT_ANTDIV))
	dm->support_ability &= ~(ODM_BB_ANT_DIV);
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Disable AntDiv function] : Not Support 2.4G & 5G Antenna Diversity\n");

	/* @2 [ 2G&5G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_2G5G_SUPPORT_ANTDIV))
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Enable AntDiv function] : 2.4G & 5G Support Antenna Diversity Simultaneously\n");
	fat_tab->ant_div_2g_5g = (ODM_ANTDIV_2G | ODM_ANTDIV_5G);

	if (dm->support_ic_type & ODM_ANTDIV_SUPPORT)
		dm->support_ability |= ODM_BB_ANT_DIV;
	if (*dm->band_type == ODM_BAND_5G) {
	#if (defined(CONFIG_5G_CGCS_RX_DIVERSITY))
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
		panic_printk("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
	#elif (defined(CONFIG_5G_CG_TRX_DIVERSITY) ||\
		defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A))
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
		panic_printk("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
	#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		dm->ant_div_type = CG_TRX_SMART_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CG_SMART_ANTDIV\n");
	#elif (defined(CONFIG_5G_S0S1_SW_ANT_DIVERSITY))
		dm->ant_div_type = S0S1_SW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = S0S1_SW_ANTDIV\n");
	#endif
	} else if (*dm->band_type == ODM_BAND_2_4G) {
	#if (defined(CONFIG_2G_CGCS_RX_DIVERSITY))
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
	#elif (defined(CONFIG_2G_CG_TRX_DIVERSITY) ||\
		defined(CONFIG_2G5G_CG_TRX_DIVERSITY_8881A))
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
	#elif (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		dm->ant_div_type = CG_TRX_SMART_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CG_SMART_ANTDIV\n");
	#elif (defined(CONFIG_2G_S0S1_SW_ANT_DIVERSITY))
		dm->ant_div_type = S0S1_SW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = S0S1_SW_ANTDIV\n");
	#endif
	}

	/* @2 [ 5G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_5G_SUPPORT_ANTDIV))
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Enable AntDiv function] : Only 5G Support Antenna Diversity\n");
	panic_printk("[ Enable AntDiv function] : Only 5G Support Antenna Diversity\n");
	fat_tab->ant_div_2g_5g = (ODM_ANTDIV_5G);
	if (*dm->band_type == ODM_BAND_5G) {
		if (dm->support_ic_type & ODM_ANTDIV_5G_SUPPORT_IC)
			dm->support_ability |= ODM_BB_ANT_DIV;
	#if (defined(CONFIG_5G_CGCS_RX_DIVERSITY))
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
		panic_printk("[ 5G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
	#elif (defined(CONFIG_5G_CG_TRX_DIVERSITY))
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
		panic_printk("[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
	#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		dm->ant_div_type = CG_TRX_SMART_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = CG_SMART_ANTDIV\n");
	#elif (defined(CONFIG_5G_S0S1_SW_ANT_DIVERSITY))
		dm->ant_div_type = S0S1_SW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 5G] : AntDiv type = S0S1_SW_ANTDIV\n");
	#endif
	} else if (*dm->band_type == ODM_BAND_2_4G) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "Not Support 2G ant_div_type\n");
		dm->support_ability &= ~(ODM_BB_ANT_DIV);
	}

	/* @2 [ 2G_SUPPORT_ANTDIV ] */
#elif (defined(CONFIG_2G_SUPPORT_ANTDIV))
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[ Enable AntDiv function] : Only 2.4G Support Antenna Diversity\n");
	fat_tab->ant_div_2g_5g = (ODM_ANTDIV_2G);
	if (*dm->band_type == ODM_BAND_2_4G) {
		if (dm->support_ic_type & ODM_ANTDIV_2G_SUPPORT_IC)
			dm->support_ability |= ODM_BB_ANT_DIV;
	#if (defined(CONFIG_2G_CGCS_RX_DIVERSITY))
		dm->ant_div_type = CGCS_RX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CGCS_RX_HW_ANTDIV\n");
	#elif (defined(CONFIG_2G_CG_TRX_DIVERSITY))
		dm->ant_div_type = CG_TRX_HW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CG_TRX_HW_ANTDIV\n");
	#elif (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		dm->ant_div_type = CG_TRX_SMART_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = CG_SMART_ANTDIV\n");
	#elif (defined(CONFIG_2G_S0S1_SW_ANT_DIVERSITY))
		dm->ant_div_type = S0S1_SW_ANTDIV;
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[ 2.4G] : AntDiv type = S0S1_SW_ANTDIV\n");
	#endif
	} else if (*dm->band_type == ODM_BAND_5G) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "Not Support 5G ant_div_type\n");
		dm->support_ability &= ~(ODM_BB_ANT_DIV);
	}
#endif

	if (!(dm->support_ic_type & ODM_ANTDIV_SUPPORT_IC)) {
		fat_tab->ant_div_2g_5g = 0;
		dm->support_ability &= ~(ODM_BB_ANT_DIV);
	}
#endif

	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[AntDiv Config Info] AntDiv_SupportAbility = (( %x ))\n",
		  ((dm->support_ability & ODM_BB_ANT_DIV) ? 1 : 0));
	PHYDM_DBG(dm, DBG_ANT_DIV,
		  "[AntDiv Config Info] be_fix_tx_ant = ((%d))\n",
		  dm->dm_fat_table.b_fix_tx_ant);
}

void odm_ant_div_timers(void *dm_void, u8 state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	if (state == INIT_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_initialize_timer(dm,
				     &dm->dm_swat_table.sw_antdiv_timer,
				     (void *)odm_sw_antdiv_callback, NULL,
				     "sw_antdiv_timer");
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) ||\
	(defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_initialize_timer(dm, &dm->fast_ant_training_timer,
				     (void *)odm_fast_ant_training_callback,
				     NULL, "fast_ant_training_timer");
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_initialize_timer(dm, &dm->evm_fast_ant_training_timer,
				     (void *)phydm_evm_antdiv_callback, NULL,
				     "evm_fast_ant_training_timer");
#endif
	} else if (state == CANCEL_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_cancel_timer(dm,
				 &dm->dm_swat_table.sw_antdiv_timer);
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) ||\
	(defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_cancel_timer(dm, &dm->fast_ant_training_timer);
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_cancel_timer(dm, &dm->evm_fast_ant_training_timer);
#endif
	} else if (state == RELEASE_ANTDIV_TIMMER) {
#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
		odm_release_timer(dm,
				  &dm->dm_swat_table.sw_antdiv_timer);
#elif (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) ||\
	(defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		odm_release_timer(dm, &dm->fast_ant_training_timer);
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
		odm_release_timer(dm, &dm->evm_fast_ant_training_timer);
#endif
	}
}

void phydm_antdiv_debug(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fat_struct	*fat_tab = &dm->dm_fat_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 dm_value[10] = {0};
	char help[] = "-h";
	u8 i, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &dm_value[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1} {0:auto, 1:fix main, 2:fix auto}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{2} {antdiv_period}\n");
		#if (RTL8821C_SUPPORT == 1)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{3} {en} {0:Default, 1:HW_Div, 2:SW_Div}\n");
		#endif

	} else if (dm_value[0] == 1) {
	/*@fixed or auto antenna*/
		if (dm_value[1] == 0) {
			dm->ant_type = ODM_AUTO_ANT;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: Auto\n");
		} else if (dm_value[1] == 1) {
			dm->ant_type = ODM_FIX_MAIN_ANT;
			
		#if (RTL8710C_SUPPORT == 1)
			dm->antdiv_select = 1;
		#endif
		
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: Fix Main\n");
		} else if (dm_value[1] == 2) {
			dm->ant_type = ODM_FIX_AUX_ANT;
			
		#if (RTL8710C_SUPPORT == 1)
			dm->antdiv_select = 2;
		#endif
		
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: Fix Aux\n");
		}

		if (dm->ant_type != ODM_AUTO_ANT) {
			odm_stop_antenna_switch_dm(dm);
			if (dm->ant_type == ODM_FIX_MAIN_ANT)
				odm_update_rx_idle_ant(dm, MAIN_ANT);
			else if (dm->ant_type == ODM_FIX_AUX_ANT)
				odm_update_rx_idle_ant(dm, AUX_ANT);
		} else {
			phydm_enable_antenna_diversity(dm);
		}
		dm->pre_ant_type = dm->ant_type;
	} else if (dm_value[0] == 2) {
	/*@dynamic period for AntDiv*/
		dm->antdiv_period = (u8)dm_value[1];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "AntDiv_period=((%d))\n", dm->antdiv_period);
	}
	#if (RTL8821C_SUPPORT == 1)
	else if (dm_value[0] == 3 &&
		 dm->support_ic_type == ODM_RTL8821C) {
		/*Only for 8821C*/
		if (dm_value[1] == 0) {
			fat_tab->force_antdiv_type = false;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[8821C] AntDiv: Default\n");
		} else if (dm_value[1] == 1) {
			fat_tab->force_antdiv_type = true;
			fat_tab->antdiv_type_dbg = CG_TRX_HW_ANTDIV;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[8821C] AntDiv: HW diversity\n");
		} else if (dm_value[1] == 2) {
			fat_tab->force_antdiv_type = true;
			fat_tab->antdiv_type_dbg = S0S1_SW_ANTDIV;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[8821C] AntDiv: SW diversity\n");
		}
	}
	#endif
	#ifdef ODM_EVM_ENHANCE_ANTDIV
	else if (dm_value[0] == 4) {
		if (dm_value[1] == 0) {
			/*@init parameters for EVM AntDiv*/
			phydm_evm_sw_antdiv_init(dm);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "init evm antdiv parameters\n");
		} else if (dm_value[1] == 1) {
			/*training number for EVM AntDiv*/
			dm->antdiv_train_num = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "antdiv_train_num = ((%d))\n",
				 dm->antdiv_train_num);
		} else if (dm_value[1] == 2) {
			/*training interval for EVM AntDiv*/
			dm->antdiv_intvl = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "antdiv_intvl = ((%d))\n",
				 dm->antdiv_intvl);
		} else if (dm_value[1] == 3) {
			/*@function period for EVM AntDiv*/
			dm->evm_antdiv_period = (u8)dm_value[2];
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "evm_antdiv_period = ((%d))\n",
				 dm->evm_antdiv_period);
		} else if (dm_value[1] == 100) {/*show parameters*/
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "ant_type = ((%d))\n", dm->ant_type);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "antdiv_train_num = ((%d))\n",
				 dm->antdiv_train_num);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "antdiv_intvl = ((%d))\n",
				 dm->antdiv_intvl);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "evm_antdiv_period = ((%d))\n",
				 dm->evm_antdiv_period);
		}
	}
	#ifdef CONFIG_2T4R_ANTENNA
	else if (dm_value[0] == 5) { /*Only for 8822B 2T4R case*/

		if (dm_value[1] == 0) {
			dm->ant_type2 = ODM_AUTO_ANT;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: PathB Auto\n");
		} else if (dm_value[1] == 1) {
			dm->ant_type2 = ODM_FIX_MAIN_ANT;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: PathB Fix Main\n");
		} else if (dm_value[1] == 2) {
			dm->ant_type2 = ODM_FIX_AUX_ANT;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "AntDiv: PathB Fix Aux\n");
		}

		if (dm->ant_type2 != ODM_AUTO_ANT) {
			odm_stop_antenna_switch_dm(dm);
			if (dm->ant_type2 == ODM_FIX_MAIN_ANT)
				phydm_update_rx_idle_ant_pathb(dm, MAIN_ANT);
			else if (dm->ant_type2 == ODM_FIX_AUX_ANT)
				phydm_update_rx_idle_ant_pathb(dm, AUX_ANT);
		} else {
			phydm_enable_antenna_diversity(dm);
		}
		dm->pre_ant_type2 = dm->ant_type2;
	}
	#endif
	#endif
	*_used = used;
	*_out_len = out_len;
}

void odm_ant_div_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_ANT_DIV))
		return;

	#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	if (dm->ant_div_type == S0S1_SW_ANTDIV)
		odm_s0s1_sw_ant_div_reset(dm);
	#endif
}

void odm_antenna_diversity_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	odm_ant_div_config(dm);
	odm_ant_div_init(dm);
}

void odm_antenna_diversity(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (*dm->mp_mode)
		return;

	if (!(dm->support_ability & ODM_BB_ANT_DIV)) {
		PHYDM_DBG(dm, DBG_ANT_DIV,
			  "[Return!!!]   Not Support Antenna Diversity Function\n");
		return;
	}

	if (dm->pause_ability & ODM_BB_ANT_DIV) {
		PHYDM_DBG(dm, DBG_ANT_DIV, "Return: Pause AntDIv in LV=%d\n",
			  dm->pause_lv_table.lv_antdiv);
		return;
	}

	odm_ant_div(dm);
}
#endif /*@#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY*/

