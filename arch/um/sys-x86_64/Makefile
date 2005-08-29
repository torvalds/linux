#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

#XXX: why into lib-y?
lib-y = bitops.o bugs.o csum-partial.o delay.o fault.o mem.o memcpy.o \
	ptrace.o ptrace_user.o semaphore.o sigcontext.o signal.o stub.o \
	stub_segv.o syscalls.o syscall_table.o sysrq.o thunk.o

obj-y := ksyms.o
obj-$(CONFIG_MODULES) += module.o um_module.o

USER_OBJS := ptrace_user.o sigcontext.o

SYMLINKS = bitops.c csum-copy.S csum-partial.c csum-wrappers.c memcpy.S \
	semaphore.c thunk.S module.c

include arch/um/scripts/Makefile.rules

bitops.c-dir = lib
csum-copy.S-dir = lib
csum-partial.c-dir = lib
csum-wrappers.c-dir = lib
memcpy.S-dir = lib
semaphore.c-dir = kernel
thunk.S-dir = lib
module.c-dir = kernel

STUB_CFLAGS = -Wp,-MD,$(depfile) $(call unprofile,$(USER_CFLAGS))

# _cflags works with kernel files, not with userspace ones, but c_flags does,
# why ask why?
$(obj)/stub_segv.o : c_flags = $(STUB_CFLAGS)

$(obj)/stub.o : a_flags = $(STUB_CFLAGS)

subdir- := util

include arch/um/scripts/Makefile.unmap
