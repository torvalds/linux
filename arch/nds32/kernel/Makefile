#
# Makefile for the linux kernel.
#

CPPFLAGS_vmlinux.lds	:= -DTEXTADDR=$(TEXTADDR)
AFLAGS_head.o		:= -DTEXTADDR=$(TEXTADDR)
# Object file lists.

obj-y			:= ex-entry.o ex-exit.o ex-scall.o irq.o \
			process.o ptrace.o setup.o signal.o \
			sys_nds32.o time.o traps.o cacheinfo.o \
			dma.o syscall_table.o vdso.o

obj-$(CONFIG_MODULES)		+= nds32_ksyms.o module.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_FPU)		+= fpu.o
obj-$(CONFIG_OF)		+= devtree.o
obj-$(CONFIG_CACHE_L2)		+= atl2c.o
obj-$(CONFIG_PERF_EVENTS) += perf_event_cpu.o
obj-$(CONFIG_PM)		+= pm.o sleep.o
extra-y := head.o vmlinux.lds

CFLAGS_fpu.o += -mext-fpu-sp -mext-fpu-dp


obj-y				+= vdso/

obj-$(CONFIG_FUNCTION_TRACER)   += ftrace.o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
endif
