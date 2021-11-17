# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the phy drivers.
#

obj-$(CONFIG_PHY_MTK_TPHY)		+= phy-mtk-tphy.o
obj-$(CONFIG_PHY_MTK_UFS)		+= phy-mtk-ufs.o
obj-$(CONFIG_PHY_MTK_XSPHY)		+= phy-mtk-xsphy.o

phy-mtk-hdmi-drv-y			:= phy-mtk-hdmi.o
phy-mtk-hdmi-drv-y			+= phy-mtk-hdmi-mt2701.o
phy-mtk-hdmi-drv-y			+= phy-mtk-hdmi-mt8173.o
obj-$(CONFIG_PHY_MTK_HDMI)		+= phy-mtk-hdmi-drv.o

phy-mtk-mipi-dsi-drv-y			:= phy-mtk-mipi-dsi.o
phy-mtk-mipi-dsi-drv-y			+= phy-mtk-mipi-dsi-mt8173.o
phy-mtk-mipi-dsi-drv-y			+= phy-mtk-mipi-dsi-mt8183.o
obj-$(CONFIG_PHY_MTK_MIPI_DSI)		+= phy-mtk-mipi-dsi-drv.o
