# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the RISC-V Linux kernel
#

ifdef CONFIG_FTRACE
CFLAGS_REMOVE_ftrace.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_patch.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_sbi.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_return_address.o	= $(CC_FLAGS_FTRACE)
endif
CFLAGS_syscall_table.o	+= $(call cc-option,-Wno-override-init,)
CFLAGS_compat_syscall_table.o += $(call cc-option,-Wno-override-init,)

ifdef CONFIG_KEXEC_CORE
AFLAGS_kexec_relocate.o := -mcmodel=medany $(call cc-option,-mno-relax)
endif

# cmodel=medany and notrace when patching early
ifdef CONFIG_RISCV_ALTERNATIVE_EARLY
CFLAGS_alternative.o := -mcmodel=medany
CFLAGS_cpufeature.o := -mcmodel=medany
ifdef CONFIG_FTRACE
CFLAGS_REMOVE_alternative.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_cpufeature.o = $(CC_FLAGS_FTRACE)
endif
ifdef CONFIG_RELOCATABLE
CFLAGS_alternative.o += -fno-pie
CFLAGS_cpufeature.o += -fno-pie
endif
ifdef CONFIG_KASAN
KASAN_SANITIZE_alternative.o := n
KASAN_SANITIZE_cpufeature.o := n
endif
endif

extra-y += vmlinux.lds

obj-y	+= head.o
obj-y	+= soc.o
obj-$(CONFIG_RISCV_ALTERNATIVE) += alternative.o
obj-y	+= cpu.o
obj-y	+= cpufeature.o
obj-y	+= entry.o
obj-y	+= irq.o
obj-y	+= process.o
obj-y	+= ptrace.o
obj-y	+= reset.o
obj-y	+= return_address.o
obj-y	+= setup.o
obj-y	+= signal.o
obj-y	+= syscall_table.o
obj-y	+= sys_riscv.o
obj-y	+= sys_hwprobe.o
obj-y	+= time.o
obj-y	+= traps.o
obj-y	+= riscv_ksyms.o
obj-y	+= stacktrace.o
obj-y	+= cacheinfo.o
obj-y	+= patch.o
obj-y	+= probes/
obj-y	+= tests/
obj-$(CONFIG_MMU) += vdso.o vdso/

obj-$(CONFIG_RISCV_MISALIGNED)	+= traps_misaligned.o
obj-$(CONFIG_RISCV_MISALIGNED)	+= unaligned_access_speed.o
obj-$(CONFIG_RISCV_PROBE_UNALIGNED_ACCESS)	+= copy-unaligned.o

obj-$(CONFIG_FPU)		+= fpu.o
obj-$(CONFIG_FPU)		+= kernel_mode_fpu.o
obj-$(CONFIG_RISCV_ISA_V)	+= vector.o
obj-$(CONFIG_RISCV_ISA_V)	+= kernel_mode_vector.o
obj-$(CONFIG_SMP)		+= smpboot.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SMP)		+= cpu_ops.o

obj-$(CONFIG_RISCV_BOOT_SPINWAIT) += cpu_ops_spinwait.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_MODULE_SECTIONS)	+= module-sections.o

obj-$(CONFIG_CPU_PM)		+= suspend_entry.o suspend.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o hibernate-asm.o

obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o ftrace.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= mcount-dyn.o

obj-$(CONFIG_PERF_EVENTS)	+= perf_callchain.o
obj-$(CONFIG_HAVE_PERF_REGS)	+= perf_regs.o
obj-$(CONFIG_RISCV_SBI)		+= sbi.o
ifeq ($(CONFIG_RISCV_SBI), y)
obj-$(CONFIG_SMP)		+= sbi-ipi.o
obj-$(CONFIG_SMP) += cpu_ops_sbi.o
endif
obj-$(CONFIG_HOTPLUG_CPU)	+= cpu-hotplug.o
obj-$(CONFIG_PARAVIRT)		+= paravirt.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_KEXEC_CORE)	+= kexec_relocate.o crash_save_regs.o machine_kexec.o
obj-$(CONFIG_KEXEC_FILE)	+= elf_kexec.o machine_kexec_file.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_VMCORE_INFO)	+= vmcore_info.o

obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o

obj-$(CONFIG_CFI_CLANG)		+= cfi.o

obj-$(CONFIG_EFI)		+= efi.o
obj-$(CONFIG_COMPAT)		+= compat_syscall_table.o
obj-$(CONFIG_COMPAT)		+= compat_signal.o
obj-$(CONFIG_COMPAT)		+= compat_vdso/

obj-$(CONFIG_64BIT)		+= pi/
obj-$(CONFIG_ACPI)		+= acpi.o
