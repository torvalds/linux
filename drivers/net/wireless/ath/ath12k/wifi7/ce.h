/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_WIFI7_CE_H
#define ATH12K_WIFI7_CE_H

extern const struct ce_pipe_config ath12k_wifi7_target_ce_config_wlan_qcn9274[];
extern const struct ce_pipe_config ath12k_wifi7_target_ce_config_wlan_wcn7850[];
extern const struct ce_pipe_config ath12k_wifi7_target_ce_config_wlan_ipq5332[];

extern const struct service_to_pipe ath12k_wifi7_target_service_to_ce_map_wlan_qcn9274[];
extern const struct service_to_pipe ath12k_wifi7_target_service_to_ce_map_wlan_wcn7850[];
extern const struct service_to_pipe ath12k_wifi7_target_service_to_ce_map_wlan_ipq5332[];

extern const struct ce_attr ath12k_wifi7_host_ce_config_qcn9274[];
extern const struct ce_attr ath12k_wifi7_host_ce_config_wcn7850[];
extern const struct ce_attr ath12k_wifi7_host_ce_config_ipq5332[];

#endif /* ATH12K_WIFI7_CE_H */
