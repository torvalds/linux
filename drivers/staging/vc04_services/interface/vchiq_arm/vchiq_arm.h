/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */

#ifndef VCHIQ_ARM_H
#define VCHIQ_ARM_H

#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/atomic.h>
#include "vchiq_core.h"
#include "vchiq_debugfs.h"

enum vc_suspend_status {
	VC_SUSPEND_FORCE_CANCELED = -3, /* Force suspend canceled, too busy */
	VC_SUSPEND_REJECTED = -2,  /* Videocore rejected suspend request */
	VC_SUSPEND_FAILED = -1,    /* Videocore suspend failed */
	VC_SUSPEND_IDLE = 0,       /* VC active, no suspend actions */
	VC_SUSPEND_REQUESTED,      /* User has requested suspend */
	VC_SUSPEND_IN_PROGRESS,    /* Slot handler has recvd suspend request */
	VC_SUSPEND_SUSPENDED       /* Videocore suspend succeeded */
};

enum vc_resume_status {
	VC_RESUME_FAILED = -1, /* Videocore resume failed */
	VC_RESUME_IDLE = 0,    /* VC suspended, no resume actions */
	VC_RESUME_REQUESTED,   /* User has requested resume */
	VC_RESUME_IN_PROGRESS, /* Slot handler has received resume request */
	VC_RESUME_RESUMED      /* Videocore resumed successfully (active) */
};

enum USE_TYPE_E {
	USE_TYPE_SERVICE,
	USE_TYPE_SERVICE_NO_RESUME,
	USE_TYPE_VCHIQ
};

struct vchiq_arm_state {
	/* Keepalive-related data */
	struct task_struct *ka_thread;
	struct completion ka_evt;
	atomic_t ka_use_count;
	atomic_t ka_use_ack_count;
	atomic_t ka_release_count;

	struct completion vc_suspend_complete;
	struct completion vc_resume_complete;

	rwlock_t susp_res_lock;
	enum vc_suspend_status vc_suspend_state;
	enum vc_resume_status vc_resume_state;

	unsigned int wake_address;

	struct vchiq_state *state;
	struct timer_list suspend_timer;
	int suspend_timer_timeout;
	int suspend_timer_running;

	/* Global use count for videocore.
	** This is equal to the sum of the use counts for all services.  When
	** this hits zero the videocore suspend procedure will be initiated.
	*/
	int videocore_use_count;

	/* Use count to track requests from videocore peer.
	** This use count is not associated with a service, so needs to be
	** tracked separately with the state.
	*/
	int peer_use_count;

	/* Flag to indicate whether resume is blocked.  This happens when the
	** ARM is suspending
	*/
	struct completion resume_blocker;
	int resume_blocked;
	struct completion blocked_blocker;
	int blocked_count;

	int autosuspend_override;

	/* Flag to indicate that the first vchiq connect has made it through.
	** This means that both sides should be fully ready, and we should
	** be able to suspend after this point.
	*/
	int first_connect;

	unsigned long long suspend_start_time;
	unsigned long long sleep_start_time;
	unsigned long long resume_start_time;
	unsigned long long last_wake_time;

};

struct vchiq_drvdata {
	const unsigned int cache_line_size;
	struct rpi_firmware *fw;
};

extern int vchiq_arm_log_level;
extern int vchiq_susp_log_level;

int vchiq_platform_init(struct platform_device *pdev,
			struct vchiq_state *state);

extern struct vchiq_state *
vchiq_get_state(void);

extern VCHIQ_STATUS_T
vchiq_arm_vcsuspend(struct vchiq_state *state);

extern VCHIQ_STATUS_T
vchiq_arm_force_suspend(struct vchiq_state *state);

extern int
vchiq_arm_allow_resume(struct vchiq_state *state);

extern VCHIQ_STATUS_T
vchiq_arm_vcresume(struct vchiq_state *state);

extern VCHIQ_STATUS_T
vchiq_arm_init_state(struct vchiq_state *state,
		     struct vchiq_arm_state *arm_state);

extern int
vchiq_check_resume(struct vchiq_state *state);

extern void
vchiq_check_suspend(struct vchiq_state *state);
VCHIQ_STATUS_T
vchiq_use_service(VCHIQ_SERVICE_HANDLE_T handle);

extern VCHIQ_STATUS_T
vchiq_release_service(VCHIQ_SERVICE_HANDLE_T handle);

extern VCHIQ_STATUS_T
vchiq_check_service(struct vchiq_service *service);

extern VCHIQ_STATUS_T
vchiq_platform_suspend(struct vchiq_state *state);

extern int
vchiq_platform_videocore_wanted(struct vchiq_state *state);

extern int
vchiq_platform_use_suspend_timer(void);

extern void
vchiq_dump_platform_use_state(struct vchiq_state *state);

extern void
vchiq_dump_service_use_state(struct vchiq_state *state);

extern struct vchiq_arm_state*
vchiq_platform_get_arm_state(struct vchiq_state *state);

extern int
vchiq_videocore_wanted(struct vchiq_state *state);

extern VCHIQ_STATUS_T
vchiq_use_internal(struct vchiq_state *state, struct vchiq_service *service,
		   enum USE_TYPE_E use_type);
extern VCHIQ_STATUS_T
vchiq_release_internal(struct vchiq_state *state,
		       struct vchiq_service *service);

extern struct vchiq_debugfs_node *
vchiq_instance_get_debugfs_node(VCHIQ_INSTANCE_T instance);

extern int
vchiq_instance_get_use_count(VCHIQ_INSTANCE_T instance);

extern int
vchiq_instance_get_pid(VCHIQ_INSTANCE_T instance);

extern int
vchiq_instance_get_trace(VCHIQ_INSTANCE_T instance);

extern void
vchiq_instance_set_trace(VCHIQ_INSTANCE_T instance, int trace);

extern void
set_suspend_state(struct vchiq_arm_state *arm_state,
		  enum vc_suspend_status new_state);

extern void
set_resume_state(struct vchiq_arm_state *arm_state,
		 enum vc_resume_status new_state);

extern void
start_suspend_timer(struct vchiq_arm_state *arm_state);

#endif /* VCHIQ_ARM_H */
