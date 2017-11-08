# SPDX-License-Identifier: GPL-2.0
#
# Makefile for metering ic drivers
#

obj-$(CONFIG_ADE7753) += ade7753.o
obj-$(CONFIG_ADE7754) += ade7754.o

ade7758-y             := ade7758_core.o
ade7758-$(CONFIG_IIO_BUFFER) += ade7758_ring.o ade7758_trigger.o
obj-$(CONFIG_ADE7758) += ade7758.o

obj-$(CONFIG_ADE7759) += ade7759.o
obj-$(CONFIG_ADE7854) += ade7854.o
obj-$(CONFIG_ADE7854_I2C) += ade7854-i2c.o
obj-$(CONFIG_ADE7854_SPI) += ade7854-spi.o
