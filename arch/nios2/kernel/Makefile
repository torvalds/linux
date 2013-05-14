#
# Makefile for the nios2 linux kernel.
#

extra-y := head.o vmlinux.lds

obj-y += entry$(MMU).o

obj-y += cpuinfo.o insnemu.o irq.o nios2_ksyms.o process.o prom.o ptrace.o \
	 setup.o signal.o sys_nios2.o syscalltable.o time.o traps.o

obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_CONSOLE)		+= console.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_ALIGNMENT_TRAP)	+= misaligned.o
obj-$(CONFIG_KGDB)		+= kgdb.o
