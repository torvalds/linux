# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

extra-y			+= vmlinux.lds

obj-$(CONFIG_AMIGA)	:= head.o
obj-$(CONFIG_ATARI)	:= head.o
obj-$(CONFIG_MAC)	:= head.o
obj-$(CONFIG_APOLLO)	:= head.o
obj-$(CONFIG_VME)	:= head.o
obj-$(CONFIG_HP300)	:= head.o
obj-$(CONFIG_Q40)	:= head.o
obj-$(CONFIG_SUN3X)	:= head.o
obj-$(CONFIG_VIRT)	:= head.o
obj-$(CONFIG_SUN3)	:= sun3-head.o

obj-y	+= entry.o irq.o module.o process.o ptrace.o
obj-y	+= setup.o signal.o sys_m68k.o syscalltable.o time.o traps.o

obj-$(CONFIG_MMU_MOTOROLA) += ints.o vectors.o
obj-$(CONFIG_MMU_SUN3) += ints.o vectors.o
obj-$(CONFIG_PCI) += pcibios.o

obj-$(CONFIG_M68K_NONCOHERENT_DMA) += dma.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_BOOTINFO_PROC)	+= bootinfo_proc.o
obj-$(CONFIG_UBOOT)		+= uboot.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

