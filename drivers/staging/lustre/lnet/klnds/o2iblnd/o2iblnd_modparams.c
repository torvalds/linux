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
module_param(service, int, 0444);
MODULE_PARM_DESC(service, "service number (within RDMA_PS_TCP)");

static int cksum;
module_param(cksum, int, 0644);
MODULE_PARM_DESC(cksum, "set non-zero to enable message (not RDMA) checksums");

static int timeout = 50;
module_param(timeout, int, 0644);
MODULE_PARM_DESC(timeout, "timeout (seconds)");

/* Number of threads in each scheduler pool which is percpt,
 * we will estimate reasonable value based on CPUs if it's set to zero. */
static int nscheds;
module_param(nscheds, int, 0444);
MODULE_PARM_DESC(nscheds, "number of threads in each scheduler pool");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int ntx = 512;
module_param(ntx, int, 0444);
MODULE_PARM_DESC(ntx, "# of message descriptors allocated for each pool");

/* NB: this value is shared by all CPTs */
static int credits = 256;
module_param(credits, int, 0444);
MODULE_PARM_DESC(credits, "# concurrent sends");

static int peer_credits = 8;
module_param(peer_credits, int, 0444);
MODULE_PARM_DESC(peer_credits, "# concurrent sends to 1 peer");

static int peer_credits_hiw;
module_param(peer_credits_hiw, int, 0444);
MODULE_PARM_DESC(peer_credits_hiw, "when eagerly to return credits");

static int peer_buffer_credits;
module_param(peer_buffer_credits, int, 0444);
MODULE_PARM_DESC(peer_buffer_credits, "# per-peer router buffer credits");

static int peer_timeout = 180;
module_param(peer_timeout, int, 0444);
MODULE_PARM_DESC(peer_timeout, "Seconds without aliveness news to declare peer dead (<=0 to disable)");

static char *ipif_name = "ib0";
module_param(ipif_name, charp, 0444);
MODULE_PARM_DESC(ipif_name, "IPoIB interface name");

static int retry_count = 5;
module_param(retry_count, int, 0644);
MODULE_PARM_DESC(retry_count, "Retransmissions when no ACK received");

static int rnr_retry_count = 6;
module_param(rnr_retry_count, int, 0644);
MODULE_PARM_DESC(rnr_retry_count, "RNR retransmissions");

static int keepalive = 100;
module_param(keepalive, int, 0644);
MODULE_PARM_DESC(keepalive, "Idle time in seconds before sending a keepalive");

static int ib_mtu;
module_param(ib_mtu, int, 0444);
MODULE_PARM_DESC(ib_mtu, "IB MTU 256/512/1024/2048/4096");

static int concurrent_sends;
module_param(concurrent_sends, int, 0444);
MODULE_PARM_DESC(concurrent_sends, "send work-queue sizing");

static int map_on_demand;
module_param(map_on_demand, int, 0444);
MODULE_PARM_DESC(map_on_demand, "map on demand");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int fmr_pool_size = 512;
module_param(fmr_pool_size, int, 0444);
MODULE_PARM_DESC(fmr_pool_size, "size of fmr pool on each CPT (>= ntx / 4)");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int fmr_flush_trigger = 384;
module_param(fmr_flush_trigger, int, 0444);
MODULE_PARM_DESC(fmr_flush_trigger, "# dirty FMRs that triggers pool flush");

static int fmr_cache = 1;
module_param(fmr_cache, int, 0444);
MODULE_PARM_DESC(fmr_cache, "non-zero to enable FMR caching");

/* NB: this value is shared by all CPTs, it can grow at runtime */
static int pmr_pool_size = 512;
module_param(pmr_pool_size, int, 0444);
MODULE_PARM_DESC(pmr_pool_size, "size of MR cache pmr pool on each CPT");

/*
 * 0: disable failover
 * 1: enable failover if necessary
 * 2: force to failover (for debug)
 */
static int dev_failover;
module_param(dev_failover, int, 0444);
MODULE_PARM_DESC(dev_failover, "HCA failover for bonding (0 off, 1 on, other values reserved)");


static int require_privileged_port;
module_param(require_privileged_port, int, 0644);
MODULE_PARM_DESC(require_privileged_port, "require privileged port when accepting connection");

static int use_privileged_port = 1;
module_param(use_privileged_port, int, 0644);
MODULE_PARM_DESC(use_privileged_port, "use privileged port when initiating connection");

kib_tunables_t kiblnd_tunables = {
	.kib_dev_failover      = &dev_failover,
	.kib_service           = &service,
	.kib_cksum             = &cksum,
	.kib_timeout           = &timeout,
	.kib_keepalive         = &keepalive,
	.kib_ntx               = &ntx,
	.kib_credits           = &credits,
	.kib_peertxcredits     = &peer_credits,
	.kib_peercredits_hiw   = &peer_credits_hiw,
	.kib_peerrtrcredits    = &peer_buffer_credits,
	.kib_peertimeout       = &peer_timeout,
	.kib_default_ipif      = &ipif_name,
	.kib_retry_count       = &retry_count,
	.kib_rnr_retry_count   = &rnr_retry_count,
	.kib_concurrent_sends  = &concurrent_sends,
	.kib_ib_mtu            = &ib_mtu,
	.kib_map_on_demand     = &map_on_demand,
	.kib_fmr_pool_size     = &fmr_pool_size,
	.kib_fmr_flush_trigger = &fmr_flush_trigger,
	.kib_fmr_cache         = &fmr_cache,
	.kib_pmr_pool_size     = &pmr_pool_size,
	.kib_require_priv_port = &require_privileged_port,
	.kib_use_priv_port     = &use_privileged_port,
	.kib_nscheds           = &nscheds
};

int
kiblnd_tunables_init(void)
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
		CWARN("Concurrent sends %d is lower than message queue size: %d, performance may drop slightly.\n",
		      *kiblnd_tunables.kib_concurrent_sends, *kiblnd_tunables.kib_peertxcredits);
	}

	return 0;
}
