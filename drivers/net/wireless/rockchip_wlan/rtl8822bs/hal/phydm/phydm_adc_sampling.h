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

#ifndef __INC_ADCSMP_H
#define __INC_ADCSMP_H

#if (PHYDM_LA_MODE_SUPPORT)

/* fix compile time flag*/
#define DYNAMIC_LA_MODE "4.1"

/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (RTL8197F_SUPPORT || RTL8198F_SUPPORT || RTL8197G_SUPPORT)
	#define PHYDM_COMPILE_LA_STORE_IN_IMEM
#endif
#endif

#define PHYDM_LA_STORE_IN_IMEM_IC (ODM_RTL8197F | ODM_RTL8198F | ODM_RTL8197G)

#define FULL_BUFF_MODE_SUPPORT (ODM_RTL8821C | ODM_RTL8195B | ODM_RTL8822C |\
				ODM_RTL8812F | ODM_RTL8814B)

/* @ ============================================================
 *  enumrate
 *  ============================================================
 */
enum la_dump_mode {
	LA_BB_ADC_DUMP		= 0,
	LA_MAC_DBG_DUMP		= 1
};

enum rt_adcsmp_trig_sel {
	PHYDM_ADC_BB_TRIG	= 0,
	PHYDM_ADC_MAC_TRIG	= 1,
	PHYDM_ADC_RF0_TRIG	= 2,
	PHYDM_ADC_RF1_TRIG	= 3,
	PHYDM_MAC_TRIG		= 4
};

enum rt_adcsmp_trig_sig_sel {
	ADCSMP_TRIG_CRCOK	= 0,
	ADCSMP_TRIG_CRCFAIL	= 1,
	ADCSMP_TRIG_CCA		= 2,
	ADCSMP_TRIG_REG		= 3
};

enum rt_adcsmp_state {
	ADCSMP_STATE_IDLE	= 0,
	ADCSMP_STATE_SET	= 1,
	ADCSMP_STATE_QUERY	= 2
};

enum la_buff_mode {
	ADCSMP_BUFF_HALF	= 0,
	ADCSMP_BUFF_ALL		= 1	/*Only use in MP Driver*/
};

/* @ ============================================================
 *   structure
 *  ============================================================
 */

struct rt_adcsmp_string {
	u32			*octet;
	u32			length;
	u32			buffer_size;
	u32			start_pos;
	u32			end_pos;	/*@buf addr*/
};

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
struct la_adv_trig {
	boolean			la_adv_bbtrigger_en;
	boolean			la_ori_bb_dis;
	u8			la_and1_sel;
	u8			la_and1_val;
	boolean			la_and1_inv;
	u8			la_and2_sel;
	u8			la_and2_val;
	boolean			la_and2_inv;
	u8			la_and3_sel;
	u8			la_and3_val;
	boolean			la_and3_inv;
	u32			la_and4_mask;
	u32			la_and4_bitmap;
	boolean			la_and4_inv;
};
#endif

struct rt_adcsmp {
	struct rt_adcsmp_string	adc_smp_buf;
	enum rt_adcsmp_state	adc_smp_state;
	enum la_buff_mode	la_buff_mode;
	enum la_dump_mode	la_dump_mode;
	u8			la_trig_mode;
	u32			la_trig_sig_sel;
	u8			la_dma_type;
	u32			la_trigger_time;
	/*@1.BB mode: Dbg port header sel, 2.MAC mode: for reference mask*/
	u32			la_mac_mask_or_hdr_sel;
	u32			la_dbg_port;
	u8			la_trigger_edge;
	u8			la_smp_rate;
	u32			la_count;
	u32			smp_number;
	u32			smp_number_max;
	u32			txff_page;
	boolean			is_la_print;
	boolean			en_fake_trig;
#if (RTL8197F_SUPPORT)
	u32			backup_dma;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	u8			la_work_item_index;
	RT_WORK_ITEM		adc_smp_work_item;
	RT_WORK_ITEM		adc_smp_work_item_1;
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	struct la_adv_trig	adv_trig_table;
#endif
};

/* @ ============================================================
 *  Function Prototype
 *  ============================================================
 */

void phydm_la_set(void *dm_void);

void phydm_la_cmd(void *dm_void, char input[][16], u32 *_used, char *output,
		  u32 *_out_len);

void phydm_la_stop(void *dm_void);

void phydm_la_init(void *dm_void);

void adc_smp_de_init(void *dm_void);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void adc_smp_work_item_callback(void *context);
#endif

#if 0
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
enum rt_status adc_smp_query(void *dm_void, ULONG info_buf_length,
			     void *info_buf, PULONG bytes_written);

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

void adc_smp_query(void *dm_void, void *output, u32 out_len, u32 *pused);

s32 adc_smp_get_sample_counts(void *dm_void);

s32 adc_smp_query_single_data(void *dm_void, void *output, u32 out_len,
			      u32 idx);
#endif
#endif

#endif
#endif
