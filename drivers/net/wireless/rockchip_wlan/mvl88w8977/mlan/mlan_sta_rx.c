/** @file mlan_sta_rx.c
 *
 *  @brief This file contains the handling of RX in MLAN
 *  module.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/********************************************************
Change log:
    10/27/2008: initial version
********************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_11n_aggr.h"
#include "mlan_11n_rxreorder.h"
#ifdef DRV_EMBEDDED_SUPPLICANT
#include "authenticator_api.h"
#endif

/********************************************************
		Local Variables
********************************************************/

/** Ethernet II header */
typedef struct {
    /** Ethernet II header destination address */
	t_u8 dest_addr[MLAN_MAC_ADDR_LENGTH];
    /** Ethernet II header source address */
	t_u8 src_addr[MLAN_MAC_ADDR_LENGTH];
    /** Ethernet II header length */
	t_u16 ethertype;

} EthII_Hdr_t;

/********************************************************
		Global Variables
********************************************************/

/********************************************************
		Local Functions
********************************************************/

/********************************************************
		Global functions
********************************************************/

/**
 *  @brief This function process tdls action frame
 *
 *  @param priv        A pointer to mlan_private structure
 *  @param pbuf        A pointer to tdls action frame buffer
 *  @param len         len of tdls action frame buffer
 *  @return            N/A
 */
void
wlan_process_tdls_action_frame(pmlan_private priv, t_u8 *pbuf, t_u32 len)
{
	sta_node *sta_ptr = MNULL;
	IEEEtypes_VendorHeader_t *pvendor_ie = MNULL;
	const t_u8 wmm_oui[] = { 0x00, 0x50, 0xf2, 0x02 };
	t_u8 *peer;
	t_u8 *pos, *end;
	t_u8 action;
	int ie_len = 0;
	t_u8 i;

#define TDLS_PAYLOAD_TYPE     2
#define TDLS_CATEGORY         0x0c
#define TDLS_REQ_FIX_LEN      6
#define TDLS_RESP_FIX_LEN     8
#define TDLS_CONFIRM_FIX_LEN  6
	if (len < (sizeof(EthII_Hdr_t) + 3))
		return;
	if (*(t_u8 *)(pbuf + sizeof(EthII_Hdr_t)) != TDLS_PAYLOAD_TYPE)
		/*TDLS payload type = 2 */
		return;
	if (*(t_u8 *)(pbuf + sizeof(EthII_Hdr_t) + 1) != TDLS_CATEGORY)
		/*TDLS category = 0xc */
		return;
	peer = pbuf + MLAN_MAC_ADDR_LENGTH;

	action = *(t_u8 *)(pbuf + sizeof(EthII_Hdr_t) + 2);
	/*2= payload type + category */

	if (action > TDLS_SETUP_CONFIRM) {
		/*just handle TDLS setup request/response/confirm */
		PRINTM(MMSG, "Recv TDLS Action: peer=" MACSTR ", action=%d\n",
		       MAC2STR(peer), action);
		return;
	}

	sta_ptr = wlan_add_station_entry(priv, peer);
	if (!sta_ptr)
		return;
	if (action == TDLS_SETUP_REQUEST) {	/*setup request */
		sta_ptr->status = TDLS_NOT_SETUP;
		PRINTM(MMSG, "Recv TDLS SETUP Request: peer=" MACSTR "\n",
		       MAC2STR(peer));
		wlan_hold_tdls_packets(priv, peer);
		if (len < (sizeof(EthII_Hdr_t) + TDLS_REQ_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + 4;
		/*payload 1+ category 1 + action 1 +dialog 1 */
		sta_ptr->capability = mlan_ntohs(*(t_u16 *)pos);
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_REQ_FIX_LEN;
		pos += 2;
	} else if (action == 1) {	/*setup respons */
		PRINTM(MMSG, "Recv TDLS SETUP Response: peer=" MACSTR "\n",
		       MAC2STR(peer));
		if (len < (sizeof(EthII_Hdr_t) + TDLS_RESP_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + 6;
		/*payload 1+ category 1 + action 1 +dialog 1 +status 2 */
		sta_ptr->capability = mlan_ntohs(*(t_u16 *)pos);
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_RESP_FIX_LEN;
		pos += 2;
	} else {		/*setup confirm */
		PRINTM(MMSG, "Recv TDLS SETUP Confirm: peer=" MACSTR "\n",
		       MAC2STR(peer));
		if (len < (sizeof(EthII_Hdr_t) + TDLS_CONFIRM_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + TDLS_CONFIRM_FIX_LEN;
		/*payload 1+ category 1 + action 1 +dialog 1 + status 2 */
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_CONFIRM_FIX_LEN;
	}
	for (end = pos + ie_len; pos + 1 < end; pos += 2 + pos[1]) {
		if (pos + 2 + pos[1] > end)
			break;
		switch (*pos) {
		case SUPPORTED_RATES:
			sta_ptr->rate_len = pos[1];
			for (i = 0; i < pos[1]; i++)
				sta_ptr->support_rate[i] = pos[2 + i];
			break;
		case EXTENDED_SUPPORTED_RATES:
			for (i = 0; i < pos[1]; i++)
				sta_ptr->support_rate[sta_ptr->rate_len + i] =
					pos[2 + i];
			sta_ptr->rate_len += pos[1];
			break;
		case HT_CAPABILITY:
			memcpy(priv->adapter, (t_u8 *)&sta_ptr->HTcap, pos,
			       sizeof(IEEEtypes_HTCap_t));
			sta_ptr->is_11n_enabled = 1;
			DBG_HEXDUMP(MDAT_D, "TDLS HT capability",
				    (t_u8 *)(&sta_ptr->HTcap),
				    MIN(sizeof(IEEEtypes_HTCap_t),
					MAX_DATA_DUMP_LEN));
			break;
		case HT_OPERATION:
			memcpy(priv->adapter, &sta_ptr->HTInfo, pos,
			       sizeof(IEEEtypes_HTInfo_t));
			DBG_HEXDUMP(MDAT_D, "TDLS HT info",
				    (t_u8 *)(&sta_ptr->HTInfo),
				    MIN(sizeof(IEEEtypes_HTInfo_t),
					MAX_DATA_DUMP_LEN));
			break;
		case BSSCO_2040:
			memcpy(priv->adapter, (t_u8 *)&sta_ptr->BSSCO_20_40,
			       pos, sizeof(IEEEtypes_2040BSSCo_t));
			break;
		case EXT_CAPABILITY:
			memcpy(priv->adapter, (t_u8 *)&sta_ptr->ExtCap, pos,
			       pos[1] + sizeof(IEEEtypes_Header_t));
			DBG_HEXDUMP(MDAT_D, "TDLS Extended capability",
				    (t_u8 *)(&sta_ptr->ExtCap),
				    sta_ptr->ExtCap.ieee_hdr.len + 2);
			break;
		case RSN_IE:
			memcpy(priv->adapter, (t_u8 *)&sta_ptr->rsn_ie, pos,
			       pos[1] + sizeof(IEEEtypes_Header_t));
			DBG_HEXDUMP(MDAT_D, "TDLS Rsn ie ",
				    (t_u8 *)(&sta_ptr->rsn_ie),
				    pos[1] + sizeof(IEEEtypes_Header_t));
			break;
		case QOS_INFO:
			sta_ptr->qos_info = pos[2];
			PRINTM(MDAT_D, "TDLS qos info %x\n", sta_ptr->qos_info);
			break;
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorHeader_t *)pos;
			if (!memcmp
			    (priv->adapter, pvendor_ie->oui, wmm_oui,
			     sizeof(wmm_oui))) {
				sta_ptr->qos_info = pos[8];	    /** qos info in wmm parameters in response and confirm */
				PRINTM(MDAT_D, "TDLS qos info %x\n",
				       sta_ptr->qos_info);
			}
			break;
		case LINK_ID:
			memcpy(priv->adapter, (t_u8 *)&sta_ptr->link_ie, pos,
			       sizeof(IEEEtypes_LinkIDElement_t));
			break;

		default:
			break;
		}
	}
	return;
}

/**
 *  @brief This function get pxpd info for radiotap info
 *
 *  @param priv A pointer to pmlan_private
 *  @param prx_pd   A pointer to RxPD
 *  @param prt_info   A pointer to radiotap_info
 *
 *  @return        N/A
 */
void
wlan_rxpdinfo_to_radiotapinfo(pmlan_private priv, RxPD *prx_pd,
			      radiotap_info * prt_info)
{
	radiotap_info rt_info_tmp;
	t_u8 rx_rate_info = 0;
	t_u8 mcs_index = 0;
	t_u8 format = 0;
	t_u8 bw = 0;
	t_u8 gi = 0;
	t_u8 ldpc = 0;

	memset(priv->adapter, &rt_info_tmp, 0x00, sizeof(rt_info_tmp));
	rt_info_tmp.snr = prx_pd->snr;
	rt_info_tmp.nf = prx_pd->nf;
	rt_info_tmp.band_config = (prx_pd->rx_info & 0xf);
	rt_info_tmp.chan_num = (prx_pd->rx_info & RXPD_CHAN_MASK) >> 5;

	rt_info_tmp.antenna = prx_pd->antenna;
	rx_rate_info = prx_pd->rate_info;
	if ((rx_rate_info & 0x3) == MLAN_RATE_FORMAT_HT) {
		/* HT rate */
		format = MLAN_RATE_FORMAT_HT;
		mcs_index = prx_pd->rx_rate;
		/* 20M: bw=0, 40M: bw=1 */
		bw = (rx_rate_info & 0xC) >> 2;
		/* LGI: gi =0, SGI: gi = 1 */
		gi = (rx_rate_info & 0x10) >> 4;
	} else {
		/* LG rate */
		format = MLAN_RATE_FORMAT_LG;
		mcs_index = (prx_pd->rx_rate > MLAN_RATE_INDEX_OFDM0) ?
			prx_pd->rx_rate - 1 : prx_pd->rx_rate;
	}
	ldpc = rx_rate_info & 0x40;

	rt_info_tmp.rate_info.mcs_index = mcs_index;
	rt_info_tmp.rate_info.rate_info =
		(ldpc << 5) | (format << 3) | (bw << 1) | gi;
	rt_info_tmp.rate_info.bitrate =
		wlan_index_to_data_rate(priv->adapter, prx_pd->rx_rate,
					prx_pd->rate_info);

	if (prx_pd->flags & RXPD_FLAG_EXTRA_HEADER)
		memcpy(priv->adapter, &rt_info_tmp.extra_info,
		       (t_u8 *)prx_pd + sizeof(*prx_pd),
		       sizeof(rt_info_tmp.extra_info));

	memset(priv->adapter, prt_info, 0x00, sizeof(*prt_info));
	memcpy(priv->adapter, prt_info, &rt_info_tmp, sizeof(*prt_info));

	return;
}

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer
 *
 *  @param pmadapter A pointer to mlan_adapter
 *  @param pmbuf   A pointer to mlan_buffer which includes the received packet
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	RxPacketHdr_t *prx_pkt;
	RxPD *prx_pd;
	int hdr_chop;
	EthII_Hdr_t *peth_hdr;
	t_u8 rfc1042_eth_hdr[MLAN_MAC_ADDR_LENGTH] = {
		0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00
	};
	t_u8 snap_oui_802_h[MLAN_MAC_ADDR_LENGTH] = {
		0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8
	};
	t_u8 appletalk_aarp_type[2] = { 0x80, 0xf3 };
	t_u8 ipx_snap_type[2] = { 0x81, 0x37 };
	t_u8 tdls_action_type[2] = { 0x89, 0x0d };
#ifdef DRV_EMBEDDED_SUPPLICANT
	t_u8 eapol_type[2] = { 0x88, 0x8e };
#endif

	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

/** Small debug type */
#define DBG_TYPE_SMALL  2
/** Size of debugging structure */
#define SIZE_OF_DBG_STRUCT 4
	if (prx_pd->rx_pkt_type == PKT_TYPE_DEBUG) {
		t_u8 dbg_type;
		dbg_type = *(t_u8 *)&prx_pkt->eth803_hdr;
		if (dbg_type == DBG_TYPE_SMALL) {
			PRINTM(MFW_D, "\n");
			DBG_HEXDUMP(MFW_D, "FWDBG",
				    (char *)((t_u8 *)&prx_pkt->eth803_hdr +
					     SIZE_OF_DBG_STRUCT),
				    prx_pd->rx_pkt_length);
			PRINTM(MFW_D, "FWDBG::\n");
		}
		goto done;
	}

	PRINTM(MINFO,
	       "RX Data: data_len - prx_pd->rx_pkt_offset = %d - %d = %d\n",
	       pmbuf->data_len, prx_pd->rx_pkt_offset,
	       pmbuf->data_len - prx_pd->rx_pkt_offset);

	HEXDUMP("RX Data: Dest", prx_pkt->eth803_hdr.dest_addr,
		sizeof(prx_pkt->eth803_hdr.dest_addr));
	HEXDUMP("RX Data: Src", prx_pkt->eth803_hdr.src_addr,
		sizeof(prx_pkt->eth803_hdr.src_addr));

	if ((memcmp(pmadapter, &prx_pkt->rfc1042_hdr,
		    snap_oui_802_h, sizeof(snap_oui_802_h)) == 0) ||
	    ((memcmp(pmadapter, &prx_pkt->rfc1042_hdr,
		     rfc1042_eth_hdr, sizeof(rfc1042_eth_hdr)) == 0) &&
	     memcmp(pmadapter, &prx_pkt->rfc1042_hdr.snap_type,
		    appletalk_aarp_type, sizeof(appletalk_aarp_type)) &&
	     memcmp(pmadapter, &prx_pkt->rfc1042_hdr.snap_type,
		    ipx_snap_type, sizeof(ipx_snap_type)))) {
		/*
		 * Replace the 803 header and rfc1042 header (llc/snap) with an
		 * EthernetII header, keep the src/dst and snap_type (ethertype).
		 * The firmware only passes up SNAP frames converting
		 * all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 * To create the Ethernet II, just move the src, dst address
		 * right before the snap_type.
		 */
		peth_hdr = (EthII_Hdr_t *)
			((t_u8 *)&prx_pkt->eth803_hdr
			 + sizeof(prx_pkt->eth803_hdr) +
			 sizeof(prx_pkt->rfc1042_hdr)
			 - sizeof(prx_pkt->eth803_hdr.dest_addr)
			 - sizeof(prx_pkt->eth803_hdr.src_addr)
			 - sizeof(prx_pkt->rfc1042_hdr.snap_type));

		memcpy(pmadapter, peth_hdr->src_addr,
		       prx_pkt->eth803_hdr.src_addr,
		       sizeof(peth_hdr->src_addr));
		memcpy(pmadapter, peth_hdr->dest_addr,
		       prx_pkt->eth803_hdr.dest_addr,
		       sizeof(peth_hdr->dest_addr));

		/* Chop off the RxPD + the excess memory from the 802.2/llc/snap
		 *  header that was removed.
		 */
		hdr_chop = (t_u32)((t_ptr)peth_hdr - (t_ptr)prx_pd);
	} else {
		HEXDUMP("RX Data: LLC/SNAP",
			(t_u8 *)&prx_pkt->rfc1042_hdr,
			sizeof(prx_pkt->rfc1042_hdr));
		if (!memcmp
		    (pmadapter, &prx_pkt->eth803_hdr.h803_len, tdls_action_type,
		     sizeof(tdls_action_type))) {
			wlan_process_tdls_action_frame(priv,
						       ((t_u8 *)prx_pd +
							prx_pd->rx_pkt_offset),
						       prx_pd->rx_pkt_length);
		}
		/* Chop off the RxPD */
		hdr_chop = (t_u32)((t_ptr)&prx_pkt->eth803_hdr - (t_ptr)prx_pd);
	}

	/* Chop off the leading header bytes so the it points to the start of
	 *   either the reconstructed EthII frame or the 802.2/llc/snap frame
	 */
	pmbuf->data_len -= hdr_chop;
	pmbuf->data_offset += hdr_chop;
	pmbuf->pparent = MNULL;
	DBG_HEXDUMP(MDAT_D, "RxPD", (t_u8 *)prx_pd,
		    MIN(sizeof(RxPD), MAX_DATA_DUMP_LEN));
	DBG_HEXDUMP(MDAT_D, "Rx Payload",
		    ((t_u8 *)prx_pd + prx_pd->rx_pkt_offset),
		    MIN(prx_pd->rx_pkt_length, MAX_DATA_DUMP_LEN));

	priv->rxpd_rate = prx_pd->rx_rate;

	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmbuf->out_ts_sec,
						  &pmbuf->out_ts_usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Data => kernel seq_num=%d tid=%d\n",
	       pmbuf->out_ts_sec, pmbuf->out_ts_usec, prx_pd->seq_num,
	       prx_pd->priority);

	if (pmadapter->enable_net_mon) {
		pmbuf->flags |= MLAN_BUF_FLAG_NET_MONITOR;
		goto mon_process;
	}

#ifdef DRV_EMBEDDED_SUPPLICANT
	if (supplicantIsEnabled(priv->psapriv) &&
	    (!memcmp
	     (pmadapter, &prx_pkt->eth803_hdr.h803_len, eapol_type,
	      sizeof(eapol_type)))) {
		//BML_SET_OFFSET(bufDesc, offset);
		if (ProcessEAPoLPkt(priv->psapriv, pmbuf)) {
			wlan_free_mlan_buffer(pmadapter, pmbuf);
			ret = MLAN_STATUS_SUCCESS;
			PRINTM(MMSG,
			       "host supplicant eapol pkt process done.\n");

			LEAVE();
			return ret;
		}
	}
#endif

mon_process:
	if (pmbuf->flags & MLAN_BUF_FLAG_NET_MONITOR) {
		//Use some rxpd space to save rxpd info for radiotap header
		//We should insure radiotap_info is not bigger than RxPD
		wlan_rxpdinfo_to_radiotapinfo(priv, prx_pd,
					      (radiotap_info *) (pmbuf->pbuf +
								 pmbuf->
								 data_offset -
								 sizeof
								 (radiotap_info)));
	}
	if (MFALSE || priv->rx_pkt_info) {
		pmbuf->u.rx_info.data_rate =
			wlan_index_to_data_rate(priv->adapter, prx_pd->rx_rate,
						prx_pd->rate_info);
		pmbuf->u.rx_info.channel =
			(prx_pd->rx_info & RXPD_CHAN_MASK) >> 5;
		pmbuf->u.rx_info.antenna = prx_pd->antenna;
		pmbuf->u.rx_info.rssi = prx_pd->snr - prx_pd->nf;
	}
	ret = pmadapter->callbacks.moal_recv_packet(pmadapter->pmoal_handle,
						    pmbuf);
	if (ret == MLAN_STATUS_FAILURE) {
		pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
		PRINTM(MERROR,
		       "STA Rx Error: moal_recv_packet returned error\n");
	}
done:
	if (ret != MLAN_STATUS_PENDING)
		wlan_free_mlan_buffer(pmadapter, pmbuf);

	LEAVE();

	return ret;
}

/**
 *   @brief This function processes the received buffer
 *
 *   @param adapter A pointer to mlan_adapter
 *   @param pmbuf     A pointer to the received buffer
 *
 *   @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_sta_process_rx_packet(IN t_void *adapter, IN pmlan_buffer pmbuf)
{
	pmlan_adapter pmadapter = (pmlan_adapter)adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	RxPD *prx_pd;
	RxPacketHdr_t *prx_pkt;
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
	t_u16 rx_pkt_type = 0;
	wlan_mgmt_pkt *pmgmt_pkt_hdr = MNULL;

	sta_node *sta_ptr = MNULL;
	t_u8 adj_rx_rate = 0;
	t_u8 antenna = 0;
	rxpd_extra_info *pextra_info = MNULL;
	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	/* Endian conversion */
	endian_convert_RxPD(prx_pd);

	if (prx_pd->flags & RXPD_FLAG_EXTRA_HEADER) {
		pextra_info =
			(rxpd_extra_info *) ((t_u8 *)prx_pd + sizeof(*prx_pd));
		endian_convert_RxPD_extra_header(pextra_info);
	}
	rx_pkt_type = prx_pd->rx_pkt_type;
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

	priv->rxpd_rate_info = prx_pd->rate_info;
	if (priv->bss_type == MLAN_BSS_TYPE_STA) {
		antenna = wlan_adjust_antenna(priv, prx_pd);
		adj_rx_rate =
			wlan_adjust_data_rate(priv, priv->rxpd_rate,
					      priv->rxpd_rate_info);
		pmadapter->callbacks.moal_hist_data_add(pmadapter->pmoal_handle,
							pmbuf->bss_index,
							adj_rx_rate,
							prx_pd->snr, prx_pd->nf,
							antenna);
	}

	if ((prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length) !=
	    (t_u16)pmbuf->data_len) {
		PRINTM(MERROR,
		       "Wrong rx packet: len=%d,rx_pkt_offset=%d,"
		       " rx_pkt_length=%d\n", pmbuf->data_len,
		       prx_pd->rx_pkt_offset, prx_pd->rx_pkt_length);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		ret = MLAN_STATUS_FAILURE;
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		goto done;
	}
	pmbuf->data_len = prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length;

	if (pmadapter->priv[pmbuf->bss_index]->mgmt_frame_passthru_mask &&
	    prx_pd->rx_pkt_type == PKT_TYPE_MGMT_FRAME) {
		/* Check if this is mgmt packet and needs to
		 * forwarded to app as an event
		 */
		pmgmt_pkt_hdr =
			(wlan_mgmt_pkt *)((t_u8 *)prx_pd +
					  prx_pd->rx_pkt_offset);
		pmgmt_pkt_hdr->frm_len =
			wlan_le16_to_cpu(pmgmt_pkt_hdr->frm_len);

		if ((pmgmt_pkt_hdr->wlan_header.frm_ctl
		     & IEEE80211_FC_MGMT_FRAME_TYPE_MASK) == 0)
			wlan_process_802dot11_mgmt_pkt(pmadapter->
						       priv[pmbuf->bss_index],
						       (t_u8 *)&pmgmt_pkt_hdr->
						       wlan_header,
						       pmgmt_pkt_hdr->frm_len +
						       sizeof(wlan_mgmt_pkt)
						       -
						       sizeof(pmgmt_pkt_hdr->
							      frm_len), prx_pd);
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		goto done;
	}

	/*
	 * If the packet is not an unicast packet then send the packet
	 * directly to os. Don't pass thru rx reordering
	 */
	if ((!IS_11N_ENABLED(priv)
	     && !IS_11N_ADHOC_ENABLED(priv)
	     && !(prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK)
	    ) ||
	    memcmp(priv->adapter, priv->curr_addr,
		   prx_pkt->eth803_hdr.dest_addr, MLAN_MAC_ADDR_LENGTH)) {
		wlan_process_rx_packet(pmadapter, pmbuf);
		goto done;
	}

	if (queuing_ra_based(priv) ||
	    (prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK)) {
		memcpy(pmadapter, ta, prx_pkt->eth803_hdr.src_addr,
		       MLAN_MAC_ADDR_LENGTH);
		if (prx_pd->priority < MAX_NUM_TID) {
			PRINTM(MDATA, "adhoc/tdls packet %p " MACSTR "\n",
			       pmbuf, MAC2STR(ta));
			sta_ptr = wlan_get_station_entry(priv, ta);
			if (sta_ptr) {
				sta_ptr->rx_seq[prx_pd->priority] =
					prx_pd->seq_num;
				if (prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK) {
					sta_ptr->snr = prx_pd->snr;
					sta_ptr->nf = prx_pd->nf;
					pmadapter->callbacks.
						moal_updata_peer_signal
						(pmadapter->pmoal_handle,
						 pmbuf->bss_index, ta,
						 prx_pd->snr, prx_pd->nf);
				}
			}
			if (!sta_ptr || !sta_ptr->is_11n_enabled) {
				wlan_process_rx_packet(pmadapter, pmbuf);
				goto done;
			}
		}
	} else {
		if ((rx_pkt_type != PKT_TYPE_BAR) &&
		    (prx_pd->priority < MAX_NUM_TID))
			priv->rx_seq[prx_pd->priority] = prx_pd->seq_num;
		memcpy(pmadapter, ta,
		       priv->curr_bss_params.bss_descriptor.mac_address,
		       MLAN_MAC_ADDR_LENGTH);
	}

	if ((priv->port_ctrl_mode == MTRUE && priv->port_open == MFALSE) &&
	    (rx_pkt_type != PKT_TYPE_BAR)) {
		mlan_11n_rxreorder_pkt(priv, prx_pd->seq_num, prx_pd->priority,
				       ta, (t_u8)prx_pd->rx_pkt_type,
				       (t_void *)RX_PKT_DROPPED_IN_FW);
		if (rx_pkt_type == PKT_TYPE_AMSDU) {
			pmbuf->data_len = prx_pd->rx_pkt_length;
			pmbuf->data_offset += prx_pd->rx_pkt_offset;
			wlan_11n_deaggregate_pkt(priv, pmbuf);
		} else {
			wlan_process_rx_packet(pmadapter, pmbuf);
		}
		goto done;
	}

	/* Reorder and send to OS */
	ret = mlan_11n_rxreorder_pkt(priv, prx_pd->seq_num,
				     prx_pd->priority, ta,
				     (t_u8)prx_pd->rx_pkt_type, (void *)pmbuf);
	if (ret || (rx_pkt_type == PKT_TYPE_BAR)
		) {
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	}

done:

	LEAVE();
	return ret;
}
