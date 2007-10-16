#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

obj-y = bug.o bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o setjmp.o signal.o stub.o stub_segv.o syscalls.o sysrq.o \
	sys_call_table.o tls.o

subarch-obj-y = lib/bitops_32.o lib/semaphore_32.o lib/string_32.o
subarch-obj-$(CONFIG_HIGHMEM) += mm/highmem_32.o
subarch-obj-$(CONFIG_MODULES) += kernel/module_32.o

USER_OBJS := bugs.o ptrace_user.o fault.o

USER_OBJS += user-offsets.s
extra-y += user-offsets.s

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

include arch/um/scripts/Makefile.rules
