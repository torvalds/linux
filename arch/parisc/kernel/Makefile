#
# Makefile for arch/parisc/kernel
#

extra-y			:= init_task.o head.o vmlinux.lds

obj-y	     	:= cache.o pacache.o setup.o traps.o time.o irq.o \
		   pa7300lc.o syscall.o entry.o sys_parisc.o firmware.o \
		   ptrace.o hardware.o inventory.o drivers.o \
		   signal.o hpmc.o real2.o parisc_ksyms.o unaligned.o \
		   process.o processor.o pdc_cons.o pdc_chassis.o unwind.o \
		   topology.o

obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_PA11)	+= pci-dma.o
obj-$(CONFIG_PCI)	+= pci.o
obj-$(CONFIG_MODULES)	+= module.o
obj-$(CONFIG_64BIT)	+= binfmt_elf32.o sys_parisc32.o signal32.o
# only supported for PCX-W/U in 64-bit mode at the moment
obj-$(CONFIG_64BIT)	+= perf.o perf_asm.o
