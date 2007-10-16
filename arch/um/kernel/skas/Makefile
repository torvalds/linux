#
# Copyright (C) 2002 - 2004 Jeff Dike (jdike@addtoit.com)
# Licensed under the GPL
#

obj-y := clone.o mmu.o process.o syscall.o uaccess.o

# clone.o is in the stub, so it can't be built with profiling
# GCC hardened also auto-enables -fpic, but we need %ebx so it can't work ->
# disable it

CFLAGS_clone.o := $(CFLAGS_NO_HARDENING)
UNPROFILE_OBJS := clone.o

include arch/um/scripts/Makefile.rules
