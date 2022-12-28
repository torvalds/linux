# SPDX-License-Identifier: GPL-2.0

# This setup links the different object files into one single
# module so we don't have to EXPORT() a lot of internal symbols
# or create unnecessary submodules.
fotg210-objs-y				+= fotg210-core.o
fotg210-objs-$(CONFIG_USB_FOTG210_HCD)	+= fotg210-hcd.o
fotg210-objs-$(CONFIG_USB_FOTG210_UDC)	+= fotg210-udc.o
fotg210-objs				:= $(fotg210-objs-y)
obj-$(CONFIG_USB_FOTG210)		+= fotg210.o
