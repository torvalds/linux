/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#ifndef _VMCI_HANDLE_ARRAY_H_
#define _VMCI_HANDLE_ARRAY_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/limits.h>
#include <linux/types.h>

struct vmci_handle_arr {
	u32 capacity;
	u32 max_capacity;
	u32 size;
	u32 pad;
	struct vmci_handle entries[];
};

/* Select a default capacity that results in a 64 byte sized array */
#define VMCI_HANDLE_ARRAY_DEFAULT_CAPACITY			6

struct vmci_handle_arr *vmci_handle_arr_create(u32 capacity, u32 max_capacity);
void vmci_handle_arr_destroy(struct vmci_handle_arr *array);
int vmci_handle_arr_append_entry(struct vmci_handle_arr **array_ptr,
				 struct vmci_handle handle);
struct vmci_handle vmci_handle_arr_remove_entry(struct vmci_handle_arr *array,
						struct vmci_handle
						entry_handle);
struct vmci_handle vmci_handle_arr_remove_tail(struct vmci_handle_arr *array);
struct vmci_handle
vmci_handle_arr_get_entry(const struct vmci_handle_arr *array, u32 index);
bool vmci_handle_arr_has_entry(const struct vmci_handle_arr *array,
			       struct vmci_handle entry_handle);
struct vmci_handle *vmci_handle_arr_get_handles(struct vmci_handle_arr *array);

static inline u32 vmci_handle_arr_get_size(
	const struct vmci_handle_arr *array)
{
	return array->size;
}


#endif /* _VMCI_HANDLE_ARRAY_H_ */
