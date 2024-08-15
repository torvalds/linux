/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/nospec.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#include <uapi/drm/i915_drm.h>

#include "i915_user_extensions.h"
#include "i915_utils.h"

int i915_user_extensions(struct i915_user_extension __user *ext,
			 const i915_user_extension_fn *tbl,
			 unsigned int count,
			 void *data)
{
	unsigned int stackdepth = 512;

	while (ext) {
		int i, err;
		u32 name;
		u64 next;

		if (!stackdepth--) /* recursion vs useful flexibility */
			return -E2BIG;

		err = check_user_mbz(&ext->flags);
		if (err)
			return err;

		for (i = 0; i < ARRAY_SIZE(ext->rsvd); i++) {
			err = check_user_mbz(&ext->rsvd[i]);
			if (err)
				return err;
		}

		if (get_user(name, &ext->name))
			return -EFAULT;

		err = -EINVAL;
		if (name < count) {
			name = array_index_nospec(name, count);
			if (tbl[name])
				err = tbl[name](ext, data);
		}
		if (err)
			return err;

		if (get_user(next, &ext->next_extension) ||
		    overflows_type(next, uintptr_t))
			return -EFAULT;

		ext = u64_to_user_ptr(next);
	}

	return 0;
}
