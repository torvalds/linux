# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VFIO) += vfio.o

vfio-y += vfio_main.o \
	  group.o \
	  iova_bitmap.o
vfio-$(CONFIG_IOMMUFD) += iommufd.o
vfio-$(CONFIG_VFIO_CONTAINER) += container.o
vfio-$(CONFIG_VFIO_VIRQFD) += virqfd.o

obj-$(CONFIG_VFIO_IOMMU_TYPE1) += vfio_iommu_type1.o
obj-$(CONFIG_VFIO_IOMMU_SPAPR_TCE) += vfio_iommu_spapr_tce.o
obj-$(CONFIG_VFIO_PCI) += pci/
obj-$(CONFIG_VFIO_PLATFORM) += platform/
obj-$(CONFIG_VFIO_MDEV) += mdev/
obj-$(CONFIG_VFIO_FSL_MC) += fsl-mc/
