#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

ifeq ($(CONFIG_X86_32),y)
	BITS := 32
else
	BITS := 64
endif

obj-y = bug.o bugs_$(BITS).o delay.o fault.o ksyms.o ldt.o \
	ptrace_$(BITS).o ptrace_user.o setjmp_$(BITS).o signal.o \
	stub_$(BITS).o stub_segv.o \
	sys_call_table_$(BITS).o sysrq_$(BITS).o tls_$(BITS).o \
	mem_$(BITS).o subarch.o os-$(OS)/

ifeq ($(CONFIG_X86_32),y)

obj-y += checksum_32.o
obj-$(CONFIG_BINFMT_ELF) += elfcore.o

subarch-y = ../lib/string_32.o ../lib/atomic64_32.o ../lib/atomic64_cx8_32.o
subarch-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += ../lib/rwsem.o

else

obj-y += syscalls_64.o vdso/

subarch-y = ../lib/csum-partial_64.o ../lib/memcpy_64.o ../entry/thunk_64.o \
		../lib/rwsem.o

endif

subarch-$(CONFIG_MODULES) += ../kernel/module.o

USER_OBJS := bugs_$(BITS).o ptrace_user.o fault.o

extra-y += user-offsets.s
$(obj)/user-offsets.s: c_flags = -Wp,-MD,$(depfile) $(USER_CFLAGS) \
	-Iarch/x86/include/generated

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

include arch/um/scripts/Makefile.rules
