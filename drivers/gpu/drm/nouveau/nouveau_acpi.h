/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_ACPI_H__
#define __NOUVEAU_ACPI_H__

#define ROM_BIOS_PAGE 4096

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
bool yesuveau_is_optimus(void);
bool yesuveau_is_v1_dsm(void);
void yesuveau_register_dsm_handler(void);
void yesuveau_unregister_dsm_handler(void);
void yesuveau_switcheroo_optimus_dsm(void);
int yesuveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len);
bool yesuveau_acpi_rom_supported(struct device *);
void *yesuveau_acpi_edid(struct drm_device *, struct drm_connector *);
#else
static inline bool yesuveau_is_optimus(void) { return false; };
static inline bool yesuveau_is_v1_dsm(void) { return false; };
static inline void yesuveau_register_dsm_handler(void) {}
static inline void yesuveau_unregister_dsm_handler(void) {}
static inline void yesuveau_switcheroo_optimus_dsm(void) {}
static inline bool yesuveau_acpi_rom_supported(struct device *dev) { return false; }
static inline int yesuveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len) { return -EINVAL; }
static inline void *yesuveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector) { return NULL; }
#endif

#endif
