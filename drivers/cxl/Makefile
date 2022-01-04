# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CXL_BUS) += cxl_core.o
obj-$(CONFIG_CXL_MEM) += cxl_pci.o
obj-$(CONFIG_CXL_ACPI) += cxl_acpi.o
obj-$(CONFIG_CXL_PMEM) += cxl_pmem.o

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=CXL
cxl_core-y := core.o
cxl_pci-y := pci.o
cxl_acpi-y := acpi.o
cxl_pmem-y := pmem.o
