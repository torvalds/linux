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
#include "mp_precomp.h"
#include "phydm_precomp.h"

static bool phydm_la_buffer_allocate(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;
	bool ret = false;

	ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "[LA mode BufferAllocate]\n");

	if (adc_smp_buf->length == 0) {
		odm_allocate_memory(dm, (void **)&adc_smp_buf->octet,
				    adc_smp_buf->buffer_size);
		if (!adc_smp_buf->octet) {
			ret = false;
		} else {
			adc_smp_buf->length = adc_smp_buf->buffer_size;
			ret = true;
		}
	}

	return ret;
}

static void phydm_la_get_tx_pkt_buf(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;
	u32 i = 0, value32, data_l = 0, data_h = 0;
	u32 addr, finish_addr;
	u32 end_addr = (adc_smp_buf->start_pos + adc_smp_buf->buffer_size) -
		       1; /*end_addr = 0x3ffff;*/
	bool is_round_up;
	static u32 page = 0xFF;
	u32 smp_cnt = 0, smp_number = 0, addr_8byte = 0;

	odm_memory_set(dm, adc_smp_buf->octet, 0, adc_smp_buf->length);
	odm_write_1byte(dm, 0x0106, 0x69);

	ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "GetTxPktBuf\n");

	value32 = odm_read_4byte(dm, 0x7c0);
	is_round_up = (bool)((value32 & BIT(31)) >> 31);
	/*Reg7C0[30:16]: finish addr (unit: 8byte)*/
	finish_addr = (value32 & 0x7FFF0000) >> 16;

	if (is_round_up) {
		addr = (finish_addr + 1) << 3;
		ODM_RT_TRACE(
			dm, ODM_COMP_UNCOND,
			"is_round_up = ((%d)), finish_addr=((0x%x)), 0x7c0=((0x%x))\n",
			is_round_up, finish_addr, value32);
		/*Byte to 64Byte*/
		smp_number = ((adc_smp_buf->buffer_size) >> 3);
	} else {
		addr = adc_smp_buf->start_pos;

		addr_8byte = addr >> 3;
		if (addr_8byte > finish_addr)
			smp_number = addr_8byte - finish_addr;
		else
			smp_number = finish_addr - addr_8byte;

		ODM_RT_TRACE(
			dm, ODM_COMP_UNCOND,
			"is_round_up = ((%d)), finish_addr=((0x%x * 8Byte)), Start_Addr = ((0x%x * 8Byte)), smp_number = ((%d))\n",
			is_round_up, finish_addr, addr_8byte, smp_number);
	}

	if (dm->support_ic_type & ODM_RTL8197F) {
		/*64K byte*/
		for (addr = 0x0, i = 0; addr < end_addr; addr += 8, i += 2) {
			if ((addr & 0xfff) == 0)
				odm_set_bb_reg(dm, 0x0140, MASKLWORD,
					       0x780 + (addr >> 12));
			data_l = odm_get_bb_reg(dm, 0x8000 + (addr & 0xfff),
						MASKDWORD);
			data_h = odm_get_bb_reg(dm, 0x8000 + (addr & 0xfff) + 4,
						MASKDWORD);

			ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "%08x%08x\n", data_h,
				     data_l);
		}
	} else {
		while (addr != (finish_addr << 3)) {
			if (page != (addr >> 12)) {
				/*Reg140=0x780+(addr>>12),
				 *addr=0x30~0x3F, total 16 pages
				 */
				page = (addr >> 12);
			}
			odm_set_bb_reg(dm, 0x0140, MASKLWORD, 0x780 + page);

			/*pDataL = 0x8000+(addr&0xfff);*/
			data_l = odm_get_bb_reg(dm, 0x8000 + (addr & 0xfff),
						MASKDWORD);
			data_h = odm_get_bb_reg(dm, 0x8000 + (addr & 0xfff) + 4,
						MASKDWORD);

			adc_smp_buf->octet[i] = data_h;
			adc_smp_buf->octet[i + 1] = data_l;

			ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "%08x%08x\n", data_h,
				     data_l);

			i = i + 2;

			if ((addr + 8) >= end_addr)
				addr = adc_smp_buf->start_pos;
			else
				addr = addr + 8;

			smp_cnt++;
			if (smp_cnt >= (smp_number - 1))
				break;
		}
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "smp_cnt = ((%d))\n",
			     smp_cnt);
	}
}

static void phydm_la_mode_set_mac_iq_dump(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	u32 reg_value;

	odm_write_1byte(dm, 0x7c0, 0); /*clear all 0x7c0*/
	odm_set_mac_reg(dm, 0x7c0, BIT(0), 1); /*Enable LA mode HW block*/

	if (adc_smp->la_trig_mode == PHYDM_MAC_TRIG) {
		adc_smp->is_bb_trigger = 0;
		odm_set_mac_reg(dm, 0x7c0, BIT(2),
				1); /*polling bit for MAC mode*/
		odm_set_mac_reg(
			dm, 0x7c0, BIT(4) | BIT(3),
			adc_smp->la_trigger_edge); /*trigger mode for MAC*/

		ODM_RT_TRACE(
			dm, ODM_COMP_UNCOND,
			"[MAC_trig] ref_mask = ((0x%x)), ref_value = ((0x%x)), dbg_port = ((0x%x))\n",
			adc_smp->la_mac_ref_mask, adc_smp->la_trig_sig_sel,
			adc_smp->la_dbg_port);
		/*[Set MAC Debug Port]*/
		odm_set_mac_reg(dm, 0xF4, BIT(16), 1);
		odm_set_mac_reg(dm, 0x38, 0xff0000, adc_smp->la_dbg_port);
		odm_set_mac_reg(dm, 0x7c4, MASKDWORD, adc_smp->la_mac_ref_mask);
		odm_set_mac_reg(dm, 0x7c8, MASKDWORD, adc_smp->la_trig_sig_sel);

	} else {
		adc_smp->is_bb_trigger = 1;
		odm_set_mac_reg(dm, 0x7c0, BIT(1),
				1); /*polling bit for BB ADC mode*/

		if (adc_smp->la_trig_mode == PHYDM_ADC_MAC_TRIG) {
			odm_set_mac_reg(
				dm, 0x7c0, BIT(3),
				1); /*polling bit for MAC trigger event*/
			odm_set_mac_reg(dm, 0x7c0, BIT(7) | BIT(6),
					adc_smp->la_trig_sig_sel);

			if (adc_smp->la_trig_sig_sel == ADCSMP_TRIG_REG)
				odm_set_mac_reg(
					dm, 0x7c0, BIT(5),
					1); /* manual trigger 0x7C0[5] = 0->1*/
		}
	}

	reg_value = odm_get_bb_reg(dm, 0x7c0, 0xff);
	ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
		     "4. [Set MAC IQ dump] 0x7c0[7:0] = ((0x%x))\n", reg_value);
}

static void phydm_la_mode_set_dma_type(void *dm_void, u8 la_dma_type)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
		     "2. [LA mode DMA setting] Dma_type = ((%d))\n",
		     la_dma_type);

	if (dm->support_ic_type & ODM_N_ANTDIV_SUPPORT)
		odm_set_bb_reg(dm, 0x9a0, 0xf00, la_dma_type); /*0x9A0[11:8]*/
	else
		odm_set_bb_reg(dm, odm_adc_trigger_jaguar2, 0xf00,
			       la_dma_type); /*0x95C[11:8]*/
}

static void phydm_adc_smp_start(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	u8 tmp_u1b;
	u8 while_cnt = 0;
	u8 polling_ok = false, target_polling_bit;

	phydm_la_mode_bb_setting(dm);
	phydm_la_mode_set_dma_type(dm, adc_smp->la_dma_type);
	phydm_la_mode_set_trigger_time(dm, adc_smp->la_trigger_time);

	if (dm->support_ic_type & ODM_RTL8197F) {
		odm_set_bb_reg(dm, 0xd00, BIT(26), 0x1);
	} else { /*for 8814A and 8822B?*/
		odm_write_1byte(dm, 0x198c, 0x7);
		odm_write_1byte(dm, 0x8b4, 0x80);
		/* odm_set_bb_reg(dm, 0x8b4, BIT(7), 1); */
	}

	phydm_la_mode_set_mac_iq_dump(dm);
	/* return; */

	target_polling_bit = (adc_smp->is_bb_trigger) ? BIT(1) : BIT(2);
	do { /*Poll time always use 100ms, when it exceed 2s, break while loop*/
		tmp_u1b = odm_read_1byte(dm, 0x7c0);

		if (adc_smp->adc_smp_state != ADCSMP_STATE_SET) {
			ODM_RT_TRACE(
				dm, ODM_COMP_UNCOND,
				"[state Error] adc_smp_state != ADCSMP_STATE_SET\n");
			break;

		} else if (tmp_u1b & target_polling_bit) {
			ODM_delay_ms(100);
			while_cnt = while_cnt + 1;
			continue;
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
				     "[LA Query OK] polling_bit=((0x%x))\n",
				     target_polling_bit);
			polling_ok = true;
			if (dm->support_ic_type & ODM_RTL8197F)
				odm_set_bb_reg(dm, 0x7c0, BIT(0), 0x0);
			break;
		}
	} while (while_cnt < 20);

	if (adc_smp->adc_smp_state == ADCSMP_STATE_SET) {
		if (polling_ok)
			phydm_la_get_tx_pkt_buf(dm);
		else
			ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
				     "[Polling timeout]\n");
	}

	if (adc_smp->adc_smp_state == ADCSMP_STATE_SET)
		adc_smp->adc_smp_state = ADCSMP_STATE_QUERY;

	ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
		     "[LA mode] LA_pattern_count = ((%d))\n",
		     adc_smp->la_count);

	adc_smp_stop(dm);

	if (adc_smp->la_count == 0) {
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
			     "LA Dump finished ---------->\n\n\n");
		/**/
	} else {
		adc_smp->la_count--;
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
			     "LA Dump more ---------->\n\n\n");
		adc_smp_set(dm, adc_smp->la_trig_mode, adc_smp->la_trig_sig_sel,
			    adc_smp->la_dma_type, adc_smp->la_trigger_time, 0);
	}
}

void adc_smp_set(void *dm_void, u8 trig_mode, u32 trig_sig_sel,
		 u8 dma_data_sig_sel, u32 trigger_time, u16 polling_time)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	bool is_set_success = true;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;

	adc_smp->la_trig_mode = trig_mode;
	adc_smp->la_trig_sig_sel = trig_sig_sel;
	adc_smp->la_dma_type = dma_data_sig_sel;
	adc_smp->la_trigger_time = trigger_time;

	if (adc_smp->adc_smp_state != ADCSMP_STATE_IDLE)
		is_set_success = false;
	else if (adc_smp->adc_smp_buf.length == 0)
		is_set_success = phydm_la_buffer_allocate(dm);

	if (is_set_success) {
		adc_smp->adc_smp_state = ADCSMP_STATE_SET;

		ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
			     "[LA Set Success] LA_State=((%d))\n",
			     adc_smp->adc_smp_state);

		phydm_adc_smp_start(dm);
	} else {
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
			     "[LA Set Fail] LA_State=((%d))\n",
			     adc_smp->adc_smp_state);
	}
}

void adc_smp_query(void *dm_void, void *output, u32 out_len, u32 *pused)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;
	u32 used = *pused;
	u32 i;

	ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "%s adc_smp_state %d", __func__,
		     adc_smp->adc_smp_state);

	for (i = 0; i < (adc_smp_buf->length >> 2) - 2; i += 2) {
		PHYDM_SNPRINTF(output + used, out_len - used, "%08x%08x\n",
			       adc_smp_buf->octet[i],
			       adc_smp_buf->octet[i + 1]);
	}

	PHYDM_SNPRINTF(output + used, out_len - used, "\n");
	*pused = used;
}

s32 adc_smp_get_sample_counts(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;

	return (adc_smp_buf->length >> 2) - 2;
}

s32 adc_smp_query_single_data(void *dm_void, void *output, u32 out_len,
			      u32 index)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;
	u32 used = 0;

	if (adc_smp->adc_smp_state != ADCSMP_STATE_QUERY) {
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "Error: la data is not ready yet ...\n");
		return -1;
	}

	if (index < ((adc_smp_buf->length >> 2) - 2)) {
		PHYDM_SNPRINTF(output + used, out_len - used, "%08x%08x\n",
			       adc_smp_buf->octet[index],
			       adc_smp_buf->octet[index + 1]);
	}
	return 0;
}

void adc_smp_stop(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;

	adc_smp->adc_smp_state = ADCSMP_STATE_IDLE;
	ODM_RT_TRACE(dm, ODM_COMP_UNCOND, "[LA_Stop] LA_state = ((%d))\n",
		     adc_smp->adc_smp_state);
}

void adc_smp_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;

	adc_smp->adc_smp_state = ADCSMP_STATE_IDLE;

	if (dm->support_ic_type & ODM_RTL8814A) {
		adc_smp_buf->start_pos = 0x30000;
		adc_smp_buf->buffer_size = 0x10000;
	} else if (dm->support_ic_type & ODM_RTL8822B) {
		adc_smp_buf->start_pos = 0x20000;
		adc_smp_buf->buffer_size = 0x20000;
	} else if (dm->support_ic_type & ODM_RTL8197F) {
		adc_smp_buf->start_pos = 0x00000;
		adc_smp_buf->buffer_size = 0x10000;
	} else if (dm->support_ic_type & ODM_RTL8821C) {
		adc_smp_buf->start_pos = 0x8000;
		adc_smp_buf->buffer_size = 0x8000;
	}
}

void adc_smp_de_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	struct rt_adcsmp_string *adc_smp_buf = &adc_smp->adc_smp_buf;

	adc_smp_stop(dm);

	if (adc_smp_buf->length != 0x0) {
		odm_free_memory(dm, adc_smp_buf->octet, adc_smp_buf->length);
		adc_smp_buf->length = 0x0;
	}
}

void phydm_la_mode_bb_setting(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;

	u8 trig_mode = adc_smp->la_trig_mode;
	u32 trig_sig_sel = adc_smp->la_trig_sig_sel;
	u32 dbg_port = adc_smp->la_dbg_port;
	u8 is_trigger_edge = adc_smp->la_trigger_edge;
	u8 sampling_rate = adc_smp->la_smp_rate;

	ODM_RT_TRACE(
		dm, ODM_COMP_UNCOND,
		"1. [LA mode bb_setting] trig_mode = ((%d)), dbg_port = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x))\n",
		trig_mode, dbg_port, is_trigger_edge, sampling_rate,
		trig_sig_sel);

	if (trig_mode == PHYDM_MAC_TRIG)
		trig_sig_sel = 0; /*ignore this setting*/

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (trig_mode == PHYDM_ADC_RF0_TRIG) {
			/*DBGOUT_RFC_a[31:0]*/
			odm_set_bb_reg(dm, 0x8f8,
				       BIT(25) | BIT(24) | BIT(23) | BIT(22),
				       9);
		} else if (trig_mode == PHYDM_ADC_RF1_TRIG) {
			/*DBGOUT_RFC_b[31:0]*/
			odm_set_bb_reg(dm, 0x8f8,
				       BIT(25) | BIT(24) | BIT(23) | BIT(22),
				       8);
		} else {
			odm_set_bb_reg(dm, 0x8f8,
				       BIT(25) | BIT(24) | BIT(23) | BIT(22),
				       0);
		}
		/*
		 *	(0:) '{ofdm_dbg[31:0]}'
		 *	(1:) '{cca,crc32_fail,dbg_ofdm[29:0]}'
		 *	(2:) '{vbon,crc32_fail,dbg_ofdm[29:0]}'
		 *	(3:) '{cca,crc32_ok,dbg_ofdm[29:0]}'
		 *	(4:) '{vbon,crc32_ok,dbg_ofdm[29:0]}'
		 *	(5:) '{dbg_iqk_anta}'
		 *	(6:) '{cca,ofdm_crc_ok,dbg_dp_anta[29:0]}'
		 *	(7:) '{dbg_iqk_antb}'
		 *	(8:) '{DBGOUT_RFC_b[31:0]}'
		 *	(9:) '{DBGOUT_RFC_a[31:0]}'
		 *	(a:) '{dbg_ofdm}'
		 *	(b:) '{dbg_cck}'
		 */

		/*disable dbg clk gating*/
		odm_set_bb_reg(dm, 0x198C, BIT(2) | BIT(1) | BIT(0), 7);

		/*0x95C[4:0], BB debug port bit*/
		odm_set_bb_reg(dm, 0x95C, 0x1f, trig_sig_sel);
		odm_set_bb_reg(dm, 0x8FC, MASKDWORD, dbg_port);
		/*0: posedge, 1: negedge*/
		odm_set_bb_reg(dm, 0x95C, BIT(31), is_trigger_edge);
		odm_set_bb_reg(dm, 0x95c, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
		 *	(1:) '40MHz'
		 *	(2:) '20MHz'
		 *	(3:) '10MHz'
		 *	(4:) '5MHz'
		 *	(5:) '2.5MHz'
		 *	(6:) '1.25MHz'
		 *	(7:) '160MHz (for BW160 ic)'
		 */
	} else {
		/*0x9A0[4:0], BB debug port bit*/
		odm_set_bb_reg(dm, 0x9a0, 0x1f, trig_sig_sel);
		odm_set_bb_reg(dm, 0x908, MASKDWORD, dbg_port);
		/*0: posedge, 1: negedge*/
		odm_set_bb_reg(dm, 0x9A0, BIT(31), is_trigger_edge);
		odm_set_bb_reg(dm, 0x9A0, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
		 *	(1:) '40MHz'
		 *	(2:) '20MHz'
		 *	(3:) '10MHz'
		 *	(4:) '5MHz'
		 *	(5:) '2.5MHz'
		 *	(6:) '1.25MHz'
		 *	(7:) '160MHz (for BW160 ic)'
		 */
	}
}

void phydm_la_mode_set_trigger_time(void *dm_void, u32 trigger_time_mu_sec)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 trigger_time_unit_num;
	u32 time_unit = 0;

	if (trigger_time_mu_sec < 128)
		time_unit = 0; /*unit: 1mu sec*/
	else if (trigger_time_mu_sec < 256)
		time_unit = 1; /*unit: 2mu sec*/
	else if (trigger_time_mu_sec < 512)
		time_unit = 2; /*unit: 4mu sec*/
	else if (trigger_time_mu_sec < 1024)
		time_unit = 3; /*unit: 8mu sec*/
	else if (trigger_time_mu_sec < 2048)
		time_unit = 4; /*unit: 16mu sec*/
	else if (trigger_time_mu_sec < 4096)
		time_unit = 5; /*unit: 32mu sec*/
	else if (trigger_time_mu_sec < 8192)
		time_unit = 6; /*unit: 64mu sec*/

	trigger_time_unit_num = (u8)(trigger_time_mu_sec >> time_unit);

	ODM_RT_TRACE(
		dm, ODM_COMP_UNCOND,
		"3. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n",
		trigger_time_unit_num, time_unit);

	odm_set_mac_reg(dm, 0x7cc, BIT(20) | BIT(19) | BIT(18), time_unit);
	odm_set_mac_reg(dm, 0x7c0, 0x7f00, (trigger_time_unit_num & 0x7f));
}

void phydm_lamode_trigger_setting(void *dm_void, char input[][16], u32 *_used,
				  char *output, u32 *_out_len, u32 input_num)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	u8 trig_mode, dma_data_sig_sel;
	u32 trig_sig_sel;
	bool is_enable_la_mode;
	u32 trigger_time_mu_sec;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE) {
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		is_enable_la_mode = (bool)var1[0];
		/*dbg_print("echo cmd input_num = %d\n", input_num);*/

		if ((strcmp(input[1], help) == 0)) {
			PHYDM_SNPRINTF(output + used,
				       out_len - used,
				       "{En} {0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC}\n {BB:dbg_port[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA type} {TrigTime}\n {polling_time/ref_mask} {dbg_port} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}\n");
		} else if (is_enable_la_mode) {
			PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);

			trig_mode = (u8)var1[1];

			if (trig_mode == PHYDM_MAC_TRIG)
				PHYDM_SSCANF(input[3], DCMD_HEX, &var1[2]);
			else
				PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
			trig_sig_sel = var1[2];

			PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var1[3]);
			PHYDM_SSCANF(input[5], DCMD_DECIMAL, &var1[4]);
			PHYDM_SSCANF(input[6], DCMD_HEX, &var1[5]);
			PHYDM_SSCANF(input[7], DCMD_HEX, &var1[6]);
			PHYDM_SSCANF(input[8], DCMD_DECIMAL, &var1[7]);
			PHYDM_SSCANF(input[9], DCMD_DECIMAL, &var1[8]);
			PHYDM_SSCANF(input[10], DCMD_DECIMAL, &var1[9]);

			dma_data_sig_sel = (u8)var1[3];
			trigger_time_mu_sec = var1[4]; /* unit: us */

			adc_smp->la_mac_ref_mask = var1[5];
			adc_smp->la_dbg_port = var1[6];
			adc_smp->la_trigger_edge = (u8)var1[7];
			adc_smp->la_smp_rate = (u8)(var1[8] & 0x7);
			adc_smp->la_count = var1[9];

			ODM_RT_TRACE(
				dm, ODM_COMP_UNCOND,
				"echo lamode %d %d %d %d %d %d %x %d %d %d\n",
				var1[0], var1[1], var1[2], var1[3], var1[4],
				var1[5], var1[6], var1[7], var1[8], var1[9]);

			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"a.En= ((1)),  b.mode = ((%d)), c.Trig_Sel = ((0x%x)), d.Dma_type = ((%d))\n",
				trig_mode, trig_sig_sel, dma_data_sig_sel);
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"e.Trig_Time = ((%dus)), f.mac_ref_mask = ((0x%x)), g.dbg_port = ((0x%x))\n",
				trigger_time_mu_sec, adc_smp->la_mac_ref_mask,
				adc_smp->la_dbg_port);
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"h.Trig_edge = ((%d)), i.smp rate = ((%d MHz)), j.Cap_num = ((%d))\n",
				adc_smp->la_trigger_edge,
				(80 >> adc_smp->la_smp_rate),
				adc_smp->la_count);

			adc_smp_set(dm, trig_mode, trig_sig_sel,
				    dma_data_sig_sel, trigger_time_mu_sec, 0);

		} else {
			adc_smp_stop(dm);
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Disable LA mode\n");
		}
	}
}
