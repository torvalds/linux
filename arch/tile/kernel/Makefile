#
# Makefile for the Linux/TILE kernel.
#

extra-y := vmlinux.lds head_$(BITS).o
obj-y := backtrace.o entry.o init_task.o irq.o messaging.o \
	pci-dma.o proc.o process.o ptrace.o reboot.o \
	setup.o signal.o single_step.o stack.o sys.o sysfs.o time.o traps.o \
	intvec_$(BITS).o regs_$(BITS).o tile-desc_$(BITS).o

obj-$(CONFIG_HARDWALL)		+= hardwall.o
obj-$(CONFIG_COMPAT)		+= compat.o compat_signal.o
obj-$(CONFIG_SMP)		+= smpboot.o smp.o tlb.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel_$(BITS).o
obj-$(CONFIG_PCI)		+= pci.o
