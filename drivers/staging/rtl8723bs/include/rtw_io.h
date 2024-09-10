/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef _RTW_IO_H_
#define _RTW_IO_H_

/*
	For prompt mode accessing, caller shall free io_req
	Otherwise, io_handler will free io_req
*/

/* below is for the intf_option bit definition... */

struct intf_priv;
struct intf_hdl;
struct io_queue;

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

		void (*_sync_irp_protocol_rw)(struct io_queue *pio_q);

		u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);

		u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
		u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

		u32 (*_write_scsi)(struct intf_hdl *pintfhdl, u32 cnt, u8 *pmem);

		void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
		void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
};

struct io_req {
	struct list_head	list;
	u32 addr;
	volatile u32 val;
	u32 command;
	u32 status;
	u8 *pbuf;

	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt);
	u8 *cnxt;
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

/*
Below is the data structure used by _io_handler

*/

struct io_queue {
	spinlock_t	lock;
	struct list_head	free_ioreqs;
	struct list_head		pending;		/* The io_req list that will be served in the single protocol read/write. */
	struct list_head		processing;
	u8 *free_ioreqs_buf; /*  4-byte aligned */
	u8 *pallocated_free_ioreqs_buf;
	struct	intf_hdl	intf;
};

struct io_priv {

	struct adapter *padapter;

	struct intf_hdl intf;

};

extern uint ioreq_flush(struct adapter *adapter, struct io_queue *ioqueue);
extern void sync_ioreq_enqueue(struct io_req *preq, struct io_queue *ioqueue);
extern uint sync_ioreq_flush(struct adapter *adapter, struct io_queue *ioqueue);


extern uint free_ioreq(struct io_req *preq, struct io_queue *pio_queue);
extern struct io_req *alloc_ioreq(struct io_queue *pio_q);

extern uint register_intf_hdl(u8 *dev, struct intf_hdl *pintfhdl);
extern void unregister_intf_hdl(struct intf_hdl *pintfhdl);

extern void _rtw_attrib_read(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_attrib_write(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

extern u8 rtw_read8(struct adapter *adapter, u32 addr);
extern u16 rtw_read16(struct adapter *adapter, u32 addr);
extern u32 rtw_read32(struct adapter *adapter, u32 addr);

extern int rtw_write8(struct adapter *adapter, u32 addr, u8 val);
extern int rtw_write16(struct adapter *adapter, u32 addr, u16 val);
extern int rtw_write32(struct adapter *adapter, u32 addr, u32 val);

extern u32 rtw_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

extern void rtw_write_scsi(struct adapter *adapter, u32 cnt, u8 *pmem);

/* ioreq */
extern void ioreq_read8(struct adapter *adapter, u32 addr, u8 *pval);
extern void ioreq_read16(struct adapter *adapter, u32 addr, u16 *pval);
extern void ioreq_read32(struct adapter *adapter, u32 addr, u32 *pval);
extern void ioreq_write8(struct adapter *adapter, u32 addr, u8 val);
extern void ioreq_write16(struct adapter *adapter, u32 addr, u16 val);
extern void ioreq_write32(struct adapter *adapter, u32 addr, u32 val);


extern uint async_read8(struct adapter *adapter, u32 addr, u8 *pbuff,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern uint async_read16(struct adapter *adapter, u32 addr,  u8 *pbuff,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern uint async_read32(struct adapter *adapter, u32 addr,  u8 *pbuff,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);

extern void async_read_mem(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void async_read_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

extern void async_write8(struct adapter *adapter, u32 addr, u8 val,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern void async_write16(struct adapter *adapter, u32 addr, u16 val,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern void async_write32(struct adapter *adapter, u32 addr, u32 val,
	void (*_async_io_callback)(struct adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);

extern void async_write_mem(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void async_write_port(struct adapter *adapter, u32 addr, u32 cnt, u8 *pmem);


int rtw_init_io_priv(struct adapter *padapter, void (*set_intf_ops)(struct adapter *padapter, struct _io_ops *pops));


extern uint alloc_io_queue(struct adapter *adapter);
extern void free_io_queue(struct adapter *adapter);
extern void async_bus_io(struct io_queue *pio_q);
extern void bus_sync_io(struct io_queue *pio_q);
extern u32 _ioreq2rwmem(struct io_queue *pio_q);
extern void dev_power_down(struct adapter *Adapter, u8 bpwrup);

#endif	/* _RTL8711_IO_H_ */
