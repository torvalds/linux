/*
 * MobiCore client library device management.
 *
 * Device and Trustlet Session management Functions.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009 - 2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MC_KAPI_DEVICE_H_
#define _MC_KAPI_DEVICE_H_

#include <linux/list.h>

#include "connection.h"
#include "session.h"
#include "wsm.h"

struct mcore_device_t {
	/* MobiCore Trustlet session associated with the device */
	struct list_head	session_vector;
	struct list_head	 wsm_l2_vector; /* WSM L2 Table  */

	uint32_t		device_id;	/* Device identifier */
	struct connection	*connection;	/* The device connection */
	struct mc_instance	*instance;	/* MobiCore Driver instance */

	/* The list param for using the kernel lists */
	struct list_head	list;
};

struct mcore_device_t *mcore_device_create(
		uint32_t device_id, struct connection *connection);
void mcore_device_cleanup(struct mcore_device_t *dev);


bool mcore_device_open(struct mcore_device_t *dev, const char *deviceName);
void mcore_device_close(struct mcore_device_t *dev);
bool mcore_device_has_sessions(struct mcore_device_t *dev);
bool mcore_device_create_new_session(
		struct mcore_device_t *dev, uint32_t session_id,
		struct connection *connection);
bool mcore_device_remove_session(
		struct mcore_device_t *dev, uint32_t session_id);
struct session *mcore_device_resolve_session_id(
		struct mcore_device_t *dev, uint32_t session_id);
struct wsm *mcore_device_allocate_contiguous_wsm(
		struct mcore_device_t *dev, uint32_t len);
bool mcore_device_free_contiguous_wsm(
		struct mcore_device_t *dev, struct wsm *wsm);
struct wsm *mcore_device_find_contiguous_wsm(
		struct mcore_device_t *dev, void *virt_addr);

#endif /* _MC_KAPI_DEVICE_H_ */
