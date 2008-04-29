#
# Makefile for the linux kernel.
#

extra-y                := head_$(BITS).o head$(BITS).o init_task.o vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

#
# vsyscalls (which work on the user stack) should have
# no stack-protector checks:
#
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_vsyscall_64.o	:= $(PROFILING) -g0 $(nostackp)
CFLAGS_hpet.o		:= $(nostackp)
CFLAGS_tsc_64.o		:= $(nostackp)

obj-y			:= process_$(BITS).o signal_$(BITS).o entry_$(BITS).o
obj-y			+= traps_$(BITS).o irq_$(BITS).o
obj-y			+= time_$(BITS).o ioport.o ldt.o
obj-y			+= setup_$(BITS).o i8259_$(BITS).o setup.o
obj-$(CONFIG_X86_32)	+= sys_i386_32.o i386_ksyms_32.o
obj-$(CONFIG_X86_64)	+= sys_x86_64.o x8664_ksyms_64.o
obj-$(CONFIG_X86_64)	+= syscall_64.o vsyscall_64.o setup64.o
obj-y			+= bootflag.o e820_$(BITS).o
obj-y			+= pci-dma.o quirks.o i8237.o topology.o kdebugfs.o
obj-y			+= alternative.o i8253.o pci-nommu.o
obj-$(CONFIG_X86_64)	+= bugs_64.o
obj-y			+= tsc_$(BITS).o io_delay.o rtc.o

obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline.o
obj-y				+= process.o
obj-y				+= i387.o
obj-y				+= ptrace.o
obj-y				+= ds.o
obj-$(CONFIG_X86_32)		+= tls.o
obj-$(CONFIG_IA32_EMULATION)	+= tls.o
obj-y				+= step.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-$(CONFIG_X86_BIOS_REBOOT)	+= reboot.o
obj-$(CONFIG_X86_64)		+= reboot.o
obj-$(CONFIG_MCA)		+= mca_32.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_PCI)		+= early-quirks.o
apm-y				:= apm_32.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_X86_SMP)		+= smp.o
obj-$(CONFIG_X86_SMP)		+= smpboot.o tsc_sync.o ipi.o tlb_$(BITS).o
obj-$(CONFIG_X86_32_SMP)	+= smpcommon.o
obj-$(CONFIG_X86_64_SMP)	+= tsc_sync.o smpcommon.o
obj-$(CONFIG_X86_TRAMPOLINE)	+= trampoline_$(BITS).o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic_$(BITS).o nmi_$(BITS).o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic_$(BITS).o
obj-$(CONFIG_X86_REBOOTFIXUPS)	+= reboot_fixups_32.o
obj-$(CONFIG_KEXEC)		+= machine_kexec_$(BITS).o
obj-$(CONFIG_KEXEC)		+= relocate_kernel_$(BITS).o crash.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump_$(BITS).o
obj-$(CONFIG_X86_NUMAQ)		+= numaq_32.o
obj-$(CONFIG_X86_SUMMIT_NUMA)	+= summit_32.o
obj-y				+= vsmp_64.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_MODULES)		+= module_$(BITS).o
obj-$(CONFIG_ACPI_SRAT) 	+= srat_32.o
obj-$(CONFIG_EFI) 		+= efi.o efi_$(BITS).o efi_stub_$(BITS).o
obj-$(CONFIG_DOUBLEFAULT) 	+= doublefault_32.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_VM86)		+= vm86_32.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

obj-$(CONFIG_HPET_TIMER) 	+= hpet.o

obj-$(CONFIG_K8_NB)		+= k8.o
obj-$(CONFIG_MGEODE_LX)		+= geode_32.o mfgpt_32.o
obj-$(CONFIG_DEBUG_RODATA_TEST)	+= test_rodata.o
obj-$(CONFIG_DEBUG_NX_TEST)	+= test_nx.o

obj-$(CONFIG_VMI)		+= vmi_32.o vmiclock_32.o
obj-$(CONFIG_KVM_GUEST)		+= kvm.o
obj-$(CONFIG_KVM_CLOCK)		+= kvmclock.o
obj-$(CONFIG_PARAVIRT)		+= paravirt.o paravirt_patch_$(BITS).o

ifdef CONFIG_INPUT_PCSPKR
obj-y				+= pcspeaker.o
endif

obj-$(CONFIG_SCx200)		+= scx200.o
scx200-y			+= scx200_32.o

obj-$(CONFIG_OLPC)		+= olpc.o

###
# 64 bit specific files
ifeq ($(CONFIG_X86_64),y)
        obj-y				+= genapic_64.o genapic_flat_64.o genx2apic_uv_x.o
        obj-$(CONFIG_X86_PM_TIMER)	+= pmtimer_64.o
        obj-$(CONFIG_AUDIT)		+= audit_64.o

        obj-$(CONFIG_GART_IOMMU)	+= pci-gart_64.o aperture_64.o
        obj-$(CONFIG_CALGARY_IOMMU)	+= pci-calgary_64.o tce_64.o
        obj-$(CONFIG_SWIOTLB)		+= pci-swiotlb_64.o
endif
