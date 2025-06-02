/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef _RTW_IO_H_
#define _RTW_IO_H_

struct intf_hdl;

struct _io_ops {
		u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);
		u16 (*_read16)(struct intf_hdl *pintfhdl, u32 addr);
		u32 (*_read32)(struct intf_hdl *pintfhdl, u32 addr);

		int (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
		int (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
		int (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
		int (*_writeN)(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata);

		int (*_write8_async)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
		int (*_write16_async)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
		int (*_write32_async)(struct intf_hdl *pintfhdl, u32 addr, u32 val);

		void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
		void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

		u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);

		u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
		u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

		u32 (*_write_scsi)(struct intf_hdl *pintfhdl, u32 cnt, u8 *pmem);

		void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
		void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
};

struct	intf_hdl {
	struct adapter *padapter;
	struct dvobj_priv *pintf_dev;/* 	pointer to &(padapter->dvobjpriv); */

	struct _io_ops	io_ops;
};

#define SD_IO_TRY_CNT (8)
#define MAX_CONTINUAL_IO_ERR SD_IO_TRY_CNT

int rtw_inc_and_chk_continual_io_error(struct dvobj_priv *dvobj);
void rtw_reset_continual_io_error(struct dvobj_priv *dvobj);

struct io_priv {

	struct adapter *padapter;

	struct intf_hdl intf;

};

extern u8 rtw_read8(struct adapter *adapter, u32 addr);
extern u16 rtw_read16(struct adapter *adapter, u32 addr);
extern u32 rtw_read32(struct adapter *adapter, u32 addr);

extern int rtw_write8(struct adapter *adapter, u32 addr, u8 val);
extern int rtw_write16(struct adapter *adapter, u32 addr, u16 val);
extern int rtw_write32(struct adapter *adapter, u32 addr, u32 val);

extern u32 rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

int rtw_init_io_priv(struct adapter *padapter, void (*set_intf_ops)(struct adapter *padapter, struct _io_ops *pops));

#endif	/* _RTL8711_IO_H_ */
