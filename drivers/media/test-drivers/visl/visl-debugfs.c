// SPDX-License-Identifier: GPL-2.0+
/*
 * Debugfs tracing for bitstream buffers. This is similar to VA-API's
 * LIBVA_TRACE_BUFDATA in that the raw bitstream can be dumped as a debugging
 * aid.
 *
 * Produces one file per OUTPUT buffer. Files are automatically cleared on
 * STREAMOFF unless the module parameter "keep_bitstream_buffers" is set.
 */

#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <media/v4l2-mem2mem.h>

#include "visl-debugfs.h"

int visl_debugfs_init(struct visl_dev *dev)
{
	dev->debugfs_root = debugfs_create_dir("visl", NULL);
	INIT_LIST_HEAD(&dev->bitstream_blobs);
	mutex_init(&dev->bitstream_lock);

	if (IS_ERR(dev->debugfs_root))
		return PTR_ERR(dev->debugfs_root);

	return visl_debugfs_bitstream_init(dev);
}

int visl_debugfs_bitstream_init(struct visl_dev *dev)
{
	dev->bitstream_debugfs = debugfs_create_dir("bitstream",
						    dev->debugfs_root);
	if (IS_ERR(dev->bitstream_debugfs))
		return PTR_ERR(dev->bitstream_debugfs);

	return 0;
}

void visl_trace_bitstream(struct visl_ctx *ctx, struct visl_run *run)
{
	u8 *vaddr = vb2_plane_vaddr(&run->src->vb2_buf, 0);
	struct visl_blob *blob;
	size_t data_sz = vb2_get_plane_payload(&run->src->vb2_buf, 0);
	struct dentry *dentry;
	char name[32];

	blob  = kzalloc(sizeof(*blob), GFP_KERNEL);
	if (!blob)
		return;

	blob->blob.data = vzalloc(data_sz);
	if (!blob->blob.data)
		goto err_vmalloc;

	blob->blob.size = data_sz;
	snprintf(name, 32, "bitstream%d", run->src->sequence);

	memcpy(blob->blob.data, vaddr, data_sz);

	dentry = debugfs_create_blob(name, 0444, ctx->dev->bitstream_debugfs,
				     &blob->blob);
	if (IS_ERR(dentry))
		goto err_debugfs;

	blob->dentry = dentry;

	mutex_lock(&ctx->dev->bitstream_lock);
	list_add_tail(&blob->list, &ctx->dev->bitstream_blobs);
	mutex_unlock(&ctx->dev->bitstream_lock);

	return;

err_debugfs:
	vfree(blob->blob.data);
err_vmalloc:
	kfree(blob);
}

void visl_debugfs_clear_bitstream(struct visl_dev *dev)
{
	struct visl_blob *blob;
	struct visl_blob *tmp;

	mutex_lock(&dev->bitstream_lock);
	if (list_empty(&dev->bitstream_blobs))
		goto unlock;

	list_for_each_entry_safe(blob, tmp, &dev->bitstream_blobs, list) {
		list_del(&blob->list);
		debugfs_remove(blob->dentry);
		vfree(blob->blob.data);
		kfree(blob);
	}

unlock:
	mutex_unlock(&dev->bitstream_lock);
}

void visl_debugfs_bitstream_deinit(struct visl_dev *dev)
{
	visl_debugfs_clear_bitstream(dev);
	debugfs_remove_recursive(dev->bitstream_debugfs);
	dev->bitstream_debugfs = NULL;
}

void visl_debugfs_deinit(struct visl_dev *dev)
{
	visl_debugfs_bitstream_deinit(dev);
	debugfs_remove_recursive(dev->debugfs_root);
	dev->debugfs_root = NULL;
}
