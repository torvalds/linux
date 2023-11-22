# SPDX-License-Identifier: GPL-2.0
#
# Makefile for multifunction miscellaneous devices that just used for display
#

obj-$(CONFIG_SERDES_DISPLAY_CHIP_MAXIM)		+= maxim/
obj-$(CONFIG_SERDES_DISPLAY_CHIP_ROHM)		+= rohm/
obj-$(CONFIG_SERDES_DISPLAY_CHIP_ROCKCHIP)	+= rockchip/
obj-$(CONFIG_SERDES_DISPLAY_CHIP_NOVO)		+= novo/

serdes-mfd-display-$(CONFIG_MFD_SERDES_DISPLAY) += serdes-core.o serdes-irq.o

obj-$(CONFIG_MFD_SERDES_DISPLAY) += serdes-mfd-display.o serdes-i2c.o serdes-bridge.o serdes-bridge-split.o serdes-panel.o serdes-panel-split.o serdes-gpio.o serdes-pinctrl.o
