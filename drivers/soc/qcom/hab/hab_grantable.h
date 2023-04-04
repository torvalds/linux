/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_GRANTABLE_H
#define __HAB_GRANTABLE_H

/* Grantable should be common between exporter and importer */
struct grantable {
	unsigned long pfn;
};

struct compressed_pfns {
	unsigned long first_pfn;
	int nregions;
	struct region {
		int size;
		int space;
	} region[];
};
#endif /* __HAB_GRANTABLE_H */
