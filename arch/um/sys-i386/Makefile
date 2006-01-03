obj-y := bitops.o bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o semaphore.o signal.o sigcontext.o syscalls.o sysrq.o \
	sys_call_table.o

obj-$(CONFIG_MODE_SKAS) += stub.o stub_segv.o

obj-$(CONFIG_HIGHMEM) += highmem.o
obj-$(CONFIG_MODULES) += module.o

USER_OBJS := bugs.o ptrace_user.o sigcontext.o fault.o stub_segv.o

SYMLINKS = bitops.c semaphore.c highmem.c module.c

include arch/um/scripts/Makefile.rules

bitops.c-dir = lib
semaphore.c-dir = kernel
highmem.c-dir = mm
module.c-dir = kernel

$(obj)/stub_segv.o : _c_flags = $(call unprofile,$(CFLAGS))

include arch/um/scripts/Makefile.unmap
