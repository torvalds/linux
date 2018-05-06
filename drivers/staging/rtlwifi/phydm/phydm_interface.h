/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __ODM_INTERFACE_H__
#define __ODM_INTERFACE_H__

#define INTERFACE_VERSION "1.1" /*2015.07.29  YuChen*/

/*
 * =========== Constant/Structure/Enum/... Define
 */

/*
 * =========== Macro Define
 */

#define _reg_all(_name) ODM_##_name
#define _reg_ic(_name, _ic) ODM_##_name##_ic
#define _bit_all(_name) BIT_##_name
#define _bit_ic(_name, _ic) BIT_##_name##_ic

/* _cat: implemented by Token-Pasting Operator. */

/*===================================
 *
 * #define ODM_REG_DIG_11N	0xC50
 * #define ODM_REG_DIG_11AC	0xDDD
 *
 * ODM_REG(DIG,_pdm_odm)
 * ===================================
 */

#define _reg_11N(_name) ODM_REG_##_name##_11N
#define _reg_11AC(_name) ODM_REG_##_name##_11AC
#define _bit_11N(_name) ODM_BIT_##_name##_11N
#define _bit_11AC(_name) ODM_BIT_##_name##_11AC

#define _cat(_name, _ic_type, _func)                                           \
	(((_ic_type) & ODM_IC_11N_SERIES) ? _func##_11N(_name) :               \
					    _func##_11AC(_name))

/* _name: name of register or bit.
 * Example: "ODM_REG(R_A_AGC_CORE1, dm)"
 * gets "ODM_R_A_AGC_CORE1" or "ODM_R_A_AGC_CORE1_8192C",
 * depends on support_ic_type.
 */
#define ODM_REG(_name, _pdm_odm) _cat(_name, _pdm_odm->support_ic_type, _reg)
#define ODM_BIT(_name, _pdm_odm) _cat(_name, _pdm_odm->support_ic_type, _bit)
enum phydm_h2c_cmd {
	PHYDM_H2C_TXBF = 0x41,
	ODM_H2C_RSSI_REPORT = 0x42,
	ODM_H2C_IQ_CALIBRATION = 0x45,
	ODM_H2C_RA_PARA_ADJUST = 0x47,
	PHYDM_H2C_DYNAMIC_TX_PATH = 0x48,
	PHYDM_H2C_FW_TRACE_EN = 0x49,
	ODM_H2C_WIFI_CALIBRATION = 0x6d,
	PHYDM_H2C_MU = 0x4a,
	ODM_MAX_H2CCMD
};

enum phydm_c2h_evt {
	PHYDM_C2H_DBG = 0,
	PHYDM_C2H_LB = 1,
	PHYDM_C2H_XBF = 2,
	PHYDM_C2H_TX_REPORT = 3,
	PHYDM_C2H_INFO = 9,
	PHYDM_C2H_BT_MP = 11,
	PHYDM_C2H_RA_RPT = 12,
	PHYDM_C2H_RA_PARA_RPT = 14,
	PHYDM_C2H_DYNAMIC_TX_PATH_RPT = 15,
	PHYDM_C2H_IQK_FINISH = 17, /*0x11*/
	PHYDM_C2H_DBG_CODE = 0xFE,
	PHYDM_C2H_EXTEND = 0xFF,
};

enum phydm_extend_c2h_evt {
	PHYDM_EXTEND_C2H_DBG_PRINT = 0

};

/*
 * =========== Extern Variable ??? It should be forbidden.
 */

/*
 * =========== EXtern Function Prototype
 */

u8 odm_read_1byte(struct phy_dm_struct *dm, u32 reg_addr);

u16 odm_read_2byte(struct phy_dm_struct *dm, u32 reg_addr);

u32 odm_read_4byte(struct phy_dm_struct *dm, u32 reg_addr);

void odm_write_1byte(struct phy_dm_struct *dm, u32 reg_addr, u8 data);

void odm_write_2byte(struct phy_dm_struct *dm, u32 reg_addr, u16 data);

void odm_write_4byte(struct phy_dm_struct *dm, u32 reg_addr, u32 data);

void odm_set_mac_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask,
		     u32 data);

u32 odm_get_mac_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask);

void odm_set_bb_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask,
		    u32 data);

u32 odm_get_bb_reg(struct phy_dm_struct *dm, u32 reg_addr, u32 bit_mask);

void odm_set_rf_reg(struct phy_dm_struct *dm, enum odm_rf_radio_path e_rf_path,
		    u32 reg_addr, u32 bit_mask, u32 data);

u32 odm_get_rf_reg(struct phy_dm_struct *dm, enum odm_rf_radio_path e_rf_path,
		   u32 reg_addr, u32 bit_mask);

/*
 * Memory Relative Function.
 */
void odm_allocate_memory(struct phy_dm_struct *dm, void **ptr, u32 length);
void odm_free_memory(struct phy_dm_struct *dm, void *ptr, u32 length);

void odm_move_memory(struct phy_dm_struct *dm, void *p_dest, void *src,
		     u32 length);

s32 odm_compare_memory(struct phy_dm_struct *dm, void *p_buf1, void *buf2,
		       u32 length);

void odm_memory_set(struct phy_dm_struct *dm, void *pbuf, s8 value, u32 length);

/*
 * ODM MISC-spin lock relative API.
 */
void odm_acquire_spin_lock(struct phy_dm_struct *dm,
			   enum rt_spinlock_type type);

void odm_release_spin_lock(struct phy_dm_struct *dm,
			   enum rt_spinlock_type type);

/*
 * ODM Timer relative API.
 */
void odm_stall_execution(u32 us_delay);

void ODM_delay_ms(u32 ms);

void ODM_delay_us(u32 us);

void ODM_sleep_ms(u32 ms);

void ODM_sleep_us(u32 us);

/*
 * ODM FW relative API.
 */
void odm_fill_h2c_cmd(struct phy_dm_struct *dm, u8 element_id, u32 cmd_len,
		      u8 *cmd_buffer);

u8 phydm_c2H_content_parsing(void *dm_void, u8 c2h_cmd_id, u8 c2h_cmd_len,
			     u8 *tmp_buf);

u64 odm_get_current_time(struct phy_dm_struct *dm);
u64 odm_get_progressing_time(struct phy_dm_struct *dm, u64 start_time);

void odm_set_tx_power_index_by_rate_section(struct phy_dm_struct *dm,
					    u8 rf_path, u8 channel,
					    u8 rate_section);

u8 odm_get_tx_power_index(struct phy_dm_struct *dm, u8 rf_path, u8 tx_rate,
			  u8 band_width, u8 channel);

#endif /* __ODM_INTERFACE_H__ */
