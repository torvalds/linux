# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

obj-y := stub.o mmu.o process.o syscall.o uaccess.o

# stub.o is in the stub, so it can't be built with profiling
# GCC hardened also auto-enables -fpic, but we need %ebx so it can't work ->
# disable it

CFLAGS_stub.o := $(CFLAGS_NO_HARDENING)
UNPROFILE_OBJS := stub.o
KCOV_INSTRUMENT := n

include $(srctree)/arch/um/scripts/Makefile.rules
