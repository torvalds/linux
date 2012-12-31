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
/*

The purpose of rtl871x_io.c

a. provides the API 

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface


Compiler Flag Option:

1. CONFIG_SDIO_HCI:
    a. USE_SYNC_IRP:  Only sync operations are provided.
    b. USE_ASYNC_IRP:Both sync/async operations are provided.

2. CONFIG_USB_HCI:
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

3. CONFIG_CFIO_HCI:
   b. USE_SYNC_IRP: Only sync operations are provided.


Only sync read/write_mem operations are provided.

jackson@realtek.com.tw

*/

#define _RTL871X_IO_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl871x_io.h>
#include <osdep_intf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifdef PLATFORM_LINUX
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
#include <linux/smp_lock.h>
#endif
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#endif

#ifdef CONFIG_SDIO_HCI
#include <sdio_ops.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

uint _init_intf_hdl(_adapter *padapter, struct intf_hdl *pintf_hdl)
{	
	struct	intf_priv	*pintf_priv;
	void (*set_intf_option)(u32 *poption) = NULL;
	void (*set_intf_funs)(struct intf_hdl *pintf_hdl);
	void (*set_intf_ops)(struct _io_ops	*pops);
	uint (*init_intf_priv)(struct intf_priv *pintfpriv);

_func_enter_;
	
#ifdef CONFIG_SDIO_HCI
	set_intf_option = &(sdio_set_intf_option);
	set_intf_funs = &(sdio_set_intf_funs);
	set_intf_ops = &sdio_set_intf_ops;
	init_intf_priv = &sdio_init_intf_priv;
#endif

#ifdef CONFIG_USB_HCI
	set_intf_option = &(usb_set_intf_option);
	set_intf_funs = &(usb_set_intf_funs);
	set_intf_ops = &usb_set_intf_ops;
	init_intf_priv = &usb_init_intf_priv;
#endif

	pintf_priv = pintf_hdl->pintfpriv =(struct intf_priv *) _malloc(sizeof(struct intf_priv));
	
	if (pintf_priv == NULL)
		goto _init_intf_hdl_fail;

	pintf_hdl->adapter = (u8*)padapter;
	
	set_intf_option(&pintf_hdl->intf_option);
	set_intf_funs(pintf_hdl);
	set_intf_ops(&pintf_hdl->io_ops);

	pintf_priv->intf_dev = (u8 *)&(padapter->dvobjpriv);
	
	if (init_intf_priv(pintf_priv) == _FAIL)
		goto _init_intf_hdl_fail;
	
_func_exit_;

	return _SUCCESS;

_init_intf_hdl_fail:
	
	if (pintf_priv) {
		_mfree((u8 *)pintf_priv, sizeof(struct intf_priv));
	}
	
_func_exit_;

	return _FAIL;
	
}

void _unload_intf_hdl(struct intf_priv *pintfpriv)
{
	void (*unload_intf_priv)(struct intf_priv *pintfpriv);

_func_enter_;	
		
#ifdef CONFIG_SDIO_HCI
	unload_intf_priv = &sdio_unload_intf_priv;
#endif

#ifdef CONFIG_USB_HCI	
      unload_intf_priv = &usb_unload_intf_priv;
#endif 
	
	unload_intf_priv(pintfpriv);

	if(pintfpriv){
		_mfree((u8 *)pintfpriv, sizeof(struct intf_priv));
	}
	
_func_exit_;
}

uint register_intf_hdl (u8 *dev, struct intf_hdl 	*pintfhdl)
{
	_adapter *adapter = (_adapter *)dev;
	
	pintfhdl->intf_option = 0;
	pintfhdl->adapter = dev;
	pintfhdl->intf_dev = (u8 *)&(adapter->dvobjpriv);
	//pintfhdl->len = 0;
	//pintfhdl->done_len = 0;
	//pintfhdl->do_flush = _FALSE;
	
_func_enter_;

	if (_init_intf_hdl(adapter, pintfhdl) == _FALSE)
		goto register_intf_hdl_fail;

_func_exit_;
	
	return _SUCCESS;

register_intf_hdl_fail:
	
	// shall release all the allocated resources here...
	//if(pintfhdl) //deleted
	//	_mfree((u8 *)pintfhdl, (sizeof (struct intf_hdl)));
	
_func_exit_;		

	return _FALSE;
}

void unregister_intf_hdl(struct intf_hdl *pintfhdl)
{
_func_enter_;

	_unload_intf_hdl(pintfhdl->pintfpriv);	

	_memset((u8 *)pintfhdl, 0, sizeof(struct intf_hdl));	
	
_func_exit_;
}

/*

Must use critical section to protect alloc_ioreq and free_ioreq,
due to the irq level possibilities of the callers.
This is to guarantee the atomic context of the service.

*/
struct io_req *alloc_ioreq(struct io_queue *pio_q)
{
	_irqL	irqL;
	_list	*phead = &pio_q->free_ioreqs;
	struct io_req *preq = NULL;

_func_enter_;

	_enter_critical(&pio_q->lock, &irqL);

	if (is_list_empty(phead) == _FALSE)
	{
		preq = LIST_CONTAINOR(get_next(phead), struct io_req, list);
		list_delete(&preq->list);

		//_memset((u8 *)preq, 0, sizeof(struct io_req));//!!!

		_init_listhead(&preq->list);	
		_init_sema(&preq->sema, 0);

	}

	_exit_critical(&pio_q->lock, &irqL);

_func_exit_;

	return preq;
}

/*

	must use critical section to protect alloc_ioreq and free_ioreq,
	due to the irq level possibilities of the callers.
	This is to guarantee the atomic context of the service.

*/
uint free_ioreq(struct io_req *preq, struct io_queue *pio_q)
{
	_irqL	irqL;
	_list	*phead = &pio_q->free_ioreqs;	

_func_enter_;

	_enter_critical(&pio_q->lock, &irqL);

	list_insert_tail(&(preq->list), phead);

	_exit_critical(&pio_q->lock, &irqL);

_func_exit_;

	return _SUCCESS;
}

#if 0
void query_ioreq_sz(struct io_req *pio_req, u32 *w_sz, u32 *r_sz)
{
	*w_sz = 2;
	*r_sz = 0;

_func_enter_;

	if (pio_req->command & _IO_WRITE_)
	{
		if (pio_req->command & _IO_BURST_)
			*w_sz = (*w_sz) + (((pio_req->command) & _IOSZ_MASK_) >> 2);
		else
			*w_sz = (*w_sz) + 1;
	}
	else
	{
	#if 0	
		if ((*r_sz) == 0)
			*r_sz = 1;
	#endif
		
		*r_sz = 2;
		
		if (pio_req->command & _IO_BURST_)
		{
			*r_sz = (*r_sz) + (((pio_req->command) & _IOSZ_MASK_) >> 2);			
		}
		else
		{
			*r_sz = (*r_sz) + 1;
		}
	}

_func_exit_;
}

/*

*/
static u32 _fillin_iocmd(struct io_req *pio_req, volatile u32 **ppcmdbuf)
{
	u32 w_cnt = 2;
	u32 cmd;
	volatile u32 *pcmdbuf = *ppcmdbuf;

_func_enter_;	

	cmd = ((pio_req->command) &  _IO_CMDMASK_);
	
	if (pio_req->command & _IO_BURST_)
		cmd |= ((pio_req->command) & _IOSZ_MASK_);

	*pcmdbuf = cmd;
	pcmdbuf++;
	*pcmdbuf = pio_req->addr;
	pcmdbuf++;	

	if (pio_req->command & _IO_WRITE_)
	{
		if (pio_req->command & _IO_BURST_)
		{
			
			_memcpy((u8 *)pcmdbuf, pio_req->pbuf, ((pio_req->command) & _IOSZ_MASK_));
			
			pcmdbuf += (((pio_req->command) & _IOSZ_MASK_) >> 2);
			
			w_cnt += (((pio_req->command) & _IOSZ_MASK_) >> 2);
		}
		else
		{
			*pcmdbuf = pio_req->val;
			pcmdbuf ++;
			w_cnt ++;
		}
	}
	else
	{
#if 0		
		if (*r_cnt == 0)
			*r_cnt = 1;
		
		*r_cnt = (*r_cnt) + 2;
		
		if (pio_req->command & _IO_BURST_)
		{
			*r_cnt += (((pio_req->command) & _IOSZ_MASK_) >> 2);			
		}
		else
		{
			*r_cnt = (*r_cnt) + 1;
		}
#endif

	}

	*ppcmdbuf = pcmdbuf;

_func_exit_;

	return w_cnt;
}

u32 _ioreq2rwmem(struct io_queue *pio_q)
{
	_list *phead, *plist;

	struct io_req *pio_req;

	struct intf_hdl *pintf = &(pio_q->intf);

	struct intf_priv *pintfpriv = pintf->pintfpriv;
	volatile u32 *pnum_reqs = (volatile u32 *)pintfpriv->io_rwmem;
	volatile u32 *pcmd = (volatile u32 *)pintfpriv->io_rwmem;

	u32 r_sz = 0, all_r_sz = 1,  w_sz = 0, all_w_sz = 1;

_func_enter_;

	phead = &(pio_q->pending);

	plist = get_next(phead);

	_memset((u8 *)pintfpriv->io_rwmem, 0, pintfpriv->max_iosz);
	
	pcmd++;
	while (1)
	{
		if ((is_list_empty(phead)) == _TRUE)
			break;
		
		pio_req = LIST_CONTAINOR(plist, struct io_req, list);
		
		query_ioreq_sz(pio_req, &w_sz, &r_sz);

		if ((all_r_sz + r_sz) > (pintfpriv->max_iosz >> 2))
			break;
		if ((all_w_sz + w_sz) > (pintfpriv->max_iosz >> 2))
			break;

		list_delete(&(pio_req->list));
		
		 _fillin_iocmd(pio_req, &pcmd);	

		list_insert_tail(&(pio_req->list),&(pio_q->processing));

		plist = get_next(phead);

		all_r_sz += r_sz;
		all_w_sz += w_sz;
		
		*pnum_reqs = *pnum_reqs + 1;
	}

	pintfpriv->io_rsz = all_r_sz;
	pintfpriv->io_wsz = all_w_sz;
	
	if ((all_r_sz > 1 ) || (all_w_sz > 1)) {
		_func_exit_;
		return _TRUE;
	} else {
		_func_exit_;
		return _FALSE;
	}
}
#endif


uint alloc_io_queue(_adapter *adapter)
{
	u32 i;
	struct io_queue *pio_queue; 
	struct io_req *pio_req;

_func_enter_;

	pio_queue = (struct io_queue *)_malloc (sizeof (struct io_queue));

	if (pio_queue == NULL) {
		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_,("\n  alloc_io_queue:pio_queue == NULL !!!!\n"));
		goto alloc_io_queue_fail;	
	}

	_init_listhead(&pio_queue->free_ioreqs);
	_init_listhead(&pio_queue->processing);
	_init_listhead(&pio_queue->pending);

	_spinlock_init(&pio_queue->lock);

	pio_queue->pallocated_free_ioreqs_buf = (u8 *)_malloc(NUM_IOREQ *(sizeof (struct io_req)) + 4);

	if ((pio_queue->pallocated_free_ioreqs_buf) == NULL) {
		RT_TRACE(_module_rtl871x_io_c_,_drv_err_,("\n  alloc_io_queue:(pio_queue->pallocated_free_ioreqs_buf) == NULL!!!!\n"));
		goto alloc_io_queue_fail;
	}

	_memset(pio_queue->pallocated_free_ioreqs_buf, 0, (NUM_IOREQ *(sizeof (struct io_req)) + 4));

	pio_queue->free_ioreqs_buf = pio_queue->pallocated_free_ioreqs_buf + 4 
			- ((u32 )(pio_queue->pallocated_free_ioreqs_buf)  & 3);

	pio_req = (struct io_req *)(pio_queue->free_ioreqs_buf);

	for(i = 0; i < 	NUM_IOREQ; i++)
	{
		_init_listhead(&pio_req->list);

		_init_sema(&pio_req->sema, 0);

#if defined ( PLATFORM_OS_XP) && defined (CONFIG_SDIO_HCI)
	pio_req->pirp = IoAllocateIrp(adapter->dvobjpriv.nextdevstacksz+2, FALSE);
	if (pio_req->pirp == NULL)
		goto alloc_io_queue_fail;
	pio_req->pmdl=IoAllocateMdl((u8 *)&pio_req->val, 4, FALSE, FALSE, NULL);
	if (pio_req->pmdl== NULL) {
		RT_TRACE(_module_rtl871x_io_c_,_drv_err_,("alloc_io_queue : MDL is NULL.\n "));	
		goto alloc_io_queue_fail;
	}
	MmBuildMdlForNonPagedPool(pio_req->pmdl);
	pio_req->sdrp = ExAllocatePool(NonPagedPool, sizeof(SDBUS_REQUEST_PACKET));
	if (pio_req->sdrp == NULL) {
		RT_TRACE(_module_rtl871x_io_c_,_drv_err_,("alloc_io_queue : sdrp is NULL.\n "));	
		goto alloc_io_queue_fail;
	}
#endif
		list_insert_tail(&pio_req->list, &pio_queue->free_ioreqs);

		pio_req++;	
	}		

	if ((register_intf_hdl((u8*)adapter, &(pio_queue->intf))) == _FAIL) {
		RT_TRACE(_module_rtl871x_ioctl_c_,_drv_err_,("\n  alloc_io_queue:register_intf_hdl == _FAIL !!!!\n"));
		goto alloc_io_queue_fail;
	}

	adapter->pio_queue = pio_queue;

_func_exit_;

	return _SUCCESS;
	
alloc_io_queue_fail:

	if (pio_queue) {
		if (pio_queue->pallocated_free_ioreqs_buf)
			_mfree(pio_queue->pallocated_free_ioreqs_buf, NUM_IOREQ *(sizeof (struct io_req)) + 4);	

		_mfree((u8*)pio_queue, sizeof (struct io_queue));
	}

	adapter->pio_queue = NULL;

_func_exit_;

	return _FAIL;
}

void mfree_io_queue_lock(struct io_queue	*pio_queue)
{
	u32 i;
	struct io_req *pio_req;

	_spinlock_free(&pio_queue->lock);

	pio_req = (struct io_req *)(pio_queue->free_ioreqs_buf);

	for (i = 0; i < NUM_IOREQ; i++) {
		_free_sema(&(pio_req->sema));
	}
}

void free_io_queue(_adapter *adapter)
{
	struct io_queue *pio_queue = (struct io_queue *)(adapter->pio_queue);

_func_enter_;

	RT_TRACE(_module_rtl871x_ioctl_c_,_drv_info_,("free_io_queue\n"));
	
	if (pio_queue) {
		mfree_io_queue_lock(pio_queue);
		if (pio_queue->pallocated_free_ioreqs_buf)
			_mfree(pio_queue->pallocated_free_ioreqs_buf, NUM_IOREQ *(sizeof (struct io_req)) + 4);	
		adapter->pio_queue = NULL;
		unregister_intf_hdl(&pio_queue->intf);
		_mfree((u8*)pio_queue, sizeof (struct io_queue));		
	}

_func_exit_;
}

