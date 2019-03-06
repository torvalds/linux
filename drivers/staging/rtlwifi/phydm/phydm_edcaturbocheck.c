// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

void odm_edca_turbo_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	dm->dm_edca_table.is_current_turbo_edca = false;
	dm->dm_edca_table.is_cur_rdl_state = false;

	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO, "Original VO PARAM: 0x%x\n",
		     odm_read_4byte(dm, ODM_EDCA_VO_PARAM));
	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO, "Original VI PARAM: 0x%x\n",
		     odm_read_4byte(dm, ODM_EDCA_VI_PARAM));
	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO, "Original BE PARAM: 0x%x\n",
		     odm_read_4byte(dm, ODM_EDCA_BE_PARAM));
	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO, "Original BK PARAM: 0x%x\n",
		     odm_read_4byte(dm, ODM_EDCA_BK_PARAM));

} /* ODM_InitEdcaTurbo */

void odm_edca_turbo_check(void *dm_void)
{
	/* For AP/ADSL use struct rtl8192cd_priv* */
	/* For CE/NIC use struct void* */

	/* 2011/09/29 MH In HW integration first stage, we provide 4 different
	 * handle to operate at the same time.
	 * In the stage2/3, we need to prive universal interface and merge all
	 * HW dynamic mechanism.
	 */
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO,
		     "%s========================>\n", __func__);

	if (!(dm->support_ability & ODM_MAC_EDCA_TURBO))
		return;

	switch (dm->support_platform) {
	case ODM_WIN:

		break;

	case ODM_CE:
		odm_edca_turbo_check_ce(dm);
		break;
	}
	ODM_RT_TRACE(dm, ODM_COMP_EDCA_TURBO,
		     "<========================%s\n", __func__);

} /* odm_CheckEdcaTurbo */

void odm_edca_turbo_check_ce(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;
	u32 edca_be_ul = 0x5ea42b;
	u32 edca_be_dl = 0x5ea42b;
	u32 edca_be = 0x5ea42b;
	bool is_cur_rdlstate;
	bool edca_turbo_on = false;

	if (dm->wifi_test)
		return;

	if (!dm->is_linked) {
		rtlpriv->dm.is_any_nonbepkts = false;
		return;
	}

	if (rtlpriv->dm.dbginfo.num_non_be_pkt > 0x100)
		rtlpriv->dm.is_any_nonbepkts = true;
	rtlpriv->dm.dbginfo.num_non_be_pkt = 0;

	cur_txok_cnt = rtlpriv->stats.txbytesunicast_inperiod;
	cur_rxok_cnt = rtlpriv->stats.rxbytesunicast_inperiod;

	/*b_bias_on_rx = false;*/
	edca_turbo_on = ((!rtlpriv->dm.is_any_nonbepkts) &&
			 (!rtlpriv->dm.disable_framebursting)) ?
				true :
				false;

	if (rtlpriv->mac80211.mode == WIRELESS_MODE_B)
		goto label_exit;

	if (edca_turbo_on) {
		is_cur_rdlstate =
			(cur_rxok_cnt > cur_txok_cnt * 4) ? true : false;

		edca_be = is_cur_rdlstate ? edca_be_dl : edca_be_ul;
		rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM_8822B, edca_be);
		rtlpriv->dm.is_cur_rdlstate = is_cur_rdlstate;
		rtlpriv->dm.current_turbo_edca = true;
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;

			rtlpriv->cfg->ops->set_hw_reg(rtlpriv->hw,
						      HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
			rtlpriv->dm.current_turbo_edca = false;
		}
	}

label_exit:
	rtlpriv->dm.is_any_nonbepkts = false;
}
