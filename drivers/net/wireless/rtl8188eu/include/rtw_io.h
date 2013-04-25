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

#ifndef _RTW_IO_H_
#define _RTW_IO_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <osdep_intf.h>

#ifdef PLATFORM_LINUX
#include <asm/byteorder.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/list.h>
//#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#ifdef CONFIG_USB_HCI
#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21))
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#define rtw_usb_buffer_alloc(dev, size, mem_flags, dma) usb_alloc_coherent((dev), (size), (mem_flags), (dma))
#define rtw_usb_buffer_free(dev, size, addr, dma) usb_free_coherent((dev), (size), (addr), (dma))
#else
#define rtw_usb_buffer_alloc(dev, size, mem_flags, dma) usb_buffer_alloc((dev), (size), (mem_flags), (dma))
#define rtw_usb_buffer_free(dev, size, addr, dma) usb_buffer_free((dev), (size), (addr), (dma))
#endif


#endif //CONFIG_USB_HCI

#endif //PLATFORM_LINUX


#define NUM_IOREQ		8

#ifdef PLATFORM_WINDOWS
#define MAX_PROT_SZ	64
#endif
#ifdef PLATFORM_LINUX
#define MAX_PROT_SZ	(64-16)
#endif

#define _IOREADY			0
#define _IO_WAIT_COMPLETE   1
#define _IO_WAIT_RSP        2

// IO COMMAND TYPE
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



// IO STATUS TYPE
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

#define IO_WR_BURST(x)		(_IO_WRITE_ | _IO_SYNC_ | _IO_BURST_ | ( (x) & _IOSZ_MASK_))
#define IO_RD_BURST(x)		(_IO_SYNC_ | _IO_BURST_ | ( (x) & _IOSZ_MASK_))



//below is for the intf_option bit defition...

#define _INTF_ASYNC_	BIT(0)	//support async io

struct intf_priv;
struct intf_hdl;
struct io_queue;

struct _io_ops
{
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

		int (*_write_port_sync)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

		u32 (*_write_scsi)(struct intf_hdl *pintfhdl,u32 cnt, u8 *pmem);

		void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
		void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
		
};

struct io_req {	
	_list	list;
	u32	addr;	
	volatile u32	val;
	u32	command;
	u32	status;
	u8	*pbuf;	
	_sema	sema;

#ifdef PLATFORM_OS_CE
#ifdef CONFIG_USB_HCI
	// URB handler for rtw_write_mem
	USB_TRANSFER usb_transfer_write_mem;
#endif
#endif
	
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt);
	u8 *cnxt;	

#ifdef PLATFORM_OS_XP	
	PMDL pmdl;
	PIRP  pirp; 

#ifdef CONFIG_SDIO_HCI
	PSDBUS_REQUEST_PACKET sdrp;
#endif	

#endif	


};

struct	intf_hdl {

/*	
	u32	intf_option;
	u32	bus_status;
	u32	do_flush;
	u8	*adapter;
	u8	*intf_dev;	
	struct intf_priv	*pintfpriv;
	u8	cnt;
	void (*intf_hdl_init)(u8 *priv);
	void (*intf_hdl_unload)(u8 *priv);
	void (*intf_hdl_open)(u8 *priv);
	void (*intf_hdl_close)(u8 *priv);
	struct	_io_ops	io_ops;
	//u8 intf_status;//moved to struct intf_priv
	u16 len;
	u16 done_len;	
*/
	_adapter *padapter;
	struct dvobj_priv *pintf_dev;//	pointer to &(padapter->dvobjpriv);

	struct _io_ops	io_ops;

};

struct reg_protocol_rd {

#ifdef CONFIG_LITTLE_ENDIAN	

	//DW1
	u32		NumOfTrans:4;
	u32		Reserved1:4;
	u32		Reserved2:24;
	//DW2
	u32		ByteCount:7;
	u32		WriteEnable:1;		//0:read, 1:write
	u32		FixOrContinuous:1;	//0:continuous, 1: Fix
	u32		BurstMode:1;
	u32		Byte1Access:1;
	u32		Byte2Access:1;
	u32		Byte4Access:1;
	u32		Reserved3:3;
	u32		Reserved4:16;
	//DW3
	u32		BusAddress;
	//DW4
	//u32		Value;
#else


//DW1
	u32 Reserved1  :4;
	u32 NumOfTrans :4;	

	u32 Reserved2  :24;	

	//DW2
	u32 WriteEnable : 1;
	u32 ByteCount :7;	


	u32 Reserved3 : 3;
	u32 Byte4Access : 1;	

	u32 Byte2Access : 1;
	u32 Byte1Access : 1;	
	u32 BurstMode :1 ;	
	u32 FixOrContinuous : 1;	

	u32 Reserved4 : 16;

	//DW3
	u32		BusAddress;

	//DW4
	//u32		Value;

#endif
	
};


struct reg_protocol_wt {
	

#ifdef CONFIG_LITTLE_ENDIAN

	//DW1
	u32		NumOfTrans:4;
	u32		Reserved1:4;
	u32		Reserved2:24;
	//DW2
	u32		ByteCount:7;
	u32		WriteEnable:1;		//0:read, 1:write
	u32		FixOrContinuous:1;	//0:continuous, 1: Fix
	u32		BurstMode:1;
	u32		Byte1Access:1;
	u32		Byte2Access:1;
	u32		Byte4Access:1;
	u32		Reserved3:3;
	u32		Reserved4:16;
	//DW3
	u32		BusAddress;
	//DW4
	u32		Value;

#else
	//DW1
	u32 Reserved1  :4;
	u32 NumOfTrans :4;	

	u32 Reserved2  :24;	

	//DW2
	u32 WriteEnable : 1;
	u32 ByteCount :7;	
		
	u32 Reserved3 : 3;
	u32 Byte4Access : 1;	

	u32 Byte2Access : 1;
	u32 Byte1Access : 1;	
	u32 BurstMode :1 ;	
	u32 FixOrContinuous : 1;	

	u32 Reserved4 : 16;

	//DW3
	u32		BusAddress;

	//DW4
	u32		Value;

#endif

};



/*
Below is the data structure used by _io_handler

*/

struct io_queue {	
	_lock	lock;	
	_list  	free_ioreqs;	
	_list		pending;		//The io_req list that will be served in the single protocol read/write.	
	_list		processing;
	u8	*free_ioreqs_buf; // 4-byte aligned
	u8	*pallocated_free_ioreqs_buf;
	struct	intf_hdl	intf;
};

struct io_priv{
	
	_adapter *padapter;	
		
	struct intf_hdl intf;

};

extern uint ioreq_flush(_adapter *adapter, struct io_queue *ioqueue);
extern void sync_ioreq_enqueue(struct io_req *preq,struct io_queue *ioqueue);
extern uint sync_ioreq_flush(_adapter *adapter, struct io_queue *ioqueue);


extern uint free_ioreq(struct io_req *preq, struct io_queue *pio_queue);
extern struct io_req *alloc_ioreq(struct io_queue *pio_q);

extern uint register_intf_hdl(u8 *dev, struct intf_hdl *pintfhdl);
extern void unregister_intf_hdl(struct intf_hdl *pintfhdl);

extern void _rtw_attrib_read(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_attrib_write(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

extern u8 _rtw_read8(_adapter *adapter, u32 addr);
extern u16 _rtw_read16(_adapter *adapter, u32 addr);
extern u32 _rtw_read32(_adapter *adapter, u32 addr);
extern void _rtw_read_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_read_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_read_port_cancel(_adapter *adapter);


extern int _rtw_write8(_adapter *adapter, u32 addr, u8 val);
extern int _rtw_write16(_adapter *adapter, u32 addr, u16 val);
extern int _rtw_write32(_adapter *adapter, u32 addr, u32 val);
extern int _rtw_writeN(_adapter *adapter, u32 addr, u32 length, u8 *pdata);

extern int _rtw_write8_async(_adapter *adapter, u32 addr, u8 val);
extern int _rtw_write16_async(_adapter *adapter, u32 addr, u16 val);
extern int _rtw_write32_async(_adapter *adapter, u32 addr, u32 val);

extern void _rtw_write_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_write_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern int _rtw_write_port_sync(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void _rtw_write_port_cancel(_adapter *adapter);

#ifdef DBG_IO
bool match_read_sniff_ranges(u16 addr, u16 len);
bool match_write_sniff_ranges(u16 addr, u16 len);

extern u8 dbg_rtw_read8(_adapter *adapter, u32 addr, const char *caller, const int line);
extern u16 dbg_rtw_read16(_adapter *adapter, u32 addr, const char *caller, const int line);
extern u32 dbg_rtw_read32(_adapter *adapter, u32 addr, const char *caller, const int line);

extern int dbg_rtw_write8(_adapter *adapter, u32 addr, u8 val, const char *caller, const int line);
extern int dbg_rtw_write16(_adapter *adapter, u32 addr, u16 val, const char *caller, const int line);
extern int dbg_rtw_write32(_adapter *adapter, u32 addr, u32 val, const char *caller, const int line);
extern int dbg_rtw_writeN(_adapter *adapter, u32 addr ,u32 length , u8 *data, const char *caller, const int line);

#define rtw_read8(adapter, addr) dbg_rtw_read8((adapter), (addr), __FUNCTION__, __LINE__)
#define rtw_read16(adapter, addr) dbg_rtw_read16((adapter), (addr), __FUNCTION__, __LINE__)
#define rtw_read32(adapter, addr) dbg_rtw_read32((adapter), (addr), __FUNCTION__, __LINE__)
#define rtw_read_mem(adapter, addr, cnt, mem) _rtw_read_mem((adapter), (addr), (cnt), (mem))
#define rtw_read_port(adapter, addr, cnt, mem) _rtw_read_port((adapter), (addr), (cnt), (mem))
#define rtw_read_port_cancel(adapter) _rtw_read_port_cancel((adapter))

#define  rtw_write8(adapter, addr, val) dbg_rtw_write8((adapter), (addr), (val), __FUNCTION__, __LINE__)
#define  rtw_write16(adapter, addr, val) dbg_rtw_write16((adapter), (addr), (val), __FUNCTION__, __LINE__)
#define  rtw_write32(adapter, addr, val) dbg_rtw_write32((adapter), (addr), (val), __FUNCTION__, __LINE__)
#define  rtw_writeN(adapter, addr, length, data) dbg_rtw_writeN((adapter), (addr), (length), (data), __FUNCTION__, __LINE__)

#define rtw_write8_async(adapter, addr, val) _rtw_write8_async((adapter), (addr), (val))
#define rtw_write16_async(adapter, addr, val) _rtw_write16_async((adapter), (addr), (val))
#define rtw_write32_async(adapter, addr, val) _rtw_write32_async((adapter), (addr), (val))

#define rtw_write_mem(adapter, addr, cnt, mem) _rtw_write_mem((adapter), addr, cnt, mem)
#define rtw_write_port(adapter, addr, cnt, mem) _rtw_write_port(adapter, addr, cnt, mem)
#define rtw_write_port_sync(adapter, addr, cnt, mem) _rtw_write_port_sync((adapter), (addr), (cnt), (mem))
#define rtw_write_port_cancel(adapter) _rtw_write_port_cancel(adapter)
#else //DBG_IO
#define rtw_read8(adapter, addr) _rtw_read8((adapter), (addr))
#define rtw_read16(adapter, addr) _rtw_read16((adapter), (addr))
#define rtw_read32(adapter, addr) _rtw_read32((adapter), (addr))
#define rtw_read_mem(adapter, addr, cnt, mem) _rtw_read_mem((adapter), (addr), (cnt), (mem))
#define rtw_read_port(adapter, addr, cnt, mem) _rtw_read_port((adapter), (addr), (cnt), (mem))
#define rtw_read_port_cancel(adapter) _rtw_read_port_cancel((adapter))

#define  rtw_write8(adapter, addr, val) _rtw_write8((adapter), (addr), (val))
#define  rtw_write16(adapter, addr, val) _rtw_write16((adapter), (addr), (val))
#define  rtw_write32(adapter, addr, val) _rtw_write32((adapter), (addr), (val))
#define  rtw_writeN(adapter, addr, length, data) _rtw_writeN((adapter), (addr), (length), (data))

#define rtw_write8_async(adapter, addr, val) _rtw_write8_async((adapter), (addr), (val))
#define rtw_write16_async(adapter, addr, val) _rtw_write16_async((adapter), (addr), (val))
#define rtw_write32_async(adapter, addr, val) _rtw_write32_async((adapter), (addr), (val))

#define rtw_write_mem(adapter, addr, cnt, mem) _rtw_write_mem((adapter), (addr), (cnt), (mem))
#define rtw_write_port(adapter, addr, cnt, mem) _rtw_write_port((adapter), (addr), (cnt), (mem))
#define rtw_write_port_sync(adapter, addr, cnt, mem) _rtw_write_port_sync((adapter), (addr), (cnt), (mem))
#define rtw_write_port_cancel(adapter) _rtw_write_port_cancel((adapter))
#endif //DBG_IO

extern void rtw_write_scsi(_adapter *adapter, u32 cnt, u8 *pmem);

//ioreq 
extern void ioreq_read8(_adapter *adapter, u32 addr, u8 *pval);
extern void ioreq_read16(_adapter *adapter, u32 addr, u16 *pval);	
extern void ioreq_read32(_adapter *adapter, u32 addr, u32 *pval);
extern void ioreq_write8(_adapter *adapter, u32 addr, u8 val);
extern void ioreq_write16(_adapter *adapter, u32 addr, u16 val);
extern void ioreq_write32(_adapter *adapter, u32 addr, u32 val);


extern uint async_read8(_adapter *adapter, u32 addr, u8 *pbuff,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt); 
extern uint async_read16(_adapter *adapter, u32 addr,  u8 *pbuff,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt); 
extern uint async_read32(_adapter *adapter, u32 addr,  u8 *pbuff,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt); 

extern void async_read_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void async_read_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);

extern void async_write8(_adapter *adapter, u32 addr, u8 val,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern void async_write16(_adapter *adapter, u32 addr, u16 val,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);
extern void async_write32(_adapter *adapter, u32 addr, u32 val,
	void (*_async_io_callback)(_adapter *padater, struct io_req *pio_req, u8 *cnxt), u8 *cnxt);

extern void async_write_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);
extern void async_write_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem);


int rtw_init_io_priv(_adapter *padapter);


extern uint alloc_io_queue(_adapter *adapter);
extern void free_io_queue(_adapter *adapter);
extern void async_bus_io(struct io_queue *pio_q);
extern void bus_sync_io(struct io_queue *pio_q);
extern u32 _ioreq2rwmem(struct io_queue *pio_q);
extern void dev_power_down(_adapter * Adapter, u8 bpwrup);

/*
#define RTL_R8(reg)		rtw_read8(padapter, reg)
#define RTL_R16(reg)            rtw_read16(padapter, reg)
#define RTL_R32(reg)            rtw_read32(padapter, reg)
#define RTL_W8(reg, val8)       rtw_write8(padapter, reg, val8)
#define RTL_W16(reg, val16)     rtw_write16(padapter, reg, val16)
#define RTL_W32(reg, val32)     rtw_write32(padapter, reg, val32)
*/

/*
#define RTL_W8_ASYNC(reg, val8) rtw_write32_async(padapter, reg, val8)
#define RTL_W16_ASYNC(reg, val16) rtw_write32_async(padapter, reg, val16)
#define RTL_W32_ASYNC(reg, val32) rtw_write32_async(padapter, reg, val32)

#define RTL_WRITE_BB(reg, val32)	phy_SetUsbBBReg(padapter, reg, val32)
#define RTL_READ_BB(reg)	phy_QueryUsbBBReg(padapter, reg)
*/

#define PlatformEFIOWrite1Byte(_a,_b,_c)		\
	rtw_write8(_a,_b,_c)
#define PlatformEFIOWrite2Byte(_a,_b,_c)		\
	rtw_write16(_a,_b,_c)
#define PlatformEFIOWrite4Byte(_a,_b,_c)		\
	rtw_write32(_a,_b,_c)

#define PlatformEFIORead1Byte(_a,_b)		\
		rtw_read8(_a,_b)
#define PlatformEFIORead2Byte(_a,_b)		\
		rtw_read16(_a,_b)
#define PlatformEFIORead4Byte(_a,_b)		\
		rtw_read32(_a,_b)

#endif	//_RTL8711_IO_H_

