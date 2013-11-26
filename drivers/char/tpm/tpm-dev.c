/*
 * Copyright (C) 2004 IBM Corporation
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Copyright (C) 2013 Obsidian Research Corp
 * Jason Gunthorpe <jgunthorpe@obsidianresearch.com>
 *
 * Device file system interface to the TPM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "tpm.h"

static void user_reader_timeout(unsigned long ptr)
{
	struct tpm_chip *chip = (struct tpm_chip *) ptr;

	schedule_work(&chip->work);
}

static void timeout_work(struct work_struct *work)
{
	struct tpm_chip *chip = container_of(work, struct tpm_chip, work);

	mutex_lock(&chip->buffer_mutex);
	atomic_set(&chip->data_pending, 0);
	memset(chip->data_buffer, 0, TPM_BUFSIZE);
	mutex_unlock(&chip->buffer_mutex);
}

static int tpm_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct tpm_chip *chip = container_of(misc, struct tpm_chip,
					     vendor.miscdev);

	/* It's assured that the chip will be opened just once,
	 * by the check of is_open variable, which is protected
	 * by driver_lock. */
	if (test_and_set_bit(0, &chip->is_open)) {
		dev_dbg(chip->dev, "Another process owns this TPM\n");
		return -EBUSY;
	}

	chip->data_buffer = kzalloc(TPM_BUFSIZE, GFP_KERNEL);
	if (chip->data_buffer == NULL) {
		clear_bit(0, &chip->is_open);
		return -ENOMEM;
	}

	atomic_set(&chip->data_pending, 0);

	file->private_data = chip;
	get_device(chip->dev);
	return 0;
}

static ssize_t tpm_read(struct file *file, char __user *buf,
			size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	ssize_t ret_size;
	int rc;

	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_work(&chip->work);
	ret_size = atomic_read(&chip->data_pending);
	if (ret_size > 0) {	/* relay data */
		ssize_t orig_ret_size = ret_size;
		if (size < ret_size)
			ret_size = size;

		mutex_lock(&chip->buffer_mutex);
		rc = copy_to_user(buf, chip->data_buffer, ret_size);
		memset(chip->data_buffer, 0, orig_ret_size);
		if (rc)
			ret_size = -EFAULT;

		mutex_unlock(&chip->buffer_mutex);
	}

	atomic_set(&chip->data_pending, 0);

	return ret_size;
}

static ssize_t tpm_write(struct file *file, const char __user *buf,
			 size_t size, loff_t *off)
{
	struct tpm_chip *chip = file->private_data;
	size_t in_size = size;
	ssize_t out_size;

	/* cannot perform a write until the read has cleared
	   either via tpm_read or a user_read_timer timeout.
	   This also prevents splitted buffered writes from blocking here.
	*/
	if (atomic_read(&chip->data_pending) != 0)
		return -EBUSY;

	if (in_size > TPM_BUFSIZE)
		return -E2BIG;

	mutex_lock(&chip->buffer_mutex);

	if (copy_from_user
	    (chip->data_buffer, (void __user *) buf, in_size)) {
		mutex_unlock(&chip->buffer_mutex);
		return -EFAULT;
	}

	/* atomic tpm command send and result receive */
	out_size = tpm_transmit(chip, chip->data_buffer, TPM_BUFSIZE);
	if (out_size < 0) {
		mutex_unlock(&chip->buffer_mutex);
		return out_size;
	}

	atomic_set(&chip->data_pending, out_size);
	mutex_unlock(&chip->buffer_mutex);

	/* Set a timeout by which the reader must come claim the result */
	mod_timer(&chip->user_read_timer, jiffies + (60 * HZ));

	return in_size;
}

/*
 * Called on file close
 */
static int tpm_release(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip = file->private_data;

	del_singleshot_timer_sync(&chip->user_read_timer);
	flush_work(&chip->work);
	file->private_data = NULL;
	atomic_set(&chip->data_pending, 0);
	kzfree(chip->data_buffer);
	clear_bit(0, &chip->is_open);
	put_device(chip->dev);
	return 0;
}

static const struct file_operations tpm_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = tpm_open,
	.read = tpm_read,
	.write = tpm_write,
	.release = tpm_release,
};

int tpm_dev_add_device(struct tpm_chip *chip)
{
	int rc;

	mutex_init(&chip->buffer_mutex);
	INIT_WORK(&chip->work, timeout_work);

	setup_timer(&chip->user_read_timer, user_reader_timeout,
			(unsigned long)chip);

	chip->vendor.miscdev.fops = &tpm_fops;
	if (chip->dev_num == 0)
		chip->vendor.miscdev.minor = TPM_MINOR;
	else
		chip->vendor.miscdev.minor = MISC_DYNAMIC_MINOR;

	chip->vendor.miscdev.name = chip->devname;
	chip->vendor.miscdev.parent = chip->dev;

	rc = misc_register(&chip->vendor.miscdev);
	if (rc) {
		chip->vendor.miscdev.name = NULL;
		dev_err(chip->dev,
			"unable to misc_register %s, minor %d err=%d\n",
			chip->vendor.miscdev.name,
			chip->vendor.miscdev.minor, rc);
	}
	return rc;
}

void tpm_dev_del_device(struct tpm_chip *chip)
{
	if (chip->vendor.miscdev.name)
		misc_deregister(&chip->vendor.miscdev);
}
