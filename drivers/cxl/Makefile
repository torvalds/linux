# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CXL_BUS) += cxl_core.o
obj-$(CONFIG_CXL_MEM) += cxl_mem.o

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=CXL
cxl_core-y := core.o
cxl_mem-y := mem.o
