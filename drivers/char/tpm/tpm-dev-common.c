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
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "tpm.h"
#include "tpm-dev.h"

static void user_reader_timeout(unsigned long ptr)
{
	struct file_priv *priv = (struct file_priv *)ptr;

	pr_warn("TPM user space timeout is deprecated (pid=%d)\n",
		task_tgid_nr(current));

	schedule_work(&priv->work);
}

static void timeout_work(struct work_struct *work)
{
	struct file_priv *priv = container_of(work, struct file_priv, work);

	mutex_lock(&priv->buffer_mutex);
	atomic_set(&priv->data_pending, 0);
	memset(priv->data_buffer, 0, sizeof(priv->data_buffer));
	mutex_unlock(&priv->buffer_mutex);
}

void tpm_common_open(struct file *file, struct tpm_chip *chip,
		     struct file_priv *priv)
{
	priv->chip = chip;
	atomic_set(&priv->data_pending, 0);
	mutex_init(&priv->buffer_mutex);
	setup_timer(&priv->user_read_timer, user_reader_timeout,
			(unsigned long)priv);
	INIT_WORK(&priv->work, timeout_work);

	file->private_data = priv;
}

ssize_t tpm_common_read(struct file *file, char __user *buf,
			size_t size, loff_t *off)
{
	struct file_priv *priv = file->private_data;
	ssize_t ret_size;
	ssize_t orig_ret_size;
	int rc;

	del_singleshot_timer_sync(&priv->user_read_timer);
	flush_work(&priv->work);
	ret_size = atomic_read(&priv->data_pending);
	if (ret_size > 0) {	/* relay data */
		orig_ret_size = ret_size;
		if (size < ret_size)
			ret_size = size;

		mutex_lock(&priv->buffer_mutex);
		rc = copy_to_user(buf, priv->data_buffer, ret_size);
		memset(priv->data_buffer, 0, orig_ret_size);
		if (rc)
			ret_size = -EFAULT;

		mutex_unlock(&priv->buffer_mutex);
	}

	atomic_set(&priv->data_pending, 0);

	return ret_size;
}

ssize_t tpm_common_write(struct file *file, const char __user *buf,
			 size_t size, loff_t *off, struct tpm_space *space)
{
	struct file_priv *priv = file->private_data;
	size_t in_size = size;
	ssize_t out_size;

	/* Cannot perform a write until the read has cleared either via
	 * tpm_read or a user_read_timer timeout. This also prevents split
	 * buffered writes from blocking here.
	 */
	if (atomic_read(&priv->data_pending) != 0)
		return -EBUSY;

	if (in_size > TPM_BUFSIZE)
		return -E2BIG;

	mutex_lock(&priv->buffer_mutex);

	if (copy_from_user
	    (priv->data_buffer, (void __user *) buf, in_size)) {
		mutex_unlock(&priv->buffer_mutex);
		return -EFAULT;
	}

	/* atomic tpm command send and result receive. We only hold the ops
	 * lock during this period so that the tpm can be unregistered even if
	 * the char dev is held open.
	 */
	if (tpm_try_get_ops(priv->chip)) {
		mutex_unlock(&priv->buffer_mutex);
		return -EPIPE;
	}
	out_size = tpm_transmit(priv->chip, space, priv->data_buffer,
				sizeof(priv->data_buffer), 0);

	tpm_put_ops(priv->chip);
	if (out_size < 0) {
		mutex_unlock(&priv->buffer_mutex);
		return out_size;
	}

	atomic_set(&priv->data_pending, out_size);
	mutex_unlock(&priv->buffer_mutex);

	/* Set a timeout by which the reader must come claim the result */
	mod_timer(&priv->user_read_timer, jiffies + (120 * HZ));

	return in_size;
}

/*
 * Called on file close
 */
void tpm_common_release(struct file *file, struct file_priv *priv)
{
	del_singleshot_timer_sync(&priv->user_read_timer);
	flush_work(&priv->work);
	file->private_data = NULL;
	atomic_set(&priv->data_pending, 0);
}
