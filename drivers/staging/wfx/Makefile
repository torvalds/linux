# SPDX-License-Identifier: GPL-2.0

wfx-y := \
	hwio.o \
	main.o
wfx-$(CONFIG_SPI) += bus_spi.o
wfx-$(subst m,y,$(CONFIG_MMC)) += bus_sdio.o

obj-$(CONFIG_WFX) += wfx.o
