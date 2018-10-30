# SPDX-License-Identifier: GPL-2.0
ccflags-$(CONFIG_CAIF_DEBUG) := -DDEBUG

# Serial interface
obj-$(CONFIG_CAIF_TTY) += caif_serial.o

# SPI slave physical interfaces module
cfspi_slave-objs := caif_spi.o caif_spi_slave.o
obj-$(CONFIG_CAIF_SPI_SLAVE) += cfspi_slave.o

# HSI interface
obj-$(CONFIG_CAIF_HSI) += caif_hsi.o

# Virtio interface
obj-$(CONFIG_CAIF_VIRTIO) += caif_virtio.o
