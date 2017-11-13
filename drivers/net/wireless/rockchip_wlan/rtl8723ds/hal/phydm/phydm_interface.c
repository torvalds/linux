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

/*
 * ODM IO Relative API.
 *   */

u8
odm_read_1byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	return	RTL_R8(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_read_byte(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return rtw_read8(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return	PlatformEFIORead1Byte(adapter, reg_addr);
#endif

}


u16
odm_read_2byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	return	RTL_R16(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_read_word(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return rtw_read16(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return	PlatformEFIORead2Byte(adapter, reg_addr);
#endif

}


u32
odm_read_4byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	return	RTL_R32(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_read_dword(rtlpriv, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return rtw_read32(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return	PlatformEFIORead4Byte(adapter, reg_addr);
#endif

}


void
odm_write_1byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr,
	u8			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	RTL_W8(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_write_byte(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	rtw_write8(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformEFIOWrite1Byte(adapter, reg_addr, data);
#endif

}


void
odm_write_2byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr,
	u16			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	RTL_W16(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_write_word(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	rtw_write16(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformEFIOWrite2Byte(adapter, reg_addr, data);
#endif

}


void
odm_write_4byte(
	struct PHY_DM_STRUCT		*p_dm,
	u32			reg_addr,
	u32			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm->priv;
	RTL_W32(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_write_dword(rtlpriv, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm->adapter;
	rtw_write32(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformEFIOWrite4Byte(adapter, reg_addr, data);
#endif

}


void
odm_set_mac_reg(
	struct PHY_DM_STRUCT	*p_dm,
	u32		reg_addr,
	u32		bit_mask,
	u32		data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(p_dm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(p_dm->adapter, reg_addr, bit_mask, data);
#endif
}


u32
odm_get_mac_reg(
	struct PHY_DM_STRUCT	*p_dm,
	u32		reg_addr,
	u32		bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(p_dm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PHY_QueryMacReg(p_dm->adapter, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
#else
	return phy_query_mac_reg(p_dm->adapter, reg_addr, bit_mask);
#endif
}


void
odm_set_bb_reg(
	struct PHY_DM_STRUCT	*p_dm,
	u32		reg_addr,
	u32		bit_mask,
	u32		data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(p_dm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_set_bbreg(rtlpriv->hw, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(p_dm->adapter, reg_addr, bit_mask, data);
#endif
}


u32
odm_get_bb_reg(
	struct PHY_DM_STRUCT	*p_dm,
	u32		reg_addr,
	u32		bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(p_dm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return PHY_QueryBBReg(adapter, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_get_bbreg(rtlpriv->hw, reg_addr, bit_mask);
#else
	return phy_query_bb_reg(p_dm->adapter, reg_addr, bit_mask);
#endif
}


void
odm_set_rf_reg(
	struct PHY_DM_STRUCT			*p_dm,
	u8			e_rf_path,
	u32				reg_addr,
	u32				bit_mask,
	u32				data
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	phy_set_rf_reg(p_dm->priv, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PHY_SetRFReg(adapter, e_rf_path, reg_addr, bit_mask, data);
	ODM_delay_us(2);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	rtl_set_rfreg(rtlpriv->hw, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_rf_reg(p_dm->adapter, e_rf_path, reg_addr, bit_mask, data);
#endif
}

u32
odm_get_rf_reg(
	struct PHY_DM_STRUCT			*p_dm,
	u8			e_rf_path,
	u32				reg_addr,
	u32				bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_rf_reg(p_dm->priv, e_rf_path, reg_addr, bit_mask, 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	return PHY_QueryRFReg(adapter, e_rf_path, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;

	return rtl_get_rfreg(rtlpriv->hw, e_rf_path, reg_addr, bit_mask);
#else
	return phy_query_rf_reg(p_dm->adapter, e_rf_path, reg_addr, bit_mask);
#endif
}

enum hal_status
phydm_set_reg_by_fw(
	struct PHY_DM_STRUCT			*p_dm,
	enum phydm_halmac_param	config_type,
	u32	offset,
	u32	data,
	u32	mask,
	enum rf_path	e_rf_path,
	u32 delay_time
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	return HAL_MAC_Config_PHY_WriteNByte(p_dm,
									config_type,
									offset,
									data,
									mask,
									e_rf_path,
									delay_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_phydm_cfg_phy_para(p_dm,
							config_type,
							offset,
							data,
							mask,
							e_rf_path,
							delay_time);
#endif

}


/*
 * ODM Memory relative API.
 *   */
void
odm_allocate_memory(
	struct PHY_DM_STRUCT	*p_dm,
	void **p_ptr,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	*p_ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	*p_ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	*p_ptr = rtw_zvmalloc(length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformAllocateMemory(adapter, p_ptr, length);
#endif
}

/* length could be ignored, used to detect memory leakage. */
void
odm_free_memory(
	struct PHY_DM_STRUCT	*p_dm,
	void		*p_ptr,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	kfree(p_ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	kfree(p_ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_vmfree(p_ptr, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	/* struct _ADAPTER*    adapter = p_dm->adapter; */
	PlatformFreeMemory(p_ptr, length);
#endif
}

void
odm_move_memory(
	struct PHY_DM_STRUCT	*p_dm,
	void		*p_dest,
	void		*p_src,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memcpy(p_dest, p_src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	memcpy(p_dest, p_src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memcpy(p_dest, p_src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformMoveMemory(p_dest, p_src, length);
#endif
}

void odm_memory_set(
	struct PHY_DM_STRUCT	*p_dm,
	void		*pbuf,
	s8		value,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFillMemory(pbuf, length, value);
#endif
}
s32 odm_compare_memory(
	struct PHY_DM_STRUCT		*p_dm,
	void           *p_buf1,
	void           *p_buf2,
	u32          length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return memcmp(p_buf1, p_buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return memcmp(p_buf1, p_buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return _rtw_memcmp(p_buf1, p_buf2, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PlatformCompareMemory(p_buf1, p_buf2, length);
#endif
}



/*
 * ODM MISC relative API.
 *   */
void
odm_acquire_spin_lock(
	struct PHY_DM_STRUCT			*p_dm,
	enum rt_spinlock_type	type
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm->adapter;
	rtw_odm_acquirespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformAcquireSpinLock(adapter, type);
#endif
}
void
odm_release_spin_lock(
	struct PHY_DM_STRUCT			*p_dm,
	enum rt_spinlock_type	type
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm->adapter;
	rtw_odm_releasespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformReleaseSpinLock(adapter, type);
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/*
 * Work item relative API. FOr MP driver only~!
 *   */
void
odm_initialize_work_item(
	struct PHY_DM_STRUCT					*p_dm,
	PRT_WORK_ITEM				p_rt_work_item,
	RT_WORKITEM_CALL_BACK		rt_work_item_callback,
	void						*p_context,
	const char					*sz_id
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformInitializeWorkItem(adapter, p_rt_work_item, rt_work_item_callback, p_context, sz_id);
#endif
}


void
odm_start_work_item(
	PRT_WORK_ITEM	p_rt_work_item
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStartWorkItem(p_rt_work_item);
#endif
}


void
odm_stop_work_item(
	PRT_WORK_ITEM	p_rt_work_item
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStopWorkItem(p_rt_work_item);
#endif
}


void
odm_free_work_item(
	PRT_WORK_ITEM	p_rt_work_item
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFreeWorkItem(p_rt_work_item);
#endif
}


void
odm_schedule_work_item(
	PRT_WORK_ITEM	p_rt_work_item
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformScheduleWorkItem(p_rt_work_item);
#endif
}


boolean
odm_is_work_item_scheduled(
	PRT_WORK_ITEM	p_rt_work_item
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PlatformIsWorkItemScheduled(p_rt_work_item);
#endif
}
#endif


/*
 * ODM Timer relative API.
 *   */

void
ODM_delay_ms(u32	ms)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	mdelay(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_mdelay_os(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	delay_ms(ms);
#endif
}

void
ODM_delay_us(u32	us)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_us(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	udelay(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_udelay_os(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us);
#endif
}

void
ODM_sleep_ms(u32	ms)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_ms(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	msleep(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_msleep_os(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	delay_ms(ms);
#endif
}

void
ODM_sleep_us(u32	us)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_us(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	usleep_range(us, us + 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_usleep_os(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us);
#endif
}

void
odm_set_timer(
	struct PHY_DM_STRUCT		*p_dm,
	struct timer_list		*p_timer,
	u32			ms_delay
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	mod_timer(p_timer, jiffies + RTL_MILISECONDS_TO_JIFFIES(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	mod_timer(p_timer, jiffies + msecs_to_jiffies(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_set_timer(p_timer, ms_delay); /* ms */
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PlatformSetTimer(adapter, p_timer, ms_delay);
#endif

}

void
odm_initialize_timer(
	struct PHY_DM_STRUCT			*p_dm,
	struct timer_list			*p_timer,
	void	*call_back_func,
	void				*p_context,
	const char			*sz_id
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	init_timer(p_timer);
	p_timer->function = call_back_func;
	p_timer->data = (unsigned long)p_dm;
	/*mod_timer(p_timer, jiffies+RTL_MILISECONDS_TO_JIFFIES(10));	*/
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	init_timer(p_timer);
	p_timer->function = call_back_func;
	p_timer->data = (unsigned long)p_dm;
	/*mod_timer(p_timer, jiffies+RTL_MILISECONDS_TO_JIFFIES(10));	*/
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm->adapter;

	_init_timer(p_timer, adapter->pnetdev, call_back_func, p_dm);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = p_dm->adapter;

	PlatformInitializeTimer(adapter, p_timer, (RT_TIMER_CALL_BACK)call_back_func, p_context, sz_id);
#endif
}


void
odm_cancel_timer(
	struct PHY_DM_STRUCT		*p_dm,
	struct timer_list		*p_timer
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	del_timer(p_timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	del_timer(p_timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_cancel_timer_ex(p_timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = p_dm->adapter;
	PlatformCancelTimer(adapter, p_timer);
#endif
}


void
odm_release_timer(
	struct PHY_DM_STRUCT		*p_dm,
	struct timer_list		*p_timer
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	struct _ADAPTER *adapter = p_dm->adapter;

	/* <20120301, Kordan> If the initilization fails, InitializeAdapterXxx will return regardless of InitHalDm.
	 * Hence, uninitialized timers cause BSOD when the driver releases resources since the init fail. */
	if (p_timer == 0) {
		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("=====>odm_release_timer(), The timer is NULL! Please check it!\n"));
		return;
	}

	PlatformReleaseTimer(adapter, p_timer);
#endif
}


u8
phydm_trans_h2c_id(
	struct PHY_DM_STRUCT	*p_dm,
	u8		phydm_h2c_id
)
{
	u8 platform_h2c_id = phydm_h2c_id;

	switch (phydm_h2c_id) {
	/* 1 [0] */
	case ODM_H2C_RSSI_REPORT:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm->support_ic_type == ODM_RTL8188E)
			platform_h2c_id = H2C_88E_RSSI_REPORT;
		else if (p_dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_8814A_RSSI_REPORT;
		else
			platform_h2c_id = H2C_RSSI_REPORT;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
		platform_h2c_id = H2C_RSSI_SETTING;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
		if (p_dm->support_ic_type == ODM_RTL8881A || p_dm->support_ic_type == ODM_RTL8192E || p_dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RSSI_REPORT;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8812)
				platform_h2c_id = H2C_8812_RSSI_REPORT;
			else
#endif
			{}
#endif

		break;

	/* 1 [3] */
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


	/* 1 [4] */
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
	/* 1 [5] */
	case ODM_H2C_RA_PARA_ADJUST:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))
			platform_h2c_id = H2C_8814A_RA_PARA_ADJUST;
		else
			platform_h2c_id = H2C_RA_PARA_ADJUST;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
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
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
		if (p_dm->support_ic_type == ODM_RTL8881A || p_dm->support_ic_type == ODM_RTL8192E || p_dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RA_PARA_ADJUST;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8812)
				platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
			else
#endif
			{}
#endif

		break;


	/* 1 [6] */
	case PHYDM_H2C_DYNAMIC_TX_PATH:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_8814A_DYNAMIC_TX_PATH;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (RTL8814A_SUPPORT == 1)
		if (p_dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_DYNAMIC_TX_PATH;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (RTL8814A_SUPPORT == 1)
		if (p_dm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_88XX_DYNAMIC_TX_PATH;
#endif

#endif

		break;

	/* [7]*/
	case PHYDM_H2C_FW_TRACE_EN:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))
			platform_h2c_id = H2C_8814A_FW_TRACE_EN;
		else
			platform_h2c_id = H2C_FW_TRACE_EN;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

		platform_h2c_id = 0x49;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
		if (p_dm->support_ic_type == ODM_RTL8881A || p_dm->support_ic_type == ODM_RTL8192E || p_dm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id  = H2C_88XX_FW_TRACE_EN;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8812)
				platform_h2c_id = H2C_8812_FW_TRACE_EN;
			else
#endif
			{}

#endif

		break;

	case PHYDM_H2C_TXBF:
#if ((RTL8192E_SUPPORT == 1) || (RTL8812A_SUPPORT == 1))
		platform_h2c_id  = 0x41;	/*H2C_TxBF*/
#endif
		break;

	case PHYDM_H2C_MU:
#if (RTL8822B_SUPPORT == 1)
		platform_h2c_id  = 0x4a;	/*H2C_MU*/
#endif
		break;

	default:
		platform_h2c_id = phydm_h2c_id;
		break;
	}

	return platform_h2c_id;

}

/*ODM FW relative API.*/

void
odm_fill_h2c_cmd(
	struct PHY_DM_STRUCT		*p_dm,
	u8			phydm_h2c_id,
	u32			cmd_len,
	u8			*p_cmd_buffer
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;
#else
	struct _ADAPTER	*adapter = p_dm->adapter;
#endif
	u8		h2c_id = phydm_trans_h2c_id(p_dm, phydm_h2c_id);

	PHYDM_DBG(p_dm, DBG_RA, ("[H2C]  h2c_id=((0x%x))\n", h2c_id));

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (p_dm->support_ic_type == ODM_RTL8188E)	{
		if (!p_dm->ra_support88e)
			FillH2CCmd88E(adapter, h2c_id, cmd_len, p_cmd_buffer);
	} else if (p_dm->support_ic_type == ODM_RTL8814A)
		FillH2CCmd8814A(adapter, h2c_id, cmd_len, p_cmd_buffer);
	else if (p_dm->support_ic_type == ODM_RTL8822B)
		FillH2CCmd8822B(adapter, h2c_id, cmd_len, p_cmd_buffer);
	else
		FillH2CCmd(adapter, h2c_id, cmd_len, p_cmd_buffer);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

	#ifdef DM_ODM_CE_MAC80211
	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->hw, h2c_id,cmd_len, p_cmd_buffer);
	#else
	rtw_hal_fill_h2c_cmd(adapter, h2c_id, cmd_len, p_cmd_buffer);
	#endif

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

	#if (RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8812) {
		fill_h2c_cmd8812(p_dm->priv, h2c_id, cmd_len, p_cmd_buffer);
	} else
	#endif
	{
		GET_HAL_INTERFACE(p_dm->priv)->fill_h2c_cmd_handler(p_dm->priv, h2c_id, cmd_len, p_cmd_buffer);
	}
#endif
}

u8
phydm_c2H_content_parsing(
	void			*p_dm_void,
	u8			c2h_cmd_id,
	u8			c2h_cmd_len,
	u8			*tmp_buf
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm->adapter;
#endif
	u8	extend_c2h_sub_id = 0;
	u8	find_c2h_cmd = true;
	
	if ((c2h_cmd_len > 12) || (c2h_cmd_len == 0)) {
		dbg_print("[Warning] Error C2H ID=%d, len=%d\n", c2h_cmd_id, c2h_cmd_len);
		
		find_c2h_cmd = false;
		return find_c2h_cmd;
	}
	
	switch (c2h_cmd_id) {
	case PHYDM_C2H_DBG:
		phydm_fw_trace_handler(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_RPT:
		phydm_c2h_ra_report_handler(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_PARA_RPT:
		odm_c2h_ra_para_report_handler(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_DYNAMIC_TX_PATH_RPT:
		if (p_dm->support_ic_type & (ODM_RTL8814A))
			phydm_c2h_dtp_handler(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_IQK_FINISH:
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

		if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821)) {

			RT_TRACE(COMP_MP, DBG_LOUD, ("== FW IQK Finish ==\n"));
			odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
			p_dm->rf_calibrate_info.is_iqk_in_progress = false;
			odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);
			p_dm->rf_calibrate_info.iqk_progressing_time = 0;
			p_dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(p_dm, p_dm->rf_calibrate_info.iqk_start_time);
		}

#endif
		break;

	case PHYDM_C2H_CLM_MONITOR:
		phydm_c2h_clm_report_handler(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_DBG_CODE:
		phydm_fw_trace_handler_code(p_dm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_EXTEND:
		extend_c2h_sub_id = tmp_buf[0];
		if (extend_c2h_sub_id == PHYDM_EXTEND_C2H_DBG_PRINT)
			phydm_fw_trace_handler_8051(p_dm, tmp_buf, c2h_cmd_len);

		break;

	default:
		find_c2h_cmd = false;
		break;
	}

	return find_c2h_cmd;

}

u64
odm_get_current_time(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return (u64)rtw_get_current_time();
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return jiffies;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_current_time();
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return  PlatformGetCurrentTime();
#endif
}

u64
odm_get_progressing_time(
	struct PHY_DM_STRUCT		*p_dm,
	u64			start_time
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return rtw_get_passing_time_ms((u32)start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	return jiffies_to_msecs(jiffies - start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_passing_time_ms((systime)start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return ((PlatformGetCurrentTime() - start_time) >> 10);
#endif
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE)) && !defined(DM_ODM_CE_MAC80211)

void
phydm_set_hw_reg_handler_interface (
	struct PHY_DM_STRUCT		*p_dm,
	u8				RegName,
	u8				*val
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	struct _ADAPTER *adapter = p_dm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	adapter->HalFunc.SetHwRegHandler(adapter, RegName, val);
#else
	adapter->hal_func.set_hw_reg_handler(adapter, RegName, val);
#endif

#endif

}

void
phydm_get_hal_def_var_handler_interface (
	struct PHY_DM_STRUCT		*p_dm,
	enum _HAL_DEF_VARIABLE		e_variable,
	void						*p_value
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	struct _ADAPTER *adapter = p_dm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	adapter->HalFunc.GetHalDefVarHandler(adapter, e_variable, p_value);
#else
	adapter->hal_func.get_hal_def_var_handler(adapter, e_variable, p_value);
#endif

#endif
}

#endif

void
odm_set_tx_power_index_by_rate_section (
	struct PHY_DM_STRUCT	*p_dm,
	enum rf_path		path,
	u8				Channel,
	u8				RateSection
	)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	PHY_SetTxPowerIndexByRateSection(adapter, path, Channel, RateSection);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void *adapter = p_dm->adapter;

	phy_set_tx_power_index_by_rs(adapter, Channel, path, RateSection);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_tx_power_index_by_rate_section(p_dm->adapter, path, Channel, RateSection);
#endif
}


u8
odm_get_tx_power_index (
	struct PHY_DM_STRUCT	*p_dm,
	enum rf_path		path,
	u8				tx_rate,
	u8				band_width,
	u8				Channel
	)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;

	return PHY_GetTxPowerIndex(p_dm->adapter, path, tx_rate, (CHANNEL_WIDTH)band_width, Channel);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void		*adapter = p_dm->adapter;

	return phy_get_tx_power_index(adapter, (enum rf_path)path, tx_rate, band_width, Channel);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return phy_get_tx_power_index(p_dm->adapter, path, tx_rate, band_width, Channel);
#endif
}



u8
odm_efuse_one_byte_read(
	struct PHY_DM_STRUCT	*p_dm,
	u16			addr,
	u8			*data,
	boolean		b_pseu_do_test
	)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct	_ADAPTER	*adapter = p_dm->adapter;

	return (u8)EFUSE_OneByteRead(adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void		*adapter = p_dm->adapter;

	return rtl_efuse_onebyte_read(adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return efuse_onebyte_read(p_dm->adapter, addr, data, b_pseu_do_test);
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	/*ReadEFuseByte(p_dm->priv, addr, data);*/
	/*return true;*/
#endif
}



void
odm_efuse_logical_map_read(
	struct	PHY_DM_STRUCT	*p_dm,
	u8	type,
	u16	offset,
	u32	*data
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct	_ADAPTER *adapter = p_dm->adapter;

	EFUSE_ShadowRead(adapter, type, offset, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	void		*adapter = p_dm->adapter;

	rtl_efuse_logical_map_read(adapter, type, offset, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	efuse_logical_map_read(p_dm->adapter, type, offset, data);
#endif
}

enum hal_status
odm_iq_calibrate_by_fw(
	struct PHY_DM_STRUCT	*p_dm,
	u8 clear,
	u8 segment
	)
{
	enum hal_status iqk_result = HAL_STATUS_FAILURE;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct	_ADAPTER	*adapter = p_dm->adapter;

	if (HAL_MAC_FWIQK_Trigger(&GET_HAL_MAC_INFO(adapter), clear, segment) == 0)
		iqk_result = HAL_STATUS_SUCCESS;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	iqk_result = rtw_phydm_fw_iqk(p_dm, clear, segment);
#endif
	return iqk_result;
}

void
odm_cmn_info_ptr_array_hook(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_cmninfo_e	cmn_info,
	u16			index,
	void			*p_value
)
{
	switch	(cmn_info) {
	/*Dynamic call by reference pointer.	*/
	case	ODM_CMNINFO_STA_STATUS:
		p_dm->p_odm_sta_info[index] = (struct sta_info *)p_value;
		break;
	/* To remove the compiler warning, must add an empty default statement to handle the other values. */
	default:
		/* do nothing */
		break;
	}

}

void
phydm_cmn_sta_info_hook(
	struct PHY_DM_STRUCT		*p_dm,
	u8			mac_id,
	struct cmn_sta_info *pcmn_sta_info
)
{
	p_dm->p_phydm_sta_info[mac_id] = pcmn_sta_info;

	if (is_sta_active(pcmn_sta_info))
		p_dm->phydm_macid_table[pcmn_sta_info->mac_id] = mac_id;
}

void
phydm_add_interrupt_mask_handler(
	struct PHY_DM_STRUCT		*p_dm,
	u8							interrupt_type
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	struct rtl8192cd_priv	*priv = p_dm->priv;

	#if IS_EXIST_PCI || IS_EXIST_EMBEDDED
	GET_HAL_INTERFACE(priv)->AddInterruptMaskHandler(priv, interrupt_type);
	#endif
	
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#endif
}

void
phydm_enable_rx_related_interrupt_handler(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	struct rtl8192cd_priv	*priv = p_dm->priv;

	#if IS_EXIST_PCI || IS_EXIST_EMBEDDED
	GET_HAL_INTERFACE(priv)->EnableRxRelatedInterruptHandler(priv);
	#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#endif
}

boolean
phydm_get_txbf_en(
	struct PHY_DM_STRUCT		*p_dm,
	u16							mac_id,
	u8							i
)
{
	boolean txbf_en = false;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && !defined(DM_ODM_CE_MAC80211)

	#ifdef CONFIG_BEAMFORMING
	enum beamforming_cap beamform_cap;
	struct _ADAPTER *adapter = p_dm->adapter;
	#if (BEAMFORMING_SUPPORT == 1)
	beamform_cap =
	phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm, mac_id);
	#else/*for drv beamforming*/
	beamform_cap =
	beamforming_get_entry_beam_cap_by_mac_id(&adapter->mlmepriv, mac_id);
	#endif
	if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
		txbf_en = true;
	else
		txbf_en = false;
	#endif /*#ifdef CONFIG_BEAMFORMING*/

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

	#if (BEAMFORMING_SUPPORT == 1)
	u8 idx = 0xff;
	boolean act_bfer = false;
	BEAMFORMING_CAP beamform_cap = BEAMFORMING_CAP_NONE;
	PRT_BEAMFORMING_ENTRY	p_entry = NULL;
	struct rtl8192cd_priv *priv			= p_dm->priv;
	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	struct _BF_DIV_COEX_	*p_dm_bdc_table = &p_dm->dm_bdc_table;

	p_dm_bdc_table->num_txbfee_client = 0;
	p_dm_bdc_table->num_txbfer_client = 0;
	#endif
	#endif

	#if (BEAMFORMING_SUPPORT == 1)
	beamform_cap = Beamforming_GetEntryBeamCapByMacId(priv, mac_id);
	p_entry = Beamforming_GetEntryByMacId(priv, mac_id, &idx);
	if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU)) {
		if (p_entry->Sounding_En)
			txbf_en = true;
		else
			txbf_en = false;
		act_bfer = true;
	}
	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY)) /*BDC*/
	if (act_bfer == true) {
		p_dm_bdc_table->w_bfee_client[i] = true; /* AP act as BFer */
		p_dm_bdc_table->num_txbfee_client++;
	} else
		p_dm_bdc_table->w_bfee_client[i] = false; /* AP act as BFer */
	
	if (beamform_cap & (BEAMFORMEE_CAP_HT_EXPLICIT | BEAMFORMEE_CAP_VHT_SU)) {
		p_dm_bdc_table->w_bfer_client[i] = true; /* AP act as BFee */
		p_dm_bdc_table->num_txbfer_client++;
	} else
		p_dm_bdc_table->w_bfer_client[i] = false; /* AP act as BFer */

	#endif
	#endif

#endif
	return txbf_en;

}

void
phydm_iqk_wait(
	struct PHY_DM_STRUCT		*p_dm,
	u32		timeout
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct _ADAPTER		*p_adapter = p_dm->adapter;

	rtl8812_iqk_wait(p_adapter, timeout);
#endif
}
