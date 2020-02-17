/* SPDX-License-Identifier: MIT */

/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 */

#ifndef AMDTEE_PRIVATE_H
#define AMDTEE_PRIVATE_H

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/tee_drv.h>
#include <linux/kref.h>
#include <linux/types.h>
#include "amdtee_if.h"

#define DRIVER_NAME	"amdtee"
#define DRIVER_AUTHOR   "AMD-TEE Linux driver team"

/* Some GlobalPlatform error codes used in this driver */
#define TEEC_SUCCESS			0x00000000
#define TEEC_ERROR_GENERIC		0xFFFF0000
#define TEEC_ERROR_BAD_PARAMETERS	0xFFFF0006
#define TEEC_ERROR_COMMUNICATION	0xFFFF000E

#define TEEC_ORIGIN_COMMS		0x00000002

/* Maximum number of sessions which can be opened with a Trusted Application */
#define TEE_NUM_SESSIONS			32

#define TA_LOAD_PATH				"/amdtee"
#define TA_PATH_MAX				60

/**
 * struct amdtee - main service struct
 * @teedev:		client device
 * @pool:		shared memory pool
 */
struct amdtee {
	struct tee_device *teedev;
	struct tee_shm_pool *pool;
};

/**
 * struct amdtee_session - Trusted Application (TA) session related information.
 * @ta_handle:     handle to Trusted Application (TA) loaded in TEE environment
 * @refcount:      counter to keep track of sessions opened for the TA instance
 * @session_info:  an array pointing to TA allocated session data.
 * @sess_mask:     session usage bit-mask. If a particular bit is set, then the
 *                 corresponding @session_info entry is in use or valid.
 *
 * Session structure is updated on open_session and this information is used for
 * subsequent operations with the Trusted Application.
 */
struct amdtee_session {
	struct list_head list_node;
	u32 ta_handle;
	struct kref refcount;
	u32 session_info[TEE_NUM_SESSIONS];
	DECLARE_BITMAP(sess_mask, TEE_NUM_SESSIONS);
	spinlock_t lock;	/* synchronizes access to @sess_mask */
};

/**
 * struct amdtee_context_data - AMD-TEE driver context data
 * @sess_list:    Keeps track of sessions opened in current TEE context
 */
struct amdtee_context_data {
	struct list_head sess_list;
};

struct amdtee_driver_data {
	struct amdtee *amdtee;
};

struct shmem_desc {
	void *kaddr;
	u64 size;
};

/**
 * struct amdtee_shm_data - Shared memory data
 * @kaddr:	Kernel virtual address of shared memory
 * @buf_id:	Buffer id of memory mapped by TEE_CMD_ID_MAP_SHARED_MEM
 */
struct amdtee_shm_data {
	struct  list_head shm_node;
	void    *kaddr;
	u32     buf_id;
};

struct amdtee_shm_context {
	struct list_head shmdata_list;
};

#define LOWER_TWO_BYTE_MASK	0x0000FFFF

/**
 * set_session_id() - Sets the session identifier.
 * @ta_handle:      [in] handle of the loaded Trusted Application (TA)
 * @session_index:  [in] Session index. Range: 0 to (TEE_NUM_SESSIONS - 1).
 * @session:        [out] Pointer to session id
 *
 * Lower two bytes of the session identifier represents the TA handle and the
 * upper two bytes is session index.
 */
static inline void set_session_id(u32 ta_handle, u32 session_index,
				  u32 *session)
{
	*session = (session_index << 16) | (LOWER_TWO_BYTE_MASK & ta_handle);
}

static inline u32 get_ta_handle(u32 session)
{
	return session & LOWER_TWO_BYTE_MASK;
}

static inline u32 get_session_index(u32 session)
{
	return (session >> 16) & LOWER_TWO_BYTE_MASK;
}

int amdtee_open_session(struct tee_context *ctx,
			struct tee_ioctl_open_session_arg *arg,
			struct tee_param *param);

int amdtee_close_session(struct tee_context *ctx, u32 session);

int amdtee_invoke_func(struct tee_context *ctx,
		       struct tee_ioctl_invoke_arg *arg,
		       struct tee_param *param);

int amdtee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session);

int amdtee_map_shmem(struct tee_shm *shm);

void amdtee_unmap_shmem(struct tee_shm *shm);

int handle_load_ta(void *data, u32 size,
		   struct tee_ioctl_open_session_arg *arg);

int handle_unload_ta(u32 ta_handle);

int handle_open_session(struct tee_ioctl_open_session_arg *arg, u32 *info,
			struct tee_param *p);

int handle_close_session(u32 ta_handle, u32 info);

int handle_map_shmem(u32 count, struct shmem_desc *start, u32 *buf_id);

void handle_unmap_shmem(u32 buf_id);

int handle_invoke_cmd(struct tee_ioctl_invoke_arg *arg, u32 sinfo,
		      struct tee_param *p);

struct tee_shm_pool *amdtee_config_shm(void);

u32 get_buffer_id(struct tee_shm *shm);
#endif /*AMDTEE_PRIVATE_H*/
