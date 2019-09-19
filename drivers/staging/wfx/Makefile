# SPDX-License-Identifier: GPL-2.0

# Necessary for CREATE_TRACE_POINTS
CFLAGS_debug.o = -I$(src)

wfx-y := \
	hwio.o \
	main.o \
	debug.o
wfx-$(CONFIG_SPI) += bus_spi.o
wfx-$(subst m,y,$(CONFIG_MMC)) += bus_sdio.o

obj-$(CONFIG_WFX) += wfx.o
