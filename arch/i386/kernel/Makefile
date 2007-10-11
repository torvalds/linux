#
# Makefile for the linux kernel.
#

extra-y := head.o init_task.o vmlinux.lds

obj-y	:= process.o signal.o entry.o traps_32.o irq.o \
		ptrace.o time.o ioport.o ldt.o setup.o i8259.o sys_i386.o \
		pci-dma_32.o i386_ksyms.o i387.o bootflag.o e820.o\
		quirks.o i8237.o topology.o alternative.o i8253_32.o tsc.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_MCA)		+= mca.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_X86_SMP)		+= smp.o smpboot.o tsc_sync.o
obj-$(CONFIG_SMP)		+= smpcommon.o
obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic_32.o
obj-$(CONFIG_X86_REBOOTFIXUPS)	+= reboot_fixups.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq.o
obj-$(CONFIG_X86_SUMMIT_NUMA)	+= summit.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_MODULES)		+= module.o
obj-y				+= sysenter.o vsyscall.o
obj-$(CONFIG_ACPI_SRAT) 	+= srat_32.o
obj-$(CONFIG_EFI) 		+= efi.o efi_stub.o
obj-$(CONFIG_DOUBLEFAULT) 	+= doublefault.o
obj-$(CONFIG_VM86)		+= vm86.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_HPET_TIMER) 	+= hpet.o
obj-$(CONFIG_K8_NB)		+= k8.o
obj-$(CONFIG_MGEODE_LX)		+= geode.o

obj-$(CONFIG_VMI)		+= vmi.o vmiclock_32.o
obj-$(CONFIG_PARAVIRT)		+= paravirt.o
obj-y				+= pcspeaker.o

obj-$(CONFIG_SCx200)		+= scx200.o

# vsyscall.o contains the vsyscall DSO images as __initdata.
# We must build both images before we can assemble it.
# Note: kbuild does not track this dependency due to usage of .incbin
$(obj)/vsyscall.o: $(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so
targets += $(foreach F,int80 sysenter,vsyscall-$F.o vsyscall-$F.so)
targets += vsyscall-note.o vsyscall.lds

# The DSO images are built using a special linker script.
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -m elf_i386 -nostdlib $(SYSCFLAGS_$(@F)) \
		          -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vsyscall.lds += -P -C -U$(ARCH)

vsyscall-flags = -shared -s -Wl,-soname=linux-gate.so.1 \
		 $(call ld-option, -Wl$(comma)--hash-style=sysv)
SYSCFLAGS_vsyscall-sysenter.so	= $(vsyscall-flags)
SYSCFLAGS_vsyscall-int80.so	= $(vsyscall-flags)

$(obj)/vsyscall-int80.so $(obj)/vsyscall-sysenter.so: \
$(obj)/vsyscall-%.so: $(src)/vsyscall.lds \
		      $(obj)/vsyscall-%.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vsyscall-syms.o
$(obj)/built-in.o: $(obj)/vsyscall-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vsyscall-syms.o

SYSCFLAGS_vsyscall-syms.o = -r
$(obj)/vsyscall-syms.o: $(src)/vsyscall.lds \
			$(obj)/vsyscall-sysenter.o $(obj)/vsyscall-note.o FORCE
	$(call if_changed,syscall)

k8-y                      += ../../x86_64/kernel/k8.o
stacktrace-y		  += ../../x86_64/kernel/stacktrace.o

