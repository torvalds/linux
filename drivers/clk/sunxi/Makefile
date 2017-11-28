# SPDX-License-Identifier: GPL-2.0
#
# Makefile for sunxi specific clk
#

obj-y += clk-sunxi.o clk-factors.o
obj-y += clk-a10-codec.o
obj-y += clk-a10-hosc.o
obj-y += clk-a10-mod1.o
obj-y += clk-a10-pll2.o
obj-y += clk-a10-ve.o
obj-y += clk-a20-gmac.o
obj-y += clk-mod0.o
obj-y += clk-simple-gates.o
obj-y += clk-sun4i-display.o
obj-y += clk-sun4i-pll3.o
obj-y += clk-sun4i-tcon-ch1.o
obj-y += clk-sun8i-bus-gates.o
obj-y += clk-sun8i-mbus.o
obj-y += clk-sun9i-core.o
obj-y += clk-sun9i-mmc.o
obj-y += clk-usb.o

obj-$(CONFIG_MACH_SUN9I) += clk-sun8i-apb0.o
obj-$(CONFIG_MACH_SUN9I) += clk-sun9i-cpus.o

obj-$(CONFIG_MFD_SUN6I_PRCM) += \
	clk-sun6i-ar100.o clk-sun6i-apb0.o clk-sun6i-apb0-gates.o \
	clk-sun8i-apb0.o
