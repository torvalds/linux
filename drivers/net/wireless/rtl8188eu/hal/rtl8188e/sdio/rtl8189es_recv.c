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
#define _RTL8189ES_RECV_C_

#include <drv_conf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#include <drv_types.h>
#include <recv_osdep.h>
#include <rtl8188e_hal.h>

static void rtl8188es_recv_tasklet(void *priv);

static s32 initrecvbuf(struct recv_buf *precvbuf, PADAPTER padapter)
{
	_rtw_init_listhead(&precvbuf->list);
	_rtw_spinlock_init(&precvbuf->recvbuf_lock);

	precvbuf->adapter = padapter;

	return _SUCCESS;
}

static void freerecvbuf(struct recv_buf *precvbuf)
{
	_rtw_spinlock_free(&precvbuf->recvbuf_lock);
}

/*
 * Initialize recv private variable for hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
s32 rtl8188es_init_recv_priv(PADAPTER padapter)
{
	s32			res;
	u32			i, n;
	struct recv_priv	*precvpriv;
	struct recv_buf		*precvbuf;


	res = _SUCCESS;
	precvpriv = &padapter->recvpriv;

	//3 1. init recv buffer
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);
	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);

	n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
	precvpriv->pallocated_recv_buf = rtw_zmalloc(n);
	if (precvpriv->pallocated_recv_buf == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("alloc recv_buf fail!\n"));
		goto exit;
	}

	precvpriv->precv_buf = (u8*)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);

	// init each recv buffer
	precvbuf = (struct recv_buf*)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++)
	{
		res = initrecvbuf(precvbuf, padapter);
		if (res == _FAIL)
			break;

		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL) {
			freerecvbuf(precvbuf);
			break;
		}

		rtw_list_insert_tail(&precvbuf->list, &precvpriv->free_recv_buf_queue.queue);

		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = i;

	if (res == _FAIL)
		goto initbuferror;

	//3 2. init tasklet
#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))rtl8188es_recv_tasklet,
	     (unsigned long)padapter);
#endif

	goto exit;

initbuferror:
	precvbuf = (struct recv_buf*)precvpriv->precv_buf;
	if (precvbuf) {
		n = precvpriv->free_recv_buf_queue_cnt;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++)
		{
			rtw_list_delete(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			freerecvbuf(precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		rtw_mfree(precvpriv->pallocated_recv_buf, n);
		precvpriv->pallocated_recv_buf = NULL;
	}

exit:
	return res;
}

/*
 * Free recv private variable of hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
void rtl8188es_free_recv_priv(PADAPTER padapter)
{
	u32			i, n;
	struct recv_priv	*precvpriv;
	struct recv_buf		*precvbuf;


	precvpriv = &padapter->recvpriv;

	//3 1. kill tasklet
#ifdef PLATFORM_LINUX
	tasklet_kill(&precvpriv->recv_tasklet);
#endif

	//3 2. free all recv buffers
	precvbuf = (struct recv_buf*)precvpriv->precv_buf;
	if (precvbuf) {
		n = NR_RECVBUFF;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++)
		{
			rtw_list_delete(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			freerecvbuf(precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		rtw_mfree(precvpriv->pallocated_recv_buf, n);
		precvpriv->pallocated_recv_buf = NULL;
	}
}

static void rtl8188es_recv_tasklet(void *priv)
{
	PADAPTER				padapter;
	PHAL_DATA_TYPE			pHalData;
	struct recv_priv		*precvpriv;
	struct recv_buf			*precvbuf;
	union recv_frame		*precvframe;
	struct recv_frame_hdr	*phdr;
	struct rx_pkt_attrib	*pattrib;
	u8			*ptr;
	_pkt		*ppkt;
	u32			pkt_offset;
	_irqL		irql;


	padapter = (PADAPTER)priv;
	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;
	
	do {
		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf) break;

		ptr = precvbuf->pdata;

		while (ptr < precvbuf->ptail)
		{
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("%s: no enough recv frame!\n",__FUNCTION__));
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

				// The case of can't allocte recvframe should be temporary,
				// schedule again and hope recvframe is available next time.
#ifdef PLATFORM_LINUX
				tasklet_schedule(&precvpriv->recv_tasklet);
#endif
				return;
			}

			phdr = &precvframe->u.hdr;
			pattrib = &phdr->attrib;

			//rx desc parsing
			update_recvframe_attrib_88e(precvframe, (struct recv_stat*)ptr);

			// fix Hardware RX data error, drop whole recv_buffer
			if ((!(pHalData->ReceiveConfig & RCR_ACRC32)) && pattrib->crc_err)
			{
				#if !(MP_DRIVER==1)
				DBG_8192C("%s()-%d: RX Warning! rx CRC ERROR !!\n", __FUNCTION__, __LINE__);
				#endif
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->pkt_len;

			if ((ptr + pkt_offset) > precvbuf->ptail) {
				DBG_8192C("%s()-%d: : next pkt len(%p,%d) exceed ptail(%p)!\n", __FUNCTION__, __LINE__, ptr, pkt_offset, precvbuf->ptail);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			if ((pattrib->crc_err) || (pattrib->icv_err))
			{
				DBG_8192C("%s: crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			}
			else
			{
				ppkt = skb_clone(precvbuf->pskb, GFP_ATOMIC);
				if (ppkt == NULL)
				{
					RT_TRACE(_module_rtl871x_recv_c_, _drv_crit_, ("%s: no enough memory to allocate SKB!\n",__FUNCTION__));
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
					rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

					// The case of can't allocte skb is serious and may never be recovered,
					// once bDriverStopped is enable, this task should be stopped.
					if (padapter->bDriverStopped == _FALSE) {
#ifdef PLATFORM_LINUX
						tasklet_schedule(&precvpriv->recv_tasklet);
#endif
					}

					return;
				}

				phdr->pkt = ppkt;
				phdr->len = 0;
				phdr->rx_head = precvbuf->phead;
				phdr->rx_data = phdr->rx_tail = precvbuf->pdata;
				phdr->rx_end = precvbuf->pend;

				recvframe_put(precvframe, pkt_offset);
				recvframe_pull(precvframe, RXDESC_SIZE + pattrib->drvinfo_sz);

				if (pHalData->ReceiveConfig & RCR_APPFCS)
					recvframe_pull_tail(precvframe, IEEE80211_FCS_LEN);

				// move to drv info position
				ptr += RXDESC_SIZE;

				// update drv info
				if (pHalData->ReceiveConfig & RCR_APP_BA_SSN) {
//					rtl8723s_update_bassn(padapter, pdrvinfo);
					ptr += 4;
				}

				if( (pattrib->physt) && (pattrib->pkt_rpt_type == NORMAL_RX))
					update_recvframe_phyinfo_88e(precvframe, (struct phy_stat*)ptr);


				if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet
				{
					//printk("rx normal pkt\n");
					if (rtw_recv_entry(precvframe) != _SUCCESS)
					{
						RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("%s: rtw_recv_entry(precvframe) != _SUCCESS\n",__FUNCTION__));
					}
				}
				else{ // pkt_rpt_type == TX_REPORT1-CCX, TX_REPORT2-TX RTP,HIS_REPORT-USB HISR RTP

					//enqueue recvframe to txrtp queue
					if(pattrib->pkt_rpt_type == TX_REPORT1){
						printk("rx CCX \n");
					}
					else if(pattrib->pkt_rpt_type == TX_REPORT2){
						//printk("rx TX RPT \n");
						ODM_RA_TxRPT2Handle_8188E(
									&pHalData->odmpriv,
									precvframe->u.hdr.rx_data,
									pattrib->pkt_len,
									pattrib->MacIDValidEntry[0],
									pattrib->MacIDValidEntry[1]
									);

					}
					/*
					else if(pattrib->pkt_rpt_type == HIS_REPORT){
						printk("rx USB HISR \n");						
					}*/

					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);					

				}
			}

			// Page size of receive package is 128 bytes alignment =>DMA AGG
			// refer to _InitTransferPageSize()
			pkt_offset = _RND128(pkt_offset);
			precvbuf->pdata += pkt_offset;
			ptr = precvbuf->pdata;

		}

		dev_kfree_skb_any(precvbuf->pskb);
		precvbuf->pskb = NULL;
		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);

	} while (1);

}

