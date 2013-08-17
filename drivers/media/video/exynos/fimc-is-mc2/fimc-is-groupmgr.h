/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_GROUP_MGR_H
#define FIMC_IS_GROUP_MGR_H

#include "fimc-is-time.h"
#include "fimc-is-device.h"
#include "fimc-is-video.h"

#define TRACE_GROUP
#define GROUP_ID_3A0		0 /* hardware : CH0 */
#define GROUP_ID_3A1		1 /* hardware : 3AA */
#define GROUP_ID_ISP		2 /* hardware : CH1 */
#define GROUP_ID_DIS		3
#define GROUP_ID_MAX		4
#define GROUP_ID_INVALID	(0xFF)
#define GROUP_ID_SHIFT		(16)
#define GROUP_ID_MASK		(0xFFFF)
#define GROUP_ID(id)		(1 << id)

#define FIMC_IS_MAX_GFRAME	15 /* max shot buffer of F/W */
#define MIN_OF_ASYNC_SHOTS	1
#define MIN_OF_SYNC_SHOTS	2
#define MIN_OF_SHOT_RSC		(MIN_OF_ASYNC_SHOTS + MIN_OF_SYNC_SHOTS)

enum fimc_is_group_state {
	FIMC_IS_GROUP_OPEN,
	FIMC_IS_GROUP_READY,
	FIMC_IS_GROUP_ACTIVE,
	FIMC_IS_GROUP_RUN,
	FIMC_IS_GROUP_REQUEST_FSTOP,
	FIMC_IS_GROUP_FORCE_STOP,
	FIMC_IS_GROUP_OTF_INPUT
};

enum fimc_is_global_group_state {
	FIMC_IS_GGROUP_INIT,
	FIMC_IS_GGROUP_STOP
};

struct fimc_is_frame;
struct fimc_is_device_ischain;
typedef int (*fimc_is_start_callback)(struct fimc_is_device_ischain *device,
	struct fimc_is_frame *frame);

struct fimc_is_group_frame {
	struct list_head		list;
	u32				fcount;
};

struct fimc_is_group_framemgr {
	struct fimc_is_group_frame	frame[FIMC_IS_MAX_GFRAME];
	spinlock_t			frame_slock;
	struct list_head		frame_free_head;
	u32				frame_free_cnt;
};

struct fimc_is_group {
	struct fimc_is_group		*next;
	struct fimc_is_group		*prev;

	struct fimc_is_subdev		leader;
	struct fimc_is_subdev		*subdev[ENTRY_END];
	struct kthread_worker		*worker;

	/* for otf interface */
	atomic_t			sensor_fcount;
	atomic_t			backup_fcount;
	struct semaphore		smp_trigger;
	struct semaphore		smp_shot;
	atomic_t			smp_shot_count;
	u32				async_shots;
	u32				sync_shots;
	struct camera2_ctl		fast_ctl;

	u32				id; /* group id */
	u32				instance; /* device instance */
	u32				fcount; /* frame count */
	atomic_t			scount; /* shot count */
	atomic_t			rcount; /* request count */
	unsigned long			state;

	struct list_head		frame_group_head;
	u32				frame_group_cnt;

	fimc_is_start_callback		start_callback;
	struct fimc_is_device_ischain	*device;

#ifdef MEASURE_TIME
#ifdef INTERNAL_TIME
	struct fimc_is_time		time;
#endif
#endif
};

struct fimc_is_groupmgr {
	struct fimc_is_group_framemgr	framemgr[FIMC_IS_MAX_NODES];
	struct fimc_is_group		*group[FIMC_IS_MAX_NODES][GROUP_ID_MAX];
	struct kthread_worker		group_worker[GROUP_ID_MAX];
	struct task_struct		*group_task[GROUP_ID_MAX];
	struct semaphore		group_smp_res[GROUP_ID_MAX];
	unsigned long			group_state[GROUP_ID_MAX];
	atomic_t			group_cnt;
};

int fimc_is_groupmgr_probe(struct fimc_is_groupmgr *groupmgr);
int fimc_is_group_open(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group, u32 id, u32 instance,
	struct fimc_is_video_ctx *vctx,
	struct fimc_is_device_ischain *device,
	fimc_is_start_callback start_callback);
int fimc_is_group_close(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_video_ctx *vctx);
int fimc_is_group_process_start(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_queue *queue);
int fimc_is_group_process_stop(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_queue *queue);
int fimc_is_group_buffer_queue(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_queue *queue,
	u32 index);
int fimc_is_group_buffer_finish(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group, u32 index);
int fimc_is_group_start(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_frame *frame);
int fimc_is_group_done(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group);

int fimc_is_gframe_cancel(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group, u32 target_fcount);

#define GET_GROUP_FRAMEMGR(group) \
	(((group) && (group)->leader.vctx) ? (&(group)->leader.vctx->q_src.framemgr) : NULL)

#endif
