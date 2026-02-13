// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/dma-buf-mapping.h>
#include <linux/pci-p2pdma.h>
#include <linux/dma-resv.h>
#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"
#include "uverbs.h"

static int uverbs_dmabuf_attach(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attachment)
{
	if (!attachment->peer2peer)
		return -EOPNOTSUPP;

	return 0;
}

static struct sg_table *
uverbs_dmabuf_map(struct dma_buf_attachment *attachment,
		  enum dma_data_direction dir)
{
	struct ib_uverbs_dmabuf_file *priv = attachment->dmabuf->priv;
	struct sg_table *ret;

	dma_resv_assert_held(priv->dmabuf->resv);

	if (priv->revoked)
		return ERR_PTR(-ENODEV);

	ret = dma_buf_phys_vec_to_sgt(attachment, priv->provider,
				      &priv->phys_vec, 1, priv->phys_vec.len,
				      dir);
	if (IS_ERR(ret))
		return ret;

	kref_get(&priv->kref);
	return ret;
}

static void uverbs_dmabuf_unmap(struct dma_buf_attachment *attachment,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	struct ib_uverbs_dmabuf_file *priv = attachment->dmabuf->priv;

	dma_resv_assert_held(priv->dmabuf->resv);
	dma_buf_free_sgt(attachment, sgt, dir);
	kref_put(&priv->kref, ib_uverbs_dmabuf_done);
}

static int uverbs_dmabuf_pin(struct dma_buf_attachment *attach)
{
	return -EOPNOTSUPP;
}

static void uverbs_dmabuf_unpin(struct dma_buf_attachment *attach)
{
}

static void uverbs_dmabuf_release(struct dma_buf *dmabuf)
{
	struct ib_uverbs_dmabuf_file *priv = dmabuf->priv;

	/*
	 * This can only happen if the fput came from alloc_abort_fd_uobject()
	 */
	if (!priv->uobj.context)
		return;

	uverbs_uobject_release(&priv->uobj);
}

static const struct dma_buf_ops uverbs_dmabuf_ops = {
	.attach = uverbs_dmabuf_attach,
	.map_dma_buf = uverbs_dmabuf_map,
	.unmap_dma_buf = uverbs_dmabuf_unmap,
	.pin = uverbs_dmabuf_pin,
	.unpin = uverbs_dmabuf_unpin,
	.release = uverbs_dmabuf_release,
};

static int UVERBS_HANDLER(UVERBS_METHOD_DMABUF_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj =
		uverbs_attr_get(attrs, UVERBS_ATTR_ALLOC_DMABUF_HANDLE)
			->obj_attr.uobject;
	struct ib_uverbs_dmabuf_file *uverbs_dmabuf =
		container_of(uobj, struct ib_uverbs_dmabuf_file, uobj);
	struct ib_device *ib_dev = attrs->context->device;
	struct rdma_user_mmap_entry *mmap_entry;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	off_t pg_off;
	int ret;

	ret = uverbs_get_const(&pg_off, attrs, UVERBS_ATTR_ALLOC_DMABUF_PGOFF);
	if (ret)
		return ret;

	mmap_entry = ib_dev->ops.pgoff_to_mmap_entry(attrs->context, pg_off);
	if (!mmap_entry)
		return -EINVAL;

	ret = ib_dev->ops.mmap_get_pfns(mmap_entry, &uverbs_dmabuf->phys_vec,
					&uverbs_dmabuf->provider);
	if (ret)
		goto err;

	exp_info.ops = &uverbs_dmabuf_ops;
	exp_info.size = uverbs_dmabuf->phys_vec.len;
	exp_info.flags = O_CLOEXEC;
	exp_info.priv = uverbs_dmabuf;

	uverbs_dmabuf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(uverbs_dmabuf->dmabuf)) {
		ret = PTR_ERR(uverbs_dmabuf->dmabuf);
		goto err;
	}

	kref_init(&uverbs_dmabuf->kref);
	init_completion(&uverbs_dmabuf->comp);
	INIT_LIST_HEAD(&uverbs_dmabuf->dmabufs_elm);
	mutex_lock(&mmap_entry->dmabufs_lock);
	if (mmap_entry->driver_removed)
		ret = -EIO;
	else
		list_add_tail(&uverbs_dmabuf->dmabufs_elm, &mmap_entry->dmabufs);
	mutex_unlock(&mmap_entry->dmabufs_lock);
	if (ret)
		goto err_revoked;

	uobj->object = uverbs_dmabuf->dmabuf->file;
	uverbs_dmabuf->mmap_entry = mmap_entry;
	uverbs_finalize_uobj_create(attrs, UVERBS_ATTR_ALLOC_DMABUF_HANDLE);
	return 0;

err_revoked:
	dma_buf_put(uverbs_dmabuf->dmabuf);
err:
	rdma_user_mmap_entry_put(mmap_entry);
	return ret;
}

DECLARE_UVERBS_NAMED_METHOD(
	UVERBS_METHOD_DMABUF_ALLOC,
	UVERBS_ATTR_FD(UVERBS_ATTR_ALLOC_DMABUF_HANDLE,
		       UVERBS_OBJECT_DMABUF,
		       UVERBS_ACCESS_NEW,
		       UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(UVERBS_ATTR_ALLOC_DMABUF_PGOFF,
			   UVERBS_ATTR_TYPE(u64),
			   UA_MANDATORY));

static void uverbs_dmabuf_fd_destroy_uobj(struct ib_uobject *uobj,
					  enum rdma_remove_reason why)
{
	struct ib_uverbs_dmabuf_file *uverbs_dmabuf =
		container_of(uobj, struct ib_uverbs_dmabuf_file, uobj);
	bool wait_for_comp = false;

	mutex_lock(&uverbs_dmabuf->mmap_entry->dmabufs_lock);
	dma_resv_lock(uverbs_dmabuf->dmabuf->resv, NULL);
	if (!uverbs_dmabuf->revoked) {
		uverbs_dmabuf->revoked = true;
		list_del(&uverbs_dmabuf->dmabufs_elm);
		dma_buf_move_notify(uverbs_dmabuf->dmabuf);
		dma_resv_wait_timeout(uverbs_dmabuf->dmabuf->resv,
				      DMA_RESV_USAGE_BOOKKEEP, false,
				      MAX_SCHEDULE_TIMEOUT);
		wait_for_comp = true;
	}
	dma_resv_unlock(uverbs_dmabuf->dmabuf->resv);
	if (wait_for_comp) {
		kref_put(&uverbs_dmabuf->kref, ib_uverbs_dmabuf_done);
		/* Let's wait till all DMA unmap are completed. */
		wait_for_completion(&uverbs_dmabuf->comp);
	}
	mutex_unlock(&uverbs_dmabuf->mmap_entry->dmabufs_lock);

	/* Matches the get done as part of pgoff_to_mmap_entry() */
	rdma_user_mmap_entry_put(uverbs_dmabuf->mmap_entry);
}

DECLARE_UVERBS_NAMED_OBJECT(
	UVERBS_OBJECT_DMABUF,
	UVERBS_TYPE_ALLOC_FD(sizeof(struct ib_uverbs_dmabuf_file),
			     uverbs_dmabuf_fd_destroy_uobj,
			     NULL, NULL, O_RDONLY),
			     &UVERBS_METHOD(UVERBS_METHOD_DMABUF_ALLOC));

const struct uapi_definition uverbs_def_obj_dmabuf[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DMABUF),
				      UAPI_DEF_OBJ_NEEDS_FN(mmap_get_pfns),
				      UAPI_DEF_OBJ_NEEDS_FN(pgoff_to_mmap_entry),
	{}
};
