/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#endif

#include "drm_netlink_gem.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <drm/drm_vma_manager.h>
#endif

#include <linux/capability.h>

#include "kernel_compatibility.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int netlink_gem_mmap_capsys(struct file *file,
				   struct vm_area_struct *vma)
{
	struct drm_file *file_priv = file->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_vma_offset_node *node;
	struct drm_gem_object *obj = NULL;
	int err;

	drm_vma_offset_lock_lookup(dev->vma_offset_manager);
	node = drm_vma_offset_exact_lookup_locked(dev->vma_offset_manager,
						  vma->vm_pgoff,
						  vma_pages(vma));
	if (node) {
		obj = container_of(node, struct drm_gem_object, vma_node);

		/* Don't mmap an object that is being destroyed */
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	drm_vma_offset_unlock_lookup(dev->vma_offset_manager);

	if (!obj)
		return -EINVAL;

	err = drm_vma_node_allow(node, file_priv);
	if (!err) {
		err = drm_gem_mmap(file, vma);

		drm_vma_node_revoke(node, file_priv);
	}

	drm_gem_object_put(obj);

	return err;
}

int netlink_gem_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err;

	err = drm_gem_mmap(file, vma);
	if (!!err && capable(CAP_SYS_RAWIO))
		err = netlink_gem_mmap_capsys(file, vma);

	return err;
}
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) */
int netlink_gem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = file->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_vma_offset_node *node;
	struct drm_gem_object *obj;
	int err;

	mutex_lock(&dev->struct_mutex);

	node = drm_vma_offset_exact_lookup(dev->vma_offset_manager,
					   vma->vm_pgoff,
					   vma_pages(vma));
	if (!node) {
		err = -EINVAL;
		goto exit_unlock;
	}

	/* Allow Netlink clients to mmap any object for reading */
	if (!capable(CAP_SYS_RAWIO) || (vma->vm_flags & VM_WRITE)) {
		if (!drm_vma_node_is_allowed(node, file_priv)) {
			err = -EACCES;
			goto exit_unlock;
		}
	}

	obj = container_of(node, struct drm_gem_object, vma_node);

	err = drm_gem_mmap_obj(obj, drm_vma_node_size(node) << PAGE_SHIFT, vma);

exit_unlock:
	mutex_unlock(&dev->struct_mutex);
	return err;
}
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) */
