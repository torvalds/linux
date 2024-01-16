# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for PN533 NFC driver
#
pn533_usb-objs  = usb.o
pn533_i2c-objs  = i2c.o
pn532_uart-objs  = uart.o

obj-$(CONFIG_NFC_PN533)     += pn533.o
obj-$(CONFIG_NFC_PN533_USB) += pn533_usb.o
obj-$(CONFIG_NFC_PN533_I2C) += pn533_i2c.o
obj-$(CONFIG_NFC_PN532_UART) += pn532_uart.o
