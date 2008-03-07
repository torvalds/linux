#include "firesat-ci.h"
#include "firesat.h"
#include "avc_api.h"

#include <linux/dvb/ca.h>
#include <dvbdev.h>
/*
static int firesat_ca_do_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *parg) {
	//struct firesat *firesat = (struct firesat*)((struct dvb_device*)file->private_data)->priv;
	int err;

//	printk(KERN_INFO "%s: ioctl %d\n",__func__,cmd);

	switch(cmd) {
	case CA_RESET:
		// TODO: Needs to be implemented with new AVC Vendor commands
		break;
	case CA_GET_CAP: {
		ca_caps_t *cap=(ca_caps_t*)parg;
		cap->slot_num = 1;
		cap->slot_type = CA_CI_LINK;
		cap->descr_num = 1;
		cap->descr_type = CA_DSS;

		err = 0;
		break;
	}
	case CA_GET_SLOT_INFO: {
		ca_slot_info_t *slot=(ca_slot_info_t*)parg;
		if(slot->num == 0) {
			slot->type = CA_CI | CA_CI_LINK | CA_DESCR;
			slot->flags = CA_CI_MODULE_PRESENT | CA_CI_MODULE_READY;
		} else {
			slot->type = 0;
			slot->flags = 0;
		}
		err = 0;
		break;
	}
	default:
			err=-EINVAL;
	}
	return err;
}
*/

static int firesat_ca_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg) {
	//return dvb_usercopy(inode, file, cmd, arg, firesat_ca_do_ioctl);
	return dvb_generic_ioctl(inode, file, cmd, arg);
}

static int firesat_ca_io_open(struct inode *inode, struct file *file) {
	printk(KERN_INFO "%s!\n",__func__);
	return dvb_generic_open(inode, file);
}

static int firesat_ca_io_release(struct inode *inode, struct file *file) {
	printk(KERN_INFO "%s!\n",__func__);
	return dvb_generic_release(inode, file);
}

static unsigned int firesat_ca_io_poll(struct file *file, poll_table *wait) {
//	printk(KERN_INFO "%s!\n",__func__);
	return POLLIN;
}

static struct file_operations firesat_ca_fops = {
	.owner = THIS_MODULE,
	.read = NULL, // There is no low level read anymore
	.write = NULL, // There is no low level write anymore
	.ioctl = firesat_ca_ioctl,
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
};

int firesat_ca_init(struct firesat *firesat) {
	int ret = dvb_register_device(firesat->adapter, &firesat->cadev, &firesat_ca, firesat, DVB_DEVICE_CA);
	if(ret) return ret;

	// avoid unnecessary delays, we're not talking to the CI yet anyways
	return 0;
}

void firesat_ca_release(struct firesat *firesat) {
	dvb_unregister_device(firesat->cadev);
}
