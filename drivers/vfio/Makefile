# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VFIO) += vfio.o

vfio-y += vfio_main.o
vfio-$(CONFIG_VFIO_DEVICE_CDEV) += device_cdev.o
vfio-$(CONFIG_VFIO_GROUP) += group.o
vfio-$(CONFIG_IOMMUFD) += iommufd.o
vfio-$(CONFIG_VFIO_CONTAINER) += container.o
vfio-$(CONFIG_VFIO_VIRQFD) += virqfd.o

obj-$(CONFIG_VFIO_IOMMU_TYPE1) += vfio_iommu_type1.o
obj-$(CONFIG_VFIO_IOMMU_SPAPR_TCE) += vfio_iommu_spapr_tce.o
obj-$(CONFIG_VFIO_PCI_CORE) += pci/
obj-$(CONFIG_VFIO_PLATFORM_BASE) += platform/
obj-$(CONFIG_VFIO_MDEV) += mdev/
obj-$(CONFIG_VFIO_FSL_MC) += fsl-mc/
obj-$(CONFIG_VFIO_CDX) += cdx/
