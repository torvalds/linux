#ifndef __NOUVEAU_ACPI_H__
#define __NOUVEAU_ACPI_H__

#define ROM_BIOS_PAGE 4096

#if defined(CONFIG_ACPI)
bool nouveau_is_optimus(void);
bool nouveau_is_v1_dsm(void);
void nouveau_register_dsm_handler(void);
void nouveau_unregister_dsm_handler(void);
void nouveau_switcheroo_optimus_dsm(void);
int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len);
bool nouveau_acpi_rom_supported(struct pci_dev *pdev);
void *nouveau_acpi_edid(struct drm_device *, struct drm_connector *);
#else
static inline bool nouveau_is_optimus(void) { return false; };
static inline bool nouveau_is_v1_dsm(void) { return false; };
static inline void nouveau_register_dsm_handler(void) {}
static inline void nouveau_unregister_dsm_handler(void) {}
static inline void nouveau_switcheroo_optimus_dsm(void) {}
static inline bool nouveau_acpi_rom_supported(struct pci_dev *pdev) { return false; }
static inline int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len) { return -EINVAL; }
static inline void *nouveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector) { return NULL; }
#endif

#endif
