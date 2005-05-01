obj-y = bitops.o bugs.o checksum.o delay.o fault.o ksyms.o ldt.o ptrace.o \
	ptrace_user.o semaphore.o signal.o sigcontext.o syscalls.o sysrq.o \
	sys_call_table.o

obj-$(CONFIG_HIGHMEM) += highmem.o
obj-$(CONFIG_MODULES) += module.o

USER_OBJS := bugs.o ptrace_user.o sigcontext.o fault.o

include arch/um/scripts/Makefile.rules

SYMLINKS = bitops.c semaphore.c highmem.c module.c

# this needs to be before the foreach, because clean-files does not accept
# complete paths like $(src)/$f.
clean-files := $(SYMLINKS)

targets += $(SYMLINKS)

SYMLINKS := $(foreach f,$(SYMLINKS),$(obj)/$f)

bitops.c-dir = lib
semaphore.c-dir = kernel
highmem.c-dir = mm
module.c-dir = kernel

$(SYMLINKS): FORCE
	$(call if_changed,make_link)

subdir- := util
