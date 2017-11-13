# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ST_NCI NCI based NFC driver
#

st-nci-objs = ndlc.o core.o se.o vendor_cmds.o
obj-$(CONFIG_NFC_ST_NCI)     += st-nci.o

st-nci_i2c-objs = i2c.o
obj-$(CONFIG_NFC_ST_NCI_I2C) += st-nci_i2c.o

st-nci_spi-objs = spi.o
obj-$(CONFIG_NFC_ST_NCI_SPI) += st-nci_spi.o
