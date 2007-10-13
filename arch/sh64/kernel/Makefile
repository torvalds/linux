#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000, 2001  Paolo Alberelli
# Copyright (C) 2003  Paul Mundt
#
# Makefile for the Linux sh64 kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

extra-y	:= head.o init_task.o vmlinux.lds

obj-y	:= process.o signal.o entry.o traps.o irq.o irq_intc.o \
	   ptrace.o setup.o time.o sys_sh64.o semaphore.o sh_ksyms.o \
	   switchto.o syscalls.o

obj-$(CONFIG_HEARTBEAT)		+= led.o
obj-$(CONFIG_SH_ALPHANUMERIC)	+= alphanum.o
obj-$(CONFIG_SH_DMA)		+= dma.o
obj-$(CONFIG_SH_FPU)		+= fpu.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_KALLSYMS)		+= unwind.o
obj-$(CONFIG_PCI)		+= pcibios.o
obj-$(CONFIG_MODULES)		+= module.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_CPU_SH5)		+= pci_sh5.o
endif

USE_STANDARD_AS_RULE := true

