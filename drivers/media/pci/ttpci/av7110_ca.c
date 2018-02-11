/*
 * av7110_ca.c: CA and CI stuff
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at https://linuxtv.org
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/gfp.h>

#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_ca.h"


void CI_handle(struct av7110 *av7110, u8 *data, u16 len)
{
	dprintk(8, "av7110:%p\n",av7110);

	if (len < 3)
		return;
	switch (data[0]) {
	case CI_MSG_CI_INFO:
		if (data[2] != 1 && data[2] != 2)
			break;
		switch (data[1]) {
		case 0:
			av7110->ci_slot[data[2] - 1].flags = 0;
			break;
		case 1:
			av7110->ci_slot[data[2] - 1].flags |= CA_CI_MODULE_PRESENT;
			break;
		case 2:
			av7110->ci_slot[data[2] - 1].flags |= CA_CI_MODULE_READY;
			break;
		}
		break;
	case CI_SWITCH_PRG_REPLY:
		//av7110->ci_stat=data[1];
		break;
	default:
		break;
	}
}


void ci_get_data(struct dvb_ringbuffer *cibuf, u8 *data, int len)
{
	if (dvb_ringbuffer_free(cibuf) < len + 2)
		return;

	DVB_RINGBUFFER_WRITE_BYTE(cibuf, len >> 8);
	DVB_RINGBUFFER_WRITE_BYTE(cibuf, len & 0xff);
	dvb_ringbuffer_write(cibuf, data, len);
	wake_up_interruptible(&cibuf->queue);
}


/******************************************************************************
 * CI link layer file ops
 ******************************************************************************/

static int ci_ll_init(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf, int size)
{
	struct dvb_ringbuffer *tab[] = { cirbuf, ciwbuf, NULL }, **p;
	void *data;

	for (p = tab; *p; p++) {
		data = vmalloc(size);
		if (!data) {
			while (p-- != tab) {
				vfree(p[0]->data);
				p[0]->data = NULL;
			}
			return -ENOMEM;
		}
		dvb_ringbuffer_init(*p, data, size);
	}
	return 0;
}

static void ci_ll_flush(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf)
{
	dvb_ringbuffer_flush_spinlock_wakeup(cirbuf);
	dvb_ringbuffer_flush_spinlock_wakeup(ciwbuf);
}

static void ci_ll_release(struct dvb_ringbuffer *cirbuf, struct dvb_ringbuffer *ciwbuf)
{
	vfree(cirbuf->data);
	cirbuf->data = NULL;
	vfree(ciwbuf->data);
	ciwbuf->data = NULL;
}

static int ci_ll_reset(struct dvb_ringbuffer *cibuf, struct file *file,
		       int slots, struct ca_slot_info *slot)
{
	int i;
	int len = 0;
	u8 msg[8] = { 0x00, 0x06, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00 };

	for (i = 0; i < 2; i++) {
		if (slots & (1 << i))
			len += 8;
	}

	if (dvb_ringbuffer_free(cibuf) < len)
		return -EBUSY;

	for (i = 0; i < 2; i++) {
		if (slots & (1 << i)) {
			msg[2] = i;
			dvb_ringbuffer_write(cibuf, msg, 8);
			slot[i].flags = 0;
		}
	}

	return 0;
}

static ssize_t ci_ll_write(struct dvb_ringbuffer *cibuf, struct file *file,
			   const char __user *buf, size_t count, loff_t *ppos)
{
	int free;
	int non_blocking = file->f_flags & O_NONBLOCK;
	u8 *page = (u8 *)__get_free_page(GFP_USER);
	int res;

	if (!page)
		return -ENOMEM;

	res = -EINVAL;
	if (count > 2048)
		goto out;

	res = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;

	free = dvb_ringbuffer_free(cibuf);
	if (count + 2 > free) {
		res = -EWOULDBLOCK;
		if (non_blocking)
			goto out;
		res = -ERESTARTSYS;
		if (wait_event_interruptible(cibuf->queue,
					     (dvb_ringbuffer_free(cibuf) >= count + 2)))
			goto out;
	}

	DVB_RINGBUFFER_WRITE_BYTE(cibuf, count >> 8);
	DVB_RINGBUFFER_WRITE_BYTE(cibuf, count & 0xff);

	res = dvb_ringbuffer_write(cibuf, page, count);
out:
	free_page((unsigned long)page);
	return res;
}

static ssize_t ci_ll_read(struct dvb_ringbuffer *cibuf, struct file *file,
			  char __user *buf, size_t count, loff_t *ppos)
{
	int avail;
	int non_blocking = file->f_flags & O_NONBLOCK;
	ssize_t len;

	if (!cibuf->data || !count)
		return 0;
	if (non_blocking && (dvb_ringbuffer_empty(cibuf)))
		return -EWOULDBLOCK;
	if (wait_event_interruptible(cibuf->queue,
				     !dvb_ringbuffer_empty(cibuf)))
		return -ERESTARTSYS;
	avail = dvb_ringbuffer_avail(cibuf);
	if (avail < 4)
		return 0;
	len = DVB_RINGBUFFER_PEEK(cibuf, 0) << 8;
	len |= DVB_RINGBUFFER_PEEK(cibuf, 1);
	if (avail < len + 2 || count < len)
		return -EINVAL;
	DVB_RINGBUFFER_SKIP(cibuf, 2);

	return dvb_ringbuffer_read_user(cibuf, buf, len);
}

static int dvb_ca_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	int err = dvb_generic_open(inode, file);

	dprintk(8, "av7110:%p\n",av7110);

	if (err < 0)
		return err;
	ci_ll_flush(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
	return 0;
}

static unsigned int dvb_ca_poll (struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	struct dvb_ringbuffer *rbuf = &av7110->ci_rbuffer;
	struct dvb_ringbuffer *wbuf = &av7110->ci_wbuffer;
	unsigned int mask = 0;

	dprintk(8, "av7110:%p\n",av7110);

	poll_wait(file, &rbuf->queue, wait);
	poll_wait(file, &wbuf->queue, wait);

	if (!dvb_ringbuffer_empty(rbuf))
		mask |= (POLLIN | POLLRDNORM);

	if (dvb_ringbuffer_free(wbuf) > 1024)
		mask |= (POLLOUT | POLLWRNORM);

	return mask;
}

static int dvb_ca_ioctl(struct file *file, unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;
	unsigned long arg = (unsigned long) parg;
	int ret = 0;

	dprintk(8, "av7110:%p\n",av7110);

	if (mutex_lock_interruptible(&av7110->ioctl_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case CA_RESET:
		ret = ci_ll_reset(&av7110->ci_wbuffer, file, arg,
				  &av7110->ci_slot[0]);
		break;
	case CA_GET_CAP:
	{
		struct ca_caps cap;

		cap.slot_num = 2;
		cap.slot_type = (FW_CI_LL_SUPPORT(av7110->arm_app) ?
				 CA_CI_LINK : CA_CI) | CA_DESCR;
		cap.descr_num = 16;
		cap.descr_type = CA_ECD;
		memcpy(parg, &cap, sizeof(cap));
		break;
	}

	case CA_GET_SLOT_INFO:
	{
		struct ca_slot_info *info=(struct ca_slot_info *)parg;

		if (info->num < 0 || info->num > 1) {
			mutex_unlock(&av7110->ioctl_mutex);
			return -EINVAL;
		}
		av7110->ci_slot[info->num].num = info->num;
		av7110->ci_slot[info->num].type = FW_CI_LL_SUPPORT(av7110->arm_app) ?
							CA_CI_LINK : CA_CI;
		memcpy(info, &av7110->ci_slot[info->num], sizeof(struct ca_slot_info));
		break;
	}

	case CA_GET_MSG:
		break;

	case CA_SEND_MSG:
		break;

	case CA_GET_DESCR_INFO:
	{
		struct ca_descr_info info;

		info.num = 16;
		info.type = CA_ECD;
		memcpy(parg, &info, sizeof (info));
		break;
	}

	case CA_SET_DESCR:
	{
		struct ca_descr *descr = (struct ca_descr*) parg;

		if (descr->index >= 16 || descr->parity > 1) {
			mutex_unlock(&av7110->ioctl_mutex);
			return -EINVAL;
		}
		av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetDescr, 5,
			      (descr->index<<8)|descr->parity,
			      (descr->cw[0]<<8)|descr->cw[1],
			      (descr->cw[2]<<8)|descr->cw[3],
			      (descr->cw[4]<<8)|descr->cw[5],
			      (descr->cw[6]<<8)|descr->cw[7]);
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&av7110->ioctl_mutex);
	return ret;
}

static ssize_t dvb_ca_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;

	dprintk(8, "av7110:%p\n",av7110);
	return ci_ll_write(&av7110->ci_wbuffer, file, buf, count, ppos);
}

static ssize_t dvb_ca_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct av7110 *av7110 = dvbdev->priv;

	dprintk(8, "av7110:%p\n",av7110);
	return ci_ll_read(&av7110->ci_rbuffer, file, buf, count, ppos);
}

static const struct file_operations dvb_ca_fops = {
	.owner		= THIS_MODULE,
	.read		= dvb_ca_read,
	.write		= dvb_ca_write,
	.unlocked_ioctl	= dvb_generic_ioctl,
	.open		= dvb_ca_open,
	.release	= dvb_generic_release,
	.poll		= dvb_ca_poll,
	.llseek		= default_llseek,
};

static struct dvb_device dvbdev_ca = {
	.priv		= NULL,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_ca_fops,
	.kernel_ioctl	= dvb_ca_ioctl,
};


int av7110_ca_register(struct av7110 *av7110)
{
	return dvb_register_device(&av7110->dvb_adapter, &av7110->ca_dev,
				   &dvbdev_ca, av7110, DVB_DEVICE_CA, 0);
}

void av7110_ca_unregister(struct av7110 *av7110)
{
	dvb_unregister_device(av7110->ca_dev);
}

int av7110_ca_init(struct av7110* av7110)
{
	return ci_ll_init(&av7110->ci_rbuffer, &av7110->ci_wbuffer, 8192);
}

void av7110_ca_exit(struct av7110* av7110)
{
	ci_ll_release(&av7110->ci_rbuffer, &av7110->ci_wbuffer);
}
