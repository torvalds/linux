#
# Makefile for the USB input drivers
#

# Multipart objects.
usbhid-objs	:= hid-core.o hid-quirks.o

# Optional parts of multipart objects.

ifeq ($(CONFIG_USB_HIDDEV),y)
	usbhid-objs	+= hiddev.o
endif
ifeq ($(CONFIG_HID_PID),y)
	usbhid-objs	+= hid-pidff.o
endif

obj-$(CONFIG_USB_HID)		+= usbhid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o

