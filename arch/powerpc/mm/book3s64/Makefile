# SPDX-License-Identifier: GPL-2.0

obj-y				+= mmu_context.o pgtable.o trace.o
ifdef CONFIG_PPC_64S_HASH_MMU
CFLAGS_REMOVE_slb.o = $(CC_FLAGS_FTRACE)
obj-y				+= hash_pgtable.o hash_utils.o hash_tlb.o slb.o slice.o
obj-$(CONFIG_PPC_HASH_MMU_NATIVE)	+= hash_native.o
obj-$(CONFIG_PPC_4K_PAGES)	+= hash_4k.o
obj-$(CONFIG_PPC_64K_PAGES)	+= hash_64k.o
obj-$(CONFIG_TRANSPARENT_HUGEPAGE) += hash_hugepage.o
obj-$(CONFIG_PPC_SUBPAGE_PROT)	+= subpage_prot.o
endif

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o

obj-$(CONFIG_PPC_RADIX_MMU)	+= radix_pgtable.o radix_tlb.o
ifdef CONFIG_HUGETLB_PAGE
obj-$(CONFIG_PPC_RADIX_MMU)	+= radix_hugetlbpage.o
endif
obj-$(CONFIG_SPAPR_TCE_IOMMU)	+= iommu_api.o
obj-$(CONFIG_PPC_PKEY)	+= pkeys.o

# Instrumenting the SLB fault path can lead to duplicate SLB entries
KCOV_INSTRUMENT_slb.o := n

# Parts of these can run in real mode and therefore are
# not safe with the current outline KASAN implementation
KASAN_SANITIZE_mmu_context.o := n
KASAN_SANITIZE_pgtable.o := n
KASAN_SANITIZE_radix_pgtable.o := n
KASAN_SANITIZE_radix_tlb.o := n
KASAN_SANITIZE_slb.o := n
KASAN_SANITIZE_pkeys.o := n
