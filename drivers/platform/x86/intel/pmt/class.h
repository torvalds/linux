/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_PMT_CLASS_H
#define _INTEL_PMT_CLASS_H

#include <linux/platform_device.h>
#include <linux/xarray.h>
#include <linux/types.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/io.h>

/* PMT access types */
#define ACCESS_BARID		2
#define ACCESS_LOCAL		3

/* PMT discovery base address/offset register layout */
#define GET_BIR(v)		((v) & GENMASK(2, 0))
#define GET_ADDRESS(v)		((v) & GENMASK(31, 3))

struct intel_pmt_entry {
	struct bin_attribute	pmt_bin_attr;
	struct kobject		*kobj;
	void __iomem		*disc_table;
	void __iomem		*base;
	unsigned long		base_addr;
	size_t			size;
	u32			guid;
	int			devid;
};

struct intel_pmt_header {
	u32	base_offset;
	u32	size;
	u32	guid;
	u8	access_type;
};

struct intel_pmt_namespace {
	const char *name;
	struct xarray *xa;
	const struct attribute_group *attr_grp;
	int (*pmt_header_decode)(struct intel_pmt_entry *entry,
				 struct intel_pmt_header *header,
				 struct device *dev);
};

bool intel_pmt_is_early_client_hw(struct device *dev);
int intel_pmt_dev_create(struct intel_pmt_entry *entry,
			 struct intel_pmt_namespace *ns,
			 struct platform_device *pdev, int idx);
void intel_pmt_dev_destroy(struct intel_pmt_entry *entry,
			   struct intel_pmt_namespace *ns);
#endif
