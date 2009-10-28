#
# Makefile for the Linux/SuperH kernel.
#

extra-y	:= head_$(BITS).o init_task.o vmlinux.lds

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_ftrace.o = -pg
endif

CFLAGS_REMOVE_return_address.o = -pg

obj-y	:= debugtraps.o dma-nommu.o dumpstack.o 			\
	   idle.o io.o io_generic.o irq.o				\
	   irq_$(BITS).o machvec.o nmi_debug.o process_$(BITS).o 	\
	   ptrace_$(BITS).o return_address.o				\
	   setup.o signal_$(BITS).o sys_sh.o sys_sh$(BITS).o		\
	   syscalls_$(BITS).o time.o topology.o traps.o			\
	   traps_$(BITS).o unwinder.o

obj-y				+= cpu/
obj-$(CONFIG_VSYSCALL)		+= vsyscall/
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SH_STANDARD_BIOS)	+= sh_bios.o
obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_SH_CPU_FREQ)	+= cpufreq.o
obj-$(CONFIG_MODULES)		+= sh_ksyms_$(BITS).o module.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_IO_TRAPPED)	+= io_trapped.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_GENERIC_GPIO)	+= gpio.o
obj-$(CONFIG_DYNAMIC_FTRACE)	+= ftrace.o
obj-$(CONFIG_FTRACE_SYSCALLS)	+= ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += ftrace.o
obj-$(CONFIG_DUMP_CODE)		+= disassemble.o
obj-$(CONFIG_HIBERNATION)	+= swsusp.o
obj-$(CONFIG_DWARF_UNWINDER)	+= dwarf.o
obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o

obj-$(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)	+= localtimer.o

EXTRA_CFLAGS += -Werror
