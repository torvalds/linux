# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.#
# Makefile for the Ralink common stuff
#
# Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
# Copyright (C) 2013 John Crispin <blogic@openwrt.org>

obj-y := prom.o of.o reset.o clk.o irq.o

obj-$(CONFIG_SOC_RT288X) += rt288x.o
obj-$(CONFIG_SOC_RT305X) += rt305x.o

obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

obj-y += dts/
