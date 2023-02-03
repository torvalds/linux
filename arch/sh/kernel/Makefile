# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/SuperH kernel.
#

extra-y	:= vmlinux.lds

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_ftrace.o = -pg
endif

CFLAGS_REMOVE_return_address.o = -pg

obj-y	:= head_32.o debugtraps.o dumpstack.o				\
	   idle.o io.o irq.o irq_32.o kdebugfs.o			\
	   machvec.o nmi_debug.o process.o				\
	   process_32.o ptrace.o ptrace_32.o				\
	   reboot.o return_address.o					\
	   setup.o signal_32.o sys_sh.o 				\
	   syscalls_32.o time.o topology.o traps.o			\
	   traps_32.o unwinder.o

ifndef CONFIG_GENERIC_IOMAP
obj-y				+= iomap.o
obj-$(CONFIG_HAS_IOPORT_MAP)	+= ioport.o
endif

obj-y				+= sys_sh32.o
obj-y				+= cpu/
obj-$(CONFIG_VSYSCALL)		+= vsyscall/
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SH_STANDARD_BIOS)	+= sh_bios.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_MODULES)		+= sh_ksyms_32.o module.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_IO_TRAPPED)	+= io_trapped.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_FTRACE_SYSCALLS)	+= ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += ftrace.o
obj-$(CONFIG_DUMP_CODE)		+= disassemble.o
obj-$(CONFIG_HIBERNATION)	+= swsusp.o
obj-$(CONFIG_DWARF_UNWINDER)	+= dwarf.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o perf_callchain.o
obj-$(CONFIG_DMA_NONCOHERENT)	+= dma-coherent.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT)		+= hw_breakpoint.o
