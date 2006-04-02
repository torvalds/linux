obj-y = bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o signal.o sigcontext.o syscalls.o sysrq.o \
	sys_call_table.o tls.o

obj-$(CONFIG_MODE_SKAS) += stub.o stub_segv.o

subarch-obj-y = lib/bitops.o kernel/semaphore.o
subarch-obj-$(CONFIG_HIGHMEM) += mm/highmem.o
subarch-obj-$(CONFIG_MODULES) += kernel/module.o

USER_OBJS := bugs.o ptrace_user.o sigcontext.o fault.o stub_segv.o

include arch/um/scripts/Makefile.rules

extra-$(CONFIG_MODE_TT) += unmap.o

$(obj)/stub_segv.o $(obj)/unmap.o: \
	_c_flags = $(call unprofile,$(CFLAGS))
