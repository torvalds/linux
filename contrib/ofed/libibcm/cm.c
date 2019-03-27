/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
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
 *
 * $Id$
 */
#define _GNU_SOURCE
#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stddef.h>

#include <infiniband/cm.h>
#include <rdma/ib_user_cm.h>
#include <infiniband/driver.h>
#include <infiniband/marshall.h>

#define PFX "libibcm: "

#define IB_USER_CM_MIN_ABI_VERSION     4
#define IB_USER_CM_MAX_ABI_VERSION     5

static int abi_ver;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

enum {
	IB_UCM_MAX_DEVICES = 32
};

static inline int ERR(int err)
{
	errno = err;
	return -1;
}


#define CM_CREATE_MSG_CMD_RESP(msg, cmd, resp, type, size) \
do {                                        \
	struct ib_ucm_cmd_hdr *hdr;         \
                                            \
	size = sizeof(*hdr) + sizeof(*cmd); \
	msg = alloca(size);                 \
	if (!msg)                           \
		return ERR(ENOMEM);         \
	hdr = msg;                          \
	cmd = msg + sizeof(*hdr);           \
	hdr->cmd = type;                    \
	hdr->in  = sizeof(*cmd);            \
	hdr->out = sizeof(*resp);           \
	memset(cmd, 0, sizeof(*cmd));       \
	resp = alloca(sizeof(*resp));       \
	if (!resp)                          \
		return ERR(ENOMEM);         \
	cmd->response = (uintptr_t)resp;\
} while (0)

#define CM_CREATE_MSG_CMD(msg, cmd, type, size) \
do {                                        \
	struct ib_ucm_cmd_hdr *hdr;         \
                                            \
	size = sizeof(*hdr) + sizeof(*cmd); \
	msg = alloca(size);                 \
	if (!msg)                           \
		return ERR(ENOMEM);         \
	hdr = msg;                          \
	cmd = msg + sizeof(*hdr);           \
	hdr->cmd = type;                    \
	hdr->in  = sizeof(*cmd);            \
	hdr->out = 0;                       \
	memset(cmd, 0, sizeof(*cmd));       \
} while (0)

struct cm_id_private {
	struct ib_cm_id id;
	int events_completed;
	pthread_cond_t cond;
	pthread_mutex_t mut;
};

static int check_abi_version(void)
{
	char value[8];

	if (ibv_read_sysfs_file(ibv_get_sysfs_path(),
				"class/infiniband_cm/abi_version",
				value, sizeof value) < 0) {
		fprintf(stderr, PFX "couldn't read ABI version\n");
		return 0;
	}

	abi_ver = strtol(value, NULL, 10);
	if (abi_ver < IB_USER_CM_MIN_ABI_VERSION ||
	    abi_ver > IB_USER_CM_MAX_ABI_VERSION) {
		fprintf(stderr, PFX "kernel ABI version %d "
				"doesn't match library version %d.\n",
				abi_ver, IB_USER_CM_MAX_ABI_VERSION);
		return -1;
	}
	return 0;
}

static int ucm_init(void)
{
	int ret = 0;

	pthread_mutex_lock(&mut);
	if (!abi_ver)
		ret = check_abi_version();
	pthread_mutex_unlock(&mut);

	return ret;
}

static int ucm_get_dev_index(char *dev_name)
{
	char *dev_path;
	char ibdev[IBV_SYSFS_NAME_MAX];
	int i, ret;

	for (i = 0; i < IB_UCM_MAX_DEVICES; i++) {
		ret = asprintf(&dev_path, "/sys/class/infiniband_cm/ucm%d", i);
		if (ret < 0)
			return -1;

		ret = ibv_read_sysfs_file(dev_path, "ibdev", ibdev, sizeof ibdev);
		if (ret < 0)
			continue;

		if (!strcmp(dev_name, ibdev)) {
			free(dev_path);
			return i;
		}

		free(dev_path);
	}
	return -1;
}

struct ib_cm_device* ib_cm_open_device(struct ibv_context *device_context)
{
	struct ib_cm_device *dev;
	char *dev_path;
	int index, ret;

	if (ucm_init())
		return NULL;

	index = ucm_get_dev_index(device_context->device->name);
	if (index < 0)
		return NULL;

	dev = malloc(sizeof *dev);
	if (!dev)
		return NULL;

	dev->device_context = device_context;

	ret = asprintf(&dev_path, "/dev/ucm%d", index);
	if (ret < 0)
		goto err1;

	dev->fd = open(dev_path, O_RDWR);
	if (dev->fd < 0)
		goto err2;

	free(dev_path);
	return dev;

err2:
	free(dev_path);
err1:
	free(dev);
	return NULL;
}

void ib_cm_close_device(struct ib_cm_device *device)
{
	close(device->fd);
	free(device);
}

static void ib_cm_free_id(struct cm_id_private *cm_id_priv)
{
	pthread_cond_destroy(&cm_id_priv->cond);
	pthread_mutex_destroy(&cm_id_priv->mut);
	free(cm_id_priv);
}

static struct cm_id_private *ib_cm_alloc_id(struct ib_cm_device *device,
					    void *context)
{
	struct cm_id_private *cm_id_priv;

	cm_id_priv = malloc(sizeof *cm_id_priv);
	if (!cm_id_priv)
		return NULL;

	memset(cm_id_priv, 0, sizeof *cm_id_priv);
	cm_id_priv->id.device = device;
	cm_id_priv->id.context = context;
	pthread_mutex_init(&cm_id_priv->mut, NULL);
	if (pthread_cond_init(&cm_id_priv->cond, NULL))
		goto err;

	return cm_id_priv;

err:	ib_cm_free_id(cm_id_priv);
	return NULL;
}

int ib_cm_create_id(struct ib_cm_device *device,
		    struct ib_cm_id **cm_id, void *context)
{
	struct ib_ucm_create_id_resp *resp;
	struct ib_ucm_create_id *cmd;
	struct cm_id_private *cm_id_priv;
	void *msg;
	int result;
	int size;

	cm_id_priv = ib_cm_alloc_id(device, context);
	if (!cm_id_priv)
		return ERR(ENOMEM);

	CM_CREATE_MSG_CMD_RESP(msg, cmd, resp, IB_USER_CM_CMD_CREATE_ID, size);
	cmd->uid = (uintptr_t) cm_id_priv;

	result = write(device->fd, msg, size);
	if (result != size)
		goto err;

	VALGRIND_MAKE_MEM_DEFINED(resp, sizeof *resp);

	cm_id_priv->id.handle = resp->id;
	*cm_id = &cm_id_priv->id;
	return 0;

err:	ib_cm_free_id(cm_id_priv);
	return result;
}

int ib_cm_destroy_id(struct ib_cm_id *cm_id)
{
	struct ib_ucm_destroy_id_resp *resp;
	struct ib_ucm_destroy_id *cmd;
	struct cm_id_private *cm_id_priv;
	void *msg;
	int result;
	int size;
	
	CM_CREATE_MSG_CMD_RESP(msg, cmd, resp, IB_USER_CM_CMD_DESTROY_ID, size);
	cmd->id = cm_id->handle;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(resp, sizeof *resp);

	cm_id_priv = container_of(cm_id, struct cm_id_private, id);

	pthread_mutex_lock(&cm_id_priv->mut);
	while (cm_id_priv->events_completed < resp->events_reported)
		pthread_cond_wait(&cm_id_priv->cond, &cm_id_priv->mut);
	pthread_mutex_unlock(&cm_id_priv->mut);

	ib_cm_free_id(cm_id_priv);
	return 0;
}

int ib_cm_attr_id(struct ib_cm_id *cm_id, struct ib_cm_attr_param *param)
{
	struct ib_ucm_attr_id_resp *resp;
	struct ib_ucm_attr_id *cmd;
	void *msg;
	int result;
	int size;

	if (!param)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD_RESP(msg, cmd, resp, IB_USER_CM_CMD_ATTR_ID, size);
	cmd->id = cm_id->handle;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	VALGRIND_MAKE_MEM_DEFINED(resp, sizeof *resp);

	param->service_id   = resp->service_id;
	param->service_mask = resp->service_mask;
	param->local_id     = resp->local_id;
	param->remote_id    = resp->remote_id;
	return 0;
}

int ib_cm_init_qp_attr(struct ib_cm_id *cm_id,
		       struct ibv_qp_attr *qp_attr,
		       int *qp_attr_mask)
{
	struct ibv_kern_qp_attr *resp;
	struct ib_ucm_init_qp_attr *cmd;
	void *msg;
	int result;
	int size;

	if (!qp_attr || !qp_attr_mask)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD_RESP(msg, cmd, resp, IB_USER_CM_CMD_INIT_QP_ATTR, size);
	cmd->id = cm_id->handle;
	cmd->qp_state = qp_attr->qp_state;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : result;

	VALGRIND_MAKE_MEM_DEFINED(resp, sizeof *resp);

	*qp_attr_mask = resp->qp_attr_mask;
	ibv_copy_qp_attr_from_kern(qp_attr, resp);

	return 0;
}

int ib_cm_listen(struct ib_cm_id *cm_id,
		 __be64 service_id,
		 __be64 service_mask)
{
	struct ib_ucm_listen *cmd;
	void *msg;
	int result;
	int size;
	
	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_LISTEN, size);
	cmd->id           = cm_id->handle;
	cmd->service_id   = service_id;
	cmd->service_mask = service_mask;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_send_req(struct ib_cm_id *cm_id, struct ib_cm_req_param *param)
{
	struct ib_user_path_rec p_path;
	struct ib_user_path_rec *a_path;
	struct ib_ucm_req *cmd;
	void *msg;
	int result;
	int size;

	if (!param || !param->primary_path)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_REQ, size);
	cmd->id				= cm_id->handle;
	cmd->qpn			= param->qp_num;
	cmd->qp_type			= param->qp_type;
	cmd->psn			= param->starting_psn;
        cmd->sid			= param->service_id;
        cmd->peer_to_peer               = param->peer_to_peer;
        cmd->responder_resources        = param->responder_resources;
        cmd->initiator_depth            = param->initiator_depth;
        cmd->remote_cm_response_timeout = param->remote_cm_response_timeout;
        cmd->flow_control               = param->flow_control;
        cmd->local_cm_response_timeout  = param->local_cm_response_timeout;
        cmd->retry_count                = param->retry_count;
        cmd->rnr_retry_count            = param->rnr_retry_count;
        cmd->max_cm_retries             = param->max_cm_retries;
        cmd->srq                        = param->srq;

	ibv_copy_path_rec_to_kern(&p_path, param->primary_path);
	cmd->primary_path = (uintptr_t) &p_path;
		
	if (param->alternate_path) {
		a_path = alloca(sizeof(*a_path));
		if (!a_path)
			return ERR(ENOMEM);

		ibv_copy_path_rec_to_kern(a_path, param->alternate_path);
		cmd->alternate_path = (uintptr_t) a_path;
	}

	if (param->private_data && param->private_data_len) {
		cmd->data = (uintptr_t) param->private_data;
		cmd->len  = param->private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_send_rep(struct ib_cm_id *cm_id, struct ib_cm_rep_param *param)
{
	struct ib_ucm_rep *cmd;
	void *msg;
	int result;
	int size;

	if (!param)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_REP, size);
	cmd->uid = (uintptr_t) container_of(cm_id, struct cm_id_private, id);
	cmd->id			 = cm_id->handle;
	cmd->qpn		 = param->qp_num;
	cmd->psn		 = param->starting_psn;
        cmd->responder_resources = param->responder_resources;
        cmd->initiator_depth     = param->initiator_depth;
	cmd->target_ack_delay    = param->target_ack_delay;
	cmd->failover_accepted   = param->failover_accepted;
        cmd->flow_control        = param->flow_control;
        cmd->rnr_retry_count     = param->rnr_retry_count;
        cmd->srq                 = param->srq;

	if (param->private_data && param->private_data_len) {
		cmd->data = (uintptr_t) param->private_data;
		cmd->len  = param->private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

static inline int cm_send_private_data(struct ib_cm_id *cm_id,
				       uint32_t type,
				       void *private_data,
				       uint8_t private_data_len)
{
	struct ib_ucm_private_data *cmd;
	void *msg;
	int result;
	int size;

	CM_CREATE_MSG_CMD(msg, cmd, type, size);
	cmd->id = cm_id->handle;

	if (private_data && private_data_len) {
		cmd->data = (uintptr_t) private_data;
		cmd->len  = private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_send_rtu(struct ib_cm_id *cm_id,
		   void *private_data,
		   uint8_t private_data_len)
{
	return cm_send_private_data(cm_id, IB_USER_CM_CMD_SEND_RTU,
				    private_data, private_data_len);
}

int ib_cm_send_dreq(struct ib_cm_id *cm_id,
		    void *private_data,
		    uint8_t private_data_len)
{
	return cm_send_private_data(cm_id, IB_USER_CM_CMD_SEND_DREQ,
				    private_data, private_data_len);
}

int ib_cm_send_drep(struct ib_cm_id *cm_id,
		    void *private_data,
		    uint8_t private_data_len)
{
	return cm_send_private_data(cm_id, IB_USER_CM_CMD_SEND_DREP,
				    private_data, private_data_len);
}

static int cm_establish(struct ib_cm_id *cm_id)
{
	/* In kernel ABI 4 ESTABLISH was repurposed as NOTIFY and gained an
	   extra field. For some reason the compat definitions were deleted
	   from the uapi headers :( */
#define IB_USER_CM_CMD_ESTABLISH IB_USER_CM_CMD_NOTIFY
	struct cm_abi_establish { /* ABI 4 support */
		__u32 id;
	};

	struct cm_abi_establish *cmd;
	void *msg;
	int result;
	int size;
	
	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_ESTABLISH, size);
	cmd->id = cm_id->handle;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_notify(struct ib_cm_id *cm_id, enum ibv_event_type event)
{
	struct ib_ucm_notify *cmd;
	void *msg;
	int result;
	int size;
	
	if (abi_ver == 4) {
		if (event == IBV_EVENT_COMM_EST)
			return cm_establish(cm_id);
		else
			return ERR(EINVAL);
	}

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_NOTIFY, size);
	cmd->id = cm_id->handle;
	cmd->event = event;

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

static inline int cm_send_status(struct ib_cm_id *cm_id,
				 uint32_t type,
				 int status,
				 void *info,
				 uint8_t info_length,
				 void *private_data,
				 uint8_t private_data_len)
{
	struct ib_ucm_info *cmd;
	void *msg;
	int result;
	int size;

	CM_CREATE_MSG_CMD(msg, cmd, type, size);
	cmd->id     = cm_id->handle;
	cmd->status = status;

	if (private_data && private_data_len) {
		cmd->data     = (uintptr_t) private_data;
		cmd->data_len = private_data_len;
	}

	if (info && info_length) {
		cmd->info     = (uintptr_t) info;
		cmd->info_len = info_length;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_send_rej(struct ib_cm_id *cm_id,
		   enum ib_cm_rej_reason reason,
		   void *ari,
		   uint8_t ari_length,
		   void *private_data,
		   uint8_t private_data_len)
{
	return cm_send_status(cm_id, IB_USER_CM_CMD_SEND_REJ, reason, 
			      ari, ari_length,
			      private_data, private_data_len);
}

int ib_cm_send_apr(struct ib_cm_id *cm_id,
		   enum ib_cm_apr_status status,
		   void *info,
		   uint8_t info_length,
		   void *private_data,
		   uint8_t private_data_len)
{
	return cm_send_status(cm_id, IB_USER_CM_CMD_SEND_APR, status, 
			      info, info_length,
			      private_data, private_data_len);
}

int ib_cm_send_mra(struct ib_cm_id *cm_id,
		   uint8_t service_timeout,
		   void *private_data,
		   uint8_t private_data_len)
{
	struct ib_ucm_mra *cmd;
	void *msg;
	int result;
	int size;

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_MRA, size);
	cmd->id      = cm_id->handle;
	cmd->timeout = service_timeout;

	if (private_data && private_data_len) {
		cmd->data = (uintptr_t) private_data;
		cmd->len  = private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : result;

	return 0;
}

int ib_cm_send_lap(struct ib_cm_id *cm_id,
		   struct ibv_sa_path_rec *alternate_path,
		   void *private_data,
		   uint8_t private_data_len)
{
	struct ib_user_path_rec abi_path;
	struct ib_ucm_lap *cmd;
	void *msg;
	int result;
	int size;

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_LAP, size);
	cmd->id = cm_id->handle;

	ibv_copy_path_rec_to_kern(&abi_path, alternate_path);
	cmd->path = (uintptr_t) &abi_path;

	if (private_data && private_data_len) {
		cmd->data = (uintptr_t) private_data;
		cmd->len  = private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

int ib_cm_send_sidr_req(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_req_param *param)
{
	struct ib_user_path_rec abi_path;
	struct ib_ucm_sidr_req *cmd;
	void *msg;
	int result;
	int size;

	if (!param || !param->path)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_SIDR_REQ, size);
	cmd->id             = cm_id->handle;
	cmd->sid            = param->service_id;
	cmd->timeout        = param->timeout_ms;
	cmd->max_cm_retries = param->max_cm_retries;

	ibv_copy_path_rec_to_kern(&abi_path, param->path);
	cmd->path = (uintptr_t) &abi_path;

	if (param->private_data && param->private_data_len) {
		cmd->data = (uintptr_t) param->private_data;
		cmd->len  = param->private_data_len;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : result;

	return 0;
}

int ib_cm_send_sidr_rep(struct ib_cm_id *cm_id,
			struct ib_cm_sidr_rep_param *param)
{
	struct ib_ucm_sidr_rep *cmd;
	void *msg;
	int result;
	int size;

	if (!param)
		return ERR(EINVAL);

	CM_CREATE_MSG_CMD(msg, cmd, IB_USER_CM_CMD_SEND_SIDR_REP, size);
	cmd->id     = cm_id->handle;
	cmd->qpn    = param->qp_num;
	cmd->qkey   = param->qkey;
	cmd->status = param->status;

	if (param->private_data && param->private_data_len) {
		cmd->data     = (uintptr_t) param->private_data;
		cmd->data_len = param->private_data_len;
	}

	if (param->info && param->info_length) {
		cmd->info     = (uintptr_t) param->info;
		cmd->info_len = param->info_length;
	}

	result = write(cm_id->device->fd, msg, size);
	if (result != size)
		return (result >= 0) ? ERR(ENODATA) : -1;

	return 0;
}

static void cm_event_req_get(struct ib_cm_req_event_param *ureq,
			     struct ib_ucm_req_event_resp *kreq)
{
	ureq->remote_ca_guid             = kreq->remote_ca_guid;
	ureq->remote_qkey                = kreq->remote_qkey;
	ureq->remote_qpn                 = kreq->remote_qpn;
	ureq->qp_type                    = kreq->qp_type;
	ureq->starting_psn               = kreq->starting_psn;
	ureq->responder_resources        = kreq->responder_resources;
	ureq->initiator_depth            = kreq->initiator_depth;
	ureq->local_cm_response_timeout  = kreq->local_cm_response_timeout;
	ureq->flow_control               = kreq->flow_control;
	ureq->remote_cm_response_timeout = kreq->remote_cm_response_timeout;
	ureq->retry_count                = kreq->retry_count;
	ureq->rnr_retry_count            = kreq->rnr_retry_count;
	ureq->srq                        = kreq->srq;
	ureq->port			 = kreq->port;

	ibv_copy_path_rec_from_kern(ureq->primary_path, &kreq->primary_path);
	if (ureq->alternate_path)
		ibv_copy_path_rec_from_kern(ureq->alternate_path,
					    &kreq->alternate_path);
}

static void cm_event_rep_get(struct ib_cm_rep_event_param *urep,
			     struct ib_ucm_rep_event_resp *krep)
{
	urep->remote_ca_guid      = krep->remote_ca_guid;
	urep->remote_qkey         = krep->remote_qkey;
	urep->remote_qpn          = krep->remote_qpn;
	urep->starting_psn        = krep->starting_psn;
	urep->responder_resources = krep->responder_resources;
	urep->initiator_depth     = krep->initiator_depth;
	urep->target_ack_delay    = krep->target_ack_delay;
	urep->failover_accepted   = krep->failover_accepted;
	urep->flow_control        = krep->flow_control;
	urep->rnr_retry_count     = krep->rnr_retry_count;
	urep->srq                 = krep->srq;
}

static void cm_event_sidr_rep_get(struct ib_cm_sidr_rep_event_param *urep,
				  struct ib_ucm_sidr_rep_event_resp *krep)
{
	urep->status = krep->status;
	urep->qkey   = krep->qkey;
	urep->qpn    = krep->qpn;
};

int ib_cm_get_event(struct ib_cm_device *device, struct ib_cm_event **event)
{
	struct cm_id_private *cm_id_priv;
	struct ib_ucm_cmd_hdr *hdr;
	struct ib_ucm_event_get *cmd;
	struct ib_ucm_event_resp *resp;
	struct ib_cm_event *evt = NULL;
	struct ibv_sa_path_rec *path_a = NULL;
	struct ibv_sa_path_rec *path_b = NULL;
	void *data = NULL;
	void *info = NULL;
	void *msg;
	int result = 0;
	int size;
	
	if (!event)
		return ERR(EINVAL);

	size = sizeof(*hdr) + sizeof(*cmd);
	msg = alloca(size);
	if (!msg)
		return ERR(ENOMEM);

	hdr = msg;
	cmd = msg + sizeof(*hdr);

	hdr->cmd = IB_USER_CM_CMD_EVENT;
	hdr->in  = sizeof(*cmd);
	hdr->out = sizeof(*resp);

	memset(cmd, 0, sizeof(*cmd));

	resp = alloca(sizeof(*resp));
	if (!resp)
		return ERR(ENOMEM);
	
	cmd->response = (uintptr_t) resp;
	cmd->data_len = (uint8_t)(~0U);
	cmd->info_len = (uint8_t)(~0U);

	data = malloc(cmd->data_len);
	if (!data) {
		result = ERR(ENOMEM);
		goto done;
	}

	info = malloc(cmd->info_len);
	if (!info) {
		result = ERR(ENOMEM);
		goto done;
	}

	cmd->data = (uintptr_t) data;
	cmd->info = (uintptr_t) info;

	result = write(device->fd, msg, size);
	if (result != size) {
		result = (result >= 0) ? ERR(ENODATA) : -1;
		goto done;
	}

	VALGRIND_MAKE_MEM_DEFINED(resp, sizeof *resp);

	/*
	 * decode event.
	 */
	evt = malloc(sizeof(*evt));
	if (!evt) {
		result = ERR(ENOMEM);
		goto done;
	}
	memset(evt, 0, sizeof(*evt));
	evt->cm_id = (void *) (uintptr_t) resp->uid;
	evt->event = resp->event;

	if (resp->present & IB_UCM_PRES_PRIMARY) {
		path_a = malloc(sizeof(*path_a));
		if (!path_a) {
			result = ERR(ENOMEM);
			goto done;
		}
	}

	if (resp->present & IB_UCM_PRES_ALTERNATE) {
		path_b = malloc(sizeof(*path_b));
		if (!path_b) {
			result = ERR(ENOMEM);
			goto done;
		}
	}

	switch (evt->event) {
	case IB_CM_REQ_RECEIVED:
		evt->param.req_rcvd.listen_id = evt->cm_id;
		cm_id_priv = ib_cm_alloc_id(evt->cm_id->device,
					    evt->cm_id->context);
		if (!cm_id_priv) {
			result = ERR(ENOMEM);
			goto done;
		}
		cm_id_priv->id.handle = resp->id;
		evt->cm_id = &cm_id_priv->id;
		evt->param.req_rcvd.primary_path   = path_a;
		evt->param.req_rcvd.alternate_path = path_b;
		path_a = NULL;
		path_b = NULL;
		cm_event_req_get(&evt->param.req_rcvd, &resp->u.req_resp);
		break;
	case IB_CM_REP_RECEIVED:
		cm_event_rep_get(&evt->param.rep_rcvd, &resp->u.rep_resp);
		break;
	case IB_CM_MRA_RECEIVED:
		evt->param.mra_rcvd.service_timeout = resp->u.mra_resp.timeout;
		break;
	case IB_CM_REJ_RECEIVED:
		evt->param.rej_rcvd.reason = resp->u.rej_resp.reason;
		evt->param.rej_rcvd.ari = info;
		info = NULL;
		break;
	case IB_CM_LAP_RECEIVED:
		evt->param.lap_rcvd.alternate_path = path_b;
		path_b = NULL;
		ibv_copy_path_rec_from_kern(evt->param.lap_rcvd.alternate_path,
					    &resp->u.lap_resp.path);
		break;
	case IB_CM_APR_RECEIVED:
		evt->param.apr_rcvd.ap_status = resp->u.apr_resp.status;
		evt->param.apr_rcvd.apr_info = info;
		info = NULL;
		break;
	case IB_CM_SIDR_REQ_RECEIVED:
		evt->param.sidr_req_rcvd.listen_id = evt->cm_id;
		cm_id_priv = ib_cm_alloc_id(evt->cm_id->device,
					    evt->cm_id->context);
		if (!cm_id_priv) {
			result = ERR(ENOMEM);
			goto done;
		}
		cm_id_priv->id.handle = resp->id;
		evt->cm_id = &cm_id_priv->id;
		evt->param.sidr_req_rcvd.pkey = resp->u.sidr_req_resp.pkey;
		evt->param.sidr_req_rcvd.port = resp->u.sidr_req_resp.port;
		break;
	case IB_CM_SIDR_REP_RECEIVED:
		cm_event_sidr_rep_get(&evt->param.sidr_rep_rcvd,
				      &resp->u.sidr_rep_resp);
		evt->param.sidr_rep_rcvd.info = info;
		info = NULL;
		break;
	default:
		evt->param.send_status = resp->u.send_status;
		break;
	}

	if (resp->present & IB_UCM_PRES_DATA) {
		evt->private_data = data;
		data = NULL;
	}

	*event = evt;
	evt    = NULL;
	result = 0;
done:
	if (data)
		free(data);
	if (info)
		free(info);
	if (path_a)
		free(path_a);
	if (path_b)
		free(path_b);
	if (evt)
		free(evt);

	return result;
}

int ib_cm_ack_event(struct ib_cm_event *event)
{
	struct cm_id_private *cm_id_priv;

	if (!event)
		return ERR(EINVAL);

	if (event->private_data)
		free(event->private_data);

	cm_id_priv = container_of(event->cm_id, struct cm_id_private, id);

	switch (event->event) {
	case IB_CM_REQ_RECEIVED:
		cm_id_priv = container_of(event->param.req_rcvd.listen_id,
					  struct cm_id_private, id);
		free(event->param.req_rcvd.primary_path);
		if (event->param.req_rcvd.alternate_path)
			free(event->param.req_rcvd.alternate_path);
		break;
	case IB_CM_REJ_RECEIVED:
		if (event->param.rej_rcvd.ari)
			free(event->param.rej_rcvd.ari);
		break;
	case IB_CM_LAP_RECEIVED:
		free(event->param.lap_rcvd.alternate_path);
		break;
	case IB_CM_APR_RECEIVED:
		if (event->param.apr_rcvd.apr_info)
			free(event->param.apr_rcvd.apr_info);
		break;
	case IB_CM_SIDR_REQ_RECEIVED:
		cm_id_priv = container_of(event->param.sidr_req_rcvd.listen_id,
					  struct cm_id_private, id);
		break;
	case IB_CM_SIDR_REP_RECEIVED:
		if (event->param.sidr_rep_rcvd.info)
			free(event->param.sidr_rep_rcvd.info);
	default:
		break;
	}

	pthread_mutex_lock(&cm_id_priv->mut);
	cm_id_priv->events_completed++;
	pthread_cond_signal(&cm_id_priv->cond);
	pthread_mutex_unlock(&cm_id_priv->mut);

	free(event);
	return 0;
}
