/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef _SDIO_OPS_LINUX_H_
#define _SDIO_OPS_LINUX_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>


#ifdef PLATFORM_LINUX


extern u8 sdbus_cmd52r(struct intf_priv *pintfpriv, u32 addr);


extern void sdbus_cmd52w(struct intf_priv *pintfpriv, u32 addr,u8 val8);
extern u8 sdbus_direct_read8(struct intf_priv *pintfpriv, u32 addr);
extern void sdbus_direct_write8(struct intf_priv *pintfpriv, u32 addr, u8 val8);

extern uint sdbus_read_bytes_to_recvbuf(struct intf_priv *pintfpriv, u32 addr, u32 cnt, u8 *pbuf);
extern uint sdbus_read_blocks_to_recvbuf(struct intf_priv *pintfpriv, u32 addr, u32 cnt, u8 *pbuf);


extern uint sdbus_write_blocks_from_xmitbuf(struct intf_priv *pintfpriv, u32 addr, u32 cnt, u8 *pbuf,u8 async);

extern uint sdbus_write_bytes_from_xmitbuf(struct intf_priv *pintfpriv, u32 addr, u32 cnt, u8 *pbuf);


extern uint sdbus_read_reg(struct intf_priv *pintfpriv, u32 addr, u32 cnt,void *pdata);
extern uint sdbus_write_reg(struct intf_priv *pintfpriv, u32 addr, u32 cnt,void *pdata);
extern void sdio_read_int(_adapter *padapter, u32 addr,u8 sz,void *pdata);
#endif

#endif

