// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "hmm.h"

#include "ia_css_refcount.h"
#include "sh_css_defs.h"

#include "platform_support.h"

#include "assert_support.h"

#include "ia_css_debug.h"

/* TODO: enable for other memory aswell
	 now only for ia_css_ptr */
struct ia_css_refcount_entry {
	u32 count;
	ia_css_ptr data;
	s32 id;
};

struct ia_css_refcount_list {
	u32 size;
	struct ia_css_refcount_entry *items;
};

static struct ia_css_refcount_list myrefcount;

static struct ia_css_refcount_entry *refcount_find_entry(ia_css_ptr ptr,
	bool firstfree)
{
	u32 i;

	if (ptr == 0)
		return NULL;
	if (!myrefcount.items) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "refcount_find_entry(): Ref count not initialized!\n");
		return NULL;
	}

	for (i = 0; i < myrefcount.size; i++) {
		if ((&myrefcount.items[i])->data == 0) {
			if (firstfree) {
				/* for new entry */
				return &myrefcount.items[i];
			}
		}
		if ((&myrefcount.items[i])->data == ptr) {
			/* found entry */
			return &myrefcount.items[i];
		}
	}
	return NULL;
}

int ia_css_refcount_init(uint32_t size)
{
	int err = 0;

	if (size == 0) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				    "ia_css_refcount_init(): Size of 0 for Ref count init!\n");
		return -EINVAL;
	}
	if (myrefcount.items) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				    "ia_css_refcount_init(): Ref count is already initialized\n");
		return -EINVAL;
	}
	myrefcount.items =
	    kvmalloc(sizeof(struct ia_css_refcount_entry) * size, GFP_KERNEL);
	if (!myrefcount.items)
		err = -ENOMEM;
	if (!err) {
		memset(myrefcount.items, 0,
		       sizeof(struct ia_css_refcount_entry) * size);
		myrefcount.size = size;
	}
	return err;
}

void ia_css_refcount_uninit(void)
{
	struct ia_css_refcount_entry *entry;
	u32 i;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_refcount_uninit() entry\n");
	for (i = 0; i < myrefcount.size; i++) {
		/* driver verifier tool has issues with &arr[i]
		   and prefers arr + i; as these are actually equivalent
		   the line below uses + i
		*/
		entry = myrefcount.items + i;
		if (entry->data != mmgr_NULL) {
			/*	ia_css_debug_dtrace(IA_CSS_DBG_TRACE,
				"ia_css_refcount_uninit: freeing (%x)\n",
				entry->data);*/
			hmm_free(entry->data);
			entry->data = mmgr_NULL;
			entry->count = 0;
			entry->id = 0;
		}
	}
	kvfree(myrefcount.items);
	myrefcount.items = NULL;
	myrefcount.size = 0;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_refcount_uninit() leave\n");
}

ia_css_ptr ia_css_refcount_increment(s32 id, ia_css_ptr ptr)
{
	struct ia_css_refcount_entry *entry;

	if (ptr == mmgr_NULL)
		return ptr;

	entry = refcount_find_entry(ptr, false);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_refcount_increment(%x) 0x%x\n", id, ptr);

	if (!entry) {
		entry = refcount_find_entry(ptr, true);
		assert(entry);
		if (!entry)
			return mmgr_NULL;
		entry->id = id;
	}

	if (entry->id != id) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "ia_css_refcount_increment(): Ref count IDS do not match!\n");
		return mmgr_NULL;
	}

	if (entry->data == ptr)
		entry->count += 1;
	else if (entry->data == mmgr_NULL) {
		entry->data = ptr;
		entry->count = 1;
	} else
		return mmgr_NULL;

	return ptr;
}

bool ia_css_refcount_decrement(s32 id, ia_css_ptr ptr)
{
	struct ia_css_refcount_entry *entry;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_refcount_decrement(%x) 0x%x\n", id, ptr);

	if (ptr == mmgr_NULL)
		return false;

	entry = refcount_find_entry(ptr, false);

	if (entry) {
		if (entry->id != id) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
					    "ia_css_refcount_decrement(): Ref count IDS do not match!\n");
			return false;
		}
		if (entry->count > 0) {
			entry->count -= 1;
			if (entry->count == 0) {
				/* ia_css_debug_dtrace(IA_CSS_DBEUG_TRACE,
				   "ia_css_refcount_decrement: freeing\n");*/
				hmm_free(ptr);
				entry->data = mmgr_NULL;
				entry->id = 0;
			}
			return true;
		}
	}

	/* SHOULD NOT HAPPEN: ptr not managed by refcount, or not
	   valid anymore */
	if (entry)
		IA_CSS_ERROR("id %x, ptr 0x%x entry %p entry->id %x entry->count %d\n",
			     id, ptr, entry, entry->id, entry->count);
	else
		IA_CSS_ERROR("entry NULL\n");
	assert(false);

	return false;
}

bool ia_css_refcount_is_single(ia_css_ptr ptr)
{
	struct ia_css_refcount_entry *entry;

	if (ptr == mmgr_NULL)
		return false;

	entry = refcount_find_entry(ptr, false);

	if (entry)
		return (entry->count == 1);

	return true;
}

void ia_css_refcount_clear(s32 id, clear_func clear_func_ptr)
{
	struct ia_css_refcount_entry *entry;
	u32 i;
	u32 count = 0;

	assert(clear_func_ptr);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_refcount_clear(%x)\n",
			    id);

	for (i = 0; i < myrefcount.size; i++) {
		/* driver verifier tool has issues with &arr[i]
		   and prefers arr + i; as these are actually equivalent
		   the line below uses + i
		*/
		entry = myrefcount.items + i;
		if ((entry->data != mmgr_NULL) && (entry->id == id)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "ia_css_refcount_clear: %x: 0x%x\n",
					    id, entry->data);
			if (clear_func_ptr) {
				/* clear using provided function */
				clear_func_ptr(entry->data);
			} else {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_refcount_clear: using hmm_free: no clear_func\n");
				hmm_free(entry->data);
			}

			if (entry->count != 0) {
				IA_CSS_WARNING("Ref count for entry %x is not zero!", entry->id);
			}

			assert(entry->count == 0);

			entry->data = mmgr_NULL;
			entry->count = 0;
			entry->id = 0;
			count++;
		}
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_refcount_clear(%x): cleared %d\n", id,
			    count);
}

bool ia_css_refcount_is_valid(ia_css_ptr ptr)
{
	struct ia_css_refcount_entry *entry;

	if (ptr == mmgr_NULL)
		return false;

	entry = refcount_find_entry(ptr, false);

	return entry;
}
