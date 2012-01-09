#
# arch/blackfin/kernel/Makefile
#

extra-y := init_task.o vmlinux.lds

obj-y := \
	entry.o process.o bfin_ksyms.o ptrace.o setup.o signal.o \
	sys_bfin.o traps.o irqchip.o dma-mapping.o flat.o \
	fixed_code.o reboot.o bfin_gpio.o bfin_dma_5xx.o \
	exception.o dumpstack.o

ifeq ($(CONFIG_GENERIC_CLOCKEVENTS),y)
    obj-y += time-ts.o
else
    obj-y += time.o
endif

obj-$(CONFIG_DYNAMIC_FTRACE)         += ftrace.o
obj-$(CONFIG_FUNCTION_TRACER)        += ftrace-entry.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER)  += ftrace.o
CFLAGS_REMOVE_ftrace.o = -pg

obj-$(CONFIG_HAVE_PWM)               += pwm.o
obj-$(CONFIG_IPIPE)                  += ipipe.o
obj-$(CONFIG_BFIN_GPTIMERS)          += gptimers.o
obj-$(CONFIG_CPLB_INFO)              += cplbinfo.o
obj-$(CONFIG_MODULES)                += module.o
obj-$(CONFIG_KGDB)                   += kgdb.o
obj-$(CONFIG_KGDB_TESTS)             += kgdb_test.o
obj-$(CONFIG_NMI_WATCHDOG)           += nmi.o
obj-$(CONFIG_EARLY_PRINTK)           += early_printk.o
obj-$(CONFIG_EARLY_PRINTK)           += shadow_console.o
obj-$(CONFIG_STACKTRACE)             += stacktrace.o
obj-$(CONFIG_DEBUG_VERBOSE)          += trace.o
obj-$(CONFIG_BFIN_PSEUDODBG_INSNS)   += pseudodbg.o
obj-$(CONFIG_PERF_EVENTS)            += perf_event.o

# the kgdb test puts code into L2 and without linker
# relaxation, we need to force long calls to/from it
CFLAGS_kgdb_test.o := -mlong-calls

obj-$(CONFIG_DEBUG_MMRS)             += debug-mmrs.o
