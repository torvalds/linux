/*
 * Copyright (C) 2005 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : memain.h
 * Author      : GG (Guenter Gebhardt)  <g.gebhardt@meilhaus.de>
 */

#ifndef _MEMAIN_H_
#define _MEMAIN_H_

#include "meinternal.h"

#include "meids.h"
#include "medebug.h"

#include "medevice.h"
/*#include "me1000_device.h"
#include "me1400_device.h"
#include "me1600_device.h"*/
#include "me4600_device.h"
/*#include "me6000_device.h"
#include "me0600_device.h"
#include "me8100_device.h"
#include "me8200_device.h"
#include "me0900_device.h"*/
#include "medummy.h"

#ifdef __KERNEL__

/*=============================================================================
  PCI device table.
  This is used by modprobe to translate PCI IDs to drivers.
  ===========================================================================*/

static struct pci_device_id me_pci_table[] __devinitdata = {
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000_A, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000_B, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1400, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140A, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140B, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14E0, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14EA, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14EB, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140C, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140D, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_4U, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_8U, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_12U, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_16U, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4610, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4650, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660I, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670I, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670S, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670IS, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680I, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680S, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680IS, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6004, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6008, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME600F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6014, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6018, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME601F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6034, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6038, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME603F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6104, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6108, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME610F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6114, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6118, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME611F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6134, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6138, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME613F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6044, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6048, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME604F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6054, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6058, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME605F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6074, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6078, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME607F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6144, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6148, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME614F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6154, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6158, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME615F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6174, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6178, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME617F, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6259, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6359, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0630, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8100_A, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8100_B, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8200_A, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8200_B, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0940, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0950, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0960, PCI_ANY_ID,
	 PCI_ANY_ID, 0, 0, 0},

	{0}
};

MODULE_DEVICE_TABLE(pci, me_pci_table);

/*=============================================================================
  USB device table.
  This is used by modprobe to translate USB IDs to drivers.
  ===========================================================================*/
/*
static struct usb_device_id me_usb_table[] __devinitdata = {
	{ USB_DEVICE(USB_VENDOR_ID_MEPHISTO_S1, USB_DEVICE_ID_MEPHISTO_S1) },
	{ 0 }
};

MODULE_DEVICE_TABLE (usb, me_usb_table);
*/

/*=============================================================================
  Templates
  ===========================================================================*/

#define ME_LOCK_MULTIPLEX_TEMPLATE(NAME, TYPE, CALL, DEV_CALL, ARGS)	\
static int CALL(struct file *filep, TYPE *arg){	\
	int err = 0; \
	int k = 0; \
	struct list_head *pos; \
	me_device_t *device; \
	TYPE karg; \
	\
	PDEBUG("executed.\n"); \
	\
	err = copy_from_user(&karg, arg, sizeof(TYPE)); \
	if(err){ \
		PERROR("Can't copy arguments to kernel space\n"); \
		return -EFAULT; \
	} \
	\
	down_read(&me_rwsem);	\
	\
	list_for_each(pos, &me_device_list){	\
		if(k == karg.device){	\
			device = list_entry(pos, me_device_t, list);	\
				break;	\
		}	\
		k++;	\
	}	\
	\
	if(pos == &me_device_list){ \
		PERROR("Invalid device number specified\n"); \
		karg.errno = ME_ERRNO_INVALID_DEVICE; \
	} \
	else{ \
		spin_lock(&me_lock);	\
		if((me_filep != NULL) && (me_filep != filep)){	\
			spin_unlock(&me_lock);	\
			PERROR("Resource is locked by another process\n");	\
			if(karg.lock == ME_LOCK_SET)	\
				karg.errno = ME_ERRNO_LOCKED;	\
			else if(karg.lock == ME_LOCK_RELEASE)	\
				karg.errno = ME_ERRNO_SUCCESS;	\
			else{	\
				PERROR("Invalid lock specified\n");	\
				karg.errno = ME_ERRNO_INVALID_LOCK;	\
			}\
		}	\
		else {	\
			me_count++;	\
			spin_unlock(&me_lock);	\
			\
			karg.errno = device->DEV_CALL ARGS;	\
			\
			spin_lock(&me_lock);	\
			me_count--;	\
			spin_unlock(&me_lock);	\
		}	\
	} \
	\
	up_read(&me_rwsem);	\
	\
	err = copy_to_user(arg, &karg, sizeof(TYPE)); \
	if(err){ \
		PERROR("Can't copy arguments back to user space\n"); \
		return -EFAULT; \
	} \
	\
	return ME_ERRNO_SUCCESS; \
}

#define ME_IO_MULTIPLEX_TEMPLATE(NAME, TYPE, CALL, DEV_CALL, ARGS)	\
static int CALL(struct file *filep, TYPE *arg){	\
	int err = 0; \
	int k = 0; \
	struct list_head *pos; \
	me_device_t *device; \
	TYPE karg; \
	\
	PDEBUG("executed.\n"); \
	\
	err = copy_from_user(&karg, arg, sizeof(TYPE)); \
	if(err){ \
		PERROR("Can't copy arguments to kernel space\n"); \
		return -EFAULT; \
	} \
	\
	down_read(&me_rwsem);	\
	\
	list_for_each(pos, &me_device_list){	\
		if(k == karg.device){	\
			device = list_entry(pos, me_device_t, list);	\
				break;	\
		}	\
		k++;	\
	}	\
	\
	if(pos == &me_device_list){ \
		PERROR("Invalid device number specified\n"); \
		karg.errno = ME_ERRNO_INVALID_DEVICE; \
	} \
	else{ \
		spin_lock(&me_lock);	\
		if((me_filep != NULL) && (me_filep != filep)){	\
			spin_unlock(&me_lock);	\
			PERROR("Resource is locked by another process\n");	\
			karg.errno = ME_ERRNO_LOCKED;	\
		}	\
		else {	\
			me_count++;	\
			spin_unlock(&me_lock);	\
			\
			karg.errno = device->DEV_CALL ARGS;	\
			\
			spin_lock(&me_lock);	\
			me_count--;	\
			spin_unlock(&me_lock);	\
		}	\
	} \
	\
	up_read(&me_rwsem);	\
	\
	err = copy_to_user(arg, &karg, sizeof(TYPE)); \
	if(err){ \
		PERROR("Can't copy arguments back to user space\n"); \
		return -EFAULT; \
	} \
 \
	return ME_ERRNO_SUCCESS; \
}

#define ME_QUERY_MULTIPLEX_STR_TEMPLATE(NAME, TYPE, CALL, DEV_CALL, ARGS)	\
static int CALL(struct file *filep, TYPE *arg){	\
	int err = 0;	\
	int k = 0;	\
	struct list_head *pos;	\
	me_device_t *device;	\
	char *msg = NULL;	\
	TYPE karg;	\
	\
	PDEBUG("executed.\n"); \
	\
	err = copy_from_user(&karg, arg, sizeof(TYPE));	\
	if(err){	\
		PERROR("Can't copy arguments to kernel space\n");	\
		return -EFAULT;	\
	}	\
	\
	down_read(&me_rwsem);	\
	\
	list_for_each(pos, &me_device_list){	\
		if(k == karg.device){	\
			device = list_entry(pos, me_device_t, list);	\
				break;	\
		}	\
		k++;	\
	}	\
	\
	if(pos == &me_device_list){	\
		PERROR("Invalid device number specified\n");	\
		karg.errno = ME_ERRNO_INVALID_DEVICE;	\
	}	\
	else{	\
		karg.errno = device->DEV_CALL ARGS;	\
		if(!karg.errno){	\
			if((strlen(msg) + 1) > karg.count){	\
				PERROR("User buffer for device name is to little\n");	\
				karg.errno = ME_ERRNO_USER_BUFFER_SIZE;	\
			}	\
			else{	\
				err = copy_to_user(karg.name, msg, strlen(msg) + 1);	\
				if(err){	\
					PERROR("Can't copy device name to user space\n");	\
					return -EFAULT;	\
				}	\
			}	\
		}	\
	}	\
	\
	up_read(&me_rwsem);	\
	\
	err = copy_to_user(arg, &karg, sizeof(TYPE));	\
	if(err){	\
		PERROR("Can't copy query back to user space\n");	\
		return -EFAULT;	\
	}	\
	\
	return ME_ERRNO_SUCCESS;	\
}

#define ME_QUERY_MULTIPLEX_TEMPLATE(NAME, TYPE, CALL, DEV_CALL, ARGS)	\
static int CALL(struct file *filep, TYPE *arg){	\
	int err = 0;	\
	int k = 0;	\
	struct list_head *pos;	\
	me_device_t *device;	\
	TYPE karg;	\
		\
	PDEBUG("executed.\n"); \
	 \
	err = copy_from_user(&karg, arg, sizeof(TYPE));	\
	if(err){	\
		PERROR("Can't copy arguments from user space\n");	\
		return -EFAULT;	\
	}	\
	\
	down_read(&me_rwsem);	\
	\
	list_for_each(pos, &me_device_list){	\
		if(k == karg.device){	\
			device = list_entry(pos, me_device_t, list);	\
			break;	\
		}	\
		k++;	\
	}	\
		\
	if(pos == &me_device_list){	\
		PERROR("Invalid device number specified\n");	\
		karg.errno = ME_ERRNO_INVALID_DEVICE;	\
	}	\
	else{	\
		karg.errno = device->DEV_CALL ARGS;	\
	}	\
	\
	up_read(&me_rwsem);	\
	\
	err = copy_to_user(arg, &karg, sizeof(TYPE));	\
	if(err){	\
		PERROR("Can't copy arguments to user space\n");	\
		return -EFAULT;	\
	}	\
		\
	return ME_ERRNO_SUCCESS;	\
}

#endif //__KERNEL__
#endif
