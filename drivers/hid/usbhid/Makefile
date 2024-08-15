# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the USB input drivers
#

usbhid-y	:= hid-core.o
usbhid-$(CONFIG_USB_HIDDEV)	+= hiddev.o
usbhid-$(CONFIG_HID_PID)	+= hid-pidff.o

obj-$(CONFIG_USB_HID)		+= usbhid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o

