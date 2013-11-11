# Makefile for xmon

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

GCOV_PROFILE := n

ccflags-$(CONFIG_PPC64) := $(NO_MINIMAL_TOC)

obj-y			+= xmon.o nonstdio.o

ifdef CONFIG_XMON_DISASSEMBLY
obj-y			+= ppc-dis.o ppc-opc.o
obj-$(CONFIG_SPU_BASE)	+= spu-dis.o spu-opc.o
endif
