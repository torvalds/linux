#
# Copyright 2003 PathScale, Inc.
#
# Licensed under the GPL
#

lib-y = bitops.o bugs.o csum-partial.o delay.o fault.o mem.o memcpy.o \
	ptrace.o ptrace_user.o semaphore.o sigcontext.o signal.o \
	syscalls.o sysrq.o thunk.o

USER_OBJS := ptrace_user.o sigcontext.o

include arch/um/scripts/Makefile.rules

SYMLINKS = bitops.c csum-copy.S csum-partial.c csum-wrappers.c memcpy.S \
	semaphore.c thunk.S

# this needs to be before the foreach, because clean-files does not accept
# complete paths like $(src)/$f.
clean-files := $(SYMLINKS)

targets += $(SYMLINKS)

SYMLINKS := $(foreach f,$(SYMLINKS),$(obj)/$f)

bitops.c-dir = lib
csum-copy.S-dir = lib
csum-partial.c-dir = lib
csum-wrappers.c-dir = lib
memcpy.S-dir = lib
semaphore.c-dir = kernel
thunk.S-dir = lib

$(SYMLINKS): FORCE
	$(call if_changed,make_link)

CFLAGS_csum-partial.o := -Dcsum_partial=arch_csum_partial
