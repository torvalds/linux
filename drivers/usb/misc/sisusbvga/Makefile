# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the sisusb driver (if driver is inside kernel tree).
#

obj-$(CONFIG_USB_SISUSBVGA) += sisusbvga.o

sisusbvga-y := sisusb.o
sisusbvga-$(CONFIG_USB_SISUSBVGA_CON) += sisusb_con.o sisusb_init.o
