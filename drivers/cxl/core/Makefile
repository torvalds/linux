# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CXL_BUS) += cxl_core.o

ccflags-y += -I$(srctree)/drivers/cxl
cxl_core-y := port.o
cxl_core-y += pmem.o
cxl_core-y += regs.o
cxl_core-y += memdev.o
cxl_core-y += mbox.o
cxl_core-y += pci.o
