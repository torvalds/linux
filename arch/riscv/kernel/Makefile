#
# Makefile for the RISC-V Linux kernel
#

ifdef CONFIG_FTRACE
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_setup.o = -pg
endif

extra-y += head.o
extra-y += vmlinux.lds

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
obj-y	+= vdso.o
obj-y	+= cacheinfo.o
obj-y	+= vdso/

CFLAGS_setup.o := -mcmodel=medany

obj-$(CONFIG_SMP)		+= smpboot.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_MODULE_SECTIONS)	+= module-sections.o
obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER)	+= ftrace.o

clean:
