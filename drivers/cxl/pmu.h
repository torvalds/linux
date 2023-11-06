/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Huawei
 * CXL Specification rev 3.0 Setion 8.2.7 (CPMU Register Interface)
 */
#ifndef CXL_PMU_H
#define CXL_PMU_H
#include <linux/device.h>

enum cxl_pmu_type {
	CXL_PMU_MEMDEV,
};

#define CXL_PMU_REGMAP_SIZE 0xe00 /* Table 8-32 CXL 3.0 specification */
struct cxl_pmu {
	struct device dev;
	void __iomem *base;
	int assoc_id;
	int index;
	enum cxl_pmu_type type;
};

#define to_cxl_pmu(dev) container_of(dev, struct cxl_pmu, dev)
struct cxl_pmu_regs;
int devm_cxl_pmu_add(struct device *parent, struct cxl_pmu_regs *regs,
		     int assoc_id, int idx, enum cxl_pmu_type type);

#endif
