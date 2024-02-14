/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH11K_FW_H
#define ATH11K_FW_H

#define ATH11K_FW_API2_FILE		"firmware-2.bin"
#define ATH11K_FIRMWARE_MAGIC		"QCOM-ATH11K-FW"

enum ath11k_fw_ie_type {
	ATH11K_FW_IE_TIMESTAMP = 0,
	ATH11K_FW_IE_FEATURES = 1,
	ATH11K_FW_IE_AMSS_IMAGE = 2,
	ATH11K_FW_IE_M3_IMAGE = 3,
};

enum ath11k_fw_features {
	/* keep last */
	ATH11K_FW_FEATURE_COUNT,
};

int ath11k_fw_pre_init(struct ath11k_base *ab);
void ath11k_fw_destroy(struct ath11k_base *ab);

#endif /* ATH11K_FW_H */
