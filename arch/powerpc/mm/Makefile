#
# Makefile for the linux ppc-specific parts of the memory manager.
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
endif

obj-y				:= fault.o mem.o lmb.o
obj-$(CONFIG_PPC32)		+= init_32.o pgtable_32.o mmu_context_32.o
hash-$(CONFIG_PPC_NATIVE)	:= hash_native_64.o
obj-$(CONFIG_PPC64)		+= init_64.o pgtable_64.o mmu_context_64.o \
				   hash_utils_64.o hash_low_64.o tlb_64.o \
				   slb_low.o slb.o stab.o mmap.o imalloc.o \
				   $(hash-y)
obj-$(CONFIG_PPC_STD_MMU_32)	+= ppc_mmu_32.o hash_low_32.o tlb_32.o
obj-$(CONFIG_40x)		+= 4xx_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
obj-$(CONFIG_NEED_MULTIPLE_NODES) += numa.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
