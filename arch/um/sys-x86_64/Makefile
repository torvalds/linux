#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

#XXX: why into lib-y?
lib-y = bitops.o bugs.o csum-partial.o delay.o fault.o ldt.o mem.o memcpy.o \
	ptrace.o ptrace_user.o sigcontext.o signal.o stub.o \
	stub_segv.o syscalls.o syscall_table.o sysrq.o thunk.o

obj-y := ksyms.o
obj-$(CONFIG_MODULES) += module.o um_module.o

USER_OBJS := ptrace_user.o sigcontext.o

SYMLINKS = bitops.c csum-copy.S csum-partial.c csum-wrappers.c ldt.c memcpy.S \
	thunk.S module.c

include arch/um/scripts/Makefile.rules

bitops.c-dir = lib
csum-copy.S-dir = lib
csum-partial.c-dir = lib
csum-wrappers.c-dir = lib
ldt.c-dir = /arch/um/sys-i386
memcpy.S-dir = lib
thunk.S-dir = lib
module.c-dir = kernel

$(obj)/stub_segv.o: _c_flags = $(call unprofile,$(CFLAGS))

include arch/um/scripts/Makefile.unmap
