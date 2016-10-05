/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef MSM_IOMMU_H
#define MSM_IOMMU_H

#include <linux/interrupt.h>
#include <linux/clk.h>

/* Sharability attributes of MSM IOMMU mappings */
#define MSM_IOMMU_ATTR_NON_SH		0x0
#define MSM_IOMMU_ATTR_SH		0x4

/* Cacheability attributes of MSM IOMMU mappings */
#define MSM_IOMMU_ATTR_NONCACHED	0x0
#define MSM_IOMMU_ATTR_CACHED_WB_WA	0x1
#define MSM_IOMMU_ATTR_CACHED_WB_NWA	0x2
#define MSM_IOMMU_ATTR_CACHED_WT	0x3

/* Mask for the cache policy attribute */
#define MSM_IOMMU_CP_MASK		0x03

/* Maximum number of Machine IDs that we are allowing to be mapped to the same
 * context bank. The number of MIDs mapped to the same CB does not affect
 * performance, but there is a practical limit on how many distinct MIDs may
 * be present. These mappings are typically determined at design time and are
 * not expected to change at run time.
 */
#define MAX_NUM_MIDS	32

/* Maximum number of context banks that can be present in IOMMU */
#define IOMMU_MAX_CBS	128

/**
 * struct msm_iommu_dev - a single IOMMU hardware instance
 * ncb		Number of context banks present on this IOMMU HW instance
 * dev:		IOMMU device
 * irq:		Interrupt number
 * clk:		The bus clock for this IOMMU hardware instance
 * pclk:	The clock for the IOMMU bus interconnect
 * dev_node:	list head in qcom_iommu_device_list
 * dom_node:	list head for domain
 * ctx_list:	list of 'struct msm_iommu_ctx_dev'
 * context_map: Bitmap to track allocated context banks
 */
struct msm_iommu_dev {
	void __iomem *base;
	int ncb;
	struct device *dev;
	int irq;
	struct clk *clk;
	struct clk *pclk;
	struct list_head dev_node;
	struct list_head dom_node;
	struct list_head ctx_list;
	DECLARE_BITMAP(context_map, IOMMU_MAX_CBS);
};

/**
 * struct msm_iommu_ctx_dev - an IOMMU context bank instance
 * of_node	node ptr of client device
 * num		Index of this context bank within the hardware
 * mids		List of Machine IDs that are to be mapped into this context
 *		bank, terminated by -1. The MID is a set of signals on the
 *		AXI bus that identifies the function associated with a specific
 *		memory request. (See ARM spec).
 * num_mids	Total number of mids
 * node		list head in ctx_list
 */
struct msm_iommu_ctx_dev {
	struct device_node *of_node;
	int num;
	int mids[MAX_NUM_MIDS];
	int num_mids;
	struct list_head list;
};

/*
 * Interrupt handler for the IOMMU context fault interrupt. Hooking the
 * interrupt is not supported in the API yet, but this will print an error
 * message and dump useful IOMMU registers.
 */
irqreturn_t msm_iommu_fault_handler(int irq, void *dev_id);

#endif
