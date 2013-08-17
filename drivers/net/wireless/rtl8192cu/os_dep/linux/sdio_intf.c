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
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>

#include <hal_init.h>
#include <sdio_hal.h>
#include <sdio_ops.h>
#include <linux/mmc/sdio_func.h> 
#include <linux/mmc/sdio_ids.h>
extern u32 rtw_start_drv_threads(_adapter *padapter);
extern void rtw_stop_drv_threads (_adapter *padapter);
extern u8 rtw_init_drv_sw(_adapter *padapter);
extern u8 rtw_free_drv_sw(_adapter *padapter);
extern void rtw_cancel_all_timer(_adapter *padapter);
extern struct net_device *rtw_init_netdev(_adapter *old_padapter);
extern void update_recvframe_attrib_from_recvstat(struct rx_pkt_attrib
*pattrib, struct recv_stat *prxstat);
static const struct sdio_device_id sdio_ids[] = {
	{ SDIO_DEVICE(0x024c, 0x8712)		},
//	{ SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN)		},
//	{ /* end: all zeroes */				},
};

typedef struct _driver_priv{
		struct sdio_driver r871xs_drv;
}drv_priv, *pdrv_priv;

void	sd_sync_int_hdl(struct sdio_func *func);

extern unsigned int sd_dvobj_init(_adapter * padapter){

	struct dvobj_priv *psddev=&padapter->dvobjpriv;
	struct sdio_func *func=psddev->func;
	int ret;
	_func_enter_;
	//_rtw_init_sema(&psddev->init_finish,0);
	sdio_claim_host(func);
	 ret=sdio_enable_func(func);
	if(ret){	
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("sd_dvobj_init: sdio_enable_func fail!!!!!\n"));
		return _FAIL;
	}
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("sd_dvobj_init: sdio_enable_func success!!!!!\n"));
	padapter->EepromAddressSize = 6;
	psddev->tx_block_mode=1;
	psddev->rx_block_mode=1;
	sdio_set_block_size(func, 512);
	psddev->block_transfer_len=512;
	psddev->blk_shiftbits=9;
	ret=sdio_claim_irq(func,sd_sync_int_hdl);
	sdio_release_host(func);
	psddev->sdio_himr=0xff;
	if(ret)	
		return _FAIL;
	_func_exit_;
	return _SUCCESS;
}

extern void sd_dvobj_deinit(_adapter * padapter)
{
       unsigned char data;    
	struct dvobj_priv *psddev=&padapter->dvobjpriv;
	struct sdio_func *func=psddev->func;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+SDIO deinit\n"));
	if(func !=0){
        sdio_claim_host(func);
       RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in sd_dvobj_deinit():sdio_claim_host !\n"));
//        sdio_release_irq(func);
        RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in  sd_dvobj_deinit():sdio_release_irq !\n"));
        sdio_disable_func(func);
      RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in  sd_dvobj_deinit():sdio_disable_func !\n"));
        sdio_release_host(func);
      RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in  sd_dvobj_deinit():sdio_release_host !\n"));


	}
	return;
}

uint sdbus_read_reg_int(struct intf_priv *pintfpriv, u32 addr, u32 cnt, void *pdata)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv*)pintfpriv->intf_dev;
	struct sdio_func *func = pdvobjpriv->func;
	u8 *mem = NULL;
	int status;

#ifdef CONFIG_IO_4B
	u32 addr_org = addr, addr_offset = 0;
	u32 cnt_org = cnt;
#endif

_func_enter_;

#ifdef CONFIG_IO_4B
	addr_offset = addr % 4;
	if (addr_offset) {
		addr = addr - addr_offset;
		cnt = cnt + addr_offset;
	}
	if (cnt % 4)
		cnt = ((cnt + 4) >> 2) << 2;
#endif

	mem = rtw_malloc(cnt);
	if (mem == NULL) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_emerg_,
			 ("SDIO_STATUS_NO_RESOURCES - memory alloc fail\n"));
		return _FAIL;
	}

	status = sdio_memcpy_fromio(func, mem, addr&0x1FFFF, cnt);
	if (status) {
		//error
		RT_TRACE(_module_hci_ops_os_c_, _drv_emerg_,
			 ("sdbus_read_reg_int error 0x%x\n"
			  "***** Addr = %x *****\n"
			  "***** Length = %d *****\n", status, addr, cnt));
		status = _FAIL;
	} else {
#ifdef CONFIG_IO_4B
		if (cnt != cnt_org)
			_rtw_memcpy(pdata, mem + addr_offset, cnt_org);
		else
#endif
		_rtw_memcpy(pdata, mem, cnt);
		status = _SUCCESS;
	}

	rtw_mfree(mem, cnt);

_func_exit_;

	return status;
}

void sdio_read_int(_adapter *padapter, u32 addr, u8 sz, void *pdata)
{
	struct io_queue	*pio_queue = (struct io_queue*)padapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	u32 ftaddr = 0, res;

_func_enter_;

//	RT_TRACE(_module_hci_ops_c_,_drv_err_,("sdio_read_int\n"));

	if ((_cvrt2ftaddr(addr, &ftaddr)) == _SUCCESS) {
		res = sdbus_read_reg_int(pintfpriv, ftaddr, sz, pdata);
		if (res != _SUCCESS) {
			RT_TRACE(_module_hci_ops_c_, _drv_emerg_, ("sdio_read_int fail!!!\n"));
		}
	} else {
		RT_TRACE(_module_hci_ops_c_, _drv_emerg_, (" sdio_read_int address translate error!!!\n"));
	}

_func_exit_;
}

uint sdbus_write_reg_int(struct intf_priv *pintfpriv, u32 addr, u32 cnt, void *pdata)
{
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv*)pintfpriv->intf_dev;
	struct sdio_func *func = pdvobjpriv->func;
	int status;
#ifdef CONFIG_IO_4B
	u32 addr_org = addr, addr_offset = 0;
	u32 cnt_org = cnt;
	void *pdata_org = pdata;
#endif

_func_enter_;

#ifdef CONFIG_IO_4B
	addr_offset = addr % 4;
	if (addr_offset) {
		addr = addr - addr_offset;
		cnt = cnt + addr_offset;
	}
	if (cnt % 4)
		cnt = ((cnt + 4) >> 2) << 2;
	if (cnt != cnt_org) {
		pdata = rtw_malloc(cnt);
		if (pdata == NULL) {
			RT_TRACE(_module_hci_ops_os_c_, _drv_emerg_,
				 ("SDIO_STATUS_NO_RESOURCES - rtw_malloc fail\n"));
			return _FAIL;
		}
		status = sdio_memcpy_fromio(func, pdata, addr&0x1FFFF, cnt);
		if (status) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_emerg_,
				 ("sdbus_write_reg_int read failed 0x%x\n "
				  "***** Addr = %x *****\n"
				  "***** Length = %d *****\n", status, addr, cnt));
			rtw_mfree(pdata, cnt);
			return _FAIL;
		}
		_rtw_memcpy(pdata + addr_offset, pdata_org, cnt_org);
		/* if data been modify between this read and write, may cause a problem */
	}
#endif
	status = sdio_memcpy_toio(func, addr&0x1FFFF, pdata, cnt);
	if (status) {
		//error
		RT_TRACE(_module_hci_ops_os_c_, _drv_emerg_,
			 ("sdbus_write_reg_int failed 0x%x\n"
			  "***** Addr = %x *****\n"
			  "***** Length = %d *****\n", status, addr, cnt));

		status = _FAIL;
	} else
		status = _SUCCESS;

#ifdef CONFIG_IO_4B
	if (cnt != cnt_org)
		rtw_mfree(pdata, cnt);
#endif

_func_exit_;

	return status;
}

void sdio_write_int(_adapter *padapter, u32 addr, u32 val, u8 sz)
{
	struct io_queue	*pio_queue = (struct io_queue*)padapter->pio_queue;
	struct intf_hdl	*pintfhdl = &pio_queue->intf;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;

	u32 ftaddr = 0, res;

_func_enter_;

//	RT_TRACE(_module_hci_ops_c_,_drv_err_,("sdio_write_int\n"));

	val = cpu_to_le32(val);

	if ((_cvrt2ftaddr(addr, &ftaddr)) == _SUCCESS) {
		res = sdbus_write_reg_int(pintfpriv, ftaddr, sz, &val);
		if (res != _SUCCESS) {
			RT_TRACE(_module_hci_ops_c_, _drv_emerg_, ("sdio_write_int fail!!!\n"));
		}
	} else {
		RT_TRACE(_module_hci_ops_c_, _drv_emerg_, ("sdio_write_int address translate error!!!\n"));
	}

_func_exit_;
}

int recvbuf2recvframe_s(_adapter *padapter, struct recv_buf *precvbuf)
{
//	_irqL irql;
	u8 *pbuf;
//	u8 bsumbit = _FALSE;
	uint pkt_len, pkt_offset;
	int transfer_len;
	struct recv_stat *prxstat;
	u16 pkt_cnt, drvinfo_sz;
	_queue *pfree_recv_queue;
	union recv_frame *precvframe = NULL,*plast_recvframe = NULL;
	struct recv_priv *precvpriv = &padapter->recvpriv;
//	struct intf_hdl *pintfhdl = &padapter->pio_queue->intf;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+recvbuf2recvframe()\n"));

	pfree_recv_queue = &(precvpriv->free_recv_queue);

	pbuf = (u8*)precvbuf->pbuf;

	prxstat = (struct recv_stat *)pbuf;
/*	{
		u8 i;
		DBG_8192C("\n-----recvbuf-----\n");
		for (i=0;i<64;i=i+8) {
			DBG_8192C("0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x\n",pbuf[i],pbuf[i+1],pbuf[i+2],pbuf[i+3],pbuf[i+4],pbuf[i+5],pbuf[i+6],pbuf[i+7]);
		}
		DBG_8192C("\n-----recvbuf end-----\n");
	}*/
	transfer_len = precvbuf->len;
	precvbuf->ref_cnt = 1;
	do {
		precvframe = NULL;
		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (precvframe == NULL){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe(), precvframe==NULL\n"));
			break;
		}
		if (plast_recvframe != NULL) {
			if (rtw_recv_entry(plast_recvframe) != _SUCCESS) {
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), rtw_recv_entry(precvframe) != _SUCCESS\n"));
			}
		}
		prxstat = (struct recv_stat*)pbuf;
		pkt_len = le32_to_cpu(prxstat->rxdw0&0x00003fff); //pkt_len = prxstat->frame_length;             

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rxdesc: offsset0:0x%08x, offsset4:0x%08x, offsset8:0x%08x, offssetc:0x%08x\n",prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		drvinfo_sz = le16_to_cpu((prxstat->rxdw0&0x000f0000)>>16);//uint 2^3 = 8 bytes
		drvinfo_sz = drvinfo_sz << 3;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("pkt_len=%d[0x%x] drvinfo_sz=%d[0x%x]\n", pkt_len, pkt_len, drvinfo_sz, drvinfo_sz));
		precvframe->u.hdr.precvbuf = precvbuf;
		precvframe->u.hdr.adapter = padapter;
		rtw_init_recvframe(precvframe, precvpriv);

		precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
		precvframe->u.hdr.rx_end = precvbuf->pend;
		update_recvframe_attrib_from_recvstat(&precvframe->u.hdr.attrib, prxstat);
		pkt_offset = pkt_len + drvinfo_sz + RXDESC_SIZE;

		recvframe_put(precvframe, pkt_len + drvinfo_sz + RXDESC_SIZE);
		recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);
/*		{
			u8 i;
			DBG_8192C("\n-----packet-----\n");
			for(i=0;i<32;i++){
			DBG_8192C("0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x:0x%.2x\n",precvframe->u.hdr.rx_data[i],precvframe->u.hdr.rx_data[i+1],precvframe->u.hdr.rx_data[i+2],precvframe->u.hdr.rx_data[i+3],precvframe->u.hdr.rx_data[i+4],precvframe->u.hdr.rx_data[i+5],precvframe->u.hdr.rx_data[i+6],precvframe->u.hdr.rx_data[i+7]);
			}
			DBG_8192C("\n-----packet end-----\n");
		}*/
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n precvframe->u.hdr.rx_head=%p precvframe->u.hdr.rx_data=%p precvframe->u.hdr.rx_tail=%p precvframe->u.hdr.rx_end=%p\n",precvframe->u.hdr.rx_head,precvframe->u.hdr.rx_data,precvframe->u.hdr.rx_tail,precvframe->u.hdr.rx_end));

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\npkt_offset=%d [1]\n",pkt_offset));
		pkt_offset = _RND512(pkt_offset);
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\npkt_offset=%d [2] transfer_len=%d\n",pkt_offset,transfer_len));
		transfer_len -= pkt_offset;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n transfer_len=%d \n",transfer_len));
		pbuf += pkt_offset;
		if (transfer_len > 0)
			precvbuf->ref_cnt++;
		plast_recvframe = precvframe;
		precvframe = NULL;
	} while (transfer_len > 0);

	if (plast_recvframe != NULL) {
		if (rtw_recv_entry(plast_recvframe) != _SUCCESS) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), rtw_recv_entry(precvframe) != _SUCCESS\n"));
		}
	}

	dev_kfree_skb_any(precvbuf->pskb);
	precvbuf->pskb = NULL;
	return _SUCCESS;
}

u32 read_pkt2recvbuf(PADAPTER padapter, u32 rd_cnt, struct recv_buf *precvbuf)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	u32 skb_buf_sz;
	if (rd_cnt < 1600)
		skb_buf_sz = 1600;
	else
		skb_buf_sz = rd_cnt;
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n read_pkt2recvbuf------skb_buf_sz=%d rd_cnt=%d\n",skb_buf_sz,rd_cnt));
//	if (precvbuf->pskb != NULL) {
//		dev_kfree_skb_any(precvbuf->pskb );
//	}

	//alloc skb
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
		precvbuf->pskb = dev_alloc_skb(skb_buf_sz);
#else
		precvbuf->pskb = netdev_alloc_skb(padapter->pnetdev, skb_buf_sz);
#endif
		if (precvbuf->pskb == NULL) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("==================init_recvbuf(): alloc_skb fail!\n"));
			return _FAIL;
		}

		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = precvbuf->pskb->tail;
		precvbuf->pend = precvbuf->pskb->end;
		precvbuf->pbuf = precvbuf->pskb->data;
	}
//	else {
//               RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("after init_recvbuf(): skb !=NULL!\n"));
//	}

	rtw_read_port(padapter, RTL8712_DMA_RX0FF, rd_cnt, (u8*)precvbuf);
	precvbuf->ptail = precvbuf->ptail + rd_cnt;
	precvbuf->len = rd_cnt;
	/*{
		u32 i;
		DBG_8192C("-----After read port[%d]-----\n",skb_buf_sz);
		for (i = 0; i < skb_buf_sz; i = i + 8) {
			DBG_8192C("0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\n",precvbuf->pbuf[i],precvbuf->pbuf[i+1],precvbuf->pbuf[i+2],precvbuf->pbuf[i+3],precvbuf->pbuf[i+4],precvbuf->pbuf[i+5],precvbuf->pbuf[i+6],precvbuf->pbuf[i+7]);
		}

		DBG_8192C("-----------\n");
	}*/
#if 1
	recvbuf2recvframe_s(padapter, precvbuf);
#else
{
	dev_kfree_skb_any(precvbuf->pskb);
	precvbuf->pskb = NULL;
	rtw_list_delete(&(precvbuf->list));
	rtw_list_insert_tail(&precvbuf->list, get_list_head(&precvpriv->free_recv_buf_queue));
	precvpriv->free_recv_buf_queue_cnt++;
}
#endif

	return _SUCCESS;
}

void sd_recv_rxfifo(PADAPTER padapter);
#if 0
void sd_recv_rxfifo(PADAPTER padapter)
{
//	u8 *pdata, *ptail, *pfixed_tail,*pfixed_head,*pfixed_end,blk_shift_bit;
	u16 rx_blknum;
	u32 blk_sz, cnt;//,remain,tmp_cnt;
	struct recv_priv *precvpriv;
//	struct recv_stat *prxstat;
	//union recv_frame *precvframe, *ppreframe = NULL; 
//	_queue *pfree_recv_queue, *ppending_recv_queue;
//	u8 tmp[2048];
	struct recv_buf *precvbuf;
	_list *precvbuf_head, *precvbuf_list;
	_irqL irql, rx_proc_irq;
//	uint pkt_len;
//	u16 drvinfo_sz;

	precvpriv = &padapter->recvpriv;
	blk_sz = padapter->dvobjpriv.block_transfer_len;
//	blk_shift_bit= (u8)padapter->dvobjpriv.blk_shiftbits;
//	pfree_recv_queue = &(precvpriv->free_recv_queue);
//	ppending_recv_queue = &(precvpriv->recv_pending_queue);

	rx_blknum = padapter->dvobjpriv.rxblknum;
//	_enter_hwio_critical(&padapter->dvobjpriv.rx_protect, &rx_proc_irq);
//	padapter->dvobjpriv.rxblknum=rtw_read16(padapter, SDIO_RX0_RDYBLK_NUM);
	sdio_read_int(padapter, SDIO_RX0_RDYBLK_NUM, 2, &padapter->dvobjpriv.rxblknum);
	if (rx_blknum>padapter->dvobjpriv.rxblknum) {
		cnt = (0x10000 - rx_blknum + padapter->dvobjpriv.rxblknum) * blk_sz;
	} else {
		cnt = (padapter->dvobjpriv.rxblknum-rx_blknum) * blk_sz;
	}
	RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("=====================sd_recv_rxfifo  padapter->dvobjpriv.rxblknum=%x Blk_Num = %x   cnt=%d",padapter->dvobjpriv.rxblknum, rx_blknum,cnt));
        
	if (cnt == 0) {
//		remain = 0;
		precvbuf = NULL;
		RT_TRACE(_module_hci_intfs_c_,_drv_info_,("---===============sd_recv_rxfifo padapter->dvobjpriv.rxblknum=0x%x padapter->dvobjpriv.rxblknum_rd=0x%x", padapter->dvobjpriv.rxblknum,padapter->dvobjpriv.rxblknum_rd));
		goto drop_pkt;
	}

	if(_rtw_queue_empty(&precvpriv->free_recv_buf_queue) == _TRUE)
	{
		precvbuf = NULL;
		RT_TRACE(_module_hci_intfs_c_,_drv_emerg_,("\n sd_recv_rxfifo : precvbuf= NULL precvpriv->free_recv_buf_queue_cnt=%d \n",precvpriv->free_recv_buf_queue_cnt));
		goto drop_pkt;
	}
	else
	{
		_enter_critical(&precvpriv->free_recv_buf_queue.lock, &irql);
		precvbuf_head = get_list_head(&precvpriv->free_recv_buf_queue);
		precvbuf_list = get_next(precvbuf_head);
		precvbuf = LIST_CONTAINOR(precvbuf_list, struct recv_buf, list);
		rtw_list_delete(&precvbuf->list);
		precvpriv->free_recv_buf_queue_cnt--;

		RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("\n sd_recv_rxfifo : precvbuf= 0x%p  dequeue: free_recv_buf_queue_cnt=%d\n",precvbuf,precvpriv->free_recv_buf_queue_cnt));
		_exit_critical(&precvpriv->free_recv_buf_queue.lock, &irql);
	}
	read_pkt2recvbuf(padapter, cnt, precvbuf);

	return;	

drop_pkt:

	if (cnt >0) {
		do{
			if (cnt > MAX_RECVBUF_SZ) {
				rtw_read_port(padapter, 0x10380000, MAX_RECVBUF_SZ, (u8 *)precvpriv->recvbuf_drop);
				RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("=========sd_recv_rxfifo precvbuf= NULL  no recvbuf    cnt=%d  tmp read %d",cnt,MAX_RECVBUF_SZ));
				cnt=cnt-MAX_RECVBUF_SZ;
			} else {
				rtw_read_port(padapter, 0x10380000, cnt, (u8 *)precvpriv->recvbuf_drop);
				RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("=========sd_recv_rxfifo precvbuf= NULL  no recvbuf    cnt=%d  tmp read(@) %d",cnt,cnt));
				cnt=0;
			}
		} while(cnt > 0);
	}

	return;
}
#endif
#if 0
void sd_c2h_hdl(PADAPTER padapter)
{
	u8 cmd_seq, pkt_num = 0;
	u16 tmp16, sz, cmd_len = 0;
	u32 rd_sz=0, cmd_sz = 0;//,ptr;
	struct evt_priv *pevtpriv = &padapter->evtpriv;
	pkt_num = rtw_read8(padapter, 0x102500BF);
//	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@ sd_c2h_hdl:pkt_num=%d",pkt_num));
get_next:
//	ptr=rtw_read32(padapter,0x102500e8);
//	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@ sd_c2h_hdl:C2H fifo RDPTR=0x%x",ptr));
//	ptr=rtw_read32(padapter,0x102500ec);
//	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@ sd_c2h_hdl:C2H fifo WTPTR=0x%x",ptr));
//	if(pkt_num==0x0 ){
//	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@ sd_c2h_hdl:cmd_pkt num=0x%x!",pkt_num));
//		return;
//	}
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@ sd_c2h_hdl:pkt_num=%d",pkt_num));
	//memset(pevtpriv->c2h_mem,0,512);
	rtw_read_port(padapter, RTL8712_DMA_C2HCMD, 512, pevtpriv->c2h_mem);
	cmd_sz = *(u16 *)&pevtpriv->c2h_mem[0];
	cmd_sz &= 0x3fff;
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl: cmd_sz=%d[0x%x]!",cmd_sz,cmd_sz));
	tmp16 = *(u16 *)&pevtpriv->c2h_mem[4];
	tmp16 &= 0x01ff;
	if (tmp16 !=0x1ff) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl: 0x1ff error[0x%x]!",pevtpriv->c2h_mem[4]));
		goto exit;
	}
	if((cmd_sz+24) >512){
		rtw_read_port(padapter, RTL8712_DMA_C2HCMD, (cmd_sz+24-512), pevtpriv->c2h_mem+512);
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl: read the second part of c2h event!"));
	}
	cmd_seq = pevtpriv->c2h_mem[27];
	cmd_seq &= 0x7f;
	if (pevtpriv->event_seq != cmd_seq) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl: pevtpriv->event_seq (%d) != c2hbuf seq(%d)",pevtpriv->event_seq,cmd_seq));
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("sd_c2h_hdl: pevtpriv->event_seq (%d) == c2hbuf seq(%d)",pevtpriv->event_seq,cmd_seq));
	}
	cmd_len = *(u16 *)&pevtpriv->c2h_mem[0];	
	cmd_len &= 0x3ff;
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("@sd_c2h_hdl: cmd_len=%d",cmd_len));
//	if(cmd_len){
//		memset(pevtpriv->c2h_mem+cmd_len,0,cmd_len);
//	rtw_read_port(padapter, RTL8712_DMA_C2HCMD, cmd_len, pevtpriv->c2h_mem+cmd_len);
//	}
//	pevtpriv->event_seq=pevtpriv->event_seq++;
//	if(pevtpriv->event_seq>127)
//		pevtpriv->event_seq=0;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl:!"));
	rxcmd_event_hdl(padapter,pevtpriv->c2h_mem);
	if (pkt_num > 1) {
		pkt_num--;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("sd_c2h_hdl: pkt_num=%d",pkt_num));
		goto get_next;
	}

exit:

	return;
}
#endif
void update_free_ffsz_int(_adapter *padapter )
{
	struct xmit_priv *pxmitpriv=&padapter->xmitpriv;
	RT_TRACE(_module_hci_ops_c_,_drv_err_,("\n====(before)=padapter->xmitpriv.public_pgsz=0x%x====update_free_ffsz:  free_pg=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x \n",
			padapter->xmitpriv.public_pgsz,
			pxmitpriv->free_pg[0],pxmitpriv->free_pg[1],pxmitpriv->free_pg[2],pxmitpriv->free_pg[3],
			pxmitpriv->free_pg[4],pxmitpriv->free_pg[5],pxmitpriv->free_pg[6],pxmitpriv->free_pg[7]));
//	rtw_read_mem(padapter,SDIO_BCNQ_FREEPG,8,pxmitpriv->free_pg);
	sdio_read_int(padapter, SDIO_BCNQ_FREEPG, 8, pxmitpriv->free_pg);
	padapter->xmitpriv.public_pgsz = pxmitpriv->free_pg[0];
	if (pxmitpriv->public_pgsz > pxmitpriv->init_pgsz) {
		pxmitpriv->init_pgsz = pxmitpriv->public_pgsz;
	}

	{
		u8 diff;
		if (pxmitpriv->public_pgsz > (pxmitpriv->init_pgsz - pxmitpriv->used_pgsz)) {
			RT_TRACE(_module_hci_ops_c_,_drv_err_,("\n====(0)=====update_free_ffsz: pxmitpriv->public_pgsz=0x%x pxmitpriv->init_pgsz=0x%x  pxmitpriv->used_pgsz=0x%x \n",pxmitpriv->public_pgsz ,pxmitpriv->init_pgsz, pxmitpriv->used_pgsz));
			diff = pxmitpriv->public_pgsz - (pxmitpriv->init_pgsz - pxmitpriv->used_pgsz);
			pxmitpriv->used_pgsz = pxmitpriv->used_pgsz - diff;
//			pxmitpriv->required_pgsz = pxmitpriv->required_pgsz - diff;
			RT_TRACE(_module_hci_ops_c_,_drv_err_,("\n====(1)=====update_free_ffsz: pxmitpriv->public_pgsz =0x%x diff=0x%x pxmitpriv->used_pgsz=0x%x pxmitpriv->required_pgsz=0x%x\n",pxmitpriv->public_pgsz,diff,pxmitpriv->used_pgsz,pxmitpriv->required_pgsz) );
		} else {

		}
	}

	RT_TRACE(_module_hci_ops_c_,_drv_err_,("\n====(after)=====update_free_ffsz:  free_pg=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x:0x%x \n",
		pxmitpriv->free_pg[0],pxmitpriv->free_pg[1],pxmitpriv->free_pg[2],pxmitpriv->free_pg[3],
		pxmitpriv->free_pg[4],pxmitpriv->free_pg[5],pxmitpriv->free_pg[6],pxmitpriv->free_pg[7]));

	return;
}

void sd_int_dpc(PADAPTER padapter);
#if 0
void sd_int_dpc(PADAPTER padapter)
{
	uint 	tasks= (padapter->IsrContent /*& padapter->ImrContent*/);
//	rtw_write16(padapter,SDIO_HIMR,0);

	RT_TRACE(_module_hci_intfs_c_,_drv_notice_,(" sd_int_dpc[0x%x] ",padapter->IsrContent));

	if ((tasks & _VOQ_AVAL_IND) || (tasks & _VIQ_AVAL_IND) || (tasks & _BEQ_AVAL_IND) || (tasks & _BKQ_AVAL_IND) || (tasks & _BMCQ_AVAL_IND)) {
		RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("==============INT : _TXDONE"));
		update_free_ffsz_int(padapter);
	} else {
		if (((padapter->xmitpriv.init_pgsz - padapter->xmitpriv.used_pgsz) > 0 && (padapter->xmitpriv.init_pgsz - padapter->xmitpriv.used_pgsz) < 0x2f) || padapter->xmitpriv.required_pgsz > 0) {
			RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("==============padapter->xmitpriv.public_pgsz[0x%x] <30 ",padapter->xmitpriv.public_pgsz));
			update_free_ffsz_int(padapter);
		}
	}

	if(tasks & _C2HCMD)
	{
//		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("======C2H_CMD========"));
		padapter->IsrContent  ^= _C2HCMD;
		sd_c2h_hdl(padapter);
//		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("======C2H_CMD[end]========"));
	}

	if(tasks & _RXDONE)
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("==============INT : _RXDONE"));
		padapter->IsrContent  ^= _RXDONE;
		sd_recv_rxfifo(padapter);
	}

}
#endif
void sd_sync_int_hdl(struct sdio_func *func)
{
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	_adapter *padapter = (_adapter*)psdpriv->padapter;
	u16 tmp16;
//	uint tasks;

_func_enter_;

	if ((padapter->bDriverStopped ==_TRUE) || (padapter->bSurpriseRemoved == _TRUE)) {
		goto exit;
	}

	//padapter->IsrContent=rtw_read16(padapter, SDIO_HISR);
	sdio_read_int(padapter, SDIO_HISR, 2, &psdpriv->sdio_hisr);

	if (psdpriv->sdio_hisr & psdpriv->sdio_himr)
	{
		sdio_write_int(padapter, SDIO_HIMR, 0, 2);
		sd_int_dpc(padapter);
		sdio_write_int(padapter, SDIO_HIMR, psdpriv->sdio_himr, 2);

		sdio_read_int(padapter, SDIO_HIMR, 2, &tmp16);
		if (tmp16 != psdpriv->sdio_himr)
			sdio_write_int(padapter, SDIO_HIMR, psdpriv->sdio_himr, 2);
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_info_, ("<=========== sd_sync_int_hdl(): not our INT"));
	}
exit:

_func_exit_;

	return;
}

static int r871xs_drv_init(struct sdio_func *func, const struct sdio_device_id *id)
{
	_adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	struct net_device *pnetdev;

	RT_TRACE(_module_hci_intfs_c_,_drv_alert_,("+871x - drv_init:id=0x%p func->vendor=0x%x func->device=0x%x\n",id,func->vendor,func->device));

	//step 1.
	pnetdev = rtw_init_netdev(NULL);
	if (!pnetdev)
		goto error;	

	padapter = rtw_netdev_priv(pnetdev);
	pdvobjpriv = &padapter->dvobjpriv;
	pdvobjpriv->padapter = padapter;
	pdvobjpriv->func = func;
	sdio_set_drvdata(func, pdvobjpriv);
	SET_NETDEV_DEV(pnetdev, &func->dev);


	//step 2.
	if (alloc_io_queue(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Can't init io_reqs\n"));
		goto error;
	}


#if 0  //temp remove
	//step 3.
	if (loadparam(padapter, pnetdev) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Read Parameter Failed!\n"));
		goto error;
	}
#endif

	//step 4.
	//dvobj_init(padapter);
	padapter->dvobj_init = &sd_dvobj_init;
	padapter->dvobj_deinit = &sd_dvobj_deinit;
	padapter->halpriv.hal_bus_init = &sd_hal_bus_init;
	padapter->halpriv.hal_bus_deinit = &sd_hal_bus_deinit;

	if (padapter->dvobj_init == NULL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Initialize dvobjpriv.dvobj_init error!!!\n"));
		goto error;
	}

	if (padapter->dvobj_init(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n initialize device object priv Failed!\n"));			
		goto error;
	}


	//step 6.
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));			
		goto error;
	}

#if 1
{
	//step 7.
	u8 mac[6];
	mac[0]=0x00;
	mac[1]=0xe0;
	mac[2]=0x4c;
	mac[3]=0x87;
	mac[4]=0x66;
	mac[5]=0x55;

	_rtw_memcpy(pnetdev->dev_addr, mac/*padapter->eeprompriv.mac_addr*/, ETH_ALEN);
	RT_TRACE(_module_hci_intfs_c_,_drv_info_,("pnetdev->dev_addr=0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\n",pnetdev->dev_addr[0],pnetdev->dev_addr[1],pnetdev->dev_addr[2],pnetdev->dev_addr[3],pnetdev->dev_addr[4],pnetdev->dev_addr[5]));
}
#endif
	//step 8.
	/* Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("register_netdev() failed\n"));
		goto error;
	}
	RT_TRACE(_module_hci_intfs_c_,_drv_info_,("register_netdev() success\n"));
	RT_TRACE(_module_hci_intfs_c_,_drv_notice_,("-drv_init - Adapter->bDriverStopped=%d, Adapter->bSurpriseRemoved=%d\n",padapter->bDriverStopped, padapter->bSurpriseRemoved));
	RT_TRACE(_module_hci_intfs_c_,_drv_info_,("-871xs_drv - drv_init, success!\n"));

	return 0;

error:

	if (padapter->dvobj_deinit == NULL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Initialize dvobjpriv.dvobj_deinit error!!!\n"));
	} else {
		padapter->dvobj_deinit(padapter);
	} 	  

	if (pnetdev) {
		unregister_netdev(pnetdev);
		rtw_free_netdev(pnetdev);
	}

	RT_TRACE(_module_hci_intfs_c_, _drv_emerg_, ("-871x_sdio - drv_init, fail!\n"));

	return -1;
}

void rtl871x_intf_stop(_adapter *padapter)
{
	// Disable interrupt, also done in rtl8712_hal_deinit
//	rtw_write16(padapter, SDIO_HIMR, 0x00);
}

void r871x_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+r871x_dev_unload\n"));

	if (padapter->bup == _TRUE)
	{
#if 0
		//s1.
		if (pnetdev) {
			netif_carrier_off(pnetdev);
			netif_stop_queue(pnetdev);
		}
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complelte s1!\n"));

		//s2.
		// indicate-disconnect if necssary (free all assoc-resources)
		// dis-assoc from assoc_sta (optional)
		rtw_indicate_disconnect(padapter);
		rtw_free_network_queue(padapter, _TRUE);
#endif

		padapter->bDriverStopped = _TRUE;
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complete s2!\n"));

		//s3.
		rtl871x_intf_stop(padapter);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complete s3!\n"));

		//s4.
		rtw_stop_drv_threads(padapter);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complete s4!\n"));

		//s5.
		if (padapter->bSurpriseRemoved == _FALSE) {
			rtl871x_hal_deinit(padapter);
			padapter->bSurpriseRemoved = _TRUE;
		}
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complelt s5!\n"));


		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("@ r871x_dev_unload:complete s6!\n"));

		padapter->bup = _FALSE;
	}
	else {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == _FALSE\n" ));
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-r871x_dev_unload\n"));
}

static void r8712s_dev_remove(struct sdio_func *func)
{
	_adapter *padapter = (_adapter*) (((struct dvobj_priv*)sdio_get_drvdata(func))->padapter);
	struct net_device *pnetdev = (struct net_device *)padapter->pnetdev;

_func_exit_;

	if (padapter)
	{
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+dev_remove()\n"));

//		padapter->bSurpriseRemoved = _TRUE;

		if (pnetdev)
			unregister_netdev(pnetdev); //will call netdev_close()

		rtw_cancel_all_timer(padapter);
		
		r871x_dev_unload(padapter);
		//s6.
		if (padapter->dvobj_deinit) {
			padapter->dvobj_deinit(padapter); // call sd_dvobj_deinit()
		} else {
			RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize hcipriv.hci_priv_init error!!!\n"));
		}

		rtw_free_drv_sw(padapter);
		//after rtw_free_drv_sw(), padapter has beed freed, don't refer to it.
		
		sdio_claim_host(func);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in dev_remove():sdio_claim_host !\n"));
		sdio_release_irq(func);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in dev_remove():sdio_release_irq !\n"));
		sdio_disable_func(func);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in dev_remove():sdio_disable_func !\n"));
		sdio_release_host(func);
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" in dev_remove():sdio_release_host !\n"));
	}
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-dev_remove()\n"));

_func_exit_;

	return;
}

static drv_priv drvpriv = {	
		.r871xs_drv.probe		= r871xs_drv_init,
		.r871xs_drv.remove		= r8712s_dev_remove,
		.r871xs_drv.name		= "rtl871x_sdio_wlan",
		.r871xs_drv.id_table	= sdio_ids,
};	


static int __init r8712s_drv_entry(void)
{
	int status;
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+8712s_sdio - drv_entry\n"));
	status = sdio_register_driver(&drvpriv.r871xs_drv);
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-8712_sdio - drv_entry, status=%d\n", status));	

	return status;
}

static void __exit r8712s_drv_halt(void)
{
	int ret;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+8712_sdio - drv_halt\n"));
	sdio_unregister_driver(&drvpriv.r871xs_drv);	// call r8712s_dev_remove()
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-8712_sdio - drv_halt\n"));

}


module_init(r8712s_drv_entry);
module_exit(r8712s_drv_halt);
