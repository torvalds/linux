/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
#define _RTL8822BE_XMIT_C_

#include <drv_types.h>		/* PADAPTER, rtw_xmit.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../halmac/halmac_api.h"
#include "../rtl8822b.h"
#include "rtl8822be.h"

/* Debug Buffer Descriptor Ring */
/*#define BUF_DESC_DEBUG*/
#ifdef BUF_DESC_DEBUG
#define buf_desc_debug(...) do {\
		RTW_INFO("BUF_DESC:" __VA_ARGS__);\
	} while (0)
#else
#define buf_desc_debug(...)  do {} while (0)
#endif

static void rtl8822be_xmit_tasklet(void *priv)
{
	_irqL irqL;
	_adapter *padapter = (_adapter *)priv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	/* try to deal with the pending packets */
	rtl8822be_xmitframe_resume(padapter);

}

s32 rtl8822be_init_xmit_priv(_adapter *padapter)
{
	s32 ret = _SUCCESS;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	_rtw_spinlock_init(&pdvobjpriv->irq_th_lock);

#ifdef PLATFORM_LINUX
	tasklet_init(&pxmitpriv->xmit_tasklet,
		     (void(*)(unsigned long))rtl8822be_xmit_tasklet,
		     (unsigned long)padapter);
#endif

	return ret;
}

void rtl8822be_free_xmit_priv(_adapter *padapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	_rtw_spinlock_free(&pdvobjpriv->irq_th_lock);
}

static s32 rtl8822be_enqueue_xmitbuf(struct rtw_tx_ring	*ring,
				     struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *ppending_queue = &ring->queue;

	_func_enter_;

	if (pxmitbuf == NULL)
		return _FAIL;

	rtw_list_delete(&pxmitbuf->list);
	rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(ppending_queue));
	ring->qlen++;

	_func_exit_;

	return _SUCCESS;
}

struct xmit_buf *rtl8822be_dequeue_xmitbuf(struct rtw_tx_ring	*ring)
{
	_irqL irqL;
	_list *plist, *phead;
	struct xmit_buf *pxmitbuf =  NULL;
	_queue *ppending_queue = &ring->queue;

	_func_enter_;

	if (_rtw_queue_empty(ppending_queue) == _TRUE)
		pxmitbuf = NULL;
	else {

		phead = get_list_head(ppending_queue);
		plist = get_next(phead);
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_list_delete(&(pxmitbuf->list));
		ring->qlen--;
	}

	_func_exit_;

	return pxmitbuf;
}

static u8 *get_txbd(_adapter *padapter, u8 q_idx)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct rtw_tx_ring *ring;
	u8 *ptxbd = NULL;
	int idx = 0;

	ring = &pxmitpriv->tx_ring[q_idx];

	/* DO NOT use last entry. */
	/* (len -1) to avoid wrap around overlap problem in cycler queue. */
	if (ring->qlen == (ring->entries - 1)) {
		RTW_INFO("No more TX desc@%d, ring->idx = %d,idx = %d\n",
			 q_idx, ring->idx, idx);
		return NULL;
	}

	if (q_idx == BCN_QUEUE_INX)
		idx = 0;
	else
		idx = (ring->idx + ring->qlen) % ring->entries;

	ptxbd = (u8 *)&ring->buf_desc[idx];

	return ptxbd;
}

/*
 * Get txbd reg addr according to q_sel
 */
static u16 get_txbd_rw_reg(u16 q_idx)
{
	u16 txbd_reg_addr = REG_BEQ_TXBD_IDX;

	switch (q_idx) {

	case BK_QUEUE_INX:
		txbd_reg_addr = REG_BKQ_TXBD_IDX;
		break;

	case BE_QUEUE_INX:
		txbd_reg_addr = REG_BEQ_TXBD_IDX;
		break;

	case VI_QUEUE_INX:
		txbd_reg_addr = REG_VIQ_TXBD_IDX;
		break;

	case VO_QUEUE_INX:
		txbd_reg_addr = REG_VOQ_TXBD_IDX;
		break;

	case BCN_QUEUE_INX:
		txbd_reg_addr = REG_BEQ_TXBD_IDX;	/* need check */
		break;

	case TXCMD_QUEUE_INX:
		txbd_reg_addr = REG_H2CQ_TXBD_IDX;
		break;

	case MGT_QUEUE_INX:
		txbd_reg_addr = REG_MGQ_TXBD_IDX;
		break;

	case HIGH_QUEUE_INX:
		txbd_reg_addr = REG_HI0Q_TXBD_IDX;   /* need check */
		break;

	default:
		break;
	}

	return txbd_reg_addr;
}

struct xmit_frame *__rtw_alloc_cmdxmitframe_8822be(struct xmit_priv *pxmitpriv,
		enum cmdbuf_type buf_type)
{
	_adapter *padapter;
	u16	queue_idx = BCN_QUEUE_INX;
	u8 *ptxdesc = NULL;

	padapter = GET_PRIMARY_ADAPTER(pxmitpriv->adapter);
	ptxdesc = get_txbd(padapter, BCN_QUEUE_INX);

	/* set OWN bit in Beacon tx descriptor */
	if (ptxdesc != NULL)
		SET_TX_BD_OWN(ptxdesc, 0);
	else
		return NULL;

	return __rtw_alloc_cmdxmitframe(pxmitpriv, CMDBUF_BEACON);
}

/*
 * Update Read/Write pointer
 *	Read pointer is h/w descriptor index
 *	Write pointer is host desciptor index:
 *	For tx side, if own bit is set in packet index n,
 *	host pointer (write pointer) point to index n + 1.)
 */
void fill_txbd_own(_adapter *padapter, u8 *txbd, u16 queue_idx,
	struct rtw_tx_ring *ptxring)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct rtw_tx_ring *ring;
	u16 host_wp = 0;

	if (queue_idx == BCN_QUEUE_INX) {

		SET_TX_BD_OWN(txbd, 1);

		/* kick start */
		rtw_write8(padapter, REG_RX_RXBD_NUM + 1,
			rtw_read8(padapter, REG_RX_RXBD_NUM + 1) | BIT(4));

		return;
	}

	/*
	 * update h/w index
	 * for tx side, if own bit is set in packet index n,
	 * host pointer (write pointer) point to index n + 1.
	 */

        /* for current tx packet, enqueue has been ring->qlen++ before.
         * so, host_wp = ring->idx + ring->qlen.
         */
        host_wp = (ptxring->idx + ptxring->qlen) % ptxring->entries;
        rtw_write16(padapter, get_txbd_rw_reg(queue_idx), host_wp);
}

static u16 ffaddr2dma(u32 addr)
{
	u16	dma_ctrl;

	switch (addr) {
	case VO_QUEUE_INX:
		dma_ctrl = BIT3;
		break;
	case VI_QUEUE_INX:
		dma_ctrl = BIT2;
		break;
	case BE_QUEUE_INX:
		dma_ctrl = BIT1;
		break;
	case BK_QUEUE_INX:
		dma_ctrl = BIT0;
		break;
	case BCN_QUEUE_INX:
		dma_ctrl = BIT4;
		break;
	case MGT_QUEUE_INX:
		dma_ctrl = BIT6;
		break;
	case HIGH_QUEUE_INX:
		dma_ctrl = BIT7;
		break;
	default:
		dma_ctrl = 0;
		break;
	}

	return dma_ctrl;
}

/*
 * Fill tx buffer desciptor. Map each buffer address in tx buffer descriptor
 * segment. Designed for tx buffer descriptor architecture
 * Input *pmem: pointer to the Tx Buffer Descriptor
 */
static void rtl8822be_update_txbd(struct xmit_frame *pxmitframe,
				  u8 *txbd, s32 sz)
{
	_adapter *padapter = pxmitframe->padapter;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	dma_addr_t mapping;
	u32 i = 0;
	u16 seg_num =
		((TX_BUFFER_SEG_NUM == 0) ? 2 : ((TX_BUFFER_SEG_NUM == 1) ? 4 : 8));
	u16 tx_page_size_reg = 1;
	u16 page_size_length = 0;

	/* map TX DESC buf_addr (including TX DESC + tx data) */
	mapping = pci_map_single(pdvobjpriv->ppcidev, pxmitframe->buf_addr ,
				 sz + TX_WIFI_INFO_SIZE, PCI_DMA_TODEVICE);

	/* Calculate page size.
	 * Total buffer length including TX_WIFI_INFO and PacketLen */
	if (tx_page_size_reg > 0) {
		page_size_length = (sz + TX_WIFI_INFO_SIZE) /
				   (tx_page_size_reg * 128);
		if (((sz + TX_WIFI_INFO_SIZE) % (tx_page_size_reg * 128)) > 0)
			page_size_length++;
	}

	/*
	 * Reset all tx buffer desciprtor content
	 * -- Reset first element
	 */
	SET_TX_BD_TX_BUFF_SIZE0(txbd, 0);
	SET_TX_BD_PSB(txbd, 0);
	SET_TX_BD_OWN(txbd, 0);

	/* -- Reset second and other element */
	for (i = 1 ; i < seg_num ; i++) {
		SET_TXBUFFER_DESC_LEN_WITH_OFFSET(txbd, i, 0);
		SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(txbd, i, 0);
		SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(txbd, i, 0);
	}

	/*
	 * Fill buffer length of the first buffer,
	 * For 8822be, it is required that TX_WIFI_INFO is put in first segment,
	 * and the size of the first segment cannot be larger than
	 * TX_WIFI_INFO_SIZE.
	 */
	SET_TX_BD_TX_BUFF_SIZE0(txbd, TX_WIFI_INFO_SIZE);
	SET_TX_BD_PSB(txbd, page_size_length);
	/* starting addr of TXDESC */
	SET_TX_BD_PHYSICAL_ADDR0_LOW(txbd, mapping);

	/*
	 * It is assumed that in linux implementation, packet is coalesced
	 * in only one buffer. Extension mode is not supported here
	 */
	SET_TXBUFFER_DESC_LEN_WITH_OFFSET(txbd, 1, sz);
	/* don't using extendsion mode. */
	SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(txbd, 1, 0);
	SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(txbd, 1,
				      mapping + TX_WIFI_INFO_SIZE); /* pkt */

	/*buf_desc_debug("TX:%s, txbd = 0x%p\n", __FUNCTION__, txbd);*/
	buf_desc_debug("%s, txbd = 0x%08x\n", __func__, txbd);
	buf_desc_debug("TXBD:, 00h(0x%08x)\n", *((u32 *)(txbd)));
	buf_desc_debug("TXBD:, 04h(0x%08x)\n", *((u32 *)(txbd + 4)));
	buf_desc_debug("TXBD:, 08h(0x%08x)\n", *((u32 *)(txbd + 8)));
	buf_desc_debug("TXBD:, 12h(0x%08x)\n", *((u32 *)(txbd + 12)));

}

static s32 update_txdesc(struct xmit_frame *pxmitframe, s32 sz)
{
	uint qsel;
	u8 data_rate, pwr_status;
	_adapter *padapter = pxmitframe->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *ptxdesc;
	sint bmcst = IS_MCAST(pattrib->ra);
	u16 SWDefineContent = 0x0;
	u8 DriverFixedRate = 0x0;

	ptxdesc = pxmitframe->buf_addr;
	_rtw_memset(ptxdesc, 0, TXDESC_SIZE);

	/* offset 0 */
	/*SET_TX_DESC_FIRST_SEG_8812(ptxdesc, 1);*/
	SET_TX_DESC_LS_8822B(ptxdesc, 1);
	/*SET_TX_DESC_OWN_8812(ptxdesc, 1);*/

	SET_TX_DESC_TXPKTSIZE_8822B(ptxdesc, sz);
	SET_TX_DESC_OFFSET_8822B(ptxdesc, TXDESC_SIZE);

#ifdef CONFIG_TX_EARLY_MODE
	SET_TX_DESC_PKT_OFFSET_8812(ptxdesc, 1);
	SET_TX_DESC_OFFSET_8822B(ptxdesc, TXDESC_SIZE + EARLY_MODE_INFO_SIZE);
#endif

	if (bmcst)
		SET_TX_DESC_BMC_8822B(ptxdesc, 1);

	SET_TX_DESC_MACID_8822B(ptxdesc, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_8822B(ptxdesc, pattrib->raid);

	SET_TX_DESC_QSEL_8822B(ptxdesc,  pattrib->qsel);

	if (!pattrib->qos_en) {
		/* Hw set sequence number */
		SET_TX_DESC_EN_HWSEQ_8822B(ptxdesc, 1);
		SET_TX_DESC_EN_HWEXSEQ_8822B(ptxdesc, 0);
		SET_TX_DESC_DISQSELSEQ_8822B(ptxdesc, 1);
		SET_TX_DESC_HW_SSN_SEL_8822B(ptxdesc, 0);
	} else
		SET_TX_DESC_SW_SEQ_8822B(ptxdesc, pattrib->seqnum);

	if ((pxmitframe->frame_tag & 0x0f) == DATA_FRAMETAG) {
		rtl8822b_fill_txdesc_sectype(pattrib, ptxdesc);
		rtl8822b_fill_txdesc_vcs(padapter, pattrib, ptxdesc);

#ifdef CONFIG_CONCURRENT_MODE
		if (bmcst)
			rtl8822b_fill_txdesc_force_bmc_camid(pattrib, ptxdesc);
#endif

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88b4) &&
		    (pattrib->dhcp_pkt != 1)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
		   ) {
			/* Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en == _TRUE) {
				/* 8822b does NOT support AGG broadcast pkt */
				if (!bmcst)
					SET_TX_DESC_AGG_EN_8822B(ptxdesc, 1);

				SET_TX_DESC_MAX_AGG_NUM_8822B(ptxdesc, 0x1f);
				/* Set A-MPDU aggregation. */
				SET_TX_DESC_AMPDU_DENSITY_8822B(ptxdesc,
							pattrib->ampdu_spacing);
			} else
				SET_TX_DESC_BK_8822B(ptxdesc, 1);

			rtl8822b_fill_txdesc_phy(padapter, pattrib, ptxdesc);

			/* DATA  Rate FB LMT */
			/* compatibility for MCC consideration, use pmlmeext->cur_channel */
			if (pmlmeext->cur_channel > 14)
				/* for 5G. OFMD 6M */
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(
					ptxdesc, 4);
			else
				/* for 2.4G. CCK 1M */
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(
					ptxdesc, 0);

			if (pHalData->fw_ractrl == _FALSE) {
				SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);
				DriverFixedRate = 0x01;

				if (pHalData->INIDATA_RATE[pattrib->mac_id] &
				    BIT(7))
					SET_TX_DESC_DATA_SHORT_8822B(
						ptxdesc, 1);

				SET_TX_DESC_DATARATE_8822B(ptxdesc,
					pHalData->INIDATA_RATE[pattrib->mac_id]
							   & 0x7F);
			}

			if (padapter->fix_rate != 0xFF) {
				/* modify data rate by iwpriv */
				SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);

				DriverFixedRate = 0x01;
				if (padapter->fix_rate & BIT(7))
					SET_TX_DESC_DATA_SHORT_8822B(
						ptxdesc, 1);

				SET_TX_DESC_DATARATE_8822B(ptxdesc,
						   (padapter->fix_rate & 0x7F));
				if (!padapter->data_fb)
					SET_TX_DESC_DISDATAFB_8822B(ptxdesc, 1);
			}

			if (pattrib->ldpc)
				SET_TX_DESC_DATA_LDPC_8822B(ptxdesc, 1);
			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8822B(ptxdesc, 1);
		} else {
			/*
			 * EAP data packet and ARP packet and DHCP.
			 * Use the 1M data rate to send the EAP/ARP packet.
			 * This will maybe make the handshake smooth.
			 */

			SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);
			DriverFixedRate = 0x01;
			SET_TX_DESC_BK_8822B(ptxdesc, 1);

			/* HW will ignore this setting if the transmission rate
			 * is legacy OFDM. */
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				SET_TX_DESC_DATA_SHORT_8822B(ptxdesc, 1);

			SET_TX_DESC_DATARATE_8822B(ptxdesc,
					   MRateToHwRate(pmlmeext->tx_rate));
		}

#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
			SET_TX_DESC_SPE_RPT_8812(ptxdesc, 1);
#ifdef DBG_CCX
			RTW_INFO("%s set tx report\n", __func__);
#endif
		}
#endif /* CONFIG_XMIT_ACK */
#endif
	} else if ((pxmitframe->frame_tag & 0x0f) == MGNT_FRAMETAG) {
		SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);
		DriverFixedRate = 0x01;

#ifdef CONFIG_INTEL_PROXIM
		if ((padapter->proximity.proxim_on == _TRUE) &&
		    (pattrib->intel_proxim == _TRUE)) {
			RTW_INFO("\n %s pattrib->rate=%d\n",
				 __func__, pattrib->rate);
			SET_TX_DESC_DATARATE_8822B(ptxdesc, pattrib->rate);
		} else
#endif
			SET_TX_DESC_DATARATE_8822B(ptxdesc,
					   MRateToHwRate(pattrib->rate));

#if 1 /* 8822bu add */
		/* VHT NDPA or HT NDPA Packet for Beamformer. */
		if ((pattrib->subtype == WIFI_NDPA) ||
		    ((pattrib->subtype == WIFI_ACTION_NOACK) &&
		     (pattrib->order == 1))) {

			SET_TX_DESC_NAVUSEHDR_8822B(ptxdesc, 1);

			SET_TX_DESC_DATA_BW_8822B(ptxdesc,
				  rtl8822b_bw_mapping(padapter, pattrib));
			SET_TX_DESC_RTS_SC_8822B(ptxdesc,
				 rtl8822b_sc_mapping(padapter, pattrib));

			SET_TX_DESC_RTY_LMT_EN_8822B(ptxdesc, 1);
			SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(ptxdesc, 5);
			SET_TX_DESC_DISRTSFB_8822B(ptxdesc, 1);
			SET_TX_DESC_NDPA_8822B(ptxdesc, 1);
		} else {
			SET_TX_DESC_RTY_LMT_EN_8822B(ptxdesc, 1);
			if (pattrib->retry_ctrl == _TRUE)
				SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(ptxdesc, 6);
			else
				SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(ptxdesc, 12);
		}
#endif

#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
			SET_TX_DESC_SPE_RPT_8822B(ptxdesc, 1);
#ifdef DBG_CCX
			RTW_INFO("%s set tx report\n", __func__);
#endif
		}
#endif /* CONFIG_XMIT_ACK */
	} else if ((pxmitframe->frame_tag & 0x0f) == TXAGG_FRAMETAG)
		RTW_INFO("pxmitframe->frame_tag == TXAGG_FRAMETAG\n");
#ifdef CONFIG_MP_INCLUDED
	else if (((pxmitframe->frame_tag & 0x0f) == MP_FRAMETAG) &&
		 (padapter->registrypriv.mp_mode == 1))
		fill_txdesc_for_mp(padapter, ptxdesc);
#endif
	else {
		RTW_INFO("pxmitframe->frame_tag = %d\n",
			 pxmitframe->frame_tag);

		SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);
		DriverFixedRate = 0x01;
		SET_TX_DESC_DATARATE_8822B(ptxdesc,
					   MRateToHwRate(pmlmeext->tx_rate));
	}

#ifdef CONFIG_ANTENNA_DIVERSITY
	ODM_SetTxAntByTxInfo(&pHalData->odmpriv, ptxdesc,
			     pxmitframe->attrib.mac_id);
#endif

#ifdef CONFIG_BEAMFORMING
	SET_TX_DESC_G_ID_8822B(ptxdesc, pattrib->txbf_g_id);
	SET_TX_DESC_P_AID_8822B(ptxdesc, pattrib->txbf_p_aid);
#endif

	/*SET_TX_DESC_TX_BUFFER_SIZE_8812(ptxdesc, sz);*/

	if (DriverFixedRate)
		SWDefineContent |= 0x01;

	SET_TX_DESC_SW_DEFINE_8822B(ptxdesc, SWDefineContent);

	rtl8822b_cal_txdesc_chksum(padapter, ptxdesc);
	rtl8822b_dbg_dump_tx_desc(padapter, pxmitframe->frame_tag, ptxdesc);
	return 0;
}

s32 rtl8822be_dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 ret = _SUCCESS;
	s32 inner_ret = _SUCCESS;
	_irqL irqL;
	int t, sz, w_sz, pull = 0;
	u32 ff_hwaddr;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	u8 *txbd;
	struct rtw_tx_ring *ptx_ring;

	DBG_COUNTER(padapter->tx_logs.intf_tx_dump_xframe);
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("rtl8822be_dump_xframe()\n"));

	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
	    (pxmitframe->attrib.ether_type != 0x0806) &&
	    (pxmitframe->attrib.ether_type != 0x888e) &&
	    (pxmitframe->attrib.dhcp_pkt != 1))
		rtw_issue_addbareq_cmd(padapter, pxmitframe);

	for (t = 0; t < pattrib->nr_frags; t++) {

		if (inner_ret != _SUCCESS && ret == _SUCCESS)
			ret = _FAIL;

		if (t != (pattrib->nr_frags - 1)) {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("pattrib->nr_frags=%d\n", pattrib->nr_frags));

			sz = pxmitpriv->frag_len - 4;

			if (!psecuritypriv->sw_encrypt)
				sz -= pattrib->icv_len;
		} else {
			/* no frag */
			sz = pattrib->last_txcmdsz;
		}

		ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);

		_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);
		txbd = get_txbd(GET_PRIMARY_ADAPTER(padapter), ff_hwaddr);

		ptx_ring = &(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.tx_ring[ff_hwaddr]);

#ifndef CONFIG_BCN_ICF
		if (BCN_QUEUE_INX == ff_hwaddr)
			padapter->xmitpriv.beaconDMAing = _TRUE;
#endif

		if (txbd == NULL) {
			_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);
			rtw_sctx_done_err(&pxmitbuf->sctx,
					  RTW_SCTX_DONE_TX_DESC_NA);
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			RTW_INFO("##### Tx desc unavailable !#####\n");
			DBG_COUNTER(padapter->tx_logs.intf_tx_dump_xframe_err_txdesc);
			break;
		}

		if (pattrib->qsel != HALMAC_TXDESC_QSEL_H2C_CMD)
			update_txdesc(pxmitframe, sz);

		/* rtl8822be_update_txbd() must be called after update_txdesc().
		 * It rely on rtl8822be_update_txbd() to map it into non cache
		 * memory */

		rtl8822be_update_txbd(pxmitframe, txbd, sz);

		if (pxmitbuf->buf_tag != XMITBUF_CMD)
			rtl8822be_enqueue_xmitbuf(ptx_ring, pxmitbuf);

		pxmitbuf->len = sz + TX_WIFI_INFO_SIZE;
		w_sz = sz;

		/* Please comment here */
		wmb();
		fill_txbd_own(padapter, txbd, ff_hwaddr, ptx_ring);

		_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);

		inner_ret = rtw_write_port(padapter, ff_hwaddr, w_sz,
					   (unsigned char *)pxmitbuf);
		if (inner_ret != _SUCCESS)
			DBG_COUNTER(padapter->tx_logs.intf_tx_dump_xframe_err_port);

		rtw_count_tx_stats(padapter, pxmitframe, sz);
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("rtw_write_port, w_sz=%d\n", w_sz));
	}

	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	if (ret != _SUCCESS)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;
}

/*
 * Packet should not be dequeued if there is no available descriptor
 * return: _SUCCESS if there is available descriptor
 */
static u8 check_tx_desc_resource(_adapter *padapter, int prio)
{
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct rtw_tx_ring	*ring;

	ring = &pxmitpriv->tx_ring[prio];

	/*
	 * for now we reserve two free descriptor as a safety boundary
	 * between the tail and the head
	 */

	if ((ring->entries - ring->qlen) >= 2)
		return _TRUE;
	else
		return _FALSE;
}

static u8 check_nic_enough_desc_all(_adapter *padapter)
{
	u8 status = (check_tx_desc_resource(padapter, VI_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, VO_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, BE_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, BK_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, MGT_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, TXCMD_QUEUE_INX) &&
		     check_tx_desc_resource(padapter, HIGH_QUEUE_INX));
	return status;
}
#ifdef TX_AMSDU
static s32 xmitframe_amsdu_direct(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	s32 res = _SUCCESS;

	res = rtw_xmitframe_coalesce_amsdu(padapter, pxmitframe, NULL);

	if(res == _FAIL)
		goto free_frame;

	DBG_COUNTER(padapter->tx_logs.intf_tx_dequeue);
	res = rtl8822be_dump_xframe(padapter, pxmitframe);

	if (res == _FAIL) {
		DBG_COUNTER(padapter->tx_logs.intf_tx_dequeue_err_coalesce);
		goto free_frame;
	}

	return res;

free_frame:
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	return res;
}
#endif

static s32 xmitframe_direct(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 res = _SUCCESS;

	DBG_COUNTER(padapter->tx_logs.intf_tx_direct);
	res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS)
		rtl8822be_dump_xframe(padapter, pxmitframe);
	else
		DBG_COUNTER(padapter->tx_logs.intf_tx_direct_err_coalesce);

	return res;
}

void rtl8822be_xmitframe_resume(_adapter *padapter)
{
	struct xmit_priv        *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_buf	*pxmitbuf = NULL;
	int res = _SUCCESS, xcnt = 0;

#ifdef TX_AMSDU
	struct mlme_priv *pmlmepriv =  &padapter->mlmepriv;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	int tx_amsdu = padapter->tx_amsdu;
	int tx_amsdu_rate = padapter->tx_amsdu_rate;
	int current_tx_rate = pdvobjpriv->traffic_stat.cur_tx_tp;

	struct pkt_attrib *pattrib = NULL;

	struct xmit_frame *pxmitframe_next = NULL;
	struct xmit_buf *pxmitbuf_next = NULL;
	struct pkt_attrib *pattrib_next = NULL;
	int num_frame = 0;

	u8 amsdu_timeout = 0;
#endif

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("%s()\n", __func__));

	while (1) {
		if (RTW_CANNOT_RUN(padapter)) {
			RTW_INFO("%s => bDriverStopped or bSurpriseRemoved\n",
				 __func__);
			break;
		}

		if (!check_nic_enough_desc_all(padapter))
			break;

		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
		if (!pxmitbuf)
			break;

	#ifdef TX_AMSDU
		if(tx_amsdu == 0)
			goto dump_pkt;

		if(!check_fwstate(pmlmepriv, WIFI_STATION_STATE))
                        goto dump_pkt;

		pxmitframe = rtw_get_xframe(pxmitpriv, &num_frame);

		if(num_frame == 0 || pxmitframe == NULL || !check_amsdu(pxmitframe))
			goto dump_pkt;

		if(tx_amsdu == 1)
		{
			pxmitframe =  rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
			if (pxmitframe)
			{
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->pbuf;
				pxmitbuf->priv_data = pxmitframe;
				xmitframe_amsdu_direct(padapter, pxmitframe);
				pxmitpriv->amsdu_debug_coalesce_one++;
				continue;
			}
			else
			{
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
				break;
			}
		}
		else if(tx_amsdu == 2 && ((tx_amsdu_rate == 0) || (current_tx_rate > tx_amsdu_rate)))
		{

			if(num_frame == 1)
			{
				pattrib = &pxmitframe->attrib;
				amsdu_timeout = rtw_amsdu_get_timer_status(padapter, pattrib->priority);

				if(amsdu_timeout == RTW_AMSDU_TIMER_UNSET)
				{
					rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
					rtw_amsdu_set_timer_status(padapter, pattrib->priority,	RTW_AMSDU_TIMER_SETTING);
					rtw_amsdu_set_timer(padapter, pattrib->priority);
					pxmitpriv->amsdu_debug_set_timer++;
					break;
				}
				else if(amsdu_timeout == RTW_AMSDU_TIMER_SETTING)
				{
					rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
					break;
				}
				else if(amsdu_timeout == RTW_AMSDU_TIMER_TIMEOUT)
				{
					rtw_amsdu_set_timer_status(padapter, pattrib->priority,	RTW_AMSDU_TIMER_UNSET);
					pxmitpriv->amsdu_debug_timeout++;
					pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
					if (pxmitframe)
					{
						pxmitframe->pxmitbuf = pxmitbuf;
						pxmitframe->buf_addr = pxmitbuf->pbuf;
						pxmitbuf->priv_data = pxmitframe;
						xmitframe_amsdu_direct(padapter, pxmitframe);
						break;
					}
					else
					{
						rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
						break;
					}

				}
			}
			else/* num_frame > 1*/
			{
				pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

				if(!pxmitframe)
				{
					rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
					break;
				}

				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->pbuf;
				pxmitbuf->priv_data = pxmitframe;

				pxmitframe_next = rtw_get_xframe(pxmitpriv, &num_frame);

				if(num_frame == 0)
				{
					xmitframe_amsdu_direct(padapter, pxmitframe);
					pxmitpriv->amsdu_debug_coalesce_one++;
					break;
				}

				if(!check_amsdu(pxmitframe_next))
				{
					xmitframe_amsdu_direct(padapter, pxmitframe);
					pxmitpriv->amsdu_debug_coalesce_one++;
					continue;
				}
				else
				{
					pxmitbuf_next = rtw_alloc_xmitbuf(pxmitpriv);
					if (!pxmitbuf_next)
					{
						xmitframe_amsdu_direct(padapter, pxmitframe);
						pxmitpriv->amsdu_debug_coalesce_one++;
						continue;
					}

					pxmitframe_next = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
					if(!pxmitframe_next)
					{
						rtw_free_xmitbuf(pxmitpriv, pxmitbuf_next);
						xmitframe_amsdu_direct(padapter, pxmitframe);
						pxmitpriv->amsdu_debug_coalesce_one++;
						continue;
					}
					pxmitframe_next->pxmitbuf = pxmitbuf_next;
					pxmitframe_next->buf_addr = pxmitbuf_next->pbuf;
					pxmitbuf_next->priv_data = pxmitframe_next;

					rtw_xmitframe_coalesce_amsdu(padapter, pxmitframe_next , pxmitframe);
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
					rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

					rtl8822be_dump_xframe(padapter, pxmitframe_next);
					pxmitpriv->amsdu_debug_coalesce_two++;

					continue;
				}

			}

		}
dump_pkt:
	#endif

		pxmitframe =  rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits,
						 pxmitpriv->hwxmit_entry);

		if (pxmitframe) {
			pxmitframe->pxmitbuf = pxmitbuf;
			pxmitframe->buf_addr = pxmitbuf->pbuf;
			pxmitbuf->priv_data = pxmitframe;

			if ((pxmitframe->frame_tag & 0x0f) == DATA_FRAMETAG) {
				if (pxmitframe->attrib.priority <= 15) {
					/* TID0~15 */
					res = rtw_xmitframe_coalesce(padapter,
						pxmitframe->pkt, pxmitframe);
				}

				/* always return ndis_packet after
				 * rtw_xmitframe_coalesce */
				rtw_os_xmit_complete(padapter, pxmitframe);
			}

			DBG_COUNTER(padapter->tx_logs.intf_tx_dequeue);
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
				 ("%s(): rtl8822be_dump_xframe\n", __func__));

			if (res == _SUCCESS)
				rtl8822be_dump_xframe(padapter, pxmitframe);
			else {
				DBG_COUNTER(padapter->tx_logs.intf_tx_dequeue_err_coalesce);
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
				rtw_free_xmitframe(pxmitpriv, pxmitframe);
			}

			xcnt++;
		} else {
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			break;
		}
	}
}

static u8 check_nic_enough_desc(_adapter *padapter, struct pkt_attrib *pattrib)
{
	u32 prio;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct rtw_tx_ring	*ring;

	switch (pattrib->qsel) {
	case 0:
	case 3:
		prio = BE_QUEUE_INX;
		break;
	case 1:
	case 2:
		prio = BK_QUEUE_INX;
		break;
	case 4:
	case 5:
		prio = VI_QUEUE_INX;
		break;
	case 6:
	case 7:
		prio = VO_QUEUE_INX;
		break;
	default:
		prio = BE_QUEUE_INX;
		break;
	}

	ring = &pxmitpriv->tx_ring[prio];

	/*
	 * for now we reserve two free descriptor as a safety boundary
	 * between the tail and the head
	 */
	if ((ring->entries - ring->qlen) >= 2)
		return _TRUE;
	else
		return _FALSE;
}

/*
 * Return
 *	_TRUE	dump packet directly
 *	_FALSE	enqueue packet
 */
static s32 pre_xmitframe(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_irqL irqL;
	s32 res;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

#ifdef TX_AMSDU
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	int tx_amsdu = padapter->tx_amsdu;
	int tx_amsdu_rate = padapter->tx_amsdu_rate;
	int current_tx_rate = pdvobjpriv->traffic_stat.cur_tx_tp;

	u8 amsdu_timeout = 0;
#endif

	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	if ((rtw_txframes_sta_ac_pending(padapter, pattrib) > 0) ||
	    (check_nic_enough_desc(padapter, pattrib) == _FALSE)) {
	    DBG_COUNTER(padapter->tx_logs.intf_tx_pending_ac);
		goto enqueue;
	}

	if (rtw_xmit_ac_blocked(padapter) == _TRUE) {
		DBG_COUNTER(padapter->tx_logs.intf_tx_pending_fw_under_survey);
		goto enqueue;
	}

	if (padapter->dvobj->iface_state.lg_sta_num) {
		DBG_COUNTER(padapter->tx_logs.intf_tx_pending_fw_under_linking);
		goto enqueue;
	}

#ifdef TX_AMSDU
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE))
	{
		if(tx_amsdu == 1)
			goto enqueue;
		else if(tx_amsdu == 2 && (tx_amsdu_rate == 0 || current_tx_rate > tx_amsdu_rate))
			goto enqueue;
	}
#endif

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL) {
		DBG_COUNTER(padapter->tx_logs.intf_tx_pending_xmitbuf);
		goto enqueue;
	}

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(padapter, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}

	return _TRUE;

enqueue:
	res = rtw_xmitframe_enqueue(padapter, pxmitframe);

#ifdef TX_AMSDU
	if(res == _SUCCESS && tx_amsdu == 2)
	{
		amsdu_timeout = rtw_amsdu_get_timer_status(padapter, pattrib->priority);
		if(amsdu_timeout == RTW_AMSDU_TIMER_SETTING)
		{
			rtw_amsdu_cancel_timer(padapter, pattrib->priority);
			rtw_amsdu_set_timer_status(padapter, pattrib->priority, RTW_AMSDU_TIMER_UNSET);
		}
	}
#endif

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if (res != _SUCCESS) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_,
			 ("pre_xmitframe: enqueue xmitframe fail\n"));
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;
		return _TRUE;
	}

#ifdef TX_AMSDU
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#endif

	return _FALSE;
}

s32 rtl8822be_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	return rtl8822be_dump_xframe(padapter, pmgntframe);
}

/*
 * Return
 *	_TRUE	dump packet directly ok
 *	_FALSE	temporary can't transmit packets to hardware
 */
s32 rtl8822be_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	DBG_COUNTER(padapter->tx_logs.intf_tx);
	return pre_xmitframe(padapter, pxmitframe);
}

s32 rtl8822be_hal_xmitframe_enqueue(_adapter *padapter,
				    struct xmit_frame *pxmitframe)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	s32 err;

	DBG_COUNTER(padapter->tx_logs.intf_tx_enqueue);
	err = rtw_xmitframe_enqueue(padapter, pxmitframe);
	if (err != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		pxmitpriv->tx_drop++;
	} else {
#ifdef PLATFORM_LINUX
		if (check_nic_enough_desc(padapter,
					  &pxmitframe->attrib) == _TRUE)
			tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#endif
	}

	return err;
}

int rtl8822be_init_txbd_ring(_adapter *padapter, unsigned int q_idx,
			     unsigned int entries)
{
	struct xmit_priv *t_priv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_dev *pdev = pdvobjpriv->ppcidev;
	struct tx_buf_desc *txbd;
	u8 *tx_desc;
	dma_addr_t dma;
	int i;

	_func_enter_;

	RTW_INFO("%s entries num:%d\n", __func__, entries);

	txbd = pci_alloc_consistent(pdev, sizeof(*txbd) * entries, &dma);

	if (!txbd || (unsigned long)txbd & 0xFF) {
		RTW_INFO("Cannot allocate TXBD (q_idx = %d)\n", q_idx);
		return _FAIL;
	}

	_rtw_memset(txbd, 0, sizeof(*txbd) * entries);
	t_priv->tx_ring[q_idx].buf_desc = txbd;
	t_priv->tx_ring[q_idx].dma = dma;
	t_priv->tx_ring[q_idx].idx = 0;
	t_priv->tx_ring[q_idx].entries = entries;
	_rtw_init_queue(&t_priv->tx_ring[q_idx].queue);
	t_priv->tx_ring[q_idx].qlen = 0;

	RTW_INFO("%s queue:%d, ring_addr:%p\n", __func__, q_idx, txbd);

	_func_exit_;

	return _SUCCESS;
}

void rtl8822be_free_txbd_ring(_adapter *padapter, unsigned int prio)
{
	struct xmit_priv *t_priv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_dev *pdev = pdvobjpriv->ppcidev;
	struct rtw_tx_ring *ring = &t_priv->tx_ring[prio];
	u8 *txbd;
	struct xmit_buf	*pxmitbuf;

	_func_enter_;

	while (ring->qlen) {
		txbd = (u8 *)(&ring->buf_desc[ring->idx]);
		SET_TX_BD_OWN(txbd, 0);

		if (prio != BCN_QUEUE_INX)
			ring->idx = (ring->idx + 1) % ring->entries;

		pxmitbuf = rtl8822be_dequeue_xmitbuf(ring);

		if (pxmitbuf) {
			pci_unmap_single(pdev,
				GET_TX_BD_PHYSICAL_ADDR0_LOW(txbd),
				pxmitbuf->len, PCI_DMA_TODEVICE);

			rtw_free_xmitbuf(t_priv, pxmitbuf);

		} else {
			RTW_INFO("%s qlen=%d!=0,but have xmitbuf in pendingQ\n",
				 __func__, ring->qlen);
			break;
		}
	}

	pci_free_consistent(pdev, sizeof(*ring->buf_desc) * ring->entries,
			    ring->buf_desc, ring->dma);
	ring->buf_desc = NULL;

	_func_exit_;
}

/*
 * Draw a line to show queue status. For debug
 * i: queue index / W:HW index / h:host index / .: enpty entry / *:ready to DMA
 * Example:  R- 3- 4- 8 ..iW***h..... (i=3,W=4,h=8,
 * *** means 3 tx_desc is reaady to dma)
 */
#ifdef BUF_DESC_DEBUG
static void _draw_queue(PADAPTER Adapter, int prio)
{
	int i;
	u8 line[TXBD_NUM + 1];
	u16 hw, host;
	u32	index, tmp_4bytes = 0;

	struct xmit_priv	*t_priv = &Adapter->xmitpriv;
	struct rtw_tx_ring	*ring = &t_priv->tx_ring[prio];

	tmp_4bytes = rtw_read32(Adapter, get_txbd_rw_reg(prio));
	hw   = (u16)((tmp_4bytes >> 16) & 0x7ff);
	host = (u16)(tmp_4bytes & 0x7ff);

	index = ring->idx;
	_rtw_memset(line, '.', TXBD_NUM);

	/* ready to return to driver */
	if (index <= hw) {
		for (i = index; i < hw; i++)
			line[i] = ':';
	} else { /* wrap */
		for (i = index; i < TXBD_NUM; i++)
			line[i] = ':';
		for (i = 0; i < hw; i++)
			line[i] = ':';
	}

	/* ready to dma */
	if (hw <= host) {
		for (i = hw; i < host; i++)
			line[i] = '*';
	} else { /* wrap */
		for (i = hw; i < TXBD_NUM; i++)
			line[i] = '*';
		for (i = 0; i < host; i++)
			line[i] = '*';
	}

	line[index] = 'i'; /* software queue index */
	line[host] = 'h';  /* host index */
	line[hw] = 'W';	   /* hardware index */
	line[TXBD_NUM] = 0x0;

	/* Q2:10-20-30: */
	buf_desc_debug("Q%d:%02d-%02d-%02d %s\n", prio, index, hw, host, line);
}
#endif

/*
 * Read pointer is h/w descriptor index
 * Write pointer is host desciptor index: For tx side, if own bit is set in
 * packet index n, host pointer (write pointer) point to index n + 1.
 */
static u32 rtl8822be_check_txdesc_closed(PADAPTER Adapter, u32 queue_idx,
		struct rtw_tx_ring *ring)
{
	/*
	 * hw_rp_cache is used to reduce REG access.
	 */
	u32	tmp32;

	/* bcn queue should not enter this function */
	if (queue_idx == BCN_QUEUE_INX)
		return _TRUE;

	/* qlen == 0 --> don't need to process */
	if (ring->qlen == 0)
		return _FALSE;

	/* sw_rp == hw_rp_cache --> sync hw_rp */
	if (ring->idx == ring->hw_rp_cache) {

		tmp32 = rtw_read32(Adapter, get_txbd_rw_reg(queue_idx));

		ring->hw_rp_cache = (tmp32 >> 16) & 0x0FFF;
	}

	/* check if need to handle TXOK */
	if (ring->idx == ring->hw_rp_cache)
		return _FALSE;

	return _TRUE;
}

#ifdef CONFIG_BCN_ICF
void rtl8822be_tx_isr(PADAPTER Adapter, int prio)
{
	struct xmit_priv *t_priv = &Adapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);
	struct rtw_tx_ring *ring = &t_priv->tx_ring[prio];
	struct xmit_buf	*pxmitbuf;
	u8 *tx_desc;
	u16 tmp_4bytes;
	u16 desc_idx_hw = 0, desc_idx_host = 0;


	while (ring->qlen) {
		tx_desc = (u8 *)&ring->buf_desc[ring->idx];

		/*  beacon use cmd buf Never run into here */
		if (!rtl8822be_check_txdesc_closed(Adapter, prio, ring))
			return;

		buf_desc_debug("TX: %s, q_idx = %d, tx_bd = %04x, close [%04x] r_idx [%04x]\n",
			       __func__, prio, (u32)tx_desc, ring->idx,
			       (ring->idx + 1) % ring->entries);

		ring->idx = (ring->idx + 1) % ring->entries;
		pxmitbuf = rtl8822be_dequeue_xmitbuf(ring);

		if (pxmitbuf) {
			pci_unmap_single(pdvobjpriv->ppcidev,
				GET_TX_BD_PHYSICAL_ADDR0_LOW(tx_desc),
				pxmitbuf->len, PCI_DMA_TODEVICE);

			rtw_sctx_done(&pxmitbuf->sctx);
			rtw_free_xmitbuf(&(pxmitbuf->padapter->xmitpriv),
					 pxmitbuf);
		} else {
			RTW_INFO("%s qlen=%d!=0,but have xmitbuf in pendingQ\n",
				 __func__, ring->qlen);
		}
	}

	if (check_tx_desc_resource(Adapter, prio)
	    && rtw_xmit_ac_blocked(Adapter) != _TRUE)
		rtw_mi_xmit_tasklet_schedule(Adapter);
}

#else /* !CONFIG_BCN_ICF */
void rtl8822be_tx_isr(PADAPTER Adapter, int prio)
{
	struct xmit_priv *t_priv = &Adapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);
	struct rtw_tx_ring *ring = &t_priv->tx_ring[prio];
	struct xmit_buf	*pxmitbuf;
	u8 *tx_desc;
	u16 tmp_4bytes;
	u16 desc_idx_hw = 0, desc_idx_host = 0;


	while (ring->qlen) {
		tx_desc = (u8 *)&ring->buf_desc[ring->idx];

		/*
		 * beacon packet will only use the first descriptor defautly,
		 * check register to see whether h/w has consumed buffer
		 * descriptor
		 */
		if (prio != BCN_QUEUE_INX) {
			if (!rtl8822be_check_txdesc_closed(Adapter,
							   prio, ring->idx))
				return;

			buf_desc_debug("TX: %s, queue_idx = %d, tx_desc = %04x, close desc [%04x] and update ring->idx to [%04x]\n",
				       __func__, prio, (u32)tx_desc, ring->idx,
				       (ring->idx + 1) % ring->entries);
			ring->idx = (ring->idx + 1) % ring->entries;
		}
		#if 0 /* 8822b change 00[31] to DISQSELSEQ */
		else if (prio == BCN_QUEUE_INX)
			SET_TX_DESC_OWN_92E(tx_desc, 0);
		#endif

		pxmitbuf = rtl8822be_dequeue_xmitbuf(ring);
		if (pxmitbuf) {
			pci_unmap_single(pdvobjpriv->ppcidev,
				 GET_TX_BD_PHYSICAL_ADDR0_LOW(tx_desc),
					 pxmitbuf->len, PCI_DMA_TODEVICE);
			rtw_sctx_done(&pxmitbuf->sctx);
			rtw_free_xmitbuf(&(pxmitbuf->padapter->xmitpriv),
					 pxmitbuf);
		} else {
			RTW_INFO("%s qlen=%d!=0,but have xmitbuf in pendingQ\n",
				 __func__, ring->qlen);
		}
	}

	if ((prio != BCN_QUEUE_INX) && check_tx_desc_resource(Adapter, prio)
	    && rtw_xmit_ac_blocked(Adapter) != _TRUE)
		rtw_mi_xmit_tasklet_schedule(Adapter);
}
#endif /* CONFIG_BCN_ICF */

#ifdef CONFIG_HOSTAPD_MLME
static void rtl8812ae_hostap_mgnt_xmit_cb(struct urb *urb)
{
#ifdef PLATFORM_LINUX
	struct sk_buff *skb = (struct sk_buff *)urb->context;

	dev_kfree_skb_any(skb);
#endif
}

s32 rtl8822be_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt)
{
#ifdef PLATFORM_LINUX
	u16 fc;
	int rc, len, pipe;
	unsigned int bmcst, tid, qsel;
	struct sk_buff *skb, *pxmit_skb;
	struct urb *urb;
	unsigned char *pxmitbuf;
	struct tx_desc *ptxdesc;
	struct rtw_ieee80211_hdr *tx_hdr;
	struct hostapd_priv *phostapdpriv = padapter->phostapdpriv;
	struct net_device *pnetdev = padapter->pnetdev;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);


	skb = pkt;
	len = skb->len;
	tx_hdr = (struct rtw_ieee80211_hdr *)(skb->data);
	fc = le16_to_cpu(tx_hdr->frame_ctl);
	bmcst = IS_MCAST(tx_hdr->addr1);

	if ((fc & RTW_IEEE80211_FCTL_FTYPE) != RTW_IEEE80211_FTYPE_MGMT)
		goto _exit;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
	/* http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
	pxmit_skb = dev_alloc_skb(len + TXDESC_SIZE);
#else
	pxmit_skb = netdev_alloc_skb(pnetdev, len + TXDESC_SIZE);
#endif

	if (!pxmit_skb)
		goto _exit;

	pxmitbuf = pxmit_skb->data;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto _exit;

	/* ----- fill tx desc ----- */
	ptxdesc = (struct tx_desc *)pxmitbuf;
	_rtw_memset(ptxdesc, 0, sizeof(*ptxdesc));

	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(len & 0x0000ffff);
	/* default = 32 bytes for TX Desc */
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) &
				      0x00ff0000);
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);

	if (bmcst)
		ptxdesc->txdw0 |= cpu_to_le32(BIT(24));

	/* offset 4 */
	ptxdesc->txdw1 |= cpu_to_le32(0x00);/* MAC_ID */

	ptxdesc->txdw1 |= cpu_to_le32((0x12 << QSEL_SHT) & 0x00001f00);

	ptxdesc->txdw1 |= cpu_to_le32((0x06 << 16) & 0x000f0000);/* b mode */

	/* offset 8 */

	/* offset 12 */
	ptxdesc->txdw3 |= cpu_to_le32((le16_to_cpu(tx_hdr->seq_ctl) << 16) &
				      0xffff0000);

	/* offset 16 */
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

	/* offset 20 */

	rtl8188e_cal_txdesc_chksum(ptxdesc);
	/* ----- end of fill tx desc ----- */

	skb_put(pxmit_skb, len + TXDESC_SIZE);
	pxmitbuf = pxmitbuf + TXDESC_SIZE;
	_rtw_memcpy(pxmitbuf, skb->data, len);

	/* ----- prepare urb for submit ----- */

	/* translate DMA FIFO addr to pipehandle */
	/*pipe = ffaddr2pipehdl(pdvobj, MGT_QUEUE_INX);*/
	pipe = usb_sndbulkpipe(pdvobj->pusbdev,
			       pHalData->Queue2EPNum[(u8)MGT_QUEUE_INX] & 0x0f);

	usb_fill_bulk_urb(urb, pdvobj->pusbdev, pipe, pxmit_skb->data,
		  pxmit_skb->len, rtl8188ee_hostap_mgnt_xmit_cb, pxmit_skb);

	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &phostapdpriv->anchored);
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		usb_unanchor_urb(urb);
		kfree_skb(skb);
	}
	usb_free_urb(urb);


_exit:

	dev_kfree_skb_any(skb);
#endif
	return 0;

}
#endif
