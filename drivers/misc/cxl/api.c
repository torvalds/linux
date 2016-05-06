/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <misc/cxl.h>
#include <linux/fs.h>

#include "cxl.h"

struct cxl_context *cxl_dev_context_init(struct pci_dev *dev)
{
	struct address_space *mapping;
	struct cxl_afu *afu;
	struct cxl_context  *ctx;
	int rc;

	afu = cxl_pci_to_afu(dev);

	ctx = cxl_context_alloc();
	if (IS_ERR(ctx)) {
		rc = PTR_ERR(ctx);
		goto err_dev;
	}

	ctx->kernelapi = true;

	/*
	 * Make our own address space since we won't have one from the
	 * filesystem like the user api has, and even if we do associate a file
	 * with this context we don't want to use the global anonymous inode's
	 * address space as that can invalidate unrelated users:
	 */
	mapping = kmalloc(sizeof(struct address_space), GFP_KERNEL);
	if (!mapping) {
		rc = -ENOMEM;
		goto err_ctx;
	}
	address_space_init_once(mapping);

	/* Make it a slave context.  We can promote it later? */
	rc = cxl_context_init(ctx, afu, false, mapping);
	if (rc)
		goto err_mapping;

	return ctx;

err_mapping:
	kfree(mapping);
err_ctx:
	kfree(ctx);
err_dev:
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(cxl_dev_context_init);

struct cxl_context *cxl_get_context(struct pci_dev *dev)
{
	return dev->dev.archdata.cxl_ctx;
}
EXPORT_SYMBOL_GPL(cxl_get_context);

int cxl_release_context(struct cxl_context *ctx)
{
	if (ctx->status >= STARTED)
		return -EBUSY;

	cxl_context_free(ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_release_context);

static irq_hw_number_t cxl_find_afu_irq(struct cxl_context *ctx, int num)
{
	__u16 range;
	int r;

	for (r = 0; r < CXL_IRQ_RANGES; r++) {
		range = ctx->irqs.range[r];
		if (num < range) {
			return ctx->irqs.offset[r] + num;
		}
		num -= range;
	}
	return 0;
}

int cxl_allocate_afu_irqs(struct cxl_context *ctx, int num)
{
	int res;
	irq_hw_number_t hwirq;

	if (num == 0)
		num = ctx->afu->pp_irqs;
	res = afu_allocate_irqs(ctx, num);
	if (!res && !cpu_has_feature(CPU_FTR_HVMODE)) {
		/* In a guest, the PSL interrupt is not multiplexed. It was
		 * allocated above, and we need to set its handler
		 */
		hwirq = cxl_find_afu_irq(ctx, 0);
		if (hwirq)
			cxl_map_irq(ctx->afu->adapter, hwirq, cxl_ops->psl_interrupt, ctx, "psl");
	}
	return res;
}
EXPORT_SYMBOL_GPL(cxl_allocate_afu_irqs);

void cxl_free_afu_irqs(struct cxl_context *ctx)
{
	irq_hw_number_t hwirq;
	unsigned int virq;

	if (!cpu_has_feature(CPU_FTR_HVMODE)) {
		hwirq = cxl_find_afu_irq(ctx, 0);
		if (hwirq) {
			virq = irq_find_mapping(NULL, hwirq);
			if (virq)
				cxl_unmap_irq(virq, ctx);
		}
	}
	afu_irq_name_free(ctx);
	cxl_ops->release_irq_ranges(&ctx->irqs, ctx->afu->adapter);
}
EXPORT_SYMBOL_GPL(cxl_free_afu_irqs);

int cxl_map_afu_irq(struct cxl_context *ctx, int num,
		    irq_handler_t handler, void *cookie, char *name)
{
	irq_hw_number_t hwirq;

	/*
	 * Find interrupt we are to register.
	 */
	hwirq = cxl_find_afu_irq(ctx, num);
	if (!hwirq)
		return -ENOENT;

	return cxl_map_irq(ctx->afu->adapter, hwirq, handler, cookie, name);
}
EXPORT_SYMBOL_GPL(cxl_map_afu_irq);

void cxl_unmap_afu_irq(struct cxl_context *ctx, int num, void *cookie)
{
	irq_hw_number_t hwirq;
	unsigned int virq;

	hwirq = cxl_find_afu_irq(ctx, num);
	if (!hwirq)
		return;

	virq = irq_find_mapping(NULL, hwirq);
	if (virq)
		cxl_unmap_irq(virq, cookie);
}
EXPORT_SYMBOL_GPL(cxl_unmap_afu_irq);

/*
 * Start a context
 * Code here similar to afu_ioctl_start_work().
 */
int cxl_start_context(struct cxl_context *ctx, u64 wed,
		      struct task_struct *task)
{
	int rc = 0;
	bool kernel = true;

	pr_devel("%s: pe: %i\n", __func__, ctx->pe);

	mutex_lock(&ctx->status_mutex);
	if (ctx->status == STARTED)
		goto out; /* already started */

	if (task) {
		ctx->pid = get_task_pid(task, PIDTYPE_PID);
		ctx->glpid = get_task_pid(task->group_leader, PIDTYPE_PID);
		kernel = false;
		ctx->real_mode = false;
	}

	cxl_ctx_get();

	if ((rc = cxl_ops->attach_process(ctx, kernel, wed, 0))) {
		put_pid(ctx->pid);
		cxl_ctx_put();
		goto out;
	}

	ctx->status = STARTED;
out:
	mutex_unlock(&ctx->status_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(cxl_start_context);

int cxl_process_element(struct cxl_context *ctx)
{
	return ctx->external_pe;
}
EXPORT_SYMBOL_GPL(cxl_process_element);

/* Stop a context.  Returns 0 on success, otherwise -Errno */
int cxl_stop_context(struct cxl_context *ctx)
{
	return __detach_context(ctx);
}
EXPORT_SYMBOL_GPL(cxl_stop_context);

void cxl_set_master(struct cxl_context *ctx)
{
	ctx->master = true;
}
EXPORT_SYMBOL_GPL(cxl_set_master);

int cxl_set_translation_mode(struct cxl_context *ctx, bool real_mode)
{
	if (ctx->status == STARTED) {
		/*
		 * We could potentially update the PE and issue an update LLCMD
		 * to support this, but it doesn't seem to have a good use case
		 * since it's trivial to just create a second kernel context
		 * with different translation modes, so until someone convinces
		 * me otherwise:
		 */
		return -EBUSY;
	}

	ctx->real_mode = real_mode;
	return 0;
}
EXPORT_SYMBOL_GPL(cxl_set_translation_mode);

/* wrappers around afu_* file ops which are EXPORTED */
int cxl_fd_open(struct inode *inode, struct file *file)
{
	return afu_open(inode, file);
}
EXPORT_SYMBOL_GPL(cxl_fd_open);
int cxl_fd_release(struct inode *inode, struct file *file)
{
	return afu_release(inode, file);
}
EXPORT_SYMBOL_GPL(cxl_fd_release);
long cxl_fd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return afu_ioctl(file, cmd, arg);
}
EXPORT_SYMBOL_GPL(cxl_fd_ioctl);
int cxl_fd_mmap(struct file *file, struct vm_area_struct *vm)
{
	return afu_mmap(file, vm);
}
EXPORT_SYMBOL_GPL(cxl_fd_mmap);
unsigned int cxl_fd_poll(struct file *file, struct poll_table_struct *poll)
{
	return afu_poll(file, poll);
}
EXPORT_SYMBOL_GPL(cxl_fd_poll);
ssize_t cxl_fd_read(struct file *file, char __user *buf, size_t count,
			loff_t *off)
{
	return afu_read(file, buf, count, off);
}
EXPORT_SYMBOL_GPL(cxl_fd_read);

#define PATCH_FOPS(NAME) if (!fops->NAME) fops->NAME = afu_fops.NAME

/* Get a struct file and fd for a context and attach the ops */
struct file *cxl_get_fd(struct cxl_context *ctx, struct file_operations *fops,
			int *fd)
{
	struct file *file;
	int rc, flags, fdtmp;

	flags = O_RDWR | O_CLOEXEC;

	/* This code is similar to anon_inode_getfd() */
	rc = get_unused_fd_flags(flags);
	if (rc < 0)
		return ERR_PTR(rc);
	fdtmp = rc;

	/*
	 * Patch the file ops.  Needs to be careful that this is rentrant safe.
	 */
	if (fops) {
		PATCH_FOPS(open);
		PATCH_FOPS(poll);
		PATCH_FOPS(read);
		PATCH_FOPS(release);
		PATCH_FOPS(unlocked_ioctl);
		PATCH_FOPS(compat_ioctl);
		PATCH_FOPS(mmap);
	} else /* use default ops */
		fops = (struct file_operations *)&afu_fops;

	file = anon_inode_getfile("cxl", fops, ctx, flags);
	if (IS_ERR(file))
		goto err_fd;

	file->f_mapping = ctx->mapping;

	*fd = fdtmp;
	return file;

err_fd:
	put_unused_fd(fdtmp);
	return NULL;
}
EXPORT_SYMBOL_GPL(cxl_get_fd);

struct cxl_context *cxl_fops_get_context(struct file *file)
{
	return file->private_data;
}
EXPORT_SYMBOL_GPL(cxl_fops_get_context);

int cxl_start_work(struct cxl_context *ctx,
		   struct cxl_ioctl_start_work *work)
{
	int rc;

	/* code taken from afu_ioctl_start_work */
	if (!(work->flags & CXL_START_WORK_NUM_IRQS))
		work->num_interrupts = ctx->afu->pp_irqs;
	else if ((work->num_interrupts < ctx->afu->pp_irqs) ||
		 (work->num_interrupts > ctx->afu->irqs_max)) {
		return -EINVAL;
	}

	rc = afu_register_irqs(ctx, work->num_interrupts);
	if (rc)
		return rc;

	rc = cxl_start_context(ctx, work->work_element_descriptor, current);
	if (rc < 0) {
		afu_release_irqs(ctx, ctx);
		return rc;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_start_work);

void __iomem *cxl_psa_map(struct cxl_context *ctx)
{
	if (ctx->status != STARTED)
		return NULL;

	pr_devel("%s: psn_phys%llx size:%llx\n",
		__func__, ctx->psn_phys, ctx->psn_size);
	return ioremap(ctx->psn_phys, ctx->psn_size);
}
EXPORT_SYMBOL_GPL(cxl_psa_map);

void cxl_psa_unmap(void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL_GPL(cxl_psa_unmap);

int cxl_afu_reset(struct cxl_context *ctx)
{
	struct cxl_afu *afu = ctx->afu;
	int rc;

	rc = cxl_ops->afu_reset(afu);
	if (rc)
		return rc;

	return cxl_ops->afu_check_and_enable(afu);
}
EXPORT_SYMBOL_GPL(cxl_afu_reset);

void cxl_perst_reloads_same_image(struct cxl_afu *afu,
				  bool perst_reloads_same_image)
{
	afu->adapter->perst_same_image = perst_reloads_same_image;
}
EXPORT_SYMBOL_GPL(cxl_perst_reloads_same_image);

ssize_t cxl_read_adapter_vpd(struct pci_dev *dev, void *buf, size_t count)
{
	struct cxl_afu *afu = cxl_pci_to_afu(dev);

	return cxl_ops->read_adapter_vpd(afu->adapter, buf, count);
}
EXPORT_SYMBOL_GPL(cxl_read_adapter_vpd);
