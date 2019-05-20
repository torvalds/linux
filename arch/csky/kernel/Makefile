extra-y := head.o vmlinux.lds

obj-y += entry.o atomic.o signal.o traps.o irq.o time.o vdso.o
obj-y += power.o syscall.o syscall_table.o setup.o
obj-y += process.o cpu-probe.o ptrace.o dumpstack.o

obj-$(CONFIG_MODULES)			+= module.o
obj-$(CONFIG_SMP)			+= smp.o
obj-$(CONFIG_FUNCTION_TRACER)		+= ftrace.o
obj-$(CONFIG_STACKTRACE)		+= stacktrace.o
obj-$(CONFIG_CSKY_PMU_V1)		+= perf_event.o
obj-$(CONFIG_PERF_EVENTS)		+= perf_callchain.o
obj-$(CONFIG_HAVE_PERF_REGS)            += perf_regs.o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
endif
