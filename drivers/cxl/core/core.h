/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_CORE_H__
#define __CXL_CORE_H__

#include <cxl/mailbox.h>
#include <linux/rwsem.h>

extern const struct device_type cxl_nvdimm_bridge_type;
extern const struct device_type cxl_nvdimm_type;
extern const struct device_type cxl_pmu_type;

extern struct attribute_group cxl_base_attribute_group;

enum cxl_detach_mode {
	DETACH_ONLY,
	DETACH_INVALIDATE,
};

#ifdef CONFIG_CXL_REGION
extern struct device_attribute dev_attr_create_pmem_region;
extern struct device_attribute dev_attr_create_ram_region;
extern struct device_attribute dev_attr_delete_region;
extern struct device_attribute dev_attr_region;
extern const struct device_type cxl_pmem_region_type;
extern const struct device_type cxl_dax_region_type;
extern const struct device_type cxl_region_type;

int cxl_decoder_detach(struct cxl_region *cxlr,
		       struct cxl_endpoint_decoder *cxled, int pos,
		       enum cxl_detach_mode mode);

#define CXL_REGION_ATTR(x) (&dev_attr_##x.attr)
#define CXL_REGION_TYPE(x) (&cxl_region_type)
#define SET_CXL_REGION_ATTR(x) (&dev_attr_##x.attr),
#define CXL_PMEM_REGION_TYPE(x) (&cxl_pmem_region_type)
#define CXL_DAX_REGION_TYPE(x) (&cxl_dax_region_type)
int cxl_region_init(void);
void cxl_region_exit(void);
int cxl_get_poison_by_endpoint(struct cxl_port *port);
struct cxl_region *cxl_dpa_to_region(const struct cxl_memdev *cxlmd, u64 dpa);
u64 cxl_dpa_to_hpa(struct cxl_region *cxlr, const struct cxl_memdev *cxlmd,
		   u64 dpa);

#else
static inline u64 cxl_dpa_to_hpa(struct cxl_region *cxlr,
				 const struct cxl_memdev *cxlmd, u64 dpa)
{
	return ULLONG_MAX;
}
static inline
struct cxl_region *cxl_dpa_to_region(const struct cxl_memdev *cxlmd, u64 dpa)
{
	return NULL;
}
static inline int cxl_get_poison_by_endpoint(struct cxl_port *port)
{
	return 0;
}
static inline int cxl_decoder_detach(struct cxl_region *cxlr,
				     struct cxl_endpoint_decoder *cxled,
				     int pos, enum cxl_detach_mode mode)
{
	return 0;
}
static inline int cxl_region_init(void)
{
	return 0;
}
static inline void cxl_region_exit(void)
{
}
#define CXL_REGION_ATTR(x) NULL
#define CXL_REGION_TYPE(x) NULL
#define SET_CXL_REGION_ATTR(x)
#define CXL_PMEM_REGION_TYPE(x) NULL
#define CXL_DAX_REGION_TYPE(x) NULL
#endif

struct cxl_send_command;
struct cxl_mem_query_commands;
int cxl_query_cmd(struct cxl_mailbox *cxl_mbox,
		  struct cxl_mem_query_commands __user *q);
int cxl_send_cmd(struct cxl_mailbox *cxl_mbox, struct cxl_send_command __user *s);
void __iomem *devm_cxl_iomap_block(struct device *dev, resource_size_t addr,
				   resource_size_t length);

struct dentry *cxl_debugfs_create_dir(const char *dir);
int cxl_dpa_set_part(struct cxl_endpoint_decoder *cxled,
		     enum cxl_partition_mode mode);
int cxl_dpa_alloc(struct cxl_endpoint_decoder *cxled, u64 size);
int cxl_dpa_free(struct cxl_endpoint_decoder *cxled);
resource_size_t cxl_dpa_size(struct cxl_endpoint_decoder *cxled);
resource_size_t cxl_dpa_resource_start(struct cxl_endpoint_decoder *cxled);
bool cxl_resource_contains_addr(const struct resource *res, const resource_size_t addr);

enum cxl_rcrb {
	CXL_RCRB_DOWNSTREAM,
	CXL_RCRB_UPSTREAM,
};
struct cxl_rcrb_info;
resource_size_t __rcrb_to_component(struct device *dev,
				    struct cxl_rcrb_info *ri,
				    enum cxl_rcrb which);
u16 cxl_rcrb_to_aer(struct device *dev, resource_size_t rcrb);

#define PCI_RCRB_CAP_LIST_ID_MASK	GENMASK(7, 0)
#define PCI_RCRB_CAP_HDR_ID_MASK	GENMASK(7, 0)
#define PCI_RCRB_CAP_HDR_NEXT_MASK	GENMASK(15, 8)
#define PCI_CAP_EXP_SIZEOF		0x3c

struct cxl_rwsem {
	/*
	 * All changes to HPA (interleave configuration) occur with this
	 * lock held for write.
	 */
	struct rw_semaphore region;
	/*
	 * All changes to a device DPA space occur with this lock held
	 * for write.
	 */
	struct rw_semaphore dpa;
};

extern struct cxl_rwsem cxl_rwsem;

int cxl_memdev_init(void);
void cxl_memdev_exit(void);
void cxl_mbox_init(void);

enum cxl_poison_trace_type {
	CXL_POISON_TRACE_LIST,
	CXL_POISON_TRACE_INJECT,
	CXL_POISON_TRACE_CLEAR,
};

enum poison_cmd_enabled_bits;
bool cxl_memdev_has_poison_cmd(struct cxl_memdev *cxlmd,
			       enum poison_cmd_enabled_bits cmd);

long cxl_pci_get_latency(struct pci_dev *pdev);
int cxl_pci_get_bandwidth(struct pci_dev *pdev, struct access_coordinate *c);
int cxl_port_get_switch_dport_bandwidth(struct cxl_port *port,
					struct access_coordinate *c);

int cxl_ras_init(void);
void cxl_ras_exit(void);
int cxl_gpf_port_setup(struct cxl_dport *dport);

struct cxl_hdm;
int cxl_hdm_decode_init(struct cxl_dev_state *cxlds, struct cxl_hdm *cxlhdm,
			struct cxl_endpoint_dvsec_info *info);
int cxl_port_get_possible_dports(struct cxl_port *port);

#ifdef CONFIG_CXL_FEATURES
struct cxl_feat_entry *
cxl_feature_info(struct cxl_features_state *cxlfs, const uuid_t *uuid);
size_t cxl_get_feature(struct cxl_mailbox *cxl_mbox, const uuid_t *feat_uuid,
		       enum cxl_get_feat_selection selection,
		       void *feat_out, size_t feat_out_size, u16 offset,
		       u16 *return_code);
int cxl_set_feature(struct cxl_mailbox *cxl_mbox, const uuid_t *feat_uuid,
		    u8 feat_version, const void *feat_data,
		    size_t feat_data_size, u32 feat_flag, u16 offset,
		    u16 *return_code);
#endif

#endif /* __CXL_CORE_H__ */
