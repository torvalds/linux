#
# Makefile for the ia32 kernel emulation subsystem.
#

obj-$(CONFIG_IA32_EMULATION) := ia32entry.o sys_ia32.o ia32_signal.o tls32.o \
	ia32_binfmt.o fpu32.o ptrace32.o syscall32.o syscall32_syscall.o \
	mmap32.o

sysv-$(CONFIG_SYSVIPC) := ipc32.o
obj-$(CONFIG_IA32_EMULATION) += $(sysv-y)

obj-$(CONFIG_IA32_AOUT) += ia32_aout.o

audit-class-$(CONFIG_AUDIT) := audit.o
obj-$(CONFIG_IA32_EMULATION) += $(audit-class-y)

$(obj)/syscall32_syscall.o: \
	$(foreach F,sysenter syscall,$(obj)/vsyscall-$F.so)

# Teach kbuild about targets
targets := $(foreach F,$(addprefix vsyscall-,sysenter syscall),\
		     $F.o $F.so $F.so.dbg)

# The DSO images are built using a special linker script
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -m32 -nostdlib -shared \
			  $(call ld-option, -Wl$(comma)--hash-style=sysv) \
			   -Wl,-soname=linux-gate.so.1 -o $@ \
			   -Wl,-T,$(filter-out FORCE,$^)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

$(obj)/vsyscall-sysenter.so.dbg $(obj)/vsyscall-syscall.so.dbg: \
$(obj)/vsyscall-%.so.dbg: $(src)/vsyscall.lds $(obj)/vsyscall-%.o FORCE
	$(call if_changed,syscall)

AFLAGS_vsyscall-sysenter.o = -m32 -Wa,-32
AFLAGS_vsyscall-syscall.o = -m32 -Wa,-32

vdsos := vdso32-sysenter.so vdso32-syscall.so

quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(@:vdso32-%.so=$(obj)/vsyscall-%.so.dbg) \
			    $(MODLIB)/vdso/$@

$(vdsos):
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: $(vdsos)
