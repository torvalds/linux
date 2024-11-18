/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ACPI_H__
#define __INTEL_ACPI_H__

struct intel_display;

#ifdef CONFIG_ACPI
void intel_register_dsm_handler(void);
void intel_unregister_dsm_handler(void);
void intel_dsm_get_bios_data_funcs_supported(struct intel_display *display);
void intel_acpi_device_id_update(struct intel_display *display);
void intel_acpi_assign_connector_fwnodes(struct intel_display *display);
void intel_acpi_video_register(struct intel_display *display);
#else
static inline void intel_register_dsm_handler(void) { return; }
static inline void intel_unregister_dsm_handler(void) { return; }
static inline
void intel_dsm_get_bios_data_funcs_supported(struct intel_display *display) { return; }
static inline
void intel_acpi_device_id_update(struct intel_display *display) { return; }
static inline
void intel_acpi_assign_connector_fwnodes(struct intel_display *display) { return; }
static inline
void intel_acpi_video_register(struct intel_display *display) { return; }
#endif /* CONFIG_ACPI */

#endif /* __INTEL_ACPI_H__ */
