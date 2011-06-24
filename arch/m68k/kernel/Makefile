#
# Makefile for the linux kernel.
#

extra-$(CONFIG_MMU)	:= head.o
extra-$(CONFIG_SUN3)	:= sun3-head.o
extra-y			+= vmlinux.lds

obj-y	:= entry.o m68k_ksyms.o module.o process.o ptrace.o setup.o signal.o \
	   sys_m68k.o syscalltable.o time.o traps.o

obj-$(CONFIG_MMU)	+= ints.o devres.o vectors.o
devres-$(CONFIG_MMU)	= ../../../kernel/irq/devres.o

ifndef CONFIG_MMU_SUN3
obj-y			+= dma.o
endif
ifndef CONFIG_MMU
obj-y			+= init_task.o irq.o
endif

