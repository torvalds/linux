/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/o2iblnd/o2iblnd_modparams.c
 *
 * Author: Eric Barton <eric@bartonsoftware.com>
 */

#include "o2iblnd.h"

static int service = 987;
CFS_MODULE_PARM(service, "i", int, 0444,
		"service number (within RDMA_PS_TCP)");

static int cksum = 0;
CFS_MODULE_PARM(cksum, "i", int, 0644,
		"set non-zero to enable message (not RDMA) checksums");

static int timeout = 50;
CFS_MODULE_PARM(timeout, "i", int, 0644,
		"timeout (seconds)");

/* Number of threads in each scheduler pool which is percpt,
 * we will estimate reasonable value based on CPUs if it's set to zero. */
static int nscheds;
CFS_MODULE_PARM(nscheds, "i", int, 0444,
		"number of threads in each scheduler pool");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int ntx = 512;
CFS_MODULE_PARM(ntx, "i", int, 0444,
		"# of message descriptors allocated for each pool");

/* NB: this value is shared by all CPTs */
static int credits = 256;
CFS_MODULE_PARM(credits, "i", int, 0444,
		"# concurrent sends");

static int peer_credits = 8;
CFS_MODULE_PARM(peer_credits, "i", int, 0444,
		"# concurrent sends to 1 peer");

static int peer_credits_hiw = 0;
CFS_MODULE_PARM(peer_credits_hiw, "i", int, 0444,
		"when eagerly to return credits");

static int peer_buffer_credits = 0;
CFS_MODULE_PARM(peer_buffer_credits, "i", int, 0444,
		"# per-peer router buffer credits");

static int peer_timeout = 180;
CFS_MODULE_PARM(peer_timeout, "i", int, 0444,
		"Seconds without aliveness news to declare peer dead (<=0 to disable)");

static char *ipif_name = "ib0";
CFS_MODULE_PARM(ipif_name, "s", charp, 0444,
		"IPoIB interface name");

static int retry_count = 5;
CFS_MODULE_PARM(retry_count, "i", int, 0644,
		"Retransmissions when no ACK received");

static int rnr_retry_count = 6;
CFS_MODULE_PARM(rnr_retry_count, "i", int, 0644,
		"RNR retransmissions");

static int keepalive = 100;
CFS_MODULE_PARM(keepalive, "i", int, 0644,
		"Idle time in seconds before sending a keepalive");

static int ib_mtu = 0;
CFS_MODULE_PARM(ib_mtu, "i", int, 0444,
		"IB MTU 256/512/1024/2048/4096");

static int concurrent_sends = 0;
CFS_MODULE_PARM(concurrent_sends, "i", int, 0444,
		"send work-queue sizing");

static int map_on_demand = 0;
CFS_MODULE_PARM(map_on_demand, "i", int, 0444,
		"map on demand");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int fmr_pool_size = 512;
CFS_MODULE_PARM(fmr_pool_size, "i", int, 0444,
		"size of fmr pool on each CPT (>= ntx / 4)");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int fmr_flush_trigger = 384;
CFS_MODULE_PARM(fmr_flush_trigger, "i", int, 0444,
		"# dirty FMRs that triggers pool flush");

static int fmr_cache = 1;
CFS_MODULE_PARM(fmr_cache, "i", int, 0444,
		"non-zero to enable FMR caching");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int pmr_pool_size = 512;
CFS_MODULE_PARM(pmr_pool_size, "i", int, 0444,
		"size of MR cache pmr pool on each CPT");

/*
 * 0: disable failover
 * 1: enable failover if necessary
 * 2: force to failover (for debug)
 */
static int dev_failover = 0;
CFS_MODULE_PARM(dev_failover, "i", int, 0444,
	       "HCA failover for bonding (0 off, 1 on, other values reserved)");


static int require_privileged_port = 0;
CFS_MODULE_PARM(require_privileged_port, "i", int, 0644,
		"require privileged port when accepting connection");

static int use_privileged_port = 1;
CFS_MODULE_PARM(use_privileged_port, "i", int, 0644,
		"use privileged port when initiating connection");

kib_tunables_t kiblnd_tunables = {
	.kib_dev_failover	   = &dev_failover,
	.kib_service		= &service,
	.kib_cksum		  = &cksum,
	.kib_timeout		= &timeout,
	.kib_keepalive	      = &keepalive,
	.kib_ntx		    = &ntx,
	.kib_credits		= &credits,
	.kib_peertxcredits	  = &peer_credits,
	.kib_peercredits_hiw	= &peer_credits_hiw,
	.kib_peerrtrcredits	 = &peer_buffer_credits,
	.kib_peertimeout	    = &peer_timeout,
	.kib_default_ipif	   = &ipif_name,
	.kib_retry_count	    = &retry_count,
	.kib_rnr_retry_count	= &rnr_retry_count,
	.kib_concurrent_sends       = &concurrent_sends,
	.kib_ib_mtu		 = &ib_mtu,
	.kib_map_on_demand	  = &map_on_demand,
	.kib_fmr_pool_size	  = &fmr_pool_size,
	.kib_fmr_flush_trigger      = &fmr_flush_trigger,
	.kib_fmr_cache	      = &fmr_cache,
	.kib_pmr_pool_size	  = &pmr_pool_size,
	.kib_require_priv_port      = &require_privileged_port,
	.kib_use_priv_port	    = &use_privileged_port,
	.kib_nscheds		    = &nscheds
};

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM

static char ipif_basename_space[32];


enum {
	O2IBLND_SERVICE  = 1,
	O2IBLND_CKSUM,
	O2IBLND_TIMEOUT,
	O2IBLND_NTX,
	O2IBLND_CREDITS,
	O2IBLND_PEER_TXCREDITS,
	O2IBLND_PEER_CREDITS_HIW,
	O2IBLND_PEER_RTRCREDITS,
	O2IBLND_PEER_TIMEOUT,
	O2IBLND_IPIF_BASENAME,
	O2IBLND_RETRY_COUNT,
	O2IBLND_RNR_RETRY_COUNT,
	O2IBLND_KEEPALIVE,
	O2IBLND_CONCURRENT_SENDS,
	O2IBLND_IB_MTU,
	O2IBLND_MAP_ON_DEMAND,
	O2IBLND_FMR_POOL_SIZE,
	O2IBLND_FMR_FLUSH_TRIGGER,
	O2IBLND_FMR_CACHE,
	O2IBLND_PMR_POOL_SIZE,
	O2IBLND_DEV_FAILOVER
};

static ctl_table_t kiblnd_ctl_table[] = {
	{
		.ctl_name = O2IBLND_SERVICE,
		.procname = "service",
		.data     = &service,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_CKSUM,
		.procname = "cksum",
		.data     = &cksum,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_TIMEOUT,
		.procname = "timeout",
		.data     = &timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_NTX,
		.procname = "ntx",
		.data     = &ntx,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_CREDITS,
		.procname = "credits",
		.data     = &credits,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_PEER_TXCREDITS,
		.procname = "peer_credits",
		.data     = &peer_credits,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_PEER_CREDITS_HIW,
		.procname = "peer_credits_hiw",
		.data     = &peer_credits_hiw,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_PEER_RTRCREDITS,
		.procname = "peer_buffer_credits",
		.data     = &peer_buffer_credits,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_PEER_TIMEOUT,
		.procname = "peer_timeout",
		.data     = &peer_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_IPIF_BASENAME,
		.procname = "ipif_name",
		.data     = ipif_basename_space,
		.maxlen   = sizeof(ipif_basename_space),
		.mode     = 0444,
		.proc_handler = &proc_dostring
	},
	{
		.ctl_name = O2IBLND_RETRY_COUNT,
		.procname = "retry_count",
		.data     = &retry_count,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_RNR_RETRY_COUNT,
		.procname = "rnr_retry_count",
		.data     = &rnr_retry_count,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_KEEPALIVE,
		.procname = "keepalive",
		.data     = &keepalive,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_CONCURRENT_SENDS,
		.procname = "concurrent_sends",
		.data     = &concurrent_sends,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_IB_MTU,
		.procname = "ib_mtu",
		.data     = &ib_mtu,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_MAP_ON_DEMAND,
		.procname = "map_on_demand",
		.data     = &map_on_demand,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},

	{
		.ctl_name = O2IBLND_FMR_POOL_SIZE,
		.procname = "fmr_pool_size",
		.data     = &fmr_pool_size,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_FMR_FLUSH_TRIGGER,
		.procname = "fmr_flush_trigger",
		.data     = &fmr_flush_trigger,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_FMR_CACHE,
		.procname = "fmr_cache",
		.data     = &fmr_cache,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_PMR_POOL_SIZE,
		.procname = "pmr_pool_size",
		.data     = &pmr_pool_size,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{
		.ctl_name = O2IBLND_DEV_FAILOVER,
		.procname = "dev_failover",
		.data     = &dev_failover,
		.maxlen   = sizeof(int),
		.mode     = 0444,
		.proc_handler = &proc_dointvec
	},
	{0}
};

static ctl_table_t kiblnd_top_ctl_table[] = {
	{
		.ctl_name = CTL_O2IBLND,
		.procname = "o2iblnd",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0555,
		.child    = kiblnd_ctl_table
	},
	{0}
};

void
kiblnd_initstrtunable(char *space, char *str, int size)
{
	strncpy(space, str, size);
	space[size-1] = 0;
}

void
kiblnd_sysctl_init (void)
{
	kiblnd_initstrtunable(ipif_basename_space, ipif_name,
			      sizeof(ipif_basename_space));

	kiblnd_tunables.kib_sysctl =
		cfs_register_sysctl_table(kiblnd_top_ctl_table, 0);

	if (kiblnd_tunables.kib_sysctl == NULL)
		CWARN("Can't setup /proc tunables\n");
}

void
kiblnd_sysctl_fini (void)
{
	if (kiblnd_tunables.kib_sysctl != NULL)
		unregister_sysctl_table(kiblnd_tunables.kib_sysctl);
}

#else

void
kiblnd_sysctl_init (void)
{
}

void
kiblnd_sysctl_fini (void)
{
}

#endif

int
kiblnd_tunables_init (void)
{
	if (kiblnd_translate_mtu(*kiblnd_tunables.kib_ib_mtu) < 0) {
		CERROR("Invalid ib_mtu %d, expected 256/512/1024/2048/4096\n",
		       *kiblnd_tunables.kib_ib_mtu);
		return -EINVAL;
	}

	if (*kiblnd_tunables.kib_peertxcredits < IBLND_CREDITS_DEFAULT)
		*kiblnd_tunables.kib_peertxcredits = IBLND_CREDITS_DEFAULT;

	if (*kiblnd_tunables.kib_peertxcredits > IBLND_CREDITS_MAX)
		*kiblnd_tunables.kib_peertxcredits = IBLND_CREDITS_MAX;

	if (*kiblnd_tunables.kib_peertxcredits > *kiblnd_tunables.kib_credits)
		*kiblnd_tunables.kib_peertxcredits = *kiblnd_tunables.kib_credits;

	if (*kiblnd_tunables.kib_peercredits_hiw < *kiblnd_tunables.kib_peertxcredits / 2)
		*kiblnd_tunables.kib_peercredits_hiw = *kiblnd_tunables.kib_peertxcredits / 2;

	if (*kiblnd_tunables.kib_peercredits_hiw >= *kiblnd_tunables.kib_peertxcredits)
		*kiblnd_tunables.kib_peercredits_hiw = *kiblnd_tunables.kib_peertxcredits - 1;

	if (*kiblnd_tunables.kib_map_on_demand < 0 ||
	    *kiblnd_tunables.kib_map_on_demand > IBLND_MAX_RDMA_FRAGS)
		*kiblnd_tunables.kib_map_on_demand = 0; /* disable map-on-demand */

	if (*kiblnd_tunables.kib_map_on_demand == 1)
		*kiblnd_tunables.kib_map_on_demand = 2; /* don't make sense to create map if only one fragment */

	if (*kiblnd_tunables.kib_concurrent_sends == 0) {
		if (*kiblnd_tunables.kib_map_on_demand > 0 &&
		    *kiblnd_tunables.kib_map_on_demand <= IBLND_MAX_RDMA_FRAGS / 8)
			*kiblnd_tunables.kib_concurrent_sends = (*kiblnd_tunables.kib_peertxcredits) * 2;
		else
			*kiblnd_tunables.kib_concurrent_sends = (*kiblnd_tunables.kib_peertxcredits);
	}

	if (*kiblnd_tunables.kib_concurrent_sends > *kiblnd_tunables.kib_peertxcredits * 2)
		*kiblnd_tunables.kib_concurrent_sends = *kiblnd_tunables.kib_peertxcredits * 2;

	if (*kiblnd_tunables.kib_concurrent_sends < *kiblnd_tunables.kib_peertxcredits / 2)
		*kiblnd_tunables.kib_concurrent_sends = *kiblnd_tunables.kib_peertxcredits / 2;

	if (*kiblnd_tunables.kib_concurrent_sends < *kiblnd_tunables.kib_peertxcredits) {
		CWARN("Concurrent sends %d is lower than message queue size: %d, "
		      "performance may drop slightly.\n",
		      *kiblnd_tunables.kib_concurrent_sends, *kiblnd_tunables.kib_peertxcredits);
	}

	kiblnd_sysctl_init();
	return 0;
}

void
kiblnd_tunables_fini (void)
{
	kiblnd_sysctl_fini();
}
