# SPDX-License-Identifier: GPL-2.0

ccflags-y	:= $(NO_MINIMAL_TOC)

CFLAGS_REMOVE_slb.o = $(CC_FLAGS_FTRACE)

obj-y				+= hash_pgtable.o hash_utils.o slb.o \
				   mmu_context.o pgtable.o hash_tlb.o
obj-$(CONFIG_PPC_NATIVE)	+= hash_native.o
obj-$(CONFIG_PPC_RADIX_MMU)	+= radix_pgtable.o radix_tlb.o
obj-$(CONFIG_PPC_4K_PAGES)	+= hash_4k.o
obj-$(CONFIG_PPC_64K_PAGES)	+= hash_64k.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hash_hugetlbpage.o
ifdef CONFIG_HUGETLB_PAGE
obj-$(CONFIG_PPC_RADIX_MMU)	+= radix_hugetlbpage.o
endif
obj-$(CONFIG_TRANSPARENT_HUGEPAGE) += hash_hugepage.o
obj-$(CONFIG_PPC_SUBPAGE_PROT)	+= subpage_prot.o
obj-$(CONFIG_SPAPR_TCE_IOMMU)	+= iommu_api.o
obj-$(CONFIG_PPC_MEM_KEYS)	+= pkeys.o

# Instrumenting the SLB fault path can lead to duplicate SLB entries
KCOV_INSTRUMENT_slb.o := n
