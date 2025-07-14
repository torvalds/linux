/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, 2023 Linaro Limited
 */

#ifndef OPTEE_PRIVATE_H
#define OPTEE_PRIVATE_H

#include <linux/arm-smccc.h>
#include <linux/notifier.h>
#include <linux/rhashtable.h>
#include <linux/rpmb.h>
#include <linux/semaphore.h>
#include <linux/tee_core.h>
#include <linux/types.h>
#include "optee_msg.h"

#define DRIVER_NAME "optee"

#define OPTEE_MAX_ARG_SIZE	1024

/* Some Global Platform error codes used in this driver */
#define TEEC_SUCCESS			0x00000000
#define TEEC_ERROR_BAD_PARAMETERS	0xFFFF0006
#define TEEC_ERROR_ITEM_NOT_FOUND	0xFFFF0008
#define TEEC_ERROR_NOT_SUPPORTED	0xFFFF000A
#define TEEC_ERROR_COMMUNICATION	0xFFFF000E
#define TEEC_ERROR_OUT_OF_MEMORY	0xFFFF000C
#define TEEC_ERROR_BUSY			0xFFFF000D
#define TEEC_ERROR_SHORT_BUFFER		0xFFFF0010

/* API Return Codes are from the GP TEE Internal Core API Specification */
#define TEE_ERROR_TIMEOUT		0xFFFF3001
#define TEE_ERROR_STORAGE_NOT_AVAILABLE	0xF0100003

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

/*
 * struct optee_call_waiter - TEE entry may need to wait for a free TEE thread
 * @list_node		Reference in waiters list
 * @c			Waiting completion reference
 * @sys_thread		True if waiter belongs to a system thread
 */
struct optee_call_waiter {
	struct list_head list_node;
	struct completion c;
	bool sys_thread;
};

/*
 * struct optee_call_queue - OP-TEE call queue management
 * @mutex			Serializes access to this struct
 * @waiters			List of threads waiting to enter OP-TEE
 * @total_thread_count		Overall number of thread context in OP-TEE or 0
 * @free_thread_count		Number of threads context free in OP-TEE
 * @sys_thread_req_count	Number of registered system thread sessions
 */
struct optee_call_queue {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head waiters;
	int total_thread_count;
	int free_thread_count;
	int sys_thread_req_count;
};

struct optee_notif {
	u_int max_key;
	/* Serializes access to the elements below in this struct */
	spinlock_t lock;
	struct list_head db;
	u_long *bitmap;
};

#define OPTEE_SHM_ARG_ALLOC_PRIV	BIT(0)
#define OPTEE_SHM_ARG_SHARED		BIT(1)
struct optee_shm_arg_entry;
struct optee_shm_arg_cache {
	u32 flags;
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head shm_args;
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

/*
 * struct optee_pcpu - per cpu notif private struct passed to work functions
 * @optee		optee device reference
 */
struct optee_pcpu {
	struct optee *optee;
};

/*
 * struct optee_smc - optee smc communication struct
 * @invoke_fn		handler function to invoke secure monitor
 * @memremaped_shm	virtual address of memory in shared memory pool
 * @sec_caps:		secure world capabilities defined by
 *			OPTEE_SMC_SEC_CAP_* in optee_smc.h
 * @notif_irq		interrupt used as async notification by OP-TEE or 0
 * @optee_pcpu		per_cpu optee instance for per cpu work or NULL
 * @notif_pcpu_wq	workqueue for per cpu asynchronous notification or NULL
 * @notif_pcpu_work	work for per cpu asynchronous notification
 * @notif_cpuhp_state   CPU hotplug state assigned for pcpu interrupt management
 */
struct optee_smc {
	optee_invoke_fn *invoke_fn;
	void *memremaped_shm;
	u32 sec_caps;
	unsigned int notif_irq;
	struct optee_pcpu __percpu *optee_pcpu;
	struct workqueue_struct *notif_pcpu_wq;
	struct work_struct notif_pcpu_work;
	unsigned int notif_cpuhp_state;
};

/**
 * struct optee_ffa_data -  FFA communication struct
 * @ffa_dev		FFA device, contains the destination id, the id of
 *			OP-TEE in secure world
 * @bottom_half_value	Notification ID used for bottom half signalling or
 *			U32_MAX if unused
 * @mutex		Serializes access to @global_ids
 * @global_ids		FF-A shared memory global handle translation
 */
struct optee_ffa {
	struct ffa_device *ffa_dev;
	u32 bottom_half_value;
	/* Serializes access to @global_ids */
	struct mutex mutex;
	struct rhashtable global_ids;
	struct workqueue_struct *notif_wq;
	struct work_struct notif_work;
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
				struct tee_shm *shm_arg, u_int offs,
				bool system_thread);
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
 * @mutex:		mutex protecting @rpmb_dev
 * @rpmb_dev:		current RPMB device or NULL
 * @rpmb_scan_bus_done	flag if device registation of RPMB dependent devices
 *			was already done
 * @rpmb_scan_bus_work	workq to for an RPMB device and to scan optee bus
 *			and register RPMB dependent optee drivers
 * @rpc_param_count:	If > 0 number of RPC parameters to make room for
 * @scan_bus_done	flag if device registation was already done.
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
	struct optee_shm_arg_cache shm_arg_cache;
	struct optee_call_queue call_queue;
	struct optee_notif notif;
	struct optee_supp supp;
	struct tee_shm_pool *pool;
	/* Protects rpmb_dev pointer */
	struct mutex rpmb_dev_mutex;
	struct rpmb_dev *rpmb_dev;
	struct notifier_block rpmb_intf;
	unsigned int rpc_param_count;
	bool scan_bus_done;
	bool rpmb_scan_bus_done;
	bool in_kernel_rpmb_routing;
	struct work_struct scan_bus_work;
	struct work_struct rpmb_scan_bus_work;
};

struct optee_session {
	struct list_head list_node;
	u32 session_id;
	bool use_sys_thread;
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

extern struct blocking_notifier_head optee_rpmb_intf_added;

int optee_notif_init(struct optee *optee, u_int max_key);
void optee_notif_uninit(struct optee *optee);
int optee_notif_wait(struct optee *optee, u_int key, u32 timeout);
int optee_notif_send(struct optee *optee, u_int key);

u32 optee_supp_thrd_req(struct tee_context *ctx, u32 func, size_t num_params,
			struct tee_param *param);

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
int optee_system_session(struct tee_context *ctx, u32 session);
int optee_close_session_helper(struct tee_context *ctx, u32 session,
			       bool system_thread);
int optee_close_session(struct tee_context *ctx, u32 session);
int optee_invoke_func(struct tee_context *ctx, struct tee_ioctl_invoke_arg *arg,
		      struct tee_param *param);
int optee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session);

#define PTA_CMD_GET_DEVICES		0x0
#define PTA_CMD_GET_DEVICES_SUPP	0x1
#define PTA_CMD_GET_DEVICES_RPMB	0x2
int optee_enumerate_devices(u32 func);
void optee_unregister_devices(void);
void optee_bus_scan_rpmb(struct work_struct *work);
int optee_rpmb_intf_rdev(struct notifier_block *intf, unsigned long action,
			 void *data);

void optee_set_dev_group(struct optee *optee);
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

void optee_cq_init(struct optee_call_queue *cq, int thread_count);
void optee_cq_wait_init(struct optee_call_queue *cq,
			struct optee_call_waiter *w, bool sys_thread);
void optee_cq_wait_for_completion(struct optee_call_queue *cq,
				  struct optee_call_waiter *w);
void optee_cq_wait_final(struct optee_call_queue *cq,
			 struct optee_call_waiter *w);
int optee_check_mem_type(unsigned long start, size_t num_pages);

void optee_shm_arg_cache_init(struct optee *optee, u32 flags);
void optee_shm_arg_cache_uninit(struct optee *optee);
struct optee_msg_arg *optee_get_msg_arg(struct tee_context *ctx,
					size_t num_params,
					struct optee_shm_arg_entry **entry,
					struct tee_shm **shm_ret,
					u_int *offs);
void optee_free_msg_arg(struct tee_context *ctx,
			struct optee_shm_arg_entry *entry, u_int offs);
size_t optee_msg_arg_size(size_t rpc_param_count);


struct tee_shm *optee_rpc_cmd_alloc_suppl(struct tee_context *ctx, size_t sz);
void optee_rpc_cmd_free_suppl(struct tee_context *ctx, struct tee_shm *shm);
void optee_rpc_cmd(struct tee_context *ctx, struct optee *optee,
		   struct optee_msg_arg *arg);

int optee_do_bottom_half(struct tee_context *ctx);
int optee_stop_async_notif(struct tee_context *ctx);

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
