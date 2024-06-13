# SPDX-License-Identifier: GPL-2.0-only

vfio-pci-core-y := vfio_pci_core.o vfio_pci_intrs.o vfio_pci_rdwr.o vfio_pci_config.o
vfio-pci-core-$(CONFIG_VFIO_PCI_ZDEV_KVM) += vfio_pci_zdev.o
obj-$(CONFIG_VFIO_PCI_CORE) += vfio-pci-core.o

vfio-pci-y := vfio_pci.o
vfio-pci-$(CONFIG_VFIO_PCI_IGD) += vfio_pci_igd.o
obj-$(CONFIG_VFIO_PCI) += vfio-pci.o

obj-$(CONFIG_MLX5_VFIO_PCI)           += mlx5/

obj-$(CONFIG_HISI_ACC_VFIO_PCI) += hisilicon/
