/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_FW_H
#define ATH12K_FW_H

#define ATH12K_FW_API2_FILE		"firmware-2.bin"
#define ATH12K_FIRMWARE_MAGIC		"QCOM-ATH12K-FW"

enum ath12k_fw_ie_type {
	ATH12K_FW_IE_TIMESTAMP = 0,
	ATH12K_FW_IE_FEATURES = 1,
	ATH12K_FW_IE_AMSS_IMAGE = 2,
	ATH12K_FW_IE_M3_IMAGE = 3,
	ATH12K_FW_IE_AMSS_DUALMAC_IMAGE = 4,
};

enum ath12k_fw_features {
	/* The firmware supports setting the QRTR id via register
	 * PCIE_LOCAL_REG_QRTR_NODE_ID
	 */
	ATH12K_FW_FEATURE_MULTI_QRTR_ID = 0,

	/* The firmware supports MLO capability */
	ATH12K_FW_FEATURE_MLO,

	/* keep last */
	ATH12K_FW_FEATURE_COUNT,
};

void ath12k_fw_map(struct ath12k_base *ab);
void ath12k_fw_unmap(struct ath12k_base *ab);
bool ath12k_fw_feature_supported(struct ath12k_base *ab, enum ath12k_fw_features feat);

#endif /* ATH12K_FW_H */
