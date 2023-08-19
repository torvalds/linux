// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

#include "vfio_dev.h"
#include "cmds.h"

static struct pds_vfio_lm_file *
pds_vfio_get_lm_file(const struct file_operations *fops, int flags, u64 size)
{
	struct pds_vfio_lm_file *lm_file = NULL;
	unsigned long long npages;
	struct page **pages;
	void *page_mem;
	const void *p;

	if (!size)
		return NULL;

	/* Alloc file structure */
	lm_file = kzalloc(sizeof(*lm_file), GFP_KERNEL);
	if (!lm_file)
		return NULL;

	/* Create file */
	lm_file->filep =
		anon_inode_getfile("pds_vfio_lm", fops, lm_file, flags);
	if (IS_ERR(lm_file->filep))
		goto out_free_file;

	stream_open(lm_file->filep->f_inode, lm_file->filep);
	mutex_init(&lm_file->lock);

	/* prevent file from being released before we are done with it */
	get_file(lm_file->filep);

	/* Allocate memory for file pages */
	npages = DIV_ROUND_UP_ULL(size, PAGE_SIZE);
	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		goto out_put_file;

	page_mem = kvzalloc(ALIGN(size, PAGE_SIZE), GFP_KERNEL);
	if (!page_mem)
		goto out_free_pages_array;

	p = page_mem - offset_in_page(page_mem);
	for (unsigned long long i = 0; i < npages; i++) {
		if (is_vmalloc_addr(p))
			pages[i] = vmalloc_to_page(p);
		else
			pages[i] = kmap_to_page((void *)p);
		if (!pages[i])
			goto out_free_page_mem;

		p += PAGE_SIZE;
	}

	/* Create scatterlist of file pages to use for DMA mapping later */
	if (sg_alloc_table_from_pages(&lm_file->sg_table, pages, npages, 0,
				      size, GFP_KERNEL))
		goto out_free_page_mem;

	lm_file->size = size;
	lm_file->pages = pages;
	lm_file->npages = npages;
	lm_file->page_mem = page_mem;
	lm_file->alloc_size = npages * PAGE_SIZE;

	return lm_file;

out_free_page_mem:
	kvfree(page_mem);
out_free_pages_array:
	kfree(pages);
out_put_file:
	fput(lm_file->filep);
	mutex_destroy(&lm_file->lock);
out_free_file:
	kfree(lm_file);

	return NULL;
}

static void pds_vfio_put_lm_file(struct pds_vfio_lm_file *lm_file)
{
	mutex_lock(&lm_file->lock);

	lm_file->size = 0;
	lm_file->alloc_size = 0;

	/* Free scatter list of file pages */
	sg_free_table(&lm_file->sg_table);

	kvfree(lm_file->page_mem);
	lm_file->page_mem = NULL;
	kfree(lm_file->pages);
	lm_file->pages = NULL;

	mutex_unlock(&lm_file->lock);

	/* allow file to be released since we are done with it */
	fput(lm_file->filep);
}

void pds_vfio_put_save_file(struct pds_vfio_pci_device *pds_vfio)
{
	if (!pds_vfio->save_file)
		return;

	pds_vfio_put_lm_file(pds_vfio->save_file);
	pds_vfio->save_file = NULL;
}

void pds_vfio_put_restore_file(struct pds_vfio_pci_device *pds_vfio)
{
	if (!pds_vfio->restore_file)
		return;

	pds_vfio_put_lm_file(pds_vfio->restore_file);
	pds_vfio->restore_file = NULL;
}

static struct page *pds_vfio_get_file_page(struct pds_vfio_lm_file *lm_file,
					   unsigned long offset)
{
	unsigned long cur_offset = 0;
	struct scatterlist *sg;
	unsigned int i;

	/* All accesses are sequential */
	if (offset < lm_file->last_offset || !lm_file->last_offset_sg) {
		lm_file->last_offset = 0;
		lm_file->last_offset_sg = lm_file->sg_table.sgl;
		lm_file->sg_last_entry = 0;
	}

	cur_offset = lm_file->last_offset;

	for_each_sg(lm_file->last_offset_sg, sg,
		    lm_file->sg_table.orig_nents - lm_file->sg_last_entry, i) {
		if (offset < sg->length + cur_offset) {
			lm_file->last_offset_sg = sg;
			lm_file->sg_last_entry += i;
			lm_file->last_offset = cur_offset;
			return nth_page(sg_page(sg),
					(offset - cur_offset) / PAGE_SIZE);
		}
		cur_offset += sg->length;
	}

	return NULL;
}

static int pds_vfio_release_file(struct inode *inode, struct file *filp)
{
	struct pds_vfio_lm_file *lm_file = filp->private_data;

	mutex_lock(&lm_file->lock);
	lm_file->filep->f_pos = 0;
	lm_file->size = 0;
	mutex_unlock(&lm_file->lock);
	mutex_destroy(&lm_file->lock);
	kfree(lm_file);

	return 0;
}

static ssize_t pds_vfio_save_read(struct file *filp, char __user *buf,
				  size_t len, loff_t *pos)
{
	struct pds_vfio_lm_file *lm_file = filp->private_data;
	ssize_t done = 0;

	if (pos)
		return -ESPIPE;
	pos = &filp->f_pos;

	mutex_lock(&lm_file->lock);
	if (*pos > lm_file->size) {
		done = -EINVAL;
		goto out_unlock;
	}

	len = min_t(size_t, lm_file->size - *pos, len);
	while (len) {
		size_t page_offset;
		struct page *page;
		size_t page_len;
		u8 *from_buff;
		int err;

		page_offset = (*pos) % PAGE_SIZE;
		page = pds_vfio_get_file_page(lm_file, *pos - page_offset);
		if (!page) {
			if (done == 0)
				done = -EINVAL;
			goto out_unlock;
		}

		page_len = min_t(size_t, len, PAGE_SIZE - page_offset);
		from_buff = kmap_local_page(page);
		err = copy_to_user(buf, from_buff + page_offset, page_len);
		kunmap_local(from_buff);
		if (err) {
			done = -EFAULT;
			goto out_unlock;
		}
		*pos += page_len;
		len -= page_len;
		done += page_len;
		buf += page_len;
	}

out_unlock:
	mutex_unlock(&lm_file->lock);
	return done;
}

static const struct file_operations pds_vfio_save_fops = {
	.owner = THIS_MODULE,
	.read = pds_vfio_save_read,
	.release = pds_vfio_release_file,
	.llseek = no_llseek,
};

static int pds_vfio_get_save_file(struct pds_vfio_pci_device *pds_vfio)
{
	struct device *dev = &pds_vfio->vfio_coredev.pdev->dev;
	struct pds_vfio_lm_file *lm_file;
	u64 size;
	int err;

	/* Get live migration state size in this state */
	err = pds_vfio_get_lm_state_size_cmd(pds_vfio, &size);
	if (err) {
		dev_err(dev, "failed to get save status: %pe\n", ERR_PTR(err));
		return err;
	}

	dev_dbg(dev, "save status, size = %lld\n", size);

	if (!size) {
		dev_err(dev, "invalid state size\n");
		return -EIO;
	}

	lm_file = pds_vfio_get_lm_file(&pds_vfio_save_fops, O_RDONLY, size);
	if (!lm_file) {
		dev_err(dev, "failed to create save file\n");
		return -ENOENT;
	}

	dev_dbg(dev, "size = %lld, alloc_size = %lld, npages = %lld\n",
		lm_file->size, lm_file->alloc_size, lm_file->npages);

	pds_vfio->save_file = lm_file;

	return 0;
}

static ssize_t pds_vfio_restore_write(struct file *filp, const char __user *buf,
				      size_t len, loff_t *pos)
{
	struct pds_vfio_lm_file *lm_file = filp->private_data;
	loff_t requested_length;
	ssize_t done = 0;

	if (pos)
		return -ESPIPE;

	pos = &filp->f_pos;

	if (*pos < 0 ||
	    check_add_overflow((loff_t)len, *pos, &requested_length))
		return -EINVAL;

	mutex_lock(&lm_file->lock);

	while (len) {
		size_t page_offset;
		struct page *page;
		size_t page_len;
		u8 *to_buff;
		int err;

		page_offset = (*pos) % PAGE_SIZE;
		page = pds_vfio_get_file_page(lm_file, *pos - page_offset);
		if (!page) {
			if (done == 0)
				done = -EINVAL;
			goto out_unlock;
		}

		page_len = min_t(size_t, len, PAGE_SIZE - page_offset);
		to_buff = kmap_local_page(page);
		err = copy_from_user(to_buff + page_offset, buf, page_len);
		kunmap_local(to_buff);
		if (err) {
			done = -EFAULT;
			goto out_unlock;
		}
		*pos += page_len;
		len -= page_len;
		done += page_len;
		buf += page_len;
		lm_file->size += page_len;
	}
out_unlock:
	mutex_unlock(&lm_file->lock);
	return done;
}

static const struct file_operations pds_vfio_restore_fops = {
	.owner = THIS_MODULE,
	.write = pds_vfio_restore_write,
	.release = pds_vfio_release_file,
	.llseek = no_llseek,
};

static int pds_vfio_get_restore_file(struct pds_vfio_pci_device *pds_vfio)
{
	struct device *dev = &pds_vfio->vfio_coredev.pdev->dev;
	struct pds_vfio_lm_file *lm_file;
	u64 size;

	size = sizeof(union pds_lm_dev_state);
	dev_dbg(dev, "restore status, size = %lld\n", size);

	if (!size) {
		dev_err(dev, "invalid state size");
		return -EIO;
	}

	lm_file = pds_vfio_get_lm_file(&pds_vfio_restore_fops, O_WRONLY, size);
	if (!lm_file) {
		dev_err(dev, "failed to create restore file");
		return -ENOENT;
	}
	pds_vfio->restore_file = lm_file;

	return 0;
}

struct file *
pds_vfio_step_device_state_locked(struct pds_vfio_pci_device *pds_vfio,
				  enum vfio_device_mig_state next)
{
	enum vfio_device_mig_state cur = pds_vfio->state;
	int err;

	if (cur == VFIO_DEVICE_STATE_STOP && next == VFIO_DEVICE_STATE_STOP_COPY) {
		err = pds_vfio_get_save_file(pds_vfio);
		if (err)
			return ERR_PTR(err);

		err = pds_vfio_get_lm_state_cmd(pds_vfio);
		if (err) {
			pds_vfio_put_save_file(pds_vfio);
			return ERR_PTR(err);
		}

		return pds_vfio->save_file->filep;
	}

	if (cur == VFIO_DEVICE_STATE_STOP_COPY && next == VFIO_DEVICE_STATE_STOP) {
		pds_vfio_put_save_file(pds_vfio);
		pds_vfio_dirty_disable(pds_vfio, true);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && next == VFIO_DEVICE_STATE_RESUMING) {
		err = pds_vfio_get_restore_file(pds_vfio);
		if (err)
			return ERR_PTR(err);

		return pds_vfio->restore_file->filep;
	}

	if (cur == VFIO_DEVICE_STATE_RESUMING && next == VFIO_DEVICE_STATE_STOP) {
		err = pds_vfio_set_lm_state_cmd(pds_vfio);
		if (err)
			return ERR_PTR(err);

		pds_vfio_put_restore_file(pds_vfio);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_RUNNING && next == VFIO_DEVICE_STATE_RUNNING_P2P) {
		pds_vfio_send_host_vf_lm_status_cmd(pds_vfio,
						    PDS_LM_STA_IN_PROGRESS);
		err = pds_vfio_suspend_device_cmd(pds_vfio,
						  PDS_LM_SUSPEND_RESUME_TYPE_P2P);
		if (err)
			return ERR_PTR(err);

		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_RUNNING_P2P && next == VFIO_DEVICE_STATE_RUNNING) {
		err = pds_vfio_resume_device_cmd(pds_vfio,
						 PDS_LM_SUSPEND_RESUME_TYPE_FULL);
		if (err)
			return ERR_PTR(err);

		pds_vfio_send_host_vf_lm_status_cmd(pds_vfio, PDS_LM_STA_NONE);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && next == VFIO_DEVICE_STATE_RUNNING_P2P) {
		err = pds_vfio_resume_device_cmd(pds_vfio,
						 PDS_LM_SUSPEND_RESUME_TYPE_P2P);
		if (err)
			return ERR_PTR(err);

		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_RUNNING_P2P && next == VFIO_DEVICE_STATE_STOP) {
		err = pds_vfio_suspend_device_cmd(pds_vfio,
						  PDS_LM_SUSPEND_RESUME_TYPE_FULL);
		if (err)
			return ERR_PTR(err);
		return NULL;
	}

	return ERR_PTR(-EINVAL);
}
