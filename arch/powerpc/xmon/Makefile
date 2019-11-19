# SPDX-License-Identifier: GPL-2.0
# Makefile for xmon

# Avoid clang warnings around longjmp/setjmp declarations
subdir-ccflags-y := -ffreestanding

subdir-ccflags-$(CONFIG_PPC_WERROR) += -Werror

GCOV_PROFILE := n
UBSAN_SANITIZE := n

# Disable ftrace for the entire directory
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst $(CC_FLAGS_FTRACE),,$(ORIG_CFLAGS))

ifdef CONFIG_CC_IS_CLANG
# clang stores addresses on the stack causing the frame size to blow
# out. See https://github.com/ClangBuiltLinux/linux/issues/252
KBUILD_CFLAGS += -Wframe-larger-than=4096
endif

ccflags-$(CONFIG_PPC64) := $(NO_MINIMAL_TOC)

obj-y			+= xmon.o nonstdio.o spr_access.o

ifdef CONFIG_XMON_DISASSEMBLY
obj-y			+= ppc-dis.o ppc-opc.o
obj-$(CONFIG_SPU_BASE)	+= spu-dis.o spu-opc.o
endif
