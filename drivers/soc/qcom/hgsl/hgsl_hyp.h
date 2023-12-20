/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef GSL_HYP_INCLUDED
#define GSL_HYP_INCLUDED

#include "hgsl_utils.h"
#include "hgsl_memory.h"

/*
 *          2-stage connection diagramm
 *
 * GFX BE                                GFX FE
 *   |                                      |
 *   |      connect(main connection ID)     |
 *   | <----------------------------------- |
 *   |                                      |
 *   |      handshake(remote name, pid)     |
 *   | <----------------------------------- |
 *   |                                      |
 *   | reply(result, client connection ID)  |
 *   | -----------------------------------> |
 *   |                                      |
 *   |     close connection on main ID      |
 *   | <----------------------------------- |
 *   |                                      |
 *   |                                      |
 *   |     connect(client connection ID)    |
 *   | <----------------------------------- |
 *   |                                      |
 *   |      sub handshake(client data)      |
 *   | <----------------------------------- |
 *   |                                      |
 *   |          reply(result)               |
 *   | -----------------------------------> |
 *   |                                      |
 *   |                                      |
 *   |    RPC function call(params data)    |
 *   | <----------------------------------- |
 *   |                                      |
 *   |   reply(result, return call data)    |
 *   | -----------------------------------> |
 *   |                                      |
 *   |              ...                     |
 */

/*
 * protocol data format
 *
 * RPC function call
 *
 * <call magic><function ID><version><size><N: number of arguments>
 * <argument1>...<argument N><checksum>
 *
 *
 * RPC function argument
 *
 * <argument magic><argument ID><version><size><data><checksum>
 *
 */

#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <uapi/linux/hgsl.h>
#include "hgsl_types.h"
#include "hgsl_hyp_socket.h"

#define OFFSET_OF(type, member) ((int) &((type *)0)->member)

#define RPC_CLIENT_NAME_SIZE (64)

struct hgsl_context;
struct hgsl_priv;

/* RPC opcodes */
/* WARNING: when inserting new opcode, please insert it to the end before RPC_FUNC_LAST */
/* Inserting the new opcode in the middle of the list will break the protocol ! */
enum gsl_rpc_func_t {
	RPC_LIBRARY_OPEN = 0,
	RPC_LIBRARY_CLOSE,
	RPC_LIBRARY_VERSION,
	RPC_LIBRARY_SET_MEMNOTIFY_TYPE,
	RPC_DEVICE_OPEN,
	RPC_DEVICE_CLOSE,
	RPC_DEVICE_GETINFO,
	RPC_DEVICE_GETINFO_EXT,
	RPC_DEVICE_SETPOWERSTATE,
	RPC_DEVICE_WAITIRQ,
	RPC_DEVICE_GETIRQCNTRBASE,
	RPC_DEVICE_DUMPSTATE,
	RPC_COMMAND_ISSUEIB,
	RPC_COMMAND_INSERTFENCE,
	RPC_COMMAND_READTIMESTAMP,
	RPC_COMMAND_ISSUEIB_SYNC,
	RPC_COMMAND_ISSUEIB_WITH_ALLOC_LIST,
	RPC_COMMAND_CHECKTIMESTAMP,
	RPC_COMMAND_WAITTIMESTAMP,
	RPC_COMMAND_FREEMEMONTIMESTAMP,
	RPC_COMMAND_RESETSTATUS_INTERNAL,
	RPC_CONTEXT_CREATE,
	RPC_CONTEXT_DESTROY,
	RPC_CONTEXT_BINDGMEMSHADOW,
	RPC_CONTEXT_SETBINBASEOFFSET,
	RPC_MEMORY_READ,
	RPC_MEMORY_WRITE,
	RPC_MEMORY_COPY,
	RPC_MEMORY_SET,
	RPC_MEMORY_QUERYSTATS,
	RPC_MEMORY_ALLOC_PURE,
	RPC_MEMORY_PHYS_ALLOC_PURE,
	RPC_MEMORY_VIRT_ALLOC_PURE,
	RPC_MEMORY_FREE_PURE,
	RPC_MEMORY_CACHEOPERATION,
	RPC_MEMORY_NOTIFY,
	RPC_MEMORY_BIND,
	RPC_MEMORY_BIND_SYNC,
	RPC_MEMORY_MMAP,
	RPC_MEMORY_MUNMAP,
	RPC_MEMORY_CREATE_PAGETABLE,
	RPC_MEMORY_DESTROY_PAGETABLE,
	RPC_MEMORY_SET_PAGETABLE,
	RPC_COMMAND_FREEMEMONTIMESTAMP_PURE,
	RPC_PERFCOUNTER_SELECT,
	RPC_PERFCOUNTER_DESELECT,
	RPC_PERFCOUNTER_QUERYSELECTIONS,
	RPC_PERFCOUNTER_READ,
	RPC_SYNCOBJ_CREATE,
	RPC_SYNCOBJ_CREATE_FROM_BIND,
	RPC_SYNCOBJ_DESTROY,
	RPC_SYNCOBJ_WAIT,
	RPC_TIMESTAMP_CMP,
	RPC_SYNCOBJ_CLONE,
	RPC_SYNCOBJ_MERGE,
	RPC_SYNCOBJ_MERGE_MULTIPLE,
	RPC_SYNCSOURCE_CREATE,
	RPC_SYNCSOURCE_DESTROY,
	RPC_SYNCOBJ_CREATE_FROM_SOURCE,
	RPC_SYNCOBJ_SIGNAL,
	RPC_SYNCOBJ_WAIT_MULTIPLE,
	RPC_DEVICE_DEBUG,
	RPC_CFFDUMP_WAITIRQ,
	RPC_CFFDUMP_WRITEVERIFYFILE,
	RPC_MEMORY_MAP_EXT_FD_PURE,  /* Linux extension */
	RPC_MEMORY_UNMAP_EXT_FD_PURE, /* Linux extension */
	RPC_GET_SHADOWMEM,
	RPC_PUT_SHADOWMEM,
	RPC_BLIT,
	RPC_HANDSHAKE,
	RPC_SUB_HANDSHAKE,
	RPC_DISCONNECT,
	RPC_MEMORY_SET_METAINFO,
	RPC_GET_SYSTEM_TIME,
	RPC_GET_DBQ_INFO,
	RPC_DBQ_CREATE,
	RPC_PERFCOUNTERS_READ,
	RPC_NOTIFY_CLEANUP,
	RPC_COMMAND_RESETSTATUS,
	RPC_CONTEXT_QUERY_DBCQ,
	RPC_CONTEXT_REGISTER_DBCQ,
	RPC_FUNC_LAST /* insert new func BEFORE this line! */
};

struct hgsl_hyp_priv_t {
	struct device *dev;
	struct mutex lock;
	struct list_head free_channels;
	struct list_head busy_channels;
	int conn_id;
	unsigned char client_name[RPC_CLIENT_NAME_SIZE];
	int client_pid;
	struct idr channel_idr;
};

/* backend i.e. server type: depends on server's underlying platform */
enum gsl_rpc_server_type_t {
	GSL_RPC_SERVER_TYPE_1 = 1,
	GSL_RPC_SERVER_TYPE_2,
	GSL_RPC_SERVER_TYPE_3,
	GSL_RPC_SERVER_TYPE_LAST
};

/* frontend i.e. client type: depends on client's underlying platform */
enum gsl_rpc_client_type_t {
	GSL_RPC_CLIENT_TYPE_1 = 1,
	GSL_RPC_CLIENT_TYPE_2,
	GSL_RPC_CLIENT_TYPE_3,
	GSL_RPC_CLIENT_TYPE_LAST
};

/* backend i.e. server mode in regards to the way of handling new client */
enum gsl_rpc_server_mode_t {
	GSL_RPC_SERVER_MODE_1 = 1,
	GSL_RPC_SERVER_MODE_2,
	GSL_RPC_SERVER_MODE_3,
	GSL_RPC_SERVER_MODE_LAST
};

#pragma pack(push, 4)

/* For RPC_HANDSHAKE version < 2 */
struct handshake_params_t {
	uint32_t size;
	uint32_t client_type;
	uint32_t client_version;
	uint32_t pid;
	char name[RPC_CLIENT_NAME_SIZE];
};

struct handshake_params_v2_t {
	uint32_t size;
	uint32_t client_type;
	uint32_t client_version;
	uint32_t pid;
	char name[RPC_CLIENT_NAME_SIZE];
	/* user id in current namespace, if set to (uid_t)(-1),
	 * backend will ignore it and use default settings
	 */
	uint32_t uid;
};

struct sub_handshake_params_t {
	uint32_t size;
	uint32_t pid;
	uint32_t memdesc_size;
};

struct library_open_params_t {
	uint32_t            size;
	uint32_t            flags;
};

struct context_create_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	enum gsl_context_type_t type;
	uint32_t                flags;
};

struct context_destroy_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
};

struct get_shadowmem_params_v1_t {
	uint32_t            size;
	enum gsl_deviceid_t device_id;
	uint32_t            ctxthandle;
};

struct put_shadowmem_params_t {
	uint32_t            size;
	uint32_t            export_id;
};

struct shadowprop_t {
	uint32_t        size;
	uint64_t        sizebytes;
	uint32_t        flags;
};

struct memory_alloc_params_t {
	uint32_t                size;
	uint32_t                sizebytes;
	uint32_t                flags;
};

struct memory_free_params_t {
	uint32_t                size;
	uint32_t                flags;
};

struct memory_map_ext_fd_params_t {
	uint32_t                size;
	int                     fd;
	uint64_t                hostptr;
	uint64_t                len;
	uint64_t                offset;
	uint32_t                memtype;
	uint32_t                flags;
};

struct memory_unmap_ext_fd_params {
	uint32_t                size;
	uint64_t                gpuaddr;
	uint64_t                hostptr;
	uint64_t                len;
	uint32_t                memtype;
};

struct hyp_ibdesc_t {
	uint32_t size;
	uint64_t gpuaddr;
	uint64_t server_priv_memdesc;
	uint32_t sizedwords;
};

struct command_issueib_params_t {
	uint32_t            size;
	uint32_t            devhandle;
	uint32_t            ctxthandle;
	uint32_t            numibs;
	uint32_t            timestamp;
	uint32_t            flags;
};

struct command_issueib_with_alloc_list_params {
	uint32_t            size;
	uint32_t            devhandle;
	uint32_t            ctxthandle;
	uint32_t            numibs;
	uint32_t            numallocations;
	uint32_t            timestamp;
	uint32_t            flags;
	uint64_t            syncobj;
};

struct memory_set_metainfo_params_t {
	uint64_t            memdesc_priv;
	uint32_t            flags;
	char                metainfo[128];
	uint64_t            metainfo_len;
};

struct command_waittimestamp_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	uint32_t                timestamp;
	uint32_t                timeout;
};

struct command_readtimestamp_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	enum gsl_timestamp_type_t    type;
};

struct command_checktimestamp_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	uint32_t                timestamp;
	enum gsl_timestamp_type_t    type;
};

struct get_system_time_params_t {
	uint32_t                size;
	enum gsl_systemtime_usage_t  usage;
};

struct get_dbq_info_params_t {
	uint32_t        size;
	uint64_t        gpuaddr;
	uint64_t        priv_memdesc;
	uint32_t        sizedwords;
	int             q_idx;
};

struct dbq_create_params_t {
	uint32_t            size;
	uint32_t            ctxthandle;
};

struct syncobj_wait_multiple_params_t {
	uint32_t            size;
	uint64_t            num_syncobjs;
	uint32_t            timeout_ms;
};

struct perfcounter_select_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	int                     num_counters;
};

struct perfcounter_deselect_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	uint32_t                timestamp;
	int                     num_counters;
};

struct perfcounter_query_selections_params_t {
	uint32_t                    size;
	uint32_t                    devhandle;
	uint32_t                    ctxthandle;
	enum gsl_perfcountergroupid_t    group;
	int                         num_counters;
};

struct perfcounter_read_params_t {
	uint32_t                    size;
	uint32_t                    devhandle;
	enum gsl_perfcountergroupid_t    group;
	uint32_t                    counter;
};

struct notify_cleanup_params_t {
	uint32_t            size;
	uint32_t            timeout;
};

struct query_dbcq_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	uint32_t                length;
};

struct register_dbcq_params_t {
	uint32_t                size;
	uint32_t                devhandle;
	uint32_t                ctxthandle;
	uint32_t                len;
	uint32_t                queue_body_offset;
	uint32_t                export_id;
};

struct context_create_params_v1_t {
	uint32_t                          size;
	struct context_create_params_t    ctxt_create_param;
	struct memory_map_ext_fd_params_t shadow_map_param;
	uint32_t                          dbq_off;
};

#pragma pack(pop)

struct hgsl_hab_channel_t {
	struct list_head node;
	int socket;
	int id;
	bool wait_retry;
	bool busy;
	struct hgsl_hyp_priv_t  *priv;
	struct gsl_hab_payload  send_buf;
	struct gsl_hab_payload  recv_buf;
};

struct hgsl_dbq_info {
	uint32_t export_id;
	uint32_t size;
	uint32_t head_dwords;
	int32_t  head_off_dwords;
	uint32_t queue_dwords;
	int32_t  queue_off_dwords;
	uint32_t db_signal;
	struct dma_buf *dma_buf;
	uint64_t gmuaddr;
	uint32_t ibdesc_max_size;
	struct hgsl_hab_channel_t *hab_channel;
};

int hgsl_hyp_init(struct hgsl_hyp_priv_t *priv, struct device *dev,
	int client_pid, const char * const client_name);

void hgsl_hyp_close(struct hgsl_hyp_priv_t *priv);

int hgsl_hyp_channel_pool_get(
	struct hgsl_hyp_priv_t *priv, int id, struct hgsl_hab_channel_t **channel);

void hgsl_hyp_channel_pool_put(struct hgsl_hab_channel_t *hab_channel);

int hgsl_hyp_generic_transaction(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_hyp_generic_transaction_params *params,
	void **pSend, void **pReply, void *pRval);

int hgsl_hyp_gsl_lib_open(struct hgsl_hyp_priv_t *priv,
	uint32_t flags, int32_t *rval);

int hgsl_hyp_ctxt_create(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_ioctl_ctxt_create_params *hgsl_params);

int hgsl_hyp_dbq_create(struct hgsl_hab_channel_t *hab_channel,
	uint32_t ctxthandle, uint32_t *dbq_info);

int hgsl_hyp_ctxt_destroy(struct hgsl_hab_channel_t *hab_channel,
	uint32_t devhandle, uint32_t context_id, uint32_t *rval, uint32_t dbcq_export_id);

int hgsl_hyp_get_shadowts_mem(struct hgsl_hab_channel_t *hab_channel,
	uint32_t context_id, uint32_t *shadow_ts_flags,
	struct hgsl_mem_node *mem_node);

int hgsl_hyp_put_shadowts_mem(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_mem_node *mem_node);

int hgsl_hyp_mem_map_smmu(struct hgsl_hab_channel_t *hab_channel,
	uint64_t size, uint64_t offset,
	struct hgsl_mem_node *mem_node);

int hgsl_hyp_mem_unmap_smmu(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_mem_node *mem_node);

int hgsl_hyp_set_metainfo(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_set_metainfo_params *hgsl_param,
	const char *metainfo);

int hgsl_hyp_issueib(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_issueib_params *hgsl_param,
	struct hgsl_ibdesc *ib);

int hgsl_hyp_issueib_with_alloc_list(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_issueib_with_alloc_list_params *hgsl_param,
	struct gsl_command_buffer_object_t *ib,
	struct gsl_memory_object_t *allocations,
	struct gsl_memdesc_t *be_descs,
	uint64_t *be_offsets);

int hgsl_hyp_wait_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_wait_ts_info *hgsl_param);

int hgsl_hyp_check_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_check_ts_params *hgsl_param);

int hgsl_hyp_read_timestamp(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_read_ts_params *hgsl_param);

int hgsl_hyp_get_system_time(struct hgsl_hyp_priv_t *priv,
	uint64_t *hgsl_param);

int hgsl_hyp_syncobj_wait_multiple(struct hgsl_hyp_priv_t *priv,
	uint64_t *rpc_syncobj, uint64_t num_syncobjs,
	uint32_t timeout_ms, int32_t *status, int32_t *result);

int hgsl_hyp_perfcounter_select(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_select_params *hgsl_param,
	uint32_t *groups, uint32_t *counter_ids,
	uint32_t *counter_val_regs, uint32_t *counter_val_hi_regs);

int hgsl_hyp_perfcounter_deselect(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_deselect_params *hgsl_param,
	uint32_t *groups, uint32_t *counter_ids);

int hgsl_hyp_perfcounter_query_selections(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_query_selections_params *hgsl_param,
	int32_t *selections);

int hgsl_hyp_perfcounter_read(struct hgsl_hyp_priv_t *priv,
	struct hgsl_ioctl_perfcounter_read_params *hgsl_param);

int hgsl_hyp_get_dbq_info(struct hgsl_hyp_priv_t *priv, uint32_t dbq_idx,
	struct hgsl_dbq_info *dbq_info);

int hgsl_hyp_notify_cleanup(struct hgsl_hab_channel_t *hab_channel, uint32_t timeout);

int hgsl_hyp_query_dbcq(struct hgsl_hab_channel_t *hab_channel, uint32_t devhandle,
	uint32_t ctxthandle, uint32_t length, uint32_t *db_signal, uint32_t *queue_gmuaddr,
	uint32_t *irq_idx);

int hgsl_hyp_context_register_dbcq(struct hgsl_hab_channel_t *hab_channel,
	uint32_t devhandle, uint32_t ctxthandle, struct dma_buf *dma_buf, uint32_t size,
	uint32_t queue_body_offset, uint32_t *export_id);

int hgsl_hyp_ctxt_create_v1(struct device *dev,
			struct hgsl_priv *priv,
			struct hgsl_hab_channel_t *hab_channel,
			struct hgsl_context *ctxt,
			struct hgsl_ioctl_ctxt_create_params *hgsl_params,
			int dbq_off, uint32_t *dbq_info);
#endif
