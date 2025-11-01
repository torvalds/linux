/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_PMT_CLASS_H
#define _INTEL_PMT_CLASS_H

#include <linux/intel_vsec.h>
#include <linux/xarray.h>
#include <linux/types.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/io.h>

#include "telemetry.h"

/* PMT access types */
#define ACCESS_BARID		2
#define ACCESS_LOCAL		3

/* PMT discovery base address/offset register layout */
#define GET_BIR(v)		((v) & GENMASK(2, 0))
#define GET_ADDRESS(v)		((v) & GENMASK(31, 3))

struct pci_dev;
extern struct class intel_pmt_class;

struct telem_endpoint {
	struct pci_dev		*pcidev;
	struct telem_header	header;
	struct pmt_callbacks	*cb;
	void __iomem		*base;
	bool			present;
	struct kref		kref;
};

struct intel_pmt_header {
	u32	base_offset;
	u32	size;
	u32	guid;
	u8	access_type;
};

struct intel_pmt_entry {
	struct telem_endpoint	*ep;
	struct pci_dev		*pcidev;
	struct intel_pmt_header	header;
	struct bin_attribute	pmt_bin_attr;
	const struct attribute_group *attr_grp;
	struct kobject		*kobj;
	void __iomem		*disc_table;
	void __iomem		*base;
	struct pmt_callbacks	*cb;
	unsigned long		base_addr;
	size_t			size;
	u64			feature_flags;
	u32			guid;
	u32			num_rmids; /* Number of Resource Monitoring IDs */
	int			devid;
};

struct intel_pmt_namespace {
	const char *name;
	struct xarray *xa;
	int (*pmt_header_decode)(struct intel_pmt_entry *entry,
				 struct device *dev);
	int (*pmt_add_endpoint)(struct intel_vsec_device *ivdev,
				struct intel_pmt_entry *entry);
};

int pmt_telem_read_mmio(struct pci_dev *pdev, struct pmt_callbacks *cb, u32 guid, void *buf,
			void __iomem *addr, loff_t off, u32 count);
bool intel_pmt_is_early_client_hw(struct device *dev);
int intel_pmt_dev_create(struct intel_pmt_entry *entry,
			 struct intel_pmt_namespace *ns,
			 struct intel_vsec_device *dev, int idx);
void intel_pmt_dev_destroy(struct intel_pmt_entry *entry,
			   struct intel_pmt_namespace *ns);
#if IS_ENABLED(CONFIG_INTEL_PMT_DISCOVERY)
void intel_pmt_get_features(struct intel_pmt_entry *entry);
#else
static inline void intel_pmt_get_features(struct intel_pmt_entry *entry) {}
#endif

#endif
