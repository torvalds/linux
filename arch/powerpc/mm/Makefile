#
# Makefile for the linux ppc-specific parts of the memory manager.
#

obj-y				:= fault.o mem.o lmb.o
obj-$(CONFIG_PPC32)		+= init_32.o pgtable_32.o mmu_context_32.o \
				   tlb_32.o
obj-$(CONFIG_PPC64)		+= init_64.o pgtable_64.o mmu_context_64.o
obj-$(CONFIG_PPC_STD_MMU_32)	+= ppc_mmu_32.o hash_low_32.o
obj-$(CONFIG_40x)		+= 4xx_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
