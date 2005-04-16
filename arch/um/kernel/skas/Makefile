# 
# Copyright (C) 2002 - 2004 Jeff Dike (jdike@addtoit.com)
# Licensed under the GPL
#

obj-y := exec_kern.o mem.o mem_user.o mmu.o process.o process_kern.o \
	syscall_kern.o syscall_user.o time.o tlb.o trap_user.o uaccess.o \

subdir- := util

USER_OBJS := process.o time.o

include arch/um/scripts/Makefile.rules
