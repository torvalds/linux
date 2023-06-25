/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_CORE_H__
#define __CXL_CORE_H__

extern const struct device_type cxl_nvdimm_bridge_type;
extern const struct device_type cxl_nvdimm_type;

extern struct attribute_group cxl_base_attribute_group;

#ifdef CONFIG_CXL_REGION
extern struct device_attribute dev_attr_create_pmem_region;
extern struct device_attribute dev_attr_create_ram_region;
extern struct device_attribute dev_attr_delete_region;
extern struct device_attribute dev_attr_region;
extern const struct device_type cxl_pmem_region_type;
extern const struct device_type cxl_dax_region_type;
extern const struct device_type cxl_region_type;
void cxl_decoder_kill_region(struct cxl_endpoint_decoder *cxled);
#define CXL_REGION_ATTR(x) (&dev_attr_##x.attr)
#define CXL_REGION_TYPE(x) (&cxl_region_type)
#define SET_CXL_REGION_ATTR(x) (&dev_attr_##x.attr),
#define CXL_PMEM_REGION_TYPE(x) (&cxl_pmem_region_type)
#define CXL_DAX_REGION_TYPE(x) (&cxl_dax_region_type)
int cxl_region_init(void);
void cxl_region_exit(void);
int cxl_get_poison_by_endpoint(struct cxl_port *port);
#else
static inline int cxl_get_poison_by_endpoint(struct cxl_port *port)
{
	return 0;
}
static inline void cxl_decoder_kill_region(struct cxl_endpoint_decoder *cxled)
{
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
int cxl_query_cmd(struct cxl_memdev *cxlmd,
		  struct cxl_mem_query_commands __user *q);
int cxl_send_cmd(struct cxl_memdev *cxlmd, struct cxl_send_command __user *s);
void __iomem *devm_cxl_iomap_block(struct device *dev, resource_size_t addr,
				   resource_size_t length);

struct dentry *cxl_debugfs_create_dir(const char *dir);
int cxl_dpa_set_mode(struct cxl_endpoint_decoder *cxled,
		     enum cxl_decoder_mode mode);
int cxl_dpa_alloc(struct cxl_endpoint_decoder *cxled, unsigned long long size);
int cxl_dpa_free(struct cxl_endpoint_decoder *cxled);
resource_size_t cxl_dpa_size(struct cxl_endpoint_decoder *cxled);
resource_size_t cxl_dpa_resource_start(struct cxl_endpoint_decoder *cxled);

enum cxl_rcrb {
	CXL_RCRB_DOWNSTREAM,
	CXL_RCRB_UPSTREAM,
};
resource_size_t __rcrb_to_component(struct device *dev, resource_size_t rcrb,
				    enum cxl_rcrb which);

extern struct rw_semaphore cxl_dpa_rwsem;

int cxl_memdev_init(void);
void cxl_memdev_exit(void);
void cxl_mbox_init(void);

enum cxl_poison_trace_type {
	CXL_POISON_TRACE_LIST,
	CXL_POISON_TRACE_INJECT,
	CXL_POISON_TRACE_CLEAR,
};

#endif /* __CXL_CORE_H__ */
