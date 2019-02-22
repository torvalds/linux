# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux ppc-specific parts of the memory manager.
#

ccflags-$(CONFIG_PPC64)	:= $(NO_MINIMAL_TOC)

CFLAGS_REMOVE_slb.o = $(CC_FLAGS_FTRACE)

obj-y				:= fault.o mem.o pgtable.o mmap.o \
				   init_$(BITS).o pgtable_$(BITS).o \
				   init-common.o mmu_context.o drmem.o
obj-$(CONFIG_PPC_MMU_NOHASH)	+= mmu_context_nohash.o tlb_nohash.o \
				   tlb_nohash_low.o
obj-$(CONFIG_PPC_BOOK3E)	+= tlb_low_$(BITS)e.o
hash64-$(CONFIG_PPC_NATIVE)	:= hash_native_64.o
obj-$(CONFIG_PPC_BOOK3E_64)   += pgtable-book3e.o
obj-$(CONFIG_PPC_BOOK3S_64)	+= pgtable-hash64.o hash_utils_64.o slb.o \
				   $(hash64-y) mmu_context_book3s64.o \
				   pgtable-book3s64.o pgtable-frag.o
obj-$(CONFIG_PPC32)		+= pgtable-frag.o
obj-$(CONFIG_PPC_RADIX_MMU)	+= pgtable-radix.o tlb-radix.o
obj-$(CONFIG_PPC_BOOK3S_32)	+= ppc_mmu_32.o hash_low_32.o mmu_context_hash32.o
obj-$(CONFIG_PPC_BOOK3S)	+= tlb_hash$(BITS).o
ifdef CONFIG_PPC_BOOK3S_64
obj-$(CONFIG_PPC_4K_PAGES)	+= hash64_4k.o
obj-$(CONFIG_PPC_64K_PAGES)	+= hash64_64k.o
endif
obj-$(CONFIG_40x)		+= 40x_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_PPC_8xx)		+= 8xx_mmu.o
obj-$(CONFIG_PPC_FSL_BOOK3E)	+= fsl_booke_mmu.o
obj-$(CONFIG_NEED_MULTIPLE_NODES) += numa.o
obj-$(CONFIG_PPC_SPLPAR)	+= vphn.o
obj-$(CONFIG_PPC_MM_SLICES)	+= slice.o
obj-y				+= hugetlbpage.o
ifdef CONFIG_HUGETLB_PAGE
obj-$(CONFIG_PPC_BOOK3S_64)	+= hugetlbpage-hash64.o
obj-$(CONFIG_PPC_RADIX_MMU)	+= hugetlbpage-radix.o
obj-$(CONFIG_PPC_BOOK3E_MMU)	+= hugetlbpage-book3e.o
endif
obj-$(CONFIG_TRANSPARENT_HUGEPAGE) += hugepage-hash64.o
obj-$(CONFIG_PPC_SUBPAGE_PROT)	+= subpage-prot.o
obj-$(CONFIG_NOT_COHERENT_CACHE) += dma-noncoherent.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o
obj-$(CONFIG_PPC_COPRO_BASE)	+= copro_fault.o
obj-$(CONFIG_SPAPR_TCE_IOMMU)	+= mmu_context_iommu.o
obj-$(CONFIG_PPC_PTDUMP)	+= ptdump/
obj-$(CONFIG_PPC_MEM_KEYS)	+= pkeys.o

# Disable kcov instrumentation on sensitive code
# This is necessary for booting with kcov enabled on book3e machines
KCOV_INSTRUMENT_tlb_nohash.o := n
KCOV_INSTRUMENT_fsl_booke_mmu.o := n
