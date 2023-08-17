# SPDX-License-Identifier: GPL-2.0
# Makefile for xmon

GCOV_PROFILE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n
KCSAN_SANITIZE := n

# Disable ftrace for the entire directory
ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

ccflags-$(CONFIG_PPC64) := $(NO_MINIMAL_TOC)

obj-y			+= xmon.o nonstdio.o spr_access.o xmon_bpts.o

ifdef CONFIG_XMON_DISASSEMBLY
obj-y			+= ppc-dis.o ppc-opc.o
obj-$(CONFIG_SPU_BASE)	+= spu-dis.o spu-opc.o
endif
