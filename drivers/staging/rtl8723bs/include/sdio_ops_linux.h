/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __SDIO_OPS_LINUX_H__
#define __SDIO_OPS_LINUX_H__

#define SDIO_ERR_VAL8	0xEA
#define SDIO_ERR_VAL16	0xEAEA
#define SDIO_ERR_VAL32	0xEAEAEAEA

s32 _sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 _sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 sd_cmd52_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);
s32 sd_cmd52_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pdata);

u8 sd_read8(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
u32 sd_read32(struct intf_hdl *pintfhdl, u32 addr, s32 *err);
s32 _sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_read(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
void sd_write8(struct intf_hdl *pintfhdl, u32 addr, u8 v, s32 *err);
void sd_write32(struct intf_hdl *pintfhdl, u32 addr, u32 v, s32 *err);
s32 _sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);
s32 sd_write(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, void *pdata);


void rtw_sdio_set_irq_thd(struct dvobj_priv *dvobj, void *thd_hdl);
#endif
