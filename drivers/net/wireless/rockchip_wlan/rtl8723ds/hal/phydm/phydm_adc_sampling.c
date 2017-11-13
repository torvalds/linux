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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	#if ((RTL8197F_SUPPORT == 1) || (RTL8822B_SUPPORT == 1))
		#include "rtl8197f/Hal8197FPhyReg.h"
		#include "WlanHAL/HalMac88XX/halmac_reg2.h"
	#else
		#include "WlanHAL/HalHeader/HalComReg.h"
	#endif
#endif

#if (PHYDM_LA_MODE_SUPPORT == 1)

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#if WPP_SOFTWARE_TRACE
	#include "phydm_adc_sampling.tmh"
#endif

#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
boolean
phydm_la_buffer_allocate(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
#endif
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);
	boolean	ret = false;

	dbg_print("[LA mode BufferAllocate]\n");

	if (adc_smp_buf->length == 0) {

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		if (PlatformAllocateMemoryWithZero(adapter, (void **)&(adc_smp_buf->octet), adc_smp_buf->buffer_size) != RT_STATUS_SUCCESS) {
#else
		odm_allocate_memory(p_dm, (void **)&adc_smp_buf->octet, adc_smp_buf->buffer_size);
		if (!adc_smp_buf->octet)	{
#endif
			ret = false;
		} else
			adc_smp_buf->length = adc_smp_buf->buffer_size;
			ret = true;
	}

	return ret;
}
#endif

void
phydm_la_get_tx_pkt_buf(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);
	u32				i = 0, value32, data_l = 0, data_h = 0;
	u32				addr, finish_addr;
	u32				end_addr = (adc_smp_buf->start_pos  + adc_smp_buf->buffer_size) - 1;	/*end_addr = 0x3ffff;*/
	boolean				is_round_up;
	static u32			page = 0xFF;
	u32				smp_cnt = 0, smp_number = 0, addr_8byte = 0;
	u8				backup_dma = 0;

	odm_memory_set(p_dm, adc_smp_buf->octet, 0, adc_smp_buf->length);
	odm_write_1byte(p_dm, 0x0106, 0x69);

	dbg_print("GetTxPktBuf\n");

	value32 = odm_read_4byte(p_dm, 0x7c0);
	is_round_up = (boolean)((value32 & BIT(31)) >> 31);
	finish_addr = (value32 & 0x7FFF0000) >> 16;	/*Reg7C0[30:16]: finish addr (unit: 8byte)*/

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	#if (RTL8197F_SUPPORT)
	if (p_dm->support_ic_type & ODM_RTL8197F) {
		odm_set_bb_reg(p_dm, 0x7c0, BIT(0), 0x0);
		
		/*Stop DMA*/
		backup_dma = odm_get_mac_reg(p_dm, 0x300, MASKLWORD);
		odm_set_mac_reg(p_dm, 0x300, 0x7fff, 0x7fff);

		/*move LA mode content from IMEM to TxPktBuffer
			Source : OCPBASE_IMEM 0x00000000
			Destination : OCPBASE_TXBUF 0x18780000
			Length : 64K*/
		GET_HAL_INTERFACE(p_dm->priv)->init_ddma_handler(p_dm->priv, OCPBASE_IMEM, OCPBASE_TXBUF, 0x10000);
	}
	#endif
	#endif

	if (is_round_up) {
		addr = (finish_addr + 1) << 3;
		dbg_print("is_round_up = ((%d)), finish_addr=((0x%x)), 0x7c0=((0x%x))\n", is_round_up, finish_addr, value32);
		smp_number = ((adc_smp_buf->buffer_size) >> 3);	/*Byte to 8Byte (64bit)*/
	} else	 {
		addr = adc_smp_buf->start_pos;
		addr_8byte = addr >> 3;
		
		if (addr_8byte > finish_addr)
			smp_number = addr_8byte - finish_addr;
		else
			smp_number = finish_addr - addr_8byte;

		dbg_print("is_round_up = ((%d)), finish_addr=((0x%x * 8Byte)), Start_Addr = ((0x%x * 8Byte)), smp_number = ((%d))\n", is_round_up, finish_addr, addr_8byte, smp_number);

	}
	/*
	dbg_print("is_round_up = %d, finish_addr=0x%x, value32=0x%x\n", is_round_up, finish_addr, value32);
	dbg_print("end_addr = %x, adc_smp_buf->start_pos = 0x%x, adc_smp_buf->buffer_size = 0x%x\n", end_addr, adc_smp_buf->start_pos, adc_smp_buf->buffer_size);
	*/

	if (p_dm->support_ic_type & ODM_RTL8197F) {
		for (addr = 0x0, i = 0; addr < end_addr; addr += 8, i += 2) {	/*64K byte*/
			if ((addr & 0xfff) == 0)
				odm_set_bb_reg(p_dm, 0x0140, MASKLWORD, 0x780 + (addr >> 12));
			data_l = odm_get_bb_reg(p_dm, 0x8000 + (addr & 0xfff), MASKDWORD);
			data_h = odm_get_bb_reg(p_dm, 0x8000 + (addr & 0xfff) + 4, MASKDWORD);

			dbg_print("%08x%08x\n", data_h, data_l);
		}
	} else {
	
		i = 0;
		while (addr != (finish_addr << 3)) {
			if (page != (addr >> 12)) {
				/*Reg140=0x780+(addr>>12), addr=0x30~0x3F, total 16 pages*/
				page = (addr >> 12);
			}
			odm_set_bb_reg(p_dm, 0x0140, MASKLWORD, 0x780 + page);

			/*pDataL = 0x8000+(addr&0xfff);*/
			data_l = odm_get_bb_reg(p_dm, 0x8000 + (addr & 0xfff), MASKDWORD);
			data_h = odm_get_bb_reg(p_dm, 0x8000 + (addr & 0xfff) + 4, MASKDWORD);

			#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
			adc_smp_buf->octet[i] = data_h;
			adc_smp_buf->octet[i + 1] = data_l;
			#endif

		#if DBG /*WIN driver check build*/
			dbg_print("%08x%08x\n", data_h, data_l);
		#else	/*WIN driver free build*/
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("%08x%08x\n", adc_smp_buf->octet[i], adc_smp_buf->octet[i + 1]));
			#endif
		#endif

			i = i + 2;

			if ((addr + 8) >= end_addr)
				addr = adc_smp_buf->start_pos;
			else
				addr = addr + 8;

			smp_cnt++;
			if (smp_cnt >= (smp_number - 1))
				break;
		}
		dbg_print("smp_cnt = ((%d))\n", smp_cnt);
		
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("smp_cnt = ((%d))\n", smp_cnt));
		#endif
	}

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	#if (RTL8197F_SUPPORT)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		odm_set_mac_reg(p_dm, 0x300, 0x7fff, backup_dma);	/*Resume DMA*/
	#endif
	#endif
}

void
phydm_la_mode_set_mac_iq_dump(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);
	u32			reg_value;

	odm_write_1byte(p_dm, 0x7c0, 0);		/*clear all 0x7c0*/
	odm_set_mac_reg(p_dm, 0x7c0, BIT(0), 1);  /*Enable LA mode HW block*/

	if (adc_smp->la_trig_mode == PHYDM_MAC_TRIG) {

		adc_smp->is_bb_trigger = 0;
		odm_set_mac_reg(p_dm, 0x7c0, BIT(2), 1); /*polling bit for MAC mode*/
		odm_set_mac_reg(p_dm, 0x7c0, BIT(4) | BIT(3), adc_smp->la_trigger_edge); /*trigger mode for MAC*/

		dbg_print("[MAC_trig] ref_mask = ((0x%x)), ref_value = ((0x%x)), dbg_port = ((0x%x))\n", adc_smp->la_mac_mask_or_hdr_sel, adc_smp->la_trig_sig_sel, adc_smp->la_dbg_port);
		/*[Set MAC Debug Port]*/
		odm_set_mac_reg(p_dm, 0xF4, BIT(16), 1);
		odm_set_mac_reg(p_dm, 0x38, 0xff0000, adc_smp->la_dbg_port);
		odm_set_mac_reg(p_dm, 0x7c4, MASKDWORD, adc_smp->la_mac_mask_or_hdr_sel);
		odm_set_mac_reg(p_dm, 0x7c8, MASKDWORD, adc_smp->la_trig_sig_sel);

	} else {

		adc_smp->is_bb_trigger = 1;
		odm_set_mac_reg(p_dm, 0x7c0, BIT(1), 1); /*polling bit for BB ADC mode*/

		if (adc_smp->la_trig_mode == PHYDM_ADC_MAC_TRIG) {

			odm_set_mac_reg(p_dm, 0x7c0, BIT(3), 1); /*polling bit for MAC trigger event*/
			odm_set_mac_reg(p_dm, 0x7c0, BIT(7) | BIT(6), adc_smp->la_trig_sig_sel);

			if (adc_smp->la_trig_sig_sel == ADCSMP_TRIG_REG)
				odm_set_mac_reg(p_dm, 0x7c0, BIT(5), 1); /* manual trigger 0x7C0[5] = 0->1*/
		}
	}

	reg_value = odm_get_bb_reg(p_dm, 0x7c0, 0xff);
	dbg_print("4. [Set MAC IQ dump] 0x7c0[7:0] = ((0x%x))\n", reg_value);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("4. [Set MAC IQ dump] 0x7c0[7:0] = ((0x%x))\n", reg_value));
#endif

}

void
phydm_adc_smp_start(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP				*adc_smp = &(p_dm->adcsmp);
	u8					tmp_u1b;
	u8					while_cnt = 0;
	u8					polling_ok = false, target_polling_bit;

	phydm_la_mode_bb_setting(p_dm);
	phydm_la_mode_set_trigger_time(p_dm, adc_smp->la_trigger_time);

	if (p_dm->support_ic_type & ODM_RTL8197F)
		odm_set_bb_reg(p_dm, 0xd00, BIT(26), 0x1);
	else {	/*for 8814A and 8822B?*/
		odm_write_1byte(p_dm, 0x8b4, 0x80);
		/* odm_set_bb_reg(p_dm, 0x8b4, BIT(7), 1); */
	}

	phydm_la_mode_set_mac_iq_dump(p_dm);

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_stop(p_dm->priv);
	#endif

	target_polling_bit = (adc_smp->is_bb_trigger) ? BIT(1) : BIT(2);
	do { /*Polling time always use 100ms, when it exceed 2s, break while loop*/
		tmp_u1b = odm_read_1byte(p_dm, 0x7c0);

		if (adc_smp->adc_smp_state != ADCSMP_STATE_SET) {
			dbg_print("[state Error] adc_smp_state != ADCSMP_STATE_SET\n");
			break;

		} else if (tmp_u1b & target_polling_bit) {
			ODM_delay_ms(100);
			while_cnt = while_cnt + 1;
			continue;
		} else {
			dbg_print("[LA Query OK] polling_bit=((0x%x))\n", target_polling_bit);
			polling_ok = true;
			break;
		}
	} while (while_cnt < 20);

	if (adc_smp->adc_smp_state == ADCSMP_STATE_SET) {

		if (polling_ok)
			phydm_la_get_tx_pkt_buf(p_dm);
		else
			dbg_print("[Polling timeout]\n");
	}

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_resume(p_dm->priv);
	#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (adc_smp->adc_smp_state == ADCSMP_STATE_SET)
		adc_smp->adc_smp_state = ADCSMP_STATE_QUERY;
#endif

	dbg_print("[LA mode] LA_pattern_count = ((%d))\n", adc_smp->la_count);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("[LA mode] la_count = ((%d))\n", adc_smp->la_count));
#endif


	adc_smp_stop(p_dm);

	if (adc_smp->la_count == 0) {
		dbg_print("LA Dump finished ---------->\n\n\n");
		phydm_release_bb_dbg_port(p_dm);
		
		if ((p_dm->support_ic_type & ODM_RTL8821C) && (p_dm->cut_version >= ODM_CUT_B))
			odm_set_bb_reg(p_dm, 0x95c, BIT(23), 0);

	} else {
		adc_smp->la_count--;
		dbg_print("LA Dump more ---------->\n\n\n");
		adc_smp_set(p_dm, adc_smp->la_trig_mode, adc_smp->la_trig_sig_sel, adc_smp->la_dma_type, adc_smp->la_trigger_time, 0);
	}

}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void
adc_smp_work_item_callback(
	void	*p_context
)
{
	struct _ADAPTER			*adapter = (struct _ADAPTER *)p_context;
	PHAL_DATA_TYPE		p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);

	dbg_print("[WorkItem Call back] LA_State=((%d))\n", adc_smp->adc_smp_state);
	phydm_adc_smp_start(p_dm);
}
#endif

void
adc_smp_set(
	void	*p_dm_void,
	u8	trig_mode,
	u32	trig_sig_sel,
	u8	dma_data_sig_sel,
	u32	trigger_time,
	u16	polling_time
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	boolean				is_set_success = true;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);

	adc_smp->la_trig_mode = trig_mode;
	adc_smp->la_trig_sig_sel = trig_sig_sel;
	adc_smp->la_dma_type = dma_data_sig_sel;
	adc_smp->la_trigger_time = trigger_time;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (adc_smp->adc_smp_state != ADCSMP_STATE_IDLE)
		is_set_success = false;
	else if (adc_smp->adc_smp_buf.length == 0)
		is_set_success = phydm_la_buffer_allocate(p_dm);
#endif

	if (is_set_success) {
		adc_smp->adc_smp_state = ADCSMP_STATE_SET;

		dbg_print("[LA Set Success] LA_State=((%d))\n", adc_smp->adc_smp_state);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

		dbg_print("ADCSmp_work_item_index = ((%d))\n", adc_smp->la_work_item_index);
		if (adc_smp->la_work_item_index != 0) {
			odm_schedule_work_item(&(adc_smp->adc_smp_work_item_1));
			adc_smp->la_work_item_index = 0;
		} else {
			odm_schedule_work_item(&(adc_smp->adc_smp_work_item));
			adc_smp->la_work_item_index = 1;
		}
#else
		phydm_adc_smp_start(p_dm);
#endif
	} else
		dbg_print("[LA Set Fail] LA_State=((%d))\n", adc_smp->adc_smp_state);


}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
enum rt_status
adc_smp_query(
	void				*p_dm_void,
	ULONG				information_buffer_length,
	void				*information_buffer,
	PULONG				bytes_written
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);
	enum rt_status			ret_status = RT_STATUS_SUCCESS;
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);

	dbg_print("[%s] LA_State=((%d))", __func__, adc_smp->adc_smp_state);

	if (information_buffer_length != adc_smp_buf->buffer_size)	{
		*bytes_written = 0;
		ret_status = RT_STATUS_RESOURCE;
	} else if (adc_smp_buf->length != adc_smp_buf->buffer_size) {
		*bytes_written = 0;
		ret_status = RT_STATUS_RESOURCE;
	} else if (adc_smp->adc_smp_state != ADCSMP_STATE_QUERY) {
		*bytes_written = 0;
		ret_status = RT_STATUS_PENDING;
	} else {
		odm_move_memory(p_dm, information_buffer, adc_smp_buf->octet, adc_smp_buf->buffer_size);
		*bytes_written = adc_smp_buf->buffer_size;

		adc_smp->adc_smp_state = ADCSMP_STATE_IDLE;
	}

	dbg_print("Return status %d\n", ret_status);

	return ret_status;
}
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)

void
adc_smp_query(
	void		*p_dm_void,
	void		*output,
	u32		out_len,
	u32		*pused
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);
	u32 used = *pused;
	u32 i;
	/* struct timespec t; */
	/* rtw_get_current_timespec(&t); */

	dbg_print("%s adc_smp_state %d", __func__, adc_smp->adc_smp_state);

	for (i = 0; i < (adc_smp_buf->length >> 2) - 2; i += 2) {
		PHYDM_SNPRINTF((output + used, out_len - used,
			"%08x%08x\n", adc_smp_buf->octet[i], adc_smp_buf->octet[i + 1]));
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\n"));
	/* PHYDM_SNPRINTF((output+used, out_len-used, "\n[%lu.%06lu]\n", t.tv_sec, t.tv_nsec)); */
	*pused = used;
}

s32
adc_smp_get_sample_counts(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);

	return (adc_smp_buf->length >> 2) - 2;
}

s32
adc_smp_query_single_data(
	void		*p_dm_void,
	void		*output,
	u32		out_len,
	u32		index
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);
	u32 used = 0;

	/* dbg_print("%s adc_smp_state %d\n", __func__, adc_smp->adc_smp_state); */
	if (adc_smp->adc_smp_state != ADCSMP_STATE_QUERY) {
		PHYDM_SNPRINTF((output + used, out_len - used,
				"Error: la data is not ready yet ...\n"));
		return -1;
	}

	if (index < ((adc_smp_buf->length >> 2) - 2)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%08x%08x\n",
			adc_smp_buf->octet[index], adc_smp_buf->octet[index + 1]));
	}
	return 0;
}

#endif

void
adc_smp_stop(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);

	adc_smp->adc_smp_state = ADCSMP_STATE_IDLE;
	dbg_print("[LA_Stop] LA_state = ((%d))\n", adc_smp->adc_smp_state);
}

void
adc_smp_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);

	adc_smp->adc_smp_state = ADCSMP_STATE_IDLE;

	if (p_dm->support_ic_type & ODM_RTL8814A) {
		adc_smp_buf->start_pos = 0x30000;
		adc_smp_buf->buffer_size = 0x10000;
	} else if (p_dm->support_ic_type & ODM_RTL8822B) {
		adc_smp_buf->start_pos = 0x20000;
		adc_smp_buf->buffer_size = 0x20000;
	} else if (p_dm->support_ic_type & ODM_RTL8197F) {
		adc_smp_buf->start_pos = 0x00000;
		adc_smp_buf->buffer_size = 0x10000;
	} else if (p_dm->support_ic_type & ODM_RTL8821C) {
		adc_smp_buf->start_pos = 0x8000;
		adc_smp_buf->buffer_size = 0x8000;
	}

}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void
adc_smp_de_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP			*adc_smp = &(p_dm->adcsmp);
	struct _RT_ADCSMP_STRING	*adc_smp_buf = &(adc_smp->adc_smp_buf);

	adc_smp_stop(p_dm);

	if (adc_smp_buf->length != 0x0) {
		odm_free_memory(p_dm, adc_smp_buf->octet, adc_smp_buf->length);
		adc_smp_buf->length = 0x0;
	}
}

#endif


void
phydm_la_mode_bb_setting(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP		*adc_smp = &(p_dm->adcsmp);

	u8	trig_mode = adc_smp->la_trig_mode;
	u32	trig_sig_sel = adc_smp->la_trig_sig_sel;
	u32	dbg_port = adc_smp->la_dbg_port;
	u8	is_trigger_edge = adc_smp->la_trigger_edge;
	u8	sampling_rate = adc_smp->la_smp_rate;
	u8	la_dma_type = adc_smp->la_dma_type;
	u32	dbg_port_header_sel = 0;

	dbg_print("1. [BB Setting] trig_mode = ((%d)), dbg_port = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x)), Dma_type = ((%d))\n",
		trig_mode, dbg_port, is_trigger_edge, sampling_rate, trig_sig_sel, la_dma_type);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("1. [LA mode bb_setting]trig_mode = ((%d)), dbg_port = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x)), Dma_type = ((%d))\n",
		trig_mode, dbg_port, is_trigger_edge, sampling_rate, trig_sig_sel, la_dma_type));
#endif

	if (trig_mode == PHYDM_MAC_TRIG)
		trig_sig_sel = 0; /*ignore this setting*/

	/*set BB debug port*/
	if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_3, dbg_port)) {
		dbg_print("Set dbg_port((0x%x)) success\n", dbg_port);
	}
	
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		if (trig_mode == PHYDM_ADC_RF0_TRIG)
			dbg_port_header_sel = 9;	/*DBGOUT_RFC_a[31:0]*/
		else if (trig_mode == PHYDM_ADC_RF1_TRIG)
			dbg_port_header_sel = 8;	/*DBGOUT_RFC_b[31:0]*/
		else if ((trig_mode == PHYDM_ADC_BB_TRIG) || (trig_mode == PHYDM_ADC_MAC_TRIG)) {
			
			if (adc_smp->la_mac_mask_or_hdr_sel <= 0xf) {
				dbg_port_header_sel = adc_smp->la_mac_mask_or_hdr_sel;
			} else {
				dbg_port_header_sel = 0;
			}
		}

		phydm_bb_dbg_port_header_sel(p_dm, dbg_port_header_sel);

		odm_set_bb_reg(p_dm, 0x95c, 0xf00, la_dma_type);	/*0x95C[11:8]*/
		odm_set_bb_reg(p_dm, 0x95C, 0x1f, trig_sig_sel);	/*0x95C[4:0], BB debug port bit*/
		odm_set_bb_reg(p_dm, 0x95C, BIT(31), is_trigger_edge); /*0: posedge, 1: negedge*/
		odm_set_bb_reg(p_dm, 0x95c, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
		if ((p_dm->support_ic_type & ODM_RTL8821C) && (p_dm->cut_version >= ODM_CUT_B)) {
			odm_set_bb_reg(p_dm, 0x95c, BIT(23), 1);
		}
	} else {

		odm_set_bb_reg(p_dm, 0x9a0, 0xf00, la_dma_type);	/*0x9A0[11:8]*/
		odm_set_bb_reg(p_dm, 0x9a0, 0x1f, trig_sig_sel);	/*0x9A0[4:0], BB debug port bit*/
		odm_set_bb_reg(p_dm, 0x9A0, BIT(31), is_trigger_edge); /*0: posedge, 1: negedge*/
		odm_set_bb_reg(p_dm, 0x9A0, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
	}
}

void
phydm_la_mode_set_trigger_time(
	void		*p_dm_void,
	u32		trigger_time_mu_sec
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			trigger_time_unit_num;
	u32			time_unit = 0;

	if (trigger_time_mu_sec < 128) {
		time_unit = 0; /*unit: 1mu sec*/
	} else if (trigger_time_mu_sec < 256) {
		time_unit = 1; /*unit: 2mu sec*/
	} else if (trigger_time_mu_sec < 512) {
		time_unit = 2; /*unit: 4mu sec*/
	} else if (trigger_time_mu_sec < 1024) {
		time_unit = 3; /*unit: 8mu sec*/
	} else if (trigger_time_mu_sec < 2048) {
		time_unit = 4; /*unit: 16mu sec*/
	} else if (trigger_time_mu_sec < 4096) {
		time_unit = 5; /*unit: 32mu sec*/
	} else if (trigger_time_mu_sec < 8192) {
		time_unit = 6; /*unit: 64mu sec*/
	}

	trigger_time_unit_num = (u8)(trigger_time_mu_sec >> time_unit);

	dbg_print("2. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n", trigger_time_unit_num, time_unit);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("3. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n", trigger_time_unit_num, time_unit));
#endif

	odm_set_mac_reg(p_dm, 0x7cc, BIT(20) | BIT(19) | BIT(18), time_unit);
	odm_set_mac_reg(p_dm, 0x7c0, 0x7f00, (trigger_time_unit_num & 0x7f));

}


void
phydm_lamode_trigger_setting(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_ADCSMP	*adc_smp = &(p_dm->adcsmp);
	u8		trig_mode, dma_data_sig_sel;
	u32		trig_sig_sel;
	boolean		is_enable_la_mode;
	u32		trigger_time_mu_sec;
	char 		help[] = "-h";
	u32			var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (p_dm->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE) {

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		is_enable_la_mode = (boolean)var1[0];
		/*dbg_print("echo cmd input_num = %d\n", input_num);*/

		if ((strcmp(input[1], help) == 0)) {
			PHYDM_SNPRINTF((output + used, out_len - used, "{En} {0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC} \n {BB:dbg_port[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA type} {TrigTime} \n {DbgPort_head/ref_mask} {dbg_port} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}\n"));
			/**/
		} else if ((is_enable_la_mode == 1)) {

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
			trigger_time_mu_sec = var1[4]; /*unit: us*/

			adc_smp->la_mac_mask_or_hdr_sel = var1[5];
			adc_smp->la_dbg_port = var1[6];
			adc_smp->la_trigger_edge = (u8) var1[7];
			adc_smp->la_smp_rate = (u8)(var1[8] & 0x7);
			adc_smp->la_count = var1[9];


			dbg_print("echo lamode %d %d %d %d %d %d %x %d %d %d\n", var1[0], var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], var1[7], var1[8], var1[9]);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("echo lamode %d %d %d %d %d %d %x %d %d %d\n", var1[0], var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], var1[7], var1[8], var1[9]));
#endif

			PHYDM_SNPRINTF((output + used, out_len - used, "a.En= ((1)),  b.mode = ((%d)), c.Trig_Sel = ((0x%x)), d.Dma_type = ((%d))\n", trig_mode, trig_sig_sel, dma_data_sig_sel));
			PHYDM_SNPRINTF((output + used, out_len - used, "e.Trig_Time = ((%dus)), f.Dbg_head/mac_ref_mask = ((0x%x)), g.dbg_port = ((0x%x))\n", trigger_time_mu_sec, adc_smp->la_mac_mask_or_hdr_sel, adc_smp->la_dbg_port));
			PHYDM_SNPRINTF((output + used, out_len - used, "h.Trig_edge = ((%d)), i.smp rate = ((%d MHz)), j.Cap_num = ((%d))\n", adc_smp->la_trigger_edge, (80 >> adc_smp->la_smp_rate), adc_smp->la_count));

			adc_smp_set(p_dm, trig_mode, trig_sig_sel, dma_data_sig_sel, trigger_time_mu_sec, 0);

		} else {
			adc_smp_stop(p_dm);
			PHYDM_SNPRINTF((output + used, out_len - used, "Disable LA mode\n"));
		}
	}
	*_used = used;
	*_out_len = out_len;
}

#endif	/*endif PHYDM_LA_MODE_SUPPORT == 1*/
