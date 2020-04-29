# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DAX) += dax.o
obj-$(CONFIG_DEV_DAX) += device_dax.o
obj-$(CONFIG_DEV_DAX_KMEM) += kmem.o
obj-$(CONFIG_DEV_DAX_HMEM) += dax_hmem.o

dax-y := super.o
dax-y += bus.o
device_dax-y := device.o
dax_hmem-y := hmem.o

obj-y += pmem/
