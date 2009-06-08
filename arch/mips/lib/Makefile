#
# Makefile for MIPS-specific library files..
#

lib-y	+= csum_partial.o delay.o memcpy.o memcpy-inatomic.o memset.o \
	   strlen_user.o strncpy_user.o strnlen_user.o uncached.o

obj-y			+= iomap.o
obj-$(CONFIG_PCI)	+= iomap-pci.o

obj-$(CONFIG_CPU_LOONGSON2)	+= dump_tlb.o
obj-$(CONFIG_CPU_MIPS32)	+= dump_tlb.o
obj-$(CONFIG_CPU_MIPS64)	+= dump_tlb.o
obj-$(CONFIG_CPU_NEVADA)	+= dump_tlb.o
obj-$(CONFIG_CPU_R10000)	+= dump_tlb.o
obj-$(CONFIG_CPU_R3000)		+= r3k_dump_tlb.o
obj-$(CONFIG_CPU_R4300)		+= dump_tlb.o
obj-$(CONFIG_CPU_R4X00)		+= dump_tlb.o
obj-$(CONFIG_CPU_R5000)		+= dump_tlb.o
obj-$(CONFIG_CPU_R5432)		+= dump_tlb.o
obj-$(CONFIG_CPU_R5500)		+= dump_tlb.o
obj-$(CONFIG_CPU_R6000)		+=
obj-$(CONFIG_CPU_R8000)		+=
obj-$(CONFIG_CPU_RM7000)	+= dump_tlb.o
obj-$(CONFIG_CPU_RM9000)	+= dump_tlb.o
obj-$(CONFIG_CPU_SB1)		+= dump_tlb.o
obj-$(CONFIG_CPU_TX39XX)	+= r3k_dump_tlb.o
obj-$(CONFIG_CPU_TX49XX)	+= dump_tlb.o
obj-$(CONFIG_CPU_VR41XX)	+= dump_tlb.o
obj-$(CONFIG_CPU_CAVIUM_OCTEON)	+= dump_tlb.o

# libgcc-style stuff needed in the kernel
obj-y += ashldi3.o ashrdi3.o cmpdi2.o lshrdi3.o ucmpdi2.o
