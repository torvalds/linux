# SPDX-License-Identifier: GPL-2.0
#  Makefile for the Hexagon arch

KBUILD_DEFCONFIG = comet_defconfig

# Do not use GP-relative jumps
KBUILD_CFLAGS += -G0
LDFLAGS_vmlinux += -G0

# Do not use single-byte enums; these will overflow.
KBUILD_CFLAGS += -fno-short-enums

# We must use long-calls:
KBUILD_CFLAGS += -mlong-calls

# Modules must use either long-calls, or use pic/plt.
# Use long-calls for now, it's easier.  And faster.
# KBUILD_CFLAGS_MODULE += -fPIC
# KBUILD_LDFLAGS_MODULE += -shared
KBUILD_CFLAGS_MODULE += -mlong-calls

cflags-y += $(call cc-option,-mv${CONFIG_HEXAGON_ARCH_VERSION})
aflags-y += $(call cc-option,-mv${CONFIG_HEXAGON_ARCH_VERSION})
ldflags-y += $(call cc-option,-mv${CONFIG_HEXAGON_ARCH_VERSION})

KBUILD_CFLAGS += $(cflags-y)
KBUILD_AFLAGS += $(aflags-y)
KBUILD_LDFLAGS += $(ldflags-y)

# Thread-info register will be r19.  This value is not configureable;
# it is hard-coded in several files.
TIR_NAME := r19
KBUILD_CFLAGS += -ffixed-$(TIR_NAME) -DTHREADINFO_REG=$(TIR_NAME) -D__linux__
KBUILD_AFLAGS += -DTHREADINFO_REG=$(TIR_NAME)

head-y := arch/hexagon/kernel/head.o
