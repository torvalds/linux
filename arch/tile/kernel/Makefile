#
# Makefile for the Linux/TILE kernel.
#

extra-y := vmlinux.lds head_$(BITS).o
obj-y := backtrace.o entry.o hvglue.o irq.o messaging.o \
	pci-dma.o proc.o process.o ptrace.o reboot.o \
	setup.o signal.o single_step.o stack.o sys.o \
	sysfs.o time.o traps.o unaligned.o vdso.o \
	intvec_$(BITS).o regs_$(BITS).o tile-desc_$(BITS).o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_early_printk.o = -pg
endif

obj-$(CONFIG_HARDWALL)		+= hardwall.o
obj-$(CONFIG_COMPAT)		+= compat.o compat_signal.o
obj-$(CONFIG_SMP)		+= smpboot.o smp.o tlb.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel_$(BITS).o
ifdef CONFIG_TILEGX
obj-$(CONFIG_PCI)		+= pci_gx.o
else
obj-$(CONFIG_PCI)		+= pci.o
endif
obj-$(CONFIG_TILE_USB)		+= usb.o
obj-$(CONFIG_TILE_HVGLUE_TRACE)	+= hvglue_trace.o
obj-$(CONFIG_FUNCTION_TRACER)	+= ftrace.o mcount_64.o

obj-y				+= vdso/
