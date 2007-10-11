#
# Makefile for the linux kernel.
#

extra-y := head.o init_task.o vmlinux.lds

obj-y	:= process.o signal_32.o entry.o traps_32.o irq.o \
		ptrace.o time_32.o ioport_32.o ldt.o setup_32.o i8259_32.o sys_i386_32.o \
		pci-dma_32.o i386_ksyms_32.o i387_32.o bootflag.o e820_32.o\
		quirks.o i8237.o topology.o alternative.o i8253_32.o tsc.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_MCA)		+= mca_32.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm_32.o
obj-$(CONFIG_X86_SMP)		+= smp_32.o smpboot_32.o tsc_sync.o
obj-$(CONFIG_SMP)		+= smpcommon_32.o
obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse_32.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic_32.o nmi_32.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic_32.o
obj-$(CONFIG_X86_REBOOTFIXUPS)	+= reboot_fixups_32.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel_32.o crash_32.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq_32.o
obj-$(CONFIG_X86_SUMMIT_NUMA)	+= summit_32.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_MODULES)		+= module_32.o
obj-y				+= sysenter_32.o vsyscall_32.o
obj-$(CONFIG_ACPI_SRAT) 	+= srat_32.o
obj-$(CONFIG_EFI) 		+= efi_32.o efi_stub_32.o
obj-$(CONFIG_DOUBLEFAULT) 	+= doublefault_32.o
obj-$(CONFIG_VM86)		+= vm86_32.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_HPET_TIMER) 	+= hpet_32.o
obj-$(CONFIG_K8_NB)		+= k8.o
obj-$(CONFIG_MGEODE_LX)		+= geode_32.o

obj-$(CONFIG_VMI)		+= vmi_32.o vmiclock_32.o
obj-$(CONFIG_PARAVIRT)		+= paravirt_32.o
obj-y				+= pcspeaker.o

obj-$(CONFIG_SCx200)		+= scx200_32.o

# vsyscall_32.o contains the vsyscall DSO images as __initdata.
# We must build both images before we can assemble it.
# Note: kbuild does not track this dependency due to usage of .incbin
$(obj)/vsyscall_32.o: $(obj)/vsyscall-int80_32.so $(obj)/vsyscall-sysenter_32.so
targets += $(foreach F,int80 sysenter,vsyscall-$F.o vsyscall-$F.so)
targets += vsyscall-note.o vsyscall_32.lds

# The DSO images are built using a special linker script.
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -m elf_i386 -nostdlib $(SYSCFLAGS_$(@F)) \
		          -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vsyscall_32.lds += -P -C -U$(ARCH)

vsyscall-flags = -shared -s -Wl,-soname=linux-gate.so.1 \
		 $(call ld-option, -Wl$(comma)--hash-style=sysv)
SYSCFLAGS_vsyscall-sysenter_32.so	= $(vsyscall-flags)
SYSCFLAGS_vsyscall-int80_32.so	= $(vsyscall-flags)

$(obj)/vsyscall-int80_32.so $(obj)/vsyscall-sysenter_32.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall_32.lds \
		      $(obj)/vsyscall-%.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vsyscall-syms.o
$(obj)/built-in.o: $(obj)/vsyscall-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vsyscall-syms.o

SYSCFLAGS_vsyscall-syms.o = -r
$(obj)/vsyscall-syms.o: $(src)/vsyscall_32.lds \
			$(obj)/vsyscall-sysenter_32.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)

k8-y                      += ../../x86_64/kernel/k8.o
stacktrace-y		  += ../../x86_64/kernel/stacktrace.o

