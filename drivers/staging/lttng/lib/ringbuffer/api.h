#ifndef _LINUX_RING_BUFFER_API_H
#define _LINUX_RING_BUFFER_API_H

/*
 * linux/ringbuffer/api.h
 *
 * Copyright (C) 2010 - Mathieu Desnoyers "mathieu.desnoyers@efficios.com"
 *
 * Ring Buffer API.
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"
#include "../../wrapper/ringbuffer/vfs.h"

/*
 * ring_buffer_frontend_api.h contains static inline functions that depend on
 * client static inlines. Hence the inclusion of this "api" header only
 * within the client.
 */
#include "../../wrapper/ringbuffer/frontend_api.h"

#endif /* _LINUX_RING_BUFFER_API_H */
