/*
 * Copyright © 2012 Red Hat
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
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *      Rob Clark <rob.clark@linaro.org>
 *
 */

#include <linux/export.h>
#include <linux/dma-buf.h>
#include <linux/rbtree.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "drm_internal.h"

/*
 * DMA-BUF/GEM Object references and lifetime overview:
 *
 * On the export the dma_buf holds a reference to the exporting GEM
 * object. It takes this reference in handle_to_fd_ioctl, when it
 * first calls .prime_export and stores the exporting GEM object in
 * the dma_buf priv. This reference needs to be released when the
 * final reference to the &dma_buf itself is dropped and its
 * &dma_buf_ops.release function is called. For GEM-based drivers,
 * the dma_buf should be exported using drm_gem_dmabuf_export() and
 * then released by drm_gem_dmabuf_release().
 *
 * On the import the importing GEM object holds a reference to the
 * dma_buf (which in turn holds a ref to the exporting GEM object).
 * It takes that reference in the fd_to_handle ioctl.
 * It calls dma_buf_get, creates an attachment to it and stores the
 * attachment in the GEM object. When this attachment is destroyed
 * when the imported object is destroyed, we remove the attachment
 * and drop the reference to the dma_buf.
 *
 * When all the references to the &dma_buf are dropped, i.e. when
 * userspace has closed both handles to the imported GEM object (through the
 * FD_TO_HANDLE IOCTL) and closed the file descriptor of the exported
 * (through the HANDLE_TO_FD IOCTL) dma_buf, and all kernel-internal references
 * are also gone, then the dma_buf gets destroyed.  This can also happen as a
 * part of the clean up procedure in the drm_release() function if userspace
 * fails to properly clean up.  Note that both the kernel and userspace (by
 * keeeping the PRIME file descriptors open) can hold references onto a
 * &dma_buf.
 *
 * Thus the chain of references always flows in one direction
 * (avoiding loops): importing_gem -> dmabuf -> exporting_gem
 *
 * Self-importing: if userspace is using PRIME as a replacement for flink
 * then it will get a fd->handle request for a GEM object that it created.
 * Drivers should detect this situation and return back the gem object
 * from the dma-buf private.  Prime will do this automatically for drivers that
 * use the drm_gem_prime_{import,export} helpers.
 *
 * GEM struct &dma_buf_ops symbols are now exported. They can be resued by
 * drivers which implement GEM interface.
 */

struct drm_prime_member {
	struct dma_buf *dma_buf;
	uint32_t handle;

	struct rb_node dmabuf_rb;
	struct rb_node handle_rb;
};

static int drm_prime_add_buf_handle(struct drm_prime_file_private *prime_fpriv,
				    struct dma_buf *dma_buf, uint32_t handle)
{
	struct drm_prime_member *member;
	struct rb_node **p, *rb;

	member = kmalloc(sizeof(*member), GFP_KERNEL);
	if (!member)
		return -ENOMEM;

	get_dma_buf(dma_buf);
	member->dma_buf = dma_buf;
	member->handle = handle;

	rb = NULL;
	p = &prime_fpriv->dmabufs.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (dma_buf > pos->dma_buf)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->dmabuf_rb, rb, p);
	rb_insert_color(&member->dmabuf_rb, &prime_fpriv->dmabufs);

	rb = NULL;
	p = &prime_fpriv->handles.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (handle > pos->handle)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->handle_rb, rb, p);
	rb_insert_color(&member->handle_rb, &prime_fpriv->handles);

	return 0;
}

static struct dma_buf *drm_prime_lookup_buf_by_handle(struct drm_prime_file_private *prime_fpriv,
						      uint32_t handle)
{
	struct rb_node *rb;

	rb = prime_fpriv->handles.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (member->handle == handle)
			return member->dma_buf;
		else if (member->handle < handle)
			rb = rb->rb_right;
		else
			rb = rb->rb_left;
	}

	return NULL;
}

static int drm_prime_lookup_buf_handle(struct drm_prime_file_private *prime_fpriv,
				       struct dma_buf *dma_buf,
				       uint32_t *handle)
{
	struct rb_node *rb;

	rb = prime_fpriv->dmabufs.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (member->dma_buf == dma_buf) {
			*handle = member->handle;
			return 0;
		} else if (member->dma_buf < dma_buf) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	return -ENOENT;
}

/**
 * drm_gem_map_attach - dma_buf attach implementation for GEM
 * @dma_buf: buffer to attach device to
 * @attach: buffer attachment data
 *
 * Calls &drm_driver.gem_prime_pin for device specific handling. This can be
 * used as the &dma_buf_ops.attach callback.
 *
 * Returns 0 on success, negative error code on failure.
 */
int drm_gem_map_attach(struct dma_buf *dma_buf,
		       struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return drm_gem_pin(obj);
}
EXPORT_SYMBOL(drm_gem_map_attach);

/**
 * drm_gem_map_detach - dma_buf detach implementation for GEM
 * @dma_buf: buffer to detach from
 * @attach: attachment to be detached
 *
 * Cleans up &dma_buf_attachment. This can be used as the &dma_buf_ops.detach
 * callback.
 */
void drm_gem_map_detach(struct dma_buf *dma_buf,
			struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;

	drm_gem_unpin(obj);
}
EXPORT_SYMBOL(drm_gem_map_detach);

void drm_prime_remove_buf_handle_locked(struct drm_prime_file_private *prime_fpriv,
					struct dma_buf *dma_buf)
{
	struct rb_node *rb;

	rb = prime_fpriv->dmabufs.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (member->dma_buf == dma_buf) {
			rb_erase(&member->handle_rb, &prime_fpriv->handles);
			rb_erase(&member->dmabuf_rb, &prime_fpriv->dmabufs);

			dma_buf_put(dma_buf);
			kfree(member);
			return;
		} else if (member->dma_buf < dma_buf) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}
}

/**
 * drm_gem_map_dma_buf - map_dma_buf implementation for GEM
 * @attach: attachment whose scatterlist is to be returned
 * @dir: direction of DMA transfer
 *
 * Calls &drm_driver.gem_prime_get_sg_table and then maps the scatterlist. This
 * can be used as the &dma_buf_ops.map_dma_buf callback.
 *
 * Returns sg_table containing the scatterlist to be returned; returns ERR_PTR
 * on error. May return -EINTR if it is interrupted by a signal.
 */

struct sg_table *drm_gem_map_dma_buf(struct dma_buf_attachment *attach,
				     enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct sg_table *sgt;

	if (WARN_ON(dir == DMA_NONE))
		return ERR_PTR(-EINVAL);

	if (obj->funcs)
		sgt = obj->funcs->get_sg_table(obj);
	else
		sgt = obj->dev->driver->gem_prime_get_sg_table(obj);

	if (!dma_map_sg_attrs(attach->dev, sgt->sgl, sgt->nents, dir,
			      DMA_ATTR_SKIP_CPU_SYNC)) {
		sg_free_table(sgt);
		kfree(sgt);
		sgt = ERR_PTR(-ENOMEM);
	}

	return sgt;
}
EXPORT_SYMBOL(drm_gem_map_dma_buf);

/**
 * drm_gem_unmap_dma_buf - unmap_dma_buf implementation for GEM
 * @attach: attachment to unmap buffer from
 * @sgt: scatterlist info of the buffer to unmap
 * @dir: direction of DMA transfer
 *
 * This can be used as the &dma_buf_ops.unmap_dma_buf callback.
 */
void drm_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
			   struct sg_table *sgt,
			   enum dma_data_direction dir)
{
	if (!sgt)
		return;

	dma_unmap_sg_attrs(attach->dev, sgt->sgl, sgt->nents, dir,
			   DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}
EXPORT_SYMBOL(drm_gem_unmap_dma_buf);

/**
 * drm_gem_dmabuf_export - dma_buf export implementation for GEM
 * @dev: parent device for the exported dmabuf
 * @exp_info: the export information used by dma_buf_export()
 *
 * This wraps dma_buf_export() for use by generic GEM drivers that are using
 * drm_gem_dmabuf_release(). In addition to calling dma_buf_export(), we take
 * a reference to the &drm_device and the exported &drm_gem_object (stored in
 * &dma_buf_export_info.priv) which is released by drm_gem_dmabuf_release().
 *
 * Returns the new dmabuf.
 */
struct dma_buf *drm_gem_dmabuf_export(struct drm_device *dev,
				      struct dma_buf_export_info *exp_info)
{
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_export(exp_info);
	if (IS_ERR(dma_buf))
		return dma_buf;

	drm_dev_get(dev);
	drm_gem_object_get(exp_info->priv);

	return dma_buf;
}
EXPORT_SYMBOL(drm_gem_dmabuf_export);

/**
 * drm_gem_dmabuf_release - dma_buf release implementation for GEM
 * @dma_buf: buffer to be released
 *
 * Generic release function for dma_bufs exported as PRIME buffers. GEM drivers
 * must use this in their dma_buf ops structure as the release callback.
 * drm_gem_dmabuf_release() should be used in conjunction with
 * drm_gem_dmabuf_export().
 */
void drm_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->dev;

	/* drop the reference on the export fd holds */
	drm_gem_object_put_unlocked(obj);

	drm_dev_put(dev);
}
EXPORT_SYMBOL(drm_gem_dmabuf_release);

/**
 * drm_gem_dmabuf_vmap - dma_buf vmap implementation for GEM
 * @dma_buf: buffer to be mapped
 *
 * Sets up a kernel virtual mapping. This can be used as the &dma_buf_ops.vmap
 * callback.
 *
 * Returns the kernel virtual address.
 */
void *drm_gem_dmabuf_vmap(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;
	void *vaddr;

	vaddr = drm_gem_vmap(obj);
	if (IS_ERR(vaddr))
		vaddr = NULL;

	return vaddr;
}
EXPORT_SYMBOL(drm_gem_dmabuf_vmap);

/**
 * drm_gem_dmabuf_vunmap - dma_buf vunmap implementation for GEM
 * @dma_buf: buffer to be unmapped
 * @vaddr: the virtual address of the buffer
 *
 * Releases a kernel virtual mapping. This can be used as the
 * &dma_buf_ops.vunmap callback.
 */
void drm_gem_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct drm_gem_object *obj = dma_buf->priv;

	drm_gem_vunmap(obj, vaddr);
}
EXPORT_SYMBOL(drm_gem_dmabuf_vunmap);

/**
 * drm_gem_dmabuf_mmap - dma_buf mmap implementation for GEM
 * @dma_buf: buffer to be mapped
 * @vma: virtual address range
 *
 * Provides memory mapping for the buffer. This can be used as the
 * &dma_buf_ops.mmap callback.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->dev;

	if (!dev->driver->gem_prime_mmap)
		return -ENOSYS;

	return dev->driver->gem_prime_mmap(obj, vma);
}
EXPORT_SYMBOL(drm_gem_dmabuf_mmap);

static const struct dma_buf_ops drm_gem_prime_dmabuf_ops =  {
	.cache_sgt_mapping = true,
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

/**
 * DOC: PRIME Helpers
 *
 * Drivers can implement @gem_prime_export and @gem_prime_import in terms of
 * simpler APIs by using the helper functions @drm_gem_prime_export and
 * @drm_gem_prime_import.  These functions implement dma-buf support in terms of
 * six lower-level driver callbacks:
 *
 * Export callbacks:
 *
 *  * @gem_prime_pin (optional): prepare a GEM object for exporting
 *  * @gem_prime_get_sg_table: provide a scatter/gather table of pinned pages
 *  * @gem_prime_vmap: vmap a buffer exported by your driver
 *  * @gem_prime_vunmap: vunmap a buffer exported by your driver
 *  * @gem_prime_mmap (optional): mmap a buffer exported by your driver
 *
 * Import callback:
 *
 *  * @gem_prime_import_sg_table (import): produce a GEM object from another
 *    driver's scatter/gather table
 */

/**
 * drm_gem_prime_export - helper library implementation of the export callback
 * @dev: drm_device to export from
 * @obj: GEM object to export
 * @flags: flags like DRM_CLOEXEC and DRM_RDWR
 *
 * This is the implementation of the gem_prime_export functions for GEM drivers
 * using the PRIME helpers.
 */
struct dma_buf *drm_gem_prime_export(struct drm_device *dev,
				     struct drm_gem_object *obj,
				     int flags)
{
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		.ops = &drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	if (dev->driver->gem_prime_res_obj)
		exp_info.resv = dev->driver->gem_prime_res_obj(obj);

	return drm_gem_dmabuf_export(dev, &exp_info);
}
EXPORT_SYMBOL(drm_gem_prime_export);

static struct dma_buf *export_and_register_object(struct drm_device *dev,
						  struct drm_gem_object *obj,
						  uint32_t flags)
{
	struct dma_buf *dmabuf;

	/* prevent races with concurrent gem_close. */
	if (obj->handle_count == 0) {
		dmabuf = ERR_PTR(-ENOENT);
		return dmabuf;
	}

	if (obj->funcs && obj->funcs->export)
		dmabuf = obj->funcs->export(obj, flags);
	else if (dev->driver->gem_prime_export)
		dmabuf = dev->driver->gem_prime_export(dev, obj, flags);
	else
		dmabuf = drm_gem_prime_export(dev, obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		return dmabuf;
	}

	/*
	 * Note that callers do not need to clean up the export cache
	 * since the check for obj->handle_count guarantees that someone
	 * will clean it up.
	 */
	obj->dma_buf = dmabuf;
	get_dma_buf(obj->dma_buf);

	return dmabuf;
}

/**
 * drm_gem_prime_handle_to_fd - PRIME export function for GEM drivers
 * @dev: dev to export the buffer from
 * @file_priv: drm file-private structure
 * @handle: buffer handle to export
 * @flags: flags like DRM_CLOEXEC
 * @prime_fd: pointer to storage for the fd id of the create dma-buf
 *
 * This is the PRIME export function which must be used mandatorily by GEM
 * drivers to ensure correct lifetime management of the underlying GEM object.
 * The actual exporting from GEM object to a dma-buf is done through the
 * gem_prime_export driver callback.
 */
int drm_gem_prime_handle_to_fd(struct drm_device *dev,
			       struct drm_file *file_priv, uint32_t handle,
			       uint32_t flags,
			       int *prime_fd)
{
	struct drm_gem_object *obj;
	int ret = 0;
	struct dma_buf *dmabuf;

	mutex_lock(&file_priv->prime.lock);
	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj)  {
		ret = -ENOENT;
		goto out_unlock;
	}

	dmabuf = drm_prime_lookup_buf_by_handle(&file_priv->prime, handle);
	if (dmabuf) {
		get_dma_buf(dmabuf);
		goto out_have_handle;
	}

	mutex_lock(&dev->object_name_lock);
	/* re-export the original imported object */
	if (obj->import_attach) {
		dmabuf = obj->import_attach->dmabuf;
		get_dma_buf(dmabuf);
		goto out_have_obj;
	}

	if (obj->dma_buf) {
		get_dma_buf(obj->dma_buf);
		dmabuf = obj->dma_buf;
		goto out_have_obj;
	}

	dmabuf = export_and_register_object(dev, obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		ret = PTR_ERR(dmabuf);
		mutex_unlock(&dev->object_name_lock);
		goto out;
	}

out_have_obj:
	/*
	 * If we've exported this buffer then cheat and add it to the import list
	 * so we get the correct handle back. We must do this under the
	 * protection of dev->object_name_lock to ensure that a racing gem close
	 * ioctl doesn't miss to remove this buffer handle from the cache.
	 */
	ret = drm_prime_add_buf_handle(&file_priv->prime,
				       dmabuf, handle);
	mutex_unlock(&dev->object_name_lock);
	if (ret)
		goto fail_put_dmabuf;

out_have_handle:
	ret = dma_buf_fd(dmabuf, flags);
	/*
	 * We must _not_ remove the buffer from the handle cache since the newly
	 * created dma buf is already linked in the global obj->dma_buf pointer,
	 * and that is invariant as long as a userspace gem handle exists.
	 * Closing the handle will clean out the cache anyway, so we don't leak.
	 */
	if (ret < 0) {
		goto fail_put_dmabuf;
	} else {
		*prime_fd = ret;
		ret = 0;
	}

	goto out;

fail_put_dmabuf:
	dma_buf_put(dmabuf);
out:
	drm_gem_object_put_unlocked(obj);
out_unlock:
	mutex_unlock(&file_priv->prime.lock);

	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_handle_to_fd);

/**
 * drm_gem_prime_mmap - PRIME mmap function for GEM drivers
 * @obj: GEM object
 * @vma: Virtual address range
 *
 * This function sets up a userspace mapping for PRIME exported buffers using
 * the same codepath that is used for regular GEM buffer mapping on the DRM fd.
 * The fake GEM offset is added to vma->vm_pgoff and &drm_driver->fops->mmap is
 * called to set up the mapping.
 *
 * Drivers can use this as their &drm_driver.gem_prime_mmap callback.
 */
int drm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct drm_file *priv;
	struct file *fil;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	fil = kzalloc(sizeof(*fil), GFP_KERNEL);
	if (!priv || !fil) {
		ret = -ENOMEM;
		goto out;
	}

	/* Used by drm_gem_mmap() to lookup the GEM object */
	priv->minor = obj->dev->primary;
	fil->private_data = priv;

	ret = drm_vma_node_allow(&obj->vma_node, priv);
	if (ret)
		goto out;

	vma->vm_pgoff += drm_vma_node_start(&obj->vma_node);

	ret = obj->dev->driver->fops->mmap(fil, vma);

	drm_vma_node_revoke(&obj->vma_node, priv);
out:
	kfree(priv);
	kfree(fil);

	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_mmap);

/**
 * drm_gem_prime_import_dev - core implementation of the import callback
 * @dev: drm_device to import into
 * @dma_buf: dma-buf object to import
 * @attach_dev: struct device to dma_buf attach
 *
 * This is the core of drm_gem_prime_import. It's designed to be called by
 * drivers who want to use a different device structure than dev->dev for
 * attaching via dma_buf.
 */
struct drm_gem_object *drm_gem_prime_import_dev(struct drm_device *dev,
					    struct dma_buf *dma_buf,
					    struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct drm_gem_object *obj;
	int ret;

	if (dma_buf->ops == &drm_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);

	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = dev->driver->gem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_gem_prime_import_dev);

/**
 * drm_gem_prime_import - helper library implementation of the import callback
 * @dev: drm_device to import into
 * @dma_buf: dma-buf object to import
 *
 * This is the implementation of the gem_prime_import functions for GEM drivers
 * using the PRIME helpers.
 */
struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}
EXPORT_SYMBOL(drm_gem_prime_import);

/**
 * drm_gem_prime_fd_to_handle - PRIME import function for GEM drivers
 * @dev: dev to export the buffer from
 * @file_priv: drm file-private structure
 * @prime_fd: fd id of the dma-buf which should be imported
 * @handle: pointer to storage for the handle of the imported buffer object
 *
 * This is the PRIME import function which must be used mandatorily by GEM
 * drivers to ensure correct lifetime management of the underlying GEM object.
 * The actual importing of GEM object from the dma-buf is done through the
 * gem_import_export driver callback.
 */
int drm_gem_prime_fd_to_handle(struct drm_device *dev,
			       struct drm_file *file_priv, int prime_fd,
			       uint32_t *handle)
{
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	int ret;

	dma_buf = dma_buf_get(prime_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	mutex_lock(&file_priv->prime.lock);

	ret = drm_prime_lookup_buf_handle(&file_priv->prime,
			dma_buf, handle);
	if (ret == 0)
		goto out_put;

	/* never seen this one, need to import */
	mutex_lock(&dev->object_name_lock);
	if (dev->driver->gem_prime_import)
		obj = dev->driver->gem_prime_import(dev, dma_buf);
	else
		obj = drm_gem_prime_import(dev, dma_buf);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto out_unlock;
	}

	if (obj->dma_buf) {
		WARN_ON(obj->dma_buf != dma_buf);
	} else {
		obj->dma_buf = dma_buf;
		get_dma_buf(dma_buf);
	}

	/* _handle_create_tail unconditionally unlocks dev->object_name_lock. */
	ret = drm_gem_handle_create_tail(file_priv, obj, handle);
	drm_gem_object_put_unlocked(obj);
	if (ret)
		goto out_put;

	ret = drm_prime_add_buf_handle(&file_priv->prime,
			dma_buf, *handle);
	mutex_unlock(&file_priv->prime.lock);
	if (ret)
		goto fail;

	dma_buf_put(dma_buf);

	return 0;

fail:
	/* hmm, if driver attached, we are relying on the free-object path
	 * to detach.. which seems ok..
	 */
	drm_gem_handle_delete(file_priv, *handle);
	dma_buf_put(dma_buf);
	return ret;

out_unlock:
	mutex_unlock(&dev->object_name_lock);
out_put:
	mutex_unlock(&file_priv->prime.lock);
	dma_buf_put(dma_buf);
	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_fd_to_handle);

int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EOPNOTSUPP;

	if (!dev->driver->prime_handle_to_fd)
		return -ENOSYS;

	/* check flags are valid */
	if (args->flags & ~(DRM_CLOEXEC | DRM_RDWR))
		return -EINVAL;

	return dev->driver->prime_handle_to_fd(dev, file_priv,
			args->handle, args->flags, &args->fd);
}

int drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EOPNOTSUPP;

	if (!dev->driver->prime_fd_to_handle)
		return -ENOSYS;

	return dev->driver->prime_fd_to_handle(dev, file_priv,
			args->fd, &args->handle);
}

/**
 * drm_prime_pages_to_sg - converts a page array into an sg list
 * @pages: pointer to the array of page pointers to convert
 * @nr_pages: length of the page vector
 *
 * This helper creates an sg table object from a set of pages
 * the driver is responsible for mapping the pages into the
 * importers address space for use with dma_buf itself.
 */
struct sg_table *drm_prime_pages_to_sg(struct page **pages, unsigned int nr_pages)
{
	struct sg_table *sg = NULL;
	int ret;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sg_alloc_table_from_pages(sg, pages, nr_pages, 0,
				nr_pages << PAGE_SHIFT, GFP_KERNEL);
	if (ret)
		goto out;

	return sg;
out:
	kfree(sg);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_prime_pages_to_sg);

/**
 * drm_prime_sg_to_page_addr_arrays - convert an sg table into a page array
 * @sgt: scatter-gather table to convert
 * @pages: optional array of page pointers to store the page array in
 * @addrs: optional array to store the dma bus address of each page
 * @max_entries: size of both the passed-in arrays
 *
 * Exports an sg table into an array of pages and addresses. This is currently
 * required by the TTM driver in order to do correct fault handling.
 */
int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_entries)
{
	unsigned count;
	struct scatterlist *sg;
	struct page *page;
	u32 len, index;
	dma_addr_t addr;

	index = 0;
	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		len = sg->length;
		page = sg_page(sg);
		addr = sg_dma_address(sg);

		while (len > 0) {
			if (WARN_ON(index >= max_entries))
				return -1;
			if (pages)
				pages[index] = page;
			if (addrs)
				addrs[index] = addr;

			page++;
			addr += PAGE_SIZE;
			len -= PAGE_SIZE;
			index++;
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_prime_sg_to_page_addr_arrays);

/**
 * drm_prime_gem_destroy - helper to clean up a PRIME-imported GEM object
 * @obj: GEM object which was created from a dma-buf
 * @sg: the sg-table which was pinned at import time
 *
 * This is the cleanup functions which GEM drivers need to call when they use
 * @drm_gem_prime_import to import dma-bufs.
 */
void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg)
{
	struct dma_buf_attachment *attach;
	struct dma_buf *dma_buf;
	attach = obj->import_attach;
	if (sg)
		dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
	dma_buf = attach->dmabuf;
	dma_buf_detach(attach->dmabuf, attach);
	/* remove the reference */
	dma_buf_put(dma_buf);
}
EXPORT_SYMBOL(drm_prime_gem_destroy);

void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv)
{
	mutex_init(&prime_fpriv->lock);
	prime_fpriv->dmabufs = RB_ROOT;
	prime_fpriv->handles = RB_ROOT;
}

void drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv)
{
	/* by now drm_gem_release should've made sure the list is empty */
	WARN_ON(!RB_EMPTY_ROOT(&prime_fpriv->dmabufs));
}
