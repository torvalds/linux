#ifndef __NVKM_PCI_H__
#define __NVKM_PCI_H__
#include <core/subdev.h>

enum nvkm_pcie_speed {
	NVKM_PCIE_SPEED_2_5,
	NVKM_PCIE_SPEED_5_0,
	NVKM_PCIE_SPEED_8_0,
};

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

	struct {
		enum nvkm_pcie_speed speed;
		u8 width;
	} pcie;

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
int g92_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int g94_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int gf100_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int gf106_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int gk104_pci_new(struct nvkm_device *, int, struct nvkm_pci **);
int gp100_pci_new(struct nvkm_device *, int, struct nvkm_pci **);

/* pcie functions */
int nvkm_pcie_set_link(struct nvkm_pci *, enum nvkm_pcie_speed, u8 width);
#endif
