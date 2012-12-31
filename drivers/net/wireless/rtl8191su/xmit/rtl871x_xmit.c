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
#define _RTL871X_XMIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl871x_byteorder.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <circ_buf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifdef PLATFORM_WINDOWS
#include <if_ether.h>
#include <ip.h>
#endif


#ifdef  PLATFORM_LINUX
#include <linux/rtnetlink.h>
#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#endif
#endif


#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif


static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };


void _init_txservq(struct tx_servq *ptxservq)
{
_func_enter_;
	_init_listhead(&ptxservq->tx_pending);
	_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;
_func_exit_;		
}


void	_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{	
	
_func_enter_;

	_memset((unsigned char *)psta_xmitpriv, 0, sizeof (struct sta_xmit_priv));

	_spinlock_init(&psta_xmitpriv->lock);
	
	//for(i = 0 ; i < MAX_NUMBLKS; i++)
	//	_init_txservq(&(psta_xmitpriv->blk_q[i]));

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	_init_listhead(&psta_xmitpriv->legacy_dz);
	_init_listhead(&psta_xmitpriv->apsd);
	
_func_exit_;	

}

sint	_init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter)
{

	sint i;
	struct xmit_buf* pxmitbuf;
	struct xmit_frame*	pxframe;
	sint	res=_SUCCESS;   

_func_enter_;   	

	_memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv));
	
	_spinlock_init(&pxmitpriv->lock);
	_init_sema(&pxmitpriv->xmit_sema, 0);
	_init_sema(&pxmitpriv->terminate_xmitthread_sema, 0);

	/* 
	Please insert all the queue initializaiton using _init_queue below
	*/

	pxmitpriv->adapter = padapter;
	
	//for(i = 0 ; i < MAX_NUMBLKS; i++)
	//	_init_queue(&pxmitpriv->blk_strms[i]);
	
	_init_queue(&pxmitpriv->be_pending);
	_init_queue(&pxmitpriv->bk_pending);
	_init_queue(&pxmitpriv->vi_pending);
	_init_queue(&pxmitpriv->vo_pending);
	_init_queue(&pxmitpriv->bm_pending);

	_init_queue(&pxmitpriv->legacy_dz_queue);
	_init_queue(&pxmitpriv->apsd_queue);

	_init_queue(&pxmitpriv->free_xmit_queue);


	/*	
	Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME, 
	and initialize free_xmit_frame below.
	Please also apply  free_txobj to link_up all the xmit_frames...
	*/

	pxmitpriv->pallocated_frame_buf = _vmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);
	
	if (pxmitpriv->pallocated_frame_buf  == NULL){
		pxmitpriv->pxmit_frame_buf =NULL;
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("alloc xmit_frame fail!\n"));	
		res= _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = pxmitpriv->pallocated_frame_buf + 4 -
							((uint) (pxmitpriv->pallocated_frame_buf) &3);

	pxframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;


	for (i = 0; i < NR_XMITFRAME; i++)
	{
		_init_listhead(&(pxframe->list));

		pxframe->padapter = padapter;
		pxframe->frame_tag = DATA_FRAMETAG;

		pxframe->pkt = NULL;		

                pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;
 
		list_insert_tail(&(pxframe->list), &(pxmitpriv->free_xmit_queue.queue));

		pxframe++;
	}

	pxmitpriv->free_xmitframe_cnt = NR_XMITFRAME;

	/*
		init xmit hw_txqueue
	*/	
	_init_hw_txqueue(&pxmitpriv->be_txqueue, BE_QUEUE_INX);
	_init_hw_txqueue(&pxmitpriv->bk_txqueue, BK_QUEUE_INX);
	_init_hw_txqueue(&pxmitpriv->vi_txqueue, VI_QUEUE_INX);
	_init_hw_txqueue(&pxmitpriv->vo_txqueue, VO_QUEUE_INX);
	_init_hw_txqueue(&pxmitpriv->bmc_txqueue, BMC_QUEUE_INX);

	//init_xmit_priv(pxmitpriv, padapter);

	pxmitpriv->frag_len = MAX_FRAG_THRESHOLD;

#ifdef CONFIG_USB_HCI

	pxmitpriv->txirp_cnt=1;

	_init_sema(&(pxmitpriv->tx_retevt), 0);

	//per AC pending irp
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;
	
#endif	

	//init xmit_buf
	_init_queue(&pxmitpriv->free_xmitbuf_queue);
	_init_queue(&pxmitpriv->pending_xmitbuf_queue);

	pxmitpriv->pallocated_xmitbuf = _malloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4);	
	if (pxmitpriv->pallocated_xmitbuf  == NULL){
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("alloc xmit_buf fail!\n"));
		res= _FAIL;
		goto exit;
	}

	pxmitpriv->pxmitbuf = pxmitpriv->pallocated_xmitbuf + 4 -
							((uint) (pxmitpriv->pallocated_xmitbuf) &3);

	pxmitbuf = (struct xmit_buf*)pxmitpriv->pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++)
	{
		_init_listhead(&pxmitbuf->list);

		//pxmitbuf->priv_data = NULL;

		pxmitbuf->pallocated_buf = _malloc(MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ);
		if (pxmitbuf->pallocated_buf == NULL)
		{
			res = _FAIL;
			goto exit;
		}

		pxmitbuf->pbuf = pxmitbuf->pallocated_buf + XMITBUF_ALIGN_SZ -((uint) (pxmitbuf->pallocated_buf) &(XMITBUF_ALIGN_SZ-1));

		os_xmit_resource_alloc(padapter, pxmitbuf);

		list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmitbuf_queue.queue));

		pxmitbuf++;
	}

	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	_init_workitem(&padapter->wkFilterRxFF0, SetFilter, padapter );

#ifndef CONFIG_SDIO_HCI

	alloc_hwxmits(padapter);
	init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

#endif

#if defined (CONFIG_USB_HCI) && defined(PLATFORM_LINUX)

	tasklet_init(&pxmitpriv->xmit_tasklet,
	     (void(*)(unsigned long))xmit_bh,
	     (unsigned long)padapter);

#endif

exit:

_func_exit_;

	return _SUCCESS;
}

void  mfree_xmit_priv_lock (struct xmit_priv *pxmitpriv)
{
	_spinlock_free(&pxmitpriv->lock);
	_free_sema(&pxmitpriv->xmit_sema);
	_free_sema(&pxmitpriv->terminate_xmitthread_sema);

	_spinlock_free(&pxmitpriv->be_pending.lock);
	_spinlock_free(&pxmitpriv->bk_pending.lock);
	_spinlock_free(&pxmitpriv->vi_pending.lock);
	_spinlock_free(&pxmitpriv->vo_pending.lock);
	_spinlock_free(&pxmitpriv->bm_pending.lock);

	_spinlock_free(&pxmitpriv->legacy_dz_queue.lock);
	_spinlock_free(&pxmitpriv->apsd_queue.lock);

	_spinlock_free(&pxmitpriv->free_xmit_queue.lock);
	_spinlock_free(&pxmitpriv->free_xmitbuf_queue.lock);
	_spinlock_free(&pxmitpriv->pending_xmitbuf_queue.lock);

}

void _free_xmit_priv (struct xmit_priv *pxmitpriv)
{
       int i;
      _adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame*	pxmitframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf*	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

 _func_enter_;   
 
	mfree_xmit_priv_lock (pxmitpriv);
 
 	if(pxmitpriv->pxmit_frame_buf==NULL)
		goto out;
	
	for(i=0; i<NR_XMITFRAME; i++)
	{	
		os_xmit_complete(padapter, pxmitframe);		

		//os_xmit_resource_free(padapter, pxmitframe);
			
		pxmitframe++;
	}		
	
	for(i=0; i<NR_XMITBUFF; i++)
	{
		os_xmit_resource_free(padapter, pxmitbuf);
		
		_mfree(pxmitbuf->pallocated_buf, MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ);
		
		pxmitbuf++;
	}
	
	if(pxmitpriv->pallocated_frame_buf)
		_vmfree(pxmitpriv->pallocated_frame_buf, NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

	if(pxmitpriv->pallocated_xmitbuf)
		_mfree(pxmitpriv->pallocated_xmitbuf, NR_XMITBUFF * sizeof(struct xmit_buf) + 4);

	free_hwxmits(padapter);
	
out:	

_func_exit_;		

}

sint update_attrib(_adapter *padapter, _pkt *pkt, struct pkt_attrib *pattrib)
{
	uint i;
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	struct tx_cmd txdesc;

	sint bmcast;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv= &pmlmepriv->qospriv;
	sint res = _SUCCESS;

 _func_enter_;

	_open_pktfile(pkt, &pktfile);

	i = _pktfile_read(&pktfile, (unsigned char*)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);

#ifdef CONFIG_PWRCTRL
{
	u8 bool;
	//If driver xmit ARP packet, driver can set ps mode to initial setting. It stands for getting DHCP or fix IP.
	if(pattrib->ether_type == 0x0806)
	{
		if(padapter->pwrctrlpriv.pwr_mode != padapter->registrypriv.power_mgnt){
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("update_attrib: get ARP Packet \n"));
			_cancel_timer(&(pmlmepriv->dhcp_timer), &bool);
			set_ps_mode(padapter, padapter->registrypriv.power_mgnt, padapter->registrypriv.smart_ps);
		}
	}
}
#endif

	_memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	_memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);

	pattrib->pctrl = 0;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
		_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);		

	}
#ifdef CONFIG_MP_INCLUDED
	else if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		//firstly, filter packet not belongs to mp
		if (pattrib->ether_type != 0x8712) {
			res = _FAIL;
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
				 ("IN WIFI_MP_STATE but the ether_type(0x%x) != 0x8712!!!\n",
				 pattrib->ether_type));
			goto exit;
		}

		//for mp storing the txcmd per packet,
		//according to the info of txcmd to update pattrib
		i = _pktfile_read(&pktfile, (u8*)&txdesc, TXDESC_SIZE);//get MP_TXDESC_SIZE bytes txcmd per packet

		_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);		 

		pattrib->pctrl = 1;
	}
#endif

	pattrib->pktlen = pktfile.pkt_len;	// xmitframe_coalesce() overwirte this!

	if (ETH_P_IP == pattrib->ether_type)
	{
		// The following is for DHCP and ARP packet, we use cck1M to tx these packets and let LPS awake some time 
		// to prevent DHCP protocol fail
		u8 tmp[24];
		_pktfile_read(&pktfile, &tmp[0], 24);
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

	bmcast = IS_MCAST(pattrib->ra);
	// get sta_info
	if (bmcast) {
		psta = get_bcmc_stainfo(padapter);
		pattrib->mac_id = 4;
	} else {
#ifdef CONFIG_MP_INCLUDED
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		{
			psta = get_stainfo(pstapriv, get_bssid(pmlmepriv));
			pattrib->mac_id = 5;
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
				 ("update_attrib: [MP]xmit pkt:%d\n", padapter->mppriv.tx_pktcount));
		}
		else
#endif
		{
			psta = get_stainfo(pstapriv, pattrib->ra);
			if (psta == NULL)	{ // if we cannot get psta => drrp the pkt
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("update_attrib => get sta_info fail\n"));
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("ra:%x:%x:%x:%x:%x:%x\n",
				pattrib->ra[0], pattrib->ra[1],
				pattrib->ra[2], pattrib->ra[3],
				pattrib->ra[4], pattrib->ra[5]));
				res =_FAIL;
				goto exit;
			}

			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				pattrib->mac_id = 5;
			} else {
				pattrib->mac_id = psta->mac_id;
			}
		}
	}

	if (psta) {
		pattrib->psta = psta;
	} else {
		// if we cannot get psta => drrp the pkt
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("update_attrib => get sta_info fail\n"));
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
			 ("ra:%x:%x:%x:%x:%x:%x\n",
			  pattrib->ra[0], pattrib->ra[1], pattrib->ra[2],
			  pattrib->ra[3], pattrib->ra[4], pattrib->ra[5]));
		res = _FAIL;
		goto exit;
	}

	pattrib->ack_policy = 0;
	// get ether_hdr_len
	pattrib->pkt_hdrlen = ETH_HLEN;//(pattrib->ether_type == 0x8100) ? (14 + 4 ): 14; //vlan tag

	if (pqospriv->qos_option) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) && psta->qos_option) 
			set_qos(&pktfile, pattrib);
		else
			set_qos(&pktfile, pattrib);
	} else {
		pattrib->hdrlen = WLAN_HDR_A3_LEN;
		pattrib->subtype = WIFI_DATA_TYPE;	
		pattrib->priority = 0;
	}
	//pattrib->priority = 5; //force to used VI queue, for testing

	if (psta->ieee8021x_blocked == _TRUE)
	{
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\n psta->ieee8021x_blocked == _TRUE \n"));

		pattrib->encrypt = 0;

		if((pattrib->ether_type != 0x888e) && (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE))
		{
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\npsta->ieee8021x_blocked == _TRUE pattrib->ether_type(%.4x) != 0x888\n",pattrib->ether_type));
			res = _FAIL;
			goto exit;
		}
	}
	else
	{
		/*
		if((psecuritypriv->ndisauthtype>2 && (psecuritypriv->ndisauthtype!=5)&&(psecuritypriv->ndisauthtype!=3)&&(psecuritypriv->ndisauthtype!=6) )&&(psecuritypriv->bgrpkey_handshake==_FALSE))
		{
			if(pattrib->ether_type== 0x888e){
				psecuritypriv->bgrpkey_handshake=_TRUE;

			}
			else{
				DbgPrint("\npsecuritypriv->bgrpkey_handshake==_FALSE\n");
				res =_FAIL;
				goto exit;
			}
		}
		*/

		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);
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

#ifdef CONFIG_MP_INCLUDED
	//if in MP_STATE, update pkt_attrib from mp_txcmd, and overwrite some settings above.
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
		pattrib->priority = (txdesc.txdw1 >> QSEL_SHT) & 0x1f;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
			 ("update_attrib: [MP]priority=0x%x\n", pattrib->priority));
	}
#endif

exit:

_func_exit_;

	return res;
}

sint xmitframe_addmic(_adapter *padapter, struct xmit_frame *pxmitframe){
	sint 			curfragnum,length;
	u32	datalen;
	u8	*pframe, *payload,mic[8];
	struct	mic_data		micdata;
	struct	sta_info		*stainfo;
	struct	qos_priv   *pqospriv= &(padapter->mlmepriv.qospriv);	
	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;
	struct 	security_priv	*psecuritypriv=&padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv=&padapter->xmitpriv;
	u8 priority[4]={0x0,0x0,0x0,0x0};
	sint bmcst = IS_MCAST(pattrib->ra);

	if(pattrib->psta)
	{
		stainfo = pattrib->psta;
	}
	else
	{
		stainfo=get_stainfo(&padapter->stapriv ,&pattrib->ra[0]);
	}	

	

_func_enter_;

	if(pattrib->encrypt ==_TKIP_)//if(psecuritypriv->dot11PrivacyAlgrthm==_TKIP_PRIVACY_) 
	{
		//encode mic code
		if(stainfo!= NULL){
			u8 null_key[16]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
			datalen=pattrib->pktlen-pattrib->hdrlen;

			//pframe=(u8 *)(pxmitframe->mem) + WLANHDR_OFFSET+TXDESC_OFFSET;
			pframe = pxmitframe->buf_addr + TXDESC_OFFSET;;
			
			if(bmcst)
			{
				if(_memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16)==_TRUE){
					//DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n");
					//msleep_os(10);
					return _FAIL;
				}
				//start to calculate the mic code
				secmicsetkey(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			}
			else
			{
				if(_memcmp(&stainfo->dot11tkiptxmickey.skey[0],null_key, 16)==_TRUE){
					//DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n");
					//msleep_os(10);
					return _FAIL;
				}
				//start to calculate the mic code
				secmicsetkey(&micdata, &stainfo->dot11tkiptxmickey.skey[0]);
			}
			
			if(pframe[1]&1){   //ToDS==1
				secmicappend(&micdata, &pframe[16], 6);  //DA
				if(pframe[1]&2)  //From Ds==1
					secmicappend(&micdata, &pframe[24], 6);
				else
				secmicappend(&micdata, &pframe[10], 6);		
			}	
			else{	//ToDS==0
				secmicappend(&micdata, &pframe[4], 6);   //DA
				if(pframe[1]&2)  //From Ds==1
					secmicappend(&micdata, &pframe[16], 6);
				else
					secmicappend(&micdata, &pframe[10], 6);

			}

			if(pqospriv->qos_option==1)
				priority[0]=(u8)pxmitframe->attrib.priority;

			
			secmicappend(&micdata, &priority[0], 4);
	
			payload=pframe;

			for(curfragnum=0;curfragnum<pattrib->nr_frags;curfragnum++){
				payload=(u8 *)RND4((uint)(payload));
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("===curfragnum=%d, pframe= 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x,!!!\n",
					curfragnum,*payload, *(payload+1),*(payload+2),*(payload+3),*(payload+4),*(payload+5),*(payload+6),*(payload+7)));

				payload=payload+pattrib->hdrlen+pattrib->iv_len;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("curfragnum=%d pattrib->hdrlen=%d pattrib->iv_len=%d",curfragnum,pattrib->hdrlen,pattrib->iv_len));
				if((curfragnum+1)==pattrib->nr_frags){
					length=pattrib->last_txcmdsz-pattrib->hdrlen-pattrib->iv_len-( (psecuritypriv->sw_encrypt) ? pattrib->icv_len : 0);
					secmicappend(&micdata, payload,length);
					payload=payload+length;
				}
				else{
					length=pxmitpriv->frag_len-pattrib->hdrlen-pattrib->iv_len-( (psecuritypriv->sw_encrypt) ? pattrib->icv_len : 0);
					secmicappend(&micdata, payload, length);
					payload=payload+length+pattrib->icv_len;
					RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("curfragnum=%d length=%d pattrib->icv_len=%d",curfragnum,length,pattrib->icv_len));
				}
			}
			secgetmic(&micdata,&(mic[0]));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: before add mic code!!!\n"));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: pattrib->last_txcmdsz=%d!!!\n",pattrib->last_txcmdsz));
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: mic[0]=0x%.2x ,mic[1]=0x%.2x ,mic[2]=0x%.2x ,mic[3]=0x%.2x \n\
  				mic[4]=0x%.2x ,mic[5]=0x%.2x ,mic[6]=0x%.2x ,mic[7]=0x%.2x !!!!\n",
				mic[0],mic[1],mic[2],mic[3],mic[4],mic[5],mic[6],mic[7]));
			//add mic code  and add the mic code length in last_txcmdsz

			_memcpy(payload, &(mic[0]),8);
			pattrib->last_txcmdsz+=8;
			
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("\n ========last pkt========\n"));
			payload=payload-pattrib->last_txcmdsz+8;
			for(curfragnum=0;curfragnum<pattrib->last_txcmdsz;curfragnum=curfragnum+8)
					RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,(" %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x ",
					*(payload+curfragnum), *(payload+curfragnum+1), *(payload+curfragnum+2),*(payload+curfragnum+3),
					*(payload+curfragnum+4),*(payload+curfragnum+5),*(payload+curfragnum+6),*(payload+curfragnum+7)));
		}
		else{
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmitframe_addmic: get_stainfo==NULL!!!\n"));
		}
	}
	
_func_exit_;

	return _SUCCESS;
}

sint xmitframe_swencrypt(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;
	struct 	security_priv	*psecuritypriv=&padapter->securitypriv;
	
_func_enter_;

	//if((psecuritypriv->sw_encrypt)||(pattrib->bswenc))	
	if(pattrib->bswenc)
	{
		//printk("start xmitframe_swencrypt\n");
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_alert_,("### xmitframe_swencrypt\n"));
		switch(pattrib->encrypt){
		case _WEP40_:
		case _WEP104_:
			wep_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _TKIP_:
			tkip_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _AES_:
			aes_encrypt(padapter, (u8 * )pxmitframe);
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


sint make_wlanhdr (_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;

//#ifdef CONFIG_PWRCTRL
//	struct pwrctrl_priv *pwrpriv = &(padapter->pwrctrlpriv);
//#endif

	sint res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_ctl;

_func_enter_;

	_memset(hdr, 0, WLANHDR_OFFSET);

	SetFrameSubType(fctrl, pattrib->subtype);

	if (pattrib->subtype & WIFI_DATA_TYPE)
	{
		if ((check_fwstate(pmlmepriv,  WIFI_STATION_STATE) == _TRUE)) {
			//to_ds = 1, fr_ds = 0;
			SetToDs(fctrl);
			_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
			_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
		}
		else if ((check_fwstate(pmlmepriv,  WIFI_AP_STATE) == _TRUE) ) {
			//to_ds = 0, fr_ds = 1;
			SetFrDs(fctrl);
			_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			_memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);
		}
		else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
			_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
		}
#ifdef CONFIG_MP_INCLUDED
		else if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
			_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
		}
#endif
		else {
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("fw_state:%x is not allowed to xmit frame\n", get_fwstate(pmlmepriv)));
			res = _FAIL;
			goto exit;
		}

/*#ifdef CONFIG_PWRCTRL
		if (pwrpriv->cpwm >= FW_PWR1 && !(padapter->mlmepriv.sitesurveyctrl.traffic_busy))
			SetPwrMgt(fctrl);
#else
		if ((get_fwstate(pmlmepriv)) & WIFI_SLEEP_STATE)
			SetPwrMgt(fctrl);
#endif*/

		if (pattrib->encrypt)
			SetPrivacy(fctrl);

		if (pqospriv->qos_option)
		{
			qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);

			if (pattrib->priority)
				SetPriority(qc, pattrib->priority);

			SetAckpolicy(qc, pattrib->ack_policy);
		}

		//TODO: fill HT Control Field



		//Update Seq Num will be handled by f/w
		{
			struct sta_info *psta;

			sint bmcst = IS_MCAST(pattrib->ra);

			if (pattrib->psta) {
				psta = pattrib->psta;
			} else {
				if(bmcst) {
					psta = get_bcmc_stainfo(padapter);
				} else {
					psta = get_stainfo(&padapter->stapriv, pattrib->ra);
				}
			}

			if(psta)
			{
				psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
				psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;

				pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];

				SetSeqNum(hdr, pattrib->seqnum);
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

void fillin_txdesc(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 *ptxdesc;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	sint frg_inx, frg_len;

	u8 *pbuf_start;

	sint bmcst = IS_MCAST(pattrib->ra);

	pbuf_start = pxmitframe->buf_addr;
	if (pbuf_start == NULL) return;

	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;
	while (1)
	{
		ptxdesc = pbuf_start;
		frg_inx++;
		if (bmcst || pattrib->nr_frags == frg_inx) {
			update_txdesc(pxmitframe,(uint *) ptxdesc, pattrib->last_txcmdsz);
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("[2] fillin_txdesc:\n"));
			break;
		} else {
			update_txdesc(pxmitframe,(uint *) ptxdesc,(frg_len - ((pattrib->bswenc) ? pattrib->icv_len : 0)));
			pbuf_start += _RND512(frg_len);
		}
	}
}

#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX

void xmitframe_tcp_checksum_offload(_pkt *pkt, struct pkt_attrib *pattrib)
{
	struct sk_buff *skb = (struct sk_buff *)pkt;
	pattrib->hw_tcp_csum = 0;
		
	if (skb->ip_summed == CHECKSUM_PARTIAL)
	{
		if (skb_shinfo(skb)->nr_frags == 0)
		{
			const struct iphdr *ip = ip_hdr(skb);

			if (ip->protocol == IPPROTO_TCP) 
			{
				// TCP checksum offload by HW
				printk("CHECKSUM_PARTIAL TCP\n");
				pattrib->hw_tcp_csum = 1;
				//skb_checksum_help(skb);
			} else if (ip->protocol == IPPROTO_UDP) {
				//printk("CHECKSUM_PARTIAL UDP\n");
#if 1                       
				skb_checksum_help(skb);
#else
				// Set UDP checksum = 0 to skip checksum check
				struct udphdr *udp = skb_transport_header(skb);
				udp->check = 0;
#endif
			} else {
				printk("%s-%d TCP CSUM offload Error!!\n", __FUNCTION__, __LINE__);
								WARN_ON(1); 	/* we need a WARN() */
			}
		} else { // IP fragmentation case
			printk("%s-%d nr_frags != 0, using skb_checksum_help(skb);!!\n", __FUNCTION__, __LINE__);
			skb_checksum_help(skb);
		}	
	}

}
#endif

/*

This sub-routine will perform all the following:

1. remove 802.3 header.
2. create wlan_header, based on the info in pxmitframe
3. append sta's iv/ext-iv
4. append LLC
5. move frag chunk from pframe to pxmitframe->mem
6. apply sw-encrypt, if necessary. 

*/
sint xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;

	sint frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;

	uint addr;

	u8 *pframe, *mem_start, *ptxdesc;

	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	u8 *pbuf_start;

	sint bmcst = IS_MCAST(pattrib->ra);
	sint res = _SUCCESS;

_func_enter_;

#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX
	xmitframe_tcp_checksum_offload(pkt, pattrib);
#endif

	if (pattrib->psta == NULL)
		return _FAIL;
	psta = pattrib->psta;

	if (pxmitframe->buf_addr == NULL)
		return _FAIL;
	pbuf_start = pxmitframe->buf_addr;

	ptxdesc = pbuf_start;
	mem_start = pbuf_start + TXDESC_OFFSET;

	if (make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("xmitframe_coalesce: make_wlanhdr fail; drop pkt\n"));
		res = _FAIL;
		goto exit;
	}

	_open_pktfile(pkt, &pktfile);
	_pktfile_read(&pktfile, NULL, (u32)pattrib->pkt_hdrlen);

	//pbuf_start = (u8*)(pxmitframe->mem) + WLANHDR_OFFSET;
	//mem_start = (u8*)(pxmitframe->mem) + WLANHDR_OFFSET + TXDESC_OFFSET;

#ifdef CONFIG_MP_INCLUDED
	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)/* && 
	    (check_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE) == _TRUE)*/)
	{	
		//truncate TXDESC_SIZE bytes txcmd if at mp mode for 871x
		if (pattrib->ether_type == 0x8712)
			_pktfile_read(&pktfile, ptxdesc, TXDESC_SIZE); // take care update_txdesc overwrite this
	}
#endif

	pattrib->pktlen = pktfile.pkt_len;

	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;//2346-4 = 2342

	while (1)
	{
		llc_sz = 0;

		mpdu_len = frg_len;

		pframe = mem_start;

		//_memcpy(pframe, (u8 *)(pxmitframe->mem), pattrib->hdrlen);

		SetMFrag(mem_start);

		pframe += pattrib->hdrlen;
		mpdu_len -= pattrib->hdrlen;

		//adding icv, if necessary...
		if (pattrib->iv_len)
		{
			//if (check_fwstate(pmlmepriv, WIFI_MP_STATE))
			//	psta = get_stainfo(pstapriv, get_bssid(pmlmepriv));
			//else
			//	psta = get_stainfo(pstapriv, pattrib->ra);

			if (psta != NULL)
			{
				switch(pattrib->encrypt)
				{
					case _WEP40_:
					case _WEP104_:
						WEP_IV(pattrib->iv, psta->dot11txpn, (u8)psecuritypriv->dot11PrivacyKeyIndex);	
						break;
					case _TKIP_:			
						if(bmcst)
							TKIP_IV(pattrib->iv, psta->dot11txpn, (u8)psecuritypriv->dot118021XGrpKeyid);
						else
							TKIP_IV(pattrib->iv, psta->dot11txpn, 0);
						break;			
					case _AES_:
						if(bmcst)
							AES_IV(pattrib->iv, psta->dot11txpn, (u8)psecuritypriv->dot118021XGrpKeyid);
						else
							AES_IV(pattrib->iv, psta->dot11txpn, 0);
						break;
				}
			}

			_memcpy(pframe, pattrib->iv, pattrib->iv_len);

			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("\n xmitframe_coalesce:keyid= %d pattrib->iv[3]=%.2x pframe=%.2x %.2x %.2x %.2x \n",psecuritypriv->dot11PrivacyKeyIndex,pattrib->iv[3],*pframe, *(pframe+1),*(pframe+2),*(pframe+3)));

			pframe += pattrib->iv_len;

			mpdu_len -= pattrib->iv_len;
		}

		if (frg_inx == 0) {
			llc_sz = rtl8711_put_snap(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}

		if ((pattrib->icv_len >0) && (pattrib->bswenc)) {
			mpdu_len -= pattrib->icv_len;
		}


		if (bmcst) {
			mem_sz = _pktfile_read(&pktfile, pframe, pattrib->pktlen);
		} else {
			mem_sz = _pktfile_read(&pktfile, pframe, mpdu_len);
		}

		pframe += mem_sz;

		if ((pattrib->icv_len >0 )&& (pattrib->bswenc)) {
			_memcpy(pframe, pattrib->icv, pattrib->icv_len); 
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || (endofpktfile(&pktfile) == _TRUE))
		{
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + ((pattrib->nr_frags==1)? llc_sz:0) + 
					((pattrib->bswenc) ? pattrib->icv_len : 0) + mem_sz;
			ClearMFrag(mem_start);
#ifdef CONFIG_SDIO_HCI
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("coalesce: pattrib->last_txcmdsz=%d pxmitframe->pxmitbuf->phead=0x%p  pxmitframe->pxmitbuf->ptail=0x%p pxmitframe->pxmitbuf->len=%d\n", pattrib->last_txcmdsz, pxmitframe->pxmitbuf->phead, pxmitframe->pxmitbuf->ptail, pxmitframe->pxmitbuf->len));
			pxmitframe->pxmitbuf->ptail = pxmitframe->buf_addr + _RND512(pframe-pxmitframe->buf_addr);
			pxmitframe->pxmitbuf->len += pxmitframe->pxmitbuf->ptail - pxmitframe->buf_addr;//(pframe-mem_start);
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("[2] coalesce: pattrib->last_txcmdsz=%d pxmitframe->pxmitbuf->ptail=0x%p pxmitframe->pxmitbuf->len=%d\n", pattrib->last_txcmdsz, pxmitframe->pxmitbuf->ptail, pxmitframe->pxmitbuf->len));
#endif
			break;
		} else {
#ifdef CONFIG_SDIO_HCI
			pxmitframe->pxmitbuf->ptail = pxmitframe->buf_addr + _RND512(pframe-pxmitframe->buf_addr);
			pxmitframe->pxmitbuf->len += pxmitframe->pxmitbuf->ptail - pxmitframe->buf_addr;
                        pframe=pxmitframe->pxmitbuf->ptail;
#endif
		}

		addr = (uint)(pframe);
		//pbuf_start = (unsigned char *)RND4(addr);
		//mem_start = pbuf_start + TXDESC_OFFSET;

		mem_start = (unsigned char *)RND4(addr) + TXDESC_OFFSET;
		_memcpy(mem_start, pbuf_start + TXDESC_OFFSET, pattrib->hdrlen);
	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("\nxmitframe_addmic(padapter, pxmitframe)==_FAIL\n"));
		res = _FAIL;
		goto exit;
	}

#ifdef CONFIG_SDIO_HCI
	fillin_txdesc(padapter, pxmitframe);
#endif

	xmitframe_swencrypt(padapter, pxmitframe);
	
exit:	
	
_func_exit_;	

	return res;
}

sint rtl8711_put_snap(u8 *data, u16 h_proto)
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

void update_protection(_adapter *padapter, u8 *ie, uint ie_len)
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
			perp = get_ie(ie, _ERPINFO_IE_, &erp_len, ie_len);
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

struct xmit_buf *alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	_list *plist, *phead;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

_func_enter_;

	//printk("+alloc_xmitbuf\n");

	_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);

	if(_queue_empty(pfree_xmitbuf_queue) == _TRUE) {
		pxmitbuf = NULL;
	} else {

		phead = get_list_head(pfree_xmitbuf_queue);

		plist = get_next(phead);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

		list_delete(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL)
	{
		pxmitpriv->free_xmitbuf_cnt--;

		//printk("alloc, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt);

		//pxmitbuf->priv_data = NULL;
#ifdef CONFIG_SDIO_HCI
		pxmitbuf->len = 0;
		pxmitbuf->phead = pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMITBUF_SZ;
#endif
	}

	_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);

_func_exit_;

	return pxmitbuf;
}

int free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;		
	
_func_enter_;	

	//printk("+free_xmitbuf\n");

	if(pxmitbuf==NULL)
	{		
		return _FAIL;
	}
	
	_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);
	
	list_delete(&pxmitbuf->list);	
	
	list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_xmitbuf_queue));

	pxmitpriv->free_xmitbuf_cnt++;
	//printk("FREE, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt);
		
	_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);	

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

struct xmit_frame *alloc_xmitframe(struct xmit_priv *pxmitpriv)//(_queue *pfree_xmit_queue)
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

_func_enter_;

	_enter_critical(&pfree_xmit_queue->lock, &irqL);

	if (_queue_empty(pfree_xmit_queue) == _TRUE) {
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("alloc_xmitframe:%d\n", pxmitpriv->free_xmitframe_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = get_next(phead);

		pxframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		list_delete(&(pxframe->list));
	}

	if (pxframe !=  NULL)
	{
		pxmitpriv->free_xmitframe_cnt--;

		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("alloc_xmitframe():free_xmitframe_cnt=%d\n", pxmitpriv->free_xmitframe_cnt));

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		pxframe->attrib.psta = NULL;

#ifdef CONFIG_USB_HCI
		pxframe->pkt = NULL;
#endif //#ifdef CONFIG_USB_HCI
	}

	_exit_critical(&pfree_xmit_queue->lock, &irqL);

_func_exit_;

	return pxframe;
}

struct xmit_frame *alloc_xmitframe_ex(struct xmit_priv *pxmitpriv, int tag)
{
	_irqL irqL;
	struct xmit_frame *pxframe;
	_list	*plist, *phead;
	_queue *pfree_xmit_queue;
	uint *pfree_cnt;
	
_func_enter_;	


	if(tag == DATA_FRAMETAG)
	{
		pfree_xmit_queue = &pxmitpriv->free_xmit_queue;
		pfree_cnt = &pxmitpriv->free_xmitframe_cnt;
	}	
	else if(tag == AMSDU_FRAMETAG)
	{
		pfree_xmit_queue = &pxmitpriv->free_amsdu_xmit_queue;
		pfree_cnt = &pxmitpriv->free_amsdu_xmitframe_cnt;
	}	
	else if(tag == TXAGG_FRAMETAG)
	{
		pfree_xmit_queue = &pxmitpriv->free_txagg_xmit_queue;
		pfree_cnt = &pxmitpriv->free_txagg_xmitframe_cnt;
	}	
	else 
	{
		return NULL;
	}
		
	
	_enter_critical(&pfree_xmit_queue->lock, &irqL);

	if(_queue_empty(pfree_xmit_queue) == _TRUE)
	{
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("free_xmitframe_cnt:%d\n", *pfree_cnt));
		pxframe =  NULL;
	
	}
	else
	{

		phead = get_list_head(pfree_xmit_queue);
		
		plist = get_next(phead);
		
		pxframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		list_delete(&(pxframe->list));
	}

	
	if( pxframe !=  NULL ) 	
	{	
		if(pxframe->frame_tag == tag)
		{
			
			*pfree_cnt--;
#ifdef CONFIG_USB_HCI
			pxframe->pkt = NULL;		
#endif //#ifdef CONFIG_USB_HCI	
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("alloc_xmitframe_ex():tag=%d, free_xmitframe_cnt=%d\n", tag, *pfree_cnt));
		}
	}

	_exit_critical(&pfree_xmit_queue->lock, &irqL);
	
_func_exit_;	

	return pxframe;

}

sint free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{	
	_irqL irqL;
	_queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;		
	_adapter *padapter = pxmitpriv->adapter;
	_pkt *pndis_pkt = NULL;

_func_enter_;	

	if (pxmitframe == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("======free_xmitframe():pxmitframe==NULL!!!!!!!!!!\n"));
		goto exit;
	}

	_enter_critical(&pfree_xmit_queue->lock, &irqL);

	list_delete(&pxmitframe->list);	

	if (pxmitframe->pkt){
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	list_insert_tail(&pxmitframe->list, get_list_head(pfree_xmit_queue));

	pxmitpriv->free_xmitframe_cnt++;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe():free_xmitframe_cnt=%d\n", pxmitpriv->free_xmitframe_cnt));

	_exit_critical(&pfree_xmit_queue->lock, &irqL);

#ifdef PLATFORM_LINUX
	if (netif_queue_stopped(padapter->pnetdev))
		netif_wake_queue(padapter->pnetdev);
#endif

#ifdef PLATFORM_WINDOWS
	if (pndis_pkt)
		NdisMSendComplete(padapter->hndis_adapter, pndis_pkt, NDIS_STATUS_SUCCESS);
#endif

exit:

_func_exit_;

	return _SUCCESS;
}
int free_xmitframe_ex(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{	
			
_func_enter_;	

	if(pxmitframe==NULL){
		goto exit;
	}

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe_ex()\n"));
	
	if(pxmitframe->frame_tag == DATA_FRAMETAG)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe_ex(), free_xmitframe\n"));
		free_xmitframe(pxmitpriv, pxmitframe);	  
	}
#ifdef CONFIG_DRVEXT_MODULE
	else if(pxmitframe->frame_tag == L2_FRAMETAG)
	{
		free_l2xmitframe(&padapter->drvextpriv, (struct l2_xmit_frame *)pxmitframe);
	}
#endif	
#ifdef CONFIG_MLME_EXT	
	else if(pxmitframe->frame_tag == MGNT_FRAMETAG)
	{
		_adapter *padapter = pxmitpriv->adapter;
		
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe_ex(), free_mgnt_xmitframe\n"));
		free_mgnt_xmitframe(&padapter->mlmeextpriv, (struct mgnt_frame *)pxmitframe);
	}		
#endif	
	else if(pxmitframe->frame_tag == TXAGG_FRAMETAG)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe_ex(), free_txagg_xmitframe\n"));
		//free_txagg_xmitframe(pxmitpriv, (struct agg_xmit_frame *)pxmitframe);
	}		
	else if(pxmitframe->frame_tag == AMSDU_FRAMETAG)
	{
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("free_xmitframe_ex(), free_amsdu_xmitframe\n"));
		//free_amsdu_xmitframe(pxmitpriv, (struct amsdu_xmit_frame *)pxmitframe);
	}

exit:
	
_func_exit_;	 

	return _SUCCESS;
	
} 

void free_xmitframe_queue(struct xmit_priv *pxmitpriv, _queue *pframequeue)
{

	_irqL irqL;
	_list	*plist, *phead;
	struct	xmit_frame 	*pxmitframe;
_func_enter_;	

	_enter_critical(&(pframequeue->lock), &irqL);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);
	
	while (end_of_queue_search(phead, plist) == _FALSE)
	{
			
		pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		plist = get_next(plist); 
		
		free_xmitframe(pxmitpriv,pxmitframe);
			
	}
	_exit_critical(&(pframequeue->lock), &irqL);

_func_exit_;
}

static __inline struct tx_servq *get_sta_pending
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
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("get_sta_pending : BK \n"));
				break;

			case 4:
			case 5:
				ptxservq = &(psta->sta_xmitpriv.vi_q);
				*ppstapending = &padapter->xmitpriv.vi_pending;
				(phwxmits+1)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("get_sta_pending : VI\n"));
				break;

			case 6:
			case 7:
				ptxservq = &(psta->sta_xmitpriv.vo_q);
				*ppstapending = &padapter->xmitpriv.vo_pending;
				(phwxmits+0)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("get_sta_pending : VO \n"));			
				break;

			case 0:
			case 3:
			default:
				ptxservq = &(psta->sta_xmitpriv.be_q);
				*ppstapending = &padapter->xmitpriv.be_pending;
				(phwxmits+2)->accnt++;
				RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("get_sta_pending : BE \n"));				
			break;
			
		}

	}

_func_exit_;

	return ptxservq;	
		
}


/*
Will enqueue pxmitframe to the proper queue, and indicate it to xx_pending list.....
*/
sint xmit_classifier(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_irqL irqL0;
	_queue *pstapending;
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	sint bmcst = IS_MCAST(pattrib->ra);
	sint res = _SUCCESS;

_func_enter_;

	if (pattrib->psta) {
		psta = pattrib->psta;		
	} else {
		if (bmcst) {
			psta = get_bcmc_stainfo(padapter);
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("xmit_classifier: get_bcmc_stainfo\n"));
		} else {
#ifdef CONFIG_MP_INCLUDED
			if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
				psta = get_stainfo(pstapriv, get_bssid(pmlmepriv));
			else
#endif
				psta = get_stainfo(pstapriv, pattrib->ra);
		}
	}

	if (psta == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("xmit_classifier: psta == NULL\n"));
		goto exit;
	}

	ptxservq = get_sta_pending(padapter, &pstapending, psta, pattrib->priority);

	_enter_critical(&pstapending->lock, &irqL0);

	if (is_list_empty(&ptxservq->tx_pending)) {
		list_insert_tail(&ptxservq->tx_pending, get_list_head(pstapending));
	}

	//_enter_critical(&ptxservq->sta_pending.lock, &irqL1);

	list_insert_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;

	//_exit_critical(&ptxservq->sta_pending.lock, &irqL1);

	_exit_critical(&pstapending->lock, &irqL0);

exit:

_func_exit_;

	return res;
}

void alloc_hwxmits(_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	pxmitpriv->hwxmits = (struct hw_xmit *)_malloc(sizeof (struct hw_xmit) * pxmitpriv->hwxmit_entry);	
	
	if(pxmitpriv->hwxmits == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc_hwxmits(): alloc hwxmits fail!!!!!!!!!!!!!!\n"));
		return;
	}
	
	hwxmits = pxmitpriv->hwxmits;

	if(pxmitpriv->hwxmit_entry == 5)
	{
		pxmitpriv->bmc_txqueue.head = 0;
		hwxmits[0] .phwtxqueue = &pxmitpriv->bmc_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->bm_pending;
	
		pxmitpriv->vo_txqueue.head = 0;
		hwxmits[1] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vo_pending;

       	pxmitpriv->vi_txqueue.head = 0;
		hwxmits[2] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->vi_pending;
	
		pxmitpriv->bk_txqueue.head = 0;
		hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;

      		pxmitpriv->be_txqueue.head = 0;
		hwxmits[4] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[4] .sta_queue = &pxmitpriv->be_pending;
		
	}	
	else if(pxmitpriv->hwxmit_entry == 4)
	{

       	pxmitpriv->vo_txqueue.head = 0;
		hwxmits[0] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->vo_pending;

       	pxmitpriv->vi_txqueue.head = 0;
		hwxmits[1] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vi_pending;

		pxmitpriv->be_txqueue.head = 0;
		hwxmits[2] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->be_pending;
	
		pxmitpriv->bk_txqueue.head = 0;
		hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
	}
	else
	{
		

	}
	

}

void free_hwxmits(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if(pxmitpriv->hwxmits)
		_mfree((u8 *)pxmitpriv->hwxmits, (sizeof (struct hw_xmit) * pxmitpriv->hwxmit_entry));
}

void init_hwxmits(struct hw_xmit *phwxmit, sint entry)
{
	sint i;
_func_enter_;	
	for(i = 0; i < entry; i++, phwxmit++)
	{
		_spinlock_init(&phwxmit->xmit_lock);
		_init_listhead(&phwxmit->pending);		
		phwxmit->txcmdcnt = 0;
		phwxmit->accnt = 0;
	}
_func_exit_;	
}

/*
tx_action == 0 == no frames to transmit
tx_action > 0 ==> we have frames to transmit
tx_action < 0 ==> we have frames to transmit, but TXFF is not even enough to transmit 1 frame.
*/
static int dequeue_xmitframes(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, sint entry)
{
	sint tx_action;

	return tx_action;
	
}
static void dump_xmitframes(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, sint entry)
{
		
}

static void free_xmitframes(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, sint entry)
{

#ifndef CONFIG_USB_HCI	

	sint i;

	_list	*plist, *phead;
	

	struct xmit_frame	*pxmitframe;
	struct	hw_txqueue	*phwtxqueue;

_func_enter_;
	for (i = 0; i < entry; i++, phwxmit++)
	{

		phwtxqueue = phwxmit->phwtxqueue;	

		phead = &phwxmit->pending;

		plist = get_next(phead);

		while (end_of_queue_search(phead, plist) == _FALSE)
		{
			
			pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);
			
			plist = get_next(plist);
			
			free_xmitframe(pxmitpriv,pxmitframe);

		}
	
	}
	
_func_exit_;	

#endif

}

void xmitframe_xmitbuf_attach(struct xmit_frame *pxmitframe, struct xmit_buf *pxmitbuf)
{
	// pxmitbuf attach to pxmitframe
	pxmitframe->pxmitbuf = pxmitbuf;

	// urb and irp connection
#ifdef CONFIG_USB_HCI

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX)
	pxmitframe->pxmit_urb[0] = pxmitbuf->pxmit_urb[0];
#endif


#ifdef PLATFORM_OS_XP
	pxmitframe->pxmit_irp[0] = pxmitbuf->pxmit_irp[0];
#endif

#endif

	// buffer addr assoc
	pxmitframe->buf_addr = pxmitbuf->pbuf;

	// pxmitframe attach to pxmitbuf
	pxmitbuf->priv_data = pxmitframe;
}

#ifdef CONFIG_USB_HCI
int pre_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_irqL irqL;
	int ret;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;


	do_queue_select(padapter, pattrib);
	

	_enter_critical(&pxmitpriv->lock, &irqL);
	
	if(txframes_sta_ac_pending(padapter, pattrib) > 0)//enqueue packet	
	{
		ret = _FALSE;
		
		//printk("pre_xmit(1)\n");
		
		xmit_enqueue(padapter, pxmitframe);	

		_exit_critical(&pxmitpriv->lock, &irqL);
		
		return ret;
	}
	

	pxmitbuf = alloc_xmitbuf(pxmitpriv);	
	
	if(pxmitbuf == NULL)//enqueue packet
	{
		ret = _FALSE;
		//printk("pre_xmit(2)\n");

		xmit_enqueue(padapter, pxmitframe);	

		_exit_critical(&pxmitpriv->lock, &irqL);
	}
	else //dump packet directly
	{
		_exit_critical(&pxmitpriv->lock, &irqL);

		ret = _TRUE;

		xmitframe_xmitbuf_attach(pxmitframe,pxmitbuf);

		xmit_direct(padapter, pxmitframe); 
	}

	return ret;
}

#else   //SDIO
int pre_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	int bq = 0;
	int ret = _TRUE;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (xmit_enqueue(padapter, pxmitframe) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("~~~~~~~~~~~~~~~pre_xmit():xmit_enqueue  fail!!!!!!!!!!!!!!\n"));
		ret = _TRUE;
	} else
		ret = _FALSE;

	return ret;
}
#endif

void check_xmit(_adapter *padapter)
{

	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);

#ifdef CONFIG_SDIO_HCI
	struct xmit_frame *pcur_xmitframe=NULL;
	struct hw_xmit *phwxmits;
	sint hwentry;


	phwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	
	if(txframes_pending(padapter)){
	
		while(pxmitpriv->init_pgsz >( pxmitpriv->public_pgsz +15))//while(pxmitpriv->public_pgsz >10)
			{
			pcur_xmitframe =  dequeue_xframe(pxmitpriv, phwxmits, hwentry);
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("<<<<<<<pre_xmit():xmit_dequeue  (xmit_frame=0x%p)\n",pcur_xmitframe));
			if(pcur_xmitframe ==NULL){
				break;
			}
			if(check_xmit_resource(padapter, pcur_xmitframe) <= 0){
				xmitframe_coalesce(padapter, pcur_xmitframe->pkt, pcur_xmitframe);
				
				dump_xframe(padapter, pcur_xmitframe);		
				//res = xmit_direct(padapter, pcur_xmitframe);	
							
			}
			else{
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("~~~~~~~check_xmit~~~~~~~~pre_xmit():xmit_dequeue  error!!!!!!!!!!!!!!\n"));

			}
		}
		
		
	}

#endif //CONFIG_SDIO_HCI
	return ;

}

