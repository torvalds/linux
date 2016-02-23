#
# Makefile for the linux kernel.
#

extra-$(CONFIG_AMIGA)	:= head.o
extra-$(CONFIG_ATARI)	:= head.o
extra-$(CONFIG_MAC)	:= head.o
extra-$(CONFIG_APOLLO)	:= head.o
extra-$(CONFIG_VME)	:= head.o
extra-$(CONFIG_HP300)	:= head.o
extra-$(CONFIG_Q40)	:= head.o
extra-$(CONFIG_SUN3X)	:= head.o
extra-$(CONFIG_SUN3)	:= sun3-head.o
extra-y			+= vmlinux.lds

obj-y	:= entry.o irq.o m68k_ksyms.o module.o process.o ptrace.o
obj-y	+= setup.o signal.o sys_m68k.o syscalltable.o time.o traps.o

obj-$(CONFIG_MMU_MOTOROLA) += ints.o vectors.o
obj-$(CONFIG_MMU_SUN3) += ints.o vectors.o
obj-$(CONFIG_PCI) += pcibios.o

obj-$(CONFIG_HAS_DMA)	+= dma.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_BOOTINFO_PROC)	+= bootinfo_proc.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

