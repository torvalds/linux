# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

always-$(KBUILD_BUILTIN)	+= vmlinux.lds

obj-$(CONFIG_MMU_MOTOROLA)	:= head.o
obj-$(CONFIG_SUN3)		:= sun3-head.o

obj-y	+= entry.o irq.o module.o process.o ptrace.o
obj-y	+= setup.o signal.o sys_m68k.o syscalltable.o time.o traps.o

obj-$(CONFIG_MMU_MOTOROLA) += ints.o vectors.o
obj-$(CONFIG_MMU_SUN3) += ints.o vectors.o
obj-$(CONFIG_PCI) += pcibios.o

obj-$(CONFIG_M68K_NONCOHERENT_DMA) += dma.o

obj-$(CONFIG_KEXEC_CORE)	+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_BOOTINFO_PROC)	+= bootinfo_proc.o
obj-$(CONFIG_UBOOT)		+= uboot.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

