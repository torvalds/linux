#include "priv.h"
#if defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE))
#ifndef __NVKM_PCI_AGP_H__
#define __NVKM_PCI_AGP_H__

void nvkm_agp_ctor(struct nvkm_pci *);
void nvkm_agp_dtor(struct nvkm_pci *);
void nvkm_agp_preinit(struct nvkm_pci *);
int nvkm_agp_init(struct nvkm_pci *);
void nvkm_agp_fini(struct nvkm_pci *);
#endif
#else
static inline void nvkm_agp_ctor(struct nvkm_pci *pci) {}
static inline void nvkm_agp_dtor(struct nvkm_pci *pci) {}
static inline void nvkm_agp_preinit(struct nvkm_pci *pci) {}
static inline int nvkm_agp_init(struct nvkm_pci *pci) { return -ENOSYS; }
static inline void nvkm_agp_fini(struct nvkm_pci *pci) {}
#endif
