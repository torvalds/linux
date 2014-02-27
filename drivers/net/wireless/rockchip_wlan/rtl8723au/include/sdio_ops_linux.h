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
#ifndef __SDIO_OPS_LINUX_H__
#define __SDIO_OPS_LINUX_H__

#define SDIO_ERR_VAL8	0xEA
#define SDIO_ERR_VAL16	0xEAEA
#define SDIO_ERR_VAL32	0xEAEAEAEA

u8 sd_f0_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
void sd_f0_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err);

s32 _sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 _sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);

u8 _sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u8 sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u16 sd_read16(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u32 _sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u32 sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
s32 _sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
void sd_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err);
void sd_write16(struct intf_hdl *pintfhdl, u32 addr, u16 v, s32 *err);
void _sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err);
void sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err);
s32 _sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);


void rtw_sdio_set_irq_thd(struct dvobj_priv *dvobj, _thread_hdl_ thd_hdl);
#endif

