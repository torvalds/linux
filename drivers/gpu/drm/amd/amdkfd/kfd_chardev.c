/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <uapi/linux/kfd_ioctl.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <uapi/asm-generic/mman-common.h>
#include <asm/processor.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"

static long kfd_ioctl(struct file *, unsigned int, unsigned long);
static int kfd_open(struct inode *, struct file *);
static int kfd_mmap(struct file *, struct vm_area_struct *);

static const char kfd_dev_name[] = "kfd";

static const struct file_operations kfd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kfd_ioctl,
	.compat_ioctl = kfd_ioctl,
	.open = kfd_open,
	.mmap = kfd_mmap,
};

static int kfd_char_dev_major = -1;
static struct class *kfd_class;
struct device *kfd_device;

int kfd_chardev_init(void)
{
	int err = 0;

	kfd_char_dev_major = register_chrdev(0, kfd_dev_name, &kfd_fops);
	err = kfd_char_dev_major;
	if (err < 0)
		goto err_register_chrdev;

	kfd_class = class_create(THIS_MODULE, kfd_dev_name);
	err = PTR_ERR(kfd_class);
	if (IS_ERR(kfd_class))
		goto err_class_create;

	kfd_device = device_create(kfd_class, NULL,
					MKDEV(kfd_char_dev_major, 0),
					NULL, kfd_dev_name);
	err = PTR_ERR(kfd_device);
	if (IS_ERR(kfd_device))
		goto err_device_create;

	return 0;

err_device_create:
	class_destroy(kfd_class);
err_class_create:
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
err_register_chrdev:
	return err;
}

void kfd_chardev_exit(void)
{
	device_destroy(kfd_class, MKDEV(kfd_char_dev_major, 0));
	class_destroy(kfd_class);
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
}

struct device *kfd_chardev(void)
{
	return kfd_device;
}


static int kfd_open(struct inode *inode, struct file *filep)
{
	struct kfd_process *process;

	if (iminor(inode) != 0)
		return -ENODEV;

	process = kfd_create_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	process->is_32bit_user_mode = is_compat_task();

	dev_dbg(kfd_device, "process %d opened, compat mode (32 bit) - %d\n",
		process->pasid, process->is_32bit_user_mode);

	kfd_init_apertures(process);

	return 0;
}

static long kfd_ioctl_get_version(struct file *filep, struct kfd_process *p,
					void __user *arg)
{
	return -ENODEV;
}

static int set_queue_properties_from_user(struct queue_properties *q_properties,
				struct kfd_ioctl_create_queue_args *args)
{
	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("kfd: queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("kfd: queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok(VERIFY_WRITE, args->ring_base_address, sizeof(uint64_t)))) {
		pr_err("kfd: can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("kfd: ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	if (!access_ok(VERIFY_WRITE, args->read_pointer_address, sizeof(uint32_t))) {
		pr_err("kfd: can't access read pointer\n");
		return -EFAULT;
	}

	if (!access_ok(VERIFY_WRITE, args->write_pointer_address, sizeof(uint32_t))) {
		pr_err("kfd: can't access write pointer\n");
		return -EFAULT;
	}

	q_properties->is_interop = false;
	q_properties->queue_percent = args->queue_percentage;
	q_properties->priority = args->queue_priority;
	q_properties->queue_address = args->ring_base_address;
	q_properties->queue_size = args->ring_size;
	q_properties->read_ptr = (uint32_t *) args->read_pointer_address;
	q_properties->write_ptr = (uint32_t *) args->write_pointer_address;
	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE ||
		args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->type = KFD_QUEUE_TYPE_COMPUTE;
	else
		return -ENOTSUPP;

	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->format = KFD_QUEUE_FORMAT_AQL;
	else
		q_properties->format = KFD_QUEUE_FORMAT_PM4;

	pr_debug("Queue Percentage (%d, %d)\n",
			q_properties->queue_percent, args->queue_percentage);

	pr_debug("Queue Priority (%d, %d)\n",
			q_properties->priority, args->queue_priority);

	pr_debug("Queue Address (0x%llX, 0x%llX)\n",
			q_properties->queue_address, args->ring_base_address);

	pr_debug("Queue Size (0x%llX, %u)\n",
			q_properties->queue_size, args->ring_size);

	pr_debug("Queue r/w Pointers (0x%llX, 0x%llX)\n",
			(uint64_t) q_properties->read_ptr,
			(uint64_t) q_properties->write_ptr);

	pr_debug("Queue Format (%d)\n", q_properties->format);

	return 0;
}

static long kfd_ioctl_create_queue(struct file *filep, struct kfd_process *p,
					void __user *arg)
{
	struct kfd_ioctl_create_queue_args args;
	struct kfd_dev *dev;
	int err = 0;
	unsigned int queue_id;
	struct kfd_process_device *pdd;
	struct queue_properties q_properties;

	memset(&q_properties, 0, sizeof(struct queue_properties));

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	pr_debug("kfd: creating queue ioctl\n");

	err = set_queue_properties_from_user(&q_properties, &args);
	if (err)
		return err;

	dev = kfd_device_by_id(args.gpu_id);
	if (dev == NULL)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd) < 0) {
		err = PTR_ERR(pdd);
		goto err_bind_process;
	}

	pr_debug("kfd: creating queue for PASID %d on GPU 0x%x\n",
			p->pasid,
			dev->id);

	err = pqm_create_queue(&p->pqm, dev, filep, &q_properties, 0,
				KFD_QUEUE_TYPE_COMPUTE, &queue_id);
	if (err != 0)
		goto err_create_queue;

	args.queue_id = queue_id;

	/* Return gpu_id as doorbell offset for mmap usage */
	args.doorbell_offset = args.gpu_id << PAGE_SHIFT;

	if (copy_to_user(arg, &args, sizeof(args))) {
		err = -EFAULT;
		goto err_copy_args_out;
	}

	mutex_unlock(&p->mutex);

	pr_debug("kfd: queue id %d was created successfully\n", args.queue_id);

	pr_debug("ring buffer address == 0x%016llX\n",
			args.ring_base_address);

	pr_debug("read ptr address    == 0x%016llX\n",
			args.read_pointer_address);

	pr_debug("write ptr address   == 0x%016llX\n",
			args.write_pointer_address);

	return 0;

err_copy_args_out:
	pqm_destroy_queue(&p->pqm, queue_id);
err_create_queue:
err_bind_process:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_destroy_queue(struct file *filp, struct kfd_process *p,
					void __user *arg)
{
	int retval;
	struct kfd_ioctl_destroy_queue_args args;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	pr_debug("kfd: destroying queue id %d for PASID %d\n",
				args.queue_id,
				p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_destroy_queue(&p->pqm, args.queue_id);

	mutex_unlock(&p->mutex);
	return retval;
}

static int kfd_ioctl_update_queue(struct file *filp, struct kfd_process *p,
					void __user *arg)
{
	int retval;
	struct kfd_ioctl_update_queue_args args;
	struct queue_properties properties;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	if (args.queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("kfd: queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args.queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("kfd: queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args.ring_base_address) &&
		(!access_ok(VERIFY_WRITE, args.ring_base_address, sizeof(uint64_t)))) {
		pr_err("kfd: can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args.ring_size) && (args.ring_size != 0)) {
		pr_err("kfd: ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	properties.queue_address = args.ring_base_address;
	properties.queue_size = args.ring_size;
	properties.queue_percent = args.queue_percentage;
	properties.priority = args.queue_priority;

	pr_debug("kfd: updating queue id %d for PASID %d\n",
			args.queue_id, p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_update_queue(&p->pqm, args.queue_id, &properties);

	mutex_unlock(&p->mutex);

	return retval;
}

static long kfd_ioctl_set_memory_policy(struct file *filep,
				struct kfd_process *p, void __user *arg)
{
	struct kfd_ioctl_set_memory_policy_args args;
	struct kfd_dev *dev;
	int err = 0;
	struct kfd_process_device *pdd;
	enum cache_policy default_policy, alternate_policy;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	if (args.default_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args.default_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	if (args.alternate_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args.alternate_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	dev = kfd_device_by_id(args.gpu_id);
	if (dev == NULL)
		return -EINVAL;

	mutex_lock(&p->mutex);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd) < 0) {
		err = PTR_ERR(pdd);
		goto out;
	}

	default_policy = (args.default_policy == KFD_IOC_CACHE_POLICY_COHERENT)
			 ? cache_policy_coherent : cache_policy_noncoherent;

	alternate_policy =
		(args.alternate_policy == KFD_IOC_CACHE_POLICY_COHERENT)
		   ? cache_policy_coherent : cache_policy_noncoherent;

	if (!dev->dqm->set_cache_memory_policy(dev->dqm,
				&pdd->qpd,
				default_policy,
				alternate_policy,
				(void __user *)args.alternate_aperture_base,
				args.alternate_aperture_size))
		err = -EINVAL;

out:
	mutex_unlock(&p->mutex);

	return err;
}

static long kfd_ioctl_get_clock_counters(struct file *filep,
				struct kfd_process *p, void __user *arg)
{
	struct kfd_ioctl_get_clock_counters_args args;
	struct kfd_dev *dev;
	struct timespec time;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	dev = kfd_device_by_id(args.gpu_id);
	if (dev == NULL)
		return -EINVAL;

	/* Reading GPU clock counter from KGD */
	args.gpu_clock_counter = kfd2kgd->get_gpu_clock_counter(dev->kgd);

	/* No access to rdtsc. Using raw monotonic time */
	getrawmonotonic(&time);
	args.cpu_clock_counter = (uint64_t)timespec_to_ns(&time);

	get_monotonic_boottime(&time);
	args.system_clock_counter = (uint64_t)timespec_to_ns(&time);

	/* Since the counter is in nano-seconds we use 1GHz frequency */
	args.system_clock_freq = 1000000000;

	if (copy_to_user(arg, &args, sizeof(args)))
		return -EFAULT;

	return 0;
}


static int kfd_ioctl_get_process_apertures(struct file *filp,
				struct kfd_process *p, void __user *arg)
{
	return -ENODEV;
}

static long kfd_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct kfd_process *process;
	long err = -EINVAL;

	dev_dbg(kfd_device,
		"ioctl cmd 0x%x (#%d), arg 0x%lx\n",
		cmd, _IOC_NR(cmd), arg);

	process = kfd_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	switch (cmd) {
	case KFD_IOC_GET_VERSION:
		err = kfd_ioctl_get_version(filep, process, (void __user *)arg);
		break;
	case KFD_IOC_CREATE_QUEUE:
		err = kfd_ioctl_create_queue(filep, process,
						(void __user *)arg);
		break;

	case KFD_IOC_DESTROY_QUEUE:
		err = kfd_ioctl_destroy_queue(filep, process,
						(void __user *)arg);
		break;

	case KFD_IOC_SET_MEMORY_POLICY:
		err = kfd_ioctl_set_memory_policy(filep, process,
						(void __user *)arg);
		break;

	case KFD_IOC_GET_CLOCK_COUNTERS:
		err = kfd_ioctl_get_clock_counters(filep, process,
						(void __user *)arg);
		break;

	case KFD_IOC_GET_PROCESS_APERTURES:
		err = kfd_ioctl_get_process_apertures(filep, process,
						(void __user *)arg);
		break;

	case KFD_IOC_UPDATE_QUEUE:
		err = kfd_ioctl_update_queue(filep, process,
						(void __user *)arg);
		break;

	default:
		dev_err(kfd_device,
			"unknown ioctl cmd 0x%x, arg 0x%lx)\n",
			cmd, arg);
		err = -EINVAL;
		break;
	}

	if (err < 0)
		dev_err(kfd_device,
			"ioctl error %ld for ioctl cmd 0x%x (#%d)\n",
			err, cmd, _IOC_NR(cmd));

	return err;
}

static int kfd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kfd_process *process;

	process = kfd_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	return kfd_doorbell_mmap(process, vma);
}
