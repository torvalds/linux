# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Qualcomm network device drivers.
#

obj-$(CONFIG_QCA7000) += qca_7k_common.o
obj-$(CONFIG_QCA7000_SPI) += qcaspi.o
qcaspi-objs := qca_7k.o qca_debug.o qca_spi.o
obj-$(CONFIG_QCA7000_UART) += qcauart.o
qcauart-objs := qca_uart.o

obj-y += emac/

obj-$(CONFIG_RMNET) += rmnet/
