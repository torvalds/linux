#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

obj-y = bugs.o delay.o fault.o ldt.o mem.o ptrace.o ptrace_user.o \
	sigcontext.o signal.o syscalls.o syscall_table.o sysrq.o ksyms.o \
	tls.o

obj-$(CONFIG_MODE_SKAS) += stub.o stub_segv.o
obj-$(CONFIG_MODULES) += um_module.o

subarch-obj-y = lib/bitops.o lib/csum-partial.o lib/memcpy.o lib/thunk.o
subarch-obj-$(CONFIG_MODULES) += kernel/module.o

ldt-y = ../sys-i386/ldt.o

USER_OBJS := ptrace_user.o sigcontext.o stub_segv.o

USER_OBJS += user-offsets.s
extra-y += user-offsets.s

CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

extra-$(CONFIG_MODE_TT) += unmap.o

include arch/um/scripts/Makefile.rules

$(obj)/stub_segv.o $(obj)/unmap.o: \
	_c_flags = $(call unprofile,$(CFLAGS))
