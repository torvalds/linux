// SPDX-License-Identifier: GPL-2.0-only
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
 */
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "tpm.h"
#include "tpm-dev.h"

static struct workqueue_struct *tpm_dev_wq;

static ssize_t tpm_dev_transmit(struct tpm_chip *chip, struct tpm_space *space,
				u8 *buf, size_t bufsiz)
{
	struct tpm_header *header = (void *)buf;
	ssize_t ret, len;

	ret = tpm2_prepare_space(chip, space, buf, bufsiz);
	/* If the command is not implemented by the TPM, synthesize a
	 * response with a TPM2_RC_COMMAND_CODE return for user-space.
	 */
	if (ret == -EOPNOTSUPP) {
		header->length = cpu_to_be32(sizeof(*header));
		header->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
		header->return_code = cpu_to_be32(TPM2_RC_COMMAND_CODE |
						  TSS2_RESMGR_TPM_RC_LAYER);
		ret = sizeof(*header);
	}
	if (ret)
		goto out_rc;

	len = tpm_transmit(chip, buf, bufsiz);
	if (len < 0)
		ret = len;

	if (!ret)
		ret = tpm2_commit_space(chip, space, buf, &len);

out_rc:
	return ret ? ret : len;
}

static void tpm_dev_async_work(struct work_struct *work)
{
	struct file_priv *priv =
			container_of(work, struct file_priv, async_work);
	ssize_t ret;

	mutex_lock(&priv->buffer_mutex);
	priv->command_enqueued = false;
	ret = tpm_try_get_ops(priv->chip);
	if (ret) {
		priv->response_length = ret;
		goto out;
	}

	ret = tpm_dev_transmit(priv->chip, priv->space, priv->data_buffer,
			       sizeof(priv->data_buffer));
	tpm_put_ops(priv->chip);
	if (ret > 0) {
		priv->response_length = ret;
		mod_timer(&priv->user_read_timer, jiffies + (120 * HZ));
	}
out:
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
	priv->response_read = true;
	priv->response_length = 0;
	memset(priv->data_buffer, 0, sizeof(priv->data_buffer));
	mutex_unlock(&priv->buffer_mutex);
	wake_up_interruptible(&priv->async_wait);
}

void tpm_common_open(struct file *file, struct tpm_chip *chip,
		     struct file_priv *priv, struct tpm_space *space)
{
	priv->chip = chip;
	priv->space = space;
	priv->response_read = true;

	mutex_init(&priv->buffer_mutex);
	timer_setup(&priv->user_read_timer, user_reader_timeout, 0);
	INIT_WORK(&priv->timeout_work, tpm_timeout_work);
	INIT_WORK(&priv->async_work, tpm_dev_async_work);
	init_waitqueue_head(&priv->async_wait);
	file->private_data = priv;
}

ssize_t tpm_common_read(struct file *file, char __user *buf,
			size_t size, loff_t *off)
{
	struct file_priv *priv = file->private_data;
	ssize_t ret_size = 0;
	int rc;

	mutex_lock(&priv->buffer_mutex);

	if (priv->response_length) {
		priv->response_read = true;

		ret_size = min_t(ssize_t, size, priv->response_length);
		if (ret_size <= 0) {
			priv->response_length = 0;
			goto out;
		}

		rc = copy_to_user(buf, priv->data_buffer + *off, ret_size);
		if (rc) {
			memset(priv->data_buffer, 0, TPM_BUFSIZE);
			priv->response_length = 0;
			ret_size = -EFAULT;
		} else {
			memset(priv->data_buffer + *off, 0, ret_size);
			priv->response_length -= ret_size;
			*off += ret_size;
		}
	}

out:
	if (!priv->response_length) {
		*off = 0;
		del_singleshot_timer_sync(&priv->user_read_timer);
		flush_work(&priv->timeout_work);
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
	if ((!priv->response_read && priv->response_length) ||
	    priv->command_enqueued) {
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

	priv->response_length = 0;
	priv->response_read = false;
	*off = 0;

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

	/* atomic tpm command send and result receive. We only hold the ops
	 * lock during this period so that the tpm can be unregistered even if
	 * the char dev is held open.
	 */
	if (tpm_try_get_ops(priv->chip)) {
		ret = -EPIPE;
		goto out;
	}

	ret = tpm_dev_transmit(priv->chip, priv->space, priv->data_buffer,
			       sizeof(priv->data_buffer));
	tpm_put_ops(priv->chip);

	if (ret > 0) {
		priv->response_length = ret;
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
	mutex_lock(&priv->buffer_mutex);

	/*
	 * The response_length indicates if there is still response
	 * (or part of it) to be consumed. Partial reads decrease it
	 * by the number of bytes read, and write resets it the zero.
	 */
	if (priv->response_length)
		mask = EPOLLIN | EPOLLRDNORM;
	else
		mask = EPOLLOUT | EPOLLWRNORM;

	mutex_unlock(&priv->buffer_mutex);
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
	priv->response_length = 0;
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
