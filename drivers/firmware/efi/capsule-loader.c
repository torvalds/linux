/*
 * EFI capsule loader driver.
 *
 * Copyright 2015 Intel Corporation
 *
 * This file is part of the Linux kernel, and is made available under
 * the terms of the GNU General Public License version 2.
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/efi.h>
#include <linux/vmalloc.h>

#define NO_FURTHER_WRITE_ACTION -1

/**
 * efi_free_all_buff_pages - free all previous allocated buffer pages
 * @cap_info: pointer to current instance of capsule_info structure
 *
 *	In addition to freeing buffer pages, it flags NO_FURTHER_WRITE_ACTION
 *	to cease processing data in subsequent write(2) calls until close(2)
 *	is called.
 **/
static void efi_free_all_buff_pages(struct capsule_info *cap_info)
{
	while (cap_info->index > 0)
		__free_page(cap_info->pages[--cap_info->index]);

	cap_info->index = NO_FURTHER_WRITE_ACTION;
}

int __efi_capsule_setup_info(struct capsule_info *cap_info)
{
	size_t pages_needed;
	int ret;
	void *temp_page;

	pages_needed = ALIGN(cap_info->total_size, PAGE_SIZE) / PAGE_SIZE;

	if (pages_needed == 0) {
		pr_err("invalid capsule size\n");
		return -EINVAL;
	}

	/* Check if the capsule binary supported */
	ret = efi_capsule_supported(cap_info->header.guid,
				    cap_info->header.flags,
				    cap_info->header.imagesize,
				    &cap_info->reset_type);
	if (ret) {
		pr_err("capsule not supported\n");
		return ret;
	}

	temp_page = krealloc(cap_info->pages,
			     pages_needed * sizeof(void *),
			     GFP_KERNEL | __GFP_ZERO);
	if (!temp_page)
		return -ENOMEM;

	cap_info->pages = temp_page;

	temp_page = krealloc(cap_info->phys,
			     pages_needed * sizeof(phys_addr_t *),
			     GFP_KERNEL | __GFP_ZERO);
	if (!temp_page)
		return -ENOMEM;

	cap_info->phys = temp_page;

	return 0;
}

/**
 * efi_capsule_setup_info - obtain the efi capsule header in the binary and
 *			    setup capsule_info structure
 * @cap_info: pointer to current instance of capsule_info structure
 * @kbuff: a mapped first page buffer pointer
 * @hdr_bytes: the total received number of bytes for efi header
 *
 * Platforms with non-standard capsule update mechanisms can override
 * this __weak function so they can perform any required capsule
 * image munging. See quark_quirk_function() for an example.
 **/
int __weak efi_capsule_setup_info(struct capsule_info *cap_info, void *kbuff,
				  size_t hdr_bytes)
{
	/* Only process data block that is larger than efi header size */
	if (hdr_bytes < sizeof(efi_capsule_header_t))
		return 0;

	memcpy(&cap_info->header, kbuff, sizeof(cap_info->header));
	cap_info->total_size = cap_info->header.imagesize;

	return __efi_capsule_setup_info(cap_info);
}

/**
 * efi_capsule_submit_update - invoke the efi_capsule_update API once binary
 *			       upload done
 * @cap_info: pointer to current instance of capsule_info structure
 **/
static ssize_t efi_capsule_submit_update(struct capsule_info *cap_info)
{
	bool do_vunmap = false;
	int ret;

	/*
	 * cap_info->capsule may have been assigned already by a quirk
	 * handler, so only overwrite it if it is NULL
	 */
	if (!cap_info->capsule) {
		cap_info->capsule = vmap(cap_info->pages, cap_info->index,
					 VM_MAP, PAGE_KERNEL);
		if (!cap_info->capsule)
			return -ENOMEM;
		do_vunmap = true;
	}

	ret = efi_capsule_update(cap_info->capsule, cap_info->phys);
	if (do_vunmap)
		vunmap(cap_info->capsule);
	if (ret) {
		pr_err("capsule update failed\n");
		return ret;
	}

	/* Indicate capsule binary uploading is done */
	cap_info->index = NO_FURTHER_WRITE_ACTION;
	pr_info("Successfully upload capsule file with reboot type '%s'\n",
		!cap_info->reset_type ? "RESET_COLD" :
		cap_info->reset_type == 1 ? "RESET_WARM" :
		"RESET_SHUTDOWN");
	return 0;
}

/**
 * efi_capsule_write - store the capsule binary and pass it to
 *		       efi_capsule_update() API
 * @file: file pointer
 * @buff: buffer pointer
 * @count: number of bytes in @buff
 * @offp: not used
 *
 *	Expectation:
 *	- A user space tool should start at the beginning of capsule binary and
 *	  pass data in sequentially.
 *	- Users should close and re-open this file note in order to upload more
 *	  capsules.
 *	- After an error returned, user should close the file and restart the
 *	  operation for the next try otherwise -EIO will be returned until the
 *	  file is closed.
 *	- An EFI capsule header must be located at the beginning of capsule
 *	  binary file and passed in as first block data of write operation.
 **/
static ssize_t efi_capsule_write(struct file *file, const char __user *buff,
				 size_t count, loff_t *offp)
{
	int ret = 0;
	struct capsule_info *cap_info = file->private_data;
	struct page *page;
	void *kbuff = NULL;
	size_t write_byte;

	if (count == 0)
		return 0;

	/* Return error while NO_FURTHER_WRITE_ACTION is flagged */
	if (cap_info->index < 0)
		return -EIO;

	/* Only alloc a new page when previous page is full */
	if (!cap_info->page_bytes_remain) {
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			ret = -ENOMEM;
			goto failed;
		}

		cap_info->pages[cap_info->index] = page;
		cap_info->phys[cap_info->index] = page_to_phys(page);
		cap_info->page_bytes_remain = PAGE_SIZE;
		cap_info->index++;
	} else {
		page = cap_info->pages[cap_info->index - 1];
	}

	kbuff = kmap(page);
	kbuff += PAGE_SIZE - cap_info->page_bytes_remain;

	/* Copy capsule binary data from user space to kernel space buffer */
	write_byte = min_t(size_t, count, cap_info->page_bytes_remain);
	if (copy_from_user(kbuff, buff, write_byte)) {
		ret = -EFAULT;
		goto fail_unmap;
	}
	cap_info->page_bytes_remain -= write_byte;

	/* Setup capsule binary info structure */
	if (cap_info->header.headersize == 0) {
		ret = efi_capsule_setup_info(cap_info, kbuff - cap_info->count,
					     cap_info->count + write_byte);
		if (ret)
			goto fail_unmap;
	}

	cap_info->count += write_byte;
	kunmap(page);

	/* Submit the full binary to efi_capsule_update() API */
	if (cap_info->header.headersize > 0 &&
	    cap_info->count >= cap_info->total_size) {
		if (cap_info->count > cap_info->total_size) {
			pr_err("capsule upload size exceeded header defined size\n");
			ret = -EINVAL;
			goto failed;
		}

		ret = efi_capsule_submit_update(cap_info);
		if (ret)
			goto failed;
	}

	return write_byte;

fail_unmap:
	kunmap(page);
failed:
	efi_free_all_buff_pages(cap_info);
	return ret;
}

/**
 * efi_capsule_flush - called by file close or file flush
 * @file: file pointer
 * @id: not used
 *
 *	If a capsule is being partially uploaded then calling this function
 *	will be treated as upload termination and will free those completed
 *	buffer pages and -ECANCELED will be returned.
 **/
static int efi_capsule_flush(struct file *file, fl_owner_t id)
{
	int ret = 0;
	struct capsule_info *cap_info = file->private_data;

	if (cap_info->index > 0) {
		pr_err("capsule upload not complete\n");
		efi_free_all_buff_pages(cap_info);
		ret = -ECANCELED;
	}

	return ret;
}

/**
 * efi_capsule_release - called by file close
 * @inode: not used
 * @file: file pointer
 *
 *	We will not free successfully submitted pages since efi update
 *	requires data to be maintained across system reboot.
 **/
static int efi_capsule_release(struct inode *inode, struct file *file)
{
	struct capsule_info *cap_info = file->private_data;

	kfree(cap_info->pages);
	kfree(cap_info->phys);
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

/**
 * efi_capsule_open - called by file open
 * @inode: not used
 * @file: file pointer
 *
 *	Will allocate each capsule_info memory for each file open call.
 *	This provided the capability to support multiple file open feature
 *	where user is not needed to wait for others to finish in order to
 *	upload their capsule binary.
 **/
static int efi_capsule_open(struct inode *inode, struct file *file)
{
	struct capsule_info *cap_info;

	cap_info = kzalloc(sizeof(*cap_info), GFP_KERNEL);
	if (!cap_info)
		return -ENOMEM;

	cap_info->pages = kzalloc(sizeof(void *), GFP_KERNEL);
	if (!cap_info->pages) {
		kfree(cap_info);
		return -ENOMEM;
	}

	cap_info->phys = kzalloc(sizeof(void *), GFP_KERNEL);
	if (!cap_info->phys) {
		kfree(cap_info->pages);
		kfree(cap_info);
		return -ENOMEM;
	}

	file->private_data = cap_info;

	return 0;
}

static const struct file_operations efi_capsule_fops = {
	.owner = THIS_MODULE,
	.open = efi_capsule_open,
	.write = efi_capsule_write,
	.flush = efi_capsule_flush,
	.release = efi_capsule_release,
	.llseek = no_llseek,
};

static struct miscdevice efi_capsule_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "efi_capsule_loader",
	.fops = &efi_capsule_fops,
};

static int __init efi_capsule_loader_init(void)
{
	int ret;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return -ENODEV;

	ret = misc_register(&efi_capsule_misc);
	if (ret)
		pr_err("Unable to register capsule loader device\n");

	return ret;
}
module_init(efi_capsule_loader_init);

static void __exit efi_capsule_loader_exit(void)
{
	misc_deregister(&efi_capsule_misc);
}
module_exit(efi_capsule_loader_exit);

MODULE_DESCRIPTION("EFI capsule firmware binary loader");
MODULE_LICENSE("GPL v2");
