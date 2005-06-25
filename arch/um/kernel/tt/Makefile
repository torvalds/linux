# 
# Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
# Licensed under the GPL
#

obj-y = exec_kern.o exec_user.o gdb.o ksyms.o mem.o mem_user.o process_kern.o \
	syscall_kern.o syscall_user.o tlb.o tracer.o trap_user.o \
	uaccess.o uaccess_user.o

obj-$(CONFIG_PT_PROXY) += gdb_kern.o ptproxy/

USER_OBJS := gdb.o tracer.o

include arch/um/scripts/Makefile.rules
