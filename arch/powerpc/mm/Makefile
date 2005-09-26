#
# Makefile for the linux ppc-specific parts of the memory manager.
#

obj-y				:= fault.o mem.o
obj-$(CONFIG_PPC32)		+= init.o pgtable.o mmu_context.o \
				   mem_pieces.o tlb.o
obj-$(CONFIG_PPC64)		+= init64.o pgtable64.o mmu_context64.o
obj-$(CONFIG_PPC_STD_MMU_32)	+= ppc_mmu.o hash_32.o
obj-$(CONFIG_40x)		+= 4xx_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
