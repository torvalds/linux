#
# Makefile
#

ifdef CONFIG_FUNCTION_TRACER
# Do not trace early boot code and low level code
CFLAGS_REMOVE_timer.o = -pg
CFLAGS_REMOVE_intc.o = -pg
CFLAGS_REMOVE_early_printk.o = -pg
CFLAGS_REMOVE_selfmod.o = -pg
CFLAGS_REMOVE_heartbeat.o = -pg
CFLAGS_REMOVE_ftrace.o = -pg
endif

extra-y := head.o vmlinux.lds

obj-y += exceptions.o \
	hw_exception_handler.o init_task.o intc.o irq.o of_device.o \
	of_platform.o process.o prom.o prom_parse.o ptrace.o \
	setup.o signal.o sys_microblaze.o timer.o traps.o reset.o

obj-y += cpu/

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_SELFMOD)		+= selfmod.o
obj-$(CONFIG_HEART_BEAT)	+= heartbeat.o
obj-$(CONFIG_MODULES)		+= microblaze_ksyms.o module.o
obj-$(CONFIG_MMU)		+= misc.o
obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_FUNCTION_TRACER)	+= ftrace.o mcount.o

obj-y	+= entry$(MMU).o
