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

#ifndef _VMCI_HANDLE_ARRAY_H_
#define _VMCI_HANDLE_ARRAY_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/types.h>

#define VMCI_HANDLE_ARRAY_DEFAULT_SIZE 4
#define VMCI_ARR_CAP_MULT 2	/* Array capacity multiplier */

struct vmci_handle_arr {
	size_t capacity;
	size_t size;
	struct vmci_handle entries[];
};

struct vmci_handle_arr *vmci_handle_arr_create(size_t capacity);
void vmci_handle_arr_destroy(struct vmci_handle_arr *array);
void vmci_handle_arr_append_entry(struct vmci_handle_arr **array_ptr,
				  struct vmci_handle handle);
struct vmci_handle vmci_handle_arr_remove_entry(struct vmci_handle_arr *array,
						struct vmci_handle
						entry_handle);
struct vmci_handle vmci_handle_arr_remove_tail(struct vmci_handle_arr *array);
struct vmci_handle
vmci_handle_arr_get_entry(const struct vmci_handle_arr *array, size_t index);
bool vmci_handle_arr_has_entry(const struct vmci_handle_arr *array,
			       struct vmci_handle entry_handle);
struct vmci_handle *vmci_handle_arr_get_handles(struct vmci_handle_arr *array);

static inline size_t vmci_handle_arr_get_size(
	const struct vmci_handle_arr *array)
{
	return array->size;
}


#endif /* _VMCI_HANDLE_ARRAY_H_ */
