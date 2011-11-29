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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _IO_H_
#define _IO_H_

#include "osdep_service.h"
#include "osdep_intf.h"

#define NUM_IOREQ		8

#define MAX_PROT_SZ	(64-16)

#define _IOREADY			0
#define _IO_WAIT_COMPLETE   1
#define _IO_WAIT_RSP        2

/* IO COMMAND TYPE */
#define _IOSZ_MASK_		(0x7F)
#define _IO_WRITE_		BIT(7)
#define _IO_FIXED_		BIT(8)
#define _IO_BURST_		BIT(9)
#define _IO_BYTE_		BIT(10)
#define _IO_HW_			BIT(11)
#define _IO_WORD_		BIT(12)
#define _IO_SYNC_		BIT(13)
#define _IO_CMDMASK_	(0x1F80)

/*
	For prompt mode accessing, caller shall free io_req
	Otherwise, io_handler will free io_req
*/
/* IO STATUS TYPE */
#define _IO_ERR_		BIT(2)
#define _IO_SUCCESS_	BIT(1)
#define _IO_DONE_		BIT(0)
#define IO_RD32			(_IO_SYNC_ | _IO_WORD_)
#define IO_RD16			(_IO_SYNC_ | _IO_HW_)
#define IO_RD8			(_IO_SYNC_ | _IO_BYTE_)
#define IO_RD32_ASYNC	(_IO_WORD_)
#define IO_RD16_ASYNC	(_IO_HW_)
#define IO_RD8_ASYNC	(_IO_BYTE_)
#define IO_WR32			(_IO_WRITE_ | _IO_SYNC_ | _IO_WORD_)
#define IO_WR16			(_IO_WRITE_ | _IO_SYNC_ | _IO_HW_)
#define IO_WR8			(_IO_WRITE_ | _IO_SYNC_ | _IO_BYTE_)
#define IO_WR32_ASYNC	(_IO_WRITE_ | _IO_WORD_)
#define IO_WR16_ASYNC	(_IO_WRITE_ | _IO_HW_)
#define IO_WR8_ASYNC	(_IO_WRITE_ | _IO_BYTE_)
/*
	Only Sync. burst accessing is provided.
*/
#define IO_WR_BURST(x)		(IO_WRITE_ | _IO_SYNC_ | _IO_BURST_ | \
				((x) & _IOSZ_MASK_))
#define IO_RD_BURST(x)		(_IO_SYNC_ | _IO_BURST_ | ((x) & _IOSZ_MASK_))
/*below is for the intf_option bit defition...*/
#define _INTF_ASYNC_	BIT(0)	/*support async io*/
struct intf_priv;
struct	intf_hdl;
struct io_queue;
struct	_io_ops {
	uint (*_sdbus_read_bytes_to_membuf)(struct intf_priv *pintfpriv,
					    u32 addr, u32 cnt, u8 *pbuf);
	uint (*_sdbus_read_blocks_to_membuf)(struct intf_priv *pintfpriv,
					     u32 addr, u32 cnt, u8 *pbuf);
	u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);
	u16 (*_read16)(struct intf_hdl *pintfhdl, u32 addr);
	u32 (*_read32)(struct intf_hdl *pintfhdl, u32 addr);
	uint (*_sdbus_write_blocks_from_membuf)(struct intf_priv *pintfpriv,
						u32 addr, u32 cnt, u8 *pbuf,
						u8 async);
	uint (*_sdbus_write_bytes_from_membuf)(struct intf_priv *pintfpriv,
					       u32 addr, u32 cnt, u8 *pbuf);
	u8 (*_cmd52r)(struct intf_priv *pintfpriv, u32 addr);
	void (*_cmd52w)(struct intf_priv *pintfpriv, u32 addr, u8 val8);
	u8 (*_cmdfunc152r)(struct intf_priv *pintfpriv, u32 addr);
	void (*_cmdfunc152w)(struct intf_priv *pintfpriv, u32 addr, u8 val8);
	void (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	void (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	void (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt,
			  u8 *pmem);
	void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt,
			   u8 *pmem);
	void (*_sync_irp_protocol_rw)(struct io_queue *pio_q);
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt,
			  u8 *pmem);
	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt,
			   u8 *pmem);
};

struct io_req {
	struct list_head list;
	u32	addr;
	/*volatile*/ u32	val;
	u32	command;
	u32	status;
	u8	*pbuf;
	struct semaphore sema;
	void (*_async_io_callback)(struct _adapter *padater,
				   struct io_req *pio_req, u8 *cnxt);
	u8 *cnxt;
};

struct	intf_hdl {
	u32	intf_option;
	u8	*adapter;
	u8	*intf_dev;
	struct intf_priv	*pintfpriv;
	void (*intf_hdl_init)(u8 *priv);
	void (*intf_hdl_unload)(u8 *priv);
	void (*intf_hdl_open)(u8 *priv);
	void (*intf_hdl_close)(u8 *priv);
	struct	_io_ops	io_ops;
};

struct reg_protocol_rd {

#ifdef __LITTLE_ENDIAN
	/* DW1 */
	u32		NumOfTrans:4;
	u32		Reserved1:4;
	u32		Reserved2:24;
	/* DW2 */
	u32		ByteCount:7;
	u32		WriteEnable:1;		/*0:read, 1:write*/
	u32		FixOrContinuous:1;	/*0:continuous, 1: Fix*/
	u32		BurstMode:1;
	u32		Byte1Access:1;
	u32		Byte2Access:1;
	u32		Byte4Access:1;
	u32		Reserved3:3;
	u32		Reserved4:16;
	/*DW3*/
	u32		BusAddress;
	/*DW4*/
#else
/*DW1*/
	u32 Reserved1:4;
	u32 NumOfTrans:4;
	u32 Reserved2:24;
	/*DW2*/
	u32 WriteEnable:1;
	u32 ByteCount:7;
	u32 Reserved3:3;
	u32 Byte4Access:1;
	u32 Byte2Access:1;
	u32 Byte1Access:1;
	u32 BurstMode:1 ;
	u32 FixOrContinuous:1;
	u32 Reserved4:16;
	/*DW3*/
	u32 BusAddress;
	/*DW4*/
#endif
};

struct reg_protocol_wt {
#ifdef __LITTLE_ENDIAN
	/*DW1*/
	u32 NumOfTrans:4;
	u32 Reserved1:4;
	u32 Reserved2:24;
	/*DW2*/
	u32 ByteCount:7;
	u32 WriteEnable:1;	/*0:read, 1:write*/
	u32 FixOrContinuous:1;	/*0:continuous, 1: Fix*/
	u32 BurstMode:1;
	u32 Byte1Access:1;
	u32 Byte2Access:1;
	u32 Byte4Access:1;
	u32 Reserved3:3;
	u32 Reserved4:16;
	/*DW3*/
	u32 BusAddress;
	/*DW4*/
	u32 Value;
#else
	/*DW1*/
	u32 Reserved1:4;
	u32 NumOfTrans:4;
	u32 Reserved2:24;
	/*DW2*/
	u32 WriteEnable:1;
	u32 ByteCount:7;
	u32 Reserved3:3;
	u32 Byte4Access:1;
	u32 Byte2Access:1;
	u32 Byte1Access:1;
	u32 BurstMode:1;
	u32 FixOrContinuous:1;
	u32 Reserved4:16;
	/*DW3*/
	u32 BusAddress;
	/*DW4*/
	u32 Value;
#endif
};

/*
Below is the data structure used by _io_handler
*/

struct io_queue {
	spinlock_t lock;
	struct list_head free_ioreqs;
	/*The io_req list that will be served in the single protocol r/w.*/
	struct list_head pending;
	struct list_head processing;
	u8 *free_ioreqs_buf; /* 4-byte aligned */
	u8 *pallocated_free_ioreqs_buf;
	struct	intf_hdl intf;
};

static inline u32 _RND4(u32 sz)
{
	u32	val;
	val = ((sz >> 2) + ((sz & 3) ? 1 : 0)) << 2;
	return val;
}

u8 r8712_read8(struct _adapter *adapter, u32 addr);
u16 r8712_read16(struct _adapter *adapter, u32 addr);
u32 r8712_read32(struct _adapter *adapter, u32 addr);
void r8712_read_mem(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void r8712_read_port(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void r8712_write8(struct _adapter *adapter, u32 addr, u8 val);
void r8712_write16(struct _adapter *adapter, u32 addr, u16 val);
void r8712_write32(struct _adapter *adapter, u32 addr, u32 val);
void r8712_write_mem(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
void r8712_write_port(struct _adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
/*ioreq */
uint r8712_alloc_io_queue(struct _adapter *adapter);
void r8712_free_io_queue(struct _adapter *adapter);

#endif	/*_RTL8711_IO_H_*/

