/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_CORE_H__
#define __CXL_CORE_H__

extern const struct device_type cxl_nvdimm_bridge_type;
extern const struct device_type cxl_nvdimm_type;

extern struct attribute_group cxl_base_attribute_group;

static inline void unregister_cxl_dev(void *dev)
{
	device_unregister(dev);
}

int cxl_memdev_init(void);
void cxl_memdev_exit(void);

#endif /* __CXL_CORE_H__ */
