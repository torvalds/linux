/*
 * This file is part of the Chelsio T4 PCI-E SR-IOV Virtual Function Ethernet
 * driver for Linux.
 *
 * Copyright (c) 2009-2010 Chelsio Communications, Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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

#ifndef __T4VF_COMMON_H__
#define __T4VF_COMMON_H__

#include "../cxgb4/t4fw_api.h"

/*
 * The "len16" field of a Firmware Command Structure ...
 */
#define FW_LEN16(fw_struct) FW_CMD_LEN16(sizeof(fw_struct) / 16)

/*
 * Per-VF statistics.
 */
struct t4vf_port_stats {
	/*
	 * TX statistics.
	 */
	u64 tx_bcast_bytes;		/* broadcast */
	u64 tx_bcast_frames;
	u64 tx_mcast_bytes;		/* multicast */
	u64 tx_mcast_frames;
	u64 tx_ucast_bytes;		/* unicast */
	u64 tx_ucast_frames;
	u64 tx_drop_frames;		/* TX dropped frames */
	u64 tx_offload_bytes;		/* offload */
	u64 tx_offload_frames;

	/*
	 * RX statistics.
	 */
	u64 rx_bcast_bytes;		/* broadcast */
	u64 rx_bcast_frames;
	u64 rx_mcast_bytes;		/* multicast */
	u64 rx_mcast_frames;
	u64 rx_ucast_bytes;
	u64 rx_ucast_frames;		/* unicast */

	u64 rx_err_frames;		/* RX error frames */
};

/*
 * Per-"port" (Virtual Interface) link configuration ...
 */
struct link_config {
	unsigned int   supported;        /* link capabilities */
	unsigned int   advertising;      /* advertised capabilities */
	unsigned short requested_speed;  /* speed user has requested */
	unsigned short speed;            /* actual link speed */
	unsigned char  requested_fc;     /* flow control user has requested */
	unsigned char  fc;               /* actual link flow control */
	unsigned char  autoneg;          /* autonegotiating? */
	unsigned char  link_ok;          /* link up? */
};

enum {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

/*
 * General device parameters ...
 */
struct dev_params {
	u32 fwrev;			/* firmware version */
	u32 tprev;			/* TP Microcode Version */
};

/*
 * Scatter Gather Engine parameters.  These are almost all determined by the
 * Physical Function Driver.  We just need to grab them to see within which
 * environment we're playing ...
 */
struct sge_params {
	u32 sge_control;		/* padding, boundaries, lengths, etc. */
	u32 sge_host_page_size;		/* RDMA page sizes */
	u32 sge_queues_per_page;	/* RDMA queues/page */
	u32 sge_user_mode_limits;	/* limits for BAR2 user mode accesses */
	u32 sge_fl_buffer_size[16];	/* free list buffer sizes */
	u32 sge_ingress_rx_threshold;	/* RX counter interrupt threshold[4] */
	u32 sge_timer_value_0_and_1;	/* interrupt coalescing timer values */
	u32 sge_timer_value_2_and_3;
	u32 sge_timer_value_4_and_5;
};

/*
 * Vital Product Data parameters.
 */
struct vpd_params {
	u32 cclk;			/* Core Clock (KHz) */
};

/*
 * Global Receive Side Scaling (RSS) parameters in host-native format.
 */
struct rss_params {
	unsigned int mode;		/* RSS mode */
	union {
	    struct {
		unsigned int synmapen:1;	/* SYN Map Enable */
		unsigned int syn4tupenipv6:1;	/* enable hashing 4-tuple IPv6 SYNs */
		unsigned int syn2tupenipv6:1;	/* enable hashing 2-tuple IPv6 SYNs */
		unsigned int syn4tupenipv4:1;	/* enable hashing 4-tuple IPv4 SYNs */
		unsigned int syn2tupenipv4:1;	/* enable hashing 2-tuple IPv4 SYNs */
		unsigned int ofdmapen:1;	/* Offload Map Enable */
		unsigned int tnlmapen:1;	/* Tunnel Map Enable */
		unsigned int tnlalllookup:1;	/* Tunnel All Lookup */
		unsigned int hashtoeplitz:1;	/* use Toeplitz hash */
	    } basicvirtual;
	} u;
};

/*
 * Virtual Interface RSS Configuration in host-native format.
 */
union rss_vi_config {
    struct {
	u16 defaultq;			/* Ingress Queue ID for !tnlalllookup */
	unsigned int ip6fourtupen:1;	/* hash 4-tuple IPv6 ingress packets */
	unsigned int ip6twotupen:1;	/* hash 2-tuple IPv6 ingress packets */
	unsigned int ip4fourtupen:1;	/* hash 4-tuple IPv4 ingress packets */
	unsigned int ip4twotupen:1;	/* hash 2-tuple IPv4 ingress packets */
	int udpen;			/* hash 4-tuple UDP ingress packets */
    } basicvirtual;
};

/*
 * Maximum resources provisioned for a PCI VF.
 */
struct vf_resources {
	unsigned int nvi;		/* N virtual interfaces */
	unsigned int neq;		/* N egress Qs */
	unsigned int nethctrl;		/* N egress ETH or CTRL Qs */
	unsigned int niqflint;		/* N ingress Qs/w free list(s) & intr */
	unsigned int niq;		/* N ingress Qs */
	unsigned int tc;		/* PCI-E traffic class */
	unsigned int pmask;		/* port access rights mask */
	unsigned int nexactf;		/* N exact MPS filters */
	unsigned int r_caps;		/* read capabilities */
	unsigned int wx_caps;		/* write/execute capabilities */
};

/*
 * Per-"adapter" (Virtual Function) parameters.
 */
struct adapter_params {
	struct dev_params dev;		/* general device parameters */
	struct sge_params sge;		/* Scatter Gather Engine */
	struct vpd_params vpd;		/* Vital Product Data */
	struct rss_params rss;		/* Receive Side Scaling */
	struct vf_resources vfres;	/* Virtual Function Resource limits */
	u8 nports;			/* # of Ethernet "ports" */
};

#include "adapter.h"

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; iter++)

static inline bool is_10g_port(const struct link_config *lc)
{
	return (lc->supported & SUPPORTED_10000baseT_Full) != 0;
}

static inline unsigned int core_ticks_per_usec(const struct adapter *adapter)
{
	return adapter->params.vpd.cclk / 1000;
}

static inline unsigned int us_to_core_ticks(const struct adapter *adapter,
					    unsigned int us)
{
	return (us * adapter->params.vpd.cclk) / 1000;
}

static inline unsigned int core_ticks_to_us(const struct adapter *adapter,
					    unsigned int ticks)
{
	return (ticks * 1000) / adapter->params.vpd.cclk;
}

int t4vf_wr_mbox_core(struct adapter *, const void *, int, void *, bool);

static inline int t4vf_wr_mbox(struct adapter *adapter, const void *cmd,
			       int size, void *rpl)
{
	return t4vf_wr_mbox_core(adapter, cmd, size, rpl, true);
}

static inline int t4vf_wr_mbox_ns(struct adapter *adapter, const void *cmd,
				  int size, void *rpl)
{
	return t4vf_wr_mbox_core(adapter, cmd, size, rpl, false);
}

int __devinit t4vf_wait_dev_ready(struct adapter *);
int __devinit t4vf_port_init(struct adapter *, int);

int t4vf_fw_reset(struct adapter *);
int t4vf_query_params(struct adapter *, unsigned int, const u32 *, u32 *);
int t4vf_set_params(struct adapter *, unsigned int, const u32 *, const u32 *);

int t4vf_get_sge_params(struct adapter *);
int t4vf_get_vpd_params(struct adapter *);
int t4vf_get_dev_params(struct adapter *);
int t4vf_get_rss_glb_config(struct adapter *);
int t4vf_get_vfres(struct adapter *);

int t4vf_read_rss_vi_config(struct adapter *, unsigned int,
			    union rss_vi_config *);
int t4vf_write_rss_vi_config(struct adapter *, unsigned int,
			     union rss_vi_config *);
int t4vf_config_rss_range(struct adapter *, unsigned int, int, int,
			  const u16 *, int);

int t4vf_alloc_vi(struct adapter *, int);
int t4vf_free_vi(struct adapter *, int);
int t4vf_enable_vi(struct adapter *, unsigned int, bool, bool);
int t4vf_identify_port(struct adapter *, unsigned int, unsigned int);

int t4vf_set_rxmode(struct adapter *, unsigned int, int, int, int, int, int,
		    bool);
int t4vf_alloc_mac_filt(struct adapter *, unsigned int, bool, unsigned int,
			const u8 **, u16 *, u64 *, bool);
int t4vf_change_mac(struct adapter *, unsigned int, int, const u8 *, bool);
int t4vf_set_addr_hash(struct adapter *, unsigned int, bool, u64, bool);
int t4vf_get_port_stats(struct adapter *, int, struct t4vf_port_stats *);

int t4vf_iq_free(struct adapter *, unsigned int, unsigned int, unsigned int,
		 unsigned int);
int t4vf_eth_eq_free(struct adapter *, unsigned int);

int t4vf_handle_fw_rpl(struct adapter *, const __be64 *);

#endif /* __T4VF_COMMON_H__ */
