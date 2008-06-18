#
# Makefile for the HID driver
#
hid-objs			:= hid-core.o hid-input.o hid-input-quirks.o

obj-$(CONFIG_HID)		+= hid.o

hid-$(CONFIG_HID_DEBUG)		+= hid-debug.o
hid-$(CONFIG_HIDRAW)		+= hidraw.o

obj-$(CONFIG_HID_APPLE)		+= hid-apple.o
obj-$(CONFIG_HID_LOGITECH)	+= hid-logitech.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

