/*
 * FireSAT DVB driver
 *
 * Copyright (c) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include "firesat-ci.h"
#include "firesat.h"
#include "avc_api.h"

#include <linux/dvb/ca.h>
#include <dvbdev.h>

static unsigned int ca_debug = 0;
module_param(ca_debug, int, 0644);
MODULE_PARM_DESC(ca_debug, "debug logging of ca system, default is 0 (no)");

static int firesat_ca_ready(ANTENNA_INPUT_INFO *info)
{
	if (ca_debug != 0)
		printk("%s: CaMmi=%d, CaInit=%d, CaError=%d, CaDvb=%d, "
		       "CaModule=%d, CaAppInfo=%d, CaDateTime=%d, "
		       "CaPmt=%d\n", __func__, info->CaMmi,
		       info->CaInitializationStatus, info->CaErrorFlag,
		       info->CaDvbFlag, info->CaModulePresentStatus,
		       info->CaApplicationInfo,
		       info->CaDateTimeRequest, info->CaPmtReply);
	return info->CaInitializationStatus == 1 &&
		info->CaErrorFlag == 0 &&
		info->CaDvbFlag == 1 &&
		info->CaModulePresentStatus == 1;
}

static int firesat_get_ca_flags(ANTENNA_INPUT_INFO *info)
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

static int firesat_ca_reset(struct firesat *firesat)
{
	if (ca_debug)
		printk(KERN_INFO "%s: ioctl CA_RESET\n", __func__);
	if (avc_ca_reset(firesat))
		return -EFAULT;
	return 0;
}

static int firesat_ca_get_caps(struct firesat *firesat, void *arg)
{
	struct ca_caps *cap_p = (struct ca_caps*)arg;
	int err = 0;

	cap_p->slot_num = 1;
	cap_p->slot_type = CA_CI;
	cap_p->descr_num = 1;
	cap_p->descr_type = CA_ECD;
	if (ca_debug)
		printk(KERN_INFO "%s: ioctl CA_GET_CAP\n", __func__);
	return err;
}

static int firesat_ca_get_slot_info(struct firesat *firesat, void *arg)
{
	ANTENNA_INPUT_INFO info;
	struct ca_slot_info *slot_p = (struct ca_slot_info*)arg;

	if (ca_debug)
		printk(KERN_INFO "%s: ioctl CA_GET_SLOT_INFO on slot %d.\n",
		       __func__, slot_p->num);
	if (AVCTunerStatus(firesat, &info))
		return -EFAULT;

	if (slot_p->num == 0) {
		slot_p->type = CA_CI;
		slot_p->flags = firesat_get_ca_flags(&info);
	}
	else {
		return -EFAULT;
	}
	return 0;
}

static int firesat_ca_app_info(struct firesat *firesat, void *arg)
{
	struct ca_msg *reply_p = (struct ca_msg*)arg;
	int i;

	if (avc_ca_app_info(firesat, reply_p->msg, &reply_p->length))
		return -EFAULT;
	if (ca_debug) {
		printk(KERN_INFO "%s: Creating TAG_APP_INFO message:",
		       __func__);
		for (i = 0; i < reply_p->length; i++)
			printk("0x%02X, ", (unsigned char)reply_p->msg[i]);
		printk("\n");
		}
	return 0;
}

static int firesat_ca_info(struct firesat *firesat, void *arg)
{
	struct ca_msg *reply_p = (struct ca_msg*)arg;
	int i;

	if (avc_ca_info(firesat, reply_p->msg, &reply_p->length))
		return -EFAULT;
	if (ca_debug) {
		printk(KERN_INFO "%s: Creating TAG_CA_INFO message:",
		       __func__);
		for (i = 0; i < reply_p->length; i++)
			printk("0x%02X, ", (unsigned char)reply_p->msg[i]);
		printk("\n");
	}
	return 0;
}

static int firesat_ca_get_mmi(struct firesat *firesat, void *arg)
{
	struct ca_msg *reply_p = (struct ca_msg*)arg;
	int i;

	if (avc_ca_get_mmi(firesat, reply_p->msg, &reply_p->length))
		return -EFAULT;
	if (ca_debug) {
		printk(KERN_INFO "%s: Creating MMI reply INFO message:",
		       __func__);
		for (i = 0; i < reply_p->length; i++)
			printk("0x%02X, ", (unsigned char)reply_p->msg[i]);
		printk("\n");
	}
	return 0;
}

static int firesat_ca_get_msg(struct firesat *firesat, void *arg)
{
	int err;
	ANTENNA_INPUT_INFO info;

	switch (firesat->ca_last_command) {
	case TAG_APP_INFO_ENQUIRY:
		err = firesat_ca_app_info(firesat, arg);
		break;
	case TAG_CA_INFO_ENQUIRY:
		err = firesat_ca_info(firesat, arg);
		break;
	default:
		if (AVCTunerStatus(firesat, &info))
			err = -EFAULT;
		else if (info.CaMmi == 1) {
			err = firesat_ca_get_mmi(firesat, arg);
		}
		else {
			printk(KERN_INFO "%s: Unhandled message 0x%08X\n",
			       __func__, firesat->ca_last_command);
			err = -EFAULT;
		}
	}
	firesat->ca_last_command = 0;
	return err;
}

static int firesat_ca_pmt(struct firesat *firesat, void *arg)
{
	struct ca_msg *msg_p = (struct ca_msg*)arg;
	int data_pos;

	if (msg_p->msg[3] & 0x80)
		data_pos = (msg_p->msg[4] && 0x7F) + 4;
	else
		data_pos = 4;
	if (avc_ca_pmt(firesat, &msg_p->msg[data_pos],
		       msg_p->length - data_pos))
		return -EFAULT;
	return 0;
}

static int firesat_ca_send_msg(struct firesat *firesat, void *arg)
{
	int err;
	struct ca_msg *msg_p = (struct ca_msg*)arg;

	// Do we need a semaphore for this?
	firesat->ca_last_command =
		(msg_p->msg[0] << 16) + (msg_p->msg[1] << 8) + msg_p->msg[2];
	switch (firesat->ca_last_command) {
	case TAG_CA_PMT:
		if (ca_debug != 0)
			printk(KERN_INFO "%s: Message received: TAG_CA_PMT\n",
			       __func__);
		err = firesat_ca_pmt(firesat, arg);
		break;
	case TAG_APP_INFO_ENQUIRY:
		// This is all handled in ca_get_msg
		if (ca_debug != 0)
			printk(KERN_INFO "%s: Message received: "
			       "TAG_APP_INFO_ENQUIRY\n", __func__);
		err = 0;
		break;
	case TAG_CA_INFO_ENQUIRY:
		// This is all handled in ca_get_msg
		if (ca_debug != 0)
			printk(KERN_INFO "%s: Message received: "
			       "TAG_CA_APP_INFO_ENQUIRY\n", __func__);
		err = 0;
		break;
	case TAG_ENTER_MENU:
		if (ca_debug != 0)
			printk(KERN_INFO "%s: Entering CA menu.\n", __func__);
		err = avc_ca_enter_menu(firesat);
		break;
	default:
		printk(KERN_ERR "%s: Unhandled unknown message 0x%08X\n",
		       __func__, firesat->ca_last_command);
		err = -EFAULT;
	}
	return err;
}

static int firesat_ca_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	struct dvb_device* dvbdev = (struct dvb_device*) file->private_data;
	struct firesat *firesat = dvbdev->priv;
	int err;
	ANTENNA_INPUT_INFO info;

	switch(cmd) {
	case CA_RESET:
		err = firesat_ca_reset(firesat);
		break;
	case CA_GET_CAP:
		err = firesat_ca_get_caps(firesat, arg);
		break;
	case CA_GET_SLOT_INFO:
		err = firesat_ca_get_slot_info(firesat, arg);
		break;
	case CA_GET_MSG:
		err = firesat_ca_get_msg(firesat, arg);
		break;
	case CA_SEND_MSG:
		err = firesat_ca_send_msg(firesat, arg);
		break;
	default:
		printk(KERN_INFO "%s: Unhandled ioctl, command: %u\n",__func__,
		       cmd);
		err = -EOPNOTSUPP;
	}

	if (AVCTunerStatus(firesat, &info))
		return err;

	firesat_ca_ready(&info);

	return err;
}

static int firesat_get_date_time_request(struct firesat *firesat)
{
	if (ca_debug)
		printk(KERN_INFO "%s: Retrieving Time/Date request\n",
		       __func__);
	if (avc_ca_get_time_date(firesat, &firesat->ca_time_interval))
		return -EFAULT;
	if (ca_debug)
		printk(KERN_INFO "%s: Time/Date interval is %d\n",
		       __func__, firesat->ca_time_interval);

	return 0;
}

static int firesat_ca_io_open(struct inode *inode, struct file *file)
{
	if (ca_debug != 0)
		printk(KERN_INFO "%s\n",__func__);
	return dvb_generic_open(inode, file);
}

static int firesat_ca_io_release(struct inode *inode, struct file *file)
{
	if (ca_debug != 0)
		printk(KERN_INFO "%s\n",__func__);
	return dvb_generic_release(inode, file);
}

static unsigned int firesat_ca_io_poll(struct file *file, poll_table *wait)
{
	if (ca_debug != 0)
		printk(KERN_INFO "%s\n",__func__);
	return POLLIN;
}

static struct file_operations firesat_ca_fops = {
	.owner = THIS_MODULE,
	.read = NULL, // There is no low level read anymore
	.write = NULL, // There is no low level write anymore
	.ioctl = dvb_generic_ioctl,
	.open = firesat_ca_io_open,
	.release = firesat_ca_io_release,
	.poll = firesat_ca_io_poll,
};

static struct dvb_device firesat_ca = {
	.priv = NULL,
	.users = 1,
	.readers = 1,
	.writers = 1,
	.fops = &firesat_ca_fops,
	.kernel_ioctl = firesat_ca_ioctl,
};

int firesat_ca_init(struct firesat *firesat)
{
	int err;
	ANTENNA_INPUT_INFO info;

	if (AVCTunerStatus(firesat, &info))
		return -EINVAL;

	if (firesat_ca_ready(&info)) {
		err = dvb_register_device(firesat->adapter,
					      &firesat->cadev,
					      &firesat_ca, firesat,
					      DVB_DEVICE_CA);

		if (info.CaApplicationInfo == 0)
			printk(KERN_ERR "%s: CaApplicationInfo is not set.\n",
			       __func__);
		if (info.CaDateTimeRequest == 1)
			firesat_get_date_time_request(firesat);
	}
	else
		err = -EFAULT;

	return err;
}

void firesat_ca_release(struct firesat *firesat)
{
	if (firesat->cadev)
	dvb_unregister_device(firesat->cadev);
}
