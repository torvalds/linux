# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the IP30 specific kernel interface routines under Linux.
#

obj-y	:= ip30-irq.o ip30-power.o ip30-setup.o ip30-timer.o ip30-xtalk.o

obj-$(CONFIG_EARLY_PRINTK)	+= ip30-console.o
obj-$(CONFIG_SMP)		+= ip30-smp.o
