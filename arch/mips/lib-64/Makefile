#
# Makefile for MIPS-specific library files..
#

lib-y	+= memset.o watch.o

obj-$(CONFIG_CPU_MIPS32)	+= dump_tlb.o
obj-$(CONFIG_CPU_MIPS64)	+= dump_tlb.o
obj-$(CONFIG_CPU_NEVADA)	+= dump_tlb.o
obj-$(CONFIG_CPU_R10000)	+= dump_tlb.o
obj-$(CONFIG_CPU_R3000)		+= r3k_dump_tlb.o
obj-$(CONFIG_CPU_R4300)		+= dump_tlb.o
obj-$(CONFIG_CPU_R4X00)		+= dump_tlb.o
obj-$(CONFIG_CPU_R5000)		+= dump_tlb.o
obj-$(CONFIG_CPU_R5432)		+= dump_tlb.o
obj-$(CONFIG_CPU_R6000)		+=
obj-$(CONFIG_CPU_R8000)		+=
obj-$(CONFIG_CPU_RM7000)	+= dump_tlb.o
obj-$(CONFIG_CPU_RM9000)	+= dump_tlb.o
obj-$(CONFIG_CPU_SB1)		+= dump_tlb.o
obj-$(CONFIG_CPU_TX39XX)	+= r3k_dump_tlb.o
obj-$(CONFIG_CPU_TX49XX)	+= dump_tlb.o
obj-$(CONFIG_CPU_VR41XX)	+= dump_tlb.o

EXTRA_AFLAGS := $(CFLAGS)
