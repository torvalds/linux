obj-y = bug.o bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o setjmp.o signal.o sigcontext.o syscalls.o sysrq.o \
	sys_call_table.o tls.o

obj-$(CONFIG_MODE_SKAS) += stub.o stub_segv.o

subarch-obj-y = lib/bitops.o lib/semaphore.o lib/string.o
subarch-obj-$(CONFIG_HIGHMEM) += mm/highmem.o
subarch-obj-$(CONFIG_MODULES) += kernel/module.o

USER_OBJS := bugs.o ptrace_user.o sigcontext.o fault.o

USER_OBJS += user-offsets.s
extra-y += user-offsets.s

extra-$(CONFIG_MODE_TT) += unmap.o

UNPROFILE_OBJS := stub_segv.o
CFLAGS_stub_segv.o := $(CFLAGS_NO_HARDENING)

include arch/um/scripts/Makefile.rules

$(obj)/unmap.%: _c_flags = $(call unprofile,$(CFLAGS))
