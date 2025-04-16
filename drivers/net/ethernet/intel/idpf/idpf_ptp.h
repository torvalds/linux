/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef _IDPF_PTP_H
#define _IDPF_PTP_H

#include <linux/ptp_clock_kernel.h>

/**
 * struct idpf_ptp - PTP parameters
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @adapter: back pointer to the adapter
 */
struct idpf_ptp {
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct idpf_adapter *adapter;
};

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int idpf_ptp_init(struct idpf_adapter *adapter);
void idpf_ptp_release(struct idpf_adapter *adapter);
#else /* CONFIG_PTP_1588_CLOCK */
static inline int idpf_ptp_init(struct idpf_adapter *adapter)
{
	return 0;
}

static inline void idpf_ptp_release(struct idpf_adapter *adapter) { }
#endif /* CONFIG_PTP_1588_CLOCK */
#endif /* _IDPF_PTP_H */
