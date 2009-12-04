/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "mantis_common.h"
#include "mantis_link.h"
#include "mantis_hif.h"


static int mantis_ca_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long parg)
{
	return 0;
}

static int mantis_ca_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mantis_ca_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t mantis_ca_read(struct file *file, char __user *buffer, size_t count, loff_t *ofset)
{
	struct dvb_device *dvbdev	= file->private_data;
	struct mantis_ca *ca		= dvbdev->priv;

	int status;

	return 0;
error:
	return status;
}

static ssize_t mantis_ca_write(struct file *file, const char __user *buffer, size_t count, loff_t *offset)
{
	struct dvb_device *dvbdev	= file->private_data;
	struct mantis_ca *ca		= dvbdev->priv;

	int status;

	return 0;
error:
	return status;
}

static struct file_operations mantis_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= mantis_ca_ioctl,
	.open		= mantis_ca_open,
	.release	= mantis_ca_release,
	.read		= mantis_ca_read,
	.write		= mantis_ca_write,
};

static struct dvb_device mantis_ca = {
	.priv		= NULL,
	.users		= 1,
	.readers	= 1,
	.writers	= 1,
	.fops		= &mantis_fops,
};

struct dvb_device *mantis_ca_init(struct mantis_pci *mantis)
{
	int ret;

	struct dvb_device *dvbdev;
	struct dvb_adapter *dvb_adapter = &mantis->dvb_adapter;
	struct mantis_ca *ca;

	if (!(ca = kzalloc(sizeof (struct mantis_ca), GFP_KERNEL))) {
		dprintk(verbose, MANTIS_ERROR, 1, "Out of memory!, exiting ..");
		return NULL;
	}

	ca->ca_priv = mantis;
	mantis->mantis_ca = ca;
	mantis_evmgr_init(ca);

	dprintk(verbose, MANTIS_ERROR, 0, "CA: Registering Mantis Adapter(%d) Slot(0)\n", mantis->num);
	if (dvb_register_device(dvb_adapter, &dvbdev, &mantis_ca, ca, DVB_DEVICE_CA) == 0) {
		ca->ca_dev = dvbdev;
		return ca->ca_dev;
	}
	return 0;

error:
	if (ca != NULL) {
		dprintk(verbose, MANTIS_ERROR, 1, "Error ..");
		if (ca->ca_dev != NULL)
			dvb_unregister_device(ca->ca_dev);

		kfree(ca);
	}
	return NULL;
}

void mantis_ca_exit(struct mantis_pci *mantis)
{
	struct mantis_ca *ca = mantis->mantis_ca;

	mantis_evmgr_exit(ca);
	dprintk(verbose, MANTIS_ERROR, 0, "CA: Unregister Mantis Adapter(%d) Slot(0)\n", mantis->num);
	if (ca->ca_dev)
		dvb_unregister_device(ca->ca_dev);

	kfree(ca);
}
