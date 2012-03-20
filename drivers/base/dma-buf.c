/*
 * Framework for buffer objects that can be shared across devices/subsystems.
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Author: Sumit Semwal <sumit.semwal@ti.com>
 *
 * Many thanks to linaro-mm-sig list, and specially
 * Arnd Bergmann <arnd@arndb.de>, Rob Clark <rob@ti.com> and
 * Daniel Vetter <daniel@ffwll.ch> for their support in creation and
 * refining of this idea.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/anon_inodes.h>
#include <linux/export.h>

static inline int is_dma_buf_file(struct file *);

static int dma_buf_release(struct inode *inode, struct file *file)
{
	struct dma_buf *dmabuf;

	if (!is_dma_buf_file(file))
		return -EINVAL;

	dmabuf = file->private_data;

	dmabuf->ops->release(dmabuf);
	kfree(dmabuf);
	return 0;
}

static const struct file_operations dma_buf_fops = {
	.release	= dma_buf_release,
};

/*
 * is_dma_buf_file - Check if struct file* is associated with dma_buf
 */
static inline int is_dma_buf_file(struct file *file)
{
	return file->f_op == &dma_buf_fops;
}

/**
 * dma_buf_export - Creates a new dma_buf, and associates an anon file
 * with this buffer, so it can be exported.
 * Also connect the allocator specific data and ops to the buffer.
 *
 * @priv:	[in]	Attach private data of allocator to this buffer
 * @ops:	[in]	Attach allocator-defined dma buf ops to the new buffer.
 * @size:	[in]	Size of the buffer
 * @flags:	[in]	mode flags for the file.
 *
 * Returns, on success, a newly created dma_buf object, which wraps the
 * supplied private data and operations for dma_buf_ops. On either missing
 * ops, or error in allocating struct dma_buf, will return negative error.
 *
 */
struct dma_buf *dma_buf_export(void *priv, struct dma_buf_ops *ops,
				size_t size, int flags)
{
	struct dma_buf *dmabuf;
	struct file *file;

	if (WARN_ON(!priv || !ops
			  || !ops->map_dma_buf
			  || !ops->unmap_dma_buf
			  || !ops->release)) {
		return ERR_PTR(-EINVAL);
	}

	dmabuf = kzalloc(sizeof(struct dma_buf), GFP_KERNEL);
	if (dmabuf == NULL)
		return ERR_PTR(-ENOMEM);

	dmabuf->priv = priv;
	dmabuf->ops = ops;
	dmabuf->size = size;

	file = anon_inode_getfile("dmabuf", &dma_buf_fops, dmabuf, flags);

	dmabuf->file = file;

	mutex_init(&dmabuf->lock);
	INIT_LIST_HEAD(&dmabuf->attachments);

	return dmabuf;
}
EXPORT_SYMBOL_GPL(dma_buf_export);


/**
 * dma_buf_fd - returns a file descriptor for the given dma_buf
 * @dmabuf:	[in]	pointer to dma_buf for which fd is required.
 *
 * On success, returns an associated 'fd'. Else, returns error.
 */
int dma_buf_fd(struct dma_buf *dmabuf)
{
	int error, fd;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	error = get_unused_fd();
	if (error < 0)
		return error;
	fd = error;

	fd_install(fd, dmabuf->file);

	return fd;
}
EXPORT_SYMBOL_GPL(dma_buf_fd);

/**
 * dma_buf_get - returns the dma_buf structure related to an fd
 * @fd:	[in]	fd associated with the dma_buf to be returned
 *
 * On success, returns the dma_buf structure associated with an fd; uses
 * file's refcounting done by fget to increase refcount. returns ERR_PTR
 * otherwise.
 */
struct dma_buf *dma_buf_get(int fd)
{
	struct file *file;

	file = fget(fd);

	if (!file)
		return ERR_PTR(-EBADF);

	if (!is_dma_buf_file(file)) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file->private_data;
}
EXPORT_SYMBOL_GPL(dma_buf_get);

/**
 * dma_buf_put - decreases refcount of the buffer
 * @dmabuf:	[in]	buffer to reduce refcount of
 *
 * Uses file's refcounting done implicitly by fput()
 */
void dma_buf_put(struct dma_buf *dmabuf)
{
	if (WARN_ON(!dmabuf || !dmabuf->file))
		return;

	fput(dmabuf->file);
}
EXPORT_SYMBOL_GPL(dma_buf_put);

/**
 * dma_buf_attach - Add the device to dma_buf's attachments list; optionally,
 * calls attach() of dma_buf_ops to allow device-specific attach functionality
 * @dmabuf:	[in]	buffer to attach device to.
 * @dev:	[in]	device to be attached.
 *
 * Returns struct dma_buf_attachment * for this attachment; may return negative
 * error codes.
 *
 */
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf,
					  struct device *dev)
{
	struct dma_buf_attachment *attach;
	int ret;

	if (WARN_ON(!dmabuf || !dev || !dmabuf->ops))
		return ERR_PTR(-EINVAL);

	attach = kzalloc(sizeof(struct dma_buf_attachment), GFP_KERNEL);
	if (attach == NULL)
		goto err_alloc;

	mutex_lock(&dmabuf->lock);

	attach->dev = dev;
	attach->dmabuf = dmabuf;
	if (dmabuf->ops->attach) {
		ret = dmabuf->ops->attach(dmabuf, dev, attach);
		if (ret)
			goto err_attach;
	}
	list_add(&attach->node, &dmabuf->attachments);

	mutex_unlock(&dmabuf->lock);
	return attach;

err_alloc:
	return ERR_PTR(-ENOMEM);
err_attach:
	kfree(attach);
	mutex_unlock(&dmabuf->lock);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dma_buf_attach);

/**
 * dma_buf_detach - Remove the given attachment from dmabuf's attachments list;
 * optionally calls detach() of dma_buf_ops for device-specific detach
 * @dmabuf:	[in]	buffer to detach from.
 * @attach:	[in]	attachment to be detached; is free'd after this call.
 *
 */
void dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{
	if (WARN_ON(!dmabuf || !attach || !dmabuf->ops))
		return;

	mutex_lock(&dmabuf->lock);
	list_del(&attach->node);
	if (dmabuf->ops->detach)
		dmabuf->ops->detach(dmabuf, attach);

	mutex_unlock(&dmabuf->lock);
	kfree(attach);
}
EXPORT_SYMBOL_GPL(dma_buf_detach);

/**
 * dma_buf_map_attachment - Returns the scatterlist table of the attachment;
 * mapped into _device_ address space. Is a wrapper for map_dma_buf() of the
 * dma_buf_ops.
 * @attach:	[in]	attachment whose scatterlist is to be returned
 * @direction:	[in]	direction of DMA transfer
 *
 * Returns sg_table containing the scatterlist to be returned; may return NULL
 * or ERR_PTR.
 *
 */
struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *attach,
					enum dma_data_direction direction)
{
	struct sg_table *sg_table = ERR_PTR(-EINVAL);

	might_sleep();

	if (WARN_ON(!attach || !attach->dmabuf || !attach->dmabuf->ops))
		return ERR_PTR(-EINVAL);

	mutex_lock(&attach->dmabuf->lock);
	if (attach->dmabuf->ops->map_dma_buf)
		sg_table = attach->dmabuf->ops->map_dma_buf(attach, direction);
	mutex_unlock(&attach->dmabuf->lock);

	return sg_table;
}
EXPORT_SYMBOL_GPL(dma_buf_map_attachment);

/**
 * dma_buf_unmap_attachment - unmaps and decreases usecount of the buffer;might
 * deallocate the scatterlist associated. Is a wrapper for unmap_dma_buf() of
 * dma_buf_ops.
 * @attach:	[in]	attachment to unmap buffer from
 * @sg_table:	[in]	scatterlist info of the buffer to unmap
 *
 */
void dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
				struct sg_table *sg_table)
{
	if (WARN_ON(!attach || !attach->dmabuf || !sg_table
			    || !attach->dmabuf->ops))
		return;

	mutex_lock(&attach->dmabuf->lock);
	if (attach->dmabuf->ops->unmap_dma_buf)
		attach->dmabuf->ops->unmap_dma_buf(attach, sg_table);
	mutex_unlock(&attach->dmabuf->lock);

}
EXPORT_SYMBOL_GPL(dma_buf_unmap_attachment);
