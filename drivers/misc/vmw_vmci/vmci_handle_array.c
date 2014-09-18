/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/slab.h>
#include "vmci_handle_array.h"

static size_t handle_arr_calc_size(size_t capacity)
{
	return sizeof(struct vmci_handle_arr) +
	    capacity * sizeof(struct vmci_handle);
}

struct vmci_handle_arr *vmci_handle_arr_create(size_t capacity)
{
	struct vmci_handle_arr *array;

	if (capacity == 0)
		capacity = VMCI_HANDLE_ARRAY_DEFAULT_SIZE;

	array = kmalloc(handle_arr_calc_size(capacity), GFP_ATOMIC);
	if (!array)
		return NULL;

	array->capacity = capacity;
	array->size = 0;

	return array;
}

void vmci_handle_arr_destroy(struct vmci_handle_arr *array)
{
	kfree(array);
}

void vmci_handle_arr_append_entry(struct vmci_handle_arr **array_ptr,
				  struct vmci_handle handle)
{
	struct vmci_handle_arr *array = *array_ptr;

	if (unlikely(array->size >= array->capacity)) {
		/* reallocate. */
		struct vmci_handle_arr *new_array;
		size_t new_capacity = array->capacity * VMCI_ARR_CAP_MULT;
		size_t new_size = handle_arr_calc_size(new_capacity);

		new_array = krealloc(array, new_size, GFP_ATOMIC);
		if (!new_array)
			return;

		new_array->capacity = new_capacity;
		*array_ptr = array = new_array;
	}

	array->entries[array->size] = handle;
	array->size++;
}

/*
 * Handle that was removed, VMCI_INVALID_HANDLE if entry not found.
 */
struct vmci_handle vmci_handle_arr_remove_entry(struct vmci_handle_arr *array,
						struct vmci_handle entry_handle)
{
	struct vmci_handle handle = VMCI_INVALID_HANDLE;
	size_t i;

	for (i = 0; i < array->size; i++) {
		if (vmci_handle_is_equal(array->entries[i], entry_handle)) {
			handle = array->entries[i];
			array->size--;
			array->entries[i] = array->entries[array->size];
			array->entries[array->size] = VMCI_INVALID_HANDLE;
			break;
		}
	}

	return handle;
}

/*
 * Handle that was removed, VMCI_INVALID_HANDLE if array was empty.
 */
struct vmci_handle vmci_handle_arr_remove_tail(struct vmci_handle_arr *array)
{
	struct vmci_handle handle = VMCI_INVALID_HANDLE;

	if (array->size) {
		array->size--;
		handle = array->entries[array->size];
		array->entries[array->size] = VMCI_INVALID_HANDLE;
	}

	return handle;
}

/*
 * Handle at given index, VMCI_INVALID_HANDLE if invalid index.
 */
struct vmci_handle
vmci_handle_arr_get_entry(const struct vmci_handle_arr *array, size_t index)
{
	if (unlikely(index >= array->size))
		return VMCI_INVALID_HANDLE;

	return array->entries[index];
}

bool vmci_handle_arr_has_entry(const struct vmci_handle_arr *array,
			       struct vmci_handle entry_handle)
{
	size_t i;

	for (i = 0; i < array->size; i++)
		if (vmci_handle_is_equal(array->entries[i], entry_handle))
			return true;

	return false;
}

/*
 * NULL if the array is empty. Otherwise, a pointer to the array
 * of VMCI handles in the handle array.
 */
struct vmci_handle *vmci_handle_arr_get_handles(struct vmci_handle_arr *array)
{
	if (array->size)
		return array->entries;

	return NULL;
}
