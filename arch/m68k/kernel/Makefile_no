#
# Makefile for arch/m68knommu/kernel.
#

extra-y := vmlinux.lds

obj-y += dma.o entry.o init_task.o irq.o m68k_ksyms.o process.o ptrace.o \
	 setup.o signal.o syscalltable.o sys_m68k.o time.o traps.o

obj-$(CONFIG_MODULES)	+= module.o
