# SPDX-License-Identifier: GPL-2.0
#
# Rockchip Clock specific Makefile
#

obj-y	+= clk.o
obj-y	+= clk-pll.o
obj-y	+= clk-cpu.o
obj-y	+= clk-half-divider.o
obj-y	+= clk-inverter.o
obj-y	+= clk-mmc-phase.o
obj-y	+= clk-muxgrf.o
obj-y	+= clk-ddr.o
obj-y	+= clk-pvtm.o
obj-y	+= clk-dclk-divider.o
obj-$(CONFIG_RESET_CONTROLLER)	+= softrst.o

obj-$(CONFIG_CPU_PX30)		+= clk-px30.o
obj-$(CONFIG_CPU_RV110X)	+= clk-rv1108.o
obj-$(CONFIG_CPU_RV1126)	+= clk-rv1126.o
obj-$(CONFIG_CPU_RK1808)	+= clk-rk1808.o
obj-$(CONFIG_CPU_RK3036)	+= clk-rk3036.o
obj-$(CONFIG_CPU_RK312X)	+= clk-rk3128.o
obj-$(CONFIG_CPU_RK3188)	+= clk-rk3188.o
obj-$(CONFIG_CPU_RK322X)	+= clk-rk3228.o
obj-$(CONFIG_CPU_RK3288)	+= clk-rk3288.o
obj-$(CONFIG_CPU_RK3308)	+= clk-rk3308.o
obj-$(CONFIG_CPU_RK3328)	+= clk-rk3328.o
obj-$(CONFIG_CPU_RK3368)	+= clk-rk3368.o
obj-$(CONFIG_CPU_RK3568)	+= clk-rk3568.o
obj-$(CONFIG_CPU_RK3399)	+= clk-rk3399.o

obj-$(CONFIG_COMMON_CLK_ROCKCHIP_REGMAP)	+= regmap/
