/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef __DRIVERS_INTERCONNECT_MEDIATEK_ICC_EMI_H
#define __DRIVERS_INTERCONNECT_MEDIATEK_ICC_EMI_H

/**
 * struct mtk_icc_node - Mediatek EMI Interconnect Node
 * @name:      The interconnect node name which is shown in debugfs
 * @ep:        Type of this endpoint
 * @id:        Unique node identifier
 * @sum_avg:   Current sum aggregate value of all average bw requests in kBps
 * @max_peak:  Current max aggregate value of all peak bw requests in kBps
 * @num_links: The total number of @links
 * @links:     Array of @id linked to this node
 */
struct mtk_icc_node {
	unsigned char *name;
	int ep;
	u16 id;
	u64 sum_avg;
	u64 max_peak;

	u16 num_links;
	u16 links[] __counted_by(num_links);
};

struct mtk_icc_desc {
	struct mtk_icc_node **nodes;
	size_t num_nodes;
};

int mtk_emi_icc_probe(struct platform_device *pdev);
void mtk_emi_icc_remove(struct platform_device *pdev);

#endif /* __DRIVERS_INTERCONNECT_MEDIATEK_ICC_EMI_H */
