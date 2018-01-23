// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#ifndef _OCXL_INTERNAL_H_
#define _OCXL_INTERNAL_H_

#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/list.h>

#define OCXL_AFU_NAME_SZ      (24+1)  /* add 1 for NULL termination */
#define MAX_IRQ_PER_LINK	2000
#define MAX_IRQ_PER_CONTEXT	MAX_IRQ_PER_LINK

#define to_ocxl_function(d) container_of(d, struct ocxl_fn, dev)
#define to_ocxl_afu(d) container_of(d, struct ocxl_afu, dev)

extern struct pci_driver ocxl_pci_driver;

/*
 * The following 2 structures are a fairly generic way of representing
 * the configuration data for a function and AFU, as read from the
 * configuration space.
 */
struct ocxl_afu_config {
	u8 idx;
	int dvsec_afu_control_pos;
	char name[OCXL_AFU_NAME_SZ];
	u8 version_major;
	u8 version_minor;
	u8 afuc_type;
	u8 afum_type;
	u8 profile;
	u8 global_mmio_bar;
	u64 global_mmio_offset;
	u32 global_mmio_size;
	u8 pp_mmio_bar;
	u64 pp_mmio_offset;
	u32 pp_mmio_stride;
	u8 log_mem_size;
	u8 pasid_supported_log;
	u16 actag_supported;
};

struct ocxl_fn_config {
	int dvsec_tl_pos;
	int dvsec_function_pos;
	int dvsec_afu_info_pos;
	s8 max_pasid_log;
	s8 max_afu_index;
};

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

extern int ocxl_config_read_function(struct pci_dev *dev,
				struct ocxl_fn_config *fn);

extern int ocxl_config_check_afu_index(struct pci_dev *dev,
				struct ocxl_fn_config *fn, int afu_idx);
extern int ocxl_config_read_afu(struct pci_dev *dev,
				struct ocxl_fn_config *fn,
				struct ocxl_afu_config *afu,
				u8 afu_idx);
extern int ocxl_config_get_pasid_info(struct pci_dev *dev, int *count);
extern void ocxl_config_set_afu_pasid(struct pci_dev *dev,
				int afu_control,
				int pasid_base, u32 pasid_count_log);
extern int ocxl_config_get_actag_info(struct pci_dev *dev,
				u16 *base, u16 *enabled, u16 *supported);
extern void ocxl_config_set_actag(struct pci_dev *dev, int func_dvsec,
				u32 tag_first, u32 tag_count);
extern void ocxl_config_set_afu_actag(struct pci_dev *dev, int afu_control,
				int actag_base, int actag_count);
extern void ocxl_config_set_afu_state(struct pci_dev *dev, int afu_control,
				int enable);
extern int ocxl_config_set_TL(struct pci_dev *dev, int tl_dvsec);
extern int ocxl_config_terminate_pasid(struct pci_dev *dev, int afu_control,
				int pasid);

extern int ocxl_link_setup(struct pci_dev *dev, int PE_mask,
			void **link_handle);
extern void ocxl_link_release(struct pci_dev *dev, void *link_handle);
extern int ocxl_link_add_pe(void *link_handle, int pasid, u32 pidr, u32 tidr,
		u64 amr, struct mm_struct *mm,
		void (*xsl_err_cb)(void *data, u64 addr, u64 dsisr),
		void *xsl_err_data);
extern int ocxl_link_remove_pe(void *link_handle, int pasid);
extern int ocxl_link_irq_alloc(void *link_handle, int *hw_irq,
			u64 *addr);
extern void ocxl_link_free_irq(void *link_handle, int hw_irq);

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
