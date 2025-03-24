# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
#

obj-y := stub.o mmu.o process.o syscall.o uaccess.o \
	 stub_exe_embed.o

# Stub executable

stub_exe_objs-y := stub_exe.o

stub_exe_objs := $(foreach F,$(stub_exe_objs-y),$(obj)/$F)

# Object file containing the ELF executable
$(obj)/stub_exe_embed.o: $(src)/stub_exe_embed.S $(obj)/stub_exe

$(obj)/stub_exe.dbg: $(stub_exe_objs) FORCE
	$(call if_changed,stub_exe)

$(obj)/stub_exe: OBJCOPYFLAGS := -S
$(obj)/stub_exe: $(obj)/stub_exe.dbg FORCE
	$(call if_changed,objcopy)

quiet_cmd_stub_exe = STUB_EXE $@
      cmd_stub_exe = $(CC) -nostdlib -o $@ \
			   $(filter-out $(UM_GPROF_OPT) $(UM_GCOV_OPT),$(KBUILD_CFLAGS)) $(STUB_EXE_LDFLAGS) \
			   $(filter %.o,$^)

STUB_EXE_LDFLAGS = -Wl,-n -static

targets += stub_exe.dbg stub_exe $(stub_exe_objs-y)

# end

# stub.o is in the stub, so it can't be built with profiling
# GCC hardened also auto-enables -fpic, but we need %ebx so it can't work ->
# disable it

CFLAGS_stub.o := $(CFLAGS_NO_HARDENING)
CFLAGS_stub_exe.o := $(CFLAGS_NO_HARDENING)

# Clang will call memset() from __builtin_alloca() when stack variable
# initialization is enabled, which is used in stub_exe.c.
CFLAGS_stub_exe.o += $(call cc-option, -ftrivial-auto-var-init=uninitialized)

UNPROFILE_OBJS := stub.o stub_exe.o
KCOV_INSTRUMENT := n

include $(srctree)/arch/um/scripts/Makefile.rules
