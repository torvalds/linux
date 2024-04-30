// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hgsl.h"
#include "hgsl_hyp.h"
#include "hgsl_utils.h"
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/habmm.h>
#include <linux/sched.h>

// #define DO_RPC_TRACE
#ifdef DO_RPC_TRACE
#define RPC_TRACE()
#define RPC_TRACE_DONE()
#else
#define RPC_TRACE()
#define RPC_TRACE_DONE()
#endif

static const enum gsl_rpc_client_type_t g_client_type = GSL_RPC_CLIENT_TYPE_2;

#define gsl_rpc_send(data, size, channel) \
	gsl_rpc_send_(__func__, __LINE__, data, size, channel)
#define gsl_rpc_recv(data, size, channel, interruptible) \
	gsl_rpc_recv_(__func__, __LINE__, data, size, channel, interruptible)

static const uint32_t g_client_version = 1;

static const char * const gsl_rpc_func_names[] = {
	"RPC_LIBRARY_OPEN",
	"RPC_LIBRARY_CLOSE",
	"RPC_LIBRARY_VERSION",
	"RPC_LIBRARY_SET_MEMNOTIFY_TYPE",
	"RPC_DEVICE_OPEN",
	"RPC_DEVICE_CLOSE",
	"RPC_DEVICE_GETINFO",
	"RPC_DEVICE_GETINFO_EXT",
	"RPC_DEVICE_SETPOWERSTATE",
	"RPC_DEVICE_WAITIRQ",
	"RPC_DEVICE_GETIRQCNTRBASE",
	"RPC_DEVICE_DUMPSTATE",
	"RPC_COMMAND_ISSUEIB",
	"RPC_COMMAND_INSERTFENCE",
	"RPC_COMMAND_READTIMESTAMP",
	"RPC_COMMAND_ISSUEIB_SYNC",
	"RPC_COMMAND_ISSUEIB_WITH_ALLOC_LIST",
	"RPC_COMMAND_CHECKTIMESTAMP",
	"RPC_COMMAND_WAITTIMESTAMP",
	"RPC_COMMAND_FREEMEMONTIMESTAMP",
	"RPC_COMMAND_RESETSTATUS_INTERNAL",
	"RPC_CONTEXT_CREATE",
	"RPC_CONTEXT_DESTROY",
	"RPC_CONTEXT_BINDGMEMSHADOW",
	"RPC_CONTEXT_SETBINBASEOFFSET",
	"RPC_MEMORY_READ",
	"RPC_MEMORY_WRITE",
	"RPC_MEMORY_COPY",
	"RPC_MEMORY_SET",
	"RPC_MEMORY_QUERYSTATS",
	"RPC_MEMORY_ALLOC_PURE",
	"RPC_MEMORY_PHYS_ALLOC_PURE",
	"RPC_MEMORY_VIRT_ALLOC_PURE",
	"RPC_MEMORY_FREE_PURE",
	"RPC_MEMORY_CACHEOPERATION",
	"RPC_MEMORY_NOTIFY",
	"RPC_MEMORY_BIND",
	"RPC_MEMORY_BIND_SYNC",
	"RPC_MEMORY_MMAP",
	"RPC_MEMORY_MUNMAP",
	"RPC_MEMORY_CREATE_PAGETABLE",
	"RPC_MEMORY_DESTROY_PAGETABLE",
	"RPC_MEMORY_SET_PAGETABLE",
	"RPC_COMMAND_FREEMEMONTIMESTAMP_PURE",
	"RPC_PERFCOUNTER_SELECT",
	"RPC_PERFCOUNTER_DESELECT",
	"RPC_PERFCOUNTER_QUERYSELECTIONS",
	"RPC_PERFCOUNTER_READ",
	"RPC_SYNCOBJ_CREATE",
	"RPC_SYNCOBJ_CREATE_FROM_BIND",
	"RPC_SYNCOBJ_DESTROY",
	"RPC_SYNCOBJ_WAIT",
	"RPC_TIMESTAMP_CMP",
	"RPC_SYNCOBJ_CLONE",
	"RPC_SYNCOBJ_MERGE",
	"RPC_SYNCOBJ_MERGE_MULTIPLE",
	"RPC_SYNCSOURCE_CREATE",
	"RPC_SYNCSOURCE_DESTROY",
	"RPC_SYNCOBJ_CREATE_FROM_SOURCE",
	"RPC_SYNCOBJ_SIGNAL",
	"RPC_SYNCOBJ_WAIT_MULTIPLE",
	"RPC_DEVICE_DEBUG",
	"RPC_CFFDUMP_WAITIRQ",
	"RPC_CFFDUMP_WRITEVERIFYFILE",
	"RPC_MEMORY_MAP_EXT_FD_PURE", /* Linux extension. */
	"RPC_MEMORY_UNMAP_EXT_FD_PURE", /* Linux extension */
	"RPC_GET_SHADOWMEM",
	"RPC_PUT_SHADOWMEM",
	"RPC_BLIT",
	"RPC_HANDSHAKE",
	"RPC_SUB_HANDSHAKE",
	"RPC_DISCONNECT",
	"RPC_MEMORY_SET_METAINFO",
	"RPC_GET_SYSTEM_TIME",
	"RPC_GET_DBQ_INFO",
	"RPC_DBQ_CREATE",
	"RPC_PERFCOUNTERS_READ",
	"RPC_NOTIFY_CLEANUP",
	"RPC_COMMAND_RESETSTATUS",
	"RPC_CONTEXT_QUERY_DBCQ",
	"RPC_CONTEXT_REGISTER_DBCQ",
	"RPC_FUNC_LAST" // insert new func BEFORE this line!
};

static inline const char *hgsl_get_rpc_fname(unsigned int opcode)
{
	const char *fname = "Invalid opcode";

	if (opcode < RPC_FUNC_LAST)
		fname = gsl_rpc_func_names[opcode];

	return fname;
}

static int hgsl_rpc_connect(struct hgsl_hyp_priv_t *priv, int *socket)
{
	int err = 0;
	int tmp_socket = priv->conn_id;

	LOGI("connecting using conn_id %d", tmp_socket);
	err = gsl_hab_open(&tmp_socket);

	LOGI("socket_open err %d, socket %d", err, tmp_socket);
	*socket = tmp_socket;

	return err;
}

static int hgsl_rpc_parcel_init(struct hgsl_hab_channel_t       *hab_channel)
{
	int ret = gsl_rpc_parcel_init(&hab_channel->send_buf);

	if (!ret)
		ret = gsl_rpc_parcel_init(&hab_channel->recv_buf);
	return ret;
}

static int hgsl_rpc_parcel_reset(struct hgsl_hab_channel_t       *hab_channel)
{
	int ret = gsl_rpc_parcel_reset(&hab_channel->send_buf);

	if (!ret)
		ret = gsl_rpc_parcel_reset(&hab_channel->recv_buf);
	return ret;
}
/*---------------------------------------------------------------------------*/
static int gsl_rpc_send_(const char *fname, int line_num, void *data,
	size_t size, struct hgsl_hab_channel_t *hab_channel)
{
	int ret = gsl_hab_send(hab_channel->socket,
		(unsigned char *)data, size);

	if (ret)
		LOGE("failed to send @ %s:%d", fname, line_num);

	return ret;
}

/*---------------------------------------------------------------------------*/
static int gsl_rpc_recv_(const char *fname, int line_num, void *data,
	size_t size, struct hgsl_hab_channel_t *hab_channel, int interruptible)
{
	int ret = gsl_hab_recv(hab_channel->socket,
		(unsigned char *)data, size, interruptible);

	return ret;
}

static int gsl_rpc_transact_ext(uint32_t opcode, uint32_t version,
	struct hgsl_hab_channel_t *hab_channel, bool interruptible)
{
	int ret = -EINVAL;
	struct gsl_hab_payload *data = &hab_channel->send_buf;
	struct gsl_hab_payload *reply = &hab_channel->recv_buf;

	if (data && reply) {
		void *p_data;
		uint32_t data_size, max_size;
		uint32_t recv_opcode;

		if (hab_channel->wait_retry && interruptible) {
			ret = 0;
		} else if (hab_channel->wait_retry) {
			LOGE("channel is waiting for retry for uninterruptible RPC call");
			ret = -EINVAL;
			goto out;
		} else {
			gsl_rpc_set_call_params(data, opcode, version);

			ret = gsl_rpc_finalize(data);

			if (!ret) {
				ret = gsl_rpc_get_data_params(data,
					&p_data, &data_size, &max_size);
			} else {
				LOGE("failed to set footer, err %d", ret);
				goto out;
			}

			if (!ret) {
				ret = gsl_rpc_send(p_data,
					data->data_pos, hab_channel);
			} else {
				LOGE("failed to get data params, err %d", ret);
				goto out;
			}
		}

		if (!ret) {
			ret = gsl_rpc_get_data_params(reply,
				&p_data, &data_size, &max_size);
		} else {
			LOGE("failed to send data, err %d", ret);
			goto out;
		}

		if (!ret) {
			ret = gsl_rpc_recv(p_data, max_size, hab_channel, interruptible);
		} else {
			LOGE("failed to get data params, err %d", ret);
			goto out;
		}

		if (ret == -EINTR) {
			goto out;
		} else if (!ret) {
			ret = gsl_rpc_get_call_params(reply,
				&recv_opcode, NULL);
		} else {
			LOGE("failed to recv data, err %d", ret);
			goto out;
		}

		if (!ret) {
			if (recv_opcode != opcode) {
				if (opcode != RPC_DISCONNECT)
					LOGE("recv opcode %d (%s), expected %d (%s)",
						recv_opcode,
						hgsl_get_rpc_fname(recv_opcode),
						opcode,
						hgsl_get_rpc_fname(opcode));
				ret = -EINVAL;
			}
		} else {
			LOGE("failed to parse data, err %d", ret);
		}
	}

out:
	return ret;
}

static int gsl_rpc_transact(uint32_t opcode,
	struct hgsl_hab_channel_t *hab_channel)
{
	int ret = gsl_rpc_transact_ext(opcode, 0, hab_channel, false);

	if (ret == -EINTR) {
		LOGE("noninterruptible transaction was interrupted");
		ret = -EINVAL;
	}

	return ret;
}

static int gsl_rpc_transact_interrruptible(uint32_t opcode,
	struct hgsl_hab_channel_t *hab_channel)
{
	int ret = gsl_rpc_transact_ext(opcode, 0, hab_channel, true);

	if (ret == -EINTR) {
		hab_channel->busy = false;
		hab_channel->wait_retry = true;
	} else {
		hab_channel->wait_retry = false;
	}

	return ret;
}

static int rpc_handshake(struct hgsl_hyp_priv_t *priv,
	struct hgsl_hab_channel_t *hab_channel)
{
	int ret = 0;
	int rval = GSL_FAILURE;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	union {
		struct handshake_params_t v1;
		struct handshake_params_v2_t v2;
	} params;
	size_t params_size;
	int handshake_version;
	int tmp = 0;
	enum gsl_rpc_server_type_t server_type = GSL_RPC_SERVER_TYPE_LAST;
	enum gsl_rpc_server_mode_t server_mode = GSL_RPC_SERVER_MODE_LAST;

	RPC_TRACE();

	for (handshake_version = 2; handshake_version >= 1; handshake_version--) {
		ret = hgsl_rpc_parcel_reset(hab_channel);
		if (ret) {
			LOGE("hgsl_rpc_parcel_reset failed %d", ret);
			goto out;
		}

		send_buf = &hab_channel->send_buf;
		recv_buf = &hab_channel->recv_buf;

		switch (handshake_version) {
		case 1:
			params_size = sizeof(params.v1);
			params.v1.client_type = g_client_type;
			params.v1.client_version = g_client_version;
			params.v1.pid = priv->client_pid;
			params.v1.size = sizeof(params.v1);
			strscpy(params.v1.name, priv->client_name, sizeof(params.v1.name));
			LOGD("client process name is (%s), handshake version %d",
				params.v1.name, handshake_version);
			break;
		case 2:
			params_size = sizeof(params.v2);
			params.v2.client_type = g_client_type;
			params.v2.client_version = g_client_version;
			params.v2.pid = priv->client_pid;
			params.v2.size = sizeof(params.v2);
			strscpy(params.v2.name, priv->client_name, sizeof(params.v2.name));
			params.v2.uid = from_kuid(current_user_ns(), current_uid());
			LOGD("client process name is (%s), uid %u, handshake version %d",
				params.v2.name, params.v2.uid, handshake_version);
			break;
		default:
			LOGE("Unknown handshake version %d", handshake_version);
			ret = -EINVAL;
			goto out;
		}

		ret = gsl_rpc_write(send_buf, &params, params_size);
		if (ret) {
			LOGE("gsl_rpc_write failed %d", ret);
			goto out;
		}

		ret = gsl_rpc_transact_ext(RPC_HANDSHAKE, handshake_version, hab_channel, 0);
		if (ret) {
			LOGE("gsl_rpc_transact_ext failed %d", ret);
			goto out;
		}

		ret = gsl_rpc_read_int32_l(recv_buf, &rval);
		if (ret) {
			LOGE("gsl_rpc_read_int32_l failed %d", ret);
			goto out;
		}

		if (rval != GSL_SUCCESS) {
			LOGE("Handshake failed %d, BE sent error %d, try smaller version",
					handshake_version, rval);
		} else {
			LOGD("Handshake success %d", handshake_version);
			break;
		}
	}
	if (ret == 0 && rval == GSL_SUCCESS) {
		ret = gsl_rpc_read_int32_l(recv_buf, &priv->conn_id);
		if (ret) {
			LOGE("Failed to read conn_id %d", ret);
			goto out;
		}
		ret = gsl_rpc_read_int32_l(recv_buf, &tmp);
		if (ret) {
			LOGE("Failed to read server_type %d", ret);
			goto out;
		}
		server_type = (enum gsl_rpc_server_type_t)tmp;
		ret = gsl_rpc_read_int32_l(recv_buf, &tmp);
		if (ret) {
			LOGE("Failed to read server_mode %d", ret);
			goto out;
		}
		server_mode = (enum gsl_rpc_server_mode_t)tmp;
		LOGI("Successfully connected to server, got connection id %d",
			priv->conn_id);
	} else {
		LOGE("handshake failed, %d", ret);
	}

out:
	RPC_TRACE_DONE();
	return ret;
}

/*---------------------------------------------------------------------------*/
static int rpc_sub_handshake(struct hgsl_hyp_priv_t *priv,
	struct hgsl_hab_channel_t *hab_channel)
{
	int ret = 0;
	int rval = GSL_SUCCESS;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	struct sub_handshake_params_t params = { 0 };
	int server_allow_direct_writes = 0;

	RPC_TRACE();

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	params.size = sizeof(params);
	params.pid = priv->client_pid;
	params.memdesc_size = sizeof(struct gsl_memdesc_t);

	ret = gsl_rpc_write(send_buf, &params, sizeof(params));
	if (ret) {
		LOGE("gsl_rpc_write failed %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact_ext(RPC_SUB_HANDSHAKE, 1, hab_channel, 0);
	if (ret) {
		LOGE("gsl_rpc_transact_ext failed %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if ((!ret) && (rval != GSL_SUCCESS)) {
		LOGE("BE sent error %d", rval);
		ret = -EINVAL;
	}

	if (!ret) {
		ret = gsl_rpc_read_int32_l(recv_buf, &server_allow_direct_writes);
		if (ret) {
			LOGE("Failed to read server_allow_direct_writes %d", ret);
			goto out;
		}
	} else {
		LOGE("sub handshake failed, %d", ret);
	}

out:
	RPC_TRACE_DONE();
	return ret;
}

static void hgsl_hyp_close_channel(struct hgsl_hab_channel_t *hab_channel)
{
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = GSL_SUCCESS;

	if (hab_channel == NULL) {
		LOGE("hab_channel is NULL");
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (hab_channel->socket != HAB_INVALID_HANDLE) {
		ret = hgsl_rpc_parcel_reset(hab_channel);
		if (ret)
			LOGE("hgsl_rpc_parcel_reset failed, %d", ret);
		else
			ret = gsl_rpc_transact(RPC_DISCONNECT, hab_channel);

		if (!ret) {
			ret = gsl_rpc_read_int32_l(recv_buf, &rval);
			if (ret || rval)
				LOGE("RPC_DISCONNECT failed %d, %d", ret, rval);
		}
		if (hab_channel->socket != HAB_INVALID_HANDLE) {
			gsl_hab_close(hab_channel->socket);
			hab_channel->socket = HAB_INVALID_HANDLE;
		}
	}

	gsl_rpc_parcel_free(send_buf);
	gsl_rpc_parcel_free(recv_buf);

	if (hab_channel->id >= 0)
		idr_remove(&hab_channel->priv->channel_idr, hab_channel->id);

	hgsl_free(hab_channel);
out:
	return;
}

static int hgsl_rpc_create_channel(
	struct hgsl_hyp_priv_t *priv,
	struct hgsl_hab_channel_t **channel)
{
	int socket = HAB_INVALID_HANDLE;
	int ret = -ENOMEM;
	struct hgsl_hab_channel_t *hab_channel
		= (struct hgsl_hab_channel_t *)hgsl_zalloc(
					sizeof(struct hgsl_hab_channel_t));

	if (hab_channel == NULL) {
		LOGE("Failed to allocate hab_channel");
		goto out;
	}

	hab_channel->socket = HAB_INVALID_HANDLE;
	hab_channel->priv = priv;
	hab_channel->busy = false;
	hab_channel->wait_retry = false;
	hab_channel->id = idr_alloc(&priv->channel_idr, hab_channel,
								1, 0, GFP_NOWAIT);
	if (hab_channel->id < 0) {
		LOGE("Failed to allocate id for hab channel");
		ret = hab_channel->id;
		goto out;
	}

	ret = hgsl_rpc_parcel_init(hab_channel);
	if (ret) {
		LOGE("Failed to init parcel");
		goto out;
	}

	if (priv->conn_id == 0) {
		ret = hgsl_rpc_connect(priv, &socket);
		if (ret) {
			LOGE("Failed to open socket %d", ret);
			goto out;
		}
		hab_channel->socket = socket;
		ret = rpc_handshake(priv, hab_channel);
		if (ret)
			LOGE("rpc_handshake failed %d", ret);
		gsl_hab_close(socket);
		hab_channel->socket = HAB_INVALID_HANDLE;
	}

	ret = hgsl_rpc_connect(priv, &socket);
	if (ret) {
		LOGE("Failed to open socket %d", ret);
		goto out;
	}
	hab_channel->socket = socket;
	ret = rpc_sub_handshake(priv, hab_channel);
	if (ret) {
		LOGE("sub handshake failed %d", ret);
		gsl_hab_close(socket);
		hab_channel->socket = HAB_INVALID_HANDLE;
	}

out:
	if (ret) {
		LOGE("Failed to create channel %d exiting", ret);
		if (hab_channel != NULL) {
			hgsl_hyp_close_channel(hab_channel);
			hab_channel = NULL;
		}
	} else {
		*channel = hab_channel;
	}
	return ret;
}

void hgsl_hyp_channel_pool_put_unsafe(struct hgsl_hab_channel_t *hab_channel)
{
	if (hab_channel != NULL) {
		struct hgsl_hyp_priv_t *priv = hab_channel->priv;

		if (hab_channel->wait_retry)
			LOGE("put channel waiting for retry");
		hab_channel->busy = false;
		list_del(&hab_channel->node);
		list_add_tail(&hab_channel->node, &priv->free_channels);
		LOGD("put %p back to free pool", hab_channel);
	}
}

void hgsl_hyp_channel_pool_put(struct hgsl_hab_channel_t *hab_channel)
{
	if (hab_channel != NULL) {
		struct hgsl_hyp_priv_t *priv = hab_channel->priv;

		mutex_lock(&priv->lock);
		hgsl_hyp_channel_pool_put_unsafe(hab_channel);
		mutex_unlock(&priv->lock);
	}
}

static int hgsl_hyp_channel_pool_get_by_id(
	struct hgsl_hyp_priv_t *priv, int id, struct hgsl_hab_channel_t **channel)
{
	struct hgsl_hab_channel_t *hab_channel = NULL;
	int ret = -EINVAL;

	if (id > 0)
		hab_channel = idr_find(&priv->channel_idr, id);

	if (hab_channel) {
		if (!hab_channel->wait_retry) {
			hab_channel = NULL;
			LOGE("channel %d isn't waiting for retry", id);
		}
	} else {
		LOGE("can't find id %d", id);
	}

	if (hab_channel) {
		ret = 0;
		*channel = hab_channel;
	}

	return ret;
}

int hgsl_hyp_channel_pool_get(
	struct hgsl_hyp_priv_t *priv, int id, struct hgsl_hab_channel_t **channel)
{
	struct hgsl_hab_channel_t *hab_channel = NULL;
	int ret = 0;

	if (!channel)
		return -EINVAL;

	mutex_lock(&priv->lock);
	if (id) {
		ret = hgsl_hyp_channel_pool_get_by_id(priv, id, &hab_channel);
		if (ret)
			LOGE("Failed to find channel %d, ret %d", id, ret);
	} else {
		if (list_empty(&priv->free_channels)) {
			ret = hgsl_rpc_create_channel(priv, &hab_channel);
			LOGD("hgsl_rpc_create_channel returned, ret %d hab_channel %p",
				ret, hab_channel);
		} else {
			hab_channel = container_of(priv->free_channels.next,
				struct hgsl_hab_channel_t, node);
			if (hab_channel != NULL) {
				list_del(&hab_channel->node);
				LOGD("get %p from free pool", hab_channel);
			} else {
				ret = -EINVAL;
				LOGE("invalid hab_channel in the list");
			}
		}

		if (!ret)
			list_add_tail(&hab_channel->node, &priv->busy_channels);
	}

	if (!ret) {
		*channel = hab_channel;
		hab_channel->busy = true;
	}
	mutex_unlock(&priv->lock);

	if ((!ret) && (!id)) {
		ret = hgsl_rpc_parcel_reset(hab_channel);
		if (ret) {
			LOGE("hgsl_rpc_parcel_reset failed %d", ret);
			hgsl_hyp_channel_pool_put(hab_channel);
			hab_channel = NULL;
		}
	}

	return ret;
}

static int hgsl_hyp_channel_pool_init(struct hgsl_hyp_priv_t *priv,
	int client_pid, const char * const client_name)
{
	INIT_LIST_HEAD(&priv->free_channels);
	INIT_LIST_HEAD(&priv->busy_channels);
	mutex_init(&priv->lock);
	priv->conn_id = 0;
	strscpy(priv->client_name, client_name, sizeof(priv->client_name));
	priv->client_pid = client_pid;
	idr_init(&priv->channel_idr);

	LOGD("pid %d, task name %s"
		, (int) priv->client_pid, priv->client_name);

	return 0;
}

static void hgsl_hyp_put_waiting_channel(struct hgsl_hyp_priv_t *priv)
{
	struct hgsl_hab_channel_t *channel = NULL;
	struct hgsl_hab_channel_t *tmp = NULL;

	list_for_each_entry_safe(channel, tmp, &priv->busy_channels, node) {
		if (!channel->busy) {
			channel->wait_retry = false;
			hgsl_hyp_channel_pool_put_unsafe(channel);
		}
	}
}

static void hgsl_hyp_channel_pool_close(struct hgsl_hyp_priv_t *priv)
{
	struct hgsl_hab_channel_t *channel = NULL;
	struct hgsl_hab_channel_t *tmp = NULL;

	mutex_lock(&priv->lock);
	hgsl_hyp_put_waiting_channel(priv);
	while (!list_empty(&priv->busy_channels)) {
		mutex_unlock(&priv->lock);
		usleep_range(100, 1000);
		mutex_lock(&priv->lock);
		hgsl_hyp_put_waiting_channel(priv);
	}

	list_for_each_entry_safe(channel, tmp, &priv->free_channels, node) {
		list_del(&channel->node);
		hgsl_hyp_close_channel(channel);
	}
	mutex_unlock(&priv->lock);
	mutex_destroy(&priv->lock);
	memset(priv, 0, sizeof(*priv));
}

int hgsl_hyp_init(struct hgsl_hyp_priv_t *priv, struct device *dev,
	int client_pid, const char * const client_name)
{
	priv->dev = dev;
	return hgsl_hyp_channel_pool_init(priv, client_pid, client_name);
}

void hgsl_hyp_close(struct hgsl_hyp_priv_t *priv)
{
	hgsl_hyp_channel_pool_close(priv);
}

int hgsl_hyp_generic_transaction(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_hyp_generic_transaction_params *params,
	void **pSend, void **pReply, void *pRval)
{
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	unsigned int i = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	for (i = 0; i < params->send_num; i++) {
		ret = gsl_rpc_write(send_buf, pSend[i], params->send_size[i]);
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
	}

	ret = gsl_rpc_transact(params->cmd_id, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	for (i = 0; i < params->reply_num; i++) {
		ret = gsl_rpc_read(recv_buf, pReply[i], params->reply_size[i]);
		if (ret) {
			LOGE("gsl_rpc_read failed, %d", ret);
			goto out;
		}
	}
	if (pRval != NULL) {
		ret = gsl_rpc_read_int32_l(recv_buf, pRval);
		if (ret) {
			LOGE("gsl_rpc_read_int32_l failed, %d", ret);
			goto out;
		}
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	if (params->cmd_id < RPC_FUNC_LAST) {
		LOGD("cmd %d %s, ret %d, rval %p %d", params->cmd_id,
			hgsl_get_rpc_fname(params->cmd_id),
			ret, pRval, pRval ? *((int *)pRval) : 0);
	} else {
		LOGE("unknown cmd id %d", params->cmd_id);
	}
	return ret;
}

int hgsl_hyp_gsl_lib_open(struct hgsl_hyp_priv_t *priv,
	uint32_t flags, int32_t *rval)
{
	struct library_open_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size = sizeof(rpc_params);
	rpc_params.flags = flags;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_LIBRARY_OPEN, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

out:
	LOGD("%d, %d", ret, *rval);
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;

}

int hgsl_hyp_ctxt_create(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_ioctl_ctxt_create_params *hgsl_params)
{
	struct context_create_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size = sizeof(rpc_params);
	rpc_params.devhandle = hgsl_params->devhandle;
	rpc_params.type = hgsl_params->type;
	rpc_params.flags = hgsl_params->flags;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_CONTEXT_CREATE, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &hgsl_params->ctxthandle);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

out:
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_ctxt_destroy(struct hgsl_hab_channel_t *hab_channel,
	uint32_t devhandle, uint32_t context_id, uint32_t *rval, uint32_t dbcq_export_id)
{
	struct context_destroy_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size = sizeof(rpc_params);
	rpc_params.devhandle = devhandle;
	rpc_params.ctxthandle = context_id;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_CONTEXT_DESTROY, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

	if (dbcq_export_id) {
		ret = habmm_unexport(hab_channel->socket, dbcq_export_id, 0);
		if (ret)
			LOGE("Failed to unexport context queue, %d, export_id %d",
			context_id, dbcq_export_id);
	}

	if (rval) {
		ret = gsl_rpc_read_uint32_l(recv_buf, rval);
		if (ret) {
			LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
			goto out;
		}
	}

out:
	LOGD("%d %d", context_id, ret);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_get_shadowts_mem(struct hgsl_hab_channel_t *hab_channel,
	uint32_t context_id, uint32_t *shadow_ts_flags,
	struct hgsl_mem_node *mem_node)
{
	struct get_shadowmem_params_v1_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	struct shadowprop_t rpc_shadow = { 0 };
	int ret = 0;
	size_t aligned_size = 0;
	struct dma_buf *dma_buf = NULL;
	uint32_t export_id;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size = sizeof(rpc_params);
	rpc_params.device_id = GSL_DEVICE_3D;
	rpc_params.ctxthandle = context_id;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact_ext(RPC_GET_SHADOWMEM, 1, hab_channel, 0);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read(recv_buf, &rpc_shadow, sizeof(rpc_shadow));
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &export_id);
	if ((ret) || (export_id == 0)) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

	*shadow_ts_flags = rpc_shadow.flags;
	mem_node->memdesc.size64 = (uint64_t)rpc_shadow.sizebytes;
	mem_node->fd = -1;

	if (rpc_shadow.flags & GSL_FLAGS_INITIALIZED) {
		aligned_size = (rpc_shadow.sizebytes + (0x1000-1)) & ~(0x1000-1);
		ret = habmm_import(hab_channel->socket,
			(void **)&dma_buf, aligned_size,
			export_id, 0);
		if (ret) {
			LOGE("habmm_import failed, ret = %d", ret);
			goto out;
		}

		mem_node->export_id = export_id;
		mem_node->dma_buf = dma_buf;
	}

out:
	LOGD("%d, 0x%x", ret, rpc_shadow.flags);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_put_shadowts_mem(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_mem_node *mem_node)
{
	struct put_shadowmem_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = GSL_SUCCESS;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("hab_channel is NULL");
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = habmm_unimport(hab_channel->socket,
		mem_node->export_id, mem_node->dma_buf, 0);
	if (ret != 0) {
		LOGE("habmm_unimport failed, ret = %d, export_id = %u",
			ret, mem_node->export_id);
	}

	rpc_params.size = sizeof(rpc_params);
	rpc_params.export_id = mem_node->export_id;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_PUT_SHADOWMEM, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval) {
		LOGE("RPC_PUT_SHADOWMEM failed, %d", rval);
		ret = -EINVAL;
		goto out;
	}

out:
	LOGD("ShadowTS %d, %u", ret, mem_node->export_id);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_mem_map_smmu(struct hgsl_hab_channel_t *hab_channel,
	uint64_t size, uint64_t offset,
	struct hgsl_mem_node *mem_node)
{
	struct memory_map_ext_fd_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;
	int hab_exp_flags = 0;
	void  *hab_exp_handle = NULL;
	uint32_t export_id = 0;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (mem_node->fd >= 0) {
		hab_exp_flags = HABMM_EXPIMP_FLAGS_FD;
		hab_exp_handle = (void *)((uintptr_t)mem_node->fd);
	} else if (mem_node->dma_buf) {
		hab_exp_flags = HABMM_EXPIMP_FLAGS_DMABUF;
		hab_exp_handle = (void *)mem_node->dma_buf;
	} else {
		//todo, add support for uva
		ret = -EINVAL;
		goto out;
	}

	ret = habmm_export(hab_channel->socket, hab_exp_handle,
		size, &export_id, hab_exp_flags);
	if (ret) {
		LOGE("export failed, fd(%d), dma_buf(%p), offset(%d)",
				mem_node->fd, mem_node->dma_buf,
				offset);
		LOGE("size(%d), hab flags(0x%X) ret(%d)",
				size, hab_exp_flags, ret);
		goto out;
	}

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.fd           = mem_node->fd;
	rpc_params.hostptr      = (uintptr_t) 0;
	rpc_params.len          = size;
	rpc_params.offset       = offset;
	rpc_params.memtype      = mem_node->memtype;
	rpc_params.flags        = mem_node->flags;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_write_uint32(send_buf, export_id);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_MEMORY_MAP_EXT_FD_PURE, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read(recv_buf, &mem_node->memdesc,
				sizeof(mem_node->memdesc));
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGE("RPC_MEMORY_MAP_EXT_FD_PURE failed, %d, %d, %d, %d",
		rval, mem_node->memtype, mem_node->fd, export_id);
		ret = -EINVAL;
		goto out;
	}

out:
	mem_node->export_id = export_id;
	LOGD("mem_map_smmu: export_id(%d), size(%d), flags(0x%x), priv(0x%lx), fd(%d), ret(%d)",
		export_id, rpc_params.len, rpc_params.flags, mem_node->memdesc.priv64,
		mem_node->fd, ret);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_mem_unmap_smmu(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_mem_node *mem_node)
{
	struct memory_unmap_ext_fd_params rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();

	if (mem_node == NULL) {
		LOGE("invalid mem node");
		ret = -EINVAL;
		goto out;
	}

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (mem_node->memdesc.gpuaddr) {
		rpc_params.size    = sizeof(rpc_params);
		rpc_params.gpuaddr = mem_node->memdesc.gpuaddr;
		rpc_params.hostptr = (uintptr_t) mem_node->memdesc.hostptr64;
		rpc_params.len     = mem_node->memdesc.size;
		rpc_params.memtype = mem_node->memtype;

		ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
		ret = gsl_rpc_write_uint32(send_buf, mem_node->export_id);
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}

		ret = gsl_rpc_transact(RPC_MEMORY_UNMAP_EXT_FD_PURE, hab_channel);
		if (ret) {
			LOGE("gsl_rpc_transact failed, %d", ret);
			goto out;
		}
		ret = gsl_rpc_read_int32_l(recv_buf, &rval);
		if (ret) {
			LOGE("gsl_rpc_read_int32_l failed, %d", ret);
			goto out;
		}
		if (rval != GSL_SUCCESS) {
			LOGE("RPC_MEMORY_UNMAP_EXT_FD_PURE failed, %d", ret);
			ret = -EINVAL;
			goto out;
		}
	}
	if (mem_node->export_id) {
		ret = habmm_unexport(hab_channel->socket,
						mem_node->export_id, 0);
		if (ret) {
			LOGE("habmm_unexport failed, socket %d export_id %d",
					hab_channel->socket,
					mem_node->export_id);
			goto out;
		}
	}

out:
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_set_metainfo(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_set_metainfo_params *hgsl_param,
	const char *metainfo)
{
	struct memory_set_metainfo_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.memdesc_priv   = hgsl_param->memdesc_priv;
	rpc_params.flags          = hgsl_param->flags;
	rpc_params.metainfo_len   = hgsl_param->metainfo_len;
	strscpy(rpc_params.metainfo, metainfo, sizeof(rpc_params.metainfo));

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_MEMORY_SET_METAINFO, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGE("RPC_MEMORY_SET_METAINFO failed, %d", rval);
		ret = -EINVAL;
		goto out;
	}

out:
	if (ret)
		LOGE("ret %d", ret);
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_issueib(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_issueib_params *hgsl_param,
	struct hgsl_ibdesc *ib)
{
	struct command_issueib_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	struct hyp_ibdesc_t hyp_ibdesc = {0};
	int ret = 0;
	unsigned int i = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, hgsl_param->channel_id, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (!hab_channel->wait_retry) {
		if (ib == NULL) {
			ret = -EINVAL;
			goto out;
		}
		rpc_params.size           = sizeof(rpc_params);
		rpc_params.devhandle      = hgsl_param->devhandle;
		rpc_params.ctxthandle     = hgsl_param->ctxthandle;
		rpc_params.timestamp      = hgsl_param->timestamp;
		rpc_params.flags          = hgsl_param->flags;
		rpc_params.numibs         = hgsl_param->num_ibs;

		ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
		for (i = 0 ; i < rpc_params.numibs; i++) {
			hyp_ibdesc.gpuaddr = ib[i].gpuaddr;
			hyp_ibdesc.sizedwords = (uint32_t)ib[i].sizedwords;
			ret = gsl_rpc_write(send_buf, (void *)&hyp_ibdesc,
					sizeof(hyp_ibdesc));
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
		}
	}

	ret = gsl_rpc_transact_interrruptible(RPC_COMMAND_ISSUEIB, hab_channel);
	if (ret == -EINTR) {
		hgsl_param->channel_id = hab_channel->id;
		return ret;
	} else if (ret) {
		LOGE("gsl_rpc_transact_interrruptible failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read(recv_buf,
		(void *)&hgsl_param->timestamp, sizeof(hgsl_param->timestamp));
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_int32_l(recv_buf, &hgsl_param->rval);
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}

	LOGD("%d, %d, %d", hgsl_param->ctxthandle, hgsl_param->timestamp, ret);

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_issueib_with_alloc_list(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_issueib_with_alloc_list_params *hgsl_param,
	struct gsl_command_buffer_object_t *ib,
	struct gsl_memory_object_t *allocations,
	struct gsl_memdesc_t *be_descs,
	uint64_t *be_offsets)
{
	struct command_issueib_with_alloc_list_params rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	unsigned int i = 0;
	size_t size = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, hgsl_param->channel_id, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (!hab_channel->wait_retry) {
		if ((ib == NULL) ||
			((allocations == NULL) && (hgsl_param->num_allocations)) ||
			(be_descs == NULL) || (be_offsets == NULL)) {
			LOGE("Invalid input");
			ret = -EINVAL;
			goto out;
		}
		rpc_params.size           = sizeof(rpc_params);
		rpc_params.devhandle      = hgsl_param->devhandle;
		rpc_params.ctxthandle     = hgsl_param->ctxthandle;
		rpc_params.timestamp      = hgsl_param->timestamp;
		rpc_params.flags          = hgsl_param->flags;
		rpc_params.numibs         = hgsl_param->num_ibs;
		rpc_params.numallocations = hgsl_param->num_allocations;
		rpc_params.syncobj = (uint64_t) hgsl_param->rpc_syncobj;

		ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
		size = sizeof(struct gsl_command_buffer_object_t) * rpc_params.numibs;
		ret = gsl_rpc_write(send_buf, (void *)ib, size);
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
		/* Send all ib[].memdesc together after the entire ib[] array */
		for (i = 0; i < rpc_params.numibs; i++) {
			ret = gsl_rpc_write(send_buf, &be_descs[i],
				sizeof(struct gsl_memdesc_t));
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
			ret = gsl_rpc_write_uint64(send_buf, be_offsets[i]);
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
		}

		if (rpc_params.numallocations) {
			size = sizeof(struct gsl_memory_object_t) *
							rpc_params.numallocations;
			ret = gsl_rpc_write(send_buf, (void *)allocations, size);
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
		}
		for (i = 0; i < rpc_params.numallocations; i++) {
			ret = gsl_rpc_write(send_buf,
					&be_descs[i + rpc_params.numibs],
					sizeof(struct gsl_memdesc_t));
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
			ret = gsl_rpc_write_uint64(send_buf,
				be_offsets[i + rpc_params.numibs]);
			if (ret) {
				LOGE("gsl_rpc_write failed, %d", ret);
				goto out;
			}
		}
	}

	ret = gsl_rpc_transact_interrruptible(RPC_COMMAND_ISSUEIB_WITH_ALLOC_LIST,
							hab_channel);
	if (ret == -EINTR) {
		hgsl_param->channel_id = hab_channel->id;
		return ret;
	} else if (ret) {
		LOGE("gsl_rpc_transact_interrruptible failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read(recv_buf,
		(void *)&hgsl_param->timestamp, sizeof(hgsl_param->timestamp));
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_int32_l(recv_buf, &hgsl_param->rval);
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}

	LOGD("%d, %d, %d", hgsl_param->ctxthandle, hgsl_param->timestamp, ret);

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_wait_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_wait_ts_info *hgsl_param)
{
	struct command_waittimestamp_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	uint32_t timeout = hgsl_param->timeout;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, hgsl_param->channel_id, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	if (!hab_channel->wait_retry) {
		hgsl_rpc_parcel_reset(hab_channel);
		rpc_params.size              = sizeof(rpc_params);
		rpc_params.devhandle         = hgsl_param->devhandle;
		rpc_params.ctxthandle        = hgsl_param->context_id;
		rpc_params.timestamp         = hgsl_param->timestamp;
		if ((timeout == GSL_TIMEOUT_INFINITE) ||
			(timeout >= GSL_RPC_WAITTIMESTAMP_SLICE))
			rpc_params.timeout = GSL_RPC_WAITTIMESTAMP_SLICE;
		else
			rpc_params.timeout = hgsl_param->timeout;

		ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
	}

	ret = gsl_rpc_transact_interrruptible(RPC_COMMAND_WAITTIMESTAMP, hab_channel);
	if (ret == -EINTR) {
		hgsl_param->channel_id = hab_channel->id;
		return ret;
	} else if (ret) {
		LOGE("gsl_rpc_transact_interrruptible failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval != GSL_SUCCESS) {
		ret = (rval == GSL_FAILURE_TIMEOUT) ? -ETIMEDOUT : -EINVAL;
		goto out;
	}

	LOGD("%d, %d, %d", hgsl_param->context_id, hgsl_param->timestamp, ret);
out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_read_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_read_ts_params *hgsl_param)
{
	struct command_readtimestamp_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size              = sizeof(rpc_params);
	rpc_params.devhandle         = hgsl_param->devhandle;
	rpc_params.ctxthandle        = hgsl_param->ctxthandle;
	rpc_params.type              = hgsl_param->type;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_COMMAND_READTIMESTAMP, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &hgsl_param->timestamp);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGE("RPC_COMMAND_READTIMESTAMP failed, %d", rval);
		ret = -EINVAL;
		goto out;
	}

	LOGD("%d, %d, %d, %d", hgsl_param->ctxthandle,
		hgsl_param->type, hgsl_param->timestamp, ret);
out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_check_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_check_ts_params *hgsl_param)
{
	struct command_checktimestamp_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size              = sizeof(rpc_params);
	rpc_params.devhandle         = hgsl_param->devhandle;
	rpc_params.ctxthandle        = hgsl_param->ctxthandle;
	rpc_params.timestamp         = hgsl_param->timestamp;
	rpc_params.type              = hgsl_param->type;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_COMMAND_CHECKTIMESTAMP, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &hgsl_param->rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	LOGD("%d, %d, %d, %d", hgsl_param->ctxthandle,
		hgsl_param->type, hgsl_param->timestamp, hgsl_param->rval);
out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_get_system_time(struct hgsl_hyp_priv_t *priv,
	uint64_t *hgsl_param)
{
	struct get_system_time_params_t rpc_params = {0};
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;
	uint64_t time_us = 0;
	enum gsl_systemtime_usage_t usage
		= (enum gsl_systemtime_usage_t)*hgsl_param;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size              = sizeof(rpc_params);
	rpc_params.usage             = usage;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_GET_SYSTEM_TIME, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval != GSL_SUCCESS) {
		LOGE("RPC_GET_SYSTEM_TIME failed, rval %d", rval);
		ret = -EINVAL;
		goto out;
	}

	ret = gsl_rpc_read_uint64_l(recv_buf, &time_us);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

	*hgsl_param = time_us;
	ret = rval;
out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_syncobj_wait_multiple(struct hgsl_hyp_priv_t *priv,
	uint64_t *rpc_syncobj, uint64_t num_syncobjs,
	uint32_t timeout_ms, int32_t *status, int32_t *result)
{
	struct syncobj_wait_multiple_params_t rpc_params = { };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	uint64_t i = 0;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.num_syncobjs = num_syncobjs;
	rpc_params.timeout_ms   = timeout_ms;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	for (i = 0; i < num_syncobjs; ++i) {
		ret = gsl_rpc_write(send_buf, &rpc_syncobj[i],
			sizeof(uint64_t));
		if (ret) {
			LOGE("gsl_rpc_write failed, %d", ret);
			goto out;
		}
	}
	ret = gsl_rpc_transact(RPC_SYNCOBJ_WAIT_MULTIPLE, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read(recv_buf, status, sizeof(int32_t) * num_syncobjs);
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, result);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_perfcounter_select(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_select_params *hgsl_param,
	uint32_t *groups, uint32_t *counter_ids,
	uint32_t *counter_val_regs, uint32_t *counter_val_hi_regs)
{
	struct perfcounter_select_params_t rpc_params = { };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.devhandle    = hgsl_param->devhandle;
	rpc_params.ctxthandle   = hgsl_param->ctxthandle;
	rpc_params.num_counters = hgsl_param->num_counters;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_write(send_buf, groups,
		sizeof(uint32_t) * hgsl_param->num_counters);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_write(send_buf, counter_ids,
		sizeof(uint32_t) * hgsl_param->num_counters);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_PERFCOUNTER_SELECT, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &hgsl_param->rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (hgsl_param->rval == GSL_SUCCESS) {
		ret = gsl_rpc_read(recv_buf, counter_val_regs,
			sizeof(uint32_t) * hgsl_param->num_counters);
		if (ret) {
			LOGE("gsl_rpc_read failed, %d", ret);
			goto out;
		}
		ret = gsl_rpc_read(recv_buf, counter_val_hi_regs,
			sizeof(uint32_t) * hgsl_param->num_counters);
		if (ret) {
			LOGE("gsl_rpc_read failed, %d", ret);
			goto out;
		}
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_perfcounter_deselect(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_deselect_params *hgsl_param,
	uint32_t *groups, uint32_t *counter_ids)
{
	struct perfcounter_deselect_params_t rpc_params = { };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.devhandle    = hgsl_param->devhandle;
	rpc_params.ctxthandle   = hgsl_param->ctxthandle;
	rpc_params.timestamp    = hgsl_param->timestamp;
	rpc_params.num_counters = hgsl_param->num_counters;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_write(send_buf, groups,
		sizeof(uint32_t) * hgsl_param->num_counters);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_write(send_buf, counter_ids,
		sizeof(uint32_t) * hgsl_param->num_counters);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_PERFCOUNTER_DESELECT, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_perfcounter_query_selections(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_query_selections_params *hgsl_param,
	int32_t *selections)
{
	struct perfcounter_query_selections_params_t rpc_params = { };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.devhandle    = hgsl_param->devhandle;
	rpc_params.ctxthandle   = hgsl_param->ctxthandle;
	rpc_params.num_counters = hgsl_param->num_counters;
	rpc_params.group        = hgsl_param->group;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_PERFCOUNTER_QUERYSELECTIONS, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &hgsl_param->max_counters);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	/* -1 max_counters indicates query_selections is not supported */
	if (-1 != hgsl_param->max_counters) {
		/* num_counters == 0 means user only wants the max_counters */
		if ((hgsl_param->num_counters != 0) && (selections != NULL)) {
			ret = gsl_rpc_read(recv_buf, selections,
				sizeof(int32_t) * hgsl_param->num_counters);
			if (ret) {
				LOGE("gsl_rpc_read failed, %d", ret);
				goto out;
			}
		}
	} else {
		LOGE("not supported, group=%d, num_counters=%d",
			hgsl_param->group, hgsl_param->num_counters);
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_perfcounter_read(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_read_params *hgsl_param)
{
	struct perfcounter_read_params_t rpc_params = { };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.devhandle    = hgsl_param->devhandle;
	rpc_params.group        = hgsl_param->group;
	rpc_params.counter      = hgsl_param->counter;

	RPC_TRACE();
	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_PERFCOUNTER_READ, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &(hgsl_param->rval));
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (hgsl_param->rval == GSL_SUCCESS) {
		ret = gsl_rpc_read_uint64_l(recv_buf, &(hgsl_param->value));
		if (ret) {
			LOGE("gsl_rpc_read_uint64_l failed, %d", ret);
			goto out;
		}
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_dbq_create(struct hgsl_hab_channel_t *hab_channel,
	uint32_t ctxthandle, uint32_t *dbq_info)
{
	struct dbq_create_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("hab_channel is invalid");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.ctxthandle   = ctxthandle;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_DBQ_CREATE, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGI("RPC_DBQ_CREATE failed, %d", rval);
		ret = -EINVAL;
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, dbq_info);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

out:
	LOGD("%d, 0x%x", ret, *dbq_info);
	RPC_TRACE_DONE();
	return ret;

}

int hgsl_hyp_get_dbq_info(struct hgsl_hyp_priv_t *priv, uint32_t dbq_idx,
	struct hgsl_dbq_info *dbq_info)
{
	struct get_dbq_info_params_t rpc_params = { 0 };
	struct hgsl_hab_channel_t *hab_channel = NULL;
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	struct dma_buf *dma_buf = NULL;
	int rval = 0;

	RPC_TRACE();

	ret = hgsl_hyp_channel_pool_get(priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.q_idx        = dbq_idx;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_GET_DBQ_INFO, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGE("RPC_GET_DBQ_INFO failed, %d", rval);
		ret = -EINVAL;
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &dbq_info->export_id);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &dbq_info->size);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}
	LOGD("RPC_GET_DBQ_INFO: export_id(%d), size(%d)",
		dbq_info->export_id, dbq_info->size);
	ret = gsl_rpc_read_int32_l(recv_buf, &dbq_info->queue_off_dwords);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d, %d", ret, rval);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &dbq_info->queue_dwords);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &dbq_info->head_off_dwords);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d, %d", ret, rval);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &dbq_info->head_dwords);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint64_l(recv_buf, &dbq_info->gmuaddr);
	if (ret)
		LOGW("gsl_rpc_read_uint64_l failed, %d", ret);
	else {
		ret = gsl_rpc_read_uint32_l(recv_buf,
					&dbq_info->ibdesc_max_size);
		if (ret)
			LOGW("gsl_rpc_read_uint32_l failed, %d", ret);
	}
	if (ret) {
		dbq_info->ibdesc_max_size = 0;
		ret = 0;
	}
	dbq_info->size = (dbq_info->size + (0x1000 - 1)) & (~(0x1000 - 1));
	ret = habmm_import(hab_channel->socket,
		(void **)&dma_buf, dbq_info->size,
		dbq_info->export_id, 0);
	if (ret) {
		LOGE("habmm_import failed, ret = %d, export_id = %d",
			ret, dbq_info->export_id);
		goto out;
	}
	dbq_info->dma_buf = dma_buf;
	/*hab requires to use same socket for unexport */
	dbq_info->hab_channel = hab_channel;

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	LOGD("%d, 0x%x", ret, dbq_idx);
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_notify_cleanup(struct hgsl_hab_channel_t *hab_channel, uint32_t timeout)
{
	struct notify_cleanup_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret;
	int rval;

	if (!hab_channel) {
		LOGE("hab_channel is invalid");
		return -EINVAL;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size         = sizeof(rpc_params);
	rpc_params.timeout      = timeout;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_transact(RPC_NOTIFY_CLEANUP, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval != GSL_SUCCESS) {
		ret = (rval == GSL_FAILURE_TIMEOUT) ? -ETIMEDOUT : -EINVAL;
		goto out;
	}

out:
	return ret;
}

int hgsl_hyp_query_dbcq(struct hgsl_hab_channel_t *hab_channel, uint32_t devhandle,
	uint32_t ctxthandle, uint32_t length, uint32_t *db_signal, uint32_t *queue_gmuaddr,
	uint32_t *irq_idx)
{
	struct query_dbcq_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;

	RPC_TRACE();

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	rpc_params.size              = sizeof(rpc_params);
	rpc_params.devhandle         = devhandle;
	rpc_params.ctxthandle        = ctxthandle;
	rpc_params.length            = length;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_CONTEXT_QUERY_DBCQ, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval != GSL_SUCCESS) {
		LOGI("RPC_CONTEXT_QUERY_DBCQ failed, rval %d", rval);
		ret = -EINVAL;
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, db_signal);
	if (ret) {
		LOGE("failed to read db_signal, %d", ret);
		ret = -EINVAL;
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, queue_gmuaddr);
	if (ret) {
		LOGE("failed to read queue_gmuaddr, %d", ret);
		ret = -EINVAL;
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, irq_idx);
	if (ret) {
		LOGE("failed to read irq_idx, %d", ret);
		ret = -EINVAL;
		goto out;
	}

out:
	RPC_TRACE_DONE();
	return ret;
}

int hgsl_hyp_context_register_dbcq(struct hgsl_hab_channel_t *hab_channel,
	uint32_t devhandle, uint32_t ctxthandle, struct dma_buf *dma_buf, uint32_t size,
	uint32_t queue_body_offset, uint32_t *export_id)
{
	struct register_dbcq_params_t rpc_params = { 0 };
	struct gsl_hab_payload *send_buf = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	int ret = 0;
	int rval = 0;
	int hab_exp_flags = 0;
	void  *hab_exp_handle = NULL;

	RPC_TRACE();

	if (!hab_channel || !export_id) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}

	send_buf = &hab_channel->send_buf;
	recv_buf = &hab_channel->recv_buf;

	hab_exp_flags = HABMM_EXPIMP_FLAGS_DMABUF;
	hab_exp_handle = (void *)dma_buf;

	ret = habmm_export(hab_channel->socket, hab_exp_handle,
		size, export_id, hab_exp_flags);
	if (ret) {
		LOGE("export failed, size(%d), hab flags(0x%X) ret(%d)", size, ret);
		goto out;
	}

	rpc_params.size              = sizeof(rpc_params);
	rpc_params.devhandle         = devhandle;
	rpc_params.ctxthandle        = ctxthandle;
	rpc_params.len               = size;
	rpc_params.queue_body_offset = queue_body_offset;
	rpc_params.export_id         = *export_id;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_transact(RPC_CONTEXT_REGISTER_DBCQ, hab_channel);
	if (ret) {
		LOGE("gsl_rpc_transact failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}
	if (rval != GSL_SUCCESS) {
		LOGE("RPC_REGISTER_DB_CONTEXT_QUEUE failed, %d, %d", rval, *export_id);
		ret = -EINVAL;
		goto out;
	}

	LOGD("ctxt id(%d) export_id(%d), size(%d)", ctxthandle, *export_id, rpc_params.len);

out:
	RPC_TRACE_DONE();
	return ret;
}

static int write_ctxt_shadowts_mem_fe(struct hgsl_hab_channel_t *hab_channel,
				struct hgsl_context *ctxt,
				struct hgsl_ioctl_ctxt_create_params *hgsl_params,
				uint32_t dbq_off,
				struct hgsl_mem_node *mem_node)
{
	struct context_create_params_v1_t rpc_params = { 0 };
	struct context_create_params_t *ctxt_create_p =
					&rpc_params.ctxt_create_param;
	struct memory_map_ext_fd_params_t *shadow_map_p =
					&rpc_params.shadow_map_param;
	struct gsl_hab_payload *send_buf = &hab_channel->send_buf;
	void  *hab_exp_handle = NULL;
	uint32_t export_id = 0;
	int ret = 0;

	if (!mem_node->dma_buf) {
		LOGE("dma_buf is invalid");
		ret = -EINVAL;
		goto out;
	}

	rpc_params.size = sizeof(rpc_params);
	ctxt_create_p->size = sizeof(*ctxt_create_p);
	ctxt_create_p->devhandle = hgsl_params->devhandle;
	ctxt_create_p->type = hgsl_params->type;
	ctxt_create_p->flags = hgsl_params->flags;

	hab_exp_handle = (void *)(mem_node->dma_buf);
	ret = habmm_export(hab_channel->socket, hab_exp_handle, PAGE_SIZE,
				&export_id, HABMM_EXPIMP_FLAGS_DMABUF);
	if (ret) {
		LOGE("habmm_export failed, %d", ret);
		goto out;
	}

	mem_node->export_id = export_id;

	shadow_map_p->size = sizeof(*shadow_map_p);
	shadow_map_p->fd = mem_node->fd;
	shadow_map_p->hostptr = 0;
	shadow_map_p->len = PAGE_SIZE;
	shadow_map_p->offset = 0;
	shadow_map_p->memtype = mem_node->memtype;
	shadow_map_p->flags = 0;

	rpc_params.dbq_off = dbq_off;

	ret = gsl_rpc_write(send_buf, &rpc_params, sizeof(rpc_params));
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_write_uint32(send_buf, export_id);
	if (ret) {
		LOGE("gsl_rpc_write failed, %d", ret);
		goto out;
	}

out:
	return ret;
}

static int read_shadowts_mem_fe(struct hgsl_hab_channel_t *hab_channel,
				struct hgsl_context *ctxt,
				struct hgsl_mem_node *mem_node)
{
	struct gsl_hab_payload *recv_buf = &hab_channel->recv_buf;
	int ret = 0;
	int rval = GSL_SUCCESS;

	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval == GSL_SUCCESS) {
		ret = gsl_rpc_read(recv_buf, &mem_node->memdesc,
					sizeof(mem_node->memdesc));
		if (ret) {
			LOGE("gsl_rpc_read failed, %d", ret);
			goto out;
		}

		if (!mem_node->dma_buf) {
			ret = -EINVAL;
			goto out;
		}

		dma_buf_begin_cpu_access(mem_node->dma_buf, DMA_FROM_DEVICE);
		ret = dma_buf_vmap(mem_node->dma_buf, &ctxt->map);
		if (ret) {
			dma_buf_end_cpu_access(mem_node->dma_buf, DMA_FROM_DEVICE);
			ret = -EFAULT;
		} else {
			ctxt->shadow_ts = (struct shadow_ts *)ctxt->map.vaddr;
			ctxt->shadow_ts_node = mem_node;
			ctxt->is_fe_shadow = true;
			ctxt->shadow_ts_flags = GSL_FLAGS_INITIALIZED;
		}
	} else {
		ret = -EINVAL;
	}

out:
	return ret;
}

static int read_shadowts_mem_be(struct hgsl_hab_channel_t *hab_channel,
				struct hgsl_context *ctxt)
{
	struct qcom_hgsl *hgsl = ctxt->priv->dev;
	struct gsl_hab_payload *recv_buf = &hab_channel->recv_buf;
	uint32_t export_id = 0;
	struct hgsl_mem_node *mem_node = NULL;
	struct shadowprop_t rpc_shadow = { 0 };
	int ret = 0;

	ret = gsl_rpc_read(recv_buf, &rpc_shadow, sizeof(rpc_shadow));
	if (ret) {
		LOGE("gsl_rpc_read failed, %d", ret);
		goto out;
	}
	ret = gsl_rpc_read_uint32_l(recv_buf, &export_id);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

	if (rpc_shadow.flags & GSL_FLAGS_INITIALIZED) {
		mem_node = hgsl_mem_node_zalloc(hgsl->default_iocoherency);
		if (mem_node == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		mem_node->memdesc.size64 = (uint64_t)rpc_shadow.sizebytes;
		mem_node->fd = -1;

		ret = habmm_import(hab_channel->socket,
			(void **)&mem_node->dma_buf,
			PAGE_ALIGN(rpc_shadow.sizebytes), export_id, 0);
		if (ret) {
			LOGE("habmm_import failed, ret = %d", ret);
			goto out;
		}

		mem_node->export_id = export_id;
		dma_buf_begin_cpu_access(mem_node->dma_buf, DMA_FROM_DEVICE);
		ret = dma_buf_vmap(mem_node->dma_buf, &ctxt->map);
		if (ret) {
			dma_buf_end_cpu_access(mem_node->dma_buf, DMA_FROM_DEVICE);
			hgsl_hyp_put_shadowts_mem(hab_channel, mem_node);
			habmm_unimport(hab_channel->socket,
					export_id, mem_node->dma_buf, 0);
			ret = -EFAULT;
		} else {
			ctxt->shadow_ts = (struct shadow_ts *)ctxt->map.vaddr;
			ctxt->shadow_ts_node = mem_node;
			ctxt->shadow_ts_flags = rpc_shadow.flags;
		}
	}

out:
	if (ret)
		kfree(mem_node);

	return ret;
}

int hgsl_hyp_ctxt_create_v1(struct device *dev,
			struct hgsl_priv *priv,
			struct hgsl_hab_channel_t *hab_channel,
			struct hgsl_context *ctxt,
			struct hgsl_ioctl_ctxt_create_params *hgsl_params,
			int dbq_off, uint32_t *dbq_info)
{
	struct hgsl_mem_node *mem_node = NULL;
	struct gsl_hab_payload *recv_buf = NULL;
	struct qcom_hgsl *hgsl = priv->dev;
	int ret = 0;
	int rval = GSL_SUCCESS;

	RPC_TRACE();
	hgsl_params->ctxthandle = -1;

	if (!hab_channel) {
		LOGE("invalid hab_channel");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_rpc_parcel_reset(hab_channel);
	if (ret) {
		LOGE("hgsl_rpc_parcel_reset failed %d", ret);
		goto out;
	}
	recv_buf = &hab_channel->recv_buf;

	mem_node = hgsl_mem_node_zalloc(hgsl->default_iocoherency);
	if (mem_node == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	mem_node->fd = -1;
	ret = hgsl_sharedmem_alloc(dev, PAGE_SIZE, 0, mem_node);
	if (ret)
		goto out;

	ret = write_ctxt_shadowts_mem_fe(hab_channel, ctxt, hgsl_params,
					dbq_off, mem_node);
	if (ret)
		goto out;

	ret = gsl_rpc_transact_ext(RPC_CONTEXT_CREATE, 1, hab_channel, false);
	if (ret) {
		LOGE("gsl_rpc_transact_ext failed, %d", ret);
		goto out;
	}

	ret = gsl_rpc_read_uint32_l(recv_buf, &hgsl_params->ctxthandle);
	if (ret) {
		LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
		goto out;
	}

	if (hgsl_params->ctxthandle >= HGSL_CONTEXT_NUM) {
		ret = -EINVAL;
		goto out;
	}

	ctxt->context_id = hgsl_params->ctxthandle;
	ctxt->devhandle = hgsl_params->devhandle;
	ctxt->pid = priv->pid;
	ctxt->priv = priv;
	ctxt->flags = hgsl_params->flags;

	ret = read_shadowts_mem_fe(hab_channel, ctxt, mem_node);
	if (ret != 0) {
		ret = read_shadowts_mem_be(hab_channel, ctxt);
		if (ret) {
			LOGE("failed to get shadow memory");
			goto out;
		}
	}

	if (dbq_off)
		goto out;

	ret = gsl_rpc_read_int32_l(recv_buf, &rval);
	if (ret) {
		LOGE("gsl_rpc_read_int32_l failed, %d", ret);
		goto out;
	}

	if (rval == GSL_SUCCESS) {
		ret = gsl_rpc_read_uint32_l(recv_buf, dbq_info);
		if (ret) {
			LOGE("gsl_rpc_read_uint32_l failed, %d", ret);
			goto out;
		}
	}

out:
	if (ret) {
		if (ctxt->shadow_ts) {
			dma_buf_vunmap(ctxt->shadow_ts_node->dma_buf,
					&ctxt->map);
			dma_buf_end_cpu_access(ctxt->shadow_ts_node->dma_buf,
						DMA_FROM_DEVICE);
			ctxt->shadow_ts = NULL;
		}

		if (ctxt->shadow_ts_node && !ctxt->is_fe_shadow) {
			hgsl_hyp_put_shadowts_mem(hab_channel,
							ctxt->shadow_ts_node);
			kfree(ctxt->shadow_ts_node);
			ctxt->shadow_ts_node = NULL;
		}

		if (hgsl_params->ctxthandle < HGSL_CONTEXT_NUM) {
			hgsl_hyp_ctxt_destroy(hab_channel,
					hgsl_params->devhandle,
					hgsl_params->ctxthandle, NULL, 0);
			hgsl_params->ctxthandle = -1;
		}
	}

	if (ret || !ctxt->is_fe_shadow) {
		hgsl_hyp_mem_unmap_smmu(hab_channel, mem_node);
		hgsl_sharedmem_free(mem_node);
	}

	RPC_TRACE_DONE();
	return ret;
}

