/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 *
 *   Author: Eric Barton <eric@bartonsoftware.com>
 *
 *   Portals is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Portals is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Portals; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "socklnd.h"

static int sock_timeout = 50;
module_param(sock_timeout, int, 0644);
MODULE_PARM_DESC(sock_timeout, "dead socket timeout (seconds)");

static int credits = 256;
module_param(credits, int, 0444);
MODULE_PARM_DESC(credits, "# concurrent sends");

static int peer_credits = 8;
module_param(peer_credits, int, 0444);
MODULE_PARM_DESC(peer_credits, "# concurrent sends to 1 peer");

static int peer_buffer_credits = 0;
module_param(peer_buffer_credits, int, 0444);
MODULE_PARM_DESC(peer_buffer_credits, "# per-peer router buffer credits");

static int peer_timeout = 180;
module_param(peer_timeout, int, 0444);
MODULE_PARM_DESC(peer_timeout, "Seconds without aliveness news to declare peer dead (<=0 to disable)");

/* Number of daemons in each thread pool which is percpt,
 * we will estimate reasonable value based on CPUs if it's not set. */
static unsigned int nscheds;
module_param(nscheds, int, 0444);
MODULE_PARM_DESC(nscheds, "# scheduler daemons in each pool while starting");

static int nconnds = 4;
module_param(nconnds, int, 0444);
MODULE_PARM_DESC(nconnds, "# connection daemons while starting");

static int nconnds_max = 64;
module_param(nconnds_max, int, 0444);
MODULE_PARM_DESC(nconnds_max, "max # connection daemons");

static int min_reconnectms = 1000;
module_param(min_reconnectms, int, 0644);
MODULE_PARM_DESC(min_reconnectms, "min connection retry interval (mS)");

static int max_reconnectms = 60000;
module_param(max_reconnectms, int, 0644);
MODULE_PARM_DESC(max_reconnectms, "max connection retry interval (mS)");

# define DEFAULT_EAGER_ACK 0
static int eager_ack = DEFAULT_EAGER_ACK;
module_param(eager_ack, int, 0644);
MODULE_PARM_DESC(eager_ack, "send tcp ack packets eagerly");

static int typed_conns = 1;
module_param(typed_conns, int, 0444);
MODULE_PARM_DESC(typed_conns, "use different sockets for bulk");

static int min_bulk = (1<<10);
module_param(min_bulk, int, 0644);
MODULE_PARM_DESC(min_bulk, "smallest 'large' message");

# define DEFAULT_BUFFER_SIZE 0
static int tx_buffer_size = DEFAULT_BUFFER_SIZE;
module_param(tx_buffer_size, int, 0644);
MODULE_PARM_DESC(tx_buffer_size, "socket tx buffer size (0 for system default)");

static int rx_buffer_size = DEFAULT_BUFFER_SIZE;
module_param(rx_buffer_size, int, 0644);
MODULE_PARM_DESC(rx_buffer_size, "socket rx buffer size (0 for system default)");

static int nagle = 0;
module_param(nagle, int, 0644);
MODULE_PARM_DESC(nagle, "enable NAGLE?");

static int round_robin = 1;
module_param(round_robin, int, 0644);
MODULE_PARM_DESC(round_robin, "Round robin for multiple interfaces");

static int keepalive = 30;
module_param(keepalive, int, 0644);
MODULE_PARM_DESC(keepalive, "# seconds before send keepalive");

static int keepalive_idle = 30;
module_param(keepalive_idle, int, 0644);
MODULE_PARM_DESC(keepalive_idle, "# idle seconds before probe");

#define DEFAULT_KEEPALIVE_COUNT  5
static int keepalive_count = DEFAULT_KEEPALIVE_COUNT;
module_param(keepalive_count, int, 0644);
MODULE_PARM_DESC(keepalive_count, "# missed probes == dead");

static int keepalive_intvl = 5;
module_param(keepalive_intvl, int, 0644);
MODULE_PARM_DESC(keepalive_intvl, "seconds between probes");

static int enable_csum = 0;
module_param(enable_csum, int, 0644);
MODULE_PARM_DESC(enable_csum, "enable check sum");

static int inject_csum_error = 0;
module_param(inject_csum_error, int, 0644);
MODULE_PARM_DESC(inject_csum_error, "set non-zero to inject a checksum error");

static int nonblk_zcack = 1;
module_param(nonblk_zcack, int, 0644);
MODULE_PARM_DESC(nonblk_zcack, "always send ZC-ACK on non-blocking connection");

static unsigned int zc_min_payload = (16 << 10);
module_param(zc_min_payload, int, 0644);
MODULE_PARM_DESC(zc_min_payload, "minimum payload size to zero copy");

static unsigned int zc_recv = 0;
module_param(zc_recv, int, 0644);
MODULE_PARM_DESC(zc_recv, "enable ZC recv for Chelsio driver");

static unsigned int zc_recv_min_nfrags = 16;
module_param(zc_recv_min_nfrags, int, 0644);
MODULE_PARM_DESC(zc_recv_min_nfrags, "minimum # of fragments to enable ZC recv");


#if SOCKNAL_VERSION_DEBUG
static int protocol = 3;
module_param(protocol, int, 0644);
MODULE_PARM_DESC(protocol, "protocol version");
#endif

ksock_tunables_t ksocknal_tunables;

int ksocknal_tunables_init(void)
{

	/* initialize ksocknal_tunables structure */
	ksocknal_tunables.ksnd_timeout	    = &sock_timeout;
	ksocknal_tunables.ksnd_nscheds		  = &nscheds;
	ksocknal_tunables.ksnd_nconnds	    = &nconnds;
	ksocknal_tunables.ksnd_nconnds_max	= &nconnds_max;
	ksocknal_tunables.ksnd_min_reconnectms    = &min_reconnectms;
	ksocknal_tunables.ksnd_max_reconnectms    = &max_reconnectms;
	ksocknal_tunables.ksnd_eager_ack	  = &eager_ack;
	ksocknal_tunables.ksnd_typed_conns	= &typed_conns;
	ksocknal_tunables.ksnd_min_bulk	   = &min_bulk;
	ksocknal_tunables.ksnd_tx_buffer_size     = &tx_buffer_size;
	ksocknal_tunables.ksnd_rx_buffer_size     = &rx_buffer_size;
	ksocknal_tunables.ksnd_nagle	      = &nagle;
	ksocknal_tunables.ksnd_round_robin	= &round_robin;
	ksocknal_tunables.ksnd_keepalive	  = &keepalive;
	ksocknal_tunables.ksnd_keepalive_idle     = &keepalive_idle;
	ksocknal_tunables.ksnd_keepalive_count    = &keepalive_count;
	ksocknal_tunables.ksnd_keepalive_intvl    = &keepalive_intvl;
	ksocknal_tunables.ksnd_credits	    = &credits;
	ksocknal_tunables.ksnd_peertxcredits      = &peer_credits;
	ksocknal_tunables.ksnd_peerrtrcredits     = &peer_buffer_credits;
	ksocknal_tunables.ksnd_peertimeout	= &peer_timeout;
	ksocknal_tunables.ksnd_enable_csum	= &enable_csum;
	ksocknal_tunables.ksnd_inject_csum_error  = &inject_csum_error;
	ksocknal_tunables.ksnd_nonblk_zcack       = &nonblk_zcack;
	ksocknal_tunables.ksnd_zc_min_payload     = &zc_min_payload;
	ksocknal_tunables.ksnd_zc_recv	    = &zc_recv;
	ksocknal_tunables.ksnd_zc_recv_min_nfrags = &zc_recv_min_nfrags;



#if SOCKNAL_VERSION_DEBUG
	ksocknal_tunables.ksnd_protocol	   = &protocol;
#endif

	if (*ksocknal_tunables.ksnd_zc_min_payload < (2 << 10))
		*ksocknal_tunables.ksnd_zc_min_payload = (2 << 10);

	return 0;
};
