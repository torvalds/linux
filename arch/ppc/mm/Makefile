#
# Makefile for the linux ppc-specific parts of the memory manager.
#

obj-y				:= fault.o init.o mem_pieces.o \
					mmu_context.o pgtable.o

obj-$(CONFIG_PPC_STD_MMU)	+= hashtable.o ppc_mmu.o tlb.o
obj-$(CONFIG_40x)		+= 4xx_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_FSL_BOOKE)		+= fsl_booke_mmu.o
