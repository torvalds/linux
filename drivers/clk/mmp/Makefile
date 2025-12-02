# SPDX-License-Identifier: GPL-2.0
#
# Makefile for mmp specific clk
#

obj-y += clk-apbc.o clk-apmu.o clk-frac.o clk-mix.o clk-gate.o clk.o

obj-$(CONFIG_RESET_CONTROLLER) += reset.o

obj-$(CONFIG_MACH_MMP_DT) += clk-of-pxa168.o clk-of-pxa910.o
obj-$(CONFIG_COMMON_CLK_MMP2) += clk-of-mmp2.o clk-pll.o pwr-island.o
obj-$(CONFIG_COMMON_CLK_MMP2_AUDIO) += clk-audio.o

obj-$(CONFIG_COMMON_CLK_PXA1908) += clk-pxa1908-apbc.o clk-pxa1908-apbcp.o \
	clk-pxa1908-mpmu.o clk-pxa1908-apmu.o

obj-$(CONFIG_ARCH_MMP) += clk-of-pxa1928.o
