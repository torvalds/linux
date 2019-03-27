/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <alloca.h>
#include <errno.h>

#include "ibverbs.h"

/* Hack to avoid GCC's -Wmissing-prototypes and the similar error from sparse
   with these prototypes. Symbol versionining requires the goofy names, the
   prototype must match the version in verbs.h.
 */
struct ibv_device **__ibv_get_device_list(int *num_devices);
void __ibv_free_device_list(struct ibv_device **list);
const char *__ibv_get_device_name(struct ibv_device *device);
__be64 __ibv_get_device_guid(struct ibv_device *device);
struct ibv_context *__ibv_open_device(struct ibv_device *device);
int __ibv_close_device(struct ibv_context *context);
int __ibv_get_async_event(struct ibv_context *context,
			  struct ibv_async_event *event);
void __ibv_ack_async_event(struct ibv_async_event *event);

static pthread_once_t device_list_once = PTHREAD_ONCE_INIT;
static int num_devices;
static struct ibv_device **device_list;

static void count_devices(void)
{
	num_devices = ibverbs_init(&device_list);
}

struct ibv_device **__ibv_get_device_list(int *num)
{
	struct ibv_device **l;
	int i;

	if (num)
		*num = 0;

	pthread_once(&device_list_once, count_devices);

	if (num_devices < 0) {
		errno = -num_devices;
		return NULL;
	}

	l = calloc(num_devices + 1, sizeof (struct ibv_device *));
	if (!l) {
		errno = ENOMEM;
		return NULL;
	}

	for (i = 0; i < num_devices; ++i)
		l[i] = device_list[i];
	if (num)
		*num = num_devices;

	return l;
}
default_symver(__ibv_get_device_list, ibv_get_device_list);

void __ibv_free_device_list(struct ibv_device **list)
{
	free(list);
}
default_symver(__ibv_free_device_list, ibv_free_device_list);

const char *__ibv_get_device_name(struct ibv_device *device)
{
	return device->name;
}
default_symver(__ibv_get_device_name, ibv_get_device_name);

__be64 __ibv_get_device_guid(struct ibv_device *device)
{
	char attr[24];
	uint64_t guid = 0;
	uint16_t parts[4];
	int i;

	if (ibv_read_sysfs_file(device->ibdev_path, "node_guid",
				attr, sizeof attr) < 0)
		return 0;

	if (sscanf(attr, "%hx:%hx:%hx:%hx",
		   parts, parts + 1, parts + 2, parts + 3) != 4)
		return 0;

	for (i = 0; i < 4; ++i)
		guid = (guid << 16) | parts[i];

	return htobe64(guid);
}
default_symver(__ibv_get_device_guid, ibv_get_device_guid);

void verbs_init_cq(struct ibv_cq *cq, struct ibv_context *context,
		       struct ibv_comp_channel *channel,
		       void *cq_context)
{
	cq->context		   = context;
	cq->channel		   = channel;

	if (cq->channel) {
		pthread_mutex_lock(&context->mutex);
		++cq->channel->refcnt;
		pthread_mutex_unlock(&context->mutex);
	}

	cq->cq_context		   = cq_context;
	cq->comp_events_completed  = 0;
	cq->async_events_completed = 0;
	pthread_mutex_init(&cq->mutex, NULL);
	pthread_cond_init(&cq->cond, NULL);
}

static struct ibv_cq_ex *
__lib_ibv_create_cq_ex(struct ibv_context *context,
		       struct ibv_cq_init_attr_ex *cq_attr)
{
	struct verbs_context *vctx = verbs_get_ctx(context);
	struct ibv_cq_ex *cq;

	if (cq_attr->wc_flags & ~IBV_CREATE_CQ_SUP_WC_FLAGS) {
		errno = EOPNOTSUPP;
		return NULL;
	}

	cq = vctx->priv->create_cq_ex(context, cq_attr);

	if (cq)
		verbs_init_cq(ibv_cq_ex_to_cq(cq), context,
			        cq_attr->channel, cq_attr->cq_context);

	return cq;
}

struct ibv_context *__ibv_open_device(struct ibv_device *device)
{
	struct verbs_device *verbs_device = verbs_get_device(device);
	char *devpath;
	int cmd_fd, ret;
	struct ibv_context *context;
	struct verbs_context *context_ex;

	if (asprintf(&devpath, "/dev/%s", device->dev_name) < 0)
		return NULL;

	/*
	 * We'll only be doing writes, but we need O_RDWR in case the
	 * provider needs to mmap() the file.
	 */
	cmd_fd = open(devpath, O_RDWR | O_CLOEXEC);
	free(devpath);

	if (cmd_fd < 0)
		return NULL;

	if (!verbs_device->ops->init_context) {
		context = verbs_device->ops->alloc_context(device, cmd_fd);
		if (!context)
			goto err;
	} else {
		struct verbs_ex_private *priv;

		/* Library now allocates the context */
		context_ex = calloc(1, sizeof(*context_ex) +
				       verbs_device->size_of_context);
		if (!context_ex) {
			errno = ENOMEM;
			goto err;
		}

		priv = calloc(1, sizeof(*priv));
		if (!priv) {
			errno = ENOMEM;
			free(context_ex);
			goto err;
		}

		context_ex->priv = priv;
		context_ex->context.abi_compat  = __VERBS_ABI_IS_EXTENDED;
		context_ex->sz = sizeof(*context_ex);

		context = &context_ex->context;
		ret = verbs_device->ops->init_context(verbs_device, context, cmd_fd);
		if (ret)
			goto verbs_err;
		/*
		 * In order to maintain backward/forward binary compatibility
		 * with apps compiled against libibverbs-1.1.8 that use the
		 * flow steering addition, we need to set the two
		 * ABI_placeholder entries to match the driver set flow
		 * entries.  This is because apps compiled against
		 * libibverbs-1.1.8 use an inline ibv_create_flow and
		 * ibv_destroy_flow function that looks in the placeholder
		 * spots for the proper entry points.  For apps compiled
		 * against libibverbs-1.1.9 and later, the inline functions
		 * will be looking in the right place.
		 */
		context_ex->ABI_placeholder1 = (void (*)(void)) context_ex->ibv_create_flow;
		context_ex->ABI_placeholder2 = (void (*)(void)) context_ex->ibv_destroy_flow;

		if (context_ex->create_cq_ex) {
			priv->create_cq_ex = context_ex->create_cq_ex;
			context_ex->create_cq_ex = __lib_ibv_create_cq_ex;
		}
	}

	context->device = device;
	context->cmd_fd = cmd_fd;
	pthread_mutex_init(&context->mutex, NULL);

	return context;

verbs_err:
	free(context_ex->priv);
	free(context_ex);
err:
	close(cmd_fd);
	return NULL;
}
default_symver(__ibv_open_device, ibv_open_device);

int __ibv_close_device(struct ibv_context *context)
{
	int async_fd = context->async_fd;
	int cmd_fd   = context->cmd_fd;
	int cq_fd    = -1;
	struct verbs_context *context_ex;
	struct verbs_device *verbs_device = verbs_get_device(context->device);

	context_ex = verbs_get_ctx(context);
	if (context_ex) {
		verbs_device->ops->uninit_context(verbs_device, context);
		free(context_ex->priv);
		free(context_ex);
	} else {
		verbs_device->ops->free_context(context);
	}

	close(async_fd);
	close(cmd_fd);
	if (abi_ver <= 2)
		close(cq_fd);

	return 0;
}
default_symver(__ibv_close_device, ibv_close_device);

int __ibv_get_async_event(struct ibv_context *context,
			  struct ibv_async_event *event)
{
	struct ibv_kern_async_event ev;

	if (read(context->async_fd, &ev, sizeof ev) != sizeof ev)
		return -1;

	event->event_type = ev.event_type;

	switch (event->event_type) {
	case IBV_EVENT_CQ_ERR:
		event->element.cq = (void *) (uintptr_t) ev.element;
		break;

	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_PATH_MIG_ERR:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
		event->element.qp = (void *) (uintptr_t) ev.element;
		break;

	case IBV_EVENT_SRQ_ERR:
	case IBV_EVENT_SRQ_LIMIT_REACHED:
		event->element.srq = (void *) (uintptr_t) ev.element;
		break;

	case IBV_EVENT_WQ_FATAL:
		event->element.wq = (void *) (uintptr_t) ev.element;
		break;
	default:
		event->element.port_num = ev.element;
		break;
	}

	if (context->ops.async_event)
		context->ops.async_event(event);

	return 0;
}
default_symver(__ibv_get_async_event, ibv_get_async_event);

void __ibv_ack_async_event(struct ibv_async_event *event)
{
	switch (event->event_type) {
	case IBV_EVENT_CQ_ERR:
	{
		struct ibv_cq *cq = event->element.cq;

		pthread_mutex_lock(&cq->mutex);
		++cq->async_events_completed;
		pthread_cond_signal(&cq->cond);
		pthread_mutex_unlock(&cq->mutex);

		return;
	}

	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_PATH_MIG_ERR:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
	{
		struct ibv_qp *qp = event->element.qp;

		pthread_mutex_lock(&qp->mutex);
		++qp->events_completed;
		pthread_cond_signal(&qp->cond);
		pthread_mutex_unlock(&qp->mutex);

		return;
	}

	case IBV_EVENT_SRQ_ERR:
	case IBV_EVENT_SRQ_LIMIT_REACHED:
	{
		struct ibv_srq *srq = event->element.srq;

		pthread_mutex_lock(&srq->mutex);
		++srq->events_completed;
		pthread_cond_signal(&srq->cond);
		pthread_mutex_unlock(&srq->mutex);

		return;
	}

	case IBV_EVENT_WQ_FATAL:
	{
		struct ibv_wq *wq = event->element.wq;

		pthread_mutex_lock(&wq->mutex);
		++wq->events_completed;
		pthread_cond_signal(&wq->cond);
		pthread_mutex_unlock(&wq->mutex);

		return;
	}

	default:
		return;
	}
}
default_symver(__ibv_ack_async_event, ibv_ack_async_event);
