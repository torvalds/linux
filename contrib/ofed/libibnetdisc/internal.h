/*
 * Copyright (c) 2008 Lawrence Livermore National Laboratory
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

/** =========================================================================
 * Define the internal data structures.
 */

#ifndef _INTERNAL_H_
#define _INTERNAL_H_

#include <infiniband/ibnetdisc.h>
#include <complib/cl_qmap.h>

#define	IBND_DEBUG(fmt, ...) \
	if (ibdebug) { \
		printf("%s:%u; " fmt, __FILE__, __LINE__, ## __VA_ARGS__); \
	}
#define	IBND_ERROR(fmt, ...) \
		fprintf(stderr, "%s:%u; " fmt, __FILE__, __LINE__, ## __VA_ARGS__)

/* HASH table defines */
#define HASHGUID(guid) ((uint32_t)(((uint32_t)(guid) * 101) ^ ((uint32_t)((guid) >> 32) * 103)))

#define MAXHOPS         63

#define DEFAULT_MAX_SMP_ON_WIRE 2
#define DEFAULT_TIMEOUT 1000
#define DEFAULT_RETRIES 3

#define	GINT_TO_POINTER(x) ((void *)(uintptr_t)(x))

typedef struct GHashTable GHashTable;

#define	g_hash_table_new_full(...) GHashTableNew()
#define	g_hash_table_destroy(...) GHashTableDestroy(__VA_ARGS__)
#define	g_hash_table_insert(...) GHashTableInsert(__VA_ARGS__)
#define	g_hash_table_lookup(...) GHashTableLookup(__VA_ARGS__)

extern GHashTable *GHashTableNew(void);
extern void GHashTableDestroy(GHashTable *);
extern void GHashTableInsert(GHashTable *, void *key, void *value);
extern void *GHashTableLookup(GHashTable *, void *key);

typedef struct f_internal {
	ibnd_fabric_t fabric;
	GHashTable *lid2guid;
} f_internal_t;
f_internal_t *allocate_fabric_internal(void);
void create_lid2guid(f_internal_t *f_int);
void destroy_lid2guid(f_internal_t *f_int);
void add_to_portlid_hash(ibnd_port_t * port, GHashTable *htable);

typedef struct ibnd_scan {
	ib_portid_t selfportid;
	f_internal_t *f_int;
	struct ibnd_config *cfg;
	unsigned initial_hops;
} ibnd_scan_t;

typedef struct ibnd_smp ibnd_smp_t;
typedef struct smp_engine smp_engine_t;
typedef int (*smp_comp_cb_t) (smp_engine_t * engine, ibnd_smp_t * smp,
			      uint8_t * mad_resp, void *cb_data);
struct ibnd_smp {
	cl_map_item_t on_wire;
	struct ibnd_smp *qnext;
	smp_comp_cb_t cb;
	void *cb_data;
	ib_portid_t path;
	ib_rpc_t rpc;
};

struct smp_engine {
	int umad_fd;
	int smi_agent;
	int smi_dir_agent;
	ibnd_smp_t *smp_queue_head;
	ibnd_smp_t *smp_queue_tail;
	void *user_data;
	cl_qmap_t smps_on_wire;
	struct ibnd_config *cfg;
	unsigned total_smps;
};

int smp_engine_init(smp_engine_t * engine, char * ca_name, int ca_port,
		    void *user_data, ibnd_config_t *cfg);
int issue_smp(smp_engine_t * engine, ib_portid_t * portid,
	      unsigned attrid, unsigned mod, smp_comp_cb_t cb, void *cb_data);
int process_mads(smp_engine_t * engine);
void smp_engine_destroy(smp_engine_t * engine);

int add_to_nodeguid_hash(ibnd_node_t * node, ibnd_node_t * hash[]);

int add_to_portguid_hash(ibnd_port_t * port, ibnd_port_t * hash[]);

void add_to_type_list(ibnd_node_t * node, f_internal_t * fabric);

void destroy_node(ibnd_node_t * node);

#endif				/* _INTERNAL_H_ */
