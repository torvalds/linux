/*
 * drivers/android/staging/vsoc.c
 *
 * Android Virtual System on a Chip (VSoC) driver
 *
 * Copyright (C) 2017 Google, Inc.
 *
 * Author: ghartman@google.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Based on drivers/char/kvm_ivshmem.c - driver for KVM Inter-VM shared memory
 *         Copyright 2009 Cam Macdonell <cam@cs.ualberta.ca>
 *
 * Based on cirrusfb.c and 8139cp.c:
 *   Copyright 1999-2001 Jeff Garzik
 *   Copyright 2001-2004 Jeff Garzik
 */

#include <linux/dma-mapping.h>
#include <linux/freezer.h>
#include <linux/futex.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include "uapi/vsoc_shm.h"

#define VSOC_DEV_NAME "vsoc"

/*
 * Description of the ivshmem-doorbell PCI device used by QEmu. These
 * constants follow docs/specs/ivshmem-spec.txt, which can be found in
 * the QEmu repository. This was last reconciled with the version that
 * came out with 2.8
 */

/*
 * These constants are determined KVM Inter-VM shared memory device
 * register offsets
 */
enum {
	INTR_MASK = 0x00,	/* Interrupt Mask */
	INTR_STATUS = 0x04,	/* Interrupt Status */
	IV_POSITION = 0x08,	/* VM ID */
	DOORBELL = 0x0c,	/* Doorbell */
};

static const int REGISTER_BAR;  /* Equal to 0 */
static const int MAX_REGISTER_BAR_LEN = 0x100;
/*
 * The MSI-x BAR is not used directly.
 *
 * static const int MSI_X_BAR = 1;
 */
static const int SHARED_MEMORY_BAR = 2;

struct vsoc_region_data {
	char name[VSOC_DEVICE_NAME_SZ + 1];
	wait_queue_head_t interrupt_wait_queue;
	/* TODO(b/73664181): Use multiple futex wait queues */
	wait_queue_head_t futex_wait_queue;
	/* Flag indicating that an interrupt has been signalled by the host. */
	atomic_t *incoming_signalled;
	/* Flag indicating the guest has signalled the host. */
	atomic_t *outgoing_signalled;
	int irq_requested;
	int device_created;
};

struct vsoc_device {
	/* Kernel virtual address of REGISTER_BAR. */
	void __iomem *regs;
	/* Physical address of SHARED_MEMORY_BAR. */
	phys_addr_t shm_phys_start;
	/* Kernel virtual address of SHARED_MEMORY_BAR. */
	void *kernel_mapped_shm;
	/* Size of the entire shared memory window in bytes. */
	size_t shm_size;
	/*
	 * Pointer to the virtual address of the shared memory layout structure.
	 * This is probably identical to kernel_mapped_shm, but saving this
	 * here saves a lot of annoying casts.
	 */
	struct vsoc_shm_layout_descriptor *layout;
	/*
	 * Points to a table of region descriptors in the kernel's virtual
	 * address space. Calculated from
	 * vsoc_shm_layout_descriptor.vsoc_region_desc_offset
	 */
	struct vsoc_device_region *regions;
	/* Head of a list of permissions that have been granted. */
	struct list_head permissions;
	struct pci_dev *dev;
	/* Per-region (and therefore per-interrupt) information. */
	struct vsoc_region_data *regions_data;
	/*
	 * Table of msi-x entries. This has to be separated from struct
	 * vsoc_region_data because the kernel deals with them as an array.
	 */
	struct msix_entry *msix_entries;
	/*
	 * Flags that indicate what we've initialzied. These are used to do an
	 * orderly cleanup of the device.
	 */
	char enabled_device;
	char requested_regions;
	char cdev_added;
	char class_added;
	char msix_enabled;
	/* Mutex that protectes the permission list */
	struct mutex mtx;
	/* Major number assigned by the kernel */
	int major;

	struct cdev cdev;
	struct class *class;
};

static struct vsoc_device vsoc_dev;

/*
 * TODO(ghartman): Add a /sys filesystem entry that summarizes the permissions.
 */

struct fd_scoped_permission_node {
	struct fd_scoped_permission permission;
	struct list_head list;
};

struct vsoc_private_data {
	struct fd_scoped_permission_node *fd_scoped_permission_node;
};

static long vsoc_ioctl(struct file *, unsigned int, unsigned long);
static int vsoc_mmap(struct file *, struct vm_area_struct *);
static int vsoc_open(struct inode *, struct file *);
static int vsoc_release(struct inode *, struct file *);
static ssize_t vsoc_read(struct file *, char *, size_t, loff_t *);
static ssize_t vsoc_write(struct file *, const char *, size_t, loff_t *);
static loff_t vsoc_lseek(struct file *filp, loff_t offset, int origin);
static int do_create_fd_scoped_permission(
	struct vsoc_device_region *region_p,
	struct fd_scoped_permission_node *np,
	struct fd_scoped_permission_arg *__user arg);
static void do_destroy_fd_scoped_permission(
	struct vsoc_device_region *owner_region_p,
	struct fd_scoped_permission *perm);
static long do_vsoc_describe_region(struct file *,
				    struct vsoc_device_region __user *);
static ssize_t vsoc_get_area(struct file *filp, __u32 *perm_off);

/**
 * Validate arguments on entry points to the driver.
 */
inline int vsoc_validate_inode(struct inode *inode)
{
	if (iminor(inode) >= vsoc_dev.layout->region_count) {
		dev_err(&vsoc_dev.dev->dev,
			"describe_region: invalid region %d\n", iminor(inode));
		return -ENODEV;
	}
	return 0;
}

inline int vsoc_validate_filep(struct file *filp)
{
	int ret = vsoc_validate_inode(file_inode(filp));

	if (ret)
		return ret;
	if (!filp->private_data) {
		dev_err(&vsoc_dev.dev->dev,
			"No private data on fd, region %d\n",
			iminor(file_inode(filp)));
		return -EBADFD;
	}
	return 0;
}

/* Converts from shared memory offset to virtual address */
static inline void *shm_off_to_virtual_addr(__u32 offset)
{
	return vsoc_dev.kernel_mapped_shm + offset;
}

/* Converts from shared memory offset to physical address */
static inline phys_addr_t shm_off_to_phys_addr(__u32 offset)
{
	return vsoc_dev.shm_phys_start + offset;
}

/**
 * Convenience functions to obtain the region from the inode or file.
 * Dangerous to call before validating the inode/file.
 */
static inline struct vsoc_device_region *vsoc_region_from_inode(
	struct inode *inode)
{
	return &vsoc_dev.regions[iminor(inode)];
}

static inline struct vsoc_device_region *vsoc_region_from_filep(
	struct file *inode)
{
	return vsoc_region_from_inode(file_inode(inode));
}

static inline uint32_t vsoc_device_region_size(struct vsoc_device_region *r)
{
	return r->region_end_offset - r->region_begin_offset;
}

static const struct file_operations vsoc_ops = {
	.owner = THIS_MODULE,
	.open = vsoc_open,
	.mmap = vsoc_mmap,
	.read = vsoc_read,
	.unlocked_ioctl = vsoc_ioctl,
	.compat_ioctl = vsoc_ioctl,
	.write = vsoc_write,
	.llseek = vsoc_lseek,
	.release = vsoc_release,
};

static struct pci_device_id vsoc_id_table[] = {
	{0x1af4, 0x1110, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0},
};

MODULE_DEVICE_TABLE(pci, vsoc_id_table);

static void vsoc_remove_device(struct pci_dev *pdev);
static int vsoc_probe_device(struct pci_dev *pdev,
			     const struct pci_device_id *ent);

static struct pci_driver vsoc_pci_driver = {
	.name = "vsoc",
	.id_table = vsoc_id_table,
	.probe = vsoc_probe_device,
	.remove = vsoc_remove_device,
};

static int do_create_fd_scoped_permission(
	struct vsoc_device_region *region_p,
	struct fd_scoped_permission_node *np,
	struct fd_scoped_permission_arg *__user arg)
{
	struct file *managed_filp;
	s32 managed_fd;
	atomic_t *owner_ptr = NULL;
	struct vsoc_device_region *managed_region_p;

	if (copy_from_user(&np->permission, &arg->perm, sizeof(*np)) ||
	    copy_from_user(&managed_fd,
			   &arg->managed_region_fd, sizeof(managed_fd))) {
		return -EFAULT;
	}
	managed_filp = fdget(managed_fd).file;
	/* Check that it's a valid fd, */
	if (!managed_filp || vsoc_validate_filep(managed_filp))
		return -EPERM;
	/* EEXIST if the given fd already has a permission. */
	if (((struct vsoc_private_data *)managed_filp->private_data)->
	    fd_scoped_permission_node)
		return -EEXIST;
	managed_region_p = vsoc_region_from_filep(managed_filp);
	/* Check that the provided region is managed by this one */
	if (&vsoc_dev.regions[managed_region_p->managed_by] != region_p)
		return -EPERM;
	/* The area must be well formed and have non-zero size */
	if (np->permission.begin_offset >= np->permission.end_offset)
		return -EINVAL;
	/* The area must fit in the memory window */
	if (np->permission.end_offset >
	    vsoc_device_region_size(managed_region_p))
		return -ERANGE;
	/* The area must be in the region data section */
	if (np->permission.begin_offset <
	    managed_region_p->offset_of_region_data)
		return -ERANGE;
	/* The area must be page aligned */
	if (!PAGE_ALIGNED(np->permission.begin_offset) ||
	    !PAGE_ALIGNED(np->permission.end_offset))
		return -EINVAL;
	/* Owner offset must be naturally aligned in the window */
	if (np->permission.owner_offset &
	    (sizeof(np->permission.owner_offset) - 1))
		return -EINVAL;
	/* The owner flag must reside in the owner memory */
	if (np->permission.owner_offset + sizeof(np->permission.owner_offset) >
	    vsoc_device_region_size(region_p))
		return -ERANGE;
	/* The owner flag must reside in the data section */
	if (np->permission.owner_offset < region_p->offset_of_region_data)
		return -EINVAL;
	/* The owner value must change to claim the memory */
	if (np->permission.owned_value == VSOC_REGION_FREE)
		return -EINVAL;
	owner_ptr =
	    (atomic_t *)shm_off_to_virtual_addr(region_p->region_begin_offset +
						np->permission.owner_offset);
	/* We've already verified that this is in the shared memory window, so
	 * it should be safe to write to this address.
	 */
	if (atomic_cmpxchg(owner_ptr,
			   VSOC_REGION_FREE,
			   np->permission.owned_value) != VSOC_REGION_FREE) {
		return -EBUSY;
	}
	((struct vsoc_private_data *)managed_filp->private_data)->
	    fd_scoped_permission_node = np;
	/* The file offset needs to be adjusted if the calling
	 * process did any read/write operations on the fd
	 * before creating the permission.
	 */
	if (managed_filp->f_pos) {
		if (managed_filp->f_pos > np->permission.end_offset) {
			/* If the offset is beyond the permission end, set it
			 * to the end.
			 */
			managed_filp->f_pos = np->permission.end_offset;
		} else {
			/* If the offset is within the permission interval
			 * keep it there otherwise reset it to zero.
			 */
			if (managed_filp->f_pos < np->permission.begin_offset) {
				managed_filp->f_pos = 0;
			} else {
				managed_filp->f_pos -=
				    np->permission.begin_offset;
			}
		}
	}
	return 0;
}

static void do_destroy_fd_scoped_permission_node(
	struct vsoc_device_region *owner_region_p,
	struct fd_scoped_permission_node *node)
{
	if (node) {
		do_destroy_fd_scoped_permission(owner_region_p,
						&node->permission);
		mutex_lock(&vsoc_dev.mtx);
		list_del(&node->list);
		mutex_unlock(&vsoc_dev.mtx);
		kfree(node);
	}
}

static void do_destroy_fd_scoped_permission(
		struct vsoc_device_region *owner_region_p,
		struct fd_scoped_permission *perm)
{
	atomic_t *owner_ptr = NULL;
	int prev = 0;

	if (!perm)
		return;
	owner_ptr = (atomic_t *)shm_off_to_virtual_addr(
		owner_region_p->region_begin_offset + perm->owner_offset);
	prev = atomic_xchg(owner_ptr, VSOC_REGION_FREE);
	if (prev != perm->owned_value)
		dev_err(&vsoc_dev.dev->dev,
			"%x-%x: owner (%s) %x: expected to be %x was %x",
			perm->begin_offset, perm->end_offset,
			owner_region_p->device_name, perm->owner_offset,
			perm->owned_value, prev);
}

static long do_vsoc_describe_region(struct file *filp,
				    struct vsoc_device_region __user *dest)
{
	struct vsoc_device_region *region_p;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	region_p = vsoc_region_from_filep(filp);
	if (copy_to_user(dest, region_p, sizeof(*region_p)))
		return -EFAULT;
	return 0;
}

/**
 * Implements the inner logic of cond_wait. Copies to and from userspace are
 * done in the helper function below.
 */
static int handle_vsoc_cond_wait(struct file *filp, struct vsoc_cond_wait *arg)
{
	DEFINE_WAIT(wait);
	u32 region_number = iminor(file_inode(filp));
	struct vsoc_region_data *data = vsoc_dev.regions_data + region_number;
	struct hrtimer_sleeper timeout, *to = NULL;
	int ret = 0;
	struct vsoc_device_region *region_p = vsoc_region_from_filep(filp);
	atomic_t *address = NULL;
	struct timespec ts;

	/* Ensure that the offset is aligned */
	if (arg->offset & (sizeof(uint32_t) - 1))
		return -EADDRNOTAVAIL;
	/* Ensure that the offset is within shared memory */
	if (((uint64_t)arg->offset) + region_p->region_begin_offset +
	    sizeof(uint32_t) > region_p->region_end_offset)
		return -E2BIG;
	address = shm_off_to_virtual_addr(region_p->region_begin_offset +
					  arg->offset);

	/* Ensure that the type of wait is valid */
	switch (arg->wait_type) {
	case VSOC_WAIT_IF_EQUAL:
		break;
	case VSOC_WAIT_IF_EQUAL_TIMEOUT:
		to = &timeout;
		break;
	default:
		return -EINVAL;
	}

	if (to) {
		/* Copy the user-supplied timesec into the kernel structure.
		 * We do things this way to flatten differences between 32 bit
		 * and 64 bit timespecs.
		 */
		ts.tv_sec = arg->wake_time_sec;
		ts.tv_nsec = arg->wake_time_nsec;

		if (!timespec_valid(&ts))
			return -EINVAL;
		hrtimer_init_on_stack(&to->timer, CLOCK_MONOTONIC,
				      HRTIMER_MODE_ABS);
		hrtimer_set_expires_range_ns(&to->timer, timespec_to_ktime(ts),
					     current->timer_slack_ns);

		hrtimer_init_sleeper(to, current);
	}

	while (1) {
		prepare_to_wait(&data->futex_wait_queue, &wait,
				TASK_INTERRUPTIBLE);
		/*
		 * Check the sentinel value after prepare_to_wait. If the value
		 * changes after this check the writer will call signal,
		 * changing the task state from INTERRUPTIBLE to RUNNING. That
		 * will ensure that schedule() will eventually schedule this
		 * task.
		 */
		if (atomic_read(address) != arg->value) {
			ret = 0;
			break;
		}
		if (to) {
			hrtimer_start_expires(&to->timer, HRTIMER_MODE_ABS);
			if (likely(to->task))
				freezable_schedule();
			hrtimer_cancel(&to->timer);
			if (!to->task) {
				ret = -ETIMEDOUT;
				break;
			}
		} else {
			freezable_schedule();
		}
		/* Count the number of times that we woke up. This is useful
		 * for unit testing.
		 */
		++arg->wakes;
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}
	finish_wait(&data->futex_wait_queue, &wait);
	if (to)
		destroy_hrtimer_on_stack(&to->timer);
	return ret;
}

/**
 * Handles the details of copying from/to userspace to ensure that the copies
 * happen on all of the return paths of cond_wait.
 */
static int do_vsoc_cond_wait(struct file *filp,
			     struct vsoc_cond_wait __user *untrusted_in)
{
	struct vsoc_cond_wait arg;
	int rval = 0;

	if (copy_from_user(&arg, untrusted_in, sizeof(arg)))
		return -EFAULT;
	/* wakes is an out parameter. Initialize it to something sensible. */
	arg.wakes = 0;
	rval = handle_vsoc_cond_wait(filp, &arg);
	if (copy_to_user(untrusted_in, &arg, sizeof(arg)))
		return -EFAULT;
	return rval;
}

static int do_vsoc_cond_wake(struct file *filp, uint32_t offset)
{
	struct vsoc_device_region *region_p = vsoc_region_from_filep(filp);
	u32 region_number = iminor(file_inode(filp));
	struct vsoc_region_data *data = vsoc_dev.regions_data + region_number;
	/* Ensure that the offset is aligned */
	if (offset & (sizeof(uint32_t) - 1))
		return -EADDRNOTAVAIL;
	/* Ensure that the offset is within shared memory */
	if (((uint64_t)offset) + region_p->region_begin_offset +
	    sizeof(uint32_t) > region_p->region_end_offset)
		return -E2BIG;
	/*
	 * TODO(b/73664181): Use multiple futex wait queues.
	 * We need to wake every sleeper when the condition changes. Typically
	 * only a single thread will be waiting on the condition, but there
	 * are exceptions. The worst case is about 10 threads.
	 */
	wake_up_interruptible_all(&data->futex_wait_queue);
	return 0;
}

static long vsoc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int rv = 0;
	struct vsoc_device_region *region_p;
	u32 reg_num;
	struct vsoc_region_data *reg_data;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	region_p = vsoc_region_from_filep(filp);
	reg_num = iminor(file_inode(filp));
	reg_data = vsoc_dev.regions_data + reg_num;
	switch (cmd) {
	case VSOC_CREATE_FD_SCOPED_PERMISSION:
		{
			struct fd_scoped_permission_node *node = NULL;

			node = kzalloc(sizeof(*node), GFP_KERNEL);
			/* We can't allocate memory for the permission */
			if (!node)
				return -ENOMEM;
			INIT_LIST_HEAD(&node->list);
			rv = do_create_fd_scoped_permission(
				region_p,
				node,
				(struct fd_scoped_permission_arg __user *)arg);
			if (!rv) {
				mutex_lock(&vsoc_dev.mtx);
				list_add(&node->list, &vsoc_dev.permissions);
				mutex_unlock(&vsoc_dev.mtx);
			} else {
				kfree(node);
				return rv;
			}
		}
		break;

	case VSOC_GET_FD_SCOPED_PERMISSION:
		{
			struct fd_scoped_permission_node *node =
			    ((struct vsoc_private_data *)filp->private_data)->
			    fd_scoped_permission_node;
			if (!node)
				return -ENOENT;
			if (copy_to_user
			    ((struct fd_scoped_permission __user *)arg,
			     &node->permission, sizeof(node->permission)))
				return -EFAULT;
		}
		break;

	case VSOC_MAYBE_SEND_INTERRUPT_TO_HOST:
		if (!atomic_xchg(
			    reg_data->outgoing_signalled,
			    1)) {
			writel(reg_num, vsoc_dev.regs + DOORBELL);
			return 0;
		} else {
			return -EBUSY;
		}
		break;

	case VSOC_SEND_INTERRUPT_TO_HOST:
		writel(reg_num, vsoc_dev.regs + DOORBELL);
		return 0;

	case VSOC_WAIT_FOR_INCOMING_INTERRUPT:
		wait_event_interruptible(
			reg_data->interrupt_wait_queue,
			(atomic_read(reg_data->incoming_signalled) != 0));
		break;

	case VSOC_DESCRIBE_REGION:
		return do_vsoc_describe_region(
			filp,
			(struct vsoc_device_region __user *)arg);

	case VSOC_SELF_INTERRUPT:
		atomic_set(reg_data->incoming_signalled, 1);
		wake_up_interruptible(&reg_data->interrupt_wait_queue);
		break;

	case VSOC_COND_WAIT:
		return do_vsoc_cond_wait(filp,
					 (struct vsoc_cond_wait __user *)arg);
	case VSOC_COND_WAKE:
		return do_vsoc_cond_wake(filp, arg);

	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t vsoc_read(struct file *filp, char *buffer, size_t len,
			 loff_t *poffset)
{
	__u32 area_off;
	void *area_p;
	ssize_t area_len;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	area_len = vsoc_get_area(filp, &area_off);
	area_p = shm_off_to_virtual_addr(area_off);
	area_p += *poffset;
	area_len -= *poffset;
	if (area_len <= 0)
		return 0;
	if (area_len < len)
		len = area_len;
	if (copy_to_user(buffer, area_p, len))
		return -EFAULT;
	*poffset += len;
	return len;
}

static loff_t vsoc_lseek(struct file *filp, loff_t offset, int origin)
{
	ssize_t area_len = 0;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	area_len = vsoc_get_area(filp, NULL);
	switch (origin) {
	case SEEK_SET:
		break;

	case SEEK_CUR:
		if (offset > 0 && offset + filp->f_pos < 0)
			return -EOVERFLOW;
		offset += filp->f_pos;
		break;

	case SEEK_END:
		if (offset > 0 && offset + area_len < 0)
			return -EOVERFLOW;
		offset += area_len;
		break;

	case SEEK_DATA:
		if (offset >= area_len)
			return -EINVAL;
		if (offset < 0)
			offset = 0;
		break;

	case SEEK_HOLE:
		/* Next hole is always the end of the region, unless offset is
		 * beyond that
		 */
		if (offset < area_len)
			offset = area_len;
		break;

	default:
		return -EINVAL;
	}

	if (offset < 0 || offset > area_len)
		return -EINVAL;
	filp->f_pos = offset;

	return offset;
}

static ssize_t vsoc_write(struct file *filp, const char *buffer,
			  size_t len, loff_t *poffset)
{
	__u32 area_off;
	void *area_p;
	ssize_t area_len;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	area_len = vsoc_get_area(filp, &area_off);
	area_p = shm_off_to_virtual_addr(area_off);
	area_p += *poffset;
	area_len -= *poffset;
	if (area_len <= 0)
		return 0;
	if (area_len < len)
		len = area_len;
	if (copy_from_user(area_p, buffer, len))
		return -EFAULT;
	*poffset += len;
	return len;
}

static irqreturn_t vsoc_interrupt(int irq, void *region_data_v)
{
	struct vsoc_region_data *region_data =
	    (struct vsoc_region_data *)region_data_v;
	int reg_num = region_data - vsoc_dev.regions_data;

	if (unlikely(!region_data))
		return IRQ_NONE;

	if (unlikely(reg_num < 0 ||
		     reg_num >= vsoc_dev.layout->region_count)) {
		dev_err(&vsoc_dev.dev->dev,
			"invalid irq @%p reg_num=0x%04x\n",
			region_data, reg_num);
		return IRQ_NONE;
	}
	if (unlikely(vsoc_dev.regions_data + reg_num != region_data)) {
		dev_err(&vsoc_dev.dev->dev,
			"irq not aligned @%p reg_num=0x%04x\n",
			region_data, reg_num);
		return IRQ_NONE;
	}
	wake_up_interruptible(&region_data->interrupt_wait_queue);
	return IRQ_HANDLED;
}

static int vsoc_probe_device(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	int result;
	int i;
	resource_size_t reg_size;
	dev_t devt;

	vsoc_dev.dev = pdev;
	result = pci_enable_device(pdev);
	if (result) {
		dev_err(&pdev->dev,
			"pci_enable_device failed %s: error %d\n",
			pci_name(pdev), result);
		return result;
	}
	vsoc_dev.enabled_device = 1;
	result = pci_request_regions(pdev, "vsoc");
	if (result < 0) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		vsoc_remove_device(pdev);
		return -EBUSY;
	}
	vsoc_dev.requested_regions = 1;
	/* Set up the control registers in BAR 0 */
	reg_size = pci_resource_len(pdev, REGISTER_BAR);
	if (reg_size > MAX_REGISTER_BAR_LEN)
		vsoc_dev.regs =
		    pci_iomap(pdev, REGISTER_BAR, MAX_REGISTER_BAR_LEN);
	else
		vsoc_dev.regs = pci_iomap(pdev, REGISTER_BAR, reg_size);

	if (!vsoc_dev.regs) {
		dev_err(&pdev->dev,
			"cannot ioremap registers of size %zu\n",
		       (size_t)reg_size);
		vsoc_remove_device(pdev);
		return -EBUSY;
	}

	/* Map the shared memory in BAR 2 */
	vsoc_dev.shm_phys_start = pci_resource_start(pdev, SHARED_MEMORY_BAR);
	vsoc_dev.shm_size = pci_resource_len(pdev, SHARED_MEMORY_BAR);

	dev_info(&pdev->dev, "shared memory @ DMA %p size=0x%zx\n",
		 (void *)vsoc_dev.shm_phys_start, vsoc_dev.shm_size);
	/* TODO(ghartman): ioremap_wc should work here */
	vsoc_dev.kernel_mapped_shm = ioremap_nocache(
			vsoc_dev.shm_phys_start, vsoc_dev.shm_size);
	if (!vsoc_dev.kernel_mapped_shm) {
		dev_err(&vsoc_dev.dev->dev, "cannot iomap region\n");
		vsoc_remove_device(pdev);
		return -EBUSY;
	}

	vsoc_dev.layout =
	    (struct vsoc_shm_layout_descriptor *)vsoc_dev.kernel_mapped_shm;
	dev_info(&pdev->dev, "major_version: %d\n",
		 vsoc_dev.layout->major_version);
	dev_info(&pdev->dev, "minor_version: %d\n",
		 vsoc_dev.layout->minor_version);
	dev_info(&pdev->dev, "size: 0x%x\n", vsoc_dev.layout->size);
	dev_info(&pdev->dev, "regions: %d\n", vsoc_dev.layout->region_count);
	if (vsoc_dev.layout->major_version !=
	    CURRENT_VSOC_LAYOUT_MAJOR_VERSION) {
		dev_err(&vsoc_dev.dev->dev,
			"driver supports only major_version %d\n",
			CURRENT_VSOC_LAYOUT_MAJOR_VERSION);
		vsoc_remove_device(pdev);
		return -EBUSY;
	}
	result = alloc_chrdev_region(&devt, 0, vsoc_dev.layout->region_count,
				     VSOC_DEV_NAME);
	if (result) {
		dev_err(&vsoc_dev.dev->dev, "alloc_chrdev_region failed\n");
		vsoc_remove_device(pdev);
		return -EBUSY;
	}
	vsoc_dev.major = MAJOR(devt);
	cdev_init(&vsoc_dev.cdev, &vsoc_ops);
	vsoc_dev.cdev.owner = THIS_MODULE;
	result = cdev_add(&vsoc_dev.cdev, devt, vsoc_dev.layout->region_count);
	if (result) {
		dev_err(&vsoc_dev.dev->dev, "cdev_add error\n");
		vsoc_remove_device(pdev);
		return -EBUSY;
	}
	vsoc_dev.cdev_added = 1;
	vsoc_dev.class = class_create(THIS_MODULE, VSOC_DEV_NAME);
	if (IS_ERR(vsoc_dev.class)) {
		dev_err(&vsoc_dev.dev->dev, "class_create failed\n");
		vsoc_remove_device(pdev);
		return PTR_ERR(vsoc_dev.class);
	}
	vsoc_dev.class_added = 1;
	vsoc_dev.regions = (struct vsoc_device_region *)
		(vsoc_dev.kernel_mapped_shm +
		 vsoc_dev.layout->vsoc_region_desc_offset);
	vsoc_dev.msix_entries = kcalloc(
			vsoc_dev.layout->region_count,
			sizeof(vsoc_dev.msix_entries[0]), GFP_KERNEL);
	if (!vsoc_dev.msix_entries) {
		dev_err(&vsoc_dev.dev->dev,
			"unable to allocate msix_entries\n");
		vsoc_remove_device(pdev);
		return -ENOSPC;
	}
	vsoc_dev.regions_data = kcalloc(
			vsoc_dev.layout->region_count,
			sizeof(vsoc_dev.regions_data[0]), GFP_KERNEL);
	if (!vsoc_dev.regions_data) {
		dev_err(&vsoc_dev.dev->dev,
			"unable to allocate regions' data\n");
		vsoc_remove_device(pdev);
		return -ENOSPC;
	}
	for (i = 0; i < vsoc_dev.layout->region_count; ++i)
		vsoc_dev.msix_entries[i].entry = i;

	result = pci_enable_msix_exact(vsoc_dev.dev, vsoc_dev.msix_entries,
				       vsoc_dev.layout->region_count);
	if (result) {
		dev_info(&pdev->dev, "pci_enable_msix failed: %d\n", result);
		vsoc_remove_device(pdev);
		return -ENOSPC;
	}
	/* Check that all regions are well formed */
	for (i = 0; i < vsoc_dev.layout->region_count; ++i) {
		const struct vsoc_device_region *region = vsoc_dev.regions + i;

		if (!PAGE_ALIGNED(region->region_begin_offset) ||
		    !PAGE_ALIGNED(region->region_end_offset)) {
			dev_err(&vsoc_dev.dev->dev,
				"region %d not aligned (%x:%x)", i,
				region->region_begin_offset,
				region->region_end_offset);
			vsoc_remove_device(pdev);
			return -EFAULT;
		}
		if (region->region_begin_offset >= region->region_end_offset ||
		    region->region_end_offset > vsoc_dev.shm_size) {
			dev_err(&vsoc_dev.dev->dev,
				"region %d offsets are wrong: %x %x %zx",
				i, region->region_begin_offset,
				region->region_end_offset, vsoc_dev.shm_size);
			vsoc_remove_device(pdev);
			return -EFAULT;
		}
		if (region->managed_by >= vsoc_dev.layout->region_count) {
			dev_err(&vsoc_dev.dev->dev,
				"region %d has invalid owner: %u",
				i, region->managed_by);
			vsoc_remove_device(pdev);
			return -EFAULT;
		}
	}
	vsoc_dev.msix_enabled = 1;
	for (i = 0; i < vsoc_dev.layout->region_count; ++i) {
		const struct vsoc_device_region *region = vsoc_dev.regions + i;
		size_t name_sz = sizeof(vsoc_dev.regions_data[i].name) - 1;
		const struct vsoc_signal_table_layout *h_to_g_signal_table =
			&region->host_to_guest_signal_table;
		const struct vsoc_signal_table_layout *g_to_h_signal_table =
			&region->guest_to_host_signal_table;

		vsoc_dev.regions_data[i].name[name_sz] = '\0';
		memcpy(vsoc_dev.regions_data[i].name, region->device_name,
		       name_sz);
		dev_info(&pdev->dev, "region %d name=%s\n",
			 i, vsoc_dev.regions_data[i].name);
		init_waitqueue_head(
				&vsoc_dev.regions_data[i].interrupt_wait_queue);
		init_waitqueue_head(&vsoc_dev.regions_data[i].futex_wait_queue);
		vsoc_dev.regions_data[i].incoming_signalled =
			vsoc_dev.kernel_mapped_shm +
			region->region_begin_offset +
			h_to_g_signal_table->interrupt_signalled_offset;
		vsoc_dev.regions_data[i].outgoing_signalled =
			vsoc_dev.kernel_mapped_shm +
			region->region_begin_offset +
			g_to_h_signal_table->interrupt_signalled_offset;

		result = request_irq(
				vsoc_dev.msix_entries[i].vector,
				vsoc_interrupt, 0,
				vsoc_dev.regions_data[i].name,
				vsoc_dev.regions_data + i);
		if (result) {
			dev_info(&pdev->dev,
				 "request_irq failed irq=%d vector=%d\n",
				i, vsoc_dev.msix_entries[i].vector);
			vsoc_remove_device(pdev);
			return -ENOSPC;
		}
		vsoc_dev.regions_data[i].irq_requested = 1;
		if (!device_create(vsoc_dev.class, NULL,
				   MKDEV(vsoc_dev.major, i),
				   NULL, vsoc_dev.regions_data[i].name)) {
			dev_err(&vsoc_dev.dev->dev, "device_create failed\n");
			vsoc_remove_device(pdev);
			return -EBUSY;
		}
		vsoc_dev.regions_data[i].device_created = 1;
	}
	return 0;
}

/*
 * This should undo all of the allocations in the probe function in reverse
 * order.
 *
 * Notes:
 *
 *   The device may have been partially initialized, so double check
 *   that the allocations happened.
 *
 *   This function may be called multiple times, so mark resources as freed
 *   as they are deallocated.
 */
static void vsoc_remove_device(struct pci_dev *pdev)
{
	int i;
	/*
	 * pdev is the first thing to be set on probe and the last thing
	 * to be cleared here. If it's NULL then there is no cleanup.
	 */
	if (!pdev || !vsoc_dev.dev)
		return;
	dev_info(&pdev->dev, "remove_device\n");
	if (vsoc_dev.regions_data) {
		for (i = 0; i < vsoc_dev.layout->region_count; ++i) {
			if (vsoc_dev.regions_data[i].device_created) {
				device_destroy(vsoc_dev.class,
					       MKDEV(vsoc_dev.major, i));
				vsoc_dev.regions_data[i].device_created = 0;
			}
			if (vsoc_dev.regions_data[i].irq_requested)
				free_irq(vsoc_dev.msix_entries[i].vector, NULL);
			vsoc_dev.regions_data[i].irq_requested = 0;
		}
		kfree(vsoc_dev.regions_data);
		vsoc_dev.regions_data = 0;
	}
	if (vsoc_dev.msix_enabled) {
		pci_disable_msix(pdev);
		vsoc_dev.msix_enabled = 0;
	}
	kfree(vsoc_dev.msix_entries);
	vsoc_dev.msix_entries = 0;
	vsoc_dev.regions = 0;
	if (vsoc_dev.class_added) {
		class_destroy(vsoc_dev.class);
		vsoc_dev.class_added = 0;
	}
	if (vsoc_dev.cdev_added) {
		cdev_del(&vsoc_dev.cdev);
		vsoc_dev.cdev_added = 0;
	}
	if (vsoc_dev.major && vsoc_dev.layout) {
		unregister_chrdev_region(MKDEV(vsoc_dev.major, 0),
					 vsoc_dev.layout->region_count);
		vsoc_dev.major = 0;
	}
	vsoc_dev.layout = 0;
	if (vsoc_dev.kernel_mapped_shm) {
		pci_iounmap(pdev, vsoc_dev.kernel_mapped_shm);
		vsoc_dev.kernel_mapped_shm = 0;
	}
	if (vsoc_dev.regs) {
		pci_iounmap(pdev, vsoc_dev.regs);
		vsoc_dev.regs = 0;
	}
	if (vsoc_dev.requested_regions) {
		pci_release_regions(pdev);
		vsoc_dev.requested_regions = 0;
	}
	if (vsoc_dev.enabled_device) {
		pci_disable_device(pdev);
		vsoc_dev.enabled_device = 0;
	}
	/* Do this last: it indicates that the device is not initialized. */
	vsoc_dev.dev = NULL;
}

static void __exit vsoc_cleanup_module(void)
{
	vsoc_remove_device(vsoc_dev.dev);
	pci_unregister_driver(&vsoc_pci_driver);
}

static int __init vsoc_init_module(void)
{
	int err = -ENOMEM;

	INIT_LIST_HEAD(&vsoc_dev.permissions);
	mutex_init(&vsoc_dev.mtx);

	err = pci_register_driver(&vsoc_pci_driver);
	if (err < 0)
		return err;
	return 0;
}

static int vsoc_open(struct inode *inode, struct file *filp)
{
	/* Can't use vsoc_validate_filep because filp is still incomplete */
	int ret = vsoc_validate_inode(inode);

	if (ret)
		return ret;
	filp->private_data =
		kzalloc(sizeof(struct vsoc_private_data), GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;
	return 0;
}

static int vsoc_release(struct inode *inode, struct file *filp)
{
	struct vsoc_private_data *private_data = NULL;
	struct fd_scoped_permission_node *node = NULL;
	struct vsoc_device_region *owner_region_p = NULL;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	private_data = (struct vsoc_private_data *)filp->private_data;
	if (!private_data)
		return 0;

	node = private_data->fd_scoped_permission_node;
	if (node) {
		owner_region_p = vsoc_region_from_inode(inode);
		if (owner_region_p->managed_by != VSOC_REGION_WHOLE) {
			owner_region_p =
			    &vsoc_dev.regions[owner_region_p->managed_by];
		}
		do_destroy_fd_scoped_permission_node(owner_region_p, node);
		private_data->fd_scoped_permission_node = NULL;
	}
	kfree(private_data);
	filp->private_data = NULL;

	return 0;
}

/*
 * Returns the device relative offset and length of the area specified by the
 * fd scoped permission. If there is no fd scoped permission set, a default
 * permission covering the entire region is assumed, unless the region is owned
 * by another one, in which case the default is a permission with zero size.
 */
static ssize_t vsoc_get_area(struct file *filp, __u32 *area_offset)
{
	__u32 off = 0;
	ssize_t length = 0;
	struct vsoc_device_region *region_p;
	struct fd_scoped_permission *perm;

	region_p = vsoc_region_from_filep(filp);
	off = region_p->region_begin_offset;
	perm = &((struct vsoc_private_data *)filp->private_data)->
		fd_scoped_permission_node->permission;
	if (perm) {
		off += perm->begin_offset;
		length = perm->end_offset - perm->begin_offset;
	} else if (region_p->managed_by == VSOC_REGION_WHOLE) {
		/* No permission set and the regions is not owned by another,
		 * default to full region access.
		 */
		length = vsoc_device_region_size(region_p);
	} else {
		/* return zero length, access is denied. */
		length = 0;
	}
	if (area_offset)
		*area_offset = off;
	return length;
}

static int vsoc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	__u32 area_off;
	phys_addr_t mem_off;
	ssize_t area_len;
	int retval = vsoc_validate_filep(filp);

	if (retval)
		return retval;
	area_len = vsoc_get_area(filp, &area_off);
	/* Add the requested offset */
	area_off += (vma->vm_pgoff << PAGE_SHIFT);
	area_len -= (vma->vm_pgoff << PAGE_SHIFT);
	if (area_len < len)
		return -EINVAL;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	mem_off = shm_off_to_phys_addr(area_off);
	if (io_remap_pfn_range(vma, vma->vm_start, mem_off >> PAGE_SHIFT,
			       len, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

module_init(vsoc_init_module);
module_exit(vsoc_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Hartman <ghartman@google.com>");
MODULE_DESCRIPTION("VSoC interpretation of QEmu's ivshmem device");
MODULE_VERSION("1.0");
