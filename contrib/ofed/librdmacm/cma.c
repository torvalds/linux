/*
 * Copyright (c) 2005-2014 Intel Corporation.  All rights reserved.
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <poll.h>
#include <unistd.h>
#include <pthread.h>
#include <infiniband/endian.h>
#include <stddef.h>
#include <netdb.h>
#include <syslog.h>
#include <limits.h>

#include "cma.h"
#include "indexer.h"
#include <infiniband/driver.h>
#include <infiniband/marshall.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_cma_abi.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/ib.h>

#define CMA_INIT_CMD(req, req_size, op)		\
do {						\
	memset(req, 0, req_size);		\
	(req)->cmd = UCMA_CMD_##op;		\
	(req)->in  = req_size - sizeof(struct ucma_abi_cmd_hdr); \
} while (0)

#define CMA_INIT_CMD_RESP(req, req_size, op, resp, resp_size) \
do {						\
	CMA_INIT_CMD(req, req_size, op);	\
	(req)->out = resp_size;			\
	(req)->response = (uintptr_t) (resp);	\
} while (0)

struct cma_port {
	uint8_t			link_layer;
};

struct cma_device {
	struct ibv_context *verbs;
	struct ibv_pd	   *pd;
	struct ibv_xrcd    *xrcd;
	struct cma_port    *port;
	__be64		    guid;
	int		    port_cnt;
	int		    refcnt;
	int		    max_qpsize;
	uint8_t		    max_initiator_depth;
	uint8_t		    max_responder_resources;
};

struct cma_id_private {
	struct rdma_cm_id	id;
	struct cma_device	*cma_dev;
	void			*connect;
	size_t			connect_len;
	int			events_completed;
	int			connect_error;
	int			sync;
	pthread_cond_t		cond;
	pthread_mutex_t		mut;
	uint32_t		handle;
	struct cma_multicast	*mc_list;
	struct ibv_qp_init_attr	*qp_init_attr;
	uint8_t			initiator_depth;
	uint8_t			responder_resources;
};

struct cma_multicast {
	struct cma_multicast  *next;
	struct cma_id_private *id_priv;
	void		*context;
	int		events_completed;
	pthread_cond_t	cond;
	uint32_t	handle;
	union ibv_gid	mgid;
	uint16_t	mlid;
	struct sockaddr_storage addr;
};

struct cma_event {
	struct rdma_cm_event	event;
	uint8_t			private_data[RDMA_MAX_PRIVATE_DATA];
	struct cma_id_private	*id_priv;
	struct cma_multicast	*mc;
};

static struct cma_device *cma_dev_array;
static int cma_dev_cnt;
static int cma_init_cnt;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static int abi_ver = RDMA_USER_CM_MAX_ABI_VERSION;
int af_ib_support;
static struct index_map ucma_idm;
static fastlock_t idm_lock;

static int check_abi_version(void)
{
	char value[8];

	if ((ibv_read_sysfs_file(ibv_get_sysfs_path(),
				 "class/misc/rdma_cm/abi_version",
				 value, sizeof value) < 0) &&
	    (ibv_read_sysfs_file(ibv_get_sysfs_path(),
				 "class/infiniband_ucma/abi_version",
				 value, sizeof value) < 0)) {
		/*
		 * Older version of Linux do not have class/misc.  To support
		 * backports, assume the most recent version of the ABI.  If
		 * we're wrong, we'll simply fail later when calling the ABI.
		 */
		return 0;
	}

	abi_ver = strtol(value, NULL, 10);
	if (abi_ver < RDMA_USER_CM_MIN_ABI_VERSION ||
	    abi_ver > RDMA_USER_CM_MAX_ABI_VERSION) {
		return -1;
	}
	return 0;
}

/*
 * This function is called holding the mutex lock
 * cma_dev_cnt must be set before calling this function to
 * ensure that the lock is not acquired recursively.
 */
static void ucma_set_af_ib_support(void)
{
	struct rdma_cm_id *id;
	struct sockaddr_ib sib;
	int ret;

	ret = rdma_create_id(NULL, &id, NULL, RDMA_PS_IB);
	if (ret)
		return;

	memset(&sib, 0, sizeof sib);
	sib.sib_family = AF_IB;
	sib.sib_sid = htobe64(RDMA_IB_IP_PS_TCP);
	sib.sib_sid_mask = htobe64(RDMA_IB_IP_PS_MASK);
	af_ib_support = 1;
	ret = rdma_bind_addr(id, (struct sockaddr *) &sib);
	af_ib_support = !ret;

	rdma_destroy_id(id);
}

int ucma_init(void)
{
	struct ibv_device **dev_list = NULL;
	int i, ret, dev_cnt;

	/* Quick check without lock to see if we're already initialized */
	if (cma_dev_cnt)
		return 0;

	pthread_mutex_lock(&mut);
	if (cma_dev_cnt) {
		pthread_mutex_unlock(&mut);
		return 0;
	}

	fastlock_init(&idm_lock);
	ret = check_abi_version();
	if (ret)
		goto err1;

	dev_list = ibv_get_device_list(&dev_cnt);
	if (!dev_list) {
		ret = ERR(ENODEV);
		goto err1;
	}

	if (!dev_cnt) {
		ret = ERR(ENODEV);
		goto err2;
	}

	cma_dev_array = calloc(dev_cnt, sizeof(*cma_dev_array));
	if (!cma_dev_array) {
		ret = ERR(ENOMEM);
		goto err2;
	}

	for (i = 0; dev_list[i]; i++)
		cma_dev_array[i].guid = ibv_get_device_guid(dev_list[i]);

	cma_dev_cnt = dev_cnt;
	ucma_set_af_ib_support();
	pthread_mutex_unlock(&mut);
	ibv_free_device_list(dev_list);
	return 0;

err2:
	ibv_free_device_list(dev_list);
err1:
	fastlock_destroy(&idm_lock);
	pthread_mutex_unlock(&mut);
	return ret;
}

static struct ibv_context *ucma_open_device(__be64 guid)
{
	struct ibv_device **dev_list;
	struct ibv_context *verbs = NULL;
	int i;

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		return NULL;
	}

	for (i = 0; dev_list[i]; i++) {
		if (ibv_get_device_guid(dev_list[i]) == guid) {
			verbs = ibv_open_device(dev_list[i]);
			break;
		}
	}

	ibv_free_device_list(dev_list);
	return verbs;
}

static int ucma_init_device(struct cma_device *cma_dev)
{
	struct ibv_port_attr port_attr;
	struct ibv_device_attr attr;
	int i, ret;

	if (cma_dev->verbs)
		return 0;

	cma_dev->verbs = ucma_open_device(cma_dev->guid);
	if (!cma_dev->verbs)
		return ERR(ENODEV);

	ret = ibv_query_device(cma_dev->verbs, &attr);
	if (ret) {
		ret = ERR(ret);
		goto err;
	}

	cma_dev->port = malloc(sizeof(*cma_dev->port) * attr.phys_port_cnt);
	if (!cma_dev->port) {
		ret = ERR(ENOMEM);
		goto err;
	}

	for (i = 1; i <= attr.phys_port_cnt; i++) {
		if (ibv_query_port(cma_dev->verbs, i, &port_attr))
			cma_dev->port[i - 1].link_layer = IBV_LINK_LAYER_UNSPECIFIED;
		else
			cma_dev->port[i - 1].link_layer = port_attr.link_layer;
	}

	cma_dev->port_cnt = attr.phys_port_cnt;
	cma_dev->max_qpsize = attr.max_qp_wr;
	cma_dev->max_initiator_depth = (uint8_t) attr.max_qp_init_rd_atom;
	cma_dev->max_responder_resources = (uint8_t) attr.max_qp_rd_atom;
	cma_init_cnt++;
	return 0;

err:
	ibv_close_device(cma_dev->verbs);
	cma_dev->verbs = NULL;
	return ret;
}

static int ucma_init_all(void)
{
	int i, ret = 0;

	if (!cma_dev_cnt) {
		ret = ucma_init();
		if (ret)
			return ret;
	}

	if (cma_init_cnt == cma_dev_cnt)
		return 0;

	pthread_mutex_lock(&mut);
	for (i = 0; i < cma_dev_cnt; i++) {
		ret = ucma_init_device(&cma_dev_array[i]);
		if (ret)
			break;
	}
	pthread_mutex_unlock(&mut);
	return ret;
}

struct ibv_context **rdma_get_devices(int *num_devices)
{
	struct ibv_context **devs = NULL;
	int i;

	if (ucma_init_all())
		goto out;

	devs = malloc(sizeof(*devs) * (cma_dev_cnt + 1));
	if (!devs)
		goto out;

	for (i = 0; i < cma_dev_cnt; i++)
		devs[i] = cma_dev_array[i].verbs;
	devs[i] = NULL;
out:
	if (num_devices)
		*num_devices = devs ? cma_dev_cnt : 0;
	return devs;
}

void rdma_free_devices(struct ibv_context **list)
{
	free(list);
}

struct rdma_event_channel *rdma_create_event_channel(void)
{
	struct rdma_event_channel *channel;

	if (ucma_init())
		return NULL;

	channel = malloc(sizeof(*channel));
	if (!channel)
		return NULL;

	channel->fd = open("/dev/rdma_cm", O_RDWR | O_CLOEXEC);
	if (channel->fd < 0) {
		goto err;
	}
	return channel;
err:
	free(channel);
	return NULL;
}

void rdma_destroy_event_channel(struct rdma_event_channel *channel)
{
	close(channel->fd);
	free(channel);
}

static int ucma_get_device(struct cma_id_private *id_priv, __be64 guid)
{
	struct cma_device *cma_dev;
	int i, ret;

	for (i = 0; i < cma_dev_cnt; i++) {
		cma_dev = &cma_dev_array[i];
		if (cma_dev->guid == guid)
			goto match;
	}

	return ERR(ENODEV);
match:
	pthread_mutex_lock(&mut);
	if ((ret = ucma_init_device(cma_dev)))
		goto out;

	if (!cma_dev->refcnt++) {
		cma_dev->pd = ibv_alloc_pd(cma_dev->verbs);
		if (!cma_dev->pd) {
			cma_dev->refcnt--;
			ret = ERR(ENOMEM);
			goto out;
		}
	}
	id_priv->cma_dev = cma_dev;
	id_priv->id.verbs = cma_dev->verbs;
	id_priv->id.pd = cma_dev->pd;
out:
	pthread_mutex_unlock(&mut);
	return ret;
}

static void ucma_put_device(struct cma_device *cma_dev)
{
	pthread_mutex_lock(&mut);
	if (!--cma_dev->refcnt) {
		ibv_dealloc_pd(cma_dev->pd);
		if (cma_dev->xrcd)
			ibv_close_xrcd(cma_dev->xrcd);
	}
	pthread_mutex_unlock(&mut);
}

static struct ibv_xrcd *ucma_get_xrcd(struct cma_device *cma_dev)
{
	struct ibv_xrcd_init_attr attr;

	pthread_mutex_lock(&mut);
	if (!cma_dev->xrcd) {
		memset(&attr, 0, sizeof attr);
		attr.comp_mask = IBV_XRCD_INIT_ATTR_FD | IBV_XRCD_INIT_ATTR_OFLAGS;
		attr.fd = -1;
		attr.oflags = O_CREAT;
		cma_dev->xrcd = ibv_open_xrcd(cma_dev->verbs, &attr);
	}
	pthread_mutex_unlock(&mut);
	return cma_dev->xrcd;
}

static void ucma_insert_id(struct cma_id_private *id_priv)
{
	fastlock_acquire(&idm_lock);
	idm_set(&ucma_idm, id_priv->handle, id_priv);
	fastlock_release(&idm_lock);
}

static void ucma_remove_id(struct cma_id_private *id_priv)
{
	if (id_priv->handle <= IDX_MAX_INDEX)
		idm_clear(&ucma_idm, id_priv->handle);
}

static struct cma_id_private *ucma_lookup_id(int handle)
{
	return idm_lookup(&ucma_idm, handle);
}

static void ucma_free_id(struct cma_id_private *id_priv)
{
	ucma_remove_id(id_priv);
	if (id_priv->cma_dev)
		ucma_put_device(id_priv->cma_dev);
	pthread_cond_destroy(&id_priv->cond);
	pthread_mutex_destroy(&id_priv->mut);
	if (id_priv->id.route.path_rec)
		free(id_priv->id.route.path_rec);

	if (id_priv->sync)
		rdma_destroy_event_channel(id_priv->id.channel);
	if (id_priv->connect_len)
		free(id_priv->connect);
	free(id_priv);
}

static struct cma_id_private *ucma_alloc_id(struct rdma_event_channel *channel,
					    void *context,
					    enum rdma_port_space ps,
					    enum ibv_qp_type qp_type)
{
	struct cma_id_private *id_priv;

	id_priv = calloc(1, sizeof(*id_priv));
	if (!id_priv)
		return NULL;

	id_priv->id.context = context;
	id_priv->id.ps = ps;
	id_priv->id.qp_type = qp_type;
	id_priv->handle = 0xFFFFFFFF;

	if (!channel) {
		id_priv->id.channel = rdma_create_event_channel();
		if (!id_priv->id.channel)
			goto err;
		id_priv->sync = 1;
	} else {
		id_priv->id.channel = channel;
	}

	pthread_mutex_init(&id_priv->mut, NULL);
	if (pthread_cond_init(&id_priv->cond, NULL))
		goto err;

	return id_priv;

err:	ucma_free_id(id_priv);
	return NULL;
}

static int rdma_create_id2(struct rdma_event_channel *channel,
			   struct rdma_cm_id **id, void *context,
			   enum rdma_port_space ps, enum ibv_qp_type qp_type)
{
	struct ucma_abi_create_id_resp resp;
	struct ucma_abi_create_id cmd;
	struct cma_id_private *id_priv;
	int ret;

	ret = ucma_init();
	if (ret)
		return ret;

	id_priv = ucma_alloc_id(channel, context, ps, qp_type);
	if (!id_priv)
		return ERR(ENOMEM);

	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, CREATE_ID, &resp, sizeof resp);
	cmd.uid = (uintptr_t) id_priv;
	cmd.ps = ps;
	cmd.qp_type = qp_type;

	ret = write(id_priv->id.channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		goto err;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	id_priv->handle = resp.id;
	ucma_insert_id(id_priv);
	*id = &id_priv->id;
	return 0;

err:	ucma_free_id(id_priv);
	return ret;
}

int rdma_create_id(struct rdma_event_channel *channel,
		   struct rdma_cm_id **id, void *context,
		   enum rdma_port_space ps)
{
	enum ibv_qp_type qp_type;

	qp_type = (ps == RDMA_PS_IPOIB || ps == RDMA_PS_UDP) ?
		  IBV_QPT_UD : IBV_QPT_RC;
	return rdma_create_id2(channel, id, context, ps, qp_type);
}

static int ucma_destroy_kern_id(int fd, uint32_t handle)
{
	struct ucma_abi_destroy_id_resp resp;
	struct ucma_abi_destroy_id cmd;
	int ret;
	
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, DESTROY_ID, &resp, sizeof resp);
	cmd.id = handle;

	ret = write(fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	return resp.events_reported;
}

int rdma_destroy_id(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);
	ret = ucma_destroy_kern_id(id->channel->fd, id_priv->handle);
	if (ret < 0)
		return ret;

	if (id_priv->id.event)
		rdma_ack_cm_event(id_priv->id.event);

	pthread_mutex_lock(&id_priv->mut);
	while (id_priv->events_completed < ret)
		pthread_cond_wait(&id_priv->cond, &id_priv->mut);
	pthread_mutex_unlock(&id_priv->mut);

	ucma_free_id(id_priv);
	return 0;
}

int ucma_addrlen(struct sockaddr *addr)
{
	if (!addr)
		return 0;

	switch (addr->sa_family) {
	case PF_INET:
		return sizeof(struct sockaddr_in);
	case PF_INET6:
		return sizeof(struct sockaddr_in6);
	case PF_IB:
		return af_ib_support ? sizeof(struct sockaddr_ib) : 0;
	default:
		return 0;
	}
}

static int ucma_query_addr(struct rdma_cm_id *id)
{
	struct ucma_abi_query_addr_resp resp;
	struct ucma_abi_query cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, QUERY, &resp, sizeof resp);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.option = UCMA_QUERY_ADDR;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	memcpy(&id->route.addr.src_addr, &resp.src_addr, resp.src_size);
	memcpy(&id->route.addr.dst_addr, &resp.dst_addr, resp.dst_size);

	if (!id_priv->cma_dev && resp.node_guid) {
		ret = ucma_get_device(id_priv, resp.node_guid);
		if (ret)
			return ret;
		id->port_num = resp.port_num;
		id->route.addr.addr.ibaddr.pkey = resp.pkey;
	}

	return 0;
}

static int ucma_query_gid(struct rdma_cm_id *id)
{
	struct ucma_abi_query_addr_resp resp;
	struct ucma_abi_query cmd;
	struct cma_id_private *id_priv;
	struct sockaddr_ib *sib;
	int ret;
	
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, QUERY, &resp, sizeof resp);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.option = UCMA_QUERY_GID;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	sib = (struct sockaddr_ib *) &resp.src_addr;
	memcpy(id->route.addr.addr.ibaddr.sgid.raw, sib->sib_addr.sib_raw,
	       sizeof id->route.addr.addr.ibaddr.sgid);

	sib = (struct sockaddr_ib *) &resp.dst_addr;
	memcpy(id->route.addr.addr.ibaddr.dgid.raw, sib->sib_addr.sib_raw,
	       sizeof id->route.addr.addr.ibaddr.dgid);

	return 0;
}

static void ucma_convert_path(struct ibv_path_data *path_data,
			      struct ibv_sa_path_rec *sa_path)
{
	uint32_t fl_hop;

	sa_path->dgid = path_data->path.dgid;
	sa_path->sgid = path_data->path.sgid;
	sa_path->dlid = path_data->path.dlid;
	sa_path->slid = path_data->path.slid;
	sa_path->raw_traffic = 0;

	fl_hop = be32toh(path_data->path.flowlabel_hoplimit);
	sa_path->flow_label = htobe32(fl_hop >> 8);
	sa_path->hop_limit = (uint8_t) fl_hop;

	sa_path->traffic_class = path_data->path.tclass;
	sa_path->reversible = path_data->path.reversible_numpath >> 7;
	sa_path->numb_path = 1;
	sa_path->pkey = path_data->path.pkey;
	sa_path->sl = be16toh(path_data->path.qosclass_sl) & 0xF;
	sa_path->mtu_selector = 2;	/* exactly */
	sa_path->mtu = path_data->path.mtu & 0x1F;
	sa_path->rate_selector = 2;
	sa_path->rate = path_data->path.rate & 0x1F;
	sa_path->packet_life_time_selector = 2;
	sa_path->packet_life_time = path_data->path.packetlifetime & 0x1F;

	sa_path->preference = (uint8_t) path_data->flags;
}

static int ucma_query_path(struct rdma_cm_id *id)
{
	struct ucma_abi_query_path_resp *resp;
	struct ucma_abi_query cmd;
	struct cma_id_private *id_priv;
	int ret, i, size;

	size = sizeof(*resp) + sizeof(struct ibv_path_data) * 6;
	resp = alloca(size);
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, QUERY, resp, size);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.option = UCMA_QUERY_PATH;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(resp, size);

	if (resp->num_paths) {
		id->route.path_rec = malloc(sizeof(*id->route.path_rec) *
					    resp->num_paths);
		if (!id->route.path_rec)
			return ERR(ENOMEM);

		id->route.num_paths = resp->num_paths;
		for (i = 0; i < resp->num_paths; i++)
			ucma_convert_path(&resp->path_data[i], &id->route.path_rec[i]);
	}

	return 0;
}

static int ucma_query_route(struct rdma_cm_id *id)
{
	struct ucma_abi_query_route_resp resp;
	struct ucma_abi_query cmd;
	struct cma_id_private *id_priv;
	int ret, i;

	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, QUERY_ROUTE, &resp, sizeof resp);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	if (resp.num_paths) {
		id->route.path_rec = malloc(sizeof(*id->route.path_rec) *
					    resp.num_paths);
		if (!id->route.path_rec)
			return ERR(ENOMEM);

		id->route.num_paths = resp.num_paths;
		for (i = 0; i < resp.num_paths; i++)
			ibv_copy_path_rec_from_kern(&id->route.path_rec[i],
						    &resp.ib_route[i]);
	}

	memcpy(id->route.addr.addr.ibaddr.sgid.raw, resp.ib_route[0].sgid,
	       sizeof id->route.addr.addr.ibaddr.sgid);
	memcpy(id->route.addr.addr.ibaddr.dgid.raw, resp.ib_route[0].dgid,
	       sizeof id->route.addr.addr.ibaddr.dgid);
	id->route.addr.addr.ibaddr.pkey = resp.ib_route[0].pkey;
	memcpy(&id->route.addr.src_addr, &resp.src_addr,
	       sizeof resp.src_addr);
	memcpy(&id->route.addr.dst_addr, &resp.dst_addr,
	       sizeof resp.dst_addr);

	if (!id_priv->cma_dev && resp.node_guid) {
		ret = ucma_get_device(id_priv, resp.node_guid);
		if (ret)
			return ret;
		id_priv->id.port_num = resp.port_num;
	}

	return 0;
}

static int rdma_bind_addr2(struct rdma_cm_id *id, struct sockaddr *addr,
			   socklen_t addrlen)
{
	struct ucma_abi_bind cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, BIND);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.addr_size = addrlen;
	memcpy(&cmd.addr, addr, addrlen);

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	ret = ucma_query_addr(id);
	if (!ret)
		ret = ucma_query_gid(id);
	return ret;
}

int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct ucma_abi_bind_ip cmd;
	struct cma_id_private *id_priv;
	int ret, addrlen;
	
	addrlen = ucma_addrlen(addr);
	if (!addrlen)
		return ERR(EINVAL);

	if (af_ib_support)
		return rdma_bind_addr2(id, addr, addrlen);

	CMA_INIT_CMD(&cmd, sizeof cmd, BIND_IP);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	memcpy(&cmd.addr, addr, addrlen);

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	return ucma_query_route(id);
}

int ucma_complete(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);
	if (!id_priv->sync)
		return 0;

	if (id_priv->id.event) {
		rdma_ack_cm_event(id_priv->id.event);
		id_priv->id.event = NULL;
	}

	ret = rdma_get_cm_event(id_priv->id.channel, &id_priv->id.event);
	if (ret)
		return ret;

	if (id_priv->id.event->status) {
		if (id_priv->id.event->event == RDMA_CM_EVENT_REJECTED)
			ret = ERR(ECONNREFUSED);
		else if (id_priv->id.event->status < 0)
			ret = ERR(-id_priv->id.event->status);
		else
			ret = ERR(-id_priv->id.event->status);
	}
	return ret;
}

static int rdma_resolve_addr2(struct rdma_cm_id *id, struct sockaddr *src_addr,
			      socklen_t src_len, struct sockaddr *dst_addr,
			      socklen_t dst_len, int timeout_ms)
{
	struct ucma_abi_resolve_addr cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, RESOLVE_ADDR);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	if ((cmd.src_size = src_len))
		memcpy(&cmd.src_addr, src_addr, src_len);
	memcpy(&cmd.dst_addr, dst_addr, dst_len);
	cmd.dst_size = dst_len;
	cmd.timeout_ms = timeout_ms;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	memcpy(&id->route.addr.dst_addr, dst_addr, dst_len);
	return ucma_complete(id);
}

int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms)
{
	struct ucma_abi_resolve_ip cmd;
	struct cma_id_private *id_priv;
	int ret, dst_len, src_len;
	
	dst_len = ucma_addrlen(dst_addr);
	if (!dst_len)
		return ERR(EINVAL);

	src_len = ucma_addrlen(src_addr);
	if (src_addr && !src_len)
		return ERR(EINVAL);

	if (af_ib_support)
		return rdma_resolve_addr2(id, src_addr, src_len, dst_addr,
					  dst_len, timeout_ms);

	CMA_INIT_CMD(&cmd, sizeof cmd, RESOLVE_IP);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	if (src_addr)
		memcpy(&cmd.src_addr, src_addr, src_len);
	memcpy(&cmd.dst_addr, dst_addr, dst_len);
	cmd.timeout_ms = timeout_ms;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	memcpy(&id->route.addr.dst_addr, dst_addr, dst_len);
	return ucma_complete(id);
}

static int ucma_set_ib_route(struct rdma_cm_id *id)
{
	struct rdma_addrinfo hint, *rai;
	int ret;

	memset(&hint, 0, sizeof hint);
	hint.ai_flags = RAI_ROUTEONLY;
	hint.ai_family = id->route.addr.src_addr.sa_family;
	hint.ai_src_len = ucma_addrlen((struct sockaddr *) &id->route.addr.src_addr);
	hint.ai_src_addr = &id->route.addr.src_addr;
	hint.ai_dst_len = ucma_addrlen((struct sockaddr *) &id->route.addr.dst_addr);
	hint.ai_dst_addr = &id->route.addr.dst_addr;

	ret = rdma_getaddrinfo(NULL, NULL, &hint, &rai);
	if (ret)
		return ret;

	if (rai->ai_route_len)
		ret = rdma_set_option(id, RDMA_OPTION_IB, RDMA_OPTION_IB_PATH,
				      rai->ai_route, rai->ai_route_len);
	else
		ret = -1;

	rdma_freeaddrinfo(rai);
	return ret;
}

int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms)
{
	struct ucma_abi_resolve_route cmd;
	struct cma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);
	if (id->verbs->device->transport_type == IBV_TRANSPORT_IB) {
		ret = ucma_set_ib_route(id);
		if (!ret)
			goto out;
	}

	CMA_INIT_CMD(&cmd, sizeof cmd, RESOLVE_ROUTE);
	cmd.id = id_priv->handle;
	cmd.timeout_ms = timeout_ms;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

out:
	return ucma_complete(id);
}

static int ucma_is_ud_qp(enum ibv_qp_type qp_type)
{
	return (qp_type == IBV_QPT_UD);
}

static int rdma_init_qp_attr(struct rdma_cm_id *id, struct ibv_qp_attr *qp_attr,
			     int *qp_attr_mask)
{
	struct ucma_abi_init_qp_attr cmd;
	struct ibv_kern_qp_attr resp;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, INIT_QP_ATTR, &resp, sizeof resp);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.qp_state = qp_attr->qp_state;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	ibv_copy_qp_attr_from_kern(qp_attr, &resp);
	*qp_attr_mask = resp.qp_attr_mask;
	return 0;
}

static int ucma_modify_qp_rtr(struct rdma_cm_id *id, uint8_t resp_res)
{
	struct cma_id_private *id_priv;
	struct ibv_qp_attr qp_attr;
	int qp_attr_mask, ret;
	uint8_t link_layer;

	if (!id->qp)
		return ERR(EINVAL);

	/* Need to update QP attributes from default values. */
	qp_attr.qp_state = IBV_QPS_INIT;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	ret = ibv_modify_qp(id->qp, &qp_attr, qp_attr_mask);
	if (ret)
		return ERR(ret);

	qp_attr.qp_state = IBV_QPS_RTR;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	/*
	 * Workaround for rdma_ucm kernel bug:
	 * mask off qp_attr_mask bits 21-24 which are used for RoCE
	 */
	id_priv = container_of(id, struct cma_id_private, id);
	link_layer = id_priv->cma_dev->port[id->port_num - 1].link_layer;

	if (link_layer == IBV_LINK_LAYER_INFINIBAND)
		qp_attr_mask &= UINT_MAX ^ 0xe00000;

	if (resp_res != RDMA_MAX_RESP_RES)
		qp_attr.max_dest_rd_atomic = resp_res;
	return rdma_seterrno(ibv_modify_qp(id->qp, &qp_attr, qp_attr_mask));
}

static int ucma_modify_qp_rts(struct rdma_cm_id *id, uint8_t init_depth)
{
	struct ibv_qp_attr qp_attr;
	int qp_attr_mask, ret;

	qp_attr.qp_state = IBV_QPS_RTS;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	if (init_depth != RDMA_MAX_INIT_DEPTH)
		qp_attr.max_rd_atomic = init_depth;
	return rdma_seterrno(ibv_modify_qp(id->qp, &qp_attr, qp_attr_mask));
}

static int ucma_modify_qp_sqd(struct rdma_cm_id *id)
{
	struct ibv_qp_attr qp_attr;

	if (!id->qp)
		return 0;

	qp_attr.qp_state = IBV_QPS_SQD;
	return rdma_seterrno(ibv_modify_qp(id->qp, &qp_attr, IBV_QP_STATE));
}

static int ucma_modify_qp_err(struct rdma_cm_id *id)
{
	struct ibv_qp_attr qp_attr;

	if (!id->qp)
		return 0;

	qp_attr.qp_state = IBV_QPS_ERR;
	return rdma_seterrno(ibv_modify_qp(id->qp, &qp_attr, IBV_QP_STATE));
}

static int ucma_find_pkey(struct cma_device *cma_dev, uint8_t port_num,
			  __be16 pkey, uint16_t *pkey_index)
{
	int ret, i;
	__be16 chk_pkey;

	for (i = 0, ret = 0; !ret; i++) {
		ret = ibv_query_pkey(cma_dev->verbs, port_num, i, &chk_pkey);
		if (!ret && pkey == chk_pkey) {
			*pkey_index = (uint16_t) i;
			return 0;
		}
	}
	return ERR(EINVAL);
}

static int ucma_init_conn_qp3(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	int ret;

	ret = ucma_find_pkey(id_priv->cma_dev, id_priv->id.port_num,
			     id_priv->id.route.addr.addr.ibaddr.pkey,
			     &qp_attr.pkey_index);
	if (ret)
		return ret;

	qp_attr.port_num = id_priv->id.port_num;
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.qp_access_flags = 0;

	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_ACCESS_FLAGS |
					  IBV_QP_PKEY_INDEX | IBV_QP_PORT);
	return rdma_seterrno(ret);
}

static int ucma_init_conn_qp(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	int qp_attr_mask, ret;

	if (abi_ver == 3)
		return ucma_init_conn_qp3(id_priv, qp);

	qp_attr.qp_state = IBV_QPS_INIT;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	return rdma_seterrno(ibv_modify_qp(qp, &qp_attr, qp_attr_mask));
}

static int ucma_init_ud_qp3(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	int ret;

	ret = ucma_find_pkey(id_priv->cma_dev, id_priv->id.port_num,
			     id_priv->id.route.addr.addr.ibaddr.pkey,
			     &qp_attr.pkey_index);
	if (ret)
		return ret;

	qp_attr.port_num = id_priv->id.port_num;
	qp_attr.qp_state = IBV_QPS_INIT;
	qp_attr.qkey = RDMA_UDP_QKEY;

	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_QKEY |
					  IBV_QP_PKEY_INDEX | IBV_QP_PORT);
	if (ret)
		return ERR(ret);

	qp_attr.qp_state = IBV_QPS_RTR;
	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE);
	if (ret)
		return ERR(ret);

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
	return rdma_seterrno(ret);
}

static int ucma_init_ud_qp(struct cma_id_private *id_priv, struct ibv_qp *qp)
{
	struct ibv_qp_attr qp_attr;
	int qp_attr_mask, ret;

	if (abi_ver == 3)
		return ucma_init_ud_qp3(id_priv, qp);

	qp_attr.qp_state = IBV_QPS_INIT;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	ret = ibv_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret)
		return ERR(ret);

	qp_attr.qp_state = IBV_QPS_RTR;
	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE);
	if (ret)
		return ERR(ret);

	qp_attr.qp_state = IBV_QPS_RTS;
	qp_attr.sq_psn = 0;
	ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
	return rdma_seterrno(ret);
}

static void ucma_destroy_cqs(struct rdma_cm_id *id)
{
	if (id->qp_type == IBV_QPT_XRC_RECV && id->srq)
		return;

	if (id->recv_cq) {
		ibv_destroy_cq(id->recv_cq);
		if (id->send_cq && (id->send_cq != id->recv_cq)) {
			ibv_destroy_cq(id->send_cq);
			id->send_cq = NULL;
		}
		id->recv_cq = NULL;
	}

	if (id->recv_cq_channel) {
		ibv_destroy_comp_channel(id->recv_cq_channel);
		if (id->send_cq_channel && (id->send_cq_channel != id->recv_cq_channel)) {
			ibv_destroy_comp_channel(id->send_cq_channel);
			id->send_cq_channel = NULL;
		}
		id->recv_cq_channel = NULL;
	}
}

static int ucma_create_cqs(struct rdma_cm_id *id, uint32_t send_size, uint32_t recv_size)
{
	if (recv_size) {
		id->recv_cq_channel = ibv_create_comp_channel(id->verbs);
		if (!id->recv_cq_channel)
			goto err;

		id->recv_cq = ibv_create_cq(id->verbs, recv_size,
					    id, id->recv_cq_channel, 0);
		if (!id->recv_cq)
			goto err;
	}

	if (send_size) {
		id->send_cq_channel = ibv_create_comp_channel(id->verbs);
		if (!id->send_cq_channel)
			goto err;

		id->send_cq = ibv_create_cq(id->verbs, send_size,
					    id, id->send_cq_channel, 0);
		if (!id->send_cq)
			goto err;
	}

	return 0;
err:
	ucma_destroy_cqs(id);
	return ERR(ENOMEM);
}

int rdma_create_srq_ex(struct rdma_cm_id *id, struct ibv_srq_init_attr_ex *attr)
{
	struct cma_id_private *id_priv;
	struct ibv_srq *srq;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);
	if (!(attr->comp_mask & IBV_SRQ_INIT_ATTR_TYPE))
		return ERR(EINVAL);

	if (!(attr->comp_mask & IBV_SRQ_INIT_ATTR_PD) || !attr->pd) {
		attr->pd = id->pd;
		attr->comp_mask |= IBV_SRQ_INIT_ATTR_PD;
	}

	if (attr->srq_type == IBV_SRQT_XRC) {
		if (!(attr->comp_mask & IBV_SRQ_INIT_ATTR_XRCD) || !attr->xrcd) {
			attr->xrcd = ucma_get_xrcd(id_priv->cma_dev);
			if (!attr->xrcd)
				return -1;
		}
		if (!(attr->comp_mask & IBV_SRQ_INIT_ATTR_CQ) || !attr->cq) {
			ret = ucma_create_cqs(id, 0, attr->attr.max_wr);
			if (ret)
				return ret;
			attr->cq = id->recv_cq;
		}
		attr->comp_mask |= IBV_SRQ_INIT_ATTR_XRCD | IBV_SRQ_INIT_ATTR_CQ;
	}

	srq = ibv_create_srq_ex(id->verbs, attr);
	if (!srq) {
		ret = -1;
		goto err;
	}

	if (!id->pd)
		id->pd = attr->pd;
	id->srq = srq;
	return 0;
err:
	ucma_destroy_cqs(id);
	return ret;
}

int rdma_create_srq(struct rdma_cm_id *id, struct ibv_pd *pd,
		    struct ibv_srq_init_attr *attr)
{
	struct ibv_srq_init_attr_ex attr_ex;
	int ret;

	memcpy(&attr_ex, attr, sizeof(*attr));
	attr_ex.comp_mask = IBV_SRQ_INIT_ATTR_TYPE | IBV_SRQ_INIT_ATTR_PD;
	if (id->qp_type == IBV_QPT_XRC_RECV) {
		attr_ex.srq_type = IBV_SRQT_XRC;
	} else {
		attr_ex.srq_type = IBV_SRQT_BASIC;
	}
	attr_ex.pd = pd;
	ret = rdma_create_srq_ex(id, &attr_ex);
	memcpy(attr, &attr_ex, sizeof(*attr));
	return ret;
}

void rdma_destroy_srq(struct rdma_cm_id *id)
{
	ibv_destroy_srq(id->srq);
	id->srq = NULL;
	ucma_destroy_cqs(id);
}

int rdma_create_qp_ex(struct rdma_cm_id *id,
		      struct ibv_qp_init_attr_ex *attr)
{
	struct cma_id_private *id_priv;
	struct ibv_qp *qp;
	int ret;

	if (id->qp)
		return ERR(EINVAL);

	id_priv = container_of(id, struct cma_id_private, id);
	if (!(attr->comp_mask & IBV_QP_INIT_ATTR_PD) || !attr->pd) {
		attr->comp_mask |= IBV_QP_INIT_ATTR_PD;
		attr->pd = id->pd;
	} else if (id->verbs != attr->pd->context)
		return ERR(EINVAL);

	if ((id->recv_cq && attr->recv_cq && id->recv_cq != attr->recv_cq) ||
	    (id->send_cq && attr->send_cq && id->send_cq != attr->send_cq))
		return ERR(EINVAL);

	if (id->qp_type == IBV_QPT_XRC_RECV) {
		if (!(attr->comp_mask & IBV_QP_INIT_ATTR_XRCD) || !attr->xrcd) {
			attr->xrcd = ucma_get_xrcd(id_priv->cma_dev);
			if (!attr->xrcd)
				return -1;
			attr->comp_mask |= IBV_QP_INIT_ATTR_XRCD;
		}
	}

	ret = ucma_create_cqs(id, attr->send_cq || id->send_cq ? 0 : attr->cap.max_send_wr,
				  attr->recv_cq || id->recv_cq ? 0 : attr->cap.max_recv_wr);
	if (ret)
		return ret;

	if (!attr->send_cq)
		attr->send_cq = id->send_cq;
	if (!attr->recv_cq)
		attr->recv_cq = id->recv_cq;
	if (id->srq && !attr->srq)
		attr->srq = id->srq;
	qp = ibv_create_qp_ex(id->verbs, attr);
	if (!qp) {
		ret = ERR(ENOMEM);
		goto err1;
	}

	if (ucma_is_ud_qp(id->qp_type))
		ret = ucma_init_ud_qp(id_priv, qp);
	else
		ret = ucma_init_conn_qp(id_priv, qp);
	if (ret)
		goto err2;

	id->pd = qp->pd;
	id->qp = qp;
	return 0;
err2:
	ibv_destroy_qp(qp);
err1:
	ucma_destroy_cqs(id);
	return ret;
}

int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,
		   struct ibv_qp_init_attr *qp_init_attr)
{
	struct ibv_qp_init_attr_ex attr_ex;
	int ret;

	memcpy(&attr_ex, qp_init_attr, sizeof(*qp_init_attr));
	attr_ex.comp_mask = IBV_QP_INIT_ATTR_PD;
	attr_ex.pd = pd ? pd : id->pd;
	ret = rdma_create_qp_ex(id, &attr_ex);
	memcpy(qp_init_attr, &attr_ex, sizeof(*qp_init_attr));
	return ret;
}

void rdma_destroy_qp(struct rdma_cm_id *id)
{
	ibv_destroy_qp(id->qp);
	id->qp = NULL;
	ucma_destroy_cqs(id);
}

static int ucma_valid_param(struct cma_id_private *id_priv,
			    struct rdma_conn_param *param)
{
	if (id_priv->id.ps != RDMA_PS_TCP)
		return 0;

	if (!id_priv->id.qp && !param)
		goto err;

	if (!param)
		return 0;

	if ((param->responder_resources != RDMA_MAX_RESP_RES) &&
	    (param->responder_resources > id_priv->cma_dev->max_responder_resources))
		goto err;

	if ((param->initiator_depth != RDMA_MAX_INIT_DEPTH) &&
	    (param->initiator_depth > id_priv->cma_dev->max_initiator_depth))
		goto err;

	return 0;
err:
	return ERR(EINVAL);
}

static void ucma_copy_conn_param_to_kern(struct cma_id_private *id_priv,
					 struct ucma_abi_conn_param *dst,
					 struct rdma_conn_param *src,
					 uint32_t qp_num, uint8_t srq)
{
	dst->qp_num = qp_num;
	dst->srq = srq;
	dst->responder_resources = id_priv->responder_resources;
	dst->initiator_depth = id_priv->initiator_depth;
	dst->valid = 1;

	if (id_priv->connect_len) {
		memcpy(dst->private_data, id_priv->connect, id_priv->connect_len);
		dst->private_data_len = id_priv->connect_len;
	}

	if (src) {
		dst->flow_control = src->flow_control;
		dst->retry_count = src->retry_count;
		dst->rnr_retry_count = src->rnr_retry_count;

		if (src->private_data && src->private_data_len) {
			memcpy(dst->private_data + dst->private_data_len,
			       src->private_data, src->private_data_len);
			dst->private_data_len += src->private_data_len;
		}
	} else {
		dst->retry_count = 7;
		dst->rnr_retry_count = 7;
	}
}

int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct ucma_abi_connect cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	id_priv = container_of(id, struct cma_id_private, id);
	ret = ucma_valid_param(id_priv, conn_param);
	if (ret)
		return ret;

	if (conn_param && conn_param->initiator_depth != RDMA_MAX_INIT_DEPTH)
		id_priv->initiator_depth = conn_param->initiator_depth;
	else
		id_priv->initiator_depth = id_priv->cma_dev->max_initiator_depth;
	if (conn_param && conn_param->responder_resources != RDMA_MAX_RESP_RES)
		id_priv->responder_resources = conn_param->responder_resources;
	else
		id_priv->responder_resources = id_priv->cma_dev->max_responder_resources;

	CMA_INIT_CMD(&cmd, sizeof cmd, CONNECT);
	cmd.id = id_priv->handle;
	if (id->qp) {
		ucma_copy_conn_param_to_kern(id_priv, &cmd.conn_param,
					     conn_param, id->qp->qp_num,
					     (id->qp->srq != NULL));
	} else if (conn_param) {
		ucma_copy_conn_param_to_kern(id_priv, &cmd.conn_param,
					     conn_param, conn_param->qp_num,
					     conn_param->srq);
	} else {
		ucma_copy_conn_param_to_kern(id_priv, &cmd.conn_param,
					     conn_param, 0, 0);
	}

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	if (id_priv->connect_len) {
		free(id_priv->connect);
		id_priv->connect_len = 0;
	}

	return ucma_complete(id);
}

int rdma_listen(struct rdma_cm_id *id, int backlog)
{
	struct ucma_abi_listen cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, LISTEN);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.backlog = backlog;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	if (af_ib_support)
		return ucma_query_addr(id);
	else
		return ucma_query_route(id);
}

int rdma_get_request(struct rdma_cm_id *listen, struct rdma_cm_id **id)
{
	struct cma_id_private *id_priv;
	struct rdma_cm_event *event;
	int ret;

	id_priv = container_of(listen, struct cma_id_private, id);
	if (!id_priv->sync)
		return ERR(EINVAL);

	if (listen->event) {
		rdma_ack_cm_event(listen->event);
		listen->event = NULL;
	}

	ret = rdma_get_cm_event(listen->channel, &event);
	if (ret)
		return ret;

	if (event->status) {
		ret = ERR(event->status);
		goto err;
	}
	
	if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
		ret = ERR(EINVAL);
		goto err;
	}

	if (id_priv->qp_init_attr) {
		struct ibv_qp_init_attr attr;

		attr = *id_priv->qp_init_attr;
		ret = rdma_create_qp(event->id, listen->pd, &attr);
		if (ret)
			goto err;
	}

	*id = event->id;
	(*id)->event = event;
	return 0;

err:
	listen->event = event;
	return ret;
}

int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct ucma_abi_accept cmd;
	struct cma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct cma_id_private, id);
	ret = ucma_valid_param(id_priv, conn_param);
	if (ret)
		return ret;

	if (!conn_param || conn_param->initiator_depth == RDMA_MAX_INIT_DEPTH) {
		id_priv->initiator_depth = min(id_priv->initiator_depth,
					       id_priv->cma_dev->max_initiator_depth);
	} else {
		id_priv->initiator_depth = conn_param->initiator_depth;
	}
	if (!conn_param || conn_param->responder_resources == RDMA_MAX_RESP_RES) {
		id_priv->responder_resources = min(id_priv->responder_resources,
						   id_priv->cma_dev->max_responder_resources);
	} else {
		id_priv->responder_resources = conn_param->responder_resources;
	}

	if (!ucma_is_ud_qp(id->qp_type)) {
		ret = ucma_modify_qp_rtr(id, id_priv->responder_resources);
		if (ret)
			return ret;

		ret = ucma_modify_qp_rts(id, id_priv->initiator_depth);
		if (ret)
			return ret;
	}

	CMA_INIT_CMD(&cmd, sizeof cmd, ACCEPT);
	cmd.id = id_priv->handle;
	cmd.uid = (uintptr_t) id_priv;
	if (id->qp)
		ucma_copy_conn_param_to_kern(id_priv, &cmd.conn_param,
					     conn_param, id->qp->qp_num,
					     (id->qp->srq != NULL));
	else
		ucma_copy_conn_param_to_kern(id_priv, &cmd.conn_param,
					     conn_param, conn_param->qp_num,
					     conn_param->srq);

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd) {
		ucma_modify_qp_err(id);
		return (ret >= 0) ? ERR(ENODATA) : -1;
	}

	if (ucma_is_ud_qp(id->qp_type))
		return 0;

	return ucma_complete(id);
}

int rdma_reject(struct rdma_cm_id *id, const void *private_data,
		uint8_t private_data_len)
{
	struct ucma_abi_reject cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, REJECT);

	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	if (private_data && private_data_len) {
		memcpy(cmd.private_data, private_data, private_data_len);
		cmd.private_data_len = private_data_len;
	}

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int rdma_notify(struct rdma_cm_id *id, enum ibv_event_type event)
{
	struct ucma_abi_notify cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, NOTIFY);

	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.event = event;
	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ucma_shutdown(struct rdma_cm_id *id)
{
	switch (id->verbs->device->transport_type) {
	case IBV_TRANSPORT_IB:
		return ucma_modify_qp_err(id);
	case IBV_TRANSPORT_IWARP:
		return ucma_modify_qp_sqd(id);
	default:
		return ERR(EINVAL);
	}
}

int rdma_disconnect(struct rdma_cm_id *id)
{
	struct ucma_abi_disconnect cmd;
	struct cma_id_private *id_priv;
	int ret;

	ret = ucma_shutdown(id);
	if (ret)
		return ret;

	CMA_INIT_CMD(&cmd, sizeof cmd, DISCONNECT);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	return ucma_complete(id);
}

static int rdma_join_multicast2(struct rdma_cm_id *id, struct sockaddr *addr,
				socklen_t addrlen, void *context)
{
	struct ucma_abi_create_id_resp resp;
	struct cma_id_private *id_priv;
	struct cma_multicast *mc, **pos;
	int ret;
	
	id_priv = container_of(id, struct cma_id_private, id);
	mc = calloc(1, sizeof(*mc));
	if (!mc)
		return ERR(ENOMEM);

	mc->context = context;
	mc->id_priv = id_priv;
	memcpy(&mc->addr, addr, addrlen);
	if (pthread_cond_init(&mc->cond, NULL)) {
		ret = -1;
		goto err1;
	}

	pthread_mutex_lock(&id_priv->mut);
	mc->next = id_priv->mc_list;
	id_priv->mc_list = mc;
	pthread_mutex_unlock(&id_priv->mut);

	if (af_ib_support) {
		struct ucma_abi_join_mcast cmd;

		CMA_INIT_CMD_RESP(&cmd, sizeof cmd, JOIN_MCAST, &resp, sizeof resp);
		cmd.id = id_priv->handle;
		memcpy(&cmd.addr, addr, addrlen);
		cmd.addr_size = addrlen;
		cmd.uid = (uintptr_t) mc;
		cmd.reserved = 0;

		ret = write(id->channel->fd, &cmd, sizeof cmd);
		if (ret != sizeof cmd) {
			ret = (ret >= 0) ? ERR(ENODATA) : -1;
			goto err2;
		}
	} else {
		struct ucma_abi_join_ip_mcast cmd;

		CMA_INIT_CMD_RESP(&cmd, sizeof cmd, JOIN_IP_MCAST, &resp, sizeof resp);
		cmd.id = id_priv->handle;
		memcpy(&cmd.addr, addr, addrlen);
		cmd.uid = (uintptr_t) mc;

		ret = write(id->channel->fd, &cmd, sizeof cmd);
		if (ret != sizeof cmd) {
			ret = (ret >= 0) ? ERR(ENODATA) : -1;
			goto err2;
		}
	}

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	mc->handle = resp.id;
	return ucma_complete(id);

err2:
	pthread_mutex_lock(&id_priv->mut);
	for (pos = &id_priv->mc_list; *pos != mc; pos = &(*pos)->next)
		;
	*pos = mc->next;
	pthread_mutex_unlock(&id_priv->mut);
err1:
	free(mc);
	return ret;
}

int rdma_join_multicast(struct rdma_cm_id *id, struct sockaddr *addr,
			void *context)
{
	int addrlen;
	
	addrlen = ucma_addrlen(addr);
	if (!addrlen)
		return ERR(EINVAL);

	return rdma_join_multicast2(id, addr, addrlen, context);
}

int rdma_leave_multicast(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct ucma_abi_destroy_id cmd;
	struct ucma_abi_destroy_id_resp resp;
	struct cma_id_private *id_priv;
	struct cma_multicast *mc, **pos;
	int ret, addrlen;
	
	addrlen = ucma_addrlen(addr);
	if (!addrlen)
		return ERR(EINVAL);

	id_priv = container_of(id, struct cma_id_private, id);
	pthread_mutex_lock(&id_priv->mut);
	for (pos = &id_priv->mc_list; *pos; pos = &(*pos)->next)
		if (!memcmp(&(*pos)->addr, addr, addrlen))
			break;

	mc = *pos;
	if (*pos)
		*pos = mc->next;
	pthread_mutex_unlock(&id_priv->mut);
	if (!mc)
		return ERR(EADDRNOTAVAIL);

	if (id->qp)
		ibv_detach_mcast(id->qp, &mc->mgid, mc->mlid);
	
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, LEAVE_MCAST, &resp, sizeof resp);
	cmd.id = mc->handle;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd) {
		ret = (ret >= 0) ? ERR(ENODATA) : -1;
		goto free;
	}

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	pthread_mutex_lock(&id_priv->mut);
	while (mc->events_completed < resp.events_reported)
		pthread_cond_wait(&mc->cond, &id_priv->mut);
	pthread_mutex_unlock(&id_priv->mut);

	ret = 0;
free:
	free(mc);
	return ret;
}

static void ucma_complete_event(struct cma_id_private *id_priv)
{
	pthread_mutex_lock(&id_priv->mut);
	id_priv->events_completed++;
	pthread_cond_signal(&id_priv->cond);
	pthread_mutex_unlock(&id_priv->mut);
}

static void ucma_complete_mc_event(struct cma_multicast *mc)
{
	pthread_mutex_lock(&mc->id_priv->mut);
	mc->events_completed++;
	pthread_cond_signal(&mc->cond);
	mc->id_priv->events_completed++;
	pthread_cond_signal(&mc->id_priv->cond);
	pthread_mutex_unlock(&mc->id_priv->mut);
}

int rdma_ack_cm_event(struct rdma_cm_event *event)
{
	struct cma_event *evt;

	if (!event)
		return ERR(EINVAL);

	evt = container_of(event, struct cma_event, event);

	if (evt->mc)
		ucma_complete_mc_event(evt->mc);
	else
		ucma_complete_event(evt->id_priv);
	free(evt);
	return 0;
}

static void ucma_process_addr_resolved(struct cma_event *evt)
{
	if (af_ib_support) {
		evt->event.status = ucma_query_addr(&evt->id_priv->id);
		if (!evt->event.status &&
		    evt->id_priv->id.verbs->device->transport_type == IBV_TRANSPORT_IB)
			evt->event.status = ucma_query_gid(&evt->id_priv->id);
	} else {
		evt->event.status = ucma_query_route(&evt->id_priv->id);
	}

	if (evt->event.status)
		evt->event.event = RDMA_CM_EVENT_ADDR_ERROR;
}

static void ucma_process_route_resolved(struct cma_event *evt)
{
	if (evt->id_priv->id.verbs->device->transport_type != IBV_TRANSPORT_IB)
		return;

	if (af_ib_support)
		evt->event.status = ucma_query_path(&evt->id_priv->id);
	else
		evt->event.status = ucma_query_route(&evt->id_priv->id);

	if (evt->event.status)
		evt->event.event = RDMA_CM_EVENT_ROUTE_ERROR;
}

static int ucma_query_req_info(struct rdma_cm_id *id)
{
	int ret;

	if (!af_ib_support)
		return ucma_query_route(id);

	ret = ucma_query_addr(id);
	if (ret)
		return ret;

	ret = ucma_query_gid(id);
	if (ret)
		return ret;

	ret = ucma_query_path(id);
	if (ret)
		return ret;

	return 0;
}

static int ucma_process_conn_req(struct cma_event *evt,
				 uint32_t handle)
{
	struct cma_id_private *id_priv;
	int ret;

	id_priv = ucma_alloc_id(evt->id_priv->id.channel,
				evt->id_priv->id.context, evt->id_priv->id.ps,
				evt->id_priv->id.qp_type);
	if (!id_priv) {
		ucma_destroy_kern_id(evt->id_priv->id.channel->fd, handle);
		ret = ERR(ENOMEM);
		goto err1;
	}

	evt->event.listen_id = &evt->id_priv->id;
	evt->event.id = &id_priv->id;
	id_priv->handle = handle;
	ucma_insert_id(id_priv);
	id_priv->initiator_depth = evt->event.param.conn.initiator_depth;
	id_priv->responder_resources = evt->event.param.conn.responder_resources;

	if (evt->id_priv->sync) {
		ret = rdma_migrate_id(&id_priv->id, NULL);
		if (ret)
			goto err2;
	}

	ret = ucma_query_req_info(&id_priv->id);
	if (ret)
		goto err2;

	return 0;

err2:
	rdma_destroy_id(&id_priv->id);
err1:
	ucma_complete_event(evt->id_priv);
	return ret;
}

static int ucma_process_conn_resp(struct cma_id_private *id_priv)
{
	struct ucma_abi_accept cmd;
	int ret;

	ret = ucma_modify_qp_rtr(&id_priv->id, RDMA_MAX_RESP_RES);
	if (ret)
		goto err;

	ret = ucma_modify_qp_rts(&id_priv->id, RDMA_MAX_INIT_DEPTH);
	if (ret)
		goto err;

	CMA_INIT_CMD(&cmd, sizeof cmd, ACCEPT);
	cmd.id = id_priv->handle;

	ret = write(id_priv->id.channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd) {
		ret = (ret >= 0) ? ERR(ENODATA) : -1;
		goto err;
	}

	return 0;
err:
	ucma_modify_qp_err(&id_priv->id);
	return ret;
}

static int ucma_process_join(struct cma_event *evt)
{
	evt->mc->mgid = evt->event.param.ud.ah_attr.grh.dgid;
	evt->mc->mlid = evt->event.param.ud.ah_attr.dlid;

	if (!evt->id_priv->id.qp)
		return 0;

	return rdma_seterrno(ibv_attach_mcast(evt->id_priv->id.qp,
					      &evt->mc->mgid, evt->mc->mlid));
}

static void ucma_copy_conn_event(struct cma_event *event,
				 struct ucma_abi_conn_param *src)
{
	struct rdma_conn_param *dst = &event->event.param.conn;

	dst->private_data_len = src->private_data_len;
	if (src->private_data_len) {
		dst->private_data = &event->private_data;
		memcpy(&event->private_data, src->private_data,
		       src->private_data_len);
	}

	dst->responder_resources = src->responder_resources;
	dst->initiator_depth = src->initiator_depth;
	dst->flow_control = src->flow_control;
	dst->retry_count = src->retry_count;
	dst->rnr_retry_count = src->rnr_retry_count;
	dst->srq = src->srq;
	dst->qp_num = src->qp_num;
}

static void ucma_copy_ud_event(struct cma_event *event,
			       struct ucma_abi_ud_param *src)
{
	struct rdma_ud_param *dst = &event->event.param.ud;

	dst->private_data_len = src->private_data_len;
	if (src->private_data_len) {
		dst->private_data = &event->private_data;
		memcpy(&event->private_data, src->private_data,
		       src->private_data_len);
	}

	ibv_copy_ah_attr_from_kern(&dst->ah_attr, &src->ah_attr);
	dst->qp_num = src->qp_num;
	dst->qkey = src->qkey;
}

int rdma_get_cm_event(struct rdma_event_channel *channel,
		      struct rdma_cm_event **event)
{
	struct ucma_abi_event_resp resp;
	struct ucma_abi_get_event cmd;
	struct cma_event *evt;
	int ret;

	ret = ucma_init();
	if (ret)
		return ret;

	if (!event)
		return ERR(EINVAL);

	evt = malloc(sizeof(*evt));
	if (!evt)
		return ERR(ENOMEM);

retry:
	memset(evt, 0, sizeof(*evt));
	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, GET_EVENT, &resp, sizeof resp);
	ret = write(channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd) {
		free(evt);
		return (ret >= 0) ? ERR(ENODATA) : -1;
	}
	
	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	evt->event.event = resp.event;
	/*
	 * We should have a non-zero uid, except for connection requests.
	 * But a bug in older kernels can report a uid 0.  Work-around this
	 * issue by looking up the cma_id based on the kernel's id when the
	 * uid is 0 and we're processing a connection established event.
	 * In all other cases, if the uid is 0, we discard the event, like
	 * the kernel should have done.
	 */
	if (resp.uid) {
		evt->id_priv = (void *) (uintptr_t) resp.uid;
	} else {
		evt->id_priv = ucma_lookup_id(resp.id);
		if (!evt->id_priv) {
			syslog(LOG_WARNING, PFX "Warning: discarding unmatched "
				"event - rdma_destroy_id may hang.\n");
			goto retry;
		}
		if (resp.event != RDMA_CM_EVENT_ESTABLISHED) {
			ucma_complete_event(evt->id_priv);
			goto retry;
		}
	}
	evt->event.id = &evt->id_priv->id;
	evt->event.status = resp.status;

	switch (resp.event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ucma_process_addr_resolved(evt);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ucma_process_route_resolved(evt);
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		evt->id_priv = (void *) (uintptr_t) resp.uid;
		if (ucma_is_ud_qp(evt->id_priv->id.qp_type))
			ucma_copy_ud_event(evt, &resp.param.ud);
		else
			ucma_copy_conn_event(evt, &resp.param.conn);

		ret = ucma_process_conn_req(evt, resp.id);
		if (ret)
			goto retry;
		break;
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		ucma_copy_conn_event(evt, &resp.param.conn);
		evt->event.status = ucma_process_conn_resp(evt->id_priv);
		if (!evt->event.status)
			evt->event.event = RDMA_CM_EVENT_ESTABLISHED;
		else {
			evt->event.event = RDMA_CM_EVENT_CONNECT_ERROR;
			evt->id_priv->connect_error = 1;
		}
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		if (ucma_is_ud_qp(evt->id_priv->id.qp_type)) {
			ucma_copy_ud_event(evt, &resp.param.ud);
			break;
		}

		ucma_copy_conn_event(evt, &resp.param.conn);
		break;
	case RDMA_CM_EVENT_REJECTED:
		if (evt->id_priv->connect_error) {
			ucma_complete_event(evt->id_priv);
			goto retry;
		}
		ucma_copy_conn_event(evt, &resp.param.conn);
		ucma_modify_qp_err(evt->event.id);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		if (evt->id_priv->connect_error) {
			ucma_complete_event(evt->id_priv);
			goto retry;
		}
		ucma_copy_conn_event(evt, &resp.param.conn);
		break;
	case RDMA_CM_EVENT_MULTICAST_JOIN:
		evt->mc = (void *) (uintptr_t) resp.uid;
		evt->id_priv = evt->mc->id_priv;
		evt->event.id = &evt->id_priv->id;
		ucma_copy_ud_event(evt, &resp.param.ud);
		evt->event.param.ud.private_data = evt->mc->context;
		evt->event.status = ucma_process_join(evt);
		if (evt->event.status)
			evt->event.event = RDMA_CM_EVENT_MULTICAST_ERROR;
		break;
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		evt->mc = (void *) (uintptr_t) resp.uid;
		evt->id_priv = evt->mc->id_priv;
		evt->event.id = &evt->id_priv->id;
		evt->event.param.ud.private_data = evt->mc->context;
		break;
	default:
		evt->id_priv = (void *) (uintptr_t) resp.uid;
		evt->event.id = &evt->id_priv->id;
		evt->event.status = resp.status;
		if (ucma_is_ud_qp(evt->id_priv->id.qp_type))
			ucma_copy_ud_event(evt, &resp.param.ud);
		else
			ucma_copy_conn_event(evt, &resp.param.conn);
		break;
	}

	*event = &evt->event;
	return 0;
}

const char *rdma_event_str(enum rdma_cm_event_type event)
{
	switch (event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		return "RDMA_CM_EVENT_ADDR_RESOLVED";
	case RDMA_CM_EVENT_ADDR_ERROR:
		return "RDMA_CM_EVENT_ADDR_ERROR";
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		return "RDMA_CM_EVENT_ROUTE_RESOLVED";
	case RDMA_CM_EVENT_ROUTE_ERROR:
		return "RDMA_CM_EVENT_ROUTE_ERROR";
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		return "RDMA_CM_EVENT_CONNECT_REQUEST";
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		return "RDMA_CM_EVENT_CONNECT_RESPONSE";
	case RDMA_CM_EVENT_CONNECT_ERROR:
		return "RDMA_CM_EVENT_CONNECT_ERROR";
	case RDMA_CM_EVENT_UNREACHABLE:
		return "RDMA_CM_EVENT_UNREACHABLE";
	case RDMA_CM_EVENT_REJECTED:
		return "RDMA_CM_EVENT_REJECTED";
	case RDMA_CM_EVENT_ESTABLISHED:
		return "RDMA_CM_EVENT_ESTABLISHED";
	case RDMA_CM_EVENT_DISCONNECTED:
		return "RDMA_CM_EVENT_DISCONNECTED";
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		return "RDMA_CM_EVENT_DEVICE_REMOVAL";
	case RDMA_CM_EVENT_MULTICAST_JOIN:
		return "RDMA_CM_EVENT_MULTICAST_JOIN";
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		return "RDMA_CM_EVENT_MULTICAST_ERROR";
	case RDMA_CM_EVENT_ADDR_CHANGE:
		return "RDMA_CM_EVENT_ADDR_CHANGE";
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		return "RDMA_CM_EVENT_TIMEWAIT_EXIT";
	default:
		return "UNKNOWN EVENT";
	}
}

int rdma_set_option(struct rdma_cm_id *id, int level, int optname,
		    void *optval, size_t optlen)
{
	struct ucma_abi_set_option cmd;
	struct cma_id_private *id_priv;
	int ret;
	
	CMA_INIT_CMD(&cmd, sizeof cmd, SET_OPTION);
	id_priv = container_of(id, struct cma_id_private, id);
	cmd.id = id_priv->handle;
	cmd.optval = (uintptr_t) optval;
	cmd.level = level;
	cmd.optname = optname;
	cmd.optlen = optlen;

	ret = write(id->channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd)
		return (ret >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int rdma_migrate_id(struct rdma_cm_id *id, struct rdma_event_channel *channel)
{
	struct ucma_abi_migrate_resp resp;
	struct ucma_abi_migrate_id cmd;
	struct cma_id_private *id_priv;
	int ret, sync;

	id_priv = container_of(id, struct cma_id_private, id);
	if (id_priv->sync && !channel)
		return ERR(EINVAL);

	if ((sync = (channel == NULL))) {
		channel = rdma_create_event_channel();
		if (!channel)
			return -1;
	}

	CMA_INIT_CMD_RESP(&cmd, sizeof cmd, MIGRATE_ID, &resp, sizeof resp);
	cmd.id = id_priv->handle;
	cmd.fd = id->channel->fd;

	ret = write(channel->fd, &cmd, sizeof cmd);
	if (ret != sizeof cmd) {
		if (sync)
			rdma_destroy_event_channel(channel);
		return (ret >= 0) ? ERR(ENODATA) : -1;
	}

	VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	if (id_priv->sync) {
		if (id->event) {
			rdma_ack_cm_event(id->event);
			id->event = NULL;
		}
		rdma_destroy_event_channel(id->channel);
	}

	/*
	 * Eventually if we want to support migrating channels while events are
	 * being processed on the current channel, we need to block here while
	 * there are any outstanding events on the current channel for this id
	 * to prevent the user from processing events for this id on the old
	 * channel after this call returns.
	 */
	pthread_mutex_lock(&id_priv->mut);
	id_priv->sync = sync;
	id->channel = channel;
	while (id_priv->events_completed < resp.events_reported)
		pthread_cond_wait(&id_priv->cond, &id_priv->mut);
	pthread_mutex_unlock(&id_priv->mut);

	return 0;
}

static int ucma_passive_ep(struct rdma_cm_id *id, struct rdma_addrinfo *res,
			   struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
	struct cma_id_private *id_priv;
	int ret;

	if (af_ib_support)
		ret = rdma_bind_addr2(id, res->ai_src_addr, res->ai_src_len);
	else
		ret = rdma_bind_addr(id, res->ai_src_addr);
	if (ret)
		return ret;

	id_priv = container_of(id, struct cma_id_private, id);
	if (pd)
		id->pd = pd;

	if (qp_init_attr) {
		id_priv->qp_init_attr = malloc(sizeof(*qp_init_attr));
		if (!id_priv->qp_init_attr)
			return ERR(ENOMEM);

		*id_priv->qp_init_attr = *qp_init_attr;
		id_priv->qp_init_attr->qp_type = res->ai_qp_type;
	}

	return 0;
}

int rdma_create_ep(struct rdma_cm_id **id, struct rdma_addrinfo *res,
		   struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
	struct rdma_cm_id *cm_id;
	struct cma_id_private *id_priv;
	int ret;

	ret = rdma_create_id2(NULL, &cm_id, NULL, res->ai_port_space, res->ai_qp_type);
	if (ret)
		return ret;

	if (res->ai_flags & RAI_PASSIVE) {
		ret = ucma_passive_ep(cm_id, res, pd, qp_init_attr);
		if (ret)
			goto err;
		goto out;
	}

	if (af_ib_support)
		ret = rdma_resolve_addr2(cm_id, res->ai_src_addr, res->ai_src_len,
					 res->ai_dst_addr, res->ai_dst_len, 2000);
	else
		ret = rdma_resolve_addr(cm_id, res->ai_src_addr, res->ai_dst_addr, 2000);
	if (ret)
		goto err;

	if (res->ai_route_len) {
		ret = rdma_set_option(cm_id, RDMA_OPTION_IB, RDMA_OPTION_IB_PATH,
				      res->ai_route, res->ai_route_len);
		if (!ret)
			ret = ucma_complete(cm_id);
	} else {
		ret = rdma_resolve_route(cm_id, 2000);
	}
	if (ret)
		goto err;

	if (qp_init_attr) {
		qp_init_attr->qp_type = res->ai_qp_type;
		ret = rdma_create_qp(cm_id, pd, qp_init_attr);
		if (ret)
			goto err;
	}

	if (res->ai_connect_len) {
		id_priv = container_of(cm_id, struct cma_id_private, id);
		id_priv->connect = malloc(res->ai_connect_len);
		if (!id_priv->connect) {
			ret = ERR(ENOMEM);
			goto err;
		}
		memcpy(id_priv->connect, res->ai_connect, res->ai_connect_len);
		id_priv->connect_len = res->ai_connect_len;
	}

out:
	*id = cm_id;
	return 0;

err:
	rdma_destroy_ep(cm_id);
	return ret;
}

void rdma_destroy_ep(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;

	if (id->qp)
		rdma_destroy_qp(id);

	if (id->srq)
		rdma_destroy_srq(id);

	id_priv = container_of(id, struct cma_id_private, id);
	if (id_priv->qp_init_attr)
		free(id_priv->qp_init_attr);

	rdma_destroy_id(id);
}

int ucma_max_qpsize(struct rdma_cm_id *id)
{
	struct cma_id_private *id_priv;
	int i, max_size = 0;

	id_priv = container_of(id, struct cma_id_private, id);
	if (id && id_priv->cma_dev) {
		max_size = id_priv->cma_dev->max_qpsize;
	} else {
		ucma_init_all();
		for (i = 0; i < cma_dev_cnt; i++) {
			if (!max_size || max_size > cma_dev_array[i].max_qpsize)
				max_size = cma_dev_array[i].max_qpsize;
		}
	}
	return max_size;
}

__be16 ucma_get_port(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		return ((struct sockaddr_in *) addr)->sin_port;
	case AF_INET6:
		return ((struct sockaddr_in6 *) addr)->sin6_port;
	case AF_IB:
		return htobe16((uint16_t) be64toh(((struct sockaddr_ib *) addr)->sib_sid));
	default:
		return 0;
	}
}

__be16 rdma_get_src_port(struct rdma_cm_id *id)
{
	return ucma_get_port(&id->route.addr.src_addr);
}

__be16 rdma_get_dst_port(struct rdma_cm_id *id)
{
	return ucma_get_port(&id->route.addr.dst_addr);
}

