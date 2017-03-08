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
#define _RTL8822BE_RECV_C_

#include <drv_types.h>		/* PADAPTER and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
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

/*
 * Wait until rx data is ready
 *	return value: _SUCCESS if Rx packet is ready, _FAIL if not ready
 */

static u32 rtl8822be_wait_rxrdy(_adapter *padapter,
				u8 *rx_bd, u16 rx_q_idx)
{
	struct recv_priv *r_priv = &padapter->recvpriv;
	u8 first_seg = 0, last_seg = 0;
	u16 total_len = 0, read_cnt = 0;

	static BOOLEAN start_rx = _FALSE;
	u16 status = _SUCCESS;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	if (rx_bd == NULL)
		return _FAIL;

	total_len = (u2Byte)GET_RX_BD_TOTALRXPKTSIZE(rx_bd);
	first_seg = (u1Byte)GET_RX_BD_FS(rx_bd);
	last_seg = (u1Byte)GET_RX_BD_LS(rx_bd);

	buf_desc_debug("RX:%s enter: rx_bd addr = %p, total_len=%d, first_seg=%d, last_seg=%d, read_cnt %d, index %d, address %p\n",
		       __func__,
		(u8 *)&r_priv->rx_ring[rx_q_idx].desc[r_priv->rx_ring[rx_q_idx].idx],
		       total_len, first_seg, last_seg, read_cnt,
		       r_priv->rx_ring[rx_q_idx].idx, rx_bd);

#if defined(USING_RX_TAG)
	/* Rx Tag not ported */
	if (!start_rx) {
		start_rx = _TRUE;
		pHalData->RxTag = 1;
	} else {
		while (total_len != (pHalData->RxTag + 1)) {

			read_cnt++;

			total_len = (u2Byte)GET_RX_BD_TOTALRXPKTSIZE(rx_bd);
			first_seg = (u1Byte)GET_RX_BD_FS(rx_bd);
			last_seg = (u1Byte)GET_RX_BD_LS(rx_bd);

			if (read_cnt > 10000) {
				pHalData->RxTag = total_len;
				break;
			}

			if (total_len == 0 && pHalData->RxTag == 0x1fff)
				break;
		}
		pHalData->RxTag = total_len;
	}
#else
	while (total_len == 0) {
		read_cnt++;

		total_len = (u2Byte) GET_RX_BD_TOTALRXPKTSIZE(rx_bd);
		first_seg = (u1Byte) GET_RX_BD_FS(rx_bd);
		last_seg = (u1Byte) GET_RX_BD_LS(rx_bd);

		if (read_cnt > 20) {
			status = _FAIL;
			break;
		}
	}
#endif

	buf_desc_debug("RX:%s exit total_len=%d, rx_tag = %d, first_seg=%d, last_seg=%d, read_cnt %d\n",
		       __func__, total_len, pHalData->RxTag,
		       first_seg, last_seg, read_cnt);

	return status;
}

/*
 * Check the number of rxdec to be handled between
 *   "index of RX queue descriptor maintained by host (write pointer)" and
 *   "index of RX queue descriptor maintained by hardware (read pointer)"
 */
#ifdef CONFIG_NAPI
u16 rtl8822be_check_rxdesc_remain(_adapter *padapter, int rx_queue_idx)
#else
static u16 rtl8822be_check_rxdesc_remain(_adapter *padapter, int rx_queue_idx)
#endif
{
	struct recv_priv *r_priv = &padapter->recvpriv;
	u16 desc_idx_hw = 0, desc_idx_host = 0, num_rxdesc_to_handle = 0;
	u32 tmp_4bytes = 0;
	static BOOLEAN	start_rx = FALSE;


	tmp_4bytes = rtw_read32(padapter, REG_RXQ_RXBD_IDX_8822B);
	desc_idx_hw = (u16)((tmp_4bytes >> 16) & 0x7ff);
	desc_idx_host = (u16)(tmp_4bytes & 0x7ff);

	/*
	 * make sure driver does not handle packet if hardware pointer
	 * keeps in zero in initial state
	 */
	buf_desc_debug("RX:%s(%d) reg_value %x\n", __func__, __LINE__,
		       tmp_4bytes);

	if (desc_idx_hw > 0)
		start_rx = TRUE;

	if (!start_rx)
		return 0;

	if (desc_idx_hw < desc_idx_host)
		/* hw idx is turn around */
		num_rxdesc_to_handle = RX_BD_NUM_8822BE - desc_idx_host + desc_idx_hw;
	else
		num_rxdesc_to_handle = desc_idx_hw - desc_idx_host;

	if (num_rxdesc_to_handle == 0)
		return 0;

	r_priv->rx_ring[rx_queue_idx].idx = desc_idx_host;

	buf_desc_debug("RX:%s reg_val %x, hw_idx %x, host_idx %x, desc to handle = %d\n",
		__func__, tmp_4bytes, desc_idx_hw, desc_idx_host, num_rxdesc_to_handle);

	return num_rxdesc_to_handle;
}

static void rtl8822be_query_rx_desc_status(union recv_frame *precvframe,
		u8 *pdesc)
{
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;

	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	/* Offset 0 */
	pattrib->pkt_len = (u16)GET_RX_DESC_PKT_LEN_8822B(pdesc);
	pattrib->crc_err = (u8)GET_RX_DESC_CRC32_8822B(pdesc);
	pattrib->icv_err = (u8)GET_RX_DESC_ICV_ERR_8822B(pdesc);
	pattrib->drvinfo_sz = (u8)GET_RX_DESC_DRV_INFO_SIZE_8822B(pdesc) * 8;
	pattrib->encrypt = (u8)GET_RX_DESC_SECURITY_8822B(pdesc);
	pattrib->qos = (u8)GET_RX_DESC_QOS_8822B(pdesc);
	pattrib->shift_sz = (u8)GET_RX_DESC_SHIFT_8822B(pdesc);
	pattrib->physt = (u8)GET_RX_DESC_PHYST_8822B(pdesc);
	pattrib->bdecrypted = !GET_RX_DESC_SWDEC_8822B(pdesc);

	/* Offset 1 */
	pattrib->priority = (u8)GET_RX_DESC_TID_8822B(pdesc);
	pattrib->mdata = (u8)GET_RX_DESC_MD_8822B(pdesc);
	pattrib->mfrag = (u8)GET_RX_DESC_MF_8822B(pdesc);

	/* Offset 8 */
	pattrib->seq_num = (u16)GET_RX_DESC_SEQ_8822B(pdesc);
	pattrib->frag_num = (u8)GET_RX_DESC_FRAG_8822B(pdesc);

	if (GET_RX_DESC_C2H_8822B(pdesc))
		pattrib->pkt_rpt_type = C2H_PACKET;
	else
		pattrib->pkt_rpt_type = NORMAL_RX;

	/* Offset 12 */
	pattrib->data_rate = (u8)GET_RX_DESC_RX_RATE_8822B(pdesc);

	/* Offset 16 */
	/* Offset 20 */

}

#ifdef CONFIG_NAPI
int rtl8822be_rx_mpdu(_adapter *padapter, int remaing_rxdesc, int budget)
#else
static void rtl8822be_rx_mpdu(_adapter *padapter)
#endif
{
	struct recv_priv *r_priv = &padapter->recvpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	_queue *pfree_recv_queue = &r_priv->free_recv_queue;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	union recv_frame *precvframe = NULL;
	u8 *pphy_info = NULL;
	struct rx_pkt_attrib *pattrib = NULL;
	int rx_q_idx = RX_MPDU_QUEUE;
	u32 count = r_priv->rxringcount;
	u8 *rx_bd;
	struct sk_buff *skb;
#ifdef CONFIG_NAPI
	int rx = 0;
	u32 status;
#else
	u16 remaing_rxdesc = 0;
#endif

	/* RX NORMAL PKT */

#ifdef CONFIG_NAPI
	while ((rx < budget) && remaing_rxdesc) {
#else
	remaing_rxdesc = rtl8822be_check_rxdesc_remain(padapter, rx_q_idx);
	while (remaing_rxdesc) {
#endif

		/* rx descriptor */
		rx_bd = (u8 *)&r_priv->rx_ring[rx_q_idx].buf_desc[r_priv->rx_ring[rx_q_idx].idx];

		/* rx packet */
		skb = r_priv->rx_ring[rx_q_idx].rx_buf[r_priv->rx_ring[rx_q_idx].idx];

		buf_desc_debug("RX:%s(%d), rx_bd addr = %x, total_len = %d, ring idx = %d\n",
			       __func__, __LINE__, (u32)rx_bd,
			       GET_RX_BD_TOTALRXPKTSIZE(rx_bd),
			       r_priv->rx_ring[rx_q_idx].idx);

		buf_desc_debug("RX:%s(%d), skb(rx_buf)=%x, buf addr(virtual = %x, phisycal = %x)\n",
			       __func__, __LINE__, (u32)skb,
			       (u32)(skb_tail_pointer(skb)),
			       GET_RX_BD_PHYSICAL_ADDR_LOW(rx_bd));

		/* wait until packet is ready. this operation is similar to
		 * check own bit and should be called before pci_unmap_single
		 * which release memory mapping
		 */

		if (rtl8822be_wait_rxrdy(padapter, rx_bd, rx_q_idx) !=
		    _SUCCESS)
			buf_desc_debug("RX:%s(%d) packet not ready\n",
				       __func__, __LINE__);

		{
			DBG_COUNTER(padapter->rx_logs.intf_rx);
			precvframe = rtw_alloc_recvframe(pfree_recv_queue);

			if (precvframe == NULL) {
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					("recvbuf2recvframe: precvframe==NULL\n"));
				DBG_COUNTER(padapter->rx_logs.intf_rx_err_recvframe);
				goto done;
			}

			_rtw_init_listhead(&precvframe->u.hdr.list);
			precvframe->u.hdr.len = 0;

			pci_unmap_single(pdvobjpriv->ppcidev,
					 *((dma_addr_t *)skb->cb),
					 r_priv->rxbuffersize,
					 PCI_DMA_FROMDEVICE);


			rtl8822be_query_rx_desc_status(precvframe, skb->data);
			pattrib = &precvframe->u.hdr.attrib;

#ifdef CONFIG_RX_PACKET_APPEND_FCS
			{
				struct mlme_priv *mlmepriv =
						&padapter->mlmepriv;

				if (check_fwstate(mlmepriv,
						  WIFI_MONITOR_STATE) == _FALSE)
					if (pattrib->pkt_rpt_type == NORMAL_RX)
						pattrib->pkt_len -=
							IEEE80211_FCS_LEN;
			}
#endif

			buf_desc_debug("RX:%s(%d), pkt_len = %d, pattrib->drvinfo_sz = %d, pattrib->qos = %d, pattrib->shift_sz = %d\n",
				       __func__, __LINE__, pattrib->pkt_len,
				       pattrib->drvinfo_sz, pattrib->qos,
				       pattrib->shift_sz);

			if (rtw_os_alloc_recvframe(padapter, precvframe,
				   (skb->data + HALMAC_RX_DESC_SIZE_8822B +
				    pattrib->drvinfo_sz + pattrib->shift_sz),
						   skb) == _FAIL) {

				rtw_free_recvframe(precvframe,
						   &r_priv->free_recv_queue);

				RTW_INFO("rtl8822be_rx_mpdu:can't allocate memory for skb copy\n");
				*((dma_addr_t *) skb->cb) =
					pci_map_single(pdvobjpriv->ppcidev,
						       skb_tail_pointer(skb),
						       r_priv->rxbuffersize,
						       PCI_DMA_FROMDEVICE);
				DBG_COUNTER(padapter->rx_logs.intf_rx_err_skb);
				goto done;
			}

			recvframe_put(precvframe, pattrib->pkt_len);

			if (pattrib->pkt_rpt_type == NORMAL_RX) {
				/* Normal rx packet */
				if (pattrib->physt)
					pphy_info = (u8 *)(skb->data) +
						    HALMAC_RX_DESC_SIZE_8822B;

#ifdef CONFIG_CONCURRENT_MODE
				pre_recv_entry(precvframe, pphy_info);
#endif

				if (pattrib->physt && pphy_info)
					rx_query_phy_status(precvframe,
							    pphy_info);

				if (rtw_recv_entry(precvframe) != _SUCCESS)
					RT_TRACE(_module_hci_ops_os_c_,
						 _drv_info_,
						("recvbuf2recvframe: rtw_recv_entry() Fail\n"));
			} else {
				if (pattrib->pkt_rpt_type == C2H_PACKET) {
					rtl8822b_c2h_handler_no_io(padapter,
							     skb->data,
						     HALMAC_RX_DESC_SIZE_8822B +
							     pattrib->pkt_len);
				} else
					DBG_COUNTER(padapter->rx_logs.intf_rx_report);

				rtw_free_recvframe(precvframe,
						   pfree_recv_queue);
			}
			*((dma_addr_t *) skb->cb) =
				pci_map_single(pdvobjpriv->ppcidev,
					       skb_tail_pointer(skb),
					       r_priv->rxbuffersize,
					       PCI_DMA_FROMDEVICE);
		}
done:


		SET_RX_BD_PHYSICAL_ADDR_LOW(rx_bd, *((dma_addr_t *)skb->cb));
		SET_RX_BD_RXBUFFSIZE(rx_bd, r_priv->rxbuffersize);

		r_priv->rx_ring[rx_q_idx].idx =
			(r_priv->rx_ring[rx_q_idx].idx + 1) %
			r_priv->rxringcount;

		rtw_write16(padapter, REG_RXQ_RXBD_IDX,
			    r_priv->rx_ring[rx_q_idx].idx);

		buf_desc_debug("RX:%s(%d) reg_value %x\n", __func__, __LINE__,
			       rtw_read32(padapter, REG_RXQ_RXBD_IDX));

		remaing_rxdesc--;
#ifdef CONFIG_NAPI
		rx++;
#endif
	}
#ifdef CONFIG_NAPI
	return rx;
#endif
}

#ifndef CONFIG_NAPI
static void rtl8822be_recv_tasklet(void *priv)
{
	_irqL	irqL;
	_adapter	*padapter = (_adapter *)priv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	rtl8822be_rx_mpdu(padapter);
	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);
	pHalData->IntrMask[0] |= (BIT_RXOK_MSK_8822B | BIT_RDU_MSK_8822B);
	pHalData->IntrMask[1] |= BIT_FOVW_MSK_8822B;
	rtw_write32(padapter, REG_HIMR0_8822B, pHalData->IntrMask[0]);
	rtw_write32(padapter, REG_HIMR1_8822B, pHalData->IntrMask[1]);
	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);
}
#endif

static void rtl8822be_xmit_beacon(PADAPTER Adapter)
{
#if defined(CONFIG_AP_MODE) && defined(CONFIG_NATIVEAP_MLME)
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* send_beacon(Adapter); */
		if (pmlmepriv->update_bcn == _TRUE)
			tx_beacon_hdl(Adapter, NULL);
	}
#endif
}

static void rtl8822be_prepare_bcn_tasklet(void *priv)
{
	_adapter *padapter = (_adapter *)priv;

	rtl8822be_xmit_beacon(padapter);
}

s32 rtl8822be_init_recv_priv(_adapter *padapter)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	s32	ret = _SUCCESS;

	_func_enter_;

#ifdef PLATFORM_LINUX
#ifndef CONFIG_NAPI
	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8822be_recv_tasklet,
		     (unsigned long)padapter);
#endif

	tasklet_init(&precvpriv->irq_prepare_beacon_tasklet,
		     (void(*)(unsigned long))rtl8822be_prepare_bcn_tasklet,
		     (unsigned long)padapter);
#endif

	_func_exit_;

	return ret;
}

void rtl8822be_free_recv_priv(_adapter *padapter)
{
	_func_enter_;

	_func_exit_;
}

int rtl8822be_init_rxbd_ring(_adapter *padapter)
{
	struct recv_priv	*r_priv = &padapter->recvpriv;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_dev	*pdev = pdvobjpriv->ppcidev;
	struct net_device	*dev = padapter->pnetdev;
	dma_addr_t *mapping = NULL;
	struct sk_buff *skb = NULL;
	u8	*rx_desc = NULL;
	int	i, rx_queue_idx;

	_func_enter_;

	/* rx_queue_idx 0:RX_MPDU_QUEUE */
	/* rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < 1; rx_queue_idx++) {
		r_priv->rx_ring[rx_queue_idx].buf_desc =
			pci_alloc_consistent(pdev,
			     sizeof(*r_priv->rx_ring[rx_queue_idx].buf_desc) *
					     r_priv->rxringcount,
				     &r_priv->rx_ring[rx_queue_idx].dma);

		if (!r_priv->rx_ring[rx_queue_idx].buf_desc ||
		    (unsigned long)r_priv->rx_ring[rx_queue_idx].buf_desc &
		    0xFF) {
			RTW_INFO("Cannot allocate RX ring\n");
			return _FAIL;
		}

		_rtw_memset(r_priv->rx_ring[rx_queue_idx].buf_desc, 0,
			    sizeof(*r_priv->rx_ring[rx_queue_idx].buf_desc) *
			    r_priv->rxringcount);
		r_priv->rx_ring[rx_queue_idx].idx = 0;

		for (i = 0; i < r_priv->rxringcount; i++) {
			skb = dev_alloc_skb(r_priv->rxbuffersize);
			if (!skb) {
				RTW_INFO("Cannot allocate skb for RX ring\n");
				return _FAIL;
			}

			rx_desc =
				(u8 *)(&r_priv->rx_ring[rx_queue_idx].buf_desc[i]);
			r_priv->rx_ring[rx_queue_idx].rx_buf[i] = skb;
			mapping = (dma_addr_t *)skb->cb;

			/* just set skb->cb to mapping addr
			 * for pci_unmap_single use */
			*mapping = pci_map_single(pdev, skb_tail_pointer(skb),
						  r_priv->rxbuffersize,
						  PCI_DMA_FROMDEVICE);

			/* Reset FS, LS, Total len */
			SET_RX_BD_LS(rx_desc, 0);
			SET_RX_BD_FS(rx_desc, 0);
			SET_RX_BD_TOTALRXPKTSIZE(rx_desc, 0);
			SET_RX_BD_RXBUFFSIZE(rx_desc, r_priv->rxbuffersize);
			SET_RX_BD_PHYSICAL_ADDR_LOW(rx_desc, *mapping);

			buf_desc_debug("RX:rx buffer desc addr[%d] = %x, skb(rx_buf) = %x, buffer addr (virtual = %x, physical = %x)\n",
				i, (u32)&r_priv->rx_ring[rx_queue_idx].buf_desc[i],
				(u32)r_priv->rx_ring[rx_queue_idx].rx_buf[i],
				(u32)(skb_tail_pointer(skb)), (u32)(*mapping));
		}
	}

	_func_exit_;

	return _SUCCESS;
}

void rtl8822be_free_rxbd_ring(_adapter *padapter)
{
	struct recv_priv *r_priv = &padapter->recvpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct pci_dev *pdev = pdvobjpriv->ppcidev;
	int i, rx_queue_idx;

	_func_enter_;

	/* rx_queue_idx 0:RX_MPDU_QUEUE */
	/* rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < 1; rx_queue_idx++) {
		for (i = 0; i < r_priv->rxringcount; i++) {
			struct sk_buff *skb;

			skb = r_priv->rx_ring[rx_queue_idx].rx_buf[i];

			if (!skb)
				continue;

			pci_unmap_single(pdev,
					 *((dma_addr_t *) skb->cb),
					 r_priv->rxbuffersize,
					 PCI_DMA_FROMDEVICE);
			kfree_skb(skb);
		}

		pci_free_consistent(pdev,
			    sizeof(*r_priv->rx_ring[rx_queue_idx].buf_desc) *
				    r_priv->rxringcount,
				    r_priv->rx_ring[rx_queue_idx].buf_desc,
				    r_priv->rx_ring[rx_queue_idx].dma);
		r_priv->rx_ring[rx_queue_idx].buf_desc = NULL;
	}

	_func_exit_;
}

