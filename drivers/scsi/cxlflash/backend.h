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

extern const struct cxlflash_backend_ops cxlflash_cxl_ops;

struct cxlflash_backend_ops {
	struct module *module;
	void __iomem * (*psa_map)(void *);
	void (*psa_unmap)(void __iomem *);
	int (*process_element)(void *);
	int (*map_afu_irq)(void *, int, irq_handler_t, void *, char *);
	void (*unmap_afu_irq)(void *, int, void *);
	int (*start_context)(void *);
	int (*stop_context)(void *);
	int (*afu_reset)(void *);
	void (*set_master)(void *);
	void * (*get_context)(struct pci_dev *, void *);
	void * (*dev_context_init)(struct pci_dev *, void *);
	int (*release_context)(void *);
	void (*perst_reloads_same_image)(void *, bool);
	ssize_t (*read_adapter_vpd)(struct pci_dev *, void *, size_t);
	int (*allocate_afu_irqs)(void *, int);
	void (*free_afu_irqs)(void *);
	void * (*create_afu)(struct pci_dev *);
	struct file * (*get_fd)(void *, struct file_operations *, int *);
	void * (*fops_get_context)(struct file *);
	int (*start_work)(void *, u64);
	int (*fd_mmap)(struct file *, struct vm_area_struct *);
	int (*fd_release)(struct inode *, struct file *);
};
