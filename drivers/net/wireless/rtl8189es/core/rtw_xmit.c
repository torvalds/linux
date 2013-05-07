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
#define _RTW_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <circ_buf.h>
#include <ip.h>

#ifdef CONFIG_TDLS
#include <rtl8192c_hal.h>
#endif //CONFIG_TDLS

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifdef PLATFORM_WINDOWS
#include <if_ether.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif


static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

#ifdef CONFIG_TDLS
extern unsigned char MCS_rate_2R[16];
extern unsigned char MCS_rate_1R[16];
#endif //CONFIG_TDLS

static void _init_txservq(struct tx_servq *ptxservq)
{
_func_enter_;
	_rtw_init_listhead(&ptxservq->tx_pending);
	_rtw_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;
_func_exit_;		
}


void	_rtw_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{	
	
_func_enter_;

	_rtw_memset((unsigned char *)psta_xmitpriv, 0, sizeof (struct sta_xmit_priv));

	_rtw_spinlock_init(&psta_xmitpriv->lock);
	
	//for(i = 0 ; i < MAX_NUMBLKS; i++)
	//	_init_txservq(&(psta_xmitpriv->blk_q[i]));

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	_rtw_init_listhead(&psta_xmitpriv->legacy_dz);
	_rtw_init_listhead(&psta_xmitpriv->apsd);
	
_func_exit_;	

}

s32	_rtw_init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	sint	res=_SUCCESS;   

_func_enter_;   	

	// We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	//_rtw_memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv));
	
	_rtw_spinlock_init(&pxmitpriv->lock);
	_rtw_init_sema(&pxmitpriv->xmit_sema, 0);
	_rtw_init_sema(&pxmitpriv->terminate_xmitthread_sema, 0);

	/* 
	Please insert all the queue initializaiton using _rtw_init_queue below
	*/

	pxmitpriv->adapter = padapter;
	
	//for(i = 0 ; i < MAX_NUMBLKS; i++)
	//	_rtw_init_queue(&pxmitpriv->blk_strms[i]);
	
	_rtw_init_queue(&pxmitpriv->be_pending);
	_rtw_init_queue(&pxmitpriv->bk_pending);
	_rtw_init_queue(&pxmitpriv->vi_pending);
	_rtw_init_queue(&pxmitpriv->vo_pending);
	_rtw_init_queue(&pxmitpriv->bm_pending);

	//_rtw_init_queue(&pxmitpriv->legacy_dz_queue);
	//_rtw_init_queue(&pxmitpriv->apsd_queue);

	_rtw_init_queue(&pxmitpriv->free_xmit_queue);


	/*	
	Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME, 
	and initialize free_xmit_frame below.
	Please also apply  free_txobj to link_up all the xmit_frames...
	*/

	pxmitpriv->pallocated_frame_buf = rtw_zvmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);
	
	if (pxmitpriv->pallocated_frame_buf  == NULL){
		pxmitpriv->pxmit_frame_buf =NULL;
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("alloc xmit_frame fail!\n"));	
		res= _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_frame_buf), 4);
	//pxmitpriv->pxmit_frame_buf = pxmitpriv->pallocated_frame_buf + 4 -
	//						((SIZE_PTR) (pxmitpriv->pallocated_frame_buf) &3);

	pxframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;

	for (i = 0; i < NR_XMITFRAME; i++)
	{
		_rtw_init_listhead(&(pxframe->list));

		pxframe->padapter = padapter;
		pxframe->frame_tag = NULL_FRAMETAG;

		pxframe->pkt = NULL;		

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;
 
		rtw_list_insert_tail(&(pxframe->list), &(pxmitpriv->free_xmit_queue.queue));

		pxframe++;
	}

	pxmitpriv->free_xmitframe_cnt = NR_XMITFRAME;

	pxmitpriv->frag_len = MAX_FRAG_THRESHOLD;


	//init xmit_buf
	_rtw_init_queue(&pxmitpriv->free_xmitbuf_queue);
	_rtw_init_queue(&pxmitpriv->pending_xmitbuf_queue);

	pxmitpriv->pallocated_xmitbuf = rtw_zvmalloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4);
	
	if (pxmitpriv->pallocated_xmitbuf  == NULL){
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("alloc xmit_buf fail!\n"));
		res= _FAIL;
		goto exit;
	}

	pxmitpriv->pxmitbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmitbuf), 4);
	//pxmitpriv->pxmitbuf = pxmitpriv->pallocated_xmitbuf + 4 -
	//						((SIZE_PTR) (pxmitpriv->pallocated_xmitbuf) &3);

	pxmitbuf = (struct xmit_buf*)pxmitpriv->pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++)
	{
		_rtw_init_listhead(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->ext_tag = _FALSE;

/*
		pxmitbuf->pallocated_buf = rtw_zmalloc(MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ);
		if (pxmitbuf->pallocated_buf == NULL)
		{
			res = _FAIL;
			goto exit;
		}

		pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);
		//pxmitbuf->pbuf = pxmitbuf->pallocated_buf + XMITBUF_ALIGN_SZ -((SIZE_PTR) (pxmitbuf->pallocated_buf) &(XMITBUF_ALIGN_SZ-1));
*/

		if((res=rtw_os_xmit_resource_alloc(padapter, pxmitbuf,(MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ))) == _FAIL) {
			res= _FAIL;
			goto exit;
		}

#ifdef CONFIG_SDIO_HCI
		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMITBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif

		pxmitbuf->flags = XMIT_VO_QUEUE;

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmitbuf_queue.queue));
		#ifdef DBG_XMIT_BUF
		pxmitbuf->no=i;
		#endif

		pxmitbuf++;
		
	}

	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	// Init xmit extension buff
	_rtw_init_queue(&pxmitpriv->free_xmit_extbuf_queue);

	pxmitpriv->pallocated_xmit_extbuf = rtw_zvmalloc(NR_XMIT_EXTBUFF * sizeof(struct xmit_buf) + 4);
	
	if (pxmitpriv->pallocated_xmit_extbuf  == NULL){
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("alloc xmit_extbuf fail!\n"));
		res= _FAIL;
		goto exit;
	}

	pxmitpriv->pxmit_extbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmit_extbuf), 4);

	pxmitbuf = (struct xmit_buf*)pxmitpriv->pxmit_extbuf;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++)
	{
		_rtw_init_listhead(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->ext_tag = _TRUE;

/*
		pxmitbuf->pallocated_buf = rtw_zmalloc(MAX_XMIT_EXTBUF_SZ);
		if (pxmitbuf->pallocated_buf == NULL)
		{
			res = _FAIL;
			goto exit;
		}

		pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitbuf->pallocated_buf), 4);
*/		

		if((res=rtw_os_xmit_resource_alloc(padapter, pxmitbuf,MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ)) == _FAIL) {
			res= _FAIL;
			goto exit;
		}
		
#ifdef CONFIG_SDIO_HCI
		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMIT_EXTBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmit_extbuf_queue.queue));
		#ifdef DBG_XMIT_BUF_EXT
		pxmitbuf->no=i;
		#endif
		pxmitbuf++;
		
	}

	pxmitpriv->free_xmit_extbuf_cnt = NR_XMIT_EXTBUFF;

	rtw_alloc_hwxmits(padapter);
	rtw_init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

#ifdef CONFIG_USB_HCI
	pxmitpriv->txirp_cnt=1;

	_rtw_init_sema(&(pxmitpriv->tx_retevt), 0);

	//per AC pending irp
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;
#endif

	rtw_hal_init_xmit_priv(padapter);

exit:

_func_exit_;	

	return res;
}

void  rtw_mfree_xmit_priv_lock (struct xmit_priv *pxmitpriv);
void  rtw_mfree_xmit_priv_lock (struct xmit_priv *pxmitpriv)
{
	_rtw_spinlock_free(&pxmitpriv->lock);
	_rtw_free_sema(&pxmitpriv->xmit_sema);
	_rtw_free_sema(&pxmitpriv->terminate_xmitthread_sema);

	_rtw_spinlock_free(&pxmitpriv->be_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bk_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vi_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vo_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bm_pending.lock);

	//_rtw_spinlock_free(&pxmitpriv->legacy_dz_queue.lock);
	//_rtw_spinlock_free(&pxmitpriv->apsd_queue.lock);

	_rtw_spinlock_free(&pxmitpriv->free_xmit_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->free_xmitbuf_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->pending_xmitbuf_queue.lock);
}


void _rtw_free_xmit_priv (struct xmit_priv *pxmitpriv)
{
       int i;
      _adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame	*pxmitframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

 _func_enter_;   

	rtw_hal_free_xmit_priv(padapter);
 
	rtw_mfree_xmit_priv_lock(pxmitpriv);
 
 	if(pxmitpriv->pxmit_frame_buf==NULL)
		goto out;
	
	for(i=0; i<NR_XMITFRAME; i++)
	{	
		rtw_os_xmit_complete(padapter, pxmitframe);

		pxmitframe++;
	}		
	
	for(i=0; i<NR_XMITBUFF; i++)
	{
		rtw_os_xmit_resource_free(padapter, pxmitbuf,(MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ));

		//if(pxmitbuf->pallocated_buf)
		//	rtw_mfree(pxmitbuf->pallocated_buf, MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ);
		
		pxmitbuf++;
	}

	if(pxmitpriv->pallocated_frame_buf) {
		rtw_vmfree(pxmitpriv->pallocated_frame_buf, NR_XMITFRAME * sizeof(struct xmit_frame) + 4);
	}
	

	if(pxmitpriv->pallocated_xmitbuf) {
		rtw_vmfree(pxmitpriv->pallocated_xmitbuf, NR_XMITBUFF * sizeof(struct xmit_buf) + 4);
	}

	// free xmit extension buff
	_rtw_spinlock_free(&pxmitpriv->free_xmit_extbuf_queue.lock);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;
	for(i=0; i<NR_XMIT_EXTBUFF; i++)
	{
		rtw_os_xmit_resource_free(padapter, pxmitbuf,(MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ));

		//if(pxmitbuf->pallocated_buf)
		//	rtw_mfree(pxmitbuf->pallocated_buf, MAX_XMIT_EXTBUF_SZ);
		
		pxmitbuf++;
	}

	if(pxmitpriv->pallocated_xmit_extbuf) {
		rtw_vmfree(pxmitpriv->pallocated_xmit_extbuf, NR_XMIT_EXTBUFF * sizeof(struct xmit_buf) + 4);	
	}

	rtw_free_hwxmits(padapter);

out:	

_func_exit_;		

}

static void update_attrib_vcs_info(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u32	sz;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info	*psta = pattrib->psta;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	if (pattrib->nr_frags != 1)
	{
		sz = padapter->xmitpriv.frag_len;
	}
	else //no frag
	{
		sz = pattrib->last_txcmdsz;
	}

	// (1) RTS_Threshold is compared to the MPDU, not MSDU.
	// (2) If there are more than one frag in  this MSDU, only the first frag uses protection frame.
	//		Other fragments are protected by previous fragment.
	//		So we only need to check the length of first fragment.
	if(pmlmeext->cur_wireless_mode < WIRELESS_11_24N  || padapter->registrypriv.wifi_spec)
	{
		if(sz > padapter->registrypriv.rts_thresh)
		{
			pattrib->vcs_mode = RTS_CTS;
		}
		else
		{
			if(psta->rtsen)
				pattrib->vcs_mode = RTS_CTS;
			else if(psta->cts2self)
				pattrib->vcs_mode = CTS_TO_SELF;
			else
				pattrib->vcs_mode = NONE_VCS;
		}
	}
	else
	{
		while (_TRUE)
		{
#if 0 //Todo
			//check IOT action
			if(pHTInfo->IOTAction & HT_IOT_ACT_FORCED_CTS2SELF)
			{
				pattrib->vcs_mode = CTS_TO_SELF;
				pattrib->rts_rate = MGN_24M;
				break;
			}
			else if(pHTInfo->IOTAction & (HT_IOT_ACT_FORCED_RTS|HT_IOT_ACT_PURE_N_MODE))
			{
				pattrib->vcs_mode = RTS_CTS;
				pattrib->rts_rate = MGN_24M;
				break;
			}
#endif

			//IOT action
			if((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_ATHEROS) && (pattrib->ampdu_en==_TRUE) &&
				(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_ ))
			{
				pattrib->vcs_mode = CTS_TO_SELF;
				break;
			}	
			

			//check ERP protection
			if(psta->rtsen || psta->cts2self)
			{
				if(psta->rtsen)
					pattrib->vcs_mode = RTS_CTS;
				else if(psta->cts2self)
					pattrib->vcs_mode = CTS_TO_SELF;

				break;
			}

			//check HT op mode
			if(pattrib->ht_en)
			{
				u8 HTOpMode = pmlmeinfo->HT_protection;
				if((pmlmeext->cur_bwmode && (HTOpMode == 2 || HTOpMode == 3)) ||
					(!pmlmeext->cur_bwmode && HTOpMode == 3) )
				{
					pattrib->vcs_mode = RTS_CTS;
					break;
				}
			}

			//check rts
			if(sz > padapter->registrypriv.rts_thresh)
			{
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			//to do list: check MIMO power save condition.

			//check AMPDU aggregation for TXOP
			if(pattrib->ampdu_en==_TRUE)
			{
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			pattrib->vcs_mode = NONE_VCS;
			break;
		}
	}
}

static void update_attrib_phy_info(struct pkt_attrib *pattrib, struct sta_info *psta)
{
	/*if(psta->rtsen)	
		pattrib->vcs_mode = RTS_CTS;	
	else if(psta->cts2self)	
		pattrib->vcs_mode = CTS_TO_SELF;	
	else
		pattrib->vcs_mode = NONE_VCS;*/
	
	pattrib->mdata = 0;
	pattrib->eosp = 0;
	pattrib->triggered=0;
	
	//qos_en, ht_en, init rate, ,bw, ch_offset, sgi
	pattrib->qos_en = psta->qos_option;
	pattrib->ht_en = psta->htpriv.ht_option;
	pattrib->raid = psta->raid;
	pattrib->bwmode = psta->htpriv.bwmode;
	pattrib->ch_offset = psta->htpriv.ch_offset;
	pattrib->sgi= psta->htpriv.sgi;
	pattrib->ampdu_en = _FALSE;

	//if(pattrib->ht_en && psta->htpriv.ampdu_enable)
	//{
	//	if(psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
	//		pattrib->ampdu_en = _TRUE;
	//}	


	pattrib->retry_ctrl = _FALSE;

}

u8	qos_acm(u8 acm_mask, u8 priority)
{
	u8	change_priority = priority;

	switch (priority)
	{
		case 0:
		case 3:
			if(acm_mask & BIT(1))
				change_priority = 1;
			break;
		case 1:
		case 2:
			break;
		case 4:
		case 5:
			if(acm_mask & BIT(2))
				change_priority = 0;
			break;
		case 6:
		case 7:
			if(acm_mask & BIT(3))
				change_priority = 5;
			break;
		default:
			DBG_871X("qos_acm(): invalid pattrib->priority: %d!!!\n", priority);
			break;
	}

	return change_priority;
}

static void set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	struct ethhdr etherhdr;
	struct iphdr ip_hdr;
	s32 UserPriority = 0;


	_rtw_open_pktfile(ppktfile->pkt, ppktfile);
	_rtw_pktfile_read(ppktfile, (unsigned char*)&etherhdr, ETH_HLEN);

	// get UserPriority from IP hdr
	if (pattrib->ether_type == 0x0800) {
		_rtw_pktfile_read(ppktfile, (u8*)&ip_hdr, sizeof(ip_hdr));
//		UserPriority = (ntohs(ip_hdr.tos) >> 5) & 0x3;
		UserPriority = ip_hdr.tos >> 5;
	} else if (pattrib->ether_type == 0x888e) {
		// "When priority processing of data frames is supported,
		// a STA's SME should send EAPOL-Key frames at the highest priority."
		UserPriority = 7;
	}

	pattrib->priority = UserPriority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

static s32 update_attrib(_adapter *padapter, _pkt *pkt, struct pkt_attrib *pattrib)
{
	uint i;
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	sint bmcast;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv= &pmlmepriv->qospriv;
	sint res = _SUCCESS;

 _func_enter_;

	_rtw_open_pktfile(pkt, &pktfile);
	i = _rtw_pktfile_read(&pktfile, (u8*)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);


	_rtw_memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	_rtw_memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);

	pattrib->pctrl = 0;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);
	}

	pattrib->pktlen = pktfile.pkt_len;

	if (ETH_P_IP == pattrib->ether_type)
	{
		// The following is for DHCP and ARP packet, we use cck1M to tx these packets and let LPS awake some time 
		// to prevent DHCP protocol fail
		u8 tmp[24];
		_rtw_pktfile_read(&pktfile, &tmp[0], 24);
		pattrib->dhcp_pkt = 0;
		if (pktfile.pkt_len > 282) {//MINIMUM_DHCP_PACKET_SIZE) {
			if (ETH_P_IP == pattrib->ether_type) {// IP header
				if (((tmp[21] == 68) && (tmp[23] == 67)) ||
					((tmp[21] == 67) && (tmp[23] == 68))) {
					// 68 : UDP BOOTP client
					// 67 : UDP BOOTP server
					RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("======================update_attrib: get DHCP Packet \n"));
					// Use low rate to send DHCP packet.
					//if(pMgntInfo->IOTAction & HT_IOT_ACT_WA_IOT_Broadcom) 
					//{
					//	tcb_desc->DataRate = MgntQuery_TxRateExcludeCCKRates(ieee);//0xc;//ofdm 6m
					//	tcb_desc->bTxDisableRateFallBack = false;
					//}
					//else
					//	pTcb->DataRate = Adapter->MgntInfo.LowestBasicRate; 
					//RTPRINT(FDM, WA_IOT, ("DHCP TranslateHeader(), pTcb->DataRate = 0x%x\n", pTcb->DataRate)); 
					pattrib->dhcp_pkt = 1;
				}
			}
		}
	}

	#ifdef CONFIG_SET_SCAN_DENY_TIMER
	if ( (pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1) )
	{
		rtw_set_scan_deny(pmlmepriv, 3000);
	}
	#endif

#ifdef CONFIG_LPS
	// If EAPOL , ARP , OR DHCP packet, driver must be in active mode.
	if ( (pattrib->ether_type == 0x0806) || (pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1) )
	{
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SPECIAL_PACKET, 1);
	}
#endif

	bmcast = IS_MCAST(pattrib->ra);
	
	// get sta_info
	if (bmcast) {
		psta = rtw_get_bcmc_stainfo(padapter);
	} else {
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);
		if (psta == NULL)	{ // if we cannot get psta => drrp the pkt
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("\nupdate_attrib => get sta_info fail, ra:" MAC_FMT"\n", MAC_ARG(pattrib->ra)));
			#ifdef DBG_TX_DROP_FRAME
			DBG_871X("DBG_TX_DROP_FRAME %s get sta_info fail, ra:" MAC_FMT"\n", __FUNCTION__, MAC_ARG(pattrib->ra));
			#endif
			res =_FAIL;
			goto exit;
		}
		else if((check_fwstate(pmlmepriv, WIFI_AP_STATE)==_TRUE)&&(!(psta->state & _FW_LINKED)))
		{
			res =_FAIL;
			goto exit;
		}
	}

	if (psta)
	{
		pattrib->mac_id = psta->mac_id;
		//printk("%s ==> mac_id(%d)\n",__FUNCTION__,pattrib->mac_id );
		pattrib->psta = psta;
	}
	else
	{
		// if we cannot get psta => drop the pkt
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("\nupdate_attrib => get sta_info fail, ra:" MAC_FMT "\n", MAC_ARG(pattrib->ra)));
		#ifdef DBG_TX_DROP_FRAME
		DBG_871X("DBG_TX_DROP_FRAME %s get sta_info fail, ra:" MAC_FMT"\n", __FUNCTION__, MAC_ARG(pattrib->ra));
		#endif
		res = _FAIL;
		goto exit;
	}

	pattrib->ack_policy = 0;
	// get ether_hdr_len
	pattrib->pkt_hdrlen = ETH_HLEN;//(pattrib->ether_type == 0x8100) ? (14 + 4 ): 14; //vlan tag

	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA_TYPE;	
	pattrib->priority = 0;
	
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
	{
		if(psta->qos_option)
			set_qos(&pktfile, pattrib);
	}
	else
	{
		if(pqospriv->qos_option)
		{
			set_qos(&pktfile, pattrib);

			if(pmlmepriv->acm_mask != 0)
			{
				pattrib->priority = qos_acm(pmlmepriv->acm_mask, pattrib->priority);
			}
		}
	}

	//pattrib->priority = 5; //force to used VI queue, for testing

	if (psta->ieee8021x_blocked == _TRUE)
	{
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\n psta->ieee8021x_blocked == _TRUE \n"));

		pattrib->encrypt = 0;

		if((pattrib->ether_type != 0x888e) && (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE))
		{
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\npsta->ieee8021x_blocked == _TRUE,  pattrib->ether_type(%.4x) != 0x888e\n",pattrib->ether_type));
			#ifdef DBG_TX_DROP_FRAME
			DBG_871X("DBG_TX_DROP_FRAME %s psta->ieee8021x_blocked == _TRUE,  pattrib->ether_type(%04x) != 0x888e\n", __FUNCTION__,pattrib->ether_type);
			#endif
			res = _FAIL;
			goto exit;
		}
	}
	else
	{
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);

		switch(psecuritypriv->dot11AuthAlgrthm)
		{
			case dot11AuthAlgrthm_Open:
			case dot11AuthAlgrthm_Shared:
			case dot11AuthAlgrthm_Auto:				
				pattrib->key_idx = (u8)psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case dot11AuthAlgrthm_8021X:
				if(bmcast)					
					pattrib->key_idx = (u8)psecuritypriv->dot118021XGrpKeyid;	
				else
					pattrib->key_idx = 0;
				break;
			default:
				pattrib->key_idx = 0;
				break;
		}
		
			
	}

	switch (pattrib->encrypt)
	{
		case _WEP40_:
		case _WEP104_:
			pattrib->iv_len = 4;
			pattrib->icv_len = 4;
			break;

		case _TKIP_:
			pattrib->iv_len = 8;
			pattrib->icv_len = 4;
			
			if(padapter->securitypriv.busetkipkey==_FAIL)
			{
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\npadapter->securitypriv.busetkipkey(%d)==_FAIL drop packet\n", padapter->securitypriv.busetkipkey));
				#ifdef DBG_TX_DROP_FRAME
				DBG_871X("DBG_TX_DROP_FRAME %s padapter->securitypriv.busetkipkey(%d)==_FAIL drop packet\n", __FUNCTION__, padapter->securitypriv.busetkipkey);
				#endif
				res =_FAIL;
				goto exit;
			}
					
			break;			
		case _AES_:
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\n pattrib->encrypt=%d  (_AES_)\n",pattrib->encrypt));
			pattrib->iv_len = 8;
			pattrib->icv_len = 8;
			break;
			
		default:
			pattrib->iv_len = 0;
			pattrib->icv_len = 0;
			break;
	}

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("update_attrib: encrypt=%d  securitypriv.sw_encrypt=%d\n",
		  pattrib->encrypt, padapter->securitypriv.sw_encrypt));

	if (pattrib->encrypt &&
	    ((padapter->securitypriv.sw_encrypt == _TRUE) || (psecuritypriv->hw_decrypted == _FALSE)))
	{
		pattrib->bswenc = _TRUE;
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,
			 ("update_attrib: encrypt=%d securitypriv.hw_decrypted=%d bswenc=_TRUE\n",
			  pattrib->encrypt, padapter->securitypriv.sw_encrypt));
	} else {
		pattrib->bswenc = _FALSE;
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("update_attrib: bswenc=_FALSE\n"));
	}

#ifdef CONFIG_CONCURRENT_MODE
	if((pattrib->encrypt && bmcast) || (pattrib->encrypt ==_WEP40_) || (pattrib->encrypt ==_WEP104_))
	{
		pattrib->bswenc = _TRUE;//force using sw enc.
	}
#endif

	rtw_set_tx_chksum_offload(pkt, pattrib);

	update_attrib_phy_info(pattrib, psta);

exit:

_func_exit_;

	return res;
}

static s32 xmitframe_addmic(_adapter *padapter, struct xmit_frame *pxmitframe){
	sint 			curfragnum,length;
	u8	*pframe, *payload,mic[8];
	struct	mic_data		micdata;
	struct	sta_info		*stainfo;
	struct	qos_priv   *pqospriv= &(padapter->mlmepriv.qospriv);	
	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;
	struct 	security_priv	*psecuritypriv=&padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv=&padapter->xmitpriv;
	u8 priority[4]={0x0,0x0,0x0,0x0};
	u8 hw_hdr_offset = 0;
	sint bmcst = IS_MCAST(pattrib->ra);

	if(pattrib->psta)
	{
		stainfo = pattrib->psta;
	}
	else
	{
		stainfo=rtw_get_stainfo(&padapter->stapriv ,&pattrib->ra[0]);
	}	
	

_func_enter_;

#ifdef CONFIG_USB_TX_AGGREGATION
	hw_hdr_offset = TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);;	
#else
	#ifdef CONFIG_TX_EARLY_MODE
	hw_hdr_offset = TXDESC_OFFSET+ EARLY_MODE_INFO_SIZE;
	#else
	hw_hdr_offset = TXDESC_OFFSET;
	#endif
#endif	
	
	if(pattrib->encrypt ==_TKIP_)//if(psecuritypriv->dot11PrivacyAlgrthm==_TKIP_PRIVACY_) 
	{
		//encode mic code
		if(stainfo!= NULL){
			u8 null_key[16]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};

			pframe = pxmitframe->buf_addr + hw_hdr_offset;
			
			if(bmcst)
			{
				if(_rtw_memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16)==_TRUE){
					//DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n");
					//rtw_msleep_os(10);
					return _FAIL;
				}				
				//start to calculate the mic code
				rtw_secmicsetkey(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			}
			else
			{
				if(_rtw_memcmp(&stainfo->dot11tkiptxmickey.skey[0],null_key, 16)==_TRUE){
					//DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n");
					//rtw_msleep_os(10);
					return _FAIL;
				}
				//start to calculate the mic code
				rtw_secmicsetkey(&micdata, &stainfo->dot11tkiptxmickey.skey[0]);
			}
			
			if(pframe[1]&1){   //ToDS==1
				rtw_secmicappend(&micdata, &pframe[16], 6);  //DA
				if(pframe[1]&2)  //From Ds==1
					rtw_secmicappend(&micdata, &pframe[24], 6);
				else
				rtw_secmicappend(&micdata, &pframe[10], 6);		
			}	
			else{	//ToDS==0
				rtw_secmicappend(&micdata, &pframe[4], 6);   //DA
				if(pframe[1]&2)  //From Ds==1
					rtw_secmicappend(&micdata, &pframe[16], 6);
				else
					rtw_secmicappend(&micdata, &pframe[10], 6);

			}

                    //if(pqospriv->qos_option==1)
                    if(pattrib->qos_en)
				priority[0]=(u8)pxmitframe->attrib.priority;

			
			rtw_secmicappend(&micdata, &priority[0], 4);
	
			payload=pframe;

			for(curfragnum=0;curfragnum<pattrib->nr_frags;curfragnum++){
				payload=(u8 *)RND4((SIZE_PTR)(payload));
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("===curfragnum=%d, pframe= 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x,!!!\n",
					curfragnum,*payload, *(payload+1),*(payload+2),*(payload+3),*(payload+4),*(payload+5),*(payload+6),*(payload+7)));

				payload=payload+pattrib->hdrlen+pattrib->iv_len;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("curfragnum=%d pattrib->hdrlen=%d pattrib->iv_len=%d",curfragnum,pattrib->hdrlen,pattrib->iv_len));
				if((curfragnum+1)==pattrib->nr_frags){
					length=pattrib->last_txcmdsz-pattrib->hdrlen-pattrib->iv_len-( (pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload,length);
					payload=payload+length;
				}
				else{
					length=pxmitpriv->frag_len-pattrib->hdrlen-pattrib->iv_len-( (pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload=payload+length+pattrib->icv_len;
					RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("curfragnum=%d length=%d pattrib->icv_len=%d",curfragnum,length,pattrib->icv_len));
				}
			}
			rtw_secgetmic(&micdata,&(mic[0]));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: before add mic code!!!\n"));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: pattrib->last_txcmdsz=%d!!!\n",pattrib->last_txcmdsz));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: mic[0]=0x%.2x ,mic[1]=0x%.2x ,mic[2]=0x%.2x ,mic[3]=0x%.2x \n\
  mic[4]=0x%.2x ,mic[5]=0x%.2x ,mic[6]=0x%.2x ,mic[7]=0x%.2x !!!!\n",
				mic[0],mic[1],mic[2],mic[3],mic[4],mic[5],mic[6],mic[7]));
			//add mic code  and add the mic code length in last_txcmdsz

			_rtw_memcpy(payload, &(mic[0]),8);
			pattrib->last_txcmdsz+=8;
			
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("\n ========last pkt========\n"));
			payload=payload-pattrib->last_txcmdsz+8;
			for(curfragnum=0;curfragnum<pattrib->last_txcmdsz;curfragnum=curfragnum+8)
					RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,(" %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x ",
					*(payload+curfragnum), *(payload+curfragnum+1), *(payload+curfragnum+2),*(payload+curfragnum+3),
					*(payload+curfragnum+4),*(payload+curfragnum+5),*(payload+curfragnum+6),*(payload+curfragnum+7)));
			}
			else{
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: rtw_get_stainfo==NULL!!!\n"));
			}
	}
	
_func_exit_;

	return _SUCCESS;
}

static s32 xmitframe_swencrypt(_adapter *padapter, struct xmit_frame *pxmitframe){

	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;
	//struct 	security_priv	*psecuritypriv=&padapter->securitypriv;
	
_func_enter_;

	//if((psecuritypriv->sw_encrypt)||(pattrib->bswenc))	
	if(pattrib->bswenc)
	{
		//DBG_871X("start xmitframe_swencrypt\n");
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_alert_,("### xmitframe_swencrypt\n"));
		switch(pattrib->encrypt){
		case _WEP40_:
		case _WEP104_:
			rtw_wep_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _TKIP_:
			rtw_tkip_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _AES_:
			rtw_aes_encrypt(padapter, (u8 * )pxmitframe);
			break;
		default:
				break;
		}

	} else {
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_notice_,("### xmitframe_hwencrypt\n"));
	}

_func_exit_;

	return _SUCCESS;
}

s32 rtw_make_wlanhdr (_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	u8 qos_option = _FALSE;
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	struct sta_info *ptdls_sta=NULL;
	u8 tdls_seq=0;
#endif //CONFIG_TDLS

	sint res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_ctl;

	struct sta_info *psta;

	sint bmcst = IS_MCAST(pattrib->ra);

_func_enter_;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		if(bmcst) {
			psta = rtw_get_bcmc_stainfo(padapter);
		} else {
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
		}
	}

	_rtw_memset(hdr, 0, WLANHDR_OFFSET);

	SetFrameSubType(fctrl, pattrib->subtype);

	if (pattrib->subtype & WIFI_DATA_TYPE)
	{
		if ((check_fwstate(pmlmepriv,  WIFI_STATION_STATE) == _TRUE)) {
			//to_ds = 1, fr_ds = 0;
#ifdef CONFIG_TDLS
			if((ptdlsinfo->setup_state==TDLS_LINKED_STATE)){
				ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
				if((ptdls_sta!=NULL)&&((ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE)==TDLS_LINKED_STATE)&&(pattrib->ether_type!=0x0806)){
					//TDLS data transfer, ToDS=0, FrDs=0
					_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
					_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
					_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
					tdls_seq=1;
				}else{
					// 1.usual data transfer
					// 2.Arp pkt will relayed by AP
					SetToDs(fctrl);							
					_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
					_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
					_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
				} 
			}else
#endif //CONFIG_TDLS
			{
				//usual data transfer
				SetToDs(fctrl);							
				_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
			} 

			if (pqospriv->qos_option)
				qos_option = _TRUE;

		}
		else if ((check_fwstate(pmlmepriv,  WIFI_AP_STATE) == _TRUE) ) {
			//to_ds = 0, fr_ds = 1;
			SetFrDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);

			if(psta->qos_option)			
				qos_option = _TRUE;
		}
		else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

			if(psta->qos_option)			
				qos_option = _TRUE;
		}
		else {
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("fw_state:%x is not allowed to xmit frame\n", get_fwstate(pmlmepriv)));
			res = _FAIL;
			goto exit;
		}

		if(pattrib->mdata)
			SetMData(fctrl);

		if (pattrib->encrypt)
			SetPrivacy(fctrl);

		if (qos_option)
		{
			qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);

			if (pattrib->priority)
				SetPriority(qc, pattrib->priority);

			SetEOSP(qc, pattrib->eosp);

			SetAckpolicy(qc, pattrib->ack_policy);
		}

		//TODO: fill HT Control Field

		//Update Seq Num will be handled by f/w
		{
#ifdef CONFIG_TDLS
			//  1. update seq_num per link by sta_info
			//  2. rewrite encrypt to _AES_, also rewrite iv_len, icv_len
			if(tdls_seq==1){
				ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
				ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;

				pattrib->seqnum = ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority];

				SetSeqNum(hdr, pattrib->seqnum);

				if (pattrib->encrypt){
					pattrib->encrypt= _AES_;
					pattrib->iv_len=8;
					pattrib->icv_len=8;
				}
			}else 
#endif //CONFIG_TDLS
			if(psta){
				psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
				psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;

				pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];

				SetSeqNum(hdr, pattrib->seqnum);


				//check if enable ampdu
				if(pattrib->ht_en && psta->htpriv.ampdu_enable)
				{
					if(psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
					pattrib->ampdu_en = _TRUE;
				}

				//re-check if enable ampdu by BA_starting_seqctrl
				if(pattrib->ampdu_en == _TRUE)
				{					
					u16 tx_seq;

					tx_seq = psta->BA_starting_seqctrl[pattrib->priority & 0x0f];
		
					//check BA_starting_seqctrl
					if(SN_LESS(pattrib->seqnum, tx_seq))
					{
						//DBG_871X("tx ampdu seqnum(%d) < tx_seq(%d)\n", pattrib->seqnum, tx_seq);
						pattrib->ampdu_en = _FALSE;//AGG BK
					}
					else if(SN_EQUAL(pattrib->seqnum, tx_seq))
					{					
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (tx_seq+1)&0xfff;
					
						pattrib->ampdu_en = _TRUE;//AGG EN
					}
					else
					{
						//DBG_871X("tx ampdu over run\n");
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (pattrib->seqnum+1)&0xfff;
						pattrib->ampdu_en = _TRUE;//AGG EN
					}

				}

			}
		}
		
	}
	else
	{

	}

exit:

_func_exit_;

	return res;
}

s32 rtw_txframes_pending(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return ((_rtw_queue_empty(&pxmitpriv->be_pending) == _FALSE) || 
			 (_rtw_queue_empty(&pxmitpriv->bk_pending) == _FALSE) || 
			 (_rtw_queue_empty(&pxmitpriv->vi_pending) == _FALSE) ||
			 (_rtw_queue_empty(&pxmitpriv->vo_pending) == _FALSE));
}

s32 rtw_txframes_sta_ac_pending(_adapter *padapter, struct pkt_attrib *pattrib)
{	
	struct sta_info *psta;
	struct tx_servq *ptxservq;
	int priority = pattrib->priority;

	psta = pattrib->psta;
	
	switch(priority) 
	{
			case 1:
			case 2:
				ptxservq = &(psta->sta_xmitpriv.bk_q);				
				break;
			case 4:
			case 5:
				ptxservq = &(psta->sta_xmitpriv.vi_q);				
				break;
			case 6:
			case 7:
				ptxservq = &(psta->sta_xmitpriv.vo_q);							
				break;
			case 0:
			case 3:
			default:
				ptxservq = &(psta->sta_xmitpriv.be_q);							
			break;
	
	}	

	return ptxservq->qcnt;	
}

#ifdef CONFIG_TDLS
void rtw_tdls_setup_req_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct rtw_ieee80211_ht_cap ht_capie;	
	struct sta_info *ptdls_sta=rtw_get_stainfo( (&padapter->stapriv) , pattrib->dst);

	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = TDLS_SETUP_REQUEST;
	u8 bssrate[NDIS_802_11_LENGTH_RATES_EX]; //Use NDIS_802_11_LENGTH_RATES_EX in order to call func.rtw_set_supported_rate
 	int	bssrate_len = 0, i = 0 ;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	u8 *p;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 iedata=0;
	u8 sup_ch[ 30 * 2 ] = {0x00 }, sup_ch_idx = 0, idx_5g = 2;	//For supported channel
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	u32 time;

	//SNonce	
	if(pattrib->encrypt){
		for(i=0;i<8;i++){
			time=rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->SNonce[4*i], (u8 *)&time, 4);
		}
	}

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));
	//category, action, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(pattrib->encrypt)
		*pframe =*pframe | BIT(4);
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
	bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;
	//for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
	//	if (pregistrypriv->dev_network.SupportedRates[bssrate_len]== 0) break;
	//	bssrate[bssrate_len] = pregistrypriv->dev_network.SupportedRates[bssrate_len];
	//}

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//country(optional)
	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	do{
		if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 11 )
		{
			sup_ch[0] = 1;	//First channel number
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;	//Number of channel
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 13 )
		{
			sup_ch[0] = 1;
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;;
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum > 13 )
		{
			sup_ch[idx_5g++] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;
			sup_ch[idx_5g++] = 1;
		}
		
		sup_ch_idx++;
	}
	while( pmlmeext->channel_set[sup_ch_idx].ChannelNum != 0 );
	pframe = rtw_set_ie(pframe, _SUPPORTED_CH_IE_, idx_5g + 2, sup_ch, &(pattrib->pktlen));
	
	//RSNIE
	if(pattrib->encrypt)
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, 20, TDLS_RSNIE, &(pattrib->pktlen));
	
	//extended capabilities
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , 5, TDLS_EXT_CAPIE, &(pattrib->pktlen));

	//QoS capability(WMM_IE)
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 7, TDLS_WMMIE,  &(pattrib->pktlen));

	//FTIE(optional)
	if(pattrib->encrypt){
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

	//Timeout interval
	timeout_itvl[0]=0x02;
	_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
	pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));

	}

	//Sup_reg_classes(optional)
	//HT capabilities
	_rtw_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

	ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |IEEE80211_HT_CAP_SGI_20 |IEEE80211_HT_CAP_SM_PS |
						IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_TX_STBC |IEEE80211_HT_CAP_DSSSCCK40;

	{
		u32 rx_packet_offset, max_recvbuf_sz;
		rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
		if(max_recvbuf_sz-rx_packet_offset>(8191-256))
			ht_capie.cap_info = ht_capie.cap_info |IEEE80211_HT_CAP_MAX_AMSDU;
	}
	
	ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);

	switch(pHalData->rf_type)
	{
		case RF_1T1R:
			ht_capie.cap_info |= 0x0100;//RX STBC One spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_1R, 16);
			break;

		case RF_2T2R:
		case RF_1T2R:
		default:
			ht_capie.cap_info|= 0x0200;//RX STBC two spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_2R, 16);
			break;
	}
			
	pframe = rtw_set_ie(pframe, _HT_CAPABILITY_IE_, 
						sizeof(struct rtw_ieee80211_ht_cap), (unsigned char*)&ht_capie, &(pattrib->pktlen));

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
	
	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_tdls_setup_rsp_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct rtw_ieee80211_ht_cap ht_capie;	

	u8 payload_type = 0x02;	
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_SETUP_RESPONSE;
	unsigned char	bssrate[NDIS_802_11_LENGTH_RATES_EX];	
	int	bssrate_len = 0;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	unsigned char *p;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 sup_ch[ 30 * 2 ] = { 0x00 }, sup_ch_idx = 0, idx_5g = 2;	//For supported channel
	u8 iedata=0;
	u8 timeout_itvl[5];	//setup response timeout interval will copy from request
	u8 ANonce[32];	//maybe it can put in ontdls_req
	u8 k;		//for random ANonce
	u8  *pftie, *ptimeout_ie, *plinkid_ie, *prsnie, *pftie_mic;
	u32 time;

	ptdls_sta = rtw_get_stainfo( &(padapter->stapriv) , pattrib->dst);

	if(ptdls_sta == NULL )
	{
		DBG_8192C("[%s] %d\n", __FUNCTION__, __LINE__);
	}
	else
		DBG_8192C("stat_code:%04x \n", ptdls_sta->stat_code);		
	
	if(pattrib->encrypt){
		for(k=0;k<8;k++){
			time=rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->ANonce[4*k], (u8*)&time, 4);
		}
	}

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, status code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));

	if(ptdls_sta->stat_code!=0)	//invalid setup request
		return;
	
	//dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(pattrib->encrypt )
		*pframe =*pframe | BIT(4);
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
	bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;
	//for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
	//	if (pregistrypriv->dev_network.SupportedRates[bssrate_len]== 0) break;
	//	bssrate[bssrate_len] = pregistrypriv->dev_network.SupportedRates[bssrate_len];
	//}

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//country(optional)
	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	do{
		if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 11 )
		{
			sup_ch[0] = 1;	//First channel number
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;;	//Number of channel
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 13 )
		{
			sup_ch[0] = 1;
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;;
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum > 13 )
		{
			sup_ch[idx_5g++] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;
			sup_ch[idx_5g++] = 1;
		}
		
		sup_ch_idx++;
	}
	while( pmlmeext->channel_set[sup_ch_idx].ChannelNum != 0 );
	pframe = rtw_set_ie(pframe, _SUPPORTED_CH_IE_, idx_5g + 2, sup_ch, &(pattrib->pktlen));
	
	//RSNIE
	if(pattrib->encrypt){
		prsnie = pframe;
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, 20, TDLS_RSNIE, &(pattrib->pktlen));
	}

	//extended capabilities
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , 5, TDLS_EXT_CAPIE, &(pattrib->pktlen));

	//QoS capability(WMM_IE)
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 7, TDLS_WMMIE,  &(pattrib->pktlen));

	//FTIE(optional)
	if(pattrib->encrypt){
		wpa_tdls_generate_tpk(padapter, ptdls_sta);

		pftie = pframe;
		pftie_mic = pframe+4;
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+20), ptdls_sta->ANonce, 32);
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

	//Timeout interval
		ptimeout_ie = pframe;
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//Sup_reg_classes(optional)
	//HT capabilities
	_rtw_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

	ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |IEEE80211_HT_CAP_SGI_20 |IEEE80211_HT_CAP_SM_PS |
						IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_TX_STBC |IEEE80211_HT_CAP_DSSSCCK40;

	{
		u32 rx_packet_offset, max_recvbuf_sz;
		rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
		if(max_recvbuf_sz-rx_packet_offset>(8191-256))
			ht_capie.cap_info = ht_capie.cap_info |IEEE80211_HT_CAP_MAX_AMSDU;
	}
	
	ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);

	switch(pHalData->rf_type)
	{
		case RF_1T1R:
			ht_capie.cap_info |= 0x0100;//RX STBC One spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_1R, 16);
			break;

		case RF_2T2R:
		case RF_1T2R:
		default:
			ht_capie.cap_info|= 0x0200;//RX STBC two spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_2R, 16);
			break;
	}
			
	pframe = rtw_set_ie(pframe, _HT_CAPABILITY_IE_, 
						sizeof(struct rtw_ieee80211_ht_cap), (unsigned char*)&ht_capie, &(pattrib->pktlen));

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));

	//Link identifier
	plinkid_ie = pframe;
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//fill ftie mic
	if(pattrib->encrypt)
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);
	
}

void rtw_tdls_setup_cfm_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta=rtw_get_stainfo( (&padapter->stapriv) , pattrib->dst);

	u8 payload_type = 0x02;	
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_SETUP_CONFIRM;
	u8 more_supportedrates = 0;
	unsigned int ie_len;
	unsigned char *p;
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8	link_id_addr[18] = {0};
	u8  *pftie, *ptimeout_ie, *plinkid_ie, *prsnie, *pftie_mic;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));
	//category, action, status code, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	if(ptdls_sta->stat_code!=0)	//invalid setup request
		return;
	
	//RSNIE
	if(pattrib->encrypt){
		prsnie = pframe;
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, 20, TDLS_RSNIE, &(pattrib->pktlen));
	}
	
	//EDCA param set; WMM param ele.
	if(pattrib->encrypt){
		//FTIE
		pftie = pframe;
		pftie_mic = pframe+4;
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		_rtw_memcpy((pframe+20), ptdls_sta->ANonce, 32);
		_rtw_memcpy((pframe+52), ptdls_sta->SNonce, 32);
		pframe += 84;
		pattrib->pktlen += 84;

		//Timeout interval
		ptimeout_ie = pframe;
		timeout_itvl[0]=0x02;
		_rtw_memcpy(timeout_itvl+1, (u8 *)(&ptdls_sta->TDLS_PeerKey_Lifetime), 4);
		ptdls_sta->TPK_count=0;
		_set_timer(&ptdls_sta->TPK_timer, ptdls_sta->TDLS_PeerKey_Lifetime/TPK_RESEND_COUNT);
		pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));
	}

	//HT operation; todo
	//Link identifier
	plinkid_ie = pframe;
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//fill ftie mic
	if(pattrib->encrypt)
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);

}

void rtw_tdls_teardown_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_TEARDOWN;
	u8	link_id_addr[18] = {0};
	
	struct sta_info *ptdls_sta = rtw_get_stainfo( &(padapter->stapriv) , pattrib->dst);
	struct sta_priv 	*pstapriv = &padapter->stapriv;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));

	//Link identifier
	if((ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)==TDLS_INITIATOR_STATE){	
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	}else  if((ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)==TDLS_RESPONDER_STATE){
		_rtw_memcpy(link_id_addr, pattrib->ra, 6);
		_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	}
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_tdls_dis_req_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	u8 category = RTW_WLAN_CATEGORY_TDLS;
	u8 action = TDLS_DISCOVERY_REQUEST;
	u8	link_id_addr[18] = {0};
	static u8 dialogtoken=0;

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));		
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogtoken), &(pattrib->pktlen));
	dialogtoken = (dialogtoken+1)%256;

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_tdls_dis_rsp_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, u8 dialog)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct rtw_ieee80211_ht_cap ht_capie;

	u8 category = RTW_WLAN_CATEGORY_PUBLIC;
	u8 action = TDLS_DISCOVERY_RESPONSE;
	u8 bssrate[NDIS_802_11_LENGTH_RATES_EX];
 	int bssrate_len = 0;
	u8 more_supportedrates = 0;
	u8 *p;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	u8 link_id_addr[18] = {0};
	u8 sup_ch[ 30 * 2 ] = {0x00 }, sup_ch_idx = 0, idx_5g = 2;	//For supported channel
	u8 iedata=0;
	u8 timeout_itvl[5];	//set timeout interval to maximum value
	u32 timeout_interval= TPK_RESEND_COUNT * 1000;
	
	//category, action, dialog token
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialog), &(pattrib->pktlen));

	//capability
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);

	if(pattrib->encrypt)
		*pframe =*pframe | BIT(4);
	pframe += 2;
	pattrib->pktlen += 2;

	//supported rates
	rtw_set_supported_rate(bssrate, WIRELESS_11BG_24N);
	bssrate_len = IEEE80211_CCK_RATE_LEN + IEEE80211_NUM_OFDM_RATESLEN;
 	//for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
	//	if (pregistrypriv->dev_network.SupportedRates[bssrate_len] == 0) break;
	//	bssrate[bssrate_len] = pregistrypriv->dev_network.SupportedRates[bssrate_len];
	//}

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//extended supported rates
	if(more_supportedrates==1){
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}

	//supported channels
	do{
		if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 11 )
		{
			sup_ch[0] = 1;	//First channel number
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;;	//Number of channel
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum == 13 )
		{
			sup_ch[0] = 1;
			sup_ch[1] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;;
		}
		else if( pmlmeext->channel_set[sup_ch_idx].ChannelNum > 13 )
		{
			sup_ch[idx_5g++] = pmlmeext->channel_set[sup_ch_idx].ChannelNum;
			sup_ch[idx_5g++] = 1;
		}
		
		sup_ch_idx++;
	}
	while( pmlmeext->channel_set[sup_ch_idx].ChannelNum != 0 );
	pframe = rtw_set_ie(pframe, _SUPPORTED_CH_IE_, idx_5g + 2, sup_ch, &(pattrib->pktlen));
	
	//RSNIE
	if(pattrib->encrypt)
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, 20, TDLS_RSNIE, &(pattrib->pktlen));
	
	//extended capability
	pframe = rtw_set_ie(pframe, _EXT_CAP_IE_ , 5, TDLS_EXT_CAPIE, &(pattrib->pktlen));

	//FTIE
	if(pattrib->encrypt){
		_rtw_memset(pframe, 0, 84);	//All fields except SNonce shall be set to 0
		_rtw_memset(pframe, _FTIE_, 1);	//version
		_rtw_memset((pframe+1), 82, 1);	//length
		pframe += 84;
		pattrib->pktlen += 84;
	//Timeout interval
	timeout_itvl[0]=0x02;
	_rtw_memcpy(timeout_itvl+1, &timeout_interval, 4);
	pframe = rtw_set_ie(pframe, _TIMEOUT_ITVL_IE_, 5, timeout_itvl,  &(pattrib->pktlen));

	}

	//Sup_reg_classes(optional)
	//HT capabilities
	_rtw_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

	ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |IEEE80211_HT_CAP_SGI_20 |IEEE80211_HT_CAP_SM_PS |
						IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_TX_STBC |IEEE80211_HT_CAP_DSSSCCK40;

	{
		u32 rx_packet_offset, max_recvbuf_sz;
		rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
		if(max_recvbuf_sz-rx_packet_offset>(8191-256))
			ht_capie.cap_info = ht_capie.cap_info |IEEE80211_HT_CAP_MAX_AMSDU;
	}
	
	ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);

	switch(pHalData->rf_type)
	{
		case RF_1T1R:
			ht_capie.cap_info |= 0x0100;//RX STBC One spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_1R, 16);
			break;

		case RF_2T2R:
		case RF_1T2R:
		default:
			ht_capie.cap_info|= 0x0200;//RX STBC two spatial stream
			_rtw_memcpy(ht_capie.supp_mcs_set, MCS_rate_2R, 16);
			break;
	}
			
	pframe = rtw_set_ie(pframe, _HT_CAPABILITY_IE_, 
						sizeof(struct rtw_ieee80211_ht_cap), (unsigned char*)&ht_capie, &(pattrib->pktlen));

	//20/40 BSS coexistence
	if(pmlmepriv->num_FortyMHzIntolerant>0)
		iedata |= BIT(2);//20 MHz BSS Width Request
	pframe = rtw_set_ie(pframe, EID_BSSCoexistence, 1, &iedata, &(pattrib->pktlen));

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->dst, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->src, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_, 18, link_id_addr, &(pattrib->pktlen));
	
}

void rtw_tdls_peer_traffic_indication_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_PEER_TRAFFIC_INDICATION;

	u8	link_id_addr[18] = {0};
	u8 AC_queue=0;
	struct sta_priv 	*pstapriv = &padapter->stapriv;	
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, reason code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdls_sta->dialog), &(pattrib->pktlen));

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//PTI control
	//PU buffer status
	if(ptdls_sta->uapsd_bk&BIT(1))
		AC_queue=BIT(0);
	if(ptdls_sta->uapsd_be&BIT(1))
		AC_queue=BIT(1);
	if(ptdls_sta->uapsd_vi&BIT(1))
		AC_queue=BIT(2);
	if(ptdls_sta->uapsd_vo&BIT(1))
		AC_queue=BIT(3);
	pframe = rtw_set_ie(pframe, _PTI_BUFFER_STATUS_, 1, &AC_queue, &(pattrib->pktlen));
	
}

void rtw_tdls_ch_switch_req_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_CHANNEL_SWITCH_REQUEST;
	u8	link_id_addr[18] = {0};
	struct sta_priv 	*pstapriv = &padapter->stapriv;	
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	u8 ch_switch_timing[4] = {0};
	u16 switch_time= CH_SWITCH_TIME, switch_timeout=CH_SWITCH_TIMEOUT;	

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, target_ch
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(ptdlsinfo->candidate_ch), &(pattrib->pktlen));

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//ch switch timing
	_rtw_memcpy(ch_switch_timing, &switch_time, 2);
	_rtw_memcpy(ch_switch_timing+2, &switch_timeout, 2);
	pframe = rtw_set_ie(pframe, _CH_SWITCH_TIMING_,  4, ch_switch_timing, &(pattrib->pktlen));

	//update ch switch attrib to sta_info
	ptdls_sta->off_ch=ptdlsinfo->candidate_ch;
	ptdls_sta->ch_switch_time=switch_time;
	ptdls_sta->ch_switch_timeout=switch_timeout;

}

void rtw_tdls_ch_switch_rsp_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 payload_type = 0x02;
	unsigned char category = RTW_WLAN_CATEGORY_TDLS;
	unsigned char action = TDLS_CHANNEL_SWITCH_RESPONSE;
	u8	link_id_addr[18] = {0};
	struct sta_priv 	*pstapriv = &padapter->stapriv;	
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 ch_switch_timing[4] = {0};

	//payload type
	pframe = rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));	
	//category, action, status_code
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 2, (u8 *)&ptdls_sta->stat_code, &(pattrib->pktlen));

	//Link identifier
	_rtw_memcpy(link_id_addr, pattrib->ra, 6);
	_rtw_memcpy((link_id_addr+6), pattrib->src, 6);
	_rtw_memcpy((link_id_addr+12), pattrib->dst, 6);
	pframe = rtw_set_ie(pframe, _LINK_ID_IE_,  18, link_id_addr, &(pattrib->pktlen));

	//ch switch timing
	_rtw_memcpy(ch_switch_timing, &ptdls_sta->ch_switch_time, 2);
	_rtw_memcpy(ch_switch_timing+2, &ptdls_sta->ch_switch_timeout, 2);
	pframe = rtw_set_ie(pframe, _CH_SWITCH_TIMING_,  4, ch_switch_timing, &(pattrib->pktlen));

}

int rtw_tdls_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, u8 action)
{
	int res=_SUCCESS;

	switch(action){
		case TDLS_SETUP_REQUEST:
			rtw_tdls_setup_req_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_SETUP_RESPONSE:
			rtw_tdls_setup_rsp_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_SETUP_CONFIRM:
			rtw_tdls_setup_cfm_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_TEARDOWN:
			rtw_tdls_teardown_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_DISCOVERY_REQUEST:
			rtw_tdls_dis_req_fr(padapter, pxmitframe, pframe);
			break;			
		case TDLS_PEER_TRAFFIC_INDICATION:
			rtw_tdls_peer_traffic_indication_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_CHANNEL_SWITCH_REQUEST:
			rtw_tdls_ch_switch_req_fr(padapter, pxmitframe, pframe);
			break;
		case TDLS_CHANNEL_SWITCH_RESPONSE:
			rtw_tdls_ch_switch_rsp_fr(padapter, pxmitframe, pframe);
			break;
		default:
			res=_FAIL;
			break;
	}

	return res;
}

s32 rtw_make_tdls_wlanhdr (_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib, u8 action)
{
	u16 *qc;
	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL, *ptdls_sta=NULL;
	u8 tdls_seq=0, baddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	sint res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_ctl;

_func_enter_;

	_rtw_memset(hdr, 0, WLANHDR_OFFSET);

	SetFrameSubType(fctrl, pattrib->subtype);

	switch(action){
		case TDLS_SETUP_REQUEST:
		case TDLS_SETUP_RESPONSE:
		case TDLS_SETUP_CONFIRM:
		case TDLS_TEARDOWN:	//directly to peer STA or via AP
		case TDLS_PEER_TRAFFIC_INDICATION:
		case TDLS_PEER_PSM_REQUEST:	//directly to peer STA or via AP
			SetToDs(fctrl);							
			_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
			break;
		case TDLS_CHANNEL_SWITCH_REQUEST:
		case TDLS_CHANNEL_SWITCH_RESPONSE:
		case TDLS_PEER_PSM_RESPONSE:
		case TDLS_PEER_TRAFFIC_RESPONSE:
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
			tdls_seq=1;
			break;
		case TDLS_DISCOVERY_REQUEST:	//unicast: directly to peer sta, Bcast: via AP
			if(_rtw_memcmp(pattrib->dst, baddr, 6) )
			{
				SetToDs(fctrl);							
				_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
			}
			else
			{
				_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
				tdls_seq=1;
			}
			break;
	}

	if (pattrib->encrypt)
		SetPrivacy(fctrl);

	if (pqospriv->qos_option)
	{
		qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);
		if (pattrib->priority)
			SetPriority(qc, pattrib->priority);
		SetAckpolicy(qc, pattrib->ack_policy);
	}

	psta = pattrib->psta;
 
	//  1. update seq_num per link by sta_info
	//  2. rewrite encrypt to _AES_, also rewrite iv_len, icv_len
	if(tdls_seq==1){
		ptdls_sta=rtw_get_stainfo(pstapriv, pattrib->dst);
		if(ptdls_sta){
			ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
			ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
			pattrib->seqnum = ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority];
			SetSeqNum(hdr, pattrib->seqnum);

			if (pattrib->encrypt){
				pattrib->encrypt= _AES_;
				pattrib->iv_len=8;
				pattrib->icv_len=8;
			}
		}else{
			res=_FAIL;
			goto exit;
		}
	}else if(psta){
		psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
		psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
		pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];
		SetSeqNum(hdr, pattrib->seqnum);
	}


exit:

_func_exit_;

	return res;
}

s32 rtw_xmit_tdls_coalesce(_adapter * padapter, struct xmit_frame * pxmitframe, u8 action)
{
	s32 llc_sz;

	u8 *pframe, *mem_start;

	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 *pbuf_start;
	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _SUCCESS;
	
_func_enter_;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {	
		if(bmcst) {
			psta = rtw_get_bcmc_stainfo(padapter);
		} else {
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	        }
	}

	if(psta==NULL)
		return _FAIL;

	if (pxmitframe->buf_addr == NULL)
		return _FAIL;

	pbuf_start = pxmitframe->buf_addr;
	mem_start = pbuf_start + TXDESC_OFFSET;

	if (rtw_make_tdls_wlanhdr(padapter, mem_start, pattrib, action) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	pframe = mem_start;
	pframe += pattrib->hdrlen;

	//adding icv, if necessary...
	if (pattrib->iv_len)
	{
		if (psta != NULL)
		{
			switch(pattrib->encrypt)
			{
				case _WEP40_:
				case _WEP104_:
						WEP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);	
					break;
				case _TKIP_:			
					if(bmcst)
						TKIP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
					else
						TKIP_IV(pattrib->iv, psta->dot11txpn, 0);
					break;			
				case _AES_:
					if(bmcst)
						AES_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
					else
						AES_IV(pattrib->iv, psta->dot11txpn, 0);
					break;
			}
		}

		_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len);
		pframe += pattrib->iv_len;

	}

	llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
	pframe += llc_sz;

	//pattrib->pktlen will be counted in rtw_tdls_fr
	pattrib->pktlen = 0;

	rtw_tdls_fr(padapter, pxmitframe, pframe, action);

	if ((pattrib->icv_len >0 )&& (pattrib->bswenc)) {
		pframe += pattrib->pktlen;
		_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len); 
		pframe += pattrib->icv_len;
	}

	pattrib->nr_frags = 1;
	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + llc_sz + 
			((pattrib->bswenc) ? pattrib->icv_len : 0) + pattrib->pktlen;
	
	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL)
	{
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);
	
	update_attrib_vcs_info(padapter, pxmitframe);
	
exit:	
	
_func_exit_;	

	return res;
}
#endif

/*
 * Calculate wlan 802.11 packet MAX size from pkt_attrib
 * This function doesn't consider fragment case
 */
u32 rtw_calculate_wlan_pkt_size_by_attribue(struct pkt_attrib *pattrib)
{
	u32	len = 0;

	len = pattrib->hdrlen + pattrib->iv_len; // WLAN Header and IV
	len += SNAP_SIZE + sizeof(u16); // LLC
	len += pattrib->pktlen;
	if (pattrib->encrypt == _TKIP_) len += 8; // MIC
	len += ((pattrib->bswenc) ? pattrib->icv_len : 0); // ICV

	return len;
}

/*

This sub-routine will perform all the following:

1. remove 802.3 header.
2. create wlan_header, based on the info in pxmitframe
3. append sta's iv/ext-iv
4. append LLC
5. move frag chunk from pframe to pxmitframe->mem
6. apply sw-encrypt, if necessary. 

*/
s32 rtw_xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;

	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;

	SIZE_PTR addr;

	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;

	struct sta_info		*psta;
	//struct sta_priv		*pstapriv = &padapter->stapriv;
	//struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	u8 *pbuf_start;

	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _SUCCESS;

_func_enter_;

	//if (pattrib->psta) {
	//	psta = pattrib->psta;
	//} else
	{	
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	}

	if(psta==NULL)
		return _FAIL;

	if (pxmitframe->buf_addr == NULL){
		printk("==> %s buf_addr==NULL \n",__FUNCTION__);
		return _FAIL;
	}

	pbuf_start = pxmitframe->buf_addr;
	
#ifdef CONFIG_USB_TX_AGGREGATION
	hw_hdr_offset =  TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
#else
	#ifdef CONFIG_TX_EARLY_MODE //for SDIO && Tx Agg
	hw_hdr_offset = TXDESC_OFFSET + EARLY_MODE_INFO_SIZE;
	#else
	hw_hdr_offset = TXDESC_OFFSET;
	#endif
#endif

	mem_start = pbuf_start +	hw_hdr_offset;

	if (rtw_make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("rtw_xmitframe_coalesce: rtw_make_wlanhdr fail; drop pkt\n"));
		printk("rtw_xmitframe_coalesce: rtw_make_wlanhdr fail; drop pkt\n");
		res = _FAIL;
		goto exit;
	}

	_rtw_open_pktfile(pkt, &pktfile);
	_rtw_pktfile_read(&pktfile, NULL, pattrib->pkt_hdrlen);

	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;//2346-4 = 2342

	while (1)
	{
		llc_sz = 0;

		mpdu_len = frg_len;

		pframe = mem_start;

		SetMFrag(mem_start);

		pframe += pattrib->hdrlen;
		mpdu_len -= pattrib->hdrlen;

		//adding icv, if necessary...
		if (pattrib->iv_len)
		{
			//if (check_fwstate(pmlmepriv, WIFI_MP_STATE))
			//	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			//else
			//	psta = rtw_get_stainfo(pstapriv, pattrib->ra);

			if (psta != NULL)
			{
				switch(pattrib->encrypt)
				{
					case _WEP40_:
					case _WEP104_:
							WEP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);	
						break;
					case _TKIP_:			
						if(bmcst)
							TKIP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
						else
							TKIP_IV(pattrib->iv, psta->dot11txpn, 0);
						break;			
					case _AES_:
						if(bmcst)
							AES_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
						else
							AES_IV(pattrib->iv, psta->dot11txpn, 0);
						break;
				}
			}

			_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len);

			RT_TRACE(_module_rtl871x_xmit_c_, _drv_notice_,
				 ("rtw_xmitframe_coalesce: keyid=%d pattrib->iv[3]=%.2x pframe=%.2x %.2x %.2x %.2x\n",
				  padapter->securitypriv.dot11PrivacyKeyIndex, pattrib->iv[3], *pframe, *(pframe+1), *(pframe+2), *(pframe+3)));

			pframe += pattrib->iv_len;

			mpdu_len -= pattrib->iv_len;
		}

		if (frg_inx == 0) {
			llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}

		if ((pattrib->icv_len >0) && (pattrib->bswenc)) {
			mpdu_len -= pattrib->icv_len;
		}


		if (bmcst) {
			// don't do fragment to broadcat/multicast packets
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, pattrib->pktlen);
		} else {
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, mpdu_len);
		}

		pframe += mem_sz;

		if ((pattrib->icv_len >0 )&& (pattrib->bswenc)) {
			_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len); 
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || (rtw_endofpktfile(&pktfile) == _TRUE))
		{
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + ((pattrib->nr_frags==1)? llc_sz:0) + 
					((pattrib->bswenc) ? pattrib->icv_len : 0) + mem_sz;
			
			ClearMFrag(mem_start);

			break;
		} else {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("%s: There're still something in packet!\n", __FUNCTION__));
		}

		addr = (SIZE_PTR)(pframe);

		mem_start = (unsigned char *)RND4(addr) + hw_hdr_offset;
		_rtw_memcpy(mem_start, pbuf_start + hw_hdr_offset, pattrib->hdrlen);	
		
	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("xmitframe_addmic(padapter, pxmitframe)==_FAIL\n"));
		printk("xmitframe_addmic(padapter, pxmitframe)==_FAIL\n");
		res = _FAIL;
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);

	if(bmcst == _FALSE)
		update_attrib_vcs_info(padapter, pxmitframe);
	else
		pattrib->vcs_mode = NONE_VCS;

exit:	
	
_func_exit_;	

	return res;
}

/* Logical Link Control(LLC) SubNetwork Attachment Point(SNAP) header
 * IEEE LLC/SNAP header contains 8 octets
 * First 3 octets comprise the LLC portion
 * SNAP portion, 5 octets, is divided into two fields:
 *	Organizationally Unique Identifier(OUI), 3 octets,
 *	type, defined by that organization, 2 octets.
 */
s32 rtw_put_snap(u8 *data, u16 h_proto)
{
	struct ieee80211_snap_hdr *snap;
	u8 *oui;

_func_enter_;

	snap = (struct ieee80211_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	*(u16 *)(data + SNAP_SIZE) = htons(h_proto);

_func_exit_;

	return SNAP_SIZE + sizeof(u16);
}

void rtw_update_protection(_adapter *padapter, u8 *ie, uint ie_len)
{

	uint	protection;
	u8	*perp;
	sint	 erp_len;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	registry_priv *pregistrypriv = &padapter->registrypriv;
	
_func_enter_;
	
	switch(pxmitpriv->vcs_setting)
	{
		case DISABLE_VCS:
			pxmitpriv->vcs = NONE_VCS;
			break;
	
		case ENABLE_VCS:
			break;
	
		case AUTO_VCS:
		default:
			perp = rtw_get_ie(ie, _ERPINFO_IE_, &erp_len, ie_len);
			if(perp == NULL)
			{
				pxmitpriv->vcs = NONE_VCS;
			}
			else
			{
				protection = (*(perp + 2)) & BIT(1);
				if (protection)
				{
					if(pregistrypriv->vcs_type == RTS_CTS)
						pxmitpriv->vcs = RTS_CTS;
					else
						pxmitpriv->vcs = CTS_TO_SELF;
				}
				else
					pxmitpriv->vcs = NONE_VCS;
			}

			break;			
	
	}

_func_exit_;

}

void rtw_count_tx_stats(PADAPTER padapter, struct xmit_frame *pxmitframe, int sz)
{
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG)
	{
		pxmitpriv->tx_bytes += sz;
#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI)
		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod += pxmitframe->agg_num;
#else
		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod++;
#endif

		psta = pxmitframe->attrib.psta;
		if (psta)
		{
			pstats = &psta->sta_stats;
#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI)
			pstats->tx_pkts += pxmitframe->agg_num;
#else
			pstats->tx_pkts++;
#endif
			pstats->tx_bytes += sz;
		}
	}
}

struct xmit_buf *rtw_alloc_xmitbuf_ext(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	_list *plist, *phead;
	_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

_func_enter_;

	_enter_critical(&pfree_queue->lock, &irqL);

	if(_rtw_queue_empty(pfree_queue) == _TRUE) {
		pxmitbuf = NULL;
	} else {

		phead = get_list_head(pfree_queue);

		plist = get_next(phead);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

		rtw_list_delete(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL)
	{
		pxmitpriv->free_xmit_extbuf_cnt--;
		#ifdef DBG_XMIT_BUF_EXT
		DBG_871X("DBG_XMIT_BUF_EXT ALLOC no=%d,  free_xmit_extbuf_cnt=%d\n",pxmitbuf->no, pxmitpriv->free_xmit_extbuf_cnt);
		#endif
		
	
		pxmitbuf->priv_data = NULL;
		//pxmitbuf->ext_tag = _TRUE;

#ifdef CONFIG_SDIO_HCI
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif
#ifdef CONFIG_PCI_HCI
		pxmitbuf->len = 0;
#endif
	}

	_exit_critical(&pfree_queue->lock, &irqL);

_func_exit_;

	return pxmitbuf;
}

s32 rtw_free_xmitbuf_ext(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

_func_enter_;

	if(pxmitbuf==NULL)
	{
		return _FAIL;
	}

	_enter_critical(&pfree_queue->lock, &irqL);

	rtw_list_delete(&pxmitbuf->list);

	rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_queue));
	pxmitpriv->free_xmit_extbuf_cnt++;
	#ifdef DBG_XMIT_BUF_EXT
	DBG_871X("DBG_XMIT_BUF_EXT FREE no=%d, free_xmit_extbuf_cnt=%d\n",pxmitbuf->no ,pxmitpriv->free_xmit_extbuf_cnt);
	#endif

	_exit_critical(&pfree_queue->lock, &irqL);

_func_exit_;

	return _SUCCESS;
} 

struct xmit_buf *rtw_alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	_list *plist, *phead;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

_func_enter_;

	//DBG_871X("+rtw_alloc_xmitbuf\n");

	_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);

	if(_rtw_queue_empty(pfree_xmitbuf_queue) == _TRUE) {
		pxmitbuf = NULL;
	} else {

		phead = get_list_head(pfree_xmitbuf_queue);

		plist = get_next(phead);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

		rtw_list_delete(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL)
	{
		pxmitpriv->free_xmitbuf_cnt--;
		#ifdef DBG_XMIT_BUF
		DBG_871X("DBG_XMIT_BUF ALLOC no=%d,  free_xmitbuf_cnt=%d\n",pxmitbuf->no, pxmitpriv->free_xmitbuf_cnt);
		#endif
		//DBG_871X("alloc, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt);

		pxmitbuf->priv_data = NULL;
		//pxmitbuf->ext_tag = _FALSE;
#ifdef CONFIG_SDIO_HCI
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 0;
		pxmitbuf->pg_num = 0;
#endif
#ifdef CONFIG_PCI_HCI
		pxmitbuf->len = 0;
#endif
	}
	#ifdef DBG_XMIT_BUF
	else
	{
		DBG_871X("DBG_XMIT_BUF rtw_alloc_xmitbuf return NULL\n");
	}
	#endif

	_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);

_func_exit_;

	return pxmitbuf;
}

s32 rtw_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

_func_enter_;

	//DBG_871X("+rtw_free_xmitbuf\n");

	if(pxmitbuf==NULL)
	{
		return _FAIL;
	}

	if(pxmitbuf->ext_tag)
	{
		rtw_free_xmitbuf_ext(pxmitpriv, pxmitbuf);
	}
	else
	{
		_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);

		rtw_list_delete(&pxmitbuf->list);	

		rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_xmitbuf_queue));

		pxmitpriv->free_xmitbuf_cnt++;
		//DBG_871X("FREE, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt);
		#ifdef DBG_XMIT_BUF
		DBG_871X("DBG_XMIT_BUF FREE no=%d, free_xmitbuf_cnt=%d\n",pxmitbuf->no ,pxmitpriv->free_xmitbuf_cnt);
		#endif
		_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);
	}

_func_exit_;	 

	return _SUCCESS;	
} 

/*
Calling context:
1. OS_TXENTRY
2. RXENTRY (rx_thread or RX_ISR/RX_CallBack)

If we turn on USE_RXTHREAD, then, no need for critical section.
Otherwise, we must use _enter/_exit critical to protect free_xmit_queue...

Must be very very cautious...

*/

struct xmit_frame *rtw_alloc_xmitframe(struct xmit_priv *pxmitpriv)//(_queue *pfree_xmit_queue)
{
	/*
		Please remember to use all the osdep_service api,
		and lock/unlock or _enter/_exit critical to protect 
		pfree_xmit_queue
	*/

	_irqL irqL;
	struct xmit_frame *pxframe = NULL;
	_list *plist, *phead;
	_queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;
#ifdef PLATFORM_LINUX
	_adapter *padapter = pxmitpriv->adapter;
#endif //PLATFORM_LINUX

_func_enter_;

	_enter_critical_bh(&pfree_xmit_queue->lock, &irqL);

	if (_rtw_queue_empty(pfree_xmit_queue) == _TRUE) {
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_alloc_xmitframe:%d\n", pxmitpriv->free_xmitframe_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = get_next(phead);

		pxframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		rtw_list_delete(&(pxframe->list));
	}

	if (pxframe !=  NULL)//default value setting
	{
		pxmitpriv->free_xmitframe_cnt--;

		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe():free_xmitframe_cnt=%d\n", pxmitpriv->free_xmitframe_cnt));

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		_rtw_memset(&pxframe->attrib, 0, sizeof(struct pkt_attrib));
		//pxframe->attrib.psta = NULL;

		pxframe->frame_tag = DATA_FRAMETAG;

#ifdef CONFIG_USB_HCI
		pxframe->pkt = NULL;
		pxframe->pkt_offset = 1;//default use pkt_offset to fill tx desc

#ifdef CONFIG_USB_TX_AGGREGATION
		pxframe->agg_num = 1;
#endif

#endif //#ifdef CONFIG_USB_HCI

#ifdef CONFIG_SDIO_HCI
		pxframe->pg_num = 1;
		pxframe->agg_num = 1;
#endif

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
		if(pxmitpriv->free_xmitframe_cnt==1)
		{
			if (!rtw_netif_queue_stopped(padapter->pnetdev))
				rtw_netif_stop_queue(padapter->pnetdev);
		}
#endif
#endif

	}

	_exit_critical_bh(&pfree_xmit_queue->lock, &irqL);

_func_exit_;

	return pxframe;
}

s32 rtw_free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{	
	_irqL irqL;
	_queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;		
	_adapter *padapter = pxmitpriv->adapter;
	_pkt *pndis_pkt = NULL;

_func_enter_;	

	if (pxmitframe == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("======rtw_free_xmitframe():pxmitframe==NULL!!!!!!!!!!\n"));
		goto exit;
	}

	_enter_critical_bh(&pfree_xmit_queue->lock, &irqL);

	rtw_list_delete(&pxmitframe->list);	

	if (pxmitframe->pkt){
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	rtw_list_insert_tail(&pxmitframe->list, get_list_head(pfree_xmit_queue));

	pxmitpriv->free_xmitframe_cnt++;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("rtw_free_xmitframe():free_xmitframe_cnt=%d\n", pxmitpriv->free_xmitframe_cnt));

	_exit_critical_bh(&pfree_xmit_queue->lock, &irqL);


	if(pndis_pkt)
		rtw_os_pkt_complete(padapter, pndis_pkt);

exit:

_func_exit_;

	return _SUCCESS;
}

s32 rtw_free_xmitframe_ex(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{	
			
_func_enter_;	

	if(pxmitframe==NULL){
		goto exit;
	}

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("rtw_free_xmitframe_ex()\n"));
	
	rtw_free_xmitframe(pxmitpriv, pxmitframe);	  

exit:
	
_func_exit_;	 

	return _SUCCESS;	
} 

void rtw_free_xmitframe_queue(struct xmit_priv *pxmitpriv, _queue *pframequeue)
{
	_irqL irqL;
	_list	*plist, *phead;
	struct	xmit_frame 	*pxmitframe;

_func_enter_;	

	_enter_critical_bh(&(pframequeue->lock), &irqL);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);
	
	while (rtw_end_of_queue_search(phead, plist) == _FALSE)
	{
			
		pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		plist = get_next(plist); 
		
		rtw_free_xmitframe(pxmitpriv,pxmitframe);
			
	}
	_exit_critical_bh(&(pframequeue->lock), &irqL);

_func_exit_;
}

s32 rtw_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (rtw_xmit_classifier(padapter, pxmitframe) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("rtw_xmitframe_enqueue: drop xmit pkt for classifier fail\n"));
//		pxmitframe->pkt = NULL;
		return _FAIL;
	}

	return _SUCCESS;
}

static struct xmit_frame *dequeue_one_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, struct tx_servq *ptxservq, _queue *pframe_queue)
{
	_list	*xmitframe_plist, *xmitframe_phead;
	struct	xmit_frame	*pxmitframe=NULL;

	xmitframe_phead = get_list_head(pframe_queue);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
	{
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

/*#ifdef RTK_DMP_PLATFORM
#ifdef CONFIG_USB_TX_AGGREGATION
		if((ptxservq->qcnt>0) && (ptxservq->qcnt<=2))
		{
			pxmitframe = NULL;

			tasklet_schedule(&pxmitpriv->xmit_tasklet);

			break;
		}
#endif
#endif*/
		rtw_list_delete(&pxmitframe->list);

		ptxservq->qcnt--;

		//rtw_list_insert_tail(&pxmitframe->list, &phwxmit->pending);

		//ptxservq->qcnt--;

		break;		

		pxmitframe = NULL;

	}

	return pxmitframe;
}

struct xmit_frame* rtw_dequeue_xframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit_i, sint entry)
{
	_irqL irqL0;
	_list *sta_plist, *sta_phead;
	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	_queue *pframe_queue = NULL;
	struct xmit_frame *pxmitframe = NULL;
	_adapter *padapter = pxmitpriv->adapter;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	int i, inx[4];
#ifdef CONFIG_USB_HCI
//	int j, tmp, acirp_cnt[4];
#endif

_func_enter_;

	inx[0] = 0; inx[1] = 1; inx[2] = 2; inx[3] = 3;

	if(pregpriv->wifi_spec==1)
	{
		int j, tmp, acirp_cnt[4];
#if 0
		if(flags<XMIT_QUEUE_ENTRY)
		{
			//priority exchange according to the completed xmitbuf flags.
			inx[flags] = 0;
			inx[0] = flags;
		}
#endif	
	
#ifdef CONFIG_USB_HCI
		//entry indx: 0->vo, 1->vi, 2->be, 3->bk.
		acirp_cnt[0] = pxmitpriv->voq_cnt;
		acirp_cnt[1] = pxmitpriv->viq_cnt;
		acirp_cnt[2] = pxmitpriv->beq_cnt;
		acirp_cnt[3] = pxmitpriv->bkq_cnt;

		for(i=0; i<4; i++)
		{
			for(j=i+1; j<4; j++)
			{
				if(acirp_cnt[j]<acirp_cnt[i])
				{
					tmp = acirp_cnt[i];
					acirp_cnt[i] = acirp_cnt[j];
					acirp_cnt[j] = tmp;

					tmp = inx[i];
					inx[i] = inx[j];
					inx[j] = tmp;
				}
			}
		}
#endif
	}

	_enter_critical_bh(&pxmitpriv->lock, &irqL0);

	for(i = 0; i < entry; i++) 
	{
		phwxmit = phwxmit_i + inx[i];

		//_enter_critical_ex(&phwxmit->sta_queue->lock, &irqL0);

		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = get_next(sta_phead);

		while ((rtw_end_of_queue_search(sta_phead, sta_plist)) == _FALSE)
		{

			ptxservq= LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);

			pframe_queue = &ptxservq->sta_pending;

			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit, ptxservq, pframe_queue);

			if(pxmitframe)
			{
				phwxmit->accnt--;

				//Remove sta node when there is no pending packets.
				if(_rtw_queue_empty(pframe_queue)) //must be done after get_next and before break
					rtw_list_delete(&ptxservq->tx_pending);

				//_exit_critical_ex(&phwxmit->sta_queue->lock, &irqL0);

				goto exit;
			}

			sta_plist = get_next(sta_plist);

		}

		//_exit_critical_ex(&phwxmit->sta_queue->lock, &irqL0);

	}

exit:

	_exit_critical_bh(&pxmitpriv->lock, &irqL0);

_func_exit_;

	return pxmitframe;
}

#if 1
struct tx_servq *rtw_get_sta_pending(_adapter *padapter, struct sta_info *psta, sint up, u8 *ac)
{
	struct tx_servq *ptxservq;
	
_func_enter_;	

	switch (up) 
	{
		case 1:
		case 2:
			ptxservq = &(psta->sta_xmitpriv.bk_q);
			*(ac) = 3;
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : BK \n"));
			break;

		case 4:
		case 5:
			ptxservq = &(psta->sta_xmitpriv.vi_q);
			*(ac) = 1;
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : VI\n"));
			break;

		case 6:
		case 7:
			ptxservq = &(psta->sta_xmitpriv.vo_q);
			*(ac) = 0;
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : VO \n"));			
			break;

		case 0:
		case 3:
		default:
			ptxservq = &(psta->sta_xmitpriv.be_q);
			*(ac) = 2;
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : BE \n"));				
		break;
		
	}

_func_exit_;

	return ptxservq;			
}
#else
__inline static struct tx_servq *rtw_get_sta_pending
	(_adapter *padapter, _queue **ppstapending, struct sta_info *psta, sint up)
{
	struct tx_servq *ptxservq;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;
	
_func_enter_;	

#ifdef CONFIG_RTL8711

	if(IS_MCAST(psta->hwaddr))
	{
		ptxservq = &(psta->sta_xmitpriv.be_q); // we will use be_q to queue bc/mc frames in BCMC_stainfo
		*ppstapending = &padapter->xmitpriv.bm_pending; 
	}
	else
#endif		
	{
		switch (up) 
		{
			case 1:
			case 2:
				ptxservq = &(psta->sta_xmitpriv.bk_q);
				*ppstapending = &padapter->xmitpriv.bk_pending;
				(phwxmits+3)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : BK \n"));
				break;

			case 4:
			case 5:
				ptxservq = &(psta->sta_xmitpriv.vi_q);
				*ppstapending = &padapter->xmitpriv.vi_pending;
				(phwxmits+1)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : VI\n"));
				break;

			case 6:
			case 7:
				ptxservq = &(psta->sta_xmitpriv.vo_q);
				*ppstapending = &padapter->xmitpriv.vo_pending;
				(phwxmits+0)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : VO \n"));			
				break;

			case 0:
			case 3:
			default:
				ptxservq = &(psta->sta_xmitpriv.be_q);
				*ppstapending = &padapter->xmitpriv.be_pending;
				(phwxmits+2)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_get_sta_pending : BE \n"));				
			break;
			
		}

	}

_func_exit_;

	return ptxservq;			
}
#endif

/*
 * Will enqueue pxmitframe to the proper queue,
 * and indicate it to xx_pending list.....
 */
s32 rtw_xmit_classifier(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	//_irqL irqL0;
	u8	ac_index;
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct hw_xmit	*phwxmits =  padapter->xmitpriv.hwxmits;
	sint res = _SUCCESS;

_func_enter_;

	if (pattrib->psta) {
		psta = pattrib->psta;		
	} else {
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);
	}

	if (psta == NULL) {
		res = _FAIL;
		printk("rtw_xmit_classifier: psta == NULL\n");
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("rtw_xmit_classifier: psta == NULL\n"));
		goto exit;
	}

	ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

	//_enter_critical(&pstapending->lock, &irqL0);

	if (rtw_is_list_empty(&ptxservq->tx_pending)) {
		rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(phwxmits[ac_index].sta_queue));
	}

	//_enter_critical(&ptxservq->sta_pending.lock, &irqL1);

	rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;
	phwxmits[ac_index].accnt++;

	//_exit_critical(&ptxservq->sta_pending.lock, &irqL1);

	//_exit_critical(&pstapending->lock, &irqL0);

exit:

_func_exit_;

	return res;
}

void rtw_alloc_hwxmits(_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	pxmitpriv->hwxmits = (struct hw_xmit *)rtw_zmalloc(sizeof (struct hw_xmit) * pxmitpriv->hwxmit_entry);	
	
	hwxmits = pxmitpriv->hwxmits;

	if(pxmitpriv->hwxmit_entry == 5)
	{
		//pxmitpriv->bmc_txqueue.head = 0;
		//hwxmits[0] .phwtxqueue = &pxmitpriv->bmc_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->bm_pending;
	
		//pxmitpriv->vo_txqueue.head = 0;
		//hwxmits[1] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vo_pending;

		//pxmitpriv->vi_txqueue.head = 0;
		//hwxmits[2] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->vi_pending;
	
		//pxmitpriv->bk_txqueue.head = 0;
		//hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;

      		//pxmitpriv->be_txqueue.head = 0;
		//hwxmits[4] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[4] .sta_queue = &pxmitpriv->be_pending;
		
	}	
	else if(pxmitpriv->hwxmit_entry == 4)
	{

		//pxmitpriv->vo_txqueue.head = 0;
		//hwxmits[0] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->vo_pending;

		//pxmitpriv->vi_txqueue.head = 0;
		//hwxmits[1] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vi_pending;

		//pxmitpriv->be_txqueue.head = 0;
		//hwxmits[2] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->be_pending;
	
		//pxmitpriv->bk_txqueue.head = 0;
		//hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
	}
	else
	{
		

	}
	

}

void rtw_free_hwxmits(_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	hwxmits = pxmitpriv->hwxmits;
	if(hwxmits)
		rtw_mfree((u8 *)hwxmits, (sizeof (struct hw_xmit) * pxmitpriv->hwxmit_entry));
}

void rtw_init_hwxmits(struct hw_xmit *phwxmit, sint entry)
{
	sint i;
_func_enter_;	
	for(i = 0; i < entry; i++, phwxmit++)
	{
		//_rtw_spinlock_init(&phwxmit->xmit_lock);
		//_rtw_init_listhead(&phwxmit->pending);		
		//phwxmit->txcmdcnt = 0;
		phwxmit->accnt = 0;
	}
_func_exit_;	
}

#ifdef CONFIG_BR_EXT
int rtw_br_client_tx(_adapter *padapter, struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_irqL irqL;
	//if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE)
	{
		void dhcp_flag_bcast(_adapter *priv, struct sk_buff *skb);
		int res, is_vlan_tag=0, i, do_nat25=1;
		unsigned short vlan_hdr=0;
		void *br_port = NULL;

		//mac_clone_handle_frame(priv, skb);

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		br_port = padapter->pnetdev->br_port;
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		rcu_read_lock();
		br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
		rcu_read_unlock();
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		_enter_critical_bh(&padapter->br_ext_lock, &irqL);
		if (	!(skb->data[0] & 1) &&
				br_port &&
				memcmp(skb->data+MACADDRLEN, padapter->br_mac, MACADDRLEN) &&
				*((unsigned short *)(skb->data+MACADDRLEN*2)) != __constant_htons(ETH_P_8021Q) &&
				*((unsigned short *)(skb->data+MACADDRLEN*2)) == __constant_htons(ETH_P_IP) &&
				!memcmp(padapter->scdb_mac, skb->data+MACADDRLEN, MACADDRLEN) && padapter->scdb_entry) {
			memcpy(skb->data+MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			padapter->scdb_entry->ageing_timer = jiffies;
			_exit_critical_bh(&padapter->br_ext_lock, &irqL);
		}
		else
		//if (!priv->pmib->ethBrExtInfo.nat25_disable) 		
		{
//			if (priv->dev->br_port &&
//				 !memcmp(skb->data+MACADDRLEN, priv->br_mac, MACADDRLEN)) {
#if 1
			if (*((unsigned short *)(skb->data+MACADDRLEN*2)) == __constant_htons(ETH_P_8021Q)) {
				is_vlan_tag = 1;
				vlan_hdr = *((unsigned short *)(skb->data+MACADDRLEN*2+2));
				for (i=0; i<6; i++)
					*((unsigned short *)(skb->data+MACADDRLEN*2+2-i*2)) = *((unsigned short *)(skb->data+MACADDRLEN*2-2-i*2));
				skb_pull(skb, 4);
			}
			//if SA == br_mac && skb== IP  => copy SIP to br_ip ?? why
			if (!memcmp(skb->data+MACADDRLEN, padapter->br_mac, MACADDRLEN) &&
				(*((unsigned short *)(skb->data+MACADDRLEN*2)) == __constant_htons(ETH_P_IP)))
				memcpy(padapter->br_ip, skb->data+WLAN_ETHHDR_LEN+12, 4);

			if (*((unsigned short *)(skb->data+MACADDRLEN*2)) == __constant_htons(ETH_P_IP)) {
				if (memcmp(padapter->scdb_mac, skb->data+MACADDRLEN, MACADDRLEN)) {
					void *scdb_findEntry(_adapter *priv, unsigned char *macAddr, unsigned char *ipAddr);
					
					if ((padapter->scdb_entry = (struct nat25_network_db_entry *)scdb_findEntry(padapter,
								skb->data+MACADDRLEN, skb->data+WLAN_ETHHDR_LEN+12)) != NULL) {
						memcpy(padapter->scdb_mac, skb->data+MACADDRLEN, MACADDRLEN);
						memcpy(padapter->scdb_ip, skb->data+WLAN_ETHHDR_LEN+12, 4);
						padapter->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					}
				}
				else {
					if (padapter->scdb_entry) {
						padapter->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					}
					else {
						memset(padapter->scdb_mac, 0, MACADDRLEN);
						memset(padapter->scdb_ip, 0, 4);
					}
				}
			}
			_exit_critical_bh(&padapter->br_ext_lock, &irqL);
#endif // 1
			if (do_nat25)
			{
				int nat25_db_handle(_adapter *priv, struct sk_buff *skb, int method);
				if (nat25_db_handle(padapter, skb, NAT25_CHECK) == 0) {
					struct sk_buff *newskb;

					if (is_vlan_tag) {
						skb_push(skb, 4);
						for (i=0; i<6; i++)
							*((unsigned short *)(skb->data+i*2)) = *((unsigned short *)(skb->data+4+i*2));
						*((unsigned short *)(skb->data+MACADDRLEN*2)) = __constant_htons(ETH_P_8021Q);
						*((unsigned short *)(skb->data+MACADDRLEN*2+2)) = vlan_hdr;
					}

					newskb = skb_copy(skb, GFP_ATOMIC);
					if (newskb == NULL) {
						//priv->ext_stats.tx_drops++;
						DEBUG_ERR("TX DROP: skb_copy fail!\n");
						//goto stop_proc;
						return -1;
					}
					dev_kfree_skb_any(skb);

					*pskb = skb = newskb;
					if (is_vlan_tag) {
						vlan_hdr = *((unsigned short *)(skb->data+MACADDRLEN*2+2));
						for (i=0; i<6; i++)
							*((unsigned short *)(skb->data+MACADDRLEN*2+2-i*2)) = *((unsigned short *)(skb->data+MACADDRLEN*2-2-i*2));
						skb_pull(skb, 4);
					}
				}

				if (skb_is_nonlinear(skb))
					DEBUG_ERR("%s(): skb_is_nonlinear!!\n", __FUNCTION__);
					

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
				res = skb_linearize(skb, GFP_ATOMIC);
#else	// (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
				res = skb_linearize(skb);
#endif	// (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
				if (res < 0) {				
						DEBUG_ERR("TX DROP: skb_linearize fail!\n");
						//goto free_and_stop;
						return -1;
				}
				
				res = nat25_db_handle(padapter, skb, NAT25_INSERT);
				if (res < 0) {
					if (res == -2) {
						//priv->ext_stats.tx_drops++;
						DEBUG_ERR("TX DROP: nat25_db_handle fail!\n");
						//goto free_and_stop;
						return -1;

					}
					// we just print warning message and let it go
					//DEBUG_WARN("%s()-%d: nat25_db_handle INSERT Warning!\n", __FUNCTION__, __LINE__);
					//return -1; // return -1 will cause system crash on 2011/08/30!
					return 0;
				}
			}

			memcpy(skb->data+MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);

			dhcp_flag_bcast(padapter, skb);

			if (is_vlan_tag) {
				skb_push(skb, 4);
				for (i=0; i<6; i++)
					*((unsigned short *)(skb->data+i*2)) = *((unsigned short *)(skb->data+4+i*2));
				*((unsigned short *)(skb->data+MACADDRLEN*2)) = __constant_htons(ETH_P_8021Q);
				*((unsigned short *)(skb->data+MACADDRLEN*2+2)) = vlan_hdr;
			}
		}
#if 0		
		else{
			if (*((unsigned short *)(skb->data+MACADDRLEN*2)) == __constant_htons(ETH_P_8021Q)) {
				is_vlan_tag = 1;
			}
				
			if(is_vlan_tag){
				if(ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A_VALN(skb->data)){
                                        memcpy(skb->data+MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
				}
			}else
			{
				if(ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A(skb->data)){
                                        memcpy(skb->data+MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
				}
			}	
		}
#endif	// 0

		// check if SA is equal to our MAC
		if (memcmp(skb->data+MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN)) {
			//priv->ext_stats.tx_drops++;
			DEBUG_ERR("TX DROP: untransformed frame SA:%02X%02X%02X%02X%02X%02X!\n",
				skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11]);
			//goto free_and_stop;
			return -1;
		}
	}
	return 0;
}
#endif	// CONFIG_BR_EXT

static void do_queue_select(_adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;
		
	qsel = pattrib->priority;
	RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("### do_queue_select priority=%d ,qsel = %d\n",pattrib->priority ,qsel));

#ifdef CONFIG_CONCURRENT_MODE	
//	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
//		qsel = 7;//
#endif
	
	pattrib->qsel = qsel;
}

/*
 * The main transmit(tx) entry
 *
 * Return
 *	1	enqueue
 *	0	success, hardware will handle this xmit frame(packet)
 *	<0	fail
 */
s32 rtw_xmit(_adapter *padapter, _pkt **ppkt)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL0;
#endif
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;
#ifdef CONFIG_BR_EXT
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	void *br_port = NULL;
#endif	// CONFIG_BR_EXT

	s32 res;

	pxmitframe = rtw_alloc_xmitframe(pxmitpriv);
	if (pxmitframe == NULL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit: no more pxmitframe\n"));
		//#ifdef DBG_TX_DROP_FRAME
		DBG_871X("DBG_TX_DROP_FRAME %s no more pxmitframe\n", __FUNCTION__);
		//#endif
		return -1;
	}

#ifdef CONFIG_BR_EXT

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	br_port = padapter->pnetdev->br_port;
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
	br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
	rcu_read_unlock();
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))

	if( br_port && check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE)
	{
		res = rtw_br_client_tx(padapter, ppkt);
		if (res == -1)
		{
			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			return -1;
		}
	}	

#endif	// CONFIG_BR_EXT

	res = update_attrib(padapter, *ppkt, &pxmitframe->attrib);
	if (res == _FAIL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit: update attrib fail\n"));
		#ifdef DBG_TX_DROP_FRAME
		DBG_871X("DBG_TX_DROP_FRAME %s update attrib fail\n", __FUNCTION__);
		#endif
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		return -1;
	}
	pxmitframe->pkt = *ppkt;

	rtw_led_control(padapter, LED_CTL_TX);

	do_queue_select(padapter, &pxmitframe->attrib);

#ifdef CONFIG_AP_MODE
	_enter_critical_bh(&pxmitpriv->lock, &irqL0);
	if(xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe) == _TRUE)
	{
		_exit_critical_bh(&pxmitpriv->lock, &irqL0);
		return 1;		
	}
	_exit_critical_bh(&pxmitpriv->lock, &irqL0);
#endif

	if (rtw_hal_xmit(padapter, pxmitframe) == _FALSE)
		return 1;

	return 0;
}

#ifdef CONFIG_TDLS
void process_enqueue_frame(_adapter *padapter, struct xmit_frame *pxmitframe, struct sta_info *psta)
{
	_irqL irqL;
	struct sta_info *ptdls_sta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	ptdls_sta=rtw_get_stainfo(pstapriv, pattrib->dst);

	//enqueue frame for TDLS sleeping STA
	rtw_list_delete(&pxmitframe->list);
	_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);	
	rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptdls_sta->sleep_q));
	ptdls_sta->sleepq_len++;

	//indicate 4-AC queue bit in TDLS peer traffic indication
	switch(pattrib->priority)
	{
		case 1:
		case 2:
			ptdls_sta->uapsd_bk = ptdls_sta->uapsd_bk | BIT(0);
			break;
		case 4:
		case 5:
			ptdls_sta->uapsd_vi = ptdls_sta->uapsd_vi | BIT(0);
			break;
		case 6:
		case 7:
			ptdls_sta->uapsd_vo = ptdls_sta->uapsd_vo | BIT(0);
			break;
		case 0:
		case 3:
		default:
			ptdls_sta->uapsd_be = ptdls_sta->uapsd_be | BIT(0);
			break;
	}

	ptdls_sta->sleepq_ac_len++;

	_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);			

}

sint xmitframe_enqueue_for_tdls_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	sint ret=_FALSE;

	_irqL irqL;
	struct sta_info *ptdls_sta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	int i;
	
	ptdls_sta=rtw_get_stainfo(pstapriv, pattrib->dst);
	if(ptdls_sta==NULL){
		return ret;
	}else if(ptdls_sta->tdls_sta_state&TDLS_LINKED_STATE){

		if(pattrib->triggered==1)
		{
			ret = _TRUE;
			return ret;
			}

		_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);	
		
		if(ptdls_sta->state&WIFI_SLEEP_STATE)
		{
			rtw_list_delete(&pxmitframe->list);
		
			//_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
			
			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptdls_sta->sleep_q));
			
			ptdls_sta->sleepq_len++;
			ptdls_sta->sleepq_ac_len++;

			//indicate 4-AC queue bit in TDLS peer traffic indication
			switch(pattrib->priority)
			{
				case 1:
				case 2:
					ptdls_sta->uapsd_bk = ptdls_sta->uapsd_bk | BIT(1);
					break;
				case 4:
				case 5:
					ptdls_sta->uapsd_vi = ptdls_sta->uapsd_vi | BIT(1);
					break;
				case 6:
				case 7:
					ptdls_sta->uapsd_vo = ptdls_sta->uapsd_vo | BIT(1);
					break;
				case 0:
				case 3:
				default:
					ptdls_sta->uapsd_be = ptdls_sta->uapsd_be | BIT(1);
					break;
			}

			if(ptdls_sta->sleepq_len==1)
			{
				//transmit TDLS PTI via AP
				ptdls_sta->option=2;
				rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_SD_PTI);
			}
			ret = _TRUE;

		}

		_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);	
	}

	return ret;
	
}
#endif //CONFIG_TDLS

#ifdef CONFIG_AP_MODE

sint xmitframe_enqueue_for_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_irqL irqL;
	sint ret=_FALSE;
	struct sta_info *psta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	sint bmcst = IS_MCAST(pattrib->ra);
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	if( ptdlsinfo->setup_state & TDLS_LINKED_STATE )
	{
		ret = xmitframe_enqueue_for_tdls_sleeping_sta(padapter, pxmitframe);
		return ret;
	}
#endif //CONFIG_TDLS

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _FALSE)
	    return ret;

	if(pattrib->psta)
	{
		psta = pattrib->psta;
	}
	else
	{
		psta=rtw_get_stainfo(pstapriv, pattrib->ra);
	}

	if(psta==NULL)
		return ret;

	if(pattrib->triggered==1)
	{
		//DBG_871X("directly xmit pspoll_triggered packet\n");

		//pattrib->triggered=0;

		if(bmcst)
			pattrib->qsel = 0x11;//HIQ
		

		return ret;
	}


	if(bmcst)
	{
		_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
	
		if(pstapriv->sta_dz_bitmap)//if anyone sta is in ps mode
		{
			//pattrib->qsel = 0x11;//HIQ
			
			rtw_list_delete(&pxmitframe->list);
			
			//_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
			
			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));
			
			psta->sleepq_len++;

			pstapriv->tim_bitmap |= BIT(0);//
			pstapriv->sta_dz_bitmap |= BIT(0);
			
			//DBG_871X("enqueue, sq_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap);

			update_beacon(padapter, _TIM_IE_, NULL, _FALSE);//tx bc/mc packets after upate bcn
			
			//_exit_critical_bh(&psta->sleep_q.lock, &irqL);				
			
			ret = _TRUE;			
			
		}
		
		_exit_critical_bh(&psta->sleep_q.lock, &irqL);	
		
		return ret;
		
	}
	

	_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
	
	if(psta->state&WIFI_SLEEP_STATE)
	{
		u8 wmmps_ac=0;
	
		if(pstapriv->sta_dz_bitmap&BIT(psta->aid))	
		{			
			rtw_list_delete(&pxmitframe->list);
		
			//_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
			
			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));
			
			psta->sleepq_len++;

			switch(pattrib->priority)
			{
				case 1:
				case 2:
					wmmps_ac = psta->uapsd_bk&BIT(0);
					break;
				case 4:
				case 5:
					wmmps_ac = psta->uapsd_vi&BIT(0);
					break;
				case 6:
				case 7:
					wmmps_ac = psta->uapsd_vo&BIT(0);
					break;
				case 0:
				case 3:
				default:
					wmmps_ac = psta->uapsd_be&BIT(0);
					break;
			}

			if(wmmps_ac)
				psta->sleepq_ac_len++;

			if(((psta->has_legacy_ac) && (!wmmps_ac)) ||((!psta->has_legacy_ac)&&(wmmps_ac)))
			{
				pstapriv->tim_bitmap |= BIT(psta->aid);

				//DBG_871X("enqueue, sq_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap);

				if(psta->sleepq_len==1)
				{
					//DBG_871X("sleepq_len==1, update BCNTIM\n");
					//upate BCN for TIM IE
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
				}
			}

			//_exit_critical_bh(&psta->sleep_q.lock, &irqL);			

			//if(psta->sleepq_len > (NR_XMITFRAME>>3))
			//{
			//	wakeup_sta_to_xmit(padapter, psta);
			//}	

			ret = _TRUE;

		}

	}

	_exit_critical_bh(&psta->sleep_q.lock, &irqL);	

	return ret;
	
}

static void dequeue_xmitframes_to_sleeping_queue(_adapter *padapter, struct sta_info *psta, _queue *pframequeue)
{
	_list	*plist, *phead;
	u8	ac_index;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib;
	struct xmit_frame 	*pxmitframe;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;
	
	phead = get_list_head(pframequeue);
	plist = get_next(phead);
	
	while (rtw_end_of_queue_search(phead, plist) == _FALSE)
	{			
		pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		plist = get_next(plist);	
		
		xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe);		

		pattrib = &pxmitframe->attrib;

		ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

		ptxservq->qcnt--;
		phwxmits[ac_index].accnt--;		
	}
	
}

void stop_sta_xmit(_adapter *padapter, struct sta_info *psta)
{	
	_irqL irqL0;	
	struct sta_info *psta_bmc;
	struct sta_xmit_priv *pstaxmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;	
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;	
	
	pstaxmitpriv = &psta->sta_xmitpriv;

	//for BC/MC Frames
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	
			
	_enter_critical_bh(&pxmitpriv->lock, &irqL0);

	psta->state |= WIFI_SLEEP_STATE;
	
#ifdef CONFIG_TDLS
	if( !(psta->tdls_sta_state & TDLS_LINKED_STATE) )
#endif //CONFIG_TDLS
	pstapriv->sta_dz_bitmap |= BIT(psta->aid);
	
	

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vo_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vo_q.tx_pending));


	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vi_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vi_q.tx_pending));


	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->be_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->be_q.tx_pending));
	

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->bk_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->bk_q.tx_pending));

#ifdef CONFIG_TDLS
	if( !(psta->tdls_sta_state & TDLS_LINKED_STATE) )
	{
		if( psta_bmc != NULL )
		{
#endif //CONFIG_TDLS


	//for BC/MC Frames
	pstaxmitpriv = &psta_bmc->sta_xmitpriv;
	dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->be_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->be_q.tx_pending));
	

#ifdef CONFIG_TDLS	
		}
	}
#endif //CONFIG_TDLS	
	_exit_critical_bh(&pxmitpriv->lock, &irqL0);
	

}	

void wakeup_sta_to_xmit(_adapter *padapter, struct sta_info *psta)
{	 
	_irqL irqL;	 
	u8 update_mask=0, wmmps_ac=0;
	struct sta_info *psta_bmc;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	_enter_critical_bh(&psta->sleep_q.lock, &irqL);	

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
	{			
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);            

		rtw_list_delete(&pxmitframe->list);

		switch(pxmitframe->attrib.priority)
		{
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk&BIT(1);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi&BIT(1);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo&BIT(1);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be&BIT(1);
				break;	
		}

		psta->sleepq_len--;
		if(psta->sleepq_len>0)
			pxmitframe->attrib.mdata = 1;
		else
			pxmitframe->attrib.mdata = 0;

		if(wmmps_ac)
		{
			psta->sleepq_ac_len--;
			if(psta->sleepq_ac_len>0)
			{
				pxmitframe->attrib.mdata = 1;
				pxmitframe->attrib.eosp = 0;
			}
			else
			{
				pxmitframe->attrib.mdata = 0;
				pxmitframe->attrib.eosp = 1;
			}
		}
		
		pxmitframe->attrib.triggered = 1;

		_exit_critical_bh(&psta->sleep_q.lock, &irqL);	
		if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
		{		
			rtw_os_xmit_complete(padapter, pxmitframe);
		}		
		_enter_critical_bh(&psta->sleep_q.lock, &irqL);

		if(psta->sleepq_len==0)
		{
#ifdef CONFIG_TDLS
			if( psta->tdls_sta_state & TDLS_LINKED_STATE )
			{
				if(psta->state&WIFI_SLEEP_STATE)
					psta->state ^= WIFI_SLEEP_STATE;
				
				_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				return;
			}
#endif //CONFIG_TDLS
			pstapriv->tim_bitmap &= ~BIT(psta->aid);

			//DBG_871X("wakeup to xmit, qlen==0, update_BCNTIM, tim=%x\n", pstapriv->tim_bitmap);
			//upate BCN for TIM IE
			//update_BCNTIM(padapter);
			update_mask = BIT(0);

			if(psta->state&WIFI_SLEEP_STATE)
				psta->state ^= WIFI_SLEEP_STATE;

			pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);				
				
		}
		
	}	
	
	_exit_critical_bh(&psta->sleep_q.lock, &irqL);	


	//for BC/MC Frames
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if(!psta_bmc)
		return;

	if((pstapriv->sta_dz_bitmap&0xfffe) == 0x0)//no any sta in ps mode
	{
		_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
		{			
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			rtw_list_delete(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if(psta_bmc->sleepq_len>0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;
			

			pxmitframe->attrib.triggered = 1;

			_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	
			if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
			{		
				rtw_os_xmit_complete(padapter, pxmitframe);
			}
			_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	


			if(psta_bmc->sleepq_len==0)
			{
				pstapriv->tim_bitmap &= ~BIT(0);
				pstapriv->sta_dz_bitmap &= ~BIT(0);

				//DBG_871X("wakeup to xmit, qlen==0, update_BCNTIM, tim=%x\n", pstapriv->tim_bitmap);
				//upate BCN for TIM IE
				//update_BCNTIM(padapter);
				update_mask |= BIT(1);
			}
		
		}	
	
		_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	

	}	

	
	if(update_mask)
	{
		//update_BCNTIM(padapter);
		update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
	}	
	
}

void xmit_delivery_enabled_frames(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;
	u8 wmmps_ac=0;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	_enter_critical_bh(&psta->sleep_q.lock, &irqL);	

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
	{			
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

		switch(pxmitframe->attrib.priority)
		{
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk&BIT(1);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi&BIT(1);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo&BIT(1);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be&BIT(1);
				break;	
		}
		
		if(!wmmps_ac)
			continue;

		rtw_list_delete(&pxmitframe->list);

		psta->sleepq_len--;
		psta->sleepq_ac_len--;

		if(psta->sleepq_ac_len>0)
		{
			pxmitframe->attrib.mdata = 1;
			pxmitframe->attrib.eosp = 0;
		}
		else
		{
			pxmitframe->attrib.mdata = 0;
			pxmitframe->attrib.eosp = 1;
		}

		pxmitframe->attrib.triggered = 1;

		if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
		{		
			rtw_os_xmit_complete(padapter, pxmitframe);
		}

		if((psta->sleepq_ac_len==0) && (!psta->has_legacy_ac) && (wmmps_ac))
		{
#ifdef CONFIG_TDLS
			if(psta->tdls_sta_state & TDLS_LINKED_STATE )
			{
				_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				return;
			}
#endif //CONFIG_TDLS
			pstapriv->tim_bitmap &= ~BIT(psta->aid);

			//DBG_871X("wakeup to xmit, qlen==0, update_BCNTIM, tim=%x\n", pstapriv->tim_bitmap);
			//upate BCN for TIM IE
			//update_BCNTIM(padapter);
			update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
			//update_mask = BIT(0);
		}
	
	}	
	
	_exit_critical_bh(&psta->sleep_q.lock, &irqL);	

}

#endif

#ifdef CONFIG_XMIT_THREAD_MODE
void enqueue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv,
	struct xmit_buf *pxmitbuf)
{
	_irqL irql;
	_queue *pqueue;


	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);
	rtw_list_delete(&pxmitbuf->list);
	rtw_list_insert_tail(&pxmitbuf->list, get_list_head(pqueue));
	_exit_critical_bh(&pqueue->lock, &irql);

	_rtw_up_sema(&pxmitpriv->xmit_sema);
}

struct xmit_buf* dequeue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;


	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		_list *plist, *phead;

		phead = get_list_head(pqueue);
		plist = get_next(phead);
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_list_delete(&pxmitbuf->list);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

struct xmit_buf* dequeue_pending_xmitbuf_under_survey(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
#ifdef CONFIG_USB_HCI	
	struct xmit_frame *pxmitframe;
#endif 
	_queue *pqueue;


	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		_list *plist, *phead;
		u8 type;

		phead = get_list_head(pqueue);
		plist = phead;
		do {
			plist = get_next(plist);
				if (plist == phead) break;
			
			pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

#ifdef CONFIG_USB_HCI
			pxmitframe = (struct xmit_frame*)pxmitbuf->priv_data;			
			if(pxmitframe)
			{				
				type = GetFrameSubType(pxmitbuf->pbuf + TXDESC_SIZE + pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
			}
			else
			{
				DBG_871X("%s, !!!ERROR!!! For USB, TODO ITEM \n", __FUNCTION__);
			}					
#else
			type = GetFrameSubType(pxmitbuf->pbuf + TXDESC_OFFSET);
#endif
			
			if ((type == WIFI_PROBEREQ) ||
				(type == WIFI_DATA_NULL) ||
				(type == WIFI_QOS_DATA_NULL))
			{
				rtw_list_delete(&pxmitbuf->list);
				break;
			}
			pxmitbuf = NULL;
		} while (1);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

sint check_pending_xmitbuf(
	struct xmit_priv *pxmitpriv)
{
	_queue *pqueue;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	if(_rtw_queue_empty(pqueue) == _FALSE)
		return _TRUE;
	else
		return _FALSE;
}

thread_return rtw_xmit_thread(thread_context context)
{
	s32 err;
	PADAPTER padapter;


	err = _SUCCESS;
	padapter = (PADAPTER)context;

	thread_enter("RTW_XMIT_THREAD");

	do {
		err = rtw_hal_xmit_thread_handler(padapter);
		flush_signals_thread();
	} while (_SUCCESS == err);

	_rtw_up_sema(&padapter->xmitpriv.terminate_xmitthread_sema);

	thread_exit();
}
#endif


