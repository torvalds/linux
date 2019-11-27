# SPDX-License-Identifier: GPL-2.0

# Necessary for CREATE_TRACE_POINTS
CFLAGS_debug.o = -I$(src)

wfx-y := \
	bh.o \
	hwio.o \
	fwio.o \
	hif_tx.o \
	hif_rx.o \
	queue.o \
	data_tx.o \
	data_rx.o \
	scan.o \
	sta.o \
	key.o \
	main.o \
	sta.o \
	debug.o
wfx-$(CONFIG_SPI) += bus_spi.o
wfx-$(subst m,y,$(CONFIG_MMC)) += bus_sdio.o

obj-$(CONFIG_WFX) += wfx.o
