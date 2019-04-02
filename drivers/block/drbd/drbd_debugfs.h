/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/defs.h>

#include "drbd_int.h"

#ifdef CONFIG_DE_FS
int __init drbd_defs_init(void);
void drbd_defs_cleanup(void);

void drbd_defs_resource_add(struct drbd_resource *resource);
void drbd_defs_resource_cleanup(struct drbd_resource *resource);

void drbd_defs_connection_add(struct drbd_connection *connection);
void drbd_defs_connection_cleanup(struct drbd_connection *connection);

void drbd_defs_device_add(struct drbd_device *device);
void drbd_defs_device_cleanup(struct drbd_device *device);

void drbd_defs_peer_device_add(struct drbd_peer_device *peer_device);
void drbd_defs_peer_device_cleanup(struct drbd_peer_device *peer_device);
#else

static inline int __init drbd_defs_init(void) { return -ENODEV; }
static inline void drbd_defs_cleanup(void) { }

static inline void drbd_defs_resource_add(struct drbd_resource *resource) { }
static inline void drbd_defs_resource_cleanup(struct drbd_resource *resource) { }

static inline void drbd_defs_connection_add(struct drbd_connection *connection) { }
static inline void drbd_defs_connection_cleanup(struct drbd_connection *connection) { }

static inline void drbd_defs_device_add(struct drbd_device *device) { }
static inline void drbd_defs_device_cleanup(struct drbd_device *device) { }

static inline void drbd_defs_peer_device_add(struct drbd_peer_device *peer_device) { }
static inline void drbd_defs_peer_device_cleanup(struct drbd_peer_device *peer_device) { }

#endif
