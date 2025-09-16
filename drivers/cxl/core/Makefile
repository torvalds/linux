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
cxl_core-y += pmu.o
cxl_core-y += cdat.o
cxl_core-y += ras.o
cxl_core-$(CONFIG_TRACING) += trace.o
cxl_core-$(CONFIG_CXL_REGION) += region.o
cxl_core-$(CONFIG_CXL_MCE) += mce.o
cxl_core-$(CONFIG_CXL_FEATURES) += features.o
cxl_core-$(CONFIG_CXL_EDAC_MEM_FEATURES) += edac.o
