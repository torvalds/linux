/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *             Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <misc/cxl.h>

#include "backend.h"

/*
 * The following routines map the cxlflash backend operations to existing CXL
 * kernel API function and are largely simple shims that provide an abstraction
 * for converting generic context and AFU cookies into cxl_context or cxl_afu
 * pointers.
 */

static void __iomem *cxlflash_psa_map(void *ctx_cookie)
{
	return cxl_psa_map(ctx_cookie);
}

static void cxlflash_psa_unmap(void __iomem *addr)
{
	cxl_psa_unmap(addr);
}

static int cxlflash_process_element(void *ctx_cookie)
{
	return cxl_process_element(ctx_cookie);
}

static int cxlflash_map_afu_irq(void *ctx_cookie, int num,
				irq_handler_t handler, void *cookie, char *name)
{
	return cxl_map_afu_irq(ctx_cookie, num, handler, cookie, name);
}

static void cxlflash_unmap_afu_irq(void *ctx_cookie, int num, void *cookie)
{
	cxl_unmap_afu_irq(ctx_cookie, num, cookie);
}

static int cxlflash_start_context(void *ctx_cookie)
{
	return cxl_start_context(ctx_cookie, 0, NULL);
}

static int cxlflash_stop_context(void *ctx_cookie)
{
	return cxl_stop_context(ctx_cookie);
}

static int cxlflash_afu_reset(void *ctx_cookie)
{
	return cxl_afu_reset(ctx_cookie);
}

static void cxlflash_set_master(void *ctx_cookie)
{
	cxl_set_master(ctx_cookie);
}

static void *cxlflash_get_context(struct pci_dev *dev, void *afu_cookie)
{
	return cxl_get_context(dev);
}

static void *cxlflash_dev_context_init(struct pci_dev *dev, void *afu_cookie)
{
	return cxl_dev_context_init(dev);
}

static int cxlflash_release_context(void *ctx_cookie)
{
	return cxl_release_context(ctx_cookie);
}

static void cxlflash_perst_reloads_same_image(void *afu_cookie, bool image)
{
	cxl_perst_reloads_same_image(afu_cookie, image);
}

static ssize_t cxlflash_read_adapter_vpd(struct pci_dev *dev,
					 void *buf, size_t count)
{
	return cxl_read_adapter_vpd(dev, buf, count);
}

static int cxlflash_allocate_afu_irqs(void *ctx_cookie, int num)
{
	return cxl_allocate_afu_irqs(ctx_cookie, num);
}

static void cxlflash_free_afu_irqs(void *ctx_cookie)
{
	cxl_free_afu_irqs(ctx_cookie);
}

static void *cxlflash_create_afu(struct pci_dev *dev)
{
	return cxl_pci_to_afu(dev);
}

static struct file *cxlflash_get_fd(void *ctx_cookie,
				    struct file_operations *fops, int *fd)
{
	return cxl_get_fd(ctx_cookie, fops, fd);
}

static void *cxlflash_fops_get_context(struct file *file)
{
	return cxl_fops_get_context(file);
}

static int cxlflash_start_work(void *ctx_cookie, u64 irqs)
{
	struct cxl_ioctl_start_work work = { 0 };

	work.num_interrupts = irqs;
	work.flags = CXL_START_WORK_NUM_IRQS;

	return cxl_start_work(ctx_cookie, &work);
}

static int cxlflash_fd_mmap(struct file *file, struct vm_area_struct *vm)
{
	return cxl_fd_mmap(file, vm);
}

static int cxlflash_fd_release(struct inode *inode, struct file *file)
{
	return cxl_fd_release(inode, file);
}

const struct cxlflash_backend_ops cxlflash_cxl_ops = {
	.module			= THIS_MODULE,
	.psa_map		= cxlflash_psa_map,
	.psa_unmap		= cxlflash_psa_unmap,
	.process_element	= cxlflash_process_element,
	.map_afu_irq		= cxlflash_map_afu_irq,
	.unmap_afu_irq		= cxlflash_unmap_afu_irq,
	.start_context		= cxlflash_start_context,
	.stop_context		= cxlflash_stop_context,
	.afu_reset		= cxlflash_afu_reset,
	.set_master		= cxlflash_set_master,
	.get_context		= cxlflash_get_context,
	.dev_context_init	= cxlflash_dev_context_init,
	.release_context	= cxlflash_release_context,
	.perst_reloads_same_image = cxlflash_perst_reloads_same_image,
	.read_adapter_vpd	= cxlflash_read_adapter_vpd,
	.allocate_afu_irqs	= cxlflash_allocate_afu_irqs,
	.free_afu_irqs		= cxlflash_free_afu_irqs,
	.create_afu		= cxlflash_create_afu,
	.get_fd			= cxlflash_get_fd,
	.fops_get_context	= cxlflash_fops_get_context,
	.start_work		= cxlflash_start_work,
	.fd_mmap		= cxlflash_fd_mmap,
	.fd_release		= cxlflash_fd_release,
};
