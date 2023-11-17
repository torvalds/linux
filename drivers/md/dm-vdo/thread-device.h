/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_THREAD_DEVICE_H
#define UDS_THREAD_DEVICE_H

#include "thread-registry.h"

void uds_register_thread_device_id(struct registered_thread *new_thread,
				   unsigned int *id_ptr);

void uds_unregister_thread_device_id(void);

int uds_get_thread_device_id(void);

void uds_initialize_thread_device_registry(void);

#endif /* UDS_THREAD_DEVICE_H */
