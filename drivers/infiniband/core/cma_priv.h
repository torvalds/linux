/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
 * Copyright (c) 1999-2005, Mellanox Technologies, Inc. All rights reserved.
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
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

#ifndef _CMA_PRIV_H
#define _CMA_PRIV_H

enum rdma_cm_state {
	RDMA_CM_IDLE,
	RDMA_CM_ADDR_QUERY,
	RDMA_CM_ADDR_RESOLVED,
	RDMA_CM_ROUTE_QUERY,
	RDMA_CM_ROUTE_RESOLVED,
	RDMA_CM_CONNECT,
	RDMA_CM_DISCONNECT,
	RDMA_CM_ADDR_BOUND,
	RDMA_CM_LISTEN,
	RDMA_CM_DEVICE_REMOVAL,
	RDMA_CM_DESTROYING
};

struct rdma_id_private {
	struct rdma_cm_id	id;

	struct rdma_bind_list	*bind_list;
	struct hlist_node	node;
	struct list_head	list; /* listen_any_list or cma_device.list */
	struct list_head	listen_list; /* per device listens */
	struct cma_device	*cma_dev;
	struct list_head	mc_list;

	int			internal_id;
	enum rdma_cm_state	state;
	spinlock_t		lock;
	struct mutex		qp_mutex;

	struct completion	comp;
	atomic_t		refcount;
	struct mutex		handler_mutex;

	int			backlog;
	int			timeout_ms;
	struct ib_sa_query	*query;
	int			query_id;
	union {
		struct ib_cm_id	*ib;
		struct iw_cm_id	*iw;
	} cm_id;

	u32			seq_num;
	u32			qkey;
	u32			qp_num;
	u32			options;
	u8			srq;
	u8			tos;
	bool			tos_set;
	u8			reuseaddr;
	u8			afonly;
	enum ib_gid_type	gid_type;

	/*
	 * Internal to RDMA/core, don't use in the drivers
	 */
	struct rdma_restrack_entry     res;
};

#if IS_ENABLED(CONFIG_INFINIBAND_ADDR_TRANS_CONFIGFS)
int cma_configfs_init(void);
void cma_configfs_exit(void);
#else
static inline int cma_configfs_init(void)
{
	return 0;
}

static inline void cma_configfs_exit(void)
{
}
#endif

void cma_ref_dev(struct cma_device *dev);
void cma_deref_dev(struct cma_device *dev);
typedef bool (*cma_device_filter)(struct ib_device *, void *);
struct cma_device *cma_enum_devices_by_ibdev(cma_device_filter filter,
					     void *cookie);
int cma_get_default_gid_type(struct cma_device *dev, unsigned int port);
int cma_set_default_gid_type(struct cma_device *dev, unsigned int port,
			     enum ib_gid_type default_gid_type);
int cma_get_default_roce_tos(struct cma_device *dev, unsigned int port);
int cma_set_default_roce_tos(struct cma_device *dev, unsigned int port,
			     u8 default_roce_tos);
struct ib_device *cma_get_ib_dev(struct cma_device *dev);

#endif /* _CMA_PRIV_H */
