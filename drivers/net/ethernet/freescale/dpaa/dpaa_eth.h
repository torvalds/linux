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

#ifndef __DPAA_H
#define __DPAA_H

#include <linux/netdevice.h>
#include <soc/fsl/qman.h>
#include <soc/fsl/bman.h>

#include "fman.h"
#include "mac.h"
#include "dpaa_eth_trace.h"

#define DPAA_ETH_TXQ_NUM	NR_CPUS

#define DPAA_BPS_NUM 3 /* number of bpools per interface */

/* More detailed FQ types - used for fine-grained WQ assignments */
enum dpaa_fq_type {
	FQ_TYPE_RX_DEFAULT = 1, /* Rx Default FQs */
	FQ_TYPE_RX_ERROR,	/* Rx Error FQs */
	FQ_TYPE_TX,		/* "Real" Tx FQs */
	FQ_TYPE_TX_CONFIRM,	/* Tx default Conf FQ (actually an Rx FQ) */
	FQ_TYPE_TX_CONF_MQ,	/* Tx conf FQs (one for each Tx FQ) */
	FQ_TYPE_TX_ERROR,	/* Tx Error FQs (these are actually Rx FQs) */
};

struct dpaa_fq {
	struct qman_fq fq_base;
	struct list_head list;
	struct net_device *net_dev;
	bool init;
	u32 fqid;
	u32 flags;
	u16 channel;
	u8 wq;
	enum dpaa_fq_type fq_type;
};

struct dpaa_fq_cbs {
	struct qman_fq rx_defq;
	struct qman_fq tx_defq;
	struct qman_fq rx_errq;
	struct qman_fq tx_errq;
	struct qman_fq egress_ern;
};

struct dpaa_bp {
	/* device used in the DMA mapping operations */
	struct device *dev;
	/* current number of buffers in the buffer pool alloted to each CPU */
	int __percpu *percpu_count;
	/* all buffers allocated for this pool have this raw size */
	size_t raw_size;
	/* all buffers in this pool have this same usable size */
	size_t size;
	/* the buffer pools are initialized with config_count buffers for each
	 * CPU; at runtime the number of buffers per CPU is constantly brought
	 * back to this level
	 */
	u16 config_count;
	u8 bpid;
	struct bman_pool *pool;
	/* bpool can be seeded before use by this cb */
	int (*seed_cb)(struct dpaa_bp *);
	/* bpool can be emptied before freeing by this cb */
	void (*free_buf_cb)(const struct dpaa_bp *, struct bm_buffer *);
	atomic_t refs;
};

struct dpaa_rx_errors {
	u64 dme;		/* DMA Error */
	u64 fpe;		/* Frame Physical Error */
	u64 fse;		/* Frame Size Error */
	u64 phe;		/* Header Error */
};

/* Counters for QMan ERN frames - one counter per rejection code */
struct dpaa_ern_cnt {
	u64 cg_tdrop;		/* Congestion group taildrop */
	u64 wred;		/* WRED congestion */
	u64 err_cond;		/* Error condition */
	u64 early_window;	/* Order restoration, frame too early */
	u64 late_window;	/* Order restoration, frame too late */
	u64 fq_tdrop;		/* FQ taildrop */
	u64 fq_retired;		/* FQ is retired */
	u64 orp_zero;		/* ORP disabled */
};

struct dpaa_napi_portal {
	struct napi_struct napi;
	struct qman_portal *p;
	bool down;
};

struct dpaa_percpu_priv {
	struct net_device *net_dev;
	struct dpaa_napi_portal np;
	u64 in_interrupt;
	u64 tx_confirm;
	/* fragmented (non-linear) skbuffs received from the stack */
	u64 tx_frag_skbuffs;
	struct rtnl_link_stats64 stats;
	struct dpaa_rx_errors rx_errors;
	struct dpaa_ern_cnt ern_cnt;
};

struct dpaa_buffer_layout {
	u16 priv_data_size;
};

struct dpaa_priv {
	struct dpaa_percpu_priv __percpu *percpu_priv;
	struct dpaa_bp *dpaa_bps[DPAA_BPS_NUM];
	/* Store here the needed Tx headroom for convenience and speed
	 * (even though it can be computed based on the fields of buf_layout)
	 */
	u16 tx_headroom;
	struct net_device *net_dev;
	struct mac_device *mac_dev;
	struct qman_fq *egress_fqs[DPAA_ETH_TXQ_NUM];
	struct qman_fq *conf_fqs[DPAA_ETH_TXQ_NUM];

	u16 channel;
	struct list_head dpaa_fq_list;

	u32 msg_enable;	/* net_device message level */

	struct {
		/* All egress queues to a given net device belong to one
		 * (and the same) congestion group.
		 */
		struct qman_cgr cgr;
		/* If congested, when it began. Used for performance stats. */
		u32 congestion_start_jiffies;
		/* Number of jiffies the Tx port was congested. */
		u32 congested_jiffies;
		/* Counter for the number of times the CGR
		 * entered congestion state
		 */
		u32 cgr_congested_count;
	} cgr_data;
	/* Use a per-port CGR for ingress traffic. */
	bool use_ingress_cgr;
	struct qman_cgr ingress_cgr;

	struct dpaa_buffer_layout buf_layout[2];
	u16 rx_headroom;
};

/* from dpaa_ethtool.c */
extern const struct ethtool_ops dpaa_ethtool_ops;

/* from dpaa_eth_sysfs.c */
void dpaa_eth_sysfs_remove(struct device *dev);
void dpaa_eth_sysfs_init(struct device *dev);
#endif	/* __DPAA_H */
