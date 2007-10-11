#
# Makefile for the linux kernel.
#

extra-y 	:= head.o head64.o init_task.o vmlinux.lds
EXTRA_AFLAGS	:= -traditional
obj-y	:= process.o signal.o entry.o traps_64.o irq.o \
		ptrace.o time.o ioport.o ldt.o setup_64.o i8259_64.o sys_x86_64.o \
		x8664_ksyms.o i387.o syscall.o vsyscall.o \
		setup64.o bootflag.o e820_64.o reboot.o quirks.o i8237.o \
		pci-dma_64.o pci-nommu.o alternative.o hpet_64.o tsc.o bugs.o \
		perfctr-watchdog.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_X86_MCE)		+= mce.o therm_throt.o
obj-$(CONFIG_X86_MCE_INTEL)	+= mce_intel_64.o
obj-$(CONFIG_X86_MCE_AMD)	+= mce_amd_64.o
obj-$(CONFIG_MTRR)		+= ../../i386/kernel/cpu/mtrr/
obj-$(CONFIG_ACPI)		+= acpi/
obj-$(CONFIG_X86_MSR)		+= msr.o
obj-$(CONFIG_MICROCODE)		+= microcode.o
obj-$(CONFIG_X86_CPUID)		+= cpuid.o
obj-$(CONFIG_SMP)		+= smp_64.o smpboot_64.o trampoline.o tsc_sync.o
obj-y				+= apic_64.o  nmi_64.o
obj-y				+= io_apic_64.o mpparse.o genapic.o genapic_flat.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel_64.o crash_64.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_PM)		+= suspend.o
obj-$(CONFIG_HIBERNATION)	+= suspend_asm.o
obj-$(CONFIG_CPU_FREQ)		+= ../../i386/kernel/cpu/cpufreq/
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_IOMMU)		+= pci-gart.o aperture_64.o
obj-$(CONFIG_CALGARY_IOMMU)	+= pci-calgary_64.o tce.o
obj-$(CONFIG_SWIOTLB)		+= pci-swiotlb.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_X86_PM_TIMER)	+= pmtimer.o
obj-$(CONFIG_X86_VSMP)		+= vsmp.o
obj-$(CONFIG_K8_NB)		+= k8.o
obj-$(CONFIG_AUDIT)		+= audit.o

obj-$(CONFIG_MODULES)		+= module_64.o
obj-$(CONFIG_PCI)		+= early-quirks.o

obj-y				+= topology.o
obj-y				+= intel_cacheinfo.o
obj-y				+= addon_cpuid_features.o
obj-y				+= pcspeaker.o

CFLAGS_vsyscall.o		:= $(PROFILING) -g0

therm_throt-y                   += ../../i386/kernel/cpu/mcheck/therm_throt.o
bootflag-y			+= ../../i386/kernel/bootflag.o
cpuid-$(subst m,y,$(CONFIG_X86_CPUID))  += ../../i386/kernel/cpuid.o
topology-y                     += ../../i386/kernel/topology.o
microcode-$(subst m,y,$(CONFIG_MICROCODE))  += ../../i386/kernel/microcode.o
intel_cacheinfo-y		+= ../../i386/kernel/cpu/intel_cacheinfo.o
addon_cpuid_features-y		+= ../../i386/kernel/cpu/addon_cpuid_features.o
quirks-y			+= ../../i386/kernel/quirks.o
i8237-y				+= ../../i386/kernel/i8237.o
msr-$(subst m,y,$(CONFIG_X86_MSR))  += ../../i386/kernel/msr.o
alternative-y			+= ../../i386/kernel/alternative.o
pcspeaker-y			+= ../../i386/kernel/pcspeaker.o
perfctr-watchdog-y		+= ../../i386/kernel/cpu/perfctr-watchdog.o
