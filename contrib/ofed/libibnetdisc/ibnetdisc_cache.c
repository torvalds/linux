/*
 * Copyright (c) 2004-2007 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2007 Xsigo Systems Inc.  All rights reserved.
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

#if HAVE_CONFIG_H
#include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <infiniband/ibnetdisc.h>

#include "internal.h"
#include "chassis.h"

/* For this caching lib, we always cache little endian */

/* Cache format
 *
 * Bytes 1-4 - magic number
 * Bytes 5-8 - version number
 * Bytes 9-12 - node count
 * Bytes 13-16 - port count
 * Bytes 17-24 - "from node" guid
 * Bytes 25-28 - maxhops discovered
 * Bytes X-Y - nodes (variable length)
 * Bytes X-Y - ports (variable length)
 *
 * Nodes are cached as
 *
 * 2 bytes - smalid
 * 1 byte - smalmc
 * 1 byte - smaenhsp0 flag
 * IB_SMP_DATA_SIZE bytes - switchinfo
 * 8 bytes - guid
 * 1 byte - type
 * 1 byte - numports
 * IB_SMP_DATA_SIZE bytes - info
 * IB_SMP_DATA_SIZE bytes - nodedesc
 * 1 byte - number of ports stored
 * 8 bytes - portguid A
 * 1 byte - port num A
 * 8 bytes - portguid B
 * 1 byte - port num B
 * ... etc., depending on number of ports stored
 *
 * Ports are cached as
 *
 * 8 bytes - guid
 * 1 byte - portnum
 * 1 byte - external portnum
 * 2 bytes - base lid
 * 1 byte - lmc
 * IB_SMP_DATA_SIZE bytes - info
 * 8 bytes - node guid port "owned" by
 * 1 byte - flag indicating if remote port exists
 * 8 bytes - port guid remotely connected to
 * 1 byte - port num remotely connected to
 */

/* Structs that hold cache info temporarily before
 * the real structs can be reconstructed.
 */

typedef struct ibnd_port_cache_key {
	uint64_t guid;
	uint8_t portnum;
} ibnd_port_cache_key_t;

typedef struct ibnd_node_cache {
	ibnd_node_t *node;
	uint8_t ports_stored_count;
	ibnd_port_cache_key_t *port_cache_keys;
	struct ibnd_node_cache *next;
	struct ibnd_node_cache *htnext;
	int node_stored_to_fabric;
} ibnd_node_cache_t;

typedef struct ibnd_port_cache {
	ibnd_port_t *port;
	uint64_t node_guid;
	uint8_t remoteport_flag;
	ibnd_port_cache_key_t remoteport_cache_key;
	struct ibnd_port_cache *next;
	struct ibnd_port_cache *htnext;
	int port_stored_to_fabric;
} ibnd_port_cache_t;

typedef struct ibnd_fabric_cache {
	f_internal_t *f_int;
	uint64_t from_node_guid;
	ibnd_node_cache_t *nodes_cache;
	ibnd_port_cache_t *ports_cache;
	ibnd_node_cache_t *nodescachetbl[HTSZ];
	ibnd_port_cache_t *portscachetbl[HTSZ];
} ibnd_fabric_cache_t;

#define IBND_FABRIC_CACHE_BUFLEN  4096
#define IBND_FABRIC_CACHE_MAGIC   0x8FE7832B
#define IBND_FABRIC_CACHE_VERSION 0x00000001

#define IBND_FABRIC_CACHE_COUNT_OFFSET 8

#define IBND_FABRIC_CACHE_HEADER_LEN   (28)
#define IBND_NODE_CACHE_HEADER_LEN     (15 + IB_SMP_DATA_SIZE*3)
#define IBND_PORT_CACHE_KEY_LEN        (8 + 1)
#define IBND_PORT_CACHE_LEN            (31 + IB_SMP_DATA_SIZE)

static ssize_t ibnd_read(int fd, void *buf, size_t count)
{
	size_t count_done = 0;
	ssize_t ret;

	while ((count - count_done) > 0) {
		ret = read(fd, ((char *) buf) + count_done, count - count_done);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else {
				IBND_DEBUG("read: %s\n", strerror(errno));
				return -1;
			}
		}
		if (!ret)
			break;
		count_done += ret;
	}

	if (count_done != count) {
		IBND_DEBUG("read: read short\n");
		return -1;
	}

	return count_done;
}

static size_t _unmarshall8(uint8_t * inbuf, uint8_t * num)
{
	(*num) = inbuf[0];

	return (sizeof(*num));
}

static size_t _unmarshall16(uint8_t * inbuf, uint16_t * num)
{
	(*num) = ((uint16_t) inbuf[1] << 8) | inbuf[0];

	return (sizeof(*num));
}

static size_t _unmarshall32(uint8_t * inbuf, uint32_t * num)
{
	(*num) = (uint32_t) inbuf[0];
	(*num) |= ((uint32_t) inbuf[1] << 8);
	(*num) |= ((uint32_t) inbuf[2] << 16);
	(*num) |= ((uint32_t) inbuf[3] << 24);

	return (sizeof(*num));
}

static size_t _unmarshall64(uint8_t * inbuf, uint64_t * num)
{
	(*num) = (uint64_t) inbuf[0];
	(*num) |= ((uint64_t) inbuf[1] << 8);
	(*num) |= ((uint64_t) inbuf[2] << 16);
	(*num) |= ((uint64_t) inbuf[3] << 24);
	(*num) |= ((uint64_t) inbuf[4] << 32);
	(*num) |= ((uint64_t) inbuf[5] << 40);
	(*num) |= ((uint64_t) inbuf[6] << 48);
	(*num) |= ((uint64_t) inbuf[7] << 56);

	return (sizeof(*num));
}

static size_t _unmarshall_buf(const void *inbuf, void *outbuf, unsigned int len)
{
	memcpy(outbuf, inbuf, len);

	return len;
}

static int _load_header_info(int fd, ibnd_fabric_cache_t * fabric_cache,
			     unsigned int *node_count, unsigned int *port_count)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	uint32_t magic = 0;
	uint32_t version = 0;
	size_t offset = 0;
	uint32_t tmp32;

	if (ibnd_read(fd, buf, IBND_FABRIC_CACHE_HEADER_LEN) < 0)
		return -1;

	offset += _unmarshall32(buf + offset, &magic);

	if (magic != IBND_FABRIC_CACHE_MAGIC) {
		IBND_DEBUG("invalid fabric cache file\n");
		return -1;
	}

	offset += _unmarshall32(buf + offset, &version);

	if (version != IBND_FABRIC_CACHE_VERSION) {
		IBND_DEBUG("invalid fabric cache version\n");
		return -1;
	}

	offset += _unmarshall32(buf + offset, node_count);
	offset += _unmarshall32(buf + offset, port_count);

	offset += _unmarshall64(buf + offset, &fabric_cache->from_node_guid);
	offset += _unmarshall32(buf + offset, &tmp32);
	fabric_cache->f_int->fabric.maxhops_discovered = tmp32;

	return 0;
}

static void _destroy_ibnd_node_cache(ibnd_node_cache_t * node_cache)
{
	free(node_cache->port_cache_keys);
	if (!node_cache->node_stored_to_fabric && node_cache->node)
		destroy_node(node_cache->node);
	free(node_cache);
}

static void _destroy_ibnd_fabric_cache(ibnd_fabric_cache_t * fabric_cache)
{
	ibnd_node_cache_t *node_cache;
	ibnd_node_cache_t *node_cache_next;
	ibnd_port_cache_t *port_cache;
	ibnd_port_cache_t *port_cache_next;

	if (!fabric_cache)
		return;

	node_cache = fabric_cache->nodes_cache;
	while (node_cache) {
		node_cache_next = node_cache->next;

		_destroy_ibnd_node_cache(node_cache);

		node_cache = node_cache_next;
	}

	port_cache = fabric_cache->ports_cache;
	while (port_cache) {
		port_cache_next = port_cache->next;

		if (!port_cache->port_stored_to_fabric && port_cache->port)
			free(port_cache->port);
		free(port_cache);

		port_cache = port_cache_next;
	}

	free(fabric_cache);
}

static void store_node_cache(ibnd_node_cache_t * node_cache,
			     ibnd_fabric_cache_t * fabric_cache)
{
	int hash_indx = HASHGUID(node_cache->node->guid) % HTSZ;

	node_cache->next = fabric_cache->nodes_cache;
	fabric_cache->nodes_cache = node_cache;

	node_cache->htnext = fabric_cache->nodescachetbl[hash_indx];
	fabric_cache->nodescachetbl[hash_indx] = node_cache;
}

static int _load_node(int fd, ibnd_fabric_cache_t * fabric_cache)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	ibnd_node_cache_t *node_cache = NULL;
	ibnd_node_t *node = NULL;
	size_t offset = 0;
	uint8_t tmp8;

	node_cache = (ibnd_node_cache_t *) malloc(sizeof(ibnd_node_cache_t));
	if (!node_cache) {
		IBND_DEBUG("OOM: node_cache\n");
		return -1;
	}
	memset(node_cache, '\0', sizeof(ibnd_node_cache_t));

	node = (ibnd_node_t *) malloc(sizeof(ibnd_node_t));
	if (!node) {
		IBND_DEBUG("OOM: node\n");
		free(node_cache);
		return -1;
	}
	memset(node, '\0', sizeof(ibnd_node_t));

	node_cache->node = node;

	if (ibnd_read(fd, buf, IBND_NODE_CACHE_HEADER_LEN) < 0)
		goto cleanup;

	offset += _unmarshall16(buf + offset, &node->smalid);
	offset += _unmarshall8(buf + offset, &node->smalmc);
	offset += _unmarshall8(buf + offset, &tmp8);
	node->smaenhsp0 = tmp8;
	offset += _unmarshall_buf(buf + offset, node->switchinfo,
				  IB_SMP_DATA_SIZE);
	offset += _unmarshall64(buf + offset, &node->guid);
	offset += _unmarshall8(buf + offset, &tmp8);
	node->type = tmp8;
	offset += _unmarshall8(buf + offset, &tmp8);
	node->numports = tmp8;
	offset += _unmarshall_buf(buf + offset, node->info, IB_SMP_DATA_SIZE);
	offset += _unmarshall_buf(buf + offset, node->nodedesc,
				  IB_SMP_DATA_SIZE);

	offset += _unmarshall8(buf + offset, &node_cache->ports_stored_count);

	if (node_cache->ports_stored_count) {
		unsigned int tomalloc = 0;
		unsigned int toread = 0;
		unsigned int i;

		tomalloc =
		    sizeof(ibnd_port_cache_key_t) *
		    node_cache->ports_stored_count;

		toread =
		    IBND_PORT_CACHE_KEY_LEN * node_cache->ports_stored_count;

		node_cache->port_cache_keys =
		    (ibnd_port_cache_key_t *) malloc(tomalloc);
		if (!node_cache->port_cache_keys) {
			IBND_DEBUG("OOM: node_cache port_cache_keys\n");
			goto cleanup;
		}

		if (ibnd_read(fd, buf, toread) < 0)
			goto cleanup;

		offset = 0;

		for (i = 0; i < node_cache->ports_stored_count; i++) {
			offset +=
			    _unmarshall64(buf + offset,
					  &node_cache->port_cache_keys[i].guid);
			offset +=
			    _unmarshall8(buf + offset,
					 &node_cache->
					 port_cache_keys[i].portnum);
		}
	}

	store_node_cache(node_cache, fabric_cache);

	return 0;

cleanup:
	_destroy_ibnd_node_cache(node_cache);
	return -1;
}

static void store_port_cache(ibnd_port_cache_t * port_cache,
			     ibnd_fabric_cache_t * fabric_cache)
{
	int hash_indx = HASHGUID(port_cache->port->guid) % HTSZ;

	port_cache->next = fabric_cache->ports_cache;
	fabric_cache->ports_cache = port_cache;

	port_cache->htnext = fabric_cache->portscachetbl[hash_indx];
	fabric_cache->portscachetbl[hash_indx] = port_cache;
}

static int _load_port(int fd, ibnd_fabric_cache_t * fabric_cache)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	ibnd_port_cache_t *port_cache = NULL;
	ibnd_port_t *port = NULL;
	size_t offset = 0;
	uint8_t tmp8;

	port_cache = (ibnd_port_cache_t *) malloc(sizeof(ibnd_port_cache_t));
	if (!port_cache) {
		IBND_DEBUG("OOM: port_cache\n");
		return -1;
	}
	memset(port_cache, '\0', sizeof(ibnd_port_cache_t));

	port = (ibnd_port_t *) malloc(sizeof(ibnd_port_t));
	if (!port) {
		IBND_DEBUG("OOM: port\n");
		free(port_cache);
		return -1;
	}
	memset(port, '\0', sizeof(ibnd_port_t));

	port_cache->port = port;

	if (ibnd_read(fd, buf, IBND_PORT_CACHE_LEN) < 0)
		goto cleanup;

	offset += _unmarshall64(buf + offset, &port->guid);
	offset += _unmarshall8(buf + offset, &tmp8);
	port->portnum = tmp8;
	offset += _unmarshall8(buf + offset, &tmp8);
	port->ext_portnum = tmp8;
	offset += _unmarshall16(buf + offset, &port->base_lid);
	offset += _unmarshall8(buf + offset, &port->lmc);
	offset += _unmarshall_buf(buf + offset, port->info, IB_SMP_DATA_SIZE);
	offset += _unmarshall64(buf + offset, &port_cache->node_guid);
	offset += _unmarshall8(buf + offset, &port_cache->remoteport_flag);
	offset +=
	    _unmarshall64(buf + offset, &port_cache->remoteport_cache_key.guid);
	offset +=
	    _unmarshall8(buf + offset,
			 &port_cache->remoteport_cache_key.portnum);

	store_port_cache(port_cache, fabric_cache);

	return 0;

cleanup:
	free(port);
	free(port_cache);
	return -1;
}

static ibnd_port_cache_t *_find_port(ibnd_fabric_cache_t * fabric_cache,
				     ibnd_port_cache_key_t * port_cache_key)
{
	int hash_indx = HASHGUID(port_cache_key->guid) % HTSZ;
	ibnd_port_cache_t *port_cache;

	for (port_cache = fabric_cache->portscachetbl[hash_indx];
	     port_cache; port_cache = port_cache->htnext) {
		if (port_cache->port->guid == port_cache_key->guid
		    && port_cache->port->portnum == port_cache_key->portnum)
			return port_cache;
	}

	return NULL;
}

static ibnd_node_cache_t *_find_node(ibnd_fabric_cache_t * fabric_cache,
				     uint64_t guid)
{
	int hash_indx = HASHGUID(guid) % HTSZ;
	ibnd_node_cache_t *node_cache;

	for (node_cache = fabric_cache->nodescachetbl[hash_indx];
	     node_cache; node_cache = node_cache->htnext) {
		if (node_cache->node->guid == guid)
			return node_cache;
	}

	return NULL;
}

static int _fill_port(ibnd_fabric_cache_t * fabric_cache, ibnd_node_t * node,
		      ibnd_port_cache_key_t * port_cache_key)
{
	ibnd_port_cache_t *port_cache;

	if (!(port_cache = _find_port(fabric_cache, port_cache_key))) {
		IBND_DEBUG("Cache invalid: cannot find port\n");
		return -1;
	}

	if (port_cache->port_stored_to_fabric) {
		IBND_DEBUG("Cache invalid: duplicate port discovered\n");
		return -1;
	}

	node->ports[port_cache->port->portnum] = port_cache->port;
	port_cache->port_stored_to_fabric++;

	/* achu: needed if user wishes to re-cache a loaded fabric.
	 * Otherwise, mostly unnecessary to do this.
	 */
	int rc = add_to_portguid_hash(port_cache->port,
				      fabric_cache->f_int->fabric.portstbl);
	if (rc) {
		IBND_DEBUG("Error Occurred when trying"
			   " to insert new port guid 0x%016" PRIx64 " to DB\n",
			   port_cache->port->guid);
	}
	return 0;
}

static int _rebuild_nodes(ibnd_fabric_cache_t * fabric_cache)
{
	ibnd_node_cache_t *node_cache;
	ibnd_node_cache_t *node_cache_next;

	node_cache = fabric_cache->nodes_cache;
	while (node_cache) {
		ibnd_node_t *node;
		int i;

		node_cache_next = node_cache->next;

		node = node_cache->node;

		/* Insert node into appropriate data structures */

		node->next = fabric_cache->f_int->fabric.nodes;
		fabric_cache->f_int->fabric.nodes = node;

		int rc = add_to_nodeguid_hash(node_cache->node,
					      fabric_cache->
					      f_int->
					      fabric.nodestbl);
		if (rc) {
			IBND_DEBUG("Error Occurred when trying"
				   " to insert new node guid 0x%016" PRIx64 " to DB\n",
				   node_cache->node->guid);
		}

		add_to_type_list(node_cache->node, fabric_cache->f_int);

		node_cache->node_stored_to_fabric++;

		/* Rebuild node ports array */

		if (!(node->ports =
		      calloc(sizeof(*node->ports), node->numports + 1))) {
			IBND_DEBUG("OOM: node->ports\n");
			return -1;
		}

		for (i = 0; i < node_cache->ports_stored_count; i++) {
			if (_fill_port(fabric_cache, node,
				       &node_cache->port_cache_keys[i]) < 0)
				return -1;
		}

		node_cache = node_cache_next;
	}

	return 0;
}

static int _rebuild_ports(ibnd_fabric_cache_t * fabric_cache)
{
	ibnd_port_cache_t *port_cache;
	ibnd_port_cache_t *port_cache_next;

	port_cache = fabric_cache->ports_cache;
	while (port_cache) {
		ibnd_node_cache_t *node_cache;
		ibnd_port_cache_t *remoteport_cache;
		ibnd_port_t *port;

		port_cache_next = port_cache->next;

		port = port_cache->port;

		if (!(node_cache =
		      _find_node(fabric_cache, port_cache->node_guid))) {
			IBND_DEBUG("Cache invalid: cannot find node\n");
			return -1;
		}

		port->node = node_cache->node;

		if (port_cache->remoteport_flag) {
			if (!(remoteport_cache = _find_port(fabric_cache,
							    &port_cache->remoteport_cache_key)))
			{
				IBND_DEBUG
				    ("Cache invalid: cannot find remote port\n");
				return -1;
			}

			port->remoteport = remoteport_cache->port;
		} else
			port->remoteport = NULL;

		add_to_portlid_hash(port, fabric_cache->f_int->lid2guid);
		port_cache = port_cache_next;
	}

	return 0;
}

ibnd_fabric_t *ibnd_load_fabric(const char *file, unsigned int flags)
{
	unsigned int node_count = 0;
	unsigned int port_count = 0;
	ibnd_fabric_cache_t *fabric_cache = NULL;
	f_internal_t *f_int = NULL;
	ibnd_node_cache_t *node_cache = NULL;
	int fd = -1;
	unsigned int i;

	if (!file) {
		IBND_DEBUG("file parameter NULL\n");
		return NULL;
	}

	if ((fd = open(file, O_RDONLY)) < 0) {
		IBND_DEBUG("open: %s\n", strerror(errno));
		return NULL;
	}

	fabric_cache =
	    (ibnd_fabric_cache_t *) malloc(sizeof(ibnd_fabric_cache_t));
	if (!fabric_cache) {
		IBND_DEBUG("OOM: fabric_cache\n");
		goto cleanup;
	}
	memset(fabric_cache, '\0', sizeof(ibnd_fabric_cache_t));

	f_int = allocate_fabric_internal();
	if (!f_int) {
		IBND_DEBUG("OOM: fabric\n");
		goto cleanup;
	}

	fabric_cache->f_int = f_int;

	if (_load_header_info(fd, fabric_cache, &node_count, &port_count) < 0)
		goto cleanup;

	for (i = 0; i < node_count; i++) {
		if (_load_node(fd, fabric_cache) < 0)
			goto cleanup;
	}

	for (i = 0; i < port_count; i++) {
		if (_load_port(fd, fabric_cache) < 0)
			goto cleanup;
	}

	/* Special case - find from node */
	if (!(node_cache =
	      _find_node(fabric_cache, fabric_cache->from_node_guid))) {
		IBND_DEBUG("Cache invalid: cannot find from node\n");
		goto cleanup;
	}
	f_int->fabric.from_node = node_cache->node;

	if (_rebuild_nodes(fabric_cache) < 0)
		goto cleanup;

	if (_rebuild_ports(fabric_cache) < 0)
		goto cleanup;

	if (group_nodes(&f_int->fabric))
		goto cleanup;

	_destroy_ibnd_fabric_cache(fabric_cache);
	close(fd);
	return (ibnd_fabric_t *)&f_int->fabric;

cleanup:
	ibnd_destroy_fabric((ibnd_fabric_t *)f_int);
	_destroy_ibnd_fabric_cache(fabric_cache);
	close(fd);
	return NULL;
}

static ssize_t ibnd_write(int fd, const void *buf, size_t count)
{
	size_t count_done = 0;
	ssize_t ret;

	while ((count - count_done) > 0) {
		ret = write(fd, ((char *) buf) + count_done, count - count_done);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else {
				IBND_DEBUG("write: %s\n", strerror(errno));
				return -1;
			}
		}
		count_done += ret;
	}
	return count_done;
}

static size_t _marshall8(uint8_t * outbuf, uint8_t num)
{
	outbuf[0] = num;

	return (sizeof(num));
}

static size_t _marshall16(uint8_t * outbuf, uint16_t num)
{
	outbuf[0] = num & 0x00FF;
	outbuf[1] = (num & 0xFF00) >> 8;

	return (sizeof(num));
}

static size_t _marshall32(uint8_t * outbuf, uint32_t num)
{
	outbuf[0] = num & 0x000000FF;
	outbuf[1] = (num & 0x0000FF00) >> 8;
	outbuf[2] = (num & 0x00FF0000) >> 16;
	outbuf[3] = (num & 0xFF000000) >> 24;

	return (sizeof(num));
}

static size_t _marshall64(uint8_t * outbuf, uint64_t num)
{
	outbuf[0] = (uint8_t) num;
	outbuf[1] = (uint8_t) (num >> 8);
	outbuf[2] = (uint8_t) (num >> 16);
	outbuf[3] = (uint8_t) (num >> 24);
	outbuf[4] = (uint8_t) (num >> 32);
	outbuf[5] = (uint8_t) (num >> 40);
	outbuf[6] = (uint8_t) (num >> 48);
	outbuf[7] = (uint8_t) (num >> 56);

	return (sizeof(num));
}

static size_t _marshall_buf(void *outbuf, const void *inbuf, unsigned int len)
{
	memcpy(outbuf, inbuf, len);

	return len;
}

static int _cache_header_info(int fd, ibnd_fabric_t * fabric)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	size_t offset = 0;

	/* Store magic number, version, and other important info */
	/* For this caching lib, we always assume cached as little endian */

	offset += _marshall32(buf + offset, IBND_FABRIC_CACHE_MAGIC);
	offset += _marshall32(buf + offset, IBND_FABRIC_CACHE_VERSION);
	/* save space for node count */
	offset += _marshall32(buf + offset, 0);
	/* save space for port count */
	offset += _marshall32(buf + offset, 0);
	offset += _marshall64(buf + offset, fabric->from_node->guid);
	offset += _marshall32(buf + offset, fabric->maxhops_discovered);

	if (ibnd_write(fd, buf, offset) < 0)
		return -1;

	return 0;
}

static int _cache_header_counts(int fd, unsigned int node_count,
				unsigned int port_count)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	size_t offset = 0;

	offset += _marshall32(buf + offset, node_count);
	offset += _marshall32(buf + offset, port_count);

	if (lseek(fd, IBND_FABRIC_CACHE_COUNT_OFFSET, SEEK_SET) < 0) {
		IBND_DEBUG("lseek: %s\n", strerror(errno));
		return -1;
	}

	if (ibnd_write(fd, buf, offset) < 0)
		return -1;

	return 0;
}

static int _cache_node(int fd, ibnd_node_t * node)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	size_t offset = 0;
	size_t ports_stored_offset = 0;
	uint8_t ports_stored_count = 0;
	int i;

	offset += _marshall16(buf + offset, node->smalid);
	offset += _marshall8(buf + offset, node->smalmc);
	offset += _marshall8(buf + offset, (uint8_t) node->smaenhsp0);
	offset += _marshall_buf(buf + offset, node->switchinfo,
				IB_SMP_DATA_SIZE);
	offset += _marshall64(buf + offset, node->guid);
	offset += _marshall8(buf + offset, (uint8_t) node->type);
	offset += _marshall8(buf + offset, (uint8_t) node->numports);
	offset += _marshall_buf(buf + offset, node->info, IB_SMP_DATA_SIZE);
	offset += _marshall_buf(buf + offset, node->nodedesc, IB_SMP_DATA_SIZE);
	/* need to come back later and store number of stored ports
	 * because port entries can be NULL or (in the case of switches)
	 * there is an additional port 0 not accounted for in numports.
	 */
	ports_stored_offset = offset;
	offset += sizeof(uint8_t);

	for (i = 0; i <= node->numports; i++) {
		if (node->ports[i]) {
			offset += _marshall64(buf + offset,
					      node->ports[i]->guid);
			offset += _marshall8(buf + offset,
					     (uint8_t) node->ports[i]->portnum);
			ports_stored_count++;
		}
	}

	/* go back and store number of port keys stored */
	_marshall8(buf + ports_stored_offset, ports_stored_count);

	if (ibnd_write(fd, buf, offset) < 0)
		return -1;

	return 0;
}

static int _cache_port(int fd, ibnd_port_t * port)
{
	uint8_t buf[IBND_FABRIC_CACHE_BUFLEN];
	size_t offset = 0;

	offset += _marshall64(buf + offset, port->guid);
	offset += _marshall8(buf + offset, (uint8_t) port->portnum);
	offset += _marshall8(buf + offset, (uint8_t) port->ext_portnum);
	offset += _marshall16(buf + offset, port->base_lid);
	offset += _marshall8(buf + offset, port->lmc);
	offset += _marshall_buf(buf + offset, port->info, IB_SMP_DATA_SIZE);
	offset += _marshall64(buf + offset, port->node->guid);
	if (port->remoteport) {
		offset += _marshall8(buf + offset, 1);
		offset += _marshall64(buf + offset, port->remoteport->guid);
		offset += _marshall8(buf + offset, (uint8_t) port->remoteport->portnum);
	} else {
		offset += _marshall8(buf + offset, 0);
		offset += _marshall64(buf + offset, 0);
		offset += _marshall8(buf + offset, 0);
	}

	if (ibnd_write(fd, buf, offset) < 0)
		return -1;

	return 0;
}

int ibnd_cache_fabric(ibnd_fabric_t * fabric, const char *file,
		      unsigned int flags)
{
	struct stat statbuf;
	ibnd_node_t *node = NULL;
	ibnd_node_t *node_next = NULL;
	unsigned int node_count = 0;
	ibnd_port_t *port = NULL;
	ibnd_port_t *port_next = NULL;
	unsigned int port_count = 0;
	int fd;
	int i;

	if (!fabric) {
		IBND_DEBUG("fabric parameter NULL\n");
		return -1;
	}

	if (!file) {
		IBND_DEBUG("file parameter NULL\n");
		return -1;
	}

	if (!(flags & IBND_CACHE_FABRIC_FLAG_NO_OVERWRITE)) {
		if (!stat(file, &statbuf)) {
			if (unlink(file) < 0) {
				IBND_DEBUG("error removing '%s': %s\n",
					   file, strerror(errno));
				return -1;
			}
		}
	}
	else {
		if (!stat(file, &statbuf)) {
			IBND_DEBUG("file '%s' already exists\n", file);
			return -1;
		}
	}

	if ((fd = open(file, O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
		IBND_DEBUG("open: %s\n", strerror(errno));
		return -1;
	}

	if (_cache_header_info(fd, fabric) < 0)
		goto cleanup;

	node = fabric->nodes;
	while (node) {
		node_next = node->next;

		if (_cache_node(fd, node) < 0)
			goto cleanup;

		node_count++;
		node = node_next;
	}

	for (i = 0; i < HTSZ; i++) {
		port = fabric->portstbl[i];
		while (port) {
			port_next = port->htnext;

			if (_cache_port(fd, port) < 0)
				goto cleanup;

			port_count++;
			port = port_next;
		}
	}

	if (_cache_header_counts(fd, node_count, port_count) < 0)
		goto cleanup;

	if (close(fd) < 0) {
		IBND_DEBUG("close: %s\n", strerror(errno));
		goto cleanup;
	}

	return 0;

cleanup:
	unlink(file);
	close(fd);
	return -1;
}
