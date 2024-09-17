# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for ST21NFCA HCI based NFC driver
#

st21nfca_hci-objs = core.o dep.o se.o vendor_cmds.o
obj-$(CONFIG_NFC_ST21NFCA)     += st21nfca_hci.o

st21nfca_i2c-objs  = i2c.o
obj-$(CONFIG_NFC_ST21NFCA_I2C) += st21nfca_i2c.o
