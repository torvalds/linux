# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

extra-y := vmlinux.lds

obj-y := process.o traps.o ptrace.o \
	 signal.o setup.o syscalls.o \
	 irq.o entry.o dma.o

obj-$(CONFIG_ROMKERNEL) += head_rom.o
obj-$(CONFIG_RAMKERNEL) += head_ram.o

obj-$(CONFIG_MODULES) += module.o h8300_ksyms.o
obj-$(CONFIG_H8300H_SIM) += sim-console.o
obj-$(CONFIG_H8S_SIM) += sim-console.o

obj-$(CONFIG_CPU_H8300H) += ptrace_h.o
obj-$(CONFIG_CPU_H8S) += ptrace_s.o

obj-$(CONFIG_KGDB) += kgdb.o
