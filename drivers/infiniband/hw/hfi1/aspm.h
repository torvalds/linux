/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2015-2017 Intel Corporation.
 */

#ifndef _ASPM_H
#define _ASPM_H

#include "hfi.h"

extern uint aspm_mode;

enum aspm_mode {
	ASPM_MODE_DISABLED = 0,	/* ASPM always disabled, performance mode */
	ASPM_MODE_ENABLED = 1,	/* ASPM always enabled, power saving mode */
	ASPM_MODE_DYNAMIC = 2,	/* ASPM enabled/disabled dynamically */
};

void aspm_init(struct hfi1_devdata *dd);
void aspm_exit(struct hfi1_devdata *dd);
void aspm_hw_disable_l1(struct hfi1_devdata *dd);
void __aspm_ctx_disable(struct hfi1_ctxtdata *rcd);
void aspm_disable_all(struct hfi1_devdata *dd);
void aspm_enable_all(struct hfi1_devdata *dd);

static inline void aspm_ctx_disable(struct hfi1_ctxtdata *rcd)
{
	/* Quickest exit for minimum impact */
	if (likely(!rcd->aspm_intr_supported))
		return;

	__aspm_ctx_disable(rcd);
}

#endif /* _ASPM_H */
