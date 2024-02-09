// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "thread-device.h"

#include "thread-registry.h"

/* A registry of threads associated with device id numbers. */
static struct thread_registry device_id_thread_registry;

/* Any registered thread must be unregistered. */
void uds_register_thread_device_id(struct registered_thread *new_thread,
				   unsigned int *id_ptr)
{
	vdo_register_thread(&device_id_thread_registry, new_thread, id_ptr);
}

void uds_unregister_thread_device_id(void)
{
	vdo_unregister_thread(&device_id_thread_registry);
}

int uds_get_thread_device_id(void)
{
	const unsigned int *pointer;

	pointer = vdo_lookup_thread(&device_id_thread_registry);
	return (pointer != NULL) ? *pointer : -1;
}

void uds_initialize_thread_device_registry(void)
{
	vdo_initialize_thread_registry(&device_id_thread_registry);
}
