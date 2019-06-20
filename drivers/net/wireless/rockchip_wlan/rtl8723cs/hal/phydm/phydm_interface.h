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

#ifndef __ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

#define INTERFACE_VERSION "1.2"

#define pdm_set_reg odm_set_bb_reg

/*@=========== Constant/Structure/Enum/... Define*/

enum phydm_h2c_cmd {
	PHYDM_H2C_RA_MASK		= 0x40,
	PHYDM_H2C_TXBF			= 0x41,
	ODM_H2C_RSSI_REPORT		= 0x42,
	ODM_H2C_IQ_CALIBRATION		= 0x45,
	PHYDM_RA_MASK_ABOVE_3SS		= 0x46,
	ODM_H2C_RA_PARA_ADJUST		= 0x47,
	PHYDM_H2C_DYNAMIC_TX_PATH	= 0x48,
	PHYDM_H2C_FW_TRACE_EN		= 0x49,
	ODM_H2C_WIFI_CALIBRATION	= 0x6d,
	PHYDM_H2C_MU			= 0x4a,
	PHYDM_H2C_FW_GENERAL_INIT	= 0x4c,
	PHYDM_H2C_FW_CLM_MNTR		= 0x4d,
	PHYDM_H2C_MCC			= 0x4f,
	PHYDM_H2C_RESP_TX_PATH_CTRL	= 0x50,
	PHYDM_H2C_RESP_TX_ANT_CTRL	= 0x51,
	ODM_MAX_H2CCMD
};

enum phydm_c2h_evt {
	PHYDM_C2H_DBG =		0,
	PHYDM_C2H_LB =		1,
	PHYDM_C2H_XBF =		2,
	PHYDM_C2H_TX_REPORT =	3,
	PHYDM_C2H_INFO =	9,
	PHYDM_C2H_BT_MP =	11,
	PHYDM_C2H_RA_RPT =	12,
	PHYDM_C2H_RA_PARA_RPT = 14,
	PHYDM_C2H_DYNAMIC_TX_PATH_RPT = 15,
	PHYDM_C2H_IQK_FINISH =	17, /*@0x11*/
	PHYDM_C2H_CLM_MONITOR =	0x2a,
	PHYDM_C2H_DBG_CODE =	0xFE,
	PHYDM_C2H_EXTEND =	0xFF,
};

enum phydm_extend_c2h_evt {
	PHYDM_EXTEND_C2H_DBG_PRINT = 0

};

enum phydm_halmac_param {
	PHYDM_HALMAC_CMD_MAC_W8 = 0,
	PHYDM_HALMAC_CMD_MAC_W16 = 1,
	PHYDM_HALMAC_CMD_MAC_W32 = 2,
	PHYDM_HALMAC_CMD_BB_W8,
	PHYDM_HALMAC_CMD_BB_W16,
	PHYDM_HALMAC_CMD_BB_W32,
	PHYDM_HALMAC_CMD_RF_W,
	PHYDM_HALMAC_CMD_DELAY_US,
	PHYDM_HALMAC_CMD_DELAY_MS,
	PHYDM_HALMAC_CMD_END = 0XFF,
};

/*@=========== Macro Define*/

#define _reg_all(_name)			ODM_##_name
#define _reg_ic(_name, _ic)		ODM_##_name##_ic
#define _bit_all(_name)			BIT_##_name
#define _bit_ic(_name, _ic)		BIT_##_name##_ic

/* @_cat: implemented by Token-Pasting Operator. */
#if 0
#define _cat(_name, _ic_type, _func) \
	(                            \
		_func##_all(_name))
#endif

#if 0

#define ODM_REG_DIG_11N		0xC50
#define ODM_REG_DIG_11AC	0xDDD

ODM_REG(DIG,_pdm_odm)
#endif

#if defined(DM_ODM_CE_MAC80211)
#define ODM_BIT(name, dm)				\
	((dm->support_ic_type & ODM_IC_11N_SERIES) ?	\
	 ODM_BIT_##name##_11N : ODM_BIT_##name##_11AC)

#define ODM_REG(name, dm)				\
	((dm->support_ic_type & ODM_IC_11N_SERIES) ?	\
	 ODM_REG_##name##_11N : ODM_REG_##name##_11AC)
#else
#define _reg_11N(_name)			ODM_REG_##_name##_11N
#define _reg_11AC(_name)		ODM_REG_##_name##_11AC
#define _bit_11N(_name)			ODM_BIT_##_name##_11N
#define _bit_11AC(_name)		ODM_BIT_##_name##_11AC

#ifdef __ECOS
#define _rtk_cat(_name, _ic_type, _func)                                \
	(                                                               \
		((_ic_type) & ODM_IC_11N_SERIES) ? _func##_11N(_name) : \
						   _func##_11AC(_name))
#else

#define _cat(_name, _ic_type, _func)                                    \
	(                                                               \
		((_ic_type) & ODM_IC_11N_SERIES) ? _func##_11N(_name) : \
						   _func##_11AC(_name))
#endif
/*@
 * only sample code
 *#define _cat(_name, _ic_type, _func)					\
 *	(								\
 *		((_ic_type) & ODM_RTL8188E) ? _func##_ic(_name, _8188E) :\
 *		_func##_ic(_name, _8195)				\
 *	)
 */

/* @_name: name of register or bit.
 * Example: "ODM_REG(R_A_AGC_CORE1, dm)"
 * gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C",
 * depends on support_ic_type.
 */
#ifdef __ECOS
	#define ODM_REG(_name, _pdm_odm)	\
		_rtk_cat(_name, _pdm_odm->support_ic_type, _reg)
	#define ODM_BIT(_name, _pdm_odm)	\
		_rtk_cat(_name, _pdm_odm->support_ic_type, _bit)
#else
	#define ODM_REG(_name, _pdm_odm)	\
		_cat(_name, _pdm_odm->support_ic_type, _reg)
	#define ODM_BIT(_name, _pdm_odm)	\
		_cat(_name, _pdm_odm->support_ic_type, _bit)
#endif

#endif
/*@
 * =========== Extern Variable ??? It should be forbidden.
 */

/*@
 * =========== EXtern Function Prototype
 */

u8 odm_read_1byte(struct dm_struct *dm, u32 reg_addr);

u16 odm_read_2byte(struct dm_struct *dm, u32 reg_addr);

u32 odm_read_4byte(struct dm_struct *dm, u32 reg_addr);

void odm_write_1byte(struct dm_struct *dm, u32 reg_addr, u8 data);

void odm_write_2byte(struct dm_struct *dm, u32 reg_addr, u16 data);

void odm_write_4byte(struct dm_struct *dm, u32 reg_addr, u32 data);

void odm_set_mac_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask,
		     u32 data);

u32 odm_get_mac_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask);

void odm_set_bb_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask, u32 data);

u32 odm_get_bb_reg(struct dm_struct *dm, u32 reg_addr, u32 bit_mask);

void odm_set_rf_reg(struct dm_struct *dm, u8 e_rf_path, u32 reg_addr,
		    u32 bit_mask, u32 data);

u32 odm_get_rf_reg(struct dm_struct *dm, u8 e_rf_path, u32 reg_addr,
		   u32 bit_mask);

/*@
 * Memory Relative Function.
 */
void odm_allocate_memory(struct dm_struct *dm, void **ptr, u32 length);
void odm_free_memory(struct dm_struct *dm, void *ptr, u32 length);

void odm_move_memory(struct dm_struct *dm, void *dest, void *src, u32 length);

s32 odm_compare_memory(struct dm_struct *dm, void *buf1, void *buf2,
		       u32 length);

void odm_memory_set(struct dm_struct *dm, void *pbuf, s8 value, u32 length);

/*@
 * ODM MISC-spin lock relative API.
 */
void odm_acquire_spin_lock(struct dm_struct *dm, enum rt_spinlock_type type);

void odm_release_spin_lock(struct dm_struct *dm, enum rt_spinlock_type type);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/*@
 * ODM MISC-workitem relative API.
 */
void odm_initialize_work_item(
	struct dm_struct *dm,
	PRT_WORK_ITEM p_rt_work_item,
	RT_WORKITEM_CALL_BACK rt_work_item_callback,
	void *context,
	const char *sz_id);

void odm_start_work_item(
	PRT_WORK_ITEM p_rt_work_item);

void odm_stop_work_item(
	PRT_WORK_ITEM p_rt_work_item);

void odm_free_work_item(
	PRT_WORK_ITEM p_rt_work_item);

void odm_schedule_work_item(
	PRT_WORK_ITEM p_rt_work_item);

boolean
odm_is_work_item_scheduled(
	PRT_WORK_ITEM p_rt_work_item);
#endif

/*@
 * ODM Timer relative API.
 */
void ODM_delay_ms(u32 ms);

void ODM_delay_us(u32 us);

void ODM_sleep_ms(u32 ms);

void ODM_sleep_us(u32 us);

void odm_set_timer(struct dm_struct *dm, struct phydm_timer_list *timer,
		   u32 ms_delay);

void odm_initialize_timer(struct dm_struct *dm, struct phydm_timer_list *timer,
			  void *call_back_func, void *context,
			  const char *sz_id);

void odm_cancel_timer(struct dm_struct *dm, struct phydm_timer_list *timer);

void odm_release_timer(struct dm_struct *dm, struct phydm_timer_list *timer);

/*ODM FW relative API.*/

enum hal_status
phydm_set_reg_by_fw(struct dm_struct *dm, enum phydm_halmac_param config_type,
		    u32 offset, u32 data, u32 mask, enum rf_path e_rf_path,
		    u32 delay_time);

void odm_fill_h2c_cmd(struct dm_struct *dm, u8 element_id, u32 cmd_len,
		      u8 *cmd_buffer);

u8 phydm_c2H_content_parsing(void *dm_void, u8 c2h_cmd_id, u8 c2h_cmd_len,
			     u8 *tmp_buf);

u64 odm_get_current_time(struct dm_struct *dm);
u64 odm_get_progressing_time(struct dm_struct *dm, u64 start_time);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE)) && \
	(!defined(DM_ODM_CE_MAC80211) && !defined(DM_ODM_CE_MAC80211_V2))

void phydm_set_hw_reg_handler_interface(struct dm_struct *dm, u8 reg_Name,
					u8 *val);

void phydm_get_hal_def_var_handler_interface(struct dm_struct *dm,
					     enum _HAL_DEF_VARIABLE e_variable,
					     void *value);

#endif

void odm_set_tx_power_index_by_rate_section(struct dm_struct *dm,
					    enum rf_path path, u8 channel,
					    u8 rate_section);

u8 odm_get_tx_power_index(struct dm_struct *dm, enum rf_path path, u8 tx_rate,
			  u8 band_width, u8 channel);

u8 odm_efuse_one_byte_read(struct dm_struct *dm, u16 addr, u8 *data,
			   boolean b_pseu_do_test);

void odm_efuse_logical_map_read(struct dm_struct *dm, u8 type, u16 offset,
				u32 *data);

enum hal_status
odm_iq_calibrate_by_fw(struct dm_struct *dm, u8 clear, u8 segment);

void odm_cmn_info_ptr_array_hook(struct dm_struct *dm,
				 enum odm_cmninfo cmn_info, u16 index,
				 void *value);

void phydm_cmn_sta_info_hook(struct dm_struct *dm, u8 index,
			     struct cmn_sta_info *pcmn_sta_info);

void phydm_macid2sta_idx_table(struct dm_struct *dm, u8 entry_idx,
			       struct cmn_sta_info *pcmn_sta_info);

void phydm_add_interrupt_mask_handler(struct dm_struct *dm, u8 interrupt_type);

void phydm_enable_rx_related_interrupt_handler(struct dm_struct *dm);

#if 0
boolean
phydm_get_txbf_en(
	struct dm_struct		*dm,
	u16		mac_id,
	u8		i
);
#endif

void phydm_iqk_wait(struct dm_struct *dm, u32 timeout);
u8 phydm_get_hwrate_to_mrate(struct dm_struct *dm, u8 rate);

void phydm_set_crystalcap(struct dm_struct *dm, u8 crystal_cap);
void phydm_run_in_thread_cmd(struct dm_struct *dm, void (*func)(void *),
			     void *context);
u8 phydm_get_tx_rate(struct dm_struct *dm);
u8 phydm_get_tx_power_dbm(struct dm_struct *dm, u8 rf_path,
					u8 rate, u8 bandwidth, u8 channel);
u64 phydm_division64(u64 x, u64 y);

#endif /* @__ODM_INTERFACE_H__ */
