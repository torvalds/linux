# SPDX-License-Identifier: GPL-2.0-only
iommufd-y := \
	device.o \
	hw_pagetable.o \
	io_pagetable.o \
	ioas.o \
	main.o \
	pages.o \
	vfio_compat.o

iommufd-$(CONFIG_IOMMUFD_TEST) += selftest.o

obj-$(CONFIG_IOMMUFD) += iommufd.o
