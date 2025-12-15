/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef ATH12K_AHB_WIFI7_H
#define ATH12K_AHB_WIFI7_H

#ifdef CONFIG_ATH12K_AHB
int ath12k_wifi7_ahb_init(void);
void ath12k_wifi7_ahb_exit(void);
#else
static inline int ath12k_wifi7_ahb_init(void)
{
	return 0;
}

static inline void ath12k_wifi7_ahb_exit(void) {}
#endif
#endif /* ATH12K_AHB_WIFI7_H */
