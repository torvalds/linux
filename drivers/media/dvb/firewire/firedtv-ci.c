/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/dvb/ca.h>
#include <linux/fs.h>
#include <linux/module.h>

#include <dvbdev.h>

#include "avc.h"
#include "firedtv.h"
#include "firedtv-ci.h"

static int fdtv_ca_ready(ANTENNA_INPUT_INFO *info)
{
	return info->CaInitializationStatus == 1 &&
	       info->CaErrorFlag == 0 &&
	       info->CaDvbFlag == 1 &&
	       info->CaModulePresentStatus == 1;
}

static int fdtv_get_ca_flags(ANTENNA_INPUT_INFO *info)
{
	int flags = 0;

	if (info->CaModulePresentStatus == 1)
		flags |= CA_CI_MODULE_PRESENT;
	if (info->CaInitializationStatus == 1 &&
	    info->CaErrorFlag == 0 &&
	    info->CaDvbFlag == 1)
		flags |= CA_CI_MODULE_READY;
	return flags;
}

static int fdtv_ca_reset(struct firedtv *fdtv)
{
	return avc_ca_reset(fdtv) ? -EFAULT : 0;
}

static int fdtv_ca_get_caps(void *arg)
{
	struct ca_caps *cap = arg;

	cap->slot_num = 1;
	cap->slot_type = CA_CI;
	cap->descr_num = 1;
	cap->descr_type = CA_ECD;
	return 0;
}

static int fdtv_ca_get_slot_info(struct firedtv *fdtv, void *arg)
{
	ANTENNA_INPUT_INFO info;
	struct ca_slot_info *slot = arg;

	if (avc_tuner_status(fdtv, &info))
		return -EFAULT;

	if (slot->num != 0)
		return -EFAULT;

	slot->type = CA_CI;
	slot->flags = fdtv_get_ca_flags(&info);
	return 0;
}

static int fdtv_ca_app_info(struct firedtv *fdtv, void *arg)
{
	struct ca_msg *reply = arg;

	return
	    avc_ca_app_info(fdtv, reply->msg, &reply->length) ? -EFAULT : 0;
}

static int fdtv_ca_info(struct firedtv *fdtv, void *arg)
{
	struct ca_msg *reply = arg;

	return avc_ca_info(fdtv, reply->msg, &reply->length) ? -EFAULT : 0;
}

static int fdtv_ca_get_mmi(struct firedtv *fdtv, void *arg)
{
	struct ca_msg *reply = arg;

	return
	    avc_ca_get_mmi(fdtv, reply->msg, &reply->length) ? -EFAULT : 0;
}

static int fdtv_ca_get_msg(struct firedtv *fdtv, void *arg)
{
	ANTENNA_INPUT_INFO info;
	int err;

	switch (fdtv->ca_last_command) {
	case TAG_APP_INFO_ENQUIRY:
		err = fdtv_ca_app_info(fdtv, arg);
		break;
	case TAG_CA_INFO_ENQUIRY:
		err = fdtv_ca_info(fdtv, arg);
		break;
	default:
		if (avc_tuner_status(fdtv, &info))
			err = -EFAULT;
		else if (info.CaMmi == 1)
			err = fdtv_ca_get_mmi(fdtv, arg);
		else {
			printk(KERN_INFO "%s: Unhandled message 0x%08X\n",
			       __func__, fdtv->ca_last_command);
			err = -EFAULT;
		}
	}
	fdtv->ca_last_command = 0;
	return err;
}

static int fdtv_ca_pmt(struct firedtv *fdtv, void *arg)
{
	struct ca_msg *msg = arg;
	int data_pos;
	int data_length;
	int i;

	data_pos = 4;
	if (msg->msg[3] & 0x80) {
		data_length = 0;
		for (i = 0; i < (msg->msg[3] & 0x7F); i++)
			data_length = (data_length << 8) + msg->msg[data_pos++];
	} else {
		data_length = msg->msg[3];
	}

	return avc_ca_pmt(fdtv, &msg->msg[data_pos], data_length) ?
	       -EFAULT : 0;
}

static int fdtv_ca_send_msg(struct firedtv *fdtv, void *arg)
{
	struct ca_msg *msg = arg;
	int err;

	/* Do we need a semaphore for this? */
	fdtv->ca_last_command =
		(msg->msg[0] << 16) + (msg->msg[1] << 8) + msg->msg[2];
	switch (fdtv->ca_last_command) {
	case TAG_CA_PMT:
		err = fdtv_ca_pmt(fdtv, arg);
		break;
	case TAG_APP_INFO_ENQUIRY:
		/* handled in ca_get_msg */
		err = 0;
		break;
	case TAG_CA_INFO_ENQUIRY:
		/* handled in ca_get_msg */
		err = 0;
		break;
	case TAG_ENTER_MENU:
		err = avc_ca_enter_menu(fdtv);
		break;
	default:
		printk(KERN_ERR "%s: Unhandled unknown message 0x%08X\n",
		       __func__, fdtv->ca_last_command);
		err = -EFAULT;
	}
	return err;
}

static int fdtv_ca_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct firedtv *fdtv = dvbdev->priv;
	ANTENNA_INPUT_INFO info;
	int err;

	switch(cmd) {
	case CA_RESET:
		err = fdtv_ca_reset(fdtv);
		break;
	case CA_GET_CAP:
		err = fdtv_ca_get_caps(arg);
		break;
	case CA_GET_SLOT_INFO:
		err = fdtv_ca_get_slot_info(fdtv, arg);
		break;
	case CA_GET_MSG:
		err = fdtv_ca_get_msg(fdtv, arg);
		break;
	case CA_SEND_MSG:
		err = fdtv_ca_send_msg(fdtv, arg);
		break;
	default:
		printk(KERN_INFO "%s: Unhandled ioctl, command: %u\n",__func__,
		       cmd);
		err = -EOPNOTSUPP;
	}

	/* FIXME Is this necessary? */
	avc_tuner_status(fdtv, &info);

	return err;
}

static unsigned int fdtv_ca_io_poll(struct file *file, poll_table *wait)
{
	return POLLIN;
}

static struct file_operations fdtv_ca_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
	.poll		= fdtv_ca_io_poll,
};

static struct dvb_device fdtv_ca = {
	.users		= 1,
	.readers	= 1,
	.writers	= 1,
	.fops		= &fdtv_ca_fops,
	.kernel_ioctl	= fdtv_ca_ioctl,
};

int fdtv_ca_register(struct firedtv *fdtv)
{
	ANTENNA_INPUT_INFO info;
	int err;

	if (avc_tuner_status(fdtv, &info))
		return -EINVAL;

	if (!fdtv_ca_ready(&info))
		return -EFAULT;

	err = dvb_register_device(&fdtv->adapter, &fdtv->cadev,
				  &fdtv_ca, fdtv, DVB_DEVICE_CA);

	if (info.CaApplicationInfo == 0)
		printk(KERN_ERR "%s: CaApplicationInfo is not set.\n",
		       __func__);
	if (info.CaDateTimeRequest == 1)
		avc_ca_get_time_date(fdtv, &fdtv->ca_time_interval);

	return err;
}

void fdtv_ca_release(struct firedtv *fdtv)
{
	if (fdtv->cadev)
		dvb_unregister_device(fdtv->cadev);
}
