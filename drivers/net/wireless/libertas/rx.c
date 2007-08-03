/**
  * This file contains the handling of RX in wlan driver.
  */
#include <linux/etherdevice.h>
#include <linux/types.h>

#include "hostcmd.h"
#include "radiotap.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"

struct eth803hdr {
	u8 dest_addr[6];
	u8 src_addr[6];
	u16 h803_len;
} __attribute__ ((packed));

struct rfc1042hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	u16 snap_type;
} __attribute__ ((packed));

struct rxpackethdr {
	struct rxpd rx_pd;
	struct eth803hdr eth803_hdr;
	struct rfc1042hdr rfc1042_hdr;
} __attribute__ ((packed));

struct rx80211packethdr {
	struct rxpd rx_pd;
	void *eth80211_hdr;
} __attribute__ ((packed));

static int process_rxed_802_11_packet(wlan_private * priv, struct sk_buff *skb);

/**
 *  @brief This function computes the avgSNR .
 *
 *  @param priv    A pointer to wlan_private structure
 *  @return 	   avgSNR
 */
static u8 wlan_getavgsnr(wlan_private * priv)
{
	u8 i;
	u16 temp = 0;
	wlan_adapter *adapter = priv->adapter;
	if (adapter->numSNRNF == 0)
		return 0;
	for (i = 0; i < adapter->numSNRNF; i++)
		temp += adapter->rawSNR[i];
	return (u8) (temp / adapter->numSNRNF);

}

/**
 *  @brief This function computes the AvgNF
 *
 *  @param priv    A pointer to wlan_private structure
 *  @return 	   AvgNF
 */
static u8 wlan_getavgnf(wlan_private * priv)
{
	u8 i;
	u16 temp = 0;
	wlan_adapter *adapter = priv->adapter;
	if (adapter->numSNRNF == 0)
		return 0;
	for (i = 0; i < adapter->numSNRNF; i++)
		temp += adapter->rawNF[i];
	return (u8) (temp / adapter->numSNRNF);

}

/**
 *  @brief This function save the raw SNR/NF to our internel buffer
 *
 *  @param priv    A pointer to wlan_private structure
 *  @param prxpd   A pointer to rxpd structure of received packet
 *  @return 	   n/a
 */
static void wlan_save_rawSNRNF(wlan_private * priv, struct rxpd *p_rx_pd)
{
	wlan_adapter *adapter = priv->adapter;
	if (adapter->numSNRNF < DEFAULT_DATA_AVG_FACTOR)
		adapter->numSNRNF++;
	adapter->rawSNR[adapter->nextSNRNF] = p_rx_pd->snr;
	adapter->rawNF[adapter->nextSNRNF] = p_rx_pd->nf;
	adapter->nextSNRNF++;
	if (adapter->nextSNRNF >= DEFAULT_DATA_AVG_FACTOR)
		adapter->nextSNRNF = 0;
	return;
}

/**
 *  @brief This function computes the RSSI in received packet.
 *
 *  @param priv    A pointer to wlan_private structure
 *  @param prxpd   A pointer to rxpd structure of received packet
 *  @return 	   n/a
 */
static void wlan_compute_rssi(wlan_private * priv, struct rxpd *p_rx_pd)
{
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_RX);

	lbs_deb_rx("rxpd: SNR %d, NF %d\n", p_rx_pd->snr, p_rx_pd->nf);
	lbs_deb_rx("before computing SNR: SNR-avg = %d, NF-avg = %d\n",
	       adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE,
	       adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE);

	adapter->SNR[TYPE_RXPD][TYPE_NOAVG] = p_rx_pd->snr;
	adapter->NF[TYPE_RXPD][TYPE_NOAVG] = p_rx_pd->nf;
	wlan_save_rawSNRNF(priv, p_rx_pd);

	adapter->SNR[TYPE_RXPD][TYPE_AVG] = wlan_getavgsnr(priv) * AVG_SCALE;
	adapter->NF[TYPE_RXPD][TYPE_AVG] = wlan_getavgnf(priv) * AVG_SCALE;
	lbs_deb_rx("after computing SNR: SNR-avg = %d, NF-avg = %d\n",
	       adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE,
	       adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE);

	adapter->RSSI[TYPE_RXPD][TYPE_NOAVG] =
	    CAL_RSSI(adapter->SNR[TYPE_RXPD][TYPE_NOAVG],
		     adapter->NF[TYPE_RXPD][TYPE_NOAVG]);

	adapter->RSSI[TYPE_RXPD][TYPE_AVG] =
	    CAL_RSSI(adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE,
		     adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE);

	lbs_deb_leave(LBS_DEB_RX);
}

void libertas_upload_rx_packet(wlan_private * priv, struct sk_buff *skb)
{
	lbs_deb_rx("skb->data %p\n", skb->data);

	if (priv->adapter->monitormode != WLAN_MONITOR_OFF) {
		skb->protocol = eth_type_trans(skb, priv->rtap_net_dev);
	} else {
		if (priv->mesh_dev && IS_MESH_FRAME(skb))
			skb->protocol = eth_type_trans(skb, priv->mesh_dev);
		else
			skb->protocol = eth_type_trans(skb, priv->dev);
	}
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_rx(skb);
}

/**
 *  @brief This function processes received packet and forwards it
 *  to kernel/upper layer
 *
 *  @param priv    A pointer to wlan_private
 *  @param skb     A pointer to skb which includes the received packet
 *  @return 	   0 or -1
 */
int libertas_process_rxed_packet(wlan_private * priv, struct sk_buff *skb)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	struct rxpackethdr *p_rx_pkt;
	struct rxpd *p_rx_pd;

	int hdrchop;
	struct ethhdr *p_ethhdr;

	const u8 rfc1042_eth_hdr[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

	lbs_deb_enter(LBS_DEB_RX);

	if (priv->adapter->monitormode != WLAN_MONITOR_OFF)
		return process_rxed_802_11_packet(priv, skb);

	p_rx_pkt = (struct rxpackethdr *) skb->data;
	p_rx_pd = &p_rx_pkt->rx_pd;
	if (p_rx_pd->rx_control & RxPD_MESH_FRAME)
		SET_MESH_FRAME(skb);
	else
		UNSET_MESH_FRAME(skb);

	lbs_deb_hex(LBS_DEB_RX, "RX Data: Before chop rxpd", skb->data,
		 min_t(unsigned int, skb->len, 100));

	if (skb->len < (ETH_HLEN + 8 + sizeof(struct rxpd))) {
		lbs_deb_rx("rx err: frame received with bad length\n");
		priv->stats.rx_length_errors++;
		ret = 0;
		goto done;
	}

	/*
	 * Check rxpd status and update 802.3 stat,
	 */
	if (!(p_rx_pd->status & cpu_to_le16(MRVDRV_RXPD_STATUS_OK))) {
		lbs_deb_rx("rx err: frame received with bad status\n");
		lbs_pr_alert("rxpd not ok\n");
		priv->stats.rx_errors++;
		ret = 0;
		goto done;
	}

	lbs_deb_rx("rx data: skb->len-sizeof(RxPd) = %d-%zd = %zd\n",
	       skb->len, sizeof(struct rxpd), skb->len - sizeof(struct rxpd));

	lbs_deb_hex(LBS_DEB_RX, "RX Data: Dest", p_rx_pkt->eth803_hdr.dest_addr,
		sizeof(p_rx_pkt->eth803_hdr.dest_addr));
	lbs_deb_hex(LBS_DEB_RX, "RX Data: Src", p_rx_pkt->eth803_hdr.src_addr,
		sizeof(p_rx_pkt->eth803_hdr.src_addr));

	if (memcmp(&p_rx_pkt->rfc1042_hdr,
		   rfc1042_eth_hdr, sizeof(rfc1042_eth_hdr)) == 0) {
		/*
		 *  Replace the 803 header and rfc1042 header (llc/snap) with an
		 *    EthernetII header, keep the src/dst and snap_type (ethertype)
		 *
		 *  The firmware only passes up SNAP frames converting
		 *    all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 *
		 *  To create the Ethernet II, just move the src, dst address right
		 *    before the snap_type.
		 */
		p_ethhdr = (struct ethhdr *)
		    ((u8 *) & p_rx_pkt->eth803_hdr
		     + sizeof(p_rx_pkt->eth803_hdr) + sizeof(p_rx_pkt->rfc1042_hdr)
		     - sizeof(p_rx_pkt->eth803_hdr.dest_addr)
		     - sizeof(p_rx_pkt->eth803_hdr.src_addr)
		     - sizeof(p_rx_pkt->rfc1042_hdr.snap_type));

		memcpy(p_ethhdr->h_source, p_rx_pkt->eth803_hdr.src_addr,
		       sizeof(p_ethhdr->h_source));
		memcpy(p_ethhdr->h_dest, p_rx_pkt->eth803_hdr.dest_addr,
		       sizeof(p_ethhdr->h_dest));

		/* Chop off the rxpd + the excess memory from the 802.2/llc/snap header
		 *   that was removed
		 */
		hdrchop = (u8 *) p_ethhdr - (u8 *) p_rx_pkt;
	} else {
		lbs_deb_hex(LBS_DEB_RX, "RX Data: LLC/SNAP",
			(u8 *) & p_rx_pkt->rfc1042_hdr,
			sizeof(p_rx_pkt->rfc1042_hdr));

		/* Chop off the rxpd */
		hdrchop = (u8 *) & p_rx_pkt->eth803_hdr - (u8 *) p_rx_pkt;
	}

	/* Chop off the leading header bytes so the skb points to the start of
	 *   either the reconstructed EthII frame or the 802.2/llc/snap frame
	 */
	skb_pull(skb, hdrchop);

	/* Take the data rate from the rxpd structure
	 * only if the rate is auto
	 */
	if (adapter->auto_rate)
		adapter->cur_rate = libertas_fw_index_to_data_rate(p_rx_pd->rx_rate);

	wlan_compute_rssi(priv, p_rx_pd);

	lbs_deb_rx("rx data: size of actual packet %d\n", skb->len);
	priv->stats.rx_bytes += skb->len;
	priv->stats.rx_packets++;

	libertas_upload_rx_packet(priv, skb);

	ret = 0;
done:
	lbs_deb_leave_args(LBS_DEB_RX, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(libertas_process_rxed_packet);

/**
 *  @brief This function converts Tx/Rx rates from the Marvell WLAN format
 *  (see Table 2 in Section 3.1) to IEEE80211_RADIOTAP_RATE units (500 Kb/s)
 *
 *  @param rate    Input rate
 *  @return 	   Output Rate (0 if invalid)
 */
static u8 convert_mv_rate_to_radiotap(u8 rate)
{
	switch (rate) {
	case 0:		/*   1 Mbps */
		return 2;
	case 1:		/*   2 Mbps */
		return 4;
	case 2:		/* 5.5 Mbps */
		return 11;
	case 3:		/*  11 Mbps */
		return 22;
	/* case 4: reserved */
	case 5:		/*   6 Mbps */
		return 12;
	case 6:		/*   9 Mbps */
		return 18;
	case 7:		/*  12 Mbps */
		return 24;
	case 8:		/*  18 Mbps */
		return 36;
	case 9:		/*  24 Mbps */
		return 48;
	case 10:		/*  36 Mbps */
		return 72;
	case 11:		/*  48 Mbps */
		return 96;
	case 12:		/*  54 Mbps */
		return 108;
	}
	lbs_pr_alert("Invalid Marvell WLAN rate %i\n", rate);
	return 0;
}

/**
 *  @brief This function processes a received 802.11 packet and forwards it
 *  to kernel/upper layer
 *
 *  @param priv    A pointer to wlan_private
 *  @param skb     A pointer to skb which includes the received packet
 *  @return 	   0 or -1
 */
static int process_rxed_802_11_packet(wlan_private * priv, struct sk_buff *skb)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	struct rx80211packethdr *p_rx_pkt;
	struct rxpd *prxpd;
	struct rx_radiotap_hdr radiotap_hdr;
	struct rx_radiotap_hdr *pradiotap_hdr;

	lbs_deb_enter(LBS_DEB_RX);

	p_rx_pkt = (struct rx80211packethdr *) skb->data;
	prxpd = &p_rx_pkt->rx_pd;

	// lbs_deb_hex(LBS_DEB_RX, "RX Data: Before chop rxpd", skb->data, min(skb->len, 100));

	if (skb->len < (ETH_HLEN + 8 + sizeof(struct rxpd))) {
		lbs_deb_rx("rx err: frame received wit bad length\n");
		priv->stats.rx_length_errors++;
		ret = 0;
		goto done;
	}

	/*
	 * Check rxpd status and update 802.3 stat,
	 */
	if (!(prxpd->status & cpu_to_le16(MRVDRV_RXPD_STATUS_OK))) {
		//lbs_deb_rx("rx err: frame received with bad status\n");
		priv->stats.rx_errors++;
	}

	lbs_deb_rx("rx data: skb->len-sizeof(RxPd) = %d-%zd = %zd\n",
	       skb->len, sizeof(struct rxpd), skb->len - sizeof(struct rxpd));

	/* create the exported radio header */
	if(priv->adapter->monitormode == WLAN_MONITOR_OFF) {
		/* no radio header */
		/* chop the rxpd */
		skb_pull(skb, sizeof(struct rxpd));
	}

	else {
		/* radiotap header */
		radiotap_hdr.hdr.it_version = 0;
		/* XXX must check this value for pad */
		radiotap_hdr.hdr.it_pad = 0;
		radiotap_hdr.hdr.it_len = cpu_to_le16 (sizeof(struct rx_radiotap_hdr));
		radiotap_hdr.hdr.it_present = cpu_to_le32 (RX_RADIOTAP_PRESENT);
		/* unknown values */
		radiotap_hdr.flags = 0;
		radiotap_hdr.chan_freq = 0;
		radiotap_hdr.chan_flags = 0;
		radiotap_hdr.antenna = 0;
		/* known values */
		radiotap_hdr.rate = convert_mv_rate_to_radiotap(prxpd->rx_rate);
		/* XXX must check no carryout */
		radiotap_hdr.antsignal = prxpd->snr + prxpd->nf;
		radiotap_hdr.rx_flags = 0;
		if (!(prxpd->status & cpu_to_le16(MRVDRV_RXPD_STATUS_OK)))
			radiotap_hdr.rx_flags |= IEEE80211_RADIOTAP_F_RX_BADFCS;
		//memset(radiotap_hdr.pad, 0x11, IEEE80211_RADIOTAP_HDRLEN - 18);

		/* chop the rxpd */
		skb_pull(skb, sizeof(struct rxpd));

		/* add space for the new radio header */
		if ((skb_headroom(skb) < sizeof(struct rx_radiotap_hdr)) &&
		    pskb_expand_head(skb, sizeof(struct rx_radiotap_hdr), 0,
				     GFP_ATOMIC)) {
			lbs_pr_alert("%s: couldn't pskb_expand_head\n",
			       __func__);
		}

		pradiotap_hdr =
		    (struct rx_radiotap_hdr *)skb_push(skb,
						     sizeof(struct
							    rx_radiotap_hdr));
		memcpy(pradiotap_hdr, &radiotap_hdr,
		       sizeof(struct rx_radiotap_hdr));
	}

	/* Take the data rate from the rxpd structure
	 * only if the rate is auto
	 */
	if (adapter->auto_rate)
		adapter->cur_rate = libertas_fw_index_to_data_rate(prxpd->rx_rate);

	wlan_compute_rssi(priv, prxpd);

	lbs_deb_rx("rx data: size of actual packet %d\n", skb->len);
	priv->stats.rx_bytes += skb->len;
	priv->stats.rx_packets++;

	libertas_upload_rx_packet(priv, skb);

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_RX, "ret %d", ret);
	return ret;
}
