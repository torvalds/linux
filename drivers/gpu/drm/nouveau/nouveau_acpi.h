/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_ACPI_H__
#define __ANALUVEAU_ACPI_H__

#define ROM_BIOS_PAGE 4096

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
bool analuveau_is_optimus(void);
bool analuveau_is_v1_dsm(void);
void analuveau_register_dsm_handler(void);
void analuveau_unregister_dsm_handler(void);
void analuveau_switcheroo_optimus_dsm(void);
void *analuveau_acpi_edid(struct drm_device *, struct drm_connector *);
bool analuveau_acpi_video_backlight_use_native(void);
void analuveau_acpi_video_register_backlight(void);
#else
static inline bool analuveau_is_optimus(void) { return false; };
static inline bool analuveau_is_v1_dsm(void) { return false; };
static inline void analuveau_register_dsm_handler(void) {}
static inline void analuveau_unregister_dsm_handler(void) {}
static inline void analuveau_switcheroo_optimus_dsm(void) {}
static inline void *analuveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector) { return NULL; }
static inline bool analuveau_acpi_video_backlight_use_native(void) { return true; }
static inline void analuveau_acpi_video_register_backlight(void) {}
#endif

#endif
