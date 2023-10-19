# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Samsung S3FWRN5 NFC driver
#

s3fwrn5-objs = core.o firmware.o nci.o phy_common.o
s3fwrn5_i2c-objs = i2c.o
s3fwrn82_uart-objs = uart.o

obj-$(CONFIG_NFC_S3FWRN5) += s3fwrn5.o
obj-$(CONFIG_NFC_S3FWRN5_I2C) += s3fwrn5_i2c.o
obj-$(CONFIG_NFC_S3FWRN82_UART) += s3fwrn82_uart.o
