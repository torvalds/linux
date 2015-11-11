#
# Makefile for arch/parisc/kernel
#

extra-y			:= head.o vmlinux.lds

obj-y	     	:= cache.o pacache.o setup.o traps.o time.o irq.o \
		   pa7300lc.o syscall.o entry.o sys_parisc.o firmware.o \
		   ptrace.o hardware.o inventory.o drivers.o \
		   signal.o hpmc.o real2.o parisc_ksyms.o unaligned.o \
		   process.o processor.o pdc_cons.o pdc_chassis.o unwind.o \
		   topology.o

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_cache.o = -pg
CFLAGS_REMOVE_irq.o = -pg
CFLAGS_REMOVE_pacache.o = -pg
CFLAGS_REMOVE_perf.o = -pg
CFLAGS_REMOVE_traps.o = -pg
CFLAGS_REMOVE_unaligned.o = -pg
CFLAGS_REMOVE_unwind.o = -pg
endif

obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_PA11)	+= pci-dma.o
obj-$(CONFIG_PCI)	+= pci.o
obj-$(CONFIG_MODULES)	+= module.o
obj-$(CONFIG_64BIT)	+= binfmt_elf32.o sys_parisc32.o signal32.o
obj-$(CONFIG_STACKTRACE)+= stacktrace.o
obj-$(CONFIG_AUDIT)	+= audit.o
obj64-$(CONFIG_AUDIT)	+= compat_audit.o
# only supported for PCX-W/U in 64-bit mode at the moment
obj-$(CONFIG_64BIT)	+= perf.o perf_asm.o $(obj64-y)
obj-$(CONFIG_FUNCTION_TRACER)		+= ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER)	+= ftrace.o
