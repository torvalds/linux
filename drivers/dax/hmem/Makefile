# SPDX-License-Identifier: GPL-2.0
# device_hmem.o deliberately precedes dax_hmem.o for initcall ordering
obj-$(CONFIG_DEV_DAX_HMEM_DEVICES) += device_hmem.o
obj-$(CONFIG_DEV_DAX_HMEM) += dax_hmem.o

device_hmem-y := device.o
dax_hmem-y := hmem.o
