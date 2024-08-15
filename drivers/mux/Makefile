# SPDX-License-Identifier: GPL-2.0
#
# Makefile for multiplexer devices.
#

mux-core-objs			:= core.o
mux-adg792a-objs		:= adg792a.o
mux-adgs1408-objs		:= adgs1408.o
mux-gpio-objs			:= gpio.o
mux-mmio-objs			:= mmio.o

obj-$(CONFIG_MULTIPLEXER)	+= mux-core.o
obj-$(CONFIG_MUX_ADG792A)	+= mux-adg792a.o
obj-$(CONFIG_MUX_ADGS1408)	+= mux-adgs1408.o
obj-$(CONFIG_MUX_GPIO)		+= mux-gpio.o
obj-$(CONFIG_MUX_MMIO)		+= mux-mmio.o
