/* SPDX-License-Identifier: GPL-2.0 */
/*
 * glue.h - DesignWare USB3 DRD glue header
 */

#ifndef __DRIVERS_USB_DWC3_GLUE_H
#define __DRIVERS_USB_DWC3_GLUE_H

#include <linux/types.h>
#include "core.h"

/**
 * dwc3_probe_data: Initialization parameters passed to dwc3_core_probe()
 * @dwc: Reference to dwc3 context structure
 * @res: resource for the DWC3 core mmio region
 */
struct dwc3_probe_data {
	struct dwc3 *dwc;
	struct resource *res;
};

int dwc3_core_probe(const struct dwc3_probe_data *data);
void dwc3_core_remove(struct dwc3 *dwc);

int dwc3_runtime_suspend(struct dwc3 *dwc);
int dwc3_runtime_resume(struct dwc3 *dwc);
int dwc3_runtime_idle(struct dwc3 *dwc);
int dwc3_pm_suspend(struct dwc3 *dwc);
int dwc3_pm_resume(struct dwc3 *dwc);
void dwc3_pm_complete(struct dwc3 *dwc);
int dwc3_pm_prepare(struct dwc3 *dwc);

#endif
