# SPDX-License-Identifier: GPL-2.0-only
iommufd-y := \
	device.o \
	fault.o \
	hw_pagetable.o \
	io_pagetable.o \
	ioas.o \
	main.o \
	pages.o \
	vfio_compat.o \
	viommu.o

iommufd-$(CONFIG_IOMMUFD_TEST) += selftest.o

obj-$(CONFIG_IOMMUFD) += iommufd.o
obj-$(CONFIG_IOMMUFD_DRIVER) += iova_bitmap.o

iommufd_driver-y := driver.o
obj-$(CONFIG_IOMMUFD_DRIVER_CORE) += iommufd_driver.o
