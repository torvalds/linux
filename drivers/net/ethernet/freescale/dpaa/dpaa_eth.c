/* Copyright 2008 - 2016 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
			  NETIF_MSG_IFDOWN)

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
#define DPAA_FD_DATA_ALIGNMENT  16

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
#define DPAA_RX_PRIV_DATA_SIZE	(u16)(DPAA_TX_PRIV_DATA_SIZE + \
					dpaa_rx_extra_headroom)

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

/* The raw buffer size must be cacheline aligned */
#define DPAA_BP_RAW_SIZE 4096
/* When using more than one buffer pool, the raw sizes are as follows:
 * 1 bp: 4KB
 * 2 bp: 2KB, 4KB
 * 3 bp: 1KB, 2KB, 4KB
 * 4 bp: 1KB, 2KB, 4KB, 8KB
 */
static inline size_t bpool_buffer_raw_size(u8 index, u8 cnt)
{
	size_t res = DPAA_BP_RAW_SIZE / 4;
	u8 i;

	for (i = (cnt < 3) ? cnt : 3; i < 3 + index; i++)
		res *= 2;
	return res;
}

/* FMan-DMA requires 16-byte alignment for Rx buffers, but SKB_DATA_ALIGN is
 * even stronger (SMP_CACHE_BYTES-aligned), so we just get away with that,
 * via SKB_WITH_OVERHEAD(). We can't rely on netdev_alloc_frag() giving us
 * half-page-aligned buffers, so we reserve some more space for start-of-buffer
 * alignment.
 */
#define dpaa_bp_size(raw_size) SKB_WITH_OVERHEAD((raw_size) - SMP_CACHE_BYTES)

static int dpaa_max_frm;

static int dpaa_rx_extra_headroom;

#define dpaa_get_max_mtu()	\
	(dpaa_max_frm - (VLAN_ETH_HLEN + ETH_FCS_LEN))

static int dpaa_netdev_init(struct net_device *net_dev,
			    const struct net_device_ops *dpaa_ops,
			    u16 tx_timeout)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct device *dev = net_dev->dev.parent;
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
	mac_addr = priv->mac_dev->addr;

	net_dev->mem_start = priv->mac_dev->res->start;
	net_dev->mem_end = priv->mac_dev->res->end;

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

	memcpy(net_dev->perm_addr, mac_addr, net_dev->addr_len);
	memcpy(net_dev->dev_addr, mac_addr, net_dev->addr_len);

	net_dev->ethtool_ops = &dpaa_ethtool_ops;

	net_dev->needed_headroom = priv->tx_headroom;
	net_dev->watchdog_timeo = msecs_to_jiffies(tx_timeout);

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
	usleep_range(5000, 10000);

	err = mac_dev->stop(mac_dev);
	if (err < 0)
		netif_err(priv, ifdown, net_dev, "mac_dev->stop() = %d\n",
			  err);

	for (i = 0; i < ARRAY_SIZE(mac_dev->port); i++) {
		error = fman_port_disable(mac_dev->port[i]);
		if (error)
			err = error;
	}

	if (net_dev->phydev)
		phy_disconnect(net_dev->phydev);
	net_dev->phydev = NULL;

	return err;
}

static void dpaa_tx_timeout(struct net_device *net_dev)
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
				   (enet_addr_t *)net_dev->dev_addr);
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
	int i;

	for (i = 0; i < DPAA_BPS_NUM; i++)
		dpaa_bp_free(priv->dpaa_bps[i]);
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

static void dpaa_eth_add_channel(u16 channel)
{
	u32 pool = QM_SDQCR_CHANNELS_POOL_CONV(channel);
	const cpumask_t *cpus = qman_affine_cpus();
	struct qman_portal *portal;
	int cpu;

	for_each_cpu_and(cpu, cpus, cpu_online_mask) {
		portal = qman_get_affine_portal(cpu);
		qman_p_static_dequeue_add(portal, pool);
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

	/* Set different thresholds based on the MAC speed.
	 * This may turn suboptimal if the MAC is reconfigured at a speed
	 * lower than its max, e.g. if a dTSEC later negotiates a 100Mbps link.
	 * In such cases, we ought to reconfigure the threshold, too.
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
			"No Qman software (affine) channels found");

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
			/* fall through */
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

static int dpaa_eth_init_rx_port(struct fman_port *port, struct dpaa_bp **bps,
				 size_t count, struct dpaa_fq *errq,
				 struct dpaa_fq *defq, struct dpaa_fq *pcdq,
				 struct dpaa_buffer_layout *buf_layout)
{
	struct fman_buffer_prefix_content buf_prefix_content;
	struct fman_port_rx_params *rx_p;
	struct fman_port_params params;
	int i, err;

	memset(&params, 0, sizeof(params));
	memset(&buf_prefix_content, 0, sizeof(buf_prefix_content));

	buf_prefix_content.priv_data_size = buf_layout->priv_data_size;
	buf_prefix_content.pass_prs_result = true;
	buf_prefix_content.pass_hash_result = true;
	buf_prefix_content.pass_time_stamp = true;
	buf_prefix_content.data_align = DPAA_FD_DATA_ALIGNMENT;

	rx_p = &params.specific_params.rx_params;
	rx_p->err_fqid = errq->fqid;
	rx_p->dflt_fqid = defq->fqid;
	if (pcdq) {
		rx_p->pcd_base_fqid = pcdq->fqid;
		rx_p->pcd_fqs_count = DPAA_ETH_PCD_RXQ_NUM;
	}

	count = min(ARRAY_SIZE(rx_p->ext_buf_pools.ext_buf_pool), count);
	rx_p->ext_buf_pools.num_of_pools_used = (u8)count;
	for (i = 0; i < count; i++) {
		rx_p->ext_buf_pools.ext_buf_pool[i].id =  bps[i]->bpid;
		rx_p->ext_buf_pools.ext_buf_pool[i].size = (u16)bps[i]->size;
	}

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
			       struct dpaa_bp **bps, size_t count,
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

	err = dpaa_eth_init_rx_port(rxport, bps, count, port_fqs->rx_errq,
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

		dma_unmap_single(dpaa_bp->dev, qm_fd_addr(fd), dpaa_bp->size,
				 DMA_FROM_DEVICE);

		dpaa_release_sgt_members(sgt);

		addr = dma_map_single(dpaa_bp->dev, vaddr, dpaa_bp->size,
				      DMA_FROM_DEVICE);
		if (dma_mapping_error(dpaa_bp->dev, addr)) {
			dev_err(dpaa_bp->dev, "DMA mapping failed");
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
			       char *parse_results)
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
	struct device *dev = dpaa_bp->dev;
	struct bm_buffer bmb[8];
	dma_addr_t addr;
	void *new_buf;
	u8 i;

	for (i = 0; i < 8; i++) {
		new_buf = netdev_alloc_frag(dpaa_bp->raw_size);
		if (unlikely(!new_buf)) {
			dev_err(dev, "netdev_alloc_frag() failed, size %zu\n",
				dpaa_bp->raw_size);
			goto release_previous_buffs;
		}
		new_buf = PTR_ALIGN(new_buf, SMP_CACHE_BYTES);

		addr = dma_map_single(dev, new_buf,
				      dpaa_bp->size, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dev, addr))) {
			dev_err(dpaa_bp->dev, "DMA map failed");
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
	int res, i;

	for (i = 0; i < DPAA_BPS_NUM; i++) {
		dpaa_bp = priv->dpaa_bps[i];
		if (!dpaa_bp)
			return -EINVAL;
		countptr = this_cpu_ptr(dpaa_bp->percpu_count);
		res  = dpaa_eth_refill_bpool(dpaa_bp, countptr);
		if (res)
			return res;
	}
	return 0;
}

/* Cleanup function for outgoing frame descriptors that were built on Tx path,
 * either contiguous frames or scatter/gather ones.
 * Skb freeing is not handled here.
 *
 * This function may be called on error paths in the Tx function, so guard
 * against cases when not all fd relevant fields were filled in.
 *
 * Return the skb backpointer, since for S/G frames the buffer containing it
 * gets freed here.
 */
static struct sk_buff *dpaa_cleanup_tx_fd(const struct dpaa_priv *priv,
					  const struct qm_fd *fd)
{
	const enum dma_data_direction dma_dir = DMA_TO_DEVICE;
	struct device *dev = priv->net_dev->dev.parent;
	struct skb_shared_hwtstamps shhwtstamps;
	dma_addr_t addr = qm_fd_addr(fd);
	const struct qm_sg_entry *sgt;
	struct sk_buff **skbh, *skb;
	int nr_frags, i;
	u64 ns;

	skbh = (struct sk_buff **)phys_to_virt(addr);
	skb = *skbh;

	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));

		if (!fman_port_get_tstamp(priv->mac_dev->port[TX], (void *)skbh,
					  &ns)) {
			shhwtstamps.hwtstamp = ns_to_ktime(ns);
			skb_tstamp_tx(skb, &shhwtstamps);
		} else {
			dev_warn(dev, "fman_port_get_tstamp failed!\n");
		}
	}

	if (unlikely(qm_fd_get_format(fd) == qm_fd_sg)) {
		nr_frags = skb_shinfo(skb)->nr_frags;
		dma_unmap_single(dev, addr,
				 qm_fd_get_offset(fd) + DPAA_SGT_SIZE,
				 dma_dir);

		/* The sgt buffer has been allocated with netdev_alloc_frag(),
		 * it's from lowmem.
		 */
		sgt = phys_to_virt(addr + qm_fd_get_offset(fd));

		/* sgt[0] is from lowmem, was dma_map_single()-ed */
		dma_unmap_single(dev, qm_sg_addr(&sgt[0]),
				 qm_sg_entry_get_len(&sgt[0]), dma_dir);

		/* remaining pages were mapped with skb_frag_dma_map() */
		for (i = 1; i <= nr_frags; i++) {
			WARN_ON(qm_sg_entry_is_ext(&sgt[i]));

			dma_unmap_page(dev, qm_sg_addr(&sgt[i]),
				       qm_sg_entry_get_len(&sgt[i]), dma_dir);
		}

		/* Free the page frag that we allocated on Tx */
		skb_free_frag(phys_to_virt(addr));
	} else {
		dma_unmap_single(dev, addr,
				 skb_tail_pointer(skb) - (u8 *)skbh, dma_dir);
	}

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
	WARN_ON(fd_off != priv->rx_headroom);
	skb_reserve(skb, fd_off);
	skb_put(skb, qm_fd_get_length(fd));

	skb->ip_summed = rx_csum_offload(priv, fd);

	return skb;

free_buffer:
	skb_free_frag(vaddr);
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
	int i;

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
		WARN_ON(!IS_ALIGNED((unsigned long)sg_vaddr,
				    SMP_CACHE_BYTES));

		/* We may use multiple Rx pools */
		dpaa_bp = dpaa_bpid2pool(sgt[i].bpid);
		if (!dpaa_bp)
			goto free_buffers;

		count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
		dma_unmap_single(dpaa_bp->dev, sg_addr, dpaa_bp->size,
				 DMA_FROM_DEVICE);
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
		(*count_ptr)--;

		if (qm_sg_entry_is_final(&sgt[i]))
			break;
	}
	WARN_ONCE(i == DPAA_SGT_MAX_ENTRIES, "No final bit on SGT\n");

	/* free the SG table buffer */
	skb_free_frag(vaddr);

	return skb;

free_buffers:
	/* compensate sw bpool counter changes */
	for (i--; i >= 0; i--) {
		dpaa_bp = dpaa_bpid2pool(sgt[i].bpid);
		if (dpaa_bp) {
			count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
			(*count_ptr)++;
		}
	}
	/* free all the SG entries */
	for (i = 0; i < DPAA_SGT_MAX_ENTRIES ; i++) {
		sg_addr = qm_sg_addr(&sgt[i]);
		sg_vaddr = phys_to_virt(sg_addr);
		skb_free_frag(sg_vaddr);
		dpaa_bp = dpaa_bpid2pool(sgt[i].bpid);
		if (dpaa_bp) {
			count_ptr = this_cpu_ptr(dpaa_bp->percpu_count);
			(*count_ptr)--;
		}

		if (qm_sg_entry_is_final(&sgt[i]))
			break;
	}
	/* free the SGT fragment */
	skb_free_frag(vaddr);

	return NULL;
}

static int skb_to_contig_fd(struct dpaa_priv *priv,
			    struct sk_buff *skb, struct qm_fd *fd,
			    int *offset)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	enum dma_data_direction dma_dir;
	unsigned char *buffer_start;
	struct sk_buff **skbh;
	dma_addr_t addr;
	int err;

	/* We are guaranteed to have at least tx_headroom bytes
	 * available, so just use that for offset.
	 */
	fd->bpid = FSL_DPAA_BPID_INV;
	buffer_start = skb->data - priv->tx_headroom;
	dma_dir = DMA_TO_DEVICE;

	skbh = (struct sk_buff **)buffer_start;
	*skbh = skb;

	/* Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpaa_enable_tx_csum(priv, skb, fd,
				  ((char *)skbh) + DPAA_TX_PRIV_DATA_SIZE);
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
	addr = dma_map_single(dev, skbh,
			      skb_tail_pointer(skb) - buffer_start, dma_dir);
	if (unlikely(dma_mapping_error(dev, addr))) {
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
	struct device *dev = net_dev->dev.parent;
	struct qm_sg_entry *sgt;
	struct sk_buff **skbh;
	int i, j, err, sz;
	void *buffer_start;
	skb_frag_t *frag;
	dma_addr_t addr;
	size_t frag_len;
	void *sgt_buf;

	/* get a page frag to store the SGTable */
	sz = SKB_DATA_ALIGN(priv->tx_headroom + DPAA_SGT_SIZE);
	sgt_buf = netdev_alloc_frag(sz);
	if (unlikely(!sgt_buf)) {
		netdev_err(net_dev, "netdev_alloc_frag() failed for size %d\n",
			   sz);
		return -ENOMEM;
	}

	/* Enable L3/L4 hardware checksum computation.
	 *
	 * We must do this before dma_map_single(DMA_TO_DEVICE), because we may
	 * need to write into the skb.
	 */
	err = dpaa_enable_tx_csum(priv, skb, fd,
				  sgt_buf + DPAA_TX_PRIV_DATA_SIZE);
	if (unlikely(err < 0)) {
		if (net_ratelimit())
			netif_err(priv, tx_err, net_dev, "HW csum error: %d\n",
				  err);
		goto csum_failed;
	}

	/* SGT[0] is used by the linear part */
	sgt = (struct qm_sg_entry *)(sgt_buf + priv->tx_headroom);
	frag_len = skb_headlen(skb);
	qm_sg_entry_set_len(&sgt[0], frag_len);
	sgt[0].bpid = FSL_DPAA_BPID_INV;
	sgt[0].offset = 0;
	addr = dma_map_single(dev, skb->data,
			      skb_headlen(skb), dma_dir);
	if (unlikely(dma_mapping_error(dev, addr))) {
		dev_err(dev, "DMA mapping failed");
		err = -EINVAL;
		goto sg0_map_failed;
	}
	qm_sg_entry_set64(&sgt[0], addr);

	/* populate the rest of SGT entries */
	for (i = 0; i < nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		frag_len = skb_frag_size(frag);
		WARN_ON(!skb_frag_page(frag));
		addr = skb_frag_dma_map(dev, frag, 0,
					frag_len, dma_dir);
		if (unlikely(dma_mapping_error(dev, addr))) {
			dev_err(dev, "DMA mapping failed");
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

	qm_fd_set_sg(fd, priv->tx_headroom, skb->len);

	/* DMA map the SGT page */
	buffer_start = (void *)sgt - priv->tx_headroom;
	skbh = (struct sk_buff **)buffer_start;
	*skbh = skb;

	addr = dma_map_single(dev, buffer_start,
			      priv->tx_headroom + DPAA_SGT_SIZE, dma_dir);
	if (unlikely(dma_mapping_error(dev, addr))) {
		dev_err(dev, "DMA mapping failed");
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
		dma_unmap_page(dev, qm_sg_addr(&sgt[j]),
			       qm_sg_entry_get_len(&sgt[j]), dma_dir);
sg0_map_failed:
csum_failed:
	skb_free_frag(sgt_buf);

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
	txq->trans_start = jiffies;

	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		fd.cmd |= cpu_to_be32(FM_FD_CMD_UPD);
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}

	if (likely(dpaa_xmit(priv, percpu_stats, queue_mapping, &fd) == 0))
		return NETDEV_TX_OK;

	dpaa_cleanup_tx_fd(priv, &fd);
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

	skb = dpaa_cleanup_tx_fd(priv, fd);
	dev_kfree_skb(skb);
}

static int dpaa_eth_poll(struct napi_struct *napi, int budget)
{
	struct dpaa_napi_portal *np =
			container_of(napi, struct dpaa_napi_portal, napi);

	int cleaned = qman_p_poll_dqrr(np->p, budget);

	if (cleaned < budget) {
		napi_complete_done(napi, cleaned);
		qman_p_irqsource_add(np->p, QM_PIRQ_DQRI);
	} else if (np->down) {
		qman_p_irqsource_add(np->p, QM_PIRQ_DQRI);
	}

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

	skb = dpaa_cleanup_tx_fd(priv, fd);

	consume_skb(skb);
}

static inline int dpaa_eth_napi_schedule(struct dpaa_percpu_priv *percpu_priv,
					 struct qman_portal *portal)
{
	if (unlikely(in_irq() || !in_serving_softirq())) {
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
					      const struct qm_dqrr_entry *dq)
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

	if (dpaa_eth_napi_schedule(percpu_priv, portal))
		return qman_cb_dqrr_stop;

	dpaa_eth_refill_bpools(priv);
	dpaa_rx_error(net_dev, priv, percpu_priv, &dq->fd, fq->fqid);

	return qman_cb_dqrr_consume;
}

static enum qman_cb_dqrr_result rx_default_dqrr(struct qman_portal *portal,
						struct qman_fq *fq,
						const struct qm_dqrr_entry *dq)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa_percpu_priv *percpu_priv;
	const struct qm_fd *fd = &dq->fd;
	dma_addr_t addr = qm_fd_addr(fd);
	enum qm_fd_format fd_format;
	struct net_device *net_dev;
	u32 fd_status, hash_offset;
	struct dpaa_bp *dpaa_bp;
	struct dpaa_priv *priv;
	unsigned int skb_len;
	struct sk_buff *skb;
	int *count_ptr;
	void *vaddr;
	u64 ns;

	fd_status = be32_to_cpu(fd->status);
	fd_format = qm_fd_get_format(fd);
	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);
	dpaa_bp = dpaa_bpid2pool(dq->fd.bpid);
	if (!dpaa_bp)
		return qman_cb_dqrr_consume;

	/* Trace the Rx fd */
	trace_dpaa_rx_fd(net_dev, fq, &dq->fd);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);
	percpu_stats = &percpu_priv->stats;

	if (unlikely(dpaa_eth_napi_schedule(percpu_priv, portal)))
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

	dpaa_bp = dpaa_bpid2pool(fd->bpid);
	if (!dpaa_bp)
		return qman_cb_dqrr_consume;

	dma_unmap_single(dpaa_bp->dev, addr, dpaa_bp->size, DMA_FROM_DEVICE);

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

	if (likely(fd_format == qm_fd_contig))
		skb = contig_fd_to_skb(priv, fd);
	else
		skb = sg_fd_to_skb(priv, fd);
	if (!skb)
		return qman_cb_dqrr_consume;

	if (priv->rx_tstamp) {
		shhwtstamps = skb_hwtstamps(skb);
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));

		if (!fman_port_get_tstamp(priv->mac_dev->port[RX], vaddr, &ns))
			shhwtstamps->hwtstamp = ns_to_ktime(ns);
		else
			dev_warn(net_dev->dev.parent, "fman_port_get_tstamp failed!\n");
	}

	skb->protocol = eth_type_trans(skb, net_dev);

	if (net_dev->features & NETIF_F_RXHASH && priv->keygen_in_use &&
	    !fman_port_get_hash_result_offset(priv->mac_dev->port[RX],
					      &hash_offset)) {
		enum pkt_hash_types type;

		/* if L4 exists, it was used in the hash generation */
		type = be32_to_cpu(fd->status) & FM_FD_STAT_L4CV ?
			PKT_HASH_TYPE_L4 : PKT_HASH_TYPE_L3;
		skb_set_hash(skb, be32_to_cpu(*(u32 *)(vaddr + hash_offset)),
			     type);
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
						const struct qm_dqrr_entry *dq)
{
	struct dpaa_percpu_priv *percpu_priv;
	struct net_device *net_dev;
	struct dpaa_priv *priv;

	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	if (dpaa_eth_napi_schedule(percpu_priv, portal))
		return qman_cb_dqrr_stop;

	dpaa_tx_error(net_dev, priv, percpu_priv, &dq->fd, fq->fqid);

	return qman_cb_dqrr_consume;
}

static enum qman_cb_dqrr_result conf_dflt_dqrr(struct qman_portal *portal,
					       struct qman_fq *fq,
					       const struct qm_dqrr_entry *dq)
{
	struct dpaa_percpu_priv *percpu_priv;
	struct net_device *net_dev;
	struct dpaa_priv *priv;

	net_dev = ((struct dpaa_fq *)fq)->net_dev;
	priv = netdev_priv(net_dev);

	/* Trace the fd */
	trace_dpaa_tx_conf_fd(net_dev, fq, &dq->fd);

	percpu_priv = this_cpu_ptr(priv->percpu_priv);

	if (dpaa_eth_napi_schedule(percpu_priv, portal))
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

	skb = dpaa_cleanup_tx_fd(priv, fd);
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

		percpu_priv->np.down = 0;
		napi_enable(&percpu_priv->np.napi);
	}
}

static void dpaa_eth_napi_disable(struct dpaa_priv *priv)
{
	struct dpaa_percpu_priv *percpu_priv;
	int i;

	for_each_online_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);

		percpu_priv->np.down = 1;
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

static int dpaa_phy_init(struct net_device *net_dev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	struct mac_device *mac_dev;
	struct phy_device *phy_dev;
	struct dpaa_priv *priv;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	phy_dev = of_phy_connect(net_dev, mac_dev->phy_node,
				 &dpaa_adjust_link, 0,
				 mac_dev->phy_if);
	if (!phy_dev) {
		netif_err(priv, ifup, net_dev, "init_phy() failed\n");
		return -ENODEV;
	}

	/* Remove any features not supported by the controller */
	ethtool_convert_legacy_u32_to_link_mode(mask, mac_dev->if_support);
	linkmode_and(phy_dev->supported, phy_dev->supported, mask);

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

	err = priv->mac_dev->start(mac_dev);
	if (err < 0) {
		netif_err(priv, ifup, net_dev, "mac_dev->start() = %d\n", err);
		goto mac_start_failed;
	}

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
	.ndo_do_ioctl = dpaa_ioctl,
	.ndo_setup_tc = dpaa_setup_tc,
};

static int dpaa_napi_add(struct net_device *net_dev)
{
	struct dpaa_priv *priv = netdev_priv(net_dev);
	struct dpaa_percpu_priv *percpu_priv;
	int cpu;

	for_each_possible_cpu(cpu) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, cpu);

		netif_napi_add(net_dev, &percpu_priv->np.napi,
			       dpaa_eth_poll, NAPI_POLL_WEIGHT);
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

	dma_unmap_single(bp->dev, addr, bp->size, DMA_FROM_DEVICE);

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

static inline u16 dpaa_get_headroom(struct dpaa_buffer_layout *bl)
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
	headroom = (u16)(bl->priv_data_size + DPAA_PARSE_RESULTS_SIZE +
		DPAA_TIME_STAMP_SIZE + DPAA_HASH_RESULTS_SIZE);

	return DPAA_FD_DATA_ALIGNMENT ? ALIGN(headroom,
					      DPAA_FD_DATA_ALIGNMENT) :
					headroom;
}

static int dpaa_eth_probe(struct platform_device *pdev)
{
	struct dpaa_bp *dpaa_bps[DPAA_BPS_NUM] = {NULL};
	struct net_device *net_dev = NULL;
	struct dpaa_fq *dpaa_fq, *tmp;
	struct dpaa_priv *priv = NULL;
	struct fm_port_fqs port_fqs;
	struct mac_device *mac_dev;
	int err = 0, i, channel;
	struct device *dev;

	/* device used for DMA mapping */
	dev = pdev->dev.parent;
	err = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (err) {
		dev_err(dev, "dma_coerce_mask_and_coherent() failed\n");
		return err;
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
	SET_NETDEV_DEV(net_dev, dev);
	dev_set_drvdata(dev, net_dev);

	priv = netdev_priv(net_dev);
	priv->net_dev = net_dev;

	priv->msg_enable = netif_msg_init(debug, DPAA_MSG_DEFAULT);

	mac_dev = dpaa_mac_dev_get(pdev);
	if (IS_ERR(mac_dev)) {
		dev_err(dev, "dpaa_mac_dev_get() failed\n");
		err = PTR_ERR(mac_dev);
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
	for (i = 0; i < DPAA_BPS_NUM; i++) {
		dpaa_bps[i] = dpaa_bp_alloc(dev);
		if (IS_ERR(dpaa_bps[i])) {
			err = PTR_ERR(dpaa_bps[i]);
			goto free_dpaa_bps;
		}
		/* the raw size of the buffers used for reception */
		dpaa_bps[i]->raw_size = bpool_buffer_raw_size(i, DPAA_BPS_NUM);
		/* avoid runtime computations by keeping the usable size here */
		dpaa_bps[i]->size = dpaa_bp_size(dpaa_bps[i]->raw_size);
		dpaa_bps[i]->dev = dev;

		err = dpaa_bp_alloc_pool(dpaa_bps[i]);
		if (err < 0)
			goto free_dpaa_bps;
		priv->dpaa_bps[i] = dpaa_bps[i];
	}

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
	dpaa_eth_add_channel(priv->channel);

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

	priv->tx_headroom = dpaa_get_headroom(&priv->buf_layout[TX]);
	priv->rx_headroom = dpaa_get_headroom(&priv->buf_layout[RX]);

	/* All real interfaces need their ports initialized */
	err = dpaa_eth_init_ports(mac_dev, dpaa_bps, DPAA_BPS_NUM, &port_fqs,
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

	dev = pdev->dev.parent;
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
