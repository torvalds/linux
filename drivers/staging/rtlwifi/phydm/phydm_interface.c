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

/*
 * ODM IO Relative API.
 */

u8 odm_read_1byte(struct phy_dm_struct *dm, u32 reg_addr)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_byte(rtlpriv, reg_addr);
}

u16 odm_read_2byte(struct phy_dm_struct *dm, u32 reg_addr)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_word(rtlpriv, reg_addr);
}

u32 odm_read_4byte(struct phy_dm_struct *dm, u32 reg_addr)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_dword(rtlpriv, reg_addr);
}

void odm_write_1byte(struct phy_dm_struct *dm, u32 reg_addr, u8 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_byte(rtlpriv, reg_addr, data);
}

void odm_write_2byte(struct phy_dm_struct *dm, u32 reg_addr, u16 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_word(rtlpriv, reg_addr, data);
}

void odm_write_4byte(struct phy_dm_struct *dm, u32 reg_addr, u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_dword(rtlpriv, reg_addr, data);
}

void odm_set_mac_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask,
		     u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
}

u32 odm_get_mac_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
}

void odm_set_bb_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask,
		    u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
}

u32 odm_get_bb_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
}

void odm_set_rf_reg(struct phy_dm_struct *dm, enum odm_rf_radio_path e_rf_path,
		    u32 reg_addr, u32 bit_mask, u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_rfreg(rtlpriv->hw, (enum radio_path)e_rf_path, reg_addr,
		      bit_mask, data);
}

u32 odm_get_rf_reg(struct phy_dm_struct *dm, enum odm_rf_radio_path e_rf_path,
		   u32 reg_addr, u32 bit_mask)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_rfreg(rtlpriv->hw, (enum radio_path)e_rf_path, reg_addr,
			     bit_mask);
}

/*
 * ODM Memory relative API.
 */
void odm_allocate_memory(struct phy_dm_struct *dm, void **ptr, u32 length)
{
	*ptr = kmalloc(length, GFP_ATOMIC);
}

/* length could be ignored, used to detect memory leakage. */
void odm_free_memory(struct phy_dm_struct *dm, void *ptr, u32 length)
{
	kfree(ptr);
}

void odm_move_memory(struct phy_dm_struct *dm, void *p_dest, void *src,
		     u32 length)
{
	memcpy(p_dest, src, length);
}

void odm_memory_set(struct phy_dm_struct *dm, void *pbuf, s8 value, u32 length)
{
	memset(pbuf, value, length);
}

s32 odm_compare_memory(struct phy_dm_struct *dm, void *p_buf1, void *buf2,
		       u32 length)
{
	return memcmp(p_buf1, buf2, length);
}

/*
 * ODM MISC relative API.
 */
void odm_acquire_spin_lock(struct phy_dm_struct *dm, enum rt_spinlock_type type)
{
}

void odm_release_spin_lock(struct phy_dm_struct *dm, enum rt_spinlock_type type)
{
}

/*
 * ODM Timer relative API.
 */
void odm_stall_execution(u32 us_delay) { udelay(us_delay); }

void ODM_delay_ms(u32 ms) { mdelay(ms); }

void ODM_delay_us(u32 us) { udelay(us); }

void ODM_sleep_ms(u32 ms) { msleep(ms); }

void ODM_sleep_us(u32 us) { usleep_range(us, us + 1); }

static u8 phydm_trans_h2c_id(struct phy_dm_struct *dm, u8 phydm_h2c_id)
{
	u8 platform_h2c_id = phydm_h2c_id;

	switch (phydm_h2c_id) {
	/* 1 [0] */
	case ODM_H2C_RSSI_REPORT:

		break;

	/* 1 [3] */
	case ODM_H2C_WIFI_CALIBRATION:

		break;

	/* 1 [4] */
	case ODM_H2C_IQ_CALIBRATION:

		break;
	/* 1 [5] */
	case ODM_H2C_RA_PARA_ADJUST:

		break;

	/* 1 [6] */
	case PHYDM_H2C_DYNAMIC_TX_PATH:

		break;

	/* [7]*/
	case PHYDM_H2C_FW_TRACE_EN:

		platform_h2c_id = 0x49;

		break;

	case PHYDM_H2C_TXBF:
		break;

	case PHYDM_H2C_MU:
		platform_h2c_id = 0x4a; /*H2C_MU*/
		break;

	default:
		platform_h2c_id = phydm_h2c_id;
		break;
	}

	return platform_h2c_id;
}

/*ODM FW relative API.*/

void odm_fill_h2c_cmd(struct phy_dm_struct *dm, u8 phydm_h2c_id, u32 cmd_len,
		      u8 *cmd_buffer)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	u8 platform_h2c_id;

	platform_h2c_id = phydm_trans_h2c_id(dm, phydm_h2c_id);

	ODM_RT_TRACE(dm, PHYDM_COMP_RA_DBG,
		     "[H2C]  platform_h2c_id = ((0x%x))\n", platform_h2c_id);

	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->hw, platform_h2c_id, cmd_len,
					cmd_buffer);
}

u8 phydm_c2H_content_parsing(void *dm_void, u8 c2h_cmd_id, u8 c2h_cmd_len,
			     u8 *tmp_buf)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 extend_c2h_sub_id = 0;
	u8 find_c2h_cmd = true;

	switch (c2h_cmd_id) {
	case PHYDM_C2H_DBG:
		phydm_fw_trace_handler(dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_RPT:
		phydm_c2h_ra_report_handler(dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_PARA_RPT:
		odm_c2h_ra_para_report_handler(dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_DYNAMIC_TX_PATH_RPT:
		break;

	case PHYDM_C2H_IQK_FINISH:
		break;

	case PHYDM_C2H_DBG_CODE:
		phydm_fw_trace_handler_code(dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_EXTEND:
		extend_c2h_sub_id = tmp_buf[0];
		if (extend_c2h_sub_id == PHYDM_EXTEND_C2H_DBG_PRINT)
			phydm_fw_trace_handler_8051(dm, tmp_buf, c2h_cmd_len);

		break;

	default:
		find_c2h_cmd = false;
		break;
	}

	return find_c2h_cmd;
}

u64 odm_get_current_time(struct phy_dm_struct *dm) { return jiffies; }

u64 odm_get_progressing_time(struct phy_dm_struct *dm, u64 start_time)
{
	return jiffies_to_msecs(jiffies - (u32)start_time);
}

void odm_set_tx_power_index_by_rate_section(struct phy_dm_struct *dm,
					    u8 rf_path, u8 channel,
					    u8 rate_section)
{
	void *adapter = dm->adapter;

	phy_set_tx_power_index_by_rs(adapter, channel, rf_path, rate_section);
}

u8 odm_get_tx_power_index(struct phy_dm_struct *dm, u8 rf_path, u8 tx_rate,
			  u8 band_width, u8 channel)
{
	void *adapter = dm->adapter;

	return phy_get_tx_power_index(adapter, (enum odm_rf_radio_path)rf_path,
				      tx_rate, band_width, channel);
}
