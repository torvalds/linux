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

enum USE_TYPE_E {
	USE_TYPE_SERVICE,
	USE_TYPE_VCHIQ
};

struct vchiq_arm_state {
	/* Keepalive-related data */
	struct task_struct *ka_thread;
	struct completion ka_evt;
	atomic_t ka_use_count;
	atomic_t ka_use_ack_count;
	atomic_t ka_release_count;

	rwlock_t susp_res_lock;

	struct vchiq_state *state;

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

	/* Flag to indicate that the first vchiq connect has made it through.
	** This means that both sides should be fully ready, and we should
	** be able to suspend after this point.
	*/
	int first_connect;
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

extern enum vchiq_status
vchiq_arm_init_state(struct vchiq_state *state,
		     struct vchiq_arm_state *arm_state);

extern void
vchiq_check_suspend(struct vchiq_state *state);
enum vchiq_status
vchiq_use_service(unsigned int handle);

extern enum vchiq_status
vchiq_release_service(unsigned int handle);

extern enum vchiq_status
vchiq_check_service(struct vchiq_service *service);

extern void
vchiq_dump_platform_use_state(struct vchiq_state *state);

extern void
vchiq_dump_service_use_state(struct vchiq_state *state);

extern struct vchiq_arm_state*
vchiq_platform_get_arm_state(struct vchiq_state *state);


extern int
vchiq_use_internal(struct vchiq_state *state, struct vchiq_service *service,
		   enum USE_TYPE_E use_type);
extern int
vchiq_release_internal(struct vchiq_state *state,
		       struct vchiq_service *service);

extern struct vchiq_debugfs_node *
vchiq_instance_get_debugfs_node(struct vchiq_instance *instance);

extern int
vchiq_instance_get_use_count(struct vchiq_instance *instance);

extern int
vchiq_instance_get_pid(struct vchiq_instance *instance);

extern int
vchiq_instance_get_trace(struct vchiq_instance *instance);

extern void
vchiq_instance_set_trace(struct vchiq_instance *instance, int trace);

#endif /* VCHIQ_ARM_H */
