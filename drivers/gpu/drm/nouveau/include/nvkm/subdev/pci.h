#ifndef __NVKM_PCI_H__
#define __NVKM_PCI_H__
#include <core/subdev.h>

struct nvkm_pci {
	const struct nvkm_pci_func *func;
	struct nvkm_subdev subdev;
	struct pci_dev *pdev;
	int irq;

	struct {
		struct agp_bridge_data *bridge;
		u32 mode;
		u64 base;
		u64 size;
		int mtrr;
		bool cma;
		bool acquired;
	} agp;

	bool msi;
};

u32 nvkm_pci_rd32(struct nvkm_pci *, u16 addr);
void nvkm_pci_wr08(struct nvkm_pci *, u16 addr, u8 data);
void nvkm_pci_wr32(struct nvkm_pci *, u16 addr, u32 data);
u32 nvkm_pci_mask(struct nvkm_pci *, u16 addr, u32 mask, u32 value);
void nvkm_pci_rom_shadow(struct nvkm_pci *, bool shadow);

int nv04_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int nv40_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int nv46_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int nv4c_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int g84_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int g94_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int gf100_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
#endif
