/*
 * Copyright 2016 Mans Rullgard <mans@mansr.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/virtio_ring.h>

#define DRV_NAME "ivshmem-net"

#define JAILHOUSE_CFG_SHMEM_PTR	0x40
#define JAILHOUSE_CFG_SHMEM_SZ	0x48

#define IVSHMEM_INTX_ENABLE	0x1

#define IVSHM_NET_STATE_RESET	0
#define IVSHM_NET_STATE_INIT	1
#define IVSHM_NET_STATE_READY	2
#define IVSHM_NET_STATE_RUN	3

#define IVSHM_NET_FLAG_RUN	0

#define IVSHM_NET_MTU_MIN 256
#define IVSHM_NET_MTU_MAX 65535
#define IVSHM_NET_MTU_DEF 16384

#define IVSHM_NET_FRAME_SIZE(s) ALIGN(18 + (s), SMP_CACHE_BYTES)

#define IVSHM_NET_VQ_ALIGN 64

struct ivshmem_regs {
	u32 intxctrl;
	u32 istat;
	u32 ivpos;
	u32 doorbell;
	u32 lstate;
	u32 rstate;
};

struct ivshm_net_queue {
	struct vring vr;
	u32 free_head;
	u32 num_free;
	u32 num_added;
	u16 last_avail_idx;
	u16 last_used_idx;

	void *data;
	void *end;
	u32 size;
	u32 head;
	u32 tail;
};

struct ivshm_net_stats {
	u32 interrupts;
	u32 tx_packets;
	u32 tx_notify;
	u32 tx_pause;
	u32 rx_packets;
	u32 rx_notify;
	u32 napi_poll;
	u32 napi_complete;
	u32 napi_poll_n[10];
};

struct ivshm_net {
	struct ivshm_net_queue rx;
	struct ivshm_net_queue tx;

	u32 vrsize;
	u32 qlen;
	u32 qsize;

	spinlock_t tx_free_lock;
	spinlock_t tx_clean_lock;

	struct napi_struct napi;

	u32 lstate;
	u32 rstate;

	unsigned long flags;

	struct workqueue_struct *state_wq;
	struct work_struct state_work;

	struct ivshm_net_stats stats;

	struct ivshmem_regs __iomem *ivshm_regs;
	void *shm;
	phys_addr_t shmaddr;
	resource_size_t shmlen;
	u32 peer_id;

	struct pci_dev *pdev;
};

static void *ivshm_net_desc_data(struct ivshm_net *in,
				 struct ivshm_net_queue *q,
				 struct vring_desc *desc,
				 u32 *len)
{
	u64 offs = READ_ONCE(desc->addr);
	u32 dlen = READ_ONCE(desc->len);
	u16 flags = READ_ONCE(desc->flags);
	void *data;

	if (flags)
		return NULL;

	if (offs >= in->shmlen)
		return NULL;

	data = in->shm + offs;

	if (data < q->data || data >= q->end)
		return NULL;

	if (dlen > q->end - data)
		return NULL;

	*len = dlen;

	return data;
}

static void ivshm_net_init_queue(struct ivshm_net *in,
				 struct ivshm_net_queue *q,
				 void *mem, unsigned int len)
{
	memset(q, 0, sizeof(*q));

	vring_init(&q->vr, len, mem, IVSHM_NET_VQ_ALIGN);
	q->data = mem + in->vrsize;
	q->end = q->data + in->qsize;
	q->size = in->qsize;
}

static void ivshm_net_init_queues(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);
	int ivpos = readl(&in->ivshm_regs->ivpos);
	void *tx;
	void *rx;
	int i;

	tx = in->shm +  ivpos * in->shmlen / 2;
	rx = in->shm + !ivpos * in->shmlen / 2;

	memset(tx, 0, in->shmlen / 2);

	ivshm_net_init_queue(in, &in->rx, rx, in->qlen);
	ivshm_net_init_queue(in, &in->tx, tx, in->qlen);

	swap(in->rx.vr.used, in->tx.vr.used);

	in->tx.num_free = in->tx.vr.num;

	for (i = 0; i < in->tx.vr.num - 1; i++)
		in->tx.vr.desc[i].next = i + 1;
}

static int ivshm_net_calc_qsize(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);
	unsigned int vrsize;
	unsigned int qsize;
	unsigned int qlen;

	for (qlen = 4096; qlen > 32; qlen >>= 1) {
		vrsize = vring_size(qlen, IVSHM_NET_VQ_ALIGN);
		vrsize = ALIGN(vrsize, IVSHM_NET_VQ_ALIGN);
		if (vrsize < in->shmlen / 16)
			break;
	}

	if (vrsize > in->shmlen / 2)
		return -EINVAL;

	qsize = in->shmlen / 2 - vrsize;

	if (qsize < 4 * IVSHM_NET_MTU_MIN)
		return -EINVAL;

	in->vrsize = vrsize;
	in->qlen = qlen;
	in->qsize = qsize;

	return 0;
}

static void ivshm_net_notify_tx(struct ivshm_net *in, unsigned int num)
{
	u16 evt, old, new;

	virt_mb();

	evt = READ_ONCE(vring_avail_event(&in->tx.vr));
	old = in->tx.last_avail_idx - num;
	new = in->tx.last_avail_idx;

	if (vring_need_event(evt, new, old)) {
		writel(in->peer_id << 16, &in->ivshm_regs->doorbell);
		in->stats.tx_notify++;
	}
}

static void ivshm_net_enable_rx_irq(struct ivshm_net *in)
{
	vring_avail_event(&in->rx.vr) = in->rx.last_avail_idx;
	virt_wmb();
}

static void ivshm_net_notify_rx(struct ivshm_net *in, unsigned int num)
{
	u16 evt, old, new;

	virt_mb();

	evt = vring_used_event(&in->rx.vr);
	old = in->rx.last_used_idx - num;
	new = in->rx.last_used_idx;

	if (vring_need_event(evt, new, old)) {
		writel(in->peer_id << 16, &in->ivshm_regs->doorbell);
		in->stats.rx_notify++;
	}
}

static void ivshm_net_enable_tx_irq(struct ivshm_net *in)
{
	vring_used_event(&in->tx.vr) = in->tx.last_used_idx;
	virt_wmb();
}

static bool ivshm_net_rx_avail(struct ivshm_net *in)
{
	virt_mb();
	return READ_ONCE(in->rx.vr.avail->idx) != in->rx.last_avail_idx;
}

static size_t ivshm_net_tx_space(struct ivshm_net *in)
{
	struct ivshm_net_queue *tx = &in->tx;
	u32 tail = tx->tail;
	u32 head = tx->head;
	u32 space;

	if (head < tail)
		space = tail - head;
	else
		space = max(tx->size - head, tail);

	return space;
}

static bool ivshm_net_tx_ok(struct ivshm_net *in, unsigned int mtu)
{
	return in->tx.num_free >= 2 &&
		ivshm_net_tx_space(in) >= 2 * IVSHM_NET_FRAME_SIZE(mtu);
}

static u32 ivshm_net_tx_advance(struct ivshm_net_queue *q, u32 *pos, u32 len)
{
	u32 p = *pos;

	len = IVSHM_NET_FRAME_SIZE(len);

	if (q->size - p < len)
		p = 0;
	*pos = p + len;

	return p;
}

static int ivshm_net_tx_frame(struct net_device *ndev, struct sk_buff *skb)
{
	struct ivshm_net *in = netdev_priv(ndev);
	struct ivshm_net_queue *tx = &in->tx;
	struct vring *vr = &tx->vr;
	struct vring_desc *desc;
	unsigned int desc_idx;
	unsigned int avail;
	u32 head;
	void *buf;

	BUG_ON(tx->num_free < 1);

	spin_lock(&in->tx_free_lock);
	desc_idx = tx->free_head;
	desc = &vr->desc[desc_idx];
	tx->free_head = desc->next;
	tx->num_free--;
	spin_unlock(&in->tx_free_lock);

	head = ivshm_net_tx_advance(tx, &tx->head, skb->len);

	buf = tx->data + head;
	skb_copy_and_csum_dev(skb, buf);

	desc->addr = buf - in->shm;
	desc->len = skb->len;
	desc->flags = 0;

	avail = tx->last_avail_idx++ & (vr->num - 1);
	vr->avail->ring[avail] = desc_idx;
	tx->num_added++;

	if (!skb->xmit_more) {
		virt_store_release(&vr->avail->idx, tx->last_avail_idx);
		ivshm_net_notify_tx(in, tx->num_added);
		tx->num_added = 0;
	}

	return 0;
}

static void ivshm_net_tx_clean(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);
	struct ivshm_net_queue *tx = &in->tx;
	struct vring_used_elem *used;
	struct vring *vr = &tx->vr;
	struct vring_desc *desc;
	struct vring_desc *fdesc;
	unsigned int num;
	u16 used_idx;
	u16 last;
	u32 fhead;

	if (!spin_trylock(&in->tx_clean_lock))
		return;

	used_idx = virt_load_acquire(&vr->used->idx);
	last = tx->last_used_idx;

	fdesc = NULL;
	fhead = 0;
	num = 0;

	while (last != used_idx) {
		void *data;
		u32 len;
		u32 tail;

		used = vr->used->ring + (last % vr->num);
		if (used->id >= vr->num || used->len != 1) {
			netdev_err(ndev, "invalid tx used->id %d ->len %d\n",
				   used->id, used->len);
			break;
		}

		desc = &vr->desc[used->id];

		data = ivshm_net_desc_data(in, &in->tx, desc, &len);
		if (!data) {
			netdev_err(ndev, "bad tx descriptor, data == NULL\n");
			break;
		}

		tail = ivshm_net_tx_advance(tx, &tx->tail, len);
		if (data != tx->data + tail) {
			netdev_err(ndev, "bad tx descriptor\n");
			break;
		}

		if (!num)
			fdesc = desc;
		else
			desc->next = fhead;

		fhead = used->id;
		last++;
		num++;
	}

	tx->last_used_idx = last;

	spin_unlock(&in->tx_clean_lock);

	if (num) {
		spin_lock(&in->tx_free_lock);
		fdesc->next = tx->free_head;
		tx->free_head = fhead;
		tx->num_free += num;
		BUG_ON(tx->num_free > vr->num);
		spin_unlock(&in->tx_free_lock);
	}
}

static struct vring_desc *ivshm_net_rx_desc(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);
	struct ivshm_net_queue *rx = &in->rx;
	struct vring *vr = &rx->vr;
	unsigned int avail;
	u16 avail_idx;

	avail_idx = virt_load_acquire(&vr->avail->idx);

	if (avail_idx == rx->last_avail_idx)
		return NULL;

	avail = vr->avail->ring[rx->last_avail_idx++ & (vr->num - 1)];
	if (avail >= vr->num) {
		netdev_err(ndev, "invalid rx avail %d\n", avail);
		return NULL;
	}

	return &vr->desc[avail];
}

static void ivshm_net_rx_finish(struct ivshm_net *in, struct vring_desc *desc)
{
	struct ivshm_net_queue *rx = &in->rx;
	struct vring *vr = &rx->vr;
	unsigned int desc_id = desc - vr->desc;
	unsigned int used;

	used = rx->last_used_idx++ & (vr->num - 1);
	vr->used->ring[used].id = desc_id;
	vr->used->ring[used].len = 1;

	virt_store_release(&vr->used->idx, rx->last_used_idx);
}

static int ivshm_net_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct ivshm_net *in = container_of(napi, struct ivshm_net, napi);
	int received = 0;

	in->stats.napi_poll++;

	ivshm_net_tx_clean(ndev);

	while (received < budget) {
		struct vring_desc *desc;
		struct sk_buff *skb;
		void *data;
		u32 len;

		desc = ivshm_net_rx_desc(ndev);
		if (!desc)
			break;

		data = ivshm_net_desc_data(in, &in->rx, desc, &len);
		if (!data) {
			netdev_err(ndev, "bad rx descriptor\n");
			break;
		}

		skb = napi_alloc_skb(napi, len);

		if (skb) {
			memcpy(skb_put(skb, len), data, len);
			skb->protocol = eth_type_trans(skb, ndev);
			napi_gro_receive(napi, skb);
		}

		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += len;

		ivshm_net_rx_finish(in, desc);
		received++;
	}

	if (received < budget) {
		in->stats.napi_complete++;
		napi_complete_done(napi, received);
		ivshm_net_enable_rx_irq(in);
		if (ivshm_net_rx_avail(in))
			napi_schedule(napi);
	}

	if (received)
		ivshm_net_notify_rx(in, received);

	in->stats.rx_packets += received;
	in->stats.napi_poll_n[received ? 1 + min(ilog2(received), 8) : 0]++;

	if (ivshm_net_tx_ok(in, ndev->mtu))
		netif_wake_queue(ndev);

	return received;
}

static netdev_tx_t ivshm_net_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);

	ivshm_net_tx_clean(ndev);

	if (!ivshm_net_tx_ok(in, ndev->mtu)) {
		ivshm_net_enable_tx_irq(in);
		netif_stop_queue(ndev);
		skb->xmit_more = 0;
		in->stats.tx_pause++;
	}

	ivshm_net_tx_frame(ndev, skb);

	in->stats.tx_packets++;
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;

	dev_consume_skb_any(skb);

	return NETDEV_TX_OK;
}

static void ivshm_net_set_state(struct ivshm_net *in, u32 state)
{
	virt_wmb();
	WRITE_ONCE(in->lstate, state);
	writel(state, &in->ivshm_regs->lstate);
}

static void ivshm_net_run(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);

	if (in->lstate < IVSHM_NET_STATE_READY)
		return;

	if (!netif_running(ndev))
		return;

	if (test_and_set_bit(IVSHM_NET_FLAG_RUN, &in->flags))
		return;

	netif_start_queue(ndev);
	napi_enable(&in->napi);
	napi_schedule(&in->napi);
	ivshm_net_set_state(in, IVSHM_NET_STATE_RUN);
}

static void ivshm_net_do_stop(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);

	ivshm_net_set_state(in, IVSHM_NET_STATE_RESET);

	if (!test_and_clear_bit(IVSHM_NET_FLAG_RUN, &in->flags))
		return;

	netif_stop_queue(ndev);
	napi_disable(&in->napi);
}

static void ivshm_net_state_change(struct work_struct *work)
{
	struct ivshm_net *in = container_of(work, struct ivshm_net, state_work);
	struct net_device *ndev = in->napi.dev;
	u32 rstate = readl(&in->ivshm_regs->rstate);


	switch (in->lstate) {
	case IVSHM_NET_STATE_RESET:
		if (rstate < IVSHM_NET_STATE_READY)
			ivshm_net_set_state(in, IVSHM_NET_STATE_INIT);
		break;

	case IVSHM_NET_STATE_INIT:
		if (rstate > IVSHM_NET_STATE_RESET) {
			ivshm_net_init_queues(ndev);
			ivshm_net_set_state(in, IVSHM_NET_STATE_READY);

			rtnl_lock();
			call_netdevice_notifiers(NETDEV_CHANGEADDR, ndev);
			rtnl_unlock();
		}
		break;

	case IVSHM_NET_STATE_READY:
	case IVSHM_NET_STATE_RUN:
		if (rstate >= IVSHM_NET_STATE_READY) {
			netif_carrier_on(ndev);
			ivshm_net_run(ndev);
		} else {
			netif_carrier_off(ndev);
			ivshm_net_do_stop(ndev);
		}
		break;
	}

	virt_wmb();
	WRITE_ONCE(in->rstate, rstate);
}

static void ivshm_net_check_state(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);
	u32 rstate = readl(&in->ivshm_regs->rstate);

	if (rstate != in->rstate || !test_bit(IVSHM_NET_FLAG_RUN, &in->flags))
		queue_work(in->state_wq, &in->state_work);
}

static irqreturn_t ivshm_net_int(int irq, void *data)
{
	struct net_device *ndev = data;
	struct ivshm_net *in = netdev_priv(ndev);

	in->stats.interrupts++;

	ivshm_net_check_state(ndev);
	napi_schedule_irqoff(&in->napi);

	return IRQ_HANDLED;
}

static int ivshm_net_open(struct net_device *ndev)
{
	netdev_reset_queue(ndev);
	ndev->operstate = IF_OPER_UP;
	ivshm_net_run(ndev);

	return 0;
}

static int ivshm_net_stop(struct net_device *ndev)
{
	ndev->operstate = IF_OPER_DOWN;
	ivshm_net_do_stop(ndev);

	return 0;
}

static int ivshm_net_change_mtu(struct net_device *ndev, int mtu)
{
	struct ivshm_net *in = netdev_priv(ndev);
	struct ivshm_net_queue *tx = &in->tx;

	if (mtu < IVSHM_NET_MTU_MIN || mtu > IVSHM_NET_MTU_MAX)
		return -EINVAL;

	if (in->tx.size / mtu < 4)
		return -EINVAL;

	if (ivshm_net_tx_space(in) < 2 * IVSHM_NET_FRAME_SIZE(mtu))
		return -EBUSY;

	if (in->tx.size - tx->head < IVSHM_NET_FRAME_SIZE(mtu) &&
	    tx->head < tx->tail)
		return -EBUSY;

	netif_tx_lock_bh(ndev);
	if (in->tx.size - tx->head < IVSHM_NET_FRAME_SIZE(mtu))
		tx->head = 0;
	netif_tx_unlock_bh(ndev);

	ndev->mtu = mtu;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ivshm_net_poll_controller(struct net_device *ndev)
{
	struct ivshm_net *in = netdev_priv(ndev);

	napi_schedule(&in->napi);
}
#endif

static const struct net_device_ops ivshm_net_ops = {
	.ndo_open		= ivshm_net_open,
	.ndo_stop		= ivshm_net_stop,
	.ndo_start_xmit		= ivshm_net_xmit,
	.ndo_change_mtu		= ivshm_net_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ivshm_net_poll_controller,
#endif
};

static const char ivshm_net_stats[][ETH_GSTRING_LEN] = {
	"interrupts",
	"tx_packets",
	"tx_notify",
	"tx_pause",
	"rx_packets",
	"rx_notify",
	"napi_poll",
	"napi_complete",
	"napi_poll_0",
	"napi_poll_1",
	"napi_poll_2",
	"napi_poll_4",
	"napi_poll_8",
	"napi_poll_16",
	"napi_poll_32",
	"napi_poll_64",
	"napi_poll_128",
	"napi_poll_256",
};

#define NUM_STATS ARRAY_SIZE(ivshm_net_stats)

static int ivshm_net_get_sset_count(struct net_device *ndev, int sset)
{
	if (sset == ETH_SS_STATS)
		return NUM_STATS;

	return -EOPNOTSUPP;
}

static void ivshm_net_get_strings(struct net_device *ndev, u32 sset, u8 *buf)
{
	if (sset == ETH_SS_STATS)
		memcpy(buf, &ivshm_net_stats, sizeof(ivshm_net_stats));
}

static void ivshm_net_get_ethtool_stats(struct net_device *ndev,
					struct ethtool_stats *estats, u64 *st)
{
	struct ivshm_net *in = netdev_priv(ndev);
	unsigned int n = 0;
	unsigned int i;

	st[n++] = in->stats.interrupts;
	st[n++] = in->stats.tx_packets;
	st[n++] = in->stats.tx_notify;
	st[n++] = in->stats.tx_pause;
	st[n++] = in->stats.rx_packets;
	st[n++] = in->stats.rx_notify;
	st[n++] = in->stats.napi_poll;
	st[n++] = in->stats.napi_complete;

	for (i = 0; i < ARRAY_SIZE(in->stats.napi_poll_n); i++)
		st[n++] = in->stats.napi_poll_n[i];

	memset(&in->stats, 0, sizeof(in->stats));
}

#define IVSHM_NET_REGS_LEN	(3 * sizeof(u32) + 6 * sizeof(u16))

static int ivshm_net_get_regs_len(struct net_device *ndev)
{
	return IVSHM_NET_REGS_LEN;
}

static void ivshm_net_get_regs(struct net_device *ndev,
			       struct ethtool_regs *regs, void *p)
{
	struct ivshm_net *in = netdev_priv(ndev);
	u32 *reg32 = p;
	u16 *reg16;

	*reg32++ = in->lstate;
	*reg32++ = in->rstate;
	*reg32++ = in->qlen;

	reg16 = (u16 *)reg32;

	*reg16++ = in->tx.vr.avail ? in->tx.vr.avail->idx : 0;
	*reg16++ = in->tx.vr.used ? in->tx.vr.used->idx : 0;
	*reg16++ = in->tx.vr.avail ? vring_avail_event(&in->tx.vr) : 0;

	*reg16++ = in->rx.vr.avail ? in->rx.vr.avail->idx : 0;
	*reg16++ = in->rx.vr.used ? in->rx.vr.used->idx : 0;
	*reg16++ = in->rx.vr.avail ? vring_avail_event(&in->rx.vr) : 0;
}

static const struct ethtool_ops ivshm_net_ethtool_ops = {
	.get_sset_count		= ivshm_net_get_sset_count,
	.get_strings		= ivshm_net_get_strings,
	.get_ethtool_stats	= ivshm_net_get_ethtool_stats,
	.get_regs_len		= ivshm_net_get_regs_len,
	.get_regs		= ivshm_net_get_regs,
};

static int ivshm_net_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct net_device *ndev;
	struct ivshm_net *in;
	struct ivshmem_regs __iomem *regs;
	resource_size_t shmaddr;
	resource_size_t shmlen;
	char *device_name;
	void *shm;
	u32 ivpos;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device: %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0), DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pcim_iomap_regions: %d\n", ret);
		return ret;
	}

	regs = pcim_iomap_table(pdev)[0];

	shmlen = pci_resource_len(pdev, 2);

	if (shmlen) {
		shmaddr = pci_resource_start(pdev, 2);
	} else {
		union { u64 v; u32 hl[2]; } val;

		pci_read_config_dword(pdev, JAILHOUSE_CFG_SHMEM_PTR,
				      &val.hl[0]);
		pci_read_config_dword(pdev, JAILHOUSE_CFG_SHMEM_PTR + 4,
				      &val.hl[1]);
		shmaddr = val.v;

		pci_read_config_dword(pdev, JAILHOUSE_CFG_SHMEM_SZ,
				      &val.hl[0]);
		pci_read_config_dword(pdev, JAILHOUSE_CFG_SHMEM_SZ + 4,
				      &val.hl[1]);
		shmlen = val.v;
	}


	if (!devm_request_mem_region(&pdev->dev, shmaddr, shmlen, DRV_NAME))
		return -EBUSY;

	shm = devm_memremap(&pdev->dev, shmaddr, shmlen, MEMREMAP_WB);
	if (!shm)
		return -ENOMEM;

	ivpos = readl(&regs->ivpos);
	if (ivpos > 1) {
		dev_err(&pdev->dev, "invalid IVPosition %d\n", ivpos);
		return -EINVAL;
	}

	device_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s[%s]", DRV_NAME,
				     dev_name(&pdev->dev));
	if (!device_name)
		return -ENOMEM;

	ndev = alloc_etherdev(sizeof(*in));
	if (!ndev)
		return -ENOMEM;

	pci_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	in = netdev_priv(ndev);
	in->ivshm_regs = regs;
	in->shm = shm;
	in->shmaddr = shmaddr;
	in->shmlen = shmlen;
	in->peer_id = !ivpos;
	in->pdev = pdev;
	spin_lock_init(&in->tx_free_lock);
	spin_lock_init(&in->tx_clean_lock);

	ret = ivshm_net_calc_qsize(ndev);
	if (ret)
		goto err_free;

	in->state_wq = alloc_ordered_workqueue(device_name, 0);
	if (!in->state_wq)
		goto err_free;

	INIT_WORK(&in->state_work, ivshm_net_state_change);

	eth_random_addr(ndev->dev_addr);
	ndev->netdev_ops = &ivshm_net_ops;
	ndev->ethtool_ops = &ivshm_net_ethtool_ops;
	ndev->mtu = min_t(u32, IVSHM_NET_MTU_DEF, in->qsize / 16);
	ndev->hw_features = NETIF_F_HW_CSUM | NETIF_F_SG;
	ndev->features = ndev->hw_features;

	netif_carrier_off(ndev);
	netif_napi_add(ndev, &in->napi, ivshm_net_poll, NAPI_POLL_WEIGHT);

	ret = register_netdev(ndev);
	if (ret)
		goto err_wq;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_LEGACY | PCI_IRQ_MSIX);
	if (ret < 0)
		goto err_alloc_irq;

	ret = request_irq(pci_irq_vector(pdev, 0), ivshm_net_int, 0,
			  device_name, ndev);
	if (ret)
		goto err_request_irq;

	pci_set_master(pdev);
	if (!pdev->msix_enabled)
		writel(IVSHMEM_INTX_ENABLE, &in->ivshm_regs->intxctrl);

	writel(IVSHM_NET_STATE_RESET, &in->ivshm_regs->lstate);
	ivshm_net_check_state(ndev);

	return 0;

err_request_irq:
	pci_free_irq_vectors(pdev);
err_alloc_irq:
	unregister_netdev(ndev);
err_wq:
	destroy_workqueue(in->state_wq);
err_free:
	free_netdev(ndev);

	return ret;
}

static void ivshm_net_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct ivshm_net *in = netdev_priv(ndev);

	writel(IVSHM_NET_STATE_RESET, &in->ivshm_regs->lstate);

	if (!pdev->msix_enabled)
		writel(0, &in->ivshm_regs->intxctrl);
	free_irq(pci_irq_vector(pdev, 0), ndev);
	pci_free_irq_vectors(pdev);

	unregister_netdev(ndev);
	cancel_work_sync(&in->state_work);
	destroy_workqueue(in->state_wq);
	free_netdev(ndev);
}

static const struct pci_device_id ivshm_net_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, 0x1110),
		(PCI_CLASS_OTHERS << 16) | (0x01 << 8), 0xffff00 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ivshm_net_id_table);

static struct pci_driver ivshm_net_driver = {
	.name		= DRV_NAME,
	.id_table	= ivshm_net_id_table,
	.probe		= ivshm_net_probe,
	.remove		= ivshm_net_remove,
};
module_pci_driver(ivshm_net_driver);

MODULE_AUTHOR("Mans Rullgard <mans@mansr.com>");
MODULE_LICENSE("GPL");
