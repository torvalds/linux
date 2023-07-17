/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __HGSL_H_
#define __HGSL_H_

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include "hgsl_hyp.h"
#include "hgsl_memory.h"
#include "hgsl_tcsr.h"

#define HGSL_TIMELINE_NAME_LEN 64

#define HGSL_ISYNC_32BITS_TIMELINE 0
#define HGSL_ISYNC_64BITS_TIMELINE 1

/* Support upto 3 GVMs: 3 DBQs(Low/Medium/High priority) per GVM */
#define MAX_DB_QUEUE 9
#define HGSL_TCSR_NUM 4

struct qcom_hgsl;
struct hgsl_hsync_timeline;

#pragma pack(push, 4)
struct shadow_ts {
	unsigned int sop;
	unsigned int unused1;
	unsigned int eop;
	unsigned int unused2;
	unsigned int reserved[6];
};
#pragma pack(pop)

struct reg {
	unsigned long paddr;
	unsigned long size;
	void __iomem *vaddr;
};

struct hw_version {
	unsigned int version;
	unsigned int release;
};

struct db_buffer {
	int32_t dwords;
	void  *vaddr;
};

struct doorbell_queue {
	struct dma_buf *dma;
	struct iosys_map map;
	void *vbase;
	struct db_buffer data;
	uint32_t state;
	int tcsr_idx;
	uint32_t dbq_idx;
	struct mutex lock;
	atomic_t seq_num;
};

struct doorbell_context_queue {
	struct hgsl_mem_node *queue_mem;
	struct iosys_map map;
	uint32_t db_signal;
	uint32_t seq_num;
	void *queue_header;
	void *queue_body;
	void *indirect_ibs;
	uint32_t queue_header_gmuaddr;
	uint32_t queue_body_gmuaddr;
	uint32_t indirect_ibs_gmuaddr;
	uint32_t queue_size;
	int irq_idx;
};

struct qcom_hgsl {
	struct device *dev;

	/* character device info */
	struct cdev cdev;
	dev_t device_no;
	struct class *driver_class;
	struct device *class_dev;

	/* registers mapping */
	struct reg reg_ver;
	struct reg reg_dbidx;

	struct doorbell_queue dbq[MAX_DB_QUEUE];
	struct hgsl_dbq_info dbq_info[MAX_DB_QUEUE];

	/* Could disable db and use isync only */
	bool db_off;

	/* global doorbell tcsr */
	struct hgsl_tcsr *tcsr[HGSL_TCSR_NUM][HGSL_TCSR_ROLE_MAX];
	int tcsr_idx;
	struct hgsl_context **contexts;
	rwlock_t ctxt_lock;

	struct list_head active_wait_list;
	spinlock_t active_wait_lock;

	struct workqueue_struct *wq;
	struct work_struct ts_retire_work;

	struct hw_version *ver;
	struct hgsl_hyp_priv_t global_hyp;
	bool global_hyp_inited;
	struct mutex mutex;
	struct list_head release_list;
	struct workqueue_struct *release_wq;
	struct work_struct release_work;
	struct idr isync_timeline_idr;
	spinlock_t isync_timeline_lock;
	atomic64_t total_mem_size;
};

/**
 * HGSL context define
 **/
struct hgsl_context {
	struct hgsl_priv *priv;
	struct iosys_map map;
	uint32_t context_id;
	uint32_t devhandle;
	uint32_t flags;
	struct shadow_ts *shadow_ts;
	wait_queue_head_t wait_q;
	pid_t pid;
	bool dbq_assigned;
	uint32_t dbq_info;
	struct doorbell_queue *dbq;
	struct hgsl_mem_node shadow_ts_node;
	uint32_t shadow_ts_flags;
	bool in_destroy;
	bool destroyed;
	struct kref kref;

	uint32_t last_ts;
	struct hgsl_hsync_timeline *timeline;
	uint32_t queued_ts;
	bool is_killed;
	int tcsr_idx;
	struct mutex lock;
	struct doorbell_context_queue *dbcq;
	uint32_t dbcq_export_id;
};

struct hgsl_priv {
	struct qcom_hgsl *dev;
	pid_t pid;
	struct list_head node;
	struct hgsl_hyp_priv_t hyp_priv;
	struct mutex lock;
	struct list_head mem_mapped;
	struct list_head mem_allocated;

	atomic64_t total_mem_size;
};


static inline bool hgsl_ts32_ge(uint32_t a, uint32_t b)
{
	static const uint32_t TIMESTAMP_WINDOW = 0x80000000;

	return (a - b) < TIMESTAMP_WINDOW;
}

static inline bool hgsl_ts64_ge(uint64_t a, uint64_t b)
{
	static const uint64_t TIMESTAMP_WINDOW = 0x8000000000000000LL;

	return (a - b) < TIMESTAMP_WINDOW;
}

static inline bool hgsl_ts_ge(uint64_t a, uint64_t b, bool is64)
{
	if (is64)
		return hgsl_ts64_ge(a, b);
	else
		return hgsl_ts32_ge((uint32_t)a, (uint32_t)b);
}

/**
 * struct hgsl_hsync_timeline - A sync timeline attached under each hgsl context
 * @kref: Refcount to keep the struct alive
 * @name: String to describe this timeline
 * @fence_context: Used by the fence driver to identify fences belonging to
 *		   this context
 * @child_list_head: List head for all fences on this timeline
 * @lock: Spinlock to protect this timeline
 * @last_ts: Last timestamp when signaling fences
 */
struct hgsl_hsync_timeline {
	struct kref kref;
	struct hgsl_context *context;

	char name[HGSL_TIMELINE_NAME_LEN];
	u64 fence_context;

	spinlock_t lock;
	struct list_head fence_list;
	unsigned int last_ts;
};

/**
 * struct hgsl_hsync_fence - A struct containing a fence and other data
 *				associated with it
 * @fence: The fence struct
 * @sync_file: Pointer to the sync file
 * @parent: Pointer to the hgsl sync timeline this fence is on
 * @child_list: List of fences on the same timeline
 * @context_id: hgsl context id
 * @ts: Context timestamp that this fence is associated with
 */
struct hgsl_hsync_fence {
	struct dma_fence fence;
	struct sync_file *sync_file;
	struct hgsl_hsync_timeline *timeline;
	struct list_head child_list;
	u32 context_id;
	unsigned int ts;
};

struct hgsl_isync_timeline {
	struct kref kref;
	struct list_head free_list;
	char name[HGSL_TIMELINE_NAME_LEN];
	int id;
	struct hgsl_priv *priv;
	struct list_head fence_list;
	u64 context;
	spinlock_t lock;
	u64 last_ts;
	u32 flags;
	bool is64bits;
};

struct hgsl_isync_fence {
	struct dma_fence fence;
	struct list_head free_list;  /* For free in batch */
	struct hgsl_isync_timeline *timeline;
	struct list_head child_list;
	u64 ts;
};

/* Fence for commands. */
struct hgsl_hsync_fence *hgsl_hsync_fence_create(
					struct hgsl_context *context,
					uint32_t ts);
int hgsl_hsync_fence_create_fd(struct hgsl_context *context,
				uint32_t ts);
int hgsl_hsync_timeline_create(struct hgsl_context *context);
void hgsl_hsync_timeline_signal(struct hgsl_hsync_timeline *timeline,
						unsigned int ts);
void hgsl_hsync_timeline_put(struct hgsl_hsync_timeline *timeline);
void hgsl_hsync_timeline_fini(struct hgsl_context *context);

/* Fence for process sync. */
int hgsl_isync_timeline_create(struct hgsl_priv *priv,
				    uint32_t *timeline_id,
				    uint32_t flags,
				    uint64_t initial_ts);
int hgsl_isync_timeline_destroy(struct hgsl_priv *priv, uint32_t id);
void hgsl_isync_fini(struct hgsl_priv *priv);
int hgsl_isync_fence_create(struct hgsl_priv *priv, uint32_t timeline_id,
				uint32_t ts, bool ts_is_valid, int *fence_fd);
int hgsl_isync_fence_signal(struct hgsl_priv *priv, uint32_t timeline_id,
							       int fence_fd);
int hgsl_isync_forward(struct hgsl_priv *priv, uint32_t timeline_id,
								uint64_t ts, bool check_owner);
int hgsl_isync_query(struct hgsl_priv *priv, uint32_t timeline_id,
							uint64_t *ts);
int hgsl_isync_wait_multiple(struct hgsl_priv *priv, struct hgsl_timeline_wait *param);

#endif /* __HGSL_H_ */
