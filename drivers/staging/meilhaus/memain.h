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
