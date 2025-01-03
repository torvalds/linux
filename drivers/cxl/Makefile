# SPDX-License-Identifier: GPL-2.0

# Order is important here for the built-in case:
# - 'core' first for fundamental init
# - 'port' before platform root drivers like 'acpi' so that CXL-root ports
#   are immediately enabled
# - 'mem' and 'pmem' before endpoint drivers so that memdevs are
#   immediately enabled
# - 'pci' last, also mirrors the hardware enumeration hierarchy
obj-y += core/
obj-$(CONFIG_CXL_PORT) += cxl_port.o
obj-$(CONFIG_CXL_ACPI) += cxl_acpi.o
obj-$(CONFIG_CXL_PMEM) += cxl_pmem.o
obj-$(CONFIG_CXL_MEM) += cxl_mem.o
obj-$(CONFIG_CXL_PCI) += cxl_pci.o

cxl_port-y := port.o
cxl_acpi-y := acpi.o
cxl_pmem-y := pmem.o security.o
cxl_mem-y := mem.o
cxl_pci-y := pci.o
