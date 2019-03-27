/*
 * Copyright (c) 2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2008 Lawrence Livermore National Lab.  All rights reserved.
 * Copyright (c) 2010-2011 Mellanox Technologies LTD.  All rights reserved.
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
 *
 */

#ifndef _IBNETDISC_H_
#define _IBNETDISC_H_

#include <stdio.h>
#include <infiniband/mad.h>
#include <iba/ib_types.h>

#include <infiniband/ibnetdisc_osd.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ibnd_chassis;		/* forward declare */
struct ibnd_port;		/* forward declare */

#define CHASSIS_TYPE_SIZE 20

/** =========================================================================
 * Node
 */
typedef struct ibnd_node {
	struct ibnd_node *next;	/* all node list in fabric */

	ib_portid_t path_portid;	/* path from "from_node" */
					/* NOTE: this is not valid on a fabric
					 * read from a cache file */
	uint16_t smalid;
	uint8_t smalmc;

	/* quick cache of switchinfo below */
	int smaenhsp0;
	/* use libibmad decoder functions for switchinfo */
	uint8_t switchinfo[IB_SMP_DATA_SIZE];

	/* quick cache of info below */
	uint64_t guid;
	int type;
	int numports;
	/* use libibmad decoder functions for info */
	uint8_t info[IB_SMP_DATA_SIZE];

	char nodedesc[IB_SMP_DATA_SIZE];

	struct ibnd_port **ports;	/* array of ports, indexed by port number
					   ports[1] == port 1,
					   ports[2] == port 2,
					   etc...
					   Any port in the array MAY BE NULL!
					   Most notable is non-switches have no
					   port 0 therefore node.ports[0] == NULL
					   for those nodes */

	/* chassis info */
	struct ibnd_node *next_chassis_node;	/* next node in ibnd_chassis_t->nodes */
	struct ibnd_chassis *chassis;	/* if != NULL the chassis this node belongs to */
	unsigned char ch_type;
	char ch_type_str[CHASSIS_TYPE_SIZE];
	unsigned char ch_anafanum;
	unsigned char ch_slotnum;
	unsigned char ch_slot;

	/* internal use only */
	unsigned char ch_found;
	struct ibnd_node *htnext;	/* hash table list */
	struct ibnd_node *type_next;	/* next based on type */
} ibnd_node_t;

/** =========================================================================
 * Port
 */
typedef struct ibnd_port {
	uint64_t guid;
	int portnum;
	int ext_portnum;	/* optional if != 0 external port num */
	ibnd_node_t *node;	/* node this port belongs to */
	struct ibnd_port *remoteport;	/* null if SMA, or does not exist */
	/* quick cache of info below */
	uint16_t base_lid;
	uint8_t lmc;
	/* use libibmad decoder functions for info */
	uint8_t info[IB_SMP_DATA_SIZE];
	uint8_t ext_info[IB_SMP_DATA_SIZE];

	/* internal use only */
	struct ibnd_port *htnext;
} ibnd_port_t;

/** =========================================================================
 * Chassis
 */
typedef struct ibnd_chassis {
	struct ibnd_chassis *next;
	uint64_t chassisguid;
	unsigned char chassisnum;

	/* generic grouping by SystemImageGUID */
	unsigned char nodecount;
	ibnd_node_t *nodes;

	/* specific to voltaire type nodes */
#define SPINES_MAX_NUM 18
#define LINES_MAX_NUM 36
	ibnd_node_t *spinenode[SPINES_MAX_NUM + 1];
	ibnd_node_t *linenode[LINES_MAX_NUM + 1];
} ibnd_chassis_t;

#define HTSZ 137

/* define config flags */
#define IBND_CONFIG_MLX_EPI (1 << 0)

typedef struct ibnd_config {
	unsigned max_smps;
	unsigned show_progress;
	unsigned max_hops;
	unsigned debug;
	unsigned timeout_ms;
	unsigned retries;
	uint32_t flags;
	uint64_t mkey;
	uint8_t pad[44];
} ibnd_config_t;

/** =========================================================================
 * Fabric
 * Main fabric object which is returned and represents the data discovered
 */
typedef struct ibnd_fabric {
	/* the node the discover was initiated from
	 * "from" parameter in ibnd_discover_fabric
	 * or by default the node you ar running on
	 */
	ibnd_node_t *from_node;
	int from_portnum;

	/* NULL term list of all nodes in the fabric */
	ibnd_node_t *nodes;
	/* NULL terminated list of all chassis found in the fabric */
	ibnd_chassis_t *chassis;
	unsigned maxhops_discovered;
	unsigned total_mads_used;

	/* internal use only */
	ibnd_node_t *nodestbl[HTSZ];
	ibnd_port_t *portstbl[HTSZ];
	ibnd_node_t *switches;
	ibnd_node_t *ch_adapters;
	ibnd_node_t *routers;
} ibnd_fabric_t;

/** =========================================================================
 * Initialization (fabric operations)
 */

IBND_EXPORT ibnd_fabric_t *ibnd_discover_fabric(char * ca_name,
					       int ca_port,
					       ib_portid_t * from,
					       struct ibnd_config *config);
	/**
	 * ca_name: (optional) name of the CA to use
	 * ca_port: (optional) CA port to use
	 * from: (optional) specify the node to start scanning from.
	 *       If NULL start from the CA/CA port specified
	 * config: (optional) additional config options for the scan
	 */
IBND_EXPORT void ibnd_destroy_fabric(ibnd_fabric_t * fabric);

IBND_EXPORT ibnd_fabric_t *ibnd_load_fabric(const char *file,
					   unsigned int flags);

IBND_EXPORT int ibnd_cache_fabric(ibnd_fabric_t * fabric, const char *file,
				 unsigned int flags);

#define IBND_CACHE_FABRIC_FLAG_DEFAULT      0x0000
#define IBND_CACHE_FABRIC_FLAG_NO_OVERWRITE 0x0001

/** =========================================================================
 * Node operations
 */
IBND_EXPORT ibnd_node_t *ibnd_find_node_guid(ibnd_fabric_t * fabric,
					    uint64_t guid);
IBND_EXPORT ibnd_node_t *ibnd_find_node_dr(ibnd_fabric_t * fabric, char *dr_str);

typedef void (*ibnd_iter_node_func_t) (ibnd_node_t * node, void *user_data);
IBND_EXPORT void ibnd_iter_nodes(ibnd_fabric_t * fabric,
				ibnd_iter_node_func_t func, void *user_data);
IBND_EXPORT void ibnd_iter_nodes_type(ibnd_fabric_t * fabric,
				     ibnd_iter_node_func_t func,
				     int node_type, void *user_data);

/** =========================================================================
 * Port operations
 */
IBND_EXPORT ibnd_port_t *ibnd_find_port_guid(ibnd_fabric_t * fabric,
					uint64_t guid);
IBND_EXPORT ibnd_port_t *ibnd_find_port_dr(ibnd_fabric_t * fabric,
					char *dr_str);
IBND_EXPORT ibnd_port_t *ibnd_find_port_lid(ibnd_fabric_t * fabric,
					    uint16_t lid);

typedef void (*ibnd_iter_port_func_t) (ibnd_port_t * port, void *user_data);
IBND_EXPORT void ibnd_iter_ports(ibnd_fabric_t * fabric,
				ibnd_iter_port_func_t func, void *user_data);

/** =========================================================================
 * Chassis queries
 */
IBND_EXPORT uint64_t ibnd_get_chassis_guid(ibnd_fabric_t * fabric,
					  unsigned char chassisnum);
IBND_EXPORT char *ibnd_get_chassis_type(ibnd_node_t * node);
IBND_EXPORT char *ibnd_get_chassis_slot_str(ibnd_node_t * node,
					   char *str, size_t size);

IBND_EXPORT int ibnd_is_xsigo_guid(uint64_t guid);
IBND_EXPORT int ibnd_is_xsigo_tca(uint64_t guid);
IBND_EXPORT int ibnd_is_xsigo_hca(uint64_t guid);

#ifdef __cplusplus
}
#endif

#endif				/* _IBNETDISC_H_ */
