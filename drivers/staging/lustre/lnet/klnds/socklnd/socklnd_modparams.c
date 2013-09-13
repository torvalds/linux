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
CFS_MODULE_PARM(sock_timeout, "i", int, 0644,
		"dead socket timeout (seconds)");

static int credits = 256;
CFS_MODULE_PARM(credits, "i", int, 0444,
		"# concurrent sends");

static int peer_credits = 8;
CFS_MODULE_PARM(peer_credits, "i", int, 0444,
		"# concurrent sends to 1 peer");

static int peer_buffer_credits = 0;
CFS_MODULE_PARM(peer_buffer_credits, "i", int, 0444,
		"# per-peer router buffer credits");

static int peer_timeout = 180;
CFS_MODULE_PARM(peer_timeout, "i", int, 0444,
		"Seconds without aliveness news to declare peer dead (<=0 to disable)");

/* Number of daemons in each thread pool which is percpt,
 * we will estimate reasonable value based on CPUs if it's not set. */
static unsigned int nscheds;
CFS_MODULE_PARM(nscheds, "i", int, 0444,
		"# scheduler daemons in each pool while starting");

static int nconnds = 4;
CFS_MODULE_PARM(nconnds, "i", int, 0444,
		"# connection daemons while starting");

static int nconnds_max = 64;
CFS_MODULE_PARM(nconnds_max, "i", int, 0444,
		"max # connection daemons");

static int min_reconnectms = 1000;
CFS_MODULE_PARM(min_reconnectms, "i", int, 0644,
		"min connection retry interval (mS)");

static int max_reconnectms = 60000;
CFS_MODULE_PARM(max_reconnectms, "i", int, 0644,
		"max connection retry interval (mS)");

# define DEFAULT_EAGER_ACK 0
static int eager_ack = DEFAULT_EAGER_ACK;
CFS_MODULE_PARM(eager_ack, "i", int, 0644,
		"send tcp ack packets eagerly");

static int typed_conns = 1;
CFS_MODULE_PARM(typed_conns, "i", int, 0444,
		"use different sockets for bulk");

static int min_bulk = (1<<10);
CFS_MODULE_PARM(min_bulk, "i", int, 0644,
		"smallest 'large' message");

# define DEFAULT_BUFFER_SIZE 0
static int tx_buffer_size = DEFAULT_BUFFER_SIZE;
CFS_MODULE_PARM(tx_buffer_size, "i", int, 0644,
		"socket tx buffer size (0 for system default)");

static int rx_buffer_size = DEFAULT_BUFFER_SIZE;
CFS_MODULE_PARM(rx_buffer_size, "i", int, 0644,
		"socket rx buffer size (0 for system default)");

static int nagle = 0;
CFS_MODULE_PARM(nagle, "i", int, 0644,
		"enable NAGLE?");

static int round_robin = 1;
CFS_MODULE_PARM(round_robin, "i", int, 0644,
		"Round robin for multiple interfaces");

static int keepalive = 30;
CFS_MODULE_PARM(keepalive, "i", int, 0644,
		"# seconds before send keepalive");

static int keepalive_idle = 30;
CFS_MODULE_PARM(keepalive_idle, "i", int, 0644,
		"# idle seconds before probe");

#define DEFAULT_KEEPALIVE_COUNT  5
static int keepalive_count = DEFAULT_KEEPALIVE_COUNT;
CFS_MODULE_PARM(keepalive_count, "i", int, 0644,
		"# missed probes == dead");

static int keepalive_intvl = 5;
CFS_MODULE_PARM(keepalive_intvl, "i", int, 0644,
		"seconds between probes");

static int enable_csum = 0;
CFS_MODULE_PARM(enable_csum, "i", int, 0644,
		"enable check sum");

static int inject_csum_error = 0;
CFS_MODULE_PARM(inject_csum_error, "i", int, 0644,
		"set non-zero to inject a checksum error");

static int nonblk_zcack = 1;
CFS_MODULE_PARM(nonblk_zcack, "i", int, 0644,
		"always send ZC-ACK on non-blocking connection");

static unsigned int zc_min_payload = (16 << 10);
CFS_MODULE_PARM(zc_min_payload, "i", int, 0644,
		"minimum payload size to zero copy");

static unsigned int zc_recv = 0;
CFS_MODULE_PARM(zc_recv, "i", int, 0644,
		"enable ZC recv for Chelsio driver");

static unsigned int zc_recv_min_nfrags = 16;
CFS_MODULE_PARM(zc_recv_min_nfrags, "i", int, 0644,
		"minimum # of fragments to enable ZC recv");


#if SOCKNAL_VERSION_DEBUG
static int protocol = 3;
CFS_MODULE_PARM(protocol, "i", int, 0644,
		"protocol version");
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

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM
	ksocknal_tunables.ksnd_sysctl	     =  NULL;
#endif

	if (*ksocknal_tunables.ksnd_zc_min_payload < (2 << 10))
		*ksocknal_tunables.ksnd_zc_min_payload = (2 << 10);

	/* initialize platform-sepcific tunables */
	return ksocknal_lib_tunables_init();
};

void ksocknal_tunables_fini(void)
{
	ksocknal_lib_tunables_fini();
}
