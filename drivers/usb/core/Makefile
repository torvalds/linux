# SPDX-License-Identifier: GPL-2.0
#
# Makefile for USB Core files and filesystem
#

usbcore-y := usb.o hub.o hcd.o urb.o message.o driver.o
usbcore-y += config.o file.o buffer.o sysfs.o endpoint.o
usbcore-y += devio.o notify.o generic.o quirks.o devices.o
usbcore-y += phy.o port.o

usbcore-$(CONFIG_OF)		+= of.o
usbcore-$(CONFIG_USB_PCI)		+= hcd-pci.o
usbcore-$(CONFIG_ACPI)		+= usb-acpi.o

ifdef CONFIG_USB_ONBOARD_DEV
usbcore-y			+= ../misc/onboard_usb_dev_pdevs.o
endif

obj-$(CONFIG_USB)		+= usbcore.o

obj-$(CONFIG_USB_LEDS_TRIGGER_USBPORT)	+= ledtrig-usbport.o
