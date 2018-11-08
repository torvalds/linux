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
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "tpm.h"
#include "tpm-dev.h"

static struct workqueue_struct *tpm_dev_wq;
static DEFINE_MUTEX(tpm_dev_wq_lock);

static void tpm_async_work(struct work_struct *work)
{
	struct file_priv *priv =
			container_of(work, struct file_priv, async_work);
	ssize_t ret;

	mutex_lock(&priv->buffer_mutex);
	priv->command_enqueued = false;
	ret = tpm_transmit(priv->chip, priv->space, priv->data_buffer,
			   sizeof(priv->data_buffer), 0);

	tpm_put_ops(priv->chip);
	if (ret > 0) {
		priv->data_pending = ret;
		mod_timer(&priv->user_read_timer, jiffies + (120 * HZ));
	}
	mutex_unlock(&priv->buffer_mutex);
	wake_up_interruptible(&priv->async_wait);
}

static void user_reader_timeout(struct timer_list *t)
{
	struct file_priv *priv = from_timer(priv, t, user_read_timer);

	pr_warn("TPM user space timeout is deprecated (pid=%d)\n",
		task_tgid_nr(current));

	schedule_work(&priv->timeout_work);
}

static void tpm_timeout_work(struct work_struct *work)
{
	struct file_priv *priv = container_of(work, struct file_priv,
					      timeout_work);

	mutex_lock(&priv->buffer_mutex);
	priv->data_pending = 0;
	memset(priv->data_buffer, 0, sizeof(priv->data_buffer));
	mutex_unlock(&priv->buffer_mutex);
	wake_up_interruptible(&priv->async_wait);
}

void tpm_common_open(struct file *file, struct tpm_chip *chip,
		     struct file_priv *priv, struct tpm_space *space)
{
	priv->chip = chip;
	priv->space = space;

	mutex_init(&priv->buffer_mutex);
	timer_setup(&priv->user_read_timer, user_reader_timeout, 0);
	INIT_WORK(&priv->timeout_work, tpm_timeout_work);
	INIT_WORK(&priv->async_work, tpm_async_work);
	init_waitqueue_head(&priv->async_wait);
	file->private_data = priv;
}

ssize_t tpm_common_read(struct file *file, char __user *buf,
			size_t size, loff_t *off)
{
	struct file_priv *priv = file->private_data;
	ssize_t ret_size = 0;
	int rc;

	del_singleshot_timer_sync(&priv->user_read_timer);
	flush_work(&priv->timeout_work);
	mutex_lock(&priv->buffer_mutex);

	if (priv->data_pending) {
		ret_size = min_t(ssize_t, size, priv->data_pending);
		if (ret_size > 0) {
			rc = copy_to_user(buf, priv->data_buffer, ret_size);
			memset(priv->data_buffer, 0, priv->data_pending);
			if (rc)
				ret_size = -EFAULT;
		}

		priv->data_pending = 0;
	}

	mutex_unlock(&priv->buffer_mutex);
	return ret_size;
}

ssize_t tpm_common_write(struct file *file, const char __user *buf,
			 size_t size, loff_t *off)
{
	struct file_priv *priv = file->private_data;
	int ret = 0;

	if (size > TPM_BUFSIZE)
		return -E2BIG;

	mutex_lock(&priv->buffer_mutex);

	/* Cannot perform a write until the read has cleared either via
	 * tpm_read or a user_read_timer timeout. This also prevents split
	 * buffered writes from blocking here.
	 */
	if (priv->data_pending != 0 || priv->command_enqueued) {
		ret = -EBUSY;
		goto out;
	}

	if (copy_from_user(priv->data_buffer, buf, size)) {
		ret = -EFAULT;
		goto out;
	}

	if (size < 6 ||
	    size < be32_to_cpu(*((__be32 *)(priv->data_buffer + 2)))) {
		ret = -EINVAL;
		goto out;
	}

	/* atomic tpm command send and result receive. We only hold the ops
	 * lock during this period so that the tpm can be unregistered even if
	 * the char dev is held open.
	 */
	if (tpm_try_get_ops(priv->chip)) {
		ret = -EPIPE;
		goto out;
	}

	/*
	 * If in nonblocking mode schedule an async job to send
	 * the command return the size.
	 * In case of error the err code will be returned in
	 * the subsequent read call.
	 */
	if (file->f_flags & O_NONBLOCK) {
		priv->command_enqueued = true;
		queue_work(tpm_dev_wq, &priv->async_work);
		mutex_unlock(&priv->buffer_mutex);
		return size;
	}

	ret = tpm_transmit(priv->chip, priv->space, priv->data_buffer,
			   sizeof(priv->data_buffer), 0);
	tpm_put_ops(priv->chip);

	if (ret > 0) {
		priv->data_pending = ret;
		mod_timer(&priv->user_read_timer, jiffies + (120 * HZ));
		ret = size;
	}
out:
	mutex_unlock(&priv->buffer_mutex);
	return ret;
}

__poll_t tpm_common_poll(struct file *file, poll_table *wait)
{
	struct file_priv *priv = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &priv->async_wait, wait);

	if (priv->data_pending)
		mask = EPOLLIN | EPOLLRDNORM;
	else
		mask = EPOLLOUT | EPOLLWRNORM;

	return mask;
}

/*
 * Called on file close
 */
void tpm_common_release(struct file *file, struct file_priv *priv)
{
	flush_work(&priv->async_work);
	del_singleshot_timer_sync(&priv->user_read_timer);
	flush_work(&priv->timeout_work);
	file->private_data = NULL;
	priv->data_pending = 0;
}

int __init tpm_dev_common_init(void)
{
	tpm_dev_wq = alloc_workqueue("tpm_dev_wq", WQ_MEM_RECLAIM, 0);

	return !tpm_dev_wq ? -ENOMEM : 0;
}

void __exit tpm_dev_common_exit(void)
{
	if (tpm_dev_wq) {
		destroy_workqueue(tpm_dev_wq);
		tpm_dev_wq = NULL;
	}
}
