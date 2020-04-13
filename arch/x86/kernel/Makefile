# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

extra-y	:= head_$(BITS).o
extra-y	+= head$(BITS).o
extra-y	+= ebda.o
extra-y	+= platform-quirks.o
extra-y	+= vmlinux.lds

CPPFLAGS_vmlinux.lds += -U$(UTS_MACHINE)

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_tsc.o = -pg
CFLAGS_REMOVE_paravirt-spinlocks.o = -pg
CFLAGS_REMOVE_pvclock.o = -pg
CFLAGS_REMOVE_kvmclock.o = -pg
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_early_printk.o = -pg
CFLAGS_REMOVE_head64.o = -pg
endif

KASAN_SANITIZE_head$(BITS).o				:= n
KASAN_SANITIZE_dumpstack.o				:= n
KASAN_SANITIZE_dumpstack_$(BITS).o			:= n
KASAN_SANITIZE_stacktrace.o				:= n
KASAN_SANITIZE_paravirt.o				:= n

# With some compiler versions the generated code results in boot hangs, caused
# by several compilation units. To be safe, disable all instrumentation.
KCSAN_SANITIZE := n

OBJECT_FILES_NON_STANDARD_test_nx.o			:= y
OBJECT_FILES_NON_STANDARD_paravirt_patch.o		:= y

ifdef CONFIG_FRAME_POINTER
OBJECT_FILES_NON_STANDARD_ftrace_$(BITS).o		:= y
endif

# If instrumentation of this dir is enabled, boot hangs during first second.
# Probably could be more selective here, but note that files related to irqs,
# boot, dumpstack/stacktrace, etc are either non-interesting or can lead to
# non-deterministic coverage.
KCOV_INSTRUMENT		:= n

CFLAGS_irq.o := -I $(srctree)/$(src)/../include/asm/trace

obj-y			:= process_$(BITS).o signal.o
obj-$(CONFIG_COMPAT)	+= signal_compat.o
obj-y			+= traps.o idt.o irq.o irq_$(BITS).o dumpstack_$(BITS).o
obj-y			+= time.o ioport.o dumpstack.o nmi.o
obj-$(CONFIG_MODIFY_LDT_SYSCALL)	+= ldt.o
obj-y			+= setup.o x86_init.o i8259.o irqinit.o
obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o
obj-$(CONFIG_IRQ_WORK)  += irq_work.o
obj-y			+= probe_roms.o
obj-$(CONFIG_X86_32)	+= sys_ia32.o
obj-$(CONFIG_IA32_EMULATION)	+= sys_ia32.o
obj-$(CONFIG_X86_64)	+= sys_x86_64.o
obj-$(CONFIG_X86_ESPFIX64)	+= espfix_64.o
obj-$(CONFIG_SYSFS)	+= ksysfs.o
obj-y			+= bootflag.o e820.o
obj-y			+= pci-dma.o quirks.o topology.o kdebugfs.o
obj-y			+= alternative.o i8253.o hw_breakpoint.o
obj-y			+= tsc.o tsc_msr.o io_delay.o rtc.o
obj-y			+= pci-iommu_table.o
obj-y			+= resource.o
obj-y			+= irqflags.o

obj-y				+= process.o
obj-y				+= fpu/
obj-y				+= ptrace.o
obj-$(CONFIG_X86_32)		+= tls.o
obj-$(CONFIG_IA32_EMULATION)	+= tls.o
obj-y				+= step.o
obj-$(CONFIG_INTEL_TXT)		+= tboot.o
obj-$(CONFIG_ISA_DMA_API)	+= i8237.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-y				+= acpi/
obj-y				+= reboot.o
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_PCI)		+= early-quirks.o
apm-y				:= apm_32.o
obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SMP)		+= smpboot.o
obj-$(CONFIG_X86_TSC)		+= tsc_sync.o
obj-$(CONFIG_SMP)		+= setup_percpu.o
obj-$(CONFIG_X86_MPPARSE)	+= mpparse.o
obj-y				+= apic/
obj-$(CONFIG_X86_REBOOTFIXUPS)	+= reboot_fixups_32.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_LIVEPATCH)	+= livepatch.o
obj-$(CONFIG_FUNCTION_TRACER)	+= ftrace_$(BITS).o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += ftrace.o
obj-$(CONFIG_FTRACE_SYSCALLS)	+= ftrace.o
obj-$(CONFIG_X86_TSC)		+= trace_clock.o
obj-$(CONFIG_CRASH_CORE)	+= crash_core_$(BITS).o
obj-$(CONFIG_KEXEC_CORE)	+= machine_kexec_$(BITS).o
obj-$(CONFIG_KEXEC_CORE)	+= relocate_kernel_$(BITS).o crash.o
obj-$(CONFIG_KEXEC_FILE)	+= kexec-bzimage64.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump_$(BITS).o
obj-y				+= kprobes/
obj-$(CONFIG_MODULES)		+= module.o
ifeq ($(CONFIG_X86_32),y)
obj-$(CONFIG_DOUBLEFAULT)	+= doublefault_32.o
endif
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_VM86)		+= vm86_32.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

obj-$(CONFIG_HPET_TIMER) 	+= hpet.o
obj-$(CONFIG_APB_TIMER)		+= apb_timer.o

obj-$(CONFIG_AMD_NB)		+= amd_nb.o
obj-$(CONFIG_DEBUG_NMI_SELFTEST) += nmi_selftest.o

obj-$(CONFIG_KVM_GUEST)		+= kvm.o kvmclock.o
obj-$(CONFIG_PARAVIRT)		+= paravirt.o paravirt_patch.o
obj-$(CONFIG_PARAVIRT_SPINLOCKS)+= paravirt-spinlocks.o
obj-$(CONFIG_PARAVIRT_CLOCK)	+= pvclock.o
obj-$(CONFIG_X86_PMEM_LEGACY_DEVICE) += pmem.o

obj-$(CONFIG_JAILHOUSE_GUEST)	+= jailhouse.o

obj-$(CONFIG_EISA)		+= eisa.o
obj-$(CONFIG_PCSPKR_PLATFORM)	+= pcspeaker.o

obj-$(CONFIG_X86_CHECK_BIOS_CORRUPTION) += check.o

obj-$(CONFIG_SWIOTLB)			+= pci-swiotlb.o
obj-$(CONFIG_OF)			+= devicetree.o
obj-$(CONFIG_UPROBES)			+= uprobes.o
obj-y					+= sysfb.o
obj-$(CONFIG_X86_SYSFB)			+= sysfb_simplefb.o
obj-$(CONFIG_EFI)			+= sysfb_efi.o

obj-$(CONFIG_PERF_EVENTS)		+= perf_regs.o
obj-$(CONFIG_TRACING)			+= tracepoint.o
obj-$(CONFIG_SCHED_MC_PRIO)		+= itmt.o
obj-$(CONFIG_X86_UMIP)			+= umip.o

obj-$(CONFIG_UNWINDER_ORC)		+= unwind_orc.o
obj-$(CONFIG_UNWINDER_FRAME_POINTER)	+= unwind_frame.o
obj-$(CONFIG_UNWINDER_GUESS)		+= unwind_guess.o

###
# 64 bit specific files
ifeq ($(CONFIG_X86_64),y)
	obj-$(CONFIG_AUDIT)		+= audit_64.o

	obj-$(CONFIG_GART_IOMMU)	+= amd_gart_64.o aperture_64.o

	obj-$(CONFIG_MMCONF_FAM10H)	+= mmconf-fam10h_64.o
	obj-y				+= vsmp_64.o
endif

obj-$(CONFIG_IMA_SECURE_AND_OR_TRUSTED_BOOT)	+= ima_arch.o
