/* SPDX-License-Identifier: GPL-2.0 */
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
#define _HAL_INIT_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>
#ifdef CONFIG_SFW_SUPPORTED
#include "hal8188e_s_fw.h"
#endif
#include "hal8188e_t_fw.h"


#if defined(CONFIG_IOL)
static void iol_mode_enable(PADAPTER padapter, u8 enable)
{
	u8 reg_0xf0 = 0;

	if (enable) {
		/* Enable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		/* RTW_INFO("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0|SW_OFFLOAD_EN); */
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 | SW_OFFLOAD_EN);

		if (GET_HAL_DATA(padapter)->bFWReady == _FALSE) {
			printk("bFWReady == _FALSE call reset 8051...\n");
			_8051Reset88E(padapter);
		}

	} else {
		/* disable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		/* RTW_INFO("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0& ~SW_OFFLOAD_EN); */
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

static s32 iol_execute(PADAPTER padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0, reg_1c7 = 0;
	systime start = 0;
	u32 passing_time = 0;

	systime t1, t2;
	control = control & 0x0f;
	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	/* RTW_INFO("%s reg_0x88:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x88, reg_0x88|control); */
	rtw_write8(padapter, REG_HMEBOX_E0,  reg_0x88 | control);

	t1 = start = rtw_get_current_time();
	while (
		/* (reg_1c7 = rtw_read8(padapter, 0x1c7) >1) && */
		(reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0)) & control
		&& (passing_time = rtw_get_passing_time_ms(start)) < 1000
	) {
		/* RTW_INFO("%s polling reg_0x88:0x%02x,reg_0x1c7:0x%02x\n", __FUNCTION__, reg_0x88,rtw_read8(padapter, 0x1c7) ); */
		/* rtw_udelay_os(100); */
	}

	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control) ? _FAIL : _SUCCESS;
	if (reg_0x88 & control << 4)
		status = _FAIL;
	t2 = rtw_get_current_time();
	/* printk("==> step iol_execute :  %5u reg-0x1c0= 0x%02x\n",rtw_get_time_interval_ms(t1,t2),rtw_read8(padapter, 0x1c0)); */
	/* RTW_INFO("%s in %u ms, reg_0x88:0x%02x\n", __FUNCTION__, passing_time, reg_0x88); */

	return status;
}

#ifdef CONFIG_IOL_LLT
static s32 iol_InitLLTTable(
	PADAPTER padapter,
	u8 txpktbuf_bndy
)
{
	s32 rst = _SUCCESS;
	iol_mode_enable(padapter, 1);
	/* RTW_INFO("%s txpktbuf_bndy:%u\n", __FUNCTION__, txpktbuf_bndy); */
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}
#endif /*CONFIG_IOL_LLT*/

static void
efuse_phymap_to_logical(u8 *phymap, u16 _offset, u16 _size_byte, u8  *pbuf)
{
	u8	*efuseTbl = NULL;
	u8	rtemp8;
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;


	efuseTbl = (u8 *)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord = (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if (eFuseWord == NULL) {
		RTW_INFO("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	/* 0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/* 1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	rtemp8 = *(phymap + eFuse_Addr);
	if (rtemp8 != 0xFF) {
		efuse_utilized++;
		/* printk("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8); */
		eFuse_Addr++;
	} else {
		RTW_INFO("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, rtemp8);
		goto exit;
	}


	/*  */
	/* 2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8)); */

		/* Check PG header for section num. */
		if ((rtemp8 & 0x1F) == 0x0F) {	/* extended header */
			u1temp = ((rtemp8 & 0xE0) >> 5);
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0)); */

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x\n", u1temp)); */

			rtemp8 = *(phymap + eFuse_Addr);

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	 */

			if ((rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				rtemp8 = *(phymap + eFuse_Addr);

				if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((rtemp8 >> 4) & 0x0f);
			wren = (rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_88E) {
			/* Get word enable value from PG header */
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren)); */

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr)); */
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */
					efuse_utilized++;
					eFuseWord[offset][i] = (rtemp8 & 0xff);


					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;

					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr)); */
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */

					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}

				wren >>= 1;

			}
		}

		/* Read next PG header */
		rtemp8 = *(phymap + eFuse_Addr);
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8)); */

		if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  */
	/* 3. Collect 16 sections and 4 word unit into Efuse map. */
	/*  */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	/*  */
	/* 4. Copy from Efuse map to output pointer memory!!! */
	/*  */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  */
	/* 5. Calculate Efuse utilization. */
	/*  */
	efuse_usage = (u8)((efuse_utilized * 100) / EFUSE_REAL_CONTENT_LEN_88E);
	/* rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized); */

exit:
	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if (eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

void efuse_read_phymap_from_txpktbuf(
	ADAPTER *adapter,
	int bcnhead,	/* beacon head, where FW store len(2-byte) and efuse physical map. */
	u8 *content,	/* buffer to store efuse physical map */
	u16 *size	/* for efuse content: the max byte to read. will update to byte read */
)
{
	u16 dbg_addr = 0;
	systime start = 0;
	u32 passing_time = 0;
	u8 reg_0x143 = 0;
	u8 reg_0x106 = 0;
	u32 lo32 = 0, hi32 = 0;
	u16 len = 0, count = 0;
	int i = 0;
	u16 limit = *size;

	u8 *pos = content;

	if (bcnhead < 0) /* if not valid */
		bcnhead = rtw_read8(adapter, REG_TDECTRL + 1);

	RTW_INFO("%s bcnhead:%d\n", __FUNCTION__, bcnhead);

	/* reg_0x106 = rtw_read8(adapter, REG_PKT_BUFF_ACCESS_CTRL); */
	/* RTW_INFO("%s reg_0x106:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x106, 0x69); */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	/* RTW_INFO("%s reg_0x106:0x%02x\n", __FUNCTION__, rtw_read8(adapter, 0x106)); */

	dbg_addr = bcnhead * 128 / 8; /* 8-bytes addressing */

	while (1) {
		/* RTW_INFO("%s dbg_addr:0x%x\n", __FUNCTION__, dbg_addr+i); */
		rtw_write16(adapter, REG_PKTBUF_DBG_ADDR, dbg_addr + i);

		/* RTW_INFO("%s write reg_0x143:0x00\n", __FUNCTION__); */
		rtw_write8(adapter, REG_TXPKTBUF_DBG, 0);
		start = rtw_get_current_time();
		while (!(reg_0x143 = rtw_read8(adapter, REG_TXPKTBUF_DBG)) /* dbg */
		       /* while(rtw_read8(adapter, REG_TXPKTBUF_DBG) & BIT0 */
		       && (passing_time = rtw_get_passing_time_ms(start)) < 1000
		      ) {
			RTW_INFO("%s polling reg_0x143:0x%02x, reg_0x106:0x%02x\n", __FUNCTION__, reg_0x143, rtw_read8(adapter, 0x106));
			rtw_usleep_os(100);
		}


		lo32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		hi32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

#if 0
		RTW_INFO("%s lo32:0x%08x, %02x %02x %02x %02x\n", __FUNCTION__, lo32
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L + 1)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L + 2)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L + 3)
			);
		RTW_INFO("%s hi32:0x%08x, %02x %02x %02x %02x\n", __FUNCTION__, hi32
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H + 1)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H + 2)
			 , rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H + 3)
			);
#endif

		if (i == 0) {
#if 1 /* for debug */
			u8 lenc[2];
			u16 lenbak, aaabak;
			u16 aaa;
			lenc[0] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L);
			lenc[1] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L + 1);

			aaabak = le16_to_cpup((u16 *)lenc);
			lenbak = le16_to_cpu(*((u16 *)lenc));
			aaa = le16_to_cpup((u16 *)&lo32);
#endif
			len = le16_to_cpu(*((u16 *)&lo32));

			limit = (len - 2 < limit) ? len - 2 : limit;

			RTW_INFO("%s len:%u, lenbak:%u, aaa:%u, aaabak:%u\n", __FUNCTION__, len, lenbak, aaa, aaabak);

			_rtw_memcpy(pos, ((u8 *)&lo32) + 2, (limit >= count + 2) ? 2 : limit - count);
			count += (limit >= count + 2) ? 2 : limit - count;
			pos = content + count;

		} else {
			_rtw_memcpy(pos, ((u8 *)&lo32), (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;


		}

		if (limit > count && len - 2 > count) {
			_rtw_memcpy(pos, (u8 *)&hi32, (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;
		}

		if (limit <= count || len - 2 <= count)
			break;

		i++;
	}

	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, DISABLE_TRXPKT_BUF_ACCESS);

	RTW_INFO("%s read count:%u\n", __FUNCTION__, count);
	*size = count;

}


static s32 iol_read_efuse(
	PADAPTER padapter,
	u8 txpktbuf_bndy,
	u16 offset,
	u16 size_byte,
	u8 *logical_map
)
{
	s32 status = _FAIL;
	u8 reg_0x106 = 0;
	u8 physical_map[512];
	u16 size = 512;
	int i;


	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	_rtw_memset(physical_map, 0xFF, 512);

	/* /reg_0x106 = rtw_read8(padapter, REG_PKT_BUFF_ACCESS_CTRL); */
	/* RTW_INFO("%s reg_0x106:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x106, 0x69); */
	rtw_write8(padapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	/* RTW_INFO("%s reg_0x106:0x%02x\n", __FUNCTION__, rtw_read8(padapter, 0x106)); */

	status = iol_execute(padapter, CMD_READ_EFUSE_MAP);

	if (status == _SUCCESS)
		efuse_read_phymap_from_txpktbuf(padapter, txpktbuf_bndy, physical_map, &size);

#if 0
	RTW_PRINT("%s physical map\n", __FUNCTION__);
	for (i = 0; i < size; i++) {
		if (i % 16 == 0)
			RTW_PRINT("%02x", physical_map[i]);
		else
			_RTW_PRINT("%02x", physical_map[i]);

		if (i % 16 == 7)
			_RTW_PRINT("    ");
		else if (i % 16 == 15)
			_RTW_PRINT("\n");
		else
			_RTW_PRINT(" ");
	}
	_RTW_PRINT("\n");
#endif

	efuse_phymap_to_logical(physical_map, offset, size_byte, logical_map);

	return status;
}

s32 rtl8188e_iol_efuse_patch(PADAPTER padapter)
{
	s32	result = _SUCCESS;
	printk("==> %s\n", __FUNCTION__);

	if (rtw_IOL_applied(padapter)) {
		iol_mode_enable(padapter, 1);
		result = iol_execute(padapter, CMD_READ_EFUSE_MAP);
		if (result == _SUCCESS)
			result = iol_execute(padapter, CMD_EFUSE_PATCH);

		iol_mode_enable(padapter, 0);
	}
	return result;
}

static s32 iol_ioconfig(
	PADAPTER padapter,
	u8 iocfg_bndy
)
{
	s32 rst = _SUCCESS;

	/* RTW_INFO("%s iocfg_bndy:%u\n", __FUNCTION__, iocfg_bndy); */
	rtw_write8(padapter, REG_TDECTRL + 1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);

	return rst;
}

int rtl8188e_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{

	systime start_time = rtw_get_current_time();
	u32 passing_time_ms;
	u8 polling_ret, i;
	int ret = _FAIL;
	systime t1, t2;

	/* printk("===> %s ,bndy_cnt = %d\n",__FUNCTION__,bndy_cnt); */
	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;
#ifdef CONFIG_USB_HCI
	{
		struct pkt_attrib	*pattrib = &xmit_frame->attrib;
		if (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE + pattrib->last_txcmdsz)) {
			if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
				goto exit;
		}
	}
#endif /* CONFIG_USB_HCI */

	/* rtw_IOL_cmd_buf_dump(adapter,xmit_frame->attrib.pktlen+TXDESC_OFFSET,xmit_frame->buf_addr); */
	/* rtw_hal_mgnt_xmit(adapter, xmit_frame); */
	/* rtw_dump_xframe_sync(adapter, xmit_frame); */

	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);

	t1 =	rtw_get_current_time();
	iol_mode_enable(adapter, 1);
	for (i = 0; i < bndy_cnt; i++) {
		u8 page_no = 0;
		page_no = i * 2 ;
		/* printk(" i = %d, page_no = %d\n",i,page_no);	 */
		ret = iol_ioconfig(adapter, page_no);
		if (ret != _SUCCESS)
			break;
	}
	iol_mode_enable(adapter, 0);
	t2 = rtw_get_current_time();
	/* printk("==> %s :  %5u\n",__FUNCTION__,rtw_get_time_interval_ms(t1,t2)); */
exit:
	/* restore BCN_HEAD */
	rtw_write8(adapter, REG_TDECTRL + 1, 0);
	return ret;
}

void rtw_IOL_cmd_tx_pkt_buf_dump(ADAPTER *Adapter, int data_len)
{
	u32 fifo_data, reg_140;
	u32 addr, rstatus, loop = 0;

	u16 data_cnts = (data_len / 8) + 1;
	u8 *pbuf = rtw_zvmalloc(data_len + 10);
	printk("###### %s ######\n", __FUNCTION__);

	rtw_write8(Adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	if (pbuf) {
		for (addr = 0; addr < data_cnts; addr++) {
			/* printk("==> addr:0x%02x\n",addr); */
			rtw_write32(Adapter, 0x140, addr);
			rtw_usleep_os(2);
			loop = 0;
			do {
				rstatus = (reg_140 = rtw_read32(Adapter, REG_PKTBUF_DBG_CTRL) & BIT24);
				/* printk("rstatus = %02x, reg_140:0x%08x\n",rstatus,reg_140); */
				if (rstatus) {
					fifo_data = rtw_read32(Adapter, REG_PKTBUF_DBG_DATA_L);
					/* printk("fifo_data_144:0x%08x\n",fifo_data);					 */
					_rtw_memcpy(pbuf + (addr * 8), &fifo_data , 4);

					fifo_data = rtw_read32(Adapter, REG_PKTBUF_DBG_DATA_H);
					/* printk("fifo_data_148:0x%08x\n",fifo_data);					 */
					_rtw_memcpy(pbuf + (addr * 8 + 4), &fifo_data, 4);

				}
				rtw_usleep_os(2);
			} while (!rstatus && (loop++ < 10));
		}
		rtw_IOL_cmd_buf_dump(Adapter, data_len, pbuf);
		rtw_vmfree(pbuf, data_len + 10);

	}
	printk("###### %s ######\n", __FUNCTION__);
}

#endif /* defined(CONFIG_IOL) */


static void
_FWDownloadEnable_8188E(
		PADAPTER		padapter,
		BOOLEAN			enable
)
{
	u8	tmp;

	if (enable) {
		/* MCU firmware download enable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		/* 8051 reset */
		tmp = rtw_read8(padapter, REG_MCUFWDL + 2);
		rtw_write8(padapter, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {

		/* MCU firmware download disable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp & 0xfe);

		/* Reserved for fw extension. */
		rtw_write8(padapter, REG_MCUFWDL + 1, 0x00);
	}
}
#define MAX_REG_BOLCK_SIZE	196
static int
_BlockWrite(
			PADAPTER	padapter,
			void			*buffer,
			u32			buffSize
)
{
	int ret = _SUCCESS;

	u32			blockSize_p1 = 4;	/* (Default) Phase #1 : PCI muse use 4-byte write to download FW */
	u32			blockSize_p2 = 8;	/* Phase #2 : Use 8-byte, if Phase#1 use big size to write FW. */
	u32			blockSize_p3 = 1;	/* Phase #3 : Use 1-byte, the remnant of FW image. */
	u32			blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32			remainSize_p1 = 0, remainSize_p2 = 0;
	u8			*bufferPtr	= (u8 *)buffer;
	u32			i = 0, offset = 0;
#ifdef CONFIG_PCI_HCI
	u8			remainFW[4] = {0, 0, 0, 0};
	u8			*p = NULL;
#endif

#ifdef CONFIG_USB_HCI
	blockSize_p1 = MAX_REG_BOLCK_SIZE;
#endif

	/* 3 Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;



	for (i = 0; i < blockCount_p1; i++) {
#ifdef CONFIG_USB_HCI
		ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
#else
		ret = rtw_write32(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), le32_to_cpu(*((u32 *)(bufferPtr + i * blockSize_p1))));
#endif

		if (ret == _FAIL)
			goto exit;
	}

#ifdef CONFIG_PCI_HCI
	p = (u8 *)((u32 *)(bufferPtr + blockCount_p1 * blockSize_p1));
	if (remainSize_p1) {
		switch (remainSize_p1) {
		case 0:
			break;
		case 3:
			remainFW[2] = *(p + 2);
		case 2:
			remainFW[1] = *(p + 1);
		case 1:
			remainFW[0] = *(p);
			ret = rtw_write32(padapter, (FW_8188E_START_ADDRESS + blockCount_p1 * blockSize_p1),
					  le32_to_cpu(*(u32 *)remainFW));
		}
		return ret;
	}
#endif

	/* 3 Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1 / blockSize_p2;
		remainSize_p2 = remainSize_p1 % blockSize_p2;



#ifdef CONFIG_USB_HCI
		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + offset + i * blockSize_p2), blockSize_p2, (bufferPtr + offset + i * blockSize_p2));

			if (ret == _FAIL)
				goto exit;
		}
#endif
	}

	/* 3 Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;


		for (i = 0 ; i < blockCount_p3 ; i++) {
			ret = rtw_write8(padapter, (FW_8188E_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if (ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int
_PageWrite(
			PADAPTER	padapter,
			u32			page,
			void			*buffer,
			u32			size
)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07) ;

	value8 = (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | u8Page ;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

	return _BlockWrite(padapter, buffer, size);
}
/*
#ifdef CONFIG_PCI_HCI
static void
_FillDummy(
	u8		*pFwBuf,
	u32	*pFwLen
)
{
	u32	FwLen = *pFwLen;
	u8	remain = (u8)(FwLen % 4);
	remain = (remain == 0) ? 0 : (4 - remain);

	while (remain > 0) {
		pFwBuf[FwLen] = 0;
		FwLen++;
		remain--;
	}

	*pFwLen = FwLen;
}
#endif
*/
static int
_WriteFW(
			PADAPTER	padapter,
			void			*buffer,
			u32			size
)
{
	/* Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	int ret = _SUCCESS;
	u32	pageNums, remainSize ;
	u32	page, offset;
	u8		*bufferPtr = (u8 *)buffer;

#ifdef CONFIG_PCI_HCI
	/* 20100120 Joseph: Add for 88CE normal chip. */
	/* Fill in zero to make firmware image to dword alignment.
	*		_FillDummy(bufferPtr, &size); */
#endif

	pageNums = size / MAX_DLFW_PAGE_SIZE ;
	/* RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4\n")); */
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr + offset, MAX_DLFW_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr + offset, remainSize);

		if (ret == _FAIL)
			goto exit;

	}

exit:
	return ret;
}

void _MCUIO_Reset88E(PADAPTER padapter, u8 bReset)
{
	u8 u1bTmp;

	if (bReset == _TRUE) {
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, (u1bTmp & (~BIT1)));
		/* Reset MCU IO Wrapper- sugggest by SD1-Gimmy */
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
		rtw_write8(padapter, REG_RSV_CTRL + 1, (u1bTmp & (~BIT3)));
	} else {
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, (u1bTmp & (~BIT1)));
		/* Enable MCU IO Wrapper */
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
		rtw_write8(padapter, REG_RSV_CTRL + 1, u1bTmp | BIT3);
	}

}

void _8051Reset88E(PADAPTER padapter)
{
	u8 u1bTmp;

	_MCUIO_Reset88E(padapter, _TRUE);
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp & (~BIT2));
	_MCUIO_Reset88E(padapter, _FALSE);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp | (BIT2));

	RTW_INFO("=====> _8051Reset88E(): 8051 reset success .\n");
}

static s32 polling_fwdl_chksum(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	systime start = rtw_get_current_time();
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt))
		goto exit;

	if (rtw_fwdl_test_trigger_chksum_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

static s32 _FWFreeToGo(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	systime start = rtw_get_current_time();
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset88E(adapter);

	/*  polling for FW ready */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & WINTINI_RDY))
		goto exit;

	if (rtw_fwdl_test_trigger_wintint_rdy_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);
	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)


#ifdef CONFIG_FILE_FWIMG
	extern char *rtw_fw_file_path;
	extern char *rtw_fw_wow_file_path;
	u8	FwBuffer8188E[FW_8188E_SIZE];
#endif /* CONFIG_FILE_FWIMG */

/*
 *	Description:
 *		Download 8192C firmware code.
 *
 *   */
s32 rtl8188e_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw)
{
	s32	rtStatus = _SUCCESS;
	u8 write_fw = 0;
	systime fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	PRT_FIRMWARE_8188E	pFirmware = NULL;
	PRT_8188E_FIRMWARE_HDR		pFwHdr = NULL;

	u8			*pFirmwareBuf;
	u32			FirmwareLen, tmp_fw_len = 0;
#ifdef CONFIG_FILE_FWIMG
	u8 *fwfilepath;
#endif /* CONFIG_FILE_FWIMG */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#endif

	pFirmware = (PRT_FIRMWARE_8188E)rtw_zmalloc(sizeof(RT_FIRMWARE_8188E));
	if (!pFirmware) {
		rtStatus = _FAIL;
		goto exit;
	}



#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
	if (bUsedWoWLANFw)
		fwfilepath = rtw_fw_wow_file_path;
	else
#endif /* CONFIG_WOWLAN */
	{
		fwfilepath = rtw_fw_file_path;
	}
#endif /* CONFIG_FILE_FWIMG */

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(fwfilepath) == _TRUE) {
		RTW_INFO("%s accquire FW from file:%s\n", __FUNCTION__, fwfilepath);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
	}

	switch (pFirmware->eFWSource) {
	case FW_SOURCE_IMG_FILE:
#ifdef CONFIG_FILE_FWIMG
		rtStatus = rtw_retrieve_from_file(fwfilepath, FwBuffer8188E, FW_8188E_SIZE);
		pFirmware->ulFwLength = rtStatus >= 0 ? rtStatus : 0;
		pFirmware->szFwBuffer = FwBuffer8188E;
#endif /* CONFIG_FILE_FWIMG */
		break;
	case FW_SOURCE_HEADER_FILE:
		if (bUsedWoWLANFw) {
#ifdef CONFIG_WOWLAN
			if (pwrpriv->wowlan_mode) {
	#ifdef CONFIG_SFW_SUPPORTED
				if (IS_VENDOR_8188E_I_CUT_SERIES(padapter)) {
					pFirmware->szFwBuffer = array_mp_8188e_s_fw_wowlan;
					pFirmware->ulFwLength = array_length_mp_8188e_s_fw_wowlan;
				} else
	#endif
				{
					pFirmware->szFwBuffer = array_mp_8188e_t_fw_wowlan;
					pFirmware->ulFwLength = array_length_mp_8188e_t_fw_wowlan;
				}
				RTW_INFO("%s fw:%s, size: %d\n", __func__,
						"WoWLAN", pFirmware->ulFwLength);
			}
#endif /*CONFIG_WOWLAN*/

#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode) {
					pFirmware->szFwBuffer = array_mp_8188e_t_fw_ap;
					pFirmware->ulFwLength = array_length_mp_8188e_t_fw_ap;

					RTW_INFO("%s fw: %s, size: %d\n", __func__,
						"AP_WoWLAN", pFirmware->ulFwLength);
			}
#endif /*CONFIG_AP_WOWLAN*/
		} else {
#ifdef CONFIG_SFW_SUPPORTED
			if (IS_VENDOR_8188E_I_CUT_SERIES(padapter)) {
				pFirmware->szFwBuffer = array_mp_8188e_s_fw_nic;
				pFirmware->ulFwLength = array_length_mp_8188e_s_fw_nic;
			} else
#endif
			{
				pFirmware->szFwBuffer = array_mp_8188e_t_fw_nic;
				pFirmware->ulFwLength = array_length_mp_8188e_t_fw_nic;
			}
			RTW_INFO("%s fw:%s, size: %d\n", __FUNCTION__, "NIC", pFirmware->ulFwLength);
		}
		break;
	}

	tmp_fw_len = IS_VENDOR_8188E_I_CUT_SERIES(padapter) ? FW_8188E_SIZE_2 : FW_8188E_SIZE;

	if ((pFirmware->ulFwLength - 32) > tmp_fw_len) {
		rtStatus = _FAIL;
		RTW_ERR("Firmware size:%u exceed %u\n", pFirmware->ulFwLength, tmp_fw_len);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	/* To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (PRT_8188E_FIRMWARE_HDR)pFirmwareBuf;

	pHalData->firmware_version =  le16_to_cpu(pFwHdr->Version);
	pHalData->firmware_sub_version = pFwHdr->Subversion;
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	RTW_INFO("%s: fw_ver=%x fw_subver=%04x sig=0x%x, Month=%02x, Date=%02x, Hour=%02x, Minute=%02x\n",
		__FUNCTION__, pHalData->firmware_version, pHalData->firmware_sub_version, pHalData->FirmwareSignature
		 , pFwHdr->Month, pFwHdr->Date, pFwHdr->Hour, pFwHdr->Minute);

	if (IS_FW_HEADER_EXIST_88E(pFwHdr)) {
		/* Shift 32 bytes for FW header */
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	/* Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/* or it will cause download Fw fail. 2010.02.01. by tynli. */
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(padapter);
	}

	_FWDownloadEnable_8188E(padapter, _TRUE);
	fwdl_start_time = rtw_get_current_time();
	while (!RTW_CANNOT_IO(padapter)
	       && (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500)) {
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL) | FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable_8188E(padapter, _FALSE);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo(padapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

fwdl_stat:
	RTW_INFO("FWDL %s. write_fw:%u, %dms\n"
		 , (rtStatus == _SUCCESS) ? "success" : "fail"
		 , write_fw
		 , rtw_get_passing_time_ms(fwdl_start_time)
		);

exit:
	if (pFirmware)
		rtw_mfree((u8 *)pFirmware, sizeof(RT_FIRMWARE_8188E));

	rtl8188e_InitializeFirmwareVars(padapter);

	return rtStatus;
}

void rtl8188e_InitializeFirmwareVars(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	/* Init Fw LPS related. */
	pwrpriv->bFwCurrentInPSMode = _FALSE;

	/* Init H2C cmd. */
	rtw_write8(padapter, REG_HMETFR, 0x0f);

	/* Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
}

/* ***********************************************************
 *				Efuse related code
 * *********************************************************** */
enum {
	VOLTAGE_V25						= 0x03,
	LDOE25_SHIFT						= 28 ,
};

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
		PADAPTER		pAdapter,
		u8				efuseType,
		u16				*pAddr,
		PPGPKT_STRUCT	pTargetPkt,
		BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
		PADAPTER		pAdapter,
		u8				efuseType,
		u16				*pAddr,
		PPGPKT_STRUCT	pTargetPkt,
		BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWriteData(
		PADAPTER		pAdapter,
		u8				efuseType,
		u16				*pAddr,
		PPGPKT_STRUCT	pTargetPkt,
		BOOLEAN			bPseudoTest);

static void
hal_EfusePowerSwitch_RTL8188E(
		PADAPTER	pAdapter,
		u8		bWrite,
		u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == _TRUE) {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);
#if 0
		/* 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid */
		tmpV16 = rtw_read16(pAdapter, REG_SYS_ISO_CTRL);
		if (!(tmpV16 & PWC_EV12V)) {
			tmpV16 |= PWC_EV12V ;
			rtw_write16(pAdapter, REG_SYS_ISO_CTRL, tmpV16);
		}
#endif
		/* Reset: 0x0000h[28], default valid */
		tmpV16 =  rtw_read16(pAdapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/* Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(pAdapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M) ;
			rtw_write16(pAdapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == _TRUE) {
			/* Enable LDO 2.5V before read/write action */
			tempval = rtw_read8(pAdapter, EFUSE_TEST + 3);
			if (IS_VENDOR_8188E_I_CUT_SERIES(pAdapter)) {
				tempval &= 0x87;
				tempval |= 0x38; /* 0x34[30:27] = 0b'0111,  Use LDO 2.25V, Suggested by SD1 Pisa */
			} else {
				tempval &= 0x0F;
				tempval |= (VOLTAGE_V25 << 4);
			}
			rtw_write8(pAdapter, EFUSE_TEST + 3, (tempval | 0x80));
		}
	} else {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite == _TRUE) {
			/* Disable LDO 2.5V after read/write action */
			tempval = rtw_read8(pAdapter, EFUSE_TEST + 3);
			rtw_write8(pAdapter, EFUSE_TEST + 3, (tempval & 0x7F));
		}
	}
}

static void
rtl8188e_EfusePowerSwitch(
		PADAPTER	pAdapter,
		u8		bWrite,
		u8		PwrState)
{
	hal_EfusePowerSwitch_RTL8188E(pAdapter, bWrite, PwrState);
}

static void
Hal_EfuseReadEFuse88E(
	PADAPTER		Adapter,
	u16			_offset,
	u16			_size_byte,
	u8		*pbuf,
		BOOLEAN	bPseudoTest
)
{
	/* u8	efuseTbl[EFUSE_MAP_LEN_88E]; */
	u8	*efuseTbl = NULL;
	u8	rtemp8[1];
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	/* u16	eFuseWord[EFUSE_MAX_SECTION_88E][EFUSE_MAX_WORD_UNIT]; */
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;

	/*  */
	/* Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/*  */
	if ((_offset + _size_byte) > EFUSE_MAP_LEN_88E) {
		/* total E-Fuse table is 512bytes */
		RTW_INFO("Hal_EfuseReadEFuse88E(): Invalid offset(%#x) with read bytes(%#x)!!\n", _offset, _size_byte);
		goto exit;
	}

	efuseTbl = (u8 *)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord = (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if (eFuseWord == NULL) {
		RTW_INFO("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	/* 0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/* 1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
	if (*rtemp8 != 0xFF) {
		efuse_utilized++;
		/* RTW_INFO("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8); */
		eFuse_Addr++;
	} else {
		RTW_INFO("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8);
		goto exit;
	}


	/*  */
	/* 2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8)); */

		/* Check PG header for section num. */
		if ((*rtemp8 & 0x1F) == 0x0F) {	/* extended header */
			u1temp = ((*rtemp8 & 0xE0) >> 5);
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0)); */

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x\n", u1temp)); */

			ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);

			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	 */

			if ((*rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);

				if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_88E) {
			/* Get word enable value from PG header */
			/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren)); */

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr)); */
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);


					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;

					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr)); */
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				 */

					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}

				wren >>= 1;

			}
		} else { /* deal with error offset,skip error data		 */
			RTW_PRINT("invalid offset:0x%02x\n", offset);
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section				 */
				if (!(wren & 0x01)) {
					eFuse_Addr++;
					efuse_utilized++;
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
					eFuse_Addr++;
					efuse_utilized++;
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}
			}
		}
		/* Read next PG header */
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
		/* RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8)); */

		if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  */
	/* 3. Collect 16 sections and 4 word unit into Efuse map. */
	/*  */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	/*  */
	/* 4. Copy from Efuse map to output pointer memory!!! */
	/*  */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  */
	/* 5. Calculate Efuse utilization. */
	/*  */
	efuse_usage = (u8)((eFuse_Addr * 100) / EFUSE_REAL_CONTENT_LEN_88E);
	rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if (eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

static void
ReadEFuseByIC(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16		_size_byte,
	u8	*pbuf,
	BOOLEAN	bPseudoTest
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
#ifdef DBG_IOL_READ_EFUSE_MAP
	u8 logical_map[512];
#endif

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if (!bPseudoTest && Adapter->registrypriv.mp_mode == 0) { /* && rtw_IOL_applied(Adapter)) */
		int ret = _FAIL;
		if (rtw_IOL_applied(Adapter)) {
			rtw_hal_power_on(Adapter);

			iol_mode_enable(Adapter, 1);
#ifdef DBG_IOL_READ_EFUSE_MAP
			iol_read_efuse(Adapter, 0, _offset, _size_byte, logical_map);
#else
			ret = iol_read_efuse(Adapter, 0, _offset, _size_byte, pbuf);
#endif
			iol_mode_enable(Adapter, 0);

			if (_SUCCESS == ret)
				goto exit;
		}
	}
#endif
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);

exit:

#ifdef DBG_IOL_READ_EFUSE_MAP
	if (_rtw_memcmp(logical_map, pHalData->efuse_eeprom_data, 0x130) == _FALSE) {
		int i;
		RTW_INFO("%s compare first 0x130 byte fail\n", __FUNCTION__);
		for (i = 0; i < 512; i++) {
			if (i % 16 == 0)
				RTW_INFO("0x%03x: ", i);
			RTW_INFO("%02x ", logical_map[i]);
			if (i % 16 == 15)
				RTW_INFO("\n");
		}
		RTW_INFO("\n");
	}
#endif

	return;
}

static void
ReadEFuse_Pseudo(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16		_size_byte,
	u8	*pbuf,
	BOOLEAN	bPseudoTest
)
{
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static void
rtl8188e_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16		_size_byte,
	u8	*pbuf,
	BOOLEAN	bPseudoTest
)
{
	if (bPseudoTest)
		ReadEFuse_Pseudo(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	else
		ReadEFuseByIC(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
}

/* Do not support BT */
void
Hal_EFUSEGetEfuseDefinition88E(
			PADAPTER	pAdapter,
			u8		efuseType,
			u8		type,
			void		*pOut
)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION: {
		u8	*pMax_section;
		pMax_section = (u8 *)pOut;
		*pMax_section = EFUSE_MAX_SECTION_88E;
	}
	break;
	case TYPE_EFUSE_REAL_CONTENT_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_EFUSE_CONTENT_LEN_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_EFUSE_MAP_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)EFUSE_MAP_LEN_88E;
	}
	break;
	case TYPE_EFUSE_PROTECT_BYTES_BANK: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	default: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
	}
	break;
	}
}
void
Hal_EFUSEGetEfuseDefinition_Pseudo88E(
			PADAPTER	pAdapter,
			u8			efuseType,
			u8			type,
			void			*pOut
)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION: {
		u8		*pMax_section;
		pMax_section = (u8 *)pOut;
		*pMax_section = EFUSE_MAX_SECTION_88E;
	}
	break;
	case TYPE_EFUSE_REAL_CONTENT_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_EFUSE_CONTENT_LEN_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_BANK: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	case TYPE_EFUSE_MAP_LEN: {
		u16 *pu2Tmp;
		pu2Tmp = (u16 *)pOut;
		*pu2Tmp = (u16)EFUSE_MAP_LEN_88E;
	}
	break;
	case TYPE_EFUSE_PROTECT_BYTES_BANK: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
	}
	break;
	default: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
	}
	break;
	}
}


static void
rtl8188e_EFUSE_GetEfuseDefinition(
			PADAPTER	pAdapter,
			u8		efuseType,
			u8		type,
			void		*pOut,
			BOOLEAN		bPseudoTest
)
{
	if (bPseudoTest)
		Hal_EFUSEGetEfuseDefinition_Pseudo88E(pAdapter, efuseType, type, pOut);
	else
		Hal_EFUSEGetEfuseDefinition88E(pAdapter, efuseType, type, pOut);
}

static u8
Hal_EfuseWordEnableDataWrite(PADAPTER	pAdapter,
					u16		efuse_addr,
					u8		word_en,
					u8		*data,
					BOOLEAN		bPseudoTest)
{
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8];

	_rtw_memset((void *)tmpdata, 0xff, PGPKT_DATA_SIZE);

	if (!(word_en & BIT0)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[1], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[1], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[0] != tmpdata[0]) || (data[1] != tmpdata[1]))
			badworden &= (~BIT0);
	}
	if (!(word_en & BIT1)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[3], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr    , &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[3], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[2] != tmpdata[2]) || (data[3] != tmpdata[3]))
			badworden &= (~BIT1);
	}
	if (!(word_en & BIT2)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[5], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[5], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[4] != tmpdata[4]) || (data[5] != tmpdata[5]))
			badworden &= (~BIT2);
	}
	if (!(word_en & BIT3)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(pAdapter, start_addr++, data[7], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

		efuse_OneByteRead(pAdapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(pAdapter, tmpaddr + 1, &tmpdata[7], bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

		if ((data[6] != tmpdata[6]) || (data[7] != tmpdata[7]))
			badworden &= (~BIT3);
	}
	return badworden;
}

static u8
Hal_EfuseWordEnableDataWrite_Pseudo(PADAPTER	pAdapter,
						u16		efuse_addr,
						u8		word_en,
						u8		*data,
						BOOLEAN		bPseudoTest)
{
	u8	ret = 0;

	ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}

static u8
rtl8188e_Efuse_WordEnableDataWrite(PADAPTER	pAdapter,
					u16		efuse_addr,
					u8		word_en,
					u8		*data,
					BOOLEAN		bPseudoTest)
{
	u8	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfuseWordEnableDataWrite_Pseudo(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	else
		ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}


static u16
hal_EfuseGetCurrentSize_8188e(PADAPTER	pAdapter,
						BOOLEAN			bPseudoTest)
{
	int	bContinual = _TRUE;

	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	efuse_data, word_cnts = 0;

	if (bPseudoTest)
		efuse_addr = (u16)(fakeEfuseUsedBytes);
	else
		rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), start_efuse_addr = %d\n", efuse_addr)); */

	while (bContinual &&
	       efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest) &&
	       AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		if (efuse_data != 0xFF) {
			if ((efuse_data & 0x1F) == 0x0F) {	/* extended header */
				hoffset = efuse_data;
				efuse_addr++;
				efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest);
				if ((efuse_data & 0x0F) == 0x0F) {
					efuse_addr++;
					continue;
				} else {
					hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					hworden = efuse_data & 0x0F;
				}
			} else {
				hoffset = (efuse_data >> 4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}
			word_cnts = Efuse_CalculateWordCnts(hworden);
			/* read next header */
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
		} else
			bContinual = _FALSE ;
	}

	if (bPseudoTest) {
		fakeEfuseUsedBytes = efuse_addr;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", fakeEfuseUsedBytes)); */
	} else {
		rtw_hal_set_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", efuse_addr)); */
	}

	return efuse_addr;
}

static u16
Hal_EfuseGetCurrentSize_Pseudo(PADAPTER	pAdapter,
						BOOLEAN			bPseudoTest)
{
	u16	ret = 0;

	ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);

	return ret;
}


static u16
rtl8188e_EfuseGetCurrentSize(
		PADAPTER	pAdapter,
		u8			efuseType,
		BOOLEAN		bPseudoTest)
{
	u16	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfuseGetCurrentSize_Pseudo(pAdapter, bPseudoTest);
	else
		ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);


	return ret;
}


static int
hal_EfusePgPacketRead_8188e(
		PADAPTER	pAdapter,
		u8			offset,
		u8			*data,
		BOOLEAN		bPseudoTest)
{
	u8	ReadState = PG_STATE_HEADER;

	int	bContinual = _TRUE;
	int	bDataEmpty = _TRUE ;

	u8	efuse_data, word_cnts = 0;
	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	tmpidx = 0;
	u8	tmpdata[8];
	u8	max_section = 0;
	u8	tmp_header = 0;

	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, (void *)&max_section, bPseudoTest);

	if (data == NULL)
		return _FALSE;
	if (offset > max_section)
		return _FALSE;

	_rtw_memset((void *)data, 0xff, sizeof(u8) * PGPKT_DATA_SIZE);
	_rtw_memset((void *)tmpdata, 0xff, sizeof(u8) * PGPKT_DATA_SIZE);


	/*  */
	/* <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/* Skip dummy parts to prevent unexpected data read from Efuse. */
	/* By pass right now. 2009.02.19. */
	/*  */
	while (bContinual && AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		/* -------  Header Read ------------- */
		if (ReadState & PG_STATE_HEADER) {
			if (efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
				if (EXT_HEADER(efuse_data)) {
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr , &efuse_data, bPseudoTest);
					if (!ALL_WORDS_DISABLED(efuse_data)) {
						hoffset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					} else {
						RTW_INFO("Error, All words disabled\n");
						efuse_addr++;
						continue;
					}
				} else {
					hoffset = (efuse_data >> 4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = _TRUE ;

				if (hoffset == offset) {
					for (tmpidx = 0; tmpidx < word_cnts * 2 ; tmpidx++) {
						if (efuse_OneByteRead(pAdapter, efuse_addr + 1 + tmpidx , &efuse_data, bPseudoTest)) {
							tmpdata[tmpidx] = efuse_data;
							if (efuse_data != 0xff)
								bDataEmpty = _FALSE;
						}
					}
					if (bDataEmpty == _FALSE)
						ReadState = PG_STATE_DATA;
					else { /* read next header */
						efuse_addr = efuse_addr + (word_cnts * 2) + 1;
						ReadState = PG_STATE_HEADER;
					}
				} else { /* read next header */
					efuse_addr = efuse_addr + (word_cnts * 2) + 1;
					ReadState = PG_STATE_HEADER;
				}

			} else
				bContinual = _FALSE ;
		}
		/* -------  Data section Read ------------- */
		else if (ReadState & PG_STATE_DATA) {
			efuse_WordEnableDataRead(hworden, tmpdata, data);
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
			ReadState = PG_STATE_HEADER;
		}

	}

	if ((data[0] == 0xff) && (data[1] == 0xff) && (data[2] == 0xff)  && (data[3] == 0xff) &&
	    (data[4] == 0xff) && (data[5] == 0xff) && (data[6] == 0xff)  && (data[7] == 0xff))
		return _FALSE;
	else
		return _TRUE;

}

static int
Hal_EfusePgPacketRead(PADAPTER	pAdapter,
				u8			offset,
				u8			*data,
				BOOLEAN			bPseudoTest)
{
	int	ret = 0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);


	return ret;
}

static int
Hal_EfusePgPacketRead_Pseudo(PADAPTER	pAdapter,
					u8			offset,
					u8			*data,
					BOOLEAN		bPseudoTest)
{
	int	ret = 0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static int
rtl8188e_Efuse_PgPacketRead(PADAPTER	pAdapter,
					u8			offset,
					u8			*data,
					BOOLEAN		bPseudoTest)
{
	int	ret = 0;

	if (bPseudoTest)
		ret = Hal_EfusePgPacketRead_Pseudo(pAdapter, offset, data, bPseudoTest);
	else
		ret = Hal_EfusePgPacketRead(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static BOOLEAN
hal_EfuseFixHeaderProcess(
			PADAPTER			pAdapter,
			u8					efuseType,
			PPGPKT_STRUCT		pFixPkt,
			u16					*pAddr,
			BOOLEAN				bPseudoTest
)
{
	u8	originaldata[8], badworden = 0;
	u16	efuse_addr = *pAddr;
	u32	PgWriteSuccess = 0;

	_rtw_memset((void *)originaldata, 0xff, 8);

	if (Efuse_PgPacketRead(pAdapter, pFixPkt->offset, originaldata, bPseudoTest)) {
		/* check if data exist */
		badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr + 1, pFixPkt->word_en, originaldata, bPseudoTest);

		if (badworden != 0xf) {	/* write fail */
			PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pFixPkt->offset, badworden, originaldata, bPseudoTest);

			if (!PgWriteSuccess)
				return _FALSE;
			else
				efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
		} else
			efuse_addr = efuse_addr + (pFixPkt->word_cnts * 2) + 1;
	} else
		efuse_addr = efuse_addr + (pFixPkt->word_cnts * 2) + 1;
	*pAddr = efuse_addr;
	return _TRUE;
}

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
				PADAPTER		pAdapter,
				u8				efuseType,
				u16				*pAddr,
				PPGPKT_STRUCT	pTargetPkt,
				BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet = _FALSE, bContinual = _TRUE;
	u16	efuse_addr = *pAddr, efuse_max_available_len = 0;
	u8	pg_header = 0, tmp_header = 0, pg_header_temp = 0;
	u8	repeatcnt = 0;

	/* RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 2byte header\n")); */
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (void *)&efuse_max_available_len, bPseudoTest);

	while (efuse_addr < efuse_max_available_len) {
		pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("pg_header = 0x%x\n", pg_header)); */
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);
		while (tmp_header == 0xFF || pg_header != tmp_header) {
			if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for pg_header!!\n")); */
				return _FALSE;
			}

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		}

		/* to write ext_header */
		if (tmp_header == pg_header) {
			efuse_addr++;
			pg_header_temp = pg_header;
			pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);
			while (tmp_header == 0xFF || pg_header != tmp_header) {
				if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for ext_header!!\n")); */
					return _FALSE;
				}

				efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
				efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			}

			if ((tmp_header & 0x0F) == 0x0F) {	/* word_en PG fail */
				if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for word_en!!\n")); */
					return _FALSE;
				} else {
					efuse_addr++;
					continue;
				}
			} else if (pg_header != tmp_header) {	/* offset PG fail */
				PGPKT_STRUCT	fixPkt;
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for offset PG fail, need to cover the existed data\n")); */
				fixPkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
				fixPkt.word_en = tmp_header & 0x0F;
				fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
				if (!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
					return _FALSE;
			} else {
				bRet = _TRUE;
				break;
			}
		} else if ((tmp_header & 0x1F) == 0x0F) {	/* wrong extended header */
			efuse_addr += 2;
			continue;
		}
	}

	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
				PADAPTER		pAdapter,
				u8				efuseType,
				u16				*pAddr,
				PPGPKT_STRUCT	pTargetPkt,
				BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet = _FALSE;
	u8	pg_header = 0, tmp_header = 0;
	u16	efuse_addr = *pAddr;
	u8	repeatcnt = 0;

	/* RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 1byte header\n")); */
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) | pTargetPkt->word_en;

	efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
	phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 0);

	efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

	phy_set_mac_reg(pAdapter, EFUSE_TEST, BIT26, 1);

	while (tmp_header == 0xFF || pg_header != tmp_header) {
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			return _FALSE;
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
	}

	if (pg_header == tmp_header)
		bRet = _TRUE;
	else {
		PGPKT_STRUCT	fixPkt;
		/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for fixed PG packet, need to cover the existed data\n")); */
		fixPkt.offset = (tmp_header >> 4) & 0x0F;
		fixPkt.word_en = tmp_header & 0x0F;
		fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
		if (!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
			return _FALSE;
	}

	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteData(
				PADAPTER		pAdapter,
				u8				efuseType,
				u16				*pAddr,
				PPGPKT_STRUCT	pTargetPkt,
				BOOLEAN			bPseudoTest)
{
	BOOLEAN	bRet = _FALSE;
	u16	efuse_addr = *pAddr;
	u8	badworden = 0;
	u32	PgWriteSuccess = 0;

	badworden = 0x0f;
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr + 1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if (badworden == 0x0F) {
		/* write ok */
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData ok!!\n")); */
		return _TRUE;
	} else {
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData Fail!!\n")); */
		/* reorganize other pg packet */

		PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

		if (!PgWriteSuccess)
			return _FALSE;
		else
			return _TRUE;
	}

	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteHeader(
				PADAPTER		pAdapter,
				u8				efuseType,
				u16				*pAddr,
				PPGPKT_STRUCT	pTargetPkt,
				BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet = _FALSE;

	if (pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
		bRet = hal_EfusePgPacketWrite2ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	else
		bRet = hal_EfusePgPacketWrite1ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);

	return bRet;
}

static BOOLEAN
wordEnMatched(
		PPGPKT_STRUCT	pTargetPkt,
		PPGPKT_STRUCT	pCurPkt,
		u8				*pWden
)
{
	u8	match_word_en = 0x0F;	/* default all words are disabled */
	u8	i;

	/* check if the same words are enabled both target and current PG packet */
	if (((pTargetPkt->word_en & BIT0) == 0) &&
	    ((pCurPkt->word_en & BIT0) == 0)) {
		match_word_en &= ~BIT0;				/* enable word 0 */
	}
	if (((pTargetPkt->word_en & BIT1) == 0) &&
	    ((pCurPkt->word_en & BIT1) == 0)) {
		match_word_en &= ~BIT1;				/* enable word 1 */
	}
	if (((pTargetPkt->word_en & BIT2) == 0) &&
	    ((pCurPkt->word_en & BIT2) == 0)) {
		match_word_en &= ~BIT2;				/* enable word 2 */
	}
	if (((pTargetPkt->word_en & BIT3) == 0) &&
	    ((pCurPkt->word_en & BIT3) == 0)) {
		match_word_en &= ~BIT3;				/* enable word 3 */
	}

	*pWden = match_word_en;

	if (match_word_en != 0xf)
		return _TRUE;
	else
		return _FALSE;
}

static BOOLEAN
hal_EfuseCheckIfDatafollowed(
			PADAPTER		pAdapter,
			u8				word_cnts,
			u16				startAddr,
			BOOLEAN			bPseudoTest
)
{
	BOOLEAN		bRet = _FALSE;
	u8	i, efuse_data;

	for (i = 0; i < (word_cnts * 2) ; i++) {
		if (efuse_OneByteRead(pAdapter, (startAddr + i) , &efuse_data, bPseudoTest) && (efuse_data != 0xFF))
			bRet = _TRUE;
	}

	return bRet;
}

static BOOLEAN
hal_EfusePartialWriteCheck(
		PADAPTER		pAdapter,
		u8				efuseType,
		u16				*pAddr,
		PPGPKT_STRUCT	pTargetPkt,
		BOOLEAN			bPseudoTest
)
{
	BOOLEAN		bRet = _FALSE;
	u8	i, efuse_data = 0, cur_header = 0;
	u8	new_wden = 0, matched_wden = 0, badworden = 0;
	u16	startAddr = 0, efuse_max_available_len = 0, efuse_max = 0;
	PGPKT_STRUCT	curPkt;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (void *)&efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&efuse_max, bPseudoTest);

	if (efuseType == EFUSE_WIFI) {
		if (bPseudoTest)
			startAddr = (u16)(fakeEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
		else {
			rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&startAddr);
			startAddr %= EFUSE_REAL_CONTENT_LEN;
		}
	} else {
		if (bPseudoTest)
			startAddr = (u16)(fakeBTEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
		else
			startAddr = (u16)(BTEfuseUsedBytes % EFUSE_REAL_CONTENT_LEN);
	}
	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePartialWriteCheck(), startAddr=%d\n", startAddr)); */

	while (1) {
		if (startAddr >= efuse_max_available_len) {
			bRet = _FALSE;
			break;
		}

		if (efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
			if (EXT_HEADER(efuse_data)) {
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuse_data)) {
					/* RTPRINT(FEEPROM, EFUSE_PG, ("Error condition, all words disabled")); */
					bRet = _FALSE;
					break;
				} else {
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			} else {
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header >> 4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			/* if same header is found but no data followed */
			/* write some part of data followed by the header. */
			if ((curPkt.offset == pTargetPkt->offset) &&
			    (!hal_EfuseCheckIfDatafollowed(pAdapter, curPkt.word_cnts, startAddr + 1, bPseudoTest)) &&
			    wordEnMatched(pTargetPkt, &curPkt, &matched_wden)) {
				/* RTPRINT(FEEPROM, EFUSE_PG, ("Need to partial write data by the previous wrote header\n")); */
				/* Here to write partial data */
				badworden = Efuse_WordEnableDataWrite(pAdapter, startAddr + 1, matched_wden, pTargetPkt->data, bPseudoTest);
				if (badworden != 0x0F) {
					u32	PgWriteSuccess = 0;
					/* if write fail on some words, write these bad words again */

					PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if (!PgWriteSuccess) {
						bRet = _FALSE;	/* write fail, return */
						break;
					}
				}
				/* partial write ok, update the target packet for later use */
				for (i = 0; i < 4; i++) {
					if ((matched_wden & (0x1 << i)) == 0) {	/* this word has been written */
						pTargetPkt->word_en |= (0x1 << i);	/* disable the word */
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			/* read from next header */
			startAddr = startAddr + (curPkt.word_cnts * 2) + 1;
		} else {
			/* not used header, 0xff */
			*pAddr = startAddr;
			/* RTPRINT(FEEPROM, EFUSE_PG, ("Started from unused header offset=%d\n", startAddr)); */
			bRet = _TRUE;
			break;
		}
	}
	return bRet;
}

static BOOLEAN
hal_EfusePgCheckAvailableAddr(
		PADAPTER	pAdapter,
		u8			efuseType,
		BOOLEAN		bPseudoTest
)
{
	u16	efuse_max_available_len = 0;

	/* Change to check TYPE_EFUSE_MAP_LEN ,beacuse 8188E raw 256,logic map over 256. */
	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&efuse_max_available_len, _FALSE);

	/* EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&efuse_max_available_len, bPseudoTest); */
	/* RTPRINT(FEEPROM, EFUSE_PG, ("efuse_max_available_len = %d\n", efuse_max_available_len)); */

	if (Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= efuse_max_available_len) {
		/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgCheckAvailableAddr error!!\n")); */
		return _FALSE;
	}
	return _TRUE;
}

static void
hal_EfuseConstructPGPkt(
		u8				offset,
		u8				word_en,
		u8				*pData,
		PPGPKT_STRUCT	pTargetPkt

)
{
	_rtw_memset((void *)pTargetPkt->data, 0xFF, sizeof(u8) * 8);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en = word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);

	/* RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseConstructPGPkt(), targetPkt, offset=%d, word_en=0x%x, word_cnts=%d\n", pTargetPkt->offset, pTargetPkt->word_en, pTargetPkt->word_cnts)); */
}



static BOOLEAN
hal_EfusePgPacketWrite_8188e(
		PADAPTER		pAdapter,
		u8			offset,
		u8			word_en,
		u8			*pData,
		BOOLEAN		bPseudoTest
)
{
	PGPKT_STRUCT	targetPkt;
	u16			startAddr = 0;
	u8			efuseType = EFUSE_WIFI;

	if (!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}


static int
Hal_EfusePgPacketWrite_Pseudo(PADAPTER	pAdapter,
					u8			offset,
					u8			word_en,
					u8			*data,
					BOOLEAN		bPseudoTest)
{
	int ret;

	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

static int
Hal_EfusePgPacketWrite(PADAPTER	pAdapter,
				u8			offset,
				u8			word_en,
				u8			*data,
				BOOLEAN		bPseudoTest)
{
	int	ret = 0;
	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);


	return ret;
}

static int
rtl8188e_Efuse_PgPacketWrite(PADAPTER	pAdapter,
					u8			offset,
					u8			word_en,
					u8			*data,
					BOOLEAN		bPseudoTest)
{
	int	ret;

	if (bPseudoTest)
		ret = Hal_EfusePgPacketWrite_Pseudo(pAdapter, offset, word_en, data, bPseudoTest);
	else
		ret = Hal_EfusePgPacketWrite(pAdapter, offset, word_en, data, bPseudoTest);
	return ret;
}

static void read_chip_version_8188e(PADAPTER padapter)
{
	u32				value32;
	HAL_DATA_TYPE	*pHalData;

	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	pHalData->version_id.ICType = CHIP_8188E;
	pHalData->version_id.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	pHalData->version_id.RFType = RF_TYPE_1T1R;
	pHalData->version_id.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	pHalData->version_id.CUTVersion = (value32 & CHIP_VER_RTL_MASK) >> CHIP_VER_RTL_SHIFT; /* IC version (CUT) */

	/* For regulator mode. by tynli. 2011.01.14 */
	pHalData->RegulatorMode = ((value32 & TRP_BT_EN) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	pHalData->version_id.ROMVer = 0;	/* ROM code version.	 */
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;

	rtw_hal_config_rftype(padapter);

#if 1
	dump_chip_info(pHalData->version_id);
#endif

}

void rtl8188e_start_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	if (xmitpriv->SdioXmitThread == NULL) {
		RTW_INFO(FUNC_ADPT_FMT " start RTWHALXT\n", FUNC_ADPT_ARG(padapter));
		xmitpriv->SdioXmitThread = kthread_run(rtl8188es_xmit_thread, padapter, "RTWHALXT");
		if (IS_ERR(xmitpriv->SdioXmitThread)) {
			RTW_ERR("%s: start rtl8188es_xmit_thread FAIL!!\n", __func__);
			xmitpriv->SdioXmitThread = NULL;
		}
	}
#endif
#endif
}

void rtl8188e_stop_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	/* stop xmit_buf_thread */
	if (xmitpriv->SdioXmitThread) {
		_rtw_up_sema(&xmitpriv->SdioXmitSema);
		rtw_thread_stop(xmitpriv->SdioXmitThread);
		xmitpriv->SdioXmitThread = NULL;
	}
#endif
#endif
}
void hal_notch_filter_8188e(_adapter *adapter, bool enable)
{
	if (enable) {
		RTW_INFO("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) | BIT1);
	} else {
		RTW_INFO("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) & ~BIT1);
	}
}

void init_hal_spec_8188e(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	hal_spec->ic_name = "rtl8188e";
	hal_spec->macid_num = 64;
	hal_spec->sec_cam_ent_num = 32;
	hal_spec->sec_cap = 0;
	hal_spec->macid_cap = MACID_DROP;

	hal_spec->rfpath_num_2g = 1;
	hal_spec->rfpath_num_5g = 0;
	hal_spec->txgi_max = 63;
	hal_spec->txgi_pdbm = 2;
	hal_spec->max_tx_cnt = 1;
	hal_spec->tx_nss_num = 1;
	hal_spec->rx_nss_num = 1;
	hal_spec->band_cap = BAND_CAP_2G;
	hal_spec->bw_cap = BW_CAP_20M | BW_CAP_40M;
	hal_spec->port_num = 2;
	hal_spec->proto_cap = PROTO_CAP_11B | PROTO_CAP_11G | PROTO_CAP_11N;
	hal_spec->wl_func = 0
			    | WL_FUNC_P2P
			    | WL_FUNC_MIRACAST
			    | WL_FUNC_TDLS
			    ;

#if CONFIG_TX_AC_LIFETIME
	hal_spec->tx_aclt_unit_factor = 1;
#endif

	hal_spec->pg_txpwr_saddr = 0x10;
	hal_spec->pg_txgi_diff_factor = 1;

	rtw_macid_ctl_init_sleep_reg(adapter_to_macidctl(adapter)
		, REG_MACID_PAUSE_0
		, REG_MACID_PAUSE_1, 0, 0);
	rtw_macid_ctl_init_drop_reg(adapter_to_macidctl(adapter)
		, REG_MACID_NO_LINK_0
		, REG_MACID_NO_LINK_1
		, 0, 0);

}

#ifdef CONFIG_RFKILL_POLL
bool rtl8188e_gpio_radio_on_off_check(_adapter *adapter, u8 *valid)
{
	u32 tmp32;
	bool ret;

#ifdef CONFIG_PCI_HCI
#if 1
	*valid = 0;
	return _FALSE; /* unblock */
#else
	tmp32  = rtw_read32(adapter, REG_GSSR);
	ret = (tmp32 & BIT(31)) ? _FALSE : _TRUE;	/* Power down pin output value, low active */
	*valid = 1;

	return ret;
#endif
#else
	*valid = 0;
	return _FALSE; /* unblock */
#endif
}
#endif


void InitBeaconParameters_8188e(_adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);

	rtw_write16(adapter, REG_BCN_CTRL, (DIS_TSF_UDT << 8) | DIS_TSF_UDT);

	/* TBTT setup time */
	rtw_write8(adapter, REG_TBTT_PROHIBIT, TBTT_PROHIBIT_SETUP_TIME);

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(adapter, REG_TBTT_PROHIBIT + 1, TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(adapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(adapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	rtw_write8(adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8188E); /* 5ms */
	rtw_write8(adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8188E); /* 2ms */

	/* Suggested by designer timchen. Change beacon AIFS to the largest number */
	/* beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(adapter, REG_BCNTCFG, 0x4413);

}

static void
_BeaconFunctionEnable(
		PADAPTER		padapter,
		BOOLEAN			Enable,
		BOOLEAN			Linked
)
{
	rtw_write8(padapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB));

	rtw_write8(padapter, REG_RD_CTRL + 1, 0x6F);
}

void SetBeaconRelatedRegisters8188E(PADAPTER padapter)
{
	u32	value32;
	/* HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter); */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bcn_ctrl_reg		= REG_BCN_CTRL;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* REG_MBSSID_BCN_SPACE */
	/* REG_BCNDMATIM */
	/* REG_ATIMWND */
	/* REG_TBTT_PROHIBIT */
	/* REG_DRVERLYINT */
	/* REG_BCN_MAX_ERR */
	/* REG_BCNTCFG */ /* (0x510) */
	/* REG_DUAL_TSF_RST */
	/* REG_BCN_CTRL */ /* (0x550) */


#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
#endif
	/*  */
	/* ATIM window */
	/*  */
	rtw_write16(padapter, REG_ATIMWND, 2);

	/*  */
	/* Beacon interval (in unit of TU). */
	/*  */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)&pmlmeinfo->bcn_interval);

	InitBeaconParameters_8188e(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	/*  */
	/* Force beacon frame transmission even after receiving beacon frame from other ad hoc STA */
	/*  */
	/* PlatformEFIOWrite1Byte(Adapter, BCN_ERR_THRESH, 0x0a); */ /* We force beacon sent to prevent unexpect disconnect status in Ad hoc mode */

	/*  */
	/* Reset TSF Timer to zero, added by Roger. 2008.06.24 */
	/*  */
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/* TODO: Modify later (Find the right parameters) */
	/* NOTE: Fix test chip's bug (about contention windows's randomness) */
	if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE | WIFI_AP_STATE | WIFI_MESH_STATE) == _TRUE) {
		rtw_write8(padapter, REG_RXTSF_OFFSET_CCK, 0x50);
		rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);
	}

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);
	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg) | DIS_BCNQ_SUB);
}

void rtl8188e_read_wmmedca_reg(PADAPTER adapter, u16 *vo_params, u16 *vi_params, u16 *be_params, u16 *bk_params)
{
	u8 vo_reg_params[4];
	u8 vi_reg_params[4];
	u8 be_reg_params[4];
	u8 bk_reg_params[4];

	GetHwReg8188E(adapter, HW_VAR_AC_PARAM_VO, vo_reg_params);
	GetHwReg8188E(adapter, HW_VAR_AC_PARAM_VI, vi_reg_params);
	GetHwReg8188E(adapter, HW_VAR_AC_PARAM_BE, be_reg_params);
	GetHwReg8188E(adapter, HW_VAR_AC_PARAM_BK, bk_reg_params);

	vo_params[0] = vo_reg_params[0];
	vo_params[1] = vo_reg_params[1] & 0x0F;
	vo_params[2] = (vo_reg_params[1] & 0xF0) >> 4;
	vo_params[3] = ((vo_reg_params[3] << 8) | (vo_reg_params[2])) * 32;

	vi_params[0] = vi_reg_params[0];
	vi_params[1] = vi_reg_params[1] & 0x0F;
	vi_params[2] = (vi_reg_params[1] & 0xF0) >> 4;
	vi_params[3] = ((vi_reg_params[3] << 8) | (vi_reg_params[2])) * 32;

	be_params[0] = be_reg_params[0];
	be_params[1] = be_reg_params[1] & 0x0F;
	be_params[2] = (be_reg_params[1] & 0xF0) >> 4;
	be_params[3] = ((be_reg_params[3] << 8) | (be_reg_params[2])) * 32;

	bk_params[0] = bk_reg_params[0];
	bk_params[1] = bk_reg_params[1] & 0x0F;
	bk_params[2] = (bk_reg_params[1] & 0xF0) >> 4;
	bk_params[3] = ((bk_reg_params[3] << 8) | (bk_reg_params[2])) * 32;

	vo_params[1] = (1 << vo_params[1]) - 1;
	vo_params[2] = (1 << vo_params[2]) - 1;
	vi_params[1] = (1 << vi_params[1]) - 1;
	vi_params[2] = (1 << vi_params[2]) - 1;
	be_params[1] = (1 << be_params[1]) - 1;
	be_params[2] = (1 << be_params[2]) - 1;
	bk_params[1] = (1 << bk_params[1]) - 1;
	bk_params[2] = (1 << bk_params[2]) - 1;
}

void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->dm_init = &rtl8188e_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8188e_deinit_dm_priv;

	pHalFunc->read_chip_version = read_chip_version_8188e;

	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8188E;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8188E;
	pHalFunc->set_tx_power_index_handler = PHY_SetTxPowerIndex_8188E;
	pHalFunc->get_tx_power_index_handler = &PHY_GetTxPowerIndex_8188E;

	pHalFunc->hal_dm_watchdog = &rtl8188e_HalDmWatchDog;

	pHalFunc->run_thread = &rtl8188e_start_thread;
	pHalFunc->cancel_thread = &rtl8188e_stop_thread;

	pHalFunc->read_bbreg = &PHY_QueryBBReg8188E;
	pHalFunc->write_bbreg = &PHY_SetBBReg8188E;
	pHalFunc->read_rfreg = &PHY_QueryRFReg8188E;
	pHalFunc->write_rfreg = &PHY_SetRFReg8188E;

	pHalFunc->read_wmmedca_reg = &rtl8188e_read_wmmedca_reg;
	
	/* Efuse related function */
	pHalFunc->EfusePowerSwitch = &rtl8188e_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8188e_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8188e_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8188e_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8188e_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8188e_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8188e_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8188e_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8188e_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress = &sreset_inprogress;
#endif /* DBG_CONFIG_ERROR_DETECT */

	pHalFunc->GetHalODMVarHandler = GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = SetHalODMVar;

#ifdef CONFIG_IOL
	pHalFunc->IOL_exec_cmds_sync = &rtl8188e_IOL_exec_cmds_sync;
#endif

	pHalFunc->hal_notch_filter = &hal_notch_filter_8188e;
	pHalFunc->fill_h2c_cmd = &FillH2CCmd_88E;
	pHalFunc->fill_fake_txdesc = &rtl8188e_fill_fake_txdesc;
	pHalFunc->fw_dl = &rtl8188e_FirmwareDownload;
	pHalFunc->hal_get_tx_buff_rsvd_page_num = &GetTxBufferRsvdPageNum8188E;

#ifdef CONFIG_GPIO_API
        pHalFunc->hal_gpio_func_check = &rtl8188e_GpioFuncCheck;
#endif
#ifdef CONFIG_RFKILL_POLL
	pHalFunc->hal_radio_onoff_check = rtl8188e_gpio_radio_on_off_check;
#endif
}

u8 GetEEPROMSize8188E(PADAPTER padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	/* 6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	RTW_INFO("EEPROM type is %s\n", size == 4 ? "E-FUSE" : "93C46");

	return size;
}

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_PCI_HCI) || defined(CONFIG_GSPI_HCI)
/* -------------------------------------------------------------------------
 *
 * LLT R/W/Init function
 *
 * ------------------------------------------------------------------------- */
s32 _LLTWrite(PADAPTER padapter, u32 address, u32 data)
{
	s32	status = _SUCCESS;
	s8	count = POLLING_LLT_THRESHOLD;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(padapter, REG_LLT_INIT, value);

	/* polling */
	do {
		value = rtw_read32(padapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;
	} while (--count);

	if (count <= 0) {
		RTW_INFO("Failed to polling write LLT done at address %d!\n", address);
		status = _FAIL;
	}

	return status;
}

u8 _LLTRead(PADAPTER padapter, u32 address)
{
	s32	count = POLLING_LLT_THRESHOLD;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);
	u16	LLTReg = REG_LLT_INIT;


	rtw_write32(padapter, LLTReg, value);

	/* polling and get value */
	do {
		value = rtw_read32(padapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			return (u8)value;
	} while (--count);




	return 0xFF;
}

s32 InitLLTTable(PADAPTER padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER_8188E(padapter);/* 176, 22k */
	HAL_DATA_TYPE *pHalData	= GET_HAL_DATA(padapter);

#if defined(CONFIG_IOL_LLT)
	if (rtw_IOL_applied(padapter))
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);
	else
#endif
	{
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite(padapter, i, i + 1);
			if (_SUCCESS != status)
				return status;
		}

		/* end of list */
		status = _LLTWrite(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (_SUCCESS != status)
			return status;

		/* Make the other pages as ring buffer */
		/* This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer. */
		/* Otherwise used as local loopback buffer. */
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite(padapter, i, (i + 1));
			if (_SUCCESS != status)
				return status;
		}

		/* Let last entry point to the start entry of ring buffer */
		status = _LLTWrite(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (_SUCCESS != status)
			return status;
	}

	return status;
}
#endif


void
Hal_InitPGData88E(PADAPTER	padapter)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32			i;
	u16			value16;

	if (_FALSE == pHalData->bautoload_fail_flag) {
		/* autoload OK. */
		if (is_boot_from_eeprom(padapter)) {
			/* Read all Content from EEPROM or EFUSE. */
			for (i = 0; i < HWSET_MAX_SIZE; i += 2) {
				/*				value16 = EF2Byte(ReadEEprom(pAdapter, (u16) (i>>1)));
				 *				*((u16*)(&PROMContent[i])) = value16; */
			}
		} else {
			/* Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
		}
	} else {
		/* autoload fail */
		/*		pHalData->AutoloadFailFlag = _TRUE; */
		/* update to default value 0xFF */
		if (!is_boot_from_eeprom(padapter))
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
	}

#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(padapter) == _FALSE) {
		if (Hal_readPGDataFromConfigFile(padapter) != _SUCCESS)
			RTW_ERR("invalid phy efuse and read from file fail, will use driver default!!\n");
	}
#endif
}

void
Hal_EfuseParseIDCode88E(
		PADAPTER	padapter,
		u8			*hwinfo
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u16			EEPROMId;


	/* Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((u16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		RTW_INFO("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pHalData->bautoload_fail_flag = _TRUE;
	} else
		pHalData->bautoload_fail_flag = _FALSE;

	RTW_INFO("EEPROM ID=0x%04x\n", EEPROMId);
}

void Hal_ReadPowerSavingMode88E(
	PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 tmpvalue;

	if (AutoLoadFail) {
		pwrctl->bHWPowerdown = _FALSE;
		pwrctl->bSupportRemoteWakeup = _FALSE;
	} else	{

		/* hw power down mode selection , 0:rf-off / 1:power down */

		if (padapter->registrypriv.hwpdn_mode == 2)
			pwrctl->bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & BIT4);
		else
			pwrctl->bHWPowerdown = padapter->registrypriv.hwpdn_mode;

		/* decide hw if support remote wakeup function */
		/* if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume */
#ifdef CONFIG_USB_HCI
		pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1) ? _TRUE : _FALSE;
#endif /* CONFIG_USB_HCI */

		RTW_INFO("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n", __FUNCTION__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		RTW_INFO("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n", padapter->registrypriv.power_mgnt, padapter->registrypriv.usbss_enable);

	}

}

void
Hal_ReadTxPowerInfo88E(
		PADAPTER		padapter,
		u8				*PROMContent,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	TxPowerInfo24G pwrInfo24G;
	
	hal_load_txpwr_info(padapter, &pwrInfo24G, NULL, PROMContent);

	/* 2010/10/19 MH Add Regulator recognize for EU. */
	if (!AutoLoadFail) {
		struct registry_priv  *registry_par = &padapter->registrypriv;

		if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION & 0x7);	/* bit0~2 */
		else
			pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x7);	/* bit0~2 */

	} else
		pHalData->EEPROMRegulatory = 0;
	RTW_INFO("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);

}


void
Hal_EfuseParseXtal_8188E(
		PADAPTER		pAdapter,
		u8			*hwinfo,
		BOOLEAN		AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->crystal_cap = hwinfo[EEPROM_XTAL_88E];
		if (pHalData->crystal_cap == 0xFF)
			pHalData->crystal_cap = EEPROM_Default_CrystalCap_88E;
	} else
		pHalData->crystal_cap = EEPROM_Default_CrystalCap_88E;
	RTW_INFO("crystal_cap: 0x%2x\n", pHalData->crystal_cap);
}

void
Hal_ReadPAType_8188E(
		PADAPTER	Adapter,
		u8			*PROMContent,
		BOOLEAN		AutoloadFail
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8			PA_LNAType_2G = 0;

	if (!AutoloadFail) {
		if (GetRegAmplifierType2G(Adapter) == 0) { /* AUTO*/

			/* PA & LNA Type */
			PA_LNAType_2G = LE_BITS_TO_1BYTE(&PROMContent[EEPROM_RFE_OPTION_8188E], 2, 2); /* 0xCA[3:2] */
			/*
				ePA/eLNA sel.(ePA+eLNA=0x0, ePA+iLNA enable = 0x1, iPA+eLNA enable =0x2, iPA+iLNA=0x3)
			*/
			switch (PA_LNAType_2G) {
			case 0:
				pHalData->ExternalPA_2G  = 1;
				pHalData->ExternalLNA_2G = 1;
				break;
			case 1:
				pHalData->ExternalPA_2G  = 1;
				pHalData->ExternalLNA_2G = 0;
				break;
			case 2:
				pHalData->ExternalPA_2G  = 0;
				pHalData->ExternalLNA_2G = 1;
				break;
			case 3:
			default:
				pHalData->ExternalPA_2G  = 0;
				pHalData->ExternalLNA_2G = 0;
				break;
			}
		} else {
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}
#if 0
		if (GetRegAmplifierType5G(Adapter) == 0) { /*  AUTO */
			pHalData->external_pa_5g = ((pHalData->PAType_5G & BIT1) && (pHalData->PAType_5G & BIT0)) ? 1 : 0;
			pHalData->external_lna_5g = ((pHalData->LNAType_5G & BIT7) && (pHalData->LNAType_5G & BIT3)) ? 1 : 0;	/*  5G only now. */
		} else {
			pHalData->external_pa_5g	= (GetRegAmplifierType5G(Adapter) & ODM_BOARD_EXT_PA_5G)	? 1 : 0;
			pHalData->external_lna_5g = (GetRegAmplifierType5G(Adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
#endif
	} else {
		pHalData->ExternalPA_2G  = EEPROM_Default_PAType;
		pHalData->external_pa_5g  = EEPROM_Default_PAType;
		pHalData->ExternalLNA_2G = EEPROM_Default_LNAType;
		pHalData->external_lna_5g = EEPROM_Default_LNAType;

		if (GetRegAmplifierType2G(Adapter) == 0) {
			/* AUTO*/
			pHalData->ExternalPA_2G  = EEPROM_Default_PAType;
			pHalData->ExternalLNA_2G = EEPROM_Default_LNAType;
		} else {
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}
#if 0
		if (GetRegAmplifierType5G(Adapter) == 0) {
			/*  AUTO */
			pHalData->external_pa_5g  = 0;
			pHalData->external_lna_5g = 0;
		} else {
			pHalData->external_pa_5g  = (GetRegAmplifierType5G(Adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			pHalData->external_lna_5g = (GetRegAmplifierType5G(Adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
#endif
	}
	RTW_INFO("pHalData->ExternalPA_2G = %d , pHalData->ExternalLNA_2G = %d\n",  pHalData->ExternalPA_2G, pHalData->ExternalLNA_2G);
}

void
Hal_ReadAmplifierType_8188E(
		PADAPTER	Adapter,
		u8			*PROMContent,
		BOOLEAN		AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	GLNA_type = 0;

	if (!AutoloadFail) {
		if (GetRegGLNAType(Adapter) == 0) /* AUTO */
			GLNA_type = LE_BITS_TO_1BYTE(&PROMContent[EEPROM_RFE_OPTION_8188E], 4, 3); /* 0xCA[6:4] */
		else
			GLNA_type  = GetRegGLNAType(Adapter) & 0x7;
	} else {
		if (GetRegGLNAType(Adapter) == 0) /* AUTO */
			GLNA_type = 0;
		else
			GLNA_type  = GetRegGLNAType(Adapter) & 0x7;
	}
	/*
		Ext-LNA Gain sel.(form 10dB to 24dB, 1table/2dB,ext: 000=10dB, 001=12dB...)
	*/
	switch (GLNA_type) {
	case 0:
		pHalData->TypeGLNA = 0x1; /* (10dB) */
		break;
	case 2:
		pHalData->TypeGLNA = 0x2; /* (14dB) */
		break;
	default:
		pHalData->TypeGLNA = 0x0; /* (others not support) */
		break;
	}
	RTW_INFO("pHalData->TypeGLNA is 0x%x\n", pHalData->TypeGLNA);
}

void
Hal_ReadRFEType_8188E(
		PADAPTER	Adapter,
		u8			*PROMContent,
		BOOLEAN		AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* Keep the same flow as 8192EU to be extensible */
	const u8 RFETypeMaxVal = 1, RFETypeMask = 0x1;

	if (!AutoloadFail) {
		if (GetRegRFEType(Adapter) != 64) {
			pHalData->rfe_type = GetRegRFEType(Adapter);
			/*
				Above 1, rfe_type is filled the default value.
			*/
			if (pHalData->rfe_type > RFETypeMaxVal)
				pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

		} else if ((0xFF == PROMContent[EEPROM_RFE_OPTION_8188E]) ||
			((pHalData->ExternalPA_2G == 0) && (pHalData->ExternalLNA_2G == 0)))
			pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;
		else {
			/*
				type 0:0x00 for 88EE/ER_HP RFE control
			*/
			pHalData->rfe_type = PROMContent[EEPROM_RFE_OPTION_8188E] & RFETypeMask; /* 0xCA[1:0] */
		}
	} else {
		if (GetRegRFEType(Adapter) != 64) {
			pHalData->rfe_type = GetRegRFEType(Adapter);
			/*
				 Above 3, rfe_type is filled the default value.
			*/
			if (pHalData->rfe_type > RFETypeMaxVal)
				pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

		} else
			pHalData->rfe_type = EEPROM_DEFAULT_RFE_OPTION_8188E;

	}

	RTW_INFO("pHalData->rfe_type is 0x%x\n", pHalData->rfe_type);
}

void
Hal_EfuseParseBoardType88E(
		PADAPTER		pAdapter,
		u8				*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->InterfaceSel = ((hwinfo[EEPROM_RF_BOARD_OPTION_88E] & 0xE0) >> 5);
		if (hwinfo[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;
	} else
		pHalData->InterfaceSel = 0;
	RTW_INFO("Board Type: 0x%2x\n", pHalData->InterfaceSel);
}

void
Hal_EfuseParseEEPROMVer88E(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_88E];
		if (pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;
	} else
		pHalData->EEPROMVersion = 1;
}

void
rtl8188e_EfuseParseChnlPlan(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	hal_com_config_channel_plan(
		padapter
		, hwinfo ? &hwinfo[EEPROM_COUNTRY_CODE_88E] : NULL
		, hwinfo ? hwinfo[EEPROM_ChannelPlan_88E] : 0xFF
		, padapter->registrypriv.alpha2
		, padapter->registrypriv.channel_plan
		, RTW_CHPLAN_WORLD_NULL
		, AutoLoadFail
	);
}

void
Hal_EfuseParseCustomerID88E(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_88E];
		/* pHalData->EEPROMSubCustomerID = hwinfo[EEPROM_CustomID_88E]; */
	} else {
		pHalData->EEPROMCustomerID = 0;
		pHalData->EEPROMSubCustomerID = 0;
	}
	RTW_INFO("EEPROM Customer ID: 0x%2x\n", pHalData->EEPROMCustomerID);
	/* RTW_INFO("EEPROM SubCustomer ID: 0x%02x\n", pHalData->EEPROMSubCustomerID); */
}


void
Hal_ReadAntennaDiversity88E(
		PADAPTER		pAdapter,
		u8				*PROMContent,
		BOOLEAN			AutoLoadFail
)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (!AutoLoadFail) {
		/* Antenna Diversity setting. */
		if (registry_par->antdiv_cfg == 2) { /* 2:By EFUSE */
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x18) >> 3;
			if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION & 0x18) >> 3;
		} else {
			pHalData->AntDivCfg = registry_par->antdiv_cfg ;  /* 0:OFF , 1:ON, 2:By EFUSE */
		}

		if (registry_par->antdiv_type == 0) { /* If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead. */
			pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_88E];
			if (pHalData->TRxAntDivType == 0xFF)
				pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; /* For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */
		} else
			pHalData->TRxAntDivType = registry_par->antdiv_type ;

		if (pHalData->TRxAntDivType == CG_TRX_HW_ANTDIV || pHalData->TRxAntDivType == CGCS_RX_HW_ANTDIV)
			pHalData->AntDivCfg = 1; /* 0xC1[3] is ignored. */
	} else
		pHalData->AntDivCfg = 0;

	RTW_INFO("EEPROM : AntDivCfg = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
#endif
}

void
Hal_ReadThermalMeter_88E(
		PADAPTER	Adapter,
		u8			*PROMContent,
		BOOLEAN	AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			tempval;

	/*  */
	/* ThermalMeter from EEPROM */
	/*  */
	if (!AutoloadFail)
		pHalData->eeprom_thermal_meter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_88E;
	/* pHalData->eeprom_thermal_meter = (tempval&0x1f);	 */ /* [4:0] */

	if (pHalData->eeprom_thermal_meter == 0xff || AutoloadFail) {
		pHalData->odmpriv.rf_calibrate_info.is_apk_thermal_meter_ignore = _TRUE;
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_88E;
	}

	/* pHalData->ThermalMeter[0] = pHalData->eeprom_thermal_meter;	 */
	RTW_INFO("ThermalMeter = 0x%x\n", pHalData->eeprom_thermal_meter);

}

#ifdef CONFIG_RF_POWER_TRIM
void Hal_ReadRFGainOffset(
			PADAPTER	Adapter,
			u8		*PROMContent,
			BOOLEAN		AutoloadFail)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8 thermal_offset = 0;
	/*  */
	/* BB_RF Gain Offset from EEPROM */
	/*  */

	if (!AutoloadFail) {
		pHalData->EEPROMRFGainOffset = PROMContent[EEPROM_RF_GAIN_OFFSET];

		if ((pHalData->EEPROMRFGainOffset  != 0xFF) &&
		    (pHalData->EEPROMRFGainOffset & BIT4))
			efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, _FALSE);
		else {
			pHalData->EEPROMRFGainOffset = 0;
			pHalData->EEPROMRFGainVal = 0;
		}

		RTW_INFO("pHalData->EEPROMRFGainVal=%x\n", pHalData->EEPROMRFGainVal);
	} else {
		efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, _FALSE);

		if (pHalData->EEPROMRFGainVal != 0xFF)
			pHalData->EEPROMRFGainOffset = BIT4;
		else
			pHalData->EEPROMRFGainOffset = 0;
		RTW_INFO("else AutoloadFail =%x,\n", AutoloadFail);
	}

	if (Adapter->registrypriv.RegPwrTrimEnable == 1) {
		efuse_OneByteRead(Adapter, EEPROM_RF_GAIN_VAL, &pHalData->EEPROMRFGainVal, _FALSE);
		RTW_INFO("pHalData->EEPROMRFGainVal=%x\n", pHalData->EEPROMRFGainVal);

	}
	/*  */
	/* BB_RF Thermal Offset from EEPROM */
	/*  */
	if (((pHalData->EEPROMRFGainOffset != 0xFF) && (pHalData->EEPROMRFGainOffset & BIT4)) || (Adapter->registrypriv.RegPwrTrimEnable == 1)) {

		efuse_OneByteRead(Adapter, EEPROM_THERMAL_OFFSET, &thermal_offset, _FALSE);
		if (thermal_offset != 0xFF) {
			if (thermal_offset & BIT0)
				pHalData->eeprom_thermal_meter += ((thermal_offset >> 1) & 0x0F);
			else
				pHalData->eeprom_thermal_meter -= ((thermal_offset >> 1) & 0x0F);

			RTW_INFO("%s =>thermal_offset:0x%02x pHalData->eeprom_thermal_meter=0x%02x\n", __FUNCTION__ , thermal_offset, pHalData->eeprom_thermal_meter);
		}
	}

	RTW_INFO("%s => EEPRORFGainOffset = 0x%02x,EEPROMRFGainVal=0x%02x,thermal_offset:0x%02x\n",
		__FUNCTION__, pHalData->EEPROMRFGainOffset, pHalData->EEPROMRFGainVal, thermal_offset);

}

#endif /*CONFIG_RF_POWER_TRIM*/

BOOLEAN HalDetectPwrDownMode88E(PADAPTER Adapter)
{
	u8 tmpvalue = 0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_RF_FEATURE_OPTION_88E, (u32 *)&tmpvalue);

	/* 2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT(4) && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = _TRUE;
	else
		pHalData->pwrdown = _FALSE;

	RTW_INFO("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	/* HalDetectPwrDownMode */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void Hal_DetectWoWMode(PADAPTER pAdapter)
{
	adapter_to_pwrctl(pAdapter)->bSupportRemoteWakeup = _TRUE;
}
#endif

void _InitTransferPageSize(PADAPTER padapter)
{
	/* Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}


static void hw_var_set_monitor(PADAPTER Adapter, u8 variable, u8 *val)
{
	u32	rcr_bits;
	u16	value_rxfltmap2;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

	if (*((u8 *)val) == _HW_STATE_MONITOR_) {
#ifdef CONFIG_CUSTOMER_ALIBABA_GENERAL
		rcr_bits = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_AMF | RCR_APP_PHYST_RXFF;
#else
		/* Receive all type */
		rcr_bits = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_ACF | RCR_AMF | RCR_APP_PHYST_RXFF;

		/* Append FCS */
		rcr_bits |= RCR_APPFCS;
#endif
#if 0
		/*
		   CRC and ICV packet will drop in recvbuf2recvframe()
		   We no turn on it.
		 */
		rcr_bits |= (RCR_ACRC32 | RCR_AICV);
#endif

		rtw_hal_get_hwreg(Adapter, HW_VAR_RCR, (u8 *)&pHalData->rcr_backup);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RCR, (u8 *)&rcr_bits);

		/* Receive all data frames */
		value_rxfltmap2 = 0xFFFF;
		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);

#if 0
		/* tx pause */
		rtw_write8(padapter, REG_TXPAUSE, 0xFF);
#endif
	} else {
		/* do nothing */
	}

}

static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8 *val)
{
	u8	val8;
	u8	mode = *((u8 *)val);
	static u8 isMonitor = _FALSE;

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (isMonitor == _TRUE) {
		/* reset RCR from backup */
		rtw_hal_set_hwreg(Adapter, HW_VAR_RCR, (u8 *)&pHalData->rcr_backup);
		rtw_hal_rcr_set_chk_bssid(Adapter, MLME_ACTION_NONE);
		isMonitor = _FALSE;
	}

	RTW_INFO(ADPT_FMT "- Port-%d  set opmode = %d\n", ADPT_ARG(Adapter),
		 get_hw_port(Adapter), mode);

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;
		/* set net_type */
		Set_MSR(Adapter, _HW_STATE_NOLINK_);

		hw_var_set_monitor(Adapter, variable, val);
		return;
	}

	rtw_hal_set_hwreg(Adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(Adapter)); /* set mac addr to mac register */

#ifdef CONFIG_CONCURRENT_MODE
	if (Adapter->hw_port == HW_PORT1) {
		/* disable Port1 TSF update */
		rtw_iface_disable_tsf_update(Adapter);

		/* set net_type		 */
		Set_MSR(Adapter, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
			if (!rtw_mi_get_ap_num(Adapter) && !rtw_mi_get_mesh_num(Adapter)) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms */

#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8188EU(Adapter, _TRUE, 0, IMR_BCNDMAINT0_88E);
#elif defined(CONFIG_SDIO_HCI)
				UpdateInterruptMask8188ESdio(Adapter, 0, SDIO_HIMR_BCNERLY_INT_MSK);
#endif

#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8188EU(Adapter, _TRUE , 0, (IMR_TBDER_88E | IMR_TBDOK_88E));
#elif defined(CONFIG_SDIO_HCI)
				UpdateInterruptMask8188ESdio(Adapter, 0, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK));
#endif

#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
#endif /* CONFIG_INTERRUPT_BASED_TXBCN		 */

				StopTxBeacon(Adapter);
#if defined(CONFIG_PCI_HCI)
				UpdateInterruptMask8188EE(Adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#endif
			}

			rtw_write8(Adapter, REG_BCN_CTRL_1, DIS_TSF_UDT | DIS_ATIM); /* disable atim wnd and disable beacon function */
			/* rtw_write8(Adapter,REG_BCN_CTRL_1, DIS_TSF_UDT | EN_BCN_FUNCTION); */
		} else if (mode == _HW_STATE_ADHOC_) {
			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL_1, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
			/* BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8188EU(Adapter, _TRUE , IMR_BCNDMAINT0_88E, 0);
#elif defined(CONFIG_SDIO_HCI)
			UpdateInterruptMask8188ESdio(Adapter, SDIO_HIMR_BCNERLY_INT_MSK, 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8188EU(Adapter, _TRUE , (IMR_TBDER_88E | IMR_TBDOK_88E), 0);
#elif defined(CONFIG_SDIO_HCI)
			UpdateInterruptMask8188ESdio(Adapter, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK), 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			rtw_write8(Adapter, REG_BCN_CTRL_1, DIS_TSF_UDT | DIS_BCNQ_SUB);

			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			/* enable to rx data frame				 */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms		 */
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* 5ms */
			/* rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(Adapter, REG_ATIMWND_1, 0x0c); /* 13ms for port1 */

			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF2	 */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));


			/* BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
			/* enable BCN1 Function for if2 */
			/* don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(Adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT| EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));

			if (!rtw_mi_buddy_check_mlmeinfo_state(Adapter, WIFI_FW_ASSOC_SUCCESS))
				rtw_write8(Adapter, REG_BCN_CTRL,
					rtw_read8(Adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);

			/* BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked */
			/* rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(5)); */
			/* rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(3)); */

			/* dis BCN0 ATIM  WND if if1 is station */
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | DIS_ATIM);

#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (DEV_STA_LD_NUM(adapter_to_dvobj(Adapter))) {
				if (rtw_hal_reset_tsf(Adapter, HW_PORT1) == _FAIL)
					RTW_INFO("ERROR! %s()-%d: Reset port1 TSF fail\n",
						 __FUNCTION__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
#if defined(CONFIG_PCI_HCI)
			UpdateInterruptMask8188EE(Adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#endif
		}
	} else	/* (Adapter->hw_port == HW_PORT1)*/
#endif /* CONFIG_CONCURRENT_MODE */
	{
#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
		hw_var_set_opmode_mbid(Adapter, mode);
#else
		/* disable Port0 TSF update */
		rtw_iface_disable_tsf_update(Adapter);

		/* set net_type */
		Set_MSR(Adapter, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
#ifdef CONFIG_CONCURRENT_MODE
			if (!rtw_mi_get_ap_num(Adapter) && !rtw_mi_get_mesh_num(Adapter))
#endif /*CONFIG_CONCURRENT_MODE*/
			{
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms	 */
#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8188EU(Adapter, _TRUE, 0, IMR_BCNDMAINT0_88E);
#elif defined(CONFIG_SDIO_HCI)
				UpdateInterruptMask8188ESdio(Adapter, 0, SDIO_HIMR_BCNERLY_INT_MSK);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8188EU(Adapter, _TRUE , 0, (IMR_TBDER_88E | IMR_TBDOK_88E));
#elif defined(CONFIG_SDIO_HCI)
				UpdateInterruptMask8188ESdio(Adapter, 0, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK));
#endif
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN		 */
				StopTxBeacon(Adapter);
#if defined(CONFIG_PCI_HCI)
				UpdateInterruptMask8188EE(Adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#endif
			}

			rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_ATIM); /* disable atim wnd */
			/* rtw_write8(Adapter,REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION); */
		} else if (mode == _HW_STATE_ADHOC_) {
			/* Beacon is polled to TXBUF */
			rtw_write16(Adapter, REG_CR, rtw_read16(Adapter, REG_CR) | BIT(8));

			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
			/* BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8188EU(Adapter, _TRUE , IMR_BCNDMAINT0_88E, 0);
#elif defined(CONFIG_SDIO_HCI)
			UpdateInterruptMask8188ESdio(Adapter, SDIO_HIMR_BCNERLY_INT_MSK, 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8188EU(Adapter, _TRUE , (IMR_TBDER_88E | IMR_TBDOK_88E), 0);
#elif defined(CONFIG_SDIO_HCI)
			UpdateInterruptMask8188ESdio(Adapter, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK), 0);
#endif
#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | DIS_BCNQ_SUB);

			/* Beacon is polled to TXBUF */
			rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR) | BIT(8));

			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms			 */
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* 5ms */
			/* rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(Adapter, REG_ATIMWND, 0x0c); /* 13ms */

			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			/* BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));

			/* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
#if defined(CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR)
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT| EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));
#else
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB));
#endif

#ifdef CONFIG_CONCURRENT_MODE
			if (!rtw_mi_buddy_check_mlmeinfo_state(Adapter, WIFI_FW_ASSOC_SUCCESS))
				rtw_write8(Adapter, REG_BCN_CTRL_1,
					rtw_read8(Adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);
#endif

			/* dis BCN1 ATIM  WND if if2 is station */
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) | DIS_ATIM);
#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (DEV_STA_LD_NUM(adapter_to_dvobj(Adapter))) {
				if (rtw_hal_reset_tsf(Adapter, HW_PORT0) == _FAIL)
					RTW_INFO("ERROR! %s()-%d: Reset port0 TSF fail\n",
						 __FUNCTION__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
#if defined(CONFIG_PCI_HCI)
			UpdateInterruptMask8188EE(Adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#endif
		}
#endif
	}
}

u8 SetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	struct dm_struct		*podmpriv = &pHalData->odmpriv;
	u8 ret = _SUCCESS;

	switch (variable) {

	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, variable, val);
		break;
	case HW_VAR_BASIC_RATE: 
		rtw_var_set_basic_rate(adapter, val);
	break;
	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE, *((u8 *)val));
		break;

	case HW_VAR_SLOT_TIME: {
		rtw_write8(adapter, REG_SLOT, val[0]);
	}
	break;
	case HW_VAR_ACK_PREAMBLE: {
		u8	regTmp;
		u8	bShortPreamble = *((PBOOLEAN)val);
		/* Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
		regTmp = (pHalData->nCur40MhzPrimeSC) << 5;
		rtw_write8(adapter, REG_RRSR + 2, regTmp);

		regTmp = rtw_read8(adapter, REG_WMAC_TRXPTCL_CTL + 2);
		if (bShortPreamble)
			regTmp |= BIT1;
		else
			regTmp &= (~BIT1);
		rtw_write8(adapter, REG_WMAC_TRXPTCL_CTL + 2, regTmp);
	}
	break;
	case HW_VAR_CAM_INVALID_ALL:
		rtw_write32(adapter, REG_CAMCMD, BIT(31) | BIT(30));
		break;
	case HW_VAR_AC_PARAM_VO:
		rtw_write32(adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_VI:
		rtw_write32(adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_BE:
		pHalData->ac_param_be = ((u32 *)(val))[0];
		rtw_write32(adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_BK:
		rtw_write32(adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_ACM_CTRL: {
		u8	acm_ctrl = *((u8 *)val);
		u8	AcmCtrl = rtw_read8(adapter, REG_ACMHWCTRL);

		if (acm_ctrl > 1)
			AcmCtrl = AcmCtrl | 0x1;

		if (acm_ctrl & BIT(1))
			AcmCtrl |= AcmHw_VoqEn;
		else
			AcmCtrl &= (~AcmHw_VoqEn);

		if (acm_ctrl & BIT(2))
			AcmCtrl |= AcmHw_ViqEn;
		else
			AcmCtrl &= (~AcmHw_ViqEn);

		if (acm_ctrl & BIT(3))
			AcmCtrl |= AcmHw_BeqEn;
		else
			AcmCtrl &= (~AcmHw_BeqEn);

		RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
		rtw_write8(adapter, REG_ACMHWCTRL, AcmCtrl);
	}
	break;
#ifdef CONFIG_80211N_HT
	case HW_VAR_AMPDU_FACTOR: {
		u8	RegToSet_Normal[4] = {0x41, 0xa8, 0x72, 0xb9};
		u8	RegToSet_BT[4] = {0x31, 0x74, 0x42, 0x97};
		u8	FactorToSet;
		u8	*pRegToSet;
		u8	index = 0;

#ifdef CONFIG_BT_COEXIST
		if ((pHalData->bt_coexist.BT_Coexist) &&
		    (pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4))
			pRegToSet = RegToSet_BT; /* 0x97427431; */
		else
#endif
			pRegToSet = RegToSet_Normal; /* 0xb972a841; */

		FactorToSet = *((u8 *)val);
		if (FactorToSet <= 3) {
			FactorToSet = (1 << (FactorToSet + 2));
			if (FactorToSet > 0xf)
				FactorToSet = 0xf;

			for (index = 0; index < 4; index++) {
				if ((pRegToSet[index] & 0xf0) > (FactorToSet << 4))
					pRegToSet[index] = (pRegToSet[index] & 0x0f) | (FactorToSet << 4);

				if ((pRegToSet[index] & 0x0f) > FactorToSet)
					pRegToSet[index] = (pRegToSet[index] & 0xf0) | (FactorToSet);

				rtw_write8(adapter, (REG_AGGLEN_LMT + index), pRegToSet[index]);
			}

		}
	}
	break;
#endif /* CONFIG_80211N_HT */
	case HW_VAR_H2C_FW_PWRMODE: {
		u8	psmode = (*(u8 *)val);

		rtl8188e_set_FwPwrMode_cmd(adapter, psmode);
	}
	break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8	mstatus = (*(u8 *)val);
		rtl8188e_set_FwJoinBssReport_cmd(adapter, mstatus);
	}
	break;
#ifdef CONFIG_P2P_PS
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD: {
		u8	p2p_ps_state = (*(u8 *)val);
		rtl8188e_set_p2p_ps_offload_cmd(adapter, p2p_ps_state);
	}
	break;
#endif /* CONFIG_P2P_PS */
#ifdef CONFIG_BT_COEXIST
	case HW_VAR_BT_SET_COEXIST: {
		u8	bStart = (*(u8 *)val);
		rtl8192c_set_dm_bt_coexist(adapter, bStart);
	}
	break;
	case HW_VAR_BT_ISSUE_DELBA: {
		u8	dir = (*(u8 *)val);
		rtl8192c_issue_delete_ba(adapter, dir);
	}
	break;
#endif
#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_RPT_TIMER_SETTING: {
		u16	min_rpt_time = (*(u16 *)val);

		odm_ra_set_tx_rpt_time(podmpriv, min_rpt_time);
	}
	break;
#endif

	case HW_VAR_EFUSE_BYTES: /* To set EFUE total used bytes, added by Roger, 2008.12.22. */
		pHalData->EfuseUsedBytes = *((u16 *)val);
		break;
	case HW_VAR_FIFO_CLEARN_UP: {
		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
		u8 trycnt = 100;

		/* pause tx */
		rtw_write8(adapter, REG_TXPAUSE, 0xff);

		/* keep sn */
		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, REG_NQOS_SEQ);

		if (pwrpriv->bkeepfwalive != _TRUE) {
			/* RX DMA stop */
			rtw_write32(adapter, REG_RXPKT_NUM, (rtw_read32(adapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
			do {
				if (!(rtw_read32(adapter, REG_RXPKT_NUM) & RXDMA_IDLE))
					break;
			} while (trycnt--);
			if (trycnt == 0)
				RTW_INFO("Stop RX DMA failed......\n");

			/* RQPN Load 0 */
			rtw_write16(adapter, REG_RQPN_NPQ, 0x0);
			rtw_write32(adapter, REG_RQPN, 0x80000000);
			rtw_mdelay_os(10);
		}
	}
	break;

	case HW_VAR_RESTORE_HW_SEQ:
		/* restore Sequence No. */
		rtw_write8(adapter, 0x4dc, adapter->xmitpriv.nqos_ssn);
		break;

#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_TX_RPT_MAX_MACID: {
		u8 maxMacid = *val;

		RTW_INFO("### MacID(%d),Set Max Tx RPT MID(%d)\n", maxMacid, maxMacid + 1);
		rtw_write8(adapter, REG_TX_RPT_CTRL + 1, maxMacid + 1);
	}
	break;
#endif /* (RATE_ADAPTIVE_SUPPORT == 1) */

	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
		rtw_write8(adapter, REG_TDECTRL + 2, rtw_read8(adapter, REG_TDECTRL + 2) | BIT0);
		break;


	case HW_VAR_CHECK_TXBUF: {
		u8 retry_limit;
		u16 val16;
		u32 reg_200 = 0, reg_204 = 0;
		u32 init_reg_200 = 0, init_reg_204 = 0;
		systime start = rtw_get_current_time();
		u32 pass_ms;
		int i = 0;

		retry_limit = 0x01;

		val16 = BIT_SRL(retry_limit) | BIT_LRL(retry_limit);
		rtw_write16(adapter, REG_RETRY_LIMIT, val16);

		while (rtw_get_passing_time_ms(start) < 2000
		       && !RTW_CANNOT_RUN(adapter)
		      ) {
			reg_200 = rtw_read32(adapter, 0x200);
			reg_204 = rtw_read32(adapter, 0x204);

			if (i == 0) {
				init_reg_200 = reg_200;
				init_reg_204 = reg_204;
			}

			i++;
			if ((reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
				/* RTW_INFO("%s: (HW_VAR_CHECK_TXBUF)TXBUF NOT empty - 0x204=0x%x, 0x200=0x%x (%d)\n", __FUNCTION__, reg_204, reg_200, i); */
				rtw_msleep_os(10);
			} else
				break;
		}

		pass_ms = rtw_get_passing_time_ms(start);

		if (RTW_CANNOT_RUN(adapter))
			;
		else if (pass_ms >= 2000 || (reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
			RTW_PRINT("%s:(HW_VAR_CHECK_TXBUF)NOT empty(%d) in %d ms\n", __FUNCTION__, i, pass_ms);
			RTW_PRINT("%s:(HW_VAR_CHECK_TXBUF)0x200=0x%08x, 0x204=0x%08x (0x%08x, 0x%08x)\n",
				__FUNCTION__, reg_200, reg_204, init_reg_200, init_reg_204);
			/* rtw_warn_on(1); */
		} else
			RTW_INFO("%s:(HW_VAR_CHECK_TXBUF)TXBUF Empty(%d) in %d ms\n", __FUNCTION__, i, pass_ms);

		retry_limit = RL_VAL_STA;
		val16 = BIT_SRL(retry_limit) | BIT_LRL(retry_limit);
		rtw_write16(adapter, REG_RETRY_LIMIT, val16);
	}
	break;
	case HW_VAR_RESP_SIFS: {
		struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

		if ((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||
		    (pmlmeext->cur_wireless_mode == WIRELESS_11BG)) { /* WIRELESS_MODE_G){ */
			val[0] = 0x0a;
			val[1] = 0x0a;
		} else {
			val[0] = 0x0e;
			val[1] = 0x0e;
		}

		/* SIFS for OFDM Data ACK */
		rtw_write8(adapter, REG_SIFS_CTX + 1, val[0]);
		/* SIFS for OFDM consecutive tx like CTS data! */
		rtw_write8(adapter, REG_SIFS_TRX + 1, val[1]);

		rtw_write8(adapter, REG_SPEC_SIFS + 1, val[0]);
		rtw_write8(adapter, REG_MAC_SPEC_SIFS + 1, val[0]);

		/* RESP_SIFS for OFDM */
		rtw_write8(adapter, REG_RESP_SIFS_OFDM, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM + 1, val[0]);
	}
	break;

	case HW_VAR_MACID_LINK: {
		u32 reg_macid_no_link;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_no_link = REG_MACID_NO_LINK_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_no_link = REG_MACID_NO_LINK_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_no_link);
		if (!(val32 & BIT(bit_shift)))
			break;

		val32 &= ~BIT(bit_shift);
		rtw_write32(adapter, reg_macid_no_link, val32);
	}
	break;

	case HW_VAR_MACID_NOLINK: {
		u32 reg_macid_no_link;
		u8 bit_shift;
		u8 id = *(u8 *)val;
		u32 val32;

		if (id < 32) {
			reg_macid_no_link = REG_MACID_NO_LINK_0;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_no_link = REG_MACID_NO_LINK_1;
			bit_shift = id - 32;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_no_link);
		if (val32 & BIT(bit_shift))
			break;

		val32 |= BIT(bit_shift);
		rtw_write32(adapter, reg_macid_no_link, val32);
	}
	break;

	default:
		ret = SetHwReg(adapter, variable, val);
		break;
	}

	return ret;
}

struct qinfo_88e {
	u32 head:8;
	u32 pkt_num:8;
	u32 tail:8;
	u32 ac:2;
	u32 macid:6;
};

struct bcn_qinfo_88e {
	u16 head:8;
	u16 pkt_num:8;
};

void dump_qinfo_88e(void *sel, struct qinfo_88e *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n"
		, tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac
		     );
}

void dump_bcn_qinfo_88e(void *sel, struct bcn_qinfo_88e *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n"
		      , tag ? tag : "", info->head, info->pkt_num
		     );
}

void dump_mac_qinfo_88e(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	/*
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	*/
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;

	q0_info = rtw_read32(adapter, REG_Q0_INFO);
	q1_info = rtw_read32(adapter, REG_Q1_INFO);
	q2_info = rtw_read32(adapter, REG_Q2_INFO);
	q3_info = rtw_read32(adapter, REG_Q3_INFO);
	/*
	q4_info = rtw_read32(adapter, REG_Q4_INFO);
	q5_info = rtw_read32(adapter, REG_Q5_INFO);
	q6_info = rtw_read32(adapter, REG_Q6_INFO);
	q7_info = rtw_read32(adapter, REG_Q7_INFO);
	*/
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO);
	hi_q_info = rtw_read32(adapter, REG_HGQ_INFO);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO);

	dump_qinfo_88e(sel, (struct qinfo_88e *)&q0_info, "Q0 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q1_info, "Q1 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q2_info, "Q2 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q3_info, "Q3 ");
	/*
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q4_info, "Q4 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q5_info, "Q5 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q6_info, "Q6 ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&q7_info, "Q7 ");
	*/
	dump_qinfo_88e(sel, (struct qinfo_88e *)&mg_q_info, "MG ");
	dump_qinfo_88e(sel, (struct qinfo_88e *)&hi_q_info, "HI ");
	dump_bcn_qinfo_88e(sel, (struct bcn_qinfo_88e *)&bcn_q_info, "BCN ");
}

static void dump_mac_txfifo_88e(void *sel, _adapter *adapter)
{
	u32 rqpn, rqpn_npq;
	u32 hpq, lpq, npq, pubq;

	rqpn = rtw_read32(adapter, REG_FIFOPAGE);
	rqpn_npq = rtw_read32(adapter, REG_RQPN_NPQ);

	hpq = (rqpn & 0xFF);
	lpq = ((rqpn & 0xFF00)>>8);
	pubq = ((rqpn & 0xFF0000)>>16);
	npq = ((rqpn_npq & 0xFF00)>>8);

	RTW_PRINT_SEL(sel, "Tx: available page num: ");
	if ((hpq == 0xEA) && (hpq == lpq) && (hpq == pubq))
		RTW_PRINT_SEL(sel, "N/A (reg val = 0xea)\n");
	else
		RTW_PRINT_SEL(sel, "HPQ: %d, LPQ: %d, NPQ: %d, PUBQ: %d\n"
			, hpq, lpq, npq, pubq);
}

void GetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	u32 val32;

	switch (variable) {
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLKR);
		break;

	case HW_VAR_TXPAUSE:
		val[0] = rtw_read8(adapter, REG_TXPAUSE);
		break;

	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
		val[0] = (BIT0 & rtw_read8(adapter, REG_TDECTRL + 2)) ? _TRUE : _FALSE;
		break;

	case HW_VAR_AC_PARAM_VO:
		val32 = rtw_read32(adapter, REG_EDCA_VO_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_VI:
		val32 = rtw_read32(adapter, REG_EDCA_VI_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BE:
		val32 = rtw_read32(adapter, REG_EDCA_BE_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BK:
		val32 = rtw_read32(adapter, REG_EDCA_BK_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_EFUSE_BYTES: /* To get EFUE total used bytes, added by Roger, 2008.12.22. */
		*((u16 *)(val)) = pHalData->EfuseUsedBytes;
		break;
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		*val = ((rtw_read32(adapter, REG_HGQ_INFO) & 0x0000ff00) == 0) ? _TRUE : _FALSE;
		break;
	case HW_VAR_CHK_MGQ_CPU_EMPTY:
		*val = (rtw_read16(adapter, REG_TXPKT_EMPTY) & BIT8) ? _TRUE : _FALSE;
		break;
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo_88e(val, adapter);
		break;
	case HW_VAR_DUMP_MAC_TXFIFO:
		dump_mac_txfifo_88e(val, adapter);
		break;
	default:
		GetHwReg(adapter, variable, val);
		break;
	}

}
void hal_ra_info_dump(_adapter *padapter , void *sel)
{
	int i;
	u8 mac_id;
	u8 bLinked = _FALSE;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	_adapter *iface;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			if (rtw_linked_check(iface)) {
				bLinked = _TRUE;
				break;
			}
		}
	}

	for (i = 0; i < macid_ctl->num; i++) {

		if (rtw_macid_is_used(macid_ctl, i) && !rtw_macid_is_bmc(macid_ctl, i)) {

			mac_id = (u8) i;

			if (bLinked) {
				RTW_PRINT_SEL(sel , "============ RA status - Mac_id:%d ===================\n", mac_id);
				if (pHalData->fw_ractrl == _FALSE) {
#if (RATE_ADAPTIVE_SUPPORT == 1)
					RTW_PRINT_SEL(sel , "Mac_id:%d ,RSSI:%d(%%)\n", mac_id, pHalData->odmpriv.ra_info[mac_id].rssi_sta_ra);

					RTW_PRINT_SEL(sel , "rate_sgi = %d, decision_rate = %s\n", rtw_get_current_tx_sgi(padapter, macid_ctl->sta[mac_id]),
						HDATA_RATE(rtw_get_current_tx_rate(padapter, macid_ctl->sta[mac_id])));

					RTW_PRINT_SEL(sel , "pt_stage = %d\n", pHalData->odmpriv.ra_info[mac_id].pt_stage);

					RTW_PRINT_SEL(sel , "rate_id = %d,ra_use_rate = 0x%08x\n", pHalData->odmpriv.ra_info[mac_id].rate_id, pHalData->odmpriv.ra_info[mac_id].ra_use_rate);

#endif /* (RATE_ADAPTIVE_SUPPORT == 1)*/
				} else {
					u8 cur_rate = 0;
					u8 sgi = 0;
					
					if (padapter->fix_rate == 0xff) {
						cur_rate = rtw_read8(padapter, REG_ADAPTIVE_DATA_RATE_0 + mac_id) & 0x7f;
						sgi = (cur_rate & BIT7) ? _TRUE : _FALSE;
					} else {
						cur_rate = padapter->fix_rate & 0x7f;
						sgi = ((padapter->fix_rate) & 0x80) >> 7;
					}
						
					RTW_PRINT_SEL(sel , "Mac_id:%d ,SGI:%d ,Rate:%s\n", mac_id, sgi, HDATA_RATE(cur_rate));
				}
			}
		}
	}
}

u8
GetHalDefVar8188E(
		PADAPTER				Adapter,
		HAL_DEF_VARIABLE		eVariable,
		void						*pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*((u8 *)pValue) = (pHalData->AntDivCfg == 0) ? _FALSE : _TRUE;
#endif
		break;
	case HAL_DEF_DRVINFO_SZ:
		*((u32 *)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
#ifdef CONFIG_SDIO_HCI
		*((u32 *)pValue) = MAX_RX_DMA_BUFFER_SIZE_88E(Adapter);
#else
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
#endif
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pValue) = RXDESC_SIZE + DRVINFO_SZ * 8;
		break;
#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HAL_DEF_RA_DECISION_RATE: {
		u8 MacID = *((u8 *)pValue);
		*((u8 *)pValue) = odm_ra_get_decision_rate_8188e(&(pHalData->odmpriv), MacID);
	}
	break;

	case HAL_DEF_RA_SGI: {
		u8 MacID = *((u8 *)pValue);
		*((u8 *)pValue) = odm_ra_get_sgi_8188e(&(pHalData->odmpriv), MacID);
	}
	break;
#endif


	case HAL_DEF_PT_PWR_STATUS:
#if (POWER_TRAINING_ACTIVE == 1)
		{
			u8 MacID = *((u8 *)pValue);
			*((u8 *)pValue) = odm_ra_get_hw_pwr_status_8188e(&(pHalData->odmpriv), MacID);
		}
#endif /* (POWER_TRAINING_ACTIVE==1) */
		break;
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
	case HAL_DEF_EXPLICIT_BEAMFORMER:
		*((u8 *)pValue) = _FALSE;
		break;

	case HW_DEF_RA_INFO_DUMP:
		hal_ra_info_dump(Adapter, pValue);
		break;

	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)pValue) = PAGE_SIZE_128;
		break;
	case HAL_DEF_TX_PAGE_BOUNDARY:
		if (!Adapter->registrypriv.wifi_spec)
			*(u8 *)pValue = TX_PAGE_BOUNDARY_88E(Adapter);
		else
			*(u8 *)pValue = WMM_NORMAL_TX_PAGE_BOUNDARY_88E(Adapter);
		break;
	case HAL_DEF_RX_DMA_SZ_WOW:
		*(u32 *)pValue = RX_DMA_SIZE_88E(Adapter) - RESV_FMWF;
		break;
	case HAL_DEF_RX_DMA_SZ:
		*(u32 *)pValue = MAX_RX_DMA_BUFFER_SIZE_88E(Adapter);
		break;
	case HAL_DEF_RX_PAGE_SIZE:
		*(u32 *)pValue = PAGE_SIZE_128;
		break;
	case HW_VAR_BEST_AMPDU_DENSITY:
		*((u32 *)pValue) = AMPDU_DENSITY_VALUE_7;
		break;
	default:
		bResult = GetHalDefVar(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

#ifdef CONFIG_GPIO_API
int rtl8188e_GpioFuncCheck(PADAPTER adapter, u8 gpio_num)
{
        int ret = _SUCCESS;

        if (IS_HARDWARE_TYPE_8188E(adapter) == _FAIL) {
                if ((gpio_num > 7) || (gpio_num < 4)) {
                        RTW_INFO("%s The gpio number does not included 4~7.\n",__FUNCTION__);
                        ret = _FAIL;
                }
        }

        return ret;
}
#endif
