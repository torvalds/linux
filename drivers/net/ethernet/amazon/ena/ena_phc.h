/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef ENA_PHC_H
#define ENA_PHC_H

#include <linux/ptp_clock_kernel.h>

struct ena_phc_info {
	/* PTP hardware capabilities */
	struct ptp_clock_info clock_info;

	/* Registered PTP clock device */
	struct ptp_clock *clock;

	/* Adapter specific private data structure */
	struct ena_adapter *adapter;

	/* PHC lock */
	spinlock_t lock;

	/* Enabled by kernel */
	bool enabled;
};

void ena_phc_enable(struct ena_adapter *adapter, bool enable);
bool ena_phc_is_enabled(struct ena_adapter *adapter);
bool ena_phc_is_active(struct ena_adapter *adapter);
int ena_phc_get_index(struct ena_adapter *adapter);
int ena_phc_init(struct ena_adapter *adapter);
void ena_phc_destroy(struct ena_adapter *adapter);
int ena_phc_alloc(struct ena_adapter *adapter);
void ena_phc_free(struct ena_adapter *adapter);

#endif /* ENA_PHC_H */
