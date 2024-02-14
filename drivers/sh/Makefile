# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the SuperH specific drivers.
#
obj-$(CONFIG_SH_INTC)			+= intc/
ifneq ($(CONFIG_COMMON_CLK),y)
obj-$(CONFIG_HAVE_CLK)			+= clk/
endif
obj-$(CONFIG_MAPLE)			+= maple/

obj-y					+= pm_runtime.o
