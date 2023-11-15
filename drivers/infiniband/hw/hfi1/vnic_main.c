// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2017 - 2020 Intel Corporation.
 */

/*
 * This file contains HFI1 support for VNIC functionality
 */

#include <linux/io.h>
#include <linux/if_vlan.h>

#include "vnic.h"
#include "netdev.h"

#define HFI_TX_TIMEOUT_MS 1000

#define HFI1_VNIC_RCV_Q_SIZE   1024

#define HFI1_VNIC_UP 0

static DEFINE_SPINLOCK(vport_cntr_lock);

#define SUM_GRP_COUNTERS(stats, qstats, x_grp) do {            \
		u64 *src64, *dst64;                            \
		for (src64 = &qstats->x_grp.unicast,           \
			dst64 = &stats->x_grp.unicast;         \
			dst64 <= &stats->x_grp.s_1519_max;) {  \
			*dst64++ += *src64++;                  \
		}                                              \
	} while (0)

#define VNIC_MASK (0xFF)
#define VNIC_ID(val) ((1ull << 24) | ((val) & VNIC_MASK))

/* hfi1_vnic_update_stats - update statistics */
static void hfi1_vnic_update_stats(struct hfi1_vnic_vport_info *vinfo,
				   struct opa_vnic_stats *stats)
{
	struct net_device *netdev = vinfo->netdev;
	u8 i;

	/* add tx counters on different queues */
	for (i = 0; i < vinfo->num_tx_q; i++) {
		struct opa_vnic_stats *qstats = &vinfo->stats[i];
		struct rtnl_link_stats64 *qnstats = &vinfo->stats[i].netstats;

		stats->netstats.tx_fifo_errors += qnstats->tx_fifo_errors;
		stats->netstats.tx_carrier_errors += qnstats->tx_carrier_errors;
		stats->tx_drop_state += qstats->tx_drop_state;
		stats->tx_dlid_zero += qstats->tx_dlid_zero;

		SUM_GRP_COUNTERS(stats, qstats, tx_grp);
		stats->netstats.tx_packets += qnstats->tx_packets;
		stats->netstats.tx_bytes += qnstats->tx_bytes;
	}

	/* add rx counters on different queues */
	for (i = 0; i < vinfo->num_rx_q; i++) {
		struct opa_vnic_stats *qstats = &vinfo->stats[i];
		struct rtnl_link_stats64 *qnstats = &vinfo->stats[i].netstats;

		stats->netstats.rx_fifo_errors += qnstats->rx_fifo_errors;
		stats->netstats.rx_nohandler += qnstats->rx_nohandler;
		stats->rx_drop_state += qstats->rx_drop_state;
		stats->rx_oversize += qstats->rx_oversize;
		stats->rx_runt += qstats->rx_runt;

		SUM_GRP_COUNTERS(stats, qstats, rx_grp);
		stats->netstats.rx_packets += qnstats->rx_packets;
		stats->netstats.rx_bytes += qnstats->rx_bytes;
	}

	stats->netstats.tx_errors = stats->netstats.tx_fifo_errors +
				    stats->netstats.tx_carrier_errors +
				    stats->tx_drop_state + stats->tx_dlid_zero;
	stats->netstats.tx_dropped = stats->netstats.tx_errors;

	stats->netstats.rx_errors = stats->netstats.rx_fifo_errors +
				    stats->netstats.rx_nohandler +
				    stats->rx_drop_state + stats->rx_oversize +
				    stats->rx_runt;
	stats->netstats.rx_dropped = stats->netstats.rx_errors;

	netdev->stats.tx_packets = stats->netstats.tx_packets;
	netdev->stats.tx_bytes = stats->netstats.tx_bytes;
	netdev->stats.tx_fifo_errors = stats->netstats.tx_fifo_errors;
	netdev->stats.tx_carrier_errors = stats->netstats.tx_carrier_errors;
	netdev->stats.tx_errors = stats->netstats.tx_errors;
	netdev->stats.tx_dropped = stats->netstats.tx_dropped;

	netdev->stats.rx_packets = stats->netstats.rx_packets;
	netdev->stats.rx_bytes = stats->netstats.rx_bytes;
	netdev->stats.rx_fifo_errors = stats->netstats.rx_fifo_errors;
	netdev->stats.multicast = stats->rx_grp.mcastbcast;
	netdev->stats.rx_length_errors = stats->rx_oversize + stats->rx_runt;
	netdev->stats.rx_errors = stats->netstats.rx_errors;
	netdev->stats.rx_dropped = stats->netstats.rx_dropped;
}

/* update_len_counters - update pkt's len histogram counters */
static inline void update_len_counters(struct opa_vnic_grp_stats *grp,
				       int len)
{
	/* account for 4 byte FCS */
	if (len >= 1515)
		grp->s_1519_max++;
	else if (len >= 1020)
		grp->s_1024_1518++;
	else if (len >= 508)
		grp->s_512_1023++;
	else if (len >= 252)
		grp->s_256_511++;
	else if (len >= 124)
		grp->s_128_255++;
	else if (len >= 61)
		grp->s_65_127++;
	else
		grp->s_64++;
}

/* hfi1_vnic_update_tx_counters - update transmit counters */
static void hfi1_vnic_update_tx_counters(struct hfi1_vnic_vport_info *vinfo,
					 u8 q_idx, struct sk_buff *skb, int err)
{
	struct ethhdr *mac_hdr = (struct ethhdr *)skb_mac_header(skb);
	struct opa_vnic_stats *stats = &vinfo->stats[q_idx];
	struct opa_vnic_grp_stats *tx_grp = &stats->tx_grp;
	u16 vlan_tci;

	stats->netstats.tx_packets++;
	stats->netstats.tx_bytes += skb->len + ETH_FCS_LEN;

	update_len_counters(tx_grp, skb->len);

	/* rest of the counts are for good packets only */
	if (unlikely(err))
		return;

	if (is_multicast_ether_addr(mac_hdr->h_dest))
		tx_grp->mcastbcast++;
	else
		tx_grp->unicast++;

	if (!__vlan_get_tag(skb, &vlan_tci))
		tx_grp->vlan++;
	else
		tx_grp->untagged++;
}

/* hfi1_vnic_update_rx_counters - update receive counters */
static void hfi1_vnic_update_rx_counters(struct hfi1_vnic_vport_info *vinfo,
					 u8 q_idx, struct sk_buff *skb, int err)
{
	struct ethhdr *mac_hdr = (struct ethhdr *)skb->data;
	struct opa_vnic_stats *stats = &vinfo->stats[q_idx];
	struct opa_vnic_grp_stats *rx_grp = &stats->rx_grp;
	u16 vlan_tci;

	stats->netstats.rx_packets++;
	stats->netstats.rx_bytes += skb->len + ETH_FCS_LEN;

	update_len_counters(rx_grp, skb->len);

	/* rest of the counts are for good packets only */
	if (unlikely(err))
		return;

	if (is_multicast_ether_addr(mac_hdr->h_dest))
		rx_grp->mcastbcast++;
	else
		rx_grp->unicast++;

	if (!__vlan_get_tag(skb, &vlan_tci))
		rx_grp->vlan++;
	else
		rx_grp->untagged++;
}

/* This function is overloaded for opa_vnic specific implementation */
static void hfi1_vnic_get_stats64(struct net_device *netdev,
				  struct rtnl_link_stats64 *stats)
{
	struct opa_vnic_stats *vstats = (struct opa_vnic_stats *)stats;
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);

	hfi1_vnic_update_stats(vinfo, vstats);
}

static u64 create_bypass_pbc(u32 vl, u32 dw_len)
{
	u64 pbc;

	pbc = ((u64)PBC_IHCRC_NONE << PBC_INSERT_HCRC_SHIFT)
		| PBC_INSERT_BYPASS_ICRC | PBC_CREDIT_RETURN
		| PBC_PACKET_BYPASS
		| ((vl & PBC_VL_MASK) << PBC_VL_SHIFT)
		| (dw_len & PBC_LENGTH_DWS_MASK) << PBC_LENGTH_DWS_SHIFT;

	return pbc;
}

/* hfi1_vnic_maybe_stop_tx - stop tx queue if required */
static void hfi1_vnic_maybe_stop_tx(struct hfi1_vnic_vport_info *vinfo,
				    u8 q_idx)
{
	netif_stop_subqueue(vinfo->netdev, q_idx);
	if (!hfi1_vnic_sdma_write_avail(vinfo, q_idx))
		return;

	netif_start_subqueue(vinfo->netdev, q_idx);
}

static netdev_tx_t hfi1_netdev_start_xmit(struct sk_buff *skb,
					  struct net_device *netdev)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);
	u8 pad_len, q_idx = skb->queue_mapping;
	struct hfi1_devdata *dd = vinfo->dd;
	struct opa_vnic_skb_mdata *mdata;
	u32 pkt_len, total_len;
	int err = -EINVAL;
	u64 pbc;

	v_dbg("xmit: queue %d skb len %d\n", q_idx, skb->len);
	if (unlikely(!netif_oper_up(netdev))) {
		vinfo->stats[q_idx].tx_drop_state++;
		goto tx_finish;
	}

	/* take out meta data */
	mdata = (struct opa_vnic_skb_mdata *)skb->data;
	skb_pull(skb, sizeof(*mdata));
	if (unlikely(mdata->flags & OPA_VNIC_SKB_MDATA_ENCAP_ERR)) {
		vinfo->stats[q_idx].tx_dlid_zero++;
		goto tx_finish;
	}

	/* add tail padding (for 8 bytes size alignment) and icrc */
	pad_len = -(skb->len + OPA_VNIC_ICRC_TAIL_LEN) & 0x7;
	pad_len += OPA_VNIC_ICRC_TAIL_LEN;

	/*
	 * pkt_len is how much data we have to write, includes header and data.
	 * total_len is length of the packet in Dwords plus the PBC should not
	 * include the CRC.
	 */
	pkt_len = (skb->len + pad_len) >> 2;
	total_len = pkt_len + 2; /* PBC + packet */

	pbc = create_bypass_pbc(mdata->vl, total_len);

	skb_get(skb);
	v_dbg("pbc 0x%016llX len %d pad_len %d\n", pbc, skb->len, pad_len);
	err = dd->process_vnic_dma_send(dd, q_idx, vinfo, skb, pbc, pad_len);
	if (unlikely(err)) {
		if (err == -ENOMEM)
			vinfo->stats[q_idx].netstats.tx_fifo_errors++;
		else if (err != -EBUSY)
			vinfo->stats[q_idx].netstats.tx_carrier_errors++;
	}
	/* remove the header before updating tx counters */
	skb_pull(skb, OPA_VNIC_HDR_LEN);

	if (unlikely(err == -EBUSY)) {
		hfi1_vnic_maybe_stop_tx(vinfo, q_idx);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_BUSY;
	}

tx_finish:
	/* update tx counters */
	hfi1_vnic_update_tx_counters(vinfo, q_idx, skb, err);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static u16 hfi1_vnic_select_queue(struct net_device *netdev,
				  struct sk_buff *skb,
				  struct net_device *sb_dev)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);
	struct opa_vnic_skb_mdata *mdata;
	struct sdma_engine *sde;

	mdata = (struct opa_vnic_skb_mdata *)skb->data;
	sde = sdma_select_engine_vl(vinfo->dd, mdata->entropy, mdata->vl);
	return sde->this_idx;
}

/* hfi1_vnic_decap_skb - strip OPA header from the skb (ethernet) packet */
static inline int hfi1_vnic_decap_skb(struct hfi1_vnic_rx_queue *rxq,
				      struct sk_buff *skb)
{
	struct hfi1_vnic_vport_info *vinfo = rxq->vinfo;
	int max_len = vinfo->netdev->mtu + VLAN_ETH_HLEN;
	int rc = -EFAULT;

	skb_pull(skb, OPA_VNIC_HDR_LEN);

	/* Validate Packet length */
	if (unlikely(skb->len > max_len))
		vinfo->stats[rxq->idx].rx_oversize++;
	else if (unlikely(skb->len < ETH_ZLEN))
		vinfo->stats[rxq->idx].rx_runt++;
	else
		rc = 0;
	return rc;
}

static struct hfi1_vnic_vport_info *get_vnic_port(struct hfi1_devdata *dd,
						  int vesw_id)
{
	int vnic_id = VNIC_ID(vesw_id);

	return hfi1_netdev_get_data(dd, vnic_id);
}

static struct hfi1_vnic_vport_info *get_first_vnic_port(struct hfi1_devdata *dd)
{
	struct hfi1_vnic_vport_info *vinfo;
	int next_id = VNIC_ID(0);

	vinfo = hfi1_netdev_get_first_data(dd, &next_id);

	if (next_id > VNIC_ID(VNIC_MASK))
		return NULL;

	return vinfo;
}

void hfi1_vnic_bypass_rcv(struct hfi1_packet *packet)
{
	struct hfi1_devdata *dd = packet->rcd->dd;
	struct hfi1_vnic_vport_info *vinfo = NULL;
	struct hfi1_vnic_rx_queue *rxq;
	struct sk_buff *skb;
	int l4_type, vesw_id = -1, rc;
	u8 q_idx;
	unsigned char *pad_info;

	l4_type = hfi1_16B_get_l4(packet->ebuf);
	if (likely(l4_type == OPA_16B_L4_ETHR)) {
		vesw_id = HFI1_VNIC_GET_VESWID(packet->ebuf);
		vinfo = get_vnic_port(dd, vesw_id);

		/*
		 * In case of invalid vesw id, count the error on
		 * the first available vport.
		 */
		if (unlikely(!vinfo)) {
			struct hfi1_vnic_vport_info *vinfo_tmp;

			vinfo_tmp = get_first_vnic_port(dd);
			if (vinfo_tmp) {
				spin_lock(&vport_cntr_lock);
				vinfo_tmp->stats[0].netstats.rx_nohandler++;
				spin_unlock(&vport_cntr_lock);
			}
		}
	}

	if (unlikely(!vinfo)) {
		dd_dev_warn(dd, "vnic rcv err: l4 %d vesw id %d ctx %d\n",
			    l4_type, vesw_id, packet->rcd->ctxt);
		return;
	}

	q_idx = packet->rcd->vnic_q_idx;
	rxq = &vinfo->rxq[q_idx];
	if (unlikely(!netif_oper_up(vinfo->netdev))) {
		vinfo->stats[q_idx].rx_drop_state++;
		return;
	}

	skb = netdev_alloc_skb(vinfo->netdev, packet->tlen);
	if (unlikely(!skb)) {
		vinfo->stats[q_idx].netstats.rx_fifo_errors++;
		return;
	}

	memcpy(skb->data, packet->ebuf, packet->tlen);
	skb_put(skb, packet->tlen);

	pad_info = skb->data + skb->len - 1;
	skb_trim(skb, (skb->len - OPA_VNIC_ICRC_TAIL_LEN -
		       ((*pad_info) & 0x7)));

	rc = hfi1_vnic_decap_skb(rxq, skb);

	/* update rx counters */
	hfi1_vnic_update_rx_counters(vinfo, rxq->idx, skb, rc);
	if (unlikely(rc)) {
		dev_kfree_skb_any(skb);
		return;
	}

	skb_checksum_none_assert(skb);
	skb->protocol = eth_type_trans(skb, rxq->netdev);

	napi_gro_receive(&rxq->napi, skb);
}

static int hfi1_vnic_up(struct hfi1_vnic_vport_info *vinfo)
{
	struct hfi1_devdata *dd = vinfo->dd;
	struct net_device *netdev = vinfo->netdev;
	int rc;

	/* ensure virtual eth switch id is valid */
	if (!vinfo->vesw_id)
		return -EINVAL;

	rc = hfi1_netdev_add_data(dd, VNIC_ID(vinfo->vesw_id), vinfo);
	if (rc < 0)
		return rc;

	rc = hfi1_netdev_rx_init(dd);
	if (rc)
		goto err_remove;

	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);
	set_bit(HFI1_VNIC_UP, &vinfo->flags);

	return 0;

err_remove:
	hfi1_netdev_remove_data(dd, VNIC_ID(vinfo->vesw_id));
	return rc;
}

static void hfi1_vnic_down(struct hfi1_vnic_vport_info *vinfo)
{
	struct hfi1_devdata *dd = vinfo->dd;

	clear_bit(HFI1_VNIC_UP, &vinfo->flags);
	netif_carrier_off(vinfo->netdev);
	netif_tx_disable(vinfo->netdev);
	hfi1_netdev_remove_data(dd, VNIC_ID(vinfo->vesw_id));

	hfi1_netdev_rx_destroy(dd);
}

static int hfi1_netdev_open(struct net_device *netdev)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);
	int rc;

	mutex_lock(&vinfo->lock);
	rc = hfi1_vnic_up(vinfo);
	mutex_unlock(&vinfo->lock);
	return rc;
}

static int hfi1_netdev_close(struct net_device *netdev)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);

	mutex_lock(&vinfo->lock);
	if (test_bit(HFI1_VNIC_UP, &vinfo->flags))
		hfi1_vnic_down(vinfo);
	mutex_unlock(&vinfo->lock);
	return 0;
}

static int hfi1_vnic_init(struct hfi1_vnic_vport_info *vinfo)
{
	struct hfi1_devdata *dd = vinfo->dd;
	int rc = 0;

	mutex_lock(&hfi1_mutex);
	if (!dd->vnic_num_vports) {
		rc = hfi1_vnic_txreq_init(dd);
		if (rc)
			goto txreq_fail;
	}

	rc = hfi1_netdev_rx_init(dd);
	if (rc) {
		dd_dev_err(dd, "Unable to initialize netdev contexts\n");
		goto alloc_fail;
	}

	hfi1_init_vnic_rsm(dd);

	dd->vnic_num_vports++;
	hfi1_vnic_sdma_init(vinfo);

alloc_fail:
	if (!dd->vnic_num_vports)
		hfi1_vnic_txreq_deinit(dd);
txreq_fail:
	mutex_unlock(&hfi1_mutex);
	return rc;
}

static void hfi1_vnic_deinit(struct hfi1_vnic_vport_info *vinfo)
{
	struct hfi1_devdata *dd = vinfo->dd;

	mutex_lock(&hfi1_mutex);
	if (--dd->vnic_num_vports == 0) {
		hfi1_deinit_vnic_rsm(dd);
		hfi1_vnic_txreq_deinit(dd);
	}
	mutex_unlock(&hfi1_mutex);
	hfi1_netdev_rx_destroy(dd);
}

static void hfi1_vnic_set_vesw_id(struct net_device *netdev, int id)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);
	bool reopen = false;

	/*
	 * If vesw_id is being changed, and if the vnic port is up,
	 * reset the vnic port to ensure new vesw_id gets picked up
	 */
	if (id != vinfo->vesw_id) {
		mutex_lock(&vinfo->lock);
		if (test_bit(HFI1_VNIC_UP, &vinfo->flags)) {
			hfi1_vnic_down(vinfo);
			reopen = true;
		}

		vinfo->vesw_id = id;
		if (reopen)
			hfi1_vnic_up(vinfo);

		mutex_unlock(&vinfo->lock);
	}
}

/* netdev ops */
static const struct net_device_ops hfi1_netdev_ops = {
	.ndo_open = hfi1_netdev_open,
	.ndo_stop = hfi1_netdev_close,
	.ndo_start_xmit = hfi1_netdev_start_xmit,
	.ndo_select_queue = hfi1_vnic_select_queue,
	.ndo_get_stats64 = hfi1_vnic_get_stats64,
};

static void hfi1_vnic_free_rn(struct net_device *netdev)
{
	struct hfi1_vnic_vport_info *vinfo = opa_vnic_dev_priv(netdev);

	hfi1_vnic_deinit(vinfo);
	mutex_destroy(&vinfo->lock);
	free_netdev(netdev);
}

struct net_device *hfi1_vnic_alloc_rn(struct ib_device *device,
				      u32 port_num,
				      enum rdma_netdev_t type,
				      const char *name,
				      unsigned char name_assign_type,
				      void (*setup)(struct net_device *))
{
	struct hfi1_devdata *dd = dd_from_ibdev(device);
	struct hfi1_vnic_vport_info *vinfo;
	struct net_device *netdev;
	struct rdma_netdev *rn;
	int i, size, rc;

	if (!dd->num_netdev_contexts)
		return ERR_PTR(-ENOMEM);

	if (!port_num || (port_num > dd->num_pports))
		return ERR_PTR(-EINVAL);

	if (type != RDMA_NETDEV_OPA_VNIC)
		return ERR_PTR(-EOPNOTSUPP);

	size = sizeof(struct opa_vnic_rdma_netdev) + sizeof(*vinfo);
	netdev = alloc_netdev_mqs(size, name, name_assign_type, setup,
				  chip_sdma_engines(dd),
				  dd->num_netdev_contexts);
	if (!netdev)
		return ERR_PTR(-ENOMEM);

	rn = netdev_priv(netdev);
	vinfo = opa_vnic_dev_priv(netdev);
	vinfo->dd = dd;
	vinfo->num_tx_q = chip_sdma_engines(dd);
	vinfo->num_rx_q = dd->num_netdev_contexts;
	vinfo->netdev = netdev;
	rn->free_rdma_netdev = hfi1_vnic_free_rn;
	rn->set_id = hfi1_vnic_set_vesw_id;

	netdev->features = NETIF_F_HIGHDMA | NETIF_F_SG;
	netdev->hw_features = netdev->features;
	netdev->vlan_features = netdev->features;
	netdev->watchdog_timeo = msecs_to_jiffies(HFI_TX_TIMEOUT_MS);
	netdev->netdev_ops = &hfi1_netdev_ops;
	mutex_init(&vinfo->lock);

	for (i = 0; i < vinfo->num_rx_q; i++) {
		struct hfi1_vnic_rx_queue *rxq = &vinfo->rxq[i];

		rxq->idx = i;
		rxq->vinfo = vinfo;
		rxq->netdev = netdev;
	}

	rc = hfi1_vnic_init(vinfo);
	if (rc)
		goto init_fail;

	return netdev;
init_fail:
	mutex_destroy(&vinfo->lock);
	free_netdev(netdev);
	return ERR_PTR(rc);
}
