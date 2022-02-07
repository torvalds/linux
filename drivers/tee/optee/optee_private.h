/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */

#ifndef OPTEE_PRIVATE_H
#define OPTEE_PRIVATE_H

#include <linux/arm-smccc.h>
#include <linux/rhashtable.h>
#include <linux/semaphore.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include "optee_msg.h"

#define DRIVER_NAME "optee"

#define OPTEE_MAX_ARG_SIZE	1024

/* Some Global Platform error codes used in this driver */
#define TEEC_SUCCESS			0x00000000
#define TEEC_ERROR_BAD_PARAMETERS	0xFFFF0006
#define TEEC_ERROR_NOT_SUPPORTED	0xFFFF000A
#define TEEC_ERROR_COMMUNICATION	0xFFFF000E
#define TEEC_ERROR_OUT_OF_MEMORY	0xFFFF000C
#define TEEC_ERROR_BUSY			0xFFFF000D
#define TEEC_ERROR_SHORT_BUFFER		0xFFFF0010

#define TEEC_ORIGIN_COMMS		0x00000002

/*
 * This value should be larger than the number threads in secure world to
 * meet the need from secure world. The number of threads in secure world
 * are usually not even close to 255 so we should be safe for now.
 */
#define OPTEE_DEFAULT_MAX_NOTIF_VALUE	255

typedef void (optee_invoke_fn)(unsigned long, unsigned long, unsigned long,
				unsigned long, unsigned long, unsigned long,
				unsigned long, unsigned long,
				struct arm_smccc_res *);

struct optee_call_waiter {
	struct list_head list_node;
	struct completion c;
};

struct optee_call_queue {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head waiters;
};

struct optee_notif {
	u_int max_key;
	/* Serializes access to the elements below in this struct */
	spinlock_t lock;
	struct list_head db;
	u_long *bitmap;
};

/**
 * struct optee_supp - supplicant synchronization struct
 * @ctx			the context of current connected supplicant.
 *			if !NULL the supplicant device is available for use,
 *			else busy
 * @mutex:		held while accessing content of this struct
 * @req_id:		current request id if supplicant is doing synchronous
 *			communication, else -1
 * @reqs:		queued request not yet retrieved by supplicant
 * @idr:		IDR holding all requests currently being processed
 *			by supplicant
 * @reqs_c:		completion used by supplicant when waiting for a
 *			request to be queued.
 */
struct optee_supp {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct tee_context *ctx;

	int req_id;
	struct list_head reqs;
	struct idr idr;
	struct completion reqs_c;
};

struct optee_smc {
	optee_invoke_fn *invoke_fn;
	void *memremaped_shm;
	u32 sec_caps;
	unsigned int notif_irq;
};

/**
 * struct optee_ffa_data -  FFA communication struct
 * @ffa_dev		FFA device, contains the destination id, the id of
 *			OP-TEE in secure world
 * @ffa_ops		FFA operations
 * @mutex		Serializes access to @global_ids
 * @global_ids		FF-A shared memory global handle translation
 */
struct optee_ffa {
	struct ffa_device *ffa_dev;
	const struct ffa_dev_ops *ffa_ops;
	/* Serializes access to @global_ids */
	struct mutex mutex;
	struct rhashtable global_ids;
};

struct optee;

/**
 * struct optee_ops - OP-TEE driver internal operations
 * @do_call_with_arg:	enters OP-TEE in secure world
 * @to_msg_param:	converts from struct tee_param to OPTEE_MSG parameters
 * @from_msg_param:	converts from OPTEE_MSG parameters to struct tee_param
 *
 * These OPs are only supposed to be used internally in the OP-TEE driver
 * as a way of abstracting the different methogs of entering OP-TEE in
 * secure world.
 */
struct optee_ops {
	int (*do_call_with_arg)(struct tee_context *ctx,
				struct tee_shm *shm_arg);
	int (*to_msg_param)(struct optee *optee,
			    struct optee_msg_param *msg_params,
			    size_t num_params, const struct tee_param *params);
	int (*from_msg_param)(struct optee *optee, struct tee_param *params,
			      size_t num_params,
			      const struct optee_msg_param *msg_params);
};

/**
 * struct optee - main service struct
 * @supp_teedev:	supplicant device
 * @teedev:		client device
 * @ops:		internal callbacks for different ways to reach secure
 *			world
 * @ctx:		driver internal TEE context
 * @smc:		specific to SMC ABI
 * @ffa:		specific to FF-A ABI
 * @call_queue:		queue of threads waiting to call @invoke_fn
 * @notif:		notification synchronization struct
 * @supp:		supplicant synchronization struct for RPC to supplicant
 * @pool:		shared memory pool
 * @rpc_arg_count:	If > 0 number of RPC parameters to make room for
 * @scan_bus_done	flag if device registation was already done.
 * @scan_bus_wq		workqueue to scan optee bus and register optee drivers
 * @scan_bus_work	workq to scan optee bus and register optee drivers
 */
struct optee {
	struct tee_device *supp_teedev;
	struct tee_device *teedev;
	const struct optee_ops *ops;
	struct tee_context *ctx;
	union {
		struct optee_smc smc;
		struct optee_ffa ffa;
	};
	struct optee_call_queue call_queue;
	struct optee_notif notif;
	struct optee_supp supp;
	struct tee_shm_pool *pool;
	unsigned int rpc_arg_count;
	bool   scan_bus_done;
	struct workqueue_struct *scan_bus_wq;
	struct work_struct scan_bus_work;
};

struct optee_session {
	struct list_head list_node;
	u32 session_id;
};

struct optee_context_data {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head sess_list;
};

struct optee_rpc_param {
	u32	a0;
	u32	a1;
	u32	a2;
	u32	a3;
	u32	a4;
	u32	a5;
	u32	a6;
	u32	a7;
};

/* Holds context that is preserved during one STD call */
struct optee_call_ctx {
	/* information about pages list used in last allocation */
	void *pages_list;
	size_t num_entries;
};

int optee_notif_init(struct optee *optee, u_int max_key);
void optee_notif_uninit(struct optee *optee);
int optee_notif_wait(struct optee *optee, u_int key);
int optee_notif_send(struct optee *optee, u_int key);

u32 optee_supp_thrd_req(struct tee_context *ctx, u32 func, size_t num_params,
			struct tee_param *param);

int optee_supp_read(struct tee_context *ctx, void __user *buf, size_t len);
int optee_supp_write(struct tee_context *ctx, void __user *buf, size_t len);
void optee_supp_init(struct optee_supp *supp);
void optee_supp_uninit(struct optee_supp *supp);
void optee_supp_release(struct optee_supp *supp);

int optee_supp_recv(struct tee_context *ctx, u32 *func, u32 *num_params,
		    struct tee_param *param);
int optee_supp_send(struct tee_context *ctx, u32 ret, u32 num_params,
		    struct tee_param *param);

int optee_open_session(struct tee_context *ctx,
		       struct tee_ioctl_open_session_arg *arg,
		       struct tee_param *param);
int optee_close_session_helper(struct tee_context *ctx, u32 session);
int optee_close_session(struct tee_context *ctx, u32 session);
int optee_invoke_func(struct tee_context *ctx, struct tee_ioctl_invoke_arg *arg,
		      struct tee_param *param);
int optee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session);

#define PTA_CMD_GET_DEVICES		0x0
#define PTA_CMD_GET_DEVICES_SUPP	0x1
int optee_enumerate_devices(u32 func);
void optee_unregister_devices(void);

int optee_pool_op_alloc_helper(struct tee_shm_pool_mgr *poolm,
			       struct tee_shm *shm, size_t size,
			       int (*shm_register)(struct tee_context *ctx,
						   struct tee_shm *shm,
						   struct page **pages,
						   size_t num_pages,
						   unsigned long start));


void optee_remove_common(struct optee *optee);
int optee_open(struct tee_context *ctx, bool cap_memref_null);
void optee_release(struct tee_context *ctx);
void optee_release_supp(struct tee_context *ctx);

static inline void optee_from_msg_param_value(struct tee_param *p, u32 attr,
					      const struct optee_msg_param *mp)
{
	p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT +
		  attr - OPTEE_MSG_ATTR_TYPE_VALUE_INPUT;
	p->u.value.a = mp->u.value.a;
	p->u.value.b = mp->u.value.b;
	p->u.value.c = mp->u.value.c;
}

static inline void optee_to_msg_param_value(struct optee_msg_param *mp,
					    const struct tee_param *p)
{
	mp->attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT + p->attr -
		   TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	mp->u.value.a = p->u.value.a;
	mp->u.value.b = p->u.value.b;
	mp->u.value.c = p->u.value.c;
}

void optee_cq_wait_init(struct optee_call_queue *cq,
			struct optee_call_waiter *w);
void optee_cq_wait_for_completion(struct optee_call_queue *cq,
				  struct optee_call_waiter *w);
void optee_cq_wait_final(struct optee_call_queue *cq,
			 struct optee_call_waiter *w);
int optee_check_mem_type(unsigned long start, size_t num_pages);
struct tee_shm *optee_get_msg_arg(struct tee_context *ctx, size_t num_params,
				  struct optee_msg_arg **msg_arg);

struct tee_shm *optee_rpc_cmd_alloc_suppl(struct tee_context *ctx, size_t sz);
void optee_rpc_cmd_free_suppl(struct tee_context *ctx, struct tee_shm *shm);
void optee_rpc_cmd(struct tee_context *ctx, struct optee *optee,
		   struct optee_msg_arg *arg);

/*
 * Small helpers
 */

static inline void *reg_pair_to_ptr(u32 reg0, u32 reg1)
{
	return (void *)(unsigned long)(((u64)reg0 << 32) | reg1);
}

static inline void reg_pair_from_64(u32 *reg0, u32 *reg1, u64 val)
{
	*reg0 = val >> 32;
	*reg1 = val;
}

/* Registration of the ABIs */
int optee_smc_abi_register(void);
void optee_smc_abi_unregister(void);
int optee_ffa_abi_register(void);
void optee_ffa_abi_unregister(void);

#endif /*OPTEE_PRIVATE_H*/
