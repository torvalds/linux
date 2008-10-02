#
# Makefile for the linux ppc-specific parts of the memory manager.
#

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS	+= -mno-minimal-toc
endif

obj-y				:= fault.o mem.o \
				   init_$(CONFIG_WORD_SIZE).o \
				   pgtable_$(CONFIG_WORD_SIZE).o \
				   mmu_context_$(CONFIG_WORD_SIZE).o
hash-$(CONFIG_PPC_NATIVE)	:= hash_native_64.o
obj-$(CONFIG_PPC64)		+= hash_utils_64.o \
				   slb_low.o slb.o stab.o \
				   gup.o mmap.o $(hash-y)
obj-$(CONFIG_PPC_STD_MMU_32)	+= ppc_mmu_32.o
obj-$(CONFIG_PPC_STD_MMU)	+= hash_low_$(CONFIG_WORD_SIZE).o \
				   tlb_$(CONFIG_WORD_SIZE).o
obj-$(CONFIG_40x)		+= 40x_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
obj-$(CONFIG_NEED_MULTIPLE_NODES) += numa.o
obj-$(CONFIG_PPC_MM_SLICES)	+= slice.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PPC_SUBPAGE_PROT)	+= subpage-prot.o
