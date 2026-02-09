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
 * @skip_core_init_mode: Skip the finial initialization of the target mode, as
 *		it must be managed by the glue
 * @properties: dwc3 software manage properties
 */
struct dwc3_probe_data {
	struct dwc3 *dwc;
	struct resource *res;
	bool ignore_clocks_and_resets;
	bool skip_core_init_mode;
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


/* All of the following functions must only be used with skip_core_init_mode */

/**
 * dwc3_core_init - Initialize DWC3 core hardware
 * @dwc: Pointer to DWC3 controller context
 *
 * Configures and initializes the core hardware, usually done by dwc3_core_probe.
 * This function is provided for platforms that use skip_core_init_mode and need
 * to finalize the core initialization after some platform-specific setup.
 * It must only be called when using skip_core_init_mode and before
 * dwc3_host_init or dwc3_gadget_init.
 *
 * Return: 0 on success, negative error code on failure
 */
int dwc3_core_init(struct dwc3 *dwc);

/**
 * dwc3_core_exit - Shut down DWC3 core hardware
 * @dwc: Pointer to DWC3 controller context
 *
 * Disables and cleans up the core hardware state. This is usually handled
 * internally by dwc3 and must only be called when using skip_core_init_mode
 * and only after dwc3_core_init. Afterwards, dwc3_core_init may be called
 * again.
 */
void dwc3_core_exit(struct dwc3 *dwc);

/**
 * dwc3_host_init - Initialize host mode operation
 * @dwc: Pointer to DWC3 controller context
 *
 * Initializes the controller for USB host mode operation, usually done by
 * dwc3_core_probe or from within the dwc3 USB role switch callback.
 * This function is provided for platforms that use skip_core_init_mode and need
 * to finalize the host initialization after some platform-specific setup.
 * It must not be called before dwc3_core_init or when skip_core_init_mode is
 * not used. It must also not be called when gadget or host mode has already
 * been initialized.
 *
 * Return: 0 on success, negative error code on failure
 */
int dwc3_host_init(struct dwc3 *dwc);

/**
 * dwc3_host_exit - Shut down host mode operation
 * @dwc: Pointer to DWC3 controller context
 *
 * Disables and cleans up host mode resources, usually done by
 * the dwc3 USB role switch callback before switching controller mode.
 * It must only be called when skip_core_init_mode is used and only after
 * dwc3_host_init.
 */
void dwc3_host_exit(struct dwc3 *dwc);

/**
 * dwc3_gadget_init - Initialize gadget mode operation
 * @dwc: Pointer to DWC3 controller context
 *
 * Initializes the controller for USB gadget mode operation, usually done by
 * dwc3_core_probe or from within the dwc3 USB role switch callback. This
 * function is provided for platforms that use skip_core_init_mode and need to
 * finalize the gadget initialization after some platform-specific setup.
 * It must not be called before dwc3_core_init or when skip_core_init_mode is
 * not used. It must also not be called when gadget or host mode has already
 * been initialized.
 *
 * Return: 0 on success, negative error code on failure
 */
int dwc3_gadget_init(struct dwc3 *dwc);

/**
 * dwc3_gadget_exit - Shut down gadget mode operation
 * @dwc: Pointer to DWC3 controller context
 *
 * Disables and cleans up gadget mode resources, usually done by
 * the dwc3 USB role switch callback before switching controller mode.
 * It must only be called when skip_core_init_mode is used and only after
 * dwc3_gadget_init.
 */
void dwc3_gadget_exit(struct dwc3 *dwc);

/**
 * dwc3_enable_susphy - Control SUSPHY status for all USB ports
 * @dwc: Pointer to DWC3 controller context
 * @enable: True to enable SUSPHY, false to disable
 *
 * Enables or disables the USB3 PHY SUSPEND and USB2 PHY SUSPHY feature for
 * all available ports.
 * This is usually handled by the dwc3 core code and should only be used
 * when skip_core_init_mode is used and the glue layer needs to manage SUSPHY
 * settings itself, e.g., due to platform-specific requirements during mode
 * switches.
 */
void dwc3_enable_susphy(struct dwc3 *dwc, bool enable);

/**
 * dwc3_set_prtcap - Set the USB controller PRTCAP mode
 * @dwc: Pointer to DWC3 controller context
 * @mode: Target mode, must be one of DWC3_GCTL_PRTCAP_{HOST,DEVICE,OTG}
 * @ignore_susphy: If true, skip disabling the SUSPHY and keep the current state
 *
 * Updates PRTCAP of the controller and current_dr_role inside the dwc3
 * structure. For DRD controllers, this also disables SUSPHY unless explicitly
 * told to skip via the ignore_susphy parameter.
 *
 * This is usually handled by the dwc3 core code and should only be used
 * when skip_core_init_mode is used and the glue layer needs to manage mode
 * transitions itself due to platform-specific requirements. It must be called
 * with the correct mode before calling dwc3_host_init or dwc3_gadget_init.
 */
void dwc3_set_prtcap(struct dwc3 *dwc, u32 mode, bool ignore_susphy);

#endif
