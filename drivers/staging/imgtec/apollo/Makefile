# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_POWERVR_APOLLO) += apollo.o
apollo-y += apollo_drv.o ion_lma_heap.o pci_support.o
ccflags-y += -include drivers/staging/imgtec/config_kernel.h \
 -I$(src) \
 -Idrivers/staging/imgtec \
 -Idrivers/staging/imgtec/rogue \
 -Idrivers/staging/imgtec/rogue/hwdefs \
 -Idrivers/staging/imgtec/rogue/hwdefs/km
