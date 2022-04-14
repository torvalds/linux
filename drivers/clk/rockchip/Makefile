# SPDX-License-Identifier: GPL-2.0
#
# Rockchip Clock specific Makefile
#

obj-$(CONFIG_COMMON_CLK_ROCKCHIP) += clk-rockchip.o
obj-$(CONFIG_COMMON_CLK_ROCKCHIP_REGMAP) += regmap/

clk-rockchip-y += clk.o
clk-rockchip-y += clk-pll.o
clk-rockchip-y += clk-cpu.o
clk-rockchip-y += clk-half-divider.o
clk-rockchip-y += clk-mmc-phase.o
clk-rockchip-y += clk-muxgrf.o
clk-rockchip-$(CONFIG_ROCKCHIP_DDRCLK) += clk-ddr.o
clk-rockchip-$(CONFIG_ROCKCHIP_CLK_INV)  += clk-inverter.o
clk-rockchip-$(CONFIG_ROCKCHIP_CLK_PVTM) += clk-pvtm.o
clk-rockchip-$(CONFIG_RESET_CONTROLLER) += softrst.o

obj-$(CONFIG_ROCKCHIP_CLK_LINK)	+= clk-link.o

obj-$(CONFIG_CLK_PX30)          += clk-px30.o
obj-$(CONFIG_CLK_RV1106)        += clk-rv1106.o
obj-$(CONFIG_CLK_RV1108)        += clk-rv1108.o
obj-$(CONFIG_CLK_RV1126)	+= clk-rv1126.o
obj-$(CONFIG_CLK_RK1808)        += clk-rk1808.o
obj-$(CONFIG_CLK_RK3036)        += clk-rk3036.o
obj-$(CONFIG_CLK_RK312X)        += clk-rk3128.o
obj-$(CONFIG_CLK_RK3188)        += clk-rk3188.o
obj-$(CONFIG_CLK_RK322X)        += clk-rk3228.o
obj-$(CONFIG_CLK_RK3288)        += clk-rk3288.o
obj-$(CONFIG_CLK_RK3308)        += clk-rk3308.o
obj-$(CONFIG_CLK_RK3328)        += clk-rk3328.o
obj-$(CONFIG_CLK_RK3368)        += clk-rk3368.o
obj-$(CONFIG_CLK_RK3399)        += clk-rk3399.o
obj-$(CONFIG_CLK_RK3568)        += clk-rk3568.o
obj-$(CONFIG_CLK_RK3588)	+= clk-rk3588.o
