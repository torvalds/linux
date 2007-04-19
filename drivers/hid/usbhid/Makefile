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
ifeq ($(CONFIG_LOGITECH_FF),y)
	usbhid-objs	+= hid-lgff.o
endif
ifeq ($(CONFIG_PANTHERLORD_FF),y)
	usbhid-objs	+= hid-plff.o
endif
ifeq ($(CONFIG_THRUSTMASTER_FF),y)
	usbhid-objs	+= hid-tmff.o
endif
ifeq ($(CONFIG_ZEROPLUS_FF),y)
	usbhid-objs	+= hid-zpff.o
endif
ifeq ($(CONFIG_HID_FF),y)
	usbhid-objs	+= hid-ff.o
endif

obj-$(CONFIG_USB_HID)		+= usbhid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o

