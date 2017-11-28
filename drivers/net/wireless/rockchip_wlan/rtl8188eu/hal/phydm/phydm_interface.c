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
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	return	RTL_R8(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return rtw_read8(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return	PlatformEFIORead1Byte(adapter, reg_addr);
#endif

}


u16
odm_read_2byte(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	return	RTL_R16(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return rtw_read16(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return	PlatformEFIORead2Byte(adapter, reg_addr);
#endif

}


u32
odm_read_4byte(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	return	RTL_R32(reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return rtw_read32(adapter, reg_addr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return	PlatformEFIORead4Byte(adapter, reg_addr);
#endif

}


void
odm_write_1byte(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr,
	u8			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	RTL_W8(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	rtw_write8(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformEFIOWrite1Byte(adapter, reg_addr, data);
#endif

}


void
odm_write_2byte(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr,
	u16			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	RTL_W16(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	rtw_write16(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformEFIOWrite2Byte(adapter, reg_addr, data);
#endif

}


void
odm_write_4byte(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u32			reg_addr,
	u32			data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv	= p_dm_odm->priv;
	RTL_W32(reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	rtw_write32(adapter, reg_addr, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformEFIOWrite4Byte(adapter, reg_addr, data);
#endif

}


void
odm_set_mac_reg(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		reg_addr,
	u32		bit_mask,
	u32		data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(p_dm_odm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(p_dm_odm->adapter, reg_addr, bit_mask, data);
#endif
}


u32
odm_get_mac_reg(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		reg_addr,
	u32		bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(p_dm_odm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return PHY_QueryMacReg(p_dm_odm->adapter, reg_addr, bit_mask);
#else
	return phy_query_mac_reg(p_dm_odm->adapter, reg_addr, bit_mask);
#endif
}


void
odm_set_bb_reg(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		reg_addr,
	u32		bit_mask,
	u32		data
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	phy_set_bb_reg(p_dm_odm->priv, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PHY_SetBBReg(adapter, reg_addr, bit_mask, data);
#else
	phy_set_bb_reg(p_dm_odm->adapter, reg_addr, bit_mask, data);
#endif
}


u32
odm_get_bb_reg(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		reg_addr,
	u32		bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_bb_reg(p_dm_odm->priv, reg_addr, bit_mask);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return PHY_QueryBBReg(adapter, reg_addr, bit_mask);
#else
	return phy_query_bb_reg(p_dm_odm->adapter, reg_addr, bit_mask);
#endif
}


void
odm_set_rf_reg(
	struct PHY_DM_STRUCT			*p_dm_odm,
	enum odm_rf_radio_path_e	e_rf_path,
	u32				reg_addr,
	u32				bit_mask,
	u32				data
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	phy_set_rf_reg(p_dm_odm->priv, e_rf_path, reg_addr, bit_mask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PHY_SetRFReg(adapter, e_rf_path, reg_addr, bit_mask, data);
	ODM_delay_us(2);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_rf_reg(p_dm_odm->adapter, e_rf_path, reg_addr, bit_mask, data);
#endif
}

u32
odm_get_rf_reg(
	struct PHY_DM_STRUCT			*p_dm_odm,
	enum odm_rf_radio_path_e	e_rf_path,
	u32				reg_addr,
	u32				bit_mask
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return phy_query_rf_reg(p_dm_odm->priv, e_rf_path, reg_addr, bit_mask, 1);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	return PHY_QueryRFReg(adapter, e_rf_path, reg_addr, bit_mask);
#else
	return phy_query_rf_reg(p_dm_odm->adapter, e_rf_path, reg_addr, bit_mask);
#endif
}




/*
 * ODM Memory relative API.
 *   */
void
odm_allocate_memory(
	struct PHY_DM_STRUCT	*p_dm_odm,
	void **p_ptr,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	*p_ptr = kmalloc(length, GFP_ATOMIC);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	*p_ptr = rtw_zvmalloc(length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformAllocateMemory(adapter, p_ptr, length);
#endif
}

/* length could be ignored, used to detect memory leakage. */
void
odm_free_memory(
	struct PHY_DM_STRUCT	*p_dm_odm,
	void		*p_ptr,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	kfree(p_ptr);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_vmfree(p_ptr, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	/* struct _ADAPTER*    adapter = p_dm_odm->adapter; */
	PlatformFreeMemory(p_ptr, length);
#endif
}

void
odm_move_memory(
	struct PHY_DM_STRUCT	*p_dm_odm,
	void		*p_dest,
	void		*p_src,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memcpy(p_dest, p_src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memcpy(p_dest, p_src, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformMoveMemory(p_dest, p_src, length);
#endif
}

void odm_memory_set(
	struct PHY_DM_STRUCT	*p_dm_odm,
	void		*pbuf,
	s8		value,
	u32		length
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_rtw_memset(pbuf, value, length);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformFillMemory(pbuf, length, value);
#endif
}
s32 odm_compare_memory(
	struct PHY_DM_STRUCT		*p_dm_odm,
	void           *p_buf1,
	void           *p_buf2,
	u32          length
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
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
	struct PHY_DM_STRUCT			*p_dm_odm,
	enum rt_spinlock_type	type
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	rtw_odm_acquirespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformAcquireSpinLock(adapter, type);
#endif
}
void
odm_release_spin_lock(
	struct PHY_DM_STRUCT			*p_dm_odm,
	enum rt_spinlock_type	type
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	rtw_odm_releasespinlock(adapter, type);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformReleaseSpinLock(adapter, type);
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/*
 * Work item relative API. FOr MP driver only~!
 *   */
void
odm_initialize_work_item(
	struct PHY_DM_STRUCT					*p_dm_odm,
	PRT_WORK_ITEM				p_rt_work_item,
	RT_WORKITEM_CALL_BACK		rt_work_item_callback,
	void						*p_context,
	const char					*sz_id
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
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


bool
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
odm_stall_execution(
	u32	us_delay
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_udelay_os(us_delay);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PlatformStallExecution(us_delay);
#endif
}

void
ODM_delay_ms(u32	ms)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	delay_ms(ms);
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

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_msleep_os(ms);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#endif
}

void
ODM_sleep_us(u32	us)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_usleep_os(us);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#endif
}

void
odm_set_timer(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct timer_list		*p_timer,
	u32			ms_delay
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	mod_timer(p_timer, jiffies + RTL_MILISECONDS_TO_JIFFIES(ms_delay));
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_set_timer(p_timer, ms_delay); /* ms */
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	PlatformSetTimer(adapter, p_timer, ms_delay);
#endif

}

void
odm_initialize_timer(
	struct PHY_DM_STRUCT			*p_dm_odm,
	struct timer_list			*p_timer,
	void	*call_back_func,
	void				*p_context,
	const char			*sz_id
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	init_timer(p_timer);
	p_timer->function = call_back_func;
	p_timer->data = (unsigned long)p_dm_odm;
	/*mod_timer(p_timer, jiffies+RTL_MILISECONDS_TO_JIFFIES(10));	*/
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	_init_timer(p_timer, adapter->pnetdev, call_back_func, p_dm_odm);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	PlatformInitializeTimer(adapter, p_timer, call_back_func, p_context, sz_id);
#endif
}


void
odm_cancel_timer(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct timer_list		*p_timer
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	del_timer(p_timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	_cancel_timer_ex(p_timer);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	PlatformCancelTimer(adapter, p_timer);
#endif
}


void
odm_release_timer(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct timer_list		*p_timer
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)

	struct _ADAPTER *adapter = p_dm_odm->adapter;

	/* <20120301, Kordan> If the initilization fails, InitializeAdapterXxx will return regardless of InitHalDm.
	 * Hence, uninitialized timers cause BSOD when the driver releases resources since the init fail. */
	if (p_timer == 0) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_SERIOUS, ("=====>odm_release_timer(), The timer is NULL! Please check it!\n"));
		return;
	}

	PlatformReleaseTimer(adapter, p_timer);
#endif
}


u8
phydm_trans_h2c_id(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		phydm_h2c_id
)
{
	u8 platform_h2c_id = phydm_h2c_id;

	switch (phydm_h2c_id) {
	/* 1 [0] */
	case ODM_H2C_RSSI_REPORT:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm_odm->support_ic_type == ODM_RTL8188E)
			platform_h2c_id = H2C_88E_RSSI_REPORT;
		else if (p_dm_odm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_8814A_RSSI_REPORT;
		else
			platform_h2c_id = H2C_RSSI_REPORT;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
		platform_h2c_id = H2C_RSSI_SETTING;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
		if (p_dm_odm->support_ic_type == ODM_RTL8881A || p_dm_odm->support_ic_type == ODM_RTL8192E || p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RSSI_REPORT;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8812)
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
		if (p_dm_odm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))
			platform_h2c_id = H2C_8814A_RA_PARA_ADJUST;
		else
			platform_h2c_id = H2C_RA_PARA_ADJUST;
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
		if (p_dm_odm->support_ic_type == ODM_RTL8881A || p_dm_odm->support_ic_type == ODM_RTL8192E || p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id = H2C_88XX_RA_PARA_ADJUST;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8812)
				platform_h2c_id = H2C_8812_RA_PARA_ADJUST;
			else
#endif
			{}
#endif

		break;


	/* 1 [6] */
	case PHYDM_H2C_DYNAMIC_TX_PATH:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm_odm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_8814A_DYNAMIC_TX_PATH;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#if (RTL8814A_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_DYNAMIC_TX_PATH;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (RTL8814A_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8814A)
			platform_h2c_id = H2C_88XX_DYNAMIC_TX_PATH;
#endif

#endif

		break;

	/* [7]*/
	case PHYDM_H2C_FW_TRACE_EN:

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (p_dm_odm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))
			platform_h2c_id = H2C_8814A_FW_TRACE_EN;
		else
			platform_h2c_id = H2C_FW_TRACE_EN;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

		platform_h2c_id = 0x49;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
		if (p_dm_odm->support_ic_type == ODM_RTL8881A || p_dm_odm->support_ic_type == ODM_RTL8192E || p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES)
			platform_h2c_id  = H2C_88XX_FW_TRACE_EN;
		else
#endif
#if (RTL8812A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8812)
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
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			phydm_h2c_id,
	u32			cmd_len,
	u8			*p_cmd_buffer
)
{
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
	u8		platform_h2c_id;

	platform_h2c_id = phydm_trans_h2c_id(p_dm_odm, phydm_h2c_id);

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C]  platform_h2c_id = ((0x%x))\n", platform_h2c_id));

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (p_dm_odm->support_ic_type == ODM_RTL8188E)	{
		if (!p_dm_odm->ra_support88e)
			FillH2CCmd88E(adapter, platform_h2c_id, cmd_len, p_cmd_buffer);
	} else if (p_dm_odm->support_ic_type == ODM_RTL8814A)
		FillH2CCmd8814A(adapter, platform_h2c_id, cmd_len, p_cmd_buffer);
	else if (p_dm_odm->support_ic_type == ODM_RTL8822B)
#if (RTL8822B_SUPPORT == 1)
		FillH2CCmd8822B(adapter, platform_h2c_id, cmd_len, p_cmd_buffer);
#endif
	else
		FillH2CCmd(adapter, platform_h2c_id, cmd_len, p_cmd_buffer);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	rtw_hal_fill_h2c_cmd(adapter, platform_h2c_id, cmd_len, p_cmd_buffer);

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8881A_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
	if (p_dm_odm->support_ic_type == ODM_RTL8881A || p_dm_odm->support_ic_type == ODM_RTL8192E || p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES)
		GET_HAL_INTERFACE(p_dm_odm->priv)->fill_h2c_cmd_handler(p_dm_odm->priv, platform_h2c_id, cmd_len, p_cmd_buffer);
	else
#endif
#if (RTL8812A_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8812)
			fill_h2c_cmd8812(p_dm_odm->priv, platform_h2c_id, cmd_len, p_cmd_buffer);
		else
#endif
		{}
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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
#endif
	u8	extend_c2h_sub_id = 0;
	u8	find_c2h_cmd = true;

	switch (c2h_cmd_id) {
	case PHYDM_C2H_DBG:
		phydm_fw_trace_handler(p_dm_odm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_RPT:
		phydm_c2h_ra_report_handler(p_dm_odm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_RA_PARA_RPT:
		odm_c2h_ra_para_report_handler(p_dm_odm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_DYNAMIC_TX_PATH_RPT:
		if (p_dm_odm->support_ic_type & (ODM_RTL8814A))
			phydm_c2h_dtp_handler(p_dm_odm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_IQK_FINISH:
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

		if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821)) {

			RT_TRACE(COMP_MP, DBG_LOUD, ("== FW IQK Finish ==\n"));
			odm_acquire_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);
			p_dm_odm->rf_calibrate_info.is_iqk_in_progress = false;
			odm_release_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);
			p_dm_odm->rf_calibrate_info.iqk_progressing_time = 0;
			p_dm_odm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(p_dm_odm, p_dm_odm->rf_calibrate_info.iqk_start_time);
		}

#endif
		break;

	case PHYDM_C2H_DBG_CODE:
		phydm_fw_trace_handler_code(p_dm_odm, tmp_buf, c2h_cmd_len);
		break;

	case PHYDM_C2H_EXTEND:
		extend_c2h_sub_id = tmp_buf[0];
		if (extend_c2h_sub_id == PHYDM_EXTEND_C2H_DBG_PRINT)
			phydm_fw_trace_handler_8051(p_dm_odm, tmp_buf, c2h_cmd_len);

		break;

	default:
		find_c2h_cmd = false;
		break;
	}

	return find_c2h_cmd;

}

u64
odm_get_current_time(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return  0;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return (u64)rtw_get_current_time();
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return  PlatformGetCurrentTime();
#endif
}

u64
odm_get_progressing_time(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u64			start_time
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	return  0;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	return rtw_get_passing_time_ms((u32)start_time);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	return ((PlatformGetCurrentTime() - start_time) >> 10);
#endif
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

void
phydm_set_hw_reg_handler_interface (
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8				RegName,
	u8				*val
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	struct _ADAPTER *adapter = p_dm_odm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	adapter->HalFunc.SetHwRegHandler(adapter, RegName, val);
#else
	adapter->hal_func.set_hw_reg_handler(adapter, RegName, val);
#endif

#endif

}

void
phydm_get_hal_def_var_handler_interface (
	struct PHY_DM_STRUCT		*p_dm_odm,
	enum _HAL_DEF_VARIABLE		e_variable,
	void						*p_value
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	struct _ADAPTER *adapter = p_dm_odm->adapter;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	adapter->HalFunc.GetHalDefVarHandler(adapter, e_variable, p_value);
#else
	adapter->hal_func.get_hal_def_var_handler(adapter, e_variable, p_value);
#endif

#endif
}

#endif