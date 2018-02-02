// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#ifndef _OCXL_INTERNAL_H_
#define _OCXL_INTERNAL_H_

#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <misc/ocxl.h>

#define MAX_IRQ_PER_LINK	2000
#define MAX_IRQ_PER_CONTEXT	MAX_IRQ_PER_LINK

#define to_ocxl_function(d) container_of(d, struct ocxl_fn, dev)
#define to_ocxl_afu(d) container_of(d, struct ocxl_afu, dev)

extern struct pci_driver ocxl_pci_driver;


struct ocxl_fn {
	struct device dev;
	int bar_used[3];
	struct ocxl_fn_config config;
	struct list_head afu_list;
	int pasid_base;
	int actag_base;
	int actag_enabled;
	int actag_supported;
	struct list_head pasid_list;
	struct list_head actag_list;
	void *link;
};

struct ocxl_afu {
	struct ocxl_fn *fn;
	struct list_head list;
	struct device dev;
	struct cdev cdev;
	struct ocxl_afu_config config;
	int pasid_base;
	int pasid_count; /* opened contexts */
	int pasid_max; /* maximum number of contexts */
	int actag_base;
	int actag_enabled;
	struct mutex contexts_lock;
	struct idr contexts_idr;
	struct mutex afu_control_lock;
	u64 global_mmio_start;
	u64 irq_base_offset;
	void __iomem *global_mmio_ptr;
	u64 pp_mmio_start;
	struct bin_attribute attr_global_mmio;
};

enum ocxl_context_status {
	CLOSED,
	OPENED,
	ATTACHED,
};

// Contains metadata about a translation fault
struct ocxl_xsl_error {
	u64 addr; // The address that triggered the fault
	u64 dsisr; // the value of the dsisr register
	u64 count; // The number of times this fault has been triggered
};

struct ocxl_context {
	struct ocxl_afu *afu;
	int pasid;
	struct mutex status_mutex;
	enum ocxl_context_status status;
	struct address_space *mapping;
	struct mutex mapping_lock;
	wait_queue_head_t events_wq;
	struct mutex xsl_error_lock;
	struct ocxl_xsl_error xsl_error;
	struct mutex irq_lock;
	struct idr irq_idr;
};

struct ocxl_process_element {
	__be64 config_state;
	__be32 reserved1[11];
	__be32 lpid;
	__be32 tid;
	__be32 pid;
	__be32 reserved2[10];
	__be64 amr;
	__be32 reserved3[3];
	__be32 software_state;
};


extern struct ocxl_afu *ocxl_afu_get(struct ocxl_afu *afu);
extern void ocxl_afu_put(struct ocxl_afu *afu);

extern int ocxl_create_cdev(struct ocxl_afu *afu);
extern void ocxl_destroy_cdev(struct ocxl_afu *afu);
extern int ocxl_register_afu(struct ocxl_afu *afu);
extern void ocxl_unregister_afu(struct ocxl_afu *afu);

extern int ocxl_file_init(void);
extern void ocxl_file_exit(void);

extern int ocxl_pasid_afu_alloc(struct ocxl_fn *fn, u32 size);
extern void ocxl_pasid_afu_free(struct ocxl_fn *fn, u32 start, u32 size);
extern int ocxl_actag_afu_alloc(struct ocxl_fn *fn, u32 size);
extern void ocxl_actag_afu_free(struct ocxl_fn *fn, u32 start, u32 size);

extern struct ocxl_context *ocxl_context_alloc(void);
extern int ocxl_context_init(struct ocxl_context *ctx, struct ocxl_afu *afu,
			struct address_space *mapping);
extern int ocxl_context_attach(struct ocxl_context *ctx, u64 amr);
extern int ocxl_context_mmap(struct ocxl_context *ctx,
			struct vm_area_struct *vma);
extern int ocxl_context_detach(struct ocxl_context *ctx);
extern void ocxl_context_detach_all(struct ocxl_afu *afu);
extern void ocxl_context_free(struct ocxl_context *ctx);

extern int ocxl_sysfs_add_afu(struct ocxl_afu *afu);
extern void ocxl_sysfs_remove_afu(struct ocxl_afu *afu);

extern int ocxl_afu_irq_alloc(struct ocxl_context *ctx, u64 *irq_offset);
extern int ocxl_afu_irq_free(struct ocxl_context *ctx, u64 irq_offset);
extern void ocxl_afu_irq_free_all(struct ocxl_context *ctx);
extern int ocxl_afu_irq_set_fd(struct ocxl_context *ctx, u64 irq_offset,
			int eventfd);
extern u64 ocxl_afu_irq_get_addr(struct ocxl_context *ctx, u64 irq_offset);

#endif /* _OCXL_INTERNAL_H_ */
