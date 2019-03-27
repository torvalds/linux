/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#define _GNU_SOURCE
#include <config.h>

#include <infiniband/endian.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "ibverbs.h"
#ifndef NRESOLVE_NEIGH
#include <net/if.h>
#include <net/if_arp.h>
#include "neigh.h"
#endif

/* Hack to avoid GCC's -Wmissing-prototypes and the similar error from sparse
   with these prototypes. Symbol versionining requires the goofy names, the
   prototype must match the version in verbs.h.
 */
int __ibv_query_device(struct ibv_context *context,
		       struct ibv_device_attr *device_attr);
int __ibv_query_port(struct ibv_context *context, uint8_t port_num,
		     struct ibv_port_attr *port_attr);
int __ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index,
		    union ibv_gid *gid);
int __ibv_query_pkey(struct ibv_context *context, uint8_t port_num, int index,
		     __be16 *pkey);
struct ibv_pd *__ibv_alloc_pd(struct ibv_context *context);
int __ibv_dealloc_pd(struct ibv_pd *pd);
struct ibv_mr *__ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			    int access);
int __ibv_rereg_mr(struct ibv_mr *mr, int flags, struct ibv_pd *pd, void *addr,
		   size_t length, int access);
int __ibv_dereg_mr(struct ibv_mr *mr);
struct ibv_cq *__ibv_create_cq(struct ibv_context *context, int cqe,
			       void *cq_context,
			       struct ibv_comp_channel *channel,
			       int comp_vector);
int __ibv_resize_cq(struct ibv_cq *cq, int cqe);
int __ibv_destroy_cq(struct ibv_cq *cq);
int __ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq,
		       void **cq_context);
void __ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents);
struct ibv_srq *__ibv_create_srq(struct ibv_pd *pd,
				 struct ibv_srq_init_attr *srq_init_attr);
int __ibv_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr,
		     int srq_attr_mask);
int __ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr);
int __ibv_destroy_srq(struct ibv_srq *srq);
struct ibv_qp *__ibv_create_qp(struct ibv_pd *pd,
			       struct ibv_qp_init_attr *qp_init_attr);
int __ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask,
		   struct ibv_qp_init_attr *init_attr);
int __ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
int __ibv_destroy_qp(struct ibv_qp *qp);
struct ibv_ah *__ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int __ibv_destroy_ah(struct ibv_ah *ah);
int __ibv_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
		       uint16_t lid);
int __ibv_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
		       uint16_t lid);

int __attribute__((const)) ibv_rate_to_mult(enum ibv_rate rate)
{
	switch (rate) {
	case IBV_RATE_2_5_GBPS: return  1;
	case IBV_RATE_5_GBPS:   return  2;
	case IBV_RATE_10_GBPS:  return  4;
	case IBV_RATE_20_GBPS:  return  8;
	case IBV_RATE_30_GBPS:  return 12;
	case IBV_RATE_40_GBPS:  return 16;
	case IBV_RATE_60_GBPS:  return 24;
	case IBV_RATE_80_GBPS:  return 32;
	case IBV_RATE_120_GBPS: return 48;
	default:           return -1;
	}
}

enum ibv_rate __attribute__((const)) mult_to_ibv_rate(int mult)
{
	switch (mult) {
	case 1:  return IBV_RATE_2_5_GBPS;
	case 2:  return IBV_RATE_5_GBPS;
	case 4:  return IBV_RATE_10_GBPS;
	case 8:  return IBV_RATE_20_GBPS;
	case 12: return IBV_RATE_30_GBPS;
	case 16: return IBV_RATE_40_GBPS;
	case 24: return IBV_RATE_60_GBPS;
	case 32: return IBV_RATE_80_GBPS;
	case 48: return IBV_RATE_120_GBPS;
	default: return IBV_RATE_MAX;
	}
}

int  __attribute__((const)) ibv_rate_to_mbps(enum ibv_rate rate)
{
	switch (rate) {
	case IBV_RATE_2_5_GBPS: return 2500;
	case IBV_RATE_5_GBPS:   return 5000;
	case IBV_RATE_10_GBPS:  return 10000;
	case IBV_RATE_20_GBPS:  return 20000;
	case IBV_RATE_30_GBPS:  return 30000;
	case IBV_RATE_40_GBPS:  return 40000;
	case IBV_RATE_60_GBPS:  return 60000;
	case IBV_RATE_80_GBPS:  return 80000;
	case IBV_RATE_120_GBPS: return 120000;
	case IBV_RATE_14_GBPS:  return 14062;
	case IBV_RATE_56_GBPS:  return 56250;
	case IBV_RATE_112_GBPS: return 112500;
	case IBV_RATE_168_GBPS: return 168750;
	case IBV_RATE_25_GBPS:  return 25781;
	case IBV_RATE_100_GBPS: return 103125;
	case IBV_RATE_200_GBPS: return 206250;
	case IBV_RATE_300_GBPS: return 309375;
	default:               return -1;
	}
}

enum ibv_rate __attribute__((const)) mbps_to_ibv_rate(int mbps)
{
	switch (mbps) {
	case 2500:   return IBV_RATE_2_5_GBPS;
	case 5000:   return IBV_RATE_5_GBPS;
	case 10000:  return IBV_RATE_10_GBPS;
	case 20000:  return IBV_RATE_20_GBPS;
	case 30000:  return IBV_RATE_30_GBPS;
	case 40000:  return IBV_RATE_40_GBPS;
	case 60000:  return IBV_RATE_60_GBPS;
	case 80000:  return IBV_RATE_80_GBPS;
	case 120000: return IBV_RATE_120_GBPS;
	case 14062:  return IBV_RATE_14_GBPS;
	case 56250:  return IBV_RATE_56_GBPS;
	case 112500: return IBV_RATE_112_GBPS;
	case 168750: return IBV_RATE_168_GBPS;
	case 25781:  return IBV_RATE_25_GBPS;
	case 103125: return IBV_RATE_100_GBPS;
	case 206250: return IBV_RATE_200_GBPS;
	case 309375: return IBV_RATE_300_GBPS;
	default:     return IBV_RATE_MAX;
	}
}

int __ibv_query_device(struct ibv_context *context,
		       struct ibv_device_attr *device_attr)
{
	return context->ops.query_device(context, device_attr);
}
default_symver(__ibv_query_device, ibv_query_device);

int __ibv_query_port(struct ibv_context *context, uint8_t port_num,
		     struct ibv_port_attr *port_attr)
{
	return context->ops.query_port(context, port_num, port_attr);
}
default_symver(__ibv_query_port, ibv_query_port);

int __ibv_query_gid(struct ibv_context *context, uint8_t port_num,
		    int index, union ibv_gid *gid)
{
	char name[24];
	char attr[41];
	uint16_t val;
	int i;

	snprintf(name, sizeof name, "ports/%d/gids/%d", port_num, index);

	if (ibv_read_sysfs_file(context->device->ibdev_path, name,
				attr, sizeof attr) < 0)
		return -1;

	for (i = 0; i < 8; ++i) {
		if (sscanf(attr + i * 5, "%hx", &val) != 1)
			return -1;
		gid->raw[i * 2    ] = val >> 8;
		gid->raw[i * 2 + 1] = val & 0xff;
	}

	return 0;
}
default_symver(__ibv_query_gid, ibv_query_gid);

int __ibv_query_pkey(struct ibv_context *context, uint8_t port_num,
		     int index, __be16 *pkey)
{
	char name[24];
	char attr[8];
	uint16_t val;

	snprintf(name, sizeof name, "ports/%d/pkeys/%d", port_num, index);

	if (ibv_read_sysfs_file(context->device->ibdev_path, name,
				attr, sizeof attr) < 0)
		return -1;

	if (sscanf(attr, "%hx", &val) != 1)
		return -1;

	*pkey = htobe16(val);
	return 0;
}
default_symver(__ibv_query_pkey, ibv_query_pkey);

struct ibv_pd *__ibv_alloc_pd(struct ibv_context *context)
{
	struct ibv_pd *pd;

	pd = context->ops.alloc_pd(context);
	if (pd)
		pd->context = context;

	return pd;
}
default_symver(__ibv_alloc_pd, ibv_alloc_pd);

int __ibv_dealloc_pd(struct ibv_pd *pd)
{
	return pd->context->ops.dealloc_pd(pd);
}
default_symver(__ibv_dealloc_pd, ibv_dealloc_pd);

struct ibv_mr *__ibv_reg_mr(struct ibv_pd *pd, void *addr,
			    size_t length, int access)
{
	struct ibv_mr *mr;

	if (ibv_dontfork_range(addr, length))
		return NULL;

	mr = pd->context->ops.reg_mr(pd, addr, length, access);
	if (mr) {
		mr->context = pd->context;
		mr->pd      = pd;
		mr->addr    = addr;
		mr->length  = length;
	} else
		ibv_dofork_range(addr, length);

	return mr;
}
default_symver(__ibv_reg_mr, ibv_reg_mr);

int __ibv_rereg_mr(struct ibv_mr *mr, int flags,
		   struct ibv_pd *pd, void *addr,
		   size_t length, int access)
{
	int dofork_onfail = 0;
	int err;
	void *old_addr;
	size_t old_len;

	if (flags & ~IBV_REREG_MR_FLAGS_SUPPORTED) {
		errno = EINVAL;
		return IBV_REREG_MR_ERR_INPUT;
	}

	if ((flags & IBV_REREG_MR_CHANGE_TRANSLATION) &&
	    (!length || !addr)) {
		errno = EINVAL;
		return IBV_REREG_MR_ERR_INPUT;
	}

	if (access && !(flags & IBV_REREG_MR_CHANGE_ACCESS)) {
		errno = EINVAL;
		return IBV_REREG_MR_ERR_INPUT;
	}

	if (!mr->context->ops.rereg_mr) {
		errno = ENOSYS;
		return IBV_REREG_MR_ERR_INPUT;
	}

	if (flags & IBV_REREG_MR_CHANGE_TRANSLATION) {
		err = ibv_dontfork_range(addr, length);
		if (err)
			return IBV_REREG_MR_ERR_DONT_FORK_NEW;
		dofork_onfail = 1;
	}

	old_addr = mr->addr;
	old_len = mr->length;
	err = mr->context->ops.rereg_mr(mr, flags, pd, addr, length, access);
	if (!err) {
		if (flags & IBV_REREG_MR_CHANGE_PD)
			mr->pd = pd;
		if (flags & IBV_REREG_MR_CHANGE_TRANSLATION) {
			mr->addr    = addr;
			mr->length  = length;
			err = ibv_dofork_range(old_addr, old_len);
			if (err)
				return IBV_REREG_MR_ERR_DO_FORK_OLD;
		}
	} else {
		err = IBV_REREG_MR_ERR_CMD;
		if (dofork_onfail) {
			if (ibv_dofork_range(addr, length))
				err = IBV_REREG_MR_ERR_CMD_AND_DO_FORK_NEW;
		}
	}

	return err;
}
default_symver(__ibv_rereg_mr, ibv_rereg_mr);

int __ibv_dereg_mr(struct ibv_mr *mr)
{
	int ret;
	void *addr	= mr->addr;
	size_t length	= mr->length;

	ret = mr->context->ops.dereg_mr(mr);
	if (!ret)
		ibv_dofork_range(addr, length);

	return ret;
}
default_symver(__ibv_dereg_mr, ibv_dereg_mr);

static struct ibv_comp_channel *ibv_create_comp_channel_v2(struct ibv_context *context)
{
	struct ibv_abi_compat_v2 *t = context->abi_compat;
	static int warned;

	if (!pthread_mutex_trylock(&t->in_use))
		return &t->channel;

	if (!warned) {
		fprintf(stderr, PFX "Warning: kernel's ABI version %d limits capacity.\n"
			"    Only one completion channel can be created per context.\n",
			abi_ver);
		++warned;
	}

	return NULL;
}

struct ibv_comp_channel *ibv_create_comp_channel(struct ibv_context *context)
{
	struct ibv_comp_channel            *channel;
	struct ibv_create_comp_channel      cmd;
	struct ibv_create_comp_channel_resp resp;

	if (abi_ver <= 2)
		return ibv_create_comp_channel_v2(context);

	channel = malloc(sizeof *channel);
	if (!channel)
		return NULL;

	IBV_INIT_CMD_RESP(&cmd, sizeof cmd, CREATE_COMP_CHANNEL, &resp, sizeof resp);
	if (write(context->cmd_fd, &cmd, sizeof cmd) != sizeof cmd) {
		free(channel);
		return NULL;
	}

	(void) VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof resp);

	channel->context = context;
	channel->fd      = resp.fd;
	channel->refcnt  = 0;

	return channel;
}

static int ibv_destroy_comp_channel_v2(struct ibv_comp_channel *channel)
{
	struct ibv_abi_compat_v2 *t = (struct ibv_abi_compat_v2 *) channel;
	pthread_mutex_unlock(&t->in_use);
	return 0;
}

int ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
	struct ibv_context *context;
	int ret;

	context = channel->context;
	pthread_mutex_lock(&context->mutex);

	if (channel->refcnt) {
		ret = EBUSY;
		goto out;
	}

	if (abi_ver <= 2) {
		ret = ibv_destroy_comp_channel_v2(channel);
		goto out;
	}

	close(channel->fd);
	free(channel);
	ret = 0;

out:
	pthread_mutex_unlock(&context->mutex);

	return ret;
}

struct ibv_cq *__ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context,
			       struct ibv_comp_channel *channel, int comp_vector)
{
	struct ibv_cq *cq;

	cq = context->ops.create_cq(context, cqe, channel, comp_vector);

	if (cq)
		verbs_init_cq(cq, context, channel, cq_context);

	return cq;
}
default_symver(__ibv_create_cq, ibv_create_cq);

int __ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
	if (!cq->context->ops.resize_cq)
		return ENOSYS;

	return cq->context->ops.resize_cq(cq, cqe);
}
default_symver(__ibv_resize_cq, ibv_resize_cq);

int __ibv_destroy_cq(struct ibv_cq *cq)
{
	struct ibv_comp_channel *channel = cq->channel;
	int ret;

	ret = cq->context->ops.destroy_cq(cq);

	if (channel) {
		if (!ret) {
			pthread_mutex_lock(&channel->context->mutex);
			--channel->refcnt;
			pthread_mutex_unlock(&channel->context->mutex);
		}
	}

	return ret;
}
default_symver(__ibv_destroy_cq, ibv_destroy_cq);

int __ibv_get_cq_event(struct ibv_comp_channel *channel,
		       struct ibv_cq **cq, void **cq_context)
{
	struct ibv_comp_event ev;

	if (read(channel->fd, &ev, sizeof ev) != sizeof ev)
		return -1;

	*cq         = (struct ibv_cq *) (uintptr_t) ev.cq_handle;
	*cq_context = (*cq)->cq_context;

	if ((*cq)->context->ops.cq_event)
		(*cq)->context->ops.cq_event(*cq);

	return 0;
}
default_symver(__ibv_get_cq_event, ibv_get_cq_event);

void __ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
	pthread_mutex_lock(&cq->mutex);
	cq->comp_events_completed += nevents;
	pthread_cond_signal(&cq->cond);
	pthread_mutex_unlock(&cq->mutex);
}
default_symver(__ibv_ack_cq_events, ibv_ack_cq_events);

struct ibv_srq *__ibv_create_srq(struct ibv_pd *pd,
				 struct ibv_srq_init_attr *srq_init_attr)
{
	struct ibv_srq *srq;

	if (!pd->context->ops.create_srq)
		return NULL;

	srq = pd->context->ops.create_srq(pd, srq_init_attr);
	if (srq) {
		srq->context          = pd->context;
		srq->srq_context      = srq_init_attr->srq_context;
		srq->pd               = pd;
		srq->events_completed = 0;
		pthread_mutex_init(&srq->mutex, NULL);
		pthread_cond_init(&srq->cond, NULL);
	}

	return srq;
}
default_symver(__ibv_create_srq, ibv_create_srq);

int __ibv_modify_srq(struct ibv_srq *srq,
		     struct ibv_srq_attr *srq_attr,
		     int srq_attr_mask)
{
	return srq->context->ops.modify_srq(srq, srq_attr, srq_attr_mask);
}
default_symver(__ibv_modify_srq, ibv_modify_srq);

int __ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
	return srq->context->ops.query_srq(srq, srq_attr);
}
default_symver(__ibv_query_srq, ibv_query_srq);

int __ibv_destroy_srq(struct ibv_srq *srq)
{
	return srq->context->ops.destroy_srq(srq);
}
default_symver(__ibv_destroy_srq, ibv_destroy_srq);

struct ibv_qp *__ibv_create_qp(struct ibv_pd *pd,
			       struct ibv_qp_init_attr *qp_init_attr)
{
	struct ibv_qp *qp = pd->context->ops.create_qp(pd, qp_init_attr);

	if (qp) {
		qp->context    	     = pd->context;
		qp->qp_context 	     = qp_init_attr->qp_context;
		qp->pd         	     = pd;
		qp->send_cq    	     = qp_init_attr->send_cq;
		qp->recv_cq    	     = qp_init_attr->recv_cq;
		qp->srq        	     = qp_init_attr->srq;
		qp->qp_type          = qp_init_attr->qp_type;
		qp->state	     = IBV_QPS_RESET;
		qp->events_completed = 0;
		pthread_mutex_init(&qp->mutex, NULL);
		pthread_cond_init(&qp->cond, NULL);
	}

	return qp;
}
default_symver(__ibv_create_qp, ibv_create_qp);

int __ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask,
		   struct ibv_qp_init_attr *init_attr)
{
	int ret;

	ret = qp->context->ops.query_qp(qp, attr, attr_mask, init_attr);
	if (ret)
		return ret;

	if (attr_mask & IBV_QP_STATE)
		qp->state = attr->qp_state;

	return 0;
}
default_symver(__ibv_query_qp, ibv_query_qp);

int __ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    int attr_mask)
{
	int ret;

	ret = qp->context->ops.modify_qp(qp, attr, attr_mask);
	if (ret)
		return ret;

	if (attr_mask & IBV_QP_STATE)
		qp->state = attr->qp_state;

	return 0;
}
default_symver(__ibv_modify_qp, ibv_modify_qp);

int __ibv_destroy_qp(struct ibv_qp *qp)
{
	return qp->context->ops.destroy_qp(qp);
}
default_symver(__ibv_destroy_qp, ibv_destroy_qp);

struct ibv_ah *__ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	struct ibv_ah *ah = pd->context->ops.create_ah(pd, attr);

	if (ah) {
		ah->context = pd->context;
		ah->pd      = pd;
	}

	return ah;
}
default_symver(__ibv_create_ah, ibv_create_ah);

/* GID types as appear in sysfs, no change is expected as of ABI
 * compatibility.
 */
#define V1_TYPE "IB/RoCE v1"
#define V2_TYPE "RoCE v2"
int ibv_query_gid_type(struct ibv_context *context, uint8_t port_num,
		       unsigned int index, enum ibv_gid_type *type)
{
	char name[32];
	char buff[11];

	snprintf(name, sizeof(name), "ports/%d/gid_attrs/types/%d", port_num,
		 index);

	/* Reset errno so that we can rely on its value upon any error flow in
	 * ibv_read_sysfs_file.
	 */
	errno = 0;
	if (ibv_read_sysfs_file(context->device->ibdev_path, name, buff,
				sizeof(buff)) <= 0) {
		char *dir_path;
		DIR *dir;

		if (errno == EINVAL) {
			/* In IB, this file doesn't exist and the kernel sets
			 * errno to -EINVAL.
			 */
			*type = IBV_GID_TYPE_IB_ROCE_V1;
			return 0;
		}
		if (asprintf(&dir_path, "%s/%s/%d/%s/",
			     context->device->ibdev_path, "ports", port_num,
			     "gid_attrs") < 0)
			return -1;
		dir = opendir(dir_path);
		free(dir_path);
		if (!dir) {
			if (errno == ENOENT)
				/* Assuming that if gid_attrs doesn't exist,
				 * we have an old kernel and all GIDs are
				 * IB/RoCE v1
				 */
				*type = IBV_GID_TYPE_IB_ROCE_V1;
			else
				return -1;
		} else {
			closedir(dir);
			errno = EFAULT;
			return -1;
		}
	} else {
		if (!strcmp(buff, V1_TYPE)) {
			*type = IBV_GID_TYPE_IB_ROCE_V1;
		} else if (!strcmp(buff, V2_TYPE)) {
			*type = IBV_GID_TYPE_ROCE_V2;
		} else {
			errno = ENOTSUP;
			return -1;
		}
	}

	return 0;
}

static int ibv_find_gid_index(struct ibv_context *context, uint8_t port_num,
			      union ibv_gid *gid, enum ibv_gid_type gid_type)
{
	enum ibv_gid_type sgid_type = 0;
	union ibv_gid sgid;
	int i = 0, ret;

	do {
		ret = ibv_query_gid(context, port_num, i, &sgid);
		if (!ret) {
			ret = ibv_query_gid_type(context, port_num, i,
						 &sgid_type);
		}
		i++;
	} while (!ret && (memcmp(&sgid, gid, sizeof(*gid)) ||
		 (gid_type != sgid_type)));

	return ret ? ret : i - 1;
}

static inline void map_ipv4_addr_to_ipv6(__be32 ipv4, struct in6_addr *ipv6)
{
	ipv6->s6_addr32[0] = 0;
	ipv6->s6_addr32[1] = 0;
	ipv6->s6_addr32[2] = htobe32(0x0000FFFF);
	ipv6->s6_addr32[3] = ipv4;
}

static inline __sum16 ipv4_calc_hdr_csum(uint16_t *data, unsigned int num_hwords)
{
	unsigned int i = 0;
	uint32_t sum = 0;

	for (i = 0; i < num_hwords; i++)
		sum += *(data++);

	sum = (sum & 0xffff) + (sum >> 16);

	return (__sum16)~sum;
}

static inline int get_grh_header_version(struct ibv_grh *grh)
{
	int ip6h_version = (be32toh(grh->version_tclass_flow) >> 28) & 0xf;
	struct ip *ip4h = (struct ip *)((void *)grh + 20);
	struct ip ip4h_checked;

	if (ip6h_version != 6) {
		if (ip4h->ip_v == 4)
			return 4;
		errno = EPROTONOSUPPORT;
		return -1;
	}
	/* version may be 6 or 4 */
	if (ip4h->ip_hl != 5) /* IPv4 header length must be 5 for RoCE v2. */
		return 6;
	/*
	* Verify checksum.
	* We can't write on scattered buffers so we have to copy to temp
	* buffer.
	*/
	memcpy(&ip4h_checked, ip4h, sizeof(ip4h_checked));
	/* Need to set the checksum field (check) to 0 before re-calculating
	 * the checksum.
	 */
	ip4h_checked.ip_sum = 0;
	ip4h_checked.ip_sum = ipv4_calc_hdr_csum((uint16_t *)&ip4h_checked, 10);
	/* if IPv4 header checksum is OK, believe it */
	if (ip4h->ip_sum == ip4h_checked.ip_sum)
		return 4;
	return 6;
}

static inline void set_ah_attr_generic_fields(struct ibv_ah_attr *ah_attr,
					      struct ibv_wc *wc,
					      struct ibv_grh *grh,
					      uint8_t port_num)
{
	uint32_t flow_class;

	flow_class = be32toh(grh->version_tclass_flow);
	ah_attr->grh.flow_label = flow_class & 0xFFFFF;
	ah_attr->dlid = wc->slid;
	ah_attr->sl = wc->sl;
	ah_attr->src_path_bits = wc->dlid_path_bits;
	ah_attr->port_num = port_num;
}

static inline int set_ah_attr_by_ipv4(struct ibv_context *context,
				      struct ibv_ah_attr *ah_attr,
				      struct ip *ip4h, uint8_t port_num)
{
	union ibv_gid sgid;
	int ret;

	/* No point searching multicast GIDs in GID table */
	if (IN_CLASSD(be32toh(ip4h->ip_dst.s_addr))) {
		errno = EINVAL;
		return -1;
	}

	map_ipv4_addr_to_ipv6(ip4h->ip_dst.s_addr, (struct in6_addr *)&sgid);
	ret = ibv_find_gid_index(context, port_num, &sgid,
				 IBV_GID_TYPE_ROCE_V2);
	if (ret < 0)
		return ret;

	map_ipv4_addr_to_ipv6(ip4h->ip_src.s_addr,
			      (struct in6_addr *)&ah_attr->grh.dgid);
	ah_attr->grh.sgid_index = (uint8_t) ret;
	ah_attr->grh.hop_limit = ip4h->ip_ttl;
	ah_attr->grh.traffic_class = ip4h->ip_tos;

	return 0;
}

#define IB_NEXT_HDR    0x1b
static inline int set_ah_attr_by_ipv6(struct ibv_context *context,
				  struct ibv_ah_attr *ah_attr,
				  struct ibv_grh *grh, uint8_t port_num)
{
	uint32_t flow_class;
	uint32_t sgid_type;
	int ret;

	/* No point searching multicast GIDs in GID table */
	if (grh->dgid.raw[0] == 0xFF) {
		errno = EINVAL;
		return -1;
	}

	ah_attr->grh.dgid = grh->sgid;
	if (grh->next_hdr == IPPROTO_UDP) {
		sgid_type = IBV_GID_TYPE_ROCE_V2;
	} else if (grh->next_hdr == IB_NEXT_HDR) {
		sgid_type = IBV_GID_TYPE_IB_ROCE_V1;
	} else {
		errno = EPROTONOSUPPORT;
		return -1;
	}

	ret = ibv_find_gid_index(context, port_num, &grh->dgid,
				 sgid_type);
	if (ret < 0)
		return ret;

	ah_attr->grh.sgid_index = (uint8_t) ret;
	flow_class = be32toh(grh->version_tclass_flow);
	ah_attr->grh.hop_limit = grh->hop_limit;
	ah_attr->grh.traffic_class = (flow_class >> 20) & 0xFF;

	return 0;
}

int ibv_init_ah_from_wc(struct ibv_context *context, uint8_t port_num,
			struct ibv_wc *wc, struct ibv_grh *grh,
			struct ibv_ah_attr *ah_attr)
{
	int version;
	int ret = 0;

	memset(ah_attr, 0, sizeof *ah_attr);
	set_ah_attr_generic_fields(ah_attr, wc, grh, port_num);

	if (wc->wc_flags & IBV_WC_GRH) {
		ah_attr->is_global = 1;
		version = get_grh_header_version(grh);

		if (version == 4)
			ret = set_ah_attr_by_ipv4(context, ah_attr,
						  (struct ip *)((void *)grh + 20),
						  port_num);
		else if (version == 6)
			ret = set_ah_attr_by_ipv6(context, ah_attr, grh,
						  port_num);
		else
			ret = -1;
	}

	return ret;
}

struct ibv_ah *ibv_create_ah_from_wc(struct ibv_pd *pd, struct ibv_wc *wc,
				     struct ibv_grh *grh, uint8_t port_num)
{
	struct ibv_ah_attr ah_attr;
	int ret;

	ret = ibv_init_ah_from_wc(pd->context, port_num, wc, grh, &ah_attr);
	if (ret)
		return NULL;

	return ibv_create_ah(pd, &ah_attr);
}

int __ibv_destroy_ah(struct ibv_ah *ah)
{
	return ah->context->ops.destroy_ah(ah);
}
default_symver(__ibv_destroy_ah, ibv_destroy_ah);

int __ibv_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	return qp->context->ops.attach_mcast(qp, gid, lid);
}
default_symver(__ibv_attach_mcast, ibv_attach_mcast);

int __ibv_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	return qp->context->ops.detach_mcast(qp, gid, lid);
}
default_symver(__ibv_detach_mcast, ibv_detach_mcast);

static inline int ipv6_addr_v4mapped(const struct in6_addr *a)
{
	return IN6_IS_ADDR_V4MAPPED(a) ||
		/* IPv4 encoded multicast addresses */
		(a->s6_addr32[0]  == htobe32(0xff0e0000) &&
		((a->s6_addr32[1] |
		 (a->s6_addr32[2] ^ htobe32(0x0000ffff))) == 0UL));
}

struct peer_address {
	void *address;
	uint32_t size;
};

static inline int create_peer_from_gid(int family, void *raw_gid,
				       struct peer_address *peer_address)
{
	switch (family) {
	case AF_INET:
		peer_address->address = raw_gid + 12;
		peer_address->size = 4;
		break;
	case AF_INET6:
		peer_address->address = raw_gid;
		peer_address->size = 16;
		break;
	default:
		return -1;
	}

	return 0;
}

#define NEIGH_GET_DEFAULT_TIMEOUT_MS 3000
int ibv_resolve_eth_l2_from_gid(struct ibv_context *context,
				struct ibv_ah_attr *attr,
				uint8_t eth_mac[ETHERNET_LL_SIZE],
				uint16_t *vid)
{
#ifndef NRESOLVE_NEIGH
	int dst_family;
	int src_family;
	int oif;
	struct get_neigh_handler neigh_handler;
	union ibv_gid sgid;
	int ether_len;
	struct peer_address src;
	struct peer_address dst;
	uint16_t ret_vid;
	int ret = -EINVAL;
	int err;

	err = ibv_query_gid(context, attr->port_num,
			    attr->grh.sgid_index, &sgid);

	if (err)
		return err;

	err = neigh_init_resources(&neigh_handler,
				   NEIGH_GET_DEFAULT_TIMEOUT_MS);

	if (err)
		return err;

	dst_family = ipv6_addr_v4mapped((struct in6_addr *)attr->grh.dgid.raw) ?
			AF_INET : AF_INET6;
	src_family = ipv6_addr_v4mapped((struct in6_addr *)sgid.raw) ?
			AF_INET : AF_INET6;

	if (create_peer_from_gid(dst_family, attr->grh.dgid.raw, &dst))
		goto free_resources;

	if (create_peer_from_gid(src_family, &sgid.raw, &src))
		goto free_resources;

	if (neigh_set_dst(&neigh_handler, dst_family, dst.address,
			  dst.size))
		goto free_resources;

	if (neigh_set_src(&neigh_handler, src_family, src.address,
			  src.size))
		goto free_resources;

	oif = neigh_get_oif_from_src(&neigh_handler);

	if (oif > 0)
		neigh_set_oif(&neigh_handler, oif);
	else
		goto free_resources;

	ret = -EHOSTUNREACH;

	/* blocking call */
	if (process_get_neigh(&neigh_handler))
		goto free_resources;

	ret_vid = neigh_get_vlan_id_from_dev(&neigh_handler);

	if (ret_vid <= 0xfff)
		neigh_set_vlan_id(&neigh_handler, ret_vid);

	/* We are using only Ethernet here */
	ether_len = neigh_get_ll(&neigh_handler,
				 eth_mac,
				 sizeof(uint8_t) * ETHERNET_LL_SIZE);

	if (ether_len <= 0)
		goto free_resources;

	*vid = ret_vid;

	ret = 0;

free_resources:
	neigh_free_resources(&neigh_handler);

	return ret;
#else
	return -ENOSYS;
#endif
}
