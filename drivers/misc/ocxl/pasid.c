// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include "ocxl_internal.h"


struct id_range {
	struct list_head list;
	u32 start;
	u32 end;
};

#ifdef DEBUG
static void dump_list(struct list_head *head, char *type_str)
{
	struct id_range *cur;

	pr_debug("%s ranges allocated:\n", type_str);
	list_for_each_entry(cur, head, list) {
		pr_debug("Range %d->%d\n", cur->start, cur->end);
	}
}
#endif

static int range_alloc(struct list_head *head, u32 size, int max_id,
		char *type_str)
{
	struct list_head *pos;
	struct id_range *cur, *new;
	int rc, last_end;

	new = kmalloc(sizeof(struct id_range), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	pos = head;
	last_end = -1;
	list_for_each_entry(cur, head, list) {
		if ((cur->start - last_end) > size)
			break;
		last_end = cur->end;
		pos = &cur->list;
	}

	new->start = last_end + 1;
	new->end = new->start + size - 1;

	if (new->end > max_id) {
		kfree(new);
		rc = -ENOSPC;
	} else {
		list_add(&new->list, pos);
		rc = new->start;
	}

#ifdef DEBUG
	dump_list(head, type_str);
#endif
	return rc;
}

static void range_free(struct list_head *head, u32 start, u32 size,
		char *type_str)
{
	bool found = false;
	struct id_range *cur, *tmp;

	list_for_each_entry_safe(cur, tmp, head, list) {
		if (cur->start == start && cur->end == (start + size - 1)) {
			found = true;
			list_del(&cur->list);
			kfree(cur);
			break;
		}
	}
	WARN_ON(!found);
#ifdef DEBUG
	dump_list(head, type_str);
#endif
}

int ocxl_pasid_afu_alloc(struct ocxl_fn *fn, u32 size)
{
	int max_pasid;

	if (fn->config.max_pasid_log < 0)
		return -ENOSPC;
	max_pasid = 1 << fn->config.max_pasid_log;
	return range_alloc(&fn->pasid_list, size, max_pasid, "afu pasid");
}

void ocxl_pasid_afu_free(struct ocxl_fn *fn, u32 start, u32 size)
{
	return range_free(&fn->pasid_list, start, size, "afu pasid");
}

int ocxl_actag_afu_alloc(struct ocxl_fn *fn, u32 size)
{
	int max_actag;

	max_actag = fn->actag_enabled;
	return range_alloc(&fn->actag_list, size, max_actag, "afu actag");
}

void ocxl_actag_afu_free(struct ocxl_fn *fn, u32 start, u32 size)
{
	return range_free(&fn->actag_list, start, size, "afu actag");
}
