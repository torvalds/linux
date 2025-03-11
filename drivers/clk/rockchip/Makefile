# SPDX-License-Identifier: GPL-2.0
#
# Rockchip Clock specific Makefile
#

obj-$(CONFIG_COMMON_CLK_ROCKCHIP) += clk-rockchip.o

clk-rockchip-y += clk.o
clk-rockchip-y += clk-pll.o
clk-rockchip-y += clk-cpu.o
clk-rockchip-y += clk-half-divider.o
clk-rockchip-y += clk-inverter.o
clk-rockchip-y += clk-mmc-phase.o
clk-rockchip-y += clk-muxgrf.o
clk-rockchip-y += clk-ddr.o
clk-rockchip-y += gate-link.o
clk-rockchip-$(CONFIG_RESET_CONTROLLER) += softrst.o

obj-$(CONFIG_CLK_PX30)          += clk-px30.o
obj-$(CONFIG_CLK_RV110X)        += clk-rv1108.o
obj-$(CONFIG_CLK_RV1126)        += clk-rv1126.o
obj-$(CONFIG_CLK_RK3036)        += clk-rk3036.o
obj-$(CONFIG_CLK_RK312X)        += clk-rk3128.o
obj-$(CONFIG_CLK_RK3188)        += clk-rk3188.o
obj-$(CONFIG_CLK_RK322X)        += clk-rk3228.o
obj-$(CONFIG_CLK_RK3288)        += clk-rk3288.o
obj-$(CONFIG_CLK_RK3308)        += clk-rk3308.o
obj-$(CONFIG_CLK_RK3328)        += clk-rk3328.o
obj-$(CONFIG_CLK_RK3368)        += clk-rk3368.o
obj-$(CONFIG_CLK_RK3399)        += clk-rk3399.o
obj-$(CONFIG_CLK_RK3528)	+= clk-rk3528.o rst-rk3528.o
obj-$(CONFIG_CLK_RK3562)	+= clk-rk3562.o rst-rk3562.o
obj-$(CONFIG_CLK_RK3568)	+= clk-rk3568.o
obj-$(CONFIG_CLK_RK3576)	+= clk-rk3576.o rst-rk3576.o
obj-$(CONFIG_CLK_RK3588)	+= clk-rk3588.o rst-rk3588.o
