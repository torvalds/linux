/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DRM_CLIENT_H_
#define _XE_DRM_CLIENT_H_

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

struct drm_file;
struct drm_printer;

struct xe_drm_client {
	struct kref kref;
	unsigned int id;
};

	static inline struct xe_drm_client *
xe_drm_client_get(struct xe_drm_client *client)
{
	kref_get(&client->kref);
	return client;
}

void __xe_drm_client_free(struct kref *kref);

static inline void xe_drm_client_put(struct xe_drm_client *client)
{
	kref_put(&client->kref, __xe_drm_client_free);
}

struct xe_drm_client *xe_drm_client_alloc(void);
static inline struct xe_drm_client *
xe_drm_client_get(struct xe_drm_client *client);
static inline void xe_drm_client_put(struct xe_drm_client *client);

#endif
