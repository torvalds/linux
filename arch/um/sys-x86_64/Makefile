#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

obj-y = bug.o bugs.o delay.o fault.o ldt.o mem.o ptrace.o ptrace_user.o \
	setjmp.o sigcontext.o signal.o syscalls.o syscall_table.o sysrq.o \
	ksyms.o tls.o

obj-$(CONFIG_MODE_SKAS) += stub.o stub_segv.o
obj-$(CONFIG_MODULES) += um_module.o

subarch-obj-y = lib/bitops.o lib/csum-partial.o lib/memcpy.o lib/thunk.o
subarch-obj-$(CONFIG_MODULES) += kernel/module.o

ldt-y = ../sys-i386/ldt.o

USER_OBJS := ptrace_user.o sigcontext.o

USER_OBJS += user-offsets.s
extra-y += user-offsets.s

extra-$(CONFIG_MODE_TT) += unmap.o

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

include arch/um/scripts/Makefile.rules

$(obj)/unmap.%: _c_flags = $(call unprofile,$(CFLAGS))
