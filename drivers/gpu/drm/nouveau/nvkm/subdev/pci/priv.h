/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_PCI_PRIV_H__
#define __NVKM_PCI_PRIV_H__
#define nvkm_pci(p) container_of((p), struct nvkm_pci, subdev)
#include <subdev/pci.h>

int nvkm_pci_new_(const struct nvkm_pci_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		  struct nvkm_pci **);

struct nvkm_pci_func {
	struct {
		u32 addr;
		u16 size;
	} cfg;

	void (*init)(struct nvkm_pci *);
	void (*msi_rearm)(struct nvkm_pci *);

	struct {
		int (*init)(struct nvkm_pci *);
		int (*set_link)(struct nvkm_pci *, enum nvkm_pcie_speed, u8);

		enum nvkm_pcie_speed (*max_speed)(struct nvkm_pci *);
		enum nvkm_pcie_speed (*cur_speed)(struct nvkm_pci *);

		void (*set_version)(struct nvkm_pci *, u8);
		int (*version)(struct nvkm_pci *);
		int (*version_supported)(struct nvkm_pci *);
	} pcie;
};

void nv40_pci_msi_rearm(struct nvkm_pci *);

void nv46_pci_msi_rearm(struct nvkm_pci *);

void g84_pci_init(struct nvkm_pci *pci);

/* pcie functions */
void g84_pcie_set_version(struct nvkm_pci *, u8);
int g84_pcie_version(struct nvkm_pci *);
void g84_pcie_set_link_speed(struct nvkm_pci *, enum nvkm_pcie_speed);
enum nvkm_pcie_speed g84_pcie_cur_speed(struct nvkm_pci *);
enum nvkm_pcie_speed g84_pcie_max_speed(struct nvkm_pci *);
int g84_pcie_init(struct nvkm_pci *);
int g84_pcie_set_link(struct nvkm_pci *, enum nvkm_pcie_speed, u8);

int g92_pcie_version_supported(struct nvkm_pci *);

void gf100_pcie_set_version(struct nvkm_pci *, u8);
int gf100_pcie_version(struct nvkm_pci *);
void gf100_pcie_set_cap_speed(struct nvkm_pci *, bool);
int gf100_pcie_cap_speed(struct nvkm_pci *);
int gf100_pcie_init(struct nvkm_pci *);
int gf100_pcie_set_link(struct nvkm_pci *, enum nvkm_pcie_speed, u8);

int nvkm_pcie_oneinit(struct nvkm_pci *);
int nvkm_pcie_init(struct nvkm_pci *);
#endif
