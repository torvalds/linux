# SPDX-License-Identifier: GPL-2.0-only
# Makefile for the Ralink common stuff
#
# Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
# Copyright (C) 2013 John Crispin <john@phrozen.org>

obj-y := prom.o of.o reset.o

ifndef CONFIG_MIPS_GIC
	obj-y += clk.o timer.o
endif

obj-$(CONFIG_RALINK_ILL_ACC) += ill_acc.o

obj-$(CONFIG_IRQ_INTC) += irq.o
obj-$(CONFIG_MIPS_GIC) += irq-gic.o timer-gic.o

obj-$(CONFIG_SOC_RT288X) += rt288x.o
obj-$(CONFIG_SOC_RT305X) += rt305x.o
obj-$(CONFIG_SOC_RT3883) += rt3883.o
obj-$(CONFIG_SOC_MT7620) += mt7620.o
obj-$(CONFIG_SOC_MT7621) += mt7621.o

obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

obj-$(CONFIG_DEBUG_FS) += bootrom.o
