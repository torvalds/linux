# SPDX-License-Identifier: GPL-2.0
#
# Makefile for NFCMRVL NCI based NFC driver
#

nfcmrvl-y += main.o fw_dnld.o
obj-$(CONFIG_NFC_MRVL) += nfcmrvl.o

nfcmrvl_usb-y += usb.o
obj-$(CONFIG_NFC_MRVL_USB) += nfcmrvl_usb.o

nfcmrvl_uart-y += uart.o
obj-$(CONFIG_NFC_MRVL_UART) += nfcmrvl_uart.o

nfcmrvl_i2c-y += i2c.o
obj-$(CONFIG_NFC_MRVL_I2C) += nfcmrvl_i2c.o

nfcmrvl_spi-y += spi.o
obj-$(CONFIG_NFC_MRVL_SPI) += nfcmrvl_spi.o
