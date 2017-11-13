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
#ifndef __SDIO_OPS_LINUX_H__
#define __SDIO_OPS_LINUX_H__

#ifndef RTW_HALMAC
u8 sd_f0_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
void sd_f0_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err);

s32 _sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 _sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);

u8 _sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u8 sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u16 sd_read16(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u32 _sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u32 sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
void sd_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err);
void sd_write16(struct intf_hdl *pintfhdl, u32 addr, u16 v, s32 *err);
void _sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err);
void sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err);
#endif /* RTW_HALMAC */

bool rtw_is_sdio30(_adapter *adapter);

/* The unit of return value is Hz */
static inline u32 rtw_sdio_get_clock(struct dvobj_priv *d)
{
	return d->intf_data.clock;
}

s32 _sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 _sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);

void rtw_sdio_set_irq_thd(struct dvobj_priv *dvobj, _thread_hdl_ thd_hdl);
int __must_check rtw_sdio_raw_read(struct dvobj_priv *d, unsigned int addr,
				void *buf, size_t len, bool fixed);
int __must_check rtw_sdio_raw_write(struct dvobj_priv *d, unsigned int addr,
				void *buf, size_t len, bool fixed);

#endif /* __SDIO_OPS_LINUX_H__ */

