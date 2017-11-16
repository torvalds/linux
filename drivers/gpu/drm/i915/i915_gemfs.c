/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>

#include "i915_drv.h"
#include "i915_gemfs.h"

int i915_gemfs_init(struct drm_i915_private *i915)
{
	struct file_system_type *type;
	struct vfsmount *gemfs;

	type = get_fs_type("tmpfs");
	if (!type)
		return -ENODEV;

	gemfs = kern_mount(type);
	if (IS_ERR(gemfs))
		return PTR_ERR(gemfs);

	/*
	 * Enable huge-pages for objects that are at least HPAGE_PMD_SIZE, most
	 * likely 2M. Note that within_size may overallocate huge-pages, if say
	 * we allocate an object of size 2M + 4K, we may get 2M + 2M, but under
	 * memory pressure shmem should split any huge-pages which can be
	 * shrunk.
	 */

	if (has_transparent_hugepage()) {
		struct super_block *sb = gemfs->mnt_sb;
		char options[] = "huge=within_size";
		int flags = 0;
		int err;

		err = sb->s_op->remount_fs(sb, &flags, options);
		if (err) {
			kern_unmount(gemfs);
			return err;
		}
	}

	i915->mm.gemfs = gemfs;

	return 0;
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
