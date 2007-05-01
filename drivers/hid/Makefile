#
# Makefile for the HID driver
#
hid-objs			:= hid-core.o hid-input.o

obj-$(CONFIG_HID)		+= hid.o
hid-$(CONFIG_HID_DEBUG)		+= hid-debug.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

