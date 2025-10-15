/* SPDX-License-Identifier: GPL-2.0 */
/*
 * glue.h - DesignWare USB3 DRD glue header
 */

#ifndef __DRIVERS_USB_DWC3_GLUE_H
#define __DRIVERS_USB_DWC3_GLUE_H

#include <linux/types.h>
#include "core.h"

/**
 * dwc3_properties: DWC3 core properties
 * @gsbuscfg0_reqinfo: Value to be programmed in the GSBUSCFG0.REQINFO field
 */
struct dwc3_properties {
	u32 gsbuscfg0_reqinfo;
};

#define DWC3_DEFAULT_PROPERTIES ((struct dwc3_properties){		\
	.gsbuscfg0_reqinfo = DWC3_GSBUSCFG0_REQINFO_UNSPECIFIED,	\
	})

/**
 * dwc3_probe_data: Initialization parameters passed to dwc3_core_probe()
 * @dwc: Reference to dwc3 context structure
 * @res: resource for the DWC3 core mmio region
 * @ignore_clocks_and_resets: clocks and resets defined for the device should
 *		be ignored by the DWC3 core, as they are managed by the glue
 * @properties: dwc3 software manage properties
 */
struct dwc3_probe_data {
	struct dwc3 *dwc;
	struct resource *res;
	bool ignore_clocks_and_resets;
	struct dwc3_properties properties;
};

/**
 * dwc3_core_probe - Initialize the core dwc3 driver
 * @data: Initialization and configuration parameters for the controller
 *
 * Initializes the DesignWare USB3 core driver by setting up resources,
 * registering interrupts, performing hardware setup, and preparing
 * the controller for operation in the appropriate mode (host, gadget,
 * or OTG). This is the main initialization function called by glue
 * layer drivers to set up the core controller.
 *
 * Return: 0 on success, negative error code on failure
 */
int dwc3_core_probe(const struct dwc3_probe_data *data);

/**
 * dwc3_core_remove - Deinitialize and remove the core dwc3 driver
 * @dwc: Pointer to DWC3 controller context
 *
 * Cleans up resources and disables the dwc3 core driver. This should be called
 * during driver removal or when the glue layer needs to shut down the
 * controller completely.
 */
void dwc3_core_remove(struct dwc3 *dwc);

/*
 * The following callbacks are provided for glue drivers to call from their
 * own pm callbacks provided in struct dev_pm_ops. Glue drivers can perform
 * platform-specific work before or after calling these functions and delegate
 * the core suspend/resume operations to the core driver.
 */
int dwc3_runtime_suspend(struct dwc3 *dwc);
int dwc3_runtime_resume(struct dwc3 *dwc);
int dwc3_runtime_idle(struct dwc3 *dwc);
int dwc3_pm_suspend(struct dwc3 *dwc);
int dwc3_pm_resume(struct dwc3 *dwc);
void dwc3_pm_complete(struct dwc3 *dwc);
int dwc3_pm_prepare(struct dwc3 *dwc);

#endif
