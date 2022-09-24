# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/LoongArch kernel.
#

extra-y		:= vmlinux.lds

obj-y		+= head.o cpu-probe.o cacheinfo.o env.o setup.o entry.o genex.o \
		   traps.o irq.o idle.o process.o dma.o mem.o io.o reset.o switch.o \
		   elf.o syscall.o signal.o time.o topology.o inst.o ptrace.o vdso.o

obj-$(CONFIG_ACPI)		+= acpi.o
obj-$(CONFIG_EFI) 		+= efi.o

obj-$(CONFIG_CPU_HAS_FPU)	+= fpu.o

obj-$(CONFIG_MODULES)		+= module.o module-sections.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o

obj-$(CONFIG_PROC_FS)		+= proc.o

obj-$(CONFIG_SMP)		+= smp.o

obj-$(CONFIG_NUMA)		+= numa.o

obj-$(CONFIG_UNWINDER_GUESS)	+= unwind_guess.o
obj-$(CONFIG_UNWINDER_PROLOGUE) += unwind_prologue.o

CPPFLAGS_vmlinux.lds		:= $(KBUILD_CFLAGS)
