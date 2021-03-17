# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the RISC-V Linux kernel
#

ifdef CONFIG_FTRACE
CFLAGS_REMOVE_ftrace.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_patch.o	= $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_sbi.o	= $(CC_FLAGS_FTRACE)
endif
CFLAGS_syscall_table.o	+= $(call cc-option,-Wno-override-init,)

ifdef CONFIG_KEXEC
AFLAGS_kexec_relocate.o := -mcmodel=medany $(call cc-option,-mno-relax)
endif

extra-y += head.o
extra-y += vmlinux.lds

obj-y	+= soc.o
obj-y	+= cpu.o
obj-y	+= cpufeature.o
obj-y	+= entry.o
obj-y	+= irq.o
obj-y	+= process.o
obj-y	+= ptrace.o
obj-y	+= reset.o
obj-y	+= setup.o
obj-y	+= signal.o
obj-y	+= syscall_table.o
obj-y	+= sys_riscv.o
obj-y	+= time.o
obj-y	+= traps.o
obj-y	+= riscv_ksyms.o
obj-y	+= stacktrace.o
obj-y	+= cacheinfo.o
obj-y	+= patch.o
obj-y	+= probes/
obj-$(CONFIG_MMU) += vdso.o vdso/

obj-$(CONFIG_RISCV_M_MODE)	+= traps_misaligned.o
obj-$(CONFIG_FPU)		+= fpu.o
obj-$(CONFIG_SMP)		+= smpboot.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SMP)		+= cpu_ops.o
obj-$(CONFIG_SMP)		+= cpu_ops_spinwait.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_MODULE_SECTIONS)	+= module-sections.o

obj-$(CONFIG_CPU_PM)		+= suspend_entry.o suspend.o

obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o ftrace.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= mcount-dyn.o

obj-$(CONFIG_PERF_EVENTS)	+= perf_callchain.o
obj-$(CONFIG_HAVE_PERF_REGS)	+= perf_regs.o
obj-$(CONFIG_RISCV_SBI)		+= sbi.o
ifeq ($(CONFIG_RISCV_SBI), y)
obj-$(CONFIG_SMP) += cpu_ops_sbi.o
endif
obj-$(CONFIG_HOTPLUG_CPU)	+= cpu-hotplug.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_KEXEC)		+= kexec_relocate.o crash_save_regs.o machine_kexec.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o

obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o

obj-$(CONFIG_EFI)		+= efi.o
