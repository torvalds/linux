obj-y += vsyscall.o vsyscall-syscall.o

$(obj)/vsyscall-syscall.o: \
	$(foreach F,trapa,$(obj)/vsyscall-$F.so)

# Teach kbuild about targets
targets += $(foreach F,trapa,vsyscall-$F.o vsyscall-$F.so)
targets += vsyscall-note.o vsyscall.lds

# The DSO images are built using a special linker script
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -nostdlib $(SYSCFLAGS_$(@F)) \
			   -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vsyscall.lds += -P -C -Ush

vsyscall-flags = -shared -s -Wl,-soname=linux-gate.so.1 \
		$(call ld-option, -Wl$(comma)--hash-style=sysv)

SYSCFLAGS_vsyscall-trapa.so	= $(vsyscall-flags)

$(obj)/vsyscall-trapa.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall.lds $(obj)/vsyscall-%.o FORCE
	$(call if_changed,syscall)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vsyscall-syms.o
$(obj)/built-in.o: $(obj)/vsyscall-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vsyscall-syms.o

SYSCFLAGS_vsyscall-syms.o = -r
$(obj)/vsyscall-syms.o: $(src)/vsyscall.lds \
			$(obj)/vsyscall-trapa.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)
