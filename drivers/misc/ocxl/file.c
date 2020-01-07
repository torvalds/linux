// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/eventfd.h>
#include <linux/uaccess.h>
#include <uapi/misc/ocxl.h>
#include <asm/reg.h>
#include <asm/switch_to.h>
#include "ocxl_internal.h"


#define OCXL_NUM_MINORS 256 /* Total to reserve */

static dev_t ocxl_dev;
static struct class *ocxl_class;
static struct mutex minors_idr_lock;
static struct idr minors_idr;

static struct ocxl_file_info *find_and_get_file_info(dev_t devno)
{
	struct ocxl_file_info *info;

	mutex_lock(&minors_idr_lock);
	info = idr_find(&minors_idr, MINOR(devno));
	if (info)
		get_device(&info->dev);
	mutex_unlock(&minors_idr_lock);
	return info;
}

static int allocate_minor(struct ocxl_file_info *info)
{
	int minor;

	mutex_lock(&minors_idr_lock);
	minor = idr_alloc(&minors_idr, info, 0, OCXL_NUM_MINORS, GFP_KERNEL);
	mutex_unlock(&minors_idr_lock);
	return minor;
}

static void free_minor(struct ocxl_file_info *info)
{
	mutex_lock(&minors_idr_lock);
	idr_remove(&minors_idr, MINOR(info->dev.devt));
	mutex_unlock(&minors_idr_lock);
}

static int afu_open(struct inode *inode, struct file *file)
{
	struct ocxl_file_info *info;
	struct ocxl_context *ctx;
	int rc;

	pr_debug("%s for device %x\n", __func__, inode->i_rdev);

	info = find_and_get_file_info(inode->i_rdev);
	if (!info)
		return -ENODEV;

	rc = ocxl_context_alloc(&ctx, info->afu, inode->i_mapping);
	if (rc) {
		put_device(&info->dev);
		return rc;
	}
	put_device(&info->dev);
	file->private_data = ctx;
	return 0;
}

static long afu_ioctl_attach(struct ocxl_context *ctx,
			struct ocxl_ioctl_attach __user *uarg)
{
	struct ocxl_ioctl_attach arg;
	u64 amr = 0;
	int rc;

	pr_debug("%s for context %d\n", __func__, ctx->pasid);

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	/* Make sure reserved fields are not set for forward compatibility */
	if (arg.reserved1 || arg.reserved2 || arg.reserved3)
		return -EINVAL;

	amr = arg.amr & mfspr(SPRN_UAMOR);
	rc = ocxl_context_attach(ctx, amr, current->mm);
	return rc;
}

static long afu_ioctl_get_metadata(struct ocxl_context *ctx,
		struct ocxl_ioctl_metadata __user *uarg)
{
	struct ocxl_ioctl_metadata arg;

	memset(&arg, 0, sizeof(arg));

	arg.version = 0;

	arg.afu_version_major = ctx->afu->config.version_major;
	arg.afu_version_minor = ctx->afu->config.version_minor;
	arg.pasid = ctx->pasid;
	arg.pp_mmio_size = ctx->afu->config.pp_mmio_stride;
	arg.global_mmio_size = ctx->afu->config.global_mmio_size;

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_PPC64
static long afu_ioctl_enable_p9_wait(struct ocxl_context *ctx,
		struct ocxl_ioctl_p9_wait __user *uarg)
{
	struct ocxl_ioctl_p9_wait arg;

	memset(&arg, 0, sizeof(arg));

	if (cpu_has_feature(CPU_FTR_P9_TIDR)) {
		enum ocxl_context_status status;

		// Locks both status & tidr
		mutex_lock(&ctx->status_mutex);
		if (!ctx->tidr) {
			if (set_thread_tidr(current)) {
				mutex_unlock(&ctx->status_mutex);
				return -ENOENT;
			}

			ctx->tidr = current->thread.tidr;
		}

		status = ctx->status;
		mutex_unlock(&ctx->status_mutex);

		if (status == ATTACHED) {
			int rc = ocxl_link_update_pe(ctx->afu->fn->link,
				ctx->pasid, ctx->tidr);

			if (rc)
				return rc;
		}

		arg.thread_id = ctx->tidr;
	} else
		return -ENOENT;

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	return 0;
}
#endif


static long afu_ioctl_get_features(struct ocxl_context *ctx,
		struct ocxl_ioctl_features __user *uarg)
{
	struct ocxl_ioctl_features arg;

	memset(&arg, 0, sizeof(arg));

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_P9_TIDR))
		arg.flags[0] |= OCXL_IOCTL_FEATURES_FLAGS0_P9_WAIT;
#endif

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	return 0;
}

#define CMD_STR(x) (x == OCXL_IOCTL_ATTACH ? "ATTACH" :			\
			x == OCXL_IOCTL_IRQ_ALLOC ? "IRQ_ALLOC" :	\
			x == OCXL_IOCTL_IRQ_FREE ? "IRQ_FREE" :		\
			x == OCXL_IOCTL_IRQ_SET_FD ? "IRQ_SET_FD" :	\
			x == OCXL_IOCTL_GET_METADATA ? "GET_METADATA" :	\
			x == OCXL_IOCTL_ENABLE_P9_WAIT ? "ENABLE_P9_WAIT" :	\
			x == OCXL_IOCTL_GET_FEATURES ? "GET_FEATURES" :	\
			"UNKNOWN")

static irqreturn_t irq_handler(void *private)
{
	struct eventfd_ctx *ev_ctx = private;

	eventfd_signal(ev_ctx, 1);
	return IRQ_HANDLED;
}

static void irq_free(void *private)
{
	struct eventfd_ctx *ev_ctx = private;

	eventfd_ctx_put(ev_ctx);
}

static long afu_ioctl(struct file *file, unsigned int cmd,
		unsigned long args)
{
	struct ocxl_context *ctx = file->private_data;
	struct ocxl_ioctl_irq_fd irq_fd;
	struct eventfd_ctx *ev_ctx;
	int irq_id;
	u64 irq_offset;
	long rc;
	bool closed;

	pr_debug("%s for context %d, command %s\n", __func__, ctx->pasid,
		CMD_STR(cmd));

	mutex_lock(&ctx->status_mutex);
	closed = (ctx->status == CLOSED);
	mutex_unlock(&ctx->status_mutex);

	if (closed)
		return -EIO;

	switch (cmd) {
	case OCXL_IOCTL_ATTACH:
		rc = afu_ioctl_attach(ctx,
				(struct ocxl_ioctl_attach __user *) args);
		break;

	case OCXL_IOCTL_IRQ_ALLOC:
		rc = ocxl_afu_irq_alloc(ctx, &irq_id);
		if (!rc) {
			irq_offset = ocxl_irq_id_to_offset(ctx, irq_id);
			rc = copy_to_user((u64 __user *) args, &irq_offset,
					sizeof(irq_offset));
			if (rc) {
				ocxl_afu_irq_free(ctx, irq_id);
				return -EFAULT;
			}
		}
		break;

	case OCXL_IOCTL_IRQ_FREE:
		rc = copy_from_user(&irq_offset, (u64 __user *) args,
				sizeof(irq_offset));
		if (rc)
			return -EFAULT;
		irq_id = ocxl_irq_offset_to_id(ctx, irq_offset);
		rc = ocxl_afu_irq_free(ctx, irq_id);
		break;

	case OCXL_IOCTL_IRQ_SET_FD:
		rc = copy_from_user(&irq_fd, (u64 __user *) args,
				sizeof(irq_fd));
		if (rc)
			return -EFAULT;
		if (irq_fd.reserved)
			return -EINVAL;
		irq_id = ocxl_irq_offset_to_id(ctx, irq_fd.irq_offset);
		ev_ctx = eventfd_ctx_fdget(irq_fd.eventfd);
		if (IS_ERR(ev_ctx))
			return PTR_ERR(ev_ctx);
		rc = ocxl_irq_set_handler(ctx, irq_id, irq_handler, irq_free, ev_ctx);
		break;

	case OCXL_IOCTL_GET_METADATA:
		rc = afu_ioctl_get_metadata(ctx,
				(struct ocxl_ioctl_metadata __user *) args);
		break;

#ifdef CONFIG_PPC64
	case OCXL_IOCTL_ENABLE_P9_WAIT:
		rc = afu_ioctl_enable_p9_wait(ctx,
				(struct ocxl_ioctl_p9_wait __user *) args);
		break;
#endif

	case OCXL_IOCTL_GET_FEATURES:
		rc = afu_ioctl_get_features(ctx,
				(struct ocxl_ioctl_features __user *) args);
		break;

	default:
		rc = -EINVAL;
	}
	return rc;
}

static long afu_compat_ioctl(struct file *file, unsigned int cmd,
			unsigned long args)
{
	return afu_ioctl(file, cmd, args);
}

static int afu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ocxl_context *ctx = file->private_data;

	pr_debug("%s for context %d\n", __func__, ctx->pasid);
	return ocxl_context_mmap(ctx, vma);
}

static bool has_xsl_error(struct ocxl_context *ctx)
{
	bool ret;

	mutex_lock(&ctx->xsl_error_lock);
	ret = !!ctx->xsl_error.addr;
	mutex_unlock(&ctx->xsl_error_lock);

	return ret;
}

/*
 * Are there any events pending on the AFU
 * ctx: The AFU context
 * Returns: true if there are events pending
 */
static bool afu_events_pending(struct ocxl_context *ctx)
{
	if (has_xsl_error(ctx))
		return true;
	return false;
}

static unsigned int afu_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ocxl_context *ctx = file->private_data;
	unsigned int mask = 0;
	bool closed;

	pr_debug("%s for context %d\n", __func__, ctx->pasid);

	poll_wait(file, &ctx->events_wq, wait);

	mutex_lock(&ctx->status_mutex);
	closed = (ctx->status == CLOSED);
	mutex_unlock(&ctx->status_mutex);

	if (afu_events_pending(ctx))
		mask = EPOLLIN | EPOLLRDNORM;
	else if (closed)
		mask = EPOLLERR;

	return mask;
}

/*
 * Populate the supplied buffer with a single XSL error
 * ctx:	The AFU context to report the error from
 * header: the event header to populate
 * buf: The buffer to write the body into (should be at least
 *      AFU_EVENT_BODY_XSL_ERROR_SIZE)
 * Return: the amount of buffer that was populated
 */
static ssize_t append_xsl_error(struct ocxl_context *ctx,
				struct ocxl_kernel_event_header *header,
				char __user *buf)
{
	struct ocxl_kernel_event_xsl_fault_error body;

	memset(&body, 0, sizeof(body));

	mutex_lock(&ctx->xsl_error_lock);
	if (!ctx->xsl_error.addr) {
		mutex_unlock(&ctx->xsl_error_lock);
		return 0;
	}

	body.addr = ctx->xsl_error.addr;
	body.dsisr = ctx->xsl_error.dsisr;
	body.count = ctx->xsl_error.count;

	ctx->xsl_error.addr = 0;
	ctx->xsl_error.dsisr = 0;
	ctx->xsl_error.count = 0;

	mutex_unlock(&ctx->xsl_error_lock);

	header->type = OCXL_AFU_EVENT_XSL_FAULT_ERROR;

	if (copy_to_user(buf, &body, sizeof(body)))
		return -EFAULT;

	return sizeof(body);
}

#define AFU_EVENT_BODY_MAX_SIZE sizeof(struct ocxl_kernel_event_xsl_fault_error)

/*
 * Reports events on the AFU
 * Format:
 *	Header (struct ocxl_kernel_event_header)
 *	Body (struct ocxl_kernel_event_*)
 *	Header...
 */
static ssize_t afu_read(struct file *file, char __user *buf, size_t count,
			loff_t *off)
{
	struct ocxl_context *ctx = file->private_data;
	struct ocxl_kernel_event_header header;
	ssize_t rc;
	ssize_t used = 0;
	DEFINE_WAIT(event_wait);

	memset(&header, 0, sizeof(header));

	/* Require offset to be 0 */
	if (*off != 0)
		return -EINVAL;

	if (count < (sizeof(struct ocxl_kernel_event_header) +
			AFU_EVENT_BODY_MAX_SIZE))
		return -EINVAL;

	for (;;) {
		prepare_to_wait(&ctx->events_wq, &event_wait,
				TASK_INTERRUPTIBLE);

		if (afu_events_pending(ctx))
			break;

		if (ctx->status == CLOSED)
			break;

		if (file->f_flags & O_NONBLOCK) {
			finish_wait(&ctx->events_wq, &event_wait);
			return -EAGAIN;
		}

		if (signal_pending(current)) {
			finish_wait(&ctx->events_wq, &event_wait);
			return -ERESTARTSYS;
		}

		schedule();
	}

	finish_wait(&ctx->events_wq, &event_wait);

	if (has_xsl_error(ctx)) {
		used = append_xsl_error(ctx, &header, buf + sizeof(header));
		if (used < 0)
			return used;
	}

	if (!afu_events_pending(ctx))
		header.flags |= OCXL_KERNEL_EVENT_FLAG_LAST;

	if (copy_to_user(buf, &header, sizeof(header)))
		return -EFAULT;

	used += sizeof(header);

	rc = used;
	return rc;
}

static int afu_release(struct inode *inode, struct file *file)
{
	struct ocxl_context *ctx = file->private_data;
	int rc;

	pr_debug("%s for device %x\n", __func__, inode->i_rdev);
	rc = ocxl_context_detach(ctx);
	mutex_lock(&ctx->mapping_lock);
	ctx->mapping = NULL;
	mutex_unlock(&ctx->mapping_lock);
	wake_up_all(&ctx->events_wq);
	if (rc != -EBUSY)
		ocxl_context_free(ctx);
	return 0;
}

static const struct file_operations ocxl_afu_fops = {
	.owner		= THIS_MODULE,
	.open           = afu_open,
	.unlocked_ioctl = afu_ioctl,
	.compat_ioctl   = afu_compat_ioctl,
	.mmap           = afu_mmap,
	.poll           = afu_poll,
	.read           = afu_read,
	.release        = afu_release,
};

// Free the info struct
static void info_release(struct device *dev)
{
	struct ocxl_file_info *info = container_of(dev, struct ocxl_file_info, dev);

	ocxl_afu_put(info->afu);
	kfree(info);
}

static int ocxl_file_make_visible(struct ocxl_file_info *info)
{
	int rc;

	cdev_init(&info->cdev, &ocxl_afu_fops);
	rc = cdev_add(&info->cdev, info->dev.devt, 1);
	if (rc) {
		dev_err(&info->dev, "Unable to add afu char device: %d\n", rc);
		return rc;
	}

	return 0;
}

static void ocxl_file_make_invisible(struct ocxl_file_info *info)
{
	cdev_del(&info->cdev);
}

int ocxl_file_register_afu(struct ocxl_afu *afu)
{
	int minor;
	int rc;
	struct ocxl_file_info *info;
	struct ocxl_fn *fn = afu->fn;
	struct pci_dev *pci_dev = to_pci_dev(fn->dev.parent);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	minor = allocate_minor(info);
	if (minor < 0) {
		kfree(info);
		return minor;
	}

	info->dev.parent = &fn->dev;
	info->dev.devt = MKDEV(MAJOR(ocxl_dev), minor);
	info->dev.class = ocxl_class;
	info->dev.release = info_release;

	info->afu = afu;
	ocxl_afu_get(afu);

	rc = dev_set_name(&info->dev, "%s.%s.%hhu",
		afu->config.name, dev_name(&pci_dev->dev), afu->config.idx);
	if (rc)
		goto err_put;

	rc = device_register(&info->dev);
	if (rc)
		goto err_put;

	rc = ocxl_sysfs_register_afu(info);
	if (rc)
		goto err_unregister;

	rc = ocxl_file_make_visible(info);
	if (rc)
		goto err_unregister;

	ocxl_afu_set_private(afu, info);

	return 0;

err_unregister:
	ocxl_sysfs_unregister_afu(info); // safe to call even if register failed
	device_unregister(&info->dev);
err_put:
	ocxl_afu_put(afu);
	free_minor(info);
	kfree(info);
	return rc;
}

void ocxl_file_unregister_afu(struct ocxl_afu *afu)
{
	struct ocxl_file_info *info = ocxl_afu_get_private(afu);

	if (!info)
		return;

	ocxl_file_make_invisible(info);
	ocxl_sysfs_unregister_afu(info);
	free_minor(info);
	device_unregister(&info->dev);
}

static char *ocxl_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "ocxl/%s", dev_name(dev));
}

int ocxl_file_init(void)
{
	int rc;

	mutex_init(&minors_idr_lock);
	idr_init(&minors_idr);

	rc = alloc_chrdev_region(&ocxl_dev, 0, OCXL_NUM_MINORS, "ocxl");
	if (rc) {
		pr_err("Unable to allocate ocxl major number: %d\n", rc);
		return rc;
	}

	ocxl_class = class_create(THIS_MODULE, "ocxl");
	if (IS_ERR(ocxl_class)) {
		pr_err("Unable to create ocxl class\n");
		unregister_chrdev_region(ocxl_dev, OCXL_NUM_MINORS);
		return PTR_ERR(ocxl_class);
	}

	ocxl_class->devnode = ocxl_devnode;
	return 0;
}

void ocxl_file_exit(void)
{
	class_destroy(ocxl_class);
	unregister_chrdev_region(ocxl_dev, OCXL_NUM_MINORS);
	idr_destroy(&minors_idr);
}
