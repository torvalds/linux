# SPDX-License-Identifier: GPL-2.0
obj-y += vsyscall.o vsyscall-syscall.o vsyscall-syms.o

$(obj)/vsyscall-syscall.o: $(obj)/vsyscall-trapa.so

# Teach kbuild about targets
targets += vsyscall-trapa.o vsyscall-traps.so
targets += vsyscall-note.o vsyscall.lds vsyscall-dummy.o

# The DSO images are built using a special linker script
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -nostdlib $(SYSCFLAGS_$(@F)) \
			   -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vsyscall.lds += -P -C -Ush

vsyscall-flags = -shared -s -Wl,-soname=linux-gate.so.1 -Wl,--hash-style=sysv

SYSCFLAGS_vsyscall-trapa.so	= $(vsyscall-flags)

$(obj)/vsyscall-trapa.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall.lds $(obj)/vsyscall-%.o FORCE
	$(call if_changed,syscall)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
SYSCFLAGS_vsyscall-dummy.o = -r
$(obj)/vsyscall-dummy.o: $(src)/vsyscall.lds \
			$(obj)/vsyscall-trapa.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)

LDFLAGS_vsyscall-syms.o := -r -R
$(obj)/vsyscall-syms.o: $(obj)/vsyscall-dummy.o FORCE
	$(call if_changed,ld)
