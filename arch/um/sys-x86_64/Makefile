#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

obj-y = bug.o bugs.o delay.o fault.o ldt.o mem.o ptrace.o ptrace_user.o \
	setjmp.o signal.o stub.o stub_segv.o syscalls.o syscall_table.o \
	sysrq.o ksyms.o tls.o

obj-$(CONFIG_MODULES) += um_module.o

subarch-obj-y = lib/csum-partial_64.o lib/memcpy_64.o lib/thunk_64.o
subarch-obj-$(CONFIG_MODULES) += kernel/module_64.o

ldt-y = ../sys-i386/ldt.o

USER_OBJS := ptrace_user.o

USER_OBJS += user-offsets.s
extra-y += user-offsets.s

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

include arch/um/scripts/Makefile.rules
