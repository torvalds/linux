# SPDX-License-Identifier: GPL-2.0
# Makefile for the linux Sparc-specific parts of the memory manager.
#

asflags-y := -ansi

obj-$(CONFIG_SPARC64)   += ultra.o tlb.o tsb.o
obj-y                   += fault_$(BITS).o
obj-y                   += init_$(BITS).o
obj-$(CONFIG_SPARC32)   += srmmu.o iommu.o io-unit.o
obj-$(CONFIG_SPARC32)   += srmmu_access.o
obj-$(CONFIG_SPARC32)   += hypersparc.o viking.o tsunami.o swift.o
obj-$(CONFIG_SPARC32)   += leon_mm.o

# Only used by sparc64
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
