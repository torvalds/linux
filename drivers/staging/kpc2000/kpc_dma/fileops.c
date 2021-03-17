// SPDX-License-Identifier: GPL-2.0+
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>     /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/cdev.h>
#include <linux/uaccess.h>  /* copy_*_user */
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include "kpc_dma_driver.h"
#include "uapi.h"

/**********  Helper Functions  **********/
static inline
unsigned int  count_pages(unsigned long iov_base, size_t iov_len)
{
	unsigned long first = (iov_base                 & PAGE_MASK) >> PAGE_SHIFT;
	unsigned long last  = ((iov_base + iov_len - 1) & PAGE_MASK) >> PAGE_SHIFT;

	return last - first + 1;
}

static inline
unsigned int  count_parts_for_sge(struct scatterlist *sg)
{
	return DIV_ROUND_UP(sg_dma_len(sg), 0x80000);
}

/**********  Transfer Helpers  **********/
static int kpc_dma_transfer(struct dev_private_data *priv,
			    unsigned long iov_base, size_t iov_len)
{
	unsigned int i = 0;
	int rv = 0, nr_pages = 0;
	struct kpc_dma_device *ldev;
	struct aio_cb_data *acd;
	DECLARE_COMPLETION_ONSTACK(done);
	u32 desc_needed = 0;
	struct scatterlist *sg;
	u32 num_descrs_avail;
	struct kpc_dma_descriptor *desc;
	unsigned int pcnt;
	unsigned int p;
	u64 card_addr;
	u64 dma_addr;
	u64 user_ctl;

	ldev = priv->ldev;

	acd = kzalloc(sizeof(*acd), GFP_KERNEL);
	if (!acd) {
		dev_err(&priv->ldev->pldev->dev, "Couldn't kmalloc space for the aio data\n");
		return -ENOMEM;
	}
	memset(acd, 0x66, sizeof(struct aio_cb_data));

	acd->priv = priv;
	acd->ldev = priv->ldev;
	acd->cpl = &done;
	acd->flags = 0;
	acd->len = iov_len;
	acd->page_count = count_pages(iov_base, iov_len);

	// Allocate an array of page pointers
	acd->user_pages = kcalloc(acd->page_count, sizeof(struct page *),
				  GFP_KERNEL);
	if (!acd->user_pages) {
		dev_err(&priv->ldev->pldev->dev, "Couldn't kmalloc space for the page pointers\n");
		rv = -ENOMEM;
		goto err_alloc_userpages;
	}

	// Lock the user buffer pages in memory, and hold on to the page pointers (for the sglist)
	mmap_read_lock(current->mm);      /*  get memory map semaphore */
	rv = pin_user_pages(iov_base, acd->page_count, FOLL_TOUCH | FOLL_WRITE, acd->user_pages, NULL);
	mmap_read_unlock(current->mm);        /*  release the semaphore */
	if (rv != acd->page_count) {
		nr_pages = rv;
		if (rv > 0)
			rv = -EFAULT;

		dev_err(&priv->ldev->pldev->dev, "Couldn't pin_user_pages (%d)\n", rv);
		goto unpin_pages;
	}
	nr_pages = acd->page_count;

	// Allocate and setup the sg_table (scatterlist entries)
	rv = sg_alloc_table_from_pages(&acd->sgt, acd->user_pages, acd->page_count, iov_base & (PAGE_SIZE - 1), iov_len, GFP_KERNEL);
	if (rv) {
		dev_err(&priv->ldev->pldev->dev, "Couldn't alloc sg_table (%d)\n", rv);
		goto unpin_pages;
	}

	// Setup the DMA mapping for all the sg entries
	acd->mapped_entry_count = dma_map_sg(&ldev->pldev->dev, acd->sgt.sgl, acd->sgt.nents, ldev->dir);
	if (acd->mapped_entry_count <= 0) {
		dev_err(&priv->ldev->pldev->dev, "Couldn't dma_map_sg (%d)\n", acd->mapped_entry_count);
		goto free_table;
	}

	// Calculate how many descriptors are actually needed for this transfer.
	for_each_sg(acd->sgt.sgl, sg, acd->mapped_entry_count, i) {
		desc_needed += count_parts_for_sge(sg);
	}

	lock_engine(ldev);

	// Figoure out how many descriptors are available and return an error if there aren't enough
	num_descrs_avail = count_descriptors_available(ldev);
	dev_dbg(&priv->ldev->pldev->dev, "    mapped_entry_count = %d    num_descrs_needed = %d    num_descrs_avail = %d\n", acd->mapped_entry_count, desc_needed, num_descrs_avail);
	if (desc_needed >= ldev->desc_pool_cnt) {
		dev_warn(&priv->ldev->pldev->dev, "    mapped_entry_count = %d    num_descrs_needed = %d    num_descrs_avail = %d    TOO MANY to ever complete!\n", acd->mapped_entry_count, desc_needed, num_descrs_avail);
		rv = -EAGAIN;
		goto err_descr_too_many;
	}
	if (desc_needed > num_descrs_avail) {
		dev_warn(&priv->ldev->pldev->dev, "    mapped_entry_count = %d    num_descrs_needed = %d    num_descrs_avail = %d    Too many to complete right now.\n", acd->mapped_entry_count, desc_needed, num_descrs_avail);
		rv = -EMSGSIZE;
		goto err_descr_too_many;
	}

	// Loop through all the sg table entries and fill out a descriptor for each one.
	desc = ldev->desc_next;
	card_addr = acd->priv->card_addr;
	for_each_sg(acd->sgt.sgl, sg, acd->mapped_entry_count, i) {
		pcnt = count_parts_for_sge(sg);
		for (p = 0 ; p < pcnt ; p++) {
			// Fill out the descriptor
			BUG_ON(!desc);
			clear_desc(desc);
			if (p != pcnt - 1)
				desc->DescByteCount = 0x80000;
			else
				desc->DescByteCount = sg_dma_len(sg) - (p * 0x80000);

			desc->DescBufferByteCount = desc->DescByteCount;

			desc->DescControlFlags |= DMA_DESC_CTL_IRQONERR;
			if (i == 0 && p == 0)
				desc->DescControlFlags |= DMA_DESC_CTL_SOP;
			if (i == acd->mapped_entry_count - 1 && p == pcnt - 1)
				desc->DescControlFlags |= DMA_DESC_CTL_EOP | DMA_DESC_CTL_IRQONDONE;

			desc->DescCardAddrLS = (card_addr & 0xFFFFFFFF);
			desc->DescCardAddrMS = (card_addr >> 32) & 0xF;
			card_addr += desc->DescByteCount;

			dma_addr  = sg_dma_address(sg) + (p * 0x80000);
			desc->DescSystemAddrLS = (dma_addr & 0x00000000FFFFFFFFUL) >>  0;
			desc->DescSystemAddrMS = (dma_addr & 0xFFFFFFFF00000000UL) >> 32;

			user_ctl = acd->priv->user_ctl;
			if (i == acd->mapped_entry_count - 1 && p == pcnt - 1)
				user_ctl = acd->priv->user_ctl_last;

			desc->DescUserControlLS = (user_ctl & 0x00000000FFFFFFFFUL) >>  0;
			desc->DescUserControlMS = (user_ctl & 0xFFFFFFFF00000000UL) >> 32;

			if (i == acd->mapped_entry_count - 1 && p == pcnt - 1)
				desc->acd = acd;

			dev_dbg(&priv->ldev->pldev->dev, "  Filled descriptor %p (acd = %p)\n", desc, desc->acd);

			ldev->desc_next = desc->Next;
			desc = desc->Next;
		}
	}

	// Send the filled descriptors off to the hardware to process!
	SetEngineSWPtr(ldev, ldev->desc_next);

	unlock_engine(ldev);

	rv = wait_for_completion_interruptible(&done);
	/*
	 * If the user aborted (rv == -ERESTARTSYS), we're no longer responsible
	 * for cleaning up the acd
	 */
	if (rv == -ERESTARTSYS)
		acd->cpl = NULL;
	if (rv == 0) {
		rv = acd->len;
		kfree(acd);
	}
	return rv;

 err_descr_too_many:
	unlock_engine(ldev);
	dma_unmap_sg(&ldev->pldev->dev, acd->sgt.sgl, acd->sgt.nents, ldev->dir);
 free_table:
	sg_free_table(&acd->sgt);

 unpin_pages:
	if (nr_pages > 0)
		unpin_user_pages(acd->user_pages, nr_pages);
	kfree(acd->user_pages);
 err_alloc_userpages:
	kfree(acd);
	dev_dbg(&priv->ldev->pldev->dev, "%s returning with error %d\n", __func__, rv);
	return rv;
}

void  transfer_complete_cb(struct aio_cb_data *acd, size_t xfr_count, u32 flags)
{
	unsigned int i;

	BUG_ON(!acd);
	BUG_ON(!acd->user_pages);
	BUG_ON(!acd->sgt.sgl);
	BUG_ON(!acd->ldev);
	BUG_ON(!acd->ldev->pldev);

	dma_unmap_sg(&acd->ldev->pldev->dev, acd->sgt.sgl, acd->sgt.nents, acd->ldev->dir);

	for (i = 0 ; i < acd->page_count ; i++) {
		if (!PageReserved(acd->user_pages[i]))
			set_page_dirty_lock(acd->user_pages[i]);
	}

	unpin_user_pages(acd->user_pages, acd->page_count);

	sg_free_table(&acd->sgt);

	kfree(acd->user_pages);

	acd->flags = flags;

	if (acd->cpl) {
		complete(acd->cpl);
	} else {
		/*
		 * There's no completion, so we're responsible for cleaning up
		 * the acd
		 */
		kfree(acd);
	}
}

/**********  Fileops  **********/
static
int  kpc_dma_open(struct inode *inode, struct file *filp)
{
	struct dev_private_data *priv;
	struct kpc_dma_device *ldev = kpc_dma_lookup_device(iminor(inode));

	if (!ldev)
		return -ENODEV;

	if (!atomic_dec_and_test(&ldev->open_count)) {
		atomic_inc(&ldev->open_count);
		return -EBUSY; /* already open */
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ldev = ldev;
	filp->private_data = priv;

	return 0;
}

static
int  kpc_dma_close(struct inode *inode, struct file *filp)
{
	struct kpc_dma_descriptor *cur;
	struct dev_private_data *priv = (struct dev_private_data *)filp->private_data;
	struct kpc_dma_device *eng = priv->ldev;

	lock_engine(eng);

	stop_dma_engine(eng);

	cur = eng->desc_completed->Next;
	while (cur != eng->desc_next) {
		dev_dbg(&eng->pldev->dev, "Aborting descriptor %p (acd = %p)\n", cur, cur->acd);
		if (cur->DescControlFlags & DMA_DESC_CTL_EOP) {
			if (cur->acd)
				transfer_complete_cb(cur->acd, 0, ACD_FLAG_ABORT);
		}

		clear_desc(cur);
		eng->desc_completed = cur;

		cur = cur->Next;
	}

	start_dma_engine(eng);

	unlock_engine(eng);

	atomic_inc(&priv->ldev->open_count); /* release the device */
	kfree(priv);
	return 0;
}

static
ssize_t  kpc_dma_read(struct file *filp,       char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dev_private_data *priv = (struct dev_private_data *)filp->private_data;

	if (priv->ldev->dir != DMA_FROM_DEVICE)
		return -EMEDIUMTYPE;

	return kpc_dma_transfer(priv, (unsigned long)user_buf, count);
}

static
ssize_t  kpc_dma_write(struct file *filp, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct dev_private_data *priv = (struct dev_private_data *)filp->private_data;

	if (priv->ldev->dir != DMA_TO_DEVICE)
		return -EMEDIUMTYPE;

	return kpc_dma_transfer(priv, (unsigned long)user_buf, count);
}

static
long  kpc_dma_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
	struct dev_private_data *priv = (struct dev_private_data *)filp->private_data;

	switch (ioctl_num) {
	case KND_IOCTL_SET_CARD_ADDR:
		priv->card_addr  = ioctl_param; return priv->card_addr;
	case KND_IOCTL_SET_USER_CTL:
		priv->user_ctl   = ioctl_param; return priv->user_ctl;
	case KND_IOCTL_SET_USER_CTL_LAST:
		priv->user_ctl_last = ioctl_param; return priv->user_ctl_last;
	case KND_IOCTL_GET_USER_STS:
		return priv->user_sts;
	}

	return -ENOTTY;
}

const struct file_operations  kpc_dma_fops = {
	.owner      = THIS_MODULE,
	.open           = kpc_dma_open,
	.release        = kpc_dma_close,
	.read           = kpc_dma_read,
	.write          = kpc_dma_write,
	.unlocked_ioctl = kpc_dma_ioctl,
};

