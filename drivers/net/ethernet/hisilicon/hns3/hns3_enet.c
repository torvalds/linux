// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/skbuff.h>
#include <linux/sctp.h>
#include <linux/vermagic.h>
#include <net/gre.h>
#include <net/ip6_checksum.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <net/vxlan.h>

#include "hnae3.h"
#include "hns3_enet.h"

#define hns3_set_field(origin, shift, val)	((origin) |= ((val) << (shift)))
#define hns3_tx_bd_count(S)	DIV_ROUND_UP(S, HNS3_MAX_BD_SIZE)

static void hns3_clear_all_ring(struct hnae3_handle *h);
static void hns3_force_clear_all_rx_ring(struct hnae3_handle *h);
static void hns3_remove_hw_addr(struct net_device *netdev);

static const char hns3_driver_name[] = "hns3";
const char hns3_driver_version[] = VERMAGIC_STRING;
static const char hns3_driver_string[] =
			"Hisilicon Ethernet Network Driver for Hip08 Family";
static const char hns3_copyright[] = "Copyright (c) 2017 Huawei Corporation.";
static struct hnae3_client client;

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, " Network interface message level setting");

#define DEFAULT_MSG_LEVEL (NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			   NETIF_MSG_IFDOWN | NETIF_MSG_IFUP)

/* hns3_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id hns3_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_VF), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_DCB_PFC_VF),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, hns3_pci_tbl);

static irqreturn_t hns3_irq_handle(int irq, void *vector)
{
	struct hns3_enet_tqp_vector *tqp_vector = vector;

	napi_schedule_irqoff(&tqp_vector->napi);

	return IRQ_HANDLED;
}

static void hns3_nic_uninit_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	unsigned int i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag != HNS3_VECTOR_INITED)
			continue;

		/* clear the affinity mask */
		irq_set_affinity_hint(tqp_vectors->vector_irq, NULL);

		/* release the irq resource */
		free_irq(tqp_vectors->vector_irq, tqp_vectors);
		tqp_vectors->irq_init_flag = HNS3_VECTOR_NOT_INITED;
	}
}

static int hns3_nic_init_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	int txrx_int_idx = 0;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag == HNS3_VECTOR_INITED)
			continue;

		if (tqp_vectors->tx_group.ring && tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "TxRx",
				 txrx_int_idx++);
			txrx_int_idx++;
		} else if (tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "Rx",
				 rx_int_idx++);
		} else if (tqp_vectors->tx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN - 1,
				 "%s-%s-%d", priv->netdev->name, "Tx",
				 tx_int_idx++);
		} else {
			/* Skip this unused q_vector */
			continue;
		}

		tqp_vectors->name[HNAE3_INT_NAME_LEN - 1] = '\0';

		ret = request_irq(tqp_vectors->vector_irq, hns3_irq_handle, 0,
				  tqp_vectors->name, tqp_vectors);
		if (ret) {
			netdev_err(priv->netdev, "request irq(%d) fail\n",
				   tqp_vectors->vector_irq);
			hns3_nic_uninit_irq(priv);
			return ret;
		}

		irq_set_affinity_hint(tqp_vectors->vector_irq,
				      &tqp_vectors->affinity_mask);

		tqp_vectors->irq_init_flag = HNS3_VECTOR_INITED;
	}

	return 0;
}

static void hns3_mask_vector_irq(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 mask_en)
{
	writel(mask_en, tqp_vector->mask_addr);
}

static void hns3_vector_enable(struct hns3_enet_tqp_vector *tqp_vector)
{
	napi_enable(&tqp_vector->napi);

	/* enable vector */
	hns3_mask_vector_irq(tqp_vector, 1);
}

static void hns3_vector_disable(struct hns3_enet_tqp_vector *tqp_vector)
{
	/* disable vector */
	hns3_mask_vector_irq(tqp_vector, 0);

	disable_irq(tqp_vector->vector_irq);
	napi_disable(&tqp_vector->napi);
}

void hns3_set_vector_coalesce_rl(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 rl_value)
{
	u32 rl_reg = hns3_rl_usec_to_reg(rl_value);

	/* this defines the configuration for RL (Interrupt Rate Limiter).
	 * Rl defines rate of interrupts i.e. number of interrupts-per-second
	 * GL and RL(Rate Limiter) are 2 ways to acheive interrupt coalescing
	 */

	if (rl_reg > 0 && !tqp_vector->tx_group.coal.gl_adapt_enable &&
	    !tqp_vector->rx_group.coal.gl_adapt_enable)
		/* According to the hardware, the range of rl_reg is
		 * 0-59 and the unit is 4.
		 */
		rl_reg |=  HNS3_INT_RL_ENABLE_MASK;

	writel(rl_reg, tqp_vector->mask_addr + HNS3_VECTOR_RL_OFFSET);
}

void hns3_set_vector_coalesce_rx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 rx_gl_reg = hns3_gl_usec_to_reg(gl_value);

	writel(rx_gl_reg, tqp_vector->mask_addr + HNS3_VECTOR_GL0_OFFSET);
}

void hns3_set_vector_coalesce_tx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 tx_gl_reg = hns3_gl_usec_to_reg(gl_value);

	writel(tx_gl_reg, tqp_vector->mask_addr + HNS3_VECTOR_GL1_OFFSET);
}

static void hns3_vector_gl_rl_init(struct hns3_enet_tqp_vector *tqp_vector,
				   struct hns3_nic_priv *priv)
{
	/* initialize the configuration for interrupt coalescing.
	 * 1. GL (Interrupt Gap Limiter)
	 * 2. RL (Interrupt Rate Limiter)
	 */

	/* Default: enable interrupt coalescing self-adaptive and GL */
	tqp_vector->tx_group.coal.gl_adapt_enable = 1;
	tqp_vector->rx_group.coal.gl_adapt_enable = 1;

	tqp_vector->tx_group.coal.int_gl = HNS3_INT_GL_50K;
	tqp_vector->rx_group.coal.int_gl = HNS3_INT_GL_50K;

	tqp_vector->rx_group.coal.flow_level = HNS3_FLOW_LOW;
	tqp_vector->tx_group.coal.flow_level = HNS3_FLOW_LOW;
}

static void hns3_vector_gl_rl_init_hw(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;

	hns3_set_vector_coalesce_tx_gl(tqp_vector,
				       tqp_vector->tx_group.coal.int_gl);
	hns3_set_vector_coalesce_rx_gl(tqp_vector,
				       tqp_vector->rx_group.coal.int_gl);
	hns3_set_vector_coalesce_rl(tqp_vector, h->kinfo.int_rl_setting);
}

static int hns3_nic_set_real_num_queue(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	unsigned int queue_size = kinfo->rss_size * kinfo->num_tc;
	int i, ret;

	if (kinfo->num_tc <= 1) {
		netdev_reset_tc(netdev);
	} else {
		ret = netdev_set_num_tc(netdev, kinfo->num_tc);
		if (ret) {
			netdev_err(netdev,
				   "netdev_set_num_tc fail, ret=%d!\n", ret);
			return ret;
		}

		for (i = 0; i < HNAE3_MAX_TC; i++) {
			if (!kinfo->tc_info[i].enable)
				continue;

			netdev_set_tc_queue(netdev,
					    kinfo->tc_info[i].tc,
					    kinfo->tc_info[i].tqp_count,
					    kinfo->tc_info[i].tqp_offset);
		}
	}

	ret = netif_set_real_num_tx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_tx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	ret = netif_set_real_num_rx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_rx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

static u16 hns3_get_max_available_channels(struct hnae3_handle *h)
{
	u16 alloc_tqps, max_rss_size, rss_size;

	h->ae_algo->ops->get_tqps_and_rss_info(h, &alloc_tqps, &max_rss_size);
	rss_size = alloc_tqps / h->kinfo.num_tc;

	return min_t(u16, rss_size, max_rss_size);
}

static void hns3_tqp_enable(struct hnae3_queue *tqp)
{
	u32 rcb_reg;

	rcb_reg = hns3_read_dev(tqp, HNS3_RING_EN_REG);
	rcb_reg |= BIT(HNS3_RING_EN_B);
	hns3_write_dev(tqp, HNS3_RING_EN_REG, rcb_reg);
}

static void hns3_tqp_disable(struct hnae3_queue *tqp)
{
	u32 rcb_reg;

	rcb_reg = hns3_read_dev(tqp, HNS3_RING_EN_REG);
	rcb_reg &= ~BIT(HNS3_RING_EN_B);
	hns3_write_dev(tqp, HNS3_RING_EN_REG, rcb_reg);
}

static void hns3_free_rx_cpu_rmap(struct net_device *netdev)
{
#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(netdev->rx_cpu_rmap);
	netdev->rx_cpu_rmap = NULL;
#endif
}

static int hns3_set_rx_cpu_rmap(struct net_device *netdev)
{
#ifdef CONFIG_RFS_ACCEL
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_enet_tqp_vector *tqp_vector;
	int i, ret;

	if (!netdev->rx_cpu_rmap) {
		netdev->rx_cpu_rmap = alloc_irq_cpu_rmap(priv->vector_num);
		if (!netdev->rx_cpu_rmap)
			return -ENOMEM;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		ret = irq_cpu_rmap_add(netdev->rx_cpu_rmap,
				       tqp_vector->vector_irq);
		if (ret) {
			hns3_free_rx_cpu_rmap(netdev);
			return ret;
		}
	}
#endif
	return 0;
}

static int hns3_nic_net_up(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int i, j;
	int ret;

	ret = hns3_nic_reset_all_ring(h);
	if (ret)
		return ret;

	/* the device can work without cpu rmap, only aRFS needs it */
	ret = hns3_set_rx_cpu_rmap(netdev);
	if (ret)
		netdev_warn(netdev, "set rx cpu rmap fail, ret=%d!\n", ret);

	/* get irq resource for all vectors */
	ret = hns3_nic_init_irq(priv);
	if (ret) {
		netdev_err(netdev, "init irq failed! ret=%d\n", ret);
		goto free_rmap;
	}

	clear_bit(HNS3_NIC_STATE_DOWN, &priv->state);

	/* enable the vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_enable(&priv->tqp_vector[i]);

	/* enable rcb */
	for (j = 0; j < h->kinfo.num_tqps; j++)
		hns3_tqp_enable(h->kinfo.tqp[j]);

	/* start the ae_dev */
	ret = h->ae_algo->ops->start ? h->ae_algo->ops->start(h) : 0;
	if (ret)
		goto out_start_err;

	return 0;

out_start_err:
	set_bit(HNS3_NIC_STATE_DOWN, &priv->state);
	while (j--)
		hns3_tqp_disable(h->kinfo.tqp[j]);

	for (j = i - 1; j >= 0; j--)
		hns3_vector_disable(&priv->tqp_vector[j]);

	hns3_nic_uninit_irq(priv);
free_rmap:
	hns3_free_rx_cpu_rmap(netdev);
	return ret;
}

static void hns3_config_xps(struct hns3_nic_priv *priv)
{
	int i;

	for (i = 0; i < priv->vector_num; i++) {
		struct hns3_enet_tqp_vector *tqp_vector = &priv->tqp_vector[i];
		struct hns3_enet_ring *ring = tqp_vector->tx_group.ring;

		while (ring) {
			int ret;

			ret = netif_set_xps_queue(priv->netdev,
						  &tqp_vector->affinity_mask,
						  ring->tqp->tqp_index);
			if (ret)
				netdev_warn(priv->netdev,
					    "set xps queue failed: %d", ret);

			ring = ring->next;
		}
	}
}

static int hns3_nic_net_open(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo;
	int i, ret;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	netif_carrier_off(netdev);

	ret = hns3_nic_set_real_num_queue(netdev);
	if (ret)
		return ret;

	ret = hns3_nic_net_up(netdev);
	if (ret) {
		netdev_err(netdev, "net up fail, ret=%d!\n", ret);
		return ret;
	}

	kinfo = &h->kinfo;
	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++)
		netdev_set_prio_tc_map(netdev, i, kinfo->prio_tc[i]);

	if (h->ae_algo->ops->set_timer_task)
		h->ae_algo->ops->set_timer_task(priv->ae_handle, true);

	hns3_config_xps(priv);
	return 0;
}

static void hns3_nic_net_down(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops;
	int i;

	/* disable vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_disable(&priv->tqp_vector[i]);

	/* disable rcb */
	for (i = 0; i < h->kinfo.num_tqps; i++)
		hns3_tqp_disable(h->kinfo.tqp[i]);

	/* stop ae_dev */
	ops = priv->ae_handle->ae_algo->ops;
	if (ops->stop)
		ops->stop(priv->ae_handle);

	hns3_free_rx_cpu_rmap(netdev);

	/* free irq resources */
	hns3_nic_uninit_irq(priv);

	hns3_clear_all_ring(priv->ae_handle);
}

static int hns3_nic_net_stop(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (test_and_set_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return 0;

	if (h->ae_algo->ops->set_timer_task)
		h->ae_algo->ops->set_timer_task(priv->ae_handle, false);

	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);

	hns3_nic_net_down(netdev);

	return 0;
}

static int hns3_nic_uc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_uc_addr)
		return h->ae_algo->ops->add_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_uc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->rm_uc_addr)
		return h->ae_algo->ops->rm_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_mc_addr)
		return h->ae_algo->ops->add_mc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->rm_mc_addr)
		return h->ae_algo->ops->rm_mc_addr(h, addr);

	return 0;
}

static u8 hns3_get_netdev_flags(struct net_device *netdev)
{
	u8 flags = 0;

	if (netdev->flags & IFF_PROMISC) {
		flags = HNAE3_USER_UPE | HNAE3_USER_MPE | HNAE3_BPE;
	} else {
		flags |= HNAE3_VLAN_FLTR;
		if (netdev->flags & IFF_ALLMULTI)
			flags |= HNAE3_USER_MPE;
	}

	return flags;
}

static void hns3_nic_set_rx_mode(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	u8 new_flags;
	int ret;

	new_flags = hns3_get_netdev_flags(netdev);

	ret = __dev_uc_sync(netdev, hns3_nic_uc_sync, hns3_nic_uc_unsync);
	if (ret) {
		netdev_err(netdev, "sync uc address fail\n");
		if (ret == -ENOSPC)
			new_flags |= HNAE3_OVERFLOW_UPE;
	}

	if (netdev->flags & IFF_MULTICAST) {
		ret = __dev_mc_sync(netdev, hns3_nic_mc_sync,
				    hns3_nic_mc_unsync);
		if (ret) {
			netdev_err(netdev, "sync mc address fail\n");
			if (ret == -ENOSPC)
				new_flags |= HNAE3_OVERFLOW_MPE;
		}
	}

	/* User mode Promisc mode enable and vlan filtering is disabled to
	 * let all packets in. MAC-VLAN Table overflow Promisc enabled and
	 * vlan fitering is enabled
	 */
	hns3_enable_vlan_filter(netdev, new_flags & HNAE3_VLAN_FLTR);
	h->netdev_flags = new_flags;
	hns3_update_promisc_mode(netdev, new_flags);
}

int hns3_update_promisc_mode(struct net_device *netdev, u8 promisc_flags)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;

	if (h->ae_algo->ops->set_promisc_mode) {
		return h->ae_algo->ops->set_promisc_mode(h,
						promisc_flags & HNAE3_UPE,
						promisc_flags & HNAE3_MPE);
	}

	return 0;
}

void hns3_enable_vlan_filter(struct net_device *netdev, bool enable)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	bool last_state;

	if (h->pdev->revision >= 0x21 && h->ae_algo->ops->enable_vlan_filter) {
		last_state = h->netdev_flags & HNAE3_VLAN_FLTR ? true : false;
		if (enable != last_state) {
			netdev_info(netdev,
				    "%s vlan filter\n",
				    enable ? "enable" : "disable");
			h->ae_algo->ops->enable_vlan_filter(h, enable);
		}
	}
}

static int hns3_set_tso(struct sk_buff *skb, u32 *paylen,
			u16 *mss, u32 *type_cs_vlan_tso)
{
	u32 l4_offset, hdr_len;
	union l3_hdr_info l3;
	union l4_hdr_info l4;
	u32 l4_paylen;
	int ret;

	if (!skb_is_gso(skb))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (unlikely(ret))
		return ret;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* Software should clear the IPv4's checksum field when tso is
	 * needed.
	 */
	if (l3.v4->version == 4)
		l3.v4->check = 0;

	/* tunnel packet */
	if (skb_shinfo(skb)->gso_type & (SKB_GSO_GRE |
					 SKB_GSO_GRE_CSUM |
					 SKB_GSO_UDP_TUNNEL |
					 SKB_GSO_UDP_TUNNEL_CSUM)) {
		if ((!(skb_shinfo(skb)->gso_type &
		    SKB_GSO_PARTIAL)) &&
		    (skb_shinfo(skb)->gso_type &
		    SKB_GSO_UDP_TUNNEL_CSUM)) {
			/* Software should clear the udp's checksum
			 * field when tso is needed.
			 */
			l4.udp->check = 0;
		}
		/* reset l3&l4 pointers from outer to inner headers */
		l3.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);

		/* Software should clear the IPv4's checksum field when
		 * tso is needed.
		 */
		if (l3.v4->version == 4)
			l3.v4->check = 0;
	}

	/* normal or tunnel packet */
	l4_offset = l4.hdr - skb->data;
	hdr_len = (l4.tcp->doff << 2) + l4_offset;

	/* remove payload length from inner pseudo checksum when tso */
	l4_paylen = skb->len - l4_offset;
	csum_replace_by_diff(&l4.tcp->check,
			     (__force __wsum)htonl(l4_paylen));

	/* find the txbd field values */
	*paylen = skb->len - hdr_len;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_TSO_B, 1);

	/* get MSS for TSO */
	*mss = skb_shinfo(skb)->gso_size;

	return 0;
}

static int hns3_get_l4_protocol(struct sk_buff *skb, u8 *ol4_proto,
				u8 *il4_proto)
{
	union l3_hdr_info l3;
	unsigned char *l4_hdr;
	unsigned char *exthdr;
	u8 l4_proto_tmp;
	__be16 frag_off;

	/* find outer header point */
	l3.hdr = skb_network_header(skb);
	l4_hdr = skb_transport_header(skb);

	if (skb->protocol == htons(ETH_P_IPV6)) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (skb->protocol == htons(ETH_P_IP)) {
		l4_proto_tmp = l3.v4->protocol;
	} else {
		return -EINVAL;
	}

	*ol4_proto = l4_proto_tmp;

	/* tunnel packet */
	if (!skb->encapsulation) {
		*il4_proto = 0;
		return 0;
	}

	/* find inner header point */
	l3.hdr = skb_inner_network_header(skb);
	l4_hdr = skb_inner_transport_header(skb);

	if (l3.v6->version == 6) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (l3.v4->version == 4) {
		l4_proto_tmp = l3.v4->protocol;
	}

	*il4_proto = l4_proto_tmp;

	return 0;
}

/* when skb->encapsulation is 0, skb->ip_summed is CHECKSUM_PARTIAL
 * and it is udp packet, which has a dest port as the IANA assigned.
 * the hardware is expected to do the checksum offload, but the
 * hardware will not do the checksum offload when udp dest port is
 * 4789.
 */
static bool hns3_tunnel_csum_bug(struct sk_buff *skb)
{
	union l4_hdr_info l4;

	l4.hdr = skb_transport_header(skb);

	if (!(!skb->encapsulation &&
	      l4.udp->dest == htons(IANA_VXLAN_UDP_PORT)))
		return false;

	skb_checksum_help(skb);

	return true;
}

static void hns3_set_outer_l2l3l4(struct sk_buff *skb, u8 ol4_proto,
				  u32 *ol_type_vlan_len_msec)
{
	u32 l2_len, l3_len, l4_len;
	unsigned char *il2_hdr;
	union l3_hdr_info l3;
	union l4_hdr_info l4;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute OL2 header size, defined in 2 Bytes */
	l2_len = l3.hdr - skb->data;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L2LEN_S, l2_len >> 1);

	/* compute OL3 header size, defined in 4 Bytes */
	l3_len = l4.hdr - l3.hdr;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L3LEN_S, l3_len >> 2);

	il2_hdr = skb_inner_mac_header(skb);
	/* compute OL4 header size, defined in 4 Bytes */
	l4_len = il2_hdr - l4.hdr;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L4LEN_S, l4_len >> 2);

	/* define outer network header type */
	if (skb->protocol == htons(ETH_P_IP)) {
		if (skb_is_gso(skb))
			hns3_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_OL3T_S,
				       HNS3_OL3T_IPV4_CSUM);
		else
			hns3_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_OL3T_S,
				       HNS3_OL3T_IPV4_NO_CSUM);

	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_OL3T_S,
			       HNS3_OL3T_IPV6);
	}

	if (ol4_proto == IPPROTO_UDP)
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_TUNTYPE_S,
			       HNS3_TUN_MAC_IN_UDP);
	else if (ol4_proto == IPPROTO_GRE)
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_TUNTYPE_S,
			       HNS3_TUN_NVGRE);
}

static int hns3_set_l2l3l4(struct sk_buff *skb, u8 ol4_proto,
			   u8 il4_proto, u32 *type_cs_vlan_tso,
			   u32 *ol_type_vlan_len_msec)
{
	unsigned char *l2_hdr = skb->data;
	u32 l4_proto = ol4_proto;
	union l4_hdr_info l4;
	union l3_hdr_info l3;
	u32 l2_len, l3_len;

	l4.hdr = skb_transport_header(skb);
	l3.hdr = skb_network_header(skb);

	/* handle encapsulation skb */
	if (skb->encapsulation) {
		/* If this is a not UDP/GRE encapsulation skb */
		if (!(ol4_proto == IPPROTO_UDP || ol4_proto == IPPROTO_GRE)) {
			/* drop the skb tunnel packet if hardware don't support,
			 * because hardware can't calculate csum when TSO.
			 */
			if (skb_is_gso(skb))
				return -EDOM;

			/* the stack computes the IP header already,
			 * driver calculate l4 checksum when not TSO.
			 */
			skb_checksum_help(skb);
			return 0;
		}

		hns3_set_outer_l2l3l4(skb, ol4_proto, ol_type_vlan_len_msec);

		/* switch to inner header */
		l2_hdr = skb_inner_mac_header(skb);
		l3.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
		l4_proto = il4_proto;
	}

	if (l3.v4->version == 4) {
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_S,
			       HNS3_L3T_IPV4);

		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (skb_is_gso(skb))
			hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3CS_B, 1);
	} else if (l3.v6->version == 6) {
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_S,
			       HNS3_L3T_IPV6);
	}

	/* compute inner(/normal) L2 header size, defined in 2 Bytes */
	l2_len = l3.hdr - l2_hdr;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L2LEN_S, l2_len >> 1);

	/* compute inner(/normal) L3 header size, defined in 4 Bytes */
	l3_len = l4.hdr - l3.hdr;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3LEN_S, l3_len >> 2);

	/* compute inner(/normal) L4 header size, defined in 4 Bytes */
	switch (l4_proto) {
	case IPPROTO_TCP:
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_TCP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       l4.tcp->doff);
		break;
	case IPPROTO_UDP:
		if (hns3_tunnel_csum_bug(skb))
			break;

		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_UDP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       (sizeof(struct udphdr) >> 2));
		break;
	case IPPROTO_SCTP:
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_SCTP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       (sizeof(struct sctphdr) >> 2));
		break;
	default:
		/* drop the skb tunnel packet if hardware don't support,
		 * because hardware can't calculate csum when TSO.
		 */
		if (skb_is_gso(skb))
			return -EDOM;

		/* the stack computes the IP header already,
		 * driver calculate l4 checksum when not TSO.
		 */
		skb_checksum_help(skb);
		return 0;
	}

	return 0;
}

static void hns3_set_txbd_baseinfo(u16 *bdtp_fe_sc_vld_ra_ri, int frag_end)
{
	/* Config bd buffer end */
	hns3_set_field(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_FE_B, !!frag_end);
	hns3_set_field(*bdtp_fe_sc_vld_ra_ri, HNS3_TXD_VLD_B, 1);
}

static int hns3_fill_desc_vtags(struct sk_buff *skb,
				struct hns3_enet_ring *tx_ring,
				u32 *inner_vlan_flag,
				u32 *out_vlan_flag,
				u16 *inner_vtag,
				u16 *out_vtag)
{
#define HNS3_TX_VLAN_PRIO_SHIFT 13

	struct hnae3_handle *handle = tx_ring->tqp->handle;

	/* Since HW limitation, if port based insert VLAN enabled, only one VLAN
	 * header is allowed in skb, otherwise it will cause RAS error.
	 */
	if (unlikely(skb_vlan_tagged_multi(skb) &&
		     handle->port_base_vlan_state ==
		     HNAE3_PORT_BASE_VLAN_ENABLE))
		return -EINVAL;

	if (skb->protocol == htons(ETH_P_8021Q) &&
	    !(tx_ring->tqp->handle->kinfo.netdev->features &
	    NETIF_F_HW_VLAN_CTAG_TX)) {
		/* When HW VLAN acceleration is turned off, and the stack
		 * sets the protocol to 802.1q, the driver just need to
		 * set the protocol to the encapsulated ethertype.
		 */
		skb->protocol = vlan_get_protocol(skb);
		return 0;
	}

	if (skb_vlan_tag_present(skb)) {
		u16 vlan_tag;

		vlan_tag = skb_vlan_tag_get(skb);
		vlan_tag |= (skb->priority & 0x7) << HNS3_TX_VLAN_PRIO_SHIFT;

		/* Based on hw strategy, use out_vtag in two layer tag case,
		 * and use inner_vtag in one tag case.
		 */
		if (skb->protocol == htons(ETH_P_8021Q)) {
			if (handle->port_base_vlan_state ==
			    HNAE3_PORT_BASE_VLAN_DISABLE){
				hns3_set_field(*out_vlan_flag,
					       HNS3_TXD_OVLAN_B, 1);
				*out_vtag = vlan_tag;
			} else {
				hns3_set_field(*inner_vlan_flag,
					       HNS3_TXD_VLAN_B, 1);
				*inner_vtag = vlan_tag;
			}
		} else {
			hns3_set_field(*inner_vlan_flag, HNS3_TXD_VLAN_B, 1);
			*inner_vtag = vlan_tag;
		}
	} else if (skb->protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *vhdr;
		int rc;

		rc = skb_cow_head(skb, 0);
		if (unlikely(rc < 0))
			return rc;
		vhdr = (struct vlan_ethhdr *)skb->data;
		vhdr->h_vlan_TCI |= cpu_to_be16((skb->priority & 0x7)
					<< HNS3_TX_VLAN_PRIO_SHIFT);
	}

	skb->protocol = vlan_get_protocol(skb);
	return 0;
}

static int hns3_fill_desc(struct hns3_enet_ring *ring, void *priv,
			  int size, int frag_end, enum hns_desc_type type)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct hns3_desc *desc = &ring->desc[ring->next_to_use];
	struct device *dev = ring_to_dev(ring);
	struct skb_frag_struct *frag;
	unsigned int frag_buf_num;
	int k, sizeoflast;
	dma_addr_t dma;

	if (type == DESC_TYPE_SKB) {
		struct sk_buff *skb = (struct sk_buff *)priv;
		u32 ol_type_vlan_len_msec = 0;
		u32 type_cs_vlan_tso = 0;
		u32 paylen = skb->len;
		u16 inner_vtag = 0;
		u16 out_vtag = 0;
		u16 mss = 0;
		int ret;

		ret = hns3_fill_desc_vtags(skb, ring, &type_cs_vlan_tso,
					   &ol_type_vlan_len_msec,
					   &inner_vtag, &out_vtag);
		if (unlikely(ret))
			return ret;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			u8 ol4_proto, il4_proto;

			skb_reset_mac_len(skb);

			ret = hns3_get_l4_protocol(skb, &ol4_proto, &il4_proto);
			if (unlikely(ret))
				return ret;

			ret = hns3_set_l2l3l4(skb, ol4_proto, il4_proto,
					      &type_cs_vlan_tso,
					      &ol_type_vlan_len_msec);
			if (unlikely(ret))
				return ret;

			ret = hns3_set_tso(skb, &paylen, &mss,
					   &type_cs_vlan_tso);
			if (unlikely(ret))
				return ret;
		}

		/* Set txbd */
		desc->tx.ol_type_vlan_len_msec =
			cpu_to_le32(ol_type_vlan_len_msec);
		desc->tx.type_cs_vlan_tso_len =	cpu_to_le32(type_cs_vlan_tso);
		desc->tx.paylen = cpu_to_le32(paylen);
		desc->tx.mss = cpu_to_le16(mss);
		desc->tx.vlan_tag = cpu_to_le16(inner_vtag);
		desc->tx.outer_vlan_tag = cpu_to_le16(out_vtag);

		dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	} else {
		frag = (struct skb_frag_struct *)priv;
		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
	}

	if (unlikely(dma_mapping_error(dev, dma))) {
		ring->stats.sw_err_cnt++;
		return -ENOMEM;
	}

	desc_cb->length = size;

	if (likely(size <= HNS3_MAX_BD_SIZE)) {
		u16 bdtp_fe_sc_vld_ra_ri = 0;

		desc_cb->priv = priv;
		desc_cb->dma = dma;
		desc_cb->type = type;
		desc->addr = cpu_to_le64(dma);
		desc->tx.send_size = cpu_to_le16(size);
		hns3_set_txbd_baseinfo(&bdtp_fe_sc_vld_ra_ri, frag_end);
		desc->tx.bdtp_fe_sc_vld_ra_ri =
			cpu_to_le16(bdtp_fe_sc_vld_ra_ri);

		ring_ptr_move_fw(ring, next_to_use);
		return 0;
	}

	frag_buf_num = hns3_tx_bd_count(size);
	sizeoflast = size & HNS3_TX_LAST_SIZE_M;
	sizeoflast = sizeoflast ? sizeoflast : HNS3_MAX_BD_SIZE;

	/* When frag size is bigger than hardware limit, split this frag */
	for (k = 0; k < frag_buf_num; k++) {
		u16 bdtp_fe_sc_vld_ra_ri = 0;

		/* The txbd's baseinfo of DESC_TYPE_PAGE & DESC_TYPE_SKB */
		desc_cb->priv = priv;
		desc_cb->dma = dma + HNS3_MAX_BD_SIZE * k;
		desc_cb->type = (type == DESC_TYPE_SKB && !k) ?
				DESC_TYPE_SKB : DESC_TYPE_PAGE;

		/* now, fill the descriptor */
		desc->addr = cpu_to_le64(dma + HNS3_MAX_BD_SIZE * k);
		desc->tx.send_size = cpu_to_le16((k == frag_buf_num - 1) ?
				     (u16)sizeoflast : (u16)HNS3_MAX_BD_SIZE);
		hns3_set_txbd_baseinfo(&bdtp_fe_sc_vld_ra_ri,
				       frag_end && (k == frag_buf_num - 1) ?
						1 : 0);
		desc->tx.bdtp_fe_sc_vld_ra_ri =
				cpu_to_le16(bdtp_fe_sc_vld_ra_ri);

		/* move ring pointer to next */
		ring_ptr_move_fw(ring, next_to_use);

		desc_cb = &ring->desc_cb[ring->next_to_use];
		desc = &ring->desc[ring->next_to_use];
	}

	return 0;
}

static int hns3_nic_bd_num(struct sk_buff *skb)
{
	int size = skb_headlen(skb);
	int i, bd_num;

	/* if the total len is within the max bd limit */
	if (likely(skb->len <= HNS3_MAX_BD_SIZE))
		return skb_shinfo(skb)->nr_frags + 1;

	bd_num = hns3_tx_bd_count(size);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		int frag_bd_num;

		size = skb_frag_size(frag);
		frag_bd_num = hns3_tx_bd_count(size);

		if (unlikely(frag_bd_num > HNS3_MAX_BD_PER_FRAG))
			return -ENOMEM;

		bd_num += frag_bd_num;
	}

	return bd_num;
}

static unsigned int hns3_gso_hdr_len(struct sk_buff *skb)
{
	if (!skb->encapsulation)
		return skb_transport_offset(skb) + tcp_hdrlen(skb);

	return skb_inner_transport_offset(skb) + inner_tcp_hdrlen(skb);
}

/* HW need every continuous 8 buffer data to be larger than MSS,
 * we simplify it by ensuring skb_headlen + the first continuous
 * 7 frags to to be larger than gso header len + mss, and the remaining
 * continuous 7 frags to be larger than MSS except the last 7 frags.
 */
static bool hns3_skb_need_linearized(struct sk_buff *skb)
{
	int bd_limit = HNS3_MAX_BD_PER_FRAG - 1;
	unsigned int tot_len = 0;
	int i;

	for (i = 0; i < bd_limit; i++)
		tot_len += skb_frag_size(&skb_shinfo(skb)->frags[i]);

	/* ensure headlen + the first 7 frags is greater than mss + header
	 * and the first 7 frags is greater than mss.
	 */
	if (((tot_len + skb_headlen(skb)) < (skb_shinfo(skb)->gso_size +
	    hns3_gso_hdr_len(skb))) || (tot_len < skb_shinfo(skb)->gso_size))
		return true;

	/* ensure the remaining continuous 7 buffer is greater than mss */
	for (i = 0; i < (skb_shinfo(skb)->nr_frags - bd_limit - 1); i++) {
		tot_len -= skb_frag_size(&skb_shinfo(skb)->frags[i]);
		tot_len += skb_frag_size(&skb_shinfo(skb)->frags[i + bd_limit]);

		if (tot_len < skb_shinfo(skb)->gso_size)
			return true;
	}

	return false;
}

static int hns3_nic_maybe_stop_tx(struct hns3_enet_ring *ring,
				  struct sk_buff **out_skb)
{
	struct sk_buff *skb = *out_skb;
	int bd_num;

	bd_num = hns3_nic_bd_num(skb);
	if (bd_num < 0)
		return bd_num;

	if (unlikely(bd_num > HNS3_MAX_BD_PER_FRAG)) {
		struct sk_buff *new_skb;

		if (skb_is_gso(skb) && !hns3_skb_need_linearized(skb))
			goto out;

		bd_num = hns3_tx_bd_count(skb->len);
		if (unlikely(ring_space(ring) < bd_num))
			return -EBUSY;
		/* manual split the send packet */
		new_skb = skb_copy(skb, GFP_ATOMIC);
		if (!new_skb)
			return -ENOMEM;
		dev_kfree_skb_any(skb);
		*out_skb = new_skb;

		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_copy++;
		u64_stats_update_end(&ring->syncp);
	}

out:
	if (unlikely(ring_space(ring) < bd_num))
		return -EBUSY;

	return bd_num;
}

static void hns3_clear_desc(struct hns3_enet_ring *ring, int next_to_use_orig)
{
	struct device *dev = ring_to_dev(ring);
	unsigned int i;

	for (i = 0; i < ring->desc_num; i++) {
		/* check if this is where we started */
		if (ring->next_to_use == next_to_use_orig)
			break;

		/* rollback one */
		ring_ptr_move_bw(ring, next_to_use);

		/* unmap the descriptor dma address */
		if (ring->desc_cb[ring->next_to_use].type == DESC_TYPE_SKB)
			dma_unmap_single(dev,
					 ring->desc_cb[ring->next_to_use].dma,
					ring->desc_cb[ring->next_to_use].length,
					DMA_TO_DEVICE);
		else if (ring->desc_cb[ring->next_to_use].length)
			dma_unmap_page(dev,
				       ring->desc_cb[ring->next_to_use].dma,
				       ring->desc_cb[ring->next_to_use].length,
				       DMA_TO_DEVICE);

		ring->desc_cb[ring->next_to_use].length = 0;
		ring->desc_cb[ring->next_to_use].dma = 0;
	}
}

netdev_tx_t hns3_nic_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_nic_ring_data *ring_data =
		&tx_ring_data(priv, skb->queue_mapping);
	struct hns3_enet_ring *ring = ring_data->ring;
	struct netdev_queue *dev_queue;
	struct skb_frag_struct *frag;
	int next_to_use_head;
	int buf_num;
	int seg_num;
	int size;
	int ret;
	int i;

	/* Prefetch the data used later */
	prefetch(skb->data);

	buf_num = hns3_nic_maybe_stop_tx(ring, &skb);
	if (unlikely(buf_num <= 0)) {
		if (buf_num == -EBUSY) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.tx_busy++;
			u64_stats_update_end(&ring->syncp);
			goto out_net_tx_busy;
		} else if (buf_num == -ENOMEM) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.sw_err_cnt++;
			u64_stats_update_end(&ring->syncp);
		}

		if (net_ratelimit())
			netdev_err(netdev, "xmit error: %d!\n", buf_num);

		goto out_err_tx_ok;
	}

	/* No. of segments (plus a header) */
	seg_num = skb_shinfo(skb)->nr_frags + 1;
	/* Fill the first part */
	size = skb_headlen(skb);

	next_to_use_head = ring->next_to_use;

	ret = hns3_fill_desc(ring, skb, size, seg_num == 1 ? 1 : 0,
			     DESC_TYPE_SKB);
	if (unlikely(ret))
		goto fill_err;

	/* Fill the fragments */
	for (i = 1; i < seg_num; i++) {
		frag = &skb_shinfo(skb)->frags[i - 1];
		size = skb_frag_size(frag);

		ret = hns3_fill_desc(ring, frag, size,
				     seg_num - 1 == i ? 1 : 0,
				     DESC_TYPE_PAGE);

		if (unlikely(ret))
			goto fill_err;
	}

	/* Complete translate all packets */
	dev_queue = netdev_get_tx_queue(netdev, ring_data->queue_index);
	netdev_tx_sent_queue(dev_queue, skb->len);

	wmb(); /* Commit all data before submit */

	hnae3_queue_xmit(ring->tqp, buf_num);

	return NETDEV_TX_OK;

fill_err:
	hns3_clear_desc(ring, next_to_use_head);

out_err_tx_ok:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

out_net_tx_busy:
	netif_stop_subqueue(netdev, ring_data->queue_index);
	smp_mb(); /* Commit all data before submit */

	return NETDEV_TX_BUSY;
}

static int hns3_nic_net_set_mac_address(struct net_device *netdev, void *p)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct sockaddr *mac_addr = p;
	int ret;

	if (!mac_addr || !is_valid_ether_addr((const u8 *)mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, mac_addr->sa_data)) {
		netdev_info(netdev, "already using mac address %pM\n",
			    mac_addr->sa_data);
		return 0;
	}

	ret = h->ae_algo->ops->set_mac_addr(h, mac_addr->sa_data, false);
	if (ret) {
		netdev_err(netdev, "set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	ether_addr_copy(netdev->dev_addr, mac_addr->sa_data);

	return 0;
}

static int hns3_nic_do_ioctl(struct net_device *netdev,
			     struct ifreq *ifr, int cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!netif_running(netdev))
		return -EINVAL;

	if (!h->ae_algo->ops->do_ioctl)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->do_ioctl(h, ifr, cmd);
}

static int hns3_nic_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	bool enable;
	int ret;

	if (changed & (NETIF_F_GRO_HW) && h->ae_algo->ops->set_gro_en) {
		enable = !!(features & NETIF_F_GRO_HW);
		ret = h->ae_algo->ops->set_gro_en(h, enable);
		if (ret)
			return ret;
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    h->ae_algo->ops->enable_vlan_filter) {
		enable = !!(features & NETIF_F_HW_VLAN_CTAG_FILTER);
		h->ae_algo->ops->enable_vlan_filter(h, enable);
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_RX) &&
	    h->ae_algo->ops->enable_hw_strip_rxvtag) {
		enable = !!(features & NETIF_F_HW_VLAN_CTAG_RX);
		ret = h->ae_algo->ops->enable_hw_strip_rxvtag(h, enable);
		if (ret)
			return ret;
	}

	if ((changed & NETIF_F_NTUPLE) && h->ae_algo->ops->enable_fd) {
		enable = !!(features & NETIF_F_NTUPLE);
		h->ae_algo->ops->enable_fd(h, enable);
	}

	netdev->features = features;
	return 0;
}

static void hns3_nic_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct hnae3_handle *handle = priv->ae_handle;
	struct hns3_enet_ring *ring;
	u64 rx_length_errors = 0;
	u64 rx_crc_errors = 0;
	u64 rx_multicast = 0;
	unsigned int start;
	u64 tx_errors = 0;
	u64 rx_errors = 0;
	unsigned int idx;
	u64 tx_bytes = 0;
	u64 rx_bytes = 0;
	u64 tx_pkts = 0;
	u64 rx_pkts = 0;
	u64 tx_drop = 0;
	u64 rx_drop = 0;

	if (test_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return;

	handle->ae_algo->ops->update_stats(handle, &netdev->stats);

	for (idx = 0; idx < queue_num; idx++) {
		/* fetch the tx stats */
		ring = priv->ring_data[idx].ring;
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			tx_bytes += ring->stats.tx_bytes;
			tx_pkts += ring->stats.tx_pkts;
			tx_drop += ring->stats.sw_err_cnt;
			tx_errors += ring->stats.sw_err_cnt;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		/* fetch the rx stats */
		ring = priv->ring_data[idx + queue_num].ring;
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			rx_bytes += ring->stats.rx_bytes;
			rx_pkts += ring->stats.rx_pkts;
			rx_drop += ring->stats.non_vld_descs;
			rx_drop += ring->stats.l2_err;
			rx_errors += ring->stats.non_vld_descs;
			rx_errors += ring->stats.l2_err;
			rx_crc_errors += ring->stats.l2_err;
			rx_crc_errors += ring->stats.l3l4_csum_err;
			rx_multicast += ring->stats.rx_multicast;
			rx_length_errors += ring->stats.err_pkt_len;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
	}

	stats->tx_bytes = tx_bytes;
	stats->tx_packets = tx_pkts;
	stats->rx_bytes = rx_bytes;
	stats->rx_packets = rx_pkts;

	stats->rx_errors = rx_errors;
	stats->multicast = rx_multicast;
	stats->rx_length_errors = rx_length_errors;
	stats->rx_crc_errors = rx_crc_errors;
	stats->rx_missed_errors = netdev->stats.rx_missed_errors;

	stats->tx_errors = tx_errors;
	stats->rx_dropped = rx_drop;
	stats->tx_dropped = tx_drop;
	stats->collisions = netdev->stats.collisions;
	stats->rx_over_errors = netdev->stats.rx_over_errors;
	stats->rx_frame_errors = netdev->stats.rx_frame_errors;
	stats->rx_fifo_errors = netdev->stats.rx_fifo_errors;
	stats->tx_aborted_errors = netdev->stats.tx_aborted_errors;
	stats->tx_carrier_errors = netdev->stats.tx_carrier_errors;
	stats->tx_fifo_errors = netdev->stats.tx_fifo_errors;
	stats->tx_heartbeat_errors = netdev->stats.tx_heartbeat_errors;
	stats->tx_window_errors = netdev->stats.tx_window_errors;
	stats->rx_compressed = netdev->stats.rx_compressed;
	stats->tx_compressed = netdev->stats.tx_compressed;
}

static int hns3_setup_tc(struct net_device *netdev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio_qopt = type_data;
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	u8 *prio_tc = mqprio_qopt->qopt.prio_tc_map;
	u8 tc = mqprio_qopt->qopt.num_tc;
	u16 mode = mqprio_qopt->mode;
	u8 hw = mqprio_qopt->qopt.hw;

	if (!((hw == TC_MQPRIO_HW_OFFLOAD_TCS &&
	       mode == TC_MQPRIO_MODE_CHANNEL) || (!hw && tc == 0)))
		return -EOPNOTSUPP;

	if (tc > HNAE3_MAX_TC)
		return -EINVAL;

	if (!netdev)
		return -EINVAL;

	return (kinfo->dcb_ops && kinfo->dcb_ops->setup_tc) ?
		kinfo->dcb_ops->setup_tc(h, tc, prio_tc) : -EOPNOTSUPP;
}

static int hns3_nic_setup_tc(struct net_device *dev, enum tc_setup_type type,
			     void *type_data)
{
	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	return hns3_setup_tc(dev, type_data);
}

static int hns3_vlan_rx_add_vid(struct net_device *netdev,
				__be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, false);

	return ret;
}

static int hns3_vlan_rx_kill_vid(struct net_device *netdev,
				 __be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, true);

	return ret;
}

static int hns3_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan,
				u8 qos, __be16 vlan_proto)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vf_vlan_filter)
		ret = h->ae_algo->ops->set_vf_vlan_filter(h, vf, vlan,
							  qos, vlan_proto);

	return ret;
}

static int hns3_nic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (!h->ae_algo->ops->set_mtu)
		return -EOPNOTSUPP;

	ret = h->ae_algo->ops->set_mtu(h, new_mtu);
	if (ret)
		netdev_err(netdev, "failed to change MTU in hardware %d\n",
			   ret);
	else
		netdev->mtu = new_mtu;

	return ret;
}

static bool hns3_get_tx_timeo_queue_info(struct net_device *ndev)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = hns3_get_handle(ndev);
	struct hns3_enet_ring *tx_ring = NULL;
	struct napi_struct *napi;
	int timeout_queue = 0;
	int hw_head, hw_tail;
	int fbd_num, fbd_oft;
	int ebd_num, ebd_oft;
	int bd_num, bd_err;
	int ring_en, tc;
	int i;

	/* Find the stopped queue the same way the stack does */
	for (i = 0; i < ndev->num_tx_queues; i++) {
		struct netdev_queue *q;
		unsigned long trans_start;

		q = netdev_get_tx_queue(ndev, i);
		trans_start = q->trans_start;
		if (netif_xmit_stopped(q) &&
		    time_after(jiffies,
			       (trans_start + ndev->watchdog_timeo))) {
			timeout_queue = i;
			break;
		}
	}

	if (i == ndev->num_tx_queues) {
		netdev_info(ndev,
			    "no netdev TX timeout queue found, timeout count: %llu\n",
			    priv->tx_timeout_count);
		return false;
	}

	priv->tx_timeout_count++;

	tx_ring = priv->ring_data[timeout_queue].ring;
	napi = &tx_ring->tqp_vector->napi;

	netdev_info(ndev,
		    "tx_timeout count: %llu, queue id: %d, SW_NTU: 0x%x, SW_NTC: 0x%x, napi state: %lu\n",
		    priv->tx_timeout_count, timeout_queue, tx_ring->next_to_use,
		    tx_ring->next_to_clean, napi->state);

	netdev_info(ndev,
		    "tx_pkts: %llu, tx_bytes: %llu, io_err_cnt: %llu, sw_err_cnt: %llu\n",
		    tx_ring->stats.tx_pkts, tx_ring->stats.tx_bytes,
		    tx_ring->stats.io_err_cnt, tx_ring->stats.sw_err_cnt);

	netdev_info(ndev,
		    "seg_pkt_cnt: %llu, tx_err_cnt: %llu, restart_queue: %llu, tx_busy: %llu\n",
		    tx_ring->stats.seg_pkt_cnt, tx_ring->stats.tx_err_cnt,
		    tx_ring->stats.restart_queue, tx_ring->stats.tx_busy);

	/* When mac received many pause frames continuous, it's unable to send
	 * packets, which may cause tx timeout
	 */
	if (h->ae_algo->ops->update_stats &&
	    h->ae_algo->ops->get_mac_pause_stats) {
		u64 tx_pause_cnt, rx_pause_cnt;

		h->ae_algo->ops->update_stats(h, &ndev->stats);
		h->ae_algo->ops->get_mac_pause_stats(h, &tx_pause_cnt,
						     &rx_pause_cnt);
		netdev_info(ndev, "tx_pause_cnt: %llu, rx_pause_cnt: %llu\n",
			    tx_pause_cnt, rx_pause_cnt);
	}

	hw_head = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_HEAD_REG);
	hw_tail = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_TAIL_REG);
	fbd_num = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_FBDNUM_REG);
	fbd_oft = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_OFFSET_REG);
	ebd_num = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_EBDNUM_REG);
	ebd_oft = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_EBD_OFFSET_REG);
	bd_num = readl_relaxed(tx_ring->tqp->io_base +
			       HNS3_RING_TX_RING_BD_NUM_REG);
	bd_err = readl_relaxed(tx_ring->tqp->io_base +
			       HNS3_RING_TX_RING_BD_ERR_REG);
	ring_en = readl_relaxed(tx_ring->tqp->io_base + HNS3_RING_EN_REG);
	tc = readl_relaxed(tx_ring->tqp->io_base + HNS3_RING_TX_RING_TC_REG);

	netdev_info(ndev,
		    "BD_NUM: 0x%x HW_HEAD: 0x%x, HW_TAIL: 0x%x, BD_ERR: 0x%x, INT: 0x%x\n",
		    bd_num, hw_head, hw_tail, bd_err,
		    readl(tx_ring->tqp_vector->mask_addr));
	netdev_info(ndev,
		    "RING_EN: 0x%x, TC: 0x%x, FBD_NUM: 0x%x FBD_OFT: 0x%x, EBD_NUM: 0x%x, EBD_OFT: 0x%x\n",
		    ring_en, tc, fbd_num, fbd_oft, ebd_num, ebd_oft);

	return true;
}

static void hns3_nic_net_timeout(struct net_device *ndev)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!hns3_get_tx_timeo_queue_info(ndev))
		return;

	/* request the reset, and let the hclge to determine
	 * which reset level should be done
	 */
	if (h->ae_algo->ops->reset_event)
		h->ae_algo->ops->reset_event(h->pdev, h);
}

#ifdef CONFIG_RFS_ACCEL
static int hns3_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			      u16 rxq_index, u32 flow_id)
{
	struct hnae3_handle *h = hns3_get_handle(dev);
	struct flow_keys fkeys;

	if (!h->ae_algo->ops->add_arfs_entry)
		return -EOPNOTSUPP;

	if (skb->encapsulation)
		return -EPROTONOSUPPORT;

	if (!skb_flow_dissect_flow_keys(skb, &fkeys, 0))
		return -EPROTONOSUPPORT;

	if ((fkeys.basic.n_proto != htons(ETH_P_IP) &&
	     fkeys.basic.n_proto != htons(ETH_P_IPV6)) ||
	    (fkeys.basic.ip_proto != IPPROTO_TCP &&
	     fkeys.basic.ip_proto != IPPROTO_UDP))
		return -EPROTONOSUPPORT;

	return h->ae_algo->ops->add_arfs_entry(h, rxq_index, flow_id, &fkeys);
}
#endif

static const struct net_device_ops hns3_nic_netdev_ops = {
	.ndo_open		= hns3_nic_net_open,
	.ndo_stop		= hns3_nic_net_stop,
	.ndo_start_xmit		= hns3_nic_net_xmit,
	.ndo_tx_timeout		= hns3_nic_net_timeout,
	.ndo_set_mac_address	= hns3_nic_net_set_mac_address,
	.ndo_do_ioctl		= hns3_nic_do_ioctl,
	.ndo_change_mtu		= hns3_nic_change_mtu,
	.ndo_set_features	= hns3_nic_set_features,
	.ndo_get_stats64	= hns3_nic_get_stats64,
	.ndo_setup_tc		= hns3_nic_setup_tc,
	.ndo_set_rx_mode	= hns3_nic_set_rx_mode,
	.ndo_vlan_rx_add_vid	= hns3_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= hns3_vlan_rx_kill_vid,
	.ndo_set_vf_vlan	= hns3_ndo_set_vf_vlan,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= hns3_rx_flow_steer,
#endif

};

bool hns3_is_phys_func(struct pci_dev *pdev)
{
	u32 dev_id = pdev->device;

	switch (dev_id) {
	case HNAE3_DEV_ID_GE:
	case HNAE3_DEV_ID_25GE:
	case HNAE3_DEV_ID_25GE_RDMA:
	case HNAE3_DEV_ID_25GE_RDMA_MACSEC:
	case HNAE3_DEV_ID_50GE_RDMA:
	case HNAE3_DEV_ID_50GE_RDMA_MACSEC:
	case HNAE3_DEV_ID_100G_RDMA_MACSEC:
		return true;
	case HNAE3_DEV_ID_100G_VF:
	case HNAE3_DEV_ID_100G_RDMA_DCB_PFC_VF:
		return false;
	default:
		dev_warn(&pdev->dev, "un-recognized pci device-id %d",
			 dev_id);
	}

	return false;
}

static void hns3_disable_sriov(struct pci_dev *pdev)
{
	/* If our VFs are assigned we cannot shut down SR-IOV
	 * without causing issues, so just leave the hardware
	 * available but disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev,
			 "disabling driver while VFs are assigned\n");
		return;
	}

	pci_disable_sriov(pdev);
}

static void hns3_get_dev_capability(struct pci_dev *pdev,
				    struct hnae3_ae_dev *ae_dev)
{
	if (pdev->revision >= 0x21) {
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_SUPPORT_FD_B, 1);
		hnae3_set_bit(ae_dev->flag, HNAE3_DEV_SUPPORT_GRO_B, 1);
	}
}

/* hns3_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in hns3_pci_tbl
 *
 * hns3_probe initializes a PF identified by a pci_dev structure.
 * The OS initialization, configuring of the PF private structure,
 * and a hardware reset occur.
 *
 * Returns 0 on success, negative on failure
 */
static int hns3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct hnae3_ae_dev *ae_dev;
	int ret;

	ae_dev = devm_kzalloc(&pdev->dev, sizeof(*ae_dev), GFP_KERNEL);
	if (!ae_dev) {
		ret = -ENOMEM;
		return ret;
	}

	ae_dev->pdev = pdev;
	ae_dev->flag = ent->driver_data;
	ae_dev->reset_type = HNAE3_NONE_RESET;
	hns3_get_dev_capability(pdev, ae_dev);
	pci_set_drvdata(pdev, ae_dev);

	ret = hnae3_register_ae_dev(ae_dev);
	if (ret) {
		devm_kfree(&pdev->dev, ae_dev);
		pci_set_drvdata(pdev, NULL);
	}

	return ret;
}

/* hns3_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void hns3_remove(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	if (hns3_is_phys_func(pdev) && IS_ENABLED(CONFIG_PCI_IOV))
		hns3_disable_sriov(pdev);

	hnae3_unregister_ae_dev(ae_dev);
	pci_set_drvdata(pdev, NULL);
}

/**
 * hns3_pci_sriov_configure
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of VFs to allocate
 *
 * Enable or change the number of VFs. Called when the user updates the number
 * of VFs in sysfs.
 **/
static int hns3_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	int ret;

	if (!(hns3_is_phys_func(pdev) && IS_ENABLED(CONFIG_PCI_IOV))) {
		dev_warn(&pdev->dev, "Can not config SRIOV\n");
		return -EINVAL;
	}

	if (num_vfs) {
		ret = pci_enable_sriov(pdev, num_vfs);
		if (ret)
			dev_err(&pdev->dev, "SRIOV enable failed %d\n", ret);
		else
			return num_vfs;
	} else if (!pci_vfs_assigned(pdev)) {
		pci_disable_sriov(pdev);
	} else {
		dev_warn(&pdev->dev,
			 "Unable to free VFs because some are assigned to VMs.\n");
	}

	return 0;
}

static void hns3_shutdown(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	hnae3_unregister_ae_dev(ae_dev);
	devm_kfree(&pdev->dev, ae_dev);
	pci_set_drvdata(pdev, NULL);

	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
}

static pci_ers_result_t hns3_error_detected(struct pci_dev *pdev,
					    pci_channel_state_t state)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	pci_ers_result_t ret;

	dev_info(&pdev->dev, "PCI error detected, state(=%d)!!\n", state);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (!ae_dev || !ae_dev->ops) {
		dev_err(&pdev->dev,
			"Can't recover - error happened before device initialized\n");
		return PCI_ERS_RESULT_NONE;
	}

	if (ae_dev->ops->handle_hw_ras_error)
		ret = ae_dev->ops->handle_hw_ras_error(ae_dev);
	else
		return PCI_ERS_RESULT_NONE;

	return ret;
}

static pci_ers_result_t hns3_slot_reset(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	const struct hnae3_ae_ops *ops = ae_dev->ops;
	enum hnae3_reset_type reset_type;
	struct device *dev = &pdev->dev;

	if (!ae_dev || !ae_dev->ops)
		return PCI_ERS_RESULT_NONE;

	/* request the reset */
	if (ops->reset_event) {
		if (!ae_dev->override_pci_need_reset) {
			reset_type = ops->get_reset_level(ae_dev,
						&ae_dev->hw_err_reset_req);
			ops->set_default_reset_request(ae_dev, reset_type);
			dev_info(dev, "requesting reset due to PCI error\n");
			ops->reset_event(pdev, NULL);
		}

		return PCI_ERS_RESULT_RECOVERED;
	}

	return PCI_ERS_RESULT_DISCONNECT;
}

static void hns3_reset_prepare(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "hns3 flr prepare\n");
	if (ae_dev && ae_dev->ops && ae_dev->ops->flr_prepare)
		ae_dev->ops->flr_prepare(ae_dev);
}

static void hns3_reset_done(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "hns3 flr done\n");
	if (ae_dev && ae_dev->ops && ae_dev->ops->flr_done)
		ae_dev->ops->flr_done(ae_dev);
}

static const struct pci_error_handlers hns3_err_handler = {
	.error_detected = hns3_error_detected,
	.slot_reset     = hns3_slot_reset,
	.reset_prepare	= hns3_reset_prepare,
	.reset_done	= hns3_reset_done,
};

static struct pci_driver hns3_driver = {
	.name     = hns3_driver_name,
	.id_table = hns3_pci_tbl,
	.probe    = hns3_probe,
	.remove   = hns3_remove,
	.shutdown = hns3_shutdown,
	.sriov_configure = hns3_pci_sriov_configure,
	.err_handler    = &hns3_err_handler,
};

/* set default feature to hns3 */
static void hns3_set_default_feature(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct pci_dev *pdev = h->pdev;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->hw_enc_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_SCTP_CRC;

	netdev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;

	netdev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_HW_VLAN_CTAG_FILTER |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_SCTP_CRC;

	netdev->vlan_features |=
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |
		NETIF_F_SG | NETIF_F_GSO | NETIF_F_GRO |
		NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_SCTP_CRC;

	netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_SCTP_CRC;

	if (pdev->revision >= 0x21) {
		netdev->hw_features |= NETIF_F_GRO_HW;
		netdev->features |= NETIF_F_GRO_HW;

		if (!(h->flags & HNAE3_SUPPORT_VF)) {
			netdev->hw_features |= NETIF_F_NTUPLE;
			netdev->features |= NETIF_F_NTUPLE;
		}
	}
}

static int hns3_alloc_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb)
{
	unsigned int order = hnae3_page_order(ring);
	struct page *p;

	p = dev_alloc_pages(order);
	if (!p)
		return -ENOMEM;

	cb->priv = p;
	cb->page_offset = 0;
	cb->reuse_flag = 0;
	cb->buf  = page_address(p);
	cb->length = hnae3_page_size(ring);
	cb->type = DESC_TYPE_PAGE;

	return 0;
}

static void hns3_free_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb)
{
	if (cb->type == DESC_TYPE_SKB)
		dev_kfree_skb_any((struct sk_buff *)cb->priv);
	else if (!HNAE3_IS_TX_RING(ring))
		put_page((struct page *)cb->priv);
	memset(cb, 0, sizeof(*cb));
}

static int hns3_map_buffer(struct hns3_enet_ring *ring, struct hns3_desc_cb *cb)
{
	cb->dma = dma_map_page(ring_to_dev(ring), cb->priv, 0,
			       cb->length, ring_to_dma_dir(ring));

	if (unlikely(dma_mapping_error(ring_to_dev(ring), cb->dma)))
		return -EIO;

	return 0;
}

static void hns3_unmap_buffer(struct hns3_enet_ring *ring,
			      struct hns3_desc_cb *cb)
{
	if (cb->type == DESC_TYPE_SKB)
		dma_unmap_single(ring_to_dev(ring), cb->dma, cb->length,
				 ring_to_dma_dir(ring));
	else if (cb->length)
		dma_unmap_page(ring_to_dev(ring), cb->dma, cb->length,
			       ring_to_dma_dir(ring));
}

static void hns3_buffer_detach(struct hns3_enet_ring *ring, int i)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc[i].addr = 0;
}

static void hns3_free_buffer_detach(struct hns3_enet_ring *ring, int i)
{
	struct hns3_desc_cb *cb = &ring->desc_cb[i];

	if (!ring->desc_cb[i].dma)
		return;

	hns3_buffer_detach(ring, i);
	hns3_free_buffer(ring, cb);
}

static void hns3_free_buffers(struct hns3_enet_ring *ring)
{
	int i;

	for (i = 0; i < ring->desc_num; i++)
		hns3_free_buffer_detach(ring, i);
}

/* free desc along with its attached buffer */
static void hns3_free_desc(struct hns3_enet_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	hns3_free_buffers(ring);

	if (ring->desc) {
		dma_free_coherent(ring_to_dev(ring), size,
				  ring->desc, ring->desc_dma_addr);
		ring->desc = NULL;
	}
}

static int hns3_alloc_desc(struct hns3_enet_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	ring->desc = dma_alloc_coherent(ring_to_dev(ring), size,
					&ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static int hns3_reserve_buffer_map(struct hns3_enet_ring *ring,
				   struct hns3_desc_cb *cb)
{
	int ret;

	ret = hns3_alloc_buffer(ring, cb);
	if (ret)
		goto out;

	ret = hns3_map_buffer(ring, cb);
	if (ret)
		goto out_with_buf;

	return 0;

out_with_buf:
	hns3_free_buffer(ring, cb);
out:
	return ret;
}

static int hns3_alloc_buffer_attach(struct hns3_enet_ring *ring, int i)
{
	int ret = hns3_reserve_buffer_map(ring, &ring->desc_cb[i]);

	if (ret)
		return ret;

	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma);

	return 0;
}

/* Allocate memory for raw pkg, and map with dma */
static int hns3_alloc_ring_buffers(struct hns3_enet_ring *ring)
{
	int i, j, ret;

	for (i = 0; i < ring->desc_num; i++) {
		ret = hns3_alloc_buffer_attach(ring, i);
		if (ret)
			goto out_buffer_fail;
	}

	return 0;

out_buffer_fail:
	for (j = i - 1; j >= 0; j--)
		hns3_free_buffer_detach(ring, j);
	return ret;
}

/* detach a in-used buffer and replace with a reserved one */
static void hns3_replace_buffer(struct hns3_enet_ring *ring, int i,
				struct hns3_desc_cb *res_cb)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc_cb[i] = *res_cb;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma);
	ring->desc[i].rx.bd_base_info = 0;
}

static void hns3_reuse_buffer(struct hns3_enet_ring *ring, int i)
{
	ring->desc_cb[i].reuse_flag = 0;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma +
					 ring->desc_cb[i].page_offset);
	ring->desc[i].rx.bd_base_info = 0;
}

static void hns3_nic_reclaim_desc(struct hns3_enet_ring *ring, int head,
				  int *bytes, int *pkts)
{
	int ntc = ring->next_to_clean;
	struct hns3_desc_cb *desc_cb;

	while (head != ntc) {
		desc_cb = &ring->desc_cb[ntc];
		(*pkts) += (desc_cb->type == DESC_TYPE_SKB);
		(*bytes) += desc_cb->length;
		/* desc_cb will be cleaned, after hnae3_free_buffer_detach */
		hns3_free_buffer_detach(ring, ntc);

		if (++ntc == ring->desc_num)
			ntc = 0;

		/* Issue prefetch for next Tx descriptor */
		prefetch(&ring->desc_cb[ntc]);
	}

	/* This smp_store_release() pairs with smp_load_acquire() in
	 * ring_space called by hns3_nic_net_xmit.
	 */
	smp_store_release(&ring->next_to_clean, ntc);
}

static int is_valid_clean_head(struct hns3_enet_ring *ring, int h)
{
	int u = ring->next_to_use;
	int c = ring->next_to_clean;

	if (unlikely(h > ring->desc_num))
		return 0;

	return u > c ? (h > c && h <= u) : (h > c || h <= u);
}

void hns3_clean_tx_ring(struct hns3_enet_ring *ring)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct netdev_queue *dev_queue;
	int bytes, pkts;
	int head;

	head = readl_relaxed(ring->tqp->io_base + HNS3_RING_TX_RING_HEAD_REG);
	rmb(); /* Make sure head is ready before touch any data */

	if (is_ring_empty(ring) || head == ring->next_to_clean)
		return; /* no data to poll */

	if (unlikely(!is_valid_clean_head(ring, head))) {
		netdev_err(netdev, "wrong head (%d, %d-%d)\n", head,
			   ring->next_to_use, ring->next_to_clean);

		u64_stats_update_begin(&ring->syncp);
		ring->stats.io_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return;
	}

	bytes = 0;
	pkts = 0;
	hns3_nic_reclaim_desc(ring, head, &bytes, &pkts);

	ring->tqp_vector->tx_group.total_bytes += bytes;
	ring->tqp_vector->tx_group.total_packets += pkts;

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_bytes += bytes;
	ring->stats.tx_pkts += pkts;
	u64_stats_update_end(&ring->syncp);

	dev_queue = netdev_get_tx_queue(netdev, ring->tqp->tqp_index);
	netdev_tx_completed_queue(dev_queue, pkts, bytes);

	if (unlikely(pkts && netif_carrier_ok(netdev) &&
		     (ring_space(ring) > HNS3_MAX_BD_PER_PKT))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_tx_queue_stopped(dev_queue) &&
		    !test_bit(HNS3_NIC_STATE_DOWN, &priv->state)) {
			netif_tx_wake_queue(dev_queue);
			ring->stats.restart_queue++;
		}
	}
}

static int hns3_desc_unused(struct hns3_enet_ring *ring)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;

	return ((ntc >= ntu) ? 0 : ring->desc_num) + ntc - ntu;
}

static void hns3_nic_alloc_rx_buffers(struct hns3_enet_ring *ring,
				      int cleand_count)
{
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc_cb res_cbs;
	int i, ret;

	for (i = 0; i < cleand_count; i++) {
		desc_cb = &ring->desc_cb[ring->next_to_use];
		if (desc_cb->reuse_flag) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.reuse_pg_cnt++;
			u64_stats_update_end(&ring->syncp);

			hns3_reuse_buffer(ring, ring->next_to_use);
		} else {
			ret = hns3_reserve_buffer_map(ring, &res_cbs);
			if (ret) {
				u64_stats_update_begin(&ring->syncp);
				ring->stats.sw_err_cnt++;
				u64_stats_update_end(&ring->syncp);

				netdev_err(ring->tqp->handle->kinfo.netdev,
					   "hnae reserve buffer map failed.\n");
				break;
			}
			hns3_replace_buffer(ring, ring->next_to_use, &res_cbs);

			u64_stats_update_begin(&ring->syncp);
			ring->stats.non_reuse_pg++;
			u64_stats_update_end(&ring->syncp);
		}

		ring_ptr_move_fw(ring, next_to_use);
	}

	wmb(); /* Make all data has been write before submit */
	writel_relaxed(i, ring->tqp->io_base + HNS3_RING_RX_RING_HEAD_REG);
}

static void hns3_nic_reuse_page(struct sk_buff *skb, int i,
				struct hns3_enet_ring *ring, int pull_len,
				struct hns3_desc_cb *desc_cb)
{
	struct hns3_desc *desc = &ring->desc[ring->next_to_clean];
	int size = le16_to_cpu(desc->rx.size);
	u32 truesize = hnae3_buf_size(ring);

	skb_add_rx_frag(skb, i, desc_cb->priv, desc_cb->page_offset + pull_len,
			size - pull_len, truesize);

	/* Avoid re-using remote pages, or the stack is still using the page
	 * when page_offset rollback to zero, flag default unreuse
	 */
	if (unlikely(page_to_nid(desc_cb->priv) != numa_mem_id()) ||
	    (!desc_cb->page_offset && page_count(desc_cb->priv) > 1))
		return;

	/* Move offset up to the next cache line */
	desc_cb->page_offset += truesize;

	if (desc_cb->page_offset + truesize <= hnae3_page_size(ring)) {
		desc_cb->reuse_flag = 1;
		/* Bump ref count on page before it is given */
		get_page(desc_cb->priv);
	} else if (page_count(desc_cb->priv) == 1) {
		desc_cb->reuse_flag = 1;
		desc_cb->page_offset = 0;
		get_page(desc_cb->priv);
	}
}

static int hns3_gro_complete(struct sk_buff *skb, u32 l234info)
{
	__be16 type = skb->protocol;
	struct tcphdr *th;
	int depth = 0;

	while (eth_type_vlan(type)) {
		struct vlan_hdr *vh;

		if ((depth + VLAN_HLEN) > skb_headlen(skb))
			return -EFAULT;

		vh = (struct vlan_hdr *)(skb->data + depth);
		type = vh->h_vlan_encapsulated_proto;
		depth += VLAN_HLEN;
	}

	skb_set_network_header(skb, depth);

	if (type == htons(ETH_P_IP)) {
		const struct iphdr *iph = ip_hdr(skb);

		depth += sizeof(struct iphdr);
		skb_set_transport_header(skb, depth);
		th = tcp_hdr(skb);
		th->check = ~tcp_v4_check(skb->len - depth, iph->saddr,
					  iph->daddr, 0);
	} else if (type == htons(ETH_P_IPV6)) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		depth += sizeof(struct ipv6hdr);
		skb_set_transport_header(skb, depth);
		th = tcp_hdr(skb);
		th->check = ~tcp_v6_check(skb->len - depth, &iph->saddr,
					  &iph->daddr, 0);
	} else {
		netdev_err(skb->dev,
			   "Error: FW GRO supports only IPv4/IPv6, not 0x%04x, depth: %d\n",
			   be16_to_cpu(type), depth);
		return -EFAULT;
	}

	skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;
	if (th->cwr)
		skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

	if (l234info & BIT(HNS3_RXD_GRO_FIXID_B))
		skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_FIXEDID;

	skb->csum_start = (unsigned char *)th - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;
	return 0;
}

static void hns3_rx_checksum(struct hns3_enet_ring *ring, struct sk_buff *skb,
			     u32 l234info, u32 bd_base_info, u32 ol_info)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	int l3_type, l4_type;
	int ol4_type;

	skb->ip_summed = CHECKSUM_NONE;

	skb_checksum_none_assert(skb);

	if (!(netdev->features & NETIF_F_RXCSUM))
		return;

	/* check if hardware has done checksum */
	if (!(bd_base_info & BIT(HNS3_RXD_L3L4P_B)))
		return;

	if (unlikely(l234info & (BIT(HNS3_RXD_L3E_B) | BIT(HNS3_RXD_L4E_B) |
				 BIT(HNS3_RXD_OL3E_B) |
				 BIT(HNS3_RXD_OL4E_B)))) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.l3l4_csum_err++;
		u64_stats_update_end(&ring->syncp);

		return;
	}

	ol4_type = hnae3_get_field(ol_info, HNS3_RXD_OL4ID_M,
				   HNS3_RXD_OL4ID_S);
	switch (ol4_type) {
	case HNS3_OL4_TYPE_MAC_IN_UDP:
	case HNS3_OL4_TYPE_NVGRE:
		skb->csum_level = 1;
		/* fall through */
	case HNS3_OL4_TYPE_NO_TUN:
		l3_type = hnae3_get_field(l234info, HNS3_RXD_L3ID_M,
					  HNS3_RXD_L3ID_S);
		l4_type = hnae3_get_field(l234info, HNS3_RXD_L4ID_M,
					  HNS3_RXD_L4ID_S);

		/* Can checksum ipv4 or ipv6 + UDP/TCP/SCTP packets */
		if ((l3_type == HNS3_L3_TYPE_IPV4 ||
		     l3_type == HNS3_L3_TYPE_IPV6) &&
		    (l4_type == HNS3_L4_TYPE_UDP ||
		     l4_type == HNS3_L4_TYPE_TCP ||
		     l4_type == HNS3_L4_TYPE_SCTP))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	default:
		break;
	}
}

static void hns3_rx_skb(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	if (skb_has_frag_list(skb))
		napi_gro_flush(&ring->tqp_vector->napi, false);

	napi_gro_receive(&ring->tqp_vector->napi, skb);
}

static bool hns3_parse_vlan_tag(struct hns3_enet_ring *ring,
				struct hns3_desc *desc, u32 l234info,
				u16 *vlan_tag)
{
	struct hnae3_handle *handle = ring->tqp->handle;
	struct pci_dev *pdev = ring->tqp->handle->pdev;

	if (pdev->revision == 0x20) {
		*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		if (!(*vlan_tag & VLAN_VID_MASK))
			*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);

		return (*vlan_tag != 0);
	}

#define HNS3_STRP_OUTER_VLAN	0x1
#define HNS3_STRP_INNER_VLAN	0x2
#define HNS3_STRP_BOTH		0x3

	/* Hardware always insert VLAN tag into RX descriptor when
	 * remove the tag from packet, driver needs to determine
	 * reporting which tag to stack.
	 */
	switch (hnae3_get_field(l234info, HNS3_RXD_STRP_TAGP_M,
				HNS3_RXD_STRP_TAGP_S)) {
	case HNS3_STRP_OUTER_VLAN:
		if (handle->port_base_vlan_state !=
				HNAE3_PORT_BASE_VLAN_DISABLE)
			return false;

		*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		return true;
	case HNS3_STRP_INNER_VLAN:
		if (handle->port_base_vlan_state !=
				HNAE3_PORT_BASE_VLAN_DISABLE)
			return false;

		*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);
		return true;
	case HNS3_STRP_BOTH:
		if (handle->port_base_vlan_state ==
				HNAE3_PORT_BASE_VLAN_DISABLE)
			*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		else
			*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);

		return true;
	default:
		return false;
	}
}

static int hns3_alloc_skb(struct hns3_enet_ring *ring, int length,
			  unsigned char *va)
{
#define HNS3_NEED_ADD_FRAG	1
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_clean];
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	struct sk_buff *skb;

	ring->skb = napi_alloc_skb(&ring->tqp_vector->napi, HNS3_RX_HEAD_SIZE);
	skb = ring->skb;
	if (unlikely(!skb)) {
		netdev_err(netdev, "alloc rx skb fail\n");

		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);

		return -ENOMEM;
	}

	prefetchw(skb->data);

	ring->pending_buf = 1;
	ring->frag_num = 0;
	ring->tail_skb = NULL;
	if (length <= HNS3_RX_HEAD_SIZE) {
		memcpy(__skb_put(skb, length), va, ALIGN(length, sizeof(long)));

		/* We can reuse buffer as-is, just make sure it is local */
		if (likely(page_to_nid(desc_cb->priv) == numa_mem_id()))
			desc_cb->reuse_flag = 1;
		else /* This page cannot be reused so discard it */
			put_page(desc_cb->priv);

		ring_ptr_move_fw(ring, next_to_clean);
		return 0;
	}
	u64_stats_update_begin(&ring->syncp);
	ring->stats.seg_pkt_cnt++;
	u64_stats_update_end(&ring->syncp);

	ring->pull_len = eth_get_headlen(netdev, va, HNS3_RX_HEAD_SIZE);
	__skb_put(skb, ring->pull_len);
	hns3_nic_reuse_page(skb, ring->frag_num++, ring, ring->pull_len,
			    desc_cb);
	ring_ptr_move_fw(ring, next_to_clean);

	return HNS3_NEED_ADD_FRAG;
}

static int hns3_add_frag(struct hns3_enet_ring *ring, struct hns3_desc *desc,
			 struct sk_buff **out_skb, bool pending)
{
	struct sk_buff *skb = *out_skb;
	struct sk_buff *head_skb = *out_skb;
	struct sk_buff *new_skb;
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc *pre_desc;
	u32 bd_base_info;
	int pre_bd;

	/* if there is pending bd, the SW param next_to_clean has moved
	 * to next and the next is NULL
	 */
	if (pending) {
		pre_bd = (ring->next_to_clean - 1 + ring->desc_num) %
			 ring->desc_num;
		pre_desc = &ring->desc[pre_bd];
		bd_base_info = le32_to_cpu(pre_desc->rx.bd_base_info);
	} else {
		bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
	}

	while (!(bd_base_info & BIT(HNS3_RXD_FE_B))) {
		desc = &ring->desc[ring->next_to_clean];
		desc_cb = &ring->desc_cb[ring->next_to_clean];
		bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
		/* make sure HW write desc complete */
		dma_rmb();
		if (!(bd_base_info & BIT(HNS3_RXD_VLD_B)))
			return -ENXIO;

		if (unlikely(ring->frag_num >= MAX_SKB_FRAGS)) {
			new_skb = napi_alloc_skb(&ring->tqp_vector->napi,
						 HNS3_RX_HEAD_SIZE);
			if (unlikely(!new_skb)) {
				netdev_err(ring->tqp->handle->kinfo.netdev,
					   "alloc rx skb frag fail\n");
				return -ENXIO;
			}
			ring->frag_num = 0;

			if (ring->tail_skb) {
				ring->tail_skb->next = new_skb;
				ring->tail_skb = new_skb;
			} else {
				skb_shinfo(skb)->frag_list = new_skb;
				ring->tail_skb = new_skb;
			}
		}

		if (ring->tail_skb) {
			head_skb->truesize += hnae3_buf_size(ring);
			head_skb->data_len += le16_to_cpu(desc->rx.size);
			head_skb->len += le16_to_cpu(desc->rx.size);
			skb = ring->tail_skb;
		}

		hns3_nic_reuse_page(skb, ring->frag_num++, ring, 0, desc_cb);
		ring_ptr_move_fw(ring, next_to_clean);
		ring->pending_buf++;
	}

	return 0;
}

static int hns3_set_gro_and_checksum(struct hns3_enet_ring *ring,
				     struct sk_buff *skb, u32 l234info,
				     u32 bd_base_info, u32 ol_info)
{
	u32 l3_type;

	skb_shinfo(skb)->gso_size = hnae3_get_field(bd_base_info,
						    HNS3_RXD_GRO_SIZE_M,
						    HNS3_RXD_GRO_SIZE_S);
	/* if there is no HW GRO, do not set gro params */
	if (!skb_shinfo(skb)->gso_size) {
		hns3_rx_checksum(ring, skb, l234info, bd_base_info, ol_info);
		return 0;
	}

	NAPI_GRO_CB(skb)->count = hnae3_get_field(l234info,
						  HNS3_RXD_GRO_COUNT_M,
						  HNS3_RXD_GRO_COUNT_S);

	l3_type = hnae3_get_field(l234info, HNS3_RXD_L3ID_M, HNS3_RXD_L3ID_S);
	if (l3_type == HNS3_L3_TYPE_IPV4)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	else if (l3_type == HNS3_L3_TYPE_IPV6)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
	else
		return -EFAULT;

	return  hns3_gro_complete(skb, l234info);
}

static void hns3_set_rx_skb_rss_type(struct hns3_enet_ring *ring,
				     struct sk_buff *skb, u32 rss_hash)
{
	struct hnae3_handle *handle = ring->tqp->handle;
	enum pkt_hash_types rss_type;

	if (rss_hash)
		rss_type = handle->kinfo.rss_type;
	else
		rss_type = PKT_HASH_TYPE_NONE;

	skb_set_hash(skb, rss_hash, rss_type);
}

static int hns3_handle_bdinfo(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	struct net_device *netdev = ring->tqp->handle->kinfo.netdev;
	enum hns3_pkt_l2t_type l2_frame_type;
	u32 bd_base_info, l234info, ol_info;
	struct hns3_desc *desc;
	unsigned int len;
	int pre_ntc, ret;

	/* bdinfo handled below is only valid on the last BD of the
	 * current packet, and ring->next_to_clean indicates the first
	 * descriptor of next packet, so need - 1 below.
	 */
	pre_ntc = ring->next_to_clean ? (ring->next_to_clean - 1) :
					(ring->desc_num - 1);
	desc = &ring->desc[pre_ntc];
	bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
	l234info = le32_to_cpu(desc->rx.l234_info);
	ol_info = le32_to_cpu(desc->rx.ol_info);

	/* Based on hw strategy, the tag offloaded will be stored at
	 * ot_vlan_tag in two layer tag case, and stored at vlan_tag
	 * in one layer tag case.
	 */
	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX) {
		u16 vlan_tag;

		if (hns3_parse_vlan_tag(ring, desc, l234info, &vlan_tag))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       vlan_tag);
	}

	if (unlikely(!(bd_base_info & BIT(HNS3_RXD_VLD_B)))) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.non_vld_descs++;
		u64_stats_update_end(&ring->syncp);

		return -EINVAL;
	}

	if (unlikely(!desc->rx.pkt_len || (l234info & (BIT(HNS3_RXD_TRUNCAT_B) |
				  BIT(HNS3_RXD_L2E_B))))) {
		u64_stats_update_begin(&ring->syncp);
		if (l234info & BIT(HNS3_RXD_L2E_B))
			ring->stats.l2_err++;
		else
			ring->stats.err_pkt_len++;
		u64_stats_update_end(&ring->syncp);

		return -EFAULT;
	}

	len = skb->len;

	/* Do update ip stack process */
	skb->protocol = eth_type_trans(skb, netdev);

	/* This is needed in order to enable forwarding support */
	ret = hns3_set_gro_and_checksum(ring, skb, l234info,
					bd_base_info, ol_info);
	if (unlikely(ret)) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.rx_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return ret;
	}

	l2_frame_type = hnae3_get_field(l234info, HNS3_RXD_DMAC_M,
					HNS3_RXD_DMAC_S);

	u64_stats_update_begin(&ring->syncp);
	ring->stats.rx_pkts++;
	ring->stats.rx_bytes += len;

	if (l2_frame_type == HNS3_L2_TYPE_MULTICAST)
		ring->stats.rx_multicast++;

	u64_stats_update_end(&ring->syncp);

	ring->tqp_vector->rx_group.total_bytes += len;

	hns3_set_rx_skb_rss_type(ring, skb, le32_to_cpu(desc->rx.rss_hash));
	return 0;
}

static int hns3_handle_rx_bd(struct hns3_enet_ring *ring,
			     struct sk_buff **out_skb)
{
	struct sk_buff *skb = ring->skb;
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc *desc;
	u32 bd_base_info;
	int length;
	int ret;

	desc = &ring->desc[ring->next_to_clean];
	desc_cb = &ring->desc_cb[ring->next_to_clean];

	prefetch(desc);

	length = le16_to_cpu(desc->rx.size);
	bd_base_info = le32_to_cpu(desc->rx.bd_base_info);

	/* Check valid BD */
	if (unlikely(!(bd_base_info & BIT(HNS3_RXD_VLD_B))))
		return -ENXIO;

	if (!skb)
		ring->va = (unsigned char *)desc_cb->buf + desc_cb->page_offset;

	/* Prefetch first cache line of first page
	 * Idea is to cache few bytes of the header of the packet. Our L1 Cache
	 * line size is 64B so need to prefetch twice to make it 128B. But in
	 * actual we can have greater size of caches with 128B Level 1 cache
	 * lines. In such a case, single fetch would suffice to cache in the
	 * relevant part of the header.
	 */
	prefetch(ring->va);
#if L1_CACHE_BYTES < 128
	prefetch(ring->va + L1_CACHE_BYTES);
#endif

	if (!skb) {
		ret = hns3_alloc_skb(ring, length, ring->va);
		*out_skb = skb = ring->skb;

		if (ret < 0) /* alloc buffer fail */
			return ret;
		if (ret > 0) { /* need add frag */
			ret = hns3_add_frag(ring, desc, &skb, false);
			if (ret)
				return ret;

			/* As the head data may be changed when GRO enable, copy
			 * the head data in after other data rx completed
			 */
			memcpy(skb->data, ring->va,
			       ALIGN(ring->pull_len, sizeof(long)));
		}
	} else {
		ret = hns3_add_frag(ring, desc, &skb, true);
		if (ret)
			return ret;

		/* As the head data may be changed when GRO enable, copy
		 * the head data in after other data rx completed
		 */
		memcpy(skb->data, ring->va,
		       ALIGN(ring->pull_len, sizeof(long)));
	}

	ret = hns3_handle_bdinfo(ring, skb);
	if (unlikely(ret)) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	skb_record_rx_queue(skb, ring->tqp->tqp_index);
	*out_skb = skb;

	return 0;
}

int hns3_clean_rx_ring(struct hns3_enet_ring *ring, int budget,
		       void (*rx_fn)(struct hns3_enet_ring *, struct sk_buff *))
{
#define RCB_NOF_ALLOC_RX_BUFF_ONCE 16
	int recv_pkts, recv_bds, clean_count, err;
	int unused_count = hns3_desc_unused(ring);
	struct sk_buff *skb = ring->skb;
	int num;

	num = readl_relaxed(ring->tqp->io_base + HNS3_RING_RX_RING_FBDNUM_REG);
	rmb(); /* Make sure num taken effect before the other data is touched */

	recv_pkts = 0, recv_bds = 0, clean_count = 0;
	num -= unused_count;
	unused_count -= ring->pending_buf;

	while (recv_pkts < budget && recv_bds < num) {
		/* Reuse or realloc buffers */
		if (clean_count + unused_count >= RCB_NOF_ALLOC_RX_BUFF_ONCE) {
			hns3_nic_alloc_rx_buffers(ring,
						  clean_count + unused_count);
			clean_count = 0;
			unused_count = hns3_desc_unused(ring) -
					ring->pending_buf;
		}

		/* Poll one pkt */
		err = hns3_handle_rx_bd(ring, &skb);
		if (unlikely(!skb)) /* This fault cannot be repaired */
			goto out;

		if (err == -ENXIO) { /* Do not get FE for the packet */
			goto out;
		} else if (unlikely(err)) {  /* Do jump the err */
			recv_bds += ring->pending_buf;
			clean_count += ring->pending_buf;
			ring->skb = NULL;
			ring->pending_buf = 0;
			continue;
		}

		rx_fn(ring, skb);
		recv_bds += ring->pending_buf;
		clean_count += ring->pending_buf;
		ring->skb = NULL;
		ring->pending_buf = 0;

		recv_pkts++;
	}

out:
	/* Make all data has been write before submit */
	if (clean_count + unused_count > 0)
		hns3_nic_alloc_rx_buffers(ring, clean_count + unused_count);

	return recv_pkts;
}

static bool hns3_get_new_flow_lvl(struct hns3_enet_ring_group *ring_group)
{
#define HNS3_RX_LOW_BYTE_RATE 10000
#define HNS3_RX_MID_BYTE_RATE 20000
#define HNS3_RX_ULTRA_PACKET_RATE 40

	enum hns3_flow_level_range new_flow_level;
	struct hns3_enet_tqp_vector *tqp_vector;
	int packets_per_msecs, bytes_per_msecs;
	u32 time_passed_ms;

	tqp_vector = ring_group->ring->tqp_vector;
	time_passed_ms =
		jiffies_to_msecs(jiffies - tqp_vector->last_jiffies);
	if (!time_passed_ms)
		return false;

	do_div(ring_group->total_packets, time_passed_ms);
	packets_per_msecs = ring_group->total_packets;

	do_div(ring_group->total_bytes, time_passed_ms);
	bytes_per_msecs = ring_group->total_bytes;

	new_flow_level = ring_group->coal.flow_level;

	/* Simple throttlerate management
	 * 0-10MB/s   lower     (50000 ints/s)
	 * 10-20MB/s   middle    (20000 ints/s)
	 * 20-1249MB/s high      (18000 ints/s)
	 * > 40000pps  ultra     (8000 ints/s)
	 */
	switch (new_flow_level) {
	case HNS3_FLOW_LOW:
		if (bytes_per_msecs > HNS3_RX_LOW_BYTE_RATE)
			new_flow_level = HNS3_FLOW_MID;
		break;
	case HNS3_FLOW_MID:
		if (bytes_per_msecs > HNS3_RX_MID_BYTE_RATE)
			new_flow_level = HNS3_FLOW_HIGH;
		else if (bytes_per_msecs <= HNS3_RX_LOW_BYTE_RATE)
			new_flow_level = HNS3_FLOW_LOW;
		break;
	case HNS3_FLOW_HIGH:
	case HNS3_FLOW_ULTRA:
	default:
		if (bytes_per_msecs <= HNS3_RX_MID_BYTE_RATE)
			new_flow_level = HNS3_FLOW_MID;
		break;
	}

	if (packets_per_msecs > HNS3_RX_ULTRA_PACKET_RATE &&
	    &tqp_vector->rx_group == ring_group)
		new_flow_level = HNS3_FLOW_ULTRA;

	ring_group->total_bytes = 0;
	ring_group->total_packets = 0;
	ring_group->coal.flow_level = new_flow_level;

	return true;
}

static bool hns3_get_new_int_gl(struct hns3_enet_ring_group *ring_group)
{
	struct hns3_enet_tqp_vector *tqp_vector;
	u16 new_int_gl;

	if (!ring_group->ring)
		return false;

	tqp_vector = ring_group->ring->tqp_vector;
	if (!tqp_vector->last_jiffies)
		return false;

	if (ring_group->total_packets == 0) {
		ring_group->coal.int_gl = HNS3_INT_GL_50K;
		ring_group->coal.flow_level = HNS3_FLOW_LOW;
		return true;
	}

	if (!hns3_get_new_flow_lvl(ring_group))
		return false;

	new_int_gl = ring_group->coal.int_gl;
	switch (ring_group->coal.flow_level) {
	case HNS3_FLOW_LOW:
		new_int_gl = HNS3_INT_GL_50K;
		break;
	case HNS3_FLOW_MID:
		new_int_gl = HNS3_INT_GL_20K;
		break;
	case HNS3_FLOW_HIGH:
		new_int_gl = HNS3_INT_GL_18K;
		break;
	case HNS3_FLOW_ULTRA:
		new_int_gl = HNS3_INT_GL_8K;
		break;
	default:
		break;
	}

	if (new_int_gl != ring_group->coal.int_gl) {
		ring_group->coal.int_gl = new_int_gl;
		return true;
	}
	return false;
}

static void hns3_update_new_int_gl(struct hns3_enet_tqp_vector *tqp_vector)
{
	struct hns3_enet_ring_group *rx_group = &tqp_vector->rx_group;
	struct hns3_enet_ring_group *tx_group = &tqp_vector->tx_group;
	bool rx_update, tx_update;

	/* update param every 1000ms */
	if (time_before(jiffies,
			tqp_vector->last_jiffies + msecs_to_jiffies(1000)))
		return;

	if (rx_group->coal.gl_adapt_enable) {
		rx_update = hns3_get_new_int_gl(rx_group);
		if (rx_update)
			hns3_set_vector_coalesce_rx_gl(tqp_vector,
						       rx_group->coal.int_gl);
	}

	if (tx_group->coal.gl_adapt_enable) {
		tx_update = hns3_get_new_int_gl(tx_group);
		if (tx_update)
			hns3_set_vector_coalesce_tx_gl(tqp_vector,
						       tx_group->coal.int_gl);
	}

	tqp_vector->last_jiffies = jiffies;
}

static int hns3_nic_common_poll(struct napi_struct *napi, int budget)
{
	struct hns3_nic_priv *priv = netdev_priv(napi->dev);
	struct hns3_enet_ring *ring;
	int rx_pkt_total = 0;

	struct hns3_enet_tqp_vector *tqp_vector =
		container_of(napi, struct hns3_enet_tqp_vector, napi);
	bool clean_complete = true;
	int rx_budget = budget;

	if (unlikely(test_bit(HNS3_NIC_STATE_DOWN, &priv->state))) {
		napi_complete(napi);
		return 0;
	}

	/* Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	hns3_for_each_ring(ring, tqp_vector->tx_group)
		hns3_clean_tx_ring(ring);

	/* make sure rx ring budget not smaller than 1 */
	if (tqp_vector->num_tqps > 1)
		rx_budget = max(budget / tqp_vector->num_tqps, 1);

	hns3_for_each_ring(ring, tqp_vector->rx_group) {
		int rx_cleaned = hns3_clean_rx_ring(ring, rx_budget,
						    hns3_rx_skb);

		if (rx_cleaned >= rx_budget)
			clean_complete = false;

		rx_pkt_total += rx_cleaned;
	}

	tqp_vector->rx_group.total_packets += rx_pkt_total;

	if (!clean_complete)
		return budget;

	if (napi_complete(napi) &&
	    likely(!test_bit(HNS3_NIC_STATE_DOWN, &priv->state))) {
		hns3_update_new_int_gl(tqp_vector);
		hns3_mask_vector_irq(tqp_vector, 1);
	}

	return rx_pkt_total;
}

static int hns3_get_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *cur_chain = head;
	struct hnae3_ring_chain_node *chain;
	struct hns3_enet_ring *tx_ring;
	struct hns3_enet_ring *rx_ring;

	tx_ring = tqp_vector->tx_group.ring;
	if (tx_ring) {
		cur_chain->tqp_index = tx_ring->tqp->tqp_index;
		hnae3_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_TX);
		hnae3_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_TX);

		cur_chain->next = NULL;

		while (tx_ring->next) {
			tx_ring = tx_ring->next;

			chain = devm_kzalloc(&pdev->dev, sizeof(*chain),
					     GFP_KERNEL);
			if (!chain)
				goto err_free_chain;

			cur_chain->next = chain;
			chain->tqp_index = tx_ring->tqp->tqp_index;
			hnae3_set_bit(chain->flag, HNAE3_RING_TYPE_B,
				      HNAE3_RING_TYPE_TX);
			hnae3_set_field(chain->int_gl_idx,
					HNAE3_RING_GL_IDX_M,
					HNAE3_RING_GL_IDX_S,
					HNAE3_RING_GL_TX);

			cur_chain = chain;
		}
	}

	rx_ring = tqp_vector->rx_group.ring;
	if (!tx_ring && rx_ring) {
		cur_chain->next = NULL;
		cur_chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae3_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_RX);
		hnae3_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		rx_ring = rx_ring->next;
	}

	while (rx_ring) {
		chain = devm_kzalloc(&pdev->dev, sizeof(*chain), GFP_KERNEL);
		if (!chain)
			goto err_free_chain;

		cur_chain->next = chain;
		chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae3_set_bit(chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_RX);
		hnae3_set_field(chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		cur_chain = chain;

		rx_ring = rx_ring->next;
	}

	return 0;

err_free_chain:
	cur_chain = head->next;
	while (cur_chain) {
		chain = cur_chain->next;
		devm_kfree(&pdev->dev, cur_chain);
		cur_chain = chain;
	}
	head->next = NULL;

	return -ENOMEM;
}

static void hns3_free_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
					struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *chain_tmp, *chain;

	chain = head->next;

	while (chain) {
		chain_tmp = chain->next;
		devm_kfree(&pdev->dev, chain);
		chain = chain_tmp;
	}
}

static void hns3_add_ring_to_group(struct hns3_enet_ring_group *group,
				   struct hns3_enet_ring *ring)
{
	ring->next = group->ring;
	group->ring = ring;

	group->count++;
}

static void hns3_nic_set_cpumask(struct hns3_nic_priv *priv)
{
	struct pci_dev *pdev = priv->ae_handle->pdev;
	struct hns3_enet_tqp_vector *tqp_vector;
	int num_vectors = priv->vector_num;
	int numa_node;
	int vector_i;

	numa_node = dev_to_node(&pdev->dev);

	for (vector_i = 0; vector_i < num_vectors; vector_i++) {
		tqp_vector = &priv->tqp_vector[vector_i];
		cpumask_set_cpu(cpumask_local_spread(vector_i, numa_node),
				&tqp_vector->affinity_mask);
	}
}

static int hns3_nic_init_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_ring_chain_node vector_ring_chain;
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int ret = 0;
	int i;

	hns3_nic_set_cpumask(priv);

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		hns3_vector_gl_rl_init_hw(tqp_vector, priv);
		tqp_vector->num_tqps = 0;
	}

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		u16 vector_i = i % priv->vector_num;
		u16 tqp_num = h->kinfo.num_tqps;

		tqp_vector = &priv->tqp_vector[vector_i];

		hns3_add_ring_to_group(&tqp_vector->tx_group,
				       priv->ring_data[i].ring);

		hns3_add_ring_to_group(&tqp_vector->rx_group,
				       priv->ring_data[i + tqp_num].ring);

		priv->ring_data[i].ring->tqp_vector = tqp_vector;
		priv->ring_data[i + tqp_num].ring->tqp_vector = tqp_vector;
		tqp_vector->num_tqps++;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];

		tqp_vector->rx_group.total_bytes = 0;
		tqp_vector->rx_group.total_packets = 0;
		tqp_vector->tx_group.total_bytes = 0;
		tqp_vector->tx_group.total_packets = 0;
		tqp_vector->handle = h;

		ret = hns3_get_vector_ring_chain(tqp_vector,
						 &vector_ring_chain);
		if (ret)
			goto map_ring_fail;

		ret = h->ae_algo->ops->map_ring_to_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		if (ret)
			goto map_ring_fail;

		netif_napi_add(priv->netdev, &tqp_vector->napi,
			       hns3_nic_common_poll, NAPI_POLL_WEIGHT);
	}

	return 0;

map_ring_fail:
	while (i--)
		netif_napi_del(&priv->tqp_vector[i].napi);

	return ret;
}

static int hns3_nic_alloc_vector_data(struct hns3_nic_priv *priv)
{
#define HNS3_VECTOR_PF_MAX_NUM		64

	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hnae3_vector_info *vector;
	struct pci_dev *pdev = h->pdev;
	u16 tqp_num = h->kinfo.num_tqps;
	u16 vector_num;
	int ret = 0;
	u16 i;

	/* RSS size, cpu online and vector_num should be the same */
	/* Should consider 2p/4p later */
	vector_num = min_t(u16, num_online_cpus(), tqp_num);
	vector_num = min_t(u16, vector_num, HNS3_VECTOR_PF_MAX_NUM);

	vector = devm_kcalloc(&pdev->dev, vector_num, sizeof(*vector),
			      GFP_KERNEL);
	if (!vector)
		return -ENOMEM;

	/* save the actual available vector number */
	vector_num = h->ae_algo->ops->get_vector(h, vector_num, vector);

	priv->vector_num = vector_num;
	priv->tqp_vector = (struct hns3_enet_tqp_vector *)
		devm_kcalloc(&pdev->dev, vector_num, sizeof(*priv->tqp_vector),
			     GFP_KERNEL);
	if (!priv->tqp_vector) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		tqp_vector->idx = i;
		tqp_vector->mask_addr = vector[i].io_addr;
		tqp_vector->vector_irq = vector[i].vector;
		hns3_vector_gl_rl_init(tqp_vector, priv);
	}

out:
	devm_kfree(&pdev->dev, vector);
	return ret;
}

static void hns3_clear_ring_group(struct hns3_enet_ring_group *group)
{
	group->ring = NULL;
	group->count = 0;
}

static void hns3_nic_uninit_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_ring_chain_node vector_ring_chain;
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];

		if (!tqp_vector->rx_group.ring && !tqp_vector->tx_group.ring)
			continue;

		hns3_get_vector_ring_chain(tqp_vector, &vector_ring_chain);

		h->ae_algo->ops->unmap_ring_from_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		if (tqp_vector->irq_init_flag == HNS3_VECTOR_INITED) {
			irq_set_affinity_hint(tqp_vector->vector_irq, NULL);
			free_irq(tqp_vector->vector_irq, tqp_vector);
			tqp_vector->irq_init_flag = HNS3_VECTOR_NOT_INITED;
		}

		hns3_clear_ring_group(&tqp_vector->rx_group);
		hns3_clear_ring_group(&tqp_vector->tx_group);
		netif_napi_del(&priv->tqp_vector[i].napi);
	}
}

static int hns3_nic_dealloc_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i, ret;

	for (i = 0; i < priv->vector_num; i++) {
		struct hns3_enet_tqp_vector *tqp_vector;

		tqp_vector = &priv->tqp_vector[i];
		ret = h->ae_algo->ops->put_vector(h, tqp_vector->vector_irq);
		if (ret)
			return ret;
	}

	devm_kfree(&pdev->dev, priv->tqp_vector);
	return 0;
}

static int hns3_ring_get_cfg(struct hnae3_queue *q, struct hns3_nic_priv *priv,
			     int ring_type)
{
	struct hns3_nic_ring_data *ring_data = priv->ring_data;
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct pci_dev *pdev = priv->ae_handle->pdev;
	struct hns3_enet_ring *ring;
	int desc_num;

	ring = devm_kzalloc(&pdev->dev, sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	if (ring_type == HNAE3_RING_TYPE_TX) {
		desc_num = priv->ae_handle->kinfo.num_tx_desc;
		ring_data[q->tqp_index].ring = ring;
		ring_data[q->tqp_index].queue_index = q->tqp_index;
		ring->io_base = (u8 __iomem *)q->io_base + HNS3_TX_REG_OFFSET;
	} else {
		desc_num = priv->ae_handle->kinfo.num_rx_desc;
		ring_data[q->tqp_index + queue_num].ring = ring;
		ring_data[q->tqp_index + queue_num].queue_index = q->tqp_index;
		ring->io_base = q->io_base;
	}

	hnae3_set_bit(ring->flag, HNAE3_RING_TYPE_B, ring_type);

	ring->tqp = q;
	ring->desc = NULL;
	ring->desc_cb = NULL;
	ring->dev = priv->dev;
	ring->desc_dma_addr = 0;
	ring->buf_size = q->buf_size;
	ring->desc_num = desc_num;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;

	return 0;
}

static int hns3_queue_to_ring(struct hnae3_queue *tqp,
			      struct hns3_nic_priv *priv)
{
	int ret;

	ret = hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_TX);
	if (ret)
		return ret;

	ret = hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_RX);
	if (ret) {
		devm_kfree(priv->dev, priv->ring_data[tqp->tqp_index].ring);
		return ret;
	}

	return 0;
}

static int hns3_get_ring_config(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i, ret;

	priv->ring_data =  devm_kzalloc(&pdev->dev,
					array3_size(h->kinfo.num_tqps,
						    sizeof(*priv->ring_data),
						    2),
					GFP_KERNEL);
	if (!priv->ring_data)
		return -ENOMEM;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		ret = hns3_queue_to_ring(h->kinfo.tqp[i], priv);
		if (ret)
			goto err;
	}

	return 0;
err:
	while (i--) {
		devm_kfree(priv->dev, priv->ring_data[i].ring);
		devm_kfree(priv->dev,
			   priv->ring_data[i + h->kinfo.num_tqps].ring);
	}

	devm_kfree(&pdev->dev, priv->ring_data);
	priv->ring_data = NULL;
	return ret;
}

static void hns3_put_ring_config(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	if (!priv->ring_data)
		return;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		devm_kfree(priv->dev, priv->ring_data[i].ring);
		devm_kfree(priv->dev,
			   priv->ring_data[i + h->kinfo.num_tqps].ring);
	}
	devm_kfree(priv->dev, priv->ring_data);
	priv->ring_data = NULL;
}

static int hns3_alloc_ring_memory(struct hns3_enet_ring *ring)
{
	int ret;

	if (ring->desc_num <= 0 || ring->buf_size <= 0)
		return -EINVAL;

	ring->desc_cb = devm_kcalloc(ring_to_dev(ring), ring->desc_num,
				     sizeof(ring->desc_cb[0]), GFP_KERNEL);
	if (!ring->desc_cb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = hns3_alloc_desc(ring);
	if (ret)
		goto out_with_desc_cb;

	if (!HNAE3_IS_TX_RING(ring)) {
		ret = hns3_alloc_ring_buffers(ring);
		if (ret)
			goto out_with_desc;
	}

	return 0;

out_with_desc:
	hns3_free_desc(ring);
out_with_desc_cb:
	devm_kfree(ring_to_dev(ring), ring->desc_cb);
	ring->desc_cb = NULL;
out:
	return ret;
}

static void hns3_fini_ring(struct hns3_enet_ring *ring)
{
	hns3_free_desc(ring);
	devm_kfree(ring_to_dev(ring), ring->desc_cb);
	ring->desc_cb = NULL;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
	ring->pending_buf = 0;
	if (ring->skb) {
		dev_kfree_skb_any(ring->skb);
		ring->skb = NULL;
	}
}

static int hns3_buf_size2type(u32 buf_size)
{
	int bd_size_type;

	switch (buf_size) {
	case 512:
		bd_size_type = HNS3_BD_SIZE_512_TYPE;
		break;
	case 1024:
		bd_size_type = HNS3_BD_SIZE_1024_TYPE;
		break;
	case 2048:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
		break;
	case 4096:
		bd_size_type = HNS3_BD_SIZE_4096_TYPE;
		break;
	default:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
	}

	return bd_size_type;
}

static void hns3_init_ring_hw(struct hns3_enet_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	struct hnae3_queue *q = ring->tqp;

	if (!HNAE3_IS_TX_RING(ring)) {
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_L_REG, (u32)dma);
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_RX_RING_BD_LEN_REG,
			       hns3_buf_size2type(ring->buf_size));
		hns3_write_dev(q, HNS3_RING_RX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);

	} else {
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_L_REG,
			       (u32)dma);
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_TX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);
	}
}

static void hns3_init_tx_ring_tc(struct hns3_nic_priv *priv)
{
	struct hnae3_knic_private_info *kinfo = &priv->ae_handle->kinfo;
	int i;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		struct hnae3_tc_info *tc_info = &kinfo->tc_info[i];
		int j;

		if (!tc_info->enable)
			continue;

		for (j = 0; j < tc_info->tqp_count; j++) {
			struct hnae3_queue *q;

			q = priv->ring_data[tc_info->tqp_offset + j].ring->tqp;
			hns3_write_dev(q, HNS3_RING_TX_RING_TC_REG,
				       tc_info->tc);
		}
	}
}

int hns3_init_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int ring_num = h->kinfo.num_tqps * 2;
	int i, j;
	int ret;

	for (i = 0; i < ring_num; i++) {
		ret = hns3_alloc_ring_memory(priv->ring_data[i].ring);
		if (ret) {
			dev_err(priv->dev,
				"Alloc ring memory fail! ret=%d\n", ret);
			goto out_when_alloc_ring_memory;
		}

		u64_stats_init(&priv->ring_data[i].ring->syncp);
	}

	return 0;

out_when_alloc_ring_memory:
	for (j = i - 1; j >= 0; j--)
		hns3_fini_ring(priv->ring_data[j].ring);

	return -ENOMEM;
}

int hns3_uninit_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		hns3_fini_ring(priv->ring_data[i].ring);
		hns3_fini_ring(priv->ring_data[i + h->kinfo.num_tqps].ring);
	}
	return 0;
}

/* Set mac addr if it is configured. or leave it to the AE driver */
static int hns3_init_mac_addr(struct net_device *netdev, bool init)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u8 mac_addr_temp[ETH_ALEN];
	int ret = 0;

	if (h->ae_algo->ops->get_mac_addr && init) {
		h->ae_algo->ops->get_mac_addr(h, mac_addr_temp);
		ether_addr_copy(netdev->dev_addr, mac_addr_temp);
	}

	/* Check if the MAC address is valid, if not get a random one */
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		eth_hw_addr_random(netdev);
		dev_warn(priv->dev, "using random MAC address %pM\n",
			 netdev->dev_addr);
	}

	if (h->ae_algo->ops->set_mac_addr)
		ret = h->ae_algo->ops->set_mac_addr(h, netdev->dev_addr, true);

	return ret;
}

static int hns3_init_phy(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = 0;

	if (h->ae_algo->ops->mac_connect_phy)
		ret = h->ae_algo->ops->mac_connect_phy(h);

	return ret;
}

static void hns3_uninit_phy(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->mac_disconnect_phy)
		h->ae_algo->ops->mac_disconnect_phy(h);
}

static int hns3_restore_fd_rules(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = 0;

	if (h->ae_algo->ops->restore_fd_rules)
		ret = h->ae_algo->ops->restore_fd_rules(h);

	return ret;
}

static void hns3_del_all_fd_rules(struct net_device *netdev, bool clear_list)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->del_all_fd_entries)
		h->ae_algo->ops->del_all_fd_entries(h, clear_list);
}

static int hns3_client_start(struct hnae3_handle *handle)
{
	if (!handle->ae_algo->ops->client_start)
		return 0;

	return handle->ae_algo->ops->client_start(handle);
}

static void hns3_client_stop(struct hnae3_handle *handle)
{
	if (!handle->ae_algo->ops->client_stop)
		return;

	handle->ae_algo->ops->client_stop(handle);
}

static void hns3_info_show(struct hns3_nic_priv *priv)
{
	struct hnae3_knic_private_info *kinfo = &priv->ae_handle->kinfo;

	dev_info(priv->dev, "MAC address: %pM\n", priv->netdev->dev_addr);
	dev_info(priv->dev, "Task queue pairs numbers: %d\n", kinfo->num_tqps);
	dev_info(priv->dev, "RSS size: %d\n", kinfo->rss_size);
	dev_info(priv->dev, "Allocated RSS size: %d\n", kinfo->req_rss_size);
	dev_info(priv->dev, "RX buffer length: %d\n", kinfo->rx_buf_len);
	dev_info(priv->dev, "Desc num per TX queue: %d\n", kinfo->num_tx_desc);
	dev_info(priv->dev, "Desc num per RX queue: %d\n", kinfo->num_rx_desc);
	dev_info(priv->dev, "Total number of enabled TCs: %d\n", kinfo->num_tc);
	dev_info(priv->dev, "Max mtu size: %d\n", priv->netdev->max_mtu);
}

static int hns3_client_init(struct hnae3_handle *handle)
{
	struct pci_dev *pdev = handle->pdev;
	u16 alloc_tqps, max_rss_size;
	struct hns3_nic_priv *priv;
	struct net_device *netdev;
	int ret;

	handle->ae_algo->ops->get_tqps_and_rss_info(handle, &alloc_tqps,
						    &max_rss_size);
	netdev = alloc_etherdev_mq(sizeof(struct hns3_nic_priv), alloc_tqps);
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->dev = &pdev->dev;
	priv->netdev = netdev;
	priv->ae_handle = handle;
	priv->tx_timeout_count = 0;
	set_bit(HNS3_NIC_STATE_DOWN, &priv->state);

	handle->msg_enable = netif_msg_init(debug, DEFAULT_MSG_LEVEL);

	handle->kinfo.netdev = netdev;
	handle->priv = (void *)priv;

	hns3_init_mac_addr(netdev, true);

	hns3_set_default_feature(netdev);

	netdev->watchdog_timeo = HNS3_TX_TIMEOUT;
	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->netdev_ops = &hns3_nic_netdev_ops;
	SET_NETDEV_DEV(netdev, &pdev->dev);
	hns3_ethtool_set_ops(netdev);

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_get_ring_cfg;
	}

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_alloc_vector_data;
	}

	ret = hns3_nic_init_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_vector_data;
	}

	ret = hns3_init_all_ring(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_ring_data;
	}

	ret = hns3_init_phy(netdev);
	if (ret)
		goto out_init_phy;

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(priv->dev, "probe register netdev fail!\n");
		goto out_reg_netdev_fail;
	}

	ret = hns3_client_start(handle);
	if (ret) {
		dev_err(priv->dev, "hns3_client_start fail! ret=%d\n", ret);
			goto out_client_start;
	}

	hns3_dcbnl_setup(handle);

	hns3_dbg_init(handle);

	/* MTU range: (ETH_MIN_MTU(kernel default) - 9702) */
	netdev->max_mtu = HNS3_MAX_MTU;

	set_bit(HNS3_NIC_STATE_INITED, &priv->state);

	if (netif_msg_drv(handle))
		hns3_info_show(priv);

	return ret;

out_client_start:
	unregister_netdev(netdev);
out_reg_netdev_fail:
	hns3_uninit_phy(netdev);
out_init_phy:
	hns3_uninit_all_ring(priv);
out_init_ring_data:
	hns3_nic_uninit_vector_data(priv);
out_init_vector_data:
	hns3_nic_dealloc_vector_data(priv);
out_alloc_vector_data:
	priv->ring_data = NULL;
out_get_ring_cfg:
	priv->ae_handle = NULL;
	free_netdev(netdev);
	return ret;
}

static void hns3_client_uninit(struct hnae3_handle *handle, bool reset)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	hns3_remove_hw_addr(netdev);

	if (netdev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(netdev);

	hns3_client_stop(handle);

	hns3_uninit_phy(netdev);

	if (!test_and_clear_bit(HNS3_NIC_STATE_INITED, &priv->state)) {
		netdev_warn(netdev, "already uninitialized\n");
		goto out_netdev_free;
	}

	hns3_del_all_fd_rules(netdev, true);

	hns3_force_clear_all_rx_ring(handle);

	hns3_nic_uninit_vector_data(priv);

	ret = hns3_nic_dealloc_vector_data(priv);
	if (ret)
		netdev_err(netdev, "dealloc vector error\n");

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		netdev_err(netdev, "uninit ring error\n");

	hns3_put_ring_config(priv);

	hns3_dbg_uninit(handle);

out_netdev_free:
	free_netdev(netdev);
}

static void hns3_link_status_change(struct hnae3_handle *handle, bool linkup)
{
	struct net_device *netdev = handle->kinfo.netdev;

	if (!netdev)
		return;

	if (linkup) {
		netif_carrier_on(netdev);
		netif_tx_wake_all_queues(netdev);
		if (netif_msg_link(handle))
			netdev_info(netdev, "link up\n");
	} else {
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
		if (netif_msg_link(handle))
			netdev_info(netdev, "link down\n");
	}
}

static int hns3_client_setup_tc(struct hnae3_handle *handle, u8 tc)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct net_device *ndev = kinfo->netdev;

	if (tc > HNAE3_MAX_TC)
		return -EINVAL;

	if (!ndev)
		return -ENODEV;

	return hns3_nic_set_real_num_queue(ndev);
}

static int hns3_recover_hw_addr(struct net_device *ndev)
{
	struct netdev_hw_addr_list *list;
	struct netdev_hw_addr *ha, *tmp;
	int ret = 0;

	netif_addr_lock_bh(ndev);
	/* go through and sync uc_addr entries to the device */
	list = &ndev->uc;
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		ret = hns3_nic_uc_sync(ndev, ha->addr);
		if (ret)
			goto out;
	}

	/* go through and sync mc_addr entries to the device */
	list = &ndev->mc;
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		ret = hns3_nic_mc_sync(ndev, ha->addr);
		if (ret)
			goto out;
	}

out:
	netif_addr_unlock_bh(ndev);
	return ret;
}

static void hns3_remove_hw_addr(struct net_device *netdev)
{
	struct netdev_hw_addr_list *list;
	struct netdev_hw_addr *ha, *tmp;

	hns3_nic_uc_unsync(netdev, netdev->dev_addr);

	netif_addr_lock_bh(netdev);
	/* go through and unsync uc_addr entries to the device */
	list = &netdev->uc;
	list_for_each_entry_safe(ha, tmp, &list->list, list)
		hns3_nic_uc_unsync(netdev, ha->addr);

	/* go through and unsync mc_addr entries to the device */
	list = &netdev->mc;
	list_for_each_entry_safe(ha, tmp, &list->list, list)
		if (ha->refcount > 1)
			hns3_nic_mc_unsync(netdev, ha->addr);

	netif_addr_unlock_bh(netdev);
}

static void hns3_clear_tx_ring(struct hns3_enet_ring *ring)
{
	while (ring->next_to_clean != ring->next_to_use) {
		ring->desc[ring->next_to_clean].tx.bdtp_fe_sc_vld_ra_ri = 0;
		hns3_free_buffer_detach(ring, ring->next_to_clean);
		ring_ptr_move_fw(ring, next_to_clean);
	}
}

static int hns3_clear_rx_ring(struct hns3_enet_ring *ring)
{
	struct hns3_desc_cb res_cbs;
	int ret;

	while (ring->next_to_use != ring->next_to_clean) {
		/* When a buffer is not reused, it's memory has been
		 * freed in hns3_handle_rx_bd or will be freed by
		 * stack, so we need to replace the buffer here.
		 */
		if (!ring->desc_cb[ring->next_to_use].reuse_flag) {
			ret = hns3_reserve_buffer_map(ring, &res_cbs);
			if (ret) {
				u64_stats_update_begin(&ring->syncp);
				ring->stats.sw_err_cnt++;
				u64_stats_update_end(&ring->syncp);
				/* if alloc new buffer fail, exit directly
				 * and reclear in up flow.
				 */
				netdev_warn(ring->tqp->handle->kinfo.netdev,
					    "reserve buffer map failed, ret = %d\n",
					    ret);
				return ret;
			}
			hns3_replace_buffer(ring, ring->next_to_use, &res_cbs);
		}
		ring_ptr_move_fw(ring, next_to_use);
	}

	/* Free the pending skb in rx ring */
	if (ring->skb) {
		dev_kfree_skb_any(ring->skb);
		ring->skb = NULL;
		ring->pending_buf = 0;
	}

	return 0;
}

static void hns3_force_clear_rx_ring(struct hns3_enet_ring *ring)
{
	while (ring->next_to_use != ring->next_to_clean) {
		/* When a buffer is not reused, it's memory has been
		 * freed in hns3_handle_rx_bd or will be freed by
		 * stack, so only need to unmap the buffer here.
		 */
		if (!ring->desc_cb[ring->next_to_use].reuse_flag) {
			hns3_unmap_buffer(ring,
					  &ring->desc_cb[ring->next_to_use]);
			ring->desc_cb[ring->next_to_use].dma = 0;
		}

		ring_ptr_move_fw(ring, next_to_use);
	}
}

static void hns3_force_clear_all_rx_ring(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hns3_enet_ring *ring;
	u32 i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		ring = priv->ring_data[i + h->kinfo.num_tqps].ring;
		hns3_force_clear_rx_ring(ring);
	}
}

static void hns3_clear_all_ring(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	u32 i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		struct netdev_queue *dev_queue;
		struct hns3_enet_ring *ring;

		ring = priv->ring_data[i].ring;
		hns3_clear_tx_ring(ring);
		dev_queue = netdev_get_tx_queue(ndev,
						priv->ring_data[i].queue_index);
		netdev_tx_reset_queue(dev_queue);

		ring = priv->ring_data[i + h->kinfo.num_tqps].ring;
		/* Continue to clear other rings even if clearing some
		 * rings failed.
		 */
		hns3_clear_rx_ring(ring);
	}
}

int hns3_nic_reset_all_ring(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hns3_enet_ring *rx_ring;
	int i, j;
	int ret;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		ret = h->ae_algo->ops->reset_queue(h, i);
		if (ret)
			return ret;

		hns3_init_ring_hw(priv->ring_data[i].ring);

		/* We need to clear tx ring here because self test will
		 * use the ring and will not run down before up
		 */
		hns3_clear_tx_ring(priv->ring_data[i].ring);
		priv->ring_data[i].ring->next_to_clean = 0;
		priv->ring_data[i].ring->next_to_use = 0;

		rx_ring = priv->ring_data[i + h->kinfo.num_tqps].ring;
		hns3_init_ring_hw(rx_ring);
		ret = hns3_clear_rx_ring(rx_ring);
		if (ret)
			return ret;

		/* We can not know the hardware head and tail when this
		 * function is called in reset flow, so we reuse all desc.
		 */
		for (j = 0; j < rx_ring->desc_num; j++)
			hns3_reuse_buffer(rx_ring, j);

		rx_ring->next_to_clean = 0;
		rx_ring->next_to_use = 0;
	}

	hns3_init_tx_ring_tc(priv);

	return 0;
}

static void hns3_store_coal(struct hns3_nic_priv *priv)
{
	/* ethtool only support setting and querying one coal
	 * configuation for now, so save the vector 0' coal
	 * configuation here in order to restore it.
	 */
	memcpy(&priv->tx_coal, &priv->tqp_vector[0].tx_group.coal,
	       sizeof(struct hns3_enet_coalesce));
	memcpy(&priv->rx_coal, &priv->tqp_vector[0].rx_group.coal,
	       sizeof(struct hns3_enet_coalesce));
}

static void hns3_restore_coal(struct hns3_nic_priv *priv)
{
	u16 vector_num = priv->vector_num;
	int i;

	for (i = 0; i < vector_num; i++) {
		memcpy(&priv->tqp_vector[i].tx_group.coal, &priv->tx_coal,
		       sizeof(struct hns3_enet_coalesce));
		memcpy(&priv->tqp_vector[i].rx_group.coal, &priv->rx_coal,
		       sizeof(struct hns3_enet_coalesce));
	}
}

static int hns3_reset_notify_down_enet(struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct net_device *ndev = kinfo->netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);

	if (test_and_set_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
		return 0;

	/* it is cumbersome for hardware to pick-and-choose entries for deletion
	 * from table space. Hence, for function reset software intervention is
	 * required to delete the entries
	 */
	if (hns3_dev_ongoing_func_reset(ae_dev)) {
		hns3_remove_hw_addr(ndev);
		hns3_del_all_fd_rules(ndev, false);
	}

	if (!netif_running(ndev))
		return 0;

	return hns3_nic_net_stop(ndev);
}

static int hns3_reset_notify_up_enet(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hns3_nic_priv *priv = netdev_priv(kinfo->netdev);
	int ret = 0;

	clear_bit(HNS3_NIC_STATE_RESETTING, &priv->state);

	if (netif_running(kinfo->netdev)) {
		ret = hns3_nic_net_open(kinfo->netdev);
		if (ret) {
			set_bit(HNS3_NIC_STATE_RESETTING, &priv->state);
			netdev_err(kinfo->netdev,
				   "net up fail, ret=%d!\n", ret);
			return ret;
		}
	}

	return ret;
}

static int hns3_reset_notify_init_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret)
		return ret;

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret)
		goto err_put_ring;

	hns3_restore_coal(priv);

	ret = hns3_nic_init_vector_data(priv);
	if (ret)
		goto err_dealloc_vector;

	ret = hns3_init_all_ring(priv);
	if (ret)
		goto err_uninit_vector;

	ret = hns3_client_start(handle);
	if (ret) {
		dev_err(priv->dev, "hns3_client_start fail! ret=%d\n", ret);
		goto err_uninit_ring;
	}

	set_bit(HNS3_NIC_STATE_INITED, &priv->state);

	return ret;

err_uninit_ring:
	hns3_uninit_all_ring(priv);
err_uninit_vector:
	hns3_nic_uninit_vector_data(priv);
err_dealloc_vector:
	hns3_nic_dealloc_vector_data(priv);
err_put_ring:
	hns3_put_ring_config(priv);

	return ret;
}

static int hns3_reset_notify_restore_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	bool vlan_filter_enable;
	int ret;

	ret = hns3_init_mac_addr(netdev, false);
	if (ret)
		return ret;

	ret = hns3_recover_hw_addr(netdev);
	if (ret)
		return ret;

	ret = hns3_update_promisc_mode(netdev, handle->netdev_flags);
	if (ret)
		return ret;

	vlan_filter_enable = netdev->flags & IFF_PROMISC ? false : true;
	hns3_enable_vlan_filter(netdev, vlan_filter_enable);

	if (handle->ae_algo->ops->restore_vlan_table)
		handle->ae_algo->ops->restore_vlan_table(handle);

	return hns3_restore_fd_rules(netdev);
}

static int hns3_reset_notify_uninit_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	if (!test_and_clear_bit(HNS3_NIC_STATE_INITED, &priv->state)) {
		netdev_warn(netdev, "already uninitialized\n");
		return 0;
	}

	hns3_force_clear_all_rx_ring(handle);

	hns3_nic_uninit_vector_data(priv);

	hns3_store_coal(priv);

	ret = hns3_nic_dealloc_vector_data(priv);
	if (ret)
		netdev_err(netdev, "dealloc vector error\n");

	ret = hns3_uninit_all_ring(priv);
	if (ret)
		netdev_err(netdev, "uninit ring error\n");

	hns3_put_ring_config(priv);

	return ret;
}

static int hns3_reset_notify(struct hnae3_handle *handle,
			     enum hnae3_reset_notify_type type)
{
	int ret = 0;

	switch (type) {
	case HNAE3_UP_CLIENT:
		ret = hns3_reset_notify_up_enet(handle);
		break;
	case HNAE3_DOWN_CLIENT:
		ret = hns3_reset_notify_down_enet(handle);
		break;
	case HNAE3_INIT_CLIENT:
		ret = hns3_reset_notify_init_enet(handle);
		break;
	case HNAE3_UNINIT_CLIENT:
		ret = hns3_reset_notify_uninit_enet(handle);
		break;
	case HNAE3_RESTORE_CLIENT:
		ret = hns3_reset_notify_restore_enet(handle);
		break;
	default:
		break;
	}

	return ret;
}

int hns3_set_channels(struct net_device *netdev,
		      struct ethtool_channels *ch)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	bool rxfh_configured = netif_is_rxfh_configured(netdev);
	u32 new_tqp_num = ch->combined_count;
	u16 org_tqp_num;
	int ret;

	if (ch->rx_count || ch->tx_count)
		return -EINVAL;

	if (new_tqp_num > hns3_get_max_available_channels(h) ||
	    new_tqp_num < 1) {
		dev_err(&netdev->dev,
			"Change tqps fail, the tqp range is from 1 to %d",
			hns3_get_max_available_channels(h));
		return -EINVAL;
	}

	if (kinfo->rss_size == new_tqp_num)
		return 0;

	ret = hns3_reset_notify(h, HNAE3_DOWN_CLIENT);
	if (ret)
		return ret;

	ret = hns3_reset_notify(h, HNAE3_UNINIT_CLIENT);
	if (ret)
		return ret;

	org_tqp_num = h->kinfo.num_tqps;
	ret = h->ae_algo->ops->set_channels(h, new_tqp_num, rxfh_configured);
	if (ret) {
		ret = h->ae_algo->ops->set_channels(h, org_tqp_num,
						    rxfh_configured);
		if (ret) {
			/* If revert to old tqp failed, fatal error occurred */
			dev_err(&netdev->dev,
				"Revert to old tqp num fail, ret=%d", ret);
			return ret;
		}
		dev_info(&netdev->dev,
			 "Change tqp num fail, Revert to old tqp num");
	}
	ret = hns3_reset_notify(h, HNAE3_INIT_CLIENT);
	if (ret)
		return ret;

	return hns3_reset_notify(h, HNAE3_UP_CLIENT);
}

static const struct hnae3_client_ops client_ops = {
	.init_instance = hns3_client_init,
	.uninit_instance = hns3_client_uninit,
	.link_status_change = hns3_link_status_change,
	.setup_tc = hns3_client_setup_tc,
	.reset_notify = hns3_reset_notify,
};

/* hns3_init_module - Driver registration routine
 * hns3_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init hns3_init_module(void)
{
	int ret;

	pr_info("%s: %s - version\n", hns3_driver_name, hns3_driver_string);
	pr_info("%s: %s\n", hns3_driver_name, hns3_copyright);

	client.type = HNAE3_CLIENT_KNIC;
	snprintf(client.name, HNAE3_CLIENT_NAME_LENGTH - 1, "%s",
		 hns3_driver_name);

	client.ops = &client_ops;

	INIT_LIST_HEAD(&client.node);

	hns3_dbg_register_debugfs(hns3_driver_name);

	ret = hnae3_register_client(&client);
	if (ret)
		goto err_reg_client;

	ret = pci_register_driver(&hns3_driver);
	if (ret)
		goto err_reg_driver;

	return ret;

err_reg_driver:
	hnae3_unregister_client(&client);
err_reg_client:
	hns3_dbg_unregister_debugfs();
	return ret;
}
module_init(hns3_init_module);

/* hns3_exit_module - Driver exit cleanup routine
 * hns3_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit hns3_exit_module(void)
{
	pci_unregister_driver(&hns3_driver);
	hnae3_unregister_client(&client);
	hns3_dbg_unregister_debugfs();
}
module_exit(hns3_exit_module);

MODULE_DESCRIPTION("HNS3: Hisilicon Ethernet Driver");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pci:hns-nic");
MODULE_VERSION(HNS3_MOD_VERSION);
