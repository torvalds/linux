#
# Makefile for the Linux/Meta kernel.
#

extra-y	+= head.o
extra-y	+= vmlinux.lds

obj-y	+= cachepart.o
obj-y	+= clock.o
obj-y	+= core_reg.o
obj-y	+= devtree.o
obj-y	+= dma.o
obj-y	+= irq.o
obj-y	+= kick.o
obj-y	+= machines.o
obj-y	+= process.o
obj-y	+= ptrace.o
obj-y	+= setup.o
obj-y	+= signal.o
obj-y	+= stacktrace.o
obj-y	+= sys_metag.o
obj-y	+= tbiunexp.o
obj-y	+= time.o
obj-y	+= topology.o
obj-y	+= traps.o
obj-y	+= user_gateway.o

obj-$(CONFIG_PERF_EVENTS)		+= perf/

obj-$(CONFIG_METAG_COREMEM)		+= coremem.o
obj-$(CONFIG_DYNAMIC_FTRACE)		+= ftrace.o
obj-$(CONFIG_FUNCTION_TRACER)		+= ftrace_stub.o
obj-$(CONFIG_MODULES)			+= metag_ksyms.o
obj-$(CONFIG_MODULES)			+= module.o
obj-$(CONFIG_PERF_EVENTS)		+= perf_callchain.o
obj-$(CONFIG_SMP)			+= smp.o
obj-$(CONFIG_METAG_SUSPEND_MEM)		+= suspend.o
obj-$(CONFIG_METAG_USER_TCM)		+= tcm.o
