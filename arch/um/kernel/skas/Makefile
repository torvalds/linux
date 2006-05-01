#
# Copyright (C) 2002 - 2004 Jeff Dike (jdike@addtoit.com)
# Licensed under the GPL
#

obj-y := clone.o exec_kern.o mem.o mmu.o process_kern.o \
	syscall.o tlb.o uaccess.o

USER_OBJS := clone.o

include arch/um/scripts/Makefile.rules

# clone.o is in the stub, so it can't be built with profiling
# GCC hardened also auto-enables -fpic, but we need %ebx so it can't work ->
# disable it

CFLAGS_clone.o := $(CFLAGS_NO_HARDENING)

# since we're setting c_flags we _must_ add $(CFLAGS_$(*F).o).

$(obj)/clone.o : c_flags = -Wp,-MD,$(depfile) $(call unprofile,$(USER_CFLAGS)) $(CFLAGS_$(*F).o)
