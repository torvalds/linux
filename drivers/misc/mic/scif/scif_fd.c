/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"

static int scif_fdopen(struct inode *inode, struct file *f)
{
	struct scif_endpt *priv = scif_open();

	if (!priv)
		return -ENOMEM;
	f->private_data = priv;
	return 0;
}

static int scif_fdclose(struct inode *inode, struct file *f)
{
	struct scif_endpt *priv = f->private_data;

	return scif_close(priv);
}

static int scif_fdflush(struct file *f, fl_owner_t id)
{
	struct scif_endpt *ep = f->private_data;

	spin_lock(&ep->lock);
	/*
	 * The listening endpoint stashes the open file information before
	 * waiting for incoming connections. The release callback would never be
	 * called if the application closed the endpoint, while waiting for
	 * incoming connections from a separate thread since the file descriptor
	 * reference count is bumped up in the accept IOCTL. Call the flush
	 * routine if the id matches the endpoint open file information so that
	 * the listening endpoint can be woken up and the fd released.
	 */
	if (ep->files == id)
		__scif_flush(ep);
	spin_unlock(&ep->lock);
	return 0;
}

static __always_inline void scif_err_debug(int err, const char *str)
{
	/*
	 * ENOTCONN is a common uninteresting error which is
	 * flooding debug messages to the console unnecessarily.
	 */
	if (err < 0 && err != -ENOTCONN)
		dev_dbg(scif_info.mdev.this_device, "%s err %d\n", str, err);
}

static long scif_fdioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct scif_endpt *priv = f->private_data;
	void __user *argp = (void __user *)arg;
	bool non_block = false;

	non_block = !!(f->f_flags & O_NONBLOCK);

	switch (cmd) {
	case SCIF_BIND:
	{
		int pn;

		if (copy_from_user(&pn, argp, sizeof(pn)))
			return -EFAULT;

		pn = scif_bind(priv, pn);
		if (pn < 0)
			return pn;

		if (copy_to_user(argp, &pn, sizeof(pn)))
			return -EFAULT;

		return 0;
	}
	case SCIF_LISTEN:
		return scif_listen(priv, arg);
	}
	return -EINVAL;
}

const struct file_operations scif_fops = {
	.open = scif_fdopen,
	.release = scif_fdclose,
	.unlocked_ioctl = scif_fdioctl,
	.flush = scif_fdflush,
	.owner = THIS_MODULE,
};
