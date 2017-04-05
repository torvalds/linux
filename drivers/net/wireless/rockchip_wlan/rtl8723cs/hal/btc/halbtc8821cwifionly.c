#include "mp_precomp.h"

static struct	rfe_type_8821c_wifi_only	gl_rfe_type_8821c_1ant;
static struct	rfe_type_8821c_wifi_only	*rfe_type = &gl_rfe_type_8821c_1ant;



VOID hal8821c_wifi_only_switch_antenna(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte is_5g
	)
{
	boolean switch_polatiry_inverse = false;
	u8	regval_0xcb7 = 0;
	u8	pos_type, ctrl_type;

	if (!rfe_type->ext_ant_switch_exist)
		return;

	/* swap control polarity if use different switch control polarity*/
	/*	Normal switch polarity for DPDT, 0xcb4[29:28] = 2b'01 => BTG to Main, WLG to Aux,  0xcb4[29:28] = 2b'10 => BTG to Aux, WLG to Main */
	/*	Normal switch polarity for SPDT, 0xcb4[29:28] = 2b'01 => Ant to BTG,  0xcb4[29:28] = 2b'10 => Ant to WLG */
	if (rfe_type->ext_ant_switch_ctrl_polarity)
		switch_polatiry_inverse =  !switch_polatiry_inverse;

	/* swap control polarity if 1-Ant at Aux */
	if (rfe_type->ant_at_main_port == false)
		switch_polatiry_inverse =  !switch_polatiry_inverse;

	if (is_5g)
		pos_type = BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLA;
	else
		pos_type = BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLG;

	switch (pos_type) {
	default:
	case BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLA:

		break;
	case BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_TO_WLG:
		if (!rfe_type->wlg_Locate_at_btg)
			switch_polatiry_inverse =  !switch_polatiry_inverse;
		break;
	}

	if (pwifionlycfg->haldata_info.ant_div_cfg)
		ctrl_type = BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_ANTDIV;
	else
		ctrl_type = BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_BBSW;


	switch (ctrl_type) {
	default:
	case BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_BBSW:
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0x4c,  0x01800000, 0x2);

		/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as control pin */
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcb4, 0x000000ff,	0x77);

		regval_0xcb7 = (switch_polatiry_inverse == false ? 0x1 : 0x2);

		/* 0xcb4[29:28] = 2b'01 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0 */
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcb4, 0x30000000,	regval_0xcb7);
		break;

	case BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_CTRL_BY_ANTDIV:
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0x4c,  0x01800000, 0x2);

		/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as control pin */
		halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcb4, 0x000000ff,	0x88);

		/* no regval_0xcb7 setup required, because	antenna switch control value by antenna diversity */

		break;

	}

}


VOID halbtc8821c_wifi_only_set_rfe_type(
	IN struct wifi_only_cfg *pwifionlycfg
	)
{

	/* the following setup should be got from Efuse in the future */
	rfe_type->rfe_module_type = (pwifionlycfg->haldata_info.rfe_type) & 0x1f;

	rfe_type->ext_ant_switch_ctrl_polarity = 0;

	switch (rfe_type->rfe_module_type) {
	case 0:
	default:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_DPDT;          /*2-Ant, DPDT, WLG*/
		rfe_type->wlg_Locate_at_btg = false;
		rfe_type->ant_at_main_port = true;
		break;
	case 1:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_SPDT;          /*1-Ant, Main, DPDT or SPDT, WLG */
		rfe_type->wlg_Locate_at_btg = false;
		rfe_type->ant_at_main_port = true;
		break;
	case 2:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_SPDT;            /*1-Ant, Main, DPDT or SPDT, BTG */
		rfe_type->wlg_Locate_at_btg = true;
		rfe_type->ant_at_main_port = true;
		break;
	case 3:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_DPDT;          /*1-Ant, Aux, DPDT, WLG */
		rfe_type->wlg_Locate_at_btg = false;
		rfe_type->ant_at_main_port = false;
		break;
	case 4:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_DPDT;          /*1-Ant, Aux, DPDT, BTG */
		rfe_type->wlg_Locate_at_btg = true;
		rfe_type->ant_at_main_port = false;
		break;
	case 5:
		rfe_type->ext_ant_switch_exist = false;					 /*2-Ant, no antenna switch, WLG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_NONE;
		rfe_type->wlg_Locate_at_btg = false;
		rfe_type->ant_at_main_port = true;
		break;
	case 6:
		rfe_type->ext_ant_switch_exist = false;				 /*2-Ant, no antenna switch, WLG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_NONE;
		rfe_type->wlg_Locate_at_btg = false;
		rfe_type->ant_at_main_port = true;
		break;
	case 7:
		rfe_type->ext_ant_switch_exist = true;				/*2-Ant, DPDT, BTG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_WIFI_ONLY_EXT_ANT_SWITCH_USE_DPDT;
		rfe_type->wlg_Locate_at_btg = true;
		rfe_type->ant_at_main_port = true;
		break;
	}

}


VOID
ex_hal8821c_wifi_only_hw_config(
	IN struct wifi_only_cfg *pwifionlycfg
	)
{
	halbtc8821c_wifi_only_set_rfe_type(pwifionlycfg);

	/* set gnt_wl, gnt_bt control owner to WL*/
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x70, 0x400000, 0x1);

	/*gnt_wl=1 , gnt_bt=0*/
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1704, 0xffffffff, 0x7700);
	halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1700, 0xffffffff, 0xc00f0038);
}

VOID
ex_hal8821c_wifi_only_scannotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{
	hal8821c_wifi_only_switch_antenna(pwifionlycfg, is_5g);
}

VOID
ex_hal8821c_wifi_only_switchbandnotify(
	IN struct wifi_only_cfg *pwifionlycfg,
	IN u1Byte  is_5g
	)
{
	hal8821c_wifi_only_switch_antenna(pwifionlycfg, is_5g);
}

