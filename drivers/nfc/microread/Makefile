# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Microread HCI based NFC driver
#

microread_i2c-objs  = i2c.o
microread_mei-objs  = mei.o

obj-$(CONFIG_NFC_MICROREAD)     += microread.o
obj-$(CONFIG_NFC_MICROREAD_I2C) += microread_i2c.o
obj-$(CONFIG_NFC_MICROREAD_MEI) += microread_mei.o
