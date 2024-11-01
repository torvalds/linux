/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *             Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 */

#ifndef _CXLFLASH_BACKEND_H
#define _CXLFLASH_BACKEND_H

extern const struct cxlflash_backend_ops cxlflash_cxl_ops;
extern const struct cxlflash_backend_ops cxlflash_ocxl_ops;

struct cxlflash_backend_ops {
	struct module *module;
	void __iomem * (*psa_map)(void *ctx_cookie);
	void (*psa_unmap)(void __iomem *addr);
	int (*process_element)(void *ctx_cookie);
	int (*map_afu_irq)(void *ctx_cookie, int num, irq_handler_t handler,
			   void *cookie, char *name);
	void (*unmap_afu_irq)(void *ctx_cookie, int num, void *cookie);
	u64 (*get_irq_objhndl)(void *ctx_cookie, int irq);
	int (*start_context)(void *ctx_cookie);
	int (*stop_context)(void *ctx_cookie);
	int (*afu_reset)(void *ctx_cookie);
	void (*set_master)(void *ctx_cookie);
	void * (*get_context)(struct pci_dev *dev, void *afu_cookie);
	void * (*dev_context_init)(struct pci_dev *dev, void *afu_cookie);
	int (*release_context)(void *ctx_cookie);
	void (*perst_reloads_same_image)(void *afu_cookie, bool image);
	ssize_t (*read_adapter_vpd)(struct pci_dev *dev, void *buf,
				    size_t count);
	int (*allocate_afu_irqs)(void *ctx_cookie, int num);
	void (*free_afu_irqs)(void *ctx_cookie);
	void * (*create_afu)(struct pci_dev *dev);
	void (*destroy_afu)(void *afu_cookie);
	struct file * (*get_fd)(void *ctx_cookie, struct file_operations *fops,
				int *fd);
	void * (*fops_get_context)(struct file *file);
	int (*start_work)(void *ctx_cookie, u64 irqs);
	int (*fd_mmap)(struct file *file, struct vm_area_struct *vm);
	int (*fd_release)(struct inode *inode, struct file *file);
};

#endif /* _CXLFLASH_BACKEND_H */
