# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CXL_BUS) += cxl_core.o
obj-$(CONFIG_CXL_SUSPEND) += suspend.o

ccflags-y += -I$(srctree)/drivers/cxl
CFLAGS_trace.o = -DTRACE_INCLUDE_PATH=. -I$(src)

cxl_core-y := port.o
cxl_core-y += pmem.o
cxl_core-y += regs.o
cxl_core-y += memdev.o
cxl_core-y += mbox.o
cxl_core-y += pci.o
cxl_core-y += hdm.o
cxl_core-$(CONFIG_TRACING) += trace.o
cxl_core-$(CONFIG_CXL_REGION) += region.o
