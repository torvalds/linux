/*
 * AGPGART driver frontend compatibility ioctls
 * Copyright (C) 2004 Silicon Graphics, Inc.
 * Copyright (C) 2002-2003 Dave Jones
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/agpgart.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "agp.h"
#include "compat_ioctl.h"

static int compat_agpioc_info_wrap(struct agp_file_private *priv, void __user *arg)
{
	struct agp_info32 userinfo;
	struct agp_kern_info kerninfo;

	agp_copy_info(agp_bridge, &kerninfo);

	userinfo.version.major = kerninfo.version.major;
	userinfo.version.minor = kerninfo.version.minor;
	userinfo.bridge_id = kerninfo.device->vendor |
	    (kerninfo.device->device << 16);
	userinfo.agp_mode = kerninfo.mode;
	userinfo.aper_base = (compat_long_t)kerninfo.aper_base;
	userinfo.aper_size = kerninfo.aper_size;
	userinfo.pg_total = userinfo.pg_system = kerninfo.max_memory;
	userinfo.pg_used = kerninfo.current_memory;

	if (copy_to_user(arg, &userinfo, sizeof(userinfo)))
		return -EFAULT;

	return 0;
}

static int compat_agpioc_reserve_wrap(struct agp_file_private *priv, void __user *arg)
{
	struct agp_region32 ureserve;
	struct agp_region kreserve;
	struct agp_client *client;
	struct agp_file_private *client_priv;

	DBG("");
	if (copy_from_user(&ureserve, arg, sizeof(ureserve)))
		return -EFAULT;

	if ((unsigned) ureserve.seg_count >= ~0U/sizeof(struct agp_segment32))
		return -EFAULT;

	kreserve.pid = ureserve.pid;
	kreserve.seg_count = ureserve.seg_count;

	client = agp_find_client_by_pid(kreserve.pid);

	if (kreserve.seg_count == 0) {
		/* remove a client */
		client_priv = agp_find_private(kreserve.pid);

		if (client_priv != NULL) {
			set_bit(AGP_FF_IS_CLIENT, &client_priv->access_flags);
			set_bit(AGP_FF_IS_VALID, &client_priv->access_flags);
		}
		if (client == NULL) {
			/* client is already removed */
			return 0;
		}
		return agp_remove_client(kreserve.pid);
	} else {
		struct agp_segment32 *usegment;
		struct agp_segment *ksegment;
		int seg;

		if (ureserve.seg_count >= 16384)
			return -EINVAL;

		usegment = kmalloc(sizeof(*usegment) * ureserve.seg_count, GFP_KERNEL);
		if (!usegment)
			return -ENOMEM;

		ksegment = kmalloc(sizeof(*ksegment) * kreserve.seg_count, GFP_KERNEL);
		if (!ksegment) {
			kfree(usegment);
			return -ENOMEM;
		}

		if (copy_from_user(usegment, (void __user *) ureserve.seg_list,
				   sizeof(*usegment) * ureserve.seg_count)) {
			kfree(usegment);
			kfree(ksegment);
			return -EFAULT;
		}

		for (seg = 0; seg < ureserve.seg_count; seg++) {
			ksegment[seg].pg_start = usegment[seg].pg_start;
			ksegment[seg].pg_count = usegment[seg].pg_count;
			ksegment[seg].prot = usegment[seg].prot;
		}

		kfree(usegment);
		kreserve.seg_list = ksegment;

		if (client == NULL) {
			/* Create the client and add the segment */
			client = agp_create_client(kreserve.pid);

			if (client == NULL) {
				kfree(ksegment);
				return -ENOMEM;
			}
			client_priv = agp_find_private(kreserve.pid);

			if (client_priv != NULL) {
				set_bit(AGP_FF_IS_CLIENT, &client_priv->access_flags);
				set_bit(AGP_FF_IS_VALID, &client_priv->access_flags);
			}
		}
		return agp_create_segment(client, &kreserve);
	}
	/* Will never really happen */
	return -EINVAL;
}

static int compat_agpioc_allocate_wrap(struct agp_file_private *priv, void __user *arg)
{
	struct agp_memory *memory;
	struct agp_allocate32 alloc;

	DBG("");
	if (copy_from_user(&alloc, arg, sizeof(alloc)))
		return -EFAULT;

	memory = agp_allocate_memory_wrap(alloc.pg_count, alloc.type);

	if (memory == NULL)
		return -ENOMEM;

	alloc.key = memory->key;
	alloc.physical = memory->physical;

	if (copy_to_user(arg, &alloc, sizeof(alloc))) {
		agp_free_memory_wrap(memory);
		return -EFAULT;
	}
	return 0;
}

static int compat_agpioc_bind_wrap(struct agp_file_private *priv, void __user *arg)
{
	struct agp_bind32 bind_info;
	struct agp_memory *memory;

	DBG("");
	if (copy_from_user(&bind_info, arg, sizeof(bind_info)))
		return -EFAULT;

	memory = agp_find_mem_by_key(bind_info.key);

	if (memory == NULL)
		return -EINVAL;

	return agp_bind_memory(memory, bind_info.pg_start);
}

static int compat_agpioc_unbind_wrap(struct agp_file_private *priv, void __user *arg)
{
	struct agp_memory *memory;
	struct agp_unbind32 unbind;

	DBG("");
	if (copy_from_user(&unbind, arg, sizeof(unbind)))
		return -EFAULT;

	memory = agp_find_mem_by_key(unbind.key);

	if (memory == NULL)
		return -EINVAL;

	return agp_unbind_memory(memory);
}

long compat_agp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct agp_file_private *curr_priv = file->private_data;
	int ret_val = -ENOTTY;

	mutex_lock(&(agp_fe.agp_mutex));

	if ((agp_fe.current_controller == NULL) &&
	    (cmd != AGPIOC_ACQUIRE32)) {
		ret_val = -EINVAL;
		goto ioctl_out;
	}
	if ((agp_fe.backend_acquired != true) &&
	    (cmd != AGPIOC_ACQUIRE32)) {
		ret_val = -EBUSY;
		goto ioctl_out;
	}
	if (cmd != AGPIOC_ACQUIRE32) {
		if (!(test_bit(AGP_FF_IS_CONTROLLER, &curr_priv->access_flags))) {
			ret_val = -EPERM;
			goto ioctl_out;
		}
		/* Use the original pid of the controller,
		 * in case it's threaded */

		if (agp_fe.current_controller->pid != curr_priv->my_pid) {
			ret_val = -EBUSY;
			goto ioctl_out;
		}
	}

	switch (cmd) {
	case AGPIOC_INFO32:
		ret_val = compat_agpioc_info_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_ACQUIRE32:
		ret_val = agpioc_acquire_wrap(curr_priv);
		break;

	case AGPIOC_RELEASE32:
		ret_val = agpioc_release_wrap(curr_priv);
		break;

	case AGPIOC_SETUP32:
		ret_val = agpioc_setup_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_RESERVE32:
		ret_val = compat_agpioc_reserve_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_PROTECT32:
		ret_val = agpioc_protect_wrap(curr_priv);
		break;

	case AGPIOC_ALLOCATE32:
		ret_val = compat_agpioc_allocate_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_DEALLOCATE32:
		ret_val = agpioc_deallocate_wrap(curr_priv, (int) arg);
		break;

	case AGPIOC_BIND32:
		ret_val = compat_agpioc_bind_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_UNBIND32:
		ret_val = compat_agpioc_unbind_wrap(curr_priv, (void __user *) arg);
		break;

	case AGPIOC_CHIPSET_FLUSH32:
		break;
	}

ioctl_out:
	DBG("ioctl returns %d\n", ret_val);
	mutex_unlock(&(agp_fe.agp_mutex));
	return ret_val;
}

