#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/flow_dissector.h>
#include "enic_res.h"
#include "enic_clsf.h"

/* enic_addfltr_5t - Add ipv4 5tuple filter
 *	@enic: enic struct of vnic
 *	@keys: flow_keys of ipv4 5tuple
 *	@rq: rq number to steer to
 *
 * This function returns filter_id(hardware_id) of the filter
 * added. In case of error it returns a negative number.
 */
int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq)
{
	int res;
	struct filter data;

	switch (keys->basic.ip_proto) {
	case IPPROTO_TCP:
		data.u.ipv4.protocol = PROTO_TCP;
		break;
	case IPPROTO_UDP:
		data.u.ipv4.protocol = PROTO_UDP;
		break;
	default:
		return -EPROTONOSUPPORT;
	};
	data.type = FILTER_IPV4_5TUPLE;
	data.u.ipv4.src_addr = ntohl(keys->addrs.v4addrs.src);
	data.u.ipv4.dst_addr = ntohl(keys->addrs.v4addrs.dst);
	data.u.ipv4.src_port = ntohs(keys->ports.src);
	data.u.ipv4.dst_port = ntohs(keys->ports.dst);
	data.u.ipv4.flags = FILTER_FIELDS_IPV4_5TUPLE;

	spin_lock_bh(&enic->devcmd_lock);
	res = vnic_dev_classifier(enic->vdev, CLSF_ADD, &rq, &data);
	spin_unlock_bh(&enic->devcmd_lock);
	res = (res == 0) ? rq : res;

	return res;
}

/* enic_delfltr - Delete clsf filter
 *	@enic: enic struct of vnic
 *	@filter_id: filter_is(hardware_id) of filter to be deleted
 *
 * This function returns zero in case of success, negative number incase of
 * error.
 */
int enic_delfltr(struct enic *enic, u16 filter_id)
{
	int ret;

	spin_lock_bh(&enic->devcmd_lock);
	ret = vnic_dev_classifier(enic->vdev, CLSF_DEL, &filter_id, NULL);
	spin_unlock_bh(&enic->devcmd_lock);

	return ret;
}

/* enic_rfs_flw_tbl_init - initialize enic->rfs_h members
 *	@enic: enic data
 */
void enic_rfs_flw_tbl_init(struct enic *enic)
{
	int i;

	spin_lock_init(&enic->rfs_h.lock);
	for (i = 0; i <= ENIC_RFS_FLW_MASK; i++)
		INIT_HLIST_HEAD(&enic->rfs_h.ht_head[i]);
	enic->rfs_h.max = enic->config.num_arfs;
	enic->rfs_h.free = enic->rfs_h.max;
	enic->rfs_h.toclean = 0;
	enic_rfs_timer_start(enic);
}

void enic_rfs_flw_tbl_free(struct enic *enic)
{
	int i;

	enic_rfs_timer_stop(enic);
	spin_lock_bh(&enic->rfs_h.lock);
	enic->rfs_h.free = 0;
	for (i = 0; i < (1 << ENIC_RFS_FLW_BITSHIFT); i++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;

		hhead = &enic->rfs_h.ht_head[i];
		hlist_for_each_entry_safe(n, tmp, hhead, node) {
			enic_delfltr(enic, n->fltr_id);
			hlist_del(&n->node);
			kfree(n);
		}
	}
	spin_unlock_bh(&enic->rfs_h.lock);
}

struct enic_rfs_fltr_node *htbl_fltr_search(struct enic *enic, u16 fltr_id)
{
	int i;

	for (i = 0; i < (1 << ENIC_RFS_FLW_BITSHIFT); i++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;

		hhead = &enic->rfs_h.ht_head[i];
		hlist_for_each_entry_safe(n, tmp, hhead, node)
			if (n->fltr_id == fltr_id)
				return n;
	}

	return NULL;
}

#ifdef CONFIG_RFS_ACCEL
void enic_flow_may_expire(unsigned long data)
{
	struct enic *enic = (struct enic *)data;
	bool res;
	int j;

	spin_lock_bh(&enic->rfs_h.lock);
	for (j = 0; j < ENIC_CLSF_EXPIRE_COUNT; j++) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;
		struct enic_rfs_fltr_node *n;

		hhead = &enic->rfs_h.ht_head[enic->rfs_h.toclean++];
		hlist_for_each_entry_safe(n, tmp, hhead, node) {
			res = rps_may_expire_flow(enic->netdev, n->rq_id,
						  n->flow_id, n->fltr_id);
			if (res) {
				res = enic_delfltr(enic, n->fltr_id);
				if (unlikely(res))
					continue;
				hlist_del(&n->node);
				kfree(n);
				enic->rfs_h.free++;
			}
		}
	}
	spin_unlock_bh(&enic->rfs_h.lock);
	mod_timer(&enic->rfs_h.rfs_may_expire, jiffies + HZ/4);
}

static struct enic_rfs_fltr_node *htbl_key_search(struct hlist_head *h,
						  struct flow_keys *k)
{
	struct enic_rfs_fltr_node *tpos;

	hlist_for_each_entry(tpos, h, node)
		if (tpos->keys.addrs.v4addrs.src == k->addrs.v4addrs.src &&
		    tpos->keys.addrs.v4addrs.dst == k->addrs.v4addrs.dst &&
		    tpos->keys.ports.ports == k->ports.ports &&
		    tpos->keys.basic.ip_proto == k->basic.ip_proto &&
		    tpos->keys.basic.n_proto == k->basic.n_proto)
			return tpos;
	return NULL;
}

int enic_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id)
{
	struct flow_keys keys;
	struct enic_rfs_fltr_node *n;
	struct enic *enic;
	u16 tbl_idx;
	int res, i;

	enic = netdev_priv(dev);
	res = skb_flow_dissect_flow_keys(skb, &keys);
	if (!res || keys.basic.n_proto != htons(ETH_P_IP) ||
	    (keys.basic.ip_proto != IPPROTO_TCP &&
	     keys.basic.ip_proto != IPPROTO_UDP))
		return -EPROTONOSUPPORT;

	tbl_idx = skb_get_hash_raw(skb) & ENIC_RFS_FLW_MASK;
	spin_lock_bh(&enic->rfs_h.lock);
	n = htbl_key_search(&enic->rfs_h.ht_head[tbl_idx], &keys);

	if (n) { /* entry already present  */
		if (rxq_index == n->rq_id) {
			res = -EEXIST;
			goto ret_unlock;
		}

		/* desired rq changed for the flow, we need to delete
		 * old fltr and add new one
		 *
		 * The moment we delete the fltr, the upcoming pkts
		 * are put it default rq based on rss. When we add
		 * new filter, upcoming pkts are put in desired queue.
		 * This could cause ooo pkts.
		 *
		 * Lets 1st try adding new fltr and then del old one.
		 */
		i = --enic->rfs_h.free;
		/* clsf tbl is full, we have to del old fltr first*/
		if (unlikely(i < 0)) {
			enic->rfs_h.free++;
			res = enic_delfltr(enic, n->fltr_id);
			if (unlikely(res < 0))
				goto ret_unlock;
			res = enic_addfltr_5t(enic, &keys, rxq_index);
			if (res < 0) {
				hlist_del(&n->node);
				enic->rfs_h.free++;
				goto ret_unlock;
			}
		/* add new fltr 1st then del old fltr */
		} else {
			int ret;

			res = enic_addfltr_5t(enic, &keys, rxq_index);
			if (res < 0) {
				enic->rfs_h.free++;
				goto ret_unlock;
			}
			ret = enic_delfltr(enic, n->fltr_id);
			/* deleting old fltr failed. Add old fltr to list.
			 * enic_flow_may_expire() will try to delete it later.
			 */
			if (unlikely(ret < 0)) {
				struct enic_rfs_fltr_node *d;
				struct hlist_head *head;

				head = &enic->rfs_h.ht_head[tbl_idx];
				d = kmalloc(sizeof(*d), GFP_ATOMIC);
				if (d) {
					d->fltr_id = n->fltr_id;
					INIT_HLIST_NODE(&d->node);
					hlist_add_head(&d->node, head);
				}
			} else {
				enic->rfs_h.free++;
			}
		}
		n->rq_id = rxq_index;
		n->fltr_id = res;
		n->flow_id = flow_id;
	/* entry not present */
	} else {
		i = --enic->rfs_h.free;
		if (i <= 0) {
			enic->rfs_h.free++;
			res = -EBUSY;
			goto ret_unlock;
		}

		n = kmalloc(sizeof(*n), GFP_ATOMIC);
		if (!n) {
			res = -ENOMEM;
			enic->rfs_h.free++;
			goto ret_unlock;
		}

		res = enic_addfltr_5t(enic, &keys, rxq_index);
		if (res < 0) {
			kfree(n);
			enic->rfs_h.free++;
			goto ret_unlock;
		}
		n->rq_id = rxq_index;
		n->fltr_id = res;
		n->flow_id = flow_id;
		n->keys = keys;
		INIT_HLIST_NODE(&n->node);
		hlist_add_head(&n->node, &enic->rfs_h.ht_head[tbl_idx]);
	}

ret_unlock:
	spin_unlock_bh(&enic->rfs_h.lock);
	return res;
}

#endif /* CONFIG_RFS_ACCEL */
