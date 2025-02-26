// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_net.h>
#include <linux/virtio_pci_admin.h>
#include <linux/anon_inodes.h>

#include "common.h"

/* Device specification max parts size */
#define MAX_LOAD_SIZE (BIT_ULL(BITS_PER_TYPE \
	(((struct virtio_admin_cmd_dev_parts_metadata_result *)0)->parts_size.size)) - 1)

/* Initial target buffer size */
#define VIRTIOVF_TARGET_INITIAL_BUF_SIZE SZ_1M

static int
virtiovf_read_device_context_chunk(struct virtiovf_migration_file *migf,
				   u32 ctx_size);

static struct page *
virtiovf_get_migration_page(struct virtiovf_data_buffer *buf,
			    unsigned long offset)
{
	unsigned long cur_offset = 0;
	struct scatterlist *sg;
	unsigned int i;

	/* All accesses are sequential */
	if (offset < buf->last_offset || !buf->last_offset_sg) {
		buf->last_offset = 0;
		buf->last_offset_sg = buf->table.sgt.sgl;
		buf->sg_last_entry = 0;
	}

	cur_offset = buf->last_offset;

	for_each_sg(buf->last_offset_sg, sg,
		    buf->table.sgt.orig_nents - buf->sg_last_entry, i) {
		if (offset < sg->length + cur_offset) {
			buf->last_offset_sg = sg;
			buf->sg_last_entry += i;
			buf->last_offset = cur_offset;
			return nth_page(sg_page(sg),
					(offset - cur_offset) / PAGE_SIZE);
		}
		cur_offset += sg->length;
	}
	return NULL;
}

static int virtiovf_add_migration_pages(struct virtiovf_data_buffer *buf,
					unsigned int npages)
{
	unsigned int to_alloc = npages;
	struct page **page_list;
	unsigned long filled;
	unsigned int to_fill;
	int ret;
	int i;

	to_fill = min_t(unsigned int, npages, PAGE_SIZE / sizeof(*page_list));
	page_list = kvcalloc(to_fill, sizeof(*page_list), GFP_KERNEL_ACCOUNT);
	if (!page_list)
		return -ENOMEM;

	do {
		filled = alloc_pages_bulk(GFP_KERNEL_ACCOUNT, to_fill,
					  page_list);
		if (!filled) {
			ret = -ENOMEM;
			goto err;
		}
		to_alloc -= filled;
		ret = sg_alloc_append_table_from_pages(&buf->table, page_list,
			filled, 0, filled << PAGE_SHIFT, UINT_MAX,
			SG_MAX_SINGLE_ALLOC, GFP_KERNEL_ACCOUNT);

		if (ret)
			goto err_append;
		buf->allocated_length += filled * PAGE_SIZE;
		/* clean input for another bulk allocation */
		memset(page_list, 0, filled * sizeof(*page_list));
		to_fill = min_t(unsigned int, to_alloc,
				PAGE_SIZE / sizeof(*page_list));
	} while (to_alloc > 0);

	kvfree(page_list);
	return 0;

err_append:
	for (i = filled - 1; i >= 0; i--)
		__free_page(page_list[i]);
err:
	kvfree(page_list);
	return ret;
}

static void virtiovf_free_data_buffer(struct virtiovf_data_buffer *buf)
{
	struct sg_page_iter sg_iter;

	/* Undo alloc_pages_bulk() */
	for_each_sgtable_page(&buf->table.sgt, &sg_iter, 0)
		__free_page(sg_page_iter_page(&sg_iter));
	sg_free_append_table(&buf->table);
	kfree(buf);
}

static struct virtiovf_data_buffer *
virtiovf_alloc_data_buffer(struct virtiovf_migration_file *migf, size_t length)
{
	struct virtiovf_data_buffer *buf;
	int ret;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL_ACCOUNT);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	ret = virtiovf_add_migration_pages(buf,
				DIV_ROUND_UP_ULL(length, PAGE_SIZE));
	if (ret)
		goto end;

	buf->migf = migf;
	return buf;
end:
	virtiovf_free_data_buffer(buf);
	return ERR_PTR(ret);
}

static void virtiovf_put_data_buffer(struct virtiovf_data_buffer *buf)
{
	spin_lock_irq(&buf->migf->list_lock);
	list_add_tail(&buf->buf_elm, &buf->migf->avail_list);
	spin_unlock_irq(&buf->migf->list_lock);
}

static int
virtiovf_pci_alloc_obj_id(struct virtiovf_pci_core_device *virtvdev, u8 type,
			  u32 *obj_id)
{
	return virtio_pci_admin_obj_create(virtvdev->core_device.pdev,
					   VIRTIO_RESOURCE_OBJ_DEV_PARTS, type, obj_id);
}

static void
virtiovf_pci_free_obj_id(struct virtiovf_pci_core_device *virtvdev, u32 obj_id)
{
	virtio_pci_admin_obj_destroy(virtvdev->core_device.pdev,
			VIRTIO_RESOURCE_OBJ_DEV_PARTS, obj_id);
}

static struct virtiovf_data_buffer *
virtiovf_get_data_buffer(struct virtiovf_migration_file *migf, size_t length)
{
	struct virtiovf_data_buffer *buf, *temp_buf;
	struct list_head free_list;

	INIT_LIST_HEAD(&free_list);

	spin_lock_irq(&migf->list_lock);
	list_for_each_entry_safe(buf, temp_buf, &migf->avail_list, buf_elm) {
		list_del_init(&buf->buf_elm);
		if (buf->allocated_length >= length) {
			spin_unlock_irq(&migf->list_lock);
			goto found;
		}
		/*
		 * Prevent holding redundant buffers. Put in a free
		 * list and call at the end not under the spin lock
		 * (&migf->list_lock) to minimize its scope usage.
		 */
		list_add(&buf->buf_elm, &free_list);
	}
	spin_unlock_irq(&migf->list_lock);
	buf = virtiovf_alloc_data_buffer(migf, length);

found:
	while ((temp_buf = list_first_entry_or_null(&free_list,
				struct virtiovf_data_buffer, buf_elm))) {
		list_del(&temp_buf->buf_elm);
		virtiovf_free_data_buffer(temp_buf);
	}

	return buf;
}

static void virtiovf_clean_migf_resources(struct virtiovf_migration_file *migf)
{
	struct virtiovf_data_buffer *entry;

	if (migf->buf) {
		virtiovf_free_data_buffer(migf->buf);
		migf->buf = NULL;
	}

	if (migf->buf_header) {
		virtiovf_free_data_buffer(migf->buf_header);
		migf->buf_header = NULL;
	}

	list_splice(&migf->avail_list, &migf->buf_list);

	while ((entry = list_first_entry_or_null(&migf->buf_list,
				struct virtiovf_data_buffer, buf_elm))) {
		list_del(&entry->buf_elm);
		virtiovf_free_data_buffer(entry);
	}

	if (migf->has_obj_id)
		virtiovf_pci_free_obj_id(migf->virtvdev, migf->obj_id);
}

static void virtiovf_disable_fd(struct virtiovf_migration_file *migf)
{
	mutex_lock(&migf->lock);
	migf->state = VIRTIOVF_MIGF_STATE_ERROR;
	migf->filp->f_pos = 0;
	mutex_unlock(&migf->lock);
}

static void virtiovf_disable_fds(struct virtiovf_pci_core_device *virtvdev)
{
	if (virtvdev->resuming_migf) {
		virtiovf_disable_fd(virtvdev->resuming_migf);
		virtiovf_clean_migf_resources(virtvdev->resuming_migf);
		fput(virtvdev->resuming_migf->filp);
		virtvdev->resuming_migf = NULL;
	}
	if (virtvdev->saving_migf) {
		virtiovf_disable_fd(virtvdev->saving_migf);
		virtiovf_clean_migf_resources(virtvdev->saving_migf);
		fput(virtvdev->saving_migf->filp);
		virtvdev->saving_migf = NULL;
	}
}

/*
 * This function is called in all state_mutex unlock cases to
 * handle a 'deferred_reset' if exists.
 */
static void virtiovf_state_mutex_unlock(struct virtiovf_pci_core_device *virtvdev)
{
again:
	spin_lock(&virtvdev->reset_lock);
	if (virtvdev->deferred_reset) {
		virtvdev->deferred_reset = false;
		spin_unlock(&virtvdev->reset_lock);
		virtvdev->mig_state = VFIO_DEVICE_STATE_RUNNING;
		virtiovf_disable_fds(virtvdev);
		goto again;
	}
	mutex_unlock(&virtvdev->state_mutex);
	spin_unlock(&virtvdev->reset_lock);
}

void virtiovf_migration_reset_done(struct pci_dev *pdev)
{
	struct virtiovf_pci_core_device *virtvdev = dev_get_drvdata(&pdev->dev);

	if (!virtvdev->migrate_cap)
		return;

	/*
	 * As the higher VFIO layers are holding locks across reset and using
	 * those same locks with the mm_lock we need to prevent ABBA deadlock
	 * with the state_mutex and mm_lock.
	 * In case the state_mutex was taken already we defer the cleanup work
	 * to the unlock flow of the other running context.
	 */
	spin_lock(&virtvdev->reset_lock);
	virtvdev->deferred_reset = true;
	if (!mutex_trylock(&virtvdev->state_mutex)) {
		spin_unlock(&virtvdev->reset_lock);
		return;
	}
	spin_unlock(&virtvdev->reset_lock);
	virtiovf_state_mutex_unlock(virtvdev);
}

static int virtiovf_release_file(struct inode *inode, struct file *filp)
{
	struct virtiovf_migration_file *migf = filp->private_data;

	virtiovf_disable_fd(migf);
	mutex_destroy(&migf->lock);
	kfree(migf);
	return 0;
}

static struct virtiovf_data_buffer *
virtiovf_get_data_buff_from_pos(struct virtiovf_migration_file *migf,
				loff_t pos, bool *end_of_data)
{
	struct virtiovf_data_buffer *buf;
	bool found = false;

	*end_of_data = false;
	spin_lock_irq(&migf->list_lock);
	if (list_empty(&migf->buf_list)) {
		*end_of_data = true;
		goto end;
	}

	buf = list_first_entry(&migf->buf_list, struct virtiovf_data_buffer,
			       buf_elm);
	if (pos >= buf->start_pos &&
	    pos < buf->start_pos + buf->length) {
		found = true;
		goto end;
	}

	/*
	 * As we use a stream based FD we may expect having the data always
	 * on first chunk
	 */
	migf->state = VIRTIOVF_MIGF_STATE_ERROR;

end:
	spin_unlock_irq(&migf->list_lock);
	return found ? buf : NULL;
}

static ssize_t virtiovf_buf_read(struct virtiovf_data_buffer *vhca_buf,
				 char __user **buf, size_t *len, loff_t *pos)
{
	unsigned long offset;
	ssize_t done = 0;
	size_t copy_len;

	copy_len = min_t(size_t,
			 vhca_buf->start_pos + vhca_buf->length - *pos, *len);
	while (copy_len) {
		size_t page_offset;
		struct page *page;
		size_t page_len;
		u8 *from_buff;
		int ret;

		offset = *pos - vhca_buf->start_pos;
		page_offset = offset % PAGE_SIZE;
		offset -= page_offset;
		page = virtiovf_get_migration_page(vhca_buf, offset);
		if (!page)
			return -EINVAL;
		page_len = min_t(size_t, copy_len, PAGE_SIZE - page_offset);
		from_buff = kmap_local_page(page);
		ret = copy_to_user(*buf, from_buff + page_offset, page_len);
		kunmap_local(from_buff);
		if (ret)
			return -EFAULT;
		*pos += page_len;
		*len -= page_len;
		*buf += page_len;
		done += page_len;
		copy_len -= page_len;
	}

	if (*pos >= vhca_buf->start_pos + vhca_buf->length) {
		spin_lock_irq(&vhca_buf->migf->list_lock);
		list_del_init(&vhca_buf->buf_elm);
		list_add_tail(&vhca_buf->buf_elm, &vhca_buf->migf->avail_list);
		spin_unlock_irq(&vhca_buf->migf->list_lock);
	}

	return done;
}

static ssize_t virtiovf_save_read(struct file *filp, char __user *buf, size_t len,
				  loff_t *pos)
{
	struct virtiovf_migration_file *migf = filp->private_data;
	struct virtiovf_data_buffer *vhca_buf;
	bool first_loop_call = true;
	bool end_of_data;
	ssize_t done = 0;

	if (pos)
		return -ESPIPE;
	pos = &filp->f_pos;

	mutex_lock(&migf->lock);
	if (migf->state == VIRTIOVF_MIGF_STATE_ERROR) {
		done = -ENODEV;
		goto out_unlock;
	}

	while (len) {
		ssize_t count;

		vhca_buf = virtiovf_get_data_buff_from_pos(migf, *pos, &end_of_data);
		if (first_loop_call) {
			first_loop_call = false;
			/* Temporary end of file as part of PRE_COPY */
			if (end_of_data && migf->state == VIRTIOVF_MIGF_STATE_PRECOPY) {
				done = -ENOMSG;
				goto out_unlock;
			}
			if (end_of_data && migf->state != VIRTIOVF_MIGF_STATE_COMPLETE) {
				done = -EINVAL;
				goto out_unlock;
			}
		}

		if (end_of_data)
			goto out_unlock;

		if (!vhca_buf) {
			done = -EINVAL;
			goto out_unlock;
		}

		count = virtiovf_buf_read(vhca_buf, &buf, &len, pos);
		if (count < 0) {
			done = count;
			goto out_unlock;
		}
		done += count;
	}

out_unlock:
	mutex_unlock(&migf->lock);
	return done;
}

static long virtiovf_precopy_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	struct virtiovf_migration_file *migf = filp->private_data;
	struct virtiovf_pci_core_device *virtvdev = migf->virtvdev;
	struct vfio_precopy_info info = {};
	loff_t *pos = &filp->f_pos;
	bool end_of_data = false;
	unsigned long minsz;
	u32 ctx_size = 0;
	int ret;

	if (cmd != VFIO_MIG_GET_PRECOPY_INFO)
		return -ENOTTY;

	minsz = offsetofend(struct vfio_precopy_info, dirty_bytes);
	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	mutex_lock(&virtvdev->state_mutex);
	if (virtvdev->mig_state != VFIO_DEVICE_STATE_PRE_COPY &&
	    virtvdev->mig_state != VFIO_DEVICE_STATE_PRE_COPY_P2P) {
		ret = -EINVAL;
		goto err_state_unlock;
	}

	/*
	 * The virtio specification does not include a PRE_COPY concept.
	 * Since we can expect the data to remain the same for a certain period,
	 * we use a rate limiter mechanism before making a call to the device.
	 */
	if (__ratelimit(&migf->pre_copy_rl_state)) {

		ret = virtio_pci_admin_dev_parts_metadata_get(virtvdev->core_device.pdev,
					VIRTIO_RESOURCE_OBJ_DEV_PARTS, migf->obj_id,
					VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE,
					&ctx_size);
		if (ret)
			goto err_state_unlock;
	}

	mutex_lock(&migf->lock);
	if (migf->state == VIRTIOVF_MIGF_STATE_ERROR) {
		ret = -ENODEV;
		goto err_migf_unlock;
	}

	if (migf->pre_copy_initial_bytes > *pos) {
		info.initial_bytes = migf->pre_copy_initial_bytes - *pos;
	} else {
		info.dirty_bytes = migf->max_pos - *pos;
		if (!info.dirty_bytes)
			end_of_data = true;
		info.dirty_bytes += ctx_size;
	}

	if (!end_of_data || !ctx_size) {
		mutex_unlock(&migf->lock);
		goto done;
	}

	mutex_unlock(&migf->lock);
	/*
	 * We finished transferring the current state and the device has a
	 * dirty state, read a new state.
	 */
	ret = virtiovf_read_device_context_chunk(migf, ctx_size);
	if (ret)
		/*
		 * The machine is running, and context size could be grow, so no reason to mark
		 * the device state as VIRTIOVF_MIGF_STATE_ERROR.
		 */
		goto err_state_unlock;

done:
	virtiovf_state_mutex_unlock(virtvdev);
	if (copy_to_user((void __user *)arg, &info, minsz))
		return -EFAULT;
	return 0;

err_migf_unlock:
	mutex_unlock(&migf->lock);
err_state_unlock:
	virtiovf_state_mutex_unlock(virtvdev);
	return ret;
}

static const struct file_operations virtiovf_save_fops = {
	.owner = THIS_MODULE,
	.read = virtiovf_save_read,
	.unlocked_ioctl = virtiovf_precopy_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.release = virtiovf_release_file,
};

static int
virtiovf_add_buf_header(struct virtiovf_data_buffer *header_buf,
			u32 data_size)
{
	struct virtiovf_migration_file *migf = header_buf->migf;
	struct virtiovf_migration_header header = {};
	struct page *page;
	u8 *to_buff;

	header.record_size = cpu_to_le64(data_size);
	header.flags = cpu_to_le32(VIRTIOVF_MIGF_HEADER_FLAGS_TAG_MANDATORY);
	header.tag = cpu_to_le32(VIRTIOVF_MIGF_HEADER_TAG_DEVICE_DATA);
	page = virtiovf_get_migration_page(header_buf, 0);
	if (!page)
		return -EINVAL;
	to_buff = kmap_local_page(page);
	memcpy(to_buff, &header, sizeof(header));
	kunmap_local(to_buff);
	header_buf->length = sizeof(header);
	header_buf->start_pos = header_buf->migf->max_pos;
	migf->max_pos += header_buf->length;
	spin_lock_irq(&migf->list_lock);
	list_add_tail(&header_buf->buf_elm, &migf->buf_list);
	spin_unlock_irq(&migf->list_lock);
	return 0;
}

static int
virtiovf_read_device_context_chunk(struct virtiovf_migration_file *migf,
				   u32 ctx_size)
{
	struct virtiovf_data_buffer *header_buf;
	struct virtiovf_data_buffer *buf;
	bool unmark_end = false;
	struct scatterlist *sg;
	unsigned int i;
	u32 res_size;
	int nent;
	int ret;

	buf = virtiovf_get_data_buffer(migf, ctx_size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* Find the total count of SG entries which satisfies the size */
	nent = sg_nents_for_len(buf->table.sgt.sgl, ctx_size);
	if (nent <= 0) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Iterate to that SG entry and mark it as last (if it's not already)
	 * to let underlay layers iterate only till that entry.
	 */
	for_each_sg(buf->table.sgt.sgl, sg, nent - 1, i)
		;

	if (!sg_is_last(sg)) {
		unmark_end = true;
		sg_mark_end(sg);
	}

	ret = virtio_pci_admin_dev_parts_get(migf->virtvdev->core_device.pdev,
					     VIRTIO_RESOURCE_OBJ_DEV_PARTS,
					     migf->obj_id,
					     VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_ALL,
					     buf->table.sgt.sgl, &res_size);
	/* Restore the original SG mark end */
	if (unmark_end)
		sg_unmark_end(sg);
	if (ret)
		goto out;

	buf->length = res_size;
	header_buf = virtiovf_get_data_buffer(migf,
				sizeof(struct virtiovf_migration_header));
	if (IS_ERR(header_buf)) {
		ret = PTR_ERR(header_buf);
		goto out;
	}

	ret = virtiovf_add_buf_header(header_buf, res_size);
	if (ret)
		goto out_header;

	buf->start_pos = buf->migf->max_pos;
	migf->max_pos += buf->length;
	spin_lock(&migf->list_lock);
	list_add_tail(&buf->buf_elm, &migf->buf_list);
	spin_unlock_irq(&migf->list_lock);
	return 0;

out_header:
	virtiovf_put_data_buffer(header_buf);
out:
	virtiovf_put_data_buffer(buf);
	return ret;
}

static int
virtiovf_pci_save_device_final_data(struct virtiovf_pci_core_device *virtvdev)
{
	struct virtiovf_migration_file *migf = virtvdev->saving_migf;
	u32 ctx_size;
	int ret;

	if (migf->state == VIRTIOVF_MIGF_STATE_ERROR)
		return -ENODEV;

	ret = virtio_pci_admin_dev_parts_metadata_get(virtvdev->core_device.pdev,
				VIRTIO_RESOURCE_OBJ_DEV_PARTS, migf->obj_id,
				VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE,
				&ctx_size);
	if (ret)
		goto err;

	if (!ctx_size) {
		ret = -EINVAL;
		goto err;
	}

	ret = virtiovf_read_device_context_chunk(migf, ctx_size);
	if (ret)
		goto err;

	migf->state = VIRTIOVF_MIGF_STATE_COMPLETE;
	return 0;

err:
	migf->state = VIRTIOVF_MIGF_STATE_ERROR;
	return ret;
}

static struct virtiovf_migration_file *
virtiovf_pci_save_device_data(struct virtiovf_pci_core_device *virtvdev,
			      bool pre_copy)
{
	struct virtiovf_migration_file *migf;
	u32 ctx_size;
	u32 obj_id;
	int ret;

	migf = kzalloc(sizeof(*migf), GFP_KERNEL_ACCOUNT);
	if (!migf)
		return ERR_PTR(-ENOMEM);

	migf->filp = anon_inode_getfile("virtiovf_mig", &virtiovf_save_fops, migf,
					O_RDONLY);
	if (IS_ERR(migf->filp)) {
		ret = PTR_ERR(migf->filp);
		kfree(migf);
		return ERR_PTR(ret);
	}

	stream_open(migf->filp->f_inode, migf->filp);
	mutex_init(&migf->lock);
	INIT_LIST_HEAD(&migf->buf_list);
	INIT_LIST_HEAD(&migf->avail_list);
	spin_lock_init(&migf->list_lock);
	migf->virtvdev = virtvdev;

	lockdep_assert_held(&virtvdev->state_mutex);
	ret = virtiovf_pci_alloc_obj_id(virtvdev, VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_GET,
					&obj_id);
	if (ret)
		goto out;

	migf->obj_id = obj_id;
	/* Mark as having a valid obj id which can be even 0 */
	migf->has_obj_id = true;
	ret = virtio_pci_admin_dev_parts_metadata_get(virtvdev->core_device.pdev,
				VIRTIO_RESOURCE_OBJ_DEV_PARTS, obj_id,
				VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE,
				&ctx_size);
	if (ret)
		goto out_clean;

	if (!ctx_size) {
		ret = -EINVAL;
		goto out_clean;
	}

	ret = virtiovf_read_device_context_chunk(migf, ctx_size);
	if (ret)
		goto out_clean;

	if (pre_copy) {
		migf->pre_copy_initial_bytes = migf->max_pos;
		/* Arbitrarily set the pre-copy rate limit to 1-second intervals */
		ratelimit_state_init(&migf->pre_copy_rl_state, 1 * HZ, 1);
		/* Prevent any rate messages upon its usage */
		ratelimit_set_flags(&migf->pre_copy_rl_state,
				    RATELIMIT_MSG_ON_RELEASE);
		migf->state = VIRTIOVF_MIGF_STATE_PRECOPY;
	} else {
		migf->state = VIRTIOVF_MIGF_STATE_COMPLETE;
	}

	return migf;

out_clean:
	virtiovf_clean_migf_resources(migf);
out:
	fput(migf->filp);
	return ERR_PTR(ret);
}

/*
 * Set the required object header at the beginning of the buffer.
 * The actual device parts data will be written post of the header offset.
 */
static int virtiovf_set_obj_cmd_header(struct virtiovf_data_buffer *vhca_buf)
{
	struct virtio_admin_cmd_resource_obj_cmd_hdr obj_hdr = {};
	struct page *page;
	u8 *to_buff;

	obj_hdr.type = cpu_to_le16(VIRTIO_RESOURCE_OBJ_DEV_PARTS);
	obj_hdr.id = cpu_to_le32(vhca_buf->migf->obj_id);
	page = virtiovf_get_migration_page(vhca_buf, 0);
	if (!page)
		return -EINVAL;
	to_buff = kmap_local_page(page);
	memcpy(to_buff, &obj_hdr, sizeof(obj_hdr));
	kunmap_local(to_buff);

	/* Mark the buffer as including the header object data */
	vhca_buf->include_header_object = 1;
	return 0;
}

static int
virtiovf_append_page_to_mig_buf(struct virtiovf_data_buffer *vhca_buf,
				const char __user **buf, size_t *len,
				loff_t *pos, ssize_t *done)
{
	unsigned long offset;
	size_t page_offset;
	struct page *page;
	size_t page_len;
	u8 *to_buff;
	int ret;

	offset = *pos - vhca_buf->start_pos;

	if (vhca_buf->include_header_object)
		/* The buffer holds the object header, update the offset accordingly */
		offset += sizeof(struct virtio_admin_cmd_resource_obj_cmd_hdr);

	page_offset = offset % PAGE_SIZE;

	page = virtiovf_get_migration_page(vhca_buf, offset - page_offset);
	if (!page)
		return -EINVAL;

	page_len = min_t(size_t, *len, PAGE_SIZE - page_offset);
	to_buff = kmap_local_page(page);
	ret = copy_from_user(to_buff + page_offset, *buf, page_len);
	kunmap_local(to_buff);
	if (ret)
		return -EFAULT;

	*pos += page_len;
	*done += page_len;
	*buf += page_len;
	*len -= page_len;
	vhca_buf->length += page_len;
	return 0;
}

static ssize_t
virtiovf_resume_read_chunk(struct virtiovf_migration_file *migf,
			   struct virtiovf_data_buffer *vhca_buf,
			   size_t chunk_size, const char __user **buf,
			   size_t *len, loff_t *pos, ssize_t *done,
			   bool *has_work)
{
	size_t copy_len, to_copy;
	int ret;

	to_copy = min_t(size_t, *len, chunk_size - vhca_buf->length);
	copy_len = to_copy;
	while (to_copy) {
		ret = virtiovf_append_page_to_mig_buf(vhca_buf, buf, &to_copy,
						      pos, done);
		if (ret)
			return ret;
	}

	*len -= copy_len;
	if (vhca_buf->length == chunk_size) {
		migf->load_state = VIRTIOVF_LOAD_STATE_LOAD_CHUNK;
		migf->max_pos += chunk_size;
		*has_work = true;
	}

	return 0;
}

static int
virtiovf_resume_read_header_data(struct virtiovf_migration_file *migf,
				 struct virtiovf_data_buffer *vhca_buf,
				 const char __user **buf, size_t *len,
				 loff_t *pos, ssize_t *done)
{
	size_t copy_len, to_copy;
	size_t required_data;
	int ret;

	required_data = migf->record_size - vhca_buf->length;
	to_copy = min_t(size_t, *len, required_data);
	copy_len = to_copy;
	while (to_copy) {
		ret = virtiovf_append_page_to_mig_buf(vhca_buf, buf, &to_copy,
						      pos, done);
		if (ret)
			return ret;
	}

	*len -= copy_len;
	if (vhca_buf->length == migf->record_size) {
		switch (migf->record_tag) {
		default:
			/* Optional tag */
			break;
		}

		migf->load_state = VIRTIOVF_LOAD_STATE_READ_HEADER;
		migf->max_pos += migf->record_size;
		vhca_buf->length = 0;
	}

	return 0;
}

static int
virtiovf_resume_read_header(struct virtiovf_migration_file *migf,
			    struct virtiovf_data_buffer *vhca_buf,
			    const char __user **buf,
			    size_t *len, loff_t *pos,
			    ssize_t *done, bool *has_work)
{
	struct page *page;
	size_t copy_len;
	u8 *to_buff;
	int ret;

	copy_len = min_t(size_t, *len,
		sizeof(struct virtiovf_migration_header) - vhca_buf->length);
	page = virtiovf_get_migration_page(vhca_buf, 0);
	if (!page)
		return -EINVAL;
	to_buff = kmap_local_page(page);
	ret = copy_from_user(to_buff + vhca_buf->length, *buf, copy_len);
	if (ret) {
		ret = -EFAULT;
		goto end;
	}

	*buf += copy_len;
	*pos += copy_len;
	*done += copy_len;
	*len -= copy_len;
	vhca_buf->length += copy_len;
	if (vhca_buf->length == sizeof(struct virtiovf_migration_header)) {
		u64 record_size;
		u32 flags;

		record_size = le64_to_cpup((__le64 *)to_buff);
		if (record_size > MAX_LOAD_SIZE) {
			ret = -ENOMEM;
			goto end;
		}

		migf->record_size = record_size;
		flags = le32_to_cpup((__le32 *)(to_buff +
			    offsetof(struct virtiovf_migration_header, flags)));
		migf->record_tag = le32_to_cpup((__le32 *)(to_buff +
			    offsetof(struct virtiovf_migration_header, tag)));
		switch (migf->record_tag) {
		case VIRTIOVF_MIGF_HEADER_TAG_DEVICE_DATA:
			migf->load_state = VIRTIOVF_LOAD_STATE_PREP_CHUNK;
			break;
		default:
			if (!(flags & VIRTIOVF_MIGF_HEADER_FLAGS_TAG_OPTIONAL)) {
				ret = -EOPNOTSUPP;
				goto end;
			}
			/* We may read and skip this optional record data */
			migf->load_state = VIRTIOVF_LOAD_STATE_PREP_HEADER_DATA;
		}

		migf->max_pos += vhca_buf->length;
		vhca_buf->length = 0;
		*has_work = true;
	}
end:
	kunmap_local(to_buff);
	return ret;
}

static ssize_t virtiovf_resume_write(struct file *filp, const char __user *buf,
				     size_t len, loff_t *pos)
{
	struct virtiovf_migration_file *migf = filp->private_data;
	struct virtiovf_data_buffer *vhca_buf = migf->buf;
	struct virtiovf_data_buffer *vhca_buf_header = migf->buf_header;
	unsigned int orig_length;
	bool has_work = false;
	ssize_t done = 0;
	int ret = 0;

	if (pos)
		return -ESPIPE;

	pos = &filp->f_pos;
	if (*pos < vhca_buf->start_pos)
		return -EINVAL;

	mutex_lock(&migf->virtvdev->state_mutex);
	mutex_lock(&migf->lock);
	if (migf->state == VIRTIOVF_MIGF_STATE_ERROR) {
		done = -ENODEV;
		goto out_unlock;
	}

	while (len || has_work) {
		has_work = false;
		switch (migf->load_state) {
		case VIRTIOVF_LOAD_STATE_READ_HEADER:
			ret = virtiovf_resume_read_header(migf, vhca_buf_header, &buf,
							  &len, pos, &done, &has_work);
			if (ret)
				goto out_unlock;
			break;
		case VIRTIOVF_LOAD_STATE_PREP_HEADER_DATA:
			if (vhca_buf_header->allocated_length < migf->record_size) {
				virtiovf_free_data_buffer(vhca_buf_header);

				migf->buf_header = virtiovf_alloc_data_buffer(migf,
						migf->record_size);
				if (IS_ERR(migf->buf_header)) {
					ret = PTR_ERR(migf->buf_header);
					migf->buf_header = NULL;
					goto out_unlock;
				}

				vhca_buf_header = migf->buf_header;
			}

			vhca_buf_header->start_pos = migf->max_pos;
			migf->load_state = VIRTIOVF_LOAD_STATE_READ_HEADER_DATA;
			break;
		case VIRTIOVF_LOAD_STATE_READ_HEADER_DATA:
			ret = virtiovf_resume_read_header_data(migf, vhca_buf_header,
							       &buf, &len, pos, &done);
			if (ret)
				goto out_unlock;
			break;
		case VIRTIOVF_LOAD_STATE_PREP_CHUNK:
		{
			u32 cmd_size = migf->record_size +
				sizeof(struct virtio_admin_cmd_resource_obj_cmd_hdr);

			/*
			 * The DMA map/unmap is managed in virtio layer, we just need to extend
			 * the SG pages to hold the extra required chunk data.
			 */
			if (vhca_buf->allocated_length < cmd_size) {
				ret = virtiovf_add_migration_pages(vhca_buf,
					DIV_ROUND_UP_ULL(cmd_size - vhca_buf->allocated_length,
							 PAGE_SIZE));
				if (ret)
					goto out_unlock;
			}

			vhca_buf->start_pos = migf->max_pos;
			migf->load_state = VIRTIOVF_LOAD_STATE_READ_CHUNK;
			break;
		}
		case VIRTIOVF_LOAD_STATE_READ_CHUNK:
			ret = virtiovf_resume_read_chunk(migf, vhca_buf, migf->record_size,
							 &buf, &len, pos, &done, &has_work);
			if (ret)
				goto out_unlock;
			break;
		case VIRTIOVF_LOAD_STATE_LOAD_CHUNK:
			/* Mark the last SG entry and set its length */
			sg_mark_end(vhca_buf->last_offset_sg);
			orig_length = vhca_buf->last_offset_sg->length;
			/* Length should include the resource object command header */
			vhca_buf->last_offset_sg->length = vhca_buf->length +
					sizeof(struct virtio_admin_cmd_resource_obj_cmd_hdr) -
					vhca_buf->last_offset;
			ret = virtio_pci_admin_dev_parts_set(migf->virtvdev->core_device.pdev,
							     vhca_buf->table.sgt.sgl);
			/* Restore the original SG data */
			vhca_buf->last_offset_sg->length = orig_length;
			sg_unmark_end(vhca_buf->last_offset_sg);
			if (ret)
				goto out_unlock;
			migf->load_state = VIRTIOVF_LOAD_STATE_READ_HEADER;
			/* be ready for reading the next chunk */
			vhca_buf->length = 0;
			break;
		default:
			break;
		}
	}

out_unlock:
	if (ret)
		migf->state = VIRTIOVF_MIGF_STATE_ERROR;
	mutex_unlock(&migf->lock);
	virtiovf_state_mutex_unlock(migf->virtvdev);
	return ret ? ret : done;
}

static const struct file_operations virtiovf_resume_fops = {
	.owner = THIS_MODULE,
	.write = virtiovf_resume_write,
	.release = virtiovf_release_file,
};

static struct virtiovf_migration_file *
virtiovf_pci_resume_device_data(struct virtiovf_pci_core_device *virtvdev)
{
	struct virtiovf_migration_file *migf;
	struct virtiovf_data_buffer *buf;
	u32 obj_id;
	int ret;

	migf = kzalloc(sizeof(*migf), GFP_KERNEL_ACCOUNT);
	if (!migf)
		return ERR_PTR(-ENOMEM);

	migf->filp = anon_inode_getfile("virtiovf_mig", &virtiovf_resume_fops, migf,
					O_WRONLY);
	if (IS_ERR(migf->filp)) {
		ret = PTR_ERR(migf->filp);
		kfree(migf);
		return ERR_PTR(ret);
	}

	stream_open(migf->filp->f_inode, migf->filp);
	mutex_init(&migf->lock);
	INIT_LIST_HEAD(&migf->buf_list);
	INIT_LIST_HEAD(&migf->avail_list);
	spin_lock_init(&migf->list_lock);

	buf = virtiovf_alloc_data_buffer(migf, VIRTIOVF_TARGET_INITIAL_BUF_SIZE);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto out;
	}

	migf->buf = buf;

	buf = virtiovf_alloc_data_buffer(migf,
		sizeof(struct virtiovf_migration_header));
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto out_clean;
	}

	migf->buf_header = buf;
	migf->load_state = VIRTIOVF_LOAD_STATE_READ_HEADER;

	migf->virtvdev = virtvdev;
	ret = virtiovf_pci_alloc_obj_id(virtvdev, VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_SET,
					&obj_id);
	if (ret)
		goto out_clean;

	migf->obj_id = obj_id;
	/* Mark as having a valid obj id which can be even 0 */
	migf->has_obj_id = true;
	ret = virtiovf_set_obj_cmd_header(migf->buf);
	if (ret)
		goto out_clean;

	return migf;

out_clean:
	virtiovf_clean_migf_resources(migf);
out:
	fput(migf->filp);
	return ERR_PTR(ret);
}

static struct file *
virtiovf_pci_step_device_state_locked(struct virtiovf_pci_core_device *virtvdev,
				      u32 new)
{
	u32 cur = virtvdev->mig_state;
	int ret;

	if (cur == VFIO_DEVICE_STATE_RUNNING_P2P && new == VFIO_DEVICE_STATE_STOP) {
		/* NOP */
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_RUNNING_P2P) {
		/* NOP */
		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_RUNNING && new == VFIO_DEVICE_STATE_RUNNING_P2P) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY && new == VFIO_DEVICE_STATE_PRE_COPY_P2P)) {
		ret = virtio_pci_admin_mode_set(virtvdev->core_device.pdev,
						BIT(VIRTIO_ADMIN_CMD_DEV_MODE_F_STOPPED));
		if (ret)
			return ERR_PTR(ret);
		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_RUNNING_P2P && new == VFIO_DEVICE_STATE_RUNNING) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P && new == VFIO_DEVICE_STATE_PRE_COPY)) {
		ret = virtio_pci_admin_mode_set(virtvdev->core_device.pdev, 0);
		if (ret)
			return ERR_PTR(ret);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_STOP_COPY) {
		struct virtiovf_migration_file *migf;

		migf = virtiovf_pci_save_device_data(virtvdev, false);
		if (IS_ERR(migf))
			return ERR_CAST(migf);
		get_file(migf->filp);
		virtvdev->saving_migf = migf;
		return migf->filp;
	}

	if ((cur == VFIO_DEVICE_STATE_STOP_COPY && new == VFIO_DEVICE_STATE_STOP) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY && new == VFIO_DEVICE_STATE_RUNNING) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P && new == VFIO_DEVICE_STATE_RUNNING_P2P)) {
		virtiovf_disable_fds(virtvdev);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_RESUMING) {
		struct virtiovf_migration_file *migf;

		migf = virtiovf_pci_resume_device_data(virtvdev);
		if (IS_ERR(migf))
			return ERR_CAST(migf);
		get_file(migf->filp);
		virtvdev->resuming_migf = migf;
		return migf->filp;
	}

	if (cur == VFIO_DEVICE_STATE_RESUMING && new == VFIO_DEVICE_STATE_STOP) {
		virtiovf_disable_fds(virtvdev);
		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_RUNNING && new == VFIO_DEVICE_STATE_PRE_COPY) ||
	    (cur == VFIO_DEVICE_STATE_RUNNING_P2P &&
	     new == VFIO_DEVICE_STATE_PRE_COPY_P2P)) {
		struct virtiovf_migration_file *migf;

		migf = virtiovf_pci_save_device_data(virtvdev, true);
		if (IS_ERR(migf))
			return ERR_CAST(migf);
		get_file(migf->filp);
		virtvdev->saving_migf = migf;
		return migf->filp;
	}

	if (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P && new == VFIO_DEVICE_STATE_STOP_COPY) {
		ret = virtiovf_pci_save_device_final_data(virtvdev);
		return ret ? ERR_PTR(ret) : NULL;
	}

	/*
	 * vfio_mig_get_next_state() does not use arcs other than the above
	 */
	WARN_ON(true);
	return ERR_PTR(-EINVAL);
}

static struct file *
virtiovf_pci_set_device_state(struct vfio_device *vdev,
			      enum vfio_device_mig_state new_state)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(
		vdev, struct virtiovf_pci_core_device, core_device.vdev);
	enum vfio_device_mig_state next_state;
	struct file *res = NULL;
	int ret;

	mutex_lock(&virtvdev->state_mutex);
	while (new_state != virtvdev->mig_state) {
		ret = vfio_mig_get_next_state(vdev, virtvdev->mig_state,
					      new_state, &next_state);
		if (ret) {
			res = ERR_PTR(ret);
			break;
		}
		res = virtiovf_pci_step_device_state_locked(virtvdev, next_state);
		if (IS_ERR(res))
			break;
		virtvdev->mig_state = next_state;
		if (WARN_ON(res && new_state != virtvdev->mig_state)) {
			fput(res);
			res = ERR_PTR(-EINVAL);
			break;
		}
	}
	virtiovf_state_mutex_unlock(virtvdev);
	return res;
}

static int virtiovf_pci_get_device_state(struct vfio_device *vdev,
				       enum vfio_device_mig_state *curr_state)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(
		vdev, struct virtiovf_pci_core_device, core_device.vdev);

	mutex_lock(&virtvdev->state_mutex);
	*curr_state = virtvdev->mig_state;
	virtiovf_state_mutex_unlock(virtvdev);
	return 0;
}

static int virtiovf_pci_get_data_size(struct vfio_device *vdev,
				      unsigned long *stop_copy_length)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(
		vdev, struct virtiovf_pci_core_device, core_device.vdev);
	bool obj_id_exists;
	u32 res_size;
	u32 obj_id;
	int ret;

	mutex_lock(&virtvdev->state_mutex);
	obj_id_exists = virtvdev->saving_migf && virtvdev->saving_migf->has_obj_id;
	if (!obj_id_exists) {
		ret = virtiovf_pci_alloc_obj_id(virtvdev,
						VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_GET,
						&obj_id);
		if (ret)
			goto end;
	} else {
		obj_id = virtvdev->saving_migf->obj_id;
	}

	ret = virtio_pci_admin_dev_parts_metadata_get(virtvdev->core_device.pdev,
				VIRTIO_RESOURCE_OBJ_DEV_PARTS, obj_id,
				VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE,
				&res_size);
	if (!ret)
		*stop_copy_length = res_size;

	/*
	 * We can't leave this obj_id alive if didn't exist before, otherwise, it might
	 * stay alive, even without an active migration flow (e.g. migration was cancelled)
	 */
	if (!obj_id_exists)
		virtiovf_pci_free_obj_id(virtvdev, obj_id);
end:
	virtiovf_state_mutex_unlock(virtvdev);
	return ret;
}

static const struct vfio_migration_ops virtvdev_pci_mig_ops = {
	.migration_set_state = virtiovf_pci_set_device_state,
	.migration_get_state = virtiovf_pci_get_device_state,
	.migration_get_data_size = virtiovf_pci_get_data_size,
};

void virtiovf_set_migratable(struct virtiovf_pci_core_device *virtvdev)
{
	virtvdev->migrate_cap = 1;
	mutex_init(&virtvdev->state_mutex);
	spin_lock_init(&virtvdev->reset_lock);
	virtvdev->core_device.vdev.migration_flags =
		VFIO_MIGRATION_STOP_COPY |
		VFIO_MIGRATION_P2P |
		VFIO_MIGRATION_PRE_COPY;
	virtvdev->core_device.vdev.mig_ops = &virtvdev_pci_mig_ops;
}

void virtiovf_open_migration(struct virtiovf_pci_core_device *virtvdev)
{
	if (!virtvdev->migrate_cap)
		return;

	virtvdev->mig_state = VFIO_DEVICE_STATE_RUNNING;
}

void virtiovf_close_migration(struct virtiovf_pci_core_device *virtvdev)
{
	if (!virtvdev->migrate_cap)
		return;

	virtiovf_disable_fds(virtvdev);
}
