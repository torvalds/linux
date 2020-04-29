/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/udp_tunnel.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>

#include <linux/qed/qed_if.h>
#include "qede.h"

#define QEDE_FILTER_PRINT_MAX_LEN	(64)
struct qede_arfs_tuple {
	union {
		__be32 src_ipv4;
		struct in6_addr src_ipv6;
	};
	union {
		__be32 dst_ipv4;
		struct in6_addr dst_ipv6;
	};
	__be16  src_port;
	__be16  dst_port;
	__be16  eth_proto;
	u8      ip_proto;

	/* Describe filtering mode needed for this kind of filter */
	enum qed_filter_config_mode mode;

	/* Used to compare new/old filters. Return true if IPs match */
	bool (*ip_comp)(struct qede_arfs_tuple *a, struct qede_arfs_tuple *b);

	/* Given an address into ethhdr build a header from tuple info */
	void (*build_hdr)(struct qede_arfs_tuple *t, void *header);

	/* Stringify the tuple for a print into the provided buffer */
	void (*stringify)(struct qede_arfs_tuple *t, void *buffer);
};

struct qede_arfs_fltr_node {
#define QEDE_FLTR_VALID	 0
	unsigned long state;

	/* pointer to aRFS packet buffer */
	void *data;

	/* dma map address of aRFS packet buffer */
	dma_addr_t mapping;

	/* length of aRFS packet buffer */
	int buf_len;

	/* tuples to hold from aRFS packet buffer */
	struct qede_arfs_tuple tuple;

	u32 flow_id;
	u64 sw_id;
	u16 rxq_id;
	u16 next_rxq_id;
	u8 vfid;
	bool filter_op;
	bool used;
	u8 fw_rc;
	bool b_is_drop;
	struct hlist_node node;
};

struct qede_arfs {
#define QEDE_ARFS_BUCKET_HEAD(edev, idx) (&(edev)->arfs->arfs_hl_head[idx])
#define QEDE_ARFS_POLL_COUNT	100
#define QEDE_RFS_FLW_BITSHIFT	(4)
#define QEDE_RFS_FLW_MASK	((1 << QEDE_RFS_FLW_BITSHIFT) - 1)
	struct hlist_head	arfs_hl_head[1 << QEDE_RFS_FLW_BITSHIFT];

	/* lock for filter list access */
	spinlock_t		arfs_list_lock;
	unsigned long		*arfs_fltr_bmap;
	int			filter_count;

	/* Currently configured filtering mode */
	enum qed_filter_config_mode mode;
};

static void qede_configure_arfs_fltr(struct qede_dev *edev,
				     struct qede_arfs_fltr_node *n,
				     u16 rxq_id, bool add_fltr)
{
	const struct qed_eth_ops *op = edev->ops;
	struct qed_ntuple_filter_params params;

	if (n->used)
		return;

	memset(&params, 0, sizeof(params));

	params.addr = n->mapping;
	params.length = n->buf_len;
	params.qid = rxq_id;
	params.b_is_add = add_fltr;
	params.b_is_drop = n->b_is_drop;

	if (n->vfid) {
		params.b_is_vf = true;
		params.vf_id = n->vfid - 1;
	}

	if (n->tuple.stringify) {
		char tuple_buffer[QEDE_FILTER_PRINT_MAX_LEN];

		n->tuple.stringify(&n->tuple, tuple_buffer);
		DP_VERBOSE(edev, NETIF_MSG_RX_STATUS,
			   "%s sw_id[0x%llx]: %s [vf %u queue %d]\n",
			   add_fltr ? "Adding" : "Deleting",
			   n->sw_id, tuple_buffer, n->vfid, rxq_id);
	}

	n->used = true;
	n->filter_op = add_fltr;
	op->ntuple_filter_config(edev->cdev, n, &params);
}

static void
qede_free_arfs_filter(struct qede_dev *edev,  struct qede_arfs_fltr_node *fltr)
{
	kfree(fltr->data);

	if (fltr->sw_id < QEDE_RFS_MAX_FLTR)
		clear_bit(fltr->sw_id, edev->arfs->arfs_fltr_bmap);

	kfree(fltr);
}

static int
qede_enqueue_fltr_and_config_searcher(struct qede_dev *edev,
				      struct qede_arfs_fltr_node *fltr,
				      u16 bucket_idx)
{
	fltr->mapping = dma_map_single(&edev->pdev->dev, fltr->data,
				       fltr->buf_len, DMA_TO_DEVICE);
	if (dma_mapping_error(&edev->pdev->dev, fltr->mapping)) {
		DP_NOTICE(edev, "Failed to map DMA memory for rule\n");
		qede_free_arfs_filter(edev, fltr);
		return -ENOMEM;
	}

	INIT_HLIST_NODE(&fltr->node);
	hlist_add_head(&fltr->node,
		       QEDE_ARFS_BUCKET_HEAD(edev, bucket_idx));

	edev->arfs->filter_count++;
	if (edev->arfs->filter_count == 1 &&
	    edev->arfs->mode == QED_FILTER_CONFIG_MODE_DISABLE) {
		edev->ops->configure_arfs_searcher(edev->cdev,
						   fltr->tuple.mode);
		edev->arfs->mode = fltr->tuple.mode;
	}

	return 0;
}

static void
qede_dequeue_fltr_and_config_searcher(struct qede_dev *edev,
				      struct qede_arfs_fltr_node *fltr)
{
	hlist_del(&fltr->node);
	dma_unmap_single(&edev->pdev->dev, fltr->mapping,
			 fltr->buf_len, DMA_TO_DEVICE);

	qede_free_arfs_filter(edev, fltr);

	edev->arfs->filter_count--;
	if (!edev->arfs->filter_count &&
	    edev->arfs->mode != QED_FILTER_CONFIG_MODE_DISABLE) {
		enum qed_filter_config_mode mode;

		mode = QED_FILTER_CONFIG_MODE_DISABLE;
		edev->ops->configure_arfs_searcher(edev->cdev, mode);
		edev->arfs->mode = QED_FILTER_CONFIG_MODE_DISABLE;
	}
}

void qede_arfs_filter_op(void *dev, void *filter, u8 fw_rc)
{
	struct qede_arfs_fltr_node *fltr = filter;
	struct qede_dev *edev = dev;

	fltr->fw_rc = fw_rc;

	if (fw_rc) {
		DP_NOTICE(edev,
			  "Failed arfs filter configuration fw_rc=%d, flow_id=%d, sw_id=0x%llx, src_port=%d, dst_port=%d, rxq=%d\n",
			  fw_rc, fltr->flow_id, fltr->sw_id,
			  ntohs(fltr->tuple.src_port),
			  ntohs(fltr->tuple.dst_port), fltr->rxq_id);

		spin_lock_bh(&edev->arfs->arfs_list_lock);

		fltr->used = false;
		clear_bit(QEDE_FLTR_VALID, &fltr->state);

		spin_unlock_bh(&edev->arfs->arfs_list_lock);
		return;
	}

	spin_lock_bh(&edev->arfs->arfs_list_lock);

	fltr->used = false;

	if (fltr->filter_op) {
		set_bit(QEDE_FLTR_VALID, &fltr->state);
		if (fltr->rxq_id != fltr->next_rxq_id)
			qede_configure_arfs_fltr(edev, fltr, fltr->rxq_id,
						 false);
	} else {
		clear_bit(QEDE_FLTR_VALID, &fltr->state);
		if (fltr->rxq_id != fltr->next_rxq_id) {
			fltr->rxq_id = fltr->next_rxq_id;
			qede_configure_arfs_fltr(edev, fltr,
						 fltr->rxq_id, true);
		}
	}

	spin_unlock_bh(&edev->arfs->arfs_list_lock);
}

/* Should be called while qede_lock is held */
void qede_process_arfs_filters(struct qede_dev *edev, bool free_fltr)
{
	int i;

	for (i = 0; i <= QEDE_RFS_FLW_MASK; i++) {
		struct hlist_node *temp;
		struct hlist_head *head;
		struct qede_arfs_fltr_node *fltr;

		head = &edev->arfs->arfs_hl_head[i];

		hlist_for_each_entry_safe(fltr, temp, head, node) {
			bool del = false;

			if (edev->state != QEDE_STATE_OPEN)
				del = true;

			spin_lock_bh(&edev->arfs->arfs_list_lock);

			if ((!test_bit(QEDE_FLTR_VALID, &fltr->state) &&
			     !fltr->used) || free_fltr) {
				qede_dequeue_fltr_and_config_searcher(edev,
								      fltr);
			} else {
				bool flow_exp = false;
#ifdef CONFIG_RFS_ACCEL
				flow_exp = rps_may_expire_flow(edev->ndev,
							       fltr->rxq_id,
							       fltr->flow_id,
							       fltr->sw_id);
#endif
				if ((flow_exp || del) && !free_fltr)
					qede_configure_arfs_fltr(edev, fltr,
								 fltr->rxq_id,
								 false);
			}

			spin_unlock_bh(&edev->arfs->arfs_list_lock);
		}
	}

#ifdef CONFIG_RFS_ACCEL
	spin_lock_bh(&edev->arfs->arfs_list_lock);

	if (edev->arfs->filter_count) {
		set_bit(QEDE_SP_ARFS_CONFIG, &edev->sp_flags);
		schedule_delayed_work(&edev->sp_task,
				      QEDE_SP_TASK_POLL_DELAY);
	}

	spin_unlock_bh(&edev->arfs->arfs_list_lock);
#endif
}

/* This function waits until all aRFS filters get deleted and freed.
 * On timeout it frees all filters forcefully.
 */
void qede_poll_for_freeing_arfs_filters(struct qede_dev *edev)
{
	int count = QEDE_ARFS_POLL_COUNT;

	while (count) {
		qede_process_arfs_filters(edev, false);

		if (!edev->arfs->filter_count)
			break;

		msleep(100);
		count--;
	}

	if (!count) {
		DP_NOTICE(edev, "Timeout in polling for arfs filter free\n");

		/* Something is terribly wrong, free forcefully */
		qede_process_arfs_filters(edev, true);
	}
}

int qede_alloc_arfs(struct qede_dev *edev)
{
	int i;

	edev->arfs = vzalloc(sizeof(*edev->arfs));
	if (!edev->arfs)
		return -ENOMEM;

	spin_lock_init(&edev->arfs->arfs_list_lock);

	for (i = 0; i <= QEDE_RFS_FLW_MASK; i++)
		INIT_HLIST_HEAD(QEDE_ARFS_BUCKET_HEAD(edev, i));

	edev->arfs->arfs_fltr_bmap =
		vzalloc(array_size(sizeof(long),
				   BITS_TO_LONGS(QEDE_RFS_MAX_FLTR)));
	if (!edev->arfs->arfs_fltr_bmap) {
		vfree(edev->arfs);
		edev->arfs = NULL;
		return -ENOMEM;
	}

#ifdef CONFIG_RFS_ACCEL
	edev->ndev->rx_cpu_rmap = alloc_irq_cpu_rmap(QEDE_RSS_COUNT(edev));
	if (!edev->ndev->rx_cpu_rmap) {
		vfree(edev->arfs->arfs_fltr_bmap);
		edev->arfs->arfs_fltr_bmap = NULL;
		vfree(edev->arfs);
		edev->arfs = NULL;
		return -ENOMEM;
	}
#endif
	return 0;
}

void qede_free_arfs(struct qede_dev *edev)
{
	if (!edev->arfs)
		return;

#ifdef CONFIG_RFS_ACCEL
	if (edev->ndev->rx_cpu_rmap)
		free_irq_cpu_rmap(edev->ndev->rx_cpu_rmap);

	edev->ndev->rx_cpu_rmap = NULL;
#endif
	vfree(edev->arfs->arfs_fltr_bmap);
	edev->arfs->arfs_fltr_bmap = NULL;
	vfree(edev->arfs);
	edev->arfs = NULL;
}

#ifdef CONFIG_RFS_ACCEL
static bool qede_compare_ip_addr(struct qede_arfs_fltr_node *tpos,
				 const struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP)) {
		if (tpos->tuple.src_ipv4 == ip_hdr(skb)->saddr &&
		    tpos->tuple.dst_ipv4 == ip_hdr(skb)->daddr)
			return true;
		else
			return false;
	} else {
		struct in6_addr *src = &tpos->tuple.src_ipv6;
		u8 size = sizeof(struct in6_addr);

		if (!memcmp(src, &ipv6_hdr(skb)->saddr, size) &&
		    !memcmp(&tpos->tuple.dst_ipv6, &ipv6_hdr(skb)->daddr, size))
			return true;
		else
			return false;
	}
}

static struct qede_arfs_fltr_node *
qede_arfs_htbl_key_search(struct hlist_head *h, const struct sk_buff *skb,
			  __be16 src_port, __be16 dst_port, u8 ip_proto)
{
	struct qede_arfs_fltr_node *tpos;

	hlist_for_each_entry(tpos, h, node)
		if (tpos->tuple.ip_proto == ip_proto &&
		    tpos->tuple.eth_proto == skb->protocol &&
		    qede_compare_ip_addr(tpos, skb) &&
		    tpos->tuple.src_port == src_port &&
		    tpos->tuple.dst_port == dst_port)
			return tpos;

	return NULL;
}

static struct qede_arfs_fltr_node *
qede_alloc_filter(struct qede_dev *edev, int min_hlen)
{
	struct qede_arfs_fltr_node *n;
	int bit_id;

	bit_id = find_first_zero_bit(edev->arfs->arfs_fltr_bmap,
				     QEDE_RFS_MAX_FLTR);

	if (bit_id >= QEDE_RFS_MAX_FLTR)
		return NULL;

	n = kzalloc(sizeof(*n), GFP_ATOMIC);
	if (!n)
		return NULL;

	n->data = kzalloc(min_hlen, GFP_ATOMIC);
	if (!n->data) {
		kfree(n);
		return NULL;
	}

	n->sw_id = (u16)bit_id;
	set_bit(bit_id, edev->arfs->arfs_fltr_bmap);
	return n;
}

int qede_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_arfs_fltr_node *n;
	int min_hlen, rc, tp_offset;
	struct ethhdr *eth;
	__be16 *ports;
	u16 tbl_idx;
	u8 ip_proto;

	if (skb->encapsulation)
		return -EPROTONOSUPPORT;

	if (skb->protocol != htons(ETH_P_IP) &&
	    skb->protocol != htons(ETH_P_IPV6))
		return -EPROTONOSUPPORT;

	if (skb->protocol == htons(ETH_P_IP)) {
		ip_proto = ip_hdr(skb)->protocol;
		tp_offset = sizeof(struct iphdr);
	} else {
		ip_proto = ipv6_hdr(skb)->nexthdr;
		tp_offset = sizeof(struct ipv6hdr);
	}

	if (ip_proto != IPPROTO_TCP && ip_proto != IPPROTO_UDP)
		return -EPROTONOSUPPORT;

	ports = (__be16 *)(skb->data + tp_offset);
	tbl_idx = skb_get_hash_raw(skb) & QEDE_RFS_FLW_MASK;

	spin_lock_bh(&edev->arfs->arfs_list_lock);

	n = qede_arfs_htbl_key_search(QEDE_ARFS_BUCKET_HEAD(edev, tbl_idx),
				      skb, ports[0], ports[1], ip_proto);
	if (n) {
		/* Filter match */
		n->next_rxq_id = rxq_index;

		if (test_bit(QEDE_FLTR_VALID, &n->state)) {
			if (n->rxq_id != rxq_index)
				qede_configure_arfs_fltr(edev, n, n->rxq_id,
							 false);
		} else {
			if (!n->used) {
				n->rxq_id = rxq_index;
				qede_configure_arfs_fltr(edev, n, n->rxq_id,
							 true);
			}
		}

		rc = n->sw_id;
		goto ret_unlock;
	}

	min_hlen = ETH_HLEN + skb_headlen(skb);

	n = qede_alloc_filter(edev, min_hlen);
	if (!n) {
		rc = -ENOMEM;
		goto ret_unlock;
	}

	n->buf_len = min_hlen;
	n->rxq_id = rxq_index;
	n->next_rxq_id = rxq_index;
	n->tuple.src_port = ports[0];
	n->tuple.dst_port = ports[1];
	n->flow_id = flow_id;

	if (skb->protocol == htons(ETH_P_IP)) {
		n->tuple.src_ipv4 = ip_hdr(skb)->saddr;
		n->tuple.dst_ipv4 = ip_hdr(skb)->daddr;
	} else {
		memcpy(&n->tuple.src_ipv6, &ipv6_hdr(skb)->saddr,
		       sizeof(struct in6_addr));
		memcpy(&n->tuple.dst_ipv6, &ipv6_hdr(skb)->daddr,
		       sizeof(struct in6_addr));
	}

	eth = (struct ethhdr *)n->data;
	eth->h_proto = skb->protocol;
	n->tuple.eth_proto = skb->protocol;
	n->tuple.ip_proto = ip_proto;
	n->tuple.mode = QED_FILTER_CONFIG_MODE_5_TUPLE;
	memcpy(n->data + ETH_HLEN, skb->data, skb_headlen(skb));

	rc = qede_enqueue_fltr_and_config_searcher(edev, n, tbl_idx);
	if (rc)
		goto ret_unlock;

	qede_configure_arfs_fltr(edev, n, n->rxq_id, true);

	spin_unlock_bh(&edev->arfs->arfs_list_lock);

	set_bit(QEDE_SP_ARFS_CONFIG, &edev->sp_flags);
	schedule_delayed_work(&edev->sp_task, 0);

	return n->sw_id;

ret_unlock:
	spin_unlock_bh(&edev->arfs->arfs_list_lock);
	return rc;
}
#endif

void qede_udp_ports_update(void *dev, u16 vxlan_port, u16 geneve_port)
{
	struct qede_dev *edev = dev;

	if (edev->vxlan_dst_port != vxlan_port)
		edev->vxlan_dst_port = 0;

	if (edev->geneve_dst_port != geneve_port)
		edev->geneve_dst_port = 0;
}

void qede_force_mac(void *dev, u8 *mac, bool forced)
{
	struct qede_dev *edev = dev;

	__qede_lock(edev);

	if (!is_valid_ether_addr(mac)) {
		__qede_unlock(edev);
		return;
	}

	ether_addr_copy(edev->ndev->dev_addr, mac);
	__qede_unlock(edev);
}

void qede_fill_rss_params(struct qede_dev *edev,
			  struct qed_update_vport_rss_params *rss, u8 *update)
{
	bool need_reset = false;
	int i;

	if (QEDE_RSS_COUNT(edev) <= 1) {
		memset(rss, 0, sizeof(*rss));
		*update = 0;
		return;
	}

	/* Need to validate current RSS config uses valid entries */
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		if (edev->rss_ind_table[i] >= QEDE_RSS_COUNT(edev)) {
			need_reset = true;
			break;
		}
	}

	if (!(edev->rss_params_inited & QEDE_RSS_INDIR_INITED) || need_reset) {
		for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
			u16 indir_val, val;

			val = QEDE_RSS_COUNT(edev);
			indir_val = ethtool_rxfh_indir_default(i, val);
			edev->rss_ind_table[i] = indir_val;
		}
		edev->rss_params_inited |= QEDE_RSS_INDIR_INITED;
	}

	/* Now that we have the queue-indirection, prepare the handles */
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		u16 idx = QEDE_RX_QUEUE_IDX(edev, edev->rss_ind_table[i]);

		rss->rss_ind_table[i] = edev->fp_array[idx].rxq->handle;
	}

	if (!(edev->rss_params_inited & QEDE_RSS_KEY_INITED)) {
		netdev_rss_key_fill(edev->rss_key, sizeof(edev->rss_key));
		edev->rss_params_inited |= QEDE_RSS_KEY_INITED;
	}
	memcpy(rss->rss_key, edev->rss_key, sizeof(rss->rss_key));

	if (!(edev->rss_params_inited & QEDE_RSS_CAPS_INITED)) {
		edev->rss_caps = QED_RSS_IPV4 | QED_RSS_IPV6 |
		    QED_RSS_IPV4_TCP | QED_RSS_IPV6_TCP;
		edev->rss_params_inited |= QEDE_RSS_CAPS_INITED;
	}
	rss->rss_caps = edev->rss_caps;

	*update = 1;
}

static int qede_set_ucast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char mac[ETH_ALEN])
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.mac_valid = 1;
	ether_addr_copy(filter_cmd.filter.ucast.mac, mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

static int qede_set_ucast_rx_vlan(struct qede_dev *edev,
				  enum qed_filter_xcast_params_type opcode,
				  u16 vid)
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.vlan_valid = 1;
	filter_cmd.filter.ucast.vlan = vid;

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

static int qede_config_accept_any_vlan(struct qede_dev *edev, bool action)
{
	struct qed_update_vport_params *params;
	int rc;

	/* Proceed only if action actually needs to be performed */
	if (edev->accept_any_vlan == action)
		return 0;

	params = vzalloc(sizeof(*params));
	if (!params)
		return -ENOMEM;

	params->vport_id = 0;
	params->accept_any_vlan = action;
	params->update_accept_any_vlan_flg = 1;

	rc = edev->ops->vport_update(edev->cdev, params);
	if (rc) {
		DP_ERR(edev, "Failed to %s accept-any-vlan\n",
		       action ? "enable" : "disable");
	} else {
		DP_INFO(edev, "%s accept-any-vlan\n",
			action ? "enabled" : "disabled");
		edev->accept_any_vlan = action;
	}

	vfree(params);
	return 0;
}

int qede_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan, *tmp;
	int rc = 0;

	DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan 0x%04x\n", vid);

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan) {
		DP_INFO(edev, "Failed to allocate struct for vlan\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&vlan->list);
	vlan->vid = vid;
	vlan->configured = false;

	/* Verify vlan isn't already configured */
	list_for_each_entry(tmp, &edev->vlan_list, list) {
		if (tmp->vid == vlan->vid) {
			DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
				   "vlan already configured\n");
			kfree(vlan);
			return -EEXIST;
		}
	}

	/* If interface is down, cache this VLAN ID and return */
	__qede_lock(edev);
	if (edev->state != QEDE_STATE_OPEN) {
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, VLAN %d will be configured when interface is up\n",
			   vid);
		if (vid != 0)
			edev->non_configured_vlans++;
		list_add(&vlan->list, &edev->vlan_list);
		goto out;
	}

	/* Check for the filter limit.
	 * Note - vlan0 has a reserved filter and can be added without
	 * worrying about quota
	 */
	if ((edev->configured_vlans < edev->dev_info.num_vlan_filters) ||
	    (vlan->vid == 0)) {
		rc = qede_set_ucast_rx_vlan(edev,
					    QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %d\n",
			       vlan->vid);
			kfree(vlan);
			goto out;
		}
		vlan->configured = true;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0)
			edev->configured_vlans++;
	} else {
		/* Out of quota; Activate accept-any-VLAN mode */
		if (!edev->non_configured_vlans) {
			rc = qede_config_accept_any_vlan(edev, true);
			if (rc) {
				kfree(vlan);
				goto out;
			}
		}

		edev->non_configured_vlans++;
	}

	list_add(&vlan->list, &edev->vlan_list);

out:
	__qede_unlock(edev);
	return rc;
}

static void qede_del_vlan_from_list(struct qede_dev *edev,
				    struct qede_vlan *vlan)
{
	/* vlan0 filter isn't consuming out of our quota */
	if (vlan->vid != 0) {
		if (vlan->configured)
			edev->configured_vlans--;
		else
			edev->non_configured_vlans--;
	}

	list_del(&vlan->list);
	kfree(vlan);
}

int qede_configure_vlan_filters(struct qede_dev *edev)
{
	int rc = 0, real_rc = 0, accept_any_vlan = 0;
	struct qed_dev_eth_info *dev_info;
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return 0;

	dev_info = &edev->dev_info;

	/* Configure non-configured vlans */
	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (vlan->configured)
			continue;

		/* We have used all our credits, now enable accept_any_vlan */
		if ((vlan->vid != 0) &&
		    (edev->configured_vlans == dev_info->num_vlan_filters)) {
			accept_any_vlan = 1;
			continue;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan %d\n", vlan->vid);

		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %u\n",
			       vlan->vid);
			real_rc = rc;
			continue;
		}

		vlan->configured = true;
		/* vlan0 filter doesn't consume our VLAN filter's quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans--;
			edev->configured_vlans++;
		}
	}

	/* enable accept_any_vlan mode if we have more VLANs than credits,
	 * or remove accept_any_vlan mode if we've actually removed
	 * a non-configured vlan, and all remaining vlans are truly configured.
	 */

	if (accept_any_vlan)
		rc = qede_config_accept_any_vlan(edev, true);
	else if (!edev->non_configured_vlans)
		rc = qede_config_accept_any_vlan(edev, false);

	if (rc && !real_rc)
		real_rc = rc;

	return real_rc;
}

int qede_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan = NULL;
	int rc = 0;

	DP_VERBOSE(edev, NETIF_MSG_IFDOWN, "Removing vlan 0x%04x\n", vid);

	/* Find whether entry exists */
	__qede_lock(edev);
	list_for_each_entry(vlan, &edev->vlan_list, list)
		if (vlan->vid == vid)
			break;

	if (!vlan || (vlan->vid != vid)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Vlan isn't configured\n");
		goto out;
	}

	if (edev->state != QEDE_STATE_OPEN) {
		/* As interface is already down, we don't have a VPORT
		 * instance to remove vlan filter. So just update vlan list
		 */
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, removing VLAN from list only\n");
		qede_del_vlan_from_list(edev, vlan);
		goto out;
	}

	/* Remove vlan */
	if (vlan->configured) {
		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_DEL,
					    vid);
		if (rc) {
			DP_ERR(edev, "Failed to remove VLAN %d\n", vid);
			goto out;
		}
	}

	qede_del_vlan_from_list(edev, vlan);

	/* We have removed a VLAN - try to see if we can
	 * configure non-configured VLAN from the list.
	 */
	rc = qede_configure_vlan_filters(edev);

out:
	__qede_unlock(edev);
	return rc;
}

void qede_vlan_mark_nonconfigured(struct qede_dev *edev)
{
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return;

	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (!vlan->configured)
			continue;

		vlan->configured = false;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans++;
			edev->configured_vlans--;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "marked vlan %d as non-configured\n", vlan->vid);
	}

	edev->accept_any_vlan = false;
}

static void qede_set_features_reload(struct qede_dev *edev,
				     struct qede_reload_args *args)
{
	edev->ndev->features = args->u.features;
}

netdev_features_t qede_fix_features(struct net_device *dev,
				    netdev_features_t features)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (edev->xdp_prog || edev->ndev->mtu > PAGE_SIZE ||
	    !(features & NETIF_F_GRO))
		features &= ~NETIF_F_GRO_HW;

	return features;
}

int qede_set_features(struct net_device *dev, netdev_features_t features)
{
	struct qede_dev *edev = netdev_priv(dev);
	netdev_features_t changes = features ^ dev->features;
	bool need_reload = false;

	if (changes & NETIF_F_GRO_HW)
		need_reload = true;

	if (need_reload) {
		struct qede_reload_args args;

		args.u.features = features;
		args.func = &qede_set_features_reload;

		/* Make sure that we definitely need to reload.
		 * In case of an eBPF attached program, there will be no FW
		 * aggregations, so no need to actually reload.
		 */
		__qede_lock(edev);
		if (edev->xdp_prog)
			args.func(edev, &args);
		else
			qede_reload(edev, &args, true);
		__qede_unlock(edev);

		return 1;
	}

	return 0;
}

void qede_udp_tunnel_add(struct net_device *dev, struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_tunn_params tunn_params;
	u16 t_port = ntohs(ti->port);
	int rc;

	memset(&tunn_params, 0, sizeof(tunn_params));

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (!edev->dev_info.common.vxlan_enable)
			return;

		if (edev->vxlan_dst_port)
			return;

		tunn_params.update_vxlan_port = 1;
		tunn_params.vxlan_port = t_port;

		__qede_lock(edev);
		rc = edev->ops->tunn_config(edev->cdev, &tunn_params);
		__qede_unlock(edev);

		if (!rc) {
			edev->vxlan_dst_port = t_port;
			DP_VERBOSE(edev, QED_MSG_DEBUG, "Added vxlan port=%d\n",
				   t_port);
		} else {
			DP_NOTICE(edev, "Failed to add vxlan UDP port=%d\n",
				  t_port);
		}

		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (!edev->dev_info.common.geneve_enable)
			return;

		if (edev->geneve_dst_port)
			return;

		tunn_params.update_geneve_port = 1;
		tunn_params.geneve_port = t_port;

		__qede_lock(edev);
		rc = edev->ops->tunn_config(edev->cdev, &tunn_params);
		__qede_unlock(edev);

		if (!rc) {
			edev->geneve_dst_port = t_port;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "Added geneve port=%d\n", t_port);
		} else {
			DP_NOTICE(edev, "Failed to add geneve UDP port=%d\n",
				  t_port);
		}

		break;
	default:
		return;
	}
}

void qede_udp_tunnel_del(struct net_device *dev,
			 struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_tunn_params tunn_params;
	u16 t_port = ntohs(ti->port);

	memset(&tunn_params, 0, sizeof(tunn_params));

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (t_port != edev->vxlan_dst_port)
			return;

		tunn_params.update_vxlan_port = 1;
		tunn_params.vxlan_port = 0;

		__qede_lock(edev);
		edev->ops->tunn_config(edev->cdev, &tunn_params);
		__qede_unlock(edev);

		edev->vxlan_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted vxlan port=%d\n",
			   t_port);

		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (t_port != edev->geneve_dst_port)
			return;

		tunn_params.update_geneve_port = 1;
		tunn_params.geneve_port = 0;

		__qede_lock(edev);
		edev->ops->tunn_config(edev->cdev, &tunn_params);
		__qede_unlock(edev);

		edev->geneve_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted geneve port=%d\n",
			   t_port);
		break;
	default:
		return;
	}
}

static void qede_xdp_reload_func(struct qede_dev *edev,
				 struct qede_reload_args *args)
{
	struct bpf_prog *old;

	old = xchg(&edev->xdp_prog, args->u.new_prog);
	if (old)
		bpf_prog_put(old);
}

static int qede_xdp_set(struct qede_dev *edev, struct bpf_prog *prog)
{
	struct qede_reload_args args;

	/* If we're called, there was already a bpf reference increment */
	args.func = &qede_xdp_reload_func;
	args.u.new_prog = prog;
	qede_reload(edev, &args, false);

	return 0;
}

int qede_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return qede_xdp_set(edev, xdp->prog);
	case XDP_QUERY_PROG:
		xdp->prog_id = edev->xdp_prog ? edev->xdp_prog->aux->id : 0;
		return 0;
	default:
		return -EINVAL;
	}
}

static int qede_set_mcast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char *mac, int num_macs)
{
	struct qed_filter_params filter_cmd;
	int i;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_MCAST;
	filter_cmd.filter.mcast.type = opcode;
	filter_cmd.filter.mcast.num = num_macs;

	for (i = 0; i < num_macs; i++, mac += ETH_ALEN)
		ether_addr_copy(filter_cmd.filter.mcast.mac[i], mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

int qede_set_mac_addr(struct net_device *ndev, void *p)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct sockaddr *addr = p;
	int rc = 0;

	/* Make sure the state doesn't transition while changing the MAC.
	 * Also, all flows accessing the dev_addr field are doing that under
	 * this lock.
	 */
	__qede_lock(edev);

	if (!is_valid_ether_addr(addr->sa_data)) {
		DP_NOTICE(edev, "The MAC address is not valid\n");
		rc = -EFAULT;
		goto out;
	}

	if (!edev->ops->check_mac(edev->cdev, addr->sa_data)) {
		DP_NOTICE(edev, "qed prevents setting MAC %pM\n",
			  addr->sa_data);
		rc = -EINVAL;
		goto out;
	}

	if (edev->state == QEDE_STATE_OPEN) {
		/* Remove the previous primary mac */
		rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
					   ndev->dev_addr);
		if (rc)
			goto out;
	}

	ether_addr_copy(ndev->dev_addr, addr->sa_data);
	DP_INFO(edev, "Setting device MAC to %pM\n", addr->sa_data);

	if (edev->state != QEDE_STATE_OPEN) {
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "The device is currently down\n");
		/* Ask PF to explicitly update a copy in bulletin board */
		if (IS_VF(edev) && edev->ops->req_bulletin_update_mac)
			edev->ops->req_bulletin_update_mac(edev->cdev,
							   ndev->dev_addr);
		goto out;
	}

	edev->ops->common->update_mac(edev->cdev, ndev->dev_addr);

	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
				   ndev->dev_addr);
out:
	__qede_unlock(edev);
	return rc;
}

static int
qede_configure_mcast_filtering(struct net_device *ndev,
			       enum qed_filter_rx_mode_type *accept_flags)
{
	struct qede_dev *edev = netdev_priv(ndev);
	unsigned char *mc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc = 0, mc_count;
	size_t size;

	size = 64 * ETH_ALEN;

	mc_macs = kzalloc(size, GFP_KERNEL);
	if (!mc_macs) {
		DP_NOTICE(edev,
			  "Failed to allocate memory for multicast MACs\n");
		rc = -ENOMEM;
		goto exit;
	}

	temp = mc_macs;

	/* Remove all previously configured MAC filters */
	rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
				   mc_macs, 1);
	if (rc)
		goto exit;

	netif_addr_lock_bh(ndev);

	mc_count = netdev_mc_count(ndev);
	if (mc_count <= 64) {
		netdev_for_each_mc_addr(ha, ndev) {
			ether_addr_copy(temp, ha->addr);
			temp += ETH_ALEN;
		}
	}

	netif_addr_unlock_bh(ndev);

	/* Check for all multicast @@@TBD resource allocation */
	if ((ndev->flags & IFF_ALLMULTI) || (mc_count > 64)) {
		if (*accept_flags == QED_FILTER_RX_MODE_TYPE_REGULAR)
			*accept_flags = QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC;
	} else {
		/* Add all multicast MAC filters */
		rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
					   mc_macs, mc_count);
	}

exit:
	kfree(mc_macs);
	return rc;
}

void qede_set_rx_mode(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	set_bit(QEDE_SP_RX_MODE, &edev->sp_flags);
	schedule_delayed_work(&edev->sp_task, 0);
}

/* Must be called with qede_lock held */
void qede_config_rx_mode(struct net_device *ndev)
{
	enum qed_filter_rx_mode_type accept_flags;
	struct qede_dev *edev = netdev_priv(ndev);
	struct qed_filter_params rx_mode;
	unsigned char *uc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc, uc_count;
	size_t size;

	netif_addr_lock_bh(ndev);

	uc_count = netdev_uc_count(ndev);
	size = uc_count * ETH_ALEN;

	uc_macs = kzalloc(size, GFP_ATOMIC);
	if (!uc_macs) {
		DP_NOTICE(edev, "Failed to allocate memory for unicast MACs\n");
		netif_addr_unlock_bh(ndev);
		return;
	}

	temp = uc_macs;
	netdev_for_each_uc_addr(ha, ndev) {
		ether_addr_copy(temp, ha->addr);
		temp += ETH_ALEN;
	}

	netif_addr_unlock_bh(ndev);

	/* Configure the struct for the Rx mode */
	memset(&rx_mode, 0, sizeof(struct qed_filter_params));
	rx_mode.type = QED_FILTER_TYPE_RX_MODE;

	/* Remove all previous unicast secondary macs and multicast macs
	 * (configure / leave the primary mac)
	 */
	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_REPLACE,
				   edev->ndev->dev_addr);
	if (rc)
		goto out;

	/* Check for promiscuous */
	if (ndev->flags & IFF_PROMISC)
		accept_flags = QED_FILTER_RX_MODE_TYPE_PROMISC;
	else
		accept_flags = QED_FILTER_RX_MODE_TYPE_REGULAR;

	/* Configure all filters regardless, in case promisc is rejected */
	if (uc_count < edev->dev_info.num_mac_filters) {
		int i;

		temp = uc_macs;
		for (i = 0; i < uc_count; i++) {
			rc = qede_set_ucast_rx_mac(edev,
						   QED_FILTER_XCAST_TYPE_ADD,
						   temp);
			if (rc)
				goto out;

			temp += ETH_ALEN;
		}
	} else {
		accept_flags = QED_FILTER_RX_MODE_TYPE_PROMISC;
	}

	rc = qede_configure_mcast_filtering(ndev, &accept_flags);
	if (rc)
		goto out;

	/* take care of VLAN mode */
	if (ndev->flags & IFF_PROMISC) {
		qede_config_accept_any_vlan(edev, true);
	} else if (!edev->non_configured_vlans) {
		/* It's possible that accept_any_vlan mode is set due to a
		 * previous setting of IFF_PROMISC. If vlan credits are
		 * sufficient, disable accept_any_vlan.
		 */
		qede_config_accept_any_vlan(edev, false);
	}

	rx_mode.filter.accept_flags = accept_flags;
	edev->ops->filter_config(edev->cdev, &rx_mode);
out:
	kfree(uc_macs);
}

static struct qede_arfs_fltr_node *
qede_get_arfs_fltr_by_loc(struct hlist_head *head, u64 location)
{
	struct qede_arfs_fltr_node *fltr;

	hlist_for_each_entry(fltr, head, node)
		if (location == fltr->sw_id)
			return fltr;

	return NULL;
}

int qede_get_cls_rule_all(struct qede_dev *edev, struct ethtool_rxnfc *info,
			  u32 *rule_locs)
{
	struct qede_arfs_fltr_node *fltr;
	struct hlist_head *head;
	int cnt = 0, rc = 0;

	info->data = QEDE_RFS_MAX_FLTR;

	__qede_lock(edev);

	if (!edev->arfs) {
		rc = -EPERM;
		goto unlock;
	}

	head = QEDE_ARFS_BUCKET_HEAD(edev, 0);

	hlist_for_each_entry(fltr, head, node) {
		if (cnt == info->rule_cnt) {
			rc = -EMSGSIZE;
			goto unlock;
		}

		rule_locs[cnt] = fltr->sw_id;
		cnt++;
	}

	info->rule_cnt = cnt;

unlock:
	__qede_unlock(edev);
	return rc;
}

int qede_get_cls_rule_entry(struct qede_dev *edev, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct qede_arfs_fltr_node *fltr = NULL;
	int rc = 0;

	cmd->data = QEDE_RFS_MAX_FLTR;

	__qede_lock(edev);

	if (!edev->arfs) {
		rc = -EPERM;
		goto unlock;
	}

	fltr = qede_get_arfs_fltr_by_loc(QEDE_ARFS_BUCKET_HEAD(edev, 0),
					 fsp->location);
	if (!fltr) {
		DP_NOTICE(edev, "Rule not found - location=0x%x\n",
			  fsp->location);
		rc = -EINVAL;
		goto unlock;
	}

	if (fltr->tuple.eth_proto == htons(ETH_P_IP)) {
		if (fltr->tuple.ip_proto == IPPROTO_TCP)
			fsp->flow_type = TCP_V4_FLOW;
		else
			fsp->flow_type = UDP_V4_FLOW;

		fsp->h_u.tcp_ip4_spec.psrc = fltr->tuple.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = fltr->tuple.dst_port;
		fsp->h_u.tcp_ip4_spec.ip4src = fltr->tuple.src_ipv4;
		fsp->h_u.tcp_ip4_spec.ip4dst = fltr->tuple.dst_ipv4;
	} else {
		if (fltr->tuple.ip_proto == IPPROTO_TCP)
			fsp->flow_type = TCP_V6_FLOW;
		else
			fsp->flow_type = UDP_V6_FLOW;
		fsp->h_u.tcp_ip6_spec.psrc = fltr->tuple.src_port;
		fsp->h_u.tcp_ip6_spec.pdst = fltr->tuple.dst_port;
		memcpy(&fsp->h_u.tcp_ip6_spec.ip6src,
		       &fltr->tuple.src_ipv6, sizeof(struct in6_addr));
		memcpy(&fsp->h_u.tcp_ip6_spec.ip6dst,
		       &fltr->tuple.dst_ipv6, sizeof(struct in6_addr));
	}

	fsp->ring_cookie = fltr->rxq_id;

	if (fltr->vfid) {
		fsp->ring_cookie |= ((u64)fltr->vfid) <<
					ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
	}

	if (fltr->b_is_drop)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
unlock:
	__qede_unlock(edev);
	return rc;
}

static int
qede_poll_arfs_filter_config(struct qede_dev *edev,
			     struct qede_arfs_fltr_node *fltr)
{
	int count = QEDE_ARFS_POLL_COUNT;

	while (fltr->used && count) {
		msleep(20);
		count--;
	}

	if (count == 0 || fltr->fw_rc) {
		DP_NOTICE(edev, "Timeout in polling filter config\n");
		qede_dequeue_fltr_and_config_searcher(edev, fltr);
		return -EIO;
	}

	return fltr->fw_rc;
}

static int qede_flow_get_min_header_size(struct qede_arfs_tuple *t)
{
	int size = ETH_HLEN;

	if (t->eth_proto == htons(ETH_P_IP))
		size += sizeof(struct iphdr);
	else
		size += sizeof(struct ipv6hdr);

	if (t->ip_proto == IPPROTO_TCP)
		size += sizeof(struct tcphdr);
	else
		size += sizeof(struct udphdr);

	return size;
}

static bool qede_flow_spec_ipv4_cmp(struct qede_arfs_tuple *a,
				    struct qede_arfs_tuple *b)
{
	if (a->eth_proto != htons(ETH_P_IP) ||
	    b->eth_proto != htons(ETH_P_IP))
		return false;

	return (a->src_ipv4 == b->src_ipv4) &&
	       (a->dst_ipv4 == b->dst_ipv4);
}

static void qede_flow_build_ipv4_hdr(struct qede_arfs_tuple *t,
				     void *header)
{
	__be16 *ports = (__be16 *)(header + ETH_HLEN + sizeof(struct iphdr));
	struct iphdr *ip = (struct iphdr *)(header + ETH_HLEN);
	struct ethhdr *eth = (struct ethhdr *)header;

	eth->h_proto = t->eth_proto;
	ip->saddr = t->src_ipv4;
	ip->daddr = t->dst_ipv4;
	ip->version = 0x4;
	ip->ihl = 0x5;
	ip->protocol = t->ip_proto;
	ip->tot_len = cpu_to_be16(qede_flow_get_min_header_size(t) - ETH_HLEN);

	/* ports is weakly typed to suit both TCP and UDP ports */
	ports[0] = t->src_port;
	ports[1] = t->dst_port;
}

static void qede_flow_stringify_ipv4_hdr(struct qede_arfs_tuple *t,
					 void *buffer)
{
	const char *prefix = t->ip_proto == IPPROTO_TCP ? "TCP" : "UDP";

	snprintf(buffer, QEDE_FILTER_PRINT_MAX_LEN,
		 "%s %pI4 (%04x) -> %pI4 (%04x)",
		 prefix, &t->src_ipv4, t->src_port,
		 &t->dst_ipv4, t->dst_port);
}

static bool qede_flow_spec_ipv6_cmp(struct qede_arfs_tuple *a,
				    struct qede_arfs_tuple *b)
{
	if (a->eth_proto != htons(ETH_P_IPV6) ||
	    b->eth_proto != htons(ETH_P_IPV6))
		return false;

	if (memcmp(&a->src_ipv6, &b->src_ipv6, sizeof(struct in6_addr)))
		return false;

	if (memcmp(&a->dst_ipv6, &b->dst_ipv6, sizeof(struct in6_addr)))
		return false;

	return true;
}

static void qede_flow_build_ipv6_hdr(struct qede_arfs_tuple *t,
				     void *header)
{
	__be16 *ports = (__be16 *)(header + ETH_HLEN + sizeof(struct ipv6hdr));
	struct ipv6hdr *ip6 = (struct ipv6hdr *)(header + ETH_HLEN);
	struct ethhdr *eth = (struct ethhdr *)header;

	eth->h_proto = t->eth_proto;
	memcpy(&ip6->saddr, &t->src_ipv6, sizeof(struct in6_addr));
	memcpy(&ip6->daddr, &t->dst_ipv6, sizeof(struct in6_addr));
	ip6->version = 0x6;

	if (t->ip_proto == IPPROTO_TCP) {
		ip6->nexthdr = NEXTHDR_TCP;
		ip6->payload_len = cpu_to_be16(sizeof(struct tcphdr));
	} else {
		ip6->nexthdr = NEXTHDR_UDP;
		ip6->payload_len = cpu_to_be16(sizeof(struct udphdr));
	}

	/* ports is weakly typed to suit both TCP and UDP ports */
	ports[0] = t->src_port;
	ports[1] = t->dst_port;
}

/* Validate fields which are set and not accepted by the driver */
static int qede_flow_spec_validate_unused(struct qede_dev *edev,
					  struct ethtool_rx_flow_spec *fs)
{
	if (fs->flow_type & FLOW_MAC_EXT) {
		DP_INFO(edev, "Don't support MAC extensions\n");
		return -EOPNOTSUPP;
	}

	if ((fs->flow_type & FLOW_EXT) &&
	    (fs->h_ext.vlan_etype || fs->h_ext.vlan_tci)) {
		DP_INFO(edev, "Don't support vlan-based classification\n");
		return -EOPNOTSUPP;
	}

	if ((fs->flow_type & FLOW_EXT) &&
	    (fs->h_ext.data[0] || fs->h_ext.data[1])) {
		DP_INFO(edev, "Don't support user defined data\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qede_set_v4_tuple_to_profile(struct qede_dev *edev,
					struct qede_arfs_tuple *t)
{
	/* We must have Only 4-tuples/l4 port/src ip/dst ip
	 * as an input.
	 */
	if (t->src_port && t->dst_port && t->src_ipv4 && t->dst_ipv4) {
		t->mode = QED_FILTER_CONFIG_MODE_5_TUPLE;
	} else if (!t->src_port && t->dst_port &&
		   !t->src_ipv4 && !t->dst_ipv4) {
		t->mode = QED_FILTER_CONFIG_MODE_L4_PORT;
	} else if (!t->src_port && !t->dst_port &&
		   !t->dst_ipv4 && t->src_ipv4) {
		t->mode = QED_FILTER_CONFIG_MODE_IP_SRC;
	} else if (!t->src_port && !t->dst_port &&
		   t->dst_ipv4 && !t->src_ipv4) {
		t->mode = QED_FILTER_CONFIG_MODE_IP_DEST;
	} else {
		DP_INFO(edev, "Invalid N-tuple\n");
		return -EOPNOTSUPP;
	}

	t->ip_comp = qede_flow_spec_ipv4_cmp;
	t->build_hdr = qede_flow_build_ipv4_hdr;
	t->stringify = qede_flow_stringify_ipv4_hdr;

	return 0;
}

static int qede_set_v6_tuple_to_profile(struct qede_dev *edev,
					struct qede_arfs_tuple *t,
					struct in6_addr *zaddr)
{
	/* We must have Only 4-tuples/l4 port/src ip/dst ip
	 * as an input.
	 */
	if (t->src_port && t->dst_port &&
	    memcmp(&t->src_ipv6, zaddr, sizeof(struct in6_addr)) &&
	    memcmp(&t->dst_ipv6, zaddr, sizeof(struct in6_addr))) {
		t->mode = QED_FILTER_CONFIG_MODE_5_TUPLE;
	} else if (!t->src_port && t->dst_port &&
		   !memcmp(&t->src_ipv6, zaddr, sizeof(struct in6_addr)) &&
		   !memcmp(&t->dst_ipv6, zaddr, sizeof(struct in6_addr))) {
		t->mode = QED_FILTER_CONFIG_MODE_L4_PORT;
	} else if (!t->src_port && !t->dst_port &&
		   !memcmp(&t->dst_ipv6, zaddr, sizeof(struct in6_addr)) &&
		   memcmp(&t->src_ipv6, zaddr, sizeof(struct in6_addr))) {
		t->mode = QED_FILTER_CONFIG_MODE_IP_SRC;
	} else if (!t->src_port && !t->dst_port &&
		   memcmp(&t->dst_ipv6, zaddr, sizeof(struct in6_addr)) &&
		   !memcmp(&t->src_ipv6, zaddr, sizeof(struct in6_addr))) {
		t->mode = QED_FILTER_CONFIG_MODE_IP_DEST;
	} else {
		DP_INFO(edev, "Invalid N-tuple\n");
		return -EOPNOTSUPP;
	}

	t->ip_comp = qede_flow_spec_ipv6_cmp;
	t->build_hdr = qede_flow_build_ipv6_hdr;

	return 0;
}

/* Must be called while qede lock is held */
static struct qede_arfs_fltr_node *
qede_flow_find_fltr(struct qede_dev *edev, struct qede_arfs_tuple *t)
{
	struct qede_arfs_fltr_node *fltr;
	struct hlist_node *temp;
	struct hlist_head *head;

	head = QEDE_ARFS_BUCKET_HEAD(edev, 0);

	hlist_for_each_entry_safe(fltr, temp, head, node) {
		if (fltr->tuple.ip_proto == t->ip_proto &&
		    fltr->tuple.src_port == t->src_port &&
		    fltr->tuple.dst_port == t->dst_port &&
		    t->ip_comp(&fltr->tuple, t))
			return fltr;
	}

	return NULL;
}

static void qede_flow_set_destination(struct qede_dev *edev,
				      struct qede_arfs_fltr_node *n,
				      struct ethtool_rx_flow_spec *fs)
{
	if (fs->ring_cookie == RX_CLS_FLOW_DISC) {
		n->b_is_drop = true;
		return;
	}

	n->vfid = ethtool_get_flow_spec_ring_vf(fs->ring_cookie);
	n->rxq_id = ethtool_get_flow_spec_ring(fs->ring_cookie);
	n->next_rxq_id = n->rxq_id;

	if (n->vfid)
		DP_VERBOSE(edev, QED_MSG_SP,
			   "Configuring N-tuple for VF 0x%02x\n", n->vfid - 1);
}

int qede_delete_flow_filter(struct qede_dev *edev, u64 cookie)
{
	struct qede_arfs_fltr_node *fltr = NULL;
	int rc = -EPERM;

	__qede_lock(edev);
	if (!edev->arfs)
		goto unlock;

	fltr = qede_get_arfs_fltr_by_loc(QEDE_ARFS_BUCKET_HEAD(edev, 0),
					 cookie);
	if (!fltr)
		goto unlock;

	qede_configure_arfs_fltr(edev, fltr, fltr->rxq_id, false);

	rc = qede_poll_arfs_filter_config(edev, fltr);
	if (rc == 0)
		qede_dequeue_fltr_and_config_searcher(edev, fltr);

unlock:
	__qede_unlock(edev);
	return rc;
}

int qede_get_arfs_filter_count(struct qede_dev *edev)
{
	int count = 0;

	__qede_lock(edev);

	if (!edev->arfs)
		goto unlock;

	count = edev->arfs->filter_count;

unlock:
	__qede_unlock(edev);
	return count;
}

static int qede_parse_actions(struct qede_dev *edev,
			      struct flow_action *flow_action,
			      struct netlink_ext_ack *extack)
{
	const struct flow_action_entry *act;
	int i;

	if (!flow_action_has_entries(flow_action)) {
		DP_NOTICE(edev, "No actions received\n");
		return -EINVAL;
	}

	if (!flow_action_basic_hw_stats_check(flow_action, extack))
		return -EOPNOTSUPP;

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			break;
		case FLOW_ACTION_QUEUE:
			if (act->queue.vf)
				break;

			if (act->queue.index >= QEDE_RSS_COUNT(edev)) {
				DP_INFO(edev, "Queue out-of-bounds\n");
				return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int
qede_flow_parse_ports(struct qede_dev *edev, struct flow_rule *rule,
		      struct qede_arfs_tuple *t)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		if ((match.key->src && match.mask->src != U16_MAX) ||
		    (match.key->dst && match.mask->dst != U16_MAX)) {
			DP_NOTICE(edev, "Do not support ports masks\n");
			return -EINVAL;
		}

		t->src_port = match.key->src;
		t->dst_port = match.key->dst;
	}

	return 0;
}

static int
qede_flow_parse_v6_common(struct qede_dev *edev, struct flow_rule *rule,
			  struct qede_arfs_tuple *t)
{
	struct in6_addr zero_addr, addr;

	memset(&zero_addr, 0, sizeof(addr));
	memset(&addr, 0xff, sizeof(addr));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		if ((memcmp(&match.key->src, &zero_addr, sizeof(addr)) &&
		     memcmp(&match.mask->src, &addr, sizeof(addr))) ||
		    (memcmp(&match.key->dst, &zero_addr, sizeof(addr)) &&
		     memcmp(&match.mask->dst, &addr, sizeof(addr)))) {
			DP_NOTICE(edev,
				  "Do not support IPv6 address prefix/mask\n");
			return -EINVAL;
		}

		memcpy(&t->src_ipv6, &match.key->src, sizeof(addr));
		memcpy(&t->dst_ipv6, &match.key->dst, sizeof(addr));
	}

	if (qede_flow_parse_ports(edev, rule, t))
		return -EINVAL;

	return qede_set_v6_tuple_to_profile(edev, t, &zero_addr);
}

static int
qede_flow_parse_v4_common(struct qede_dev *edev, struct flow_rule *rule,
			struct qede_arfs_tuple *t)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		if ((match.key->src && match.mask->src != U32_MAX) ||
		    (match.key->dst && match.mask->dst != U32_MAX)) {
			DP_NOTICE(edev, "Do not support ipv4 prefix/masks\n");
			return -EINVAL;
		}

		t->src_ipv4 = match.key->src;
		t->dst_ipv4 = match.key->dst;
	}

	if (qede_flow_parse_ports(edev, rule, t))
		return -EINVAL;

	return qede_set_v4_tuple_to_profile(edev, t);
}

static int
qede_flow_parse_tcp_v6(struct qede_dev *edev, struct flow_rule *rule,
		     struct qede_arfs_tuple *tuple)
{
	tuple->ip_proto = IPPROTO_TCP;
	tuple->eth_proto = htons(ETH_P_IPV6);

	return qede_flow_parse_v6_common(edev, rule, tuple);
}

static int
qede_flow_parse_tcp_v4(struct qede_dev *edev, struct flow_rule *rule,
		     struct qede_arfs_tuple *tuple)
{
	tuple->ip_proto = IPPROTO_TCP;
	tuple->eth_proto = htons(ETH_P_IP);

	return qede_flow_parse_v4_common(edev, rule, tuple);
}

static int
qede_flow_parse_udp_v6(struct qede_dev *edev, struct flow_rule *rule,
		     struct qede_arfs_tuple *tuple)
{
	tuple->ip_proto = IPPROTO_UDP;
	tuple->eth_proto = htons(ETH_P_IPV6);

	return qede_flow_parse_v6_common(edev, rule, tuple);
}

static int
qede_flow_parse_udp_v4(struct qede_dev *edev, struct flow_rule *rule,
		     struct qede_arfs_tuple *tuple)
{
	tuple->ip_proto = IPPROTO_UDP;
	tuple->eth_proto = htons(ETH_P_IP);

	return qede_flow_parse_v4_common(edev, rule, tuple);
}

static int
qede_parse_flow_attr(struct qede_dev *edev, __be16 proto,
		     struct flow_rule *rule, struct qede_arfs_tuple *tuple)
{
	struct flow_dissector *dissector = rule->match.dissector;
	int rc = -EINVAL;
	u8 ip_proto = 0;

	memset(tuple, 0, sizeof(*tuple));

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS))) {
		DP_NOTICE(edev, "Unsupported key set:0x%x\n",
			  dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (proto != htons(ETH_P_IP) &&
	    proto != htons(ETH_P_IPV6)) {
		DP_NOTICE(edev, "Unsupported proto=0x%x\n", proto);
		return -EPROTONOSUPPORT;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ip_proto = match.key->ip_proto;
	}

	if (ip_proto == IPPROTO_TCP && proto == htons(ETH_P_IP))
		rc = qede_flow_parse_tcp_v4(edev, rule, tuple);
	else if (ip_proto == IPPROTO_TCP && proto == htons(ETH_P_IPV6))
		rc = qede_flow_parse_tcp_v6(edev, rule, tuple);
	else if (ip_proto == IPPROTO_UDP && proto == htons(ETH_P_IP))
		rc = qede_flow_parse_udp_v4(edev, rule, tuple);
	else if (ip_proto == IPPROTO_UDP && proto == htons(ETH_P_IPV6))
		rc = qede_flow_parse_udp_v6(edev, rule, tuple);
	else
		DP_NOTICE(edev, "Invalid protocol request\n");

	return rc;
}

int qede_add_tc_flower_fltr(struct qede_dev *edev, __be16 proto,
			    struct flow_cls_offload *f)
{
	struct qede_arfs_fltr_node *n;
	int min_hlen, rc = -EINVAL;
	struct qede_arfs_tuple t;

	__qede_lock(edev);

	if (!edev->arfs) {
		rc = -EPERM;
		goto unlock;
	}

	/* parse flower attribute and prepare filter */
	if (qede_parse_flow_attr(edev, proto, f->rule, &t))
		goto unlock;

	/* Validate profile mode and number of filters */
	if ((edev->arfs->filter_count && edev->arfs->mode != t.mode) ||
	    edev->arfs->filter_count == QEDE_RFS_MAX_FLTR) {
		DP_NOTICE(edev,
			  "Filter configuration invalidated, filter mode=0x%x, configured mode=0x%x, filter count=0x%x\n",
			  t.mode, edev->arfs->mode, edev->arfs->filter_count);
		goto unlock;
	}

	/* parse tc actions and get the vf_id */
	if (qede_parse_actions(edev, &f->rule->action, f->common.extack))
		goto unlock;

	if (qede_flow_find_fltr(edev, &t)) {
		rc = -EEXIST;
		goto unlock;
	}

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) {
		rc = -ENOMEM;
		goto unlock;
	}

	min_hlen = qede_flow_get_min_header_size(&t);

	n->data = kzalloc(min_hlen, GFP_KERNEL);
	if (!n->data) {
		kfree(n);
		rc = -ENOMEM;
		goto unlock;
	}

	memcpy(&n->tuple, &t, sizeof(n->tuple));

	n->buf_len = min_hlen;
	n->b_is_drop = true;
	n->sw_id = f->cookie;

	n->tuple.build_hdr(&n->tuple, n->data);

	rc = qede_enqueue_fltr_and_config_searcher(edev, n, 0);
	if (rc)
		goto unlock;

	qede_configure_arfs_fltr(edev, n, n->rxq_id, true);
	rc = qede_poll_arfs_filter_config(edev, n);

unlock:
	__qede_unlock(edev);
	return rc;
}

static int qede_flow_spec_validate(struct qede_dev *edev,
				   struct flow_action *flow_action,
				   struct qede_arfs_tuple *t,
				   __u32 location)
{
	if (location >= QEDE_RFS_MAX_FLTR) {
		DP_INFO(edev, "Location out-of-bounds\n");
		return -EINVAL;
	}

	/* Check location isn't already in use */
	if (test_bit(location, edev->arfs->arfs_fltr_bmap)) {
		DP_INFO(edev, "Location already in use\n");
		return -EINVAL;
	}

	/* Check if the filtering-mode could support the filter */
	if (edev->arfs->filter_count &&
	    edev->arfs->mode != t->mode) {
		DP_INFO(edev,
			"flow_spec would require filtering mode %08x, but %08x is configured\n",
			t->mode, edev->arfs->filter_count);
		return -EINVAL;
	}

	if (qede_parse_actions(edev, flow_action, NULL))
		return -EINVAL;

	return 0;
}

static int qede_flow_spec_to_rule(struct qede_dev *edev,
				  struct qede_arfs_tuple *t,
				  struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_rx_flow_spec_input input = {};
	struct ethtool_rx_flow_rule *flow;
	__be16 proto;
	int err = 0;

	if (qede_flow_spec_validate_unused(edev, fs))
		return -EOPNOTSUPP;

	switch ((fs->flow_type & ~FLOW_EXT)) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		proto = htons(ETH_P_IP);
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		proto = htons(ETH_P_IPV6);
		break;
	default:
		DP_VERBOSE(edev, NETIF_MSG_IFUP,
			   "Can't support flow of type %08x\n", fs->flow_type);
		return -EOPNOTSUPP;
	}

	input.fs = fs;
	flow = ethtool_rx_flow_rule_create(&input);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (qede_parse_flow_attr(edev, proto, flow->rule, t)) {
		err = -EINVAL;
		goto err_out;
	}

	/* Make sure location is valid and filter isn't already set */
	err = qede_flow_spec_validate(edev, &flow->rule->action, t,
				      fs->location);
err_out:
	ethtool_rx_flow_rule_destroy(flow);
	return err;

}

int qede_add_cls_rule(struct qede_dev *edev, struct ethtool_rxnfc *info)
{
	struct ethtool_rx_flow_spec *fsp = &info->fs;
	struct qede_arfs_fltr_node *n;
	struct qede_arfs_tuple t;
	int min_hlen, rc;

	__qede_lock(edev);

	if (!edev->arfs) {
		rc = -EPERM;
		goto unlock;
	}

	/* Translate the flow specification into something fittign our DB */
	rc = qede_flow_spec_to_rule(edev, &t, fsp);
	if (rc)
		goto unlock;

	if (qede_flow_find_fltr(edev, &t)) {
		rc = -EINVAL;
		goto unlock;
	}

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) {
		rc = -ENOMEM;
		goto unlock;
	}

	min_hlen = qede_flow_get_min_header_size(&t);
	n->data = kzalloc(min_hlen, GFP_KERNEL);
	if (!n->data) {
		kfree(n);
		rc = -ENOMEM;
		goto unlock;
	}

	n->sw_id = fsp->location;
	set_bit(n->sw_id, edev->arfs->arfs_fltr_bmap);
	n->buf_len = min_hlen;

	memcpy(&n->tuple, &t, sizeof(n->tuple));

	qede_flow_set_destination(edev, n, fsp);

	/* Build a minimal header according to the flow */
	n->tuple.build_hdr(&n->tuple, n->data);

	rc = qede_enqueue_fltr_and_config_searcher(edev, n, 0);
	if (rc)
		goto unlock;

	qede_configure_arfs_fltr(edev, n, n->rxq_id, true);
	rc = qede_poll_arfs_filter_config(edev, n);
unlock:
	__qede_unlock(edev);

	return rc;
}
