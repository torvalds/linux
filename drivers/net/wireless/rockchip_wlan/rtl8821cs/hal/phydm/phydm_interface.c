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

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

/*@
 * ODM IO Relative API.
 */

u8 odm_read_1byte(struct dm_struct *dm, u32 reg_addr)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	return RTL_R8(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_byte(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_read8(rtwdev, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	return rtw_read8(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	return PlatformEFIORead1Byte(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	return rtw_read8(adapter, reg_addr);
#endif
}

u16 odm_read_2byte(struct dm_struct *dm, u32 reg_addr)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	return RTL_R16(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_word(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_read16(rtwdev, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	return rtw_read16(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	return PlatformEFIORead2Byte(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	return rtw_read16(adapter, reg_addr);
#endif
}

u32 odm_read_4byte(struct dm_struct *dm, u32 reg_addr)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	return RTL_R32(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_read_dword(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_read32(rtwdev, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	return rtw_read32(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	return PlatformEFIORead4Byte(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	return rtw_read32(adapter, reg_addr);
#endif
}

void odm_write_1byte(struct dm_struct *dm, u32 reg_addr, u8 data)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	RTL_W8(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_byte(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_write8(rtwdev, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	rtw_write8(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformEFIOWrite1Byte(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	rtw_write8(adapter, reg_addr, data);
#endif

	if (dm->en_reg_mntr_byte)
		pr_debug("1byte:addr=0x%x, data=0x%x\n", reg_addr, data);
}

void odm_write_2byte(struct dm_struct *dm, u32 reg_addr, u16 data)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	RTL_W16(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_word(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_write16(rtwdev, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	rtw_write16(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformEFIOWrite2Byte(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	rtw_write16(adapter, reg_addr, data);
#endif

	if (dm->en_reg_mntr_byte)
		pr_debug("2byte:addr=0x%x, data=0x%x\n", reg_addr, data);
}

void odm_write_4byte(struct dm_struct *dm, u32 reg_addr, u32 data)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv *priv = dm->priv;
	RTL_W32(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_write_dword(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_write32(rtwdev, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	rtw_write32(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformEFIOWrite4Byte(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	rtw_write32(adapter, reg_addr, data);
#endif

	if (dm->en_reg_mntr_byte)
		pr_debug("4byte:addr=0x%x, data=0x%x\n", reg_addr, data);
}

void odm_set_mac_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask, u32 data)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(dm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_set_reg_with_mask(rtwdev, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	phy_set_bb_reg(dm->adapter, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(dm->adapter, reg_addr, bit_mask, data);
#endif

	if (dm->en_reg_mntr_mac)
		pr_debug("MAC:addr=0x%x, mask=0x%x, data=0x%x\n",
			 reg_addr, bit_mask, data);
}

u32 odm_get_mac_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(dm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PHY_QueryMacReg(dm->adapter, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_get_reg_with_mask(rtwdev, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return phy_query_bb_reg(dm->adapter, reg_addr, bit_mask);
#else
	return phy_query_mac_reg(dm->adapter, reg_addr, bit_mask);
#endif
}

void odm_set_bb_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask, u32 data)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(dm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_set_reg_with_mask(rtwdev, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	phy_set_bb_reg(dm->adapter, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(dm->adapter, reg_addr, bit_mask, data);
#endif

	if (dm->en_reg_mntr_bb)
		pr_debug("BB:addr=0x%x, mask=0x%x, data=0x%x\n",
			 reg_addr, bit_mask, data);
}

u32 odm_get_bb_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(dm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	return PHY_QueryBBReg(adapter, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_get_reg_with_mask(rtwdev, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return phy_query_bb_reg(dm->adapter, reg_addr, bit_mask);
#else
	return phy_query_bb_reg(dm->adapter, reg_addr, bit_mask);
#endif
}

void odm_set_rf_reg(struct dm_struct *dm, u8 e_rf_path, u32 reg_addr,
		    u32 bit_mask, u32 data)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	phy_set_rf_reg(dm->priv, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PHY_SetRFReg(adapter, e_rf_path, reg_addr, bit_mask, data);
	ODM_delay_us(2);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_set_rfreg(rtlpriv->hw, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	rtw_write_rf(rtwdev, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_rf_reg(dm->adapter, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	phy_set_rf_reg(dm->adapter, e_rf_path, reg_addr, bit_mask, data);
	ODM_delay_us(2);
#endif

	if (dm->en_reg_mntr_rf)
		pr_debug("RF:path=0x%x, addr=0x%x, mask=0x%x, data=0x%x\n",
			 e_rf_path, reg_addr, bit_mask, data);
}

u32 odm_get_rf_reg(struct dm_struct *dm, u8 e_rf_path, u32 reg_addr,
		   u32 bit_mask)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_rf_reg(dm->priv, e_rf_path, reg_addr, bit_mask, 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	return PHY_QueryRFReg(adapter, e_rf_path, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	return rtl_get_rfreg(rtlpriv->hw, e_rf_path, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	return rtw_read_rf(rtwdev, e_rf_path, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return phy_query_rf_reg(dm->adapter, e_rf_path, reg_addr, bit_mask);
#else
	return phy_query_rf_reg(dm->adapter, e_rf_path, reg_addr, bit_mask);
#endif
}

enum hal_status
phydm_set_reg_by_fw(struct dm_struct *dm, enum phydm_halmac_param config_type,
		    u32 offset, u32 data, u32 mask, enum rf_path e_rf_path,
		    u32 delay_time)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	return HAL_MAC_Config_PHY_WriteNByte(dm,
					     config_type,
					     offset,
					     data,
					     mask,
					     e_rf_path,
					     delay_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	PHYDM_DBG(dm, DBG_CMN, "Not support for CE MAC80211 driver!\n");
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	return -ENOTSUPP;
#else
	return rtw_phydm_cfg_phy_para(dm,
				      config_type,
				      offset,
				      data,
				      mask,
				      e_rf_path,
				      delay_time);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	PHYDM_DBG(dm, DBG_CMN, "Not support for CE MAC80211 driver!\n");
#endif
}

/*@
 * ODM Memory relative API.
 */
void odm_allocate_memory(struct dm_struct *dm, void **ptr, u32 length)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	*ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	*ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	*ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	*ptr = rtw_zvmalloc(length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformAllocateMemory(adapter, ptr, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	*ptr = rtw_zvmalloc(length);
#endif
}

/* @length could be ignored, used to detect memory leakage. */
void odm_free_memory(struct dm_struct *dm, void *ptr, u32 length)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	kfree(ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	kfree(ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	kfree(ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_vmfree(ptr, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	/* struct void*    adapter = dm->adapter; */
	PlatformFreeMemory(ptr, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_vmfree(ptr, length);
#endif
}

void odm_move_memory(struct dm_struct *dm, void *dest, void *src, u32 length)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memcpy(dest, src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	memcpy(dest, src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	memcpy(dest, src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memcpy(dest, src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformMoveMemory(dest, src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_memcpy(dest, src, length);
#endif
}

void odm_memory_set(struct dm_struct *dm, void *pbuf, s8 value, u32 length)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFillMemory(pbuf, length, value);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_memset(pbuf, value, length);
#endif
}

s32 odm_compare_memory(struct dm_struct *dm, void *buf1, void *buf2, u32 length)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return memcmp(buf1, buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return memcmp(buf1, buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	return memcmp(buf1, buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return _rtw_memcmp(buf1, buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PlatformCompareMemory(buf1, buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return rtw_memcmp(buf1, buf2, length);
#endif
}

/*@
 * ODM MISC relative API.
 */
void odm_acquire_spin_lock(struct dm_struct *dm, enum rt_spinlock_type type)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_odm_acquirespinlock(rtlpriv, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	spin_lock(&rtwdev->hal.dm_lock);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	rtw_odm_acquirespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformAcquireSpinLock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	rtw_odm_acquirespinlock(adapter, type);
#endif
}

void odm_release_spin_lock(struct dm_struct *dm, enum rt_spinlock_type type)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;

	rtl_odm_releasespinlock(rtlpriv, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;

	spin_unlock(&rtwdev->hal.dm_lock);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;
	rtw_odm_releasespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformReleaseSpinLock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	rtw_odm_releasespinlock(adapter, type);
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/*@
 * Work item relative API. FOr MP driver only~!
 *   */
void odm_initialize_work_item(
	struct dm_struct *dm,
	PRT_WORK_ITEM work_item,
	RT_WORKITEM_CALL_BACK callback,
	void *context,
	const char *id)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformInitializeWorkItem(adapter, work_item, callback, context, id);
#endif
}

void odm_start_work_item(
	PRT_WORK_ITEM p_rt_work_item)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStartWorkItem(p_rt_work_item);
#endif
}

void odm_stop_work_item(
	PRT_WORK_ITEM p_rt_work_item)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStopWorkItem(p_rt_work_item);
#endif
}

void odm_free_work_item(
	PRT_WORK_ITEM p_rt_work_item)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFreeWorkItem(p_rt_work_item);
#endif
}

void odm_schedule_work_item(
	PRT_WORK_ITEM p_rt_work_item)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformScheduleWorkItem(p_rt_work_item);
#endif
}

boolean
odm_is_work_item_scheduled(
	PRT_WORK_ITEM p_rt_work_item)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PlatformIsWorkItemScheduled(p_rt_work_item);
#endif
}
#endif

/*@
 * ODM Timer relative API.
 */

void ODM_delay_ms(u32 ms)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	mdelay(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	mdelay(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_mdelay_os(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_mdelay_os(ms);
#endif
}

void ODM_delay_us(u32 us)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_us(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	udelay(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	udelay(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_udelay_os(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_udelay_os(us);
#endif
}

void ODM_sleep_ms(u32 ms)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	msleep(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	msleep(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_msleep_os(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_msleep_os(ms);
#endif
}

void ODM_sleep_us(u32 us)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_us(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	usleep_range(us, us + 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	usleep_range(us, us + 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_usleep_os(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_usleep_os(us);
#endif
}

void odm_set_timer(struct dm_struct *dm, struct phydm_timer_list *timer,
		   u32 ms_delay)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	mod_timer(timer, jiffies + RTL_MILISECONDS_TO_JIFFIES(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	mod_timer(timer, jiffies + msecs_to_jiffies(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	mod_timer(&timer->timer, jiffies + msecs_to_jiffies(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_set_timer(timer, ms_delay); /* @ms */
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformSetTimer(adapter, timer, ms_delay);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_set_timer(timer, ms_delay); /* @ms */
#endif
}

void odm_initialize_timer(struct dm_struct *dm, struct phydm_timer_list *timer,
			  void *call_back_func, void *context,
			  const char *sz_id)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	init_timer(timer);
	timer->function = call_back_func;
	timer->data = (unsigned long)dm;
#if 0
	/*@mod_timer(timer, jiffies+RTL_MILISECONDS_TO_JIFFIES(10));	*/
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	timer_setup(timer, call_back_func, 0);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = dm->adapter;

	_init_timer(timer, adapter->pnetdev, call_back_func, dm);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;

	PlatformInitializeTimer(adapter, timer, (RT_TIMER_CALL_BACK)call_back_func, context, sz_id);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	struct _ADAPTER *adapter = dm->adapter;

	rtw_init_timer(timer, adapter->pnetdev, (TIMER_FUN)call_back_func, dm, NULL);
#endif
}

void odm_cancel_timer(struct dm_struct *dm, struct phydm_timer_list *timer)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	del_timer(timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	del_timer(timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	del_timer(&timer->timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_cancel_timer_ex(timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PlatformCancelTimer(adapter, timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_cancel_timer(timer);
#endif
}

void odm_release_timer(struct dm_struct *dm, struct phydm_timer_list *timer)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	void *adapter = dm->adapter;

	/* @<20120301, Kordan> If the initilization fails,
	 * InitializeAdapterXxx will return regardless of InitHalDm.
	 * Hence, uninitialized timers cause BSOD when the driver
	 * releases resources since the init fail.
	 */
	if (timer == 0) {
		PHYDM_DBG(dm, ODM_COMP_INIT,
			  "[%s] Timer is NULL! Please check!\n", __func__);
		return;
	}

	PlatformReleaseTimer(adapter, timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_del_timer(timer);
#endif
}

u8 phydm_trans_h2c_id(struct dm_struct *dm, u8 phydm_h2c_id)
{
	u8 platform_h2c_id = phydm_h2c_id;

	switch (phydm_h2c_id) {
	/* @1 [0] */
	case ODM_H2C_RSSI_REPORT:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		#if (RTL8188E_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8188E)
			platform_h2c_id = H2C_88E_RSSI_REPORT;
		else
		#endif
			platform_h2c_id = H2C_RSSI_REPORT;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
		platform_h2c_id = H2C_RSSI_SETTING;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8192F_SUPPORT == 1)) /*@jj add 20170822*/
		if (dm->support_ic_type == ODM_RTL8881A || dm->support_ic_type == ODM_RTL8192E || dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RSSI_REPORT;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (dm->support_ic_type == ODM_RTL8812)
			platform_h2c_id = H2C_8812_RSSI_REPORT;
		else
#endif
		{
		}
#endif

		break;

	/* @1 [3] */
	case ODM_H2C_WIFI_CALIBRATION:
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		platform_h2c_id = H2C_WIFI_CALIBRATION;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (RTL8723B_SUPPORT == 1)
		platform_h2c_id = H2C_8723B_BT_WLAN_CALIBRATION;
#endif

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#endif
		break;

	/* @1 [4] */
	case ODM_H2C_IQ_CALIBRATION:
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		platform_h2c_id = H2C_IQ_CALIBRATION;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
		platform_h2c_id = H2C_8812_IQ_CALIBRATION;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#endif

		break;
	/* @1 [5] */
	case ODM_H2C_RA_PARA_ADJUST:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		platform_h2c_id = H2C_RA_PARA_ADJUST;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
		platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
#elif ((RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1))
		platform_h2c_id = H2C_RA_PARA_ADJUST;
#elif (RTL8192E_SUPPORT == 1)
		platform_h2c_id = H2C_8192E_RA_PARA_ADJUST;
#elif (RTL8723B_SUPPORT == 1)
		platform_h2c_id = H2C_8723B_RA_PARA_ADJUST;
#endif

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8192F_SUPPORT == 1)) /*@jj add 20170822*/
		if (dm->support_ic_type == ODM_RTL8881A || dm->support_ic_type == ODM_RTL8192E || dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RA_PARA_ADJUST;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (dm->support_ic_type == ODM_RTL8812)
			platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
		else
#endif
		{
		}
#endif

		break;

	/* @1 [6] */
	case PHYDM_H2C_DYNAMIC_TX_PATH:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if (RTL8814A_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_8814A_DYNAMIC_TX_PATH;
	#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (RTL8814A_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_DYNAMIC_TX_PATH;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (RTL8814A_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_88XX_DYNAMIC_TX_PATH;
#endif

#endif

		break;

	/* @[7]*/
	case PHYDM_H2C_FW_TRACE_EN:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

		platform_h2c_id = H2C_FW_TRACE_EN;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

		platform_h2c_id = 0x49;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8192F_SUPPORT == 1)) /*@jj add 20170822*/
		if (dm->support_ic_type == ODM_RTL8881A || dm->support_ic_type == ODM_RTL8192E || dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_FW_TRACE_EN;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
		if (dm->support_ic_type == ODM_RTL8812)
			platform_h2c_id = H2C_8812_FW_TRACE_EN;
		else
#endif
		{
		}

#endif

		break;

	case PHYDM_H2C_TXBF:
#if ((RTL8192E_SUPPORT == 1) || (RTL8812A_SUPPORT == 1))
		platform_h2c_id = 0x41; /*@H2C_TxBF*/
#endif
		break;

	case PHYDM_H2C_MU:
#if (RTL8822B_SUPPORT == 1)
		platform_h2c_id = 0x4a; /*@H2C_MU*/
#endif
		break;

	default:
		platform_h2c_id = phydm_h2c_id;
		break;
	}

	return platform_h2c_id;
}

/*@ODM FW relative API.*/

void odm_fill_h2c_cmd(struct dm_struct *dm, u8 phydm_h2c_id, u32 cmd_len,
		      u8 *cmd_buf)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	struct rtw_dev *rtwdev = dm->adapter;
	u8 cmd_id, cmd_class;
	u8 h2c_pkt[8];
#else
	void *adapter = dm->adapter;
#endif
	u8 h2c_id = phydm_trans_h2c_id(dm, phydm_h2c_id);

	PHYDM_DBG(dm, DBG_RA, "[H2C]  h2c_id=((0x%x))\n", h2c_id);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (dm->support_ic_type == ODM_RTL8188E) {
		if (!dm->ra_support88e)
			FillH2CCmd88E(adapter, h2c_id, cmd_len, cmd_buf);
	} else if (dm->support_ic_type == ODM_RTL8814A)
		FillH2CCmd8814A(adapter, h2c_id, cmd_len, cmd_buf);
	else if (dm->support_ic_type == ODM_RTL8822B)
		FillH2CCmd8822B(adapter, h2c_id, cmd_len, cmd_buf);
	else
		FillH2CCmd(adapter, h2c_id, cmd_len, cmd_buf);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

	#ifdef DM_ODM_CE_MAC80211
	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->hw, h2c_id, cmd_len, cmd_buf);
	#elif defined(DM_ODM_CE_MAC80211_V2)
	cmd_id = phydm_h2c_id & 0x1f;
	cmd_class = (phydm_h2c_id >> RTW_H2C_CLASS_OFFSET) & 0x7;
	memcpy(h2c_pkt + 1, cmd_buf, 7);
	h2c_pkt[0] = phydm_h2c_id;
	rtw_fw_send_h2c_packet(rtwdev, h2c_pkt, cmd_id, cmd_class);
	/* TODO: implement fill h2c command for rtwlan */
	#else
	rtw_hal_fill_h2c_cmd(adapter, h2c_id, cmd_len, cmd_buf);
	#endif

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

	#if (RTL8812A_SUPPORT == 1)
	if (dm->support_ic_type == ODM_RTL8812) {
		fill_h2c_cmd8812(dm->priv, h2c_id, cmd_len, cmd_buf);
	} else
	#endif
	{
		GET_HAL_INTERFACE(dm->priv)->fill_h2c_cmd_handler(dm->priv, h2c_id, cmd_len, cmd_buf);
	}

#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	rtw_hal_fill_h2c_cmd(adapter, h2c_id, cmd_len, cmd_buf);

#endif
}

u8 phydm_c2H_content_parsing(void *dm_void, u8 c2h_cmd_id, u8 c2h_cmd_len,
			     u8 *tmp_buf)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = dm->adapter;
#endif
	u8 extend_c2h_sub_id = 0;
	u8 find_c2h_cmd = true;

	if (c2h_cmd_len > 12 || c2h_cmd_len == 0) {
		pr_debug("[Warning] Error C2H ID=%d, len=%d\n",
			 c2h_cmd_id, c2h_cmd_len);

		find_c2h_cmd = false;
		return find_c2h_cmd;
	}

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
#ifdef CONFIG_PATH_DIVERSITY
	case PHYDM_C2H_DYNAMIC_TX_PATH_RPT:
		if (dm->support_ic_type & (ODM_RTL8814A))
			phydm_c2h_dtp_handler(dm, tmp_buf, c2h_cmd_len);
		break;
#endif

	case PHYDM_C2H_IQK_FINISH:
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

		if (dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821)) {
			RT_TRACE(COMP_MP, DBG_LOUD, ("== FW IQK Finish ==\n"));
			odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.is_iqk_in_progress = false;
			odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.iqk_progressing_time = 0;
			dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(dm, dm->rf_calibrate_info.iqk_start_time);
		}

#endif
		break;

	case PHYDM_C2H_CLM_MONITOR:
		phydm_clm_c2h_report_handler(dm, tmp_buf, c2h_cmd_len);
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

u64 odm_get_current_time(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return (u64)rtw_get_current_time();
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return jiffies;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	return jiffies;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_current_time();
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PlatformGetCurrentTime();
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return rtw_get_current_time();
#endif
}

u64 odm_get_progressing_time(struct dm_struct *dm, u64 start_time)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return rtw_get_passing_time_ms((u32)start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return jiffies_to_msecs(jiffies - start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	return jiffies_to_msecs(jiffies - start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_passing_time_ms((systime)start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return ((PlatformGetCurrentTime() - start_time) >> 10);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	return rtw_get_passing_time_ms(start_time);
#endif
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE)) && \
	(!defined(DM_ODM_CE_MAC80211) && !defined(DM_ODM_CE_MAC80211_V2))

void phydm_set_hw_reg_handler_interface(struct dm_struct *dm, u8 RegName,
					u8 *val)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct _ADAPTER *adapter = dm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	((PADAPTER)adapter)->HalFunc.SetHwRegHandler(adapter, RegName, val);
#else
	adapter->hal_func.set_hw_reg_handler(adapter, RegName, val);
#endif

#endif
}

void phydm_get_hal_def_var_handler_interface(struct dm_struct *dm,
					     enum _HAL_DEF_VARIABLE e_variable,
					     void *value)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct _ADAPTER *adapter = dm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	((PADAPTER)adapter)->HalFunc.GetHalDefVarHandler(adapter, e_variable, value);
#else
	adapter->hal_func.get_hal_def_var_handler(adapter, e_variable, value);
#endif

#endif
}

#endif

void odm_set_tx_power_index_by_rate_section(struct dm_struct *dm,
					    enum rf_path path, u8 ch,
					    u8 section)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	PHY_SetTxPowerIndexByRateSection(adapter, path, ch, section);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = dm->adapter;

	phy_set_tx_power_index_by_rs(adapter, ch, path, section);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_tx_power_index_by_rate_section(dm->adapter, path, ch, section);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	PHY_SetTxPowerIndexByRateSection(adapter, path, ch, section);
#endif
}

u8 odm_get_tx_power_index(struct dm_struct *dm, enum rf_path path, u8 rate,
			  u8 bw, u8 ch)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;

	return PHY_GetTxPowerIndex(dm->adapter, path, rate, (CHANNEL_WIDTH)bw, ch);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = dm->adapter;

	return phy_get_tx_power_index(adapter, path, rate, bw, ch);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	void *adapter = dm->adapter;

	return phy_get_tx_power_index(adapter, path, rate, bw, ch);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return phy_get_tx_power_index(dm->adapter, path, rate, bw, ch);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	return PHY_GetTxPowerIndex(dm->adapter, path, rate, bw, ch);
#endif
}

u8 odm_efuse_one_byte_read(struct dm_struct *dm, u16 addr, u8 *data,
			   boolean b_pseu_do_test)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;

	return (u8)EFUSE_OneByteRead(adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = dm->adapter;

	return rtl_efuse_onebyte_read(adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
	return -1;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return efuse_onebyte_read(dm->adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	return Efuse_OneByteRead(dm, addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	return (u8)efuse_OneByteRead(adapter, addr, data, b_pseu_do_test);
#endif
}

void odm_efuse_logical_map_read(struct dm_struct *dm, u8 type, u16 offset,
				u32 *data)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;

	EFUSE_ShadowRead(adapter, type, offset, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = dm->adapter;

	rtl_efuse_logical_map_read(adapter, type, offset, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	efuse_logical_map_read(dm->adapter, type, offset, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	void *adapter = dm->adapter;

	EFUSE_ShadowRead(adapter, type, offset, data);
#endif
}

enum hal_status
odm_iq_calibrate_by_fw(struct dm_struct *dm, u8 clear, u8 segment)
{
	enum hal_status iqk_result = HAL_STATUS_FAILURE;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = dm->adapter;

	if (HAL_MAC_FWIQK_Trigger(&GET_HAL_MAC_INFO(adapter), clear, segment) == 0)
		iqk_result = HAL_STATUS_SUCCESS;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = dm->adapter;

	iqk_result = rtl_phydm_fw_iqk(adapter, clear, segment);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#else
	iqk_result = rtw_phydm_fw_iqk(dm, clear, segment);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	iqk_result = rtw_phydm_fw_iqk(dm, clear, segment);
#endif
	return iqk_result;
}

enum hal_status
odm_dpk_by_fw(struct dm_struct *dm)
{
	enum hal_status dpk_result = HAL_STATUS_FAILURE;
#if 0

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = dm->adapter;

	if (HAL_MAC_FWDPK_Trigger(&GET_HAL_MAC_INFO(adapter)) == 0)
		dpk_result = HAL_STATUS_SUCCESS;
#else
	dpk_result = rtw_phydm_fw_dpk(dm);
#endif

#endif
	return dpk_result;
}

void phydm_cmn_sta_info_hook(struct dm_struct *dm, u8 mac_id,
			     struct cmn_sta_info *pcmn_sta_info)
{
	dm->phydm_sta_info[mac_id] = pcmn_sta_info;

	if (is_sta_active(pcmn_sta_info))
		dm->phydm_macid_table[pcmn_sta_info->mac_id] = mac_id;
}

void phydm_macid2sta_idx_table(struct dm_struct *dm, u8 entry_idx,
			       struct cmn_sta_info *pcmn_sta_info)
{
	if (is_sta_active(pcmn_sta_info))
		dm->phydm_macid_table[pcmn_sta_info->mac_id] = entry_idx;
}

void phydm_add_interrupt_mask_handler(struct dm_struct *dm, u8 interrupt_type)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	struct rtl8192cd_priv *priv = dm->priv;

	#if IS_EXIST_PCI || IS_EXIST_EMBEDDED
	if (dm->support_interface == ODM_ITRF_PCIE)
		GET_HAL_INTERFACE(priv)->AddInterruptMaskHandler(priv,
								 interrupt_type)
								 ;
	#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#endif
}

void phydm_enable_rx_related_interrupt_handler(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	struct rtl8192cd_priv *priv = dm->priv;

	#if IS_EXIST_PCI || IS_EXIST_EMBEDDED
	if (dm->support_interface == ODM_ITRF_PCIE)
		GET_HAL_INTERFACE(priv)->EnableRxRelatedInterruptHandler(priv);
	#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#endif
}

#if 0
boolean
phydm_get_txbf_en(
	struct dm_struct		*dm,
	u16							mac_id,
	u8							i
)
{
	boolean txbf_en = false;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && !defined(DM_ODM_CE_MAC80211)

#ifdef CONFIG_BEAMFORMING
	enum beamforming_cap beamform_cap;
	void *adapter = dm->adapter;
	#ifdef PHYDM_BEAMFORMING_SUPPORT
	beamform_cap =
	phydm_beamforming_get_entry_beam_cap_by_mac_id(dm, mac_id);
	#else/*@for drv beamforming*/
	beamform_cap =
	beamforming_get_entry_beam_cap_by_mac_id(&adapter->mlmepriv, mac_id);
	#endif
	if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
		txbf_en = true;
	else
		txbf_en = false;
#endif /*@#ifdef CONFIG_BEAMFORMING*/

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

#ifdef PHYDM_BEAMFORMING_SUPPORT
	u8 idx = 0xff;
	boolean act_bfer = false;
	BEAMFORMING_CAP beamform_cap = BEAMFORMING_CAP_NONE;
	PRT_BEAMFORMING_ENTRY	entry = NULL;
	struct rtl8192cd_priv *priv			= dm->priv;
	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	struct _BF_DIV_COEX_	*dm_bdc_table = &dm->dm_bdc_table;

	dm_bdc_table->num_txbfee_client = 0;
	dm_bdc_table->num_txbfer_client = 0;
	#endif
#endif

#ifdef PHYDM_BEAMFORMING_SUPPORT
	beamform_cap = Beamforming_GetEntryBeamCapByMacId(priv, mac_id);
	entry = Beamforming_GetEntryByMacId(priv, mac_id, &idx);
	if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU)) {
		if (entry->Sounding_En)
			txbf_en = true;
		else
			txbf_en = false;
		act_bfer = true;
	}
	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY)) /*@BDC*/
	if (act_bfer == true) {
		dm_bdc_table->w_bfee_client[i] = true; /* @AP act as BFer */
		dm_bdc_table->num_txbfee_client++;
	} else
		dm_bdc_table->w_bfee_client[i] = false; /* @AP act as BFer */

	if (beamform_cap & (BEAMFORMEE_CAP_HT_EXPLICIT | BEAMFORMEE_CAP_VHT_SU)) {
		dm_bdc_table->w_bfer_client[i] = true; /* @AP act as BFee */
		dm_bdc_table->num_txbfer_client++;
	} else
		dm_bdc_table->w_bfer_client[i] = false; /* @AP act as BFer */

	#endif
#endif

#endif
	return txbf_en;
}
#endif

void phydm_iqk_wait(struct dm_struct *dm, u32 timeout)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	PHYDM_DBG(dm, DBG_CMN, "Not support for CE MAC80211 driver!\n");
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211_V2)
#else
	void *adapter = dm->adapter;

	rtl8812_iqk_wait(adapter, timeout);
#endif
#endif
}

u8 phydm_get_hwrate_to_mrate(struct dm_struct *dm, u8 rate)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	return HwRateToMRate(rate);
#endif
	return 0;
}

void phydm_set_crystalcap(struct dm_struct *dm, u8 crystal_cap)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	ROM_odm_SetCrystalCap(dm, crystal_cap);
#endif
}

void phydm_run_in_thread_cmd(struct dm_struct *dm, void (*func)(void *),
			     void *context)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	PHYDM_DBG(dm, DBG_CMN, "Not support for CE MAC80211 driver!\n");
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void *adapter = dm->adapter;

	rtw_run_in_thread_cmd(adapter, func, context);
#endif
}

u8 phydm_get_tx_rate(struct dm_struct *dm)
{
	struct _hal_rf_ *rf = &dm->rf_table;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = dm->adapter;
#endif
	u8 tx_rate = 0xff;
	u8 mpt_rate_index = 0;

	if (*dm->mp_mode == 1) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &adapter->MptCtx;

		tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#ifdef CONFIG_MP_INCLUDED
		if (rf->mp_rate_index)
			mpt_rate_index = *rf->mp_rate_index;

		tx_rate = mpt_to_mgnt_rate(mpt_rate_index);
#endif
#endif
#endif
	} else {
		u16 rate = *dm->forced_data_rate;

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			struct _ADAPTER *adapter = dm->adapter;

			tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
			tx_rate = dm->tx_rate;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
			else
				tx_rate = rf->p_rate_index;
#endif
		} else { /*force rate*/
			tx_rate = (u8)rate;
		}
	}

	return tx_rate;
}

u8 phydm_get_tx_power_dbm(struct dm_struct *dm, u8 rf_path,
					u8 rate, u8 bandwidth, u8 channel)
{
	u8 tx_power_dbm = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER *adapter = dm->adapter;
	tx_power_dbm = PHY_GetTxPowerFinalAbsoluteValue(adapter, rf_path, rate, bandwidth, channel);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	tx_power_dbm = phy_get_tx_power_final_absolute_value(dm->adapter, rf_path, rate, bandwidth, channel);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	tx_power_dbm = PHY_GetTxPowerFinalAbsoluteValue(dm, rf_path, rate, bandwidth, channel);
#endif
	return tx_power_dbm;
}

u64 phydm_division64(u64 x, u64 y)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	do_div(x, y); 
	return x;
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return x / y;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_division64(x, y);
#endif
}
