obj-y = bitops.o bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o semaphore.o signal.o sigcontext.o stub.o stub_segv.o \
	syscalls.o sysrq.o sys_call_table.o

obj-$(CONFIG_HIGHMEM) += highmem.o
obj-$(CONFIG_MODULES) += module.o

USER_OBJS := bugs.o ptrace_user.o sigcontext.o fault.o

SYMLINKS = bitops.c semaphore.c highmem.c module.c

include arch/um/scripts/Makefile.rules

bitops.c-dir = lib
semaphore.c-dir = kernel
highmem.c-dir = mm
module.c-dir = kernel

STUB_CFLAGS = -Wp,-MD,$(depfile) $(call unprofile,$(USER_CFLAGS))

# _cflags works with kernel files, not with userspace ones, but c_flags does,
# why ask why?
$(obj)/stub_segv.o : c_flags = $(STUB_CFLAGS)

$(obj)/stub.o : a_flags = $(STUB_CFLAGS)

subdir- := util

include arch/um/scripts/Makefile.unmap
