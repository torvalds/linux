# 
# Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
# Licensed under the GPL
#

extra-y := unmap_fin.o
targets := unmap.o
clean-files := unmap_tmp.o

obj-y = exec_kern.o exec_user.o gdb.o ksyms.o mem.o mem_user.o process_kern.o \
	syscall_kern.o syscall_user.o time.o tlb.o tracer.o trap_user.o \
	uaccess.o uaccess_user.o

obj-$(CONFIG_PT_PROXY) += gdb_kern.o ptproxy/

USER_OBJS := gdb.o time.o tracer.o

include arch/um/scripts/Makefile.rules

UNMAP_CFLAGS := $(patsubst -pg -DPROFILING,,$(USER_CFLAGS))
UNMAP_CFLAGS := $(patsubst -fprofile-arcs -ftest-coverage,,$(UNMAP_CFLAGS))

#XXX: partially copied from arch/um/scripts/Makefile.rules
$(obj)/unmap.o: c_flags = -Wp,-MD,$(depfile) $(UNMAP_CFLAGS)

$(obj)/unmap_fin.o : $(obj)/unmap.o
	$(LD) -r -o $(obj)/unmap_tmp.o $< $(shell $(CC) -print-file-name=libc.a)
	$(OBJCOPY) $(obj)/unmap_tmp.o $@ -G switcheroo

