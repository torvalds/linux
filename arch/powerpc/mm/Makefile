#
# Makefile for the linux ppc-specific parts of the memory manager.
#

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
endif

obj-y				:= fault.o mem.o pgtable.o gup.o \
				   init_$(CONFIG_WORD_SIZE).o \
				   pgtable_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_PPC_MMU_NOHASH)	+= mmu_context_nohash.o tlb_nohash.o \
				   tlb_nohash_low.o
obj-$(CONFIG_PPC_BOOK3E)	+= tlb_low_$(CONFIG_WORD_SIZE)e.o
obj-$(CONFIG_PPC64)		+= mmap_64.o
hash64-$(CONFIG_PPC_NATIVE)	:= hash_native_64.o
obj-$(CONFIG_PPC_STD_MMU_64)	+= hash_utils_64.o \
				   slb_low.o slb.o stab.o \
				   mmap_64.o $(hash64-y)
obj-$(CONFIG_PPC_STD_MMU_32)	+= ppc_mmu_32.o
obj-$(CONFIG_PPC_STD_MMU)	+= hash_low_$(CONFIG_WORD_SIZE).o \
				   tlb_hash$(CONFIG_WORD_SIZE).o \
				   mmu_context_hash$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_40x)		+= 40x_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
obj-$(CONFIG_NEED_MULTIPLE_NODES) += numa.o
obj-$(CONFIG_PPC_MM_SLICES)	+= slice.o
ifeq ($(CONFIG_HUGETLB_PAGE),y)
obj-y				+= hugetlbpage.o
obj-$(CONFIG_PPC_STD_MMU_64)	+= hugetlbpage-hash64.o
endif
obj-$(CONFIG_PPC_SUBPAGE_PROT)	+= subpage-prot.o
obj-$(CONFIG_NOT_COHERENT_CACHE) += dma-noncoherent.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o
