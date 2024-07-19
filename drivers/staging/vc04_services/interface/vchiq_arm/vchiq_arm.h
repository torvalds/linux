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

/* Some per-instance constants */
#define MAX_COMPLETIONS 128
#define MAX_SERVICES 64
#define MAX_ELEMENTS 8
#define MSG_QUEUE_SIZE 128

#define VCHIQ_DRV_MAX_CALLBACKS 10

struct rpi_firmware;
struct vchiq_device;

enum USE_TYPE_E {
	USE_TYPE_SERVICE,
	USE_TYPE_VCHIQ
};

struct vchiq_platform_info {
	unsigned int cache_line_size;
};

struct vchiq_drv_mgmt {
	struct rpi_firmware *fw;
	const struct vchiq_platform_info *info;

	bool connected;
	int num_deferred_callbacks;
	/* Protects connected and num_deferred_callbacks */
	struct mutex connected_mutex;

	void (*deferred_callback[VCHIQ_DRV_MAX_CALLBACKS])(void);

	struct semaphore free_fragments_sema;
	struct semaphore free_fragments_mutex;
	char *fragments_base;
	char *free_fragments;
	unsigned int fragments_size;

	void __iomem *regs;

	struct vchiq_state state;
};

struct user_service {
	struct vchiq_service *service;
	void __user *userdata;
	struct vchiq_instance *instance;
	char is_vchi;
	char dequeue_pending;
	char close_pending;
	int message_available_pos;
	int msg_insert;
	int msg_remove;
	struct completion insert_event;
	struct completion remove_event;
	struct completion close_event;
	struct vchiq_header *msg_queue[MSG_QUEUE_SIZE];
};

struct bulk_waiter_node {
	struct bulk_waiter bulk_waiter;
	int pid;
	struct list_head list;
};

struct vchiq_instance {
	struct vchiq_state *state;
	struct vchiq_completion_data_kernel completions[MAX_COMPLETIONS];
	int completion_insert;
	int completion_remove;
	struct completion insert_event;
	struct completion remove_event;
	struct mutex completion_mutex;

	int connected;
	int closing;
	int pid;
	int mark;
	int use_close_delivered;
	int trace;

	struct list_head bulk_waiter_list;
	struct mutex bulk_waiter_list_mutex;

	struct vchiq_debugfs_node debugfs_node;
};

int
vchiq_use_service(struct vchiq_instance *instance, unsigned int handle);

extern int
vchiq_release_service(struct vchiq_instance *instance, unsigned int handle);

extern int
vchiq_check_service(struct vchiq_service *service);

extern void
vchiq_dump_service_use_state(struct vchiq_state *state);

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

extern void
vchiq_add_connected_callback(struct vchiq_device *device,
			     void (*callback)(void));

#if IS_ENABLED(CONFIG_VCHIQ_CDEV)

extern void
vchiq_deregister_chrdev(void);

extern int
vchiq_register_chrdev(struct device *parent);

#else

static inline void vchiq_deregister_chrdev(void) { }
static inline int vchiq_register_chrdev(struct device *parent) { return 0; }

#endif /* IS_ENABLED(CONFIG_VCHIQ_CDEV) */

extern int
service_callback(struct vchiq_instance *vchiq_instance, enum vchiq_reason reason,
		 struct vchiq_header *header, unsigned int handle, void *bulk_userdata);

extern void
free_bulk_waiter(struct vchiq_instance *instance);

#endif /* VCHIQ_ARM_H */
