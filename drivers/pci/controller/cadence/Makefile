# SPDX-License-Identifier: GPL-2.0
pcie-cadence-mod-y := pcie-cadence-hpa.o pcie-cadence.o
pcie-cadence-host-mod-y := pcie-cadence-host-common.o pcie-cadence-host.o pcie-cadence-host-hpa.o
pcie-cadence-ep-mod-y := pcie-cadence-ep.o

obj-$(CONFIG_PCIE_CADENCE) = pcie-cadence-mod.o
obj-$(CONFIG_PCIE_CADENCE_HOST) += pcie-cadence-host-mod.o
obj-$(CONFIG_PCIE_CADENCE_EP) += pcie-cadence-ep-mod.o
obj-$(CONFIG_PCIE_CADENCE_PLAT) += pcie-cadence-plat.o
obj-$(CONFIG_PCI_J721E) += pci-j721e.o
obj-$(CONFIG_PCIE_SG2042_HOST) += pcie-sg2042.o
obj-$(CONFIG_PCI_SKY1_HOST) += pci-sky1.o
