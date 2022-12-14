// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
/*
 * Copyright 2008 - 2016 Freescale Semiconductor Inc.
 * Copyright 2020 NXP
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/io.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/highmem.h>
#include <linux/percpu.h>
#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <linux/phy_fixed.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <soc/fsl/bman.h>
#include <soc/fsl/qman.h>
#include "fman.h"
#include "fman_port.h"
#include "mac.h"
#include "dpaa_eth.h"

/* CREATE_TRACE_POINTS only needs to be defined once. Other dpaa files
 * using trace events only need to #include <trace/events/sched.h>
 */
#define CREATE_TRACE_POINTS
#include "dpaa_eth_trace.h"

static int debug = -1;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Module/Driver verbosity level (0=none,...,16=all)");

static u16 tx_timeout = 1000;
module_param(tx_timeout, ushort, 0444);
MODULE_PARM_DESC(tx_timeout, "The Tx timeout in ms");

#define FM_FD_STAT_RX_ERRORS						\
	(FM_FD_ERR_DMA | FM_FD_ERR_PHYSICAL	| \
	 FM_FD_ERR_SIZE | FM_FD_ERR_CLS_DISCARD | \
	 FM_FD_ERR_EXTRACTION | FM_FD_ERR_NO_SCHEME	| \
	 FM_FD_ERR_PRS_TIMEOUT | FM_FD_ERR_PRS_ILL_INSTRUCT | \
	 FM_FD_ERR_PRS_HDR_ERR)

#define FM_FD_STAT_TX_ERRORS \
	(FM_FD_ERR_UNSUPPORTED_FORMAT | \
	 FM_FD_ERR_LENGTH | FM_FD_ERR_DMA)

#define DPAA_MSG_DEFAULT (NETIF_MSG_DRV | NETIF_MSG_PROBE | \
			  NETIF_MSG_LINK | NETIF_MSG_IFUP | \
			  NETIF_MSG_IFDOWN | NETIF_MSG_HW)

#define DPAA_INGRESS_CS_THRESHOLD 0x10000000
/* Ingress congestion threshold on FMan ports
 * The size in bytes of the ingress tail-drop threshold on FMan ports.
 * Traffic piling up above this value will be rejected by QMan and discarded
 * by FMan.
 */

/* Size in bytes of the FQ taildrop threshold */
#define DPAA_FQ_TD 0x200000

#define DPAA_CS_THRESHOLD_1G 0x06000000
/* Egress congestion threshold on 1G ports, range 0x1000 .. 0x10000000
 * The size in bytes of the egress Congestion State notification threshold on
 * 1G ports. The 1G dTSECs can quite easily be flooded by cores doing Tx in a
 * tight loop (e.g. by sending UDP datagrams at "while(1) speed"),
 * and the larger the frame size, the more acute the problem.
 * So we have to find a balance between these factors:
 * - avoiding the device staying congested for a prolonged time (risking
 *   the netdev watchdog to fire - see also the tx_timeout module param);
 * - affecting performance of protocols such as TCP, which otherwise
 *   behave well under the congestion notification mechanism;
 * - preventing the Tx cores from tightly-looping (as if the congestion
 *   threshold was too low to be effective);
 * - running out of memory if the CS threshold is set too high.
 */

#define DPAA_CS_THRESHOLD_10G 0x10000000
/* The size in bytes of the egress Congestion State notification threshold on
 * 10G ports, range 0x1000 .. 0x10000000
 */

/* Largest value that the FQD's OAL field can hold */
#define FSL_QMAN_MAX_OAL	127

/* Default alignment for start of data in an Rx FD */
#ifdef CONFIG_DPAA_ERRATUM_A050385
/* aligning data start to 64 avoids DMA transaction splits, unless the buffer
 * is crossing a 4k page boundary
 */
#define DPAA_FD_DATA_ALIGNMENT  (fman_has_errata_a050385() ? 64 : 16)
/* aligning to 256 avoids DMA transaction splits caused by 4k page boundary
 * crossings; also, all SG fragments except the last must have a size multiple
 * of 256 to avoid DMA transaction splits
 */
#define DPAA_A050385_ALIGN 256
#define DPAA_FD_RX_DATA_ALIGNMENT (fman_has_errata_a050385() ? \
				   DPAA_A050385_ALIGN : 16)
#else
#define DPAA_FD_DATA_ALIGNMENT  16
#define DPAA_FD_RX_DATA_ALIGNMENT DPAA_FD_DATA_ALIGNMENT
#endif

/* The DPAA requires 256 bytes reserved and mapped for the SGT */
#define DPAA_SGT_SIZE 256

/* Values for the L3R field of the FM Parse Results
 */
/* L3 Type field: First IP Present IPv4 */
#define FM_L3_PARSE_RESULT_IPV4	0x8000
/* L3 Type field: First IP Present IPv6 */
#define FM_L3_PARSE_RESULT_IPV6	0x4000
/* Values for the L4R field of the FM Parse Results */
/* L4 Type field: UDP */
#define FM_L4_PARSE_RESULT_UDP	0x40
/* L4 Type field: TCP */
#define FM_L4_PARSE_RESULT_TCP	0x20

/* FD status field indicating whether the FM Parser has attempted to validate
 * the L4 csum of the frame.
 * Note that having this bit set doesn't necessarily imply that the checksum
 * is valid. One would have to check the parse results to find that out.
 */
#define FM_FD_STAT_L4CV         0x00000004

#define DPAA_SGT_MAX_ENTRIES 16 /* maximum number of entries in SG Table */
#define DPAA_BUFF_RELEASE_MAX 8 /* maximum number of buffers released at once */

#define FSL_DPAA_BPID_INV		0xff
#define FSL_DPAA_ETH_MAX_BUF_COUNT	128
#define FSL_DPAA_ETH_REFILL_THRESHOLD	80

#define DPAA_TX_PRIV_DATA_SIZE	16
#define DPAA_PARSE_RESULTS_SIZE sizeof(struct fman_prs_result)
#define DPAA_TIME_STAMP_SIZE 8
#define DPAA_HASH_RESULTS_SIZE 8
#define DPAA_HWA_SIZE (DPAA_PARSE_RESULTS_SIZE + DPAA_TIME_STAMP_SIZE \
		       + DPAA_HASH_RESULTS_SIZE)
#define DPAA_RX_PRIV_DATA_DEFAULT_SIZE (DPAA_TX_PRIV_DATA_SIZE + \
					XDP_PACKET_HEADROOM - DPAA_HWA_SIZE)
#ifdef CONFIG_DPAA_ERRATUM_A050385
#define DPAA_RX_PRIV_DATA_A050385_SIZE (DPAA_A050385_ALIGN - DPAA_HWA_SIZE)
#define DPAA_RX_PRIV_DATA_SIZE (fman_has_errata_a050385() ? \
				DPAA_RX_PRIV_DATA_A050385_SIZE : \
				DPAA_RX_PRIV_DATA_DEFAULT_SIZE)
#else
#define DPAA_RX_PRIV_DATA_SIZE DPAA_RX_PRIV_DATA_DEFAULT_SIZE
#endif

#define DPAA_ETH_PCD_RXQ_NUM	128

#define DPAA_ENQUEUE_RETRIES	100000

enum port_type {RX, TX};

struct fm_port_fqs {
	struct dpaa_fq *tx_defq;
	struct dpaa_fq *tx_errq;
	struct dpaa_fq *rx_defq;
	struct dpaa_fq *rx_errq;
	struct dpaa_fq *rx_pcdq;
};

/* All the dpa bps in use at any moment */
static struct dpaa_bp *dpaa_bp_array[BM_MAX_NUM_OF_POOLS];

#define DPAA_BP_RAW_SIZE 4096

#ifdef CONFIG_DPAA_ERRATUM_A050385
#define dpaa_bp_size(raw_size) (SKB_WITH_OVERHEAD(raw_size) & \
				~(DPAA_A050385_ALIGN - 1))
#else
#define dpaa_bp_size(raw_size) SKB_WITH_OVERHEAD(raw_size)
#endif

static int dpaa_max_frm;

static int dpaa_rx_extra_headroom;

#define dpaa_get_max_mtu()	\
	(dpaa_max_frm - (VLAN_ETH_HLEN + ETH_FCS_LEN))

static void dpaa_eth_cgr_set_speed(struct mac_device *mac_dev, int speed);

static int dpaa_netdev_init(struct net_device *net_dev,
			    const struct net_device_ops *dpaa_ops,
			    u16 tx_timeout)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct device *dev = net_dev->dev.parent;
	struct mac_device *mac_dev = priv->mac_dev;
	struct dpaa_percpu_priv *percpu_priv;
	const u8 *mac_addr;
	int i, err;

	/* Although we access another CPU's private data here
	 * we do it at initialization so it is safe
	 */
	for_each_possible_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);
		percpu_priv->net_dev = net_dev;
	}

	net_dev->netdev_ops = dpaa_ops;
	mac_addr = mac_dev->addr;

	net_dev->mem_start = (unsigned long)priv->mac_dev->res->start;
	net_dev->mem_end = (unsigned long)priv->mac_dev->res->end;

	net_dev->min_mtu = ETH_MIN_MTU;
	net_dev->max_mtu = dpaa_get_max_mtu();

	net_dev->hw_features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				 NETIF_F_LLTX | NETIF_F_RXHASH);

	net_dev->hw_features |= NETIF_F_SG | NETIF_F_HIGHDMA;
	/* The kernels enables GSO automatically, if we declare NETIF_F_SG.
	 * For conformity, we'll still declare GSO explicitly.
	 */
	net_dev->features |= NETIF_F_GSO;
	net_dev->features |= NETIF_F_RXCSUM;

	net_dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	/* we do not want shared skbs on TX */
	net_dev->priv_flags &= ~IFF_TX_SKB_SHARING;

	net_dev->features |= net_dev->hw_features;
	net_dev->vlan_features = net_dev->features;

	if (is_valid_ether_addr(mac_addr)) {
		memcpy(net_dev->perm_addr, mac_addr, net_dev->addr_len);
		eth_hw_addr_set(net_dev, mac_addr);
	} else {
		eth_hw_addr_random(net_dev);
		err = mac_dev->change_addr(mac_dev->fman_mac,
			(const enet_addr_t *)net_dev->dev_addr);
		if (err) {
			dev_err(dev, "Failed to set random MAC address\n");
			return -EINVAL;
		}
		dev_info(dev, "Using random MAC address: %pM\n",
			 net_dev->dev_addr);
	}

	net_dev->ethtool_ops = &dpaa_ethtool_ops;

	net_dev->needed_headroom = priv->tx_headroom;
	net_dev->watchdog_timeo = msecs_to_jiffies(tx_timeout);

	mac_dev->net_dev = net_dev;
	mac_dev->update_speed = dpaa_eth_cgr_set_speed;

	/* start without the RUNNING flag, phylib controls it later */
	netif_carrier_off(net_dev);

	err = register_netdev(net_dev);
	if (err < 0) {
		dev_err(dev, "register_netdev() = %d\n", err);
		return err;
	}

	return 0;
}

static int dpaa_stop(struct net_device *net_dev)
{
	struct mac_device *mac_dev;
	struct dpaa_priv *priv;
	int i, err, error;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	netif_tx_stop_all_queues(net_dev);
	/* Allow the Fman (Tx) port to process in-flight frames before we
	 * try switching it off.
	 */
	msleep(200);

	if (mac_dev->phy_dev)
		phy_stop(mac_dev->phy_dev);
	mac_dev->disable(mac_dev->fman_mac);

	for (i = 0; i < ARRAY_SIZE(mac_dev->port); i++) {
		error = fman_port_disable(mac_dev->port[i]);
		if (error)
			err = error;
	}

	if (net_dev->phydev)
		phy_disconnect(net_dev->phydev);
	net_dev->phydev = NULL;

	msleep(200);

	return err;
}

static void dpaa_tx_timeout(struct net_device *net_dev, unsigned int txqueue)
{
	struct dpaa_percpu_priv *percpu_priv;
	const struct dpaa_priv	*priv;

	priv = netdev_priv(net_dev);
	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	netif_crit(priv, timer, net_dev, "Transmit timeout latency: %u ms\n",
		   jiffies_to_msecs(jiffies - dev_trans_start(net_dev)));

	percpu_priv->stats.tx_errors++;
}

/* Calculates the statistics for the given device by adding the statistics
 * collected by each CPU.
 */
static void dpaa_get_stats64(struct net_device *net_dev,
			     struct rtnl_link_stats64 *s)
{
	int numstats = sizeof(struct rtnl_link_stats64) / sizeof(u64);
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct dpaa_percpu_priv *percpu_priv;
	u64 *netstats = (u64 *)s;
	u64 *cpustats;
	int i, j;

	for_each_possible_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		cpustats = (u64 *)&percpu_priv->stats;

		/* add stats from all CPUs */
		for (j = 0; j < numstats; j++)
			netstats[j] += cpustats[j];
	}
}

static int dpaa_setup_tc(struct net_device *net_dev, enum tc_setup_type type,
			 void *type_data)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct tc_mqprio_qopt *mqprio = type_data;
	u8 num_tc;
	int i;

	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
	num_tc = mqprio->num_tc;

	if (num_tc == priv->num_tc)
		return 0;

	if (!num_tc) {
		netdev_reset_tc(net_dev);
		goto out;
	}

	if (num_tc > DPAA_TC_NUM) {
		netdev_err(net_dev, "Too many traffic classes: max %d supported.\n",
			   DPAA_TC_NUM);
		return -EINVAL;
	}

	netdev_set_num_tc(net_dev, num_tc);

	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(net_dev, i, DPAA_TC_TXQ_NUM,
				    i * DPAA_TC_TXQ_NUM);

out:
	priv->num_tc = num_tc ? : 1;
	netif_set_real_num_tx_queues(net_dev, priv->num_tc * DPAA_TC_TXQ_NUM);
	return 0;
}

static struct mac_device *dpaa_mac_dev_get(struct platform_device *pdev)
{
	struct dpaa_eth_data *eth_data;
	struct device *dpaa_dev;
	struct mac_device *mac_dev;

	dpaa_dev = &pdev->dev;
	eth_data = dpaa_dev->platform_data;
	if (!eth_data) {
		dev_err(dpaa_dev, "eth_data missing\n");
		return ERR_PTR(-ENODEV);
	}
	mac_dev = eth_data->mac_dev;
	if (!mac_dev) {
		dev_err(dpaa_dev, "mac_dev missing\n");
		return ERR_PTR(-EINVAL);
	}

	return mac_dev;
}

static int dpaa_set_mac_address(struct net_device *net_dev, void *addr)
{
	const struct dpaa_priv *priv;
	struct mac_device *mac_dev;
	struct sockaddr old_addr;
	int err;

	priv = netdev_priv(net_dev);

	memcpy(old_addr.sa_data, net_dev->dev_addr,  ETH_ALEN);

	err = eth_mac_addr(net_dev, addr);
	if (err < 0) {
		netif_err(priv, drv, net_dev, "eth_mac_addr() = %d\n", err);
		return err;
	}

	mac_dev = priv->mac_dev;

	err = mac_dev->change_addr(mac_dev->fman_mac,
				   (const enet_addr_t *)net_dev->dev_addr);
	if (err < 0) {
		netif_err(priv, drv, net_dev, "mac_dev->change_addr() = %d\n",
			  err);
		/* reverting to previous address */
		eth_mac_addr(net_dev, &old_addr);

		return err;
	}

	return 0;
}

static void dpaa_set_rx_mode(struct net_device *net_dev)
{
	const struct dpaa_priv	*priv;
	int err;

	priv = netdev_priv(net_dev);

	if (!!(net_dev->flags & IFF_PROMISC) != priv->mac_dev->promisc) {
		priv->mac_dev->promisc = !priv->mac_dev->promisc;
		err = priv->mac_dev->set_promisc(priv->mac_dev->fman_mac,
						 priv->mac_dev->promisc);
		if (err < 0)
			netif_err(priv, drv, net_dev,
				  "mac_dev->set_promisc() = %d\n",
				  err);
	}

	if (!!(net_dev->flags & IFF_ALLMULTI) != priv->mac_dev->allmulti) {
		priv->mac_dev->allmulti = !priv->mac_dev->allmulti;
		err = priv->mac_dev->set_allmulti(priv->mac_dev->fman_mac,
						  priv->mac_dev->allmulti);
		if (err < 0)
			netif_err(priv, drv, net_dev,
				  "mac_dev->set_allmulti() = %d\n",
				  err);
	}

	err = priv->mac_dev->set_multi(net_dev, priv->mac_dev);
	if (err < 0)
		netif_err(priv, drv, net_dev, "mac_dev->set_multi() = %d\n",
			  err);
}

static struct dpaa_bp *dpaa_bpid2pool(int bpid)
{
	if (WARN_ON(bpid < 0 || bpid >= BM_MAX_NUM_OF_POOLS))
		return NULL;

	return dpaa_bp_array[bpid];
}

/* checks if this bpool is already allocated */
static bool dpaa_bpid2pool_use(int bpid)
{
	if (dpaa_bpid2pool(bpid)) {
		refcount_inc(&dpaa_bp_array[bpid]->refs);
		return true;
	}

	return false;
}

/* called only once per bpid by dpaa_bp_alloc_pool() */
static void dpaa_bpid2pool_map(int bpid, struct dpaa_bp *dpaa_bp)
{
	dpaa_bp_array[bpid] = dpaa_bp;
	refcount_set(&dpaa_bp->refs, 1);
}

static int dpaa_bp_alloc_pool(struct dpaa_bp *dpaa_bp)
{
	int err;

	if (dpaa_bp->size == 0 || dpaa_bp->config_count == 0) {
		pr_err("%s: Buffer pool is not properly initialized! Missing size or initial number of buffers\n",
		       __func__);
		return -EINVAL;
	}

	/* If the pool is already specified, we only create one per bpid */
	if (dpaa_bp->bpid != FSL_DPAA_BPID_INV &&
	    dpaa_bpid2pool_use(dpaa_bp->bpid))
		return 0;

	if (dpaa_bp->bpid == FSL_DPAA_BPID_INV) {
		dpaa_bp->pool = bman_new_pool();
		if (!dpaa_bp->pool) {
			pr_err("%s: bman_new_pool() failed\n",
			       __func__);
			return -ENODEV;
		}

		dpaa_bp->bpid = (u8)bman_get_bpid(dpaa_bp->pool);
	}

	if (dpaa_bp->seed_cb) {
		err = dpaa_bp->seed_cb(dpaa_bp);
		if (err)
			goto pool_seed_failed;
	}

	dpaa_bpid2pool_map(dpaa_bp->bpid, dpaa_bp);

	return 0;

pool_seed_failed:
	pr_err("%s: pool seeding failed\n", __func__);
	bman_free_pool(dpaa_bp->pool);

	return err;
}

/* remove and free all the buffers from the given buffer pool */
static void dpaa_bp_drain(struct dpaa_bp *bp)
{
	u8 num = 8;
	int ret;

	do {
		struct bm_buffer bmb[8];
		int i;

		ret = bman_acquire(bp->pool, bmb, num);
		if (ret < 0) {
			if (num == 8) {
				/* we have less than 8 buffers left;
				 * drain them one by one
				 */
				num = 1;
				ret = 1;
				continue;
			} else {
				/* Pool is fully drained */
				break;
			}
		}

		if (bp->free_buf_cb)
			for (i = 0; i < num; i++)
				bp->free_buf_cb(bp, &bmb[i]);
	} while (ret > 0);
}

static void dpaa_bp_free(struct dpaa_bp *dpaa_bp)
{
	struct dpaa_bp *bp = dpaa_bpid2pool(dpaa_bp->bpid);

	/* the mapping between bpid and dpaa_bp is done very late in the
	 * allocation procedure; if something failed before the mapping, the bp
	 * was not configured, therefore we don't need the below instructions
	 */
	if (!bp)
		return;

	if (!refcount_dec_and_test(&bp->refs))
		return;

	if (bp->free_buf_cb)
		dpaa_bp_drain(bp);

	dpaa_bp_array[bp->bpid] = NULL;
	bman_free_pool(bp->pool);
}

static void dpaa_bps_free(struct dpaa_priv *priv)
{
	dpaa_bp_free(priv->dpaa_bp);
}

/* Use multiple WQs for FQ assignment:
 *	- Tx Confirmation queues go to WQ1.
 *	- Rx Error and Tx Error queues go to WQ5 (giving them a better chance
 *	  to be scheduled, in case there are many more FQs in WQ6).
 *	- Rx Default goes to WQ6.
 *	- Tx queues go to different WQs depending on their priority. Equal
 *	  chunks of NR_CPUS queues go to WQ6 (lowest priority), WQ2, WQ1 and
 *	  WQ0 (highest priority).
 * This ensures that Tx-confirmed buffers are timely released. In particular,
 * it avoids congestion on the Tx Confirm FQs, which can pile up PFDRs if they
 * are greatly outnumbered by other FQs in the system, while
 * dequeue scheduling is round-robin.
 */
static inline void dpaa_assign_wq(struct dpaa_fq *fq, int idx)
{
	switch (fq->fq_type) {
	case FQ_TYPE_TX_CONFIRM:
	case FQ_TYPE_TX_CONF_MQ:
		fq->wq = 1;
		break;
	case FQ_TYPE_RX_ERROR:
	case FQ_TYPE_TX_ERROR:
		fq->wq = 5;
		break;
	case FQ_TYPE_RX_DEFAULT:
	case FQ_TYPE_RX_PCD:
		fq->wq = 6;
		break;
	case FQ_TYPE_TX:
		switch (idx / DPAA_TC_TXQ_NUM) {
		case 0:
			/* Low priority (best effort) */
			fq->wq = 6;
			break;
		case 1:
			/* Medium priority */
			fq->wq = 2;
			break;
		case 2:
			/* High priority */
			fq->wq = 1;
			break;
		case 3:
			/* Very high priority */
			fq->wq = 0;
			break;
		default:
			WARN(1, "Too many TX FQs: more than %d!\n",
			     DPAA_ETH_TXQ_NUM);
		}
		break;
	default:
		WARN(1, "Invalid FQ type %d for FQID %d!\n",
		     fq->fq_type, fq->fqid);
	}
}

static struct dpaa_fq *dpaa_fq_alloc(struct device *dev,
				     u32 start, u32 count,
				     struct list_head *list,
				     enum dpaa_fq_type fq_type)
{
	struct dpaa_fq *dpaa_fq;
	int i;

	dpaa_fq = devm_kcalloc(dev, count, sizeof(*dpaa_fq),
			       GFP_KERNEL);
	if (!dpaa_fq)
		return NULL;

	for (i = 0; i < count; i++) {
		dpaa_fq[i].fq_type = fq_type;
		dpaa_fq[i].fqid = start ? start + i : 0;
		list_add_tail(&dpaa_fq[i].list, list);
	}

	for (i = 0; i < count; i++)
		dpaa_assign_wq(dpaa_fq + i, i);

	return dpaa_fq;
}

static int dpaa_alloc_all_fqs(struct device *dev, struct list_head *list,
			      struct fm_port_fqs *port_fqs)
{
	struct dpaa_fq *dpaa_fq;
	u32 fq_base, fq_base_aligned, i;

	dpaa_fq = dpaa_fq_alloc(dev, 0, 1, list, FQ_TYPE_RX_ERROR);
	if (!dpaa_fq)
		goto fq_alloc_failed;

	port_fqs->rx_errq = &dpaa_fq[0];

	dpaa_fq = dpaa_fq_alloc(dev, 0, 1, list, FQ_TYPE_RX_DEFAULT);
	if (!dpaa_fq)
		goto fq_alloc_failed;

	port_fqs->rx_defq = &dpaa_fq[0];

	/* the PCD FQIDs range needs to be aligned for correct operation */
	if (qman_alloc_fqid_range(&fq_base, 2 * DPAA_ETH_PCD_RXQ_NUM))
		goto fq_alloc_failed;

	fq_base_aligned = ALIGN(fq_base, DPAA_ETH_PCD_RXQ_NUM);

	for (i = fq_base; i < fq_base_aligned; i++)
		qman_release_fqid(i);

	for (i = fq_base_aligned + DPAA_ETH_PCD_RXQ_NUM;
	     i < (fq_base + 2 * DPAA_ETH_PCD_RXQ_NUM); i++)
		qman_release_fqid(i);

	dpaa_fq = dpaa_fq_alloc(dev, fq_base_aligned, DPAA_ETH_PCD_RXQ_NUM,
				list, FQ_TYPE_RX_PCD);
	if (!dpaa_fq)
		goto fq_alloc_failed;

	port_fqs->rx_pcdq = &dpaa_fq[0];

	if (!dpaa_fq_alloc(dev, 0, DPAA_ETH_TXQ_NUM, list, FQ_TYPE_TX_CONF_MQ))
		goto fq_alloc_failed;

	dpaa_fq = dpaa_fq_alloc(dev, 0, 1, list, FQ_TYPE_TX_ERROR);
	if (!dpaa_fq)
		goto fq_alloc_failed;

	port_fqs->tx_errq = &dpaa_fq[0];

	dpaa_fq = dpaa_fq_alloc(dev, 0, 1, list, FQ_TYPE_TX_CONFIRM);
	if (!dpaa_fq)
		goto fq_alloc_failed;

	port_fqs->tx_defq = &dpaa_fq[0];

	if (!dpaa_fq_alloc(dev, 0, DPAA_ETH_TXQ_NUM, list, FQ_TYPE_TX))
		goto fq_alloc_failed;

	return 0;

fq_alloc_failed:
	dev_err(dev, "dpaa_fq_alloc() failed\n");
	return -ENOMEM;
}

static u32 rx_pool_channel;
static DEFINE_SPINLOCK(rx_pool_channel_init);

static int dpaa_get_channel(void)
{
	spin_lock(&rx_pool_channel_init);
	if (!rx_pool_channel) {
		u32 pool;
		int ret;

		ret = qman_alloc_pool(&pool);

		if (!ret)
			rx_pool_channel = pool;
	}
	spin_unlock(&rx_pool_channel_init);
	if (!rx_pool_channel)
		return -ENOMEM;
	return rx_pool_channel;
}

static void dpaa_release_channel(void)
{
	qman_release_pool(rx_pool_channel);
}

static void dpaa_eth_add_channel(u16 channel, struct device *dev)
{
	u32 pool = QM_SDQCR_CHANNELS_POOL_CONV(channel);
	const cpumask_t *cpus = qman_affine_cpus();
	struct qman_portal *portal;
	int cpu;

	for_each_cpu_and(cpu, cpus, cpu_online_mask) {
		portal = qman_get_affine_portal(cpu);
		qman_p_static_dequeue_add(portal, pool);
		qman_start_using_portal(portal, dev);
	}
}

/* Congestion group state change notification callback.
 * Stops the device's egress queues while they are congested and
 * wakes them upon exiting congested state.
 * Also updates some CGR-related stats.
 */
static void dpaa_eth_cgscn(struct qman_portal *qm, struct qman_cgr *cgr,
			   int congested)
{
	struct dpaa_priv *priv = (struct dpaa_priv *)container_of(cgr,
		struct dpaa_priv, cgr_data.cgr);

	if (congested) {
		priv->cgr_data.congestion_start_jiffies = jiffies;
		netif_tx_stop_all_queues(priv->net_dev);
		priv->cgr_data.cgr_congested_count++;
	} else {
		priv->cgr_data.congested_jiffies +=
			(jiffies - priv->cgr_data.congestion_start_jiffies);
		netif_tx_wake_all_queues(priv->net_dev);
	}
}

static int dpaa_eth_cgr_init(struct dpaa_priv *priv)
{
	struct qm_mcc_initcgr initcgr;
	u32 cs_th;
	int err;

	err = qman_alloc_cgrid(&priv->cgr_data.cgr.cgrid);
	if (err < 0) {
		if (netif_msg_drv(priv))
			pr_err("%s: Error %d allocating CGR ID\n",
			       __func__, err);
		goto out_error;
	}
	priv->cgr_data.cgr.cb = dpaa_eth_cgscn;

	/* Enable Congestion State Change Notifications and CS taildrop */
	memset(&initcgr, 0, sizeof(initcgr));
	initcgr.we_mask = cpu_to_be16(QM_CGR_WE_CSCN_EN | QM_CGR_WE_CS_THRES);
	initcgr.cgr.cscn_en = QM_CGR_EN;

	/* Set different thresholds based on the configured MAC speed.
	 * This may turn suboptimal if the MAC is reconfigured at another
	 * speed, so MACs must call dpaa_eth_cgr_set_speed in their adjust_link
	 * callback.
	 */
	if (priv->mac_dev->if_support & SUPPORTED_10000baseT_Full)
		cs_th = DPAA_CS_THRESHOLD_10G;
	else
		cs_th = DPAA_CS_THRESHOLD_1G;
	qm_cgr_cs_thres_set64(&initcgr.cgr.cs_thres, cs_th, 1);

	initcgr.we_mask |= cpu_to_be16(QM_CGR_WE_CSTD_EN);
	initcgr.cgr.cstd_en = QM_CGR_EN;

	err = qman_create_cgr(&priv->cgr_data.cgr, QMAN_CGR_FLAG_USE_INIT,
			      &initcgr);
	if (err < 0) {
		if (netif_msg_drv(priv))
			pr_err("%s: Error %d creating CGR with ID %d\n",
			       __func__, err, priv->cgr_data.cgr.cgrid);
		qman_release_cgrid(priv->cgr_data.cgr.cgrid);
		goto out_error;
	}
	if (netif_msg_drv(priv))
		pr_debug("Created CGR %d for netdev with hwaddr %pM on QMan channel %d\n",
			 priv->cgr_data.cgr.cgrid, priv->mac_dev->addr,
			 priv->cgr_data.cgr.chan);

out_error:
	return err;
}

static void dpaa_eth_cgr_set_speed(struct mac_device *mac_dev, int speed)
{
	struct net_device *net_dev = mac_dev->net_dev;
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct qm_mcc_initcgr opts = { };
	u32 cs_th;
	int err;

	opts.we_mask = cpu_to_be16(QM_CGR_WE_CS_THRES);
	switch (speed) {
	case SPEED_10000:
		cs_th = DPAA_CS_THRESHOLD_10G;
		break;
	case SPEED_1000:
	default:
		cs_th = DPAA_CS_THRESHOLD_1G;
		break;
	}
	qm_cgr_cs_thres_set64(&opts.cgr.cs_thres, cs_th, 1);

	err = qman_update_cgr_safe(&priv->cgr_data.cgr, &opts);
	if (err)
		netdev_err(net_dev, "could not update speed: %d\n", err);
}

static inline void dpaa_setup_ingress(const struct dpaa_priv *priv,
				      struct dpaa_fq *fq,
				      const struct qman_fq *template)
{
	fq->fq_base = *template;
	fq->net_dev = priv->net_dev;

	fq->flags = QMAN_FQ_FLAG_NO_ENQUEUE;
	fq->channel = priv->channel;
}

static inline void dpaa_setup_egress(const struct dpaa_priv *priv,
				     struct dpaa_fq *fq,
				     struct fman_port *port,
				     const struct qman_fq *template)
{
	fq->fq_base = *template;
	fq->net_dev = priv->net_dev;

	if (port) {
		fq->flags = QMAN_FQ_FLAG_TO_DCPORTAL;
		fq->channel = (u16)fman_port_get_qman_channel_id(port);
	} else {
		fq->flags = QMAN_FQ_FLAG_NO_MODIFY;
	}
}

static void dpaa_fq_setup(struct dpaa_priv *priv,
			  const struct dpaa_fq_cbs *fq_cbs,
			  struct fman_port *tx_port)
{
	int egress_cnt = 0, conf_cnt = 0, num_portals = 0, portal_cnt = 0, cpu;
	const cpumask_t *affine_cpus = qman_affine_cpus();
	u16 channels[NR_CPUS];
	struct dpaa_fq *fq;

	for_each_cpu_and(cpu, affine_cpus, cpu_online_mask)
		channels[num_portals++] = qman_affine_channel(cpu);

	if (num_portals == 0)
		dev_err(priv->net_dev->dev.parent,
			"No Qman software (affine) channels found\n");

	/* Initialize each FQ in the list */
	list_for_each_entry(fq, &priv->dpaa_fq_list, list) {
		switch (fq->fq_type) {
		case FQ_TYPE_RX_DEFAULT:
			dpaa_setup_ingress(priv, fq, &fq_cbs->rx_defq);
			break;
		case FQ_TYPE_RX_ERROR:
			dpaa_setup_ingress(priv, fq, &fq_cbs->rx_errq);
			break;
		case FQ_TYPE_RX_PCD:
			if (!num_portals)
				continue;
			dpaa_setup_ingress(priv, fq, &fq_cbs->rx_defq);
			fq->channel = channels[portal_cnt++ % num_portals];
			break;
		case FQ_TYPE_TX:
			dpaa_setup_egress(priv, fq, tx_port,
					  &fq_cbs->egress_ern);
			/* If we have more Tx queues than the number of cores,
			 * just ignore the extra ones.
			 */
			if (egress_cnt < DPAA_ETH_TXQ_NUM)
				priv->egress_fqs[egress_cnt++] = &fq->fq_base;
			break;
		case FQ_TYPE_TX_CONF_MQ:
			priv->conf_fqs[conf_cnt++] = &fq->fq_base;
			fallthrough;
		case FQ_TYPE_TX_CONFIRM:
			dpaa_setup_ingress(priv, fq, &fq_cbs->tx_defq);
			break;
		case FQ_TYPE_TX_ERROR:
			dpaa_setup_ingress(priv, fq, &fq_cbs->tx_errq);
			break;
		default:
			dev_warn(priv->net_dev->dev.parent,
				 "Unknown FQ type detected!\n");
			break;
		}
	}

	 /* Make sure all CPUs receive a corresponding Tx queue. */
	while (egress_cnt < DPAA_ETH_TXQ_NUM) {
		list_for_each_entry(fq, &priv->dpaa_fq_list, list) {
			if (fq->fq_type != FQ_TYPE_TX)
				continue;
			priv->egress_fqs[egress_cnt++] = &fq->fq_base;
			if (egress_cnt == DPAA_ETH_TXQ_NUM)
				break;
		}
	}
}

static inline int dpaa_tx_fq_to_id(const struct dpaa_priv *priv,
				   struct qman_fq *tx_fq)
{
	int i;

	for (i = 0; i < DPAA_ETH_TXQ_NUM; i++)
		if (priv->egress_fqs[i] == tx_fq)
			return i;

	return -EINVAL;
}

static int dpaa_fq_init(struct dpaa_fq *dpaa_fq, bool td_enable)
{
	const struct dpaa_priv	*priv;
	struct qman_fq *confq = NULL;
	struct qm_mcc_initfq initfq;
	struct device *dev;
	struct qman_fq *fq;
	int queue_id;
	int err;

	priv = netdev_priv(dpaa_fq->net_dev);
	dev = dpaa_fq->net_dev->dev.parent;

	if (dpaa_fq->fqid == 0)
		dpaa_fq->flags |= QMAN_FQ_FLAG_DYNAMIC_FQID;

	dpaa_fq->init = !(dpaa_fq->flags & QMAN_FQ_FLAG_NO_MODIFY);

	err = qman_create_fq(dpaa_fq->fqid, dpaa_fq->flags, &dpaa_fq->fq_base);
	if (err) {
		dev_err(dev, "qman_create_fq() failed\n");
		return err;
	}
	fq = &dpaa_fq->fq_base;

	if (dpaa_fq->init) {
		memset(&initfq, 0, sizeof(initfq));

		initfq.we_mask = cpu_to_be16(QM_INITFQ_WE_FQCTRL);
		/* Note: we may get to keep an empty FQ in cache */
		initfq.fqd.fq_ctrl = cpu_to_be16(QM_FQCTRL_PREFERINCACHE);

		/* Try to reduce the number of portal interrupts for
		 * Tx Confirmation FQs.
		 */
		if (dpaa_fq->fq_type == FQ_TYPE_TX_CONFIRM)
			initfq.fqd.fq_ctrl |= cpu_to_be16(QM_FQCTRL_AVOIDBLOCK);

		/* FQ placement */
		initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_DESTWQ);

		qm_fqd_set_destwq(&initfq.fqd, dpaa_fq->channel, dpaa_fq->wq);

		/* Put all egress queues in a congestion group of their own.
		 * Sensu stricto, the Tx confirmation queues are Rx FQs,
		 * rather than Tx - but they nonetheless account for the
		 * memory footprint on behalf of egress traffic. We therefore
		 * place them in the netdev's CGR, along with the Tx FQs.
		 */
		if (dpaa_fq->fq_type == FQ_TYPE_TX ||
		    dpaa_fq->fq_type == FQ_TYPE_TX_CONFIRM ||
		    dpaa_fq->fq_type == FQ_TYPE_TX_CONF_MQ) {
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_CGID);
			initfq.fqd.fq_ctrl |= cpu_to_be16(QM_FQCTRL_CGE);
			initfq.fqd.cgid = (u8)priv->cgr_data.cgr.cgrid;
			/* Set a fixed overhead accounting, in an attempt to
			 * reduce the impact of fixed-size skb shells and the
			 * driver's needed headroom on system memory. This is
			 * especially the case when the egress traffic is
			 * composed of small datagrams.
			 * Unfortunately, QMan's OAL value is capped to an
			 * insufficient value, but even that is better than
			 * no overhead accounting at all.
			 */
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_OAC);
			qm_fqd_set_oac(&initfq.fqd, QM_OAC_CG);
			qm_fqd_set_oal(&initfq.fqd,
				       min(sizeof(struct sk_buff) +
				       priv->tx_headroom,
				       (size_t)FSL_QMAN_MAX_OAL));
		}

		if (td_enable) {
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_TDTHRESH);
			qm_fqd_set_taildrop(&initfq.fqd, DPAA_FQ_TD, 1);
			initfq.fqd.fq_ctrl = cpu_to_be16(QM_FQCTRL_TDE);
		}

		if (dpaa_fq->fq_type == FQ_TYPE_TX) {
			queue_id = dpaa_tx_fq_to_id(priv, &dpaa_fq->fq_base);
			if (queue_id >= 0)
				confq = priv->conf_fqs[queue_id];
			if (confq) {
				initfq.we_mask |=
					cpu_to_be16(QM_INITFQ_WE_CONTEXTA);
			/* ContextA: OVOM=1(use contextA2 bits instead of ICAD)
			 *	     A2V=1 (contextA A2 field is valid)
			 *	     A0V=1 (contextA A0 field is valid)
			 *	     B0V=1 (contextB field is valid)
			 * ContextA A2: EBD=1 (deallocate buffers inside FMan)
			 * ContextB B0(ASPID): 0 (absolute Virtual Storage ID)
			 */
				qm_fqd_context_a_set64(&initfq.fqd,
						       0x1e00000080000000ULL);
			}
		}

		/* Put all the ingress queues in our "ingress CGR". */
		if (priv->use_ingress_cgr &&
		    (dpaa_fq->fq_type == FQ_TYPE_RX_DEFAULT ||
		     dpaa_fq->fq_type == FQ_TYPE_RX_ERROR ||
		     dpaa_fq->fq_type == FQ_TYPE_RX_PCD)) {
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_CGID);
			initfq.fqd.fq_ctrl |= cpu_to_be16(QM_FQCTRL_CGE);
			initfq.fqd.cgid = (u8)priv->ingress_cgr.cgrid;
			/* Set a fixed overhead accounting, just like for the
			 * egress CGR.
			 */
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_OAC);
			qm_fqd_set_oac(&initfq.fqd, QM_OAC_CG);
			qm_fqd_set_oal(&initfq.fqd,
				       min(sizeof(struct sk_buff) +
				       priv->tx_headroom,
				       (size_t)FSL_QMAN_MAX_OAL));
		}

		/* Initialization common to all ingress queues */
		if (dpaa_fq->flags & QMAN_FQ_FLAG_NO_ENQUEUE) {
			initfq.we_mask |= cpu_to_be16(QM_INITFQ_WE_CONTEXTA);
			initfq.fqd.fq_ctrl |= cpu_to_be16(QM_FQCTRL_HOLDACTIVE |
						QM_FQCTRL_CTXASTASHING);
			initfq.fqd.context_a.stashing.exclusive =
				QM_STASHING_EXCL_DATA | QM_STASHING_EXCL_CTX |
				QM_STASHING_EXCL_ANNOTATION;
			qm_fqd_set_stashing(&initfq.fqd, 1, 2,
					    DIV_ROUND_UP(sizeof(struct qman_fq),
							 64));
		}

		err = qman_init_fq(fq, QMAN_INITFQ_FLAG_SCHED, &initfq);
		if (err < 0) {
			dev_err(dev, "qman_init_fq(%u) = %d\n",
				qman_fq_fqid(fq), err);
			qman_destroy_fq(fq);
			return err;
		}
	}

	dpaa_fq->fqid = qman_fq_fqid(fq);

	if (dpaa_fq->fq_type == FQ_TYPE_RX_DEFAULT ||
	    dpaa_fq->fq_type == FQ_TYPE_RX_PCD) {
		err = xdp_rxq_info_reg(&dpaa_fq->xdp_rxq, dpaa_fq->net_dev,
				       dpaa_fq->fqid, 0);
		if (err) {
			dev_err(dev, "xdp_rxq_info_reg() = %d\n", err);
			return err;
		}

		err = xdp_rxq_info_reg_mem_model(&dpaa_fq->xdp_rxq,
						 MEM_TYPE_PAGE_ORDER0, NULL);
		if (err) {
			dev_err(dev, "xdp_rxq_info_reg_mem_model() = %d\n",
				err);
			xdp_rxq_info_unreg(&dpaa_fq->xdp_rxq);
			return err;
		}
	}

	return 0;
}

static int dpaa_fq_free_entry(struct device *dev, struct qman_fq *fq)
{
	const struct dpaa_priv  *priv;
	struct dpaa_fq *dpaa_fq;
	int err, error;

	err = 0;

	dpaa_fq = container_of(fq, struct dpaa_fq, fq_base);
	priv = netdev_priv(dpaa_fq->net_dev);

	if (dpaa_fq->init) {
		err = qman_retire_fq(fq, NULL);
		if (err < 0 && netif_msg_drv(priv))
			dev_err(dev, "qman_retire_fq(%u) = %d\n",
				qman_fq_fqid(fq), err);

		error = qman_oos_fq(fq);
		if (error < 0 && netif_msg_drv(priv)) {
			dev_err(dev, "qman_oos_fq(%u) = %d\n",
				qman_fq_fqid(fq), error);
			if (err >= 0)
				err = error;
		}
	}

	if ((dpaa_fq->fq_type == FQ_TYPE_RX_DEFAULT ||
	     dpaa_fq->fq_type == FQ_TYPE_RX_PCD) &&
	    xdp_rxq_info_is_reg(&dpaa_fq->xdp_rxq))
		xdp_rxq_info_unreg(&dpaa_fq->xdp_rxq);

	qman_destroy_fq(fq);
	list_del(&dpaa_fq->list);

	return err;
}

static int dpaa_fq_free(struct device *dev, struct list_head *list)
{
	struct dpaa_fq *dpaa_fq, *tmp;
	int err, error;

	err = 0;
	list_for_each_entry_safe(dpaa_fq, tmp, list, list) {
		error = dpaa_fq_free_entry(dev, (struct qman_fq *)dpaa_fq);
		if (error < 0 && err >= 0)
			err = error;
	}

	return err;
}

static int dpaa_eth_init_tx_port(struct fman_port *port, struct dpaa_fq *errq,
				 struct dpaa_fq *defq,
				 struct dpaa_buffer_layout *buf_layout)
{
	struct fman_buffer_prefix_content buf_prefix_content;
	struct fman_port_params params;
	int err;

	memset(&params, 0, sizeof(params));
	memset(&buf_prefix_content, 0, sizeof(buf_prefix_content));

	buf_prefix_content.priv_data_size = buf_layout->priv_data_size;
	buf_prefix_content.pass_prs_result = true;
	buf_prefix_content.pass_hash_result = true;
	buf_prefix_content.pass_time_stamp = true;
	buf_prefix_content.data_align = DPAA_FD_DATA_ALIGNMENT;

	params.specific_params.non_rx_params.err_fqid = errq->fqid;
	params.specific_params.non_rx_params.dflt_fqid = defq->fqid;

	err = fman_port_config(port, &params);
	if (err) {
		pr_err("%s: fman_port_config failed\n", __func__);
		return err;
	}

	err = fman_port_cfg_buf_prefix_content(port, &buf_prefix_content);
	if (err) {
		pr_err("%s: fman_port_cfg_buf_prefix_content failed\n",
		       __func__);
		return err;
	}

	err = fman_port_init(port);
	if (err)
		pr_err("%s: fm_port_init failed\n", __func__);

	return err;
}

static int dpaa_eth_init_rx_port(struct fman_port *port, struct dpaa_bp *bp,
				 struct dpaa_fq *errq,
				 struct dpaa_fq *defq, struct dpaa_fq *pcdq,
				 struct dpaa_buffer_layout *buf_layout)
{
	struct fman_buffer_prefix_content buf_prefix_content;
	struct fman_port_rx_params *rx_p;
	struct fman_port_params params;
	int err;

	memset(&params, 0, sizeof(params));
	memset(&buf_prefix_content, 0, sizeof(buf_prefix_content));

	buf_prefix_content.priv_data_size = buf_layout->priv_data_size;
	buf_prefix_content.pass_prs_result = true;
	buf_prefix_content.pass_hash_result = true;
	buf_prefix_content.pass_time_stamp = true;
	buf_prefix_content.data_align = DPAA_FD_RX_DATA_ALIGNMENT;

	rx_p = &params.specific_params.rx_params;
	rx_p->err_fqid = errq->fqid;
	rx_p->dflt_fqid = defq->fqid;
	if (pcdq) {
		rx_p->pcd_base_fqid = pcdq->fqid;
		rx_p->pcd_fqs_count = DPAA_ETH_PCD_RXQ_NUM;
	}

	rx_p->ext_buf_pools.num_of_pools_used = 1;
	rx_p->ext_buf_pools.ext_buf_pool[0].id =  bp->bpid;
	rx_p->ext_buf_pools.ext_buf_pool[0].size = (u16)bp->size;

	err = fman_port_config(port, &params);
	if (err) {
		pr_err("%s: fman_port_config failed\n", __func__);
		return err;
	}

	err = fman_port_cfg_buf_prefix_content(port, &buf_prefix_content);
	if (err) {
		pr_err("%s: fman_port_cfg_buf_prefix_content failed\n",
		       __func__);
		return err;
	}

	err = fman_port_init(port);
	if (err)
		pr_err("%s: fm_port_init failed\n", __func__);

	return err;
}

static int dpaa_eth_init_ports(struct mac_device *mac_dev,
			       struct dpaa_bp *bp,
			       struct fm_port_fqs *port_fqs,
			       struct dpaa_buffer_layout *buf_layout,
			       struct device *dev)
{
	struct fman_port *rxport = mac_dev->port[RX];
	struct fman_port *txport = mac_dev->port[TX];
	int err;

	err = dpaa_eth_init_tx_port(txport, port_fqs->tx_errq,
				    port_fqs->tx_defq, &buf_layout[TX]);
	if (err)
		return err;

	err = dpaa_eth_init_rx_port(rxport, bp, port_fqs->rx_errq,
				    port_fqs->rx_defq, port_fqs->rx_pcdq,
				    &buf_layout[RX]);

	return err;
}

static int dpaa_bman_release(const struct dpaa_bp *dpaa_bp,
			     struct bm_buffer *bmb, int cnt)
{
	int err;

	err = bman_release(dpaa_bp->pool, bmb, cnt);
	/* Should never occur, address anyway to avoid leaking the buffers */
	if (WARN_ON(err) && dpaa_bp->free_buf_cb)
		while (cnt-- > 0)
			dpaa_bp->free_buf_cb(dpaa_bp, &bmb[cnt]);

	return cnt;
}

static void dpaa_release_sgt_members(struct qm_sg_entry *sgt)
{
	struct bm_buffer bmb[DPAA_BUFF_RELEASE_MAX];
	struct dpaa_bp *dpaa_bp;
	int i = 0, j;

	memset(bmb, 0, sizeof(bmb));

	do {
		dpaa_bp = dpaa_bpid2pool(sgt[i].bpid);
		if (!dpaa_bp)
			return;

		j = 0;
		do {
			WARN_ON(qm_sg_entry_is_ext(&sgt[i]));

			bm_buffer_set64(&bmb[j], qm_sg_entry_get64(&sgt[i]));

			j++; i++;
		} while (j < ARRAY_SIZE(bmb) &&
				!qm_sg_entry_is_final(&sgt[i - 1]) &&
				sgt[i - 1].bpid == sgt[i].bpid);

		dpaa_bman_release(dpaa_bp, bmb, j);
	} while (!qm_sg_entry_is_final(&sgt[i - 1]));
}

static void dpaa_fd_release(const struct net_device *net_dev,
			    const struct qm_fd *fd)
{
	struct qm_sg_entry *sgt;
	struct dpaa_bp *dpaa_bp;
	struct bm_buffer bmb;
	dma_addr_t addr;
	void *vaddr;

	bmb.data = 0;
	bm_buffer_set64(&bmb, qm_fd_addr(fd));

	dpaa_bp = dpaa_bpid2pool(fd->bpid);
	if (!dpaa_bp)
		return;

	if (qm_fd_get_format(fd) == qm_fd_sg) {
		vaddr = phys_to_virt(qm_fd_addr(fd));
		sgt = vaddr + qm_fd_get_offset(fd);

		dma_unmap_page(dpaa_bp->priv->rx_dma_dev, qm_fd_addr(fd),
			       DPAA_BP_RAW_SIZE, DMA_FROM_DEVICE);

		dpaa_release_sgt_members(sgt);

		addr = dma_map_page(dpaa_bp->priv->rx_dma_dev,
				    virt_to_page(vaddr), 0, DPAA_BP_RAW_SIZE,
				    DMA_FROM_DEVICE);
		if (dma_mapping_error(dpaa_bp->priv->rx_dma_dev, addr)) {
			netdev_err(net_dev, "DMA mapping failed\n");
			return;
		}
		bm_buffer_set64(&bmb, addr);
	}

	dpaa_bman_release(dpaa_bp, &bmb, 1);
}

static void count_ern(struct dpaa_percpu_priv *percpu_priv,
		      const union qm_mr_entry *msg)
{
	switch (msg->ern.rc & QM_MR_RC_MASK) {
	case QM_MR_RC_CGR_TAILDROP:
		percpu_priv->ern_cnt.cg_tdrop++;
		break;
	case QM_MR_RC_WRED:
		percpu_priv->ern_cnt.wred++;
		break;
	case QM_MR_RC_ERROR:
		percpu_priv->ern_cnt.err_cond++;
		break;
	case QM_MR_RC_ORPWINDOW_EARLY:
		percpu_priv->ern_cnt.early_window++;
		break;
	case QM_MR_RC_ORPWINDOW_LATE:
		percpu_priv->ern_cnt.late_window++;
		break;
	case QM_MR_RC_FQ_TAILDROP:
		percpu_priv->ern_cnt.fq_tdrop++;
		break;
	case QM_MR_RC_ORPWINDOW_RETIRED:
		percpu_priv->ern_cnt.fq_retired++;
		break;
	case QM_MR_RC_ORP_ZERO:
		percpu_priv->ern_cnt.orp_zero++;
		break;
	}
}

/* Turn on HW checksum computation for this outgoing frame.
 * If the current protocol is not something we support in this regard
 * (or if the stack has already computed the SW checksum), we do nothing.
 *
 * Returns 0 if all goes well (or HW csum doesn't apply), and a negative value
 * otherwise.
 *
 * Note that this function may modify the fd->cmd field and the skb data buffer
 * (the Parse Results area).
 */
static int dpaa_enable_tx_csum(struct dpaa_priv *priv,
			       struct sk_buff *skb,
			       struct qm_fd *fd,
			       void *parse_results)
{
	struct fman_prs_result *parse_result;
	u16 ethertype = ntohs(skb->protocol);
	struct ipv6hdr *ipv6h = NULL;
	struct iphdr *iph;
	int retval = 0;
	u8 l4_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	/* Note: L3 csum seems to be already computed in sw, but we can't choose
	 * L4 alone from the FM configuration anyway.
	 */

	/* Fill in some fields of the Parse Results array, so the FMan
	 * can find them as if they came from the FMan Parser.
	 */
	parse_result = (struct fman_prs_result *)parse_results;

	/* If we're dealing with VLAN, get the real Ethernet type */
	if (ethertype == ETH_P_8021Q) {
		/* We can't always assume the MAC header is set correctly
		 * by the stack, so reset to beginning of skb->data
		 */
		skb_reset_mac_header(skb);
		ethertype = ntohs(vlan_eth_hdr(skb)->h_vlan_encapsulated_proto);
	}

	/* Fill in the relevant L3 parse result fields
	 * and read the L4 protocol type
	 */
	switch (ethertype) {
	case ETH_P_IP:
		parse_result->l3r = cpu_to_be16(FM_L3_PARSE_RESULT_IPV4);
		iph = ip_hdr(skb);
		WARN_ON(!iph);
		l4_proto = iph->protocol;
		break;
	case ETH_P_IPV6:
		parse_result->l3r = cpu_to_be16(FM_L3_PARSE_RESULT_IPV6);
		ipv6h = ipv6_hdr(skb);
		WARN_ON(!ipv6h);
		l4_proto = ipv6h->nexthdr;
		break;
	default:
		/* We shouldn't even be here */
		if (net_ratelimit())
			netif_alert(priv, tx_err, priv->net_dev,
				    "Can't compute HW csum for L3 proto 0x%x\n",
				    ntohs(skb->protocol));
		retval = -EIO;
		goto return_error;
	}

	/* Fill in the relevant L4 parse result fields */
	switch (l4_proto) {
	case IPPROTO_UDP:
		parse_result->l4r = FM_L4_PARSE_RESULT_UDP;
		break;
	case IPPROTO_TCP:
		parse_result->l4r = FM_L4_PARSE_RESULT_TCP;
		break;
	default:
		if (net_ratelimit())
			netif_alert(priv, tx_err, priv->net_dev,
				    "Can't compute HW csum for L4 proto 0x%x\n",
				    l4_proto);
		retval = -EIO;
		goto return_error;
	}

	/* At index 0 is IPOffset_1 as defined in the Parse Results */
	parse_result->ip_off[0] = (u8)skb_network_offset(skb);
	parse_result->l4_off = (u8)skb_transport_offset(skb);

	/* Enable L3 (and L4, if TCP or UDP) HW checksum. */
	fd->cmd |= cpu_to_be32(FM_FD_CMD_RPD | FM_FD_CMD_DTC);

	/* On P1023 and similar platforms fd->cmd interpretation could
	 * be disabled by setting CONTEXT_A bit ICMD; currently this bit
	 * is not set so we do not need to check; in the future, if/when
	 * using context_a we need to check this bit
	 */

return_error:
	return retval;
}

static int dpaa_bp_add_8_bufs(const struct dpaa_bp *dpaa_bp)
{
	struct net_device *net_dev = dpaa_bp->priv->net_dev;
	struct bm_buffer bmb[8];
	dma_addr_t addr;
	struct page *p;
	u8 i;

	for (i = 0; i < 8; i++) {
		p = dev_alloc_pages(0);
		if (unlikely(!p)) {
			netdev_err(net_dev, "dev_alloc_pages() failed\n");
			goto release_previous_buffs;
		}

		addr = dma_map_page(dpaa_bp->priv->rx_dma_dev, p, 0,
				    DPAA_BP_RAW_SIZE, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dpaa_bp->priv->rx_dma_dev,
					       addr))) {
			netdev_err(net_dev, "DMA map failed\n");
			goto release_previous_buffs;
		}

		bmb[i].data = 0;
		bm_buffer_set64(&bmb[i], addr);
	}

release_bufs:
	return dpaa_bman_release(dpaa_bp, bmb, i);

release_previous_buffs:
	WARN_ONCE(1, "dpaa_eth: failed to add buffers on Rx\n");

	bm_buffer_set64(&bmb[i], 0);
	/* Avoid releasing a completely null buffer; bman_release() requires
	 * at least one buffer.
	 */
	if (likely(i))
		goto release_bufs;

	return 0;
}

static int dpaa_bp_seed(struct dpaa_bp *dpaa_bp)
{
	int i;

	/* Give each CPU an allotment of "config_count" buffers */
	for_each_possible_cpu(i) {
		int *count_ptr = per_cpu_ptr(dpaa_bp->percpu_count, i);
		int j;

		/* Although we access another CPU's counters here
		 * we do it at boot time so it is safe
		 */
		for (j = 0; j < dpaa_bp->config_count; j += 8)
			*count_ptr += dpaa_bp_add_8_bufs(dpaa_bp);
	}
	return 0;
}

/* Add buffers/(pages) for Rx processing whenever bpool count falls below
 * REFILL_THRESHOLD.
 */
static int dpaa_eth_refill_bpool(struct dpaa_bp *dpaa_bp, int *countptr)
{
	int count = *countptr;
	int new_bufs;

	if (unlikely(count < FSL_DPAA_ETH_REFILL_THRESHOLD)) {
		do {
			new_bufs = dpaa_bp_add_8_bufs(dpaa_bp);
			if (unlikely(!new_bufs)) {
				/* Avoid looping forever if we've temporarily
				 * run out of memory. We'll try again at the
				 * next NAPI cycle.
				 */
				break;
			}
			count += new_bufs;
		} while (count < FSL_DPAA_ETH_MAX_BUF_COUNT);

		*countptr = count;
		if (unlikely(count < FSL_DPAA_ETH_MAX_BUF_COUNT))
			return -ENOMEM;
	}

	return 0;
}

static int dpaa_eth_refill_bpools(struct dpaa_priv *priv)
{
	struct dpaa_bp *dpaa_bp;
	int *countptr;

	dpaa_bp = priv->dpaa_bp;
	if (!dpaa_bp)
		return -EINVAL;
	countptr = this_cpu_ptr(dpaa_bp->percpu_count);

	return dpaa_eth_refill_bpool(dpaa_bp, countptr);
}

/* Cleanup function for outgoing frame descriptors that were built on Tx path,
 * either contiguous frames or scatter/gather ones.
 * Skb freeing is not handled here.
 *
 * This function may be called on error paths in the Tx function, so guard
 * against cases when not all fd relevant fields were filled in. To avoid
 * reading the invalid transmission timestamp for the error paths set ts to
 * false.
 *
 * Return the skb backpointer, since for S/G frames the buffer containing it
 * gets freed here.
 *
 * No skb backpointer is set when transmitting XDP frames. Cleanup the buffer
 * and return NULL in this case.
 */
static struct sk_buff *dpaa_cleanup_tx_fd(const struct dpaa_priv *priv,
					  const struct qm_fd *fd, bool ts)
{
	const enum dma_data_direction dma_dir = DMA_TO_DEVICE;
	struct device *dev = priv->net_dev->dev.parent;
	struct skb_shared_hwtstamps shhwtstamps;
	dma_addr_t addr = qm_fd_addr(fd);
	void *vaddr = phys_to_virt(addr);
	const struct qm_sg_entry *sgt;
	struct dpaa_eth_swbp *swbp;
	struct sk_buff *skb;
	u64 ns;
	int i;

	if (unlikely(qm_fd_get_format(fd) == qm_fd_sg)) {
		dma_unmap_page(priv->tx_dma_dev, addr,
			       qm_fd_get_offset(fd) + DPAA_SGT_SIZE,
			       dma_dir);

		/* The sgt buffer has been allocated with netdev_alloc_frag(),
		 * it's from lowmem.
		 */
		sgt = vaddr + qm_fd_get_offset(fd);

		/* sgt[0] is from lowmem, was dma_map_single()-ed */
		dma_unmap_single(priv->tx_dma_dev, qm_sg_addr(&sgt[0]),
				 qm_sg_entry_get_len(&sgt[0]), dma_dir);

		/* remaining pages were mapped with skb_frag_dma_map() */
		for (i = 1; (i < DPAA_SGT_MAX_ENTRIES) &&
		     !qm_sg_entry_is_final(&sgt[i - 1]); i++) {
			WARN_ON(qm_sg_entry_is_ext(&sgt[i]));

			dma_unmap_page(priv->tx_dma_dev, qm_sg_addr(&sgt[i]),
				       qm_sg_entry_get_len(&sgt[i]), dma_dir);
		}
	} else {
		dma_unmap_single(priv->tx_dma_dev, addr,
				 qm_fd_get_offset(fd) + qm_fd_get_length(fd),
				 dma_dir);
	}

	swbp = (struct dpaa_eth_swbp *)vaddr;
	skb = swbp->skb;

	/* No skb backpointer is set when running XDP. An xdp_frame
	 * backpointer is saved instead.
	 */
	if (!skb) {
		xdp_return_frame(swbp->xdpf);
		return NULL;
	}

	/* DMA unmapping is required before accessing the HW provided info */
	if (ts && priv->tx_tstamp &&
	    skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));

		if (!fman_port_get_tstamp(priv->mac_dev->port[TX], vaddr,
					  &ns)) {
			shhwtstamps.hwtstamp = ns_to_ktime(ns);
			skb_tstamp_tx(skb, &shhwtstamps);
		} else {
			dev_warn(dev, "fman_port_get_tstamp failed!\n");
		}
	}

	if (qm_fd_get_format(fd) == qm_fd_sg)
		/* Free the page that we allocated on Tx for the SGT */
		free_pages((unsigned long)vaddr, 0);

	return skb;
}

static u8 rx_csum_offload(const struct dpaa_priv *priv, const struct qm_fd *fd)
{
	/* The parser has run and performed L4 checksum validation.
	 * We know there were no parser errors (and implicitly no
	 * L4 csum error), otherwise we wouldn't be here.
	 */
	if ((priv->net_dev->features & NETIF_F_RXCSUM) &&
	    (be32_to_cpu(fd->status) & FM_FD_STAT_L4CV))
		return CHECKSUM_UNNECESSARY;

	/* We're here because either the parser didn't run or the L4 checksum
	 * was not verified. This may include the case of a UDP frame with
	 * checksum zero or an L4 proto other than TCP/UDP
	 */
	return CHECKSUM_NONE;
}

#define PTR_IS_ALIGNED(x, a) (IS_ALIGNED((unsigned long)(x), (a)))

/* Build a linear skb around the received buffer.
 * We are guaranteed there is enough room at the end of the data buffer to
 * accommodate the shared info area of the skb.
 */
static struct sk_buff *contig_fd_to_skb(const struct dpaa_priv *priv,
					const struct qm_fd *fd)
{
	ssize_t fd_off = qm_fd_get_offset(fd);
	dma_addr_t addr = qm_fd_addr(fd);
	struct dpaa_bp *dpaa_bp;
	struct sk_buff *skb;
	void *vaddr;

	vaddr = phys_to_virt(addr);
	WARN_ON(!IS_ALIGNED((unsigned long)vaddr, SMP_CACHE_BYTES));

	dpaa_bp = dpaa_bpid2pool(fd->bpid);
	if (!dpaa_bp)
		goto free_buffer;

	skb = build_skb(vaddr, dpaa_bp->size +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (WARN_ONCE(!skb, "Build skb failure on Rx\n"))
		goto free_buffer;
	skb_reserve(skb, fd_off);
	skb_put(skb, qm_fd_get_length(fd));

	skb->ip_summed = rx_csum_offload(priv, fd);

	return skb;

free_buffer:
	free_pages((unsigned long)vaddr, 0);
	return NULL;
}

/* Build an skb with the data of the first S/G entry in the linear portion and
 * the rest of the frame as skb fragments.
 *
 * The page fragment holding the S/G Table is recycled here.
 */
static struct sk_buff *sg_fd_to_skb(const struct dpaa_priv *priv,
				    const struct qm_fd *fd)
{
	ssize_t fd_off = qm_fd_get_offset(fd);
	dma_addr_t addr = qm_fd_addr(fd);
	const struct qm_sg_entry *sgt;
	struct page *page, *head_page;
	struct dpaa_bp *dpaa_bp;
	void *vaddr, *sg_vaddr;
	int frag_off, frag_len;
	struct sk_buff *skb;
	dma_addr_t sg_addr;
	int page_offset;
	unsigned int sz;
	int *count_ptr;
	int i, j;

	vaddr = phys_to_virt(addr);
	WARN_ON(!IS_ALIGNED((unsigned long)vaddr, SMP_CACHE_BYTES));

	/* Iterate through the SGT entries and add data buffers to the skb */
	sgt = vaddr + fd_off;
	skb = NULL;
	for (i = 0; i < DPAA_SGT_MAX_ENTRIES; i++) {
		/* Extension bit is not supported */
		WARN_ON(qm_sg_entry_is_ext(&sgt[i]));

		sg_addr = qm_sg_addr(&sgt[i]);
		sg_vaddr = phys_to_virt(sg_addr);
		WARN_ON(!PTR_IS_ALIGNED(sg_vaddr, SMP_CACHE_BYTES));

		dma_unmap_page(priv->rx_dma_dev, sg_addr,
			       DPAA_BP_RAW_SIZE, DMA_FROM_DEVICE);

		/* We may use multiple Rx pools */
		dpaa_bp = dpaa_bpid2pool(sgt[i].bpid);
		if (!dpaa_bp)
			goto free_buffers;

		if (!skb) {
			sz = dpaa_bp->size +
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
			skb = build_skb(sg_vaddr, sz);
			if (WARN_ON(!skb))
				goto free_buffers;

			skb->ip_summed = rx_csum_offload(priv, fd);

			/* Make sure forwarded skbs will have enough space
			 * on Tx, if extra headers are added.
			 */
			WARN_ON(fd_off != priv->rx_headroom);
			skb_reserve(skb, fd_off);
			skb_put(skb, qm_sg_entry_get_len(&sgt[i]));
		} else {
			/* Not the first S/G entry; all data from buffer will
			 * be added in an skb fragment; fragment index is offset
			 * by one since first S/G entry was incorporated in the
			 * linear part of the skb.
			 *
			 * Caution: 'page' may be a tail page.
			 */
			page = virt_to_page(sg_vaddr);
			head_page = virt_to_head_page(sg_vaddr);

			/* Compute offset in (possibly tail) page */
			page_offset = ((unsigned long)sg_vaddr &
					(PAGE_SIZE - 1)) +
				(page_address(page) - page_address(head_page));
			/* page_offset only refers to the beginning of sgt[i];
			 * but the buffer itself may have an internal offset.
			 */
			frag_off = qm_sg_entry_get_off(&sgt[i]) + page_offset;
			frag_len = qm_sg_entry_get_len(&sgt[i]);
			/* skb_add_rx_frag() does no checking on the page; if
			 * we pass it a tail page, we'll end up with
			 * bad page accounting and eventually with segafults.
			 */
			skb_add_rx_frag(skb, i - 1, head_page, frag_off,
					frag_len, dpaa_bp->size);
		}

		/* Update the pool count for the current {cpu x bpool} */
		count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
		(*count_ptr)--;

		if (qm_sg_entry_is_final(&sgt[i]))
			break;
	}
	WARN_ONCE(i == DPAA_SGT_MAX_ENTRIES, "No final bit on SGT\n");

	/* free the SG table buffer */
	free_pages((unsigned long)vaddr, 0);

	return skb;

free_buffers:
	/* free all the SG entries */
	for (j = 0; j < DPAA_SGT_MAX_ENTRIES ; j++) {
		sg_addr = qm_sg_addr(&sgt[j]);
		sg_vaddr = phys_to_virt(sg_addr);
		/* all pages 0..i were unmaped */
		if (j > i)
			dma_unmap_page(priv->rx_dma_dev, qm_sg_addr(&sgt[j]),
				       DPAA_BP_RAW_SIZE, DMA_FROM_DEVICE);
		free_pages((unsigned long)sg_vaddr, 0);
		/* counters 0..i-1 were decremented */
		if (j >= i) {
			dpaa_bp = dpaa_bpid2pool(sgt[j].bpid);
			if (dpaa_bp) {
				count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
				(*count_ptr)--;
			}
		}

		if (qm_sg_entry_is_final(&sgt[j]))
			break;
	}
	/* free the SGT fragment */
	free_pages((unsigned long)vaddr, 0);

	return NULL;
}

static int skb_to_contig_fd(struct dpaa_priv *priv,
			    struct sk_buff *skb, struct qm_fd *fd,
			    int *offset)
{
	struct net_device *net_dev = priv->net_dev;
	enum dma_data_direction dma_dir;
	struct dpaa_eth_swbp *swbp;
	unsigned char *buff_start;
	dma_addr_t addr;
	int err;

	/* We are guaranteed to have at least tx_headroom bytes
	 * available, so just use that for offset.
	 */
	fd->bpid = FSL_DPAA_BPID_INV;
	buff_start = skb->data - priv->tx_headroom;
	dma_dir = DMA_TO_DEVICE;

	swbp = (struct dpaa_eth_swbp *)buff_start;
	swbp->skb = skb;

	/* Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpaa_enable_tx_csum(priv, skb, fd,
				  buff_start + DPAA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (net_ratelimit())
			netif_err(priv, tx_err, net_dev, "HW csum error: %d\n",
				  err);
		return err;
	}

	/* Fill in the rest of the FD fields */
	qm_fd_set_contig(fd, priv->tx_headroom, skb->len);
	fd->cmd |= cpu_to_be32(FM_FD_CMD_FCO);

	/* Map the entire buffer size that may be seen by FMan, but no more */
	addr = dma_map_single(priv->tx_dma_dev, buff_start,
			      priv->tx_headroom + skb->len, dma_dir);
	if (unlikely(dma_mapping_error(priv->tx_dma_dev, addr))) {
		if (net_ratelimit())
			netif_err(priv, tx_err, net_dev, "dma_map_single() failed\n");
		return -EINVAL;
	}
	qm_fd_addr_set64(fd, addr);

	return 0;
}

static int skb_to_sg_fd(struct dpaa_priv *priv,
			struct sk_buff *skb, struct qm_fd *fd)
{
	const enum dma_data_direction dma_dir = DMA_TO_DEVICE;
	const int nr_frags = skb_shinfo(skb)->nr_frags;
	struct net_device *net_dev = priv->net_dev;
	struct dpaa_eth_swbp *swbp;
	struct qm_sg_entry *sgt;
	void *buff_start;
	skb_frag_t *frag;
	dma_addr_t addr;
	size_t frag_len;
	struct page *p;
	int i, j, err;

	/* get a page to store the SGTable */
	p = dev_alloc_pages(0);
	if (unlikely(!p)) {
		netdev_err(net_dev, "dev_alloc_pages() failed\n");
		return -ENOMEM;
	}
	buff_start = page_address(p);

	/* Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpaa_enable_tx_csum(priv, skb, fd,
				  buff_start + DPAA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (net_ratelimit())
			netif_err(priv, tx_err, net_dev, "HW csum error: %d\n",
				  err);
		goto csum_failed;
	}

	/* SGT[0] is used by the linear part */
	sgt = (struct qm_sg_entry *)(buff_start + priv->tx_headroom);
	frag_len = skb_headlen(skb);
	qm_sg_entry_set_len(&sgt[0], frag_len);
	sgt[0].bpid = FSL_DPAA_BPID_INV;
	sgt[0].offset = 0;
	addr = dma_map_single(priv->tx_dma_dev, skb->data,
			      skb_headlen(skb), dma_dir);
	if (unlikely(dma_mapping_error(priv->tx_dma_dev, addr))) {
		netdev_err(priv->net_dev, "DMA mapping failed\n");
		err = -EINVAL;
		goto sg0_map_failed;
	}
	qm_sg_entry_set64(&sgt[0], addr);

	/* populate the rest of SGT entries */
	for (i = 0; i < nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		frag_len = skb_frag_size(frag);
		WARN_ON(!skb_frag_page(frag));
		addr = skb_frag_dma_map(priv->tx_dma_dev, frag, 0,
					frag_len, dma_dir);
		if (unlikely(dma_mapping_error(priv->tx_dma_dev, addr))) {
			netdev_err(priv->net_dev, "DMA mapping failed\n");
			err = -EINVAL;
			goto sg_map_failed;
		}

		qm_sg_entry_set_len(&sgt[i + 1], frag_len);
		sgt[i + 1].bpid = FSL_DPAA_BPID_INV;
		sgt[i + 1].offset = 0;

		/* keep the offset in the address */
		qm_sg_entry_set64(&sgt[i + 1], addr);
	}

	/* Set the final bit in the last used entry of the SGT */
	qm_sg_entry_set_f(&sgt[nr_frags], frag_len);

	/* set fd offset to priv->tx_headroom */
	qm_fd_set_sg(fd, priv->tx_headroom, skb->len);

	/* DMA map the SGT page */
	swbp = (struct dpaa_eth_swbp *)buff_start;
	swbp->skb = skb;

	addr = dma_map_page(priv->tx_dma_dev, p, 0,
			    priv->tx_headroom + DPAA_SGT_SIZE, dma_dir);
	if (unlikely(dma_mapping_error(priv->tx_dma_dev, addr))) {
		netdev_err(priv->net_dev, "DMA mapping failed\n");
		err = -EINVAL;
		goto sgt_map_failed;
	}

	fd->bpid = FSL_DPAA_BPID_INV;
	fd->cmd |= cpu_to_be32(FM_FD_CMD_FCO);
	qm_fd_addr_set64(fd, addr);

	return 0;

sgt_map_failed:
sg_map_failed:
	for (j = 0; j < i; j++)
		dma_unmap_page(priv->tx_dma_dev, qm_sg_addr(&sgt[j]),
			       qm_sg_entry_get_len(&sgt[j]), dma_dir);
sg0_map_failed:
csum_failed:
	free_pages((unsigned long)buff_start, 0);

	return err;
}

static inline int dpaa_xmit(struct dpaa_priv *priv,
			    struct rtnl_link_stats64 *percpu_stats,
			    int queue,
			    struct qm_fd *fd)
{
	struct qman_fq *egress_fq;
	int err, i;

	egress_fq = priv->egress_fqs[queue];
	if (fd->bpid == FSL_DPAA_BPID_INV)
		fd->cmd |= cpu_to_be32(qman_fq_fqid(priv->conf_fqs[queue]));

	/* Trace this Tx fd */
	trace_dpaa_tx_fd(priv->net_dev, egress_fq, fd);

	for (i = 0; i < DPAA_ENQUEUE_RETRIES; i++) {
		err = qman_enqueue(egress_fq, fd);
		if (err != -EBUSY)
			break;
	}

	if (unlikely(err < 0)) {
		percpu_stats->tx_fifo_errors++;
		return err;
	}

	percpu_stats->tx_packets++;
	percpu_stats->tx_bytes += qm_fd_get_length(fd);

	return 0;
}

#ifdef CONFIG_DPAA_ERRATUM_A050385
static int dpaa_a050385_wa_skb(struct net_device *net_dev, struct sk_buff **s)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct sk_buff *new_skb, *skb = *s;
	unsigned char *start, i;

	/* check linear buffer alignment */
	if (!PTR_IS_ALIGNED(skb->data, DPAA_A050385_ALIGN))
		goto workaround;

	/* linear buffers just need to have an aligned start */
	if (!skb_is_nonlinear(skb))
		return 0;

	/* linear data size for nonlinear skbs needs to be aligned */
	if (!IS_ALIGNED(skb_headlen(skb), DPAA_A050385_ALIGN))
		goto workaround;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		/* all fragments need to have aligned start addresses */
		if (!IS_ALIGNED(skb_frag_off(frag), DPAA_A050385_ALIGN))
			goto workaround;

		/* all but last fragment need to have aligned sizes */
		if (!IS_ALIGNED(skb_frag_size(frag), DPAA_A050385_ALIGN) &&
		    (i < skb_shinfo(skb)->nr_frags - 1))
			goto workaround;
	}

	return 0;

workaround:
	/* copy all the skb content into a new linear buffer */
	new_skb = netdev_alloc_skb(net_dev, skb->len + DPAA_A050385_ALIGN - 1 +
						priv->tx_headroom);
	if (!new_skb)
		return -ENOMEM;

	/* NET_SKB_PAD bytes already reserved, adding up to tx_headroom */
	skb_reserve(new_skb, priv->tx_headroom - NET_SKB_PAD);

	/* Workaround for DPAA_A050385 requires data start to be aligned */
	start = PTR_ALIGN(new_skb->data, DPAA_A050385_ALIGN);
	if (start - new_skb->data)
		skb_reserve(new_skb, start - new_skb->data);

	skb_put(new_skb, skb->len);
	skb_copy_bits(skb, 0, new_skb->data, skb->len);
	skb_copy_header(new_skb, skb);
	new_skb->dev = skb->dev;

	/* Copy relevant timestamp info from the old skb to the new */
	if (priv->tx_tstamp) {
		skb_shinfo(new_skb)->tx_flags = skb_shinfo(skb)->tx_flags;
		skb_shinfo(new_skb)->hwtstamps = skb_shinfo(skb)->hwtstamps;
		skb_shinfo(new_skb)->tskey = skb_shinfo(skb)->tskey;
		if (skb->sk)
			skb_set_owner_w(new_skb, skb->sk);
	}

	/* We move the headroom when we align it so we have to reset the
	 * network and transport header offsets relative to the new data
	 * pointer. The checksum offload relies on these offsets.
	 */
	skb_set_network_header(new_skb, skb_network_offset(skb));
	skb_set_transport_header(new_skb, skb_transport_offset(skb));

	dev_kfree_skb(skb);
	*s = new_skb;

	return 0;
}

static int dpaa_a050385_wa_xdpf(struct dpaa_priv *priv,
				struct xdp_frame **init_xdpf)
{
	struct xdp_frame *new_xdpf, *xdpf = *init_xdpf;
	void *new_buff, *aligned_data;
	struct page *p;
	u32 data_shift;
	int headroom;

	/* Check the data alignment and make sure the headroom is large
	 * enough to store the xdpf backpointer. Use an aligned headroom
	 * value.
	 *
	 * Due to alignment constraints, we give XDP access to the full 256
	 * byte frame headroom. If the XDP program uses all of it, copy the
	 * data to a new buffer and make room for storing the backpointer.
	 */
	if (PTR_IS_ALIGNED(xdpf->data, DPAA_FD_DATA_ALIGNMENT) &&
	    xdpf->headroom >= priv->tx_headroom) {
		xdpf->headroom = priv->tx_headroom;
		return 0;
	}

	/* Try to move the data inside the buffer just enough to align it and
	 * store the xdpf backpointer. If the available headroom isn't large
	 * enough, resort to allocating a new buffer and copying the data.
	 */
	aligned_data = PTR_ALIGN_DOWN(xdpf->data, DPAA_FD_DATA_ALIGNMENT);
	data_shift = xdpf->data - aligned_data;

	/* The XDP frame's headroom needs to be large enough to accommodate
	 * shifting the data as well as storing the xdpf backpointer.
	 */
	if (xdpf->headroom  >= data_shift + priv->tx_headroom) {
		memmove(aligned_data, xdpf->data, xdpf->len);
		xdpf->data = aligned_data;
		xdpf->headroom = priv->tx_headroom;
		return 0;
	}

	/* The new xdp_frame is stored in the new buffer. Reserve enough space
	 * in the headroom for storing it along with the driver's private
	 * info. The headroom needs to be aligned to DPAA_FD_DATA_ALIGNMENT to
	 * guarantee the data's alignment in the buffer.
	 */
	headroom = ALIGN(sizeof(*new_xdpf) + priv->tx_headroom,
			 DPAA_FD_DATA_ALIGNMENT);

	/* Assure the extended headroom and data don't overflow the buffer,
	 * while maintaining the mandatory tailroom.
	 */
	if (headroom + xdpf->len > DPAA_BP_RAW_SIZE -
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
		return -ENOMEM;

	p = dev_alloc_pages(0);
	if (unlikely(!p))
		return -ENOMEM;

	/* Copy the data to the new buffer at a properly aligned offset */
	new_buff = page_address(p);
	memcpy(new_buff + headroom, xdpf->data, xdpf->len);

	/* Create an XDP frame around the new buffer in a similar fashion
	 * to xdp_convert_buff_to_frame.
	 */
	new_xdpf = new_buff;
	new_xdpf->data = new_buff + headroom;
	new_xdpf->len = xdpf->len;
	new_xdpf->headroom = priv->tx_headroom;
	new_xdpf->frame_sz = DPAA_BP_RAW_SIZE;
	new_xdpf->mem.type = MEM_TYPE_PAGE_ORDER0;

	/* Release the initial buffer */
	xdp_return_frame_rx_napi(xdpf);

	*init_xdpf = new_xdpf;
	return 0;
}
#endif

static netdev_tx_t
dpaa_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	const int queue_mapping = skb_get_queue_mapping(skb);
	bool nonlinear = skb_is_nonlinear(skb);
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa_percpu_priv *percpu_priv;
	struct netdev_queue *txq;
	struct dpaa_priv *priv;
	struct qm_fd fd;
	int offset = 0;
	int err = 0;

	priv = netdev_priv(net_dev);
	percpu_priv = this_cpu_ptr(priv->percpu_priv);
	percpu_stats = &percpu_priv->stats;

	qm_fd_clear_fd(&fd);

	if (!nonlinear) {
		/* We're going to store the skb backpointer at the beginning
		 * of the data buffer, so we need a privately owned skb
		 *
		 * We've made sure skb is not shared in dev->priv_flags,
		 * we need to verify the skb head is not cloned
		 */
		if (skb_cow_head(skb, priv->tx_headroom))
			goto enomem;

		WARN_ON(skb_is_nonlinear(skb));
	}

	/* MAX_SKB_FRAGS is equal or larger than our dpaa_SGT_MAX_ENTRIES;
	 * make sure we don't feed FMan with more fragments than it supports.
	 */
	if (unlikely(nonlinear &&
		     (skb_shinfo(skb)->nr_frags >= DPAA_SGT_MAX_ENTRIES))) {
		/* If the egress skb contains more fragments than we support
		 * we have no choice but to linearize it ourselves.
		 */
		if (__skb_linearize(skb))
			goto enomem;

		nonlinear = skb_is_nonlinear(skb);
	}

#ifdef CONFIG_DPAA_ERRATUM_A050385
	if (unlikely(fman_has_errata_a050385())) {
		if (dpaa_a050385_wa_skb(net_dev, &skb))
			goto enomem;
		nonlinear = skb_is_nonlinear(skb);
	}
#endif

	if (nonlinear) {
		/* Just create a S/G fd based on the skb */
		err = skb_to_sg_fd(priv, skb, &fd);
		percpu_priv->tx_frag_skbuffs++;
	} else {
		/* Create a contig FD from this skb */
		err = skb_to_contig_fd(priv, skb, &fd, &offset);
	}
	if (unlikely(err < 0))
		goto skb_to_fd_failed;

	txq = netdev_get_tx_queue(net_dev, queue_mapping);

	/* LLTX requires to do our own update of trans_start */
	txq_trans_cond_update(txq);

	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		fd.cmd |= cpu_to_be32(FM_FD_CMD_UPD);
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}

	if (likely(dpaa_xmit(priv, percpu_stats, queue_mapping, &fd) == 0))
		return NETDEV_TX_OK;

	dpaa_cleanup_tx_fd(priv, &fd, false);
skb_to_fd_failed:
enomem:
	percpu_stats->tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void dpaa_rx_error(struct net_device *net_dev,
			  const struct dpaa_priv *priv,
			  struct dpaa_percpu_priv *percpu_priv,
			  const struct qm_fd *fd,
			  u32 fqid)
{
	if (net_ratelimit())
		netif_err(priv, hw, net_dev, "Err FD status = 0x%08x\n",
			  be32_to_cpu(fd->status) & FM_FD_STAT_RX_ERRORS);

	percpu_priv->stats.rx_errors++;

	if (be32_to_cpu(fd->status) & FM_FD_ERR_DMA)
		percpu_priv->rx_errors.dme++;
	if (be32_to_cpu(fd->status) & FM_FD_ERR_PHYSICAL)
		percpu_priv->rx_errors.fpe++;
	if (be32_to_cpu(fd->status) & FM_FD_ERR_SIZE)
		percpu_priv->rx_errors.fse++;
	if (be32_to_cpu(fd->status) & FM_FD_ERR_PRS_HDR_ERR)
		percpu_priv->rx_errors.phe++;

	dpaa_fd_release(net_dev, fd);
}

static void dpaa_tx_error(struct net_device *net_dev,
			  const struct dpaa_priv *priv,
			  struct dpaa_percpu_priv *percpu_priv,
			  const struct qm_fd *fd,
			  u32 fqid)
{
	struct sk_buff *skb;

	if (net_ratelimit())
		netif_warn(priv, hw, net_dev, "FD status = 0x%08x\n",
			   be32_to_cpu(fd->status) & FM_FD_STAT_TX_ERRORS);

	percpu_priv->stats.tx_errors++;

	skb = dpaa_cleanup_tx_fd(priv, fd, false);
	dev_kfree_skb(skb);
}

static int dpaa_eth_poll(struct napi_struct *napi, int budget)
{
	struct dpaa_napi_portal *np =
			container_of(napi, struct dpaa_napi_portal, napi);
	int cleaned;

	np->xdp_act = 0;

	cleaned = qman_p_poll_dqrr(np->p, budget);

	if (cleaned < budget) {
		napi_complete_done(napi, cleaned);
		qman_p_irqsource_add(np->p, QM_PIRQ_DQRI);
	} else if (np->down) {
		qman_p_irqsource_add(np->p, QM_PIRQ_DQRI);
	}

	if (np->xdp_act & XDP_REDIRECT)
		xdp_do_flush();

	return cleaned;
}

static void dpaa_tx_conf(struct net_device *net_dev,
			 const struct dpaa_priv *priv,
			 struct dpaa_percpu_priv *percpu_priv,
			 const struct qm_fd *fd,
			 u32 fqid)
{
	struct sk_buff	*skb;

	if (unlikely(be32_to_cpu(fd->status) & FM_FD_STAT_TX_ERRORS)) {
		if (net_ratelimit())
			netif_warn(priv, hw, net_dev, "FD status = 0x%08x\n",
				   be32_to_cpu(fd->status) &
				   FM_FD_STAT_TX_ERRORS);

		percpu_priv->stats.tx_errors++;
	}

	percpu_priv->tx_confirm++;

	skb = dpaa_cleanup_tx_fd(priv, fd, true);

	consume_skb(skb);
}

static inline int dpaa_eth_napi_schedule(struct dpaa_percpu_priv *percpu_priv,
					 struct qman_portal *portal, bool sched_napi)
{
	if (sched_napi) {
		/* Disable QMan IRQ and invoke NAPI */
		qman_p_irqsource_remove(portal, QM_PIRQ_DQRI);

		percpu_priv->np.p = portal;
		napi_schedule(&percpu_priv->np.napi);
		percpu_priv->in_interrupt++;
		return 1;
	}
	return 0;
}

static enum qman_cb_dqrr_result rx_error_dqrr(struct qman_portal *portal,
					      struct qman_fq *fq,
					      const struct qm_dqrr_entry *dq,
					      bool sched_napi)
{
	struct dpaa_fq *dpaa_fq = container_of(fq, struct dpaa_fq, fq_base);
	struct dpaa_percpu_priv *percpu_priv;
	struct net_device *net_dev;
	struct dpaa_bp *dpaa_bp;
	struct dpaa_priv *priv;

	net_dev = dpaa_fq->net_dev;
	priv = netdev_priv(net_dev);
	dpaa_bp = dpaa_bpid2pool(dq->fd.bpid);
	if (!dpaa_bp)
		return qman_cb_dqrr_consume;

	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	if (dpaa_eth_napi_schedule(percpu_priv, portal, sched_napi))
		return qman_cb_dqrr_stop;

	dpaa_eth_refill_bpools(priv);
	dpaa_rx_error(net_dev, priv, percpu_priv, &dq->fd, fq->fqid);

	return qman_cb_dqrr_consume;
}

static int dpaa_xdp_xmit_frame(struct net_device *net_dev,
			       struct xdp_frame *xdpf)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa_percpu_priv *percpu_priv;
	struct dpaa_eth_swbp *swbp;
	struct netdev_queue *txq;
	void *buff_start;
	struct qm_fd fd;
	dma_addr_t addr;
	int err;

	percpu_priv = this_cpu_ptr(priv->percpu_priv);
	percpu_stats = &percpu_priv->stats;

#ifdef CONFIG_DPAA_ERRATUM_A050385
	if (unlikely(fman_has_errata_a050385())) {
		if (dpaa_a050385_wa_xdpf(priv, &xdpf)) {
			err = -ENOMEM;
			goto out_error;
		}
	}
#endif

	if (xdpf->headroom < DPAA_TX_PRIV_DATA_SIZE) {
		err = -EINVAL;
		goto out_error;
	}

	buff_start = xdpf->data - xdpf->headroom;

	/* Leave empty the skb backpointer at the start of the buffer.
	 * Save the XDP frame for easy cleanup on confirmation.
	 */
	swbp = (struct dpaa_eth_swbp *)buff_start;
	swbp->skb = NULL;
	swbp->xdpf = xdpf;

	qm_fd_clear_fd(&fd);
	fd.bpid = FSL_DPAA_BPID_INV;
	fd.cmd |= cpu_to_be32(FM_FD_CMD_FCO);
	qm_fd_set_contig(&fd, xdpf->headroom, xdpf->len);

	addr = dma_map_single(priv->tx_dma_dev, buff_start,
			      xdpf->headroom + xdpf->len,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->tx_dma_dev, addr))) {
		err = -EINVAL;
		goto out_error;
	}

	qm_fd_addr_set64(&fd, addr);

	/* Bump the trans_start */
	txq = netdev_get_tx_queue(net_dev, smp_processor_id());
	txq_trans_cond_update(txq);

	err = dpaa_xmit(priv, percpu_stats, smp_processor_id(), &fd);
	if (err) {
		dma_unmap_single(priv->tx_dma_dev, addr,
				 qm_fd_get_offset(&fd) + qm_fd_get_length(&fd),
				 DMA_TO_DEVICE);
		goto out_error;
	}

	return 0;

out_error:
	percpu_stats->tx_errors++;
	return err;
}

static u32 dpaa_run_xdp(struct dpaa_priv *priv, struct qm_fd *fd, void *vaddr,
			struct dpaa_fq *dpaa_fq, unsigned int *xdp_meta_len)
{
	ssize_t fd_off = qm_fd_get_offset(fd);
	struct bpf_prog *xdp_prog;
	struct xdp_frame *xdpf;
	struct xdp_buff xdp;
	u32 xdp_act;
	int err;

	xdp_prog = READ_ONCE(priv->xdp_prog);
	if (!xdp_prog)
		return XDP_PASS;

	xdp_init_buff(&xdp, DPAA_BP_RAW_SIZE - DPAA_TX_PRIV_DATA_SIZE,
		      &dpaa_fq->xdp_rxq);
	xdp_prepare_buff(&xdp, vaddr + fd_off - XDP_PACKET_HEADROOM,
			 XDP_PACKET_HEADROOM, qm_fd_get_length(fd), true);

	/* We reserve a fixed headroom of 256 bytes under the erratum and we
	 * offer it all to XDP programs to use. If no room is left for the
	 * xdpf backpointer on TX, we will need to copy the data.
	 * Disable metadata support since data realignments might be required
	 * and the information can be lost.
	 */
#ifdef CONFIG_DPAA_ERRATUM_A050385
	if (unlikely(fman_has_errata_a050385())) {
		xdp_set_data_meta_invalid(&xdp);
		xdp.data_hard_start = vaddr;
		xdp.frame_sz = DPAA_BP_RAW_SIZE;
	}
#endif

	xdp_act = bpf_prog_run_xdp(xdp_prog, &xdp);

	/* Update the length and the offset of the FD */
	qm_fd_set_contig(fd, xdp.data - vaddr, xdp.data_end - xdp.data);

	switch (xdp_act) {
	case XDP_PASS:
#ifdef CONFIG_DPAA_ERRATUM_A050385
		*xdp_meta_len = xdp_data_meta_unsupported(&xdp) ? 0 :
				xdp.data - xdp.data_meta;
#else
		*xdp_meta_len = xdp.data - xdp.data_meta;
#endif
		break;
	case XDP_TX:
		/* We can access the full headroom when sending the frame
		 * back out
		 */
		xdp.data_hard_start = vaddr;
		xdp.frame_sz = DPAA_BP_RAW_SIZE;
		xdpf = xdp_convert_buff_to_frame(&xdp);
		if (unlikely(!xdpf)) {
			free_pages((unsigned long)vaddr, 0);
			break;
		}

		if (dpaa_xdp_xmit_frame(priv->net_dev, xdpf))
			xdp_return_frame_rx_napi(xdpf);

		break;
	case XDP_REDIRECT:
		/* Allow redirect to use the full headroom */
		xdp.data_hard_start = vaddr;
		xdp.frame_sz = DPAA_BP_RAW_SIZE;

		err = xdp_do_redirect(priv->net_dev, &xdp, xdp_prog);
		if (err) {
			trace_xdp_exception(priv->net_dev, xdp_prog, xdp_act);
			free_pages((unsigned long)vaddr, 0);
		}
		break;
	default:
		bpf_warn_invalid_xdp_action(priv->net_dev, xdp_prog, xdp_act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(priv->net_dev, xdp_prog, xdp_act);
		fallthrough;
	case XDP_DROP:
		/* Free the buffer */
		free_pages((unsigned long)vaddr, 0);
		break;
	}

	return xdp_act;
}

static enum qman_cb_dqrr_result rx_default_dqrr(struct qman_portal *portal,
						struct qman_fq *fq,
						const struct qm_dqrr_entry *dq,
						bool sched_napi)
{
	bool ts_valid = false, hash_valid = false;
	struct skb_shared_hwtstamps *shhwtstamps;
	unsigned int skb_len, xdp_meta_len = 0;
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa_percpu_priv *percpu_priv;
	const struct qm_fd *fd = &dq->fd;
	dma_addr_t addr = qm_fd_addr(fd);
	struct dpaa_napi_portal *np;
	enum qm_fd_format fd_format;
	struct net_device *net_dev;
	u32 fd_status, hash_offset;
	struct qm_sg_entry *sgt;
	struct dpaa_bp *dpaa_bp;
	struct dpaa_fq *dpaa_fq;
	struct dpaa_priv *priv;
	struct sk_buff *skb;
	int *count_ptr;
	u32 xdp_act;
	void *vaddr;
	u32 hash;
	u64 ns;

	dpaa_fq = container_of(fq, struct dpaa_fq, fq_base);
	fd_status = be32_to_cpu(fd->status);
	fd_format = qm_fd_get_format(fd);
	net_dev = dpaa_fq->net_dev;
	priv = netdev_priv(net_dev);
	dpaa_bp = dpaa_bpid2pool(dq->fd.bpid);
	if (!dpaa_bp)
		return qman_cb_dqrr_consume;

	/* Trace the Rx fd */
	trace_dpaa_rx_fd(net_dev, fq, &dq->fd);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);
	percpu_stats = &percpu_priv->stats;
	np = &percpu_priv->np;

	if (unlikely(dpaa_eth_napi_schedule(percpu_priv, portal, sched_napi)))
		return qman_cb_dqrr_stop;

	/* Make sure we didn't run out of buffers */
	if (unlikely(dpaa_eth_refill_bpools(priv))) {
		/* Unable to refill the buffer pool due to insufficient
		 * system memory. Just release the frame back into the pool,
		 * otherwise we'll soon end up with an empty buffer pool.
		 */
		dpaa_fd_release(net_dev, &dq->fd);
		return qman_cb_dqrr_consume;
	}

	if (unlikely(fd_status & FM_FD_STAT_RX_ERRORS) != 0) {
		if (net_ratelimit())
			netif_warn(priv, hw, net_dev, "FD status = 0x%08x\n",
				   fd_status & FM_FD_STAT_RX_ERRORS);

		percpu_stats->rx_errors++;
		dpaa_fd_release(net_dev, fd);
		return qman_cb_dqrr_consume;
	}

	dma_unmap_page(dpaa_bp->priv->rx_dma_dev, addr, DPAA_BP_RAW_SIZE,
		       DMA_FROM_DEVICE);

	/* prefetch the first 64 bytes of the frame or the SGT start */
	vaddr = phys_to_virt(addr);
	prefetch(vaddr + qm_fd_get_offset(fd));

	/* The only FD types that we may receive are contig and S/G */
	WARN_ON((fd_format != qm_fd_contig) && (fd_format != qm_fd_sg));

	/* Account for either the contig buffer or the SGT buffer (depending on
	 * which case we were in) having been removed from the pool.
	 */
	count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
	(*count_ptr)--;

	/* Extract the timestamp stored in the headroom before running XDP */
	if (priv->rx_tstamp) {
		if (!fman_port_get_tstamp(priv->mac_dev->port[RX], vaddr, &ns))
			ts_valid = true;
		else
			WARN_ONCE(1, "fman_port_get_tstamp failed!\n");
	}

	/* Extract the hash stored in the headroom before running XDP */
	if (net_dev->features & NETIF_F_RXHASH && priv->keygen_in_use &&
	    !fman_port_get_hash_result_offset(priv->mac_dev->port[RX],
					      &hash_offset)) {
		hash = be32_to_cpu(*(u32 *)(vaddr + hash_offset));
		hash_valid = true;
	}

	if (likely(fd_format == qm_fd_contig)) {
		xdp_act = dpaa_run_xdp(priv, (struct qm_fd *)fd, vaddr,
				       dpaa_fq, &xdp_meta_len);
		np->xdp_act |= xdp_act;
		if (xdp_act != XDP_PASS) {
			percpu_stats->rx_packets++;
			percpu_stats->rx_bytes += qm_fd_get_length(fd);
			return qman_cb_dqrr_consume;
		}
		skb = contig_fd_to_skb(priv, fd);
	} else {
		/* XDP doesn't support S/G frames. Return the fragments to the
		 * buffer pool and release the SGT.
		 */
		if (READ_ONCE(priv->xdp_prog)) {
			WARN_ONCE(1, "S/G frames not supported under XDP\n");
			sgt = vaddr + qm_fd_get_offset(fd);
			dpaa_release_sgt_members(sgt);
			free_pages((unsigned long)vaddr, 0);
			return qman_cb_dqrr_consume;
		}
		skb = sg_fd_to_skb(priv, fd);
	}
	if (!skb)
		return qman_cb_dqrr_consume;

	if (xdp_meta_len)
		skb_metadata_set(skb, xdp_meta_len);

	/* Set the previously extracted timestamp */
	if (ts_valid) {
		shhwtstamps = skb_hwtstamps(skb);
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(ns);
	}

	skb->protocol = eth_type_trans(skb, net_dev);

	/* Set the previously extracted hash */
	if (hash_valid) {
		enum pkt_hash_types type;

		/* if L4 exists, it was used in the hash generation */
		type = be32_to_cpu(fd->status) & FM_FD_STAT_L4CV ?
			PKT_HASH_TYPE_L4 : PKT_HASH_TYPE_L3;
		skb_set_hash(skb, hash, type);
	}

	skb_len = skb->len;

	if (unlikely(netif_receive_skb(skb) == NET_RX_DROP)) {
		percpu_stats->rx_dropped++;
		return qman_cb_dqrr_consume;
	}

	percpu_stats->rx_packets++;
	percpu_stats->rx_bytes += skb_len;

	return qman_cb_dqrr_consume;
}

static enum qman_cb_dqrr_result conf_error_dqrr(struct qman_portal *portal,
						struct qman_fq *fq,
						const struct qm_dqrr_entry *dq,
						bool sched_napi)
{
	struct dpaa_percpu_priv *percpu_priv;
	struct net_device *net_dev;
	struct dpaa_priv *priv;

	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	if (dpaa_eth_napi_schedule(percpu_priv, portal, sched_napi))
		return qman_cb_dqrr_stop;

	dpaa_tx_error(net_dev, priv, percpu_priv, &dq->fd, fq->fqid);

	return qman_cb_dqrr_consume;
}

static enum qman_cb_dqrr_result conf_dflt_dqrr(struct qman_portal *portal,
					       struct qman_fq *fq,
					       const struct qm_dqrr_entry *dq,
					       bool sched_napi)
{
	struct dpaa_percpu_priv *percpu_priv;
	struct net_device *net_dev;
	struct dpaa_priv *priv;

	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	/* Trace the fd */
	trace_dpaa_tx_conf_fd(net_dev, fq, &dq->fd);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	if (dpaa_eth_napi_schedule(percpu_priv, portal, sched_napi))
		return qman_cb_dqrr_stop;

	dpaa_tx_conf(net_dev, priv, percpu_priv, &dq->fd, fq->fqid);

	return qman_cb_dqrr_consume;
}

static void egress_ern(struct qman_portal *portal,
		       struct qman_fq *fq,
		       const union qm_mr_entry *msg)
{
	const struct qm_fd *fd = &msg->ern.fd;
	struct dpaa_percpu_priv *percpu_priv;
	const struct dpaa_priv *priv;
	struct net_device *net_dev;
	struct sk_buff *skb;

	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);
	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	percpu_priv->stats.tx_dropped++;
	percpu_priv->stats.tx_fifo_errors++;
	count_ern(percpu_priv, msg);

	skb = dpaa_cleanup_tx_fd(priv, fd, false);
	dev_kfree_skb_any(skb);
}

static const struct dpaa_fq_cbs dpaa_fq_cbs = {
	.rx_defq = { .cb = { .dqrr = rx_default_dqrr } },
	.tx_defq = { .cb = { .dqrr = conf_dflt_dqrr } },
	.rx_errq = { .cb = { .dqrr = rx_error_dqrr } },
	.tx_errq = { .cb = { .dqrr = conf_error_dqrr } },
	.egress_ern = { .cb = { .ern = egress_ern } }
};

static void dpaa_eth_napi_enable(struct dpaa_priv *priv)
{
	struct dpaa_percpu_priv *percpu_priv;
	int i;

	for_each_online_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		percpu_priv->np.down = false;
		napi_enable(&percpu_priv->np.napi);
	}
}

static void dpaa_eth_napi_disable(struct dpaa_priv *priv)
{
	struct dpaa_percpu_priv *percpu_priv;
	int i;

	for_each_online_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		percpu_priv->np.down = true;
		napi_disable(&percpu_priv->np.napi);
	}
}

static void dpaa_adjust_link(struct net_device *net_dev)
{
	struct mac_device *mac_dev;
	struct dpaa_priv *priv;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;
	mac_dev->adjust_link(mac_dev);
}

/* The Aquantia PHYs are capable of performing rate adaptation */
#define PHY_VEND_AQUANTIA	0x03a1b400
#define PHY_VEND_AQUANTIA2	0x31c31c00

static int dpaa_phy_init(struct net_device *net_dev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	struct mac_device *mac_dev;
	struct phy_device *phy_dev;
	struct dpaa_priv *priv;
	u32 phy_vendor;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	phy_dev = of_phy_connect(net_dev, mac_dev->phy_node,
				 &dpaa_adjust_link, 0,
				 mac_dev->phy_if);
	if (!phy_dev) {
		netif_err(priv, ifup, net_dev, "init_phy() failed\n");
		return -ENODEV;
	}

	phy_vendor = phy_dev->drv->phy_id & GENMASK(31, 10);
	/* Unless the PHY is capable of rate adaptation */
	if (mac_dev->phy_if != PHY_INTERFACE_MODE_XGMII ||
	    (phy_vendor != PHY_VEND_AQUANTIA &&
	     phy_vendor != PHY_VEND_AQUANTIA2)) {
		/* remove any features not supported by the controller */
		ethtool_convert_legacy_u32_to_link_mode(mask,
							mac_dev->if_support);
		linkmode_and(phy_dev->supported, phy_dev->supported, mask);
	}

	phy_support_asym_pause(phy_dev);

	mac_dev->phy_dev = phy_dev;
	net_dev->phydev = phy_dev;

	return 0;
}

static int dpaa_open(struct net_device *net_dev)
{
	struct mac_device *mac_dev;
	struct dpaa_priv *priv;
	int err, i;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;
	dpaa_eth_napi_enable(priv);

	err = dpaa_phy_init(net_dev);
	if (err)
		goto phy_init_failed;

	for (i = 0; i < ARRAY_SIZE(mac_dev->port); i++) {
		err = fman_port_enable(mac_dev->port[i]);
		if (err)
			goto mac_start_failed;
	}

	err = priv->mac_dev->enable(mac_dev->fman_mac);
	if (err < 0) {
		netif_err(priv, ifup, net_dev, "mac_dev->enable() = %d\n", err);
		goto mac_start_failed;
	}
	phy_start(priv->mac_dev->phy_dev);

	netif_tx_start_all_queues(net_dev);

	return 0;

mac_start_failed:
	for (i = 0; i < ARRAY_SIZE(mac_dev->port); i++)
		fman_port_disable(mac_dev->port[i]);

phy_init_failed:
	dpaa_eth_napi_disable(priv);

	return err;
}

static int dpaa_eth_stop(struct net_device *net_dev)
{
	struct dpaa_priv *priv;
	int err;

	err = dpaa_stop(net_dev);

	priv = netdev_priv(net_dev);
	dpaa_eth_napi_disable(priv);

	return err;
}

static bool xdp_validate_mtu(struct dpaa_priv *priv, int mtu)
{
	int max_contig_data = priv->dpaa_bp->size - priv->rx_headroom;

	/* We do not support S/G fragments when XDP is enabled.
	 * Limit the MTU in relation to the buffer size.
	 */
	if (mtu + VLAN_ETH_HLEN + ETH_FCS_LEN > max_contig_data) {
		dev_warn(priv->net_dev->dev.parent,
			 "The maximum MTU for XDP is %d\n",
			 max_contig_data - VLAN_ETH_HLEN - ETH_FCS_LEN);
		return false;
	}

	return true;
}

static int dpaa_change_mtu(struct net_device *net_dev, int new_mtu)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);

	if (priv->xdp_prog && !xdp_validate_mtu(priv, new_mtu))
		return -EINVAL;

	net_dev->mtu = new_mtu;
	return 0;
}

static int dpaa_setup_xdp(struct net_device *net_dev, struct netdev_bpf *bpf)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct bpf_prog *old_prog;
	int err;
	bool up;

	/* S/G fragments are not supported in XDP-mode */
	if (bpf->prog && !xdp_validate_mtu(priv, net_dev->mtu)) {
		NL_SET_ERR_MSG_MOD(bpf->extack, "MTU too large for XDP");
		return -EINVAL;
	}

	up = netif_running(net_dev);

	if (up)
		dpaa_eth_stop(net_dev);

	old_prog = xchg(&priv->xdp_prog, bpf->prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	if (up) {
		err = dpaa_open(net_dev);
		if (err) {
			NL_SET_ERR_MSG_MOD(bpf->extack, "dpaa_open() failed");
			return err;
		}
	}

	return 0;
}

static int dpaa_xdp(struct net_device *net_dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return dpaa_setup_xdp(net_dev, xdp);
	default:
		return -EINVAL;
	}
}

static int dpaa_xdp_xmit(struct net_device *net_dev, int n,
			 struct xdp_frame **frames, u32 flags)
{
	struct xdp_frame *xdpf;
	int i, nxmit = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	if (!netif_running(net_dev))
		return -ENETDOWN;

	for (i = 0; i < n; i++) {
		xdpf = frames[i];
		if (dpaa_xdp_xmit_frame(net_dev, xdpf))
			break;
		nxmit++;
	}

	return nxmit;
}

static int dpaa_ts_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct dpaa_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;

	if (copy_from_user(&config, rq->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		/* Couldn't disable rx/tx timestamping separately.
		 * Do nothing here.
		 */
		priv->tx_tstamp = false;
		break;
	case HWTSTAMP_TX_ON:
		priv->mac_dev->set_tstamp(priv->mac_dev->fman_mac, true);
		priv->tx_tstamp = true;
		break;
	default:
		return -ERANGE;
	}

	if (config.rx_filter == HWTSTAMP_FILTER_NONE) {
		/* Couldn't disable rx/tx timestamping separately.
		 * Do nothing here.
		 */
		priv->rx_tstamp = false;
	} else {
		priv->mac_dev->set_tstamp(priv->mac_dev->fman_mac, true);
		priv->rx_tstamp = true;
		/* TS is set for all frame types, not only those requested */
		config.rx_filter = HWTSTAMP_FILTER_ALL;
	}

	return copy_to_user(rq->ifr_data, &config, sizeof(config)) ?
			-EFAULT : 0;
}

static int dpaa_ioctl(struct net_device *net_dev, struct ifreq *rq, int cmd)
{
	int ret = -EINVAL;

	if (cmd == SIOCGMIIREG) {
		if (net_dev->phydev)
			return phy_mii_ioctl(net_dev->phydev, rq, cmd);
	}

	if (cmd == SIOCSHWTSTAMP)
		return dpaa_ts_ioctl(net_dev, rq, cmd);

	return ret;
}

static const struct net_device_ops dpaa_ops = {
	.ndo_open = dpaa_open,
	.ndo_start_xmit = dpaa_start_xmit,
	.ndo_stop = dpaa_eth_stop,
	.ndo_tx_timeout = dpaa_tx_timeout,
	.ndo_get_stats64 = dpaa_get_stats64,
	.ndo_change_carrier = fixed_phy_change_carrier,
	.ndo_set_mac_address = dpaa_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = dpaa_set_rx_mode,
	.ndo_eth_ioctl = dpaa_ioctl,
	.ndo_setup_tc = dpaa_setup_tc,
	.ndo_change_mtu = dpaa_change_mtu,
	.ndo_bpf = dpaa_xdp,
	.ndo_xdp_xmit = dpaa_xdp_xmit,
};

static int dpaa_napi_add(struct net_device *net_dev)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct dpaa_percpu_priv *percpu_priv;
	int cpu;

	for_each_possible_cpu(cpu) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, cpu);

		netif_napi_add(net_dev, &percpu_priv->np.napi, dpaa_eth_poll);
	}

	return 0;
}

static void dpaa_napi_del(struct net_device *net_dev)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct dpaa_percpu_priv *percpu_priv;
	int cpu;

	for_each_possible_cpu(cpu) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, cpu);

		netif_napi_del(&percpu_priv->np.napi);
	}
}

static inline void dpaa_bp_free_pf(const struct dpaa_bp *bp,
				   struct bm_buffer *bmb)
{
	dma_addr_t addr = bm_buf_addr(bmb);

	dma_unmap_page(bp->priv->rx_dma_dev, addr, DPAA_BP_RAW_SIZE,
		       DMA_FROM_DEVICE);

	skb_free_frag(phys_to_virt(addr));
}

/* Alloc the dpaa_bp struct and configure default values */
static struct dpaa_bp *dpaa_bp_alloc(struct device *dev)
{
	struct dpaa_bp *dpaa_bp;

	dpaa_bp = devm_kzalloc(dev, sizeof(*dpaa_bp), GFP_KERNEL);
	if (!dpaa_bp)
		return ERR_PTR(-ENOMEM);

	dpaa_bp->bpid = FSL_DPAA_BPID_INV;
	dpaa_bp->percpu_count = devm_alloc_percpu(dev, *dpaa_bp->percpu_count);
	if (!dpaa_bp->percpu_count)
		return ERR_PTR(-ENOMEM);

	dpaa_bp->config_count = FSL_DPAA_ETH_MAX_BUF_COUNT;

	dpaa_bp->seed_cb = dpaa_bp_seed;
	dpaa_bp->free_buf_cb = dpaa_bp_free_pf;

	return dpaa_bp;
}

/* Place all ingress FQs (Rx Default, Rx Error) in a dedicated CGR.
 * We won't be sending congestion notifications to FMan; for now, we just use
 * this CGR to generate enqueue rejections to FMan in order to drop the frames
 * before they reach our ingress queues and eat up memory.
 */
static int dpaa_ingress_cgr_init(struct dpaa_priv *priv)
{
	struct qm_mcc_initcgr initcgr;
	u32 cs_th;
	int err;

	err = qman_alloc_cgrid(&priv->ingress_cgr.cgrid);
	if (err < 0) {
		if (netif_msg_drv(priv))
			pr_err("Error %d allocating CGR ID\n", err);
		goto out_error;
	}

	/* Enable CS TD, but disable Congestion State Change Notifications. */
	memset(&initcgr, 0, sizeof(initcgr));
	initcgr.we_mask = cpu_to_be16(QM_CGR_WE_CS_THRES);
	initcgr.cgr.cscn_en = QM_CGR_EN;
	cs_th = DPAA_INGRESS_CS_THRESHOLD;
	qm_cgr_cs_thres_set64(&initcgr.cgr.cs_thres, cs_th, 1);

	initcgr.we_mask |= cpu_to_be16(QM_CGR_WE_CSTD_EN);
	initcgr.cgr.cstd_en = QM_CGR_EN;

	/* This CGR will be associated with the SWP affined to the current CPU.
	 * However, we'll place all our ingress FQs in it.
	 */
	err = qman_create_cgr(&priv->ingress_cgr, QMAN_CGR_FLAG_USE_INIT,
			      &initcgr);
	if (err < 0) {
		if (netif_msg_drv(priv))
			pr_err("Error %d creating ingress CGR with ID %d\n",
			       err, priv->ingress_cgr.cgrid);
		qman_release_cgrid(priv->ingress_cgr.cgrid);
		goto out_error;
	}
	if (netif_msg_drv(priv))
		pr_debug("Created ingress CGR %d for netdev with hwaddr %pM\n",
			 priv->ingress_cgr.cgrid, priv->mac_dev->addr);

	priv->use_ingress_cgr = true;

out_error:
	return err;
}

static u16 dpaa_get_headroom(struct dpaa_buffer_layout *bl,
			     enum port_type port)
{
	u16 headroom;

	/* The frame headroom must accommodate:
	 * - the driver private data area
	 * - parse results, hash results, timestamp if selected
	 * If either hash results or time stamp are selected, both will
	 * be copied to/from the frame headroom, as TS is located between PR and
	 * HR in the IC and IC copy size has a granularity of 16bytes
	 * (see description of FMBM_RICP and FMBM_TICP registers in DPAARM)
	 *
	 * Also make sure the headroom is a multiple of data_align bytes
	 */
	headroom = (u16)(bl[port].priv_data_size + DPAA_HWA_SIZE);

	if (port == RX) {
#ifdef CONFIG_DPAA_ERRATUM_A050385
		if (unlikely(fman_has_errata_a050385()))
			headroom = XDP_PACKET_HEADROOM;
#endif

		return ALIGN(headroom, DPAA_FD_RX_DATA_ALIGNMENT);
	} else {
		return ALIGN(headroom, DPAA_FD_DATA_ALIGNMENT);
	}
}

static int dpaa_eth_probe(struct platform_device *pdev)
{
	struct net_device *net_dev = NULL;
	struct dpaa_bp *dpaa_bp = NULL;
	struct dpaa_fq *dpaa_fq, *tmp;
	struct dpaa_priv *priv = NULL;
	struct fm_port_fqs port_fqs;
	struct mac_device *mac_dev;
	int err = 0, channel;
	struct device *dev;

	dev = &pdev->dev;

	err = bman_is_probed();
	if (!err)
		return -EPROBE_DEFER;
	if (err < 0) {
		dev_err(dev, "failing probe due to bman probe error\n");
		return -ENODEV;
	}
	err = qman_is_probed();
	if (!err)
		return -EPROBE_DEFER;
	if (err < 0) {
		dev_err(dev, "failing probe due to qman probe error\n");
		return -ENODEV;
	}
	err = bman_portals_probed();
	if (!err)
		return -EPROBE_DEFER;
	if (err < 0) {
		dev_err(dev,
			"failing probe due to bman portals probe error\n");
		return -ENODEV;
	}
	err = qman_portals_probed();
	if (!err)
		return -EPROBE_DEFER;
	if (err < 0) {
		dev_err(dev,
			"failing probe due to qman portals probe error\n");
		return -ENODEV;
	}

	/* Allocate this early, so we can store relevant information in
	 * the private area
	 */
	net_dev = alloc_etherdev_mq(sizeof(*priv), DPAA_ETH_TXQ_NUM);
	if (!net_dev) {
		dev_err(dev, "alloc_etherdev_mq() failed\n");
		return -ENOMEM;
	}

	/* Do this here, so we can be verbose early */
	SET_NETDEV_DEV(net_dev, dev->parent);
	dev_set_drvdata(dev, net_dev);

	priv = netdev_priv(net_dev);
	priv->net_dev = net_dev;

	priv->msg_enable = netif_msg_init(debug, DPAA_MSG_DEFAULT);

	mac_dev = dpaa_mac_dev_get(pdev);
	if (IS_ERR(mac_dev)) {
		netdev_err(net_dev, "dpaa_mac_dev_get() failed\n");
		err = PTR_ERR(mac_dev);
		goto free_netdev;
	}

	/* Devices used for DMA mapping */
	priv->rx_dma_dev = fman_port_get_device(mac_dev->port[RX]);
	priv->tx_dma_dev = fman_port_get_device(mac_dev->port[TX]);
	err = dma_coerce_mask_and_coherent(priv->rx_dma_dev, DMA_BIT_MASK(40));
	if (!err)
		err = dma_coerce_mask_and_coherent(priv->tx_dma_dev,
						   DMA_BIT_MASK(40));
	if (err) {
		netdev_err(net_dev, "dma_coerce_mask_and_coherent() failed\n");
		goto free_netdev;
	}

	/* If fsl_fm_max_frm is set to a higher value than the all-common 1500,
	 * we choose conservatively and let the user explicitly set a higher
	 * MTU via ifconfig. Otherwise, the user may end up with different MTUs
	 * in the same LAN.
	 * If on the other hand fsl_fm_max_frm has been chosen below 1500,
	 * start with the maximum allowed.
	 */
	net_dev->mtu = min(dpaa_get_max_mtu(), ETH_DATA_LEN);

	netdev_dbg(net_dev, "Setting initial MTU on net device: %d\n",
		   net_dev->mtu);

	priv->buf_layout[RX].priv_data_size = DPAA_RX_PRIV_DATA_SIZE; /* Rx */
	priv->buf_layout[TX].priv_data_size = DPAA_TX_PRIV_DATA_SIZE; /* Tx */

	/* bp init */
	dpaa_bp = dpaa_bp_alloc(dev);
	if (IS_ERR(dpaa_bp)) {
		err = PTR_ERR(dpaa_bp);
		goto free_dpaa_bps;
	}
	/* the raw size of the buffers used for reception */
	dpaa_bp->raw_size = DPAA_BP_RAW_SIZE;
	/* avoid runtime computations by keeping the usable size here */
	dpaa_bp->size = dpaa_bp_size(dpaa_bp->raw_size);
	dpaa_bp->priv = priv;

	err = dpaa_bp_alloc_pool(dpaa_bp);
	if (err < 0)
		goto free_dpaa_bps;
	priv->dpaa_bp = dpaa_bp;

	INIT_LIST_HEAD(&priv->dpaa_fq_list);

	memset(&port_fqs, 0, sizeof(port_fqs));

	err = dpaa_alloc_all_fqs(dev, &priv->dpaa_fq_list, &port_fqs);
	if (err < 0) {
		dev_err(dev, "dpaa_alloc_all_fqs() failed\n");
		goto free_dpaa_bps;
	}

	priv->mac_dev = mac_dev;

	channel = dpaa_get_channel();
	if (channel < 0) {
		dev_err(dev, "dpaa_get_channel() failed\n");
		err = channel;
		goto free_dpaa_bps;
	}

	priv->channel = (u16)channel;

	/* Walk the CPUs with affine portals
	 * and add this pool channel to each's dequeue mask.
	 */
	dpaa_eth_add_channel(priv->channel, &pdev->dev);

	dpaa_fq_setup(priv, &dpaa_fq_cbs, priv->mac_dev->port[TX]);

	/* Create a congestion group for this netdev, with
	 * dynamically-allocated CGR ID.
	 * Must be executed after probing the MAC, but before
	 * assigning the egress FQs to the CGRs.
	 */
	err = dpaa_eth_cgr_init(priv);
	if (err < 0) {
		dev_err(dev, "Error initializing CGR\n");
		goto free_dpaa_bps;
	}

	err = dpaa_ingress_cgr_init(priv);
	if (err < 0) {
		dev_err(dev, "Error initializing ingress CGR\n");
		goto delete_egress_cgr;
	}

	/* Add the FQs to the interface, and make them active */
	list_for_each_entry_safe(dpaa_fq, tmp, &priv->dpaa_fq_list, list) {
		err = dpaa_fq_init(dpaa_fq, false);
		if (err < 0)
			goto free_dpaa_fqs;
	}

	priv->tx_headroom = dpaa_get_headroom(priv->buf_layout, TX);
	priv->rx_headroom = dpaa_get_headroom(priv->buf_layout, RX);

	/* All real interfaces need their ports initialized */
	err = dpaa_eth_init_ports(mac_dev, dpaa_bp, &port_fqs,
				  &priv->buf_layout[0], dev);
	if (err)
		goto free_dpaa_fqs;

	/* Rx traffic distribution based on keygen hashing defaults to on */
	priv->keygen_in_use = true;

	priv->percpu_priv = devm_alloc_percpu(dev, *priv->percpu_priv);
	if (!priv->percpu_priv) {
		dev_err(dev, "devm_alloc_percpu() failed\n");
		err = -ENOMEM;
		goto free_dpaa_fqs;
	}

	priv->num_tc = 1;
	netif_set_real_num_tx_queues(net_dev, priv->num_tc * DPAA_TC_TXQ_NUM);

	/* Initialize NAPI */
	err = dpaa_napi_add(net_dev);
	if (err < 0)
		goto delete_dpaa_napi;

	err = dpaa_netdev_init(net_dev, &dpaa_ops, tx_timeout);
	if (err < 0)
		goto delete_dpaa_napi;

	dpaa_eth_sysfs_init(&net_dev->dev);

	netif_info(priv, probe, net_dev, "Probed interface %s\n",
		   net_dev->name);

	return 0;

delete_dpaa_napi:
	dpaa_napi_del(net_dev);
free_dpaa_fqs:
	dpaa_fq_free(dev, &priv->dpaa_fq_list);
	qman_delete_cgr_safe(&priv->ingress_cgr);
	qman_release_cgrid(priv->ingress_cgr.cgrid);
delete_egress_cgr:
	qman_delete_cgr_safe(&priv->cgr_data.cgr);
	qman_release_cgrid(priv->cgr_data.cgr.cgrid);
free_dpaa_bps:
	dpaa_bps_free(priv);
free_netdev:
	dev_set_drvdata(dev, NULL);
	free_netdev(net_dev);

	return err;
}

static int dpaa_remove(struct platform_device *pdev)
{
	struct net_device *net_dev;
	struct dpaa_priv *priv;
	struct device *dev;
	int err;

	dev = &pdev->dev;
	net_dev = dev_get_drvdata(dev);

	priv = netdev_priv(net_dev);

	dpaa_eth_sysfs_remove(dev);

	dev_set_drvdata(dev, NULL);
	unregister_netdev(net_dev);

	err = dpaa_fq_free(dev, &priv->dpaa_fq_list);

	qman_delete_cgr_safe(&priv->ingress_cgr);
	qman_release_cgrid(priv->ingress_cgr.cgrid);
	qman_delete_cgr_safe(&priv->cgr_data.cgr);
	qman_release_cgrid(priv->cgr_data.cgr.cgrid);

	dpaa_napi_del(net_dev);

	dpaa_bps_free(priv);

	free_netdev(net_dev);

	return err;
}

static const struct platform_device_id dpaa_devtype[] = {
	{
		.name = "dpaa-ethernet",
		.driver_data = 0,
	}, {
	}
};
MODULE_DEVICE_TABLE(platform, dpaa_devtype);

static struct platform_driver dpaa_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.id_table = dpaa_devtype,
	.probe = dpaa_eth_probe,
	.remove = dpaa_remove
};

static int __init dpaa_load(void)
{
	int err;

	pr_debug("FSL DPAA Ethernet driver\n");

	/* initialize dpaa_eth mirror values */
	dpaa_rx_extra_headroom = fman_get_rx_extra_headroom();
	dpaa_max_frm = fman_get_max_frm();

	err = platform_driver_register(&dpaa_driver);
	if (err < 0)
		pr_err("Error, platform_driver_register() = %d\n", err);

	return err;
}
module_init(dpaa_load);

static void __exit dpaa_unload(void)
{
	platform_driver_unregister(&dpaa_driver);

	/* Only one channel is used and needs to be released after all
	 * interfaces are removed
	 */
	dpaa_release_channel();
}
module_exit(dpaa_unload);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL DPAA Ethernet driver");
