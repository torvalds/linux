# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CXL_BUS) += cxl_bus.o
obj-$(CONFIG_CXL_MEM) += cxl_mem.o

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=CXL
cxl_bus-y := bus.o
cxl_mem-y := mem.o
