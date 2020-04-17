# SPDX-License-Identifier: GPL-2.0
#
# Makefile for mmp specific clk
#

obj-y += clk-apbc.o clk-apmu.o clk-frac.o clk-mix.o clk-gate.o clk.o

obj-$(CONFIG_RESET_CONTROLLER) += reset.o

obj-$(CONFIG_MACH_MMP_DT) += clk-of-pxa168.o clk-of-pxa910.o
obj-$(CONFIG_COMMON_CLK_MMP2) += clk-of-mmp2.o clk-pll.o

obj-$(CONFIG_CPU_PXA168) += clk-pxa168.o
obj-$(CONFIG_CPU_PXA910) += clk-pxa910.o
obj-$(CONFIG_CPU_MMP2) += clk-mmp2.o

obj-y += clk-of-pxa1928.o
